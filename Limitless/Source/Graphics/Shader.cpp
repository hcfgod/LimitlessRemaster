#include "Shader.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLShader.h"
#include "Graphics/Renderer.h"
#include "Graphics/OpenGL/OpenGLContext.h"

namespace Limitless
{
    std::shared_ptr<Shader> Shader::CreateFromSource(
        const std::string& name,
        const std::string& vertexSource,
        const std::string& fragmentSource)
    {
        auto& renderer = Renderer::GetInstance();
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
}

