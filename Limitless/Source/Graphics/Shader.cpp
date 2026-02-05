#include "Shader.h"
#include "Graphics/GraphicsAPIDetector.h"
#include "Graphics/OpenGL/OpenGLShader.h"

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
            case GraphicsAPI::OpenGL: return std::make_shared<OpenGLShader>(name, vertexSource, fragmentSource);
            default:                  return std::make_shared<OpenGLShader>(name, vertexSource, fragmentSource);
        }
    }
}

