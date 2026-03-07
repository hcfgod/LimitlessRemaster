#pragma once

#include <cstdint>
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
        virtual void SetIntArray(const std::string& name, const int* values, uint32_t count) = 0;
        virtual void SetFloat(const std::string& name, float value) = 0;
        virtual void SetFloat2(const std::string& name, const glm::vec2& value) = 0;
        virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
        virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
        virtual void SetFloat2Array(const std::string& name, const glm::vec2* values, uint32_t count) = 0;
        virtual void SetFloat4Array(const std::string& name, const glm::vec4* values, uint32_t count) = 0;
        virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

        virtual const std::string& GetName() const = 0;
        virtual uintptr_t GetNativeHandle() const = 0;

        static std::shared_ptr<Shader> CreateFromSource(
            const std::string& name,
            const std::string& vertexSource,
            const std::string& fragmentSource);
    };
}

