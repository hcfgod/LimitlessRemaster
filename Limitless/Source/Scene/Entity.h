#pragma once

#include "EnTT/entt.hpp"

#include <string>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Limitless
{
    using EntityDestroyBridgeCallback = void (*)(entt::entity entity);
    using EntityParallelExecutionBridgeCallback = bool (*)();

    namespace detail
    {
        template<typename T, typename = void>
        struct HasTransformDirtyFields : std::false_type
        {
        };

        template<typename T>
        struct HasTransformDirtyFields<T, std::void_t<decltype(std::declval<T&>().Dirty), decltype(std::declval<T&>().WorldTransform)>> : std::true_type
        {
        };

    }

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

        /// Construct an inspector/runtime prefab asset reference that can be
        /// passed to ScriptableEntity::Instantiate.
        static Entity FromPrefabAssetKey(const std::string& prefabAssetKey)
        {
            Entity entity;
            entity.m_PrefabAssetKey = prefabAssetKey;
            return entity;
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

        /// Returns true when this value represents a prefab asset reference.
        bool IsPrefabReference() const { return !m_PrefabAssetKey.empty(); }

        /// Returns prefab asset key when IsPrefabReference() is true.
        const std::string& GetPrefabAssetKey() const { return m_PrefabAssetKey; }

        static void SetDestroyBridgeCallback(EntityDestroyBridgeCallback callback)
        {
            s_DestroyBridgeCallback = callback;
        }

        static void SetParallelExecutionBridgeCallback(EntityParallelExecutionBridgeCallback callback)
        {
            s_ParallelExecutionBridgeCallback = callback;
        }

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
            ComponentType& component = m_Registry->get<ComponentType>(m_EntityHandle);
            if constexpr (detail::HasTransformDirtyFields<ComponentType>::value)
                component.Dirty = true;
            return component;
        }

        /// Returns a pointer to the component, or nullptr if the entity is
        /// invalid or does not have the component.  This is the recommended
        /// null-safe accessor for scripts and editor code.
        template<typename ComponentType>
        ComponentType* TryGetComponent() const
        {
            if (m_Registry == nullptr || !m_Registry->valid(m_EntityHandle))
                return nullptr;
            ComponentType* component = m_Registry->try_get<ComponentType>(m_EntityHandle);
            if constexpr (detail::HasTransformDirtyFields<ComponentType>::value)
            {
                if (component)
                {
                    component->Dirty = true;
                }
            }
            return component;
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
            if (IsParallelExecutionContext() && !m_Registry->all_of<ComponentType>(m_EntityHandle))
                throw std::runtime_error("Entity::AddComponent is not supported during parallel script execution");
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
            if (IsParallelExecutionContext() && m_Registry->all_of<ComponentType>(m_EntityHandle))
                throw std::runtime_error("Entity::RemoveComponent is not supported during parallel script execution");
            if (m_Registry->all_of<ComponentType>(m_EntityHandle))
                m_Registry->remove<ComponentType>(m_EntityHandle);
        }

        /// Destroy this entity (removes it from the registry entirely).
        void Destroy()
        {
            if (m_EntityHandle == entt::null)
                return;

            if (IsParallelExecutionContext())
                throw std::runtime_error("Entity::Destroy is not supported during parallel script execution");

            if (s_DestroyBridgeCallback)
                s_DestroyBridgeCallback(m_EntityHandle);
            else if (m_Registry != nullptr && m_Registry->valid(m_EntityHandle))
                m_Registry->destroy(m_EntityHandle);
            m_EntityHandle = entt::null;
        }

    private:
        static bool IsParallelExecutionContext()
        {
            if (s_ParallelExecutionBridgeCallback)
                return s_ParallelExecutionBridgeCallback();
            return false;
        }

    private:
        static inline EntityDestroyBridgeCallback s_DestroyBridgeCallback = nullptr;
        static inline EntityParallelExecutionBridgeCallback s_ParallelExecutionBridgeCallback = nullptr;
        entt::registry* m_Registry = nullptr;
        entt::entity m_EntityHandle = entt::null;
        std::string m_PrefabAssetKey;
    };
}
