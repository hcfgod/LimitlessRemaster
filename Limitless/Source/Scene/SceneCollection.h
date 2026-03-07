#pragma once

#include "Scene/Scene.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    enum class SceneRole : uint32_t
    {
        None = 0u,
        GameplayPrimary = 1u << 0u,
        RuntimeUpdate = 1u << 1u,
        FixedUpdate = 1u << 2u,
        Render = 1u << 3u,
        ScriptQueryTarget = 1u << 4u,
        AudioPlayback = 1u << 5u,
        EditAuthoring = 1u << 6u
    };

    using SceneRoleMask = uint32_t;

    constexpr SceneRoleMask ToSceneRoleMask(SceneRole role)
    {
        return static_cast<SceneRoleMask>(role);
    }

    constexpr SceneRoleMask operator|(SceneRole lhs, SceneRole rhs)
    {
        return ToSceneRoleMask(lhs) | ToSceneRoleMask(rhs);
    }

    constexpr SceneRoleMask operator|(SceneRoleMask lhs, SceneRole rhs)
    {
        return lhs | ToSceneRoleMask(rhs);
    }

    constexpr bool HasAllSceneRoles(SceneRoleMask roles, SceneRoleMask requiredRoles)
    {
        return (roles & requiredRoles) == requiredRoles;
    }

    enum class SceneCollectionLifecycleState : uint8_t
    {
        Loading = 0,
        Ready = 1,
        Active = 2,
        Suspended = 3,
        Unloading = 4
    };

    class SceneCollection final
    {
    public:
        using Handle = uint64_t;
        static constexpr Handle InvalidHandle = 0;

        struct Record final
        {
            Handle Id = InvalidHandle;
            std::string AssetKey;
            std::unique_ptr<Scene> SceneInstance;
            SceneCollectionLifecycleState Lifecycle = SceneCollectionLifecycleState::Ready;
            SceneRoleMask Roles = 0u;
        };

    public:
        Handle AddScene(std::unique_ptr<Scene> scene,
                        std::string assetKey = {},
                        SceneCollectionLifecycleState lifecycle = SceneCollectionLifecycleState::Ready,
                        SceneRoleMask roles = 0u);
        bool SetScene(Handle handle,
                      std::unique_ptr<Scene> scene,
                      std::string assetKey,
                      SceneCollectionLifecycleState lifecycle,
                      SceneRoleMask roles);
        std::unique_ptr<Scene> ReleaseScene(Handle handle);
        bool RemoveScene(Handle handle);
        void Clear();

        size_t GetSceneCount() const { return m_Records.size(); }

        Scene* GetScene(Handle handle);
        const Scene* GetScene(Handle handle) const;

        Record* GetRecord(Handle handle);
        const Record* GetRecord(Handle handle) const;

        Handle FindFirstHandleWithRoles(SceneRoleMask requiredRoles) const;
        Scene* FindFirstSceneWithRoles(SceneRoleMask requiredRoles);
        const Scene* FindFirstSceneWithRoles(SceneRoleMask requiredRoles) const;
        std::vector<Handle> CollectHandlesWithRoles(SceneRoleMask requiredRoles) const;

        bool SetRoles(Handle handle, SceneRoleMask roles);
        bool AddRoles(Handle handle, SceneRoleMask roles);
        bool RemoveRoles(Handle handle, SceneRoleMask roles);
        bool SetLifecycleState(Handle handle, SceneCollectionLifecycleState lifecycle);

    private:
        std::vector<Record>::iterator FindIterator(Handle handle);
        std::vector<Record>::const_iterator FindIterator(Handle handle) const;

    private:
        std::vector<Record> m_Records;
        Handle m_NextHandle = 1;
    };

    class SceneCollectionSlot final
    {
    public:
        SceneCollectionSlot() = default;
        SceneCollectionSlot(SceneCollection& collection, SceneCollection::Handle& handle);

        void Bind(SceneCollection& collection, SceneCollection::Handle& handle);

        Scene* get() const;
        Scene* operator->() const { return get(); }
        Scene& operator*() const { return *get(); }
        explicit operator bool() const { return get() != nullptr; }

        SceneCollection::Handle GetHandle() const;

        void reset();
        std::unique_ptr<Scene> Release();
        void SetOwnedScene(std::unique_ptr<Scene> scene,
                           std::string assetKey = {},
                           SceneCollectionLifecycleState lifecycle = SceneCollectionLifecycleState::Ready,
                           SceneRoleMask roles = 0u);
        SceneCollectionSlot& operator=(std::unique_ptr<Scene> scene);
        void swap(std::unique_ptr<Scene>& scene);

        bool SetRoles(SceneRoleMask roles);
        bool AddRoles(SceneRoleMask roles);
        bool RemoveRoles(SceneRoleMask roles);
        bool SetLifecycleState(SceneCollectionLifecycleState lifecycle);

        SceneRoleMask GetRoles() const;
        SceneCollectionLifecycleState GetLifecycleState() const;
        std::string GetAssetKey() const;

    private:
        bool IsBound() const;

    private:
        SceneCollection* m_Collection = nullptr;
        SceneCollection::Handle* m_Handle = nullptr;
    };
}
