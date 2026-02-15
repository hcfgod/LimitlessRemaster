#pragma once

#include "Core/Error.h"
#include "EnTT/entt.hpp"
#include "Scripting/ScriptProperty.h"

#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Limitless
{
    class Scene;

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

        template<typename ComponentType>
        bool HasComponent() const
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            return m_Registry->all_of<ComponentType>(m_EntityHandle);
        }

        template<typename ComponentType>
        ComponentType& GetComponent()
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("ScriptableEntity has no registry binding");
            if (!m_Registry->all_of<ComponentType>(m_EntityHandle))
                throw std::runtime_error("ScriptableEntity missing requested component");
            return m_Registry->get<ComponentType>(m_EntityHandle);
        }

    protected:
        float GetExposedFloat(const std::string& name, float fallbackValue = 0.0f) const;
        int32_t GetExposedInteger(const std::string& name, int32_t fallbackValue = 0) const;
        bool GetExposedBoolean(const std::string& name, bool fallbackValue = false) const;
        glm::vec3 GetExposedVector3(const std::string& name, const glm::vec3& fallbackValue = glm::vec3(0.0f)) const;
        std::string GetExposedString(const std::string& name, const std::string& fallbackValue = {}) const;

        void SetExposedFloat(const std::string& name, float value);
        void SetExposedInteger(const std::string& name, int32_t value);
        void SetExposedBoolean(const std::string& name, bool value);
        void SetExposedVector3(const std::string& name, const glm::vec3& value);
        void SetExposedString(const std::string& name, const std::string& value);

        void SyncExposedField(const std::string& name, float& value) { value = GetExposedFloat(name, value); }
        void SyncExposedField(const std::string& name, int32_t& value) { value = GetExposedInteger(name, value); }
        void SyncExposedField(const std::string& name, bool& value) { value = GetExposedBoolean(name, value); }
        void SyncExposedField(const std::string& name, glm::vec3& value) { value = GetExposedVector3(name, value); }
        void SyncExposedField(const std::string& name, std::string& value) { value = GetExposedString(name, value); }

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
}

#define LT_SYNC_EXPOSED_FIELD(FieldName) SyncExposedField(#FieldName, FieldName)
#define LT_BEGIN_AUTO_EXPOSED_FIELD_SYNC() \
    void OnSynchronizeExposedFields() override \
    {
#define LT_AUTO_EXPOSED_FIELD(FieldName) \
        LT_SYNC_EXPOSED_FIELD(FieldName);
#define LT_END_AUTO_EXPOSED_FIELD_SYNC() \
    }
