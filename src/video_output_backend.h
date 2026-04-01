#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

class VideoOutputBackend {
public:
	virtual ~VideoOutputBackend() = default;

	virtual void attach(godot::Node *p_owner, const godot::Callable &p_texture_ready, const godot::Callable &p_probe_failed) = 0;
	virtual void detach() = 0;
	virtual godot::Ref<godot::Texture2D> get_texture() const = 0;
	virtual godot::String get_status() const = 0;
};

} // namespace libmpv_zero
