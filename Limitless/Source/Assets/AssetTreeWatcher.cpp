#include "Assets/AssetTreeWatcher.h"

#include "Core/Debug/Log.h"

#include <vector>

#ifdef LT_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <string>
#elif defined(LT_PLATFORM_LINUX)
    #include <sys/inotify.h>
    #include <poll.h>
    #include <unistd.h>
    #include <cerrno>
    #include <unordered_map>
    #include <string>
    #include <system_error>
#elif defined(LT_PLATFORM_MACOS)
    #include <CoreServices/CoreServices.h>
    #include <string>
#endif

namespace Limitless::Assets
{
    // -----------------------------------------------------------------------------
    // Backend interface
    // -----------------------------------------------------------------------------
    struct AssetTreeWatcherBackend
    {
        virtual ~AssetTreeWatcherBackend() = default;
        virtual bool Initialize(const std::filesystem::path& root) = 0;
        virtual void RequestStop() = 0;
        virtual void Run(std::atomic<bool>& stopRequested, const std::function<void(const std::filesystem::path&)>& emitPath) = 0;

        virtual void SetPollInterval(std::chrono::milliseconds) {}
    };

    // -----------------------------------------------------------------------------
    // Polling backend (portable fallback)
    // -----------------------------------------------------------------------------
    class PollingAssetTreeWatcherBackend final : public AssetTreeWatcherBackend
    {
    public:
        bool Initialize(const std::filesystem::path& root) override
        {
            m_Root = root;
            m_LastWriteTimes.clear();

            // Seed initial state without firing callbacks.
            std::unordered_map<std::string, std::filesystem::file_time_type> snapshot;
            snapshot.reserve(4096);

            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(m_Root, ec);
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

                const auto& path = it->path();
                if (path.extension() == ".meta")
                {
                    continue;
                }

                const auto time = std::filesystem::last_write_time(path, ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                snapshot[path.string()] = time;
            }

            m_LastWriteTimes = std::move(snapshot);
            return true;
        }

        void RequestStop() override {}

        void SetPollInterval(std::chrono::milliseconds interval) override
        {
            m_PollInterval = interval;
        }

        void Run(std::atomic<bool>& stopRequested, const std::function<void(const std::filesystem::path&)>& emitPath) override
        {
            while (!stopRequested.load(std::memory_order_relaxed))
            {
                try
                {
                    ScanOnce(emitPath);
                }
                catch (const std::exception& e)
                {
                    LT_CORE_ERROR("AssetTreeWatcher(Polling): scan error: {}", e.what());
                }
                catch (...)
                {
                    LT_CORE_ERROR("AssetTreeWatcher(Polling): scan error (unknown)");
                }

                std::this_thread::sleep_for(m_PollInterval);
            }
        }

    private:
        void ScanOnce(const std::function<void(const std::filesystem::path&)>& emitPath)
        {
            if (m_Root.empty() || !std::filesystem::exists(m_Root))
            {
                return;
            }

            std::unordered_map<std::string, std::filesystem::file_time_type> current;
            current.reserve(4096);

            std::error_code ec;
            for (auto it = std::filesystem::recursive_directory_iterator(m_Root, ec);
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

                const auto& path = it->path();
                if (path.extension() == ".meta")
                {
                    continue;
                }

                const auto time = std::filesystem::last_write_time(path, ec);
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                current[path.string()] = time;
            }

            // Compare with previous snapshot.
            auto previous = m_LastWriteTimes;
            m_LastWriteTimes = current;

            // Modified/added.
            for (const auto& [pathString, time] : current)
            {
                const auto itPrev = previous.find(pathString);
                if (itPrev == previous.end() || itPrev->second != time)
                {
                    emitPath(std::filesystem::path(pathString));
                }
            }

            // Removed.
            for (const auto& [pathString, _] : previous)
            {
                if (current.find(pathString) == current.end())
                {
                    emitPath(std::filesystem::path(pathString));
                }
            }
        }

    private:
        std::filesystem::path m_Root;
        std::chrono::milliseconds m_PollInterval{250};
        std::unordered_map<std::string, std::filesystem::file_time_type> m_LastWriteTimes;
    };

#ifdef LT_PLATFORM_WINDOWS
    // -----------------------------------------------------------------------------
    // Windows backend: ReadDirectoryChangesW
    // -----------------------------------------------------------------------------
    class WindowsAssetTreeWatcherBackend final : public AssetTreeWatcherBackend
    {
    public:
        ~WindowsAssetTreeWatcherBackend() override
        {
            RequestStop();
            if (m_OverlappedEvent)
            {
                CloseHandle(m_OverlappedEvent);
                m_OverlappedEvent = nullptr;
            }
            if (m_Directory != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_Directory);
                m_Directory = INVALID_HANDLE_VALUE;
            }
        }

        bool Initialize(const std::filesystem::path& root) override
        {
            m_Root = root;

            const std::wstring rootWide = m_Root.wstring();
            m_Directory = CreateFileW(
                rootWide.c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                nullptr);

            if (m_Directory == INVALID_HANDLE_VALUE)
            {
                LT_CORE_WARN("AssetTreeWatcher(Windows): CreateFileW failed for '{}'", m_Root.string());
                return false;
            }

            m_OverlappedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!m_OverlappedEvent)
            {
                LT_CORE_WARN("AssetTreeWatcher(Windows): CreateEvent failed");
                return false;
            }

            return true;
        }

        void RequestStop() override
        {
            if (m_Directory != INVALID_HANDLE_VALUE)
            {
                // Cancel pending I/O to wake the thread.
                CancelIoEx(m_Directory, nullptr);
            }
            if (m_OverlappedEvent)
            {
                SetEvent(m_OverlappedEvent);
            }
        }

        void Run(std::atomic<bool>& stopRequested, const std::function<void(const std::filesystem::path&)>& emitPath) override
        {
            constexpr DWORD kBufferSize = 64 * 1024;
            std::vector<uint8_t> buffer;
            buffer.resize(kBufferSize);

            const DWORD notifyFilter =
                FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_CREATION;

            while (!stopRequested.load(std::memory_order_relaxed))
            {
                OVERLAPPED ov{};
                ov.hEvent = m_OverlappedEvent;
                ResetEvent(m_OverlappedEvent);

                DWORD bytesReturned = 0;
                const BOOL ok = ReadDirectoryChangesW(
                    m_Directory,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    TRUE,
                    notifyFilter,
                    &bytesReturned,
                    &ov,
                    nullptr);

                if (!ok)
                {
                    // Avoid tight loop if the handle becomes invalid.
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }

                // Wait for completion or cancellation.
                WaitForSingleObject(m_OverlappedEvent, INFINITE);

                if (stopRequested.load(std::memory_order_relaxed))
                {
                    break;
                }

                DWORD transferred = 0;
                if (!GetOverlappedResult(m_Directory, &ov, &transferred, FALSE) || transferred == 0)
                {
                    continue;
                }

                const uint8_t* ptr = buffer.data();
                while (true)
                {
                    const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);
                    const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
                    const std::filesystem::path full = m_Root / std::filesystem::path(name);
                    emitPath(full);

                    if (info->NextEntryOffset == 0)
                    {
                        break;
                    }
                    ptr += info->NextEntryOffset;
                }
            }
        }

    private:
        std::filesystem::path m_Root;
        HANDLE m_Directory = INVALID_HANDLE_VALUE;
        HANDLE m_OverlappedEvent = nullptr;
    };
#endif

#ifdef LT_PLATFORM_LINUX
    // -----------------------------------------------------------------------------
    // Linux backend: inotify
    // -----------------------------------------------------------------------------
    class LinuxAssetTreeWatcherBackend final : public AssetTreeWatcherBackend
    {
    public:
        ~LinuxAssetTreeWatcherBackend() override
        {
            RequestStop();
        }

        bool Initialize(const std::filesystem::path& root) override
        {
            m_Root = root;
            m_Fd = inotify_init1(IN_NONBLOCK);
            if (m_Fd < 0)
            {
                LT_CORE_WARN("AssetTreeWatcher(Linux): inotify_init1 failed: {}", std::strerror(errno));
                return false;
            }

            m_WdToPath.clear();
            m_PathToWd.clear();

            AddWatchRecursive(m_Root);
            return true;
        }

        void RequestStop() override
        {
            if (m_Fd >= 0)
            {
                close(m_Fd);
                m_Fd = -1;
            }
        }

        void Run(std::atomic<bool>& stopRequested, const std::function<void(const std::filesystem::path&)>& emitPath) override
        {
            std::vector<char> buffer;
            buffer.resize(64 * 1024);

            while (!stopRequested.load(std::memory_order_relaxed))
            {
                if (m_Fd < 0)
                {
                    return;
                }

                pollfd pfd{};
                pfd.fd = m_Fd;
                pfd.events = POLLIN;

                const int r = poll(&pfd, 1, 250);
                if (r <= 0)
                {
                    continue;
                }

                const ssize_t len = read(m_Fd, buffer.data(), buffer.size());
                if (len <= 0)
                {
                    continue;
                }

                size_t i = 0;
                while (i < static_cast<size_t>(len))
                {
                    const auto* ev = reinterpret_cast<const inotify_event*>(buffer.data() + i);
                    i += sizeof(inotify_event) + ev->len;

                    const auto it = m_WdToPath.find(ev->wd);
                    if (it == m_WdToPath.end())
                    {
                        continue;
                    }

                    const std::filesystem::path base = it->second;
                    std::filesystem::path full = base;
                    if (ev->len > 0)
                    {
                        full /= std::string(ev->name);
                    }

                    // If a directory was created/moved into place, add watches.
                    if ((ev->mask & IN_ISDIR) != 0)
                    {
                        if ((ev->mask & IN_CREATE) != 0 || (ev->mask & IN_MOVED_TO) != 0)
                        {
                            AddWatchRecursive(full);
                        }
                    }

                    emitPath(full);
                }
            }
        }

    private:
        void AddWatchRecursive(const std::filesystem::path& dir)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
            {
                return;
            }

            AddWatchDir(dir);

            for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
                 it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (ec)
                {
                    ec.clear();
                    continue;
                }

                if (it->is_directory(ec))
                {
                    AddWatchDir(it->path());
                }
            }
        }

        void AddWatchDir(const std::filesystem::path& dir)
        {
            const std::string p = dir.string();
            if (m_PathToWd.find(p) != m_PathToWd.end())
            {
                return;
            }

            constexpr uint32_t mask =
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO |
                IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF;

            const int wd = inotify_add_watch(m_Fd, p.c_str(), mask);
            if (wd < 0)
            {
                return;
            }

            m_PathToWd[p] = wd;
            m_WdToPath[wd] = p;
        }

    private:
        std::filesystem::path m_Root;
        int m_Fd = -1;
        std::unordered_map<int, std::string> m_WdToPath;
        std::unordered_map<std::string, int> m_PathToWd;
    };
#endif

#ifdef LT_PLATFORM_MACOS
    // -----------------------------------------------------------------------------
    // macOS backend: FSEvents
    // -----------------------------------------------------------------------------
    class MacAssetTreeWatcherBackend final : public AssetTreeWatcherBackend
    {
    public:
        ~MacAssetTreeWatcherBackend() override
        {
            RequestStop();
        }

        bool Initialize(const std::filesystem::path& root) override
        {
            m_Root = root;
            return true;
        }

        void RequestStop() override
        {
            if (m_RunLoop)
            {
                CFRunLoopStop(m_RunLoop);
            }
        }

        void Run(std::atomic<bool>& stopRequested, const std::function<void(const std::filesystem::path&)>& emitPath) override
        {
            m_Emit = &emitPath;
            m_RunLoop = CFRunLoopGetCurrent();

            CFStringRef path = CFStringCreateWithCString(nullptr, m_Root.string().c_str(), kCFStringEncodingUTF8);
            CFArrayRef pathsToWatch = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&path), 1, &kCFTypeArrayCallBacks);

            FSEventStreamContext ctx{};
            ctx.info = this;

            m_Stream = FSEventStreamCreate(
                nullptr,
                &MacAssetTreeWatcherBackend::Callback,
                &ctx,
                pathsToWatch,
                kFSEventStreamEventIdSinceNow,
                0.2,
                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);

            CFRelease(pathsToWatch);
            CFRelease(path);

            if (!m_Stream)
            {
                return;
            }

            FSEventStreamScheduleWithRunLoop(m_Stream, m_RunLoop, kCFRunLoopDefaultMode);
            FSEventStreamStart(m_Stream);

            // Run until stopped.
            while (!stopRequested.load(std::memory_order_relaxed))
            {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, true);
            }

            FSEventStreamStop(m_Stream);
            FSEventStreamInvalidate(m_Stream);
            FSEventStreamRelease(m_Stream);
            m_Stream = nullptr;
            m_RunLoop = nullptr;
            m_Emit = nullptr;
        }

    private:
        static void Callback(ConstFSEventStreamRef, void* info, size_t numEvents, void* eventPaths, const FSEventStreamEventFlags*, const FSEventStreamEventId*)
        {
            auto* self = static_cast<MacAssetTreeWatcherBackend*>(info);
            if (!self || !self->m_Emit)
            {
                return;
            }

            char** paths = static_cast<char**>(eventPaths);
            for (size_t i = 0; i < numEvents; ++i)
            {
                (*self->m_Emit)(std::filesystem::path(paths[i]));
            }
        }

    private:
        std::filesystem::path m_Root;
        FSEventStreamRef m_Stream = nullptr;
        CFRunLoopRef m_RunLoop = nullptr;
        const std::function<void(const std::filesystem::path&)>* m_Emit = nullptr;
    };
#endif

    static std::unique_ptr<AssetTreeWatcherBackend> CreateBackend()
    {
#ifdef LT_PLATFORM_WINDOWS
        return std::make_unique<WindowsAssetTreeWatcherBackend>();
#elif defined(LT_PLATFORM_LINUX)
        return std::make_unique<LinuxAssetTreeWatcherBackend>();
#elif defined(LT_PLATFORM_MACOS)
        return std::make_unique<MacAssetTreeWatcherBackend>();
#else
        return std::make_unique<PollingAssetTreeWatcherBackend>();
#endif
    }

    AssetTreeWatcher::AssetTreeWatcher() = default;

    AssetTreeWatcher::~AssetTreeWatcher()
    {
        Stop();
    }

    void AssetTreeWatcher::Start(const std::filesystem::path& rootDirectory, ChangeCallback callback)
    {
        Stop();

        m_Root = rootDirectory;
        m_Callback = std::move(callback);
        m_StopRequested.store(false, std::memory_order_relaxed);
        m_Running.store(true, std::memory_order_relaxed);

        m_Backend = CreateBackend();
        if (!m_Backend || !m_Backend->Initialize(m_Root))
        {
            LT_CORE_WARN("AssetTreeWatcher: native backend init failed, falling back to polling");
            m_Backend = std::make_unique<PollingAssetTreeWatcherBackend>();
            (void)m_Backend->Initialize(m_Root);
        }
        m_Backend->SetPollInterval(m_PollInterval);

        m_Thread = std::thread(&AssetTreeWatcher::ThreadMain, this);
        LT_CORE_INFO("AssetTreeWatcher: started for '{}'", m_Root.string());
    }

    void AssetTreeWatcher::Stop()
    {
        if (!m_Running.exchange(false, std::memory_order_relaxed))
        {
            return;
        }

        m_StopRequested.store(true, std::memory_order_relaxed);
        if (m_Backend)
        {
            m_Backend->RequestStop();
        }
        if (m_Thread.joinable())
        {
            m_Thread.join();
        }

        m_Backend.reset();
        LT_CORE_INFO("AssetTreeWatcher: stopped");
    }

    void AssetTreeWatcher::ThreadMain()
    {
        const auto emit = [this](const std::filesystem::path& p) {
            EmitPathIfRelevant(p);
        };

        if (m_Backend)
        {
            m_Backend->Run(m_StopRequested, emit);
        }
    }

    void AssetTreeWatcher::EmitPathIfRelevant(const std::filesystem::path& path)
    {
        if (!m_Callback)
        {
            return;
        }

        // Skip meta changes from triggering asset rebuilds directly; we generate these ourselves.
        if (path.extension() == ".meta")
        {
            return;
        }

        m_Callback(path);
    }
}

