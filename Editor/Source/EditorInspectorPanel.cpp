#include "EditorInspectorPanel.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetManager.h"
#include "Assets/TextureAssetImporter.h"
#include "Graphics/Renderer.h"
#include "Scene/Scene.h"
#include "imgui/imgui.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace Limitless::EditorInspectorPanel
{
    namespace
    {
        void InvalidateSpriteCachesForTexture(Scene* scene, const std::string& textureKey)
        {
            if (!scene || textureKey.empty())
                return;

            auto& registry = scene->GetRegistry();
            auto view = registry.view<SpriteComponent>();
            for (entt::entity entity : view)
            {
                auto& sprite = view.get<SpriteComponent>(entity);
                if (sprite.TextureKey == textureKey)
                    sprite.CachedTexture.reset();
            }
        }

        void ApplyTextureSpecificationAndPersist(Scene* scene,
                                                 Assets::TextureAsset::Ptr textureAsset,
                                                 const TextureSpecification& specification)
        {
            auto texture = textureAsset->GetTexture();
            if (!texture)
                return;

            Renderer::GetInstance().ExecuteImmediate(std::make_unique<SetTextureSpecificationCommand>(texture, specification));
            textureAsset->SetSpecification(specification);

            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        void PersistTextureSpecificationAndReload(Scene* scene,
                                                  Assets::TextureAsset::Ptr textureAsset,
                                                  const TextureSpecification& specification)
        {
            auto& database = Assets::AssetDatabase::GetInstance();
            const auto settingsJson = Assets::AssetImporter<Assets::TextureAsset>::SettingsToJson(specification);
            database.ImportOrUpdate(textureAsset->GetKey(), Assets::AssetType::Texture2D, settingsJson);
            textureAsset->SetSpecification(specification);
            textureAsset->Reload();
            InvalidateSpriteCachesForTexture(scene, textureAsset->GetKey());
        }

        void DrawTextureInspector(Scene* scene,
                                  std::string& selectedTextureAssetKey,
                                  Assets::TextureAsset::Ptr& cachedTextureAsset)
        {
            if (!cachedTextureAsset || cachedTextureAsset->GetKey() != selectedTextureAssetKey)
                cachedTextureAsset = Assets::AssetManager::LoadBlocking<Assets::TextureAsset>(selectedTextureAssetKey);

            auto textureAsset = cachedTextureAsset;
            if (!textureAsset)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Failed to load texture: %s", selectedTextureAssetKey.c_str());
                cachedTextureAsset.reset();
                return;
            }

            const auto* texture = textureAsset->GetTexture().get();
            if (!texture)
            {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Texture not ready.");
                return;
            }

            const std::string fileName = std::filesystem::path(selectedTextureAssetKey).filename().string();
            ImGui::Text("Texture: %s", fileName.c_str());
            ImGui::Text("%u x %u", texture->GetWidth(), texture->GetHeight());
            ImGui::Spacing();

            const float previewSize = 256.0f;
            const float aspect = static_cast<float>(texture->GetHeight()) / static_cast<float>(texture->GetWidth());
            const ImVec2 uv0(0.0f, 1.0f);
            const ImVec2 uv1(1.0f, 0.0f);
            const ImVec4 tintColor(1.0f, 1.0f, 1.0f, 1.0f);
            const ImVec4 borderColor(0.4f, 0.4f, 0.4f, 1.0f);
            if (aspect > 1.0f)
            {
                const float width = previewSize / aspect;
                ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(width, previewSize), uv0, uv1, tintColor, borderColor);
            }
            else
            {
                const float height = previewSize * aspect;
                ImGui::Image((ImTextureID)(void*)(uintptr_t)texture->GetRendererID(), ImVec2(previewSize, height), uv0, uv1, tintColor, borderColor);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            TextureSpecification specification = textureAsset->GetSpecification();

            const char* filterNames[] = { "Nearest", "Linear" };
            int minimumFilterIndex = static_cast<int>(specification.MinFilter);
            if (ImGui::Combo("Min Filter", &minimumFilterIndex, filterNames, 2))
            {
                specification.MinFilter = static_cast<TextureFilter>(minimumFilterIndex);
                ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
            }

            int magnificationFilterIndex = static_cast<int>(specification.MagFilter);
            if (ImGui::Combo("Mag Filter", &magnificationFilterIndex, filterNames, 2))
            {
                specification.MagFilter = static_cast<TextureFilter>(magnificationFilterIndex);
                ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
            }

            const char* wrapNames[] = { "Repeat", "Clamp To Edge" };
            int wrapUIndex = static_cast<int>(specification.WrapU);
            if (ImGui::Combo("Wrap U", &wrapUIndex, wrapNames, 2))
            {
                specification.WrapU = static_cast<TextureWrap>(wrapUIndex);
                ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
            }

            int wrapVIndex = static_cast<int>(specification.WrapV);
            if (ImGui::Combo("Wrap V", &wrapVIndex, wrapNames, 2))
            {
                specification.WrapV = static_cast<TextureWrap>(wrapVIndex);
                ApplyTextureSpecificationAndPersist(scene, textureAsset, specification);
            }

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Generate Mipmaps");
            ImGui::SameLine(160);
            bool generateMipmaps = specification.GenerateMipmaps;
            if (ImGui::Checkbox("##GenerateMipmaps", &generateMipmaps))
            {
                specification.GenerateMipmaps = generateMipmaps;
                PersistTextureSpecificationAndReload(scene, textureAsset, specification);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Requires texture reload to take effect.");

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Flip Vertically On Load");
            ImGui::SameLine(160);
            bool flipVerticallyOnLoad = specification.FlipVerticallyOnLoad;
            if (ImGui::Checkbox("##FlipVerticallyOnLoad", &flipVerticallyOnLoad))
            {
                specification.FlipVerticallyOnLoad = flipVerticallyOnLoad;
                PersistTextureSpecificationAndReload(scene, textureAsset, specification);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Requires texture reload to take effect.");
        }
    }

    void Draw(Scene* scene,
              entt::entity selectedEntity,
              const char* texturePayloadId,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        ImGui::Begin("Inspector");

        if (!selectedTextureAssetKey.empty())
        {
            DrawTextureInspector(scene, selectedTextureAssetKey, cachedTextureAsset);
        }
        else if (!scene || selectedEntity == entt::null || !scene->IsValid(selectedEntity))
        {
            ImGui::Text("Select an object to edit.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("No selection.");
        }
        else
        {
            auto& registry = scene->GetRegistry();
            if (auto* tag = registry.try_get<TagComponent>(selectedEntity))
            {
                static entt::entity renameEntity = entt::null;
                static std::array<char, 256> renameBuffer{};
                if (renameEntity != selectedEntity)
                {
                    renameEntity = selectedEntity;
                    std::snprintf(renameBuffer.data(), renameBuffer.size(), "%s", tag->Tag.c_str());
                }

                ImGui::AlignTextToFramePadding();
                ImGui::Text("Name");
                ImGui::SameLine(80);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##EntityName", renameBuffer.data(), renameBuffer.size());
                if (ImGui::IsItemDeactivatedAfterEdit())
                    tag->Tag = renameBuffer.data();
            }

            ImGui::Spacing();
            ImGui::Separator();

            if (auto* transform = registry.try_get<TransformComponent>(selectedEntity))
            {
                if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat3("Position", &transform->Position.x, 0.1f);
                    ImGui::DragFloat3("Rotation", &transform->Rotation.x, 1.0f);
                    ImGui::DragFloat3("Scale", &transform->Scale.x, 0.1f);
                    ImGui::TreePop();
                }
            }

            if (auto* sprite = registry.try_get<SpriteComponent>(selectedEntity))
            {
                if (ImGui::TreeNodeEx("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Image");
                    ImGui::SameLine(80);
                    const char* label = sprite->TextureKey.empty() ? "None" : sprite->TextureKey.c_str();
                    ImGui::Button(label, ImVec2(ImGui::GetContentRegionAvail().x - 60, 0));

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(texturePayloadId))
                        {
                            const char* key = static_cast<const char*>(payload->Data);
                            if (key && key[0])
                            {
                                sprite->TextureKey = key;
                                sprite->CachedTexture.reset();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (!sprite->TextureKey.empty())
                    {
                        ImGui::SameLine();
                        if (ImGui::Button("Clear"))
                        {
                            sprite->TextureKey.clear();
                            sprite->CachedTexture.reset();
                        }
                    }

                    ImGui::ColorEdit4("Color", &sprite->Color.r);
                    ImGui::TreePop();
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
                ImGui::OpenPopup("AddComponentPopup");

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                const bool hasSpriteComponent = registry.all_of<SpriteComponent>(selectedEntity);
                if (hasSpriteComponent)
                    ImGui::BeginDisabled();

                if (ImGui::MenuItem("Sprite Component"))
                    registry.emplace<SpriteComponent>(selectedEntity);

                if (hasSpriteComponent)
                    ImGui::EndDisabled();

                ImGui::EndPopup();
            }
        }

        ImGui::End();
    }
}
