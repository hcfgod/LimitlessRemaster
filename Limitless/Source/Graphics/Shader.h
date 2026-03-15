#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Limitless
{
    struct ShaderDescriptor;
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

        /// Create a shader from a ShaderDescriptor (SPIR-V canonical path).
        /// GLSL source → SPIR-V (via ShaderCompiler) → backend transpile → native compile.
        /// Reflection data in the descriptor is used for validation and binding layout.
        static std::shared_ptr<Shader> CreateFromDescriptor(const ShaderDescriptor& descriptor);
    };
}

