#pragma once

#include "mini_vulkan.h"

#include <cstddef>
#include <cstdint>

struct mpv_handle;
struct mpv_render_context;
struct mpv_render_param;

using mpv_render_param_type = int;
using mpv_render_update_fn = void (*)(void *cb_ctx);

constexpr mpv_render_param_type MPV_RENDER_PARAM_INVALID = 0;
constexpr mpv_render_param_type MPV_RENDER_PARAM_API_TYPE = 1;
constexpr mpv_render_param_type MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME = 12;
constexpr mpv_render_param_type MPV_RENDER_PARAM_VULKAN_INIT_PARAMS = 21;
constexpr mpv_render_param_type MPV_RENDER_PARAM_VULKAN_IMAGE = 22;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_SIZE = 17;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_FORMAT = 18;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_STRIDE = 19;
constexpr mpv_render_param_type MPV_RENDER_PARAM_SW_POINTER = 20;

constexpr uint64_t MPV_RENDER_UPDATE_FRAME = 1u << 0;

#define MPV_RENDER_API_TYPE_VULKAN "vulkan"
#define MPV_RENDER_API_TYPE_SW "sw"

struct mpv_vulkan_queue {
	uint32_t index;
	uint32_t count;
};

struct mpv_vulkan_init_params {
	VkInstance instance;
	PFN_vkGetInstanceProcAddr get_proc_addr;
	VkPhysicalDevice phys_device;
	VkDevice device;
	const char *const *extensions;
	int num_extensions;
	mpv_vulkan_queue queue_graphics;
	mpv_vulkan_queue queue_compute;
	mpv_vulkan_queue queue_transfer;
	const void *features;
	void (*lock_queue)(void *ctx, uint32_t qf, uint32_t qidx);
	void (*unlock_queue)(void *ctx, uint32_t qf, uint32_t qidx);
	void *queue_ctx;
	bool no_compute;
	int max_glsl_version;
	uint32_t max_api_version;
};

struct mpv_vulkan_image {
	VkImage image;
	VkImageAspectFlags aspect;
	int width;
	int height;
	int depth;
	VkFormat format;
	VkImageUsageFlags usage;
	VkImageLayout layout;
	uint32_t queue_family;
};

struct mpv_render_param {
	mpv_render_param_type type;
	void *data;
};

using PFN_mpv_render_context_create = int (*)(mpv_render_context **res, mpv_handle *mpv, mpv_render_param *params);
using PFN_mpv_render_context_set_update_callback = void (*)(mpv_render_context *ctx, mpv_render_update_fn callback, void *callback_ctx);
using PFN_mpv_render_context_update = uint64_t (*)(mpv_render_context *ctx);
using PFN_mpv_render_context_render = int (*)(mpv_render_context *ctx, mpv_render_param *params);
using PFN_mpv_render_context_free = void (*)(mpv_render_context *ctx);
