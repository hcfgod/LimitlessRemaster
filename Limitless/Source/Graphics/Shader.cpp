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
        auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        switch (api)
        {
            case GraphicsAPI::OpenGL:
            default:
            {
                auto& renderer = Renderer::GetInstance();
                return renderer.SubmitResourceAndWait("CreateShader/FromSource", [&](GraphicsContext*) -> std::shared_ptr<Shader> {
                    return std::make_shared<OpenGLShader>(name, vertexSource, fragmentSource);
                });
            }
        }
    }
}

