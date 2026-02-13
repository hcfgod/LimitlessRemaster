#include "Graphics/Font.h"

#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"

#include <msdf-atlas-gen.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        constexpr uint32_t kFallbackCodepoint = static_cast<uint32_t>('?');
        std::mutex g_FontCacheMutex;
        std::unordered_map<std::string, std::weak_ptr<Font>> g_FontCache;

        msdf_atlas::Charset BuildDefaultCharset()
        {
            msdf_atlas::Charset charset = msdf_atlas::Charset::ASCII;
            charset.add('\n');
            charset.add('\t');
            return charset;
        }

        std::string ResolveFontPath(const std::string& fontPath)
        {
            if (fontPath.empty())
            {
                return {};
            }

            std::filesystem::path inputPath(fontPath);
            if (inputPath.is_absolute() && std::filesystem::exists(inputPath))
            {
                return inputPath.string();
            }

            if (std::filesystem::exists(inputPath))
            {
                return std::filesystem::absolute(inputPath).string();
            }

            if (fontPath.rfind("Assets/", 0) == 0)
            {
                const auto resolved = Assets::ResolveAssetKeyToPath(fontPath);
                if (resolved.IsSuccess())
                {
                    return resolved.GetValue().string();
                }
            }

            if (fontPath.rfind("Assets/", 0) != 0)
            {
                const std::string asAssetKey = "Assets/" + fontPath;
                const auto resolved = Assets::ResolveAssetKeyToPath(asAssetKey);
                if (resolved.IsSuccess())
                {
                    return resolved.GetValue().string();
                }
            }

            return {};
        }
    }

    Font::Ptr Font::CreateFromFile(const std::string& fontPath, const FontSpecification& specification)
    {
        const std::string resolvedFontPath = ResolveFontPath(fontPath);
        if (resolvedFontPath.empty())
        {
            LT_CORE_ERROR("Font: failed to resolve font path '{}'", fontPath);
            return nullptr;
        }

        {
            std::scoped_lock cacheLock(g_FontCacheMutex);
            auto cacheIt = g_FontCache.find(resolvedFontPath);
            if (cacheIt != g_FontCache.end())
            {
                if (auto cachedFont = cacheIt->second.lock())
                {
                    return cachedFont;
                }
                g_FontCache.erase(cacheIt);
            }
        }

        msdfgen::FreetypeHandle* freetype = msdfgen::initializeFreetype();
        if (!freetype)
        {
            LT_CORE_ERROR("Font: failed to initialize FreeType");
            return nullptr;
        }

        msdfgen::FontHandle* fontHandle = msdfgen::loadFont(freetype, resolvedFontPath.c_str());
        if (!fontHandle)
        {
            LT_CORE_ERROR("Font: failed to load font '{}' (resolved '{}')", fontPath, resolvedFontPath);
            msdfgen::deinitializeFreetype(freetype);
            return nullptr;
        }

        std::vector<msdf_atlas::GlyphGeometry> glyphs;
        glyphs.reserve(128);

        msdf_atlas::FontGeometry fontGeometry(&glyphs);
        const msdf_atlas::Charset charset = BuildDefaultCharset();

        const int loadedGlyphCount = fontGeometry.loadCharset(fontHandle, 1.0, charset, true, true);
        if (loadedGlyphCount <= 0)
        {
            LT_CORE_ERROR("Font: no glyphs loaded from '{}'", fontPath);
            msdfgen::destroyFont(fontHandle);
            msdfgen::deinitializeFreetype(freetype);
            return nullptr;
        }

        unsigned long long glyphSeed = 0;
        for (msdf_atlas::GlyphGeometry& glyph : glyphs)
        {
            glyphSeed *= 6364136223846793005ull;
            glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, glyphSeed);
        }

        msdf_atlas::TightAtlasPacker atlasPacker;
        atlasPacker.setDimensionsConstraint(msdf_atlas::TightAtlasPacker::DimensionsConstraint::MULTIPLE_OF_FOUR_SQUARE);
        atlasPacker.setPadding(std::max(0, specification.Padding));
        atlasPacker.setScale(std::max(1.0f, specification.EmSize));
        atlasPacker.setPixelRange(std::max(1.0f, specification.PixelRange));
        atlasPacker.setMiterLimit(1.0);

        if (atlasPacker.pack(glyphs.data(), static_cast<int>(glyphs.size())) != 0)
        {
            LT_CORE_ERROR("Font: failed to pack glyphs for '{}'", fontPath);
            msdfgen::destroyFont(fontHandle);
            msdfgen::deinitializeFreetype(freetype);
            return nullptr;
        }

        int atlasWidth = 0;
        int atlasHeight = 0;
        atlasPacker.getDimensions(atlasWidth, atlasHeight);
        if (atlasWidth <= 0 || atlasHeight <= 0)
        {
            LT_CORE_ERROR("Font: generated invalid atlas dimensions for '{}'", fontPath);
            msdfgen::destroyFont(fontHandle);
            msdfgen::deinitializeFreetype(freetype);
            return nullptr;
        }

        using AtlasGenerator = msdf_atlas::ImmediateAtlasGenerator<float, 3, msdf_atlas::msdfGenerator, msdf_atlas::BitmapAtlasStorage<float, 3>>;
        AtlasGenerator generator(atlasWidth, atlasHeight);
        msdf_atlas::GeneratorAttributes generatorAttributes{};
        generator.setAttributes(generatorAttributes);
        const int hardwareThreadCount = static_cast<int>(std::thread::hardware_concurrency());
        generator.setThreadCount(hardwareThreadCount > 0 ? hardwareThreadCount : 1);
        generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

        const msdfgen::BitmapConstRef<float, 3> atlasBitmap = generator.atlasStorage();
        std::vector<uint8_t> rgbaPixels;
        rgbaPixels.resize(static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * 4ull, 255u);

        for (int y = 0; y < atlasHeight; ++y)
        {
            for (int x = 0; x < atlasWidth; ++x)
            {
                const float* source = atlasBitmap(x, y);
                const size_t destinationBase = (static_cast<size_t>(y) * static_cast<size_t>(atlasWidth) + static_cast<size_t>(x)) * 4ull;
                for (int channel = 0; channel < 3; ++channel)
                {
                    const float clamped = std::clamp(source[channel], 0.0f, 1.0f);
                    rgbaPixels[destinationBase + static_cast<size_t>(channel)] = static_cast<uint8_t>(std::lround(clamped * 255.0f));
                }
                rgbaPixels[destinationBase + 3ull] = 255u;
            }
        }

        TextureSpecification textureSpecification{};
        textureSpecification.GenerateMipmaps = false;
        textureSpecification.MinFilter = TextureFilter::Linear;
        textureSpecification.MagFilter = TextureFilter::Linear;
        textureSpecification.WrapU = TextureWrap::ClampToEdge;
        textureSpecification.WrapV = TextureWrap::ClampToEdge;
        textureSpecification.FlipVerticallyOnLoad = false;

        std::shared_ptr<Texture2D> atlasTexture = Texture2D::CreateFromRGBA8(
            static_cast<uint32_t>(atlasWidth),
            static_cast<uint32_t>(atlasHeight),
            rgbaPixels.data(),
            textureSpecification);

        if (!atlasTexture)
        {
            LT_CORE_ERROR("Font: failed to create atlas texture for '{}'", fontPath);
            msdfgen::destroyFont(fontHandle);
            msdfgen::deinitializeFreetype(freetype);
            return nullptr;
        }

        auto font = std::make_shared<Font>();
        font->m_AtlasTexture = std::move(atlasTexture);
        font->m_PixelRange = static_cast<float>(atlasPacker.getPixelRange());
        font->m_EmSize = static_cast<float>(atlasPacker.getScale());
        font->m_AtlasWidth = static_cast<uint32_t>(atlasWidth);
        font->m_AtlasHeight = static_cast<uint32_t>(atlasHeight);

        const msdfgen::FontMetrics& metrics = fontGeometry.getMetrics();
        font->m_LineHeight = static_cast<float>(metrics.lineHeight);
        font->m_Ascender = static_cast<float>(metrics.ascenderY);
        font->m_Descender = static_cast<float>(metrics.descenderY);

        font->m_Glyphs.reserve(glyphs.size());
        for (const msdf_atlas::GlyphGeometry& glyph : glyphs)
        {
            Font::Glyph runtimeGlyph{};
            runtimeGlyph.Codepoint = static_cast<uint32_t>(glyph.getCodepoint());
            runtimeGlyph.Advance = static_cast<float>(glyph.getAdvance());

            double planeLeft = 0.0;
            double planeBottom = 0.0;
            double planeRight = 0.0;
            double planeTop = 0.0;
            glyph.getQuadPlaneBounds(planeLeft, planeBottom, planeRight, planeTop);
            runtimeGlyph.PlaneMin = glm::vec2(static_cast<float>(planeLeft), static_cast<float>(planeBottom));
            runtimeGlyph.PlaneMax = glm::vec2(static_cast<float>(planeRight), static_cast<float>(planeTop));
            runtimeGlyph.HasGeometry = !glyph.isWhitespace();

            double atlasLeft = 0.0;
            double atlasBottom = 0.0;
            double atlasRight = 0.0;
            double atlasTop = 0.0;
            glyph.getQuadAtlasBounds(atlasLeft, atlasBottom, atlasRight, atlasTop);
            runtimeGlyph.AtlasMin = glm::vec2(
                static_cast<float>(atlasLeft / static_cast<double>(atlasWidth)),
                static_cast<float>(atlasBottom / static_cast<double>(atlasHeight)));
            runtimeGlyph.AtlasMax = glm::vec2(
                static_cast<float>(atlasRight / static_cast<double>(atlasWidth)),
                static_cast<float>(atlasTop / static_cast<double>(atlasHeight)));

            font->m_Glyphs[runtimeGlyph.Codepoint] = runtimeGlyph;
        }

        for (const msdf_atlas::GlyphGeometry& left : glyphs)
        {
            for (const msdf_atlas::GlyphGeometry& right : glyphs)
            {
                double kerningAdvance = 0.0;
                if (fontGeometry.getAdvance(kerningAdvance, left.getCodepoint(), right.getCodepoint()))
                {
                    font->m_KerningPairs[{ static_cast<uint32_t>(left.getCodepoint()), static_cast<uint32_t>(right.getCodepoint()) }] =
                        static_cast<float>(kerningAdvance);
                }
            }
        }

        if (font->m_Glyphs.find(kFallbackCodepoint) == font->m_Glyphs.end())
        {
            LT_CORE_WARN("Font '{}': fallback '?' glyph missing; unsupported characters will be skipped", fontPath);
        }

        msdfgen::destroyFont(fontHandle);
        msdfgen::deinitializeFreetype(freetype);

        {
            std::scoped_lock cacheLock(g_FontCacheMutex);
            g_FontCache[resolvedFontPath] = font;
        }
        return font;
    }

    const Font::Glyph* Font::GetGlyph(uint32_t codepoint) const
    {
        auto it = m_Glyphs.find(codepoint);
        if (it != m_Glyphs.end())
        {
            return &it->second;
        }

        it = m_Glyphs.find(kFallbackCodepoint);
        return it != m_Glyphs.end() ? &it->second : nullptr;
    }

    float Font::GetKerningAdvance(uint32_t leftCodepoint, uint32_t rightCodepoint) const
    {
        const auto it = m_KerningPairs.find({ leftCodepoint, rightCodepoint });
        if (it != m_KerningPairs.end())
        {
            return it->second;
        }

        const Glyph* leftGlyph = GetGlyph(leftCodepoint);
        if (!leftGlyph)
        {
            return 0.0f;
        }

        return leftGlyph->Advance;
    }
}
