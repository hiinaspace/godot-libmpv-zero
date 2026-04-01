#pragma once

#include "mini_mpv_render.h"
#include "video_output_backend.h"

#include <atomic>

#ifdef _WIN32
#include <windows.h>
#endif

#include <godot_cpp/classes/image_texture.hpp>

namespace libmpv_zero {

class MpvCore;

class SwVideoOutput : public VideoOutputBackend {
public:
	void attach(godot::Node *p_owner, MpvCore *p_mpv_core, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) override;
	void detach() override;
	void update() override;
	bool is_ready_for_playback() const override { return true; }
	godot::Ref<godot::Texture2D> get_texture() const override;
	godot::String get_status() const override;

private:
	struct RenderDispatch {
#ifdef _WIN32
		HMODULE library = nullptr;
#endif
		PFN_mpv_render_context_create create = nullptr;
		PFN_mpv_render_context_set_update_callback set_update_callback = nullptr;
		PFN_mpv_render_context_update update = nullptr;
		PFN_mpv_render_context_render render = nullptr;
		PFN_mpv_render_context_free free = nullptr;
	};

	static void _on_render_update(void *p_context);
	bool _load_render_dispatch();
	void _unload_render_dispatch();
	bool _ensure_render_context();
	void _recreate_texture_if_needed(int p_width, int p_height);

	MpvCore *mpv_core = nullptr;
	RenderDispatch dispatch;
	mpv_render_context *render_context = nullptr;
	godot::Callable texture_ready_callback;
	godot::Callable probe_failed_callback;
	godot::Ref<godot::Image> image;
	godot::Ref<godot::ImageTexture> texture;
	godot::PackedByteArray frame_buffer;
	std::atomic<bool> frame_dirty = false;
	bool texture_ready_emitted = false;
	int texture_width = 0;
	int texture_height = 0;
	godot::String status = "software video backend idle";
};

} // namespace libmpv_zero
