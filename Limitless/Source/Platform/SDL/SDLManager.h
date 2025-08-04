#pragma once

#include "Core/Error.h"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace Limitless
{
    class SDLManager
    {
    public:
        static SDLManager& GetInstance();
        
        Result<void> Initialize();
        void Shutdown();
        
        bool IsInitialized() const { return m_Initialized; }
        
        // Additional SDL management methods
        Result<void> InitializeSubsystem(uint32_t flags);
        Result<void> QuitSubsystem(uint32_t flags);
        Result<std::string> GetLastError();
        Result<void> ClearError();
        
        // Prevent copying
        SDLManager(const SDLManager&) = delete;
        SDLManager& operator=(const SDLManager&) = delete;

    private:
        SDLManager() = default;
        ~SDLManager() = default;
        
        bool m_Initialized = false;
    };
} 