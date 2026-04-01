#include "vk_video_output.h"

#include "mini_mpv_render.h"
#include "mpv_core.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace godot;

namespace libmpv_zero {

void VkVideoOutput::_on_render_update(void *p_context) {
	VkVideoOutput *self = static_cast<VkVideoOutput *>(p_context);
	if (self) {
		self->update_callback_count.fetch_add(1, std::memory_order_acq_rel);
		self->frame_dirty.store(true, std::memory_order_relaxed);
	}
}

void VkVideoOutput::_render_frame_on_render_thread_static(uint64_t p_self) {
	VkVideoOutput *self = reinterpret_cast<VkVideoOutput *>(p_self);
	if (self) {
		self->_render_frame_on_render_thread();
	}
}

void VkVideoOutput::_render_frame_on_render_thread() {
	if (!render_thread_logged) {
		UtilityFunctions::print("VkVideoOutput render-thread callback entered");
		render_thread_logged = true;
	}

	if (!render_context || !external_texture.success) {
		last_render_result.store(0, std::memory_order_release);
		render_request_in_flight.store(false, std::memory_order_release);
		return;
	}

	mpv_vulkan_image image = {};
	image.image = reinterpret_cast<VkImage>(external_texture.image_handle);
	image.width = static_cast<int>(external_texture.width);
	image.height = static_cast<int>(external_texture.height);
	image.depth = 0;
	image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	image.format = VK_FORMAT_R8G8B8A8_UNORM;
	image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image.layout = image_layout;
	image.queue_family = external_texture.queue_family_index;

	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_VULKAN_IMAGE, &image },
		{ MPV_RENDER_PARAM_INVALID, nullptr },
	};

	const int render_result = dispatch.render(render_context, render_params);
	if (!render_thread_logged || render_result < 0) {
		UtilityFunctions::print(vformat("VkVideoOutput render-thread result: %d", render_result));
	}
	if (render_result >= 0) {
		image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		successful_render_count.fetch_add(1, std::memory_order_acq_rel);
	}
	last_render_result.store(render_result, std::memory_order_release);
	render_request_in_flight.store(false, std::memory_order_release);
}

bool VkVideoOutput::_load_render_dispatch() {
#ifdef _WIN32
	if (dispatch.library && dispatch.vulkan_library) {
		return true;
	}

	dispatch.library = LoadLibraryW(L"libmpv-2.dll");
	dispatch.vulkan_library = LoadLibraryW(L"vulkan-1.dll");
	if (!dispatch.library || !dispatch.vulkan_library) {
		status = "failed to load Vulkan libmpv runtime";
		_unload_render_dispatch();
		return false;
	}

	dispatch.create = reinterpret_cast<PFN_mpv_render_context_create>(GetProcAddress(static_cast<HMODULE>(dispatch.library), "mpv_render_context_create"));
	dispatch.set_update_callback = reinterpret_cast<PFN_mpv_render_context_set_update_callback>(GetProcAddress(static_cast<HMODULE>(dispatch.library), "mpv_render_context_set_update_callback"));
	dispatch.update = reinterpret_cast<PFN_mpv_render_context_update>(GetProcAddress(static_cast<HMODULE>(dispatch.library), "mpv_render_context_update"));
	dispatch.render = reinterpret_cast<PFN_mpv_render_context_render>(GetProcAddress(static_cast<HMODULE>(dispatch.library), "mpv_render_context_render"));
	dispatch.free = reinterpret_cast<PFN_mpv_render_context_free>(GetProcAddress(static_cast<HMODULE>(dispatch.library), "mpv_render_context_free"));
	dispatch.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(dispatch.vulkan_library), "vkGetInstanceProcAddr"));

	if (!dispatch.create || !dispatch.set_update_callback || !dispatch.update || !dispatch.render || !dispatch.free || !dispatch.get_instance_proc_addr) {
		status = "failed to resolve Vulkan libmpv symbols";
		_unload_render_dispatch();
		return false;
	}

	return true;
#else
	status = "vulkan video backend currently only supports Windows";
	return false;
#endif
}

void VkVideoOutput::_unload_render_dispatch() {
#ifdef _WIN32
	if (dispatch.library) {
		FreeLibrary(static_cast<HMODULE>(dispatch.library));
	}
	if (dispatch.vulkan_library) {
		FreeLibrary(static_cast<HMODULE>(dispatch.vulkan_library));
	}
#endif
	dispatch = RenderDispatch();
}

bool VkVideoOutput::_ensure_external_texture(int p_width, int p_height) {
	if (!render_thread_service || p_width <= 0 || p_height <= 0) {
		return false;
	}
	if (external_texture.success && static_cast<int>(external_texture.width) == p_width && static_cast<int>(external_texture.height) == p_height) {
		return true;
	}
	if (texture_request_in_flight && requested_width == p_width && requested_height == p_height) {
		return false;
	}
	if (render_thread_service->has_pending_work()) {
		return false;
	}

	requested_width = p_width;
	requested_height = p_height;
	texture_request_in_flight = render_thread_service->request_external_texture(p_width, p_height, Color(0.0, 0.0, 0.0, 1.0), false);
	if (!texture_request_in_flight) {
		status = "failed to queue Vulkan texture request";
		return false;
	}

	status = vformat("requesting Vulkan texture %dx%d", p_width, p_height);
	if (!texture_request_logged) {
		UtilityFunctions::print(vformat("VkVideoOutput requested texture: %dx%d", p_width, p_height));
		texture_request_logged = true;
	}
	return false;
}

bool VkVideoOutput::_ensure_render_context() {
	if (render_context) {
		return true;
	}
	if (!external_texture.success || !mpv_core || !mpv_core->is_initialized()) {
		return false;
	}
	if (!_load_render_dispatch()) {
		return false;
	}

	mpv_vulkan_init_params init_params = {};
	init_params.instance = reinterpret_cast<VkInstance>(external_texture.instance_handle);
	init_params.get_proc_addr = dispatch.get_instance_proc_addr;
	init_params.phys_device = reinterpret_cast<VkPhysicalDevice>(external_texture.physical_device_handle);
	init_params.device = reinterpret_cast<VkDevice>(external_texture.logical_device);
	init_params.queue_graphics.index = external_texture.queue_family_index;
	init_params.queue_graphics.count = 1;

	mpv_render_param params[] = {
		{ MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_VULKAN) },
		{ MPV_RENDER_PARAM_VULKAN_INIT_PARAMS, &init_params },
		{ MPV_RENDER_PARAM_INVALID, nullptr },
	};

	const int create_result = dispatch.create(&render_context, static_cast<mpv_handle *>(mpv_core->get_native_handle()), params);
	if (create_result < 0 || !render_context) {
		status = vformat("mpv Vulkan render context create failed (%d)", create_result);
		render_context = nullptr;
		return false;
	}

	dispatch.set_update_callback(render_context, &VkVideoOutput::_on_render_update, this);
	status = "vulkan render context ready";
	if (!render_context_logged) {
		UtilityFunctions::print("VkVideoOutput render context ready");
		render_context_logged = true;
	}
	return true;
}

void VkVideoOutput::_handle_texture_ready() {
	if (texture.is_null()) {
		texture.instantiate();
	}
	texture->set_texture_rd_rid(external_texture.wrapped_texture);
	status = "vulkan external texture ready";
	image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	texture_published = true;
	if (texture_ready_callback.is_valid()) {
		texture_ready_callback.call(texture);
	}
}

void VkVideoOutput::_release_external_texture(const RenderThreadService::ExternalTextureHandle &p_texture) {
	if (!render_thread_service) {
		return;
	}

	render_thread_service->release_external_texture(p_texture);
}

void VkVideoOutput::attach(Node *p_owner, MpvCore *p_mpv_core, const Callable &p_texture_ready, const Callable &p_probe_failed) {
	(void)p_owner;
	if (render_thread_service) {
		return;
	}

	mpv_core = p_mpv_core;
	render_thread_service = memnew(RenderThreadService);
	texture_ready_callback = p_texture_ready;
	probe_failed_callback = p_probe_failed;
	requested_width = 0;
	requested_height = 0;
	texture_request_in_flight = false;
	status = "vulkan backend waiting for video size";
	_ensure_external_texture(1, 1);
	if (!attach_logged) {
		UtilityFunctions::print("VkVideoOutput attached");
		attach_logged = true;
	}
}

void VkVideoOutput::update() {
	if (!render_thread_service) {
		return;
	}

	RenderThreadService::ExternalTextureHandle result;
	if (render_thread_service->poll_external_texture_result(result)) {
		texture_request_in_flight = false;
		status = result.status;
	if (!result.success) {
			if (probe_failed_callback.is_valid()) {
				probe_failed_callback.call(status);
			}
			return;
		}

		if (!texture_ready_logged) {
			UtilityFunctions::print(vformat("VkVideoOutput texture ready: %dx%d", result.width, result.height));
			texture_ready_logged = true;
		}

		if (external_texture.success && external_texture.wrapped_texture != result.wrapped_texture) {
			_release_external_texture(external_texture);
		}
		external_texture = result;
		image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	if (!mpv_core) {
		return;
	}

	if (!_ensure_render_context()) {
		return;
	}

	if (!mpv_core->has_loaded_file()) {
		return;
	}

	const int video_width = mpv_core->get_video_width();
	const int video_height = mpv_core->get_video_height();
	if (!_ensure_external_texture(video_width, video_height)) {
		return;
	}

	const bool forced_dirty = frame_dirty.exchange(false, std::memory_order_relaxed);
	uint64_t update_flags = dispatch.update(render_context);
	if ((update_flags & MPV_RENDER_UPDATE_FRAME) == 0 && forced_dirty && update_callback_count.load(std::memory_order_acquire) > 0) {
		update_flags |= MPV_RENDER_UPDATE_FRAME;
	}
	if (!update_flags_logged) {
		UtilityFunctions::print(vformat(
				"VkVideoOutput update flags: %d forced_dirty=%d update_callbacks=%d",
				static_cast<int>(update_flags),
				forced_dirty ? 1 : 0,
				update_callback_count.load(std::memory_order_acquire)));
		update_flags_logged = true;
	}
	if ((update_flags & MPV_RENDER_UPDATE_FRAME) == 0) {
		if (!update_callback_logged && update_callback_count.load(std::memory_order_acquire) > 0) {
			UtilityFunctions::print(vformat(
					"VkVideoOutput waiting for frame-ready flag after %d update callbacks",
					update_callback_count.load(std::memory_order_acquire)));
			update_callback_logged = true;
		}
		return;
	}

	const int previous_render_result = last_render_result.load(std::memory_order_acquire);
	if (previous_render_result < 0) {
		last_render_result.store(0, std::memory_order_release);
		status = vformat("mpv Vulkan render failed (%d)", previous_render_result);
		if (probe_failed_callback.is_valid()) {
			probe_failed_callback.call(status);
		}
		return;
	}

	if (!texture_published && successful_render_count.load(std::memory_order_acquire) >= 1) {
		UtilityFunctions::print(vformat(
				"VkVideoOutput publishing completed frame after %d update callbacks",
				update_callback_count.load(std::memory_order_acquire)));
		_handle_texture_ready();
	}

	if (render_request_in_flight.exchange(true, std::memory_order_acq_rel)) {
		return;
	}

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		render_request_in_flight.store(false, std::memory_order_release);
		status = "rendering server unavailable";
		return;
	}

	rendering_server->call_on_render_thread(callable_mp_static(&VkVideoOutput::_render_frame_on_render_thread_static).bind(reinterpret_cast<uint64_t>(this)));
	if (!render_queue_logged) {
		UtilityFunctions::print("VkVideoOutput queued render-thread frame");
		render_queue_logged = true;
	}
	status = vformat("queued Vulkan frame %dx%d", external_texture.width, external_texture.height);
}

void VkVideoOutput::detach() {
	if (render_context && dispatch.free) {
		dispatch.free(render_context);
		render_context = nullptr;
	}
	_unload_render_dispatch();

	if (!render_thread_service) {
		mpv_core = nullptr;
		return;
	}

	if (texture.is_valid()) {
		texture->set_texture_rd_rid(RID());
		texture.unref();
	}
	_release_external_texture(external_texture);
	if (render_thread_service->has_pending_work()) {
		render_thread_service = nullptr;
	} else {
		memdelete(render_thread_service);
		render_thread_service = nullptr;
	}
	external_texture = RenderThreadService::ExternalTextureHandle();
	mpv_core = nullptr;
	requested_width = 0;
	requested_height = 0;
	texture_request_in_flight = false;
	render_request_in_flight.store(false, std::memory_order_release);
	last_render_result.store(0, std::memory_order_release);
	successful_render_count.store(0, std::memory_order_release);
	readback_logged = false;
	render_queue_logged = false;
	render_thread_logged = false;
	update_callback_count.store(0, std::memory_order_release);
	update_flags_logged = false;
	forced_render_logged = false;
	update_callback_logged = false;
	texture_published = false;
	status = "vulkan video backend detached";
}

Ref<Texture2D> VkVideoOutput::get_texture() const {
	return texture;
}

bool VkVideoOutput::is_ready_for_playback() const {
	return render_context != nullptr;
}

String VkVideoOutput::get_status() const {
	if (render_thread_service && render_thread_service->has_pending_work()) {
		return render_thread_service->get_status();
	}

	return status;
}

} // namespace libmpv_zero
