#include "EditorProjectPanel.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "ProjectAssetOperations.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Limitless::EditorProjectPanel
{
    namespace
    {
        void CopyTextToBuffer(std::array<char, 256>& destination, const char* source)
        {
            if (!source)
            {
                destination[0] = '\0';
                return;
            }

            std::snprintf(destination.data(), destination.size(), "%s", source);
        }

        bool IsTextureExtension(const std::filesystem::path& path)
        {
            const std::string extension = path.extension().string();
            if (extension.empty())
                return false;

            std::string extensionLower = extension;
            for (char& character : extensionLower)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

            return extensionLower == ".png" || extensionLower == ".jpg" || extensionLower == ".jpeg" ||
                   extensionLower == ".ppm" || extensionLower == ".pnm" || extensionLower == ".bmp" ||
                   extensionLower == ".tga" || extensionLower == ".gif";
        }

        void DrawAssetTree(const std::filesystem::path& assetsDirectory,
                           const std::filesystem::path& relativePath,
                           EditorProjectPanelState& state,
                           entt::entity& selectedEntity,
                           std::string& selectedTextureAssetKey,
                           Assets::TextureAsset::Ptr& cachedTextureAsset,
                           const char* texturePayloadId,
                           const char* assetMovePayloadId)
        {
            const std::filesystem::path currentDirectory = assetsDirectory / relativePath;
            std::error_code errorCode;
            if (!std::filesystem::exists(currentDirectory, errorCode) || !std::filesystem::is_directory(currentDirectory, errorCode))
                return;

            std::vector<std::filesystem::path> entries;
            for (const auto& entry : std::filesystem::directory_iterator(currentDirectory, errorCode))
            {
                if (errorCode)
                    continue;

                const std::string name = entry.path().filename().string();
                if (name.empty() || name[0] == '.')
                    continue;
                if (name == "Cache")
                    continue;

                std::string extension = entry.path().extension().string();
                for (char& character : extension)
                    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                if (extension == ".meta")
                    continue;

                entries.push_back(entry.path());
            }

            std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                const bool leftDirectory = std::filesystem::is_directory(left);
                const bool rightDirectory = std::filesystem::is_directory(right);
                if (leftDirectory != rightDirectory)
                    return leftDirectory;
                return left.filename().string() < right.filename().string();
            });

            for (const auto& entry : entries)
            {
                const std::string fileName = entry.filename().string();
                const bool isDirectory = std::filesystem::is_directory(entry);
                const std::string assetKey = "Assets/" + (relativePath / fileName).generic_string();
                const std::filesystem::path entryRelativePath = relativePath / fileName;

                if (isDirectory)
                {
                    const bool nodeOpen = ImGui::TreeNodeEx(fileName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

                    if (ImGui::BeginPopupContextItem())
                    {
                        state.FolderPopupParent = entryRelativePath;
                        if (ImGui::MenuItem("Create Folder"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Create;
                            CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
                        }
                        if (ImGui::MenuItem("Rename"))
                        {
                            state.FolderPopupPending = EditorProjectFolderPopup::Rename;
                            CopyTextToBuffer(state.FolderPopupBuffer, fileName.c_str());
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete"))
                        {
                            if (ProjectAssetOperations::DeleteFolderInAssets(assetsDirectory, entryRelativePath))
                                LT_INFO("Deleted folder {}", entryRelativePath.generic_string());
                        }
                        ImGui::EndPopup();
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
                        }
                        else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                if (std::filesystem::path(key).extension().empty())
                                    ProjectAssetOperations::MoveFolderToFolder(key, entryRelativePath);
                                else
                                    ProjectAssetOperations::MoveAssetToFolder(key, entryRelativePath);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        ImGui::Text("%s", fileName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (nodeOpen)
                    {
                        DrawAssetTree(assetsDirectory,
                                      entryRelativePath,
                                      state,
                                      selectedEntity,
                                      selectedTextureAssetKey,
                                      cachedTextureAsset,
                                      texturePayloadId,
                                      assetMovePayloadId);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    const bool isTexture = IsTextureExtension(entry);
                    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    const bool isSelected = isTexture && (selectedTextureAssetKey == assetKey);
                    ImGui::TreeNodeEx(fileName.c_str(), isSelected ? (flags | ImGuiTreeNodeFlags_Selected) : flags);

                    if (isTexture && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                    {
                        selectedTextureAssetKey = assetKey;
                        selectedEntity = entt::null;
                        cachedTextureAsset.reset();
                    }

                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
                    {
                        if (isTexture)
                            ImGui::SetDragDropPayload(texturePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);
                        else
                            ImGui::SetDragDropPayload(assetMovePayloadId, assetKey.c_str(), static_cast<uint32_t>(assetKey.size() + 1), ImGuiCond_Once);

                        ImGui::Text("%s", fileName.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }

        void DrawProjectFolderPopups(const std::filesystem::path& assetsDirectory, EditorProjectPanelState& state)
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
                            LT_INFO("Created folder {}", state.FolderPopupBuffer.data());
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
                            LT_INFO("Renamed folder to {}", state.FolderPopupBuffer.data());
                        state.RenameFolderPopupOpen = false;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    state.RenameFolderPopupOpen = false;

                ImGui::EndPopup();
            }
        }
    }

    void Draw(EditorProjectPanelState& state,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset,
              const char* texturePayloadId,
              const char* assetMovePayloadId)
    {
        ImGui::Begin("Project");

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Could not find Assets folder.");
            ImGui::End();
            return;
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        std::error_code errorCode;
        if (!std::filesystem::exists(assetsDirectory, errorCode) || !std::filesystem::is_directory(assetsDirectory, errorCode))
        {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Assets directory not found.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginPopupContextWindow("ProjectContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Create Folder"))
            {
                state.FolderPopupParent = "";
                state.FolderPopupPending = EditorProjectFolderPopup::Create;
                CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
            }
            ImGui::EndPopup();
        }

        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginPopupContextItem())
            {
                state.FolderPopupParent = "";
                if (ImGui::MenuItem("Create Folder"))
                {
                    state.FolderPopupPending = EditorProjectFolderPopup::Create;
                    CopyTextToBuffer(state.FolderPopupBuffer, "New Folder");
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
                }
                else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetMovePayloadId))
                {
                    const char* key = static_cast<const char*>(payload->Data);
                    if (key && key[0])
                    {
                        if (std::filesystem::path(key).extension().empty())
                            ProjectAssetOperations::MoveFolderToFolder(key, "");
                        else
                            ProjectAssetOperations::MoveAssetToFolder(key, "");
                    }
                }
                ImGui::EndDragDropTarget();
            }

            DrawAssetTree(assetsDirectory,
                          "",
                          state,
                          selectedEntity,
                          selectedTextureAssetKey,
                          cachedTextureAsset,
                          texturePayloadId,
                          assetMovePayloadId);
            ImGui::TreePop();
        }

        DrawProjectFolderPopups(assetsDirectory, state);
        ImGui::End();
    }
}
