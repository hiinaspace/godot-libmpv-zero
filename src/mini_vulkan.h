#pragma once

#include <cstdint>

#ifdef _WIN32
#define VKAPI_CALL __stdcall
#define VKAPI_PTR VKAPI_CALL
#else
#define VKAPI_CALL
#define VKAPI_PTR
#endif

using VkFlags = uint32_t;
using VkBool32 = uint32_t;
using VkDeviceSize = uint64_t;
using VkResult = int32_t;
using VkStructureType = uint32_t;
using VkImageType = uint32_t;
using VkFormat = uint32_t;
using VkImageTiling = uint32_t;
using VkImageLayout = uint32_t;
using VkImageUsageFlags = VkFlags;
using VkImageCreateFlags = VkFlags;
using VkSampleCountFlagBits = uint32_t;
using VkSampleCountFlags = VkFlags;
using VkSharingMode = uint32_t;
using VkMemoryPropertyFlags = VkFlags;

struct VkInstance_T;
struct VkPhysicalDevice_T;
struct VkDevice_T;
struct VkQueue_T;
struct VkImage_T;
struct VkDeviceMemory_T;
struct VkAllocationCallbacks;

using VkInstance = VkInstance_T *;
using VkPhysicalDevice = VkPhysicalDevice_T *;
using VkDevice = VkDevice_T *;
using VkQueue = VkQueue_T *;
using VkImage = VkImage_T *;
using VkDeviceMemory = VkDeviceMemory_T *;

struct VkExtent3D {
	uint32_t width;
	uint32_t height;
	uint32_t depth;
};

struct VkImageCreateInfo {
	VkStructureType sType;
	const void *pNext;
	VkImageCreateFlags flags;
	VkImageType imageType;
	VkFormat format;
	VkExtent3D extent;
	uint32_t mipLevels;
	uint32_t arrayLayers;
	VkSampleCountFlagBits samples;
	VkImageTiling tiling;
	VkImageUsageFlags usage;
	VkSharingMode sharingMode;
	uint32_t queueFamilyIndexCount;
	const uint32_t *pQueueFamilyIndices;
	VkImageLayout initialLayout;
};

struct VkMemoryRequirements {
	VkDeviceSize size;
	VkDeviceSize alignment;
	uint32_t memoryTypeBits;
};

struct VkMemoryAllocateInfo {
	VkStructureType sType;
	const void *pNext;
	VkDeviceSize allocationSize;
	uint32_t memoryTypeIndex;
};

struct VkMemoryType {
	VkMemoryPropertyFlags propertyFlags;
	uint32_t heapIndex;
};

struct VkMemoryHeap {
	VkDeviceSize size;
	VkFlags flags;
};

struct VkPhysicalDeviceMemoryProperties {
	uint32_t memoryTypeCount;
	VkMemoryType memoryTypes[32];
	uint32_t memoryHeapCount;
	VkMemoryHeap memoryHeaps[16];
};

using PFN_vkVoidFunction = void (VKAPI_PTR *)(void);
using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction(VKAPI_PTR *)(VkInstance p_instance, const char *p_name);
using PFN_vkGetDeviceProcAddr = PFN_vkVoidFunction(VKAPI_PTR *)(VkDevice p_device, const char *p_name);
using PFN_vkGetPhysicalDeviceMemoryProperties = void(VKAPI_PTR *)(VkPhysicalDevice p_physical_device, VkPhysicalDeviceMemoryProperties *p_memory_properties);
using PFN_vkCreateImage = VkResult(VKAPI_PTR *)(VkDevice p_device, const VkImageCreateInfo *p_create_info, const VkAllocationCallbacks *p_allocator, VkImage *p_image);
using PFN_vkDestroyImage = void(VKAPI_PTR *)(VkDevice p_device, VkImage p_image, const VkAllocationCallbacks *p_allocator);
using PFN_vkGetImageMemoryRequirements = void(VKAPI_PTR *)(VkDevice p_device, VkImage p_image, VkMemoryRequirements *p_memory_requirements);
using PFN_vkAllocateMemory = VkResult(VKAPI_PTR *)(VkDevice p_device, const VkMemoryAllocateInfo *p_allocate_info, const VkAllocationCallbacks *p_allocator, VkDeviceMemory *p_memory);
using PFN_vkFreeMemory = void(VKAPI_PTR *)(VkDevice p_device, VkDeviceMemory p_memory, const VkAllocationCallbacks *p_allocator);
using PFN_vkBindImageMemory = VkResult(VKAPI_PTR *)(VkDevice p_device, VkImage p_image, VkDeviceMemory p_memory, VkDeviceSize p_memory_offset);

constexpr VkResult VK_SUCCESS = 0;

constexpr VkStructureType VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 5;
constexpr VkStructureType VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14;

constexpr VkImageLayout VK_IMAGE_LAYOUT_UNDEFINED = 0;

constexpr VkFormat VK_FORMAT_R8G8B8A8_UNORM = 37;

constexpr VkImageTiling VK_IMAGE_TILING_OPTIMAL = 0;
constexpr VkImageType VK_IMAGE_TYPE_2D = 1;
constexpr VkSharingMode VK_SHARING_MODE_EXCLUSIVE = 0;

constexpr VkSampleCountFlagBits VK_SAMPLE_COUNT_1_BIT = 0x00000001;

constexpr VkImageUsageFlags VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_SAMPLED_BIT = 0x00000004;
constexpr VkImageUsageFlags VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010;

constexpr VkMemoryPropertyFlags VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT = 0x00000001;
