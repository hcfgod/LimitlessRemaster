#pragma once

#include <memory>
#include <string>

namespace Limitless {

// Graphics API enum (must be declared before GraphicsContext)
enum class GraphicsAPI {
    OpenGL,
    Vulkan,
    DirectX,
    Metal,
};

class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;

    // Setup platform-specific attributes before window creation
    virtual void SetupAttributes() = 0;
    
    // Make the context current for this thread
    virtual void MakeCurrent() = 0;

    // Initialize graphics API (load functions, setup capabilities, etc.)
    virtual void Init(void* nativeWindow, GraphicsAPI api) = 0;

    // Swap the backbuffer to the screen
    virtual void SwapBuffers() = 0;

    // VSync control
    virtual bool SetVSync(bool enabled) = 0; // returns true if succeeded
    virtual bool IsVSync() const = 0;
};

// Factory
std::unique_ptr<GraphicsContext> CreateGraphicsContext();

// Helper conversion utilities
const char* GraphicsAPIToString(GraphicsAPI api);
GraphicsAPI GraphicsAPIFromString(const std::string& name);
} // namespace Limitless