#pragma once

#include <mutex>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

class RenderThreadService : public godot::Object {
	GDCLASS(RenderThreadService, godot::Object)

public:
	struct ExternalTextureHandle {
		bool success = false;
		bool ready = false;
		godot::RID wrapped_texture;
		uint64_t instance_handle = 0;
		uint64_t physical_device_handle = 0;
		uint64_t logical_device = 0;
		uint64_t queue_handle = 0;
		uint32_t queue_family_index = 0;
		uint64_t image_handle = 0;
		uint64_t image_memory_handle = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		godot::String status;
	};

	RenderThreadService() = default;
	~RenderThreadService() override = default;

	bool request_external_texture(uint32_t p_width, uint32_t p_height, const godot::Color &p_clear_color, bool p_clear_texture = true);
	bool poll_external_texture_result(ExternalTextureHandle &r_result);
	void release_external_texture(const ExternalTextureHandle &p_handle);
	bool has_pending_work() const;
	godot::String get_status() const;

protected:
	static void _bind_methods();

private:
	struct PendingCreateRequest {
		bool active = false;
		uint32_t width = 0;
		uint32_t height = 0;
		godot::Color clear_color;
		bool clear_texture = true;
	};

	struct PendingReleaseRequest {
		bool active = false;
		godot::RID wrapped_texture;
		uint64_t logical_device = 0;
		uint64_t image_handle = 0;
		uint64_t image_memory_handle = 0;
	};

	void _create_external_texture_on_render_thread();
	void _release_external_texture_on_render_thread();

	mutable std::mutex mutex;
	PendingCreateRequest create_request;
	PendingReleaseRequest release_request;
	ExternalTextureHandle pending_result;
	godot::String status = "render thread service idle";
};

} // namespace libmpv_zero
