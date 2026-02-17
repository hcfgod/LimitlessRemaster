#pragma once

#include "EnTT/entt.hpp"

#include <stdexcept>
#include <utility>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Engine-level entity wrapper (Unity-style).
    //
    // Holds a raw entt::entity handle together with the owning registry pointer
    // so that component operations can be performed ergonomically without
    // passing the registry separately.
    //
    // Usable from engine code, editor code, and native scripts alike.
    // -------------------------------------------------------------------------
    class Entity final
    {
    public:
        /// Default-constructed entity represents "no entity" (null).
        Entity() = default;

        /// Construct from a registry and raw handle. Both must remain valid for
        /// the lifetime of this object -- Entity does not own the registry.
        Entity(entt::registry* registry, entt::entity entityHandle)
            : m_Registry(registry), m_EntityHandle(entityHandle)
        {
        }

        /// Returns true when the handle refers to a live entity in its registry.
        bool IsValid() const
        {
            if (m_Registry == nullptr || m_EntityHandle == entt::null)
                return false;
            return m_Registry->valid(m_EntityHandle);
        }

        /// Implicit boolean conversion -- same as IsValid().
        explicit operator bool() const { return IsValid(); }

        /// Access the raw EnTT handle (for interop with low-level code).
        entt::entity GetHandle() const { return m_EntityHandle; }

        /// Access the underlying registry (for advanced / editor use).
        entt::registry* GetRegistry() const { return m_Registry; }

        // -----------------------------------------------------------------
        // Component access (mirrors Unity GetComponent / TryGetComponent)
        // -----------------------------------------------------------------

        /// Returns true if the entity has a component of the given type.
        template<typename ComponentType>
        bool HasComponent() const
        {
            if (!IsValid())
                return false;
            return m_Registry->all_of<ComponentType>(m_EntityHandle);
        }

        /// Returns a reference to the component. Throws if the entity is
        /// invalid or does not have the component.
        template<typename ComponentType>
        ComponentType& GetComponent() const
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("Entity has no registry binding");
            if (!m_Registry->valid(m_EntityHandle))
                throw std::runtime_error("Entity referenced invalid handle");
            if (!m_Registry->all_of<ComponentType>(m_EntityHandle))
                throw std::runtime_error("Entity missing requested component");
            return m_Registry->get<ComponentType>(m_EntityHandle);
        }

        /// Returns a pointer to the component, or nullptr if the entity is
        /// invalid or does not have the component.  This is the recommended
        /// null-safe accessor for scripts and editor code.
        template<typename ComponentType>
        ComponentType* TryGetComponent() const
        {
            if (m_Registry == nullptr || !m_Registry->valid(m_EntityHandle))
                return nullptr;
            return m_Registry->try_get<ComponentType>(m_EntityHandle);
        }

        /// Adds a component (or returns the existing one if already present).
        /// Idempotent for scripting convenience.
        template<typename ComponentType, typename... ConstructorArgs>
        ComponentType& AddComponent(ConstructorArgs&&... args) const
        {
            if (m_Registry == nullptr)
                throw std::runtime_error("Entity has no registry binding");
            if (!m_Registry->valid(m_EntityHandle))
                throw std::runtime_error("Entity referenced invalid handle");
            if (m_Registry->all_of<ComponentType>(m_EntityHandle))
                return m_Registry->get<ComponentType>(m_EntityHandle);
            return m_Registry->emplace<ComponentType>(m_EntityHandle, std::forward<ConstructorArgs>(args)...);
        }

        /// Removes a component if present. Safe to call even if missing.
        template<typename ComponentType>
        void RemoveComponent() const
        {
            if (m_Registry == nullptr || !m_Registry->valid(m_EntityHandle))
                return;
            if (m_Registry->all_of<ComponentType>(m_EntityHandle))
                m_Registry->remove<ComponentType>(m_EntityHandle);
        }

        /// Destroy this entity (removes it from the registry entirely).
        void Destroy()
        {
            if (m_Registry != nullptr && m_Registry->valid(m_EntityHandle))
                m_Registry->destroy(m_EntityHandle);
            m_EntityHandle = entt::null;
        }

    private:
        entt::registry* m_Registry = nullptr;
        entt::entity m_EntityHandle = entt::null;
    };
}
