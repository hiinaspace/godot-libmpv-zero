#include "phase0_texture_probe.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/memory.hpp>

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
	if (render_thread_service) {
		return;
	}

	render_thread_service = memnew(libmpv_zero::RenderThreadService);
	render_thread_service->request_external_texture(probe_width, probe_height, Color(1.0, 0.0, 1.0, 1.0));
	status = "queued render-thread probe";
	set_process(true);
}

void Phase0TextureProbe::_process(double /*p_delta*/) {
	_publish_pending_result();
}

void Phase0TextureProbe::_exit_tree() {
	set_process(false);
	if (published_texture.is_valid()) {
		published_texture->set_texture_rd_rid(RID());
		published_texture.unref();
	}
	if (render_thread_service) {
		render_thread_service->release_external_texture(external_texture);
		memdelete(render_thread_service);
		render_thread_service = nullptr;
	}
	external_texture = libmpv_zero::RenderThreadService::ExternalTextureHandle();
}

Ref<Texture2D> Phase0TextureProbe::get_texture() const {
	return published_texture;
}

String Phase0TextureProbe::get_status() const {
	return status;
}

uint64_t Phase0TextureProbe::get_logical_device_handle() const {
	return external_texture.logical_device;
}

uint64_t Phase0TextureProbe::get_image_handle() const {
	return external_texture.image_handle;
}

void Phase0TextureProbe::_publish_pending_result() {
	if (!render_thread_service) {
		return;
	}

	libmpv_zero::RenderThreadService::ExternalTextureHandle result;
	if (!render_thread_service->poll_external_texture_result(result)) {
		return;
	}

	status = result.status;
	if (!result.success) {
		set_process(false);
		emit_signal("probe_failed", status);
		return;
	}

	external_texture = result;
	if (published_texture.is_null()) {
		published_texture.instantiate();
	}
	published_texture->set_texture_rd_rid(external_texture.wrapped_texture);
	set_process(false);
	emit_signal("texture_ready", published_texture);
}
