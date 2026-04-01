#include "vk_video_output.h"

#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/memory.hpp>

using namespace godot;

namespace libmpv_zero {

void VkVideoOutput::attach(Node *p_owner, MpvCore * /*p_mpv_core*/, const Callable &p_texture_ready, const Callable &p_probe_failed) {
	(void)p_owner;
	if (render_thread_service) {
		return;
	}

	render_thread_service = memnew(RenderThreadService);
	texture_ready_callback = p_texture_ready;
	probe_failed_callback = p_probe_failed;
	render_thread_service->request_external_texture(256, 256, Color(1.0, 0.0, 1.0, 1.0));
	status = "vulkan backend waiting for external texture";
}

void VkVideoOutput::update() {
	if (!render_thread_service) {
		return;
	}

	RenderThreadService::ExternalTextureHandle result;
	if (!render_thread_service->poll_external_texture_result(result)) {
		return;
	}

	status = result.status;
	if (!result.success) {
		if (probe_failed_callback.is_valid()) {
			probe_failed_callback.call(status);
		}
		return;
	}

	external_texture = result;
	if (texture.is_null()) {
		texture.instantiate();
	}
	texture->set_texture_rd_rid(external_texture.wrapped_texture);
	status = "vulkan external texture ready";
	if (texture_ready_callback.is_valid()) {
		texture_ready_callback.call(texture);
	}
}

void VkVideoOutput::detach() {
	if (!render_thread_service) {
		return;
	}

	if (texture.is_valid()) {
		texture->set_texture_rd_rid(RID());
		texture.unref();
	}
	render_thread_service->release_external_texture(external_texture);
	memdelete(render_thread_service);
	render_thread_service = nullptr;
	external_texture = RenderThreadService::ExternalTextureHandle();
	status = "vulkan video backend detached";
}

Ref<Texture2D> VkVideoOutput::get_texture() const {
	return texture;
}

String VkVideoOutput::get_status() const {
	if (render_thread_service) {
		return render_thread_service->get_status();
	}

	return status;
}

} // namespace libmpv_zero
