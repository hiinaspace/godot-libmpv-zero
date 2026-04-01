#pragma once

#include "video_output_backend.h"

namespace godot {
class Phase0TextureProbe;
}

namespace libmpv_zero {

class VkVideoOutput : public VideoOutputBackend {
public:
	void attach(godot::Node *p_owner, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) override;
	void detach() override;
	godot::Ref<godot::Texture2D> get_texture() const override;
	godot::String get_status() const override;

private:
	godot::Phase0TextureProbe *probe = nullptr;
	godot::Node *owner = nullptr;
	godot::String status = "vulkan video backend idle";
};

} // namespace libmpv_zero
