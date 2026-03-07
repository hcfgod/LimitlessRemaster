#include "Scene/SceneCollection.h"

#include <algorithm>
#include <utility>

namespace Limitless
{
    namespace
    {
        SceneCollection::Record* FindRecord(std::vector<SceneCollection::Record>& records, SceneCollection::Handle handle)
        {
            if (handle == SceneCollection::InvalidHandle)
                return nullptr;

            auto iterator = std::find_if(records.begin(), records.end(), [handle](const SceneCollection::Record& record) {
                return record.Id == handle;
            });
            if (iterator == records.end())
                return nullptr;
            return &(*iterator);
        }

        const SceneCollection::Record* FindRecord(const std::vector<SceneCollection::Record>& records, SceneCollection::Handle handle)
        {
            if (handle == SceneCollection::InvalidHandle)
                return nullptr;

            auto iterator = std::find_if(records.begin(), records.end(), [handle](const SceneCollection::Record& record) {
                return record.Id == handle;
            });
            if (iterator == records.end())
                return nullptr;
            return &(*iterator);
        }
    }

    SceneCollection::Handle SceneCollection::AddScene(std::unique_ptr<Scene> scene,
                                                      std::string assetKey,
                                                      SceneCollectionLifecycleState lifecycle,
                                                      SceneRoleMask roles)
    {
        const Handle handle = m_NextHandle++;
        Record record{};
        record.Id = handle;
        record.AssetKey = std::move(assetKey);
        record.SceneInstance = std::move(scene);
        record.Lifecycle = lifecycle;
        record.Roles = roles;
        m_Records.push_back(std::move(record));
        return handle;
    }

    bool SceneCollection::SetScene(Handle handle,
                                   std::unique_ptr<Scene> scene,
                                   std::string assetKey,
                                   SceneCollectionLifecycleState lifecycle,
                                   SceneRoleMask roles)
    {
        if (handle == InvalidHandle)
        {
            AddScene(std::move(scene), std::move(assetKey), lifecycle, roles);
            return true;
        }

        Record* record = GetRecord(handle);
        if (!record)
            return false;

        record->SceneInstance = std::move(scene);
        record->AssetKey = std::move(assetKey);
        record->Lifecycle = lifecycle;
        record->Roles = roles;
        return true;
    }

    std::unique_ptr<Scene> SceneCollection::ReleaseScene(Handle handle)
    {
        auto iterator = FindIterator(handle);
        if (iterator == m_Records.end())
            return nullptr;

        std::unique_ptr<Scene> releasedScene = std::move(iterator->SceneInstance);
        m_Records.erase(iterator);
        return releasedScene;
    }

    bool SceneCollection::RemoveScene(Handle handle)
    {
        auto iterator = FindIterator(handle);
        if (iterator == m_Records.end())
            return false;

        m_Records.erase(iterator);
        return true;
    }

    void SceneCollection::Clear()
    {
        m_Records.clear();
    }

    Scene* SceneCollection::GetScene(Handle handle)
    {
        Record* record = GetRecord(handle);
        return record ? record->SceneInstance.get() : nullptr;
    }

    const Scene* SceneCollection::GetScene(Handle handle) const
    {
        const Record* record = GetRecord(handle);
        return record ? record->SceneInstance.get() : nullptr;
    }

    SceneCollection::Record* SceneCollection::GetRecord(Handle handle)
    {
        return FindRecord(m_Records, handle);
    }

    const SceneCollection::Record* SceneCollection::GetRecord(Handle handle) const
    {
        return FindRecord(m_Records, handle);
    }

    SceneCollection::Handle SceneCollection::FindFirstHandleWithRoles(SceneRoleMask requiredRoles) const
    {
        const auto iterator = std::find_if(m_Records.begin(), m_Records.end(), [requiredRoles](const Record& record) {
            return record.SceneInstance && HasAllSceneRoles(record.Roles, requiredRoles);
        });
        return iterator != m_Records.end() ? iterator->Id : InvalidHandle;
    }

    Scene* SceneCollection::FindFirstSceneWithRoles(SceneRoleMask requiredRoles)
    {
        const Handle handle = FindFirstHandleWithRoles(requiredRoles);
        return GetScene(handle);
    }

    const Scene* SceneCollection::FindFirstSceneWithRoles(SceneRoleMask requiredRoles) const
    {
        const Handle handle = FindFirstHandleWithRoles(requiredRoles);
        return GetScene(handle);
    }

    std::vector<SceneCollection::Handle> SceneCollection::CollectHandlesWithRoles(SceneRoleMask requiredRoles) const
    {
        std::vector<Handle> handles;
        handles.reserve(m_Records.size());
        for (const Record& record : m_Records)
        {
            if (record.SceneInstance && HasAllSceneRoles(record.Roles, requiredRoles))
                handles.push_back(record.Id);
        }
        return handles;
    }

    bool SceneCollection::SetRoles(Handle handle, SceneRoleMask roles)
    {
        Record* record = GetRecord(handle);
        if (!record)
            return false;
        record->Roles = roles;
        return true;
    }

    bool SceneCollection::AddRoles(Handle handle, SceneRoleMask roles)
    {
        Record* record = GetRecord(handle);
        if (!record)
            return false;
        record->Roles |= roles;
        return true;
    }

    bool SceneCollection::RemoveRoles(Handle handle, SceneRoleMask roles)
    {
        Record* record = GetRecord(handle);
        if (!record)
            return false;
        record->Roles &= ~roles;
        return true;
    }

    bool SceneCollection::SetLifecycleState(Handle handle, SceneCollectionLifecycleState lifecycle)
    {
        Record* record = GetRecord(handle);
        if (!record)
            return false;
        record->Lifecycle = lifecycle;
        return true;
    }

    std::vector<SceneCollection::Record>::iterator SceneCollection::FindIterator(Handle handle)
    {
        return std::find_if(m_Records.begin(), m_Records.end(), [handle](const Record& record) {
            return record.Id == handle;
        });
    }

    std::vector<SceneCollection::Record>::const_iterator SceneCollection::FindIterator(Handle handle) const
    {
        return std::find_if(m_Records.begin(), m_Records.end(), [handle](const Record& record) {
            return record.Id == handle;
        });
    }

    SceneCollectionSlot::SceneCollectionSlot(SceneCollection& collection, SceneCollection::Handle& handle)
    {
        Bind(collection, handle);
    }

    void SceneCollectionSlot::Bind(SceneCollection& collection, SceneCollection::Handle& handle)
    {
        m_Collection = &collection;
        m_Handle = &handle;
    }

    Scene* SceneCollectionSlot::get() const
    {
        if (!IsBound())
            return nullptr;
        return m_Collection->GetScene(*m_Handle);
    }

    SceneCollection::Handle SceneCollectionSlot::GetHandle() const
    {
        if (!IsBound())
            return SceneCollection::InvalidHandle;
        return *m_Handle;
    }

    void SceneCollectionSlot::reset()
    {
        if (!IsBound())
            return;

        if (*m_Handle != SceneCollection::InvalidHandle)
        {
            m_Collection->RemoveScene(*m_Handle);
            *m_Handle = SceneCollection::InvalidHandle;
        }
    }

    std::unique_ptr<Scene> SceneCollectionSlot::Release()
    {
        if (!IsBound())
            return nullptr;

        if (*m_Handle == SceneCollection::InvalidHandle)
            return nullptr;

        std::unique_ptr<Scene> releasedScene = m_Collection->ReleaseScene(*m_Handle);
        *m_Handle = SceneCollection::InvalidHandle;
        return releasedScene;
    }

    void SceneCollectionSlot::SetOwnedScene(std::unique_ptr<Scene> scene,
                                            std::string assetKey,
                                            SceneCollectionLifecycleState lifecycle,
                                            SceneRoleMask roles)
    {
        if (!IsBound())
            return;

        if (*m_Handle == SceneCollection::InvalidHandle)
        {
            *m_Handle = m_Collection->AddScene(std::move(scene), std::move(assetKey), lifecycle, roles);
            return;
        }

        if (!m_Collection->SetScene(*m_Handle, std::move(scene), std::move(assetKey), lifecycle, roles))
        {
            *m_Handle = m_Collection->AddScene(std::move(scene), std::move(assetKey), lifecycle, roles);
        }
    }

    SceneCollectionSlot& SceneCollectionSlot::operator=(std::unique_ptr<Scene> scene)
    {
        std::string assetKey;
        SceneCollectionLifecycleState lifecycle = SceneCollectionLifecycleState::Ready;
        SceneRoleMask roles = 0u;
        if (IsBound() && *m_Handle != SceneCollection::InvalidHandle)
        {
            if (const SceneCollection::Record* record = m_Collection->GetRecord(*m_Handle))
            {
                assetKey = record->AssetKey;
                lifecycle = record->Lifecycle;
                roles = record->Roles;
            }
        }
        SetOwnedScene(std::move(scene), std::move(assetKey), lifecycle, roles);
        return *this;
    }

    void SceneCollectionSlot::swap(std::unique_ptr<Scene>& scene)
    {
        std::string assetKey;
        SceneCollectionLifecycleState lifecycle = SceneCollectionLifecycleState::Ready;
        SceneRoleMask roles = 0u;
        if (IsBound() && *m_Handle != SceneCollection::InvalidHandle)
        {
            if (const SceneCollection::Record* record = m_Collection->GetRecord(*m_Handle))
            {
                assetKey = record->AssetKey;
                lifecycle = record->Lifecycle;
                roles = record->Roles;
            }
        }

        std::unique_ptr<Scene> slotScene = Release();
        SetOwnedScene(std::move(scene), std::move(assetKey), lifecycle, roles);
        scene = std::move(slotScene);
    }

    bool SceneCollectionSlot::SetRoles(SceneRoleMask roles)
    {
        return IsBound() && *m_Handle != SceneCollection::InvalidHandle && m_Collection->SetRoles(*m_Handle, roles);
    }

    bool SceneCollectionSlot::AddRoles(SceneRoleMask roles)
    {
        return IsBound() && *m_Handle != SceneCollection::InvalidHandle && m_Collection->AddRoles(*m_Handle, roles);
    }

    bool SceneCollectionSlot::RemoveRoles(SceneRoleMask roles)
    {
        return IsBound() && *m_Handle != SceneCollection::InvalidHandle && m_Collection->RemoveRoles(*m_Handle, roles);
    }

    bool SceneCollectionSlot::SetLifecycleState(SceneCollectionLifecycleState lifecycle)
    {
        return IsBound() && *m_Handle != SceneCollection::InvalidHandle && m_Collection->SetLifecycleState(*m_Handle, lifecycle);
    }

    SceneRoleMask SceneCollectionSlot::GetRoles() const
    {
        if (!IsBound() || *m_Handle == SceneCollection::InvalidHandle)
            return 0u;
        const SceneCollection::Record* record = m_Collection->GetRecord(*m_Handle);
        return record ? record->Roles : 0u;
    }

    SceneCollectionLifecycleState SceneCollectionSlot::GetLifecycleState() const
    {
        if (!IsBound() || *m_Handle == SceneCollection::InvalidHandle)
            return SceneCollectionLifecycleState::Ready;
        const SceneCollection::Record* record = m_Collection->GetRecord(*m_Handle);
        return record ? record->Lifecycle : SceneCollectionLifecycleState::Ready;
    }

    std::string SceneCollectionSlot::GetAssetKey() const
    {
        if (!IsBound() || *m_Handle == SceneCollection::InvalidHandle)
            return {};
        const SceneCollection::Record* record = m_Collection->GetRecord(*m_Handle);
        return record ? record->AssetKey : std::string{};
    }

    bool SceneCollectionSlot::IsBound() const
    {
        return m_Collection != nullptr && m_Handle != nullptr;
    }
}
