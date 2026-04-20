#pragma once

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

class MPVTexture : public Texture2D {
	GDCLASS(MPVTexture, Texture2D)

private:
	Ref<Texture2D> live_texture;
	Ref<Texture2D> placeholder_texture;
	Ref<ImageTexture> generated_placeholder;
	Ref<Texture2DRD> presentation_texture;
	bool playback_active = false;

	static void _bind_methods();
	Ref<Texture2D> _get_active_texture() const;
	void _ensure_placeholder();
	void _update_presentation_texture();

public:
	MPVTexture();
	~MPVTexture() override;

	void set_placeholder_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_placeholder_texture() const;

	void set_live_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_live_texture() const;

	void set_playback_active(bool p_active);
	bool is_playback_active() const;

	int32_t _get_width() const override;
	int32_t _get_height() const override;
	bool _is_pixel_opaque(int32_t p_x, int32_t p_y) const override;
	bool _has_alpha() const override;
	RID _get_rid() const override;
	void _draw(const RID &p_to_canvas_item, const Vector2 &p_pos, const Color &p_modulate, bool p_transpose) const override;
	void _draw_rect(const RID &p_to_canvas_item, const Rect2 &p_rect, bool p_tile, const Color &p_modulate, bool p_transpose) const override;
	void _draw_rect_region(const RID &p_to_canvas_item, const Rect2 &p_rect, const Rect2 &p_src_rect, const Color &p_modulate, bool p_transpose, bool p_clip_uv) const override;
};

} // namespace godot
