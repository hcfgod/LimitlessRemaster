#include "EditorPrefabSystem.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Limitless::EditorPrefabSystem
{
    namespace
    {
        bool CopyEntitySubtreeToScene(const Scene& sourceScene,
                                      Scene& destinationScene,
                                      entt::entity sourceRootEntity,
                                      entt::entity destinationParentEntity,
                                      const std::string& prefabAssetKey,
                                      entt::entity* outDestinationRootEntity)
        {
            if (!sourceScene.IsValid(sourceRootEntity))
                return false;

            const auto& sourceRegistry = sourceScene.GetRegistry();
            auto& destinationRegistry = destinationScene.GetRegistry();

            std::vector<entt::entity> sourceEntities;
            sourceEntities.push_back(sourceRootEntity);
            for (size_t index = 0; index < sourceEntities.size(); ++index)
            {
                const auto children = sourceScene.GetChildren(sourceEntities[index]);
                sourceEntities.insert(sourceEntities.end(), children.begin(), children.end());
            }

            std::unordered_map<entt::entity, entt::entity> entityMap;
            entityMap.reserve(sourceEntities.size());

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto* sourceTag = sourceRegistry.try_get<TagComponent>(sourceEntity);
                const auto* sourceTransform = sourceRegistry.try_get<TransformComponent>(sourceEntity);
                if (!sourceTag || !sourceTransform)
                    return false;

                const entt::entity destinationEntity = destinationScene.CreateEntity(sourceTag->Tag);
                entityMap.emplace(sourceEntity, destinationEntity);

                destinationRegistry.replace<TransformComponent>(destinationEntity, *sourceTransform);

                if (const auto* sourceSprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
                {
                    auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                    destinationSprite.TextureKey = sourceSprite->TextureKey;
                    destinationSprite.CachedTexture.reset();
                    destinationSprite.TextureLoadAttempted = false;
                    destinationSprite.Color = sourceSprite->Color;
                }

                if (const auto* sourceMaterial = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
                {
                    auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                    destinationMaterial.MaterialKey = sourceMaterial->MaterialKey;
                    destinationMaterial.CachedMaterial.reset();
                    destinationMaterial.MaterialLoadAttempted = false;
                }

                if (const auto* sourceText = sourceRegistry.try_get<TextComponent>(sourceEntity))
                {
                    auto& destinationText = destinationRegistry.emplace<TextComponent>(destinationEntity);
                    destinationText.Text = sourceText->Text;
                    destinationText.FontFilePath = sourceText->FontFilePath;
                    destinationText.CachedFont.reset();
                    destinationText.FontLoadAttempted = false;
                    destinationText.FontSize = sourceText->FontSize;
                    destinationText.Color = sourceText->Color;
                    destinationText.Space = sourceText->Space;
                    destinationText.Anchor = sourceText->Anchor;
                }

                if (const auto* sourceCamera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
                    destinationRegistry.emplace<CameraComponent>(destinationEntity, *sourceCamera);

                if (const auto* sourceAudio = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
                {
                    auto& destinationAudio = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                    destinationAudio.AudioClipKey = sourceAudio->AudioClipKey;
                    destinationAudio.Volume = sourceAudio->Volume;
                    destinationAudio.PlayOnStart = sourceAudio->PlayOnStart;
                    destinationAudio.Loop = sourceAudio->Loop;
                    destinationAudio.Muted = sourceAudio->Muted;
                    destinationAudio.RuntimeVoiceId = 0;
                    destinationAudio.RuntimePlaybackStarted = false;
                }

                if (const auto* sourceScripts = sourceRegistry.try_get<NativeScriptComponent>(sourceEntity))
                {
                    auto& destinationScripts = destinationRegistry.emplace<NativeScriptComponent>(destinationEntity);
                    destinationScripts.Scripts.reserve(sourceScripts->Scripts.size());
                    for (const auto& sourceScriptEntry : sourceScripts->Scripts)
                    {
                        auto& destinationScriptEntry = destinationScripts.Scripts.emplace_back();
                        destinationScriptEntry.ScriptClassName = sourceScriptEntry.ScriptClassName;
                        destinationScriptEntry.ScriptAssetRelativePath = sourceScriptEntry.ScriptAssetRelativePath;
                        destinationScriptEntry.Enabled = sourceScriptEntry.Enabled;
                        destinationScriptEntry.ExposedProperties = sourceScriptEntry.ExposedProperties;
                        destinationScriptEntry.RuntimeInitialized = false;
                        destinationScriptEntry.RuntimeInstance.reset();
                    }
                }
            }

            for (entt::entity sourceEntity : sourceEntities)
            {
                const auto mappedEntity = entityMap.find(sourceEntity);
                if (mappedEntity == entityMap.end())
                    continue;
                const entt::entity destinationEntity = mappedEntity->second;

                auto* destinationHierarchy = destinationRegistry.try_get<HierarchyComponent>(destinationEntity);
                if (!destinationHierarchy)
                    destinationHierarchy = &destinationRegistry.emplace<HierarchyComponent>(destinationEntity);

                const entt::entity sourceParent = sourceScene.GetParent(sourceEntity);
                if (sourceParent != entt::null)
                {
                    const auto mappedParent = entityMap.find(sourceParent);
                    destinationHierarchy->Parent = (mappedParent != entityMap.end()) ? mappedParent->second : destinationParentEntity;
                }
                else
                {
                    destinationHierarchy->Parent = destinationParentEntity;
                }

                if (const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity))
                    destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
                else
                    destinationHierarchy->SiblingOrder = 0;
            }

            const auto mappedRoot = entityMap.find(sourceRootEntity);
            if (mappedRoot == entityMap.end())
                return false;

            if (!prefabAssetKey.empty())
            {
                auto& prefabInstance = destinationRegistry.emplace_or_replace<PrefabInstanceComponent>(mappedRoot->second);
                prefabInstance.PrefabAssetKey = prefabAssetKey;
            }

            if (outDestinationRootEntity)
                *outDestinationRootEntity = mappedRoot->second;
            return true;
        }

        bool EnsureParentDirectoryExists(const std::filesystem::path& filePath)
        {
            std::error_code errorCode;
            const std::filesystem::path parentPath = filePath.parent_path();
            if (parentPath.empty())
                return true;

            if (!std::filesystem::exists(parentPath, errorCode))
                std::filesystem::create_directories(parentPath, errorCode);
            return !errorCode;
        }
    }

    bool CreateOrUpdatePrefabFromEntity(Scene& scene, entt::entity rootEntity, const std::string& prefabAssetKey)
    {
        if (!scene.IsValid(rootEntity) || prefabAssetKey.empty())
            return false;

        const auto resolvedPrefabPath = Assets::ResolveAssetKeyToPath(prefabAssetKey);
        if (resolvedPrefabPath.IsFailure())
            return false;
        if (!EnsureParentDirectoryExists(resolvedPrefabPath.GetValue()))
            return false;

        Scene prefabScene;
        entt::entity prefabRoot = entt::null;
        if (!CopyEntitySubtreeToScene(scene, prefabScene, rootEntity, entt::null, {}, &prefabRoot))
            return false;
        if (prefabRoot == entt::null)
            return false;

        auto& prefabRegistry = prefabScene.GetRegistry();
        prefabRegistry.remove<PrefabInstanceComponent>(prefabRoot);

        const auto saveResult = prefabScene.SaveToFile(resolvedPrefabPath.GetValue());
        if (saveResult.IsFailure())
        {
            LT_WARN("Prefab save failed for '{}': {}", prefabAssetKey, saveResult.GetError().GetErrorMessage());
            return false;
        }

        auto& sceneRegistry = scene.GetRegistry();
        auto& prefabInstance = sceneRegistry.emplace_or_replace<PrefabInstanceComponent>(rootEntity);
        prefabInstance.PrefabAssetKey = prefabAssetKey;
        return true;
    }

    entt::entity InstantiatePrefab(Scene& destinationScene, const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (prefabAssetKey.empty())
            return entt::null;

        const auto resolvedPrefabPath = Assets::ResolveAssetKeyToPath(prefabAssetKey);
        if (resolvedPrefabPath.IsFailure())
            return entt::null;

        const auto loadedPrefabSceneResult = Scene::LoadFromFile(resolvedPrefabPath.GetValue());
        if (loadedPrefabSceneResult.IsFailure())
        {
            LT_WARN("Prefab load failed for '{}': {}", prefabAssetKey, loadedPrefabSceneResult.GetError().GetErrorMessage());
            return entt::null;
        }

        auto& loadedPrefabScene = *loadedPrefabSceneResult.GetValue();
        const auto prefabRoots = loadedPrefabScene.GetChildren(entt::null);
        if (prefabRoots.empty())
            return entt::null;

        entt::entity createdRoot = entt::null;
        if (!CopyEntitySubtreeToScene(loadedPrefabScene, destinationScene, prefabRoots.front(), parentEntity, prefabAssetKey, &createdRoot))
            return entt::null;

        return createdRoot;
    }

    bool ApplyPrefabFromInstance(Scene& scene, entt::entity instanceRootEntity)
    {
        if (!scene.IsValid(instanceRootEntity))
            return false;

        const auto* prefabInstance = scene.GetRegistry().try_get<PrefabInstanceComponent>(instanceRootEntity);
        if (!prefabInstance || prefabInstance->PrefabAssetKey.empty())
            return false;

        return CreateOrUpdatePrefabFromEntity(scene, instanceRootEntity, prefabInstance->PrefabAssetKey);
    }

    entt::entity RevertPrefabInstance(Scene& scene, entt::entity instanceRootEntity)
    {
        if (!scene.IsValid(instanceRootEntity))
            return entt::null;

        auto& registry = scene.GetRegistry();
        const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(instanceRootEntity);
        if (!prefabInstance || prefabInstance->PrefabAssetKey.empty())
            return entt::null;

        const std::string prefabAssetKey = prefabInstance->PrefabAssetKey;
        const entt::entity previousParent = scene.GetParent(instanceRootEntity);

        scene.DestroyEntity(instanceRootEntity);
        return InstantiatePrefab(scene, prefabAssetKey, previousParent);
    }

    bool UnpackPrefabInstance(Scene& scene, entt::entity instanceRootEntity)
    {
        if (!scene.IsValid(instanceRootEntity))
            return false;
        return scene.GetRegistry().remove<PrefabInstanceComponent>(instanceRootEntity) > 0;
    }
}
