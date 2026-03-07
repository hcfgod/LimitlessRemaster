#include "NativeScriptExternalEditor.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <cstdio>
#endif

namespace Limitless::NativeScriptExternalEditor
{
    namespace
    {
        constexpr std::string_view kProjectGuid = "{79AB7667-9503-4BE6-A2F6-37D26AE99E97}";
        constexpr std::string_view kCppProjectTypeGuid = "{BC8A1FFA-BEE3-4634-8014-F334798102B3}";
        constexpr std::string_view kSolutionName = "ProjectScriptCore.External.sln";
        constexpr std::string_view kProjectName = "ProjectScriptCore.External";
        constexpr std::string_view kProjectFileName = "ProjectScriptCore.External.vcxproj";
        constexpr std::string_view kProjectFiltersFileName = "ProjectScriptCore.External.vcxproj.filters";

        std::string TrimCopy(std::string value)
        {
            auto isWhitespace = [](unsigned char character) {
                return std::isspace(character) != 0;
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [&](char character) {
                            return !isWhitespace(static_cast<unsigned char>(character));
                        }).base(),
                        value.end());
            return value;
        }

        std::string ToUpperCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::toupper(character));
            });
            return value;
        }

        std::string NormalizePlatform(std::string platform)
        {
            platform = ToUpperCopy(platform);
            if (platform == "ARM64")
                return "ARM64";
            return "x64";
        }

        std::string NormalizeConfiguration(std::string configuration)
        {
            const std::string upper = ToUpperCopy(configuration);
            if (upper == "DEBUG")
                return "Debug";
            if (upper == "RELEASE")
                return "Release";
            return "Dist";
        }

        std::string ToBuildConfigShortname(const std::string& configuration, const std::string& platform)
        {
            std::string lower = configuration;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lower + (platform == "ARM64" ? "_arm64" : "_x64");
        }

        std::string BuildConfigFolderName(const std::string& configuration, const std::string& platform)
        {
            return ToBuildConfigShortname(configuration, platform) + "-windows-" + platform;
        }

        std::string XmlEscape(const std::string& value)
        {
            std::string escaped;
            escaped.reserve(value.size() + 32);
            for (char character : value)
            {
                switch (character)
                {
                    case '&':
                        escaped += "&amp;";
                        break;
                    case '<':
                        escaped += "&lt;";
                        break;
                    case '>':
                        escaped += "&gt;";
                        break;
                    case '"':
                        escaped += "&quot;";
                        break;
                    case '\'':
                        escaped += "&apos;";
                        break;
                    default:
                        escaped.push_back(character);
                        break;
                }
            }
            return escaped;
        }

        std::string ToWindowsPath(std::filesystem::path path)
        {
            path.make_preferred();
            return path.string();
        }

        std::string ToProjectRelativeWindowsPath(const std::filesystem::path& filePath, const std::filesystem::path& projectDirectory)
        {
            std::error_code errorCode;
            std::filesystem::path relativePath = std::filesystem::relative(filePath, projectDirectory, errorCode);
            if (errorCode || relativePath.empty())
                relativePath = filePath;
            return ToWindowsPath(relativePath);
        }

        std::vector<std::filesystem::path> DiscoverScriptFiles(const std::filesystem::path& assetsRoot, const std::string& extension)
        {
            std::vector<std::filesystem::path> files;
            std::error_code errorCode;
            if (!std::filesystem::exists(assetsRoot, errorCode))
                return files;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() == extension)
                    files.push_back(entry.path());
            }

            std::sort(files.begin(), files.end());
            return files;
        }

        std::string BuildFilterPathForFile(const std::filesystem::path& filePath, const std::filesystem::path& rootDirectory)
        {
            std::error_code errorCode;
            std::filesystem::path relativePath = std::filesystem::relative(filePath, rootDirectory, errorCode);
            if (errorCode || relativePath.empty())
                return {};

            const std::filesystem::path parentPath = relativePath.parent_path();
            if (parentPath.empty() || parentPath == ".")
                return {};

            return ToWindowsPath(parentPath);
        }

        void InsertFilterHierarchy(std::set<std::string>& filters, const std::string& filterPath)
        {
            if (filterPath.empty())
                return;

            std::filesystem::path currentPath;
            for (const auto& segment : std::filesystem::path(filterPath))
            {
                const std::string segmentText = segment.string();
                if (segmentText.empty() || segmentText == ".")
                    continue;
                currentPath /= segment;
                filters.insert(ToWindowsPath(currentPath));
            }
        }

        std::vector<std::filesystem::path> BuildIncludePaths(const OpenVisualStudioRequest& request,
                                                              const std::filesystem::path& generatedScriptRoot)
        {
            std::vector<std::filesystem::path> includePaths;
            auto addPath = [&](const std::filesystem::path& path) {
                if (path.empty())
                    return;
                if (std::find(includePaths.begin(), includePaths.end(), path) != includePaths.end())
                    return;
                includePaths.push_back(path);
            };

            if (request.UseInternalToolchain)
            {
                addPath(request.BuildRoot / "SDK" / "include");
                addPath(request.BuildRoot / "SDK" / "vendor");
                addPath(request.BuildRoot / "SDK" / "vendor" / "box2d" / "include");
                addPath(request.BuildRoot / "SDK" / "vendor" / "glad");
                addPath(request.BuildRoot / "SDK" / "vendor" / "spdlog");
                addPath(request.BuildRoot / "SDK" / "vendor" / "doctest");
                addPath(request.BuildRoot / "SDK" / "vendor" / "SDL3");
                addPath(request.BuildRoot / "SDK" / "vendor" / "ffmpeg" / "include");
                addPath(request.BuildRoot / "SDK" / "vendor" / "imgui");
                addPath(request.BuildRoot / "SDK" / "vendor" / "glm");
            }
            else
            {
                addPath(request.BuildRoot / "Limitless" / "Source");
                addPath(request.BuildRoot / "Limitless" / "Vendor");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "box2d" / "include");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "spdlog");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "nlohmann");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "SDL3");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "ffmpeg" / "include");
                addPath(request.BuildRoot / "Limitless" / "Vendor" / "imgui");
                addPath(request.BuildRoot / "ScriptCore" / "Source");
            }

            addPath(generatedScriptRoot);
            return includePaths;
        }

        std::string BuildPreprocessorDefinitions(const OpenVisualStudioRequest& request)
        {
            const std::string configuration = NormalizeConfiguration(request.Configuration);
            const std::string platform = NormalizePlatform(request.Platform);

            std::string configurationDefine = "LT_CONFIG_DIST";
            if (configuration == "Debug")
                configurationDefine = "LT_CONFIG_DEBUG";
            else if (configuration == "Release")
                configurationDefine = "LT_CONFIG_RELEASE";

            const std::string archDefine = (platform == "ARM64")
                ? "LT_ARCHITECTURE_ARM64"
                : "LT_ARCHITECTURE_X64";

            return configurationDefine
                + ";" + archDefine
                + ";LT_PLATFORM_WINDOWS;SCRIPTCORE_EXPORTS;_UNICODE;UNICODE;%(PreprocessorDefinitions)";
        }

        bool WriteTextFile(const std::filesystem::path& path, const std::string& contents, std::string& outError)
        {
            outError.clear();
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            if (errorCode)
            {
                outError = "Failed to create directory '" + path.parent_path().string() + "': " + errorCode.message();
                return false;
            }

            std::ofstream output(path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                outError = "Failed to write file: " + path.string();
                return false;
            }

            output << contents;
            if (!output.good())
            {
                outError = "Failed writing file contents: " + path.string();
                return false;
            }
            return true;
        }

#ifdef LT_PLATFORM_WINDOWS
        std::optional<std::filesystem::path> FindVisualStudioDevenv()
        {
            if (const char* vsInstallDir = std::getenv("VSINSTALLDIR"); vsInstallDir && vsInstallDir[0] != '\0')
            {
                const std::filesystem::path candidate = std::filesystem::path(vsInstallDir) / "Common7" / "IDE" / "devenv.exe";
                std::error_code errorCode;
                if (std::filesystem::exists(candidate, errorCode))
                    return candidate;
            }

            const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
            if (programFilesX86 && programFilesX86[0] != '\0')
            {
                const std::filesystem::path vsWherePath =
                    std::filesystem::path(programFilesX86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
                std::error_code errorCode;
                if (std::filesystem::exists(vsWherePath, errorCode))
                {
                    const std::string command =
                        "\"" + vsWherePath.string() + "\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find Common7\\IDE\\devenv.exe";
                    FILE* pipe = _popen(command.c_str(), "r");
                    if (pipe)
                    {
                        std::array<char, 1024> buffer{};
                        std::string output;
                        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
                            output += buffer.data();
                        _pclose(pipe);

                        const std::string resolvedPath = TrimCopy(output);
                        if (!resolvedPath.empty())
                        {
                            const std::filesystem::path candidate(resolvedPath);
                            errorCode.clear();
                            if (std::filesystem::exists(candidate, errorCode))
                                return candidate;
                        }
                    }
                }
            }

            const char* programFiles = std::getenv("ProgramFiles");
            if (!programFiles || programFiles[0] == '\0')
                return std::nullopt;

            const std::filesystem::path basePath(programFiles);
            const std::array<const char*, 4> editions = { "Community", "Professional", "Enterprise", "Preview" };
            for (const char* edition : editions)
            {
                const std::filesystem::path candidate =
                    basePath / "Microsoft Visual Studio" / "2022" / edition / "Common7" / "IDE" / "devenv.exe";
                std::error_code errorCode;
                if (std::filesystem::exists(candidate, errorCode))
                    return candidate;
            }

            return std::nullopt;
        }

        std::string GetWin32ErrorMessage(DWORD errorCode)
        {
            LPSTR messageBuffer = nullptr;
            const DWORD size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&messageBuffer),
                0,
                nullptr);

            std::string message;
            if (size > 0 && messageBuffer)
                message.assign(messageBuffer, size);
            else
                message = "Unknown error";

            if (messageBuffer)
                LocalFree(messageBuffer);

            return TrimCopy(message);
        }

        bool LaunchVisualStudio(const std::filesystem::path& devenvPath,
                                const std::filesystem::path& solutionPath,
                                const std::filesystem::path& targetScriptPath,
                                std::string& outError)
        {
            outError.clear();
            (void)targetScriptPath;

            // Open the solution itself so IntelliSense uses the generated project context.
            // Using /Edit can open a loose file without binding to the solution.
            std::string commandLine = "\"" + devenvPath.string() + "\" \"" + solutionPath.string() + "\"";
            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInformation{};
            std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back('\0');

            const BOOL created = CreateProcessA(
                nullptr,
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                solutionPath.parent_path().string().c_str(),
                &startupInfo,
                &processInformation);

            if (!created)
            {
                outError = "Failed to launch Visual Studio: " + GetWin32ErrorMessage(GetLastError());
                return false;
            }

            CloseHandle(processInformation.hThread);
            CloseHandle(processInformation.hProcess);
            return true;
        }
#endif

        bool EnsureVisualStudioProject(const OpenVisualStudioRequest& request,
                                       std::filesystem::path& outSolutionPath,
                                       std::string& outError)
        {
#ifndef LT_PLATFORM_WINDOWS
            (void)request;
            outSolutionPath.clear();
            outError = "External Visual Studio editing is only available on Windows.";
            return false;
#else
            outError.clear();
            const std::string configuration = NormalizeConfiguration(request.Configuration);
            const std::string platform = NormalizePlatform(request.Platform);
            const std::filesystem::path assetsRoot = request.ProjectRoot / "Assets";
            const std::filesystem::path generatedScriptRoot = request.ProjectRoot / "Build" / "Generated" / "ScriptCore";
            const std::filesystem::path externalEditorDirectory =
                request.ProjectRoot / "Build" / "ScriptCore" / BuildConfigFolderName(configuration, platform) / "ExternalEditor";
            const std::filesystem::path solutionPath = externalEditorDirectory / std::string(kSolutionName);
            const std::filesystem::path projectPath = externalEditorDirectory / std::string(kProjectFileName);
            const std::filesystem::path projectFiltersPath = externalEditorDirectory / std::string(kProjectFiltersFileName);

            const std::vector<std::filesystem::path> cppFiles = DiscoverScriptFiles(assetsRoot, ".cpp");
            const std::vector<std::filesystem::path> headerFiles = DiscoverScriptFiles(assetsRoot, ".h");
            const std::vector<std::filesystem::path> includePaths = BuildIncludePaths(request, generatedScriptRoot);

            std::ostringstream includeStream;
            for (size_t index = 0; index < includePaths.size(); ++index)
            {
                if (index > 0)
                    includeStream << ';';
                includeStream << XmlEscape(ToProjectRelativeWindowsPath(includePaths[index], externalEditorDirectory));
            }
            includeStream << ";%(AdditionalIncludeDirectories)";

            std::ostringstream compileItems;
            for (const auto& file : cppFiles)
                compileItems << "    <ClCompile Include=\"" << XmlEscape(ToProjectRelativeWindowsPath(file, externalEditorDirectory)) << "\" />\n";
            if (cppFiles.empty())
            {
                compileItems
                    << "    <ClCompile Include=\"Generated\\DummyScriptCoreTranslationUnit.cpp\" />\n";
            }

            std::ostringstream includeItems;
            for (const auto& file : headerFiles)
                includeItems << "    <ClInclude Include=\"" << XmlEscape(ToProjectRelativeWindowsPath(file, externalEditorDirectory)) << "\" />\n";

            std::set<std::string> filters;
            std::ostringstream compileFilterItems;
            for (const auto& file : cppFiles)
            {
                const std::string filterPath = BuildFilterPathForFile(file, assetsRoot);
                if (!filterPath.empty())
                    InsertFilterHierarchy(filters, filterPath);

                compileFilterItems << "    <ClCompile Include=\"" << XmlEscape(ToProjectRelativeWindowsPath(file, externalEditorDirectory)) << "\"";
                if (filterPath.empty())
                {
                    compileFilterItems << " />\n";
                }
                else
                {
                    compileFilterItems << ">\n";
                    compileFilterItems << "      <Filter>" << XmlEscape(filterPath) << "</Filter>\n";
                    compileFilterItems << "    </ClCompile>\n";
                }
            }

            std::ostringstream includeFilterItems;
            for (const auto& file : headerFiles)
            {
                const std::string filterPath = BuildFilterPathForFile(file, assetsRoot);
                if (!filterPath.empty())
                    InsertFilterHierarchy(filters, filterPath);

                includeFilterItems << "    <ClInclude Include=\"" << XmlEscape(ToProjectRelativeWindowsPath(file, externalEditorDirectory)) << "\"";
                if (filterPath.empty())
                {
                    includeFilterItems << " />\n";
                }
                else
                {
                    includeFilterItems << ">\n";
                    includeFilterItems << "      <Filter>" << XmlEscape(filterPath) << "</Filter>\n";
                    includeFilterItems << "    </ClInclude>\n";
                }
            }

            const std::string preprocessorDefinitions = XmlEscape(BuildPreprocessorDefinitions(request));
            const std::string projectConfiguration = configuration + "|" + platform;

            const std::string vcxprojContents =
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                "  <ItemGroup Label=\"ProjectConfigurations\">\n"
                "    <ProjectConfiguration Include=\"" + XmlEscape(projectConfiguration) + "\">\n"
                "      <Configuration>" + XmlEscape(configuration) + "</Configuration>\n"
                "      <Platform>" + XmlEscape(platform) + "</Platform>\n"
                "    </ProjectConfiguration>\n"
                "  </ItemGroup>\n"
                "  <PropertyGroup Label=\"Globals\">\n"
                "    <VCProjectVersion>17.0</VCProjectVersion>\n"
                "    <ProjectGuid>" + std::string(kProjectGuid) + "</ProjectGuid>\n"
                "    <Keyword>Win32Proj</Keyword>\n"
                "    <RootNamespace>ProjectScriptCoreExternal</RootNamespace>\n"
                "    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>\n"
                "  </PropertyGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
                "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" + XmlEscape(projectConfiguration) + "'\" Label=\"Configuration\">\n"
                "    <ConfigurationType>DynamicLibrary</ConfigurationType>\n"
                "    <UseDebugLibraries>false</UseDebugLibraries>\n"
                "    <PlatformToolset>v143</PlatformToolset>\n"
                "    <CharacterSet>Unicode</CharacterSet>\n"
                "    <WholeProgramOptimization>false</WholeProgramOptimization>\n"
                "  </PropertyGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
                "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='" + XmlEscape(projectConfiguration) + "'\">\n"
                "    <TargetName>ProjectScriptCoreExternal</TargetName>\n"
                "    <IntDir>$(ProjectDir)\\obj\\$(Configuration)\\</IntDir>\n"
                "    <OutDir>$(ProjectDir)\\bin\\$(Configuration)\\</OutDir>\n"
                "  </PropertyGroup>\n"
                "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='" + XmlEscape(projectConfiguration) + "'\">\n"
                "    <ClCompile>\n"
                "      <LanguageStandard>stdcpp20</LanguageStandard>\n"
                "      <ConformanceMode>true</ConformanceMode>\n"
                "      <PrecompiledHeader>NotUsing</PrecompiledHeader>\n"
                "      <AdditionalIncludeDirectories>" + includeStream.str() + "</AdditionalIncludeDirectories>\n"
                "      <PreprocessorDefinitions>" + preprocessorDefinitions + "</PreprocessorDefinitions>\n"
                "      <MultiProcessorCompilation>true</MultiProcessorCompilation>\n"
                "    </ClCompile>\n"
                "  </ItemDefinitionGroup>\n"
                "  <ItemGroup>\n"
                + compileItems.str() +
                "  </ItemGroup>\n"
                "  <ItemGroup>\n"
                + includeItems.str() +
                "  </ItemGroup>\n"
                "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
                "</Project>\n";

            const std::string solutionContents =
                "Microsoft Visual Studio Solution File, Format Version 12.00\n"
                "# Visual Studio Version 17\n"
                "VisualStudioVersion = 17.0.31903.59\n"
                "MinimumVisualStudioVersion = 10.0.40219.1\n"
                "Project(\"" + std::string(kCppProjectTypeGuid) + "\") = \"" + std::string(kProjectName) + "\", \"" + std::string(kProjectFileName) + "\", \"" + std::string(kProjectGuid) + "\"\n"
                "EndProject\n"
                "Global\n"
                "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n"
                "\t\t" + projectConfiguration + " = " + projectConfiguration + "\n"
                "\tEndGlobalSection\n"
                "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n"
                "\t\t" + std::string(kProjectGuid) + "." + projectConfiguration + ".ActiveCfg = " + projectConfiguration + "\n"
                "\t\t" + std::string(kProjectGuid) + "." + projectConfiguration + ".Build.0 = " + projectConfiguration + "\n"
                "\tEndGlobalSection\n"
                "\tGlobalSection(SolutionProperties) = preSolution\n"
                "\t\tHideSolutionNode = FALSE\n"
                "\tEndGlobalSection\n"
                "EndGlobal\n";

            std::ostringstream dummyFilterItems;
            if (cppFiles.empty())
            {
                InsertFilterHierarchy(filters, "Generated");
                dummyFilterItems
                    << "    <ClCompile Include=\"Generated\\DummyScriptCoreTranslationUnit.cpp\">\n"
                    << "      <Filter>Generated</Filter>\n"
                    << "    </ClCompile>\n";
            }

            std::ostringstream filterDefinitions;
            for (const std::string& filter : filters)
                filterDefinitions << "    <Filter Include=\"" << XmlEscape(filter) << "\" />\n";

            const std::string vcxprojFiltersContents =
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                + (filters.empty() ? std::string{} : "  <ItemGroup>\n" + filterDefinitions.str() + "  </ItemGroup>\n")
                + ((compileFilterItems.str().empty() && dummyFilterItems.str().empty()) ? std::string{} : "  <ItemGroup>\n" + compileFilterItems.str() + dummyFilterItems.str() + "  </ItemGroup>\n")
                + (includeFilterItems.str().empty() ? std::string{} : "  <ItemGroup>\n" + includeFilterItems.str() + "  </ItemGroup>\n")
                + "</Project>\n";

            std::error_code directoryError;
            std::filesystem::create_directories(externalEditorDirectory / "Generated", directoryError);
            if (directoryError)
            {
                outError = "Failed to prepare external editor project directory: " + directoryError.message();
                return false;
            }

            if (cppFiles.empty())
            {
                std::string dummyError;
                if (!WriteTextFile(externalEditorDirectory / "Generated" / "DummyScriptCoreTranslationUnit.cpp",
                                   "// Auto-generated placeholder translation unit for IntelliSense project.\n",
                                   dummyError))
                {
                    outError = dummyError;
                    return false;
                }
            }

            if (!WriteTextFile(projectPath, vcxprojContents, outError))
                return false;
            if (!WriteTextFile(projectFiltersPath, vcxprojFiltersContents, outError))
                return false;
            if (!WriteTextFile(solutionPath, solutionContents, outError))
                return false;

            outSolutionPath = solutionPath;
            return true;
#endif
        }
    }

    OpenVisualStudioResult OpenScriptInVisualStudio(const OpenVisualStudioRequest& request)
    {
        OpenVisualStudioResult result;

#ifndef LT_PLATFORM_WINDOWS
        result.ErrorMessage = "External Visual Studio editing is only available on Windows.";
        return result;
#else
        if (request.ProjectRoot.empty())
        {
            result.ErrorMessage = "No open project root is available for external editor launch.";
            return result;
        }
        if (request.BuildRoot.empty())
        {
            result.ErrorMessage = "Script build root is unavailable. Configure Build Settings backend/toolchain root first.";
            return result;
        }
        if (request.TargetScriptPath.empty())
        {
            result.ErrorMessage = "Target script path is empty.";
            return result;
        }

        std::error_code targetError;
        if (!std::filesystem::exists(request.TargetScriptPath, targetError))
        {
            result.ErrorMessage = "Target script file was not found: " + request.TargetScriptPath.string();
            return result;
        }

        const auto devenvPath = FindVisualStudioDevenv();
        if (!devenvPath.has_value())
        {
            result.ErrorMessage = "Visual Studio was not detected. Install Visual Studio 2022 with C++ workload.";
            return result;
        }
        result.DevenvPath = devenvPath.value();

        std::filesystem::path solutionPath;
        if (!EnsureVisualStudioProject(request, solutionPath, result.ErrorMessage))
            return result;
        result.SolutionPath = solutionPath;

        if (!LaunchVisualStudio(devenvPath.value(), solutionPath, request.TargetScriptPath, result.ErrorMessage))
            return result;

        LT_INFO("Native scripts: opened Visual Studio external editor using '{}'.", solutionPath.string());
        result.Launched = true;
        return result;
#endif
    }
}
