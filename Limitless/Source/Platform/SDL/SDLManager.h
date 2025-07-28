#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <iostream>

namespace Limitless
{
    class SDLManager
    {
    public:
        static SDLManager& GetInstance();
        
        bool Initialize();
        void Shutdown();
        
        bool IsInitialized() const { return m_Initialized; }
        
        // Prevent copying
        SDLManager(const SDLManager&) = delete;
        SDLManager& operator=(const SDLManager&) = delete;

    private:
        SDLManager() = default;
        ~SDLManager() = default;
        
        bool m_Initialized = false;
    };
} 