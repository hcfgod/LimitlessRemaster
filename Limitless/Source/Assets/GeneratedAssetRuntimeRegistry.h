#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetDatabase.h"
#include "Core/Error.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Limitless::Assets
{
    class GeneratedAssetRuntimeRegistry final
    {
    public:
        struct Entry
        {
            std::weak_ptr<Asset> AssetInstance;
            std::function<std::shared_ptr<Asset>()> AssetFactory;
            std::function<bool()> Reload;
            std::function<Result<std::string>()> LoadText;
            std::function<Result<std::vector<uint8_t>>()> LoadBytes;
        };

        static GeneratedAssetRuntimeRegistry& GetInstance();

        void Register(const std::string& key, Entry entry);
        void RegisterAsset(const std::string& key,
                           const std::shared_ptr<Asset>& asset,
                           std::function<bool()> reload = {});
        void Unregister(const std::string& key);
        bool Has(const std::string& key) const;

        template<typename TAsset>
        std::shared_ptr<TAsset> GetAsset(const std::string& key)
        {
            static_assert(std::is_base_of_v<Asset, TAsset>, "TAsset must derive from Asset");

            if (key.empty())
                return nullptr;

            Entry entry = GetEntrySnapshot(key);
            if (auto existing = entry.AssetInstance.lock())
                return std::dynamic_pointer_cast<TAsset>(existing);

            if (!entry.AssetFactory)
                return nullptr;

            auto createdBase = entry.AssetFactory();
            auto created = std::dynamic_pointer_cast<TAsset>(createdBase);
            if (!created)
                return nullptr;

            std::unique_lock<std::shared_mutex> lock(m_Mutex);
            if (auto it = m_ByKey.find(key); it != m_ByKey.end())
                it->second.AssetInstance = created;
            return created;
        }

        bool Reload(const std::string& key);
        Result<std::string> LoadText(const std::string& key) const;
        Result<std::vector<uint8_t>> LoadBytes(const std::string& key) const;

    private:
        Entry GetEntrySnapshot(const std::string& key) const;

    private:
        mutable std::shared_mutex m_Mutex;
        std::unordered_map<std::string, Entry> m_ByKey;
    };

    inline Result<AssetDatabase::Record> FindGeneratedAssetRecord(const std::string& key, AssetType expectedType)
    {
        auto recordResult = AssetDatabase::GetInstance().FindByKey(key);
        if (recordResult.IsFailure())
            return recordResult;

        const auto& record = recordResult.GetValue();
        if (!record.IsGenerated())
            return Result<AssetDatabase::Record>(ErrorCode::ResourceNotFound, "Asset is not generated: " + key);
        if (record.Type != expectedType)
        {
            return Result<AssetDatabase::Record>(
                ErrorCode::InvalidState,
                "Generated asset type mismatch for key '" + key + "'");
        }

        return record;
    }

    inline bool IsGeneratedAssetKey(const std::string& key,
                                    AssetType expectedType,
                                    AssetDatabase::Record* outRecord = nullptr)
    {
        auto recordResult = FindGeneratedAssetRecord(key, expectedType);
        if (recordResult.IsFailure())
            return false;

        if (outRecord)
            *outRecord = recordResult.GetValue();
        return true;
    }
}
