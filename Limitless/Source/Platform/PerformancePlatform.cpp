#include "Platform/PerformancePlatform.h"
#include "Platform/Platform.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

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
        std::string errorMsg = "CPU Platform not supported on this platform";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("PerformancePlatformFactory::CreateCPUPlatform");
        error.SetClassName("PerformancePlatformFactory");
        error.SetModuleName("Platform");
        error.AddContext("platform", LT_PLATFORM_NAME);
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
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
        std::string errorMsg = "GPU Platform not supported on this platform";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("PerformancePlatformFactory::CreateGPUPlatform");
        error.SetClassName("PerformancePlatformFactory");
        error.SetModuleName("Platform");
        error.AddContext("platform", LT_PLATFORM_NAME);
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
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
        std::string errorMsg = "System Platform not supported on this platform";
        PlatformError error(errorMsg, std::source_location::current());
        error.SetFunctionName("PerformancePlatformFactory::CreateSystemPlatform");
        error.SetClassName("PerformancePlatformFactory");
        error.SetModuleName("Platform");
        error.AddContext("platform", LT_PLATFORM_NAME);
        
        LT_CORE_ERROR("{}", errorMsg);
        Error::LogError(error);
        LT_THROW_PLATFORM_ERROR(errorMsg);
#endif
    }

} // namespace Limitless 