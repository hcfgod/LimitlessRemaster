#pragma once

#include "Core/Error.h"
#include "Core/Debug/Log.h"

#include <memory>
#include <string>

namespace Limitless
{
    // -----------------------------------------------------------------------------
    // Asset
    // Unity-style runtime asset base class.
    //
    // - Assets have a stable identity GUID stored in a sidecar `.meta` file.
    // - Assets are typically referenced via AssetHandle<T> (weak GUID reference).
    // - AssetManager caches assets by key (path) and GUID, but uses weak_ptr so
    //   unused assets can be garbage collected naturally.
    // -----------------------------------------------------------------------------
    class Asset : public std::enable_shared_from_this<Asset>
    {
    public:
        virtual ~Asset() = default;

        const std::string& GetKey() const { return m_Key; }
        const std::string& GetGuid() const { return m_Guid; }

        // Optional hot-reload entry point. Default does nothing.
        virtual bool Reload() { return false; }

    protected:
        explicit Asset(std::string key, std::string guid)
            : m_Key(std::move(key))
            , m_Guid(std::move(guid))
        {
            LT_VERIFY(!m_Key.empty(), "Asset key must not be empty");
            LT_VERIFY(!m_Guid.empty(), "Asset GUID must not be empty");
        }

    private:
        std::string m_Key;
        std::string m_Guid;
    };
}

