#pragma once

#include "Graphics/Texture.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Limitless
{
    struct FontSpecification
    {
        // Glyph size in output pixels per EM.
        float EmSize = 48.0f;
        // Distance field pixel range used by msdf-atlas-gen.
        float PixelRange = 2.0f;
        // Extra padding between glyphs in the atlas.
        int Padding = 1;
    };

    class Font final
    {
    public:
        using Ptr = std::shared_ptr<Font>;

        struct Glyph
        {
            uint32_t Codepoint = 0;
            float Advance = 0.0f;
            glm::vec2 PlaneMin{0.0f};
            glm::vec2 PlaneMax{0.0f};
            glm::vec2 AtlasMin{0.0f};
            glm::vec2 AtlasMax{0.0f};
            bool HasGeometry = false;
        };

        static Ptr CreateFromFile(const std::string& fontPath, const FontSpecification& specification = {});

        const Glyph* GetGlyph(uint32_t codepoint) const;
        float GetKerningAdvance(uint32_t leftCodepoint, uint32_t rightCodepoint) const;

        std::shared_ptr<Texture2D> GetAtlasTexture() const { return m_AtlasTexture; }
        float GetLineHeight() const { return m_LineHeight; }
        float GetAscender() const { return m_Ascender; }
        float GetDescender() const { return m_Descender; }
        float GetPixelRange() const { return m_PixelRange; }
        float GetEmSize() const { return m_EmSize; }
        uint32_t GetAtlasWidth() const { return m_AtlasWidth; }
        uint32_t GetAtlasHeight() const { return m_AtlasHeight; }

    private:
        struct PairHash
        {
            size_t operator()(const std::pair<uint32_t, uint32_t>& value) const noexcept
            {
                const uint64_t packed = (static_cast<uint64_t>(value.first) << 32ull) | static_cast<uint64_t>(value.second);
                return std::hash<uint64_t>{}(packed);
            }
        };

        std::unordered_map<uint32_t, Glyph> m_Glyphs;
        std::unordered_map<std::pair<uint32_t, uint32_t>, float, PairHash> m_KerningPairs;
        std::shared_ptr<Texture2D> m_AtlasTexture;
        float m_LineHeight = 0.0f;
        float m_Ascender = 0.0f;
        float m_Descender = 0.0f;
        float m_PixelRange = 0.0f;
        float m_EmSize = 1.0f;
        uint32_t m_AtlasWidth = 0;
        uint32_t m_AtlasHeight = 0;
    };
}
