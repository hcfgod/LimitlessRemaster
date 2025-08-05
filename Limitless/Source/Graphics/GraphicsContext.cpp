// #include "ltpch.h" TODO: implement a pch file for the project
#include "GraphicsContext.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Core/Debug/Log.h"
#include "OpenGL/OpenGLContext.h"
#include <SDL3/SDL.h>

namespace Limitless {
    std::unique_ptr<GraphicsContext> CreateGraphicsContext() 
    {
        // Check if detection system is initialized
        if (!GraphicsAPIDetector::IsInitialized()) {
            LT_CORE_ERROR("Graphics API Detection not initialized! Call GraphicsAPIDetector::Initialize() first.");
            LT_CORE_WARN("Falling back to OpenGL context creation without detection");
            return std::make_unique<OpenGLContext>();
        }
        
        // Get the best available graphics API
        auto bestAPI = GraphicsAPIDetector::GetBestAPI();
        if (!bestAPI) {
            LT_CORE_ERROR("No suitable graphics API found! Falling back to OpenGL");
            return std::make_unique<OpenGLContext>();
        }
        
        GraphicsAPI selectedAPI = bestAPI.value();
        LT_CORE_INFO("Selected graphics API: {}", GraphicsAPIToString(selectedAPI));
        
        // Get detailed information about the selected API
        auto apiInfo = GraphicsAPIDetector::GetAPI(selectedAPI);
        if (apiInfo) {
            LT_CORE_DEBUG("API Version: {}", apiInfo->version.ToString());
            LT_CORE_DEBUG("API Vendor: {}", apiInfo->version.vendor);
            LT_CORE_DEBUG("API Renderer: {}", apiInfo->version.renderer);
        }
        
        // Create context based on selected API
        switch (selectedAPI) {
            case GraphicsAPI::OpenGL:
                LT_CORE_DEBUG("Creating OpenGL graphics context");
                return std::make_unique<OpenGLContext>();
            case GraphicsAPI::Vulkan:
                LT_CORE_DEBUG("Creating Vulkan graphics context");
                // TODO: Implement VulkanContext
                LT_CORE_WARN("Vulkan context not yet implemented, falling back to OpenGL");
                return std::make_unique<OpenGLContext>();
            case GraphicsAPI::DirectX:
                LT_CORE_DEBUG("Creating DirectX graphics context");
                // TODO: Implement DirectXContext
                LT_CORE_WARN("DirectX context not yet implemented, falling back to OpenGL");
                return std::make_unique<OpenGLContext>();
            case GraphicsAPI::Metal:
                LT_CORE_DEBUG("Creating Metal graphics context");
                // TODO: Implement MetalContext
                LT_CORE_WARN("Metal context not yet implemented, falling back to OpenGL");
                return std::make_unique<OpenGLContext>();
            default:
                LT_CORE_ERROR("Unknown graphics API '{}', falling back to OpenGL", GraphicsAPIToString(selectedAPI));
                return std::make_unique<OpenGLContext>();
        }
    }

    const char* GraphicsAPIToString(GraphicsAPI api) {
        switch (api) {
            case GraphicsAPI::OpenGL:
                return "OpenGL";
            case GraphicsAPI::Vulkan:
                return "Vulkan";
            case GraphicsAPI::DirectX:
                return "DirectX";
            case GraphicsAPI::Metal:
                return "Metal";
            default:
                return "Unknown";
        }
    }

    GraphicsAPI GraphicsAPIFromString(const std::string& name) {
        if (name == "OpenGL" || name == "opengl" || name == "GL")
            return GraphicsAPI::OpenGL;
        if (name == "Vulkan" || name == "vulkan" || name == "VK")
            return GraphicsAPI::Vulkan;
        if (name == "DirectX" || name == "directx" || name == "DX" || name == "D3D")
            return GraphicsAPI::DirectX;
        if (name == "Metal" || name == "metal" || name == "MTL")
            return GraphicsAPI::Metal;

        LT_CORE_WARN("Unknown graphics API string '{}', falling back to OpenGL", name);
        return GraphicsAPI::OpenGL;
    }
} // namespace Limitless