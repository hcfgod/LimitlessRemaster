#pragma once

#include "Assets/AssetTreeWatcher.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // IncrementalScriptCompiler
    //
    // Watches the project's Assets/ directory for .cpp / .h changes and
    // signals the main thread to trigger an incremental script build.
    //
    // Typical flow:
    //   1. AssetTreeWatcher detects a .cpp/.h write in Assets/
    //   2. After a debounce window, HasPendingScriptChanges() returns true
    //   3. EditorLayer::OnUpdate calls BuildProjectScripts() which mirrors,
    //      invokes the (now-incremental) build script, and copies the DLL
    //   4. ScriptCoreModuleRuntime::Update detects the new DLL and hot-reloads
    //
    // Thread safety: public methods are safe to call from any thread.
    // HasPendingScriptChanges() should be polled from the main/render thread.
    // -------------------------------------------------------------------------
    class IncrementalScriptCompiler final
    {
    public:
        static IncrementalScriptCompiler& GetInstance();

        // Initialize with engine/project paths.  Starts the file watcher.
        void Initialize(const std::filesystem::path& engineRoot,
                        const std::filesystem::path& projectRoot);

        void Shutdown();

        // Poll from the main thread each frame.  Returns true (once) when
        // the file watcher has detected script changes and the debounce
        // window has elapsed.  The caller should then trigger the build.
        bool HasPendingScriptChanges();

        // Auto-recompile toggle.  When disabled the watcher still runs but
        // HasPendingScriptChanges() always returns false.
        void SetAutoRecompileEnabled(bool enabled) { m_AutoRecompileEnabled.store(enabled, std::memory_order_relaxed); }
        bool IsAutoRecompileEnabled() const { return m_AutoRecompileEnabled.load(std::memory_order_relaxed); }

        // Debounce window (ms) for coalescing rapid saves.
        void SetDebounceMs(int milliseconds) { m_DebounceMs = milliseconds; }

    private:
        IncrementalScriptCompiler() = default;
        ~IncrementalScriptCompiler();

        IncrementalScriptCompiler(const IncrementalScriptCompiler&) = delete;
        IncrementalScriptCompiler& operator=(const IncrementalScriptCompiler&) = delete;

        void OnFileChanged(const std::filesystem::path& changedPath);
        void SnapshotExistingScriptFiles(const std::filesystem::path& assetsDir);

        std::filesystem::path m_EngineRoot;
        std::filesystem::path m_ProjectRoot;

        std::unique_ptr<Assets::AssetTreeWatcher> m_ScriptWatcher;

        std::atomic<bool> m_AutoRecompileEnabled{true};
        std::atomic<bool> m_Initialized{false};

        // Written on the watcher thread, read on the main thread.
        std::atomic<bool> m_ScriptChangeDetected{false};
        std::atomic<int64_t> m_LastChangeTimestampMs{0};
        std::mutex m_ScriptContentHashesMutex;
        std::unordered_map<std::string, size_t> m_ScriptContentHashes;
        int m_DebounceMs = 500;
    };
}
