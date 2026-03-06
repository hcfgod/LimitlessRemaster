#pragma once

#include "Core/Error.h"
#include "EnTT/entt.hpp"
#include "Scene/Entity.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/CoroutineTypes.h"
#include "Scripting/ScriptEvent.h"
#include "Scripting/ScriptProperty.h"

#include <functional>
#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Limitless
{
    struct ScriptAccess
    {
        static constexpr uint64_t None = 0ull;

        static constexpr uint64_t Transform = ToAccessMask(SceneSystemAccessComponent::Transform);
        static constexpr uint64_t Hierarchy = ToAccessMask(SceneSystemAccessComponent::Hierarchy);
        static constexpr uint64_t Rigidbody2D = ToAccessMask(SceneSystemAccessComponent::Rigidbody2D);
        static constexpr uint64_t BoxCollider2D = ToAccessMask(SceneSystemAccessComponent::BoxCollider2D);
        static constexpr uint64_t CircleCollider2D = ToAccessMask(SceneSystemAccessComponent::CircleCollider2D);
        static constexpr uint64_t Joint2D = ToAccessMask(SceneSystemAccessComponent::Joint2D);
        static constexpr uint64_t Animator = ToAccessMask(SceneSystemAccessComponent::Animator);
        static constexpr uint64_t ParticleEmitter = ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
        static constexpr uint64_t NativeScript = ToAccessMask(SceneSystemAccessComponent::NativeScript);

        static constexpr uint64_t Rendering2D = ToAccessMask(SceneSystemAccessComponent::Rendering2D);
        static constexpr uint64_t Lighting2D = ToAccessMask(SceneSystemAccessComponent::Lighting2D);
        static constexpr uint64_t UI = ToAccessMask(SceneSystemAccessComponent::UI);
        static constexpr uint64_t Audio = ToAccessMask(SceneSystemAccessComponent::Audio);
        static constexpr uint64_t Camera = ToAccessMask(SceneSystemAccessComponent::Camera);
        static constexpr uint64_t Tilemap = ToAccessMask(SceneSystemAccessComponent::Tilemap);
        static constexpr uint64_t Metadata = ToAccessMask(SceneSystemAccessComponent::Metadata);

        template<typename... Masks>
        static constexpr uint64_t Combine(Masks... masks)
        {
            return (0ull | ... | static_cast<uint64_t>(masks));
        }
    };

    class Scene;
    class Coroutine;
    class ScriptableEntity;
    struct AnimatorComponent;
    using ScriptCreateEntityBridgeCallback = entt::entity (*)(const char* name);
    using ScriptDestroyEntityBridgeCallback = void (*)(entt::entity entity);
    using ScriptInstantiatePrefabBridgeCallback = entt::entity (*)(const char* prefabAssetKey, entt::entity parentEntity);
    using ScriptResolveEntityReferenceBridgeCallback = entt::entity (*)(entt::entity entity);
    using ScriptParallelExecutionBridgeCallback = bool (*)();
    using ScriptGetContactEntityHandlesBridgeCallback =
        uint32_t (*)(entt::entity entity, bool includeSensorContacts, entt::entity* outHandles, uint32_t capacity);

    // Base type for native C++ entity scripts.
    // Derive from this, register in NativeScriptRegistry, then assign in NativeScriptComponent.
    class ScriptableEntity
    {
        friend class Scene;
        friend class Coroutine;

    public:
        virtual ~ScriptableEntity() = default;

        ScriptableEntity() = default;
        ScriptableEntity(const ScriptableEntity&) = delete;
        ScriptableEntity& operator=(const ScriptableEntity&) = delete;

        entt::entity GetEntityHandle() const
        {
            return m_EntityHandle;
        }

        Scene* GetScene() const
        {
            return m_Scene;
        }

        /// Returns the engine Entity wrapper for this script's own entity.
        Entity GetEntity()
        {
            return Entity(m_Registry, m_EntityHandle);
        }

        Entity GetEntity() const
        {
            return Entity(m_Registry, m_EntityHandle);
        }

        template<typename ComponentType>
        bool HasComponent() const
        {
            return HasComponent<ComponentType>(m_EntityHandle);
        }

        template<typename ComponentType>
        bool HasComponent(entt::entity entity) const
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            if (!m_Registry->valid(entity))
                return false;
            return m_Registry->all_of<ComponentType>(entity);
        }

        template<typename ComponentType>
        ComponentType& GetComponent()
        {
            return GetComponent<ComponentType>(m_EntityHandle);
        }

        template<typename ComponentType>
        ComponentType& GetComponent(entt::entity entity)
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            if (!m_Registry->valid(entity))
                throw std::runtime_error("ScriptableEntity referenced invalid entity");
            if (!m_Registry->all_of<ComponentType>(entity))
                throw std::runtime_error("ScriptableEntity missing requested component");
            ComponentType& component = m_Registry->get<ComponentType>(entity);
            if constexpr (detail::HasTransformDirtyFields<ComponentType>::value)
                component.Dirty = true;
            return component;
        }

        template<typename ComponentType, typename... ConstructorArgs>
        ComponentType& AddComponent(ConstructorArgs&&... args)
        {
            return AddComponent<ComponentType>(m_EntityHandle, std::forward<ConstructorArgs>(args)...);
        }

        template<typename ComponentType, typename... ConstructorArgs>
        ComponentType& AddComponent(entt::entity entity, ConstructorArgs&&... args)
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            if (!m_Registry->valid(entity))
                throw std::runtime_error("ScriptableEntity referenced invalid entity");
            if (IsParallelScriptExecutionContext() && !m_Registry->all_of<ComponentType>(entity))
                throw std::runtime_error("ScriptableEntity::AddComponent is not supported during parallel script execution");
            if (m_Registry->all_of<ComponentType>(entity))
                return m_Registry->get<ComponentType>(entity);
            return m_Registry->emplace<ComponentType>(entity, std::forward<ConstructorArgs>(args)...);
        }

        template<typename ComponentType>
        void RemoveComponent()
        {
            RemoveComponent<ComponentType>(m_EntityHandle);
        }

        template<typename ComponentType>
        void RemoveComponent(entt::entity entity)
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            if (!m_Registry->valid(entity))
                return;
            if (IsParallelScriptExecutionContext() && m_Registry->all_of<ComponentType>(entity))
                throw std::runtime_error("ScriptableEntity::RemoveComponent is not supported during parallel script execution");
            if (m_Registry->all_of<ComponentType>(entity))
                m_Registry->remove<ComponentType>(entity);
        }

        template<typename ScriptType>
        ScriptType* GetScript()
        {
            return GetScript<ScriptType>(m_EntityHandle);
        }

        template<typename ScriptType>
        ScriptType* GetScript(const Entity& entity)
        {
            return GetScript<ScriptType>(entity.GetHandle());
        }

        template<typename ScriptType>
        ScriptType* GetScript(entt::entity entity)
        {
            static_assert(std::is_base_of_v<ScriptableEntity, ScriptType>, "ScriptType must derive from ScriptableEntity");
            const std::vector<ScriptableEntity*> scripts = GetRuntimeScripts(entity);
            for (ScriptableEntity* script : scripts)
            {
                if (auto* typedScript = dynamic_cast<ScriptType*>(script))
                    return typedScript;
            }
            return nullptr;
        }

        ScriptableEntity* GetScript(const std::string& className);
        ScriptableEntity* GetScript(const Entity& entity, const std::string& className);
        ScriptableEntity* GetScript(entt::entity entity, const std::string& className);
        std::vector<ScriptableEntity*> GetScripts();
        std::vector<ScriptableEntity*> GetScripts(const Entity& entity);
        std::vector<ScriptableEntity*> GetScripts(entt::entity entity);

        Entity CreateEntity(const std::string& name = "Entity");
        entt::entity CreateEntityHandle(const std::string& name = "Entity");
        Entity Instantiate(const std::string& prefabAssetKey, entt::entity parentEntity = entt::null);
        Entity Instantiate(const std::string& prefabAssetKey, const Entity& parentEntity);
        Entity Instantiate(const Prefab& prefabReference, entt::entity parentEntity = entt::null);
        Entity Instantiate(const Prefab& prefabReference, const Entity& parentEntity);
        Entity Instantiate(const Entity& prefabReference, entt::entity parentEntity = entt::null);
        Entity Instantiate(const Entity& prefabReference, const Entity& parentEntity);
        void DestroyEntity(Entity entity);
        void DestroyEntity(entt::entity entity);
        bool IsEntityValid(entt::entity entity) const;
        Entity GetEntity(entt::entity entity) const;
        Entity ResolveEntity(const Entity& entity) const;
        Entity FindEntityByTag(const std::string& tag) const;
        Entity GetParent(const Entity& entity) const;
        Entity GetParent(entt::entity entity) const;
        std::vector<Entity> GetChildren(const Entity& parent) const;
        std::vector<Entity> GetChildren(entt::entity parent) const;
        std::vector<Entity> GetHierarchy(const Entity& root, bool includeRoot = true) const;
        std::vector<Entity> GetHierarchy(entt::entity root, bool includeRoot = true) const;

        static void SetCreateEntityBridgeCallback(ScriptCreateEntityBridgeCallback callback);
        static void SetDestroyEntityBridgeCallback(ScriptDestroyEntityBridgeCallback callback);
        static void SetInstantiatePrefabBridgeCallback(ScriptInstantiatePrefabBridgeCallback callback);
        static void SetResolveEntityReferenceBridgeCallback(ScriptResolveEntityReferenceBridgeCallback callback);
        static void SetParallelScriptExecutionBridgeCallback(ScriptParallelExecutionBridgeCallback callback);
        static void SetContactEntityHandlesBridgeCallback(ScriptGetContactEntityHandlesBridgeCallback callback);

        // Runtime dispatch wrappers used by scene physics integration.
        // Keep callback virtuals protected for script authors.
        void DispatchCollisionEnter(const Entity& other) { OnCollisionEnter(other); }
        void DispatchCollisionStay(const Entity& other) { OnCollisionStay(other); }
        void DispatchCollisionExit(const Entity& other) { OnCollisionExit(other); }
        void DispatchTriggerEnter(const Entity& other) { OnTriggerEnter(other); }
        void DispatchTriggerStay(const Entity& other) { OnTriggerStay(other); }
        void DispatchTriggerExit(const Entity& other) { OnTriggerExit(other); }
        void DispatchUIButtonClicked(const Entity& buttonEntity) { OnUIButtonClicked(buttonEntity); }

        /// Unsubscribe all ScriptEvent subscriptions made via Subscribe().
        /// Called automatically by the runtime before OnDestroy.
        void UnsubscribeAllScriptEvents()
        {
            for (auto& unsub : m_EventUnsubscribeActions)
            {
                if (unsub) unsub();
            }
            m_EventUnsubscribeActions.clear();
        }

        // Optional class-level declaration for parallel compatibility scheduling.
        // If a NativeScriptEntry has no authored masks, runtime will use these defaults.
        virtual uint64_t GetDeclaredReadAccessMask() const { return ScriptAccess::None; }
        virtual uint64_t GetDeclaredWriteAccessMask() const { return ScriptAccess::None; }

    protected:
        float GetExposedFloat(const std::string& name, float fallbackValue = 0.0f) const;
        int32_t GetExposedInteger(const std::string& name, int32_t fallbackValue = 0) const;
        bool GetExposedBoolean(const std::string& name, bool fallbackValue = false) const;
        glm::vec3 GetExposedVector3(const std::string& name, const glm::vec3& fallbackValue = glm::vec3(0.0f)) const;
        std::string GetExposedString(const std::string& name, const std::string& fallbackValue = {}) const;
        Entity GetExposedEntity(const std::string& name, const Entity& fallbackValue = Entity{}) const;
        Prefab GetExposedPrefab(const std::string& name, const Prefab& fallbackValue = {}) const;

        void SetExposedFloat(const std::string& name, float value);
        void SetExposedInteger(const std::string& name, int32_t value);
        void SetExposedBoolean(const std::string& name, bool value);
        void SetExposedVector3(const std::string& name, const glm::vec3& value);
        void SetExposedString(const std::string& name, const std::string& value);
        void SetExposedEntity(const std::string& name, const Entity& value);
        void SetExposedPrefab(const std::string& name, const Prefab& value);

        void SyncExposedField(const std::string& name, float& value) { if (HasPendingExposedPropertySync()) value = GetExposedFloat(name, value); }
        void SyncExposedField(const std::string& name, int32_t& value) { if (HasPendingExposedPropertySync()) value = GetExposedInteger(name, value); }
        void SyncExposedField(const std::string& name, bool& value) { if (HasPendingExposedPropertySync()) value = GetExposedBoolean(name, value); }
        void SyncExposedField(const std::string& name, glm::vec3& value) { if (HasPendingExposedPropertySync()) value = GetExposedVector3(name, value); }
        void SyncExposedField(const std::string& name, std::string& value) { if (HasPendingExposedPropertySync()) value = GetExposedString(name, value); }
        void SyncExposedField(const std::string& name, Entity& value) { if (HasPendingExposedPropertySync()) value = GetExposedEntity(name, value); }
        void SyncExposedField(const std::string& name, Prefab& value) { if (HasPendingExposedPropertySync()) value = GetExposedPrefab(name, value); }

        void WriteBackExposedField(const std::string& name, float value) { SetExposedFloat(name, value); }
        void WriteBackExposedField(const std::string& name, int32_t value) { SetExposedInteger(name, value); }
        void WriteBackExposedField(const std::string& name, bool value) { SetExposedBoolean(name, value); }
        void WriteBackExposedField(const std::string& name, const glm::vec3& value) { SetExposedVector3(name, value); }
        void WriteBackExposedField(const std::string& name, const std::string& value) { SetExposedString(name, value); }
        void WriteBackExposedField(const std::string& name, const Entity& value) { SetExposedEntity(name, value); }
        void WriteBackExposedField(const std::string& name, const Prefab& value) { SetExposedPrefab(name, value); }

        bool Raycast2D(const glm::vec2& origin,
                       const glm::vec2& direction,
                       float maxDistance,
                       entt::entity& outEntity,
                       glm::vec2& outPoint,
                       glm::vec2& outNormal,
                       float& outFraction,
                       uint64_t collisionMask = ~0ull) const;

        bool HasContactWith(entt::entity otherEntity, bool includeSensorContacts = true) const;
        int GetContactCount(bool includeSensorContacts = true) const;
        std::vector<entt::entity> GetContactEntityHandles(bool includeSensorContacts = true) const;
        std::vector<entt::entity> GetContactEntityHandles(entt::entity entity, bool includeSensorContacts = true) const;
        std::vector<Entity> GetContactEntities(bool includeSensorContacts = true) const;
        std::vector<Entity> GetContactEntities(entt::entity entity, bool includeSensorContacts = true) const;

        // Animator 2D runtime controls.
        bool HasAnimator() const;
        bool PlayAnimatorState(const std::string& stateName, bool restartIfSameState = true);
        bool PlayAnimatorClip(const std::string& clipKey, bool restartIfSameClip = true);
        bool SetAnimatorBool(const std::string& parameterName, bool value);
        bool GetAnimatorBool(const std::string& parameterName, bool fallback = false) const;
        bool SetAnimatorFloat(const std::string& parameterName, float value);
        float GetAnimatorFloat(const std::string& parameterName, float fallback = 0.0f) const;
        bool SetAnimatorInteger(const std::string& parameterName, int32_t value);
        int32_t GetAnimatorInteger(const std::string& parameterName, int32_t fallback = 0) const;
        bool SetAnimatorTrigger(const std::string& parameterName);
        bool ResetAnimatorTrigger(const std::string& parameterName);
        std::string GetAnimatorCurrentStateName() const;
        float GetAnimatorStateTimeSeconds() const;

        // Particle emitter controls.
        void PlayParticles();
        void StopParticles(bool clearParticles = true);
        void PauseParticles();
        void ResumeParticles();
        void EmitParticles(uint32_t count);
        void SetSpawnRate(float rate);
        void SetParticleColor(const glm::vec4& startColor, const glm::vec4& endColor);
        bool IsEmitterPlaying() const;
        uint32_t GetAliveParticleCount() const;

        virtual void OnSynchronizeExposedFields() {}
        virtual void OnWriteBackExposedFields() {}

        virtual void OnCreate() {}
        virtual void OnFixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnCollisionEnter(const Entity& other) { (void)other; }
        virtual void OnCollisionStay(const Entity& other) { (void)other; }
        virtual void OnCollisionExit(const Entity& other) { (void)other; }
        virtual void OnTriggerEnter(const Entity& other) { (void)other; }
        virtual void OnTriggerStay(const Entity& other) { (void)other; }
        virtual void OnTriggerExit(const Entity& other) { (void)other; }
        virtual void OnUIButtonClicked(const Entity& buttonEntity) { (void)buttonEntity; }
        virtual void OnDestroy() {}

        /// Subscribe to a ScriptEvent with automatic unsubscription when this script is destroyed.
        /// Usage:  Subscribe(button.OnClicked, [this]() { HandleClick(); });
        template<typename... Args>
        void Subscribe(ScriptEvent<Args...>& event, typename ScriptEvent<Args...>::CallbackType callback)
        {
            auto token = event += std::move(callback);
            m_EventUnsubscribeActions.push_back([&event, token]() { event -= token; });
        }

    private:
        bool HasPendingExposedPropertySync() const
        {
            return m_ExposedPropertiesRevision != nullptr &&
                   m_LastSynchronizedExposedPropertiesRevision != *m_ExposedPropertiesRevision;
        }

        void MarkExposedPropertySyncComplete()
        {
            if (m_ExposedPropertiesRevision != nullptr)
                m_LastSynchronizedExposedPropertiesRevision = *m_ExposedPropertiesRevision;
        }

        struct CoroutineState final
        {
            CoroutineHandle Handle{};
            CoroutineRoutine Routine{};
            bool WaitingForSeconds = false;
            bool WaitingForFrames = false;
            float RemainingWaitSeconds = 0.0f;
            uint32_t RemainingWaitFrames = 0;
            bool SkipWaitTickThisFrame = false;
        };

        Scene* m_Scene = nullptr;
        entt::registry* m_Registry = nullptr;
        entt::entity m_EntityHandle = entt::null;
        std::unordered_map<std::string, ScriptPropertyValue>* m_ExposedProperties = nullptr;
        uint64_t* m_ExposedPropertiesRevision = nullptr;
        uint64_t m_LastSynchronizedExposedPropertiesRevision = 0;
        std::vector<CoroutineState> m_ActiveCoroutines;
        std::vector<CoroutineState> m_PendingCoroutineStarts;
        std::vector<CoroutineHandle> m_PendingCoroutineStops;
        std::vector<std::function<void()>> m_EventUnsubscribeActions;
        uint64_t m_NextCoroutineIdentifier = 1;
        bool m_IsAdvancingCoroutines = false;

        static bool IsParallelScriptExecutionContext();
        std::vector<ScriptableEntity*> GetRuntimeScripts(entt::entity entity) const;
    };
}

#define LT_SYNC_EXPOSED_FIELD(FieldName) SyncExposedField(#FieldName, FieldName);
#define LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC() \
    void OnSynchronizeExposedFields() override \
    {
#define LT_AUTO_EXPOSED_FIELD(FieldName) \
        LT_SYNC_EXPOSED_FIELD(FieldName);
#define LT_END_AUTO_EXPOSED_FIELD_SYNC() \
    }
#define LT_WRITEBACK_EXPOSED_FIELD(FieldName) WriteBackExposedField(#FieldName, FieldName);

#define LT_SCRIPT_ACCESS_MASK(...) ::Limitless::ScriptAccess::Combine(__VA_ARGS__)
#define LT_DECLARE_SCRIPT_ACCESS(ReadMaskExpr, WriteMaskExpr) \
    uint64_t GetDeclaredReadAccessMask() const override { return static_cast<uint64_t>(ReadMaskExpr); } \
    uint64_t GetDeclaredWriteAccessMask() const override { return static_cast<uint64_t>(WriteMaskExpr); }

// ---------------------------------------------------------------------------
// LT_EXPOSED_FIELDS(Field1, Field2, ...) — Unity-style bidirectional sync.
// Generates both OnSynchronizeExposedFields (pull map → fields) and
// OnWriteBackExposedFields (push fields → map) so that scripts can read and
// write member variables directly instead of calling Get/SetExposed*.
// Supports up to 32 fields.
// ---------------------------------------------------------------------------
#define LT_PP_CAT_IMPL_(a, b) a ## b
#define LT_PP_CAT_(a, b) LT_PP_CAT_IMPL_(a, b)
#define LT_PP_EXPAND_(...) __VA_ARGS__

#define LT_PP_FE_1(M, x)       M(x)
#define LT_PP_FE_2(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_1(M, __VA_ARGS__))
#define LT_PP_FE_3(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_2(M, __VA_ARGS__))
#define LT_PP_FE_4(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_3(M, __VA_ARGS__))
#define LT_PP_FE_5(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_4(M, __VA_ARGS__))
#define LT_PP_FE_6(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_5(M, __VA_ARGS__))
#define LT_PP_FE_7(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_6(M, __VA_ARGS__))
#define LT_PP_FE_8(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_7(M, __VA_ARGS__))
#define LT_PP_FE_9(M, x, ...)  M(x) LT_PP_EXPAND_(LT_PP_FE_8(M, __VA_ARGS__))
#define LT_PP_FE_10(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_9(M, __VA_ARGS__))
#define LT_PP_FE_11(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_10(M, __VA_ARGS__))
#define LT_PP_FE_12(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_11(M, __VA_ARGS__))
#define LT_PP_FE_13(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_12(M, __VA_ARGS__))
#define LT_PP_FE_14(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_13(M, __VA_ARGS__))
#define LT_PP_FE_15(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_14(M, __VA_ARGS__))
#define LT_PP_FE_16(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_15(M, __VA_ARGS__))
#define LT_PP_FE_17(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_16(M, __VA_ARGS__))
#define LT_PP_FE_18(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_17(M, __VA_ARGS__))
#define LT_PP_FE_19(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_18(M, __VA_ARGS__))
#define LT_PP_FE_20(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_19(M, __VA_ARGS__))
#define LT_PP_FE_21(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_20(M, __VA_ARGS__))
#define LT_PP_FE_22(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_21(M, __VA_ARGS__))
#define LT_PP_FE_23(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_22(M, __VA_ARGS__))
#define LT_PP_FE_24(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_23(M, __VA_ARGS__))
#define LT_PP_FE_25(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_24(M, __VA_ARGS__))
#define LT_PP_FE_26(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_25(M, __VA_ARGS__))
#define LT_PP_FE_27(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_26(M, __VA_ARGS__))
#define LT_PP_FE_28(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_27(M, __VA_ARGS__))
#define LT_PP_FE_29(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_28(M, __VA_ARGS__))
#define LT_PP_FE_30(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_29(M, __VA_ARGS__))
#define LT_PP_FE_31(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_30(M, __VA_ARGS__))
#define LT_PP_FE_32(M, x, ...) M(x) LT_PP_EXPAND_(LT_PP_FE_31(M, __VA_ARGS__))

#define LT_PP_ARG_N_( \
    _1,_2,_3,_4,_5,_6,_7,_8, \
    _9,_10,_11,_12,_13,_14,_15,_16, \
    _17,_18,_19,_20,_21,_22,_23,_24, \
    _25,_26,_27,_28,_29,_30,_31,_32, N, ...) N

#define LT_PP_NARG_(...) \
    LT_PP_EXPAND_(LT_PP_ARG_N_(__VA_ARGS__, \
    32,31,30,29,28,27,26,25,24,23,22,21,20,19,18,17, \
    16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1))

#define LT_PP_FOR_EACH_N_(N, M, ...) LT_PP_EXPAND_(LT_PP_CAT_(LT_PP_FE_, N)(M, __VA_ARGS__))
#define LT_PP_FOR_EACH_(M, ...) LT_PP_FOR_EACH_N_(LT_PP_NARG_(__VA_ARGS__), M, __VA_ARGS__)

#define LT_SYNC_FIELD_ENTRY_(F)     SyncExposedField(#F, F);
#define LT_WRITEBACK_FIELD_ENTRY_(F) WriteBackExposedField(#F, F);

#define LT_EXPOSED_FIELDS(...) \
    void OnSynchronizeExposedFields() override \
    { \
        LT_PP_FOR_EACH_(LT_SYNC_FIELD_ENTRY_, __VA_ARGS__) \
    } \
    void OnWriteBackExposedFields() override \
    { \
        LT_PP_FOR_EACH_(LT_WRITEBACK_FIELD_ENTRY_, __VA_ARGS__) \
    }
