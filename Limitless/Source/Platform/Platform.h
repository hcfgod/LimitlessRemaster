#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace Limitless
{
    // Platform enumeration
    enum class Platform
    {
        Unknown = 0,
        Windows,
        macOS,
        Linux,
        Android,
        iOS,
        Web
    };

    // Architecture enumeration
    enum class Architecture
    {
        Unknown = 0,
        x86,
        x64,
        ARM32,
        ARM64,
        RISC_V
    };

    // Compiler enumeration
    enum class Compiler
    {
        Unknown = 0,
        MSVC,
        GCC,
        Clang,
        AppleClang
    };

    // System capabilities
    struct SystemCapabilities
    {
        bool hasSSE2 = false;
        bool hasSSE3 = false;
        bool hasSSE4_1 = false;
        bool hasSSE4_2 = false;
        bool hasAVX = false;
        bool hasAVX2 = false;
        bool hasAVX512 = false;
        bool hasNEON = false;
        bool hasAltiVec = false;
        
        uint32_t cpuCount = 0;
        uint64_t totalMemory = 0;
        uint64_t availableMemory = 0;
        
        bool hasOpenGL = false;
        bool hasVulkan = false;
        bool hasMetal = false;
        bool hasDirectX = false;
        
        std::string gpuVendor;
        std::string gpuRenderer;
        std::string gpuVersion;
    };

    // Platform information structure
    struct PlatformInfo
    {
        Platform platform = Platform::Unknown;
        Architecture architecture = Architecture::Unknown;
        Compiler compiler = Compiler::Unknown;
        
        std::string platformName;
        std::string architectureName;
        std::string compilerName;
        std::string compilerVersion;
        
        std::string osName;
        std::string osVersion;
        std::string osBuild;
        
        SystemCapabilities capabilities;
        
        // Platform-specific paths
        std::string executablePath;
        std::string workingDirectory;
        std::string userDataPath;
        std::string tempPath;
        std::string systemPath;
        
        // Build information
        std::string buildDate;
        std::string buildTime;
        std::string buildType;
        std::string buildVersion;
    };

    // Platform detection and utilities
    class PlatformDetection
    {
    public:
        // Get platform information
        static const PlatformInfo& GetPlatformInfo();
        
        // Platform checks
        static bool IsWindows() { return GetPlatformInfo().platform == Platform::Windows; }
        static bool IsMacOS() { return GetPlatformInfo().platform == Platform::macOS; }
        static bool IsLinux() { return GetPlatformInfo().platform == Platform::Linux; }
        static bool IsAndroid() { return GetPlatformInfo().platform == Platform::Android; }
        static bool IsIOS() { return GetPlatformInfo().platform == Platform::iOS; }
        static bool IsWeb() { return GetPlatformInfo().platform == Platform::Web; }
        
        // Architecture checks
        static bool IsX86() { return GetPlatformInfo().architecture == Architecture::x86; }
        static bool IsX64() { return GetPlatformInfo().architecture == Architecture::x64; }
        static bool IsARM32() { return GetPlatformInfo().architecture == Architecture::ARM32; }
        static bool IsARM64() { return GetPlatformInfo().architecture == Architecture::ARM64; }
        static bool IsRISC_V() { return GetPlatformInfo().architecture == Architecture::RISC_V; }
        
        // Compiler checks
        static bool IsMSVC() { return GetPlatformInfo().compiler == Compiler::MSVC; }
        static bool IsGCC() { return GetPlatformInfo().compiler == Compiler::GCC; }
        static bool IsClang() { return GetPlatformInfo().compiler == Compiler::Clang; }
        static bool IsAppleClang() { return GetPlatformInfo().compiler == Compiler::AppleClang; }
        
        // System capability checks
        static bool HasSSE2() { return GetPlatformInfo().capabilities.hasSSE2; }
        static bool HasSSE3() { return GetPlatformInfo().capabilities.hasSSE3; }
        static bool HasSSE4_1() { return GetPlatformInfo().capabilities.hasSSE4_1; }
        static bool HasSSE4_2() { return GetPlatformInfo().capabilities.hasSSE4_2; }
        static bool HasAVX() { return GetPlatformInfo().capabilities.hasAVX; }
        static bool HasAVX2() { return GetPlatformInfo().capabilities.hasAVX2; }
        static bool HasAVX512() { return GetPlatformInfo().capabilities.hasAVX512; }
        static bool HasNEON() { return GetPlatformInfo().capabilities.hasNEON; }
        static bool HasAltiVec() { return GetPlatformInfo().capabilities.hasAltiVec; }
        
        // Graphics API checks
        static bool HasOpenGL() { return GetPlatformInfo().capabilities.hasOpenGL; }
        static bool HasVulkan() { return GetPlatformInfo().capabilities.hasVulkan; }
        static bool HasMetal() { return GetPlatformInfo().capabilities.hasMetal; }
        static bool HasDirectX() { return GetPlatformInfo().capabilities.hasDirectX; }
        
        // System information
        static uint32_t GetCPUCount() { return GetPlatformInfo().capabilities.cpuCount; }
        static uint64_t GetTotalMemory() { return GetPlatformInfo().capabilities.totalMemory; }
        static uint64_t GetAvailableMemory() { return GetPlatformInfo().capabilities.availableMemory; }
        
        // Path utilities
        static std::string GetExecutablePath() { return GetPlatformInfo().executablePath; }
        static std::string GetWorkingDirectory() { return GetPlatformInfo().workingDirectory; }
        static std::string GetUserDataPath() { return GetPlatformInfo().userDataPath; }
        static std::string GetTempPath() { return GetPlatformInfo().tempPath; }
        static std::string GetSystemPath() { return GetPlatformInfo().systemPath; }
        
        // Platform-specific utilities
        static std::string GetPlatformString();
        static std::string GetArchitectureString();
        static std::string GetCompilerString();
        static std::string GetOSString();
        
        // Initialize platform detection (call once at startup)
        static void Initialize();
        
        // Refresh system capabilities (useful for hot reloading)
        static void RefreshCapabilities();
        
        // Check if platform detection has been initialized
        static bool IsInitialized();

    private:
        static PlatformInfo s_PlatformInfo;
        static bool s_Initialized;
        
        static void DetectPlatform();
        static void DetectArchitecture();
        static void DetectCompiler();
        static void DetectOS();
        static void DetectCapabilities();
        static void DetectPaths();
        static void DetectGraphicsAPIs();
    };

    // Platform-specific utilities
    namespace PlatformUtils
    {
        // File system utilities
        std::string GetPathSeparator();
        std::string NormalizePath(const std::string& path);
        std::string JoinPath(const std::string& path1, const std::string& path2);
        std::string GetDirectoryName(const std::string& path);
        std::string GetFileName(const std::string& path);
        std::string GetFileExtension(const std::string& path);
        
        // Environment utilities
        std::optional<std::string> GetEnvironmentVariable(const std::string& name);
        bool SetEnvironmentVariable(const std::string& name, const std::string& value);
        
        // Process utilities
        uint32_t GetCurrentProcessId();
        uint32_t GetCurrentThreadId();
        void Sleep(uint32_t milliseconds);
        
        // System utilities
        void* LoadLibrary(const std::string& path);
        void* GetProcAddress(void* library, const std::string& name);
        void FreeLibrary(void* library);
        
        // Memory utilities
        void* AllocateAligned(size_t size, size_t alignment);
        void FreeAligned(void* ptr);
        
        // Time utilities
        uint64_t GetHighResolutionTime();
        uint64_t GetSystemTime();
        
        // Console utilities
        void SetConsoleColor(uint32_t color);
        void ResetConsoleColor();
        bool IsConsoleAvailable();
        
        // Debug utilities
        void BreakIntoDebugger();
        void OutputDebugString(const std::string& message);
    }

    // Platform-specific macros for compile-time detection
    #if defined(LIMITLESS_PLATFORM_WINDOWS) || defined(LT_PLATFORM_WINDOWS)
        #define LT_PLATFORM_WINDOWS 1
        #define LT_PLATFORM_NAME "Windows"
    #elif defined(LT_PLATFORM_MAC) || defined(__APPLE__)
        #define LT_PLATFORM_MACOS 1
        #define LT_PLATFORM_NAME "macOS"
    #elif defined(LT_PLATFORM_LINUX) || defined(__linux__)
        #define LT_PLATFORM_LINUX 1
        #define LT_PLATFORM_NAME "Linux"
    #else
        #define LT_PLATFORM_UNKNOWN 1
        #define LT_PLATFORM_NAME "Unknown"
    #endif

    #if defined(_MSC_VER)
        #define LT_COMPILER_MSVC 1
        #define LT_COMPILER_NAME "MSVC"
    #elif defined(__GNUC__) && !defined(__clang__)
        #define LT_COMPILER_GCC 1
        #define LT_COMPILER_NAME "GCC"
    #elif defined(__clang__)
        #if defined(__apple_build_version__) || (defined(__APPLE__) && defined(__clang__))
            #define LT_COMPILER_APPLE_CLANG 1
            #define LT_COMPILER_NAME "AppleClang"
        #else
            #define LT_COMPILER_CLANG 1
            #define LT_COMPILER_NAME "Clang"
        #endif
    #else
        #define LT_COMPILER_UNKNOWN 1
        #define LT_COMPILER_NAME "Unknown"
    #endif

    #if defined(_M_X64) || defined(__x86_64__)
        #define LT_ARCHITECTURE_X64 1
        #define LT_ARCHITECTURE_NAME "x64"
    #elif defined(_M_IX86) || defined(__i386__)
        #define LT_ARCHITECTURE_X86 1
        #define LT_ARCHITECTURE_NAME "x86"
    #elif defined(__aarch64__) || defined(_M_ARM64)
        #define LT_ARCHITECTURE_ARM64 1
        #define LT_ARCHITECTURE_NAME "ARM64"
    #elif defined(__arm__) || defined(_M_ARM)
        #define LT_ARCHITECTURE_ARM32 1
        #define LT_ARCHITECTURE_NAME "ARM32"
    #else
        #define LT_ARCHITECTURE_UNKNOWN 1
        #define LT_ARCHITECTURE_NAME "Unknown"
    #endif

    // Debug configuration macros
    #if defined(LIMITLESS_DEBUG) || defined(LT_DEBUG)
        #define LT_DEBUG_BUILD 1
        #define LT_BUILD_TYPE "Debug"
    #elif defined(LIMITLESS_RELEASE) || defined(LT_RELEASE)
        #define LT_RELEASE_BUILD 1
        #define LT_BUILD_TYPE "Release"
    #elif defined(LIMITLESS_DIST) || defined(LT_DIST)
        #define LT_DIST_BUILD 1
        #define LT_BUILD_TYPE "Dist"
    #else
        #define LT_BUILD_TYPE "Unknown"
    #endif
} 