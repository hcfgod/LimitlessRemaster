#include "Platform/Window.h"
#include "Platform/SDL/SDLWindow.h"

namespace Limitless
{
    std::unique_ptr<Window> Window::Create(const WindowProps& props)
    {
        return std::make_unique<SDLWindow>(props);
    }
} 