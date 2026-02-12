#include "EditorScenePanel.h"

#include "Scene/Scene.h"
#include "imgui/imgui.h"

namespace Limitless::EditorScenePanel
{
    void Draw(Scene* scene,
              entt::entity& selectedEntity,
              std::string& selectedTextureAssetKey,
              Assets::TextureAsset::Ptr& cachedTextureAsset)
    {
        ImGui::Begin("Scene");

        if (scene)
        {
            if (ImGui::TreeNodeEx("Scene Root", ImGuiTreeNodeFlags_DefaultOpen))
            {
                auto view = scene->GetRegistry().view<TagComponent>();
                for (entt::entity entity : view)
                {
                    const auto& tag = view.get<TagComponent>(entity);
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (selectedEntity == entity)
                        flags |= ImGuiTreeNodeFlags_Selected;

                    ImGui::TreeNodeEx(tag.Tag.c_str(), flags);
                    if (ImGui::IsItemClicked())
                    {
                        selectedEntity = entity;
                        selectedTextureAssetKey.clear();
                        cachedTextureAsset.reset();
                    }
                }

                ImGui::TreePop();
            }
        }

        ImGui::End();
    }
}
