#include "SDLManager.h"
#include <SDL3/SDL.h>
#include "Core/Logger.h"

namespace Limitless
{
    SDLManager& SDLManager::GetInstance()
    {
        static SDLManager instance;
        return instance;
    }

    bool SDLManager::Initialize()
    {
        if (m_Initialized)
        {
            LT_WARN("SDL already initialized");
            return true;
        }

        // Initialize SDL
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        {
            LT_ERROR("SDL could not initialize! SDL_Error: {}", SDL_GetError());
            return false;
        }

        // Note: VSync will be handled by custom rendering system

        m_Initialized = true;
        LT_INFO("SDL initialized successfully");
        return true;
    }

    void SDLManager::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        SDL_Quit();
        m_Initialized = false;
        LT_INFO("SDL shutdown successfully");
    }
} 