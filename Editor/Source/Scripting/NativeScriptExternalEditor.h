#pragma once

#include <filesystem>
#include <string>

namespace Limitless::NativeScriptExternalEditor
{
    struct OpenVisualStudioRequest final
    {
        std::filesystem::path ProjectRoot;
        std::filesystem::path BuildRoot;
        std::filesystem::path EngineSourceRoot;
        std::filesystem::path TargetScriptPath;
        std::string Configuration = "Dist";
        std::string Platform = "x64";
        bool UseInternalToolchain = false;
    };

    struct OpenVisualStudioResult final
    {
        bool Launched = false;
        std::string ErrorMessage;
        std::filesystem::path SolutionPath;
        std::filesystem::path DevenvPath;
    };

    [[nodiscard]] OpenVisualStudioResult OpenScriptInVisualStudio(const OpenVisualStudioRequest& request);
}
