// #include "ltpch.h" TODO: implement a pch file for the project
#include "OpenGLContext.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Core/Debug/Log.h"

#define LT_USE_GLAD

// Optionally include glad if available
#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

namespace Limitless {
    OpenGLContext::OpenGLContext() : m_Window(nullptr), m_Context(nullptr) {
        // Window will be set during Init()
    }

    OpenGLContext::~OpenGLContext() {
        if (m_Context) {
            LT_CORE_INFO("Destroying OpenGL context");
            SDL_GL_DestroyContext(m_Context);
            m_Context = nullptr;
        }
    }

    void OpenGLContext::SetupAttributes()
     {
        LT_CORE_INFO("Setting up OpenGL attributes");
 
        // Get the best supported OpenGL version from detection system
        auto [bestMajor, bestMinor] = GraphicsAPIDetector::GetBestSupportedOpenGLVersion();
        
        // Store the best version for later use
        m_RequestMajor = bestMajor;
        m_RequestMinor = bestMinor;
        
        LT_CORE_INFO("Setting up OpenGL attributes for version {}.{}", m_RequestMajor, m_RequestMinor);
        
        // Set OpenGL attributes for the best supported version
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, m_RequestMajor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, m_RequestMinor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    }

    void OpenGLContext::MakeCurrent() {
        if (!m_Window) {
            LT_CORE_CRITICAL("Cannot make OpenGL context current: window is null");
            throw std::runtime_error("Window is null");
        }
        
        LT_CORE_INFO("Making OpenGL context current");
        
        // SDL_GL_MakeCurrent returns 0 on success, non-zero on failure
        if (!SDL_GL_MakeCurrent(m_Window, m_Context)) {
            LT_CORE_CRITICAL("Could not make GL context current: {}", SDL_GetError());
            throw std::runtime_error("Failed to make OpenGL context current");
        }
        
        LT_CORE_INFO("OpenGL context is now current");
    }

    void OpenGLContext::Init(void* nativeWindow, GraphicsAPI api) {
        if (api != GraphicsAPI::OpenGL) {
            LT_CORE_CRITICAL("OpenGLContext cannot initialize non-OpenGL API");
            throw std::runtime_error("Invalid graphics API for OpenGL context");
        }
        
        m_Window = static_cast<SDL_Window*>(nativeWindow);
        
        if (!m_Window) {
            LT_CORE_CRITICAL("Cannot initialize OpenGL: window is null");
            throw std::runtime_error("Window is null");
        }

        LT_CORE_INFO("Initializing OpenGL");
        
        // Note: SetupAttributes() should be called before Init() to set the best OpenGL version
        // If SetupAttributes() wasn't called, we'll use the default values (4, 5)

        // Note: GraphicsAPIDetector::Initialize() should be called once at application startup
        // We don't call it here to avoid redundant initialization
        
        // Get OpenGL capabilities from detection system
        auto openGLCaps = GraphicsAPIDetector::GetAPI(GraphicsAPI::OpenGL);
        
        if (!openGLCaps || !openGLCaps->isSupported) {
            LT_CORE_CRITICAL("OpenGL is not supported on this system");
            throw std::runtime_error("OpenGL not supported");
        }

        // Create the OpenGL context
        LT_CORE_INFO("Creating OpenGL context");

        // Create context using attributes already set up in SetupAttributes()
        m_Context = SDL_GL_CreateContext(m_Window);
        
        if (!m_Context) {
            LT_CORE_ERROR("Failed to create OpenGL context: {}", SDL_GetError());
            throw std::runtime_error("Failed to create OpenGL context");
        }

        LT_CORE_INFO("Successfully created OpenGL context with requested version {}.{}", m_RequestMajor, m_RequestMinor);

        // Load OpenGL functions via GLAD (if enabled) using SDL loader
    #ifdef LT_USE_GLAD
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            LT_CORE_CRITICAL("Failed to load OpenGL functions via GLAD");
            throw std::runtime_error("gladLoadGLLoader failed");
        }
        LT_CORE_INFO("OpenGL loaded: version {}.{}", GLVersion.major, GLVersion.minor);
    #else
        LT_CORE_INFO("GLAD not enabled; assuming system headers provide required symbols");
    #endif

        // Log the actual OpenGL version that was created
        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        
        LT_CORE_INFO("OpenGL context created successfully: {}.{} ({})", 
                     GLVersion.major, GLVersion.minor, renderer ? renderer : "Unknown");
        
        // Update the graphics API detection system with the actual context information
        // This ensures the detection system has accurate information about the created context
        if (version && vendor && renderer) {
            GraphicsAPIDetector::UpdateOpenGLInfo(version, vendor, renderer);
            LT_CORE_INFO("Updated GraphicsAPIDetector with actual OpenGL info: {} ({})", version, renderer);
        }
        
        // Log feature availability based on the created context
        LT_CORE_INFO("OpenGL context created with version {}.{}", GLVersion.major, GLVersion.minor);
        
        if (GLVersion.major >= 4 && GLVersion.minor >= 5) {
            LT_CORE_INFO("Full OpenGL 4.5 support - all modern features available");
        } else if (GLVersion.major >= 4 && GLVersion.minor >= 3) {
            LT_CORE_INFO("OpenGL 4.3+ support - compute shaders and multi-draw indirect available");
        } else if (GLVersion.major >= 4 && GLVersion.minor >= 0) {
            LT_CORE_INFO("OpenGL 4.0+ support - tessellation shaders available");
        } else if (GLVersion.major >= 3 && GLVersion.minor >= 3) {
            LT_CORE_INFO("OpenGL 3.3+ support - instanced rendering available");
        } else if (GLVersion.major >= 3 && GLVersion.minor >= 2) {
            LT_CORE_INFO("OpenGL 3.2+ support - geometry shaders available");
        } else {
            LT_CORE_WARN("Running on OpenGL {}.{}, certain advanced features will be disabled.",
                         GLVersion.major, GLVersion.minor);
        }

        // Default to VSync enabled as before (can be changed later)
        SetVSync(true);
        
        LT_CORE_INFO("OpenGL initialization completed successfully");
    }

    void OpenGLContext::SwapBuffers() {
        SDL_GL_SwapWindow(m_Window);
    }

    bool OpenGLContext::SetVSync(bool enabled) {
        bool result = SDL_GL_SetSwapInterval(enabled ? 1 : 0);
        if (!result) {
            LT_CORE_ERROR("Failed to set swap interval {}: {}", enabled, SDL_GetError());
            m_VSyncActuallyEnabled = false;
            return false;
        }

        int interval;
        if (SDL_GL_GetSwapInterval(&interval)) {
            m_VSyncActuallyEnabled = (interval > 0);
            LT_CORE_INFO("VSync {} (swap interval {})", m_VSyncActuallyEnabled ? "enabled" : "disabled", interval);
        } else {
            m_VSyncActuallyEnabled = enabled;
            LT_CORE_WARN("Could not query swap interval after setting VSync");
        }
        return true;
    }
} // namespace Limitless