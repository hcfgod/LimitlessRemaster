#include "Platform/PerformancePlatform.h"

// Platform-specific includes
#ifdef LT_PLATFORM_WINDOWS
    #include "Platform/Windows/WindowsPerformancePlatform.h"
#elif defined(LT_PLATFORM_LINUX)
    #include "Platform/Linux/LinuxPerformancePlatform.h"
#elif defined(LT_PLATFORM_MACOS)
    #include "Platform/macOS/macOSPerformancePlatform.h"
#endif

namespace Limitless {

    std::unique_ptr<ICPUPlatform> PerformancePlatformFactory::CreateCPUPlatform() {
#ifdef LT_PLATFORM_WINDOWS
        return std::make_unique<WindowsCPUPlatform>();
#elif defined(LT_PLATFORM_LINUX)
        return std::make_unique<LinuxCPUPlatform>();
#elif defined(LT_PLATFORM_MACOS)
        return std::make_unique<macOSCPUPlatform>();
#else
        // Return a null implementation for unsupported platforms
        return nullptr;
#endif
    }

    std::unique_ptr<IGPUPlatform> PerformancePlatformFactory::CreateGPUPlatform() {
#ifdef LT_PLATFORM_WINDOWS
        return std::make_unique<WindowsGPUPlatform>();
#elif defined(LT_PLATFORM_LINUX)
        return std::make_unique<LinuxGPUPlatform>();
#elif defined(LT_PLATFORM_MACOS)
        return std::make_unique<macOSGPUPlatform>();
#else
        // Return a null implementation for unsupported platforms
        return nullptr;
#endif
    }

    std::unique_ptr<ISystemPlatform> PerformancePlatformFactory::CreateSystemPlatform() {
#ifdef LT_PLATFORM_WINDOWS
        return std::make_unique<WindowsSystemPlatform>();
#elif defined(LT_PLATFORM_LINUX)
        return std::make_unique<LinuxSystemPlatform>();
#elif defined(LT_PLATFORM_MACOS)
        return std::make_unique<macOSSystemPlatform>();
#else
        // Return a null implementation for unsupported platforms
        return nullptr;
#endif
    }

} // namespace Limitless 