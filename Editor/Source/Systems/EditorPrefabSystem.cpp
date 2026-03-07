#include "EditorPrefabSystem.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "Scene/Scene.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace Limitless::EditorPrefabSystem
{
    namespace
    {
        constexpr float kParentInverseDeterminantEpsilon = 1e-6f;

        bool IsFiniteMatrix(const glm::mat4& matrix)
        {
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (!std::isfinite(matrix[column][row]))
                        return false;
                }
            }
            return true;
        }

        bool IsFiniteVec3(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteQuat(const glm::quat& value)
        {
            return std::isfinite(value.w) && std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool TryAssignLocalTransformFromWorld(const glm::mat4& parentWorld,
                                              const glm::mat4& childWorld,
                                              TransformComponent& destinationTransform)
        {
            if (!IsFiniteMatrix(parentWorld) || !IsFiniteMatrix(childWorld))
                return false;

            const float determinant = glm::determinant(parentWorld);
            if (!std::isfinite(determinant) || std::abs(determinant) <= kParentInverseDeterminantEpsilon)
                return false;

            const glm::mat4 childLocal = glm::inverse(parentWorld) * childWorld;
            if (!IsFiniteMatrix(childLocal))
                return false;

            glm::vec3 skew(0.0f);
            glm::vec4 perspective(0.0f);
            glm::quat orientation(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 translation(0.0f);
            glm::vec3 scale(1.0f);
            if (!glm::decompose(childLocal, scale, orientation, translation, skew, perspective))
                return false;
            if (!IsFiniteVec3(translation) || !IsFiniteVec3(scale) || !IsFiniteQuat(orientation))
                return false;

            const float orientationLengthSquared =
                orientation.w * orientation.w +
                orientation.x * orientation.x +
                orientation.y * orientation.y +
                orientation.z * orientation.z;
            if (!std::isfinite(orientationLengthSquared) || orientationLengthSquared <= 0.0f)
                return false;
            orientation = glm::normalize(orientation);

            const glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(orientation));
            if (!IsFiniteVec3(eulerDegrees))
                return false;

            destinationTransform.Position = translation;
            destinationTransform.Rotation = eulerDegrees;
            destinationTransform.Scale = scale;
            return true;
        }

        bool IsNullEntity(entt::entity entity)
        {
            return entity == entt::null;
        }

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
            const entt::entity resolvedDestinationParentEntity =
                (destinationParentEntity != entt::null && destinationScene.IsValid(destinationParentEntity))
                ? destinationParentEntity
                : entt::null;

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
                if (auto* destinationTag = destinationRegistry.try_get<TagComponent>(destinationEntity))
                    destinationTag->Enabled = sourceTag->Enabled;

                destinationRegistry.replace<TransformComponent>(destinationEntity, *sourceTransform);

                if (const auto* sourceCanvas = sourceRegistry.try_get<CanvasComponent>(sourceEntity))
                {
                    destinationRegistry.emplace<CanvasComponent>(destinationEntity, *sourceCanvas);
                }

                if (const auto* sourceRectTransform = sourceRegistry.try_get<RectTransformComponent>(sourceEntity))
                {
                    destinationRegistry.emplace<RectTransformComponent>(destinationEntity, *sourceRectTransform);
                }

                if (const auto* sourceSprite = sourceRegistry.try_get<SpriteComponent>(sourceEntity))
                {
                    auto& destinationSprite = destinationRegistry.emplace<SpriteComponent>(destinationEntity);
                    destinationSprite.TextureKey = sourceSprite->TextureKey;
                    destinationSprite.CachedTexture.reset();
                    destinationSprite.TextureLoadAttempted = false;
                    destinationSprite.Color = sourceSprite->Color;
                    destinationSprite.TilingFactor = sourceSprite->TilingFactor;
                    destinationSprite.CastShadows = sourceSprite->CastShadows;
                    destinationSprite.ReceiveShadows = sourceSprite->ReceiveShadows;
                }

                if (const auto* sourceMaterial = sourceRegistry.try_get<MaterialComponent>(sourceEntity))
                {
                    auto& destinationMaterial = destinationRegistry.emplace<MaterialComponent>(destinationEntity);
                    destinationMaterial.MaterialKey = sourceMaterial->MaterialKey;
                    destinationMaterial.CachedMaterial.reset();
                    destinationMaterial.MaterialLoadAttempted = false;
                }

                if (const auto* sourceDirectionalLight = sourceRegistry.try_get<DirectionalLight2DComponent>(sourceEntity))
                {
                    auto& destinationDirectionalLight = destinationRegistry.emplace<DirectionalLight2DComponent>(destinationEntity, *sourceDirectionalLight);
                    destinationDirectionalLight.RuntimeResolvedDirection = glm::vec2(0.0f, -1.0f);
                }

                if (const auto* sourcePointLight = sourceRegistry.try_get<PointLight2DComponent>(sourceEntity))
                {
                    auto& destinationPointLight = destinationRegistry.emplace<PointLight2DComponent>(destinationEntity, *sourcePointLight);
                    destinationPointLight.RuntimeViewportPosition = glm::vec2(0.0f);
                    destinationPointLight.RuntimeViewportRadius = 0.0f;
                }

                if (const auto* sourceShadowOccluder = sourceRegistry.try_get<ShadowOccluder2DComponent>(sourceEntity))
                {
                    auto& destinationShadowOccluder = destinationRegistry.emplace<ShadowOccluder2DComponent>(destinationEntity, *sourceShadowOccluder);
                    destinationShadowOccluder.RuntimeResolvedPolygonPoints.clear();
                    destinationShadowOccluder.RuntimeGeometryRevision = 0;
                }

                if (const auto* sourceUIImage = sourceRegistry.try_get<UIImageComponent>(sourceEntity))
                {
                    destinationRegistry.emplace<UIImageComponent>(destinationEntity, *sourceUIImage);
                }

                if (const auto* sourceUIPanel = sourceRegistry.try_get<UIPanelComponent>(sourceEntity))
                {
                    destinationRegistry.emplace<UIPanelComponent>(destinationEntity, *sourceUIPanel);
                }

                if (const auto* sourceUIText = sourceRegistry.try_get<UITextComponent>(sourceEntity))
                {
                    auto& destinationUIText = destinationRegistry.emplace<UITextComponent>(destinationEntity);
                    destinationUIText.Text = sourceUIText->Text;
                    destinationUIText.FontFilePath = sourceUIText->FontFilePath;
                    destinationUIText.CachedFont.reset();
                    destinationUIText.FontLoadAttempted = false;
                    destinationUIText.FontSize = sourceUIText->FontSize;
                    destinationUIText.Color = sourceUIText->Color;
                    destinationUIText.RaycastTarget = sourceUIText->RaycastTarget;
                }

                if (const auto* sourceUIButton = sourceRegistry.try_get<UIButtonComponent>(sourceEntity))
                {
                    auto& destinationUIButton = destinationRegistry.emplace<UIButtonComponent>(destinationEntity, *sourceUIButton);
                    destinationUIButton.IsHovered = false;
                    destinationUIButton.IsPressed = false;
                    destinationUIButton.RuntimeHoverEnteredThisFrame = false;
                    destinationUIButton.RuntimeHoverExitedThisFrame = false;
                    destinationUIButton.RuntimePressedThisFrame = false;
                    destinationUIButton.RuntimeClickedThisFrame = false;
                }

                if (const auto* sourceUISlider = sourceRegistry.try_get<UISliderComponent>(sourceEntity))
                {
                    auto& destinationUISlider = destinationRegistry.emplace<UISliderComponent>(destinationEntity, *sourceUISlider);
                    destinationUISlider.Value = std::clamp(destinationUISlider.Value, destinationUISlider.MinValue, destinationUISlider.MaxValue);
                    destinationUISlider.RuntimeDragging = false;
                    destinationUISlider.RuntimeValueChangedThisFrame = false;
                }

                if (const auto* sourceCamera = sourceRegistry.try_get<CameraComponent>(sourceEntity))
                    destinationRegistry.emplace<CameraComponent>(destinationEntity, *sourceCamera);

                if (const auto* sourceAudioListener = sourceRegistry.try_get<AudioListener2DComponent>(sourceEntity))
                    destinationRegistry.emplace<AudioListener2DComponent>(destinationEntity, *sourceAudioListener);

                if (const auto* sourceAudioListener3D = sourceRegistry.try_get<AudioListener3DComponent>(sourceEntity))
                {
                    auto& destinationAudioListener3D = destinationRegistry.emplace<AudioListener3DComponent>(destinationEntity, *sourceAudioListener3D);
                    destinationAudioListener3D.RuntimeHasPreviousWorldPosition = false;
                    destinationAudioListener3D.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                }

                if (const auto* sourceAudio = sourceRegistry.try_get<AudioSourceComponent>(sourceEntity))
                {
                    auto& destinationAudio = destinationRegistry.emplace<AudioSourceComponent>(destinationEntity);
                    destinationAudio.AudioClipKey = sourceAudio->AudioClipKey;
                    destinationAudio.Volume = sourceAudio->Volume;
                    destinationAudio.Pitch = sourceAudio->Pitch;
                    destinationAudio.PlayOnStart = sourceAudio->PlayOnStart;
                    destinationAudio.Loop = sourceAudio->Loop;
                    destinationAudio.Muted = sourceAudio->Muted;
                    destinationAudio.Space = sourceAudio->Space;
                    destinationAudio.MixerGroup = sourceAudio->MixerGroup;
                    destinationAudio.SpatialMinDistance = sourceAudio->SpatialMinDistance;
                    destinationAudio.SpatialMaxDistance = sourceAudio->SpatialMaxDistance;
                    destinationAudio.SpatialRolloffExponent = sourceAudio->SpatialRolloffExponent;
                    destinationAudio.StereoPanStrength = sourceAudio->StereoPanStrength;
                    destinationAudio.SpatialRolloffMode = sourceAudio->SpatialRolloffMode;
                    destinationAudio.DopplerFactor = sourceAudio->DopplerFactor;
                    destinationAudio.EnableDirectionalAttenuation = sourceAudio->EnableDirectionalAttenuation;
                    destinationAudio.DirectionalInnerAngleDegrees = sourceAudio->DirectionalInnerAngleDegrees;
                    destinationAudio.DirectionalOuterAngleDegrees = sourceAudio->DirectionalOuterAngleDegrees;
                    destinationAudio.DirectionalOuterVolume = sourceAudio->DirectionalOuterVolume;
                    destinationAudio.AttenuationCurveKey = sourceAudio->AttenuationCurveKey;
                    destinationAudio.RuntimeVoiceId = 0;
                    destinationAudio.RuntimePlaybackStarted = false;
                    destinationAudio.RuntimePlayOnStartConsumed = false;
                    destinationAudio.RuntimeHasPreviousWorldPosition = false;
                    destinationAudio.RuntimePreviousWorldPosition = glm::vec3(0.0f);
                }

                const auto sourceScriptEntities = sourceScene.GetScriptComponentEntities(sourceEntity);
                for (entt::entity sourceScriptEntity : sourceScriptEntities)
                {
                    const auto* sourceScriptComponent = sourceScene.GetScriptComponent(sourceScriptEntity);
                    if (!sourceScriptComponent || sourceScriptComponent->OwnerEntity != sourceEntity)
                        continue;

                    NativeScriptEntry destinationScriptEntry{};
                    destinationScriptEntry.ScriptClassName = sourceScriptComponent->Script.ScriptClassName;
                    destinationScriptEntry.ScriptAssetRelativePath = sourceScriptComponent->Script.ScriptAssetRelativePath;
                    destinationScriptEntry.Enabled = sourceScriptComponent->Script.Enabled;
                    destinationScriptEntry.ExecutionPolicy = sourceScriptComponent->Script.ExecutionPolicy;
                    destinationScriptEntry.DeclaredReadAccessMask = sourceScriptComponent->Script.DeclaredReadAccessMask;
                    destinationScriptEntry.DeclaredWriteAccessMask = sourceScriptComponent->Script.DeclaredWriteAccessMask;
                    destinationScriptEntry.ExposedProperties = sourceScriptComponent->Script.ExposedProperties;
                    destinationScriptEntry.RuntimeExposedPropertiesRevision = 1;
                    destinationScriptEntry.RuntimeInitialized = false;
                    destinationScriptEntry.RuntimeInstance.reset();
                    destinationScriptEntry.RuntimeUpdateCount = 0;
                    destinationScriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    destinationScriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    destinationScriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                    destinationScriptEntry.RuntimeWarnedAccessMaskMismatch = false;

                    const entt::entity destinationScriptEntity = destinationScene.AttachScriptComponent(destinationEntity, std::move(destinationScriptEntry));
                    if (auto* attachedScriptComponent = destinationScene.GetScriptComponent(destinationScriptEntity))
                        attachedScriptComponent->ComponentOrder = sourceScriptComponent->ComponentOrder;
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
                    destinationHierarchy->Parent = (mappedParent != entityMap.end()) ? mappedParent->second : resolvedDestinationParentEntity;
                }
                else
                {
                    destinationHierarchy->Parent = resolvedDestinationParentEntity;
                }

                if (const auto* sourceHierarchy = sourceRegistry.try_get<HierarchyComponent>(sourceEntity))
                    destinationHierarchy->SiblingOrder = sourceHierarchy->SiblingOrder;
                else
                    destinationHierarchy->SiblingOrder = 0;

                // Keep prefab root world transform stable when instantiating under a parent.
                if (sourceEntity == sourceRootEntity && resolvedDestinationParentEntity != entt::null)
                {
                    if (auto* destinationTransform = destinationRegistry.try_get<TransformComponent>(destinationEntity))
                    {
                        const glm::mat4 sourceRootWorld = sourceScene.GetWorldTransformMatrix(sourceEntity);
                        const glm::mat4 parentWorld = destinationScene.GetWorldTransformMatrix(resolvedDestinationParentEntity);
                        if (TryAssignLocalTransformFromWorld(parentWorld, sourceRootWorld, *destinationTransform))
                        {
                            destinationScene.MarkTransformDirty(destinationEntity);
                        }
                    }
                }
            }

            const auto mappedRoot = entityMap.find(sourceRootEntity);
            if (mappedRoot == entityMap.end())
                return false;

            if (!prefabAssetKey.empty())
            {
                auto& prefabInstance = destinationRegistry.emplace_or_replace<PrefabInstanceComponent>(mappedRoot->second);
                prefabInstance.PrefabAssetKey = prefabAssetKey;
            }

            destinationScene.MarkTransformDirty(mappedRoot->second);

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

        std::string NormalizeLooseAssetKey(const std::string& assetKey)
        {
            auto trim = [](std::string_view value) -> std::string {
                size_t begin = 0;
                while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
                    ++begin;
                size_t end = value.size();
                while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
                    --end;
                return std::string(value.substr(begin, end - begin));
            };

            std::string normalized = trim(assetKey);
            if (normalized.empty())
                return normalized;

            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            std::string collapsed;
            collapsed.reserve(normalized.size());
            bool previousWasSlash = false;
            for (char character : normalized)
            {
                if (character == '/')
                {
                    if (previousWasSlash)
                        continue;
                    previousWasSlash = true;
                    collapsed.push_back(character);
                }
                else
                {
                    previousWasSlash = false;
                    collapsed.push_back(character);
                }
            }

            std::string rebuilt;
            rebuilt.reserve(collapsed.size());
            size_t segmentStart = 0;
            while (segmentStart <= collapsed.size())
            {
                const size_t separator = collapsed.find('/', segmentStart);
                const bool hasSeparator = separator != std::string::npos;
                const size_t segmentEnd = hasSeparator ? separator : collapsed.size();
                const std::string trimmedSegment = trim(std::string_view(collapsed).substr(segmentStart, segmentEnd - segmentStart));
                rebuilt += trimmedSegment;
                if (hasSeparator)
                {
                    rebuilt.push_back('/');
                    segmentStart = separator + 1;
                }
                else
                {
                    break;
                }
            }

            return rebuilt;
        }

        Result<std::unique_ptr<Scene>> LoadPrefabSceneFromAssetKey(const std::string& prefabAssetKey, std::string& outEffectivePrefabAssetKey)
        {
            outEffectivePrefabAssetKey = prefabAssetKey;

            auto loadedPrefabSceneResult = Scene::LoadFromFile(prefabAssetKey);
            if (!loadedPrefabSceneResult.IsFailure())
                return loadedPrefabSceneResult;

            if (const auto resolvedPath = Assets::ResolveAssetKeyToPath(prefabAssetKey); resolvedPath.IsSuccess())
            {
                auto resolvedLoadResult = Scene::LoadFromFile(resolvedPath.GetValue());
                if (!resolvedLoadResult.IsFailure())
                    return resolvedLoadResult;
            }

            const std::string normalizedKey = NormalizeLooseAssetKey(prefabAssetKey);
            if (!normalizedKey.empty() && normalizedKey != prefabAssetKey)
            {
                auto normalizedLoadResult = Scene::LoadFromFile(normalizedKey);
                if (!normalizedLoadResult.IsFailure())
                {
                    outEffectivePrefabAssetKey = normalizedKey;
                    return normalizedLoadResult;
                }

                if (const auto normalizedResolvedPath = Assets::ResolveAssetKeyToPath(normalizedKey); normalizedResolvedPath.IsSuccess())
                {
                    auto normalizedResolvedLoadResult = Scene::LoadFromFile(normalizedResolvedPath.GetValue());
                    if (!normalizedResolvedLoadResult.IsFailure())
                    {
                        outEffectivePrefabAssetKey = normalizedKey;
                        return normalizedResolvedLoadResult;
                    }
                }
            }

            return loadedPrefabSceneResult;
        }

        std::string GetRootPrefabAssetKeyIfAny(const Scene& scene, entt::entity rootEntity)
        {
            if (!scene.IsValid(rootEntity))
                return {};

            const auto* prefabInstance = scene.GetRegistry().try_get<PrefabInstanceComponent>(rootEntity);
            if (!prefabInstance)
                return {};
            return prefabInstance->PrefabAssetKey;
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

        std::string effectivePrefabAssetKey;
        const auto loadedPrefabSceneResult = LoadPrefabSceneFromAssetKey(prefabAssetKey, effectivePrefabAssetKey);
        if (loadedPrefabSceneResult.IsFailure())
        {
            LT_WARN("Prefab load failed for '{}': {}", prefabAssetKey, loadedPrefabSceneResult.GetError().GetErrorMessage());
            return entt::null;
        }

        if (effectivePrefabAssetKey != prefabAssetKey)
            LT_WARN("Prefab instantiate normalized asset key '{}' -> '{}'.", prefabAssetKey, effectivePrefabAssetKey);

        auto& loadedPrefabScene = *loadedPrefabSceneResult.GetValue();
        const auto prefabRoots = loadedPrefabScene.GetChildren(entt::null);
        if (prefabRoots.empty())
            return entt::null;

        entt::entity createdRoot = entt::null;
        if (!CopyEntitySubtreeToScene(loadedPrefabScene, destinationScene, prefabRoots.front(), parentEntity, effectivePrefabAssetKey, &createdRoot))
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

    bool ApplyPrefabAssetToInstancesInScene(Scene& scene, const std::string& prefabAssetKey)
    {
        if (prefabAssetKey.empty())
            return false;

        std::string effectivePrefabAssetKey;
        const auto loadedPrefabSceneResult = LoadPrefabSceneFromAssetKey(prefabAssetKey, effectivePrefabAssetKey);
        if (loadedPrefabSceneResult.IsFailure())
        {
            LT_WARN("Prefab load failed for '{}': {}", prefabAssetKey, loadedPrefabSceneResult.GetError().GetErrorMessage());
            return false;
        }
        auto& loadedPrefabScene = *loadedPrefabSceneResult.GetValue();
        const auto prefabRoots = loadedPrefabScene.GetChildren(entt::null);
        if (prefabRoots.empty())
            return false;
        const entt::entity prefabSourceRoot = prefabRoots.front();

        auto& registry = scene.GetRegistry();
        std::vector<entt::entity> instanceRoots;
        auto view = registry.view<PrefabInstanceComponent>();
        for (entt::entity entity : view)
        {
            const auto& prefabInstance = view.get<PrefabInstanceComponent>(entity);
            if (prefabInstance.PrefabAssetKey == prefabAssetKey || prefabInstance.PrefabAssetKey == effectivePrefabAssetKey)
                instanceRoots.push_back(entity);
        }

        if (instanceRoots.empty())
            return true;

        struct StoredRootState
        {
            entt::entity Parent = entt::null;
            int32_t SiblingOrder = 0;
            TransformComponent Transform{};
            std::string Tag;
        };

        std::unordered_map<entt::entity, StoredRootState> stored;
        stored.reserve(instanceRoots.size());

        for (entt::entity root : instanceRoots)
        {
            if (!scene.IsValid(root))
                continue;
            StoredRootState state{};
            state.Parent = scene.GetParent(root);
            if (const auto* hierarchy = registry.try_get<HierarchyComponent>(root))
                state.SiblingOrder = hierarchy->SiblingOrder;
            if (const auto* transform = registry.try_get<TransformComponent>(root))
                state.Transform = *transform;
            if (const auto* tag = registry.try_get<TagComponent>(root))
                state.Tag = tag->Tag;
            stored.emplace(root, std::move(state));
        }

        // Destroy old instances (roots destroy their children).
        for (entt::entity root : instanceRoots)
        {
            if (scene.IsValid(root))
                scene.DestroyEntity(root);
        }

        bool anyApplied = false;
        for (const auto& [oldRoot, state] : stored)
        {
            entt::entity newRoot = entt::null;
            if (!CopyEntitySubtreeToScene(loadedPrefabScene, scene, prefabSourceRoot, state.Parent, prefabAssetKey, &newRoot))
                continue;
            if (IsNullEntity(newRoot) || !scene.IsValid(newRoot))
                continue;

            auto& newRegistry = scene.GetRegistry();
            if (auto* newTransform = newRegistry.try_get<TransformComponent>(newRoot))
                *newTransform = state.Transform;

            if (!state.Tag.empty())
            {
                if (auto* newTag = newRegistry.try_get<TagComponent>(newRoot))
                    newTag->Tag = state.Tag;
            }

            if (auto* newHierarchy = newRegistry.try_get<HierarchyComponent>(newRoot))
                newHierarchy->SiblingOrder = state.SiblingOrder;

            anyApplied = true;
        }

        return anyApplied;
    }

    std::unique_ptr<Scene> CreateDetachedEntitySubtree(const Scene& sourceScene, entt::entity sourceRootEntity)
    {
        if (!sourceScene.IsValid(sourceRootEntity))
            return nullptr;

        auto detachedScene = std::make_unique<Scene>();
        const std::string sourcePrefabAssetKey = GetRootPrefabAssetKeyIfAny(sourceScene, sourceRootEntity);
        entt::entity detachedRoot = entt::null;
        if (!CopyEntitySubtreeToScene(sourceScene,
                                      *detachedScene,
                                      sourceRootEntity,
                                      entt::null,
                                      sourcePrefabAssetKey,
                                      &detachedRoot))
        {
            return nullptr;
        }

        if (detachedRoot == entt::null || !detachedScene->IsValid(detachedRoot))
            return nullptr;

        return detachedScene;
    }

    entt::entity InstantiateDetachedEntitySubtree(Scene& destinationScene, const Scene& sourceSubtreeScene, entt::entity parentEntity)
    {
        const auto roots = sourceSubtreeScene.GetChildren(entt::null);
        if (roots.empty())
            return entt::null;

        const entt::entity sourceRoot = roots.front();
        const std::string prefabAssetKey = GetRootPrefabAssetKeyIfAny(sourceSubtreeScene, sourceRoot);
        entt::entity createdRoot = entt::null;
        if (!CopyEntitySubtreeToScene(sourceSubtreeScene,
                                      destinationScene,
                                      sourceRoot,
                                      parentEntity,
                                      prefabAssetKey,
                                      &createdRoot))
        {
            return entt::null;
        }

        return createdRoot;
    }
}
