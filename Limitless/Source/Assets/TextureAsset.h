#pragma once

#include "Assets/Asset.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetUtils.h"

#include "Core/Concurrency/AsyncIO.h"
#include "Graphics/Texture.h"

#include <memory>
#include <string>

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // TextureAsset
    // Unity-style texture asset:
    // - CPU decode happens on AsyncIO (background)
    // - GPU upload happens on the render thread via Texture2D::CreateFromRGBA8Async
    //
    // Note: This is the "good" async path (no decode on the render thread).
    // -----------------------------------------------------------------------------
    class TextureAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<TextureAsset>;

        static Async::Task<Ptr> LoadAsync(const std::string& assetPath, const TextureSpecification& specification = {});
        static Async::Task<Ptr> LoadAsync(const std::string& assetPath, const TextureSpecification& specification, uint64_t generation);
        static Ptr LoadBlocking(const std::string& assetPath, const TextureSpecification& specification = {});

        const std::shared_ptr<Texture2D>& GetTexture() const { return m_Texture; }

        bool Reload() override;

    private:
        TextureAsset(std::string key, std::string guid, std::shared_ptr<Texture2D> texture, TextureSpecification specification)
            : Asset(std::move(key), std::move(guid))
            , m_Texture(std::move(texture))
            , m_Specification(specification)
        {
        }

        std::shared_ptr<Texture2D> m_Texture;
        TextureSpecification m_Specification{};
    };
}

