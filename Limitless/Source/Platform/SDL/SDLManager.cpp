#include "SDLManager.h"
#include "Core/Error.h"
#include <SDL3/SDL.h>
#include "Core/Debug/Log.h"

namespace Limitless
{
    SDLManager& SDLManager::GetInstance()
    {
        static SDLManager instance;
        return instance;
    }

    Result<void> SDLManager::Initialize()
    {
        LT_VERIFY(!m_Initialized, "SDL already initialized");
        
        if (m_Initialized)
        {
            LT_CORE_WARN("SDL already initialized");
            return Result<void>();
        }

        // Initialize SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        {
            std::string errorMsg = fmt::format("SDL could not initialize! SDL_Error: {}", SDL_GetError());
            PlatformError error(errorMsg, std::source_location::current());
            error.SetSystemErrorCode(0); // SDL doesn't provide numeric error codes
            error.SetFunctionName("SDLManager::Initialize");
            error.SetClassName("SDLManager");
            error.SetModuleName("Platform/SDL");
            error.AddContext("init_flags", "SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS");
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            return Result<void>(error);
        }

        // Note: VSync will be handled by custom rendering system
        m_Initialized = true;
        LT_CORE_INFO("SDL initialized successfully");
        return Result<void>();
    }

    void SDLManager::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        SDL_Quit();
        m_Initialized = false;
        LT_CORE_INFO("SDL shutdown successfully");
    }

    Result<void> SDLManager::InitializeSubsystem(uint32_t flags)
    {
        LT_VERIFY(m_Initialized, "SDL not initialized");
        
        if (!m_Initialized)
        {
            return Result<void>(PlatformError("SDL not initialized", std::source_location::current()));
        }

        if (!SDL_InitSubSystem(flags))
        {
            std::string errorMsg = fmt::format("SDL subsystem initialization failed! SDL_Error: {}", SDL_GetError());
            PlatformError error(errorMsg, std::source_location::current());
            error.SetSystemErrorCode(0); // SDL doesn't provide numeric error codes
            error.SetFunctionName("SDLManager::InitializeSubsystem");
            error.AddContext("flags", std::to_string(flags));
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            return Result<void>(error);
        }

        LT_CORE_INFO("SDL subsystem initialized successfully (flags: {})", flags);
        return Result<void>();
    }

    Result<void> SDLManager::QuitSubsystem(uint32_t flags)
    {
        LT_VERIFY(m_Initialized, "SDL not initialized");
        
        if (!m_Initialized)
        {
            return Result<void>(PlatformError("SDL not initialized", std::source_location::current()));
        }

        SDL_QuitSubSystem(flags);
        LT_CORE_INFO("SDL subsystem quit successfully (flags: {})", flags);
        return Result<void>();
    }

    Result<std::string> SDLManager::GetLastError()
    {
        const char* error = SDL_GetError();
        if (error && strlen(error) > 0)
        {
            return Result<std::string>(std::string(error));
        }
        return Result<std::string>("No SDL error");
    }

    Result<void> SDLManager::ClearError()
    {
        SDL_ClearError();
        return Result<void>();
    }
} 