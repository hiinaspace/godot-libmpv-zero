#include "mpv_texture.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
using namespace godot;

MPVTexture::MPVTexture() {
	_ensure_placeholder();
	presentation_texture.instantiate();
	_update_presentation_texture();
}

MPVTexture::~MPVTexture() = default;

void MPVTexture::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_placeholder_texture", "texture"), &MPVTexture::set_placeholder_texture);
	ClassDB::bind_method(D_METHOD("get_placeholder_texture"), &MPVTexture::get_placeholder_texture);
	ClassDB::bind_method(D_METHOD("get_live_texture"), &MPVTexture::get_live_texture);
	ClassDB::bind_method(D_METHOD("is_playback_active"), &MPVTexture::is_playback_active);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "placeholder_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_placeholder_texture", "get_placeholder_texture");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "live_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_live_texture");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "playback_active", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "is_playback_active");
}

Ref<Texture2D> MPVTexture::_get_active_texture() const {
	if (live_texture.is_valid()) {
		return live_texture;
	}
	if (placeholder_texture.is_valid()) {
		return placeholder_texture;
	}
	if (generated_placeholder.is_valid()) {
		return generated_placeholder;
	}
	return Ref<Texture2D>();
}

void MPVTexture::_ensure_placeholder() {
	if (generated_placeholder.is_valid()) {
		return;
	}

	Ref<Image> image = Image::create_empty(2, 2, false, Image::FORMAT_RGBA8);
	image->fill(Color(0.0, 0.0, 0.0, 1.0));
	generated_placeholder = ImageTexture::create_from_image(image);
}

void MPVTexture::_update_presentation_texture() {
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		return;
	}

	if (presentation_texture.is_null()) {
		presentation_texture.instantiate();
	}

	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_null()) {
		return;
	}

	RID rd_rid;
	if (const Texture2DRD *rd_texture = Object::cast_to<Texture2DRD>(active.ptr())) {
		rd_rid = rd_texture->get_texture_rd_rid();
	} else {
		const RID active_rid = active->get_rid();
		if (active_rid.is_valid()) {
			rd_rid = rendering_server->texture_get_rd_texture(active_rid, false);
		}
	}

	if (!rd_rid.is_valid()) {
		return;
	}

	presentation_texture->set_texture_rd_rid(rd_rid);
}

void MPVTexture::set_placeholder_texture(const Ref<Texture2D> &p_texture) {
	placeholder_texture = p_texture;
	if (placeholder_texture.is_null()) {
		_ensure_placeholder();
	}
	_update_presentation_texture();
	emit_changed();
}

Ref<Texture2D> MPVTexture::get_placeholder_texture() const {
	return placeholder_texture;
}

void MPVTexture::set_live_texture(const Ref<Texture2D> &p_texture) {
	live_texture = p_texture;
	_update_presentation_texture();
	emit_changed();
}

Ref<Texture2D> MPVTexture::get_live_texture() const {
	return live_texture;
}

void MPVTexture::set_playback_active(bool p_active) {
	if (playback_active == p_active) {
		return;
	}
	playback_active = p_active;
	emit_changed();
}

bool MPVTexture::is_playback_active() const {
	return playback_active;
}

int32_t MPVTexture::_get_width() const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		return active->get_width();
	}
	return 0;
}

int32_t MPVTexture::_get_height() const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		return active->get_height();
	}
	return 0;
}

bool MPVTexture::_is_pixel_opaque(int32_t /*p_x*/, int32_t /*p_y*/) const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_null()) {
		return false;
	}
	return !active->has_alpha();
}

bool MPVTexture::_has_alpha() const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		return active->has_alpha();
	}
	return false;
}

RID MPVTexture::_get_rid() const {
	if (presentation_texture.is_null()) {
		const_cast<MPVTexture *>(this)->_update_presentation_texture();
	}
	return presentation_texture.is_valid() ? presentation_texture->get_rid() : RID();
}

void MPVTexture::_draw(const RID &p_to_canvas_item, const Vector2 &p_pos, const Color &p_modulate, bool p_transpose) const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		active->draw(p_to_canvas_item, p_pos, p_modulate, p_transpose);
	}
}

void MPVTexture::_draw_rect(const RID &p_to_canvas_item, const Rect2 &p_rect, bool p_tile, const Color &p_modulate, bool p_transpose) const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		active->draw_rect(p_to_canvas_item, p_rect, p_tile, p_modulate, p_transpose);
	}
}

void MPVTexture::_draw_rect_region(const RID &p_to_canvas_item, const Rect2 &p_rect, const Rect2 &p_src_rect, const Color &p_modulate, bool p_transpose, bool p_clip_uv) const {
	const Ref<Texture2D> active = _get_active_texture();
	if (active.is_valid()) {
		active->draw_rect_region(p_to_canvas_item, p_rect, p_src_rect, p_modulate, p_transpose, p_clip_uv);
	}
}
