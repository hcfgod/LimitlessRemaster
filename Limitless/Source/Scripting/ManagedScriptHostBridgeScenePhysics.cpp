#include "Scripting/ManagedScriptHostInternal.h"

#include "Audio/AudioEngine.h"
#include "Core/Debug/Log.h"
#include "Scene/SceneManager.h"
#include "Scripting/Random.h"

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        void ManagedLogInfoIcall(Coral::String message)
        {
            LT_INFO("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedLogWarningIcall(Coral::String message)
        {
            LT_WARN("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedLogErrorIcall(Coral::String message)
        {
            LT_ERROR("{}: {}", BuildManagedLogPrefix(), ToUtf8Borrowed(message));
        }

        void ManagedSetRandomSeedIcall(uint32_t seed)
        {
            Random::SetSeed(seed);
        }

        int32_t ManagedRandomRangeIntIcall(int32_t minInclusive, int32_t maxExclusive)
        {
            return Random::Range(minInclusive, maxExclusive);
        }

        float ManagedRandomRangeFloatIcall(float minInclusive, float maxInclusive)
        {
            return Random::Range(minInclusive, maxInclusive);
        }

        float ManagedRandomValueIcall()
        {
            return Random::Value();
        }

        bool ManagedLoadSceneIcall(Coral::String sceneIdentifier, int loadMode)
        {
            return SceneManager::LoadScene(ToUtf8Borrowed(sceneIdentifier), static_cast<LoadSceneMode>(loadMode));
        }

        bool ManagedReloadCurrentSceneIcall()
        {
            return SceneManager::ReloadCurrentScene();
        }

        bool ManagedSetActiveSceneIcall(Coral::String sceneIdentifier)
        {
            return SceneManager::SetActiveScene(ToUtf8Borrowed(sceneIdentifier));
        }

        bool ManagedUnloadSceneIcall(Coral::String sceneIdentifier)
        {
            return SceneManager::UnloadScene(ToUtf8Borrowed(sceneIdentifier));
        }

        uint32_t ManagedCreateEntityIcall(Coral::String name)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            return static_cast<uint32_t>(s_HostState.ActiveScene->CreateEntity(ToUtf8Borrowed(name)));
        }

        bool ManagedDestroyEntityIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return false;

            s_HostState.ActiveScene->DestroyEntity(entity);
            return true;
        }

        bool ManagedEntityExistsIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return entity != entt::null && s_HostState.ActiveScene->IsValid(entity);
        }

        bool ManagedGetEntityEnabledIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            const auto* tagComponent = registry->try_get<TagComponent>(entity);
            return tagComponent != nullptr && tagComponent->Enabled;
        }

        bool ManagedSetEntityEnabledIcall(uint32_t entityHandle, bool enabled)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return false;

            tagComponent->Enabled = enabled;
            return true;
        }

        bool ManagedIsEntityEnabledInHierarchyIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return entity != entt::null && s_HostState.ActiveScene->IsEntityEnabledInHierarchy(entity);
        }

        bool ManagedSetParentIcall(uint32_t childEntityHandle, uint32_t parentEntityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return false;

            const entt::entity childEntity = ResolveManagedEntityHandle(childEntityHandle);
            entt::entity parentEntity = entt::null;
            if (parentEntityHandle != static_cast<uint32_t>(entt::null))
                parentEntity = ResolveManagedEntityHandle(parentEntityHandle);
            if (childEntity == entt::null)
                return false;
            if (parentEntityHandle != static_cast<uint32_t>(entt::null) && parentEntity == entt::null)
                return false;

            return s_HostState.ActiveScene->SetParent(childEntity, parentEntity);
        }

        uint32_t ManagedGetParentIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return static_cast<uint32_t>(entt::null);

            const entt::entity parentEntity = s_HostState.ActiveScene->GetParent(entity);
            if (parentEntity == entt::null)
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(parentEntity));
        }

        uint32_t ManagedGetChildCountIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return 0;

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return 0;

            return static_cast<uint32_t>(s_HostState.ActiveScene->GetChildren(entity).size());
        }

        uint32_t ManagedGetChildAtIcall(uint32_t entityHandle, uint32_t index)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (entity == entt::null)
                return static_cast<uint32_t>(entt::null);

            const auto children = s_HostState.ActiveScene->GetChildren(entity);
            if (index >= children.size())
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(children[index]));
        }

        uint32_t ManagedFindEntityByTagIcall(Coral::String tag)
        {
            entt::registry* registry = GetActiveRegistry();
            if (registry == nullptr)
                return static_cast<uint32_t>(entt::null);

            const std::string requestedTag = ToUtf8Borrowed(tag);
            if (requestedTag.empty())
                return static_cast<uint32_t>(entt::null);

            auto view = registry->view<TagComponent>();
            for (entt::entity entity : view)
            {
                const auto& tagComponent = view.get<TagComponent>(entity);
                if (tagComponent.Tag == requestedTag)
                    return static_cast<uint32_t>(entity);
            }

            return static_cast<uint32_t>(entt::null);
        }

        bool ManagedHasTagComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<TagComponent>(entity);
        }

        Coral::String ManagedGetTagIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return Coral::String::New("");

            const auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return Coral::String::New("");
            return Coral::String::New(tagComponent->Tag);
        }

        bool ManagedSetTagIcall(uint32_t entityHandle, Coral::String tag)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            auto* tagComponent = registry->try_get<TagComponent>(entity);
            if (tagComponent == nullptr)
                return false;

            tagComponent->Tag = ToUtf8Borrowed(tag);
            return true;
        }

        bool ManagedHasTransformComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<TransformComponent>(entity);
        }

        ManagedVector3 ManagedGetTransformPositionIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Position) : ManagedVector3{};
        }

        ManagedVector3 ManagedGetTransformRotationIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Rotation) : ManagedVector3{};
        }

        ManagedVector3 ManagedGetTransformScaleIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return {};

            const auto* transformComponent = registry->try_get<TransformComponent>(entity);
            return transformComponent ? ToManagedVector3(transformComponent->Scale) : ManagedVector3{};
        }

        void ManagedSetTransformPositionIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Position = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        void ManagedSetTransformRotationIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Rotation = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        void ManagedSetTransformScaleIcall(uint32_t entityHandle, ManagedVector3 value)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null || s_HostState.ActiveScene == nullptr)
                return;

            auto* transformComponent = registry->try_get<TransformComponent>(entity);
            if (transformComponent == nullptr)
                return;

            transformComponent->Scale = ToGlmVector3(value);
            s_HostState.ActiveScene->MarkTransformDirty(entity);
        }

        bool ManagedHasCameraComponentIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            return registry != nullptr && entity != entt::null && registry->all_of<CameraComponent>(entity);
        }

        int ManagedGetCameraProjectionIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return static_cast<int>(CameraComponent::ProjectionType::Orthographic2D);

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? static_cast<int>(cameraComponent->Projection) : static_cast<int>(CameraComponent::ProjectionType::Orthographic2D);
        }

        void ManagedSetCameraProjectionIcall(uint32_t entityHandle, int projection)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->Projection = static_cast<CameraComponent::ProjectionType>(projection);
        }

        bool ManagedGetCameraPrimaryIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return false;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent != nullptr && cameraComponent->IsPrimary;
        }

        void ManagedSetCameraPrimaryIcall(uint32_t entityHandle, bool primary)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->IsPrimary = primary;
        }

        float ManagedGetCameraZoomIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->Zoom : 1.0f;
        }

        void ManagedSetCameraZoomIcall(uint32_t entityHandle, float zoom)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->Zoom = zoom;
        }

        float ManagedGetCameraNearPlaneIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return -1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->NearPlane : -1.0f;
        }

        void ManagedSetCameraNearPlaneIcall(uint32_t entityHandle, float nearPlane)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->NearPlane = nearPlane;
        }

        float ManagedGetCameraFarPlaneIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 1.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->FarPlane : 1.0f;
        }

        void ManagedSetCameraFarPlaneIcall(uint32_t entityHandle, float farPlane)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->FarPlane = farPlane;
        }

        float ManagedGetCameraFieldOfViewYDegreesIcall(uint32_t entityHandle)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return 60.0f;

            const auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            return cameraComponent ? cameraComponent->FieldOfViewYDegrees : 60.0f;
        }

        void ManagedSetCameraFieldOfViewYDegreesIcall(uint32_t entityHandle, float fieldOfViewYDegrees)
        {
            entt::registry* registry = GetActiveRegistry();
            const entt::entity entity = ResolveManagedEntityHandle(entityHandle);
            if (registry == nullptr || entity == entt::null)
                return;

            auto* cameraComponent = registry->try_get<CameraComponent>(entity);
            if (cameraComponent == nullptr)
                return;

            cameraComponent->FieldOfViewYDegrees = fieldOfViewYDegrees;
        }

        LT_MANAGED_COMPONENT_HAS(HasBoxCollider2DComponentIcall, TryGetManagedBoxCollider2DComponent);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DOffsetIcall, ManagedVector2, TryGetManagedBoxCollider2DComponent, ToManagedVector2(component->Offset), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DOffsetIcall, ManagedVector2, TryGetManagedBoxCollider2DComponent, component->Offset = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DSizeIcall, ManagedVector2, TryGetManagedBoxCollider2DComponent, ToManagedVector2(component->Size), ManagedVector2{ 1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DSizeIcall, ManagedVector2, TryGetManagedBoxCollider2DComponent, component->Size = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DDensityIcall, float, TryGetManagedBoxCollider2DComponent, component->Density, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DDensityIcall, float, TryGetManagedBoxCollider2DComponent, component->Density = value;);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DFrictionIcall, float, TryGetManagedBoxCollider2DComponent, component->Friction, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DFrictionIcall, float, TryGetManagedBoxCollider2DComponent, component->Friction = value;);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DRestitutionIcall, float, TryGetManagedBoxCollider2DComponent, component->Restitution, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DRestitutionIcall, float, TryGetManagedBoxCollider2DComponent, component->Restitution = value;);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DIsSensorIcall, bool, TryGetManagedBoxCollider2DComponent, component->IsSensor, false);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DIsSensorIcall, bool, TryGetManagedBoxCollider2DComponent, component->IsSensor = value;);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DCollisionLayerIcall, uint64_t, TryGetManagedBoxCollider2DComponent, component->CollisionLayer, 1ull);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DCollisionLayerIcall, uint64_t, TryGetManagedBoxCollider2DComponent, component->CollisionLayer = value;);
        LT_MANAGED_COMPONENT_GET(GetBoxCollider2DCollisionMaskIcall, uint64_t, TryGetManagedBoxCollider2DComponent, component->CollisionMask, ~0ull);
        LT_MANAGED_COMPONENT_SET(SetBoxCollider2DCollisionMaskIcall, uint64_t, TryGetManagedBoxCollider2DComponent, component->CollisionMask = value;);

        LT_MANAGED_COMPONENT_HAS(HasCircleCollider2DComponentIcall, TryGetManagedCircleCollider2DComponent);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DOffsetIcall, ManagedVector2, TryGetManagedCircleCollider2DComponent, ToManagedVector2(component->Offset), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DOffsetIcall, ManagedVector2, TryGetManagedCircleCollider2DComponent, component->Offset = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DRadiusIcall, float, TryGetManagedCircleCollider2DComponent, component->Radius, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DRadiusIcall, float, TryGetManagedCircleCollider2DComponent, component->Radius = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DDensityIcall, float, TryGetManagedCircleCollider2DComponent, component->Density, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DDensityIcall, float, TryGetManagedCircleCollider2DComponent, component->Density = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DFrictionIcall, float, TryGetManagedCircleCollider2DComponent, component->Friction, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DFrictionIcall, float, TryGetManagedCircleCollider2DComponent, component->Friction = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DRestitutionIcall, float, TryGetManagedCircleCollider2DComponent, component->Restitution, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DRestitutionIcall, float, TryGetManagedCircleCollider2DComponent, component->Restitution = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DIsSensorIcall, bool, TryGetManagedCircleCollider2DComponent, component->IsSensor, false);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DIsSensorIcall, bool, TryGetManagedCircleCollider2DComponent, component->IsSensor = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DCollisionLayerIcall, uint64_t, TryGetManagedCircleCollider2DComponent, component->CollisionLayer, 1ull);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DCollisionLayerIcall, uint64_t, TryGetManagedCircleCollider2DComponent, component->CollisionLayer = value;);
        LT_MANAGED_COMPONENT_GET(GetCircleCollider2DCollisionMaskIcall, uint64_t, TryGetManagedCircleCollider2DComponent, component->CollisionMask, ~0ull);
        LT_MANAGED_COMPONENT_SET(SetCircleCollider2DCollisionMaskIcall, uint64_t, TryGetManagedCircleCollider2DComponent, component->CollisionMask = value;);

        LT_MANAGED_COMPONENT_HAS(HasPolygonCollider2DComponentIcall, TryGetManagedPolygonCollider2DComponent);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DOffsetIcall, ManagedVector2, TryGetManagedPolygonCollider2DComponent, ToManagedVector2(component->Offset), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DOffsetIcall, ManagedVector2, TryGetManagedPolygonCollider2DComponent, component->Offset = ToGlmVector2(value););

        int ManagedGetPolygonCollider2DPointCountIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? static_cast<int>(collider->Points.size()) : 0;
        }

        void ManagedSetPolygonCollider2DPointCountIcall(uint32_t entityHandle, int pointCount)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
            {
                const size_t clampedPointCount = static_cast<size_t>(std::clamp(pointCount, 0, static_cast<int>(kPhysics2DPolygonMaxPoints)));
                collider->Points.resize(clampedPointCount, glm::vec2(0.0f));
            }
        }

        ManagedVector2 ManagedGetPolygonCollider2DPointIcall(uint32_t entityHandle, int index)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            if (collider == nullptr || index < 0 || static_cast<size_t>(index) >= collider->Points.size())
                return ManagedVector2{};
            return ToManagedVector2(collider->Points[static_cast<size_t>(index)]);
        }

        void ManagedSetPolygonCollider2DPointIcall(uint32_t entityHandle, int index, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
            {
                if (index < 0 || static_cast<size_t>(index) >= collider->Points.size())
                    return;
                collider->Points[static_cast<size_t>(index)] = ToGlmVector2(value);
            }
        }
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DDensityIcall, float, TryGetManagedPolygonCollider2DComponent, component->Density, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DDensityIcall, float, TryGetManagedPolygonCollider2DComponent, component->Density = value;);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DFrictionIcall, float, TryGetManagedPolygonCollider2DComponent, component->Friction, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DFrictionIcall, float, TryGetManagedPolygonCollider2DComponent, component->Friction = value;);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DRestitutionIcall, float, TryGetManagedPolygonCollider2DComponent, component->Restitution, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DRestitutionIcall, float, TryGetManagedPolygonCollider2DComponent, component->Restitution = value;);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DIsSensorIcall, bool, TryGetManagedPolygonCollider2DComponent, component->IsSensor, false);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DIsSensorIcall, bool, TryGetManagedPolygonCollider2DComponent, component->IsSensor = value;);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DCollisionLayerIcall, uint64_t, TryGetManagedPolygonCollider2DComponent, component->CollisionLayer, 1ull);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DCollisionLayerIcall, uint64_t, TryGetManagedPolygonCollider2DComponent, component->CollisionLayer = value;);
        LT_MANAGED_COMPONENT_GET(GetPolygonCollider2DCollisionMaskIcall, uint64_t, TryGetManagedPolygonCollider2DComponent, component->CollisionMask, ~0ull);
        LT_MANAGED_COMPONENT_SET(SetPolygonCollider2DCollisionMaskIcall, uint64_t, TryGetManagedPolygonCollider2DComponent, component->CollisionMask = value;);

        LT_MANAGED_COMPONENT_HAS(HasEdgeCollider2DComponentIcall, TryGetManagedEdgeCollider2DComponent);
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DOffsetIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, ToManagedVector2(component->Offset), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DOffsetIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, component->Offset = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DPointAIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, ToManagedVector2(component->PointA), ManagedVector2{ -0.5f, 0.0f });
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DPointAIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, component->PointA = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DPointBIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, ToManagedVector2(component->PointB), ManagedVector2{ 0.5f, 0.0f });
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DPointBIcall, ManagedVector2, TryGetManagedEdgeCollider2DComponent, component->PointB = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DFrictionIcall, float, TryGetManagedEdgeCollider2DComponent, component->Friction, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DFrictionIcall, float, TryGetManagedEdgeCollider2DComponent, component->Friction = value;);
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DRestitutionIcall, float, TryGetManagedEdgeCollider2DComponent, component->Restitution, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DRestitutionIcall, float, TryGetManagedEdgeCollider2DComponent, component->Restitution = value;);
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DIsSensorIcall, bool, TryGetManagedEdgeCollider2DComponent, component->IsSensor, false);
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DIsSensorIcall, bool, TryGetManagedEdgeCollider2DComponent, component->IsSensor = value;);
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DCollisionLayerIcall, uint64_t, TryGetManagedEdgeCollider2DComponent, component->CollisionLayer, 1ull);
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DCollisionLayerIcall, uint64_t, TryGetManagedEdgeCollider2DComponent, component->CollisionLayer = value;);
        LT_MANAGED_COMPONENT_GET(GetEdgeCollider2DCollisionMaskIcall, uint64_t, TryGetManagedEdgeCollider2DComponent, component->CollisionMask, ~0ull);
        LT_MANAGED_COMPONENT_SET(SetEdgeCollider2DCollisionMaskIcall, uint64_t, TryGetManagedEdgeCollider2DComponent, component->CollisionMask = value;);

        LT_MANAGED_COMPONENT_HAS(HasCapsuleCollider2DComponentIcall, TryGetManagedCapsuleCollider2DComponent);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DOffsetIcall, ManagedVector2, TryGetManagedCapsuleCollider2DComponent, ToManagedVector2(component->Offset), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DOffsetIcall, ManagedVector2, TryGetManagedCapsuleCollider2DComponent, component->Offset = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DSizeIcall, ManagedVector2, TryGetManagedCapsuleCollider2DComponent, ToManagedVector2(component->Size), ManagedVector2{ 1.0f, 2.0f });
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DSizeIcall, ManagedVector2, TryGetManagedCapsuleCollider2DComponent, component->Size = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DDirectionIcall, int, TryGetManagedCapsuleCollider2DComponent, static_cast<int>(component->Direction), static_cast<int>(CapsuleCollider2DComponent::Orientation::Vertical));
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DDirectionIcall, int, TryGetManagedCapsuleCollider2DComponent, component->Direction = static_cast<CapsuleCollider2DComponent::Orientation>(value););
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DDensityIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Density, 1.0f);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DDensityIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Density = value;);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DFrictionIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Friction, 0.5f);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DFrictionIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Friction = value;);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DRestitutionIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Restitution, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DRestitutionIcall, float, TryGetManagedCapsuleCollider2DComponent, component->Restitution = value;);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DIsSensorIcall, bool, TryGetManagedCapsuleCollider2DComponent, component->IsSensor, false);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DIsSensorIcall, bool, TryGetManagedCapsuleCollider2DComponent, component->IsSensor = value;);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DCollisionLayerIcall, uint64_t, TryGetManagedCapsuleCollider2DComponent, component->CollisionLayer, 1ull);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DCollisionLayerIcall, uint64_t, TryGetManagedCapsuleCollider2DComponent, component->CollisionLayer = value;);
        LT_MANAGED_COMPONENT_GET(GetCapsuleCollider2DCollisionMaskIcall, uint64_t, TryGetManagedCapsuleCollider2DComponent, component->CollisionMask, ~0ull);
        LT_MANAGED_COMPONENT_SET(SetCapsuleCollider2DCollisionMaskIcall, uint64_t, TryGetManagedCapsuleCollider2DComponent, component->CollisionMask = value;);

        LT_MANAGED_COMPONENT_HAS(HasJoint2DComponentIcall, TryGetManagedJoint2DComponent);
        LT_MANAGED_COMPONENT_GET(GetJoint2DTypeIcall, int, TryGetManagedJoint2DComponent, static_cast<int>(component->Type), static_cast<int>(Joint2DComponent::JointType::Distance));
        LT_MANAGED_COMPONENT_SET(SetJoint2DTypeIcall, int, TryGetManagedJoint2DComponent, component->Type = static_cast<Joint2DComponent::JointType>(value););

        uint32_t ManagedGetJoint2DConnectedEntityIcall(uint32_t entityHandle)
        {
            if (s_HostState.ActiveScene == nullptr)
                return static_cast<uint32_t>(entt::null);

            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            if (joint == nullptr || joint->ConnectedEntity == entt::null)
                return static_cast<uint32_t>(entt::null);
            return static_cast<uint32_t>(s_HostState.ActiveScene->ResolveEntityReference(joint->ConnectedEntity));
        }

        void ManagedSetJoint2DConnectedEntityIcall(uint32_t entityHandle, uint32_t connectedEntityHandle)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
            {
                if (connectedEntityHandle == static_cast<uint32_t>(entt::null))
                {
                    joint->ConnectedEntity = entt::null;
                    return;
                }

                joint->ConnectedEntity = ResolveManagedEntityHandle(connectedEntityHandle);
            }
        }

        LT_MANAGED_COMPONENT_GET(GetJoint2DCollideConnectedIcall, bool, TryGetManagedJoint2DComponent, component->CollideConnected, false);
        LT_MANAGED_COMPONENT_SET(SetJoint2DCollideConnectedIcall, bool, TryGetManagedJoint2DComponent, component->CollideConnected = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DAnchorAIcall, ManagedVector2, TryGetManagedJoint2DComponent, ToManagedVector2(component->AnchorA), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetJoint2DAnchorAIcall, ManagedVector2, TryGetManagedJoint2DComponent, component->AnchorA = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetJoint2DAnchorBIcall, ManagedVector2, TryGetManagedJoint2DComponent, ToManagedVector2(component->AnchorB), ManagedVector2{});
        LT_MANAGED_COMPONENT_SET(SetJoint2DAnchorBIcall, ManagedVector2, TryGetManagedJoint2DComponent, component->AnchorB = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetJoint2DAxisIcall, ManagedVector2, TryGetManagedJoint2DComponent, ToManagedVector2(component->Axis), ManagedVector2{ 1.0f, 0.0f });
        LT_MANAGED_COMPONENT_SET(SetJoint2DAxisIcall, ManagedVector2, TryGetManagedJoint2DComponent, component->Axis = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetJoint2DEnableLimitIcall, bool, TryGetManagedJoint2DComponent, component->EnableLimit, false);
        LT_MANAGED_COMPONENT_SET(SetJoint2DEnableLimitIcall, bool, TryGetManagedJoint2DComponent, component->EnableLimit = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DLimitsIcall, ManagedVector2, TryGetManagedJoint2DComponent, ToManagedVector2(component->Limits), ManagedVector2{ -1.0f, 1.0f });
        LT_MANAGED_COMPONENT_SET(SetJoint2DLimitsIcall, ManagedVector2, TryGetManagedJoint2DComponent, component->Limits = ToGlmVector2(value););
        LT_MANAGED_COMPONENT_GET(GetJoint2DEnableMotorIcall, bool, TryGetManagedJoint2DComponent, component->EnableMotor, false);
        LT_MANAGED_COMPONENT_SET(SetJoint2DEnableMotorIcall, bool, TryGetManagedJoint2DComponent, component->EnableMotor = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DMotorSpeedIcall, float, TryGetManagedJoint2DComponent, component->MotorSpeed, 0.0f);
        LT_MANAGED_COMPONENT_SET(SetJoint2DMotorSpeedIcall, float, TryGetManagedJoint2DComponent, component->MotorSpeed = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DMaxMotorForceOrTorqueIcall, float, TryGetManagedJoint2DComponent, component->MaxMotorForceOrTorque, 10.0f);
        LT_MANAGED_COMPONENT_SET(SetJoint2DMaxMotorForceOrTorqueIcall, float, TryGetManagedJoint2DComponent, component->MaxMotorForceOrTorque = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DEnableSpringIcall, bool, TryGetManagedJoint2DComponent, component->EnableSpring, false);
        LT_MANAGED_COMPONENT_SET(SetJoint2DEnableSpringIcall, bool, TryGetManagedJoint2DComponent, component->EnableSpring = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DHertzIcall, float, TryGetManagedJoint2DComponent, component->Hertz, 5.0f);
        LT_MANAGED_COMPONENT_SET(SetJoint2DHertzIcall, float, TryGetManagedJoint2DComponent, component->Hertz = value;);
        LT_MANAGED_COMPONENT_GET(GetJoint2DDampingRatioIcall, float, TryGetManagedJoint2DComponent, component->DampingRatio, 0.7f);
        LT_MANAGED_COMPONENT_SET(SetJoint2DDampingRatioIcall, float, TryGetManagedJoint2DComponent, component->DampingRatio = value;);

        void RegisterScenePhysicsInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            RegisterInternalCallBatch(contractAssembly, {
                LT_MANAGED_INTERNAL_CALL(LogInfoIcall),
                LT_MANAGED_INTERNAL_CALL(LogWarningIcall),
                LT_MANAGED_INTERNAL_CALL(LogErrorIcall),
                LT_MANAGED_INTERNAL_CALL(SetRandomSeedIcall),
                LT_MANAGED_INTERNAL_CALL(RandomRangeIntIcall),
                LT_MANAGED_INTERNAL_CALL(RandomRangeFloatIcall),
                LT_MANAGED_INTERNAL_CALL(RandomValueIcall),
                LT_MANAGED_INTERNAL_CALL(LoadSceneIcall),
                LT_MANAGED_INTERNAL_CALL(ReloadCurrentSceneIcall),
                LT_MANAGED_INTERNAL_CALL(SetActiveSceneIcall),
                LT_MANAGED_INTERNAL_CALL(UnloadSceneIcall),
                LT_MANAGED_INTERNAL_CALL(CreateEntityIcall),
                LT_MANAGED_INTERNAL_CALL(DestroyEntityIcall),
                LT_MANAGED_INTERNAL_CALL(EntityExistsIcall),
                LT_MANAGED_INTERNAL_CALL(GetEntityEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(SetEntityEnabledIcall),
                LT_MANAGED_INTERNAL_CALL(IsEntityEnabledInHierarchyIcall),
                LT_MANAGED_INTERNAL_CALL(SetParentIcall),
                LT_MANAGED_INTERNAL_CALL(GetParentIcall),
                LT_MANAGED_INTERNAL_CALL(GetChildCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetChildAtIcall),
                LT_MANAGED_INTERNAL_CALL(FindEntityByTagIcall),
                LT_MANAGED_INTERNAL_CALL(HasTagComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetTagIcall),
                LT_MANAGED_INTERNAL_CALL(SetTagIcall),
                LT_MANAGED_INTERNAL_CALL(HasTransformComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetTransformPositionIcall),
                LT_MANAGED_INTERNAL_CALL(SetTransformPositionIcall),
                LT_MANAGED_INTERNAL_CALL(GetTransformRotationIcall),
                LT_MANAGED_INTERNAL_CALL(SetTransformRotationIcall),
                LT_MANAGED_INTERNAL_CALL(GetTransformScaleIcall),
                LT_MANAGED_INTERNAL_CALL(SetTransformScaleIcall),
                LT_MANAGED_INTERNAL_CALL(HasCameraComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraProjectionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraProjectionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraPrimaryIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraPrimaryIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraZoomIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraZoomIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraNearPlaneIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraNearPlaneIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraFarPlaneIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraFarPlaneIcall),
                LT_MANAGED_INTERNAL_CALL(GetCameraFieldOfViewYDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(SetCameraFieldOfViewYDegreesIcall),
                LT_MANAGED_INTERNAL_CALL(HasBoxCollider2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DSizeIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DSizeIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(GetBoxCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(SetBoxCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(HasCircleCollider2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DRadiusIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DRadiusIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(GetCircleCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(SetCircleCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(HasPolygonCollider2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DPointCountIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DPointCountIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DPointIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DPointIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(GetPolygonCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(SetPolygonCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(HasEdgeCollider2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DPointAIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DPointAIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DPointBIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DPointBIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(GetEdgeCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(SetEdgeCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(HasCapsuleCollider2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DOffsetIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DSizeIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DSizeIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DDirectionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DDirectionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DDensityIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DFrictionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DRestitutionIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DIsSensorIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DCollisionLayerIcall),
                LT_MANAGED_INTERNAL_CALL(GetCapsuleCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(SetCapsuleCollider2DCollisionMaskIcall),
                LT_MANAGED_INTERNAL_CALL(HasJoint2DComponentIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DTypeIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DTypeIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DConnectedEntityIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DConnectedEntityIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DCollideConnectedIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DCollideConnectedIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DAnchorAIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DAnchorAIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DAnchorBIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DAnchorBIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DAxisIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DAxisIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DEnableLimitIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DEnableLimitIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DLimitsIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DLimitsIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DEnableMotorIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DEnableMotorIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DMotorSpeedIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DMotorSpeedIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DMaxMotorForceOrTorqueIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DMaxMotorForceOrTorqueIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DEnableSpringIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DEnableSpringIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DHertzIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DHertzIcall),
                LT_MANAGED_INTERNAL_CALL(GetJoint2DDampingRatioIcall),
                LT_MANAGED_INTERNAL_CALL(SetJoint2DDampingRatioIcall)
            });
        }
    }
}
