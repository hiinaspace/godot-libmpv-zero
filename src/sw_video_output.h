#pragma once

#include "video_output_backend.h"

#include <godot_cpp/classes/image_texture.hpp>

namespace libmpv_zero {

class SwVideoOutput : public VideoOutputBackend {
public:
	void attach(godot::Node *p_owner, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) override;
	void detach() override;
	godot::Ref<godot::Texture2D> get_texture() const override;
	godot::String get_status() const override;

private:
	godot::Ref<godot::ImageTexture> texture;
	godot::String status = "software video backend idle";
};

} // namespace libmpv_zero
