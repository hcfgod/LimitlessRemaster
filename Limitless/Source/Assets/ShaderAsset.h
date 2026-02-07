#pragma once

#include "Assets/Asset.h"

#include "Core/Concurrency/AsyncIO.h"
#include "Graphics/Shader.h"

#include <memory>
#include <string>

namespace Limitless::Assets
{
    class ShaderAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<ShaderAsset>;

        struct Settings
        {
            // Optional override; if empty, derived from filename stem.
            std::string Name;
        };

        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings = {});
        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings, uint64_t generation);
        static Ptr LoadBlocking(const std::string& key, Settings settings = {});

        const std::shared_ptr<Shader>& GetShader() const { return m_Shader; }

        bool Reload() override;

    private:
        ShaderAsset(std::string key, std::string guid, std::shared_ptr<Shader> shader, Settings settings)
            : Asset(std::move(key), std::move(guid))
            , m_Shader(std::move(shader))
            , m_Settings(std::move(settings))
        {
        }

        std::shared_ptr<Shader> m_Shader;
        Settings m_Settings{};
    };
}

