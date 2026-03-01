#pragma once

#include "Core/Error.h"
#include "EnTT/entt.hpp"
#include "Scene/Entity.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/CoroutineTypes.h"
#include "Scripting/ScriptProperty.h"

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

        void SyncExposedField(const std::string& name, float& value) { value = GetExposedFloat(name, value); }
        void SyncExposedField(const std::string& name, int32_t& value) { value = GetExposedInteger(name, value); }
        void SyncExposedField(const std::string& name, bool& value) { value = GetExposedBoolean(name, value); }
        void SyncExposedField(const std::string& name, glm::vec3& value) { value = GetExposedVector3(name, value); }
        void SyncExposedField(const std::string& name, std::string& value) { value = GetExposedString(name, value); }
        void SyncExposedField(const std::string& name, Entity& value) { value = GetExposedEntity(name, value); }
        void SyncExposedField(const std::string& name, Prefab& value) { value = GetExposedPrefab(name, value); }

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

        virtual void OnCreate() {}
        virtual void OnFixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnCollisionEnter(const Entity& other) { (void)other; }
        virtual void OnCollisionStay(const Entity& other) { (void)other; }
        virtual void OnCollisionExit(const Entity& other) { (void)other; }
        virtual void OnTriggerEnter(const Entity& other) { (void)other; }
        virtual void OnTriggerStay(const Entity& other) { (void)other; }
        virtual void OnTriggerExit(const Entity& other) { (void)other; }
        virtual void OnDestroy() {}

    private:
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
        std::vector<CoroutineState> m_ActiveCoroutines;
        std::vector<CoroutineState> m_PendingCoroutineStarts;
        std::vector<CoroutineHandle> m_PendingCoroutineStops;
        uint64_t m_NextCoroutineIdentifier = 1;
        bool m_IsAdvancingCoroutines = false;

        static bool IsParallelScriptExecutionContext();
        std::vector<ScriptableEntity*> GetRuntimeScripts(entt::entity entity) const;
    };
}

#define LT_SYNC_EXPOSED_FIELD(FieldName) SyncExposedField(#FieldName, FieldName)
#define LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC() \
    void OnSynchronizeExposedFields() override \
    {
#define LT_AUTO_EXPOSED_FIELD(FieldName) \
        LT_SYNC_EXPOSED_FIELD(FieldName);
#define LT_END_AUTO_EXPOSED_FIELD_SYNC() \
    }
#define LT_SCRIPT_ACCESS_MASK(...) ::Limitless::ScriptAccess::Combine(__VA_ARGS__)
#define LT_DECLARE_SCRIPT_ACCESS(ReadMaskExpr, WriteMaskExpr) \
    uint64_t GetDeclaredReadAccessMask() const override { return static_cast<uint64_t>(ReadMaskExpr); } \
    uint64_t GetDeclaredWriteAccessMask() const override { return static_cast<uint64_t>(WriteMaskExpr); }
