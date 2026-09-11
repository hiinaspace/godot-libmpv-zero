#include "render_thread_service.h"

#include "mini_vulkan.h"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>


using namespace godot;

namespace libmpv_zero {

void RenderThreadService::_bind_methods() {
}

bool RenderThreadService::_release_requests_match(const PendingReleaseRequest &p_a, const PendingReleaseRequest &p_b) {
	return p_a.wrapped_texture == p_b.wrapped_texture &&
			p_a.logical_device == p_b.logical_device &&
			p_a.image_handle == p_b.image_handle &&
			p_a.image_memory_handle == p_b.image_memory_handle;
}

bool RenderThreadService::request_external_texture(uint32_t p_width, uint32_t p_height, const Color &p_clear_color, bool p_clear_texture) {
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	ERR_FAIL_NULL_V(rendering_server, false);

	{
		std::lock_guard<std::mutex> lock(mutex);
		if (create_request.active) {
			status = "external texture request already in flight";
			return false;
		}

		create_request.active = true;
		create_request.width = p_width;
		create_request.height = p_height;
		create_request.clear_color = p_clear_color;
		create_request.clear_texture = p_clear_texture;
	}

	status = "queued external texture request";
	rendering_server->call_on_render_thread(callable_mp(this, &RenderThreadService::_create_external_texture_on_render_thread));
	return true;
}

bool RenderThreadService::poll_external_texture_result(ExternalTextureHandle &r_result) {
	std::lock_guard<std::mutex> lock(mutex);
	if (!pending_result.ready) {
		return false;
	}

	r_result = pending_result;
	pending_result = ExternalTextureHandle();
	status = r_result.status;
	return true;
}

void RenderThreadService::release_external_texture(const ExternalTextureHandle &p_handle) {
	if (!p_handle.wrapped_texture.is_valid() && !p_handle.image_handle && !p_handle.image_memory_handle) {
		return;
	}

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rendering_server);

	{
		std::lock_guard<std::mutex> lock(mutex);
		PendingReleaseRequest request;
		request.wrapped_texture = p_handle.wrapped_texture;
		request.logical_device = p_handle.logical_device;
		request.image_handle = p_handle.image_handle;
		request.image_memory_handle = p_handle.image_memory_handle;
		if (release_request_active && _release_requests_match(active_release_request, request)) {
			status = "queued external texture release";
			return;
		}
		for (const PendingReleaseRequest &queued : release_requests) {
			if (_release_requests_match(queued, request)) {
				status = "queued external texture release";
				return;
			}
		}
		release_requests.push_back(request);
		if (release_request_active) {
			status = "queued external texture release";
			return;
		}
		release_request_active = true;
		active_release_request = release_requests.front();
	}

	status = "queued external texture release";
	rendering_server->call_on_render_thread(callable_mp(this, &RenderThreadService::_release_external_texture_on_render_thread));
}

bool RenderThreadService::has_pending_work() const {
	std::lock_guard<std::mutex> lock(mutex);
	return create_request.active || release_request_active || !release_requests.empty();
}

String RenderThreadService::get_status() const {
	std::lock_guard<std::mutex> lock(mutex);
	return status;
}

void RenderThreadService::_create_external_texture_on_render_thread() {
	PendingCreateRequest request;
	{
		std::lock_guard<std::mutex> lock(mutex);
		request = create_request;
		create_request = PendingCreateRequest();
	}

	ExternalTextureHandle result;
	result.ready = true;
	result.width = request.width;
	result.height = request.height;

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		result.status = "rendering server unavailable";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	RenderingDevice *rendering_device = rendering_server->get_rendering_device();
	if (!rendering_device) {
		result.status = "rendering device unavailable";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	const BitField<RenderingDevice::TextureUsageBits> usage_bits =
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT;

	VkInstance instance = reinterpret_cast<VkInstance>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, RID(), 0));
	VkPhysicalDevice physical_device = reinterpret_cast<VkPhysicalDevice>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE, RID(), 0));
	VkDevice device = reinterpret_cast<VkDevice>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE, RID(), 0));
	VkQueue queue = reinterpret_cast<VkQueue>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_QUEUE, RID(), 0));
	const uint32_t queue_family_index = static_cast<uint32_t>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_QUEUE_FAMILY_INDEX, RID(), 0));
	if (!instance || !physical_device || !device || !queue) {
		result.status = "failed to query Godot Vulkan handles";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	Ref<RDTextureFormat> texture_format;
	texture_format.instantiate();
	texture_format->set_width(request.width);
	texture_format->set_height(request.height);
	texture_format->set_depth(1);
	texture_format->set_array_layers(1);
	texture_format->set_mipmaps(1);
	texture_format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
	texture_format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
	texture_format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	texture_format->set_usage_bits(usage_bits);
	texture_format->add_shareable_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	texture_format->add_shareable_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB);

	Ref<RDTextureView> texture_view;
	texture_view.instantiate();

	TypedArray<PackedByteArray> initial_data;
	result.wrapped_texture = rendering_device->texture_create(texture_format, texture_view, initial_data);
	if (!rendering_device->texture_is_valid(result.wrapped_texture)) {
		result.status = "failed to create RenderingDevice texture";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	result.instance_handle = reinterpret_cast<uint64_t>(instance);
	result.physical_device_handle = reinterpret_cast<uint64_t>(physical_device);
	result.logical_device = reinterpret_cast<uint64_t>(device);
	result.queue_handle = reinterpret_cast<uint64_t>(queue);
	result.queue_family_index = queue_family_index;
	result.image_handle = rendering_device->texture_get_native_handle(result.wrapped_texture);
	result.image_memory_handle = 0;

	if (!result.image_handle) {
		rendering_device->free_rid(result.wrapped_texture);
		result.wrapped_texture = RID();
		result.status = "failed to query native texture handle";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	if (request.clear_texture) {
		const Error clear_error = rendering_device->texture_clear(result.wrapped_texture, request.clear_color, 0, 1, 0, 1);
		if (clear_error != OK) {
			rendering_device->free_rid(result.wrapped_texture);
			result.wrapped_texture = RID();
			result.status = "failed to clear RenderingDevice texture";
			std::lock_guard<std::mutex> lock(mutex);
			pending_result = result;
			return;
		}
	}

	result.success = true;
	result.status = "external texture ready";
	std::lock_guard<std::mutex> lock(mutex);
	pending_result = result;
}

void RenderThreadService::_release_external_texture_on_render_thread() {
	PendingReleaseRequest release;
	bool has_release = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!release_requests.empty()) {
			release = release_requests.front();
			release_requests.erase(release_requests.begin());
			has_release = true;
			active_release_request = release;
		} else {
			release_request_active = false;
			active_release_request = PendingReleaseRequest();
		}
	}

	if (!has_release) {
		return;
	}

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		return;
	}

	RenderingDevice *rendering_device = rendering_server->get_rendering_device();
	if (!rendering_device) {
		return;
	}

	if (release.wrapped_texture.is_valid()) {
		rendering_device->free_rid(release.wrapped_texture);
	}

	bool should_schedule_next = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		should_schedule_next = !release_requests.empty();
		if (!should_schedule_next) {
			release_request_active = false;
			active_release_request = PendingReleaseRequest();
		} else {
			active_release_request = release_requests.front();
		}
	}

	if (should_schedule_next) {
		rendering_server->call_on_render_thread(callable_mp(this, &RenderThreadService::_release_external_texture_on_render_thread));
	}
}

} // namespace libmpv_zero
