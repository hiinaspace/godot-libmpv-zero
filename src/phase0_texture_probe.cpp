#include "phase0_texture_probe.h"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void Phase0TextureProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_texture"), &Phase0TextureProbe::get_texture);
	ClassDB::bind_method(D_METHOD("get_status"), &Phase0TextureProbe::get_status);
	ClassDB::bind_method(D_METHOD("get_logical_device_handle"), &Phase0TextureProbe::get_logical_device_handle);
	ClassDB::bind_method(D_METHOD("get_image_handle"), &Phase0TextureProbe::get_image_handle);

	ADD_SIGNAL(MethodInfo("texture_ready", PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")));
	ADD_SIGNAL(MethodInfo("probe_failed", PropertyInfo(Variant::STRING, "status")));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "status"), "", "get_status");
}

void Phase0TextureProbe::_ready() {
	if (probe_requested) {
		return;
	}

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	ERR_FAIL_NULL(rendering_server);

	probe_requested = true;
	status = "queued render-thread probe";
	set_process(true);
	rendering_server->call_on_render_thread(callable_mp(this, &Phase0TextureProbe::_create_probe_texture_on_render_thread));
}

void Phase0TextureProbe::_process(double /*p_delta*/) {
	_publish_pending_result();
}

void Phase0TextureProbe::_exit_tree() {
	if (cleanup_requested) {
		return;
	}

	cleanup_requested = true;
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		return;
	}

	rendering_server->call_on_render_thread(callable_mp(this, &Phase0TextureProbe::_cleanup_render_resources_on_render_thread));
}

Ref<Texture2D> Phase0TextureProbe::get_texture() const {
	return published_texture;
}

String Phase0TextureProbe::get_status() const {
	return status;
}

uint64_t Phase0TextureProbe::get_logical_device_handle() const {
	return logical_device_handle;
}

uint64_t Phase0TextureProbe::get_image_handle() const {
	return image_handle;
}

void Phase0TextureProbe::_publish_pending_result() {
	PendingPublish publish;
	{
		std::lock_guard<std::mutex> lock(pending_mutex);
		if (!pending_publish.ready) {
			return;
		}

		publish = pending_publish;
		pending_publish = PendingPublish();
	}

	status = publish.status;
	if (!publish.success) {
		set_process(false);
		emit_signal("probe_failed", status);
		return;
	}

	source_texture_rid = publish.source_texture;
	wrapped_texture_rid = publish.wrapped_texture;
	logical_device_handle = publish.logical_device;
	image_handle = publish.image_handle;

	if (published_texture.is_null()) {
		published_texture.instantiate();
	}

	published_texture->set_texture_rd_rid(wrapped_texture_rid);
	set_process(false);
	emit_signal("texture_ready", published_texture);
}

void Phase0TextureProbe::_create_probe_texture_on_render_thread() {
	PendingPublish result;
	result.ready = true;

	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		result.status = "rendering server unavailable";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	RenderingDevice *rendering_device = rendering_server->get_rendering_device();
	if (!rendering_device) {
		result.status = "rendering device unavailable";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	Ref<RDTextureFormat> format;
	format.instantiate();
	format->set_width(probe_width);
	format->set_height(probe_height);
	format->set_depth(1);
	format->set_array_layers(1);
	format->set_mipmaps(1);
	format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
	format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	format->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
	const BitField<RenderingDevice::TextureUsageBits> usage_bits =
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	format->set_usage_bits(usage_bits);

	Ref<RDTextureView> view;
	view.instantiate();

	result.source_texture = rendering_device->texture_create(format, view);
	if (!rendering_device->texture_is_valid(result.source_texture)) {
		result.status = "failed to create source RD texture";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	rendering_device->texture_clear(result.source_texture, Color(1.0, 0.0, 1.0, 1.0), 0, 1, 0, 1);

	result.logical_device = rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE, RID(), 0);
	result.image_handle = rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_IMAGE, result.source_texture, 0);

	if (result.logical_device == 0) {
		rendering_device->free_rid(result.source_texture);
		result.source_texture = RID();
		result.status = "failed to query Vulkan device handle";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	if (result.image_handle == 0) {
		rendering_device->free_rid(result.source_texture);
		result.source_texture = RID();
		result.status = "failed to query Vulkan image handle";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	result.wrapped_texture = rendering_device->texture_create_from_extension(
			RenderingDevice::TEXTURE_TYPE_2D,
			RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
			RenderingDevice::TEXTURE_SAMPLES_1,
			usage_bits,
			result.image_handle,
			probe_width,
			probe_height,
			1,
			1);

	if (!rendering_device->texture_is_valid(result.wrapped_texture)) {
		rendering_device->free_rid(result.source_texture);
		result.source_texture = RID();
		result.status = "failed to wrap external texture from Vulkan image";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	result.success = true;
	result.status = "phase 0 texture probe ready";
	std::lock_guard<std::mutex> lock(pending_mutex);
	pending_publish = result;
}

void Phase0TextureProbe::_cleanup_render_resources_on_render_thread() {
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		return;
	}

	RenderingDevice *rendering_device = rendering_server->get_rendering_device();
	if (!rendering_device) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(pending_mutex);
		if (pending_publish.wrapped_texture.is_valid()) {
			rendering_device->free_rid(pending_publish.wrapped_texture);
			pending_publish.wrapped_texture = RID();
		}
		if (pending_publish.source_texture.is_valid()) {
			rendering_device->free_rid(pending_publish.source_texture);
			pending_publish.source_texture = RID();
		}
		pending_publish.ready = false;
	}

	if (wrapped_texture_rid.is_valid()) {
		rendering_device->free_rid(wrapped_texture_rid);
		wrapped_texture_rid = RID();
	}

	if (source_texture_rid.is_valid()) {
		rendering_device->free_rid(source_texture_rid);
		source_texture_rid = RID();
	}
}
