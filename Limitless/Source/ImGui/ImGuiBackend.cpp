#include "ImGuiBackend.h"

#include "ImGuiOpenGLBackend.h"
#include "Graphics/GraphicsContext.h"
#include "Core/Debug/Log.h"

namespace Limitless
{
    std::unique_ptr<ImGuiBackend> ImGuiBackend::Create(GraphicsAPI api)
    {
        switch (api)
        {
            case GraphicsAPI::OpenGL:
                return std::make_unique<ImGuiOpenGLBackend>();

            case GraphicsAPI::Vulkan:
                LT_CORE_ERROR("ImGuiBackend: Vulkan backend not yet implemented.");
                return nullptr;

            case GraphicsAPI::DirectX:
                LT_CORE_ERROR("ImGuiBackend: DirectX backend not yet implemented.");
                return nullptr;

            case GraphicsAPI::Metal:
                LT_CORE_ERROR("ImGuiBackend: Metal backend not yet implemented.");
                return nullptr;

            default:
                LT_CORE_ERROR("ImGuiBackend: Unknown graphics API.");
                return nullptr;
        }
    }

}  // namespace Limitless
