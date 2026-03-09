#include "Assets/AudioClipAsset.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetLoadProgress.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Audio/Decoders/FfmpegAudioDecoder.h"

#include "Core/Debug/Log.h"

#include <future>
#include <vector>

namespace Limitless::Assets
{
    Async::Task<AudioClipAsset::Ptr> AudioClipAsset::LoadAsync(const std::string& assetPath)
    {
        return LoadAsync(assetPath, Settings{});
    }

    Async::Task<AudioClipAsset::Ptr> AudioClipAsset::LoadAsync(const std::string& assetPath, const Settings& settings)
    {
        return LoadAsync(assetPath, settings, AssetLoadCoordinator::GetGeneration());
    }

    Async::Task<AudioClipAsset::Ptr> AudioClipAsset::LoadAsync(const std::string& assetPath, const Settings& settings, uint64_t generation)
    {
        // CPU-only async: decode on AsyncIO worker thread.
        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([assetPath, settings, generation, promise = std::move(promise)]() mutable -> void {
            try
            {
                AssetLoadProgress::SetProgress(assetPath, 0.05f, "Resolving...");

                if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
                {
                    AssetLoadProgress::ClearProgress(assetPath);
                    promise.set_value(nullptr);
                    return;
                }

                // Bundle-first loading (shipping mode).
                bool fromBundle = false;
                std::vector<uint8_t> bundleBytes;
                std::string guid;
                std::string resolvedPath;
                std::string debugName = assetPath;

                auto& bundle = AssetBundle::GetInstance();
                if (bundle.IsEnabled() && bundle.IsLoaded())
                {
                    const auto entry = bundle.FindEntryByKey(assetPath);
                    if (entry.has_value())
                    {
                        const auto bytesResult = bundle.ReadAllBytesByKey(assetPath);
                        if (bytesResult.IsSuccess())
                        {
                            fromBundle = true;
                            bundleBytes = bytesResult.GetValue();
                            guid = entry->Guid;
                            AssetLoadProgress::SetProgress(assetPath, 0.15f, "Reading from bundle...");
                        }
                    }
                }

                if (!fromBundle)
                {
                    const auto resolvedPathResult = ResolveAssetKeyToPath(assetPath);
                    if (resolvedPathResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(assetPath);
                        LT_CORE_ERROR("AudioClipAsset::LoadAsync: failed to resolve key '{}': {}",
                                      assetPath, resolvedPathResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    resolvedPath = resolvedPathResult.GetValue().string();
                    AssetLoadProgress::SetProgress(assetPath, 0.15f, "Reading...");
                    debugName = resolvedPath;

                    auto guidResult = LoadOrCreateGuid(resolvedPath);
                    if (guidResult.IsFailure())
                    {
                        AssetLoadProgress::ClearProgress(assetPath);
                        LT_CORE_ERROR("AudioClipAsset::LoadAsync: failed GUID/meta for '{}': {}",
                                      resolvedPath, guidResult.GetError().GetErrorMessage());
                        promise.set_value(nullptr);
                        return;
                    }

                    guid = guidResult.GetValue();
                }

                AssetLoadProgress::SetProgress(assetPath, 0.40f, "Decoding audio...");

                Audio::Decoders::FfmpegAudioDecoder::DecodeSettings decodeSettings{};
                decodeSettings.TargetSampleRateHz = settings.TargetSampleRateHz;
                decodeSettings.TargetChannelCount = settings.TargetChannelCount;

                Result<std::shared_ptr<Audio::AudioClip>> decodedResult(ErrorCode::Unknown, "not decoded");
                if (fromBundle)
                {
                    decodedResult = Audio::Decoders::FfmpegAudioDecoder::DecodeFromMemory(bundleBytes.data(), bundleBytes.size(), debugName, decodeSettings);
                }
                else
                {
                    decodedResult = Audio::Decoders::FfmpegAudioDecoder::DecodeFromFile(resolvedPath, decodeSettings);
                }

                if (decodedResult.IsFailure())
                {
                    AssetLoadProgress::ClearProgress(assetPath);
                    LT_CORE_ERROR("AudioClipAsset::LoadAsync: decode failed for '{}': {}", debugName, decodedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                auto clip = decodedResult.GetValue();
                if (!clip)
                {
                    AssetLoadProgress::ClearProgress(assetPath);
                    LT_CORE_ERROR("AudioClipAsset::LoadAsync: decode returned null for '{}'", debugName);
                    promise.set_value(nullptr);
                    return;
                }

                AssetLoadProgress::ClearProgress(assetPath);
                auto asset = AssetManager::GetOrLoad<AudioClipAsset>(assetPath, [&]() -> Ptr {
                    return Ptr(new AudioClipAsset(assetPath, guid, clip, settings));
                });
                promise.set_value(std::move(asset));
            }
            catch (const std::exception& e)
            {
                AssetLoadProgress::ClearProgress(assetPath);
                LT_CORE_ERROR("AudioClipAsset::LoadAsync: exception while loading '{}': {}", assetPath, e.what());
                try { promise.set_value(nullptr); } catch (...) {}
            }
            catch (...)
            {
                AssetLoadProgress::ClearProgress(assetPath);
                LT_CORE_ERROR("AudioClipAsset::LoadAsync: unknown exception while loading '{}'", assetPath);
                try { promise.set_value(nullptr); } catch (...) {}
            }
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    AudioClipAsset::Ptr AudioClipAsset::LoadBlocking(const std::string& assetPath, const Settings& settings)
    {
        auto task = LoadAsync(assetPath, settings);
        task.Wait();
        return task.Get();
    }

    AudioClipAsset::Ptr AudioClipAsset::LoadBlocking(const std::string& assetPath)
    {
        return LoadBlocking(assetPath, Settings{});
    }

    bool AudioClipAsset::Reload()
    {
        const std::string key = GetKey();

        bool fromBundle = false;
        std::vector<uint8_t> bundleBytes;
        std::string resolvedPath;
        std::string debugName = key;

        auto& bundle = AssetBundle::GetInstance();
        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto entry = bundle.FindEntryByKey(key);
            if (entry.has_value())
            {
                const auto bytesResult = bundle.ReadAllBytesByKey(key);
                if (bytesResult.IsSuccess())
                {
                    fromBundle = true;
                    bundleBytes = bytesResult.GetValue();
                }
            }
        }

        if (!fromBundle)
        {
            const auto resolvedPathResult = ResolveAssetKeyToPath(key);
            if (resolvedPathResult.IsFailure())
            {
                LT_CORE_ERROR("AudioClipAsset::Reload: failed to resolve key '{}': {}", key, resolvedPathResult.GetError().GetErrorMessage());
                return false;
            }
            resolvedPath = resolvedPathResult.GetValue().string();
            debugName = resolvedPath;
        }

        Audio::Decoders::FfmpegAudioDecoder::DecodeSettings decodeSettings{};
        decodeSettings.TargetSampleRateHz = m_Settings.TargetSampleRateHz;
        decodeSettings.TargetChannelCount = m_Settings.TargetChannelCount;

        Result<std::shared_ptr<Audio::AudioClip>> decodedResult(ErrorCode::Unknown, "not decoded");
        if (fromBundle)
        {
            decodedResult = Audio::Decoders::FfmpegAudioDecoder::DecodeFromMemory(bundleBytes.data(), bundleBytes.size(), debugName, decodeSettings);
        }
        else
        {
            decodedResult = Audio::Decoders::FfmpegAudioDecoder::DecodeFromFile(resolvedPath, decodeSettings);
        }

        if (decodedResult.IsFailure())
        {
            LT_CORE_ERROR("AudioClipAsset::Reload: decode failed for '{}': {}", debugName, decodedResult.GetError().GetErrorMessage());
            return false;
        }

        m_Clip = decodedResult.GetValue();
        return m_Clip != nullptr;
    }
}

