#include "Project/GameBuilderInternal.h"

#include "Core/Debug/Log.h"
#include "Core/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Limitless::Project
{
    bool GameBuilder::CopyRuntimeFiles(const GameBuildRequest& request, const BuildArtifactLayout& layout, GameBuildResult& result)
    {
        result.StepLog.push_back("Copying runtime files...");

        const std::string projectName = request.ProjectName.empty() ? "Game" : request.ProjectName;
        const std::string targetOS = ResolveTargetOS(request);
        const std::filesystem::path runtimeDir = layout.RuntimeDirectory;
        if (!std::filesystem::is_directory(runtimeDir))
        {
            result.ErrorMessage = "Runtime artifact directory is missing: " + runtimeDir.string();
            return false;
        }

        // 1. Copy Runtime executable (renamed to project name).
        const auto runtimeExePath = runtimeDir / GetRuntimeExecutableName(targetOS);
        const auto gameExePath = request.OutputDirectory / GetGameExecutableName(projectName, targetOS);
        if (!std::filesystem::exists(runtimeExePath))
        {
            result.ErrorMessage = "Runtime executable not found after build: " + runtimeExePath.string();
            return false;
        }

        if (!CopySingleFile(runtimeExePath, gameExePath, result))
        {
            result.ErrorMessage = "Failed to copy Runtime executable to output.";
            return false;
        }
        result.OutputExecutablePath = gameExePath;

        std::filesystem::path configuredWindowIconPath;
        std::string configuredWindowIconError;
        if (!ResolveConfiguredWindowIconPath(request, configuredWindowIconPath, configuredWindowIconError))
        {
            result.ErrorMessage = configuredWindowIconError;
            return false;
        }

        const bool hasConfiguredWindowIcon = !configuredWindowIconPath.empty();
        const std::string shippedWindowIconName = hasConfiguredWindowIcon
            ? configuredWindowIconPath.filename().string()
            : "LimitlessLogo.ico";
        std::filesystem::path shippedWindowIconPath = request.OutputDirectory / shippedWindowIconName;

        // 2. Copy config.json.
        const auto sourceConfig = runtimeDir / "config.json";
        if (std::filesystem::exists(sourceConfig))
        {
            CopySingleFile(sourceConfig, request.OutputDirectory / "config.json", result);
            try
            {
                // Stamp shipped game config with project-specific title/icon.
                const auto outputConfigPath = request.OutputDirectory / "config.json";
                std::ifstream in(outputConfigPath, std::ios::in | std::ios::binary);
                if (in.is_open())
                {
                    json configRoot;
                    in >> configRoot;
                    in.close();

                    if (!configRoot.contains("window") || !configRoot["window"].is_object())
                        configRoot["window"] = json::object();
                    configRoot["window"]["title"] = projectName;
                    configRoot["window"]["icon"] = shippedWindowIconName;

                    std::ofstream out(outputConfigPath, std::ios::out | std::ios::binary | std::ios::trunc);
                    if (out.is_open())
                    {
                        out << configRoot.dump(4);
                        result.StepLog.push_back("Updated config.json window settings (title='" + projectName
                            + "', icon='" + shippedWindowIconName + "').");
                    }
                }
            }
            catch (const std::exception& e)
            {
                result.StepLog.push_back(std::string("Warning: failed to update window settings in config.json: ") + e.what());
            }
        }

        // 2b. Copy game window icon so shipped config can resolve `window.icon`.
        if (hasConfiguredWindowIcon)
        {
            if (!CopySingleFile(configuredWindowIconPath, shippedWindowIconPath, result))
            {
                result.ErrorMessage = "Failed to copy configured game window icon: " + configuredWindowIconPath.string();
                return false;
            }
            result.StepLog.push_back("Copied configured game window icon: " + configuredWindowIconPath.string());
        }
        else
        {
            const auto sourceWindowIcon = runtimeDir / "LimitlessLogo.ico";
            if (std::filesystem::exists(sourceWindowIcon))
            {
                shippedWindowIconPath = request.OutputDirectory / "LimitlessLogo.ico";
                if (!CopySingleFile(sourceWindowIcon, shippedWindowIconPath, result))
                {
                    result.ErrorMessage = "Failed to copy runtime window icon.";
                    return false;
                }
            }
        }

#if defined(LT_PLATFORM_WINDOWS)
        if (hasConfiguredWindowIcon && targetOS == BuildTargetOS::Windows)
        {
            std::filesystem::path executableIconPath = shippedWindowIconPath;
            if (!HasIcoExtension(executableIconPath))
            {
                // Explorer executable icons require .ico resource data.
                // If the runtime icon is non-.ico (png/jpg/etc), allow a companion
                // same-stem .ico to drive executable metadata.
                std::filesystem::path companionIcoPath = configuredWindowIconPath;
                companionIcoPath.replace_extension(".ico");
                if (!std::filesystem::exists(companionIcoPath))
                {
                    result.ErrorMessage =
                        "Configured game window icon updates runtime window icon, but Windows executable icon embedding "
                        "requires a .ico file. Provide a .ico icon path (or add companion icon: "
                        + companionIcoPath.string() + ").";
                    return false;
                }

                executableIconPath = request.OutputDirectory / companionIcoPath.filename();
                if (!CopySingleFile(companionIcoPath, executableIconPath, result))
                {
                    result.ErrorMessage = "Failed to copy companion .ico for executable embedding: " + companionIcoPath.string();
                    return false;
                }
                result.StepLog.push_back("Copied companion .ico for executable icon embedding: " + companionIcoPath.string());
            }

            std::string embedError;
            if (!EmbedIconIntoWindowsExecutable(gameExePath, executableIconPath, embedError))
            {
                result.ErrorMessage = "Failed to embed executable icon: " + embedError;
                return false;
            }
            result.StepLog.push_back("Embedded Windows executable icon from: " + executableIconPath.string());
        }
#endif

        // 3. Copy ScriptCore DLL (prefer project-local staging in internal mode).
        const std::filesystem::path scriptCorePath = layout.ScriptCoreLibraryPath;
        if (std::filesystem::exists(scriptCorePath))
        {
            CopySingleFile(scriptCorePath,
                           request.OutputDirectory / GetScriptCoreLibraryName(targetOS),
                           result);
            result.StepLog.push_back("Copied ScriptCore library.");
        }
        else
        {
            result.StepLog.push_back("Warning: ScriptCore library not found at " + scriptCorePath.string());
        }

        const std::filesystem::path managedPayloadDirectory = layout.ManagedPayloadDirectory.empty()
            ? (runtimeDir / "Managed")
            : layout.ManagedPayloadDirectory;
        if (std::filesystem::is_directory(managedPayloadDirectory))
        {
            if (!CopyDirectoryRecursive(managedPayloadDirectory, request.OutputDirectory / "Managed", result))
            {
                result.ErrorMessage = "Failed to copy managed scripting payload.";
                return false;
            }
            result.StepLog.push_back("Copied managed scripting payload.");
        }

        // 4. Copy runtime dynamic libraries (shaderc, ffmpeg, etc.).
        std::string dynamicLibraryExtension;
        if (targetOS == BuildTargetOS::Windows)
            dynamicLibraryExtension = ".dll";
        else if (targetOS == BuildTargetOS::Linux)
            dynamicLibraryExtension = ".so";
        else
            dynamicLibraryExtension = ".dylib";

        size_t totalDynamicLibrariesCopied = 0;
        for (const auto& sourceDirectory : layout.DynamicLibrarySourceDirectories)
            totalDynamicLibrariesCopied += CopyDllsFromDirectory(sourceDirectory, request.OutputDirectory, dynamicLibraryExtension, result);
        result.StepLog.push_back("Copied " + std::to_string(totalDynamicLibrariesCopied)
                                 + " runtime '" + dynamicLibraryExtension + "' file(s).");

        result.StepLog.push_back("Runtime files copied.");
        return true;
    }

    bool GameBuilder::FinalizePlatformArtifacts(const GameBuildRequest& request, GameBuildResult& result)
    {
        const std::string projectName = request.ProjectName.empty() ? "Game" : request.ProjectName;
        const std::string targetOS = ResolveTargetOS(request);

        if (targetOS == BuildTargetOS::Windows)
        {
            // Windows layout already finalized during copy stage.
            return true;
        }

        if (result.OutputExecutablePath.empty() || !std::filesystem::exists(result.OutputExecutablePath))
        {
            result.ErrorMessage = "Cannot finalize platform artifacts: output executable not found.";
            return false;
        }

        if (targetOS == BuildTargetOS::MacOS)
        {
            const std::filesystem::path appBundlePath = request.OutputDirectory / (projectName + ".app");
            const std::filesystem::path contentsPath = appBundlePath / "Contents";
            const std::filesystem::path macosPath = contentsPath / "MacOS";
            const std::filesystem::path resourcesPath = contentsPath / "Resources";

            std::error_code ec;
            std::filesystem::remove_all(appBundlePath, ec);
            ec.clear();
            std::filesystem::create_directories(macosPath, ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to create macOS bundle directory '" + macosPath.string() + "': " + ec.message();
                return false;
            }
            ec.clear();
            std::filesystem::create_directories(resourcesPath, ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to create macOS bundle resources directory '" + resourcesPath.string() + "': " + ec.message();
                return false;
            }

            for (const auto& entry : std::filesystem::directory_iterator(request.OutputDirectory))
            {
                const std::filesystem::path sourcePath = entry.path();
                if (sourcePath == appBundlePath)
                    continue;

                const std::filesystem::path destinationPath = macosPath / sourcePath.filename();
                ec.clear();
                if (entry.is_directory())
                {
                    std::filesystem::copy(sourcePath,
                                          destinationPath,
                                          std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                                          ec);
                }
                else if (entry.is_regular_file())
                {
                    std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, ec);
                }

                if (ec)
                {
                    result.ErrorMessage = "Failed to stage file into macOS app bundle ('" + sourcePath.string() + "'): " + ec.message();
                    return false;
                }
            }

            const std::filesystem::path bundledExecutablePath = macosPath / result.OutputExecutablePath.filename();
            ec.clear();
            std::filesystem::permissions(
                bundledExecutablePath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
            {
                result.ErrorMessage = "Failed to mark bundled executable as executable: " + ec.message();
                return false;
            }

            const std::filesystem::path shippedIconPath = request.OutputDirectory /
                (request.Settings.GameWindowIconPath.empty()
                     ? std::string("LimitlessLogo.ico")
                     : std::filesystem::path(request.Settings.GameWindowIconPath).filename().string());

            std::string iconPlistValue;
            if (std::filesystem::exists(shippedIconPath))
            {
                std::string extension = shippedIconPath.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (extension == ".icns")
                {
                    const std::filesystem::path iconDestination = resourcesPath / shippedIconPath.filename();
                    ec.clear();
                    std::filesystem::copy_file(shippedIconPath, iconDestination, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec)
                    {
                        result.ErrorMessage = "Failed to copy .icns into app bundle resources: " + ec.message();
                        return false;
                    }
                    iconPlistValue = shippedIconPath.filename().string();
                }
                else if (!request.Settings.GameWindowIconPath.empty())
                {
                    result.StepLog.push_back("Warning: macOS app icon embedding requires a .icns file; using window icon only.");
                }
            }

            auto sanitizeIdentifierPart = [](std::string value)
            {
                for (char& c : value)
                {
                    const bool isAlnum = std::isalnum(static_cast<unsigned char>(c)) != 0;
                    if (!isAlnum && c != '-' && c != '.')
                        c = '-';
                }
                if (value.empty())
                    value = "game";
                return value;
            };
            const std::string bundleIdentifier = "com.limitless." + sanitizeIdentifierPart(projectName);

            const std::filesystem::path infoPlistPath = contentsPath / "Info.plist";
            std::ofstream infoPlist(infoPlistPath, std::ios::out | std::ios::trunc);
            if (!infoPlist.is_open())
            {
                result.ErrorMessage = "Failed to write Info.plist for macOS app bundle.";
                return false;
            }

            infoPlist
                << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                << "<plist version=\"1.0\">\n"
                << "<dict>\n"
                << "    <key>CFBundleName</key>\n"
                << "    <string>" << projectName << "</string>\n"
                << "    <key>CFBundleDisplayName</key>\n"
                << "    <string>" << projectName << "</string>\n"
                << "    <key>CFBundleExecutable</key>\n"
                << "    <string>" << result.OutputExecutablePath.filename().string() << "</string>\n"
                << "    <key>CFBundleIdentifier</key>\n"
                << "    <string>" << bundleIdentifier << "</string>\n"
                << "    <key>CFBundlePackageType</key>\n"
                << "    <string>APPL</string>\n"
                << "    <key>CFBundleVersion</key>\n"
                << "    <string>1.0</string>\n"
                << "    <key>CFBundleShortVersionString</key>\n"
                << "    <string>1.0</string>\n";
            if (!iconPlistValue.empty())
            {
                infoPlist
                    << "    <key>CFBundleIconFile</key>\n"
                    << "    <string>" << iconPlistValue << "</string>\n";
            }
            infoPlist
                << "</dict>\n"
                << "</plist>\n";
            infoPlist.close();

            if (!infoPlist.good())
            {
                result.ErrorMessage = "Failed writing Info.plist content for macOS app bundle.";
                return false;
            }

            result.OutputExecutablePath = appBundlePath;
            result.StepLog.push_back("Created macOS app bundle: " + appBundlePath.string());
            return true;
        }

        if (targetOS == BuildTargetOS::Linux)
        {
            const std::filesystem::path shippedIconPath = request.OutputDirectory /
                (request.Settings.GameWindowIconPath.empty()
                     ? std::string("LimitlessLogo.ico")
                     : std::filesystem::path(request.Settings.GameWindowIconPath).filename().string());
            const std::filesystem::path desktopPath = request.OutputDirectory / (projectName + ".desktop");

            std::error_code ec;
            std::filesystem::permissions(
                result.OutputExecutablePath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark Linux executable as executable: " + ec.message());

            auto toLowerCopy = [](std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return value;
            };
            auto escapeShellDoubleQuoted = [](const std::string& value)
            {
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char c : value)
                {
                    if (c == '\\' || c == '"' || c == '$' || c == '`')
                        escaped.push_back('\\');
                    escaped.push_back(c);
                }
                return escaped;
            };
            auto escapeDesktopValue = [](std::string value)
            {
                std::string escaped;
                escaped.reserve(value.size() + 8);
                for (char c : value)
                {
                    if (c == '\\' || c == ' ')
                        escaped.push_back('\\');
                    escaped.push_back(c);
                }
                return escaped;
            };
            auto sanitizeDesktopId = [](std::string value)
            {
                std::string sanitized;
                sanitized.reserve(value.size());
                for (char c : value)
                {
                    if (std::isalnum(static_cast<unsigned char>(c)) != 0)
                        sanitized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                    else if (c == '.' || c == '-' || c == '_')
                        sanitized.push_back(c == '_' ? '-' : c);
                    else
                        sanitized.push_back('-');
                }

                while (!sanitized.empty() && sanitized.front() == '-')
                    sanitized.erase(sanitized.begin());
                while (!sanitized.empty() && sanitized.back() == '-')
                    sanitized.pop_back();

                if (sanitized.empty())
                    sanitized = "game";
                return sanitized;
            };

            std::filesystem::path launcherIconPath = shippedIconPath;
            if (std::filesystem::exists(launcherIconPath))
            {
                const std::string extension = toLowerCopy(launcherIconPath.extension().string());
                if (extension == ".ico")
                {
                    std::filesystem::path companionPngPath = launcherIconPath;
                    companionPngPath.replace_extension(".png");
                    if (std::filesystem::exists(companionPngPath))
                    {
                        launcherIconPath = companionPngPath;
                        result.StepLog.push_back("Linux launcher icon switched to companion .png: " + companionPngPath.string());
                    }
                    else if (request.Settings.GameWindowIconPath.empty())
                    {
                        const std::filesystem::path defaultLogoPngSource = request.EngineRoot / "Resources" / "LimitlessLogo.png";
                        const std::filesystem::path defaultLogoPngDestination = request.OutputDirectory / "LimitlessLogo.png";
                        if (std::filesystem::exists(defaultLogoPngSource))
                        {
                            if (CopySingleFile(defaultLogoPngSource, defaultLogoPngDestination, result))
                            {
                                launcherIconPath = defaultLogoPngDestination;
                                result.StepLog.push_back("Copied Linux launcher icon fallback (.png): " + defaultLogoPngSource.string());
                            }
                        }
                    }
                }
            }
            const bool hasLauncherIcon = std::filesystem::exists(launcherIconPath);

            const std::string escapedExecutableName =
                escapeShellDoubleQuoted(result.OutputExecutablePath.filename().string());
            const std::string desktopExecLine =
                "sh -c \"cd \\\"$(dirname \\\"$1\\\")\\\" && if [ -f \\\"./install-linux-desktop-entry.sh\\\" ]; "
                "then sh ./install-linux-desktop-entry.sh >/dev/null 2>&1 || true; fi && exec \\\"./"
                + escapedExecutableName + "\\\"\" sh \"%k\"";

            std::ofstream desktopFile(desktopPath, std::ios::out | std::ios::trunc);
            if (!desktopFile.is_open())
            {
                result.ErrorMessage = "Failed to write Linux desktop entry: " + desktopPath.string();
                return false;
            }

            desktopFile
                << "[Desktop Entry]\n"
                << "Version=1.0\n"
                << "Type=Application\n"
                << "Name=" << projectName << "\n"
                << "Exec=" << desktopExecLine << "\n"
                << "Terminal=false\n"
                << "Categories=Game;\n";
            if (hasLauncherIcon)
            {
                const std::string iconValue = "./" + launcherIconPath.filename().string();
                desktopFile << "Icon=" << escapeDesktopValue(iconValue) << "\n";

                if (toLowerCopy(launcherIconPath.extension().string()) == ".ico")
                {
                    result.StepLog.push_back(
                        "Warning: Linux desktop launchers often ignore .ico icon files. Prefer a .png icon path in Build Settings.");
                }
            }
            desktopFile.close();

            if (!desktopFile.good())
            {
                result.ErrorMessage = "Failed writing Linux desktop entry content.";
                return false;
            }

            ec.clear();
            std::filesystem::permissions(
                desktopPath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark desktop entry executable: " + ec.message());

            const std::filesystem::path installScriptPath = request.OutputDirectory / "install-linux-desktop-entry.sh";
            std::ofstream installScript(installScriptPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!installScript.is_open())
            {
                result.ErrorMessage = "Failed to write Linux desktop install script: " + installScriptPath.string();
                return false;
            }

            const std::string desktopAppId = sanitizeDesktopId(projectName);
            installScript
                << "#!/usr/bin/env bash\n"
                << "set -euo pipefail\n\n"
                << "SCRIPT_DIR=\"$(cd -- \"$(dirname -- \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
                << "EXECUTABLE_NAME=\"" << escapeShellDoubleQuoted(result.OutputExecutablePath.filename().string()) << "\"\n"
                << "APP_NAME=\"" << escapeShellDoubleQuoted(projectName) << "\"\n"
                << "APP_ID=\"" << escapeShellDoubleQuoted(desktopAppId) << "\"\n"
                << "ICON_NAME=\"" << (hasLauncherIcon ? escapeShellDoubleQuoted(launcherIconPath.filename().string()) : std::string()) << "\"\n\n"
                << "escape_exec_value() {\n"
                << "  local value=\"$1\"\n"
                << "  value=\"${value//\\\\/\\\\\\\\}\"\n"
                << "  value=\"${value// /\\\\ }\"\n"
                << "  printf '%s' \"$value\"\n"
                << "}\n\n"
                << "TARGET_DESKTOP_DIR=\"${XDG_DATA_HOME:-$HOME/.local/share}/applications\"\n"
                << "TARGET_DESKTOP_PATH=\"$TARGET_DESKTOP_DIR/${APP_ID}.desktop\"\n"
                << "mkdir -p \"$TARGET_DESKTOP_DIR\"\n\n"
                << "EXEC_PATH=\"$SCRIPT_DIR/$EXECUTABLE_NAME\"\n"
                << "if [[ ! -f \"$EXEC_PATH\" ]]; then\n"
                << "  echo \"Missing executable: $EXEC_PATH\" >&2\n"
                << "  exit 1\n"
                << "fi\n\n"
                << "ESCAPED_EXEC_PATH=\"$(escape_exec_value \"$EXEC_PATH\")\"\n\n"
                << "{\n"
                << "  echo \"[Desktop Entry]\"\n"
                << "  echo \"Version=1.0\"\n"
                << "  echo \"Type=Application\"\n"
                << "  echo \"Name=$APP_NAME\"\n"
                << "  echo \"Exec=$ESCAPED_EXEC_PATH\"\n"
                << "  echo \"Path=$SCRIPT_DIR\"\n"
                << "  echo \"Terminal=false\"\n"
                << "  echo \"Categories=Game;\"\n"
                << "  if [[ -n \"$ICON_NAME\" ]]; then\n"
                << "    ICON_PATH=\"$SCRIPT_DIR/$ICON_NAME\"\n"
                << "    if [[ -f \"$ICON_PATH\" ]]; then\n"
                << "      echo \"Icon=$ICON_PATH\"\n"
                << "    fi\n"
                << "  fi\n"
                << "} > \"$TARGET_DESKTOP_PATH\"\n\n"
                << "chmod +x \"$EXEC_PATH\" \"$TARGET_DESKTOP_PATH\"\n\n"
                << "DESKTOP_SHORTCUT_PATH=\"$HOME/Desktop/${APP_ID}.desktop\"\n"
                << "if [[ -d \"$HOME/Desktop\" ]]; then\n"
                << "  cp \"$TARGET_DESKTOP_PATH\" \"$DESKTOP_SHORTCUT_PATH\" 2>/dev/null || true\n"
                << "  chmod +x \"$DESKTOP_SHORTCUT_PATH\" 2>/dev/null || true\n"
                << "fi\n\n"
                << "if command -v update-desktop-database >/dev/null 2>&1; then\n"
                << "  update-desktop-database \"$TARGET_DESKTOP_DIR\" >/dev/null 2>&1 || true\n"
                << "fi\n\n"
                << "echo \"Installed launcher: $TARGET_DESKTOP_PATH\"\n"
                << "if [[ -f \"$DESKTOP_SHORTCUT_PATH\" ]]; then\n"
                << "  echo \"Desktop shortcut: $DESKTOP_SHORTCUT_PATH\"\n"
                << "fi\n"
                << "echo \"If icon cache is stale, log out/in or restart the desktop shell.\"\n";
            installScript.close();
            if (!installScript.good())
            {
                result.ErrorMessage = "Failed writing Linux desktop install script content.";
                return false;
            }

            ec.clear();
            std::filesystem::permissions(
                installScriptPath,
                std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
                std::filesystem::perm_options::add,
                ec);
            if (ec)
                result.StepLog.push_back("Warning: could not mark desktop install script executable: " + ec.message());

            result.StepLog.push_back("Generated Linux desktop launcher: " + desktopPath.string());
            result.StepLog.push_back("Generated portable Linux launcher command (relative to .desktop location).");
            result.StepLog.push_back("Generated Linux launcher installer script: " + installScriptPath.string());
            result.StepLog.push_back("Linux launcher auto-runs 'install-linux-desktop-entry.sh' on launch (best-effort).");
            result.StepLog.push_back("Note: Linux executable files do not support embedded icon metadata; launcher icon is provided via .desktop file.");
            return true;
        }

        result.ErrorMessage = "Unsupported target OS for finalization: " + targetOS;
        return false;
    }

    void GameBuilder::LaunchExecutable(const std::filesystem::path& executablePath)
    {
        if (executablePath.empty() || !std::filesystem::exists(executablePath))
            return;

        const std::string command =
#if defined(LT_PLATFORM_WINDOWS)
            "start \"\" /D \"" + executablePath.parent_path().string() + "\" \"" + executablePath.string() + "\"";
#elif defined(LT_PLATFORM_MACOS)
            "open \"" + executablePath.string() + "\" &";
#else
            "\"" + executablePath.string() + "\" &";
#endif

        LT_CORE_INFO("GameBuilder: launching: {}", command);
        std::system(command.c_str());
    }
}
