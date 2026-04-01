#include "sw_video_output.h"

#include "mpv_core.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace godot;

namespace libmpv_zero {

void SwVideoOutput::_on_render_update(void *p_context) {
	SwVideoOutput *self = static_cast<SwVideoOutput *>(p_context);
	if (self) {
		self->frame_dirty.store(true, std::memory_order_relaxed);
	}
}

bool SwVideoOutput::_load_render_dispatch() {
#ifdef _WIN32
	if (dispatch.library) {
		return true;
	}

	dispatch.library = LoadLibraryW(L"libmpv-2.dll");
	if (!dispatch.library) {
		status = "failed to load libmpv-2.dll for render API";
		return false;
	}

	dispatch.create = reinterpret_cast<PFN_mpv_render_context_create>(GetProcAddress(dispatch.library, "mpv_render_context_create"));
	dispatch.set_update_callback = reinterpret_cast<PFN_mpv_render_context_set_update_callback>(GetProcAddress(dispatch.library, "mpv_render_context_set_update_callback"));
	dispatch.update = reinterpret_cast<PFN_mpv_render_context_update>(GetProcAddress(dispatch.library, "mpv_render_context_update"));
	dispatch.render = reinterpret_cast<PFN_mpv_render_context_render>(GetProcAddress(dispatch.library, "mpv_render_context_render"));
	dispatch.free = reinterpret_cast<PFN_mpv_render_context_free>(GetProcAddress(dispatch.library, "mpv_render_context_free"));

	if (!dispatch.create || !dispatch.set_update_callback || !dispatch.update || !dispatch.render || !dispatch.free) {
		status = "failed to resolve libmpv render symbols";
		_unload_render_dispatch();
		return false;
	}

	return true;
#else
	status = "software render backend currently only supports Windows";
	return false;
#endif
}

void SwVideoOutput::_unload_render_dispatch() {
#ifdef _WIN32
	if (dispatch.library) {
		FreeLibrary(dispatch.library);
	}
#endif
	dispatch = RenderDispatch();
}

bool SwVideoOutput::_ensure_render_context() {
	if (render_context) {
		return true;
	}
	if (!mpv_core || !mpv_core->is_initialized()) {
		status = "mpv core is not initialized";
		return false;
	}
	if (!_load_render_dispatch()) {
		return false;
	}

	mpv_render_param params[] = {
		{ MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW) },
		{ MPV_RENDER_PARAM_INVALID, nullptr },
	};

	const int create_result = dispatch.create(&render_context, static_cast<mpv_handle *>(mpv_core->get_native_handle()), params);
	if (create_result < 0 || !render_context) {
		status = "mpv_render_context_create failed";
		render_context = nullptr;
		return false;
	}

	dispatch.set_update_callback(render_context, &SwVideoOutput::_on_render_update, this);
	frame_dirty.store(true, std::memory_order_relaxed);
	status = "software render context ready";
	return true;
}

void SwVideoOutput::_recreate_texture_if_needed(int p_width, int p_height) {
	if (p_width <= 0 || p_height <= 0) {
		return;
	}
	if (texture.is_valid() && texture_width == p_width && texture_height == p_height) {
		return;
	}

	texture_width = p_width;
	texture_height = p_height;
	frame_buffer.resize(texture_width * texture_height * 4);

	if (image.is_null()) {
		image.instantiate();
	}
	image->set_data(texture_width, texture_height, false, Image::FORMAT_RGBA8, frame_buffer);

	if (texture.is_null()) {
		texture = ImageTexture::create_from_image(image);
	} else {
		texture->set_image(image);
	}

	if (!texture_ready_emitted) {
		texture_ready_emitted = true;
		texture_ready_callback.call(texture);
	}
}

void SwVideoOutput::attach(Node * /*p_owner*/, MpvCore *p_mpv_core, const Callable &p_texture_ready, const Callable &p_probe_failed) {
	mpv_core = p_mpv_core;
	texture_ready_callback = p_texture_ready;
	probe_failed_callback = p_probe_failed;
	texture_ready_emitted = false;
	status = "software video backend attached";
}

void SwVideoOutput::detach() {
	if (render_context && dispatch.free) {
		dispatch.free(render_context);
		render_context = nullptr;
	}
	_unload_render_dispatch();
	frame_buffer.clear();
	texture.unref();
	image.unref();
	texture_width = 0;
	texture_height = 0;
	texture_ready_emitted = false;
	status = "software video backend detached";
}

void SwVideoOutput::update() {
	if (!_ensure_render_context()) {
		return;
	}
	if (!mpv_core || !mpv_core->has_loaded_file()) {
		return;
	}

	const int width = mpv_core->get_video_width();
	const int height = mpv_core->get_video_height();
	if (width <= 0 || height <= 0) {
		return;
	}

	_recreate_texture_if_needed(width, height);

	if (!frame_dirty.exchange(false, std::memory_order_relaxed)) {
		const uint64_t update_flags = dispatch.update(render_context);
		if ((update_flags & MPV_RENDER_UPDATE_FRAME) == 0) {
			return;
		}
	}

	int size[2] = { texture_width, texture_height };
	size_t stride = static_cast<size_t>(texture_width) * 4;
	char format[] = "rgb0";
	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_SW_SIZE, size },
		{ MPV_RENDER_PARAM_SW_FORMAT, format },
		{ MPV_RENDER_PARAM_SW_STRIDE, &stride },
		{ MPV_RENDER_PARAM_SW_POINTER, frame_buffer.ptrw() },
		{ MPV_RENDER_PARAM_INVALID, nullptr },
	};

	const int render_result = dispatch.render(render_context, render_params);
	if (render_result < 0) {
		status = "mpv_render_context_render failed";
		if (probe_failed_callback.is_valid()) {
			probe_failed_callback.call(status);
		}
		return;
	}

	for (int i = 3; i < frame_buffer.size(); i += 4) {
		frame_buffer.set(i, 255);
	}

	image->set_data(texture_width, texture_height, false, Image::FORMAT_RGBA8, frame_buffer);
	texture->set_image(image);
	status = vformat("software frame ready %dx%d", texture_width, texture_height);
}

Ref<Texture2D> SwVideoOutput::get_texture() const {
	return texture;
}

String SwVideoOutput::get_status() const {
	return status;
}

} // namespace libmpv_zero
