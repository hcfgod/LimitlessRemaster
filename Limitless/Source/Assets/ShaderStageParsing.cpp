#include "Assets/ShaderStageParsing.h"

#include "Core/Debug/Log.h"
#include "Graphics/GraphicsAPIDetector.h"

#if defined(LT_ENABLE_SHADERC)
    #include "Graphics/ShaderCompilation/ShaderCompiler.h"
#endif

namespace Limitless::Assets
{
    Result<ParsedShaderStages> ParseCombinedGlsl(
        const std::string& key,
        const std::string& resolvedPath,
        const std::string& fileText,
        const std::string& nameOverride)
    {
        // Format:
        //   #type vertex
        //   ...
        //   #type fragment
        //   ...
        auto findStage = [&](const std::string& stage) -> size_t {
            const std::string tag = "#type " + stage;
            return fileText.find(tag);
        };

        const size_t vPos = findStage("vertex");
        const size_t fPos = findStage("fragment");
        if (vPos == std::string::npos || fPos == std::string::npos)
        {
            return Result<ParsedShaderStages>(ErrorCode::ResourceFormatNotSupported,
                "Shader file must contain '#type vertex' and '#type fragment': " + resolvedPath);
        }

        auto readStageBody = [&](const size_t tagPos, const size_t nextTagPos) -> std::string {
            const size_t lineEnd = fileText.find('\n', tagPos);
            const size_t bodyStart = (lineEnd == std::string::npos) ? tagPos : (lineEnd + 1);
            const size_t bodyEnd = (nextTagPos == std::string::npos) ? fileText.size() : nextTagPos;
            if (bodyStart >= bodyEnd)
            {
                return {};
            }
            return fileText.substr(bodyStart, bodyEnd - bodyStart);
        };

        ParsedShaderStages out;
        if (!nameOverride.empty())
        {
            out.Name = nameOverride;
        }
        else
        {
            // Default to filename stem from key (Unity-style path).
            const auto slash = key.find_last_of("/\\");
            const std::string fileName = (slash == std::string::npos) ? key : key.substr(slash + 1);
            const auto dot = fileName.find_last_of('.');
            out.Name = (dot == std::string::npos) ? fileName : fileName.substr(0, dot);
        }

        // Determine ordering so we can cut stage bodies cleanly.
        if (vPos < fPos)
        {
            out.Vertex = readStageBody(vPos, fPos);
            out.Fragment = readStageBody(fPos, std::string::npos);
        }
        else
        {
            out.Fragment = readStageBody(fPos, vPos);
            out.Vertex = readStageBody(vPos, std::string::npos);
        }

        if (out.Vertex.empty() || out.Fragment.empty())
        {
            return Result<ParsedShaderStages>(ErrorCode::ResourceCorrupted, "Shader stage source was empty: " + resolvedPath);
        }

        return out;
    }

    Result<ParsedShaderStages> PrepareShaderStagesForActiveGraphicsAPI(ParsedShaderStages parsed, const std::string& debugPath)
    {
#if defined(LT_ENABLE_SHADERC)
        const auto api = GraphicsAPIDetector::GetBestAPI().value_or(GraphicsAPI::OpenGL);
        if (api == GraphicsAPI::OpenGL)
        {
            const std::string vertexName = parsed.Name + " (vertex) " + debugPath;
            const std::string fragmentName = parsed.Name + " (fragment) " + debugPath;

            const auto vertexSpirvResult = ShaderCompilation::CompileGlslToSpirv(
                ShaderCompilation::ShaderStage::Vertex,
                parsed.Vertex,
                vertexName);
            if (vertexSpirvResult.IsFailure())
            {
                return Result<ParsedShaderStages>(vertexSpirvResult.GetError());
            }

            const auto fragmentSpirvResult = ShaderCompilation::CompileGlslToSpirv(
                ShaderCompilation::ShaderStage::Fragment,
                parsed.Fragment,
                fragmentName);
            if (fragmentSpirvResult.IsFailure())
            {
                return Result<ParsedShaderStages>(fragmentSpirvResult.GetError());
            }

            ShaderCompilation::DebugLogSpirvResourceSummary(vertexSpirvResult.GetValue(), vertexName);
            ShaderCompilation::DebugLogSpirvResourceSummary(fragmentSpirvResult.GetValue(), fragmentName);
        }
#else
        (void)debugPath;
#endif

        return parsed;
    }
}

