#pragma once

#include "render_thread_service.h"
#include "video_output_backend.h"

#include <godot_cpp/classes/texture2drd.hpp>

namespace libmpv_zero {

class VkVideoOutput : public VideoOutputBackend {
public:
	void attach(godot::Node *p_owner, MpvCore *p_mpv_core, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) override;
	void detach() override;
	void update() override;
	godot::Ref<godot::Texture2D> get_texture() const override;
	godot::String get_status() const override;

private:
	RenderThreadService *render_thread_service = nullptr;
	godot::Ref<godot::Texture2DRD> texture;
	RenderThreadService::ExternalTextureHandle external_texture;
	godot::Callable texture_ready_callback;
	godot::Callable probe_failed_callback;
	godot::String status = "vulkan video backend idle";
};

} // namespace libmpv_zero
