#include "Graphics/ShaderCompilation/ShaderCompiler.h"

#include "Core/Debug/Log.h"

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#if defined(LT_ENABLE_SHADERC)
    #include <shaderc/shaderc.hpp>
#endif

namespace Limitless::ShaderCompilation
{
    namespace
    {
        const char* ToString(ShaderStage stage)
        {
            switch (stage)
            {
                case ShaderStage::Vertex: return "Vertex";
                case ShaderStage::Fragment: return "Fragment";
                case ShaderStage::Compute: return "Compute";
                default: return "Unknown";
            }
        }

#if defined(LT_ENABLE_SHADERC)
        shaderc_shader_kind ToShadercKind(ShaderStage stage)
        {
            switch (stage)
            {
                case ShaderStage::Vertex: return shaderc_vertex_shader;
                case ShaderStage::Fragment: return shaderc_fragment_shader;
                case ShaderStage::Compute: return shaderc_compute_shader;
                default: return shaderc_glsl_infer_from_source;
            }
        }
#endif
    }

    Result<std::vector<uint32_t>> CompileGlslToSpirv(
        ShaderStage stage,
        std::string_view glslSource,
        std::string_view debugName,
        std::string_view entryPoint)
    {
#if !defined(LT_ENABLE_SHADERC)
        (void)stage;
        (void)glslSource;
        (void)debugName;
        (void)entryPoint;
        return Result<std::vector<uint32_t>>(ErrorCode::NotSupported, "shaderc is not enabled for this platform/configuration.");
#else
        if (glslSource.empty())
        {
            return Result<std::vector<uint32_t>>(ErrorCode::InvalidArgument, "CompileGlslToSpirv: GLSL source is empty.");
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        options.SetSourceLanguage(shaderc_source_language_glsl);

        // Our current runtime backend is OpenGL. When Vulkan lands, we can switch this based on the selected API.
        options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);

        // SPIR-V requires explicit locations/bindings, but classic GLSL often omits them.
        // To avoid forcing all authoring shaders to be "SPIR-V-style", we let shaderc
        // assign locations and uniform bindings automatically.
        //
        // This keeps existing GLSL like:
        //   out vec2 v_UV;
        // compiling cleanly even when routed through SPIR-V for validation/reflection.
        options.SetAutoMapLocations(true);
        options.SetAutoBindUniforms(true);

#if defined(LT_CONFIG_DEBUG)
        options.SetOptimizationLevel(shaderc_optimization_level_zero);
        options.SetGenerateDebugInfo();
#else
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

        const shaderc_shader_kind kind = ToShadercKind(stage);
        const std::string debugNameString(debugName);
        const std::string entryPointString(entryPoint);
        const shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
            glslSource.data(),
            glslSource.size(),
            kind,
            debugNameString.c_str(),
            entryPointString.c_str(),
            options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            const std::string message =
                std::string("shaderc compile failed (") + ToString(stage) + ") for '" + std::string(debugName) + "':\n" +
                result.GetErrorMessage();
            return Result<std::vector<uint32_t>>(ErrorCode::ShaderCompilationFailed, message);
        }

        std::vector<uint32_t> spirv;
        spirv.assign(result.cbegin(), result.cend());
        if (spirv.empty())
        {
            return Result<std::vector<uint32_t>>(ErrorCode::ShaderCompilationFailed,
                                                 "shaderc compile succeeded but produced empty SPIR-V for '" + std::string(debugName) + "'.");
        }

        return spirv;
#endif
    }

    Result<std::string> TranspileSpirvToGlsl(
        const std::vector<uint32_t>& spirvWords,
        int glslVersion,
        bool emitLineDirectives,
        std::string_view debugName)
    {
        if (spirvWords.empty())
        {
            return Result<std::string>(ErrorCode::InvalidArgument, "TranspileSpirvToGlsl: SPIR-V input is empty.");
        }

        try
        {
            spirv_cross::CompilerGLSL compiler(spirvWords);
            spirv_cross::CompilerGLSL::Options options{};

            // Match the current project shader style (`#version 330 core`).
            options.version = glslVersion;
            options.es = false;
            options.vulkan_semantics = false;
            options.emit_line_directives = emitLineDirectives;

            compiler.set_common_options(options);

            std::string glsl = compiler.compile();
            if (glsl.empty())
            {
                return Result<std::string>(ErrorCode::ShaderCompilationFailed,
                                           "SPIRV-Cross produced empty GLSL for '" + std::string(debugName) + "'.");
            }

            return glsl;
        }
        catch (const std::exception& e)
        {
            const std::string message =
                "SPIRV-Cross transpile failed for '" + std::string(debugName) + "': " + e.what();
            LT_CORE_ERROR("{}", message);
            return Result<std::string>(ErrorCode::ShaderCompilationFailed, message);
        }
        catch (...)
        {
            const std::string message =
                "SPIRV-Cross transpile failed for '" + std::string(debugName) + "': unknown exception.";
            LT_CORE_ERROR("{}", message);
            return Result<std::string>(ErrorCode::ShaderCompilationFailed, message);
        }
    }

    void DebugLogSpirvResourceSummary(const std::vector<uint32_t>& spirvWords, std::string_view debugName)
    {
#if defined(LT_CONFIG_DEBUG)
        if (spirvWords.empty())
        {
            return;
        }

        try
        {
            spirv_cross::Compiler compiler(spirvWords);
            const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

            // Keep this intentionally high-level to avoid log spam.
            LT_CORE_DEBUG("SPIR-V reflection summary for '{}': stageInputs={}, stageOutputs={}, uniformBuffers={}, storageBuffers={}, sampledImages={}, separateSamplers={}, separateImages={}, pushConstants={}",
                          std::string(debugName),
                          resources.stage_inputs.size(),
                          resources.stage_outputs.size(),
                          resources.uniform_buffers.size(),
                          resources.storage_buffers.size(),
                          resources.sampled_images.size(),
                          resources.separate_samplers.size(),
                          resources.separate_images.size(),
                          resources.push_constant_buffers.size());
        }
        catch (...)
        {
            // Reflection failures must never take down runtime shader loading.
        }
#else
        (void)spirvWords;
        (void)debugName;
#endif
    }
}

