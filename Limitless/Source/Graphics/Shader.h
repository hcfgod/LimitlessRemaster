#pragma once

#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace Limitless
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        // Minimal uniform helpers (enough for textures and simple demos).
        virtual void SetInt(const std::string& name, int value) = 0;
        virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

        virtual const std::string& GetName() const = 0;

        static std::shared_ptr<Shader> CreateFromSource(
            const std::string& name,
            const std::string& vertexSource,
            const std::string& fragmentSource);
    };
}

