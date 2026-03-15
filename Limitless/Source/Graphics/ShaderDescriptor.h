#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Limitless
{
    /// Shader stage identifier (mirrors ShaderCompilation::ShaderStage but lives
    /// in the engine-level Graphics namespace so descriptors don't depend on the
    /// compilation toolchain headers).
    enum class ShaderStage : uint8_t
    {
        Vertex   = 0,
        Fragment = 1,
        Compute  = 2
    };

    /// Describes a single resource binding discovered via SPIR-V reflection.
    struct ShaderResourceEntry
    {
        enum class Type : uint8_t
        {
            UniformBuffer,
            StorageBuffer,
            SampledImage,   // combined image+sampler (texture)
            SeparateImage,
            SeparateSampler,
            PushConstant
        };

        std::string Name;
        Type        ResourceType = Type::UniformBuffer;
        uint32_t    Set          = 0;   // descriptor set (Vulkan/DX) — 0 for OpenGL
        uint32_t    Binding      = 0;   // binding index
        uint32_t    ArraySize    = 1;   // >1 for arrays of textures/buffers
        uint32_t    BlockSize    = 0;   // byte size for uniform/storage buffers; 0 otherwise
        ShaderStage StageVisibility = ShaderStage::Vertex;
    };

    /// Reflection data extracted from SPIR-V at shader creation time.
    ///
    /// This is the authoritative source of "what resources does this shader need?"
    /// and is used by the engine to build ResourceBindingLayouts automatically.
    struct ShaderReflection
    {
        std::vector<ShaderResourceEntry> Resources;

        // Vertex input attributes (location → name mapping for diagnostics)
        struct VertexInput
        {
            uint32_t    Location = 0;
            std::string Name;
        };
        std::vector<VertexInput> VertexInputs;
    };

    /// Source data for a single shader stage.
    ///
    /// The canonical path is SPIR-V: populate `SPIRV` and leave `NativeSource`
    /// empty. The OpenGL backend will transpile SPIR-V → GLSL internally.
    ///
    /// The legacy path is raw GLSL: populate `NativeSource` and leave `SPIRV`
    /// empty. This is supported for backward compatibility but bypasses
    /// validation and reflection.
    struct ShaderStageSource
    {
        ShaderStage             Stage = ShaderStage::Vertex;
        std::vector<uint32_t>   SPIRV;          // canonical (preferred)
        std::string             NativeSource;   // optional fallback (GLSL/HLSL/MSL)
    };

    /// Complete description of a shader program, suitable for passing to
    /// `GraphicsDevice::CreateShaderFromDescriptor()`.
    struct ShaderDescriptor
    {
        std::string                     DebugName;
        std::vector<ShaderStageSource>  Stages;
        ShaderReflection                Reflection;  // populated from SPIR-V or manually
    };

}  // namespace Limitless
