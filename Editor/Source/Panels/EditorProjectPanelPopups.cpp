#include "EditorProjectPanelInternal.h"

#include "EditorInspectorPanel.h"
#include "EditorTilePalettePanel.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/AssetPaths.h"
#include "Assets/TilePaletteAsset.h"
#include "Core/Debug/Log.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
        std::string EscapeRegexLiteral(const std::string& value)
        {
            std::string escaped;
            escaped.reserve(value.size() * 2);
            for (char character : value)
            {
                switch (character)
                {
                    case '.': case '^': case '$': case '|': case '(': case ')':
                    case '[': case ']': case '{': case '}': case '*': case '+':
                    case '?': case '\\':
                        escaped.push_back('\\');
                        break;
                    default:
                        break;
                }
                escaped.push_back(character);
            }
            return escaped;
        }

        bool ReplaceWholeWordInFile(const std::filesystem::path& filePath,
                                    const std::string& oldWord,
                                    const std::string& newWord)
        {
            std::ifstream input(filePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::regex wholeWordPattern("\\b" + EscapeRegexLiteral(oldWord) + "\\b");
            content = std::regex_replace(content, wholeWordPattern, newWord);

            std::ofstream output(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }

        bool RewriteSourceIncludeForRenamedScriptPair(const std::filesystem::path& sourcePath,
                                                      const std::string& oldHeaderName,
                                                      const std::string& newHeaderName)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
                return false;

            std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            const std::string oldInclude = "#include \"" + oldHeaderName + "\"";
            const std::string newInclude = "#include \"" + newHeaderName + "\"";
            const size_t includePosition = content.find(oldInclude);
            if (includePosition == std::string::npos)
                return true;
            content.replace(includePosition, oldInclude.size(), newInclude);

            std::ofstream output(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
                return false;
            output << content;
            return output.good();
        }
    }

    void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
    {
        if (!source)
        {
            destination[0] = '\0';
            return;
        }

        std::snprintf(destination.data(), destination.size(), "%s", source);
    }

    std::string SanitizeScriptClassBaseName(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            value.erase(value.begin());
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            value.pop_back();

        std::string sanitized;
        sanitized.reserve(value.size() + 8);
        for (char character : value)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (std::isalnum(raw) || character == '_')
                sanitized.push_back(character);
        }

        if (sanitized.empty())
            sanitized = "NewNativeScript";
        if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0)
            sanitized.insert(0, "Script_");
        return sanitized;
    }

    std::string SanitizeManagedScriptClassName(std::string value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            value.erase(value.begin());
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            value.pop_back();

        std::string sanitized;
        sanitized.reserve(value.size() + 8);
        for (char character : value)
        {
            const unsigned char raw = static_cast<unsigned char>(character);
            if (std::isalnum(raw) || character == '_')
                sanitized.push_back(character);
        }

        if (sanitized.empty())
            sanitized = "NewManagedScript";
        if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0)
            sanitized.insert(0, "Script_");
        return sanitized;
    }

    bool CreateNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                        const std::filesystem::path& parentRelativePath,
                                        const std::string& requestedClassName,
                                        std::string& outCreatedSourceAssetKey,
                                        std::string& outError)
    {
        const std::string className = SanitizeScriptClassBaseName(requestedClassName);
        const std::filesystem::path scriptDirectory = assetsDirectory / parentRelativePath;

        std::error_code errorCode;
        std::filesystem::create_directories(scriptDirectory, errorCode);
        if (errorCode)
        {
            outError = "Failed to create script directory: " + errorCode.message();
            return false;
        }

        const std::filesystem::path headerPath = scriptDirectory / (className + ".h");
        const std::filesystem::path sourcePath = scriptDirectory / (className + ".cpp");
        if (std::filesystem::exists(headerPath) || std::filesystem::exists(sourcePath))
        {
            outError = "Script already exists: " + className;
            return false;
        }

        const std::string headerTemplate =
            "#pragma once\n\n"
            "#include \"Limitless.h\"\n\n"
            "class " + className + " final : public Limitless::ScriptableEntity\n"
            "{\n"
            "public:\n"
            "    float RotationSpeed = 90.0f;\n\n"
            "    LT_EXPOSED_FIELDS(RotationSpeed)\n\n"
            "protected:\n"
            "    void OnCreate() override;\n"
            "    void OnUpdate(float deltaTime) override;\n"
            "    void OnDestroy() override;\n"
            "};\n";

        const std::string sourceTemplate =
            "#include \"" + className + ".h\"\n\n"
            "#include \"ScriptCoreRegistration.h\"\n\n"
            "void " + className + "::OnCreate()\n"
            "{\n"
            "}\n\n"
            "void " + className + "::OnUpdate(float deltaTime)\n"
            "{\n"
            "    auto& transform = GetComponent<Limitless::TransformComponent>();\n"
            "    transform.Rotation.z += RotationSpeed * deltaTime;\n"
            "    if (transform.Rotation.z > 360.0f)\n"
            "        transform.Rotation.z -= 360.0f;\n"
            "}\n\n"
            "void " + className + "::OnDestroy()\n"
            "{\n"
            "}\n\n"
            "LT_REGISTER_SCRIPTCORE_SCRIPT(" + className + ");\n";

        {
            std::ofstream headerOutput(headerPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!headerOutput.is_open())
            {
                outError = "Failed to create header file: " + headerPath.string();
                return false;
            }
            headerOutput << headerTemplate;
        }

        {
            std::ofstream sourceOutput(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!sourceOutput.is_open())
            {
                outError = "Failed to create source file: " + sourcePath.string();
                return false;
            }
            sourceOutput << sourceTemplate;
        }

        (void)Assets::AssetImportPipeline::ReimportChanged(true);
        outCreatedSourceAssetKey = "Assets/" + (parentRelativePath / (className + ".cpp")).generic_string();
        outError.clear();
        InvalidateProjectDirectoryCache();
        return true;
    }

    bool CreateManagedScriptInAssets(const std::filesystem::path& assetsDirectory,
                                     const std::filesystem::path& parentRelativePath,
                                     const std::string& requestedClassName,
                                     std::string& outCreatedAssetKey,
                                     std::string& outError)
    {
        const std::string className = SanitizeManagedScriptClassName(requestedClassName);
        const std::filesystem::path scriptDirectory = assetsDirectory / parentRelativePath;

        std::error_code errorCode;
        std::filesystem::create_directories(scriptDirectory, errorCode);
        if (errorCode)
        {
            outError = "Failed to create script directory: " + errorCode.message();
            return false;
        }

        const std::filesystem::path sourcePath = scriptDirectory / (className + ".cs");
        if (std::filesystem::exists(sourcePath))
        {
            outError = "Script already exists: " + className;
            return false;
        }

        const std::string sourceTemplate =
            "using Limitless.Managed;\n\n"
            "namespace GameScripts;\n\n"
            "public sealed class " + className + " : ScriptableEntity\n"
            "{\n"
            "    public override void OnCreate()\n"
            "    {\n"
            "    }\n\n"
            "    public override void OnUpdate(float deltaTime)\n"
            "    {\n"
            "    }\n"
            "}\n";

        {
            std::ofstream sourceOutput(sourcePath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!sourceOutput.is_open())
            {
                outError = "Failed to create script source file: " + sourcePath.string();
                return false;
            }

            sourceOutput.write(sourceTemplate.data(), static_cast<std::streamsize>(sourceTemplate.size()));
            if (!sourceOutput.good())
            {
                outError = "Failed to write script source file: " + sourcePath.string();
                return false;
            }
        }

        (void)Assets::AssetImportPipeline::ReimportChanged(true);
        outCreatedAssetKey = "Assets/" + (parentRelativePath / (className + ".cs")).generic_string();
        outError.clear();
        InvalidateProjectDirectoryCache();
        return true;
    }

    bool RenameNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory,
                                        const std::filesystem::path& scriptRelativePath,
                                        const std::string& newDisplayName,
                                        std::filesystem::path& outNewHeaderRelativePath,
                                        std::filesystem::path& outNewSourceRelativePath)
    {
        const std::string sanitizedBaseName = SanitizeScriptClassBaseName(newDisplayName);

        const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
        const std::string oldClassName = baseRelativePath.stem().string();
        const std::string newClassName = sanitizedBaseName;
        const std::filesystem::path headerRelativePath = baseRelativePath.string() + ".h";
        const std::filesystem::path sourceRelativePath = baseRelativePath.string() + ".cpp";
        const std::filesystem::path headerPath = assetsDirectory / headerRelativePath;
        const std::filesystem::path sourcePath = assetsDirectory / sourceRelativePath;

        std::error_code errorCode;
        if (!std::filesystem::exists(headerPath, errorCode) || !std::filesystem::exists(sourcePath, errorCode))
            return false;

        const std::filesystem::path newBaseRelativePath = baseRelativePath.parent_path() / sanitizedBaseName;
        outNewHeaderRelativePath = newBaseRelativePath.string() + ".h";
        outNewSourceRelativePath = newBaseRelativePath.string() + ".cpp";
        const std::filesystem::path newHeaderPath = assetsDirectory / outNewHeaderRelativePath;
        const std::filesystem::path newSourcePath = assetsDirectory / outNewSourceRelativePath;

        if (newHeaderPath == headerPath && newSourcePath == sourcePath)
            return true;
        if (std::filesystem::exists(newHeaderPath, errorCode) || std::filesystem::exists(newSourcePath, errorCode))
            return false;

        std::filesystem::rename(headerPath, newHeaderPath, errorCode);
        if (errorCode)
            return false;
        std::filesystem::rename(sourcePath, newSourcePath, errorCode);
        if (errorCode)
            return false;

        const std::filesystem::path oldHeaderMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
        const std::filesystem::path oldSourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
        const std::filesystem::path newHeaderMetaPath = newHeaderPath.parent_path() / (newHeaderPath.filename().string() + ".meta");
        const std::filesystem::path newSourceMetaPath = newSourcePath.parent_path() / (newSourcePath.filename().string() + ".meta");
        if (std::filesystem::exists(oldHeaderMetaPath, errorCode))
            std::filesystem::rename(oldHeaderMetaPath, newHeaderMetaPath, errorCode);
        errorCode.clear();
        if (std::filesystem::exists(oldSourceMetaPath, errorCode))
            std::filesystem::rename(oldSourceMetaPath, newSourceMetaPath, errorCode);

        (void)RewriteSourceIncludeForRenamedScriptPair(newSourcePath, headerPath.filename().string(), newHeaderPath.filename().string());
        (void)ReplaceWholeWordInFile(newHeaderPath, oldClassName, newClassName);
        (void)ReplaceWholeWordInFile(newSourcePath, oldClassName, newClassName);
        (void)Assets::AssetImportPipeline::ReimportChanged(true);
        InvalidateProjectDirectoryCache();
        return true;
    }

    bool DeleteNativeScriptPairInAssets(const std::filesystem::path& assetsDirectory, const std::filesystem::path& scriptRelativePath)
    {
        const std::filesystem::path baseRelativePath = scriptRelativePath.parent_path() / scriptRelativePath.stem();
        const std::filesystem::path headerPath = assetsDirectory / (baseRelativePath.string() + ".h");
        const std::filesystem::path sourcePath = assetsDirectory / (baseRelativePath.string() + ".cpp");

        std::error_code errorCode;
        bool removedAny = false;
        if (std::filesystem::exists(headerPath, errorCode))
            removedAny |= std::filesystem::remove(headerPath, errorCode);
        errorCode.clear();
        if (std::filesystem::exists(sourcePath, errorCode))
            removedAny |= std::filesystem::remove(sourcePath, errorCode);
        errorCode.clear();

        const std::filesystem::path headerMetaPath = headerPath.parent_path() / (headerPath.filename().string() + ".meta");
        const std::filesystem::path sourceMetaPath = sourcePath.parent_path() / (sourcePath.filename().string() + ".meta");
        std::filesystem::remove(headerMetaPath, errorCode);
        errorCode.clear();
        std::filesystem::remove(sourceMetaPath, errorCode);

        if (removedAny)
        {
            (void)Assets::AssetImportPipeline::ReimportChanged(true);
            InvalidateProjectDirectoryCache();
        }
        return removedAny;
    }

    void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory,
                                 EditorProjectPanelState& state,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateMaterialRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateTilesetRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAudioMixerRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateInputActionsRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimationClipRequested,
                                 const std::function<void(const std::filesystem::path&, const std::string&)>& onCreateAnimatorControllerRequested,
                                 const std::function<void(const std::string&, const std::string&)>& onAssetRenamed)
    {
        if (state.FolderPopupPending == EditorProjectFolderPopup::Create)
        {
            ImGui::OpenPopup("CreateFolder");
            ImGui::SetNextWindowFocus();
            state.FolderPopupPending = EditorProjectFolderPopup::None;
            state.CreateFolderPopupOpen = true;
        }
        else if (state.FolderPopupPending == EditorProjectFolderPopup::Rename)
        {
            ImGui::OpenPopup("RenameFolder");
            ImGui::SetNextWindowFocus();
            state.FolderPopupPending = EditorProjectFolderPopup::None;
            state.RenameFolderPopupOpen = true;
        }

        if (ImGui::BeginPopupModal("CreateFolder", &state.CreateFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Folder");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("##Name",
                                                 state.FolderPopupBuffer.data(),
                                                 state.FolderPopupBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                if (state.FolderPopupBuffer[0] != '\0')
                {
                    if (ProjectAssetOperations::CreateFolderInDirectory(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                    {
                        InvalidateProjectDirectoryCache();
                        LT_INFO("Created folder {}", state.FolderPopupBuffer.data());
                    }
                    state.CreateFolderPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateFolderPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("RenameFolder", &state.RenameFolderPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Rename Folder");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool rename = ImGui::InputText("##Name",
                                                 state.FolderPopupBuffer.data(),
                                                 state.FolderPopupBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
            {
                if (state.FolderPopupBuffer[0] != '\0')
                {
                    if (ProjectAssetOperations::RenameFolderInAssets(assetsDirectory, state.FolderPopupParent, state.FolderPopupBuffer.data()))
                    {
                        InvalidateProjectDirectoryCache();
                        LT_INFO("Renamed folder to {}", state.FolderPopupBuffer.data());
                    }
                    state.RenameFolderPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.RenameFolderPopupOpen = false;

            ImGui::EndPopup();
        }

        if (state.RenameAssetPopupPending)
        {
            ImGui::OpenPopup("RenameAsset");
            ImGui::SetNextWindowFocus();
            state.RenameAssetPopupPending = false;
            state.RenameAssetPopupOpen = true;
        }

        if (state.CreateNativeScriptPopupPending)
        {
            ImGui::OpenPopup("CreateNativeScriptAsset");
            ImGui::SetNextWindowFocus();
            state.CreateNativeScriptPopupPending = false;
            state.CreateNativeScriptPopupOpen = true;
        }

        if (state.CreateManagedScriptPopupPending)
        {
            ImGui::OpenPopup("CreateManagedScriptAsset");
            ImGui::SetNextWindowFocus();
            state.CreateManagedScriptPopupPending = false;
            state.CreateManagedScriptPopupOpen = true;
        }

        if (state.CreateMaterialPopupPending)
        {
            ImGui::OpenPopup("CreateMaterialAsset");
            ImGui::SetNextWindowFocus();
            state.CreateMaterialPopupPending = false;
            state.CreateMaterialPopupOpen = true;
        }

        if (state.CreateTilesetPopupPending)
        {
            ImGui::OpenPopup("CreateTilesetAsset");
            ImGui::SetNextWindowFocus();
            state.CreateTilesetPopupPending = false;
            state.CreateTilesetPopupOpen = true;
        }

        if (state.CreateTilePalettePopupPending)
        {
            ImGui::OpenPopup("CreateTilePaletteAsset");
            ImGui::SetNextWindowFocus();
            state.CreateTilePalettePopupPending = false;
            state.CreateTilePalettePopupOpen = true;
        }

        if (state.CreateAudioMixerPopupPending)
        {
            ImGui::OpenPopup("CreateAudioMixerAsset");
            ImGui::SetNextWindowFocus();
            state.CreateAudioMixerPopupPending = false;
            state.CreateAudioMixerPopupOpen = true;
        }

        if (state.CreateInputActionsPopupPending)
        {
            ImGui::OpenPopup("CreateInputActionsAsset");
            ImGui::SetNextWindowFocus();
            state.CreateInputActionsPopupPending = false;
            state.CreateInputActionsPopupOpen = true;
        }

        if (state.CreateAnimationClipPopupPending)
        {
            ImGui::OpenPopup("CreateAnimationClipAsset");
            ImGui::SetNextWindowFocus();
            state.CreateAnimationClipPopupPending = false;
            state.CreateAnimationClipPopupOpen = true;
        }

        if (state.CreateAnimatorControllerPopupPending)
        {
            ImGui::OpenPopup("CreateAnimatorControllerAsset");
            ImGui::SetNextWindowFocus();
            state.CreateAnimatorControllerPopupPending = false;
            state.CreateAnimatorControllerPopupOpen = true;
        }

        if (ImGui::BeginPopupModal("RenameAsset", &state.RenameAssetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Rename Asset");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool rename = ImGui::InputText("##AssetName",
                                                 state.RenameAssetBuffer.data(),
                                                 state.RenameAssetBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Rename", ImVec2(120, 0)) || rename)
            {
                if (state.RenameAssetBuffer[0] != '\0')
                {
                    if (state.RenameAssetAsNativeScriptPair)
                    {
                        std::filesystem::path newHeaderRelativePath;
                        std::filesystem::path newSourceRelativePath;
                        if (RenameNativeScriptPairInAssets(
                                assetsDirectory,
                                state.RenameAssetRelativePath,
                                state.RenameAssetBuffer.data(),
                                newHeaderRelativePath,
                                newSourceRelativePath))
                        {
                            if (onAssetRenamed)
                            {
                                const std::filesystem::path oldBase = state.RenameAssetRelativePath.parent_path() / state.RenameAssetRelativePath.stem();
                                onAssetRenamed("Assets/" + (oldBase.generic_string() + ".h"), "Assets/" + newHeaderRelativePath.generic_string());
                                onAssetRenamed("Assets/" + (oldBase.generic_string() + ".cpp"), "Assets/" + newSourceRelativePath.generic_string());
                            }
                            LT_INFO("Renamed script pair to {}", state.RenameAssetBuffer.data());
                        }
                    }
                    else
                    {
                        const std::string oldAssetKey = "Assets/" + state.RenameAssetRelativePath.generic_string();
                        std::filesystem::path newAssetRelativePath;
                        if (ProjectAssetOperations::RenameAssetInAssets(
                                assetsDirectory,
                                state.RenameAssetRelativePath,
                                state.RenameAssetBuffer.data(),
                                &newAssetRelativePath))
                        {
                            InvalidateProjectDirectoryCache();
                            if (onAssetRenamed)
                            {
                                const std::string newAssetKey = "Assets/" + newAssetRelativePath.generic_string();
                                onAssetRenamed(oldAssetKey, newAssetKey);
                            }
                            LT_INFO("Renamed asset to {}", state.RenameAssetBuffer.data());
                        }
                    }
                    state.RenameAssetAsNativeScriptPair = false;
                    state.RenameAssetRelativePath.clear();
                    state.RenameAssetBuffer[0] = '\0';
                    state.RenameAssetPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                state.RenameAssetAsNativeScriptPair = false;
                state.RenameAssetRelativePath.clear();
                state.RenameAssetBuffer[0] = '\0';
                state.RenameAssetPopupOpen = false;
            }

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateNativeScriptAsset", &state.CreateNativeScriptPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Native Script");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Class Name",
                                                 state.CreateNativeScriptClassNameBuffer.data(),
                                                 state.CreateNativeScriptClassNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                std::string createdScriptAssetKey;
                std::string createError;
                if (CreateNativeScriptPairInAssets(
                        assetsDirectory,
                        state.CreateNativeScriptParentRelativePath,
                        state.CreateNativeScriptClassNameBuffer.data(),
                        createdScriptAssetKey,
                        createError))
                {
                    LT_INFO("Created native script {}", createdScriptAssetKey);
                    state.CreateNativeScriptPopupOpen = false;
                }
                else if (!createError.empty())
                {
                    LT_WARN("Failed to create native script: {}", createError);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateNativeScriptPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateManagedScriptAsset", &state.CreateManagedScriptPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create C# Script");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Class Name",
                                                 state.CreateManagedScriptClassNameBuffer.data(),
                                                 state.CreateManagedScriptClassNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                std::string createdScriptAssetKey;
                std::string createError;
                if (CreateManagedScriptInAssets(
                        assetsDirectory,
                        state.CreateManagedScriptParentRelativePath,
                        state.CreateManagedScriptClassNameBuffer.data(),
                        createdScriptAssetKey,
                        createError))
                {
                    LT_INFO("Created managed script {}", createdScriptAssetKey);
                    state.MultiSelectedAssetKeys.clear();
                    state.MultiSelectedAssetKeys.push_back(createdScriptAssetKey);
                    state.SelectionAnchorAssetKey = createdScriptAssetKey;
                    state.MultiSelectedSubSpriteKeys.clear();
                    state.SubSpriteSelectionAnchorKey.clear();

                    std::string buildStatusMessage;
                    const bool buildStarted = EditorInspectorPanel::BuildProjectNativeScripts(&buildStatusMessage);
                    if (!buildStatusMessage.empty())
                    {
                        if (buildStarted)
                            LT_INFO("{}", buildStatusMessage);
                        else
                            LT_WARN("{}", buildStatusMessage);
                    }
                    state.CreateManagedScriptPopupOpen = false;
                }
                else if (!createError.empty())
                {
                    LT_WARN("Failed to create managed script: {}", createError);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateManagedScriptPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateMaterialAsset", &state.CreateMaterialPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Material");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateMaterialNameBuffer.data(),
                                                 state.CreateMaterialNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateMaterialNameBuffer.data();
                if (!requestedName.empty() && onCreateMaterialRequested)
                {
                    onCreateMaterialRequested(state.CreateMaterialParentRelativePath, requestedName);
                    state.CreateMaterialPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateMaterialPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateTilesetAsset", &state.CreateTilesetPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Tileset");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateTilesetNameBuffer.data(),
                                                 state.CreateTilesetNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateTilesetNameBuffer.data();
                if (!requestedName.empty() && onCreateTilesetRequested)
                {
                    onCreateTilesetRequested(state.CreateTilesetParentRelativePath, requestedName);
                    state.CreateTilesetPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateTilesetPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateTilePaletteAsset", &state.CreateTilePalettePopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Tile Palette");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateTilePaletteNameBuffer.data(),
                                                 state.CreateTilePaletteNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateTilePaletteNameBuffer.data();
                if (!requestedName.empty())
                {
                    const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
                    if (rootResult.IsSuccess())
                    {
                        const std::filesystem::path targetDir = rootResult.GetValue() / "Assets" / state.CreateTilePaletteParentRelativePath;
                        std::filesystem::create_directories(targetDir);

                        std::string filename = requestedName + ".tilepalette.json";
                        std::filesystem::path filePath = targetDir / filename;
                        int suffix = 1;
                        while (std::filesystem::exists(filePath))
                        {
                            filename = requestedName + " " + std::to_string(suffix++) + ".tilepalette.json";
                            filePath = targetDir / filename;
                        }

                        Assets::TilePaletteData emptyPalette;
                        const auto writeResult = Assets::WriteTilePaletteFile(filePath, emptyPalette);
                        if (writeResult.IsSuccess())
                        {
                            const std::filesystem::path relPath = std::filesystem::relative(filePath, rootResult.GetValue());
                            const std::string assetKey = relPath.generic_string();
                            Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::TilePalette);
                            EditorTilePalettePanel::InvalidatePaletteKeyCache();
                        }
                    }
                    state.CreateTilePalettePopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateTilePalettePopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateAudioMixerAsset", &state.CreateAudioMixerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Audio Mixer");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateAudioMixerNameBuffer.data(),
                                                 state.CreateAudioMixerNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateAudioMixerNameBuffer.data();
                if (!requestedName.empty() && onCreateAudioMixerRequested)
                {
                    onCreateAudioMixerRequested(state.CreateAudioMixerParentRelativePath, requestedName);
                    state.CreateAudioMixerPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateAudioMixerPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateInputActionsAsset", &state.CreateInputActionsPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Input Actions");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateInputActionsNameBuffer.data(),
                                                 state.CreateInputActionsNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateInputActionsNameBuffer.data();
                if (!requestedName.empty() && onCreateInputActionsRequested)
                {
                    onCreateInputActionsRequested(state.CreateInputActionsParentRelativePath, requestedName);
                    state.CreateInputActionsPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateInputActionsPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateAnimationClipAsset", &state.CreateAnimationClipPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Animation Clip");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateAnimationClipNameBuffer.data(),
                                                 state.CreateAnimationClipNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateAnimationClipNameBuffer.data();
                if (!requestedName.empty() && onCreateAnimationClipRequested)
                {
                    onCreateAnimationClipRequested(state.CreateAnimationClipParentRelativePath, requestedName);
                    state.CreateAnimationClipPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateAnimationClipPopupOpen = false;

            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("CreateAnimatorControllerAsset", &state.CreateAnimatorControllerPopupOpen, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Create Animator Controller");
            ImGui::Separator();
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();

            const bool create = ImGui::InputText("Name",
                                                 state.CreateAnimatorControllerNameBuffer.data(),
                                                 state.CreateAnimatorControllerNameBuffer.size(),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Create", ImVec2(120, 0)) || create)
            {
                const std::string requestedName = state.CreateAnimatorControllerNameBuffer.data();
                if (!requestedName.empty() && onCreateAnimatorControllerRequested)
                {
                    onCreateAnimatorControllerRequested(state.CreateAnimatorControllerParentRelativePath, requestedName);
                    state.CreateAnimatorControllerPopupOpen = false;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                state.CreateAnimatorControllerPopupOpen = false;

            ImGui::EndPopup();
        }
    }
}
