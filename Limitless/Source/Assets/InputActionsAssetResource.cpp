#include "Assets/InputActionsAssetResource.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetLoadProgress.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputActionAssetSerializer.h"
#include "Platform/Platform.h"

#include <filesystem>
#include <fstream>

namespace Limitless::Assets
{
    static std::filesystem::path GetInputActionsUserOverridePathForKey(const std::string& key)
    {
        const std::string userData = Limitless::PlatformDetection::GetUserDataPath();
        if (userData.empty())
        {
            return {};
        }

        std::filesystem::path root(userData);
        root /= "InputActionsOverrides";

        std::filesystem::path rel(key);
        if (rel.is_absolute())
        {
            rel = rel.filename();
        }

        return root / rel;
    }

    static bool TryReadAllTextFile(const std::filesystem::path& path, std::string& outText)
    {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in.is_open())
        {
            return false;
        }

        outText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    Async::Task<InputActionsAssetResource::Ptr> InputActionsAssetResource::LoadAsync(const std::string& key, Settings settings)
    {
        const uint64_t generation = AssetLoadCoordinator::GetGeneration();

        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([key, settings, generation, promise = std::move(promise)]() mutable -> void {
            AssetLoadProgress::SetProgress(key, 0.05f, "Resolving...");

            if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
            {
                AssetLoadProgress::ClearProgress(key);
                promise.set_value(nullptr);
                return;
            }

            bool fromBundle = false;
            bool fromOverride = false;
            std::string resolvedPath;
            std::string sourceResolvedPath;
            std::string guid;
            std::string jsonText;

            auto& bundle = AssetBundle::GetInstance();

            // Shipping-safe override: if a user override file exists, it wins over both bundle and source assets.
            // This allows runtime rebinding persistence without writing into `Assets/...`.
            {
                const std::filesystem::path overridePath = GetInputActionsUserOverridePathForKey(key);
                if (!overridePath.empty() && std::filesystem::exists(overridePath))
                {
                    if (TryReadAllTextFile(overridePath, jsonText))
                    {
                        fromOverride = true;
                        resolvedPath = overridePath.string();
                    }
                }
            }

            if (bundle.IsEnabled() && bundle.IsLoaded())
            {
                const auto entry = bundle.FindEntryByKey(key);
                if (entry.has_value())
                {
                    // Prefer bundle GUID even if we load JSON from a user override.
                    guid = entry->Guid;

                    if (!fromOverride)
                    {
                        const auto textResult = bundle.ReadAllTextByKey(key);
                        if (textResult.IsSuccess())
                        {
                            fromBundle = true;
                            resolvedPath = "<AssetBundle>";
                            jsonText = textResult.GetValue();
                            AssetLoadProgress::SetProgress(key, 0.25f, "Reading from bundle...");
                        }
                    }
                }
            }

            if (!fromBundle && !fromOverride)
            {
                const auto resolvedResult = ResolveAssetKeyToPath(key);
                if (resolvedResult.IsFailure())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: failed to resolve key '{}': {}", key, resolvedResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                resolvedPath = resolvedResult.GetValue().string();
                sourceResolvedPath = resolvedPath;
                AssetLoadProgress::SetProgress(key, 0.25f, "Reading...");
            }
            else
            {
                // Even when loading content from an override file, asset identity must come from the base asset.
                // Try to resolve the source asset path so GUID comes from `Assets/.../*.meta`, not from a new
                // `.meta` created next to the user override file.
                const auto resolvedResult = ResolveAssetKeyToPath(key);
                if (resolvedResult.IsSuccess())
                {
                    sourceResolvedPath = resolvedResult.GetValue().string();
                }
            }

            // GUID resolution:
            // - Bundle provides GUID directly.
            // - Otherwise, source assets provide GUID via `.meta`.
            // - Override files do not define identity; they simply override the asset contents for this key.
            if (guid.empty())
            {
                if (sourceResolvedPath.empty())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR(
                        "InputActionsAssetResource::LoadAsync: cannot determine GUID for '{}' (override present={}, bundle enabled={}). "
                        "Identity must come from the base asset (.meta or bundle entry), not from an override file.",
                        key, fromOverride, (bundle.IsEnabled() && bundle.IsLoaded()));
                    promise.set_value(nullptr);
                    return;
                }

                const auto guidResult = LoadOrCreateGuid(sourceResolvedPath, {{"key", key}, {"type", "InputActions"}});
                if (guidResult.IsFailure())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: meta GUID failed for '{}': {}", sourceResolvedPath, guidResult.GetError().GetErrorMessage());
                    promise.set_value(nullptr);
                    return;
                }

                guid = guidResult.GetValue();
            }

            AssetLoadProgress::SetProgress(key, 0.60f, "Parsing...");

            auto asset = AssetManager::GetOrLoad<InputActionsAssetResource>(key, [&]() -> Ptr {
                // IMPORTANT:
                // Keep a stable InputActionAsset instance (Unity-style) and rebuild it in-place on reload.
                // This allows InputSystem and other holders of shared_ptr<InputActionAsset> to see updates
                // without pointer swaps.
                auto stable = std::make_shared<Limitless::InputActionAsset>();
                const auto loaded = (fromBundle || fromOverride)
                    ? Limitless::InputActionAssetSerializer::LoadIntoFromString(*stable, jsonText, fromOverride ? resolvedPath : key)
                    : Limitless::InputActionAssetSerializer::LoadInto(*stable, resolvedPath);
                if (loaded.IsFailure())
                {
                    AssetLoadProgress::ClearProgress(key);
                    LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: load failed for '{}': {}",
                        (fromBundle ? key : resolvedPath), loaded.GetError().GetErrorMessage());
                    return nullptr;
                }

                AssetLoadProgress::ClearProgress(key);
                Ptr created(new InputActionsAssetResource(key, guid, stable, settings));
                created->m_ResolvedPath = resolvedPath;
                created->m_Revision.fetch_add(1, std::memory_order_relaxed);
                return created;
            });

            promise.set_value(std::move(asset));
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    InputActionsAssetResource::Ptr InputActionsAssetResource::LoadBlocking(const std::string& key, Settings settings)
    {
        auto task = LoadAsync(key, std::move(settings));
        task.Wait();
        return task.Get();
    }

    bool InputActionsAssetResource::Reload()
    {
        const std::string key = GetKey();

        bool fromBundle = false;
        bool fromOverride = false;
        std::string jsonText;

        auto& bundle = AssetBundle::GetInstance();

        // User override wins over bundle/source.
        {
            const std::filesystem::path overridePath = GetInputActionsUserOverridePathForKey(key);
            if (!overridePath.empty() && std::filesystem::exists(overridePath))
            {
                if (TryReadAllTextFile(overridePath, jsonText))
                {
                    fromOverride = true;
                    m_ResolvedPath = overridePath.string();
                }
            }
        }

        if (bundle.IsEnabled() && bundle.IsLoaded())
        {
            const auto entry = bundle.FindEntryByKey(key);
            if (entry.has_value())
            {
                if (!fromOverride)
                {
                    const auto textResult = bundle.ReadAllTextByKey(key);
                    if (textResult.IsSuccess())
                    {
                        fromBundle = true;
                        m_ResolvedPath = "<AssetBundle>";
                        jsonText = textResult.GetValue();
                    }
                }
            }
        }

        if (!fromBundle && !fromOverride)
        {
            const auto resolvedResult = ResolveAssetKeyToPath(key);
            if (resolvedResult.IsFailure())
            {
                return false;
            }
            m_ResolvedPath = resolvedResult.GetValue().string();
        }

        // Keep identity stable across reloads:
        // - If the bundle is providing a GUID, `Asset` identity is already stable.
        // - Otherwise, ensure the base `.meta` exists for the source asset key (do NOT create `.meta` next to overrides).
        if (!fromBundle)
        {
            const auto resolvedResult = ResolveAssetKeyToPath(key);
            if (resolvedResult.IsSuccess())
            {
                const std::string sourceResolvedPath = resolvedResult.GetValue().string();
                (void)LoadOrCreateGuid(sourceResolvedPath, {{"key", key}, {"type", "InputActions"}});
            }
        }

        if (!m_Value)
        {
            m_Value = std::make_shared<Limitless::InputActionAsset>();
        }

        const auto loaded = (fromBundle || fromOverride)
            ? Limitless::InputActionAssetSerializer::LoadIntoFromString(*m_Value, jsonText, fromOverride ? m_ResolvedPath : key)
            : Limitless::InputActionAssetSerializer::LoadInto(*m_Value, m_ResolvedPath);
        if (loaded.IsFailure())
        {
            LT_CORE_ERROR("InputActionsAssetResource::Reload: load failed for '{}': {}", m_ResolvedPath, loaded.GetError().GetErrorMessage());
            return false;
        }

        m_Revision.fetch_add(1, std::memory_order_relaxed);

        // No deps for now.
        (void)AssetDatabase::GetInstance().SetDependencies(GetGuid(), {});
        return true;
    }
}

