#include "vk_video_output.h"

#include "mini_mpv_render.h"
#include "mpv_core.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
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
	if (!render_context || render_slot_index < 0 || render_slot_index >= static_cast<int>(slots.size())) {
		last_render_result.store(0, std::memory_order_release);
		render_request_in_flight.store(false, std::memory_order_release);
		return;
	}

	TextureSlot &slot = slots[render_slot_index];
	if (!slot.handle.success) {
		last_render_result.store(0, std::memory_order_release);
		render_request_in_flight.store(false, std::memory_order_release);
		return;
	}

	mpv_vulkan_image image = {};
	int block_for_target_time = 0;
	int skip_rendering = mpv_core && mpv_core->is_seeking() ? 1 : 0;
	image.image = reinterpret_cast<VkImage>(slot.handle.image_handle);
	image.width = static_cast<int>(slot.handle.width);
	image.height = static_cast<int>(slot.handle.height);
	image.depth = 0;
	image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	image.format = VK_FORMAT_R8G8B8A8_UNORM;
	image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	image.layout = slot.image_layout;
	image.queue_family = slot.handle.queue_family_index;

	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block_for_target_time },
		{ MPV_RENDER_PARAM_SKIP_RENDERING, &skip_rendering },
		{ MPV_RENDER_PARAM_VULKAN_IMAGE, &image },
		{ MPV_RENDER_PARAM_INVALID, nullptr },
	};

	const int render_result = dispatch.render(render_context, render_params);
	if (render_result >= 0) {
		slot.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		slot.published = false;
		last_rendered_slot.store(render_slot_index, std::memory_order_release);
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

bool VkVideoOutput::_slot_matches_size(const TextureSlot &p_slot, int p_width, int p_height) const {
	return p_slot.handle.success && static_cast<int>(p_slot.handle.width) == p_width && static_cast<int>(p_slot.handle.height) == p_height;
}

bool VkVideoOutput::_ensure_external_textures(int p_width, int p_height, int p_target_count) {
	if (!render_thread_service || p_width <= 0 || p_height <= 0) {
		return false;
	}

	int matching_slots = 0;
	for (TextureSlot &slot : slots) {
		if (_slot_matches_size(slot, p_width, p_height)) {
			matching_slots += 1;
			continue;
		}
		if (slot.handle.success) {
			_retire_slot(slot, 120);
		}
	}

	if (matching_slots >= p_target_count) {
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
	texture_request_logged = true;
	return false;
}

bool VkVideoOutput::_ensure_render_context() {
	if (render_context) {
		return true;
	}
	const TextureSlot *init_slot = nullptr;
	for (const TextureSlot &slot : slots) {
		if (slot.handle.success) {
			init_slot = &slot;
			break;
		}
	}
	if (!init_slot || !mpv_core || !mpv_core->is_initialized()) {
		return false;
	}
	if (!_load_render_dispatch()) {
		return false;
	}

	mpv_vulkan_init_params init_params = {};
	init_params.instance = reinterpret_cast<VkInstance>(init_slot->handle.instance_handle);
	init_params.get_proc_addr = dispatch.get_instance_proc_addr;
	init_params.phys_device = reinterpret_cast<VkPhysicalDevice>(init_slot->handle.physical_device_handle);
	init_params.device = reinterpret_cast<VkDevice>(init_slot->handle.logical_device);
	init_params.queue_graphics.index = init_slot->handle.queue_family_index;
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
	render_context_logged = true;
	return true;
}

void VkVideoOutput::_publish_slot(int p_slot_index) {
	if (p_slot_index < 0 || p_slot_index >= static_cast<int>(slots.size())) {
		return;
	}

	TextureSlot &slot = slots[p_slot_index];
	if (!slot.handle.success) {
		return;
	}

	if (texture.is_null()) {
		texture.instantiate();
	}
	texture->set_texture_rd_rid(slot.handle.wrapped_texture);
	status = "vulkan external texture ready";
	published_slot_index = p_slot_index;
	slot.published = true;
	if (texture_ready_callback.is_valid()) {
		texture_ready_callback.call(texture);
	}
}

void VkVideoOutput::_retire_slot(TextureSlot &p_slot, int p_delay_updates) {
	if (!p_slot.handle.success) {
		return;
	}

	if (published_slot_index >= 0 && slots[published_slot_index].handle.wrapped_texture == p_slot.handle.wrapped_texture) {
		published_slot_index = -1;
	}

	retired_textures.push_back({ p_slot.handle, p_delay_updates });
	p_slot = TextureSlot();
}

void VkVideoOutput::_poll_retired_textures() {
	for (RetiredTexture &retired : retired_textures) {
		if (retired.release_after_updates > 0) {
			retired.release_after_updates -= 1;
		}
	}

	if (!render_thread_service || render_thread_service->has_pending_work()) {
		return;
	}

	for (size_t i = 0; i < retired_textures.size(); ++i) {
		if (retired_textures[i].release_after_updates > 0) {
			continue;
		}
		_release_external_texture(retired_textures[i].handle);
		retired_textures.erase(retired_textures.begin() + static_cast<int64_t>(i));
		break;
	}
}

int VkVideoOutput::_find_next_render_slot() const {
	for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
		if (!slots[i].handle.success) {
			continue;
		}
		if (static_cast<int>(slots.size()) > 1 && i == published_slot_index) {
			continue;
		}
		return i;
	}

	for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
		if (slots[i].handle.success) {
			return i;
		}
	}

	return -1;
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
	_ensure_external_textures(1, 1, 1);
	attach_logged = true;
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

		texture_ready_logged = true;

		bool stored = false;
		for (TextureSlot &slot : slots) {
			if (!slot.handle.success) {
				slot.handle = result;
				slot.image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
				slot.published = false;
				stored = true;
				break;
			}
		}
		if (!stored) {
			retired_textures.push_back({ result, 120 });
		}
	}
	_poll_retired_textures();

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
	if (!_ensure_external_textures(video_width, video_height, 2)) {
		return;
	}

	const int finished_slot = last_rendered_slot.exchange(-1, std::memory_order_acq_rel);
	if (finished_slot >= 0 && finished_slot != published_slot_index) {
		_publish_slot(finished_slot);
	}

	const bool forced_dirty = frame_dirty.exchange(false, std::memory_order_relaxed);
	if (!forced_dirty) {
		return;
	}

	uint64_t update_flags = dispatch.update(render_context);
	if ((update_flags & MPV_RENDER_UPDATE_FRAME) == 0 && update_callback_count.load(std::memory_order_acquire) > 0) {
		update_flags |= MPV_RENDER_UPDATE_FRAME;
	}
	if ((update_flags & MPV_RENDER_UPDATE_FRAME) == 0) {
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

	if (render_request_in_flight.exchange(true, std::memory_order_acq_rel)) {
		return;
	}

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		render_request_in_flight.store(false, std::memory_order_release);
		status = "rendering server unavailable";
		return;
	}

	render_slot_index = _find_next_render_slot();
	if (render_slot_index < 0) {
		render_request_in_flight.store(false, std::memory_order_release);
		status = "no Vulkan render slot available";
		return;
	}

	rendering_server->call_on_render_thread(callable_mp_static(&VkVideoOutput::_render_frame_on_render_thread_static).bind(reinterpret_cast<uint64_t>(this)));
	status = vformat("queued Vulkan frame %dx%d", slots[render_slot_index].handle.width, slots[render_slot_index].handle.height);
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

	texture.unref();

	for (TextureSlot &slot : slots) {
		_release_external_texture(slot.handle);
		slot = TextureSlot();
	}
	for (const RetiredTexture &retired : retired_textures) {
		_release_external_texture(retired.handle);
	}
	retired_textures.clear();
	if (render_thread_service->has_pending_work()) {
		render_thread_service = nullptr;
	} else {
		memdelete(render_thread_service);
		render_thread_service = nullptr;
	}
	mpv_core = nullptr;
	requested_width = 0;
	requested_height = 0;
	texture_request_in_flight = false;
	render_request_in_flight.store(false, std::memory_order_release);
	last_render_result.store(0, std::memory_order_release);
	last_rendered_slot.store(-1, std::memory_order_release);
	update_callback_count.store(0, std::memory_order_release);
	published_slot_index = -1;
	render_slot_index = -1;
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
