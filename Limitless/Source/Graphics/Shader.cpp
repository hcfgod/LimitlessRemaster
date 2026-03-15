#include "Shader.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Renderer.h"
#include "Graphics/ShaderDescriptor.h"
#include "Core/Debug/Log.h"

// Legacy fallback (used only when GraphicsDevice is not yet available during early init)
#include "Graphics/OpenGL/OpenGLShader.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<Shader> Shader::CreateFromSource(
        const std::string& name,
        const std::string& vertexSource,
        const std::string& fragmentSource)
    {
        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateShaderFromSource(name, vertexSource, fragmentSource);
        }

        // Legacy fallback: direct OpenGL construction
        const GraphicsAPI api = renderer.GetActiveAPI();
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                return renderer.SubmitResourceAndWait("CreateShader/FromSource", [&](GraphicsContext*) -> std::shared_ptr<Shader> {
                    return std::make_shared<OpenGLShader>(name, vertexSource, fragmentSource);
                });
            }
        }
    }

    std::shared_ptr<Shader> Shader::CreateFromDescriptor(const ShaderDescriptor& descriptor)
    {
        auto& renderer = Renderer::GetInstance();

        if (auto* device = renderer.GetDevice())
        {
            return device->CreateShaderFromDescriptor(descriptor);
        }

        LT_CORE_ERROR("Shader::CreateFromDescriptor: no GraphicsDevice available; cannot create shader '{}'", descriptor.DebugName);
        return nullptr;
    }
}

