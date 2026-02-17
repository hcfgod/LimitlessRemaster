#pragma once

#include "Core/Error.h"
#include "EnTT/entt.hpp"
#include "Scripting/ScriptProperty.h"

#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace Limitless
{
    class Scene;
    class ScriptableEntity;
    struct AnimatorComponent;
    using ScriptCreateEntityBridgeCallback = entt::entity (*)(const char* name);
    using ScriptDestroyEntityBridgeCallback = void (*)(entt::entity entity);

    // Unity-style script-facing entity wrapper.
    // This keeps script code ergonomic while preserving explicit native ownership rules.
    class Entity final
    {
    public:
        Entity() = default;

        bool IsValid() const;
        explicit operator bool() const { return IsValid(); }

        entt::entity GetHandle() const { return m_EntityHandle; }

        template<typename ComponentType>
        bool HasComponent() const;

        template<typename ComponentType>
        ComponentType& GetComponent() const;

        template<typename ComponentType, typename... ConstructorArgs>
        ComponentType& AddComponent(ConstructorArgs&&... args) const;

        template<typename ComponentType>
        void RemoveComponent() const;

        void Destroy();

    private:
        friend class ScriptableEntity;

        Entity(ScriptableEntity* scriptOwner, entt::entity entityHandle)
            : m_ScriptOwner(scriptOwner), m_EntityHandle(entityHandle)
        {
        }

        ScriptableEntity* m_ScriptOwner = nullptr;
        entt::entity m_EntityHandle = entt::null;
    };

    // Base type for native C++ entity scripts.
    // Derive from this, register in NativeScriptRegistry, then assign in NativeScriptComponent.
    class ScriptableEntity
    {
        friend class Scene;

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

        Entity GetEntity()
        {
            return Entity(this, m_EntityHandle);
        }

        Entity GetEntity() const
        {
            return Entity(const_cast<ScriptableEntity*>(this), m_EntityHandle);
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
            return m_Registry->get<ComponentType>(entity);
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
            if (m_Registry->all_of<ComponentType>(entity))
                m_Registry->remove<ComponentType>(entity);
        }

        Entity CreateEntity(const std::string& name = "Entity");
        entt::entity CreateEntityHandle(const std::string& name = "Entity");
        void DestroyEntity(Entity entity);
        void DestroyEntity(entt::entity entity);
        bool IsEntityValid(entt::entity entity) const;
        Entity GetEntity(entt::entity entity) const;
        Entity FindEntityByTag(const std::string& tag) const;

        static void SetCreateEntityBridgeCallback(ScriptCreateEntityBridgeCallback callback);
        static void SetDestroyEntityBridgeCallback(ScriptDestroyEntityBridgeCallback callback);

    protected:
        float GetExposedFloat(const std::string& name, float fallbackValue = 0.0f) const;
        int32_t GetExposedInteger(const std::string& name, int32_t fallbackValue = 0) const;
        bool GetExposedBoolean(const std::string& name, bool fallbackValue = false) const;
        glm::vec3 GetExposedVector3(const std::string& name, const glm::vec3& fallbackValue = glm::vec3(0.0f)) const;
        std::string GetExposedString(const std::string& name, const std::string& fallbackValue = {}) const;
        Entity GetExposedEntity(const std::string& name, const Entity& fallbackValue = Entity{}) const;

        void SetExposedFloat(const std::string& name, float value);
        void SetExposedInteger(const std::string& name, int32_t value);
        void SetExposedBoolean(const std::string& name, bool value);
        void SetExposedVector3(const std::string& name, const glm::vec3& value);
        void SetExposedString(const std::string& name, const std::string& value);
        void SetExposedEntity(const std::string& name, const Entity& value);

        void SyncExposedField(const std::string& name, float& value) { value = GetExposedFloat(name, value); }
        void SyncExposedField(const std::string& name, int32_t& value) { value = GetExposedInteger(name, value); }
        void SyncExposedField(const std::string& name, bool& value) { value = GetExposedBoolean(name, value); }
        void SyncExposedField(const std::string& name, glm::vec3& value) { value = GetExposedVector3(name, value); }
        void SyncExposedField(const std::string& name, std::string& value) { value = GetExposedString(name, value); }
        void SyncExposedField(const std::string& name, Entity& value) { value = GetExposedEntity(name, value); }

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

        virtual void OnSynchronizeExposedFields() {}

        virtual void OnCreate() {}
        virtual void OnFixedUpdate(float fixedDeltaTime) { (void)fixedDeltaTime; }
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnDestroy() {}

    private:
        Scene* m_Scene = nullptr;
        entt::registry* m_Registry = nullptr;
        entt::entity m_EntityHandle = entt::null;
        std::unordered_map<std::string, ScriptPropertyValue>* m_ExposedProperties = nullptr;
    };

    template<typename ComponentType>
    inline bool Entity::HasComponent() const
    {
        return m_ScriptOwner ? m_ScriptOwner->HasComponent<ComponentType>(m_EntityHandle) : false;
    }

    template<typename ComponentType>
    inline ComponentType& Entity::GetComponent() const
    {
        if (!m_ScriptOwner)
            throw std::runtime_error("Entity has no owning script context");
        return m_ScriptOwner->GetComponent<ComponentType>(m_EntityHandle);
    }

    template<typename ComponentType, typename... ConstructorArgs>
    inline ComponentType& Entity::AddComponent(ConstructorArgs&&... args) const
    {
        if (!m_ScriptOwner)
            throw std::runtime_error("Entity has no owning script context");
        return m_ScriptOwner->AddComponent<ComponentType>(m_EntityHandle, std::forward<ConstructorArgs>(args)...);
    }

    template<typename ComponentType>
    inline void Entity::RemoveComponent() const
    {
        if (!m_ScriptOwner)
            return;
        m_ScriptOwner->RemoveComponent<ComponentType>(m_EntityHandle);
    }
}

#define LT_SYNC_EXPOSED_FIELD(FieldName) SyncExposedField(#FieldName, FieldName)
#define LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC() \
    void OnSynchronizeExposedFields() override \
    {
#define LT_AUTO_EXPOSED_FIELD(FieldName) \
        LT_SYNC_EXPOSED_FIELD(FieldName);
#define LT_END_AUTO_EXPOSED_FIELD_SYNC() \
    }
