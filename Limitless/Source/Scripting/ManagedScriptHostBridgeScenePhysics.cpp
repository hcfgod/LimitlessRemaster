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

        bool ManagedHasBoxCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedBoxCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetBoxCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetBoxCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetBoxCollider2DSizeIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Size) : ManagedVector2{ 1.0f, 1.0f };
        }

        void ManagedSetBoxCollider2DSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Size = ToGlmVector2(value);
        }

        float ManagedGetBoxCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetBoxCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetBoxCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetBoxCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetBoxCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetBoxCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetBoxCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetBoxCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetBoxCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetBoxCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetBoxCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetBoxCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedBoxCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasCircleCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCircleCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetCircleCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetCircleCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        float ManagedGetCircleCollider2DRadiusIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Radius : 0.5f;
        }

        void ManagedSetCircleCollider2DRadiusIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Radius = value;
        }

        float ManagedGetCircleCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetCircleCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetCircleCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetCircleCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetCircleCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetCircleCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetCircleCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetCircleCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetCircleCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetCircleCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetCircleCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetCircleCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCircleCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasPolygonCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedPolygonCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetPolygonCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetPolygonCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

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

        float ManagedGetPolygonCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetPolygonCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetPolygonCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetPolygonCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetPolygonCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetPolygonCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetPolygonCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetPolygonCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetPolygonCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetPolygonCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetPolygonCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetPolygonCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedPolygonCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasEdgeCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedEdgeCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetEdgeCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetEdgeCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetEdgeCollider2DPointAIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->PointA) : ManagedVector2{ -0.5f, 0.0f };
        }

        void ManagedSetEdgeCollider2DPointAIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->PointA = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetEdgeCollider2DPointBIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->PointB) : ManagedVector2{ 0.5f, 0.0f };
        }

        void ManagedSetEdgeCollider2DPointBIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->PointB = ToGlmVector2(value);
        }

        float ManagedGetEdgeCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetEdgeCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetEdgeCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetEdgeCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetEdgeCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetEdgeCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetEdgeCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetEdgeCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetEdgeCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetEdgeCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedEdgeCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasCapsuleCollider2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedCapsuleCollider2DComponent(entityHandle) != nullptr;
        }

        ManagedVector2 ManagedGetCapsuleCollider2DOffsetIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Offset) : ManagedVector2{};
        }

        void ManagedSetCapsuleCollider2DOffsetIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Offset = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetCapsuleCollider2DSizeIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? ToManagedVector2(collider->Size) : ManagedVector2{ 1.0f, 2.0f };
        }

        void ManagedSetCapsuleCollider2DSizeIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Size = ToGlmVector2(value);
        }

        int ManagedGetCapsuleCollider2DDirectionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? static_cast<int>(collider->Direction) : static_cast<int>(CapsuleCollider2DComponent::Orientation::Vertical);
        }

        void ManagedSetCapsuleCollider2DDirectionIcall(uint32_t entityHandle, int value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Direction = static_cast<CapsuleCollider2DComponent::Orientation>(value);
        }

        float ManagedGetCapsuleCollider2DDensityIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Density : 1.0f;
        }

        void ManagedSetCapsuleCollider2DDensityIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Density = value;
        }

        float ManagedGetCapsuleCollider2DFrictionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Friction : 0.5f;
        }

        void ManagedSetCapsuleCollider2DFrictionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Friction = value;
        }

        float ManagedGetCapsuleCollider2DRestitutionIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->Restitution : 0.0f;
        }

        void ManagedSetCapsuleCollider2DRestitutionIcall(uint32_t entityHandle, float value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->Restitution = value;
        }

        bool ManagedGetCapsuleCollider2DIsSensorIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->IsSensor : false;
        }

        void ManagedSetCapsuleCollider2DIsSensorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->IsSensor = value;
        }

        uint64_t ManagedGetCapsuleCollider2DCollisionLayerIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionLayer : 1ull;
        }

        void ManagedSetCapsuleCollider2DCollisionLayerIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->CollisionLayer = value;
        }

        uint64_t ManagedGetCapsuleCollider2DCollisionMaskIcall(uint32_t entityHandle)
        {
            const auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle);
            return collider ? collider->CollisionMask : ~0ull;
        }

        void ManagedSetCapsuleCollider2DCollisionMaskIcall(uint32_t entityHandle, uint64_t value)
        {
            if (auto* collider = TryGetManagedCapsuleCollider2DComponent(entityHandle))
                collider->CollisionMask = value;
        }

        bool ManagedHasJoint2DComponentIcall(uint32_t entityHandle)
        {
            return TryGetManagedJoint2DComponent(entityHandle) != nullptr;
        }

        int ManagedGetJoint2DTypeIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? static_cast<int>(joint->Type) : static_cast<int>(Joint2DComponent::JointType::Distance);
        }

        void ManagedSetJoint2DTypeIcall(uint32_t entityHandle, int value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Type = static_cast<Joint2DComponent::JointType>(value);
        }

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

        bool ManagedGetJoint2DCollideConnectedIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->CollideConnected : false;
        }

        void ManagedSetJoint2DCollideConnectedIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->CollideConnected = value;
        }

        ManagedVector2 ManagedGetJoint2DAnchorAIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->AnchorA) : ManagedVector2{};
        }

        void ManagedSetJoint2DAnchorAIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->AnchorA = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetJoint2DAnchorBIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->AnchorB) : ManagedVector2{};
        }

        void ManagedSetJoint2DAnchorBIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->AnchorB = ToGlmVector2(value);
        }

        ManagedVector2 ManagedGetJoint2DAxisIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->Axis) : ManagedVector2{ 1.0f, 0.0f };
        }

        void ManagedSetJoint2DAxisIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Axis = ToGlmVector2(value);
        }

        bool ManagedGetJoint2DEnableLimitIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableLimit : false;
        }

        void ManagedSetJoint2DEnableLimitIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableLimit = value;
        }

        ManagedVector2 ManagedGetJoint2DLimitsIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? ToManagedVector2(joint->Limits) : ManagedVector2{ -1.0f, 1.0f };
        }

        void ManagedSetJoint2DLimitsIcall(uint32_t entityHandle, ManagedVector2 value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Limits = ToGlmVector2(value);
        }

        bool ManagedGetJoint2DEnableMotorIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableMotor : false;
        }

        void ManagedSetJoint2DEnableMotorIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableMotor = value;
        }

        float ManagedGetJoint2DMotorSpeedIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->MotorSpeed : 0.0f;
        }

        void ManagedSetJoint2DMotorSpeedIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->MotorSpeed = value;
        }

        float ManagedGetJoint2DMaxMotorForceOrTorqueIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->MaxMotorForceOrTorque : 10.0f;
        }

        void ManagedSetJoint2DMaxMotorForceOrTorqueIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->MaxMotorForceOrTorque = value;
        }

        bool ManagedGetJoint2DEnableSpringIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->EnableSpring : false;
        }

        void ManagedSetJoint2DEnableSpringIcall(uint32_t entityHandle, bool value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->EnableSpring = value;
        }

        float ManagedGetJoint2DHertzIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->Hertz : 5.0f;
        }

        void ManagedSetJoint2DHertzIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->Hertz = value;
        }

        float ManagedGetJoint2DDampingRatioIcall(uint32_t entityHandle)
        {
            const auto* joint = TryGetManagedJoint2DComponent(entityHandle);
            return joint ? joint->DampingRatio : 0.7f;
        }

        void ManagedSetJoint2DDampingRatioIcall(uint32_t entityHandle, float value)
        {
            if (auto* joint = TryGetManagedJoint2DComponent(entityHandle))
                joint->DampingRatio = value;
        }

        void RegisterScenePhysicsInternalCalls(Coral::ManagedAssembly& contractAssembly)
        {
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogInfoIcall", reinterpret_cast<void*>(&ManagedLogInfoIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogWarningIcall", reinterpret_cast<void*>(&ManagedLogWarningIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LogErrorIcall", reinterpret_cast<void*>(&ManagedLogErrorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetRandomSeedIcall", reinterpret_cast<void*>(&ManagedSetRandomSeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomRangeIntIcall", reinterpret_cast<void*>(&ManagedRandomRangeIntIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomRangeFloatIcall", reinterpret_cast<void*>(&ManagedRandomRangeFloatIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "RandomValueIcall", reinterpret_cast<void*>(&ManagedRandomValueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "LoadSceneIcall", reinterpret_cast<void*>(&ManagedLoadSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "ReloadCurrentSceneIcall", reinterpret_cast<void*>(&ManagedReloadCurrentSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetActiveSceneIcall", reinterpret_cast<void*>(&ManagedSetActiveSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "UnloadSceneIcall", reinterpret_cast<void*>(&ManagedUnloadSceneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "CreateEntityIcall", reinterpret_cast<void*>(&ManagedCreateEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "DestroyEntityIcall", reinterpret_cast<void*>(&ManagedDestroyEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "EntityExistsIcall", reinterpret_cast<void*>(&ManagedEntityExistsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEntityEnabledIcall", reinterpret_cast<void*>(&ManagedGetEntityEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEntityEnabledIcall", reinterpret_cast<void*>(&ManagedSetEntityEnabledIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "IsEntityEnabledInHierarchyIcall", reinterpret_cast<void*>(&ManagedIsEntityEnabledInHierarchyIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetParentIcall", reinterpret_cast<void*>(&ManagedSetParentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetParentIcall", reinterpret_cast<void*>(&ManagedGetParentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetChildCountIcall", reinterpret_cast<void*>(&ManagedGetChildCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetChildAtIcall", reinterpret_cast<void*>(&ManagedGetChildAtIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "FindEntityByTagIcall", reinterpret_cast<void*>(&ManagedFindEntityByTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasTagComponentIcall", reinterpret_cast<void*>(&ManagedHasTagComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTagIcall", reinterpret_cast<void*>(&ManagedGetTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTagIcall", reinterpret_cast<void*>(&ManagedSetTagIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasTransformComponentIcall", reinterpret_cast<void*>(&ManagedHasTransformComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformPositionIcall", reinterpret_cast<void*>(&ManagedGetTransformPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformPositionIcall", reinterpret_cast<void*>(&ManagedSetTransformPositionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformRotationIcall", reinterpret_cast<void*>(&ManagedGetTransformRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformRotationIcall", reinterpret_cast<void*>(&ManagedSetTransformRotationIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetTransformScaleIcall", reinterpret_cast<void*>(&ManagedGetTransformScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetTransformScaleIcall", reinterpret_cast<void*>(&ManagedSetTransformScaleIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCameraComponentIcall", reinterpret_cast<void*>(&ManagedHasCameraComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraProjectionIcall", reinterpret_cast<void*>(&ManagedGetCameraProjectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraProjectionIcall", reinterpret_cast<void*>(&ManagedSetCameraProjectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraPrimaryIcall", reinterpret_cast<void*>(&ManagedGetCameraPrimaryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraPrimaryIcall", reinterpret_cast<void*>(&ManagedSetCameraPrimaryIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraZoomIcall", reinterpret_cast<void*>(&ManagedGetCameraZoomIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraZoomIcall", reinterpret_cast<void*>(&ManagedSetCameraZoomIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraNearPlaneIcall", reinterpret_cast<void*>(&ManagedGetCameraNearPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraNearPlaneIcall", reinterpret_cast<void*>(&ManagedSetCameraNearPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraFarPlaneIcall", reinterpret_cast<void*>(&ManagedGetCameraFarPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraFarPlaneIcall", reinterpret_cast<void*>(&ManagedSetCameraFarPlaneIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCameraFieldOfViewYDegreesIcall", reinterpret_cast<void*>(&ManagedGetCameraFieldOfViewYDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCameraFieldOfViewYDegreesIcall", reinterpret_cast<void*>(&ManagedSetCameraFieldOfViewYDegreesIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasBoxCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasBoxCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetBoxCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetBoxCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetBoxCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetBoxCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCircleCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasCircleCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DRadiusIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DRadiusIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DRadiusIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCircleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetCircleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCircleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetCircleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasPolygonCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasPolygonCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DPointCountIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DPointCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DPointCountIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DPointCountIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DPointIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DPointIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DPointIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DPointIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetPolygonCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetPolygonCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetPolygonCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetPolygonCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasEdgeCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasEdgeCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DPointAIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DPointAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DPointAIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DPointAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DPointBIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DPointBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DPointBIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DPointBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetEdgeCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetEdgeCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetEdgeCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetEdgeCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasCapsuleCollider2DComponentIcall", reinterpret_cast<void*>(&ManagedHasCapsuleCollider2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DOffsetIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DOffsetIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DSizeIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DSizeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DDirectionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DDirectionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DDirectionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DDensityIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DDensityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DFrictionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DFrictionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DRestitutionIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DRestitutionIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DIsSensorIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DIsSensorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DCollisionLayerIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DCollisionLayerIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetCapsuleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedGetCapsuleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetCapsuleCollider2DCollisionMaskIcall", reinterpret_cast<void*>(&ManagedSetCapsuleCollider2DCollisionMaskIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "HasJoint2DComponentIcall", reinterpret_cast<void*>(&ManagedHasJoint2DComponentIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DTypeIcall", reinterpret_cast<void*>(&ManagedGetJoint2DTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DTypeIcall", reinterpret_cast<void*>(&ManagedSetJoint2DTypeIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DConnectedEntityIcall", reinterpret_cast<void*>(&ManagedGetJoint2DConnectedEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DConnectedEntityIcall", reinterpret_cast<void*>(&ManagedSetJoint2DConnectedEntityIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DCollideConnectedIcall", reinterpret_cast<void*>(&ManagedGetJoint2DCollideConnectedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DCollideConnectedIcall", reinterpret_cast<void*>(&ManagedSetJoint2DCollideConnectedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAnchorAIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAnchorAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAnchorAIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAnchorAIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAnchorBIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAnchorBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAnchorBIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAnchorBIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DAxisIcall", reinterpret_cast<void*>(&ManagedGetJoint2DAxisIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DAxisIcall", reinterpret_cast<void*>(&ManagedSetJoint2DAxisIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableLimitIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableLimitIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableLimitIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableLimitIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DLimitsIcall", reinterpret_cast<void*>(&ManagedGetJoint2DLimitsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DLimitsIcall", reinterpret_cast<void*>(&ManagedSetJoint2DLimitsIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableMotorIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableMotorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableMotorIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableMotorIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DMotorSpeedIcall", reinterpret_cast<void*>(&ManagedGetJoint2DMotorSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DMotorSpeedIcall", reinterpret_cast<void*>(&ManagedSetJoint2DMotorSpeedIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DMaxMotorForceOrTorqueIcall", reinterpret_cast<void*>(&ManagedGetJoint2DMaxMotorForceOrTorqueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DMaxMotorForceOrTorqueIcall", reinterpret_cast<void*>(&ManagedSetJoint2DMaxMotorForceOrTorqueIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DEnableSpringIcall", reinterpret_cast<void*>(&ManagedGetJoint2DEnableSpringIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DEnableSpringIcall", reinterpret_cast<void*>(&ManagedSetJoint2DEnableSpringIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DHertzIcall", reinterpret_cast<void*>(&ManagedGetJoint2DHertzIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DHertzIcall", reinterpret_cast<void*>(&ManagedSetJoint2DHertzIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "GetJoint2DDampingRatioIcall", reinterpret_cast<void*>(&ManagedGetJoint2DDampingRatioIcall));
            contractAssembly.AddInternalCall(kScriptBridgeTypeName, "SetJoint2DDampingRatioIcall", reinterpret_cast<void*>(&ManagedSetJoint2DDampingRatioIcall));
        }
    }
}
