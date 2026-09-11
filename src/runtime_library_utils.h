#pragma once

#include <godot_cpp/variant/string.hpp>

namespace libmpv_zero {

void *open_mpv_runtime(godot::String &r_path, godot::String &r_error);
void *open_vulkan_runtime(godot::String &r_error);
void *runtime_symbol(void *p_library, const char *p_name);
void close_runtime(void *p_library);

} // namespace libmpv_zero
