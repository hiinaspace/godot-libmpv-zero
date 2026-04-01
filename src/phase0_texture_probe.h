#pragma once

#include "render_thread_service.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class Phase0TextureProbe : public Node {
	GDCLASS(Phase0TextureProbe, Node)

	static void _bind_methods();

	void _publish_pending_result();

	Ref<Texture2DRD> published_texture;
	libmpv_zero::RenderThreadService *render_thread_service = nullptr;
	libmpv_zero::RenderThreadService::ExternalTextureHandle external_texture;
	uint32_t probe_width = 256;
	uint32_t probe_height = 256;
	String status = "idle";

public:
	Phase0TextureProbe() = default;
	~Phase0TextureProbe() override = default;

	void _ready() override;
	void _process(double p_delta) override;
	void _exit_tree() override;

	Ref<Texture2D> get_texture() const;
	String get_status() const;
	uint64_t get_logical_device_handle() const;
	uint64_t get_image_handle() const;
};

} // namespace godot
