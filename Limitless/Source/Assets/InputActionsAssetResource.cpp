#include "Assets/InputActionsAssetResource.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputActionAssetSerializer.h"

namespace Limitless::Assets
{
    Async::Task<InputActionsAssetResource::Ptr> InputActionsAssetResource::LoadAsync(const std::string& key, Settings settings)
    {
        const uint64_t generation = AssetLoadCoordinator::GetGeneration();

        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([key, settings, generation, promise = std::move(promise)]() mutable -> void {
            if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
            {
                promise.set_value(nullptr);
                return;
            }

            const auto resolvedResult = ResolveAssetKeyToPath(key);
            if (resolvedResult.IsFailure())
            {
                LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: failed to resolve key '{}': {}", key, resolvedResult.GetError().GetErrorMessage());
                promise.set_value(nullptr);
                return;
            }

            const std::string resolvedPath = resolvedResult.GetValue().string();

            const auto guidResult = LoadOrCreateGuid(resolvedPath, {{"key", key}, {"type", "InputActions"}});
            if (guidResult.IsFailure())
            {
                LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: meta GUID failed for '{}': {}", resolvedPath, guidResult.GetError().GetErrorMessage());
                promise.set_value(nullptr);
                return;
            }

            const std::string guid = guidResult.GetValue();

            auto asset = AssetManager::GetOrLoad<InputActionsAssetResource>(key, [&]() -> Ptr {
                // IMPORTANT:
                // Keep a stable InputActionAsset instance (Unity-style) and rebuild it in-place on reload.
                // This allows InputSystem and other holders of shared_ptr<InputActionAsset> to see updates
                // without pointer swaps.
                auto stable = std::make_shared<Limitless::InputActionAsset>();
                const auto loaded = Limitless::InputActionAssetSerializer::LoadInto(*stable, resolvedPath);
                if (loaded.IsFailure())
                {
                    LT_CORE_ERROR("InputActionsAssetResource::LoadAsync: load failed for '{}': {}", resolvedPath, loaded.GetError().GetErrorMessage());
                    return nullptr;
                }

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
        const auto resolvedResult = ResolveAssetKeyToPath(GetKey());
        if (resolvedResult.IsFailure())
        {
            return false;
        }
        m_ResolvedPath = resolvedResult.GetValue().string();

        if (!m_Value)
        {
            m_Value = std::make_shared<Limitless::InputActionAsset>();
        }

        const auto loaded = Limitless::InputActionAssetSerializer::LoadInto(*m_Value, m_ResolvedPath);
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

