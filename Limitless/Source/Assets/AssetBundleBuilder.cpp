#include "Assets/AssetBundleBuilder.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"
#include "Assets/ImageDecode.h"
#include "Assets/Cooking/CookedTexture2DFormat.h"
#include "Assets/ShaderStageParsing.h"
#include "Assets/Cooking/CookedShaderStagesFormat.h"
#include "Assets/TextureSpecificationJson.h"

#include "Core/Compression/ZstdCompression.h"
#include "Core/Hash/XxHash64.h"
#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Limitless::Assets
{
    using json = nlohmann::json;

    static constexpr uint32_t kAssetBundleCacheVersion = 1;
    static constexpr uint32_t kAssetCookingVersion = 1;

    static const char* ToString(AssetBundleCompression compression)
    {
        switch (compression)
        {
        case AssetBundleCompression::None: return "None";
        case AssetBundleCompression::Zstd: return "Zstd";
        default: return "None";
        }
    }

    static const char* ToString(AssetBundlePayloadFormat format)
    {
        switch (format)
        {
        case AssetBundlePayloadFormat::Raw: return "Raw";
        case AssetBundlePayloadFormat::CookedTexture2D: return "CookedTexture2D";
        case AssetBundlePayloadFormat::CookedShaderStages: return "CookedShaderStages";
        default: return "Raw";
        }
    }

    static bool EndsWith(const std::string& s, const std::string& suffix)
    {
        if (s.size() < suffix.size()) return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    static std::optional<AssetType> GuessTypeFromPath(const std::filesystem::path& path)
    {
        const std::string name = path.filename().string();
        const std::string ext = path.extension().string();

        if (EndsWith(name, ".material.json")) return AssetType::Material;
        if (EndsWith(name, ".inputactions.json")) return AssetType::InputActions;
        if (ext == ".glsl") return AssetType::Shader;

        // Textures
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
            ext == ".hdr" || ext == ".psd" || ext == ".gif" || ext == ".ppm" || ext == ".pnm")
        {
            return AssetType::Texture2D;
        }

        // Audio
        if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
        {
            return AssetType::AudioClip;
        }

        return std::nullopt;
    }

    static Result<std::vector<uint8_t>> ReadAllBytes(const std::filesystem::path& file)
    {
        std::ifstream in(file, std::ios::in | std::ios::binary | std::ios::ate);
        if (!in.is_open())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::FileNotFound, "Failed to open file: " + file.string());
        }

        const std::streamsize size = in.tellg();
        if (size <= 0)
        {
            return Result<std::vector<uint8_t>>(ErrorCode::FileCorrupted, "File is empty: " + file.string());
        }

        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes;
        bytes.resize(static_cast<size_t>(size));
        in.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!in.good())
        {
            return Result<std::vector<uint8_t>>(ErrorCode::FileCorrupted, "Failed to read file: " + file.string());
        }

        return bytes;
    }

    static Result<void> AtomicWriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        try
        {
            if (path.has_parent_path())
            {
                std::filesystem::create_directories(path.parent_path());
            }

            const std::filesystem::path tmp = path.string() + ".tmp";
            {
                std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!out.is_open())
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to write temp file: " + tmp.string());
                }
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                out.flush();
            }

            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                ec.clear();
                std::filesystem::remove(path, ec);
                ec.clear();
                std::filesystem::rename(tmp, path, ec);
                if (ec)
                {
                    return Result<void>(ErrorCode::FileAccessDenied, "Failed to replace file: " + ec.message());
                }
            }
        }
        catch (const std::exception& e)
        {
            return Result<void>(ErrorCode::FileAccessDenied, std::string("AtomicWriteFile failed: ") + e.what());
        }

        return Result<void>();
    }

    static Result<void> AtomicWriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        const std::vector<uint8_t> bytes(text.begin(), text.end());
        return AtomicWriteFile(path, bytes);
    }

    struct AssetBundleCacheEntry
    {
        uint64_t ContentHash64 = 0;
        AssetBundlePayloadFormat PayloadFormat = AssetBundlePayloadFormat::Raw;
        AssetBundleCompression Compression = AssetBundleCompression::None;
        uint64_t UncompressedSize = 0;
        uint64_t StoredSize = 0;
        std::string BlobFile;
    };

    static AssetBundleCompression AssetBundleCompressionFromString(const std::string& value)
    {
        if (value == "Zstd") return AssetBundleCompression::Zstd;
        return AssetBundleCompression::None;
    }

    static AssetBundlePayloadFormat AssetBundlePayloadFormatFromString(const std::string& value)
    {
        if (value == "CookedTexture2D") return AssetBundlePayloadFormat::CookedTexture2D;
        if (value == "CookedShaderStages") return AssetBundlePayloadFormat::CookedShaderStages;
        return AssetBundlePayloadFormat::Raw;
    }

    static Result<json> TryReadJsonFile(const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return json::object();
        }

        try
        {
            std::ifstream in(path, std::ios::in | std::ios::binary);
            if (!in.is_open())
            {
                return Result<json>(ErrorCode::FileAccessDenied, "Failed to open json file: " + path.string());
            }

            json root;
            in >> root;
            return root;
        }
        catch (const std::exception& e)
        {
            return Result<json>(ErrorCode::FileCorrupted, std::string("Failed to parse json file: ") + e.what());
        }
    }

    static std::unordered_map<std::string, AssetBundleCacheEntry> ParseCacheEntries(const json& root)
    {
        std::unordered_map<std::string, AssetBundleCacheEntry> out;

        if (!root.is_object())
        {
            return out;
        }

        const uint32_t version = root.value("version", 0u);
        if (version != kAssetBundleCacheVersion)
        {
            return out;
        }

        if (!root.contains("entries") || !root["entries"].is_object())
        {
            return out;
        }

        for (auto it = root["entries"].begin(); it != root["entries"].end(); ++it)
        {
            if (!it.value().is_object())
            {
                continue;
            }

            const std::string guid = it.key();
            const auto& e = it.value();

            AssetBundleCacheEntry entry;
            entry.ContentHash64 = e.value("contentHash64", 0ull);
            entry.PayloadFormat = AssetBundlePayloadFormatFromString(e.value("payloadFormat", "Raw"));
            entry.Compression = AssetBundleCompressionFromString(e.value("compression", "None"));
            entry.UncompressedSize = e.value("uncompressedSize", 0ull);
            entry.StoredSize = e.value("storedSize", 0ull);
            entry.BlobFile = e.value("blobFile", "");

            if (guid.empty() || entry.StoredSize == 0 || entry.BlobFile.empty())
            {
                continue;
            }

            out.emplace(guid, std::move(entry));
        }

        return out;
    }

    static json BuildCacheJson(const std::unordered_map<std::string, AssetBundleCacheEntry>& entries)
    {
        json root;
        root["version"] = kAssetBundleCacheVersion;
        root["entries"] = json::object();

        for (const auto& kv : entries)
        {
            const std::string& guid = kv.first;
            const AssetBundleCacheEntry& e = kv.second;

            json j;
            j["contentHash64"] = e.ContentHash64;
            j["payloadFormat"] = ToString(e.PayloadFormat);
            j["compression"] = ToString(e.Compression);
            j["uncompressedSize"] = e.UncompressedSize;
            j["storedSize"] = e.StoredSize;
            j["blobFile"] = e.BlobFile;

            root["entries"][guid] = std::move(j);
        }

        return root;
    }

    Result<void> AssetBundleBuilder::BuildProjectAssetBundle()
    {
        return BuildProjectAssetBundle(Settings{});
    }

    Result<void> AssetBundleBuilder::BuildProjectAssetBundle(Settings settings)
    {
        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<void>(rootResult.GetError());
        }

        const std::filesystem::path root = rootResult.GetValue();
        const std::filesystem::path outDir = root / "Build" / "AssetBundle";
        return BuildAtOutputDirectory(outDir, settings);
    }

    Result<void> AssetBundleBuilder::BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory)
    {
        return BuildAssetBundleToDirectory(outputDirectory, Settings{});
    }

    Result<void> AssetBundleBuilder::BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory, Settings settings)
    {
        return BuildAtOutputDirectory(outputDirectory, settings);
    }

    Result<void> AssetBundleBuilder::BuildAtOutputDirectory(const std::filesystem::path& outputDirectory, Settings settings)
    {
        if (outputDirectory.empty())
        {
            return Result<void>(ErrorCode::InvalidArgument, "AssetBundleBuilder: outputDirectory is empty");
        }

        const auto rootResult = FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            return Result<void>(rootResult.GetError());
        }

        const std::filesystem::path projectRoot = rootResult.GetValue();
        const std::filesystem::path assetsRoot = projectRoot / "Assets";
        if (!std::filesystem::exists(assetsRoot))
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetBundleBuilder: Assets/ directory not found: " + assetsRoot.string());
        }

        // Discover and import all known asset types under Assets/.
        size_t discovered = 0;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(assetsRoot, ec);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            const auto& filePath = it->path();
            if (filePath.extension() == ".meta")
            {
                continue;
            }

            auto typeOpt = GuessTypeFromPath(filePath);
            if (!typeOpt.has_value())
            {
                continue;
            }

            // Convert absolute file path -> Unity-style key "Assets/.."
            std::filesystem::path rel = std::filesystem::relative(filePath, projectRoot, ec);
            if (ec)
            {
                ec.clear();
                continue;
            }
            const std::string key = rel.generic_string();

            // Import (ensures GUID) while preserving existing importer settings.
            // IMPORTANT:
            // We must NOT clobber ImporterSettings here. The settings come from type-specific
            // importers (example: `TextureSpecification` from `AssetImporter<TextureAsset>`)
            // and they directly impact cooking results (filters/wrap/mips).
            nlohmann::json settings = nlohmann::json::object();
            const auto existingRecord = AssetDatabase::GetInstance().FindByKey(key);
            if (existingRecord.IsSuccess())
            {
                settings = existingRecord.GetValue().ImporterSettings;
            }
            (void)AssetDatabase::GetInstance().ImportOrUpdate(key, *typeOpt, settings);
            discovered++;
        }

        const auto records = AssetDatabase::GetInstance().GetAllRecords();
        if (records.empty())
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetBundleBuilder: AssetDatabase has no records (load/import failed)");
        }

        const std::filesystem::path cachePath = outputDirectory / "AssetBundleCache.json";
        const std::filesystem::path cacheDirectory = outputDirectory / "Cache";
        std::filesystem::create_directories(cacheDirectory, ec);
        if (ec)
        {
            LT_CORE_WARN("AssetBundleBuilder: failed to create cache directory '{}': {}", cacheDirectory.string(), ec.message());
            ec.clear();
        }

        auto cacheRootResult = TryReadJsonFile(cachePath);
        if (cacheRootResult.IsFailure())
        {
            LT_CORE_WARN("AssetBundleBuilder: failed to read cache file '{}': {}", cachePath.string(), cacheRootResult.GetError().GetErrorMessage());
        }

        std::unordered_map<std::string, AssetBundleCacheEntry> cacheEntries =
            cacheRootResult.IsSuccess() ? ParseCacheEntries(cacheRootResult.GetValue()) : std::unordered_map<std::string, AssetBundleCacheEntry>{};

        size_t cacheHits = 0;
        size_t cacheMisses = 0;
        bool zstdUnavailable = false;

        struct OutEntry
        {
            std::string Guid;
            std::string Key;
            AssetType Type = AssetType::Unknown;
            AssetBundlePayloadFormat PayloadFormat = AssetBundlePayloadFormat::Raw;
            AssetBundleCompression Compression = AssetBundleCompression::None;
            uint64_t Offset = 0;
            uint64_t Size = 0;
            uint64_t UncompressedSize = 0;
            uint64_t ContentHash64 = 0;
        };

        std::vector<uint8_t> data;
        data.reserve(8 * 1024 * 1024);

        std::vector<OutEntry> entries;
        entries.reserve(records.size());

        // Validate GUID uniqueness across bundled keys.
        // If two keys share the same GUID, `AssetManager` will GUID-dedup them at runtime,
        // which can manifest as "all quads show the wrong texture".
        std::unordered_map<std::string, std::string> firstKeyByGuid;
        firstKeyByGuid.reserve(records.size());

        for (const auto& r : records)
        {
            if (r.Guid.empty() || r.Key.empty() || r.ResolvedPath.empty())
            {
                continue;
            }

            // Only bundle known/ship-worthy types for now.
            if (r.Type != AssetType::Texture2D &&
                r.Type != AssetType::Shader &&
                r.Type != AssetType::Material &&
                r.Type != AssetType::InputActions &&
                r.Type != AssetType::AudioClip)
            {
                continue;
            }

            if (const auto it = firstKeyByGuid.find(r.Guid); it != firstKeyByGuid.end())
            {
                if (it->second != r.Key)
                {
                    return Result<void>(ErrorCode::ResourceCorrupted,
                        "AssetBundleBuilder: duplicate GUID detected (guid=" + r.Guid + ") for keys '" + it->second + "' and '" + r.Key + "'");
                }
            }
            else
            {
                firstKeyByGuid.emplace(r.Guid, r.Key);
            }

            const std::filesystem::path filePath = r.ResolvedPath;
            const auto bytesResult = ReadAllBytes(filePath);
            if (bytesResult.IsFailure())
            {
                LT_CORE_WARN("AssetBundleBuilder: skipping '{}' (read failed): {}", r.Key, bytesResult.GetError().GetErrorMessage());
                continue;
            }

            const auto& bytes = bytesResult.GetValue();

            // Default payload is raw source bytes.
            AssetBundlePayloadFormat payloadFormat = AssetBundlePayloadFormat::Raw;
            std::vector<uint8_t> payloadBytes = bytes;

            // Cook formats per type (P0: Texture2D).
            if (r.Type == AssetType::Texture2D)
            {
                const TextureSpecification textureSpec = TextureSpecificationFromImporterSettingsJson(r.ImporterSettings);

                auto decodedResult = DecodeToRGBA8FromMemory(bytes.data(), bytes.size(), r.Key, textureSpec.FlipVerticallyOnLoad);
                if (decodedResult.IsFailure())
                {
                    // stb_image can't decode ASCII PPM (P3) reliably across builds.
                    auto ppmFallback = TryDecodePpmP3ToRGBA8FromMemory(bytes.data(), bytes.size(), r.Key);
                    if (ppmFallback.IsSuccess())
                    {
                        auto img = ppmFallback.GetValue();
                        if (textureSpec.FlipVerticallyOnLoad)
                        {
                            FlipVerticalRGBA8(img);
                        }
                        decodedResult = img;
                    }
                }

                if (decodedResult.IsSuccess())
                {
                    const auto& decoded = decodedResult.GetValue();
                    const auto cookedResult = ::Limitless::Assets::Cooking::CookTexture2DFromRGBA8(decoded.Width, decoded.Height, decoded.Pixels.data(), textureSpec);
                    if (cookedResult.IsSuccess())
                    {
                        payloadBytes = cookedResult.GetValue();
                        payloadFormat = AssetBundlePayloadFormat::CookedTexture2D;
                    }
                    else
                    {
                        LT_CORE_WARN("AssetBundleBuilder: Texture2D cook failed for '{}' ({}). Falling back to raw payload.",
                            r.Key, cookedResult.GetError().GetErrorMessage());
                    }
                }
                else
                {
                    LT_CORE_WARN("AssetBundleBuilder: Texture2D decode failed for '{}' ({}). Falling back to raw payload.",
                        r.Key, decodedResult.GetError().GetErrorMessage());
                }
            }
            else if (r.Type == AssetType::Shader)
            {
                std::string nameOverride;
                if (r.ImporterSettings.is_object())
                {
                    nameOverride = r.ImporterSettings.value("name", std::string{});
                }

                const std::string fileText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                const auto parsedResult = ParseCombinedGlsl(r.Key, r.ResolvedPath, fileText, nameOverride);
                if (parsedResult.IsSuccess())
                {
                    auto parsed = parsedResult.GetValue();
                    (void)PrepareShaderStagesForActiveGraphicsAPI(parsed, r.Key);

                    ::Limitless::Assets::Cooking::CookedShaderStages cooked;
                    cooked.Name = parsed.Name;
                    cooked.Vertex = parsed.Vertex;
                    cooked.Fragment = parsed.Fragment;

                    const auto cookedBytesResult = ::Limitless::Assets::Cooking::CookShaderStagesToBytes(cooked);
                    if (cookedBytesResult.IsSuccess())
                    {
                        payloadBytes = cookedBytesResult.GetValue();
                        payloadFormat = AssetBundlePayloadFormat::CookedShaderStages;
                    }
                    else
                    {
                        LT_CORE_WARN("AssetBundleBuilder: Shader cook failed for '{}' ({}). Falling back to raw payload.",
                            r.Key, cookedBytesResult.GetError().GetErrorMessage());
                    }
                }
                else
                {
                    LT_CORE_WARN("AssetBundleBuilder: Shader parse failed for '{}' ({}). Falling back to raw payload.",
                        r.Key, parsedResult.GetError().GetErrorMessage());
                }
            }

            std::vector<uint8_t> storedBytes = payloadBytes;

            // Compute content hash: source bytes + importer settings + cooking version.
            ::Limitless::Hash::XxHash64::State hasher(0);
            hasher.Update(bytes.data(), bytes.size());

            const std::string settingsText = r.ImporterSettings.dump();
            hasher.Update(settingsText.data(), settingsText.size());

            hasher.Update(&kAssetCookingVersion, sizeof(kAssetCookingVersion));

            const uint64_t contentHash64 = hasher.Digest();

            OutEntry e;
            e.Guid = r.Guid;
            e.Key = r.Key;
            e.Type = r.Type;
            e.PayloadFormat = payloadFormat;
            e.ContentHash64 = contentHash64;

            e.Compression = AssetBundleCompression::None;
            e.UncompressedSize = static_cast<uint64_t>(payloadBytes.size());

            const AssetBundleCompression desiredCompression =
                (settings.Compression == AssetBundleCompression::Zstd && zstdUnavailable) ? AssetBundleCompression::None : settings.Compression;

            bool usedCache = false;
            const auto cacheIt = cacheEntries.find(e.Guid);
            if (cacheIt != cacheEntries.end())
            {
                const AssetBundleCacheEntry& cached = cacheIt->second;
                const bool settingsMatch =
                    cached.ContentHash64 == contentHash64 &&
                    cached.PayloadFormat == e.PayloadFormat &&
                    cached.Compression == desiredCompression;

                const std::filesystem::path cachedBlobPath = outputDirectory / cached.BlobFile;
                if (settingsMatch && std::filesystem::exists(cachedBlobPath))
                {
                    const auto cachedBytesResult = ReadAllBytes(cachedBlobPath);
                    if (cachedBytesResult.IsSuccess())
                    {
                        const auto& cachedBytes = cachedBytesResult.GetValue();
                        if (cached.StoredSize == cachedBytes.size())
                        {
                            storedBytes = cachedBytes;
                            e.Compression = cached.Compression;
                            e.UncompressedSize = cached.UncompressedSize;
                            usedCache = true;
                            cacheHits++;
                        }
                    }
                }
            }

            if (!usedCache)
            {
                cacheMisses++;

                // Apply compression if requested.
                e.Compression = AssetBundleCompression::None;
                if (desiredCompression == AssetBundleCompression::Zstd)
                {
                    const auto compressed = ::Limitless::Compression::ZstdCompression::Compress(payloadBytes.data(), payloadBytes.size(), settings.ZstdCompressionLevel);
                    if (compressed.IsSuccess())
                    {
                        storedBytes = compressed.GetValue();
                        e.Compression = AssetBundleCompression::Zstd;
                    }
                    else
                    {
                        if (compressed.GetError().GetCode() == ErrorCode::NotSupported)
                        {
                            zstdUnavailable = true;
                            LT_CORE_WARN("AssetBundleBuilder: Zstd is not enabled in this build (missing Vendor/Zstd libs). Falling back to uncompressed payloads.");
                        }
                        else
                        {
                            LT_CORE_WARN("AssetBundleBuilder: Zstd compression failed for '{}': {} (falling back to None)",
                                r.Key, compressed.GetError().GetErrorMessage());
                        }
                    }
                }

                // Write cache blob (stored bytes).
                const char* extension = (e.Compression == AssetBundleCompression::Zstd) ? ".zst" : ".bin";
                const std::string blobRel = std::string("Cache/") + e.Guid + extension;
                const std::filesystem::path blobAbs = outputDirectory / blobRel;
                (void)AtomicWriteFile(blobAbs, storedBytes);

                AssetBundleCacheEntry cacheEntry;
                cacheEntry.ContentHash64 = contentHash64;
                cacheEntry.PayloadFormat = e.PayloadFormat;
                cacheEntry.Compression = e.Compression;
                cacheEntry.UncompressedSize = e.UncompressedSize;
                cacheEntry.StoredSize = static_cast<uint64_t>(storedBytes.size());
                cacheEntry.BlobFile = blobRel;
                cacheEntries[e.Guid] = std::move(cacheEntry);
            }

            if (r.Type == AssetType::Texture2D || r.Type == AssetType::Shader)
            {
                LT_CORE_INFO("AssetBundleBuilder: entry '{}' guid={} payloadFormat={} compression={} storedBytes={} uncompressedBytes={}",
                    r.Key,
                    e.Guid,
                    ToString(e.PayloadFormat),
                    ToString(e.Compression),
                    static_cast<uint64_t>(storedBytes.size()),
                    e.UncompressedSize);
            }

            e.Offset = static_cast<uint64_t>(data.size());
            e.Size = static_cast<uint64_t>(storedBytes.size());

            data.insert(data.end(), storedBytes.begin(), storedBytes.end());
            entries.push_back(std::move(e));
        }

        if (entries.empty())
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetBundleBuilder: no bundle entries produced");
        }

        // Write files.
        const std::filesystem::path dataPath = outputDirectory / "AssetBundle.bin";
        const std::filesystem::path manifestPath = outputDirectory / "AssetBundleManifest.json";

        json manifest;
        manifest["version"] = 2;
        manifest["dataFile"] = "AssetBundle.bin";
        manifest["entries"] = json::array();

        for (const auto& e : entries)
        {
            json j;
            j["guid"] = e.Guid;
            j["key"] = e.Key;
            j["type"] = ToString(e.Type);
            j["payloadFormat"] = ToString(e.PayloadFormat);
            j["compression"] = ToString(e.Compression);
            j["uncompressedSize"] = e.UncompressedSize;
            j["contentHash64"] = e.ContentHash64;
            j["offset"] = e.Offset;
            j["size"] = e.Size;
            manifest["entries"].push_back(std::move(j));
        }

        const auto dataWrite = AtomicWriteFile(dataPath, data);
        if (dataWrite.IsFailure())
        {
            return dataWrite;
        }

        const auto manifestWrite = AtomicWriteTextFile(manifestPath, manifest.dump(2));
        if (manifestWrite.IsFailure())
        {
            return manifestWrite;
        }

        // Update incremental build cache.
        const json cacheOut = BuildCacheJson(cacheEntries);
        const auto cacheWrite = AtomicWriteTextFile(cachePath, cacheOut.dump(2));
        if (cacheWrite.IsFailure())
        {
            LT_CORE_WARN("AssetBundleBuilder: failed to write cache file '{}': {}", cachePath.string(), cacheWrite.GetError().GetErrorMessage());
        }

        LT_CORE_INFO("AssetBundleBuilder: built bundle: entries={} bytes={} discoveredFiles={} output='{}'",
            entries.size(), data.size(), discovered, outputDirectory.string());
        LT_CORE_INFO("AssetBundleBuilder: incremental cache: hits={} misses={} cacheFile='{}'", cacheHits, cacheMisses, cachePath.string());

        return Result<void>();
    }
}

