#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/ScriptingComponents.h"

#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/ScriptableEntity.h"

#include <algorithm>
#include <exception>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace Limitless
{
    namespace
    {
        constexpr uint16_t kMinPhysicsWorldCount = 1;
        constexpr uint16_t kMaxPhysicsWorldCount = 16;

        uint16_t SanitizeWorldCount(uint16_t requestedCount)
        {
            return static_cast<uint16_t>(std::clamp<int>(requestedCount, kMinPhysicsWorldCount, kMaxPhysicsWorldCount));
        }

        uint16_t ClampWorldSlot(uint16_t requestedSlot, uint16_t worldCount)
        {
            if (worldCount == 0)
                return 0;
            return std::min<uint16_t>(requestedSlot, static_cast<uint16_t>(worldCount - 1));
        }

        RuntimeContactPairKey MakeContactPairKey(entt::entity entityA, entt::entity entityB, uint16_t worldSlot, bool isSensor)
        {
            const uint32_t encodedA = static_cast<uint32_t>(entityA);
            const uint32_t encodedB = static_cast<uint32_t>(entityB);
            RuntimeContactPairKey key{};
            key.EntityA = std::min(encodedA, encodedB);
            key.EntityB = std::max(encodedA, encodedB);
            key.WorldSlot = worldSlot;
            key.IsSensor = isSensor;
            return key;
        }

        bool RuntimeContactPairSortLess(const RuntimeContactPairKey& left, const RuntimeContactPairKey& right)
        {
            return std::tie(left.WorldSlot, left.IsSensor, left.EntityA, left.EntityB) <
                   std::tie(right.WorldSlot, right.IsSensor, right.EntityA, right.EntityB);
        }

        void DispatchScriptContactCallbackForEntity(Scene& scene,
                                                    entt::entity selfEntity,
                                                    entt::entity otherEntity,
                                                    bool isSensor,
                                                    bool dispatchEnter,
                                                    bool dispatchStay,
                                                    bool dispatchExit)
        {
            if (!scene.IsValid(selfEntity) || !scene.IsValid(otherEntity))
                return;
            if (!scene.IsEntityEnabledInHierarchy(selfEntity))
                return;

            auto& registry = scene.GetRegistry();
            const Entity other(&registry, otherEntity);
            const auto scriptEntities = scene.GetScriptComponentEntities(selfEntity);
            for (entt::entity scriptEntity : scriptEntities)
            {
                auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntity);
                if (!scriptComponent)
                    continue;

                const auto* tag = registry.try_get<TagComponent>(selfEntity);
                if (NativeScriptEntry* scriptEntry = scriptComponent->TryGetNativeEntry())
                {
                    if (!scriptEntry->Enabled || !scriptEntry->RuntimeInstance || !scriptEntry->RuntimeInitialized)
                        continue;

                    auto invokeNativeCallback = [&](bool shouldDispatch, auto&& callback, const char* callbackName) {
                        if (!shouldDispatch)
                            return;

                        try
                        {
                            callback(*scriptEntry->RuntimeInstance);
                        }
                        catch (const std::exception& exception)
                        {
                            LT_WARN("Script '{}' on entity '{}' threw during {} callback: {}",
                                    scriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    callbackName,
                                    exception.what());
                            return;
                        }
                        catch (...)
                        {
                            LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during {} callback",
                                    scriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    callbackName);
                            return;
                        }
                    };

                    if (isSensor)
                    {
                        invokeNativeCallback(dispatchEnter,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchTriggerEnter(other); },
                                             "OnTriggerEnter");
                        invokeNativeCallback(dispatchStay,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchTriggerStay(other); },
                                             "OnTriggerStay");
                        invokeNativeCallback(dispatchExit,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchTriggerExit(other); },
                                             "OnTriggerExit");
                    }
                    else
                    {
                        invokeNativeCallback(dispatchEnter,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchCollisionEnter(other); },
                                             "OnCollisionEnter");
                        invokeNativeCallback(dispatchStay,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchCollisionStay(other); },
                                             "OnCollisionStay");
                        invokeNativeCallback(dispatchExit,
                                             [&](ScriptableEntity& runtimeInstance) { runtimeInstance.DispatchCollisionExit(other); },
                                             "OnCollisionExit");
                    }
                }
                else if (ManagedScriptEntry* scriptEntry = scriptComponent->TryGetManagedEntry())
                {
                    if (!scriptEntry->Enabled || scriptEntry->RuntimeInstanceId == 0 || !scriptEntry->RuntimeInitialized)
                        continue;

                    auto invokeManagedCallback = [&](bool shouldDispatch, auto&& callback, const char* callbackName) {
                        if (!shouldDispatch)
                            return;

                        std::string managedError;
                        if (!callback(scriptEntry->RuntimeInstanceId, &scene, static_cast<uint32_t>(otherEntity), &managedError))
                        {
                            LT_WARN("Managed script '{}' on entity '{}' failed during {} callback: {}",
                                    scriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    callbackName,
                                    managedError.empty() ? "unknown error" : managedError.c_str());
                            return;
                        }

                        ScriptComponent* mutableScriptComponent = scene.GetScriptComponent(scriptEntity);
                        ManagedScriptEntry* mutableScriptEntry = mutableScriptComponent ? mutableScriptComponent->TryGetManagedEntry() : nullptr;
                        if (!mutableScriptEntry || mutableScriptEntry->RuntimeInstanceId == 0)
                            return;

                        managedError.clear();
                        if (!ManagedScriptHost::ReadBackScriptExposedProperties(mutableScriptEntry->RuntimeInstanceId,
                                                                                &scene,
                                                                                mutableScriptEntry->ExposedProperties,
                                                                                &mutableScriptEntry->RuntimeExposedPropertiesRevision,
                                                                                &managedError))
                        {
                            LT_WARN("Managed script '{}' on entity '{}' failed during exposed property readback after {} callback: {}",
                                    mutableScriptEntry->ScriptClassName,
                                    tag ? tag->Tag : "Entity",
                                    callbackName,
                                    managedError.empty() ? "unknown error" : managedError.c_str());
                        }
                    };

                    if (isSensor)
                    {
                        invokeManagedCallback(dispatchEnter, ManagedScriptHost::InvokeScriptOnTriggerEnter, "OnTriggerEnter");
                        invokeManagedCallback(dispatchStay, ManagedScriptHost::InvokeScriptOnTriggerStay, "OnTriggerStay");
                        invokeManagedCallback(dispatchExit, ManagedScriptHost::InvokeScriptOnTriggerExit, "OnTriggerExit");
                    }
                    else
                    {
                        invokeManagedCallback(dispatchEnter, ManagedScriptHost::InvokeScriptOnCollisionEnter, "OnCollisionEnter");
                        invokeManagedCallback(dispatchStay, ManagedScriptHost::InvokeScriptOnCollisionStay, "OnCollisionStay");
                        invokeManagedCallback(dispatchExit, ManagedScriptHost::InvokeScriptOnCollisionExit, "OnCollisionExit");
                    }
                }
            }
        }
    }

    void Scene::BeginLoadingState()
    {
        m_LoadState = LoadState::Loading;
        m_SceneObjectsInitialized = false;
        m_PhysicsWorldInitializedForLoading = false;
        m_RuntimeActiveContactPairs.clear();
    }

    void Scene::MarkSceneObjectsInitialized()
    {
        m_SceneObjectsInitialized = true;
    }

    bool Scene::InitializePhysicsWorldForLoading()
    {
        if (m_PhysicsWorldInitializedForLoading)
            return true;

        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (!physicsWorld)
                continue;
            if (!physicsWorld->IsInitialized())
                physicsWorld->Initialize(m_Physics2DSettings);
            physicsWorld->SetSettings(m_Physics2DSettings);
            physicsWorld->RebuildScene(*this);
        }
        m_PhysicsWorldInitializedForLoading = true;
        return true;
    }

    void Scene::SetLoadStateReady()
    {
        m_LoadState = LoadState::Ready;
        m_SceneObjectsInitialized = true;
        m_PhysicsWorldInitializedForLoading = true;
    }

    void Scene::StepPhysics2D(float fixedDeltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        SetRuntimePhase(RuntimePhase::Simulation);
        std::vector<Physics2DWorld*> activeWorlds;
        activeWorlds.reserve(m_Physics2DWorlds.size());
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (!physicsWorld)
                continue;
            if (!physicsWorld->IsInitialized())
                physicsWorld->Initialize(m_Physics2DSettings);
            physicsWorld->SetSettings(m_Physics2DSettings);
            physicsWorld->PrepareForStep(*this, fixedDeltaTime);
            activeWorlds.push_back(physicsWorld.get());
        }

        const bool enableParallelPhysicsWorldStep =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_physics_world_step", true);
        auto& jobSystem = Concurrency::GetJobSystem();
        if (enableParallelPhysicsWorldStep && jobSystem.IsInitialized() && activeWorlds.size() > 1)
        {
            jobSystem.ParallelFor(0, activeWorlds.size(), 1, [&activeWorlds, fixedDeltaTime](size_t worldIndex) {
                activeWorlds[worldIndex]->StepWorldOnly(fixedDeltaTime);
            });
        }
        else
        {
            for (Physics2DWorld* physicsWorld : activeWorlds)
                physicsWorld->StepWorldOnly(fixedDeltaTime);
        }

        for (Physics2DWorld* physicsWorld : activeWorlds)
        {
            physicsWorld->SyncAfterStep(*this);
        }

        // Dispatch Unity-style collision/trigger callbacks for runtime scripts.
        // This keeps contact state persistent across steps for Enter/Stay/Exit semantics.
        if (!m_RuntimeActiveContactPairs.empty())
        {
            for (auto it = m_RuntimeActiveContactPairs.begin(); it != m_RuntimeActiveContactPairs.end();)
            {
                const entt::entity entityA = static_cast<entt::entity>(it->EntityA);
                const entt::entity entityB = static_cast<entt::entity>(it->EntityB);
                if (!IsValid(entityA) || !IsValid(entityB))
                {
                    it = m_RuntimeActiveContactPairs.erase(it);
                    continue;
                }
                ++it;
            }
        }

        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> beginPairs;
        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> endPairs;
        for (Physics2DWorld* physicsWorld : activeWorlds)
        {
            if (!physicsWorld)
                continue;

            const uint16_t worldSlot = physicsWorld->GetSceneWorldSlot();
            const auto& events = physicsWorld->GetContactListener().GetEvents();
            for (const auto& eventData : events)
            {
                if (eventData.EntityA == entt::null || eventData.EntityB == entt::null)
                    continue;
                const RuntimeContactPairKey key = MakeContactPairKey(eventData.EntityA, eventData.EntityB, worldSlot, eventData.IsSensor);
                if (eventData.IsBegin)
                    beginPairs.insert(key);
                else
                    endPairs.insert(key);
            }
        }

        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> changedPairs = beginPairs;
        changedPairs.insert(endPairs.begin(), endPairs.end());
        std::vector<RuntimeContactPairKey> orderedChangedPairs(changedPairs.begin(), changedPairs.end());
        std::sort(orderedChangedPairs.begin(), orderedChangedPairs.end(), RuntimeContactPairSortLess);

        [[maybe_unused]] auto forcedDeferredDestroyScope = MakeForcedDeferredEntityDestructionScope();
        for (const RuntimeContactPairKey& key : orderedChangedPairs)
        {
            const bool hasBegin = beginPairs.find(key) != beginPairs.end();
            const bool hasEnd = endPairs.find(key) != endPairs.end();
            const bool wasActive = m_RuntimeActiveContactPairs.find(key) != m_RuntimeActiveContactPairs.end();

            const entt::entity entityA = static_cast<entt::entity>(key.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(key.EntityB);

            if (!wasActive)
            {
                if (hasBegin && !hasEnd)
                {
                    m_RuntimeActiveContactPairs.insert(key);
                    DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, true, false, false);
                    DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, true, false, false);
                }
                continue;
            }

            if (hasEnd && !hasBegin)
            {
                DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, false, true);
                DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, false, true);
                m_RuntimeActiveContactPairs.erase(key);
            }
        }

        std::vector<RuntimeContactPairKey> orderedActivePairs(m_RuntimeActiveContactPairs.begin(), m_RuntimeActiveContactPairs.end());
        std::sort(orderedActivePairs.begin(), orderedActivePairs.end(), RuntimeContactPairSortLess);
        for (const RuntimeContactPairKey& key : orderedActivePairs)
        {
            const entt::entity entityA = static_cast<entt::entity>(key.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(key.EntityB);
            DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, true, false);
            DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, true, false);
        }
        FlushDeferredStructuralMutations();

        m_PhysicsWorldInitializedForLoading = true;
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }

    void Scene::SetPhysics2DSettings(const Physics2DWorldSettings& settings)
    {
        m_Physics2DSettings = settings;
        m_Physics2DSettings.WorldCount = SanitizeWorldCount(m_Physics2DSettings.WorldCount);
        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->SetSettings(m_Physics2DSettings);
        }
    }

    Physics2DWorld* Scene::GetPhysics2DWorld(uint16_t worldSlot)
    {
        if (m_Physics2DWorlds.empty())
            return nullptr;
        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        return m_Physics2DWorlds[clampedSlot].get();
    }

    const Physics2DWorld* Scene::GetPhysics2DWorld(uint16_t worldSlot) const
    {
        if (m_Physics2DWorlds.empty())
            return nullptr;
        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        return m_Physics2DWorlds[clampedSlot].get();
    }

    uint16_t Scene::GetPhysics2DWorldCount() const
    {
        if (!m_Physics2DWorlds.empty())
            return static_cast<uint16_t>(m_Physics2DWorlds.size());
        return SanitizeWorldCount(m_Physics2DSettings.WorldCount);
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEvents() const
    {
        const Physics2DWorld* world = GetPhysics2DWorld(0);
        if (!world)
            return nullptr;
        return &world->GetContactListener();
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEventsForEntity(entt::entity entity) const
    {
        if (entity == entt::null)
            return GetPhysics2DContactEvents();

        const auto& registry = GetRegistry();
        const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
        if (!rigidbody)
            return GetPhysics2DContactEvents();

        const uint16_t worldCount = GetPhysics2DWorldCount();
        const uint16_t worldSlot = ClampWorldSlot(rigidbody->PhysicsWorldSlot, worldCount);
        const Physics2DWorld* world = GetPhysics2DWorld(worldSlot);
        if (!world)
            return nullptr;
        return &world->GetContactListener();
    }

    bool Scene::HasActivePhysics2DContact(entt::entity entity, entt::entity otherEntity, bool includeSensorContacts) const
    {
        if (!IsValid(entity) || !IsValid(otherEntity) || entity == otherEntity)
            return false;

        const uint32_t encodedEntity = static_cast<uint32_t>(entity);
        const uint32_t encodedOtherEntity = static_cast<uint32_t>(otherEntity);
        const uint32_t keyEntityA = std::min(encodedEntity, encodedOtherEntity);
        const uint32_t keyEntityB = std::max(encodedEntity, encodedOtherEntity);
        for (const RuntimeContactPairKey& key : m_RuntimeActiveContactPairs)
        {
            if (!includeSensorContacts && key.IsSensor)
                continue;
            if (key.EntityA == keyEntityA && key.EntityB == keyEntityB)
                return true;
        }

        return false;
    }

    int Scene::GetActivePhysics2DContactCount(entt::entity entity, bool includeSensorContacts) const
    {
        return static_cast<int>(GetActivePhysics2DContactEntityHandles(entity, includeSensorContacts).size());
    }

    std::vector<entt::entity> Scene::GetActivePhysics2DContactEntityHandles(entt::entity entity, bool includeSensorContacts) const
    {
        std::vector<entt::entity> contactsOut;
        if (!IsValid(entity))
            return contactsOut;

        std::unordered_set<entt::entity> uniqueContacts;
        for (const RuntimeContactPairKey& key : m_RuntimeActiveContactPairs)
        {
            if (!includeSensorContacts && key.IsSensor)
                continue;

            entt::entity other = entt::null;
            if (key.EntityA == static_cast<uint32_t>(entity))
                other = static_cast<entt::entity>(key.EntityB);
            else if (key.EntityB == static_cast<uint32_t>(entity))
                other = static_cast<entt::entity>(key.EntityA);

            if (other == entt::null || !IsValid(other))
                continue;
            if (uniqueContacts.insert(other).second)
                contactsOut.push_back(other);
        }

        std::sort(contactsOut.begin(), contactsOut.end(), [](entt::entity left, entt::entity right) {
            return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
        });
        return contactsOut;
    }

    bool Scene::TryGetPhysics2DBodyDiagnostics(entt::entity entity, Physics2DBodyDiagnostics& outDiagnostics) const
    {
        outDiagnostics = Physics2DBodyDiagnostics{};
        const auto& registry = GetRegistry();
        if (const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity))
        {
            const uint16_t worldCount = GetPhysics2DWorldCount();
            const uint16_t worldSlot = ClampWorldSlot(rigidbody->PhysicsWorldSlot, worldCount);
            if (const Physics2DWorld* world = GetPhysics2DWorld(worldSlot))
                return world->TryGetBodyDiagnostics(entity, outDiagnostics);
            return false;
        }

        for (const auto& world : m_Physics2DWorlds)
        {
            if (world && world->TryGetBodyDiagnostics(entity, outDiagnostics))
                return true;
        }
        return false;
    }

    Physics2DRaycastHit Scene::RaycastClosestAcrossPhysicsWorlds(const glm::vec2& origin,
                                                                 const glm::vec2& direction,
                                                                 float maxDistance,
                                                                 uint64_t collisionMask) const
    {
        Physics2DRaycastHit bestHit{};
        float bestFraction = 1.0f;
        for (const auto& world : m_Physics2DWorlds)
        {
            if (!world)
                continue;
            const Physics2DRaycastHit hit = world->RaycastClosest(origin, direction, maxDistance, collisionMask);
            if (!hit.HasHit)
                continue;
            if (!bestHit.HasHit || hit.Fraction < bestFraction)
            {
                bestHit = hit;
                bestFraction = hit.Fraction;
            }
        }
        return bestHit;
    }

    void Scene::ResetPhysicsRuntimeState()
    {
        m_RuntimeActiveContactPairs.clear();
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->Shutdown(*this);
        }
    }

    void Scene::ResetPhysicsRuntimeState(uint16_t worldSlot)
    {
        if (m_Physics2DWorlds.empty())
            return;

        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        for (auto it = m_RuntimeActiveContactPairs.begin(); it != m_RuntimeActiveContactPairs.end();)
        {
            if (it->WorldSlot == clampedSlot)
                it = m_RuntimeActiveContactPairs.erase(it);
            else
                ++it;
        }

        auto& physicsWorld = m_Physics2DWorlds[clampedSlot];
        if (physicsWorld)
            physicsWorld->Shutdown(*this);
    }

    void Scene::EnsurePhysics2DWorldCount(uint16_t worldCount)
    {
        const uint16_t sanitizedWorldCount = SanitizeWorldCount(worldCount);
        m_Physics2DSettings.WorldCount = sanitizedWorldCount;

        const size_t existingCount = m_Physics2DWorlds.size();
        if (existingCount > sanitizedWorldCount)
        {
            for (size_t worldIndex = sanitizedWorldCount; worldIndex < existingCount; ++worldIndex)
            {
                if (m_Physics2DWorlds[worldIndex])
                    m_Physics2DWorlds[worldIndex]->Shutdown(*this);
            }
            m_Physics2DWorlds.resize(sanitizedWorldCount);
        }
        else if (existingCount < sanitizedWorldCount)
        {
            m_Physics2DWorlds.resize(sanitizedWorldCount);
        }

        for (uint16_t worldIndex = 0; worldIndex < sanitizedWorldCount; ++worldIndex)
        {
            if (!m_Physics2DWorlds[worldIndex])
                m_Physics2DWorlds[worldIndex] = std::make_unique<Physics2DWorld>(worldIndex);
        }

        auto bodyView = m_Registry.view<Rigidbody2DComponent>();
        const uint16_t maxSlot = static_cast<uint16_t>(sanitizedWorldCount - 1);
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            rigidbody.PhysicsWorldSlot = std::min<uint16_t>(rigidbody.PhysicsWorldSlot, maxSlot);
        }
    }
}
