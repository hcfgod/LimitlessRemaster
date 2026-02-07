#include "Assets/AssetBundleBuilder.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <vector>

namespace Limitless::Assets
{
    using json = nlohmann::json;

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

    Result<void> AssetBundleBuilder::BuildAssetBundleToDirectory(const std::filesystem::path& outputDirectory, Settings settings)
    {
        return BuildAtOutputDirectory(outputDirectory, settings);
    }

    Result<void> AssetBundleBuilder::BuildAtOutputDirectory(const std::filesystem::path& outputDirectory, Settings settings)
    {
        (void)settings;

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

            // Import (ensures GUID).
            (void)AssetDatabase::GetInstance().ImportOrUpdate(key, *typeOpt, nlohmann::json::object());
            discovered++;
        }

        const auto records = AssetDatabase::GetInstance().GetAllRecords();
        if (records.empty())
        {
            return Result<void>(ErrorCode::ResourceNotFound, "AssetBundleBuilder: AssetDatabase has no records (load/import failed)");
        }

        struct OutEntry
        {
            std::string Guid;
            std::string Key;
            AssetType Type = AssetType::Unknown;
            uint64_t Offset = 0;
            uint64_t Size = 0;
        };

        std::vector<uint8_t> data;
        data.reserve(8 * 1024 * 1024);

        std::vector<OutEntry> entries;
        entries.reserve(records.size());

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
                r.Type != AssetType::InputActions)
            {
                continue;
            }

            const std::filesystem::path filePath = r.ResolvedPath;
            const auto bytesResult = ReadAllBytes(filePath);
            if (bytesResult.IsFailure())
            {
                LT_CORE_WARN("AssetBundleBuilder: skipping '{}' (read failed): {}", r.Key, bytesResult.GetError().GetErrorMessage());
                continue;
            }

            const auto& bytes = bytesResult.GetValue();
            OutEntry e;
            e.Guid = r.Guid;
            e.Key = r.Key;
            e.Type = r.Type;
            e.Offset = static_cast<uint64_t>(data.size());
            e.Size = static_cast<uint64_t>(bytes.size());

            data.insert(data.end(), bytes.begin(), bytes.end());
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
        manifest["version"] = 1;
        manifest["dataFile"] = "AssetBundle.bin";
        manifest["entries"] = json::array();

        for (const auto& e : entries)
        {
            json j;
            j["guid"] = e.Guid;
            j["key"] = e.Key;
            j["type"] = ToString(e.Type);
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

        LT_CORE_INFO("AssetBundleBuilder: built bundle: entries={} bytes={} discoveredFiles={} output='{}'",
            entries.size(), data.size(), discovered, outputDirectory.string());

        return Result<void>();
    }
}

