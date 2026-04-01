#include "render_thread_service.h"

#include "mini_vulkan.h"

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace godot;

namespace libmpv_zero {

namespace {

struct VulkanDispatch {
#ifdef _WIN32
	HMODULE library = nullptr;
#endif
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

bool vk_load_release_dispatch(VkDevice p_device, VulkanDispatch &r_dispatch, String &r_error) {
#ifdef _WIN32
	r_dispatch.library = LoadLibraryW(L"vulkan-1.dll");
	if (!r_dispatch.library) {
		r_error = "failed to load vulkan-1.dll";
		return false;
	}

	r_dispatch.get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(r_dispatch.library, "vkGetDeviceProcAddr"));
	if (!r_dispatch.get_device_proc_addr) {
		r_error = "failed to load vkGetDeviceProcAddr";
		FreeLibrary(r_dispatch.library);
		r_dispatch.library = nullptr;
		return false;
	}

	r_dispatch.destroy_image = vk_load_device_proc<PFN_vkDestroyImage>(r_dispatch.get_device_proc_addr, p_device, "vkDestroyImage");
	r_dispatch.free_memory = vk_load_device_proc<PFN_vkFreeMemory>(r_dispatch.get_device_proc_addr, p_device, "vkFreeMemory");
	if (!r_dispatch.destroy_image || !r_dispatch.free_memory) {
		r_error = "failed to resolve Vulkan release functions";
		FreeLibrary(r_dispatch.library);
		r_dispatch = VulkanDispatch();
		return false;
	}

	return true;
#else
	r_error = "render thread service currently only supports Windows";
	return false;
#endif
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
	r_error = "render thread service currently only supports Windows";
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

void RenderThreadService::_bind_methods() {
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
		if (release_request.active) {
			status = "release request already in flight";
			return;
		}

		release_request.active = true;
		release_request.wrapped_texture = p_handle.wrapped_texture;
		release_request.logical_device = p_handle.logical_device;
		release_request.image_handle = p_handle.image_handle;
		release_request.image_memory_handle = p_handle.image_memory_handle;
	}

	status = "queued external texture release";
	rendering_server->call_on_render_thread(callable_mp(this, &RenderThreadService::_release_external_texture_on_render_thread));
}

bool RenderThreadService::has_pending_work() const {
	std::lock_guard<std::mutex> lock(mutex);
	return create_request.active || release_request.active;
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

	VulkanDispatch dispatch;
	String dispatch_error;
	if (!vk_load_dispatch(instance, physical_device, device, dispatch, dispatch_error)) {
		result.status = dispatch_error;
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
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
		{ request.width, request.height, 1 },
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
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	VkMemoryRequirements memory_requirements = {};
	dispatch.get_image_memory_requirements(device, image, &memory_requirements);

	VkPhysicalDeviceMemoryProperties memory_properties = {};
	dispatch.get_physical_device_memory_properties(physical_device, &memory_properties);

	uint32_t memory_type_index = 0;
	if (!vk_find_memory_type(memory_properties, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_type_index)) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "failed to find Vulkan memory type";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
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
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	if (dispatch.bind_image_memory(device, image, image_memory, 0) != VK_SUCCESS) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "vkBindImageMemory failed";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	result.instance_handle = reinterpret_cast<uint64_t>(instance);
	result.physical_device_handle = reinterpret_cast<uint64_t>(physical_device);
	result.logical_device = reinterpret_cast<uint64_t>(device);
	result.queue_handle = reinterpret_cast<uint64_t>(queue);
	result.queue_family_index = queue_family_index;
	result.image_handle = reinterpret_cast<uint64_t>(image);
	result.image_memory_handle = reinterpret_cast<uint64_t>(image_memory);

	result.wrapped_texture = rendering_device->texture_create_from_extension(
			RenderingDevice::TEXTURE_TYPE_2D,
			RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
			RenderingDevice::TEXTURE_SAMPLES_1,
			usage_bits,
			reinterpret_cast<uint64_t>(image),
			request.width,
			request.height,
			1,
			1);

	if (!rendering_device->texture_is_valid(result.wrapped_texture)) {
		vk_destroy_external_image(dispatch, device, image, image_memory);
		vk_unload_dispatch(dispatch);
		result.status = "failed to wrap external texture from Vulkan image";
		std::lock_guard<std::mutex> lock(mutex);
		pending_result = result;
		return;
	}

	if (request.clear_texture) {
		const Error clear_error = rendering_device->texture_clear(result.wrapped_texture, request.clear_color, 0, 1, 0, 1);
		if (clear_error != OK) {
			rendering_device->free_rid(result.wrapped_texture);
			result.wrapped_texture = RID();
			vk_destroy_external_image(dispatch, device, image, image_memory);
			vk_unload_dispatch(dispatch);
			result.status = "failed to clear wrapped external texture";
			std::lock_guard<std::mutex> lock(mutex);
			pending_result = result;
			return;
		}
	}

	vk_unload_dispatch(dispatch);
	result.success = true;
	result.status = "external texture ready";
	std::lock_guard<std::mutex> lock(mutex);
	pending_result = result;
}

void RenderThreadService::_release_external_texture_on_render_thread() {
	PendingReleaseRequest release;
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = release_request;
		release_request = PendingReleaseRequest();
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

	if (release.image_handle || release.image_memory_handle) {
		VulkanDispatch dispatch;
		String error;
		VkDevice device = reinterpret_cast<VkDevice>(release.logical_device);
		if (device && vk_load_release_dispatch(device, dispatch, error)) {
			VkImage image = reinterpret_cast<VkImage>(release.image_handle);
			VkDeviceMemory memory = reinterpret_cast<VkDeviceMemory>(release.image_memory_handle);
			vk_destroy_external_image(dispatch, device, image, memory);
			vk_unload_dispatch(dispatch);
		}
	}
}

} // namespace libmpv_zero
