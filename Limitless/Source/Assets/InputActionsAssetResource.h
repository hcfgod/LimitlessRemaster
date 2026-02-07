#pragma once

#include "Assets/Asset.h"

#include "Core/Concurrency/AsyncIO.h"
#include "Core/Input/InputAction.h"

#include <atomic>
#include <memory>
#include <string>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // InputActionsAssetResource
    // Unity-style Input Actions asset as a first-class engine Asset:
    // - Stored as JSON in Assets/
    // - Loaded via InputActionAssetSerializer
    // -----------------------------------------------------------------------------
    class InputActionsAssetResource final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<InputActionsAssetResource>;

        struct Settings
        {
            // reserved for importer settings (validation rules, etc.)
        };

        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings = {});
        static Ptr LoadBlocking(const std::string& key, Settings settings = {});

        bool Reload() override;

        const std::shared_ptr<Limitless::InputActionAsset>& GetValue() const { return m_Value; }
        uint64_t GetRevision() const { return m_Revision.load(std::memory_order_relaxed); }

    private:
        InputActionsAssetResource(std::string key, std::string guid, std::shared_ptr<Limitless::InputActionAsset> value, Settings settings)
            : Asset(std::move(key), std::move(guid))
            , m_Value(std::move(value))
            , m_Settings(std::move(settings))
        {
        }

        std::string m_ResolvedPath;
        std::shared_ptr<Limitless::InputActionAsset> m_Value;
        Settings m_Settings{};
        std::atomic<uint64_t> m_Revision{0};
    };
}

