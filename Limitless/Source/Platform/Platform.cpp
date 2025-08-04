#include "Platform.h"
#include "Core/Debug/Log.h"
#include "Core/Error.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <cstring>

// Platform-specific includes
#ifdef LT_PLATFORM_WINDOWS
    #include <windows.h>
    #include <psapi.h>
    #include <intrin.h>
    #include <direct.h>
    #include <shlobj.h>
    #include <sysinfoapi.h>
    #include <memoryapi.h>
#elif defined(LT_PLATFORM_MACOS)
    #include <sys/sysctl.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <mach/mach_time.h>
    #include <dlfcn.h>
    #include <pwd.h>
    #include <sys/utsname.h>
    #include <stdlib.h>
    #include <signal.h>
    #include <pthread.h>
    #include <sys/malloc.h>
#elif defined(LT_PLATFORM_LINUX)
    #include <sys/sysinfo.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <dlfcn.h>
    #include <pwd.h>
    #include <sys/utsname.h>
    #include <cpuid.h>
    #include <fstream>
    #include <stdlib.h>
    #include <signal.h>
    #include <pthread.h>
#endif

namespace Limitless
{
    // Static member initialization
    PlatformInfo PlatformDetection::s_PlatformInfo;
    bool PlatformDetection::s_Initialized = false;

    const PlatformInfo& PlatformDetection::GetPlatformInfo()
    {
        if (!s_Initialized)
        {
            Initialize();
        }
        return s_PlatformInfo;
    }

    void PlatformDetection::Initialize()
    {
        if (s_Initialized)
            return;

        try
        {
            DetectPlatform();
            DetectArchitecture();
            DetectCompiler();
            DetectOS();
            DetectCapabilities();
            DetectPaths();
            DetectGraphicsAPIs();

            s_Initialized = true;
            
            LT_CORE_INFO("Platform Detection Initialized:");
            LT_CORE_INFO("  Platform: {} ({})", s_PlatformInfo.platformName, GetPlatformString());
            LT_CORE_INFO("  Architecture: {} ({})", s_PlatformInfo.architectureName, GetArchitectureString());
            LT_CORE_INFO("  Compiler: {} {} ({})", s_PlatformInfo.compilerName, s_PlatformInfo.compilerVersion, GetCompilerString());
            LT_CORE_INFO("  OS: {} {} ({})", s_PlatformInfo.osName, s_PlatformInfo.osVersion, GetOSString());
            LT_CORE_INFO("  CPU Cores: {}", s_PlatformInfo.capabilities.cpuCount);
            LT_CORE_INFO("  Total Memory: {} MB", s_PlatformInfo.capabilities.totalMemory / (1024 * 1024));
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = fmt::format("Failed to initialize platform detection: {}", e.what());
            PlatformError error(errorMsg, std::source_location::current());
            error.SetFunctionName("PlatformDetection::Initialize");
            error.SetClassName("PlatformDetection");
            error.SetModuleName("Platform");
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_PLATFORM_ERROR(errorMsg);
        }
    }

    void PlatformDetection::RefreshCapabilities()
    {
        DetectCapabilities();
        DetectGraphicsAPIs();
    }
    
    bool PlatformDetection::IsInitialized()
    {
        return s_Initialized;
    }

    void PlatformDetection::DetectPlatform()
    {
        #ifdef LT_PLATFORM_WINDOWS
            s_PlatformInfo.platform = Platform::Windows;
            s_PlatformInfo.platformName = "Windows";
        #elif defined(LT_PLATFORM_MACOS)
            s_PlatformInfo.platform = Platform::macOS;
            s_PlatformInfo.platformName = "macOS";
        #elif defined(LT_PLATFORM_LINUX)
            s_PlatformInfo.platform = Platform::Linux;
            s_PlatformInfo.platformName = "Linux";
        #else
            s_PlatformInfo.platform = Platform::Unknown;
            s_PlatformInfo.platformName = "Unknown";
        #endif
    }

    void PlatformDetection::DetectArchitecture()
    {
        #ifdef LT_ARCHITECTURE_X64
            s_PlatformInfo.architecture = Architecture::x64;
            s_PlatformInfo.architectureName = "x64";
        #elif defined(LT_ARCHITECTURE_X86)
            s_PlatformInfo.architecture = Architecture::x86;
            s_PlatformInfo.architectureName = "x86";
        #elif defined(LT_ARCHITECTURE_ARM64)
            s_PlatformInfo.architecture = Architecture::ARM64;
            s_PlatformInfo.architectureName = "ARM64";
        #elif defined(LT_ARCHITECTURE_ARM32)
            s_PlatformInfo.architecture = Architecture::ARM32;
            s_PlatformInfo.architectureName = "ARM32";
        #else
            s_PlatformInfo.architecture = Architecture::Unknown;
            s_PlatformInfo.architectureName = "Unknown";
        #endif
    }

    void PlatformDetection::DetectCompiler()
    {
        #ifdef LT_COMPILER_MSVC
            s_PlatformInfo.compiler = Compiler::MSVC;
            s_PlatformInfo.compilerName = "MSVC";
            s_PlatformInfo.compilerVersion = std::to_string(_MSC_VER);
        #elif defined(LT_COMPILER_GCC)
            s_PlatformInfo.compiler = Compiler::GCC;
            s_PlatformInfo.compilerName = "GCC";
            s_PlatformInfo.compilerVersion = std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
        #elif defined(LT_COMPILER_APPLE_CLANG)
            s_PlatformInfo.compiler = Compiler::AppleClang;
            s_PlatformInfo.compilerName = "AppleClang";
            s_PlatformInfo.compilerVersion = std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
        #elif defined(LT_COMPILER_CLANG)
            s_PlatformInfo.compiler = Compiler::Clang;
            s_PlatformInfo.compilerName = "Clang";
            s_PlatformInfo.compilerVersion = std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
        #else
            s_PlatformInfo.compiler = Compiler::Unknown;
            s_PlatformInfo.compilerName = "Unknown";
            s_PlatformInfo.compilerVersion = "Unknown";
        #endif
    }

    void PlatformDetection::DetectOS()
    {
        #ifdef LT_PLATFORM_WINDOWS
            OSVERSIONINFOEX osvi;
            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
            
            #pragma warning(push)
            #pragma warning(disable:4996)
            GetVersionEx((OSVERSIONINFO*)&osvi);
            #pragma warning(pop)
            
            s_PlatformInfo.osName = "Windows";
            s_PlatformInfo.osVersion = std::to_string(osvi.dwMajorVersion) + "." + std::to_string(osvi.dwMinorVersion);
            s_PlatformInfo.osBuild = std::to_string(osvi.dwBuildNumber);
            
        #elif defined(LT_PLATFORM_MACOS)
            s_PlatformInfo.osName = "macOS";
            
            // Get macOS version
            char version[256];
            size_t size = sizeof(version);
            if (sysctlbyname("kern.osrelease", version, &size, nullptr, 0) == 0)
            {
                s_PlatformInfo.osVersion = version;
            }
            
            // Get build number
            char build[256];
            size = sizeof(build);
            if (sysctlbyname("kern.osversion", build, &size, nullptr, 0) == 0)
            {
                s_PlatformInfo.osBuild = build;
            }
            
        #elif defined(LT_PLATFORM_LINUX)
            struct utsname uts;
            if (uname(&uts) == 0)
            {
                s_PlatformInfo.osName = uts.sysname;
                s_PlatformInfo.osVersion = uts.release;
                s_PlatformInfo.osBuild = uts.version;
            }
            else
            {
                s_PlatformInfo.osName = "Linux";
                s_PlatformInfo.osVersion = "Unknown";
                s_PlatformInfo.osBuild = "Unknown";
            }
        #else
            s_PlatformInfo.osName = "Unknown";
            s_PlatformInfo.osVersion = "Unknown";
            s_PlatformInfo.osBuild = "Unknown";
        #endif
    }

    void PlatformDetection::DetectCapabilities()
    {
        // CPU count
        #ifdef LT_PLATFORM_WINDOWS
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            s_PlatformInfo.capabilities.cpuCount = sysInfo.dwNumberOfProcessors;
        #elif defined(LT_PLATFORM_MACOS)
            int cpuCount;
            size_t cpuSize = sizeof(cpuCount);
            if (sysctlbyname("hw.ncpu", &cpuCount, &cpuSize, nullptr, 0) == 0)
            {
                s_PlatformInfo.capabilities.cpuCount = static_cast<uint32_t>(cpuCount);
            }
        #elif defined(LT_PLATFORM_LINUX)
            s_PlatformInfo.capabilities.cpuCount = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
        #endif

        // Memory information
        #ifdef LT_PLATFORM_WINDOWS
            MEMORYSTATUSEX memInfo;
            memInfo.dwLength = sizeof(MEMORYSTATUSEX);
            if (GlobalMemoryStatusEx(&memInfo))
            {
                s_PlatformInfo.capabilities.totalMemory = memInfo.ullTotalPhys;
                s_PlatformInfo.capabilities.availableMemory = memInfo.ullAvailPhys;
            }
        #elif defined(LT_PLATFORM_MACOS)
            int64_t totalMem;
            size_t memSize = sizeof(totalMem);
            if (sysctlbyname("hw.memsize", &totalMem, &memSize, nullptr, 0) == 0)
            {
                s_PlatformInfo.capabilities.totalMemory = static_cast<uint64_t>(totalMem);
                
                // Get available memory
                vm_size_t pageSize;
                host_page_size(mach_host_self(), &pageSize);
                
                vm_statistics64_data_t vmStats;
                mach_msg_type_number_t infoCount = sizeof(vmStats) / sizeof(natural_t);
                if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vmStats, &infoCount) == KERN_SUCCESS)
                {
                    uint64_t freeMem = static_cast<uint64_t>(vmStats.free_count) * pageSize;
                    s_PlatformInfo.capabilities.availableMemory = freeMem;
                }
            }
        #elif defined(LT_PLATFORM_LINUX)
            struct sysinfo si;
            if (sysinfo(&si) == 0)
            {
                s_PlatformInfo.capabilities.totalMemory = static_cast<uint64_t>(si.totalram) * si.mem_unit;
                s_PlatformInfo.capabilities.availableMemory = static_cast<uint64_t>(si.freeram) * si.mem_unit;
            }
        #endif

        // CPU instruction set detection
        #ifdef LT_PLATFORM_WINDOWS
            int cpuInfo[4];
            __cpuid(cpuInfo, 1);
            
            s_PlatformInfo.capabilities.hasSSE2 = (cpuInfo[3] & (1 << 26)) != 0;
            s_PlatformInfo.capabilities.hasSSE3 = (cpuInfo[2] & (1 << 0)) != 0;
            s_PlatformInfo.capabilities.hasSSE4_1 = (cpuInfo[2] & (1 << 19)) != 0;
            s_PlatformInfo.capabilities.hasSSE4_2 = (cpuInfo[2] & (1 << 20)) != 0;
            
            // Check for AVX
            __cpuid(cpuInfo, 7);
            s_PlatformInfo.capabilities.hasAVX = (cpuInfo[1] & (1 << 5)) != 0;
            s_PlatformInfo.capabilities.hasAVX2 = (cpuInfo[1] & (1 << 28)) != 0;
            s_PlatformInfo.capabilities.hasAVX512 = (cpuInfo[1] & (1 << 16)) != 0;
            
        #elif defined(LT_PLATFORM_LINUX)
            unsigned int eax, ebx, ecx, edx;
            
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
            {
                s_PlatformInfo.capabilities.hasSSE2 = (edx & (1 << 26)) != 0;
                s_PlatformInfo.capabilities.hasSSE3 = (ecx & (1 << 0)) != 0;
                s_PlatformInfo.capabilities.hasSSE4_1 = (ecx & (1 << 19)) != 0;
                s_PlatformInfo.capabilities.hasSSE4_2 = (ecx & (1 << 20)) != 0;
            }
            
            if (__get_cpuid(7, &eax, &ebx, &ecx, &edx))
            {
                s_PlatformInfo.capabilities.hasAVX = (ebx & (1 << 5)) != 0;
                s_PlatformInfo.capabilities.hasAVX2 = (ebx & (1 << 28)) != 0;
                s_PlatformInfo.capabilities.hasAVX512 = (ebx & (1 << 16)) != 0;
            }
        #endif

        // ARM-specific capabilities
        #if defined(LT_ARCHITECTURE_ARM64) || defined(LT_ARCHITECTURE_ARM32)
            s_PlatformInfo.capabilities.hasNEON = true;
        #endif

        // PowerPC capabilities (if applicable)
        #ifdef __powerpc__
            s_PlatformInfo.capabilities.hasAltiVec = true;
        #endif
    }

    void PlatformDetection::DetectPaths()
    {
        // Executable path
        #ifdef LT_PLATFORM_WINDOWS
            char path[MAX_PATH];
            GetModuleFileNameA(nullptr, path, MAX_PATH);
            s_PlatformInfo.executablePath = path;
        #elif defined(LT_PLATFORM_MACOS) || defined(LT_PLATFORM_LINUX)
            char path[PATH_MAX];
            ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
            if (len != -1)
            {
                path[len] = '\0';
                s_PlatformInfo.executablePath = path;
            }
        #endif

        // Working directory
        #ifdef LT_PLATFORM_WINDOWS
            char cwd[MAX_PATH];
            if (_getcwd(cwd, MAX_PATH) != nullptr)
            {
                s_PlatformInfo.workingDirectory = cwd;
            }
        #elif defined(LT_PLATFORM_MACOS) || defined(LT_PLATFORM_LINUX)
            char cwd[PATH_MAX];
            if (getcwd(cwd, PATH_MAX) != nullptr)
            {
                s_PlatformInfo.workingDirectory = cwd;
            }
        #endif

        // User data path
        #ifdef LT_PLATFORM_WINDOWS
            char appData[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
            {
                s_PlatformInfo.userDataPath = std::string(appData) + "\\Limitless";
            }
        #elif defined(LT_PLATFORM_MACOS)
            const char* home = getenv("HOME");
            if (home)
            {
                s_PlatformInfo.userDataPath = std::string(home) + "/Library/Application Support/Limitless";
            }
        #elif defined(LT_PLATFORM_LINUX)
            const char* home = getenv("HOME");
            if (home)
            {
                s_PlatformInfo.userDataPath = std::string(home) + "/.local/share/Limitless";
            }
        #endif

        // Temp path
        #ifdef LT_PLATFORM_WINDOWS
            char tempPath[MAX_PATH];
            if (GetTempPathA(MAX_PATH, tempPath) != 0)
            {
                s_PlatformInfo.tempPath = tempPath;
            }
        #elif defined(LT_PLATFORM_MACOS) || defined(LT_PLATFORM_LINUX)
            const char* temp = getenv("TMPDIR");
            if (temp)
            {
                s_PlatformInfo.tempPath = temp;
            }
            else
            {
                s_PlatformInfo.tempPath = "/tmp";
            }
        #endif

        // System path
        #ifdef LT_PLATFORM_WINDOWS
            char systemPath[MAX_PATH];
            if (GetSystemDirectoryA(systemPath, MAX_PATH) != 0)
            {
                s_PlatformInfo.systemPath = systemPath;
            }
        #elif defined(LT_PLATFORM_MACOS)
            s_PlatformInfo.systemPath = "/System";
        #elif defined(LT_PLATFORM_LINUX)
            s_PlatformInfo.systemPath = "/usr";
        #endif
    }

    void PlatformDetection::DetectGraphicsAPIs()
    {
        // This would typically involve checking for available graphics APIs
        // For now, we'll set basic defaults based on platform
        
        #ifdef LT_PLATFORM_WINDOWS
            s_PlatformInfo.capabilities.hasDirectX = true;
            s_PlatformInfo.capabilities.hasOpenGL = true;
            s_PlatformInfo.capabilities.hasVulkan = true;
        #elif defined(LT_PLATFORM_MACOS)
            s_PlatformInfo.capabilities.hasMetal = true;
            s_PlatformInfo.capabilities.hasOpenGL = true;
            s_PlatformInfo.capabilities.hasVulkan = false; // macOS doesn't support Vulkan natively
        #elif defined(LT_PLATFORM_LINUX)
            s_PlatformInfo.capabilities.hasOpenGL = true;
            s_PlatformInfo.capabilities.hasVulkan = true;
        #endif
    }

    std::string PlatformDetection::GetPlatformString()
    {
        return s_PlatformInfo.platformName;
    }

    std::string PlatformDetection::GetArchitectureString()
    {
        return s_PlatformInfo.architectureName;
    }

    std::string PlatformDetection::GetCompilerString()
    {
        return s_PlatformInfo.compilerName + " " + s_PlatformInfo.compilerVersion;
    }

    std::string PlatformDetection::GetOSString()
    {
        return s_PlatformInfo.osName + " " + s_PlatformInfo.osVersion;
    }

    // PlatformUtils implementation
    namespace PlatformUtils
    {
        std::string GetPathSeparator()
        {
            #ifdef LT_PLATFORM_WINDOWS
                return "\\";
            #else
                return "/";
            #endif
        }

        std::string NormalizePath(const std::string& path)
        {
            std::filesystem::path fsPath(path);
            return fsPath.lexically_normal().string();
        }

        std::string JoinPath(const std::string& path1, const std::string& path2)
        {
            std::filesystem::path fsPath1(path1);
            std::filesystem::path fsPath2(path2);
            return (fsPath1 / fsPath2).string();
        }

        std::string GetDirectoryName(const std::string& path)
        {
            std::filesystem::path fsPath(path);
            return fsPath.parent_path().string();
        }

        std::string GetFileName(const std::string& path)
        {
            std::filesystem::path fsPath(path);
            return fsPath.filename().string();
        }

        std::string GetFileExtension(const std::string& path)
        {
            std::filesystem::path fsPath(path);
            return fsPath.extension().string();
        }

        std::optional<std::string> GetEnvironmentVariable(const std::string& name)
        {
            #ifdef LT_PLATFORM_WINDOWS
                char* value = nullptr;
                size_t size = 0;
                if (_dupenv_s(&value, &size, name.c_str()) == 0 && value != nullptr)
                {
                    std::string result(value);
                    free(value);
                    return result;
                }
            #else
                const char* value = getenv(name.c_str());
                if (value != nullptr)
                {
                    return std::string(value);
                }
            #endif
            return std::nullopt;
        }

        bool SetEnvironmentVariable(const std::string& name, const std::string& value)
        {
            #ifdef LT_PLATFORM_WINDOWS
                return _putenv_s(name.c_str(), value.c_str()) == 0;
            #else
                return setenv(name.c_str(), value.c_str(), 1) == 0;
            #endif
        }

        uint32_t GetCurrentProcessId()
        {
            #ifdef LT_PLATFORM_WINDOWS
                return static_cast<uint32_t>(::GetCurrentProcessId());
            #else
                return static_cast<uint32_t>(getpid());
            #endif
        }

        uint32_t GetCurrentThreadId()
        {
            #ifdef LT_PLATFORM_WINDOWS
                return static_cast<uint32_t>(::GetCurrentThreadId());
            #elif defined(LT_PLATFORM_MACOS)
                // On macOS, pthread_t is a pointer type, so we need to get a unique ID differently
                uint64_t threadId;
                pthread_threadid_np(pthread_self(), &threadId);
                return static_cast<uint32_t>(threadId);
            #else
                return static_cast<uint32_t>(pthread_self());
            #endif
        }

        void Sleep(uint32_t milliseconds)
        {
            #ifdef LT_PLATFORM_WINDOWS
                ::Sleep(milliseconds);
            #else
                usleep(milliseconds * 1000);
            #endif
        }

        void* LoadLibrary(const std::string& path)
        {
            #ifdef LT_PLATFORM_WINDOWS
                return LoadLibraryA(path.c_str());
            #else
                return dlopen(path.c_str(), RTLD_LAZY);
            #endif
        }

        void* GetProcAddress(void* library, const std::string& name)
        {
            #ifdef LT_PLATFORM_WINDOWS
                return ::GetProcAddress(static_cast<HMODULE>(library), name.c_str());
            #else
                return dlsym(library, name.c_str());
            #endif
        }

        void FreeLibrary(void* library)
        {
            #ifdef LT_PLATFORM_WINDOWS
                ::FreeLibrary(static_cast<HMODULE>(library));
            #else
                dlclose(library);
            #endif
        }

        void* AllocateAligned(size_t size, size_t alignment)
        {
            #ifdef LT_PLATFORM_WINDOWS
                return _aligned_malloc(size, alignment);
            #else
                void* ptr = nullptr;
                if (posix_memalign(&ptr, alignment, size) == 0)
                {
                    return ptr;
                }
                return nullptr;
            #endif
        }

        void FreeAligned(void* ptr)
        {
            #ifdef LT_PLATFORM_WINDOWS
                _aligned_free(ptr);
            #else
                free(ptr);
            #endif
        }

        uint64_t GetHighResolutionTime()
        {
            #ifdef LT_PLATFORM_WINDOWS
                LARGE_INTEGER frequency, counter;
                QueryPerformanceFrequency(&frequency);
                QueryPerformanceCounter(&counter);
                return static_cast<uint64_t>((counter.QuadPart * 1000000) / frequency.QuadPart);
            #elif defined(LT_PLATFORM_MACOS)
                return static_cast<uint64_t>(mach_absolute_time() / 1000); // Convert to microseconds
            #else
                auto now = std::chrono::high_resolution_clock::now();
                auto duration = now.time_since_epoch();
                return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            #endif
        }

        uint64_t GetSystemTime()
        {
            auto now = std::chrono::system_clock::now();
            auto duration = now.time_since_epoch();
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        }

        void SetConsoleColor(uint32_t color)
        {
            #ifdef LT_PLATFORM_WINDOWS
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, static_cast<WORD>(color));
            #else
                // ANSI color codes
                #ifdef LT_CONSOLE_LOGGING_ENABLED
                std::cout << "\033[" << color << "m";
                #endif
            #endif
        }

        void ResetConsoleColor()
        {
            #ifdef LT_PLATFORM_WINDOWS
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            #else
                #ifdef LT_CONSOLE_LOGGING_ENABLED
                std::cout << "\033[0m";
                #endif
            #endif
        }

        bool IsConsoleAvailable()
        {
            #ifdef LT_PLATFORM_WINDOWS
                return GetConsoleWindow() != nullptr;
            #else
                return isatty(STDOUT_FILENO) != 0;
            #endif
        }

        void BreakIntoDebugger()
        {
            #ifdef LT_PLATFORM_WINDOWS
                __debugbreak();
            #elif defined(LT_PLATFORM_MACOS)
                __builtin_trap();
            #else
                raise(SIGTRAP);
            #endif
        }

        void OutputDebugString(const std::string& message)
        {
            #ifdef LT_PLATFORM_WINDOWS
                OutputDebugStringA(message.c_str());
            #else
                // On non-Windows platforms, just output to stderr
                #ifdef LT_CONSOLE_LOGGING_ENABLED
                std::cerr << "[DEBUG] " << message << std::endl;
                #endif
            #endif
        }
    }
} 