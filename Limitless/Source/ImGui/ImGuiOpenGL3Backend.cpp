/**
 * @file ImGuiOpenGL3Backend.cpp
 * @brief Wrapper that ensures GLAD is loaded before the ImGui OpenGL3 backend.
 *
 * ImGui's OpenGL3 backend requires OpenGL symbols (GLuint, glTexSubImage2D, etc.)
 * to be available when IMGUI_IMPL_OPENGL_LOADER_CUSTOM is defined. Including GLAD
 * first guarantees this on all platforms (Windows, Linux, macOS). On Linux/gmake2,
 * premake's forceincludes can be unreliable for vendor files, so this wrapper is
 * the most robust solution.
 */

#include "glad/glad.h"
#include "imgui/backends/imgui_impl_opengl3.cpp"
