#include "phase0_texture_probe.h"
#include "mini_vulkan.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace godot;

namespace {

struct VulkanDispatch {
	HMODULE library = nullptr;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr = nullptr;
	PFN_vkGetDeviceProcAddr get_device_proc_addr = nullptr;
	PFN_vkGetPhysicalDeviceMemoryProperties get_physical_device_memory_properties = nullptr;
	PFN_vkCreateImage create_image = nullptr;
	PFN_vkDestroyImage destroy_image = nullptr;
	PFN_vkGetImageMemoryRequirements get_image_memory_requirements = nullptr;
	PFN_vkAllocateMemory allocate_memory = nullptr;
	PFN_vkFreeMemory free_memory = nullptr;
	PFN_vkBindImageMemory bind_image_memory = nullptr;
};

template <typename T>
T vk_load_instance_proc(PFN_vkGetInstanceProcAddr p_get_instance_proc_addr, VkInstance p_instance, const char *p_name) {
	return reinterpret_cast<T>(p_get_instance_proc_addr(p_instance, p_name));
}

template <typename T>
T vk_load_device_proc(PFN_vkGetDeviceProcAddr p_get_device_proc_addr, VkDevice p_device, const char *p_name) {
	return reinterpret_cast<T>(p_get_device_proc_addr(p_device, p_name));
}

bool vk_load_dispatch(VkInstance p_instance, VkPhysicalDevice p_physical_device, VkDevice p_device, VulkanDispatch &r_dispatch, String &r_error) {
#ifdef _WIN32
	r_dispatch.library = LoadLibraryW(L"vulkan-1.dll");
	if (!r_dispatch.library) {
		r_error = "failed to load vulkan-1.dll";
		return false;
	}

	r_dispatch.get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(r_dispatch.library, "vkGetInstanceProcAddr"));
	if (!r_dispatch.get_instance_proc_addr) {
		r_error = "failed to load vkGetInstanceProcAddr";
		FreeLibrary(r_dispatch.library);
		r_dispatch.library = nullptr;
		return false;
	}

	r_dispatch.get_device_proc_addr = vk_load_instance_proc<PFN_vkGetDeviceProcAddr>(r_dispatch.get_instance_proc_addr, p_instance, "vkGetDeviceProcAddr");
	r_dispatch.get_physical_device_memory_properties = vk_load_instance_proc<PFN_vkGetPhysicalDeviceMemoryProperties>(r_dispatch.get_instance_proc_addr, p_instance, "vkGetPhysicalDeviceMemoryProperties");
	r_dispatch.create_image = vk_load_device_proc<PFN_vkCreateImage>(r_dispatch.get_device_proc_addr, p_device, "vkCreateImage");
	r_dispatch.destroy_image = vk_load_device_proc<PFN_vkDestroyImage>(r_dispatch.get_device_proc_addr, p_device, "vkDestroyImage");
	r_dispatch.get_image_memory_requirements = vk_load_device_proc<PFN_vkGetImageMemoryRequirements>(r_dispatch.get_device_proc_addr, p_device, "vkGetImageMemoryRequirements");
	r_dispatch.allocate_memory = vk_load_device_proc<PFN_vkAllocateMemory>(r_dispatch.get_device_proc_addr, p_device, "vkAllocateMemory");
	r_dispatch.free_memory = vk_load_device_proc<PFN_vkFreeMemory>(r_dispatch.get_device_proc_addr, p_device, "vkFreeMemory");
	r_dispatch.bind_image_memory = vk_load_device_proc<PFN_vkBindImageMemory>(r_dispatch.get_device_proc_addr, p_device, "vkBindImageMemory");

	if (!r_dispatch.get_device_proc_addr || !r_dispatch.get_physical_device_memory_properties || !r_dispatch.create_image || !r_dispatch.destroy_image || !r_dispatch.get_image_memory_requirements || !r_dispatch.allocate_memory || !r_dispatch.free_memory || !r_dispatch.bind_image_memory) {
		r_error = "failed to resolve required Vulkan function pointers";
		FreeLibrary(r_dispatch.library);
		r_dispatch = VulkanDispatch();
		return false;
	}

	return true;
#else
	r_error = "Phase 0 Vulkan probe currently only supports Windows";
	return false;
#endif
}

void vk_unload_dispatch(VulkanDispatch &r_dispatch) {
#ifdef _WIN32
	if (r_dispatch.library) {
		FreeLibrary(r_dispatch.library);
	}
#endif
	r_dispatch = VulkanDispatch();
}

bool vk_find_memory_type(const VkPhysicalDeviceMemoryProperties &p_memory_properties, uint32_t p_memory_type_bits, VkMemoryPropertyFlags p_required_flags, uint32_t &r_memory_type_index) {
	for (uint32_t i = 0; i < p_memory_properties.memoryTypeCount; ++i) {
		const bool supported = (p_memory_type_bits & (1u << i)) != 0;
		const bool matches_flags = (p_memory_properties.memoryTypes[i].propertyFlags & p_required_flags) == p_required_flags;
		if (supported && matches_flags) {
			r_memory_type_index = i;
			return true;
		}
	}

	return false;
}

void vk_destroy_external_image(const VulkanDispatch &p_dispatch, VkDevice p_device, VkImage &r_image, VkDeviceMemory &r_memory) {
	if (r_image && p_dispatch.destroy_image) {
		p_dispatch.destroy_image(p_device, r_image, nullptr);
		r_image = nullptr;
	}

	if (r_memory && p_dispatch.free_memory) {
		p_dispatch.free_memory(p_device, r_memory, nullptr);
		r_memory = nullptr;
	}
}

} // namespace

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
	UtilityFunctions::print("Phase0TextureProbe: queueing render-thread probe");
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
	set_process(false);
	if (published_texture.is_valid()) {
		published_texture->set_texture_rd_rid(RID());
		published_texture.unref();
	}

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
	UtilityFunctions::print("Phase0TextureProbe: publish status = ", status);
	if (!publish.success) {
		set_process(false);
		emit_signal("probe_failed", status);
		return;
	}

	wrapped_texture_rid = publish.wrapped_texture;
	logical_device_handle = publish.logical_device;
	image_handle = publish.image_handle;
	image_memory_handle = publish.image_memory_handle;

	if (published_texture.is_null()) {
		published_texture.instantiate();
	}

	UtilityFunctions::print("Phase0TextureProbe: assigning Texture2DRD RID");
	published_texture->set_texture_rd_rid(wrapped_texture_rid);
	set_process(false);
	UtilityFunctions::print("Phase0TextureProbe: texture ready signal");
	emit_signal("texture_ready", published_texture);
}

void Phase0TextureProbe::_create_probe_texture_on_render_thread() {
	UtilityFunctions::print("Phase0TextureProbe: render-thread probe start");
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

	const BitField<RenderingDevice::TextureUsageBits> usage_bits =
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT;

	VkInstance instance = reinterpret_cast<VkInstance>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, RID(), 0));
	VkPhysicalDevice physical_device = reinterpret_cast<VkPhysicalDevice>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE, RID(), 0));
	VkDevice device = reinterpret_cast<VkDevice>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_DEVICE, RID(), 0));
	UtilityFunctions::print("Phase0TextureProbe: queried Vulkan handles");

	if (!instance || !physical_device || !device) {
		result.status = "failed to query Godot Vulkan handles";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	VulkanDispatch dispatch;
	String dispatch_error;
	if (!vk_load_dispatch(instance, physical_device, device, dispatch, dispatch_error)) {
		result.status = dispatch_error;
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	VkImage image = nullptr;
	VkDeviceMemory image_memory = nullptr;

	const VkImageCreateInfo image_create_info = {
			VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
			nullptr,
			0,
			VK_IMAGE_TYPE_2D,
			VK_FORMAT_R8G8B8A8_UNORM,
			{ probe_width, probe_height, 1 },
			1,
			1,
			VK_SAMPLE_COUNT_1_BIT,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_SHARING_MODE_EXCLUSIVE,
			0,
			nullptr,
			VK_IMAGE_LAYOUT_UNDEFINED
	};

	if (dispatch.create_image(device, &image_create_info, nullptr, &image) != VK_SUCCESS || !image) {
		vk_unload_dispatch(dispatch);
		result.status = "vkCreateImage failed";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}
	UtilityFunctions::print("Phase0TextureProbe: vkCreateImage ok");

	VkMemoryRequirements memory_requirements = {};
	dispatch.get_image_memory_requirements(device, image, &memory_requirements);

	VkPhysicalDeviceMemoryProperties memory_properties = {};
	dispatch.get_physical_device_memory_properties(physical_device, &memory_properties);

	uint32_t memory_type_index = 0;
	if (!vk_find_memory_type(memory_properties, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_type_index)) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "failed to find Vulkan memory type";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	const VkMemoryAllocateInfo memory_allocate_info = {
			VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
			nullptr,
			memory_requirements.size,
			memory_type_index
	};

	if (dispatch.allocate_memory(device, &memory_allocate_info, nullptr, &image_memory) != VK_SUCCESS || !image_memory) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "vkAllocateMemory failed";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}
	UtilityFunctions::print("Phase0TextureProbe: vkAllocateMemory ok");

	if (dispatch.bind_image_memory(device, image, image_memory, 0) != VK_SUCCESS) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "vkBindImageMemory failed";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}
	UtilityFunctions::print("Phase0TextureProbe: vkBindImageMemory ok");

	result.logical_device = reinterpret_cast<uint64_t>(device);
	result.image_handle = reinterpret_cast<uint64_t>(image);
	result.image_memory_handle = reinterpret_cast<uint64_t>(image_memory);

	result.wrapped_texture = rendering_device->texture_create_from_extension(
			RenderingDevice::TEXTURE_TYPE_2D,
			RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
			RenderingDevice::TEXTURE_SAMPLES_1,
			usage_bits,
			reinterpret_cast<uint64_t>(image),
			probe_width,
			probe_height,
			1,
			1);
	UtilityFunctions::print("Phase0TextureProbe: texture_create_from_extension returned");

	if (!rendering_device->texture_is_valid(result.wrapped_texture)) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "failed to wrap external texture from Vulkan image";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}
	UtilityFunctions::print("Phase0TextureProbe: wrapped texture valid");

	const Error clear_error = rendering_device->texture_clear(result.wrapped_texture, Color(1.0, 0.0, 1.0, 1.0), 0, 1, 0, 1);
	UtilityFunctions::print("Phase0TextureProbe: texture_clear returned ", clear_error);
	if (clear_error != OK) {
		rendering_device->free_rid(result.wrapped_texture);
		result.wrapped_texture = RID();
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "failed to clear wrapped external texture";
		std::lock_guard<std::mutex> lock(pending_mutex);
		pending_publish = result;
		return;
	}

	vk_unload_dispatch(dispatch);
	result.success = true;
	result.status = "phase 0 texture probe ready";
	UtilityFunctions::print("Phase0TextureProbe: render-thread probe success");
	std::lock_guard<std::mutex> lock(pending_mutex);
	pending_publish = result;
}

void Phase0TextureProbe::_cleanup_render_resources_on_render_thread() {
	UtilityFunctions::print("Phase0TextureProbe: cleanup start");
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (!rendering_server) {
		return;
	}

	RenderingDevice *rendering_device = rendering_server->get_rendering_device();
	if (!rendering_device) {
		return;
	}

	VkDevice device = reinterpret_cast<VkDevice>(logical_device_handle);

	VulkanDispatch dispatch;
	String dispatch_error;
	const bool have_dispatch = device && vk_load_dispatch(
			reinterpret_cast<VkInstance>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_INSTANCE, RID(), 0)),
			reinterpret_cast<VkPhysicalDevice>(rendering_device->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_VULKAN_PHYSICAL_DEVICE, RID(), 0)),
			device,
			dispatch,
			dispatch_error);

	{
		std::lock_guard<std::mutex> lock(pending_mutex);
		if (pending_publish.wrapped_texture.is_valid()) {
			rendering_device->free_rid(pending_publish.wrapped_texture);
			pending_publish.wrapped_texture = RID();
		}
		pending_publish.image_handle = 0;
		pending_publish.image_memory_handle = 0;
		pending_publish.ready = false;
	}

	if (wrapped_texture_rid.is_valid()) {
		rendering_device->free_rid(wrapped_texture_rid);
		wrapped_texture_rid = RID();
	}
	if (have_dispatch) {
		vk_unload_dispatch(dispatch);
	}

	// TODO: Destroy the external VkImage/VkDeviceMemory after explicit GPU-idle or
	// multi-frame retirement. Destroying it here races Godot's in-flight work.
	image_handle = 0;
	image_memory_handle = 0;
	logical_device_handle = 0;
}
