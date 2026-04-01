#include "vk_video_output.h"

#include "phase0_texture_probe.h"

#include <godot_cpp/core/memory.hpp>

using namespace godot;

namespace libmpv_zero {

void VkVideoOutput::attach(Node *p_owner, MpvCore * /*p_mpv_core*/, const Callable &p_texture_ready, const Callable &p_probe_failed) {
	if (probe) {
		return;
	}

	owner = p_owner;
	probe = memnew(Phase0TextureProbe);
	probe->set_name("Phase0TextureProbe");
	owner->add_child(probe, false, Node::INTERNAL_MODE_FRONT);
	probe->connect("texture_ready", p_texture_ready);
	probe->connect("probe_failed", p_probe_failed);
	status = "vulkan probe attached";
}

void VkVideoOutput::update() {
}

void VkVideoOutput::detach() {
	if (!probe) {
		return;
	}

	if (Object::cast_to<Node>(probe->get_parent()) == owner) {
		owner->remove_child(probe);
	}
	probe->queue_free();
	probe = nullptr;
	owner = nullptr;
	status = "vulkan video backend detached";
}

Ref<Texture2D> VkVideoOutput::get_texture() const {
	if (!probe) {
		return Ref<Texture2D>();
	}

	return probe->get_texture();
}

String VkVideoOutput::get_status() const {
	if (probe) {
		return probe->get_status();
	}

	return status;
}

} // namespace libmpv_zero
