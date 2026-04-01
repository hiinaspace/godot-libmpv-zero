#include "sw_video_output.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace libmpv_zero {

void SwVideoOutput::attach(Node * /*p_owner*/, const Callable &p_texture_ready, const Callable & /*p_probe_failed*/) {
	Ref<Image> image = Image::create_empty(256, 256, false, Image::FORMAT_RGBA8);
	image->fill(Color(1.0, 0.0, 1.0, 1.0));

	texture = ImageTexture::create_from_image(image);
	status = "software placeholder texture ready";
	p_texture_ready.call(texture);
}

void SwVideoOutput::detach() {
	texture.unref();
	status = "software video backend detached";
}

Ref<Texture2D> SwVideoOutput::get_texture() const {
	return texture;
}

String SwVideoOutput::get_status() const {
	return status;
}

} // namespace libmpv_zero
