#pragma once

#include "mini_mpv_render.h"
#include "render_thread_service.h"
#include "video_output_backend.h"

#include <atomic>
#include <array>
#include <vector>

#include <godot_cpp/classes/texture2drd.hpp>

namespace libmpv_zero {

class VkVideoOutput : public VideoOutputBackend {
public:
	void attach(godot::Node *p_owner, MpvCore *p_mpv_core, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) override;
	void detach() override;
	void update() override;
	bool is_ready_for_playback() const override;
	godot::Ref<godot::Texture2D> get_texture() const override;
	godot::String get_status() const override;

private:
	struct TextureSlot {
		RenderThreadService::ExternalTextureHandle handle;
		VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool published = false;
	};

	struct RetiredTexture {
		RenderThreadService::ExternalTextureHandle handle;
		int release_after_updates = 0;
	};

	struct RenderDispatch {
#ifdef _WIN32
		void *library = nullptr;
		void *vulkan_library = nullptr;
#endif
		PFN_mpv_render_context_create create = nullptr;
		PFN_mpv_render_context_set_update_callback set_update_callback = nullptr;
		PFN_mpv_render_context_update update = nullptr;
		PFN_mpv_render_context_render render = nullptr;
		PFN_mpv_render_context_free free = nullptr;
		PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
	};

	static void _on_render_update(void *p_context);
	static void _render_frame_on_render_thread_static(uint64_t p_self);
	void _render_frame_on_render_thread();
	bool _load_render_dispatch();
	void _unload_render_dispatch();
	bool _ensure_external_textures(int p_width, int p_height, int p_target_count);
	bool _ensure_render_context();
	void _publish_slot(int p_slot_index);
	void _retire_slot(TextureSlot &p_slot, int p_delay_updates);
	void _poll_retired_textures();
	int _find_next_render_slot() const;
	bool _slot_matches_size(const TextureSlot &p_slot, int p_width, int p_height) const;
	void _release_external_texture(const RenderThreadService::ExternalTextureHandle &p_texture);

	RenderDispatch dispatch;
	RenderThreadService *render_thread_service = nullptr;
	MpvCore *mpv_core = nullptr;
	godot::Ref<godot::Texture2DRD> texture;
	std::array<TextureSlot, 2> slots;
	std::vector<RetiredTexture> retired_textures;
	godot::Callable texture_ready_callback;
	godot::Callable probe_failed_callback;
	mpv_render_context *render_context = nullptr;
	std::atomic_bool frame_dirty = false;
	std::atomic_bool render_request_in_flight = false;
	std::atomic_int update_callback_count = 0;
	std::atomic_int last_render_result = 0;
	std::atomic_int successful_render_count = 0;
	std::atomic_int last_rendered_slot = -1;
	bool readback_logged = false;
	bool render_queue_logged = false;
	bool render_thread_logged = false;
	bool attach_logged = false;
	bool texture_request_logged = false;
	bool texture_ready_logged = false;
	bool render_context_logged = false;
	bool update_flags_logged = false;
	bool forced_render_logged = false;
	bool update_callback_logged = false;
	int published_slot_index = -1;
	int render_slot_index = -1;
	int requested_width = 0;
	int requested_height = 0;
	bool texture_request_in_flight = false;
	godot::String status = "vulkan video backend idle";
};

} // namespace libmpv_zero
