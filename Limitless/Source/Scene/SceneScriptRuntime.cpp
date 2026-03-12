#include "Scene/Scene.h"
#include "Scene/SceneScriptRuntimeInternal.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Scene/Components/TilemapComponents.h"

#include "Core/Application.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Window.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/Coroutine.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

namespace Limitless
{
    namespace SceneScriptRuntimeInternal
    {
        std::atomic<uint64_t> s_ParallelScriptAccessMaskMismatchCount{ 0 };

        constexpr float kScriptTransformDirtyEpsilon = 0.0001f;

        size_t ResolveParallelScriptMinSlots(const Concurrency::JobSystem& jobSystem)
        {
            auto& config = ConfigManager::GetInstance();
            const size_t configuredMinSlots = config.GetValue<size_t>("ecs.mt.parallel_script_min_slots", 0);
            if (configuredMinSlots > 0)
                return std::max<size_t>(1, configuredMinSlots);

            const size_t configuredSlotsPerWorker =
                std::max<size_t>(1, config.GetValue<size_t>("ecs.mt.parallel_script_min_slots_per_worker", 2));
            const size_t workerCount = std::max<size_t>(1, jobSystem.GetWorkerCount());
            return std::max<size_t>(2, workerCount * configuredSlotsPerWorker);
        }

        size_t ResolveParallelScriptMinBatchSize()
        {
            const size_t configuredMinBatchSize =
                ConfigManager::GetInstance().GetValue<size_t>("ecs.mt.parallel_script_min_batch_size", 2);
            return std::max<size_t>(2, configuredMinBatchSize);
        }

        std::vector<entt::entity> CollectOrderedScriptComponentEntities(const entt::registry& registry)
        {
            auto view = registry.view<ScriptComponent>();
            std::vector<entt::entity> scriptEntities;
            for (entt::entity scriptEntity : view)
                scriptEntities.push_back(scriptEntity);

            std::sort(scriptEntities.begin(), scriptEntities.end(), [&registry](entt::entity left, entt::entity right) {
                const auto* leftScript = registry.try_get<ScriptComponent>(left);
                const auto* rightScript = registry.try_get<ScriptComponent>(right);
                if (leftScript == nullptr || rightScript == nullptr)
                    return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
                if (leftScript->OwnerEntity != rightScript->OwnerEntity)
                    return static_cast<uint32_t>(leftScript->OwnerEntity) < static_cast<uint32_t>(rightScript->OwnerEntity);
                if (leftScript->ComponentOrder != rightScript->ComponentOrder)
                    return leftScript->ComponentOrder < rightScript->ComponentOrder;
                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });
            return scriptEntities;
        }

        NativeScriptEntry* TryGetScriptEntry(entt::registry& registry, entt::entity scriptEntity)
        {
            auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                return nullptr;
            return scriptComponent->TryGetNativeEntry();
        }

        const NativeScriptEntry* TryGetScriptEntry(const entt::registry& registry, entt::entity scriptEntity)
        {
            const auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                return nullptr;
            return scriptComponent->TryGetNativeEntry();
        }

        ManagedScriptEntry* TryGetManagedScriptEntry(entt::registry& registry, entt::entity scriptEntity)
        {
            auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                return nullptr;
            return scriptComponent->TryGetManagedEntry();
        }

        const ManagedScriptEntry* TryGetManagedScriptEntry(const entt::registry& registry, entt::entity scriptEntity)
        {
            const auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                return nullptr;
            return scriptComponent->TryGetManagedEntry();
        }

        entt::entity TryGetScriptOwnerEntity(const entt::registry& registry, entt::entity scriptEntity)
        {
            const auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                return entt::null;
            return scriptComponent->OwnerEntity;
        }

        bool HasTransformChangedForAccessValidation(const TransformComponent& before, const TransformComponent& after)
        {
            const bool positionChanged = glm::length(after.Position - before.Position) > kScriptTransformDirtyEpsilon;
            const bool rotationChanged = glm::length(after.Rotation - before.Rotation) > kScriptTransformDirtyEpsilon;
            const bool scaleChanged = glm::length(after.Scale - before.Scale) > kScriptTransformDirtyEpsilon;
            return positionChanged || rotationChanged || scaleChanged;
        }

        bool HasHierarchyChangedForAccessValidation(const HierarchyComponent& before, const HierarchyComponent& after)
        {
            return before.Parent != after.Parent || before.SiblingOrder != after.SiblingOrder;
        }

        bool HasRigidbodyChangedForAccessValidation(const Rigidbody2DComponent& before, const Rigidbody2DComponent& after)
        {
            return before.Type != after.Type ||
                   before.FreezePositionX != after.FreezePositionX ||
                   before.FreezePositionY != after.FreezePositionY ||
                   before.FixedRotation != after.FixedRotation ||
                   before.UseCCD != after.UseCCD ||
                   before.EnableSleep != after.EnableSleep ||
                   before.StartAwake != after.StartAwake ||
                   before.Interpolate != after.Interpolate ||
                   before.HighContactQuality != after.HighContactQuality ||
                   before.ExtraSolverSubSteps != after.ExtraSolverSubSteps ||
                   before.GravityScale != after.GravityScale ||
                   before.LinearDamping != after.LinearDamping ||
                   before.AngularDamping != after.AngularDamping ||
                   before.RuntimePendingLinearVelocity != after.RuntimePendingLinearVelocity ||
                   before.RuntimeHasPendingLinearVelocity != after.RuntimeHasPendingLinearVelocity ||
                   before.RuntimePendingLinearVelocityX != after.RuntimePendingLinearVelocityX ||
                   before.RuntimeHasPendingLinearVelocityX != after.RuntimeHasPendingLinearVelocityX ||
                   before.RuntimePendingLinearVelocityY != after.RuntimePendingLinearVelocityY ||
                   before.RuntimeHasPendingLinearVelocityY != after.RuntimeHasPendingLinearVelocityY ||
                   before.PhysicsWorldSlot != after.PhysicsWorldSlot;
        }

        bool HasBoxColliderChangedForAccessValidation(const BoxCollider2DComponent& before, const BoxCollider2DComponent& after)
        {
            return glm::length(after.Offset - before.Offset) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.Size - before.Size) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Density - before.Density) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Friction - before.Friction) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Restitution - before.Restitution) > kScriptTransformDirtyEpsilon ||
                   before.IsSensor != after.IsSensor ||
                   before.CollisionLayer != after.CollisionLayer ||
                   before.CollisionMask != after.CollisionMask;
        }

        bool HasCircleColliderChangedForAccessValidation(const CircleCollider2DComponent& before, const CircleCollider2DComponent& after)
        {
            return glm::length(after.Offset - before.Offset) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Radius - before.Radius) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Density - before.Density) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Friction - before.Friction) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Restitution - before.Restitution) > kScriptTransformDirtyEpsilon ||
                   before.IsSensor != after.IsSensor ||
                   before.CollisionLayer != after.CollisionLayer ||
                   before.CollisionMask != after.CollisionMask;
        }

        bool HasPointListChangedForAccessValidation(const std::vector<glm::vec2>& before, const std::vector<glm::vec2>& after)
        {
            if (before.size() != after.size())
                return true;
            for (size_t pointIndex = 0; pointIndex < before.size(); ++pointIndex)
            {
                if (glm::length(after[pointIndex] - before[pointIndex]) > kScriptTransformDirtyEpsilon)
                    return true;
            }
            return false;
        }

        bool HasPolygonColliderChangedForAccessValidation(const PolygonCollider2DComponent& before, const PolygonCollider2DComponent& after)
        {
            return glm::length(after.Offset - before.Offset) > kScriptTransformDirtyEpsilon ||
                   HasPointListChangedForAccessValidation(before.Points, after.Points) ||
                   std::abs(after.Density - before.Density) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Friction - before.Friction) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Restitution - before.Restitution) > kScriptTransformDirtyEpsilon ||
                   before.IsSensor != after.IsSensor ||
                   before.CollisionLayer != after.CollisionLayer ||
                   before.CollisionMask != after.CollisionMask;
        }

        bool HasEdgeColliderChangedForAccessValidation(const EdgeCollider2DComponent& before, const EdgeCollider2DComponent& after)
        {
            return glm::length(after.Offset - before.Offset) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.PointA - before.PointA) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.PointB - before.PointB) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Friction - before.Friction) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Restitution - before.Restitution) > kScriptTransformDirtyEpsilon ||
                   before.IsSensor != after.IsSensor ||
                   before.CollisionLayer != after.CollisionLayer ||
                   before.CollisionMask != after.CollisionMask;
        }

        bool HasCapsuleColliderChangedForAccessValidation(const CapsuleCollider2DComponent& before, const CapsuleCollider2DComponent& after)
        {
            return glm::length(after.Offset - before.Offset) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.Size - before.Size) > kScriptTransformDirtyEpsilon ||
                   before.Direction != after.Direction ||
                   std::abs(after.Density - before.Density) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Friction - before.Friction) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Restitution - before.Restitution) > kScriptTransformDirtyEpsilon ||
                   before.IsSensor != after.IsSensor ||
                   before.CollisionLayer != after.CollisionLayer ||
                   before.CollisionMask != after.CollisionMask;
        }

        bool HasJoint2DChangedForAccessValidation(const Joint2DComponent& before, const Joint2DComponent& after)
        {
            return before.Type != after.Type ||
                   before.ConnectedEntity != after.ConnectedEntity ||
                   before.CollideConnected != after.CollideConnected ||
                   glm::length(after.AnchorA - before.AnchorA) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.AnchorB - before.AnchorB) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.Axis - before.Axis) > kScriptTransformDirtyEpsilon ||
                   before.EnableLimit != after.EnableLimit ||
                   glm::length(after.Limits - before.Limits) > kScriptTransformDirtyEpsilon ||
                   before.EnableMotor != after.EnableMotor ||
                   std::abs(after.MotorSpeed - before.MotorSpeed) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.MaxMotorForceOrTorque - before.MaxMotorForceOrTorque) > kScriptTransformDirtyEpsilon ||
                   before.EnableSpring != after.EnableSpring ||
                   std::abs(after.Hertz - before.Hertz) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.DampingRatio - before.DampingRatio) > kScriptTransformDirtyEpsilon;
        }

        bool HasTagChangedForAccessValidation(const TagComponent& before, const TagComponent& after)
        {
            return before.Tag != after.Tag || before.Enabled != after.Enabled;
        }

        bool HasSpriteChangedForAccessValidation(const SpriteComponent& before, const SpriteComponent& after)
        {
            return before.TextureKey != after.TextureKey ||
                   glm::length(after.Color - before.Color) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.TilingFactor - before.TilingFactor) > kScriptTransformDirtyEpsilon ||
                   before.RenderOrder != after.RenderOrder ||
                   before.CastShadows != after.CastShadows ||
                   before.ReceiveShadows != after.ReceiveShadows ||
                   before.SubSpriteIndex != after.SubSpriteIndex ||
                   glm::length(after.UvMin - before.UvMin) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.UvMax - before.UvMax) > kScriptTransformDirtyEpsilon;
        }

        bool HasMaterialChangedForAccessValidation(const MaterialComponent& before, const MaterialComponent& after)
        {
            return before.MaterialKey != after.MaterialKey;
        }

        bool HasCanvasChangedForAccessValidation(const CanvasComponent& before, const CanvasComponent& after)
        {
            return before.Mode != after.Mode ||
                   before.SortOrder != after.SortOrder ||
                   glm::length(after.ReferenceResolution - before.ReferenceResolution) > kScriptTransformDirtyEpsilon;
        }

        bool HasRectTransformChangedForAccessValidation(const RectTransformComponent& before, const RectTransformComponent& after)
        {
            return glm::length(after.AnchorMin - before.AnchorMin) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.AnchorMax - before.AnchorMax) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.Pivot - before.Pivot) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.SizeDelta - before.SizeDelta) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.AnchoredPosition - before.AnchoredPosition) > kScriptTransformDirtyEpsilon;
        }

        bool HasUIImageChangedForAccessValidation(const UIImageComponent& before, const UIImageComponent& after)
        {
            return before.RaycastTarget != after.RaycastTarget;
        }

        bool HasUIPanelChangedForAccessValidation(const UIPanelComponent& before, const UIPanelComponent& after)
        {
            return glm::length(after.BackgroundColor - before.BackgroundColor) > kScriptTransformDirtyEpsilon ||
                   before.UseSpriteTexture != after.UseSpriteTexture ||
                   before.RaycastTarget != after.RaycastTarget;
        }

        bool HasUITextChangedForAccessValidation(const UITextComponent& before, const UITextComponent& after)
        {
            return before.Text != after.Text ||
                   before.FontFilePath != after.FontFilePath ||
                   std::abs(after.FontSize - before.FontSize) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.Color - before.Color) > kScriptTransformDirtyEpsilon ||
                   before.RaycastTarget != after.RaycastTarget;
        }

        bool HasUIButtonChangedForAccessValidation(const UIButtonComponent& before, const UIButtonComponent& after)
        {
            return before.Interactable != after.Interactable ||
                   before.UseStateColors != after.UseStateColors ||
                   glm::length(after.NormalColor - before.NormalColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.HoveredColor - before.HoveredColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.PressedColor - before.PressedColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.DisabledColor - before.DisabledColor) > kScriptTransformDirtyEpsilon ||
                   before.OnClickEvent != after.OnClickEvent ||
                   before.OnHoverEnterEvent != after.OnHoverEnterEvent ||
                   before.OnHoverExitEvent != after.OnHoverExitEvent ||
                   before.OnPressedEvent != after.OnPressedEvent;
        }

        bool HasUISliderChangedForAccessValidation(const UISliderComponent& before, const UISliderComponent& after)
        {
            return before.Interactable != after.Interactable ||
                   std::abs(after.MinValue - before.MinValue) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.MaxValue - before.MaxValue) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Value - before.Value) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.BackgroundColor - before.BackgroundColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.FillColor - before.FillColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.HandleColor - before.HandleColor) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.HandleWidth - before.HandleWidth) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.HandleHeightMultiplier - before.HandleHeightMultiplier) > kScriptTransformDirtyEpsilon ||
                   before.ShowHandle != after.ShowHandle ||
                   before.OnValueChangedEvent != after.OnValueChangedEvent;
        }

        bool HasDirectionalLight2DChangedForAccessValidation(const DirectionalLight2DComponent& before, const DirectionalLight2DComponent& after)
        {
            return before.Enabled != after.Enabled ||
                   glm::length(after.Color - before.Color) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Intensity - before.Intensity) > kScriptTransformDirtyEpsilon ||
                   before.UseEntityRotation != after.UseEntityRotation ||
                   glm::length(after.Direction - before.Direction) > kScriptTransformDirtyEpsilon ||
                   before.CastShadows != after.CastShadows ||
                   std::abs(after.ShadowStrength - before.ShadowStrength) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.ShadowSoftness - before.ShadowSoftness) > kScriptTransformDirtyEpsilon ||
                   before.ShadowSamples != after.ShadowSamples ||
                   std::abs(after.ShadowDistance - before.ShadowDistance) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.ShadowBias - before.ShadowBias) > kScriptTransformDirtyEpsilon;
        }

        bool HasPointLight2DChangedForAccessValidation(const PointLight2DComponent& before, const PointLight2DComponent& after)
        {
            return before.Enabled != after.Enabled ||
                   glm::length(after.Color - before.Color) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Intensity - before.Intensity) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Radius - before.Radius) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Falloff - before.Falloff) > kScriptTransformDirtyEpsilon ||
                   before.CastShadows != after.CastShadows ||
                   std::abs(after.ShadowStrength - before.ShadowStrength) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.ShadowSoftness - before.ShadowSoftness) > kScriptTransformDirtyEpsilon ||
                   before.ShadowSamples != after.ShadowSamples ||
                   std::abs(after.ShadowBias - before.ShadowBias) > kScriptTransformDirtyEpsilon;
        }

        bool HasShadowOccluder2DChangedForAccessValidation(const ShadowOccluder2DComponent& before, const ShadowOccluder2DComponent& after)
        {
            if (before.Enabled != after.Enabled ||
                before.Source != after.Source ||
                before.Closed != after.Closed ||
                std::abs(after.Extrusion - before.Extrusion) > kScriptTransformDirtyEpsilon)
            {
                return true;
            }

            if (before.PolygonPoints.size() != after.PolygonPoints.size())
                return true;
            for (size_t pointIndex = 0; pointIndex < before.PolygonPoints.size(); ++pointIndex)
            {
                if (glm::length(after.PolygonPoints[pointIndex] - before.PolygonPoints[pointIndex]) > kScriptTransformDirtyEpsilon)
                    return true;
            }
            return false;
        }

        bool HasAudioListener2DChangedForAccessValidation(const AudioListener2DComponent& before, const AudioListener2DComponent& after)
        {
            return before.Enabled != after.Enabled ||
                   before.UsePrimaryCameraPosition != after.UsePrimaryCameraPosition;
        }

        bool HasAudioListener3DChangedForAccessValidation(const AudioListener3DComponent& before, const AudioListener3DComponent& after)
        {
            return before.Enabled != after.Enabled ||
                   before.UsePrimaryCameraTransform != after.UsePrimaryCameraTransform;
        }

        bool HasAudioSourceChangedForAccessValidation(const AudioSourceComponent& before, const AudioSourceComponent& after)
        {
            return before.AudioClipKey != after.AudioClipKey ||
                   std::abs(after.Volume - before.Volume) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.Pitch - before.Pitch) > kScriptTransformDirtyEpsilon ||
                   before.PlayOnStart != after.PlayOnStart ||
                   before.Loop != after.Loop ||
                   before.Muted != after.Muted ||
                   before.Space != after.Space ||
                   before.MixerGroup != after.MixerGroup ||
                   std::abs(after.SpatialMinDistance - before.SpatialMinDistance) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.SpatialMaxDistance - before.SpatialMaxDistance) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.SpatialRolloffExponent - before.SpatialRolloffExponent) > kScriptTransformDirtyEpsilon ||
                   before.SpatialRolloffMode != after.SpatialRolloffMode ||
                   std::abs(after.StereoPanStrength - before.StereoPanStrength) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.DopplerFactor - before.DopplerFactor) > kScriptTransformDirtyEpsilon ||
                   before.EnableDirectionalAttenuation != after.EnableDirectionalAttenuation ||
                   std::abs(after.DirectionalInnerAngleDegrees - before.DirectionalInnerAngleDegrees) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.DirectionalOuterAngleDegrees - before.DirectionalOuterAngleDegrees) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.DirectionalOuterVolume - before.DirectionalOuterVolume) > kScriptTransformDirtyEpsilon ||
                   before.AttenuationCurveKey != after.AttenuationCurveKey;
        }

        bool HasCameraChangedForAccessValidation(const CameraComponent& before, const CameraComponent& after)
        {
            return before.Projection != after.Projection ||
                   before.IsPrimary != after.IsPrimary ||
                   std::abs(after.Zoom - before.Zoom) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.NearPlane - before.NearPlane) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.FarPlane - before.FarPlane) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.FieldOfViewYDegrees - before.FieldOfViewYDegrees) > kScriptTransformDirtyEpsilon;
        }

        bool HasPrefabInstanceChangedForAccessValidation(const PrefabInstanceComponent& before, const PrefabInstanceComponent& after)
        {
            return before.PrefabAssetKey != after.PrefabAssetKey;
        }

        bool HasGrid2DChangedForAccessValidation(const Grid2DComponent& before, const Grid2DComponent& after)
        {
            return glm::length(after.CellSize - before.CellSize) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.CellGap - before.CellGap) > kScriptTransformDirtyEpsilon ||
                   before.GridSize != after.GridSize ||
                   glm::length(after.OriginCell - before.OriginCell) > kScriptTransformDirtyEpsilon;
        }

        bool HasTilemapLayerChangedForAccessValidation(const TilemapLayerComponent& before, const TilemapLayerComponent& after)
        {
            if (before.RenderOrder != after.RenderOrder ||
                before.CollisionEnabled != after.CollisionEnabled ||
                before.CastShadows != after.CastShadows ||
                before.TileTable != after.TileTable ||
                before.Tiles.size() != after.Tiles.size())
            {
                return true;
            }

            return before.Tiles != after.Tiles;
        }

        bool HasAnimatorChangedForAccessValidation(const AnimatorComponent& before, const AnimatorComponent& after)
        {
            return before.ControllerKey != after.ControllerKey ||
                   before.DefaultClipKey != after.DefaultClipKey ||
                   std::abs(after.PlaybackSpeed - before.PlaybackSpeed) > kScriptTransformDirtyEpsilon ||
                   before.Enabled != after.Enabled ||
                   before.ApplyToSprite != after.ApplyToSprite ||
                   before.ApplyToTransform != after.ApplyToTransform ||
                   before.AutoPlay != after.AutoPlay ||
                   before.BoolParameters != after.BoolParameters ||
                   before.FloatParameters != after.FloatParameters ||
                   before.IntegerParameters != after.IntegerParameters ||
                   before.TriggerParameters != after.TriggerParameters;
        }

        bool HasAnimationEventReceiverChangedForAccessValidation(const AnimationEventReceiverComponent& before, const AnimationEventReceiverComponent& after)
        {
            return before.Enabled != after.Enabled;
        }

        bool HasParticleEmitterChangedForAccessValidation(const ParticleEmitterComponent& before, const ParticleEmitterComponent& after)
        {
            return std::abs(after.SpawnRate - before.SpawnRate) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.LifetimeMin - before.LifetimeMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.LifetimeMax - before.LifetimeMax) > kScriptTransformDirtyEpsilon ||
                   before.Looping != after.Looping ||
                   std::abs(after.Duration - before.Duration) > kScriptTransformDirtyEpsilon ||
                   before.PlayOnStart != after.PlayOnStart ||
                   before.BurstEnabled != after.BurstEnabled ||
                   before.BurstCount != after.BurstCount ||
                   glm::length(after.SpawnOffsetMin - before.SpawnOffsetMin) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.SpawnOffsetMax - before.SpawnOffsetMax) > kScriptTransformDirtyEpsilon ||
                   before.UseRadialSpawn != after.UseRadialSpawn ||
                   std::abs(after.SpawnRadiusMin - before.SpawnRadiusMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.SpawnRadiusMax - before.SpawnRadiusMax) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.SpeedMin - before.SpeedMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.SpeedMax - before.SpeedMax) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.AngleMin - before.AngleMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.AngleMax - before.AngleMax) > kScriptTransformDirtyEpsilon ||
                   before.RadialVelocity != after.RadialVelocity ||
                   std::abs(after.GravityModifier - before.GravityModifier) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.StartSizeMin - before.StartSizeMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.StartSizeMax - before.StartSizeMax) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.EndSize - before.EndSize) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.StartColor - before.StartColor) > kScriptTransformDirtyEpsilon ||
                   glm::length(after.EndColor - before.EndColor) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.StartRotationMin - before.StartRotationMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.StartRotationMax - before.StartRotationMax) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.RotationSpeedMin - before.RotationSpeedMin) > kScriptTransformDirtyEpsilon ||
                   std::abs(after.RotationSpeedMax - before.RotationSpeedMax) > kScriptTransformDirtyEpsilon ||
                   before.TextureKey != after.TextureKey ||
                   before.MaxParticles != after.MaxParticles;
        }

        std::string DescribeAccessMask(uint64_t mask)
        {
            std::string description;
            auto append = [&description](const char* name) {
                if (!description.empty())
                    description += ", ";
                description += name;
            };

            if ((mask & ToAccessMask(SceneSystemAccessComponent::Transform)) != 0)
                append("Transform");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Hierarchy)) != 0)
                append("Hierarchy");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Rigidbody2D)) != 0)
                append("Rigidbody2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::BoxCollider2D)) != 0)
                append("BoxCollider2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::CircleCollider2D)) != 0)
                append("CircleCollider2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::PolygonCollider2D)) != 0)
                append("PolygonCollider2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::EdgeCollider2D)) != 0)
                append("EdgeCollider2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::CapsuleCollider2D)) != 0)
                append("CapsuleCollider2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Joint2D)) != 0)
                append("Joint2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Animator)) != 0)
                append("Animator");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::ParticleEmitter)) != 0)
                append("ParticleEmitter");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::NativeScript)) != 0)
                append("NativeScript");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Rendering2D)) != 0)
                append("Rendering2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Lighting2D)) != 0)
                append("Lighting2D");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::UI)) != 0)
                append("UI");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Audio)) != 0)
                append("Audio");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Camera)) != 0)
                append("Camera");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Tilemap)) != 0)
                append("Tilemap");
            if ((mask & ToAccessMask(SceneSystemAccessComponent::Metadata)) != 0)
                append("Metadata");

            if (description.empty())
                description = "None";
            return description;
        }
    }

    using namespace SceneScriptRuntimeInternal;

    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath);
    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);
    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame);

}

