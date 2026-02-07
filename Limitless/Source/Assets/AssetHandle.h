#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetManager.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <type_traits>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // AssetHandle<T>
    // Unity-like lightweight reference:
    // - Stores GUID (persistent)
    // - Stores weak_ptr cache (non-owning)
    // -----------------------------------------------------------------------------
    template<typename T>
    class AssetHandle final
    {
        static_assert(std::is_base_of_v<Asset, T>, "AssetHandle<T>: T must derive from Asset");

    public:
        AssetHandle() = default;
        AssetHandle(std::nullptr_t) {}

        explicit AssetHandle(const std::shared_ptr<T>& asset)
        {
            if (asset)
            {
                m_Guid = asset->GetGuid();
                m_Cached = asset;
            }
        }

        explicit AssetHandle(std::string guid)
            : m_Guid(std::move(guid))
        {
        }

        std::shared_ptr<T> Lock() const
        {
            if (auto sp = m_Cached.lock())
            {
                return sp;
            }

            if (!m_Guid.empty())
            {
                auto resolved = AssetManager::GetByGuid<T>(m_Guid);
                if (resolved)
                {
                    m_Cached = resolved;
                }
                return resolved;
            }

            return nullptr;
        }

        const std::string& GetGuid() const { return m_Guid; }

        void Reset()
        {
            m_Cached.reset();
            m_Guid.clear();
        }

        explicit operator bool() const { return Lock() != nullptr; }
        operator std::shared_ptr<T>() const { return Lock(); }

        T* operator->() const { return Lock().get(); }
        T& operator*() const { return *Lock(); }

        friend bool operator==(const AssetHandle& a, const AssetHandle& b) { return a.m_Guid == b.m_Guid; }
        friend bool operator!=(const AssetHandle& a, const AssetHandle& b) { return !(a == b); }

        struct Hasher
        {
            size_t operator()(const AssetHandle& h) const noexcept
            {
                return std::hash<std::string>{}(h.m_Guid);
            }
        };

    private:
        mutable std::weak_ptr<T> m_Cached;
        std::string m_Guid;
    };

    // -------------------------------------------------------------------------
    // JSON serialization
    //
    // Unity-style: store only stable GUID. This keeps scene/prefab files robust
    // against asset moves/renames.
    //
    // Supported formats:
    // - { "guid": "..." }
    // - "..." (string GUID)  [accepted for convenience]
    // -------------------------------------------------------------------------
    template<typename T>
    inline void to_json(nlohmann::json& j, const AssetHandle<T>& handle)
    {
        j = nlohmann::json::object();
        j["guid"] = handle.GetGuid();
    }

    template<typename T>
    inline void from_json(const nlohmann::json& j, AssetHandle<T>& handle)
    {
        if (j.is_string())
        {
            handle = AssetHandle<T>(j.get<std::string>());
            return;
        }

        if (j.is_object() && j.contains("guid") && j["guid"].is_string())
        {
            handle = AssetHandle<T>(j["guid"].get<std::string>());
            return;
        }

        handle.Reset();
    }
}

