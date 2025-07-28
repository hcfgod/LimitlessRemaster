#include "SDLManager.h"

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
            std::cout << "SDL already initialized" << std::endl;
            return true;
        }

        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0)
        {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }

        // Note: VSync will be handled by custom rendering system

        m_Initialized = true;
        std::cout << "SDL initialized successfully" << std::endl;
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
        std::cout << "SDL shutdown successfully" << std::endl;
    }
} 