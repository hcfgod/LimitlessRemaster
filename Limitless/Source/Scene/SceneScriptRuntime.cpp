#include "Scene/Scene.h"

#include "Core/Application.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Window.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/Coroutine.h"
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
    namespace
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
                   std::abs(after.StereoPanStrength - before.StereoPanStrength) > kScriptTransformDirtyEpsilon ||
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
                   glm::length(after.CellGap - before.CellGap) > kScriptTransformDirtyEpsilon;
        }

        bool HasTilemapLayerChangedForAccessValidation(const TilemapLayerComponent& before, const TilemapLayerComponent& after)
        {
            if (before.GridSize.x != after.GridSize.x ||
                before.GridSize.y != after.GridSize.y ||
                before.RenderOrder != after.RenderOrder ||
                before.CollisionEnabled != after.CollisionEnabled ||
                before.CastShadows != after.CastShadows ||
                before.TileTable != after.TileTable ||
                before.Tiles.size() != after.Tiles.size())
            {
                return true;
            }

            if (!before.Tiles.empty())
            {
                const size_t firstIndex = 0;
                const size_t middleIndex = before.Tiles.size() / 2;
                const size_t lastIndex = before.Tiles.size() - 1;
                if (before.Tiles[firstIndex] != after.Tiles[firstIndex] ||
                    before.Tiles[middleIndex] != after.Tiles[middleIndex] ||
                    before.Tiles[lastIndex] != after.Tiles[lastIndex])
                {
                    return true;
                }
            }

            return false;
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

    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath);
    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);
    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame);

    void Scene::Update(float deltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            ProcessUiInteractionSystemForSceneRuntime(*this, window.GetWidth(), window.GetHeight());
        }
        else
        {
            ProcessUiInteractionSystemForSceneRuntime(*this, 0, 0);
        }

        auto runScheduledSimulationSystems = [&]() {
            SetRuntimePhase(RuntimePhase::Simulation);
            const bool enableSystemScheduler = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_system_scheduler", true);
            if (!enableSystemScheduler)
            {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
                return;
            }

            std::vector<ScheduledSceneSystem> scheduledSystems;
            ScheduledSceneSystem animationSystem{};
            animationSystem.Name = "Animation2D";
            animationSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Animator);
            animationSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::Animator) |
                                           ToAccessMask(SceneSystemAccessComponent::Transform);
            animationSystem.Execute = [this, deltaTime]() {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
            };
            animationSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(animationSystem));

            ScheduledSceneSystem particleSystem{};
            particleSystem.Name = "ParticleEmitter";
            particleSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Transform);
            particleSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
            particleSystem.Execute = [this, deltaTime]() {
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
            };
            particleSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(particleSystem));
            SceneSystemScheduler::Run(Concurrency::GetJobSystem(), scheduledSystems);
        };

        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::ScriptMainThread);
            auto scriptView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : scriptView)
            {
                auto& nativeScript = scriptView.get<NativeScriptComponent>(entity);
                for (auto& scriptEntry : nativeScript.Scripts)
                {
                    if (scriptEntry.RuntimeInstance)
                    {
                        if (scriptEntry.RuntimeInitialized)
                        {
                            try
                            {
                                scriptEntry.RuntimeInstance->OnDestroy();
                            }
                            catch (const std::exception& exception)
                            {
                                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                                LT_ERROR("Script '{}' on entity '{}' threw during OnDestroy while script execution is blocked: {}",
                                         scriptEntry.ScriptClassName,
                                         tag ? tag->Tag : "Entity",
                                         exception.what());
                            }
                            catch (...)
                            {
                                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                                LT_ERROR("Script '{}' on entity '{}' threw a non-standard exception during OnDestroy while script execution is blocked",
                                         scriptEntry.ScriptClassName,
                                         tag ? tag->Tag : "Entity");
                            }
                        }

                        try
                        {
                            Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                        }
                        catch (const std::exception& exception)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(entity);
                            LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup while script execution is blocked: {}",
                                    scriptEntry.ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    exception.what());
                        }
                        catch (...)
                        {
                            const auto* tag = m_Registry.try_get<TagComponent>(entity);
                            LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup while script execution is blocked",
                                    scriptEntry.ScriptClassName,
                                    tag ? tag->Tag : "Entity");
                        }
                        scriptEntry.RuntimeInstance.reset();
                    }

                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry.RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            runScheduledSimulationSystems();
            SetRuntimePhase(RuntimePhase::Transform);
            UpdateTransforms();
            SetRuntimePhase(RuntimePhase::Idle);
            return;
        }

        std::vector<std::pair<entt::entity, size_t>> scriptSlots;
        {
            auto snapshotView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : snapshotView)
            {
                const auto& nativeScript = snapshotView.get<NativeScriptComponent>(entity);
                for (size_t scriptIndex = 0; scriptIndex < nativeScript.Scripts.size(); ++scriptIndex)
                    scriptSlots.emplace_back(entity, scriptIndex);
            }
        }

        auto tryGetScriptEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> NativeScriptEntry* {
            auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(scriptEntity);
            if (!nativeScript || scriptIndex >= nativeScript->Scripts.size())
                return nullptr;
            return &nativeScript->Scripts[scriptIndex];
        };

        auto applyScriptDeclaredAccessDefaults = [&](NativeScriptEntry& scriptEntry) {
            if (!scriptEntry.RuntimeInstance)
                return;
            if (scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0)
                return;

            const uint64_t defaultReadMask = scriptEntry.RuntimeInstance->GetDeclaredReadAccessMask();
            const uint64_t defaultWriteMask = scriptEntry.RuntimeInstance->GetDeclaredWriteAccessMask();
            if (defaultReadMask != 0 || defaultWriteMask != 0)
            {
                scriptEntry.DeclaredReadAccessMask = defaultReadMask;
                scriptEntry.DeclaredWriteAccessMask = defaultWriteMask;
            }
        };

        auto handleScriptCallbackFailure = [&](entt::entity scriptEntity,
                                               size_t scriptIndex,
                                               std::string_view callbackName,
                                               const char* message) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            if (scriptEntry->RuntimeInstance)
            {
                try
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                }
                catch (const std::exception& exception)
                {
                    LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup after {} failure: {}",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName,
                            exception.what());
                }
                catch (...)
                {
                    LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup after {} failure",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName);
                }
            }
            scriptEntry->RuntimeInstance.reset();
            scriptEntry->RuntimeInitialized = false;
            scriptEntry->RuntimeUpdateCount = 0;
            scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
            scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
        };

        const bool validateParallelScriptAccessMasks =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool warnOnParallelScriptAccessMaskMismatch =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);
        auto validateParallelScriptAccessMask = [&](entt::entity entity,
                                                    NativeScriptEntry& scriptEntry,
                                                    uint64_t observedWriteMask,
                                                    std::string_view callbackName) {
            if (!validateParallelScriptAccessMasks || observedWriteMask == 0)
                return;
            if (!Scene::IsCurrentThreadParallelScriptExecution())
                return;
            if (scriptEntry.ExecutionPolicy != ScriptExecutionPolicy::ParallelSafe)
                return;

            const uint64_t missingWriteMask = observedWriteMask & ~scriptEntry.DeclaredWriteAccessMask;
            if (missingWriteMask == 0)
                return;

            const uint64_t mismatchCount = s_ParallelScriptAccessMaskMismatchCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (warnOnParallelScriptAccessMaskMismatch && !scriptEntry.RuntimeWarnedAccessMaskMismatch)
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                LT_WARN("ParallelSafe script '{}' on entity '{}' mutated [{}] during {} without declaring write access (declared={} observed={} missing={}). Add required bits to DeclaredWriteAccessMask. total_mismatches={}",
                        scriptEntry.ScriptClassName,
                        tag ? tag->Tag : "Entity",
                        DescribeAccessMask(observedWriteMask),
                        callbackName,
                        DescribeAccessMask(scriptEntry.DeclaredWriteAccessMask),
                        DescribeAccessMask(observedWriteMask),
                        DescribeAccessMask(missingWriteMask),
                        mismatchCount);
                scriptEntry.RuntimeWarnedAccessMaskMismatch = true;
            }
        };

        auto executeScriptUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeUpdate{};
            bool hadTransformBeforeUpdate = false;
            HierarchyComponent hierarchyBeforeUpdate{};
            bool hadHierarchyBeforeUpdate = false;
            Rigidbody2DComponent rigidbodyBeforeUpdate{};
            bool hadRigidbodyBeforeUpdate = false;
            BoxCollider2DComponent boxColliderBeforeUpdate{};
            bool hadBoxColliderBeforeUpdate = false;
            CircleCollider2DComponent circleColliderBeforeUpdate{};
            bool hadCircleColliderBeforeUpdate = false;
            Joint2DComponent joint2DBeforeUpdate{};
            bool hadJoint2DBeforeUpdate = false;
            TagComponent tagBeforeUpdate{};
            bool hadTagBeforeUpdate = false;
            SpriteComponent spriteBeforeUpdate{};
            bool hadSpriteBeforeUpdate = false;
            MaterialComponent materialBeforeUpdate{};
            bool hadMaterialBeforeUpdate = false;
            CanvasComponent canvasBeforeUpdate{};
            bool hadCanvasBeforeUpdate = false;
            RectTransformComponent rectTransformBeforeUpdate{};
            bool hadRectTransformBeforeUpdate = false;
            UIImageComponent uiImageBeforeUpdate{};
            bool hadUIImageBeforeUpdate = false;
            UIPanelComponent uiPanelBeforeUpdate{};
            bool hadUIPanelBeforeUpdate = false;
            UITextComponent uiTextBeforeUpdate{};
            bool hadUITextBeforeUpdate = false;
            UIButtonComponent uiButtonBeforeUpdate{};
            bool hadUIButtonBeforeUpdate = false;
            UISliderComponent uiSliderBeforeUpdate{};
            bool hadUISliderBeforeUpdate = false;
            DirectionalLight2DComponent directionalLightBeforeUpdate{};
            bool hadDirectionalLightBeforeUpdate = false;
            PointLight2DComponent pointLightBeforeUpdate{};
            bool hadPointLightBeforeUpdate = false;
            ShadowOccluder2DComponent shadowOccluderBeforeUpdate{};
            bool hadShadowOccluderBeforeUpdate = false;
            AudioListener2DComponent audioListenerBeforeUpdate{};
            bool hadAudioListenerBeforeUpdate = false;
            AudioSourceComponent audioSourceBeforeUpdate{};
            bool hadAudioSourceBeforeUpdate = false;
            CameraComponent cameraBeforeUpdate{};
            bool hadCameraBeforeUpdate = false;
            PrefabInstanceComponent prefabInstanceBeforeUpdate{};
            bool hadPrefabInstanceBeforeUpdate = false;
            Grid2DComponent grid2DBeforeUpdate{};
            bool hadGrid2DBeforeUpdate = false;
            TilemapLayerComponent tilemapLayerBeforeUpdate{};
            bool hadTilemapLayerBeforeUpdate = false;
            AnimatorComponent animatorBeforeUpdate{};
            bool hadAnimatorBeforeUpdate = false;
            AnimationEventReceiverComponent animationEventReceiverBeforeUpdate{};
            bool hadAnimationEventReceiverBeforeUpdate = false;
            ParticleEmitterComponent particleEmitterBeforeUpdate{};
            bool hadParticleEmitterBeforeUpdate = false;
            bool trackTransformMutation = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeUpdate = *transform;
                hadTransformBeforeUpdate = true;
                if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    trackTransformMutation = rigidbody2D->Type == Rigidbody2DComponent::BodyType::Dynamic ||
                                            rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic;
                }
            }
            const bool trackParallelAccessValidation =
                validateParallelScriptAccessMasks &&
                scriptEntry->ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe;
            if (trackParallelAccessValidation)
            {
                if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity))
                {
                    hierarchyBeforeUpdate = *hierarchy;
                    hadHierarchyBeforeUpdate = true;
                }
                if (auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    rigidbodyBeforeUpdate = *rigidbody2D;
                    hadRigidbodyBeforeUpdate = true;
                }
                if (auto* boxCollider2D = m_Registry.try_get<BoxCollider2DComponent>(entity))
                {
                    boxColliderBeforeUpdate = *boxCollider2D;
                    hadBoxColliderBeforeUpdate = true;
                }
                if (auto* circleCollider2D = m_Registry.try_get<CircleCollider2DComponent>(entity))
                {
                    circleColliderBeforeUpdate = *circleCollider2D;
                    hadCircleColliderBeforeUpdate = true;
                }
                if (auto* joint2D = m_Registry.try_get<Joint2DComponent>(entity))
                {
                    joint2DBeforeUpdate = *joint2D;
                    hadJoint2DBeforeUpdate = true;
                }
                if (auto* tag = m_Registry.try_get<TagComponent>(entity))
                {
                    tagBeforeUpdate = *tag;
                    hadTagBeforeUpdate = true;
                }
                if (auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
                {
                    spriteBeforeUpdate = *sprite;
                    hadSpriteBeforeUpdate = true;
                }
                if (auto* material = m_Registry.try_get<MaterialComponent>(entity))
                {
                    materialBeforeUpdate = *material;
                    hadMaterialBeforeUpdate = true;
                }
                if (auto* canvas = m_Registry.try_get<CanvasComponent>(entity))
                {
                    canvasBeforeUpdate = *canvas;
                    hadCanvasBeforeUpdate = true;
                }
                if (auto* rectTransform = m_Registry.try_get<RectTransformComponent>(entity))
                {
                    rectTransformBeforeUpdate = *rectTransform;
                    hadRectTransformBeforeUpdate = true;
                }
                if (auto* uiImage = m_Registry.try_get<UIImageComponent>(entity))
                {
                    uiImageBeforeUpdate = *uiImage;
                    hadUIImageBeforeUpdate = true;
                }
                if (auto* uiPanel = m_Registry.try_get<UIPanelComponent>(entity))
                {
                    uiPanelBeforeUpdate = *uiPanel;
                    hadUIPanelBeforeUpdate = true;
                }
                if (auto* uiText = m_Registry.try_get<UITextComponent>(entity))
                {
                    uiTextBeforeUpdate = *uiText;
                    hadUITextBeforeUpdate = true;
                }
                if (auto* uiButton = m_Registry.try_get<UIButtonComponent>(entity))
                {
                    uiButtonBeforeUpdate = *uiButton;
                    hadUIButtonBeforeUpdate = true;
                }
                if (auto* uiSlider = m_Registry.try_get<UISliderComponent>(entity))
                {
                    uiSliderBeforeUpdate = *uiSlider;
                    hadUISliderBeforeUpdate = true;
                }
                if (auto* directionalLight = m_Registry.try_get<DirectionalLight2DComponent>(entity))
                {
                    directionalLightBeforeUpdate = *directionalLight;
                    hadDirectionalLightBeforeUpdate = true;
                }
                if (auto* pointLight = m_Registry.try_get<PointLight2DComponent>(entity))
                {
                    pointLightBeforeUpdate = *pointLight;
                    hadPointLightBeforeUpdate = true;
                }
                if (auto* shadowOccluder = m_Registry.try_get<ShadowOccluder2DComponent>(entity))
                {
                    shadowOccluderBeforeUpdate = *shadowOccluder;
                    hadShadowOccluderBeforeUpdate = true;
                }
                if (auto* audioListener = m_Registry.try_get<AudioListener2DComponent>(entity))
                {
                    audioListenerBeforeUpdate = *audioListener;
                    hadAudioListenerBeforeUpdate = true;
                }
                if (auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
                {
                    audioSourceBeforeUpdate = *audioSource;
                    hadAudioSourceBeforeUpdate = true;
                }
                if (auto* camera = m_Registry.try_get<CameraComponent>(entity))
                {
                    cameraBeforeUpdate = *camera;
                    hadCameraBeforeUpdate = true;
                }
                if (auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
                {
                    prefabInstanceBeforeUpdate = *prefabInstance;
                    hadPrefabInstanceBeforeUpdate = true;
                }
                if (auto* grid2D = m_Registry.try_get<Grid2DComponent>(entity))
                {
                    grid2DBeforeUpdate = *grid2D;
                    hadGrid2DBeforeUpdate = true;
                }
                if (auto* tilemapLayer = m_Registry.try_get<TilemapLayerComponent>(entity))
                {
                    tilemapLayerBeforeUpdate = *tilemapLayer;
                    hadTilemapLayerBeforeUpdate = true;
                }
                if (auto* animator = m_Registry.try_get<AnimatorComponent>(entity))
                {
                    animatorBeforeUpdate = *animator;
                    hadAnimatorBeforeUpdate = true;
                }
                if (auto* animationEventReceiver = m_Registry.try_get<AnimationEventReceiverComponent>(entity))
                {
                    animationEventReceiverBeforeUpdate = *animationEventReceiver;
                    hadAnimationEventReceiverBeforeUpdate = true;
                }
                if (auto* particleEmitter = m_Registry.try_get<ParticleEmitterComponent>(entity))
                {
                    particleEmitterBeforeUpdate = *particleEmitter;
                    hadParticleEmitterBeforeUpdate = true;
                }
            }

            try
            {
                scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry->RuntimeInstance->OnUpdate(deltaTime);
                Coroutine::TickOwner(*scriptEntry->RuntimeInstance, deltaTime);
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", "non-standard exception");
                return;
            }

            scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;
            ++scriptEntry->RuntimeUpdateCount;

            const auto* transformAfterUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeUpdate, *transformAfterUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeUpdate && transformAfterUpdate)
            {
                transformChanged = true;
                MarkTransformDirty(entity);
            }

            uint64_t observedWriteMask = 0;
            if (transformChanged)
                observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Transform);
            if (trackParallelAccessValidation)
            {
                const auto* hierarchyAfterUpdate = m_Registry.try_get<HierarchyComponent>(entity);
                const bool hasHierarchyAfterUpdate = hierarchyAfterUpdate != nullptr;
                if (hadHierarchyBeforeUpdate != hasHierarchyAfterUpdate ||
                    (hadHierarchyBeforeUpdate && hierarchyAfterUpdate &&
                     HasHierarchyChangedForAccessValidation(hierarchyBeforeUpdate, *hierarchyAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Hierarchy);
                }

                const auto* rigidbodyAfterUpdate = m_Registry.try_get<Rigidbody2DComponent>(entity);
                const bool hasRigidbodyAfterUpdate = rigidbodyAfterUpdate != nullptr;
                if (hadRigidbodyBeforeUpdate != hasRigidbodyAfterUpdate ||
                    (hadRigidbodyBeforeUpdate && rigidbodyAfterUpdate &&
                     HasRigidbodyChangedForAccessValidation(rigidbodyBeforeUpdate, *rigidbodyAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rigidbody2D);
                }

                const auto* boxColliderAfterUpdate = m_Registry.try_get<BoxCollider2DComponent>(entity);
                const bool hasBoxColliderAfterUpdate = boxColliderAfterUpdate != nullptr;
                if (hadBoxColliderBeforeUpdate != hasBoxColliderAfterUpdate ||
                    (hadBoxColliderBeforeUpdate && boxColliderAfterUpdate &&
                     HasBoxColliderChangedForAccessValidation(boxColliderBeforeUpdate, *boxColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::BoxCollider2D);
                }

                const auto* circleColliderAfterUpdate = m_Registry.try_get<CircleCollider2DComponent>(entity);
                const bool hasCircleColliderAfterUpdate = circleColliderAfterUpdate != nullptr;
                if (hadCircleColliderBeforeUpdate != hasCircleColliderAfterUpdate ||
                    (hadCircleColliderBeforeUpdate && circleColliderAfterUpdate &&
                     HasCircleColliderChangedForAccessValidation(circleColliderBeforeUpdate, *circleColliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CircleCollider2D);
                }

                const auto* joint2DAfterUpdate = m_Registry.try_get<Joint2DComponent>(entity);
                const bool hasJoint2DAfterUpdate = joint2DAfterUpdate != nullptr;
                if (hadJoint2DBeforeUpdate != hasJoint2DAfterUpdate ||
                    (hadJoint2DBeforeUpdate && joint2DAfterUpdate &&
                     HasJoint2DChangedForAccessValidation(joint2DBeforeUpdate, *joint2DAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Joint2D);
                }

                const auto* tagAfterUpdate = m_Registry.try_get<TagComponent>(entity);
                const bool hasTagAfterUpdate = tagAfterUpdate != nullptr;
                if (hadTagBeforeUpdate != hasTagAfterUpdate ||
                    (hadTagBeforeUpdate && tagAfterUpdate &&
                     HasTagChangedForAccessValidation(tagBeforeUpdate, *tagAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* spriteAfterUpdate = m_Registry.try_get<SpriteComponent>(entity);
                const bool hasSpriteAfterUpdate = spriteAfterUpdate != nullptr;
                if (hadSpriteBeforeUpdate != hasSpriteAfterUpdate ||
                    (hadSpriteBeforeUpdate && spriteAfterUpdate &&
                     HasSpriteChangedForAccessValidation(spriteBeforeUpdate, *spriteAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* materialAfterUpdate = m_Registry.try_get<MaterialComponent>(entity);
                const bool hasMaterialAfterUpdate = materialAfterUpdate != nullptr;
                if (hadMaterialBeforeUpdate != hasMaterialAfterUpdate ||
                    (hadMaterialBeforeUpdate && materialAfterUpdate &&
                     HasMaterialChangedForAccessValidation(materialBeforeUpdate, *materialAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* canvasAfterUpdate = m_Registry.try_get<CanvasComponent>(entity);
                const bool hasCanvasAfterUpdate = canvasAfterUpdate != nullptr;
                if (hadCanvasBeforeUpdate != hasCanvasAfterUpdate ||
                    (hadCanvasBeforeUpdate && canvasAfterUpdate &&
                     HasCanvasChangedForAccessValidation(canvasBeforeUpdate, *canvasAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* rectTransformAfterUpdate = m_Registry.try_get<RectTransformComponent>(entity);
                const bool hasRectTransformAfterUpdate = rectTransformAfterUpdate != nullptr;
                if (hadRectTransformBeforeUpdate != hasRectTransformAfterUpdate ||
                    (hadRectTransformBeforeUpdate && rectTransformAfterUpdate &&
                     HasRectTransformChangedForAccessValidation(rectTransformBeforeUpdate, *rectTransformAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiImageAfterUpdate = m_Registry.try_get<UIImageComponent>(entity);
                const bool hasUIImageAfterUpdate = uiImageAfterUpdate != nullptr;
                if (hadUIImageBeforeUpdate != hasUIImageAfterUpdate ||
                    (hadUIImageBeforeUpdate && uiImageAfterUpdate &&
                     HasUIImageChangedForAccessValidation(uiImageBeforeUpdate, *uiImageAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiPanelAfterUpdate = m_Registry.try_get<UIPanelComponent>(entity);
                const bool hasUIPanelAfterUpdate = uiPanelAfterUpdate != nullptr;
                if (hadUIPanelBeforeUpdate != hasUIPanelAfterUpdate ||
                    (hadUIPanelBeforeUpdate && uiPanelAfterUpdate &&
                     HasUIPanelChangedForAccessValidation(uiPanelBeforeUpdate, *uiPanelAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiTextAfterUpdate = m_Registry.try_get<UITextComponent>(entity);
                const bool hasUITextAfterUpdate = uiTextAfterUpdate != nullptr;
                if (hadUITextBeforeUpdate != hasUITextAfterUpdate ||
                    (hadUITextBeforeUpdate && uiTextAfterUpdate &&
                     HasUITextChangedForAccessValidation(uiTextBeforeUpdate, *uiTextAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiButtonAfterUpdate = m_Registry.try_get<UIButtonComponent>(entity);
                const bool hasUIButtonAfterUpdate = uiButtonAfterUpdate != nullptr;
                if (hadUIButtonBeforeUpdate != hasUIButtonAfterUpdate ||
                    (hadUIButtonBeforeUpdate && uiButtonAfterUpdate &&
                     HasUIButtonChangedForAccessValidation(uiButtonBeforeUpdate, *uiButtonAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiSliderAfterUpdate = m_Registry.try_get<UISliderComponent>(entity);
                const bool hasUISliderAfterUpdate = uiSliderAfterUpdate != nullptr;
                if (hadUISliderBeforeUpdate != hasUISliderAfterUpdate ||
                    (hadUISliderBeforeUpdate && uiSliderAfterUpdate &&
                     HasUISliderChangedForAccessValidation(uiSliderBeforeUpdate, *uiSliderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* directionalLightAfterUpdate = m_Registry.try_get<DirectionalLight2DComponent>(entity);
                const bool hasDirectionalLightAfterUpdate = directionalLightAfterUpdate != nullptr;
                if (hadDirectionalLightBeforeUpdate != hasDirectionalLightAfterUpdate ||
                    (hadDirectionalLightBeforeUpdate && directionalLightAfterUpdate &&
                     HasDirectionalLight2DChangedForAccessValidation(directionalLightBeforeUpdate, *directionalLightAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* pointLightAfterUpdate = m_Registry.try_get<PointLight2DComponent>(entity);
                const bool hasPointLightAfterUpdate = pointLightAfterUpdate != nullptr;
                if (hadPointLightBeforeUpdate != hasPointLightAfterUpdate ||
                    (hadPointLightBeforeUpdate && pointLightAfterUpdate &&
                     HasPointLight2DChangedForAccessValidation(pointLightBeforeUpdate, *pointLightAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* shadowOccluderAfterUpdate = m_Registry.try_get<ShadowOccluder2DComponent>(entity);
                const bool hasShadowOccluderAfterUpdate = shadowOccluderAfterUpdate != nullptr;
                if (hadShadowOccluderBeforeUpdate != hasShadowOccluderAfterUpdate ||
                    (hadShadowOccluderBeforeUpdate && shadowOccluderAfterUpdate &&
                     HasShadowOccluder2DChangedForAccessValidation(shadowOccluderBeforeUpdate, *shadowOccluderAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* audioListenerAfterUpdate = m_Registry.try_get<AudioListener2DComponent>(entity);
                const bool hasAudioListenerAfterUpdate = audioListenerAfterUpdate != nullptr;
                if (hadAudioListenerBeforeUpdate != hasAudioListenerAfterUpdate ||
                    (hadAudioListenerBeforeUpdate && audioListenerAfterUpdate &&
                     HasAudioListener2DChangedForAccessValidation(audioListenerBeforeUpdate, *audioListenerAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioSourceAfterUpdate = m_Registry.try_get<AudioSourceComponent>(entity);
                const bool hasAudioSourceAfterUpdate = audioSourceAfterUpdate != nullptr;
                if (hadAudioSourceBeforeUpdate != hasAudioSourceAfterUpdate ||
                    (hadAudioSourceBeforeUpdate && audioSourceAfterUpdate &&
                     HasAudioSourceChangedForAccessValidation(audioSourceBeforeUpdate, *audioSourceAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* cameraAfterUpdate = m_Registry.try_get<CameraComponent>(entity);
                const bool hasCameraAfterUpdate = cameraAfterUpdate != nullptr;
                if (hadCameraBeforeUpdate != hasCameraAfterUpdate ||
                    (hadCameraBeforeUpdate && cameraAfterUpdate &&
                     HasCameraChangedForAccessValidation(cameraBeforeUpdate, *cameraAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Camera);
                }

                const auto* prefabInstanceAfterUpdate = m_Registry.try_get<PrefabInstanceComponent>(entity);
                const bool hasPrefabInstanceAfterUpdate = prefabInstanceAfterUpdate != nullptr;
                if (hadPrefabInstanceBeforeUpdate != hasPrefabInstanceAfterUpdate ||
                    (hadPrefabInstanceBeforeUpdate && prefabInstanceAfterUpdate &&
                     HasPrefabInstanceChangedForAccessValidation(prefabInstanceBeforeUpdate, *prefabInstanceAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* grid2DAfterUpdate = m_Registry.try_get<Grid2DComponent>(entity);
                const bool hasGrid2DAfterUpdate = grid2DAfterUpdate != nullptr;
                if (hadGrid2DBeforeUpdate != hasGrid2DAfterUpdate ||
                    (hadGrid2DBeforeUpdate && grid2DAfterUpdate &&
                     HasGrid2DChangedForAccessValidation(grid2DBeforeUpdate, *grid2DAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* tilemapLayerAfterUpdate = m_Registry.try_get<TilemapLayerComponent>(entity);
                const bool hasTilemapLayerAfterUpdate = tilemapLayerAfterUpdate != nullptr;
                if (hadTilemapLayerBeforeUpdate != hasTilemapLayerAfterUpdate ||
                    (hadTilemapLayerBeforeUpdate && tilemapLayerAfterUpdate &&
                     HasTilemapLayerChangedForAccessValidation(tilemapLayerBeforeUpdate, *tilemapLayerAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* animatorAfterUpdate = m_Registry.try_get<AnimatorComponent>(entity);
                const bool hasAnimatorAfterUpdate = animatorAfterUpdate != nullptr;
                if (hadAnimatorBeforeUpdate != hasAnimatorAfterUpdate ||
                    (hadAnimatorBeforeUpdate && animatorAfterUpdate &&
                     HasAnimatorChangedForAccessValidation(animatorBeforeUpdate, *animatorAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* animationEventReceiverAfterUpdate = m_Registry.try_get<AnimationEventReceiverComponent>(entity);
                const bool hasAnimationEventReceiverAfterUpdate = animationEventReceiverAfterUpdate != nullptr;
                if (hadAnimationEventReceiverBeforeUpdate != hasAnimationEventReceiverAfterUpdate ||
                    (hadAnimationEventReceiverBeforeUpdate && animationEventReceiverAfterUpdate &&
                     HasAnimationEventReceiverChangedForAccessValidation(animationEventReceiverBeforeUpdate, *animationEventReceiverAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* particleEmitterAfterUpdate = m_Registry.try_get<ParticleEmitterComponent>(entity);
                const bool hasParticleEmitterAfterUpdate = particleEmitterAfterUpdate != nullptr;
                if (hadParticleEmitterBeforeUpdate != hasParticleEmitterAfterUpdate ||
                    (hadParticleEmitterBeforeUpdate && particleEmitterAfterUpdate &&
                     HasParticleEmitterChangedForAccessValidation(particleEmitterBeforeUpdate, *particleEmitterAfterUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
                }
            }

            validateParallelScriptAccessMask(entity, *scriptEntry, observedWriteMask, "OnUpdate");

            if (trackTransformMutation && !scriptEntry->RuntimeWarnedOnUpdateTransformMutation)
            {
                if (transformAfterUpdate && transformChanged)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is mutating Transform in OnUpdate while Rigidbody2D is Dynamic/Kinematic. Move physics-related transform writes to OnFixedUpdate for stable simulation.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = true;
                }
            }
        };

        // Two-phase runtime bootstrapping:
        // 1) Create all script instances for currently active slots.
        // 2) Invoke OnCreate/OnUpdate in slot order.
        // This lets scripts safely reference sibling scripts during OnCreate,
        // even when the referenced script appears later in the list.
        std::vector<std::pair<entt::entity, size_t>> parallelScriptSlots;
        parallelScriptSlots.reserve(scriptSlots.size());
        const bool enableParallelScripts = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool requireParallelScriptAccessDeclarations =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool warnOnImplicitParallelScriptAccess =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        auto tryQueueParallelScriptSlot = [&](entt::entity entity, size_t scriptIndex, NativeScriptEntry& scriptEntry) {
            if (!(enableParallelScripts && scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe))
                return false;

            const bool hasAccessDeclaration = scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0;
            if (!hasAccessDeclaration)
            {
                if (warnOnImplicitParallelScriptAccess && !scriptEntry.RuntimeWarnedMissingAccessDeclaration)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("ParallelSafe script '{}' on entity '{}' has no declared component access mask and will {}. Author DeclaredReadAccessMask/DeclaredWriteAccessMask for deterministic scheduling.",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            requireParallelScriptAccessDeclarations ? "run on the main thread this frame" : "use conservative scheduler barriers");
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = true;
                }

                if (requireParallelScriptAccessDeclarations)
                    return false;
            }

            parallelScriptSlots.emplace_back(entity, scriptIndex);
            return true;
        };
        SetRuntimePhase(RuntimePhase::ScriptMainThread);
        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance->m_Scene = this;
                scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInstance)
                {
                    if (scriptEntry->RuntimeInitialized)
                    {
                        try
                        {
                            scriptEntry->RuntimeInstance->OnDestroy();
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", "non-standard exception");
                        }
                    }
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                if (scriptEntry->RuntimeInstance)
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                }
                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                continue;
            }

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (!scriptEntry->RuntimeInstance)
            {
                if (!scriptEntry->RuntimeWarnedMissingCompiledScript)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingCompiledScript = true;
                }
                continue;
            }

            // Rebind runtime context every frame. NativeScriptEntry objects can move in memory
            // when the scripts vector grows/reorders, so cached pointers must be refreshed.
            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;

            if (!scriptEntry->RuntimeInitialized)
            {
                try
                {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->OnCreate();
                }
                catch (const std::exception& exception)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", exception.what());
                    continue;
                }
                catch (...)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", "non-standard exception");
                    continue;
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry || !scriptEntry->RuntimeInstance)
                    continue;
                scriptEntry->RuntimeInitialized = true;
            }

            applyScriptDeclaredAccessDefaults(*scriptEntry);
            if (tryQueueParallelScriptSlot(entity, scriptIndex, *scriptEntry))
            {
                continue;
            }

            executeScriptUpdateSlot(entity, scriptIndex);
        }

        if (!parallelScriptSlots.empty())
        {
            auto& jobSystem = Concurrency::GetJobSystem();
            const size_t parallelScriptMinSlots = ResolveParallelScriptMinSlots(jobSystem);
            const size_t parallelScriptMinBatchSize = ResolveParallelScriptMinBatchSize();
            const bool shouldUseParallelJobs = jobSystem.IsInitialized() &&
                                               parallelScriptSlots.size() >= parallelScriptMinSlots;
            auto runParallelSlotAtIndex = [&](size_t slotIndex) {
                struct ScopedParallelScriptThreadFlag final
                {
                    ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(true); }
                    ~ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(false); }
                } scopedThreadFlag;

                const auto& slot = parallelScriptSlots[slotIndex];
                executeScriptUpdateSlot(slot.first, slot.second);
            };
            if (shouldUseParallelJobs)
                SetRuntimePhase(RuntimePhase::ScriptParallel);

            auto hasAccessHazard = [](const SceneSystemAccess& left, const SceneSystemAccess& right) {
                return (left.Writes & right.Writes) != 0 ||
                       (left.Writes & right.Reads) != 0 ||
                       (left.Reads & right.Writes) != 0;
            };
            auto getScriptAccess = [&](size_t slotIndex) {
                SceneSystemAccess access{};
                const auto& slot = parallelScriptSlots[slotIndex];
                const NativeScriptEntry* scriptEntry = tryGetScriptEntry(slot.first, slot.second);
                if (!scriptEntry)
                {
                    access.Writes = ~0ull;
                    return access;
                }
                access.Reads = scriptEntry->DeclaredReadAccessMask;
                access.Writes = scriptEntry->DeclaredWriteAccessMask;
                if (access.Reads == 0 && access.Writes == 0)
                    access.Writes = ~0ull;
                return access;
            };

            std::vector<size_t> pendingIndices(parallelScriptSlots.size());
            for (size_t index = 0; index < pendingIndices.size(); ++index)
                pendingIndices[index] = index;

            while (!pendingIndices.empty())
            {
                std::vector<size_t> batchIndices;
                std::vector<size_t> nextPendingIndices;
                batchIndices.reserve(pendingIndices.size());
                nextPendingIndices.reserve(pendingIndices.size());

                for (size_t pendingIndex : pendingIndices)
                {
                    const SceneSystemAccess candidateAccess = getScriptAccess(pendingIndex);
                    bool conflicts = false;
                    for (size_t batchIndex : batchIndices)
                    {
                        if (hasAccessHazard(candidateAccess, getScriptAccess(batchIndex)))
                        {
                            conflicts = true;
                            break;
                        }
                    }

                    if (!conflicts)
                        batchIndices.push_back(pendingIndex);
                    else
                        nextPendingIndices.push_back(pendingIndex);
                }

                if (batchIndices.empty())
                {
                    batchIndices.push_back(pendingIndices.front());
                    nextPendingIndices.erase(std::remove(nextPendingIndices.begin(), nextPendingIndices.end(), pendingIndices.front()), nextPendingIndices.end());
                }

                if (shouldUseParallelJobs && batchIndices.size() >= parallelScriptMinBatchSize)
                {
                    Concurrency::WaitGroup waitGroup;
                    for (size_t batchIndex : batchIndices)
                    {
                        waitGroup.Add(1);
                        jobSystem.Submit([&runParallelSlotAtIndex, &waitGroup, batchIndex]() {
                            runParallelSlotAtIndex(batchIndex);
                            waitGroup.Done();
                        });
                    }
                    waitGroup.Wait();
                }
                else
                {
                    for (size_t batchIndex : batchIndices)
                        runParallelSlotAtIndex(batchIndex);
                }

                pendingIndices = std::move(nextPendingIndices);
            }
        }

        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        runScheduledSimulationSystems();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }

    void Scene::FixedUpdate(float fixedDeltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::Idle);
            return;
        }

        std::vector<std::pair<entt::entity, size_t>> scriptSlots;
        {
            auto snapshotView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : snapshotView)
            {
                const auto& nativeScript = snapshotView.get<NativeScriptComponent>(entity);
                for (size_t scriptIndex = 0; scriptIndex < nativeScript.Scripts.size(); ++scriptIndex)
                    scriptSlots.emplace_back(entity, scriptIndex);
            }
        }

        auto tryGetScriptEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> NativeScriptEntry* {
            auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(scriptEntity);
            if (!nativeScript || scriptIndex >= nativeScript->Scripts.size())
                return nullptr;
            return &nativeScript->Scripts[scriptIndex];
        };

        auto applyScriptDeclaredAccessDefaults = [&](NativeScriptEntry& scriptEntry) {
            if (!scriptEntry.RuntimeInstance)
                return;
            if (scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0)
                return;

            const uint64_t defaultReadMask = scriptEntry.RuntimeInstance->GetDeclaredReadAccessMask();
            const uint64_t defaultWriteMask = scriptEntry.RuntimeInstance->GetDeclaredWriteAccessMask();
            if (defaultReadMask != 0 || defaultWriteMask != 0)
            {
                scriptEntry.DeclaredReadAccessMask = defaultReadMask;
                scriptEntry.DeclaredWriteAccessMask = defaultWriteMask;
            }
        };

        auto handleScriptCallbackFailure = [&](entt::entity scriptEntity,
                                               size_t scriptIndex,
                                               std::string_view callbackName,
                                               const char* message) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            if (scriptEntry->RuntimeInstance)
            {
                try
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                }
                catch (const std::exception& exception)
                {
                    LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup after {} failure: {}",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName,
                            exception.what());
                }
                catch (...)
                {
                    LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup after {} failure",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            callbackName);
                }
            }
            scriptEntry->RuntimeInstance.reset();
            scriptEntry->RuntimeInitialized = false;
            scriptEntry->RuntimeUpdateCount = 0;
            scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
            scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
        };

        const bool validateParallelScriptAccessMasks =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.validate_parallel_script_access_masks", true);
        const bool warnOnParallelScriptAccessMaskMismatch =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_parallel_script_access_mismatch", true);
        auto validateParallelScriptAccessMask = [&](entt::entity entity,
                                                    NativeScriptEntry& scriptEntry,
                                                    uint64_t observedWriteMask,
                                                    std::string_view callbackName) {
            if (!validateParallelScriptAccessMasks || observedWriteMask == 0)
                return;
            if (!Scene::IsCurrentThreadParallelScriptExecution())
                return;
            if (scriptEntry.ExecutionPolicy != ScriptExecutionPolicy::ParallelSafe)
                return;

            const uint64_t missingWriteMask = observedWriteMask & ~scriptEntry.DeclaredWriteAccessMask;
            if (missingWriteMask == 0)
                return;

            const uint64_t mismatchCount = s_ParallelScriptAccessMaskMismatchCount.fetch_add(1, std::memory_order_relaxed) + 1;
            if (warnOnParallelScriptAccessMaskMismatch && !scriptEntry.RuntimeWarnedAccessMaskMismatch)
            {
                const auto* tag = m_Registry.try_get<TagComponent>(entity);
                LT_WARN("ParallelSafe script '{}' on entity '{}' mutated [{}] during {} without declaring write access (declared={} observed={} missing={}). Add required bits to DeclaredWriteAccessMask. total_mismatches={}",
                        scriptEntry.ScriptClassName,
                        tag ? tag->Tag : "Entity",
                        DescribeAccessMask(observedWriteMask),
                        callbackName,
                        DescribeAccessMask(scriptEntry.DeclaredWriteAccessMask),
                        DescribeAccessMask(observedWriteMask),
                        DescribeAccessMask(missingWriteMask),
                        mismatchCount);
                scriptEntry.RuntimeWarnedAccessMaskMismatch = true;
            }
        };

        auto executeScriptFixedUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeFixedUpdate{};
            bool hadTransformBeforeFixedUpdate = false;
            HierarchyComponent hierarchyBeforeFixedUpdate{};
            bool hadHierarchyBeforeFixedUpdate = false;
            Rigidbody2DComponent rigidbodyBeforeFixedUpdate{};
            bool hadRigidbodyBeforeFixedUpdate = false;
            BoxCollider2DComponent boxColliderBeforeFixedUpdate{};
            bool hadBoxColliderBeforeFixedUpdate = false;
            CircleCollider2DComponent circleColliderBeforeFixedUpdate{};
            bool hadCircleColliderBeforeFixedUpdate = false;
            Joint2DComponent joint2DBeforeFixedUpdate{};
            bool hadJoint2DBeforeFixedUpdate = false;
            TagComponent tagBeforeFixedUpdate{};
            bool hadTagBeforeFixedUpdate = false;
            SpriteComponent spriteBeforeFixedUpdate{};
            bool hadSpriteBeforeFixedUpdate = false;
            MaterialComponent materialBeforeFixedUpdate{};
            bool hadMaterialBeforeFixedUpdate = false;
            CanvasComponent canvasBeforeFixedUpdate{};
            bool hadCanvasBeforeFixedUpdate = false;
            RectTransformComponent rectTransformBeforeFixedUpdate{};
            bool hadRectTransformBeforeFixedUpdate = false;
            UIImageComponent uiImageBeforeFixedUpdate{};
            bool hadUIImageBeforeFixedUpdate = false;
            UIPanelComponent uiPanelBeforeFixedUpdate{};
            bool hadUIPanelBeforeFixedUpdate = false;
            UITextComponent uiTextBeforeFixedUpdate{};
            bool hadUITextBeforeFixedUpdate = false;
            UIButtonComponent uiButtonBeforeFixedUpdate{};
            bool hadUIButtonBeforeFixedUpdate = false;
            UISliderComponent uiSliderBeforeFixedUpdate{};
            bool hadUISliderBeforeFixedUpdate = false;
            DirectionalLight2DComponent directionalLightBeforeFixedUpdate{};
            bool hadDirectionalLightBeforeFixedUpdate = false;
            PointLight2DComponent pointLightBeforeFixedUpdate{};
            bool hadPointLightBeforeFixedUpdate = false;
            ShadowOccluder2DComponent shadowOccluderBeforeFixedUpdate{};
            bool hadShadowOccluderBeforeFixedUpdate = false;
            AudioListener2DComponent audioListenerBeforeFixedUpdate{};
            bool hadAudioListenerBeforeFixedUpdate = false;
            AudioSourceComponent audioSourceBeforeFixedUpdate{};
            bool hadAudioSourceBeforeFixedUpdate = false;
            CameraComponent cameraBeforeFixedUpdate{};
            bool hadCameraBeforeFixedUpdate = false;
            PrefabInstanceComponent prefabInstanceBeforeFixedUpdate{};
            bool hadPrefabInstanceBeforeFixedUpdate = false;
            Grid2DComponent grid2DBeforeFixedUpdate{};
            bool hadGrid2DBeforeFixedUpdate = false;
            TilemapLayerComponent tilemapLayerBeforeFixedUpdate{};
            bool hadTilemapLayerBeforeFixedUpdate = false;
            AnimatorComponent animatorBeforeFixedUpdate{};
            bool hadAnimatorBeforeFixedUpdate = false;
            AnimationEventReceiverComponent animationEventReceiverBeforeFixedUpdate{};
            bool hadAnimationEventReceiverBeforeFixedUpdate = false;
            ParticleEmitterComponent particleEmitterBeforeFixedUpdate{};
            bool hadParticleEmitterBeforeFixedUpdate = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeFixedUpdate = *transform;
                hadTransformBeforeFixedUpdate = true;
            }
            const bool trackParallelAccessValidation =
                validateParallelScriptAccessMasks &&
                scriptEntry->ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe;
            if (trackParallelAccessValidation)
            {
                if (auto* hierarchy = m_Registry.try_get<HierarchyComponent>(entity))
                {
                    hierarchyBeforeFixedUpdate = *hierarchy;
                    hadHierarchyBeforeFixedUpdate = true;
                }
                if (auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    rigidbodyBeforeFixedUpdate = *rigidbody2D;
                    hadRigidbodyBeforeFixedUpdate = true;
                }
                if (auto* boxCollider2D = m_Registry.try_get<BoxCollider2DComponent>(entity))
                {
                    boxColliderBeforeFixedUpdate = *boxCollider2D;
                    hadBoxColliderBeforeFixedUpdate = true;
                }
                if (auto* circleCollider2D = m_Registry.try_get<CircleCollider2DComponent>(entity))
                {
                    circleColliderBeforeFixedUpdate = *circleCollider2D;
                    hadCircleColliderBeforeFixedUpdate = true;
                }
                if (auto* joint2D = m_Registry.try_get<Joint2DComponent>(entity))
                {
                    joint2DBeforeFixedUpdate = *joint2D;
                    hadJoint2DBeforeFixedUpdate = true;
                }
                if (auto* tag = m_Registry.try_get<TagComponent>(entity))
                {
                    tagBeforeFixedUpdate = *tag;
                    hadTagBeforeFixedUpdate = true;
                }
                if (auto* sprite = m_Registry.try_get<SpriteComponent>(entity))
                {
                    spriteBeforeFixedUpdate = *sprite;
                    hadSpriteBeforeFixedUpdate = true;
                }
                if (auto* material = m_Registry.try_get<MaterialComponent>(entity))
                {
                    materialBeforeFixedUpdate = *material;
                    hadMaterialBeforeFixedUpdate = true;
                }
                if (auto* canvas = m_Registry.try_get<CanvasComponent>(entity))
                {
                    canvasBeforeFixedUpdate = *canvas;
                    hadCanvasBeforeFixedUpdate = true;
                }
                if (auto* rectTransform = m_Registry.try_get<RectTransformComponent>(entity))
                {
                    rectTransformBeforeFixedUpdate = *rectTransform;
                    hadRectTransformBeforeFixedUpdate = true;
                }
                if (auto* uiImage = m_Registry.try_get<UIImageComponent>(entity))
                {
                    uiImageBeforeFixedUpdate = *uiImage;
                    hadUIImageBeforeFixedUpdate = true;
                }
                if (auto* uiPanel = m_Registry.try_get<UIPanelComponent>(entity))
                {
                    uiPanelBeforeFixedUpdate = *uiPanel;
                    hadUIPanelBeforeFixedUpdate = true;
                }
                if (auto* uiText = m_Registry.try_get<UITextComponent>(entity))
                {
                    uiTextBeforeFixedUpdate = *uiText;
                    hadUITextBeforeFixedUpdate = true;
                }
                if (auto* uiButton = m_Registry.try_get<UIButtonComponent>(entity))
                {
                    uiButtonBeforeFixedUpdate = *uiButton;
                    hadUIButtonBeforeFixedUpdate = true;
                }
                if (auto* uiSlider = m_Registry.try_get<UISliderComponent>(entity))
                {
                    uiSliderBeforeFixedUpdate = *uiSlider;
                    hadUISliderBeforeFixedUpdate = true;
                }
                if (auto* directionalLight = m_Registry.try_get<DirectionalLight2DComponent>(entity))
                {
                    directionalLightBeforeFixedUpdate = *directionalLight;
                    hadDirectionalLightBeforeFixedUpdate = true;
                }
                if (auto* pointLight = m_Registry.try_get<PointLight2DComponent>(entity))
                {
                    pointLightBeforeFixedUpdate = *pointLight;
                    hadPointLightBeforeFixedUpdate = true;
                }
                if (auto* shadowOccluder = m_Registry.try_get<ShadowOccluder2DComponent>(entity))
                {
                    shadowOccluderBeforeFixedUpdate = *shadowOccluder;
                    hadShadowOccluderBeforeFixedUpdate = true;
                }
                if (auto* audioListener = m_Registry.try_get<AudioListener2DComponent>(entity))
                {
                    audioListenerBeforeFixedUpdate = *audioListener;
                    hadAudioListenerBeforeFixedUpdate = true;
                }
                if (auto* audioSource = m_Registry.try_get<AudioSourceComponent>(entity))
                {
                    audioSourceBeforeFixedUpdate = *audioSource;
                    hadAudioSourceBeforeFixedUpdate = true;
                }
                if (auto* camera = m_Registry.try_get<CameraComponent>(entity))
                {
                    cameraBeforeFixedUpdate = *camera;
                    hadCameraBeforeFixedUpdate = true;
                }
                if (auto* prefabInstance = m_Registry.try_get<PrefabInstanceComponent>(entity))
                {
                    prefabInstanceBeforeFixedUpdate = *prefabInstance;
                    hadPrefabInstanceBeforeFixedUpdate = true;
                }
                if (auto* grid2D = m_Registry.try_get<Grid2DComponent>(entity))
                {
                    grid2DBeforeFixedUpdate = *grid2D;
                    hadGrid2DBeforeFixedUpdate = true;
                }
                if (auto* tilemapLayer = m_Registry.try_get<TilemapLayerComponent>(entity))
                {
                    tilemapLayerBeforeFixedUpdate = *tilemapLayer;
                    hadTilemapLayerBeforeFixedUpdate = true;
                }
                if (auto* animator = m_Registry.try_get<AnimatorComponent>(entity))
                {
                    animatorBeforeFixedUpdate = *animator;
                    hadAnimatorBeforeFixedUpdate = true;
                }
                if (auto* animationEventReceiver = m_Registry.try_get<AnimationEventReceiverComponent>(entity))
                {
                    animationEventReceiverBeforeFixedUpdate = *animationEventReceiver;
                    hadAnimationEventReceiverBeforeFixedUpdate = true;
                }
                if (auto* particleEmitter = m_Registry.try_get<ParticleEmitterComponent>(entity))
                {
                    particleEmitterBeforeFixedUpdate = *particleEmitter;
                    hadParticleEmitterBeforeFixedUpdate = true;
                }
            }

            try
            {
                scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry->RuntimeInstance->OnFixedUpdate(fixedDeltaTime);
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", "non-standard exception");
                return;
            }

            const auto* transformAfterFixedUpdate = m_Registry.try_get<TransformComponent>(entity);
            bool transformChanged = false;
            if (hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                transformChanged = HasTransformChangedForAccessValidation(transformBeforeFixedUpdate, *transformAfterFixedUpdate);
                if (transformChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                transformChanged = true;
                MarkTransformDirty(entity);
            }

            uint64_t observedWriteMask = 0;
            if (transformChanged)
                observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Transform);
            if (trackParallelAccessValidation)
            {
                const auto* hierarchyAfterFixedUpdate = m_Registry.try_get<HierarchyComponent>(entity);
                const bool hasHierarchyAfterFixedUpdate = hierarchyAfterFixedUpdate != nullptr;
                if (hadHierarchyBeforeFixedUpdate != hasHierarchyAfterFixedUpdate ||
                    (hadHierarchyBeforeFixedUpdate && hierarchyAfterFixedUpdate &&
                     HasHierarchyChangedForAccessValidation(hierarchyBeforeFixedUpdate, *hierarchyAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Hierarchy);
                }

                const auto* rigidbodyAfterFixedUpdate = m_Registry.try_get<Rigidbody2DComponent>(entity);
                const bool hasRigidbodyAfterFixedUpdate = rigidbodyAfterFixedUpdate != nullptr;
                if (hadRigidbodyBeforeFixedUpdate != hasRigidbodyAfterFixedUpdate ||
                    (hadRigidbodyBeforeFixedUpdate && rigidbodyAfterFixedUpdate &&
                     HasRigidbodyChangedForAccessValidation(rigidbodyBeforeFixedUpdate, *rigidbodyAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rigidbody2D);
                }

                const auto* boxColliderAfterFixedUpdate = m_Registry.try_get<BoxCollider2DComponent>(entity);
                const bool hasBoxColliderAfterFixedUpdate = boxColliderAfterFixedUpdate != nullptr;
                if (hadBoxColliderBeforeFixedUpdate != hasBoxColliderAfterFixedUpdate ||
                    (hadBoxColliderBeforeFixedUpdate && boxColliderAfterFixedUpdate &&
                     HasBoxColliderChangedForAccessValidation(boxColliderBeforeFixedUpdate, *boxColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::BoxCollider2D);
                }

                const auto* circleColliderAfterFixedUpdate = m_Registry.try_get<CircleCollider2DComponent>(entity);
                const bool hasCircleColliderAfterFixedUpdate = circleColliderAfterFixedUpdate != nullptr;
                if (hadCircleColliderBeforeFixedUpdate != hasCircleColliderAfterFixedUpdate ||
                    (hadCircleColliderBeforeFixedUpdate && circleColliderAfterFixedUpdate &&
                     HasCircleColliderChangedForAccessValidation(circleColliderBeforeFixedUpdate, *circleColliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::CircleCollider2D);
                }

                const auto* joint2DAfterFixedUpdate = m_Registry.try_get<Joint2DComponent>(entity);
                const bool hasJoint2DAfterFixedUpdate = joint2DAfterFixedUpdate != nullptr;
                if (hadJoint2DBeforeFixedUpdate != hasJoint2DAfterFixedUpdate ||
                    (hadJoint2DBeforeFixedUpdate && joint2DAfterFixedUpdate &&
                     HasJoint2DChangedForAccessValidation(joint2DBeforeFixedUpdate, *joint2DAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Joint2D);
                }

                const auto* tagAfterFixedUpdate = m_Registry.try_get<TagComponent>(entity);
                const bool hasTagAfterFixedUpdate = tagAfterFixedUpdate != nullptr;
                if (hadTagBeforeFixedUpdate != hasTagAfterFixedUpdate ||
                    (hadTagBeforeFixedUpdate && tagAfterFixedUpdate &&
                     HasTagChangedForAccessValidation(tagBeforeFixedUpdate, *tagAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* spriteAfterFixedUpdate = m_Registry.try_get<SpriteComponent>(entity);
                const bool hasSpriteAfterFixedUpdate = spriteAfterFixedUpdate != nullptr;
                if (hadSpriteBeforeFixedUpdate != hasSpriteAfterFixedUpdate ||
                    (hadSpriteBeforeFixedUpdate && spriteAfterFixedUpdate &&
                     HasSpriteChangedForAccessValidation(spriteBeforeFixedUpdate, *spriteAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* materialAfterFixedUpdate = m_Registry.try_get<MaterialComponent>(entity);
                const bool hasMaterialAfterFixedUpdate = materialAfterFixedUpdate != nullptr;
                if (hadMaterialBeforeFixedUpdate != hasMaterialAfterFixedUpdate ||
                    (hadMaterialBeforeFixedUpdate && materialAfterFixedUpdate &&
                     HasMaterialChangedForAccessValidation(materialBeforeFixedUpdate, *materialAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Rendering2D);
                }

                const auto* canvasAfterFixedUpdate = m_Registry.try_get<CanvasComponent>(entity);
                const bool hasCanvasAfterFixedUpdate = canvasAfterFixedUpdate != nullptr;
                if (hadCanvasBeforeFixedUpdate != hasCanvasAfterFixedUpdate ||
                    (hadCanvasBeforeFixedUpdate && canvasAfterFixedUpdate &&
                     HasCanvasChangedForAccessValidation(canvasBeforeFixedUpdate, *canvasAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* rectTransformAfterFixedUpdate = m_Registry.try_get<RectTransformComponent>(entity);
                const bool hasRectTransformAfterFixedUpdate = rectTransformAfterFixedUpdate != nullptr;
                if (hadRectTransformBeforeFixedUpdate != hasRectTransformAfterFixedUpdate ||
                    (hadRectTransformBeforeFixedUpdate && rectTransformAfterFixedUpdate &&
                     HasRectTransformChangedForAccessValidation(rectTransformBeforeFixedUpdate, *rectTransformAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiImageAfterFixedUpdate = m_Registry.try_get<UIImageComponent>(entity);
                const bool hasUIImageAfterFixedUpdate = uiImageAfterFixedUpdate != nullptr;
                if (hadUIImageBeforeFixedUpdate != hasUIImageAfterFixedUpdate ||
                    (hadUIImageBeforeFixedUpdate && uiImageAfterFixedUpdate &&
                     HasUIImageChangedForAccessValidation(uiImageBeforeFixedUpdate, *uiImageAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiPanelAfterFixedUpdate = m_Registry.try_get<UIPanelComponent>(entity);
                const bool hasUIPanelAfterFixedUpdate = uiPanelAfterFixedUpdate != nullptr;
                if (hadUIPanelBeforeFixedUpdate != hasUIPanelAfterFixedUpdate ||
                    (hadUIPanelBeforeFixedUpdate && uiPanelAfterFixedUpdate &&
                     HasUIPanelChangedForAccessValidation(uiPanelBeforeFixedUpdate, *uiPanelAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiTextAfterFixedUpdate = m_Registry.try_get<UITextComponent>(entity);
                const bool hasUITextAfterFixedUpdate = uiTextAfterFixedUpdate != nullptr;
                if (hadUITextBeforeFixedUpdate != hasUITextAfterFixedUpdate ||
                    (hadUITextBeforeFixedUpdate && uiTextAfterFixedUpdate &&
                     HasUITextChangedForAccessValidation(uiTextBeforeFixedUpdate, *uiTextAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiButtonAfterFixedUpdate = m_Registry.try_get<UIButtonComponent>(entity);
                const bool hasUIButtonAfterFixedUpdate = uiButtonAfterFixedUpdate != nullptr;
                if (hadUIButtonBeforeFixedUpdate != hasUIButtonAfterFixedUpdate ||
                    (hadUIButtonBeforeFixedUpdate && uiButtonAfterFixedUpdate &&
                     HasUIButtonChangedForAccessValidation(uiButtonBeforeFixedUpdate, *uiButtonAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* uiSliderAfterFixedUpdate = m_Registry.try_get<UISliderComponent>(entity);
                const bool hasUISliderAfterFixedUpdate = uiSliderAfterFixedUpdate != nullptr;
                if (hadUISliderBeforeFixedUpdate != hasUISliderAfterFixedUpdate ||
                    (hadUISliderBeforeFixedUpdate && uiSliderAfterFixedUpdate &&
                     HasUISliderChangedForAccessValidation(uiSliderBeforeFixedUpdate, *uiSliderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::UI);
                }

                const auto* directionalLightAfterFixedUpdate = m_Registry.try_get<DirectionalLight2DComponent>(entity);
                const bool hasDirectionalLightAfterFixedUpdate = directionalLightAfterFixedUpdate != nullptr;
                if (hadDirectionalLightBeforeFixedUpdate != hasDirectionalLightAfterFixedUpdate ||
                    (hadDirectionalLightBeforeFixedUpdate && directionalLightAfterFixedUpdate &&
                     HasDirectionalLight2DChangedForAccessValidation(directionalLightBeforeFixedUpdate, *directionalLightAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* pointLightAfterFixedUpdate = m_Registry.try_get<PointLight2DComponent>(entity);
                const bool hasPointLightAfterFixedUpdate = pointLightAfterFixedUpdate != nullptr;
                if (hadPointLightBeforeFixedUpdate != hasPointLightAfterFixedUpdate ||
                    (hadPointLightBeforeFixedUpdate && pointLightAfterFixedUpdate &&
                     HasPointLight2DChangedForAccessValidation(pointLightBeforeFixedUpdate, *pointLightAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* shadowOccluderAfterFixedUpdate = m_Registry.try_get<ShadowOccluder2DComponent>(entity);
                const bool hasShadowOccluderAfterFixedUpdate = shadowOccluderAfterFixedUpdate != nullptr;
                if (hadShadowOccluderBeforeFixedUpdate != hasShadowOccluderAfterFixedUpdate ||
                    (hadShadowOccluderBeforeFixedUpdate && shadowOccluderAfterFixedUpdate &&
                     HasShadowOccluder2DChangedForAccessValidation(shadowOccluderBeforeFixedUpdate, *shadowOccluderAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Lighting2D);
                }

                const auto* audioListenerAfterFixedUpdate = m_Registry.try_get<AudioListener2DComponent>(entity);
                const bool hasAudioListenerAfterFixedUpdate = audioListenerAfterFixedUpdate != nullptr;
                if (hadAudioListenerBeforeFixedUpdate != hasAudioListenerAfterFixedUpdate ||
                    (hadAudioListenerBeforeFixedUpdate && audioListenerAfterFixedUpdate &&
                     HasAudioListener2DChangedForAccessValidation(audioListenerBeforeFixedUpdate, *audioListenerAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* audioSourceAfterFixedUpdate = m_Registry.try_get<AudioSourceComponent>(entity);
                const bool hasAudioSourceAfterFixedUpdate = audioSourceAfterFixedUpdate != nullptr;
                if (hadAudioSourceBeforeFixedUpdate != hasAudioSourceAfterFixedUpdate ||
                    (hadAudioSourceBeforeFixedUpdate && audioSourceAfterFixedUpdate &&
                     HasAudioSourceChangedForAccessValidation(audioSourceBeforeFixedUpdate, *audioSourceAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Audio);
                }

                const auto* cameraAfterFixedUpdate = m_Registry.try_get<CameraComponent>(entity);
                const bool hasCameraAfterFixedUpdate = cameraAfterFixedUpdate != nullptr;
                if (hadCameraBeforeFixedUpdate != hasCameraAfterFixedUpdate ||
                    (hadCameraBeforeFixedUpdate && cameraAfterFixedUpdate &&
                     HasCameraChangedForAccessValidation(cameraBeforeFixedUpdate, *cameraAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Camera);
                }

                const auto* prefabInstanceAfterFixedUpdate = m_Registry.try_get<PrefabInstanceComponent>(entity);
                const bool hasPrefabInstanceAfterFixedUpdate = prefabInstanceAfterFixedUpdate != nullptr;
                if (hadPrefabInstanceBeforeFixedUpdate != hasPrefabInstanceAfterFixedUpdate ||
                    (hadPrefabInstanceBeforeFixedUpdate && prefabInstanceAfterFixedUpdate &&
                     HasPrefabInstanceChangedForAccessValidation(prefabInstanceBeforeFixedUpdate, *prefabInstanceAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Metadata);
                }

                const auto* grid2DAfterFixedUpdate = m_Registry.try_get<Grid2DComponent>(entity);
                const bool hasGrid2DAfterFixedUpdate = grid2DAfterFixedUpdate != nullptr;
                if (hadGrid2DBeforeFixedUpdate != hasGrid2DAfterFixedUpdate ||
                    (hadGrid2DBeforeFixedUpdate && grid2DAfterFixedUpdate &&
                     HasGrid2DChangedForAccessValidation(grid2DBeforeFixedUpdate, *grid2DAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* tilemapLayerAfterFixedUpdate = m_Registry.try_get<TilemapLayerComponent>(entity);
                const bool hasTilemapLayerAfterFixedUpdate = tilemapLayerAfterFixedUpdate != nullptr;
                if (hadTilemapLayerBeforeFixedUpdate != hasTilemapLayerAfterFixedUpdate ||
                    (hadTilemapLayerBeforeFixedUpdate && tilemapLayerAfterFixedUpdate &&
                     HasTilemapLayerChangedForAccessValidation(tilemapLayerBeforeFixedUpdate, *tilemapLayerAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Tilemap);
                }

                const auto* animatorAfterFixedUpdate = m_Registry.try_get<AnimatorComponent>(entity);
                const bool hasAnimatorAfterFixedUpdate = animatorAfterFixedUpdate != nullptr;
                if (hadAnimatorBeforeFixedUpdate != hasAnimatorAfterFixedUpdate ||
                    (hadAnimatorBeforeFixedUpdate && animatorAfterFixedUpdate &&
                     HasAnimatorChangedForAccessValidation(animatorBeforeFixedUpdate, *animatorAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* animationEventReceiverAfterFixedUpdate = m_Registry.try_get<AnimationEventReceiverComponent>(entity);
                const bool hasAnimationEventReceiverAfterFixedUpdate = animationEventReceiverAfterFixedUpdate != nullptr;
                if (hadAnimationEventReceiverBeforeFixedUpdate != hasAnimationEventReceiverAfterFixedUpdate ||
                    (hadAnimationEventReceiverBeforeFixedUpdate && animationEventReceiverAfterFixedUpdate &&
                     HasAnimationEventReceiverChangedForAccessValidation(animationEventReceiverBeforeFixedUpdate, *animationEventReceiverAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::Animator);
                }

                const auto* particleEmitterAfterFixedUpdate = m_Registry.try_get<ParticleEmitterComponent>(entity);
                const bool hasParticleEmitterAfterFixedUpdate = particleEmitterAfterFixedUpdate != nullptr;
                if (hadParticleEmitterBeforeFixedUpdate != hasParticleEmitterAfterFixedUpdate ||
                    (hadParticleEmitterBeforeFixedUpdate && particleEmitterAfterFixedUpdate &&
                     HasParticleEmitterChangedForAccessValidation(particleEmitterBeforeFixedUpdate, *particleEmitterAfterFixedUpdate)))
                {
                    observedWriteMask |= ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
                }
            }

            validateParallelScriptAccessMask(entity, *scriptEntry, observedWriteMask, "OnFixedUpdate");
        };

        // Mirror Update() bootstrap so FixedUpdate callbacks can also resolve
        // other scripts during OnCreate regardless of declaration order.
        std::vector<std::pair<entt::entity, size_t>> parallelScriptSlots;
        parallelScriptSlots.reserve(scriptSlots.size());
        const bool enableParallelScripts = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool requireParallelScriptAccessDeclarations =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool warnOnImplicitParallelScriptAccess =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        auto tryQueueParallelScriptSlot = [&](entt::entity entity, size_t scriptIndex, NativeScriptEntry& scriptEntry) {
            if (!(enableParallelScripts && scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe))
                return false;

            const bool hasAccessDeclaration = scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0;
            if (!hasAccessDeclaration)
            {
                if (warnOnImplicitParallelScriptAccess && !scriptEntry.RuntimeWarnedMissingAccessDeclaration)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("ParallelSafe script '{}' on entity '{}' has no declared component access mask and will {}. Author DeclaredReadAccessMask/DeclaredWriteAccessMask for deterministic scheduling.",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            requireParallelScriptAccessDeclarations ? "run on the main thread this frame" : "use conservative scheduler barriers");
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = true;
                }

                if (requireParallelScriptAccessDeclarations)
                    return false;
            }

            parallelScriptSlots.emplace_back(entity, scriptIndex);
            return true;
        };
        SetRuntimePhase(RuntimePhase::ScriptMainThread);
        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance->m_Scene = this;
                scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInstance)
                {
                    if (scriptEntry->RuntimeInitialized)
                    {
                        try
                        {
                            scriptEntry->RuntimeInstance->OnDestroy();
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", "non-standard exception");
                        }
                    }
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                if (scriptEntry->RuntimeInstance)
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                }
                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                continue;
            }

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                    scriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                }
            }

            if (!scriptEntry->RuntimeInstance)
            {
                if (!scriptEntry->RuntimeWarnedMissingCompiledScript)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingCompiledScript = true;
                }
                continue;
            }

            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;

            if (!scriptEntry->RuntimeInitialized)
            {
                try
                {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->OnCreate();
                }
                catch (const std::exception& exception)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", exception.what());
                    continue;
                }
                catch (...)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", "non-standard exception");
                    continue;
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry || !scriptEntry->RuntimeInstance)
                    continue;
                scriptEntry->RuntimeInitialized = true;
            }

            applyScriptDeclaredAccessDefaults(*scriptEntry);
            if (tryQueueParallelScriptSlot(entity, scriptIndex, *scriptEntry))
            {
                continue;
            }

            executeScriptFixedUpdateSlot(entity, scriptIndex);
        }

        if (!parallelScriptSlots.empty())
        {
            auto& jobSystem = Concurrency::GetJobSystem();
            const size_t parallelScriptMinSlots = ResolveParallelScriptMinSlots(jobSystem);
            const size_t parallelScriptMinBatchSize = ResolveParallelScriptMinBatchSize();
            const bool shouldUseParallelJobs = jobSystem.IsInitialized() &&
                                               parallelScriptSlots.size() >= parallelScriptMinSlots;
            auto runParallelSlotAtIndex = [&](size_t slotIndex) {
                struct ScopedParallelScriptThreadFlag final
                {
                    ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(true); }
                    ~ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(false); }
                } scopedThreadFlag;

                const auto& slot = parallelScriptSlots[slotIndex];
                executeScriptFixedUpdateSlot(slot.first, slot.second);
            };
            if (shouldUseParallelJobs)
                SetRuntimePhase(RuntimePhase::ScriptParallel);

            auto hasAccessHazard = [](const SceneSystemAccess& left, const SceneSystemAccess& right) {
                return (left.Writes & right.Writes) != 0 ||
                       (left.Writes & right.Reads) != 0 ||
                       (left.Reads & right.Writes) != 0;
            };
            auto getScriptAccess = [&](size_t slotIndex) {
                SceneSystemAccess access{};
                const auto& slot = parallelScriptSlots[slotIndex];
                const NativeScriptEntry* scriptEntry = tryGetScriptEntry(slot.first, slot.second);
                if (!scriptEntry)
                {
                    access.Writes = ~0ull;
                    return access;
                }
                access.Reads = scriptEntry->DeclaredReadAccessMask;
                access.Writes = scriptEntry->DeclaredWriteAccessMask;
                if (access.Reads == 0 && access.Writes == 0)
                    access.Writes = ~0ull;
                return access;
            };

            std::vector<size_t> pendingIndices(parallelScriptSlots.size());
            for (size_t index = 0; index < pendingIndices.size(); ++index)
                pendingIndices[index] = index;

            while (!pendingIndices.empty())
            {
                std::vector<size_t> batchIndices;
                std::vector<size_t> nextPendingIndices;
                batchIndices.reserve(pendingIndices.size());
                nextPendingIndices.reserve(pendingIndices.size());

                for (size_t pendingIndex : pendingIndices)
                {
                    const SceneSystemAccess candidateAccess = getScriptAccess(pendingIndex);
                    bool conflicts = false;
                    for (size_t batchIndex : batchIndices)
                    {
                        if (hasAccessHazard(candidateAccess, getScriptAccess(batchIndex)))
                        {
                            conflicts = true;
                            break;
                        }
                    }

                    if (!conflicts)
                        batchIndices.push_back(pendingIndex);
                    else
                        nextPendingIndices.push_back(pendingIndex);
                }

                if (batchIndices.empty())
                {
                    batchIndices.push_back(pendingIndices.front());
                    nextPendingIndices.erase(std::remove(nextPendingIndices.begin(), nextPendingIndices.end(), pendingIndices.front()), nextPendingIndices.end());
                }

                if (shouldUseParallelJobs && batchIndices.size() >= parallelScriptMinBatchSize)
                {
                    Concurrency::WaitGroup waitGroup;
                    for (size_t batchIndex : batchIndices)
                    {
                        waitGroup.Add(1);
                        jobSystem.Submit([&runParallelSlotAtIndex, &waitGroup, batchIndex]() {
                            runParallelSlotAtIndex(batchIndex);
                            waitGroup.Done();
                        });
                    }
                    waitGroup.Wait();
                }
                else
                {
                    for (size_t batchIndex : batchIndices)
                        runParallelSlotAtIndex(batchIndex);
                }

                pendingIndices = std::move(nextPendingIndices);
            }
        }

        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }
}
