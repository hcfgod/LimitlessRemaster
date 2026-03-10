#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Limitless
{
    class Shader
    {
    public:
        virtual ~Shader() = default;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;

        virtual const std::string& GetName() const = 0;
        virtual uintptr_t GetNativeHandle() const = 0;

        static std::shared_ptr<Shader> CreateFromSource(
            const std::string& name,
            const std::string& vertexSource,
            const std::string& fragmentSource);
    };
}

