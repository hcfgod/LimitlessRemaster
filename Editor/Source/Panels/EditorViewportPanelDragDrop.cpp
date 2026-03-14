#include "EditorViewportPanelShared.h"

#include "EditorInspectorPanelNativeScriptEditor.h"
#include "Graphics/Camera/Camera.h"
#include "Scene/Scene.h"
#include "Scripting/ManagedScriptHost.h"
#include "Undo/EditorUndoService.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace Limitless::EditorViewportPanel::Internal
{
    namespace
    {
        bool IsNativeScriptAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string lowerKey = ToLowerAscii(NormalizeSlashes(assetKey));
            return lowerKey.rfind("assets/", 0) == 0 &&
                   (lowerKey.ends_with(".h") || lowerKey.ends_with(".cpp"));
        }

        bool IsManagedScriptAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;

            const std::string lowerKey = ToLowerAscii(NormalizeSlashes(assetKey));
            return lowerKey.rfind("assets/", 0) == 0 && lowerKey.ends_with(".cs");
        }

        NativeScriptEntry BuildScriptEntryFromAssetKey(const std::string& assetKey)
        {
            NativeScriptEntry scriptEntry{};
            std::string relativePath = NormalizeSlashes(assetKey);
            if (relativePath.rfind("Assets/", 0) == 0)
                relativePath.erase(0, 7);

            std::filesystem::path scriptPath(relativePath);
            scriptPath.replace_extension();
            scriptEntry.ScriptAssetRelativePath = scriptPath.generic_string();

            const std::string requestedClassName = scriptPath.stem().string();
            const std::string resolvedClassName = EditorInspectorPanel::ResolveRegisteredScriptClassNameForInspector(requestedClassName);
            scriptEntry.ScriptClassName = resolvedClassName.empty() ? requestedClassName : resolvedClassName;
            return scriptEntry;
        }

        ManagedScriptEntry BuildManagedScriptEntryFromAssetKey(const std::string& assetKey)
        {
            ManagedScriptEntry scriptEntry{};
            std::string relativePath = NormalizeSlashes(assetKey);
            if (relativePath.rfind("Assets/", 0) == 0)
                relativePath.erase(0, 7);

            scriptEntry.ScriptAssetRelativePath = relativePath;
            const std::string requestedClassName = std::filesystem::path(relativePath).stem().string();
            const std::string resolvedClassName = ManagedScriptHost::ResolveDiscoveredClassName(requestedClassName);
            scriptEntry.ScriptClassName = resolvedClassName.empty() ? requestedClassName : resolvedClassName;
            return scriptEntry;
        }

        bool TryAttachScriptAssetToEntity(Scene* scene,
                                          entt::entity ownerEntity,
                                          const std::string& assetKey,
                                          EditorUndoService* undoService)
        {
            if (!scene || !scene->IsValid(ownerEntity))
                return false;

            if (IsNativeScriptAssetKey(assetKey))
            {
                NativeScriptEntry scriptEntry = BuildScriptEntryFromAssetKey(assetKey);
                if (scriptEntry.ScriptClassName.empty() && scriptEntry.ScriptAssetRelativePath.empty())
                    return false;

                if (undoService)
                {
                    return undoService->ExecuteSceneMutation("Attach Native Script", [&](Scene& mutableScene) {
                        return mutableScene.AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
                    });
                }

                return scene->AttachScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
            }

            if (!IsManagedScriptAssetKey(assetKey))
                return false;

            ManagedScriptEntry scriptEntry = BuildManagedScriptEntryFromAssetKey(assetKey);
            if (scriptEntry.ScriptClassName.empty() && scriptEntry.ScriptAssetRelativePath.empty())
                return false;

            if (undoService)
            {
                return undoService->ExecuteSceneMutation("Attach Managed Script", [&](Scene& mutableScene) {
                    return mutableScene.AttachManagedScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
                });
            }

            return scene->AttachManagedScriptComponent(ownerEntity, std::move(scriptEntry)) != entt::null;
        }

        void ClearSelectedViewportAssetState(ViewportPanelContext& context)
        {
            context.SelectedTextureAssetKey.clear();
            context.CachedTextureAsset.reset();
            context.SelectedMaterialAssetKey.clear();
            context.CachedMaterialAsset.reset();
            context.SelectedNativeScriptAssetKey.clear();
        }
    }

    std::string NormalizeSlashes(std::string pathText)
    {
        std::replace(pathText.begin(), pathText.end(), '\\', '/');
        return pathText;
    }

    std::string ToLowerAscii(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return text;
    }

    void HandleSceneViewDragDrop(ViewportPanelContext& context, const ImVec2& viewportMin, const ImVec2& viewportMax)
    {
        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(context.ScenePayloadId))
        {
            const char* key = static_cast<const char*>(payload->Data);
            if (key && key[0] && context.OnSceneDropped)
                context.PendingDroppedSceneAssetKey = key;
        }

        if (context.PrefabPayloadId)
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(context.PrefabPayloadId))
            {
                std::string key;
                if (payload->Data && payload->DataSize > 0)
                {
                    const auto* keyChars = static_cast<const char*>(payload->Data);
                    const int keyLength = std::max(0, payload->DataSize - 1);
                    key.assign(keyChars, keyChars + keyLength);
                }
                if (!key.empty() && context.OnPrefabDropped)
                {
                    glm::vec3 worldPosition(0.0f);
                    if (context.SceneViewCamera)
                    {
                        const ImVec2 mousePos = ImGui::GetMousePos();
                        if (!TryComputeDropWorldPosition(*context.SceneViewCamera, viewportMin, viewportMax, mousePos, worldPosition))
                            worldPosition = glm::vec3(0.0f);
                    }
                    context.OnPrefabDropped(key, worldPosition);
                }
            }
        }

        if (context.MaterialPayloadId)
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(context.MaterialPayloadId))
            {
                const char* key = static_cast<const char*>(payload->Data);
                if (key && key[0] && context.SceneContext && context.SceneViewCamera)
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();

                    entt::entity targetEntity = entt::null;
                    if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                        mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                    {
                        const float viewportWidth = viewportMax.x - viewportMin.x;
                        const float viewportHeight = viewportMax.y - viewportMin.y;
                        const auto picked = PickTopmostSpriteEntityAtPoint(*context.SceneContext,
                                                                           *context.SceneViewCamera,
                                                                           viewportMin,
                                                                           viewportWidth,
                                                                           viewportHeight,
                                                                           mousePos);
                        if (picked.has_value())
                            targetEntity = *picked;
                    }

                    if (targetEntity == entt::null && context.SelectedEntity != entt::null && context.SceneContext->IsValid(context.SelectedEntity))
                        targetEntity = context.SelectedEntity;

                    if (targetEntity != entt::null && context.SceneContext->IsValid(targetEntity))
                    {
                        auto& registry = context.SceneContext->GetRegistry();
                        if (registry.all_of<SpriteComponent>(targetEntity))
                        {
                            auto* material = registry.try_get<MaterialComponent>(targetEntity);
                            if (!material)
                                material = &registry.emplace<MaterialComponent>(targetEntity);

                            material->MaterialKey = key;
                            material->CachedMaterial.reset();
                            material->MaterialLoadAttempted = false;

                            context.SelectedEntity = targetEntity;
                            ClearSelectedViewportAssetState(context);
                        }
                    }
                }
            }
        }

        if (context.AssetMovePayloadId)
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(context.AssetMovePayloadId))
            {
                std::string assetKey;
                if (payload->Data && payload->DataSize > 0)
                {
                    const auto* keyChars = static_cast<const char*>(payload->Data);
                    const int keyLength = std::max(0, payload->DataSize - 1);
                    assetKey.assign(keyChars, keyChars + keyLength);
                }

                if (!assetKey.empty() && context.SceneContext && context.SceneViewCamera)
                {
                    const ImVec2 mousePos = ImGui::GetMousePos();

                    entt::entity targetEntity = entt::null;
                    if (mousePos.x >= viewportMin.x && mousePos.x <= viewportMax.x &&
                        mousePos.y >= viewportMin.y && mousePos.y <= viewportMax.y)
                    {
                        const float viewportWidth = viewportMax.x - viewportMin.x;
                        const float viewportHeight = viewportMax.y - viewportMin.y;
                        const auto picked = PickTopmostSpriteEntityAtPoint(*context.SceneContext,
                                                                           *context.SceneViewCamera,
                                                                           viewportMin,
                                                                           viewportWidth,
                                                                           viewportHeight,
                                                                           mousePos);
                        if (picked.has_value())
                            targetEntity = *picked;
                    }

                    if (targetEntity == entt::null && context.SelectedEntity != entt::null && context.SceneContext->IsValid(context.SelectedEntity))
                        targetEntity = context.SelectedEntity;

                    if (targetEntity != entt::null && context.SceneContext->IsValid(targetEntity) &&
                        TryAttachScriptAssetToEntity(context.SceneContext, targetEntity, assetKey, context.UndoService))
                    {
                        context.SelectedEntity = targetEntity;
                        ClearSelectedViewportAssetState(context);
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }
}
