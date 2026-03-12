#include "IncrementalScriptCompiler.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    static bool IsScriptSourceExtension(const std::filesystem::path& filePath)
    {
        const std::filesystem::path ext = filePath.extension();
        if (ext.empty())
            return false;

        // Normalise to lowercase for comparison.
        std::string extStr = ext.string();
        std::transform(extStr.begin(), extStr.end(), extStr.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        return extStr == ".cpp" || extStr == ".h" || extStr == ".hpp" || extStr == ".cs";
    }

    static int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    static std::string NormalizeTrackedPath(const std::filesystem::path& filePath)
    {
        std::error_code errorCode;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(filePath, errorCode);
        if (errorCode)
            normalized = filePath.lexically_normal();

        std::string key = normalized.generic_string();
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return key;
    }

    static bool TryReadFileContents(const std::filesystem::path& filePath, std::string& outContents)
    {
        outContents.clear();
        std::ifstream input(filePath, std::ios::in | std::ios::binary);
        if (!input.is_open())
            return false;

        outContents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    static size_t HashContents(const std::string& contents)
    {
        return std::hash<std::string>{}(contents);
    }

    // -------------------------------------------------------------------------
    // Singleton
    // -------------------------------------------------------------------------

    IncrementalScriptCompiler& IncrementalScriptCompiler::GetInstance()
    {
        static IncrementalScriptCompiler instance;
        return instance;
    }

    IncrementalScriptCompiler::~IncrementalScriptCompiler()
    {
        Shutdown();
    }

    // -------------------------------------------------------------------------
    // Initialize / Shutdown
    // -------------------------------------------------------------------------

    void IncrementalScriptCompiler::Initialize(const std::filesystem::path& engineRoot,
                                                const std::filesystem::path& projectRoot)
    {
        if (m_Initialized.load(std::memory_order_relaxed))
            return;

        m_EngineRoot = engineRoot;
        m_ProjectRoot = projectRoot;
        m_ScriptChangeDetected.store(false, std::memory_order_relaxed);
        m_LastChangeTimestampMs.store(0, std::memory_order_relaxed);

        // Start watching the project's Assets/ directory for script changes.
        const std::filesystem::path assetsDir = projectRoot / "Assets";
        std::error_code ec;
        if (std::filesystem::is_directory(assetsDir, ec))
        {
            SnapshotExistingScriptFiles(assetsDir);
            m_ScriptWatcher = std::make_unique<Assets::AssetTreeWatcher>();
            m_ScriptWatcher->Start(assetsDir, [this](const std::filesystem::path& changedPath) {
                OnFileChanged(changedPath);
            });

            LT_CORE_INFO("IncrementalScriptCompiler: Watching '{}' for script changes.", assetsDir.string());
        }
        else
        {
            LT_WARN("IncrementalScriptCompiler: Assets directory '{}' not found, file watcher not started.", assetsDir.string());
        }

        m_Initialized.store(true, std::memory_order_relaxed);

        LT_CORE_INFO("IncrementalScriptCompiler: Initialized (engine='{}', project='{}')",
                      engineRoot.string(), projectRoot.string());
    }

    void IncrementalScriptCompiler::Shutdown()
    {
        if (!m_Initialized.load(std::memory_order_relaxed))
            return;

        if (m_ScriptWatcher)
        {
            m_ScriptWatcher->Stop();
            m_ScriptWatcher.reset();
        }

        m_Initialized.store(false, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_ScriptContentHashesMutex);
            m_ScriptContentHashes.clear();
        }
        LT_CORE_INFO("IncrementalScriptCompiler: Shutdown complete.");
    }

    // -------------------------------------------------------------------------
    // File watcher callback  (called on the watcher thread)
    // -------------------------------------------------------------------------

    void IncrementalScriptCompiler::SnapshotExistingScriptFiles(const std::filesystem::path& assetsDir)
    {
        std::unordered_map<std::string, size_t> scannedHashes;
        std::error_code errorCode;
        for (std::filesystem::recursive_directory_iterator iterator(assetsDir, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
             iterator != end;
             iterator.increment(errorCode))
        {
            if (errorCode)
            {
                errorCode.clear();
                continue;
            }

            if (!iterator->is_regular_file(errorCode) || errorCode)
            {
                errorCode.clear();
                continue;
            }

            const std::filesystem::path currentPath = iterator->path();
            if (!IsScriptSourceExtension(currentPath))
                continue;

            std::string contents;
            if (!TryReadFileContents(currentPath, contents))
                continue;

            scannedHashes[NormalizeTrackedPath(currentPath)] = HashContents(contents);
        }

        std::lock_guard<std::mutex> lock(m_ScriptContentHashesMutex);
        m_ScriptContentHashes = std::move(scannedHashes);
    }

    void IncrementalScriptCompiler::OnFileChanged(const std::filesystem::path& changedPath)
    {
        if (!IsScriptSourceExtension(changedPath))
            return;

        const std::string normalizedPath = NormalizeTrackedPath(changedPath);
        std::error_code errorCode;
        const bool fileExists = std::filesystem::exists(changedPath, errorCode);
        if (errorCode)
            return;

        if (!fileExists)
        {
            bool removedExistingHash = false;
            {
                std::lock_guard<std::mutex> lock(m_ScriptContentHashesMutex);
                removedExistingHash = (m_ScriptContentHashes.erase(normalizedPath) > 0);
            }

            if (!removedExistingHash)
                return;

            m_LastChangeTimestampMs.store(NowMs(), std::memory_order_relaxed);
            m_ScriptChangeDetected.store(true, std::memory_order_release);
            return;
        }

        std::string contents;
        if (!TryReadFileContents(changedPath, contents))
            return;

        const size_t newHash = HashContents(contents);
        {
            std::lock_guard<std::mutex> lock(m_ScriptContentHashesMutex);
            const auto existingHash = m_ScriptContentHashes.find(normalizedPath);
            if (existingHash != m_ScriptContentHashes.end() && existingHash->second == newHash)
                return;
            m_ScriptContentHashes[normalizedPath] = newHash;
        }

        m_LastChangeTimestampMs.store(NowMs(), std::memory_order_relaxed);
        m_ScriptChangeDetected.store(true, std::memory_order_release);
    }

    // -------------------------------------------------------------------------
    // Main-thread poll
    // -------------------------------------------------------------------------

    bool IncrementalScriptCompiler::HasPendingScriptChanges()
    {
        if (!m_Initialized.load(std::memory_order_relaxed))
            return false;

        if (!m_AutoRecompileEnabled.load(std::memory_order_relaxed))
            return false;

        if (!m_ScriptChangeDetected.load(std::memory_order_acquire))
            return false;

        // Debounce: only fire after no new changes for m_DebounceMs.
        const int64_t lastChange = m_LastChangeTimestampMs.load(std::memory_order_relaxed);
        const int64_t now = NowMs();
        if (now - lastChange < static_cast<int64_t>(m_DebounceMs))
            return false;

        // Consume the flag so we only fire once per batch of changes.
        m_ScriptChangeDetected.store(false, std::memory_order_relaxed);
        return true;
    }
}
