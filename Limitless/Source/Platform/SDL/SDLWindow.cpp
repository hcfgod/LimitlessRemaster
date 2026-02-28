#include "SDLWindow.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include "Core/Input/InputSystem.h"
#include "Core/Error.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Platform/Platform.h"
#include <SDL3/SDL.h>
#include <spdlog/fmt/fmt.h>
#include "stb/stb_image/stb_image.h"
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::uint8_t PngSignature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

    std::uint16_t ReadUint16LE(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
    }

    std::uint32_t ReadUint32LE(const std::uint8_t* data)
    {
        return static_cast<std::uint32_t>(data[0])
            | (static_cast<std::uint32_t>(data[1]) << 8)
            | (static_cast<std::uint32_t>(data[2]) << 16)
            | (static_cast<std::uint32_t>(data[3]) << 24);
    }

    SDL_Surface* CreateSurfaceFromRgba(const std::uint8_t* rgbaPixels, int width, int height)
    {
        if (!rgbaPixels || width <= 0 || height <= 0)
            return nullptr;

        SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return nullptr;

        auto* destinationPixels = static_cast<std::uint8_t*>(surface->pixels);
        const int sourcePitch = width * 4;
        for (int y = 0; y < height; ++y)
        {
            std::memcpy(destinationPixels + (y * surface->pitch), rgbaPixels + (y * sourcePitch), static_cast<size_t>(sourcePitch));
        }
        return surface;
    }

    SDL_Surface* TryLoadSurfaceWithStb(const std::string& iconPath)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* rgbaPixels = stbi_load(iconPath.c_str(), &width, &height, &channels, 4);
        if (!rgbaPixels)
            return nullptr;

        SDL_Surface* surface = CreateSurfaceFromRgba(rgbaPixels, width, height);
        stbi_image_free(rgbaPixels);
        return surface;
    }

    SDL_Surface* TryLoadIcoPngEntry(const std::vector<std::uint8_t>& icoBytes)
    {
        if (icoBytes.size() < 6)
            return nullptr;

        const std::uint16_t iconType = ReadUint16LE(icoBytes.data() + 2);
        const std::uint16_t entryCount = ReadUint16LE(icoBytes.data() + 4);
        if (iconType != 1 || entryCount == 0)
            return nullptr;

        const size_t directorySize = 6u + (static_cast<size_t>(entryCount) * 16u);
        if (icoBytes.size() < directorySize)
            return nullptr;

        const std::uint8_t* bestData = nullptr;
        std::uint32_t bestDataSize = 0;
        int bestScore = -1;

        for (std::uint16_t i = 0; i < entryCount; ++i)
        {
            const size_t entryOffset = 6u + (static_cast<size_t>(i) * 16u);
            const std::uint8_t* entry = icoBytes.data() + entryOffset;
            const int width = entry[0] == 0 ? 256 : entry[0];
            const int height = entry[1] == 0 ? 256 : entry[1];
            const std::uint16_t bitsPerPixel = ReadUint16LE(entry + 6);
            const std::uint32_t bytesInResource = ReadUint32LE(entry + 8);
            const std::uint32_t imageOffset = ReadUint32LE(entry + 12);

            if (bytesInResource < 8)
                continue;

            const size_t dataStart = static_cast<size_t>(imageOffset);
            const size_t dataEnd = dataStart + static_cast<size_t>(bytesInResource);
            if (dataStart >= icoBytes.size() || dataEnd > icoBytes.size())
                continue;

            const std::uint8_t* imageBytes = icoBytes.data() + dataStart;
            if (!std::equal(std::begin(PngSignature), std::end(PngSignature), imageBytes))
                continue;

            const int score = (width * height * 64) + bitsPerPixel;
            if (score > bestScore)
            {
                bestScore = score;
                bestData = imageBytes;
                bestDataSize = bytesInResource;
            }
        }

        if (!bestData || bestDataSize == 0)
            return nullptr;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* rgbaPixels = stbi_load_from_memory(bestData, static_cast<int>(bestDataSize), &width, &height, &channels, 4);
        if (!rgbaPixels)
            return nullptr;

        SDL_Surface* surface = CreateSurfaceFromRgba(rgbaPixels, width, height);
        stbi_image_free(rgbaPixels);
        return surface;
    }

    SDL_Surface* TryLoadIcoBmpEntry(const std::vector<std::uint8_t>& icoBytes)
    {
        if (icoBytes.size() < 6)
            return nullptr;

        const std::uint16_t iconType = ReadUint16LE(icoBytes.data() + 2);
        const std::uint16_t entryCount = ReadUint16LE(icoBytes.data() + 4);
        if (iconType != 1 || entryCount == 0)
            return nullptr;

        const size_t directorySize = 6u + (static_cast<size_t>(entryCount) * 16u);
        if (icoBytes.size() < directorySize)
            return nullptr;

        const std::uint8_t* bestData = nullptr;
        std::uint32_t bestDataSize = 0;
        int bestScore = -1;

        for (std::uint16_t i = 0; i < entryCount; ++i)
        {
            const size_t entryOffset = 6u + (static_cast<size_t>(i) * 16u);
            const std::uint8_t* entry = icoBytes.data() + entryOffset;
            const int width = entry[0] == 0 ? 256 : entry[0];
            const int height = entry[1] == 0 ? 256 : entry[1];
            const std::uint16_t bitsPerPixel = ReadUint16LE(entry + 6);
            const std::uint32_t bytesInResource = ReadUint32LE(entry + 8);
            const std::uint32_t imageOffset = ReadUint32LE(entry + 12);

            if (bytesInResource < 40)
                continue;

            const size_t dataStart = static_cast<size_t>(imageOffset);
            const size_t dataEnd = dataStart + static_cast<size_t>(bytesInResource);
            if (dataStart >= icoBytes.size() || dataEnd > icoBytes.size())
                continue;

            const std::uint8_t* imageBytes = icoBytes.data() + dataStart;
            const bool isPng = std::equal(std::begin(PngSignature), std::end(PngSignature), imageBytes);
            if (isPng)
                continue;

            const std::uint32_t dibHeaderSize = ReadUint32LE(imageBytes);
            if (dibHeaderSize < 40 || dibHeaderSize > bytesInResource)
                continue;

            const int score = (width * height * 64) + bitsPerPixel;
            if (score > bestScore)
            {
                bestScore = score;
                bestData = imageBytes;
                bestDataSize = bytesInResource;
            }
        }

        if (!bestData || bestDataSize < 40)
            return nullptr;

        const std::uint32_t dibHeaderSize = ReadUint32LE(bestData + 0);
        const std::int32_t dibWidth = static_cast<std::int32_t>(ReadUint32LE(bestData + 4));
        const std::int32_t dibHeight = static_cast<std::int32_t>(ReadUint32LE(bestData + 8));
        const std::uint16_t dibBitCount = ReadUint16LE(bestData + 14);
        const std::uint32_t dibCompression = ReadUint32LE(bestData + 16);

        if (dibWidth <= 0 || dibHeight == 0)
            return nullptr;
        if (dibCompression != 0)
            return nullptr; // BI_RGB only
        if (dibBitCount != 24 && dibBitCount != 32)
            return nullptr;

        const int iconWidth = dibWidth;
        const int iconHeight = static_cast<int>(std::abs(dibHeight) / 2);
        if (iconHeight <= 0)
            return nullptr;

        const size_t xorStride = static_cast<size_t>(((iconWidth * dibBitCount + 31) / 32) * 4);
        const size_t andStride = static_cast<size_t>(((iconWidth + 31) / 32) * 4);
        const size_t xorDataOffset = dibHeaderSize;
        const size_t xorDataSize = xorStride * static_cast<size_t>(iconHeight);
        const size_t andDataOffset = xorDataOffset + xorDataSize;
        const size_t andDataSize = andStride * static_cast<size_t>(iconHeight);
        if (andDataOffset + andDataSize > bestDataSize)
            return nullptr;

        SDL_Surface* surface = SDL_CreateSurface(iconWidth, iconHeight, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return nullptr;

        auto* destinationPixels = static_cast<std::uint8_t*>(surface->pixels);
        bool hasNonZeroAlpha = false;
        const bool bottomUp = (dibHeight > 0);
        for (int y = 0; y < iconHeight; ++y)
        {
            const int sourceY = bottomUp ? (iconHeight - 1 - y) : y;
            const std::uint8_t* srcRow = bestData + xorDataOffset + (xorStride * static_cast<size_t>(sourceY));
            std::uint8_t* dstRow = destinationPixels + (static_cast<size_t>(y) * static_cast<size_t>(surface->pitch));

            for (int x = 0; x < iconWidth; ++x)
            {
                if (dibBitCount == 32)
                {
                    const std::uint8_t b = srcRow[x * 4 + 0];
                    const std::uint8_t g = srcRow[x * 4 + 1];
                    const std::uint8_t r = srcRow[x * 4 + 2];
                    const std::uint8_t a = srcRow[x * 4 + 3];
                    dstRow[x * 4 + 0] = r;
                    dstRow[x * 4 + 1] = g;
                    dstRow[x * 4 + 2] = b;
                    dstRow[x * 4 + 3] = a;
                    if (a != 0)
                        hasNonZeroAlpha = true;
                }
                else
                {
                    const std::uint8_t b = srcRow[x * 3 + 0];
                    const std::uint8_t g = srcRow[x * 3 + 1];
                    const std::uint8_t r = srcRow[x * 3 + 2];
                    dstRow[x * 4 + 0] = r;
                    dstRow[x * 4 + 1] = g;
                    dstRow[x * 4 + 2] = b;
                    dstRow[x * 4 + 3] = 255;
                }
            }
        }

        if (!hasNonZeroAlpha || dibBitCount == 24)
        {
            for (int y = 0; y < iconHeight; ++y)
            {
                const int sourceY = bottomUp ? (iconHeight - 1 - y) : y;
                const std::uint8_t* maskRow = bestData + andDataOffset + (andStride * static_cast<size_t>(sourceY));
                std::uint8_t* dstRow = destinationPixels + (static_cast<size_t>(y) * static_cast<size_t>(surface->pitch));

                for (int x = 0; x < iconWidth; ++x)
                {
                    const std::uint8_t maskByte = maskRow[x / 8];
                    const std::uint8_t maskBit = static_cast<std::uint8_t>((maskByte >> (7 - (x % 8))) & 0x1u);
                    if (maskBit)
                        dstRow[x * 4 + 3] = 0;
                    else if (dibBitCount == 24)
                        dstRow[x * 4 + 3] = 255;
                }
            }
        }

        return surface;
    }

    SDL_Surface* TryLoadSurfaceFromIco(const std::string& iconPath)
    {
        std::ifstream file(iconPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return nullptr;

        const std::streamoff endPos = file.tellg();
        if (endPos <= 0)
            return nullptr;

        file.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> icoBytes(static_cast<size_t>(endPos));
        file.read(reinterpret_cast<char*>(icoBytes.data()), static_cast<std::streamsize>(icoBytes.size()));
        if (!file)
            return nullptr;

        if (SDL_Surface* pngSurface = TryLoadIcoPngEntry(icoBytes))
            return pngSurface;

        return TryLoadIcoBmpEntry(icoBytes);
    }

    std::optional<std::filesystem::path> ResolveIconPath(const std::string& iconPath)
    {
        if (iconPath.empty())
            return std::nullopt;

        std::vector<std::filesystem::path> candidates;
        candidates.reserve(24);

        const std::filesystem::path inputPath(iconPath);
        candidates.push_back(inputPath);

        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec)
        {
            candidates.push_back(cwd / inputPath);
            candidates.push_back(cwd / "Resources" / inputPath.filename());
        }

        std::filesystem::path exeDir;
        const std::string executablePath = Limitless::PlatformDetection::GetExecutablePath();
        if (!executablePath.empty())
        {
            exeDir = std::filesystem::path(executablePath).parent_path();
            if (!exeDir.empty())
            {
                candidates.push_back(exeDir / inputPath);
                candidates.push_back(exeDir / "Resources" / inputPath.filename());
            }
        }

        auto appendParentResources = [&](std::filesystem::path probeRoot) {
            if (probeRoot.empty())
                return;
            for (int depth = 0; depth < 8; ++depth)
            {
                candidates.push_back(probeRoot / "Resources" / inputPath.filename());
                if (!probeRoot.has_parent_path())
                    break;
                const std::filesystem::path parent = probeRoot.parent_path();
                if (parent == probeRoot)
                    break;
                probeRoot = parent;
            }
        };
        appendParentResources(cwd);
        appendParentResources(exeDir);

        std::unordered_set<std::string> seen;
        for (const auto& candidate : candidates)
        {
            if (candidate.empty())
                continue;
            const std::string candidateText = candidate.lexically_normal().generic_string();
            if (!seen.insert(candidateText).second)
                continue;

            ec.clear();
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
            {
                ec.clear();
                const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
                if (!ec)
                    return canonicalCandidate;
                return candidate;
            }
        }

        return std::nullopt;
    }

    bool ApplyWindowIcon(SDL_Window* window, const std::string& iconPath, std::string* outAppliedPath)
    {
        if (!window || iconPath.empty())
            return false;

        const auto resolvedIconPath = ResolveIconPath(iconPath);
        if (!resolvedIconPath.has_value())
        {
            LT_CORE_WARN("Failed to resolve icon path '{}'. Expected either a path relative to CWD/executable or under a nearby Resources/ folder.", iconPath);
            return false;
        }

        const std::string resolvedPathString = resolvedIconPath->string();

        SDL_Surface* surface = SDL_LoadBMP(resolvedPathString.c_str());
        if (!surface)
            surface = TryLoadSurfaceWithStb(resolvedPathString);
        if (!surface)
            surface = TryLoadSurfaceFromIco(resolvedPathString);

        if (!surface)
        {
            const std::string errorMsg = fmt::format(
                "Failed to load resolved icon '{}' (requested '{}') as BMP, standard image, or PNG-based ICO. SDL error: {}",
                resolvedPathString,
                iconPath,
                SDL_GetError()
            );
            LT_CORE_WARN("{}", errorMsg);
            return false;
        }

        SDL_SetWindowIcon(window, surface);
        SDL_DestroySurface(surface);
        if (outAppliedPath)
            *outAppliedPath = iconPath;
        return true;
    }
}

namespace Limitless
{
    SDLWindow::SDLWindow(const WindowProps& props)
    {
        Init(props);
    }

    SDLWindow::~SDLWindow()
    {
        UnsubscribeFromEvents();
        Shutdown();
    }

    void SDLWindow::SubscribeToEvents()
    {
        // Subscribe to window configuration change events (only if EventSystem is initialized)
        try
        {
            if (GetEventSystem().IsInitialized())
            {
                // Use callbacks instead of listeners to avoid shared_ptr issues
                m_WindowConfigChangedCallbackToken =
                    GetEventSystem().AddCallback(EventType::WindowConfigChanged,
                        [this](Event& event) { OnWindowConfigChangedCallback(event); });
            }
        }
        catch (...)
        {
            // Ignore any exceptions during initialization
            LT_CORE_WARN("SDLWindow: Warning - Could not subscribe to events during initialization");
        }
    }

    void SDLWindow::UnsubscribeFromEvents()
    {
        // Unsubscribe from events (only if EventSystem is still initialized)
        try
        {
            if (GetEventSystem().IsInitialized())
            {
                if (m_WindowConfigChangedCallbackToken != 0)
                {
                    GetEventSystem().RemoveCallback(EventType::WindowConfigChanged, m_WindowConfigChangedCallbackToken);
                    m_WindowConfigChangedCallbackToken = 0;
                }
            }
        }
        catch (...)
        {
            // Ignore any exceptions during shutdown
            LT_CORE_WARN("SDLWindow: Warning - Could not unsubscribe from events during shutdown");
        }
    }

    void SDLWindow::OnWindowConfigChangedCallback(Event& event)
    {
        if (event.GetType() == EventType::WindowConfigChanged)
        {
            if (auto* windowEvent = dynamic_cast<Events::WindowConfigChangedEvent*>(&event))
            {
                OnWindowConfigChanged(*windowEvent);
            }
        }
    }

    void SDLWindow::Init(const WindowProps& props)
    {
        LT_VERIFY(!props.Title.empty(), "Window title cannot be empty");
        LT_VERIFY(props.Width > 0, "Window width must be greater than 0");
        LT_VERIFY(props.Height > 0, "Window height must be greater than 0");
        
        m_Data.Title = props.Title;
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;
        m_Data.Fullscreen = props.Fullscreen;
        m_Data.FullscreenDesktop = (static_cast<uint32_t>(props.Flags) & static_cast<uint32_t>(WindowFlags::FullscreenDesktop)) != 0;
        m_Data.Resizable = props.Resizable;
        m_Data.PositionX = props.PositionX;
        m_Data.PositionY = props.PositionY;
        m_Data.Flags = props.Flags;
        m_Data.MinWidth = props.MinWidth;
        m_Data.MinHeight = props.MinHeight;
        m_Data.MaxWidth = props.MaxWidth;
        m_Data.MaxHeight = props.MaxHeight;
        m_Data.Opacity = 1.0f;
        m_Data.Brightness = 1.0f;
        m_Data.HighDPI = props.HighDPI;
        m_Data.Borderless = props.Borderless;
        m_Data.AlwaysOnTop = props.AlwaysOnTop;
        m_Data.IconPath = props.IconPath;
        m_Data.Api = props.Api;

        LT_CORE_INFO("Creating window {} ({}, {})", props.Title, props.Width, props.Height);
        if (props.Fullscreen) {
            LT_CORE_INFO("Window will be fullscreen");
        }
        if (props.Resizable) {
            LT_CORE_INFO("Window will be resizable");
        }

        // Create graphics context early (before window) to setup attributes
        m_Context = Limitless::CreateGraphicsContext();
        if (!m_Context)
        {
            std::string errorMsg = "Failed to create graphics context";
            GraphicsError error(errorMsg, std::source_location::current());
            error.SetFunctionName("SDLWindow::Init");
            error.SetClassName("SDLWindow");
            error.SetModuleName("Platform/SDL");
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_GRAPHICS_ERROR(errorMsg);
        }

        // Setup graphics-specific attributes before window creation
        m_Context->SetupAttributes();

        // Set up SDL window flags
        uint32_t windowFlags = static_cast<uint32_t>(ConvertToSDLFags(props.Flags));
        
        // Add graphics API specific flags
        if (props.Api == GraphicsAPI::OpenGL) {
            windowFlags |= SDL_WINDOW_OPENGL;
        }

        // Create window
        m_Window = SDL_CreateWindow(
            props.Title.c_str(),
            props.Width,
            props.Height,
            windowFlags
        );

        if (!m_Window)
        {
            std::string errorMsg = fmt::format("SDL_CreateWindow failed: {}", SDL_GetError());
            GraphicsError error(errorMsg, std::source_location::current());
            error.SetFunctionName("SDLWindow::Init");
            error.SetClassName("SDLWindow");
            error.SetModuleName("Platform/SDL");
            error.AddContext("title", props.Title);
            error.AddContext("width", std::to_string(props.Width));
            error.AddContext("height", std::to_string(props.Height));
            error.AddContext("flags", std::to_string(windowFlags));
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_GRAPHICS_ERROR(errorMsg);
        }

        // Set window position if specified
        if (props.PositionX != 0 || props.PositionY != 0)
        {
            SDL_SetWindowPosition(m_Window, props.PositionX, props.PositionY);
        }

        // Set size constraints if specified
        if (props.MinWidth > 0 || props.MinHeight > 0)
        {
            SDL_SetWindowMinimumSize(m_Window, props.MinWidth, props.MinHeight);
        }
        if (props.MaxWidth > 0 || props.MaxHeight > 0)
        {
            SDL_SetWindowMaximumSize(m_Window, props.MaxWidth, props.MaxHeight);
        }

        // Set icon if specified
        if (!props.IconPath.empty())
        {
            (void)ApplyWindowIcon(m_Window, props.IconPath, &m_Data.IconPath);
        }

        // Initialize graphics context using clean step-by-step approach
        LT_CORE_INFO("Initializing graphics context...");
        try
        {
            m_Context->Init(m_Window, m_Data.Api); // Create context, load functions, setup capabilities
            // IMPORTANT:
            // Do not force the context to remain current on the main thread here.
            // The renderer may execute/present from a dedicated render thread; OpenGL contexts
            // cannot be current on multiple threads at the same time.
            LT_CORE_INFO("Graphics context initialized successfully");
        }
        catch (const std::exception& e)
        {
            std::string errorMsg = fmt::format("Graphics context initialization failed: {}", e.what());
            GraphicsError error(errorMsg, std::source_location::current());
            error.SetFunctionName("SDLWindow::Init");
            error.SetClassName("SDLWindow");
            error.SetModuleName("Platform/SDL");
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            throw;
        }

        LT_CORE_INFO("Window created successfully");
    }

    void SDLWindow::Shutdown()
    {
        // OpenGL context must be destroyed BEFORE the window.
        // Otherwise the Intel driver (igxelpicd64.dll) can crash during process exit.
        // See: SDL/OpenGL best practice - destroy context first, then window.
        if (m_Context)
        {
            m_Context.reset();
            LT_CORE_DEBUG("Graphics context destroyed");
        }

        if (m_Window)
        {
            SDL_DestroyWindow(m_Window);
            m_Window = nullptr;
        }
    }

    void SDLWindow::OnUpdate()
    {
        std::vector<std::filesystem::path> droppedFilesThisFrame;

        // Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            // Feed external event handlers first (e.g. ImGui) before internal handling.
            if (m_SdlEventCallback)
                m_SdlEventCallback(event);

            // Feed the Unity-style input system immediately (same-frame polling).
            GetInputSystem().OnSdlEvent(event);

            switch (event.type)
            {
                case SDL_EVENT_QUIT:
                    if (m_CloseCallback)
                        m_CloseCallback();
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_MOVED:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_SHOWN:
                case SDL_EVENT_WINDOW_HIDDEN:
                    HandleWindowEvent(event);
                    break;

                // Input events (deferred) so Layers can handle them through EventSystem.
                case SDL_EVENT_KEY_DOWN:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::KeyPressedEvent>(static_cast<int>(event.key.key), event.key.repeat));
                    break;
                case SDL_EVENT_KEY_UP:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::KeyReleasedEvent>(static_cast<int>(event.key.key)));
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::MouseMovedEvent>(event.motion.x, event.motion.y));
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::MouseButtonPressedEvent>(static_cast<int>(event.button.button)));
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::MouseButtonReleasedEvent>(static_cast<int>(event.button.button)));
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    LT_DISPATCH_DEFERRED(std::make_unique<Events::MouseScrolledEvent>(event.wheel.x, event.wheel.y));
                    break;

                // OS drag-and-drop (Explorer/Finder -> window)
                case SDL_EVENT_DROP_FILE:
                {
                    if (event.drop.data && event.drop.data[0] != '\0')
                    {
                        droppedFilesThisFrame.emplace_back(std::filesystem::path(event.drop.data));
                    }
                    break;
                }
            }
        }

        if (!droppedFilesThisFrame.empty() && m_FileDropCallback)
        {
            m_FileDropCallback(droppedFilesThisFrame);
        }
    }

    void SDLWindow::HandleWindowEvent(const SDL_Event& event)
    {
        switch (event.type)
        {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                // For correctness with HighDPI, always treat the "drawable size" (pixel size)
                // as the source of truth for the rendering viewport.
                int drawableWidth = 0;
                int drawableHeight = 0;
                if (!SDL_GetWindowSizeInPixels(m_Window, &drawableWidth, &drawableHeight))
                {
                    LT_CORE_WARN("SDLWindow: SDL_GetWindowSizeInPixels failed during resize: {}", SDL_GetError());
                    drawableWidth = event.window.data1;
                    drawableHeight = event.window.data2;
                }

                const uint32_t widthPixels = drawableWidth > 0 ? static_cast<uint32_t>(drawableWidth) : 0u;
                const uint32_t heightPixels = drawableHeight > 0 ? static_cast<uint32_t>(drawableHeight) : 0u;

                m_Data.Width = widthPixels;
                m_Data.Height = heightPixels;

                if (m_ResizeCallback)
                {
                    m_ResizeCallback(widthPixels, heightPixels);
                }

                // Keep the render viewport in sync with the window size.
                // Note: During minimize some platforms report 0x0; avoid submitting a 0-sized viewport.
                auto& renderer = Renderer::GetInstance();
                if (renderer.IsInitialized() && widthPixels > 0 && heightPixels > 0)
                {
                    renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(widthPixels), static_cast<int>(heightPixels)));
                }

                // Notify the engine-side event system so layers/cameras can react.
                LT_DISPATCH_DEFERRED(std::make_unique<Events::WindowResizeEvent>(widthPixels, heightPixels));

                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Resized);
                break;
            }
            case SDL_EVENT_WINDOW_MOVED:
                m_Data.PositionX = event.window.data1;
                m_Data.PositionY = event.window.data2;
                if (m_MoveCallback)
                    m_MoveCallback(event.window.data1, event.window.data2);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Moved);
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (m_FocusCallback)
                    m_FocusCallback(true);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::FocusGained);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (m_FocusCallback)
                    m_FocusCallback(false);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::FocusLost);
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                if (m_StateChangeCallback)
                    m_StateChangeCallback(WindowState::Minimized);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Minimized);
                break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
                if (m_StateChangeCallback)
                    m_StateChangeCallback(WindowState::Maximized);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Maximized);
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                if (m_StateChangeCallback)
                    m_StateChangeCallback(WindowState::Normal);
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Restored);
                break;
            case SDL_EVENT_WINDOW_SHOWN:
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Shown);
                break;
            case SDL_EVENT_WINDOW_HIDDEN:
                if (m_EventCallback)
                    m_EventCallback(WindowEventType::Hidden);
                break;
        }
    }

    // Basic window operations
    void SDLWindow::GetSize(uint32_t& width, uint32_t& height) const
    {
        width = m_Data.Width;
        height = m_Data.Height;
    }

    void SDLWindow::SetSize(uint32_t width, uint32_t height)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(width > 0, "Window width must be greater than 0");
        LT_VERIFY(height > 0, "Window height must be greater than 0");
        
        if (m_Window)
        {
            SDL_SetWindowSize(m_Window, static_cast<int>(width), static_cast<int>(height));
            m_Data.Width = width;
            m_Data.Height = height;
        }
    }

    // Window position
    void SDLWindow::GetPosition(int& x, int& y) const
    {
        x = m_Data.PositionX;
        y = m_Data.PositionY;
    }

    void SDLWindow::SetPosition(int x, int y)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (m_Window)
        {
            SDL_SetWindowPosition(m_Window, x, y);
            m_Data.PositionX = x;
            m_Data.PositionY = y;
        }
    }

    void SDLWindow::CenterOnScreen()
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (m_Window)
        {
            SDL_SetWindowPosition(m_Window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            SDL_GetWindowPosition(m_Window, &m_Data.PositionX, &m_Data.PositionY);
        }
    }

    // Window title and properties
    void SDLWindow::SetTitle(const std::string& title)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(!title.empty(), "Window title cannot be empty");
        
        if (m_Window)
        {
            SDL_SetWindowTitle(m_Window, title.c_str());
            m_Data.Title = title;
        }
    }

    void SDLWindow::SetIcon(const std::string& iconPath)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(!iconPath.empty(), "Icon path cannot be empty");

        if (m_Window && !iconPath.empty())
            (void)ApplyWindowIcon(m_Window, iconPath, &m_Data.IconPath);
    }

    // Window state management
    WindowState SDLWindow::GetState() const
    {
        if (!m_Window) return WindowState::Normal;
        
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        if (flags & SDL_WINDOW_FULLSCREEN)
            return m_Data.FullscreenDesktop ? WindowState::FullscreenDesktop : WindowState::Fullscreen;
        if (flags & SDL_WINDOW_MINIMIZED) return WindowState::Minimized;
        if (flags & SDL_WINDOW_MAXIMIZED) return WindowState::Maximized;
        return WindowState::Normal;
    }

    void SDLWindow::SetState(WindowState state)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (!m_Window) return;
        
        switch (state)
        {
            case WindowState::Minimized:
                SDL_MinimizeWindow(m_Window);
                break;
            case WindowState::Maximized:
                SDL_MaximizeWindow(m_Window);
                break;
            case WindowState::Fullscreen:
                SDL_SetWindowFullscreen(m_Window, SDL_WINDOW_FULLSCREEN);
                break;
            case WindowState::FullscreenDesktop:
                SDL_SetWindowFullscreen(m_Window, SDL_WINDOW_FULLSCREEN);
                break;
            case WindowState::Normal:
                SDL_RestoreWindow(m_Window);
                break;
            default:
                std::string errorMsg = fmt::format("Unknown window state: {}", static_cast<int>(state));
                PlatformError error(errorMsg, std::source_location::current());
                error.SetFunctionName("SDLWindow::SetState");
                error.SetClassName("SDLWindow");
                error.SetModuleName("Platform/SDL");
                error.AddContext("state", std::to_string(static_cast<int>(state)));
                
                LT_CORE_ERROR("{}", errorMsg);
                Error::LogError(error);
                LT_THROW_PLATFORM_ERROR(errorMsg);
        }
    }

    void SDLWindow::Minimize() 
    { 
        LT_VERIFY(m_Window, "Window not initialized");
        SetState(WindowState::Minimized); 
    }
    
    void SDLWindow::Maximize() 
    { 
        LT_VERIFY(m_Window, "Window not initialized");
        SetState(WindowState::Maximized); 
    }
    
    void SDLWindow::Restore() 
    { 
        LT_VERIFY(m_Window, "Window not initialized");
        SetState(WindowState::Normal); 
    }
    
    void SDLWindow::Show() 
    { 
        LT_VERIFY(m_Window, "Window not initialized");
        if (m_Window) SDL_ShowWindow(m_Window); 
    }
    
    void SDLWindow::Hide() 
    { 
        LT_VERIFY(m_Window, "Window not initialized");
        if (m_Window) SDL_HideWindow(m_Window); 
    }

    bool SDLWindow::IsVisible() const
    {
        if (!m_Window) return false;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return !(flags & SDL_WINDOW_HIDDEN);
    }

    bool SDLWindow::IsMinimized() const
    {
        if (!m_Window) return false;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_MINIMIZED) != 0;
    }

    bool SDLWindow::IsMaximized() const
    {
        if (!m_Window) return false;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_MAXIMIZED) != 0;
    }

    // Fullscreen management
    void SDLWindow::SetFullscreen(bool fullscreen)
    {
        if (m_Window)
        {
            if (fullscreen)
                SDL_SetWindowFullscreen(m_Window, SDL_WINDOW_FULLSCREEN);
            else
                SDL_SetWindowFullscreen(m_Window, 0);
            m_Data.Fullscreen = fullscreen;
            if (!fullscreen)
                m_Data.FullscreenDesktop = false;
        }
    }

    void SDLWindow::SetFullscreenDesktop(bool fullscreen)
    {
        if (m_Window)
        {
            if (fullscreen)
                SDL_SetWindowFullscreen(m_Window, SDL_WINDOW_FULLSCREEN);
            else
                SDL_SetWindowFullscreen(m_Window, 0);
            m_Data.Fullscreen = fullscreen;
            m_Data.FullscreenDesktop = fullscreen;
        }
    }

    bool SDLWindow::IsFullscreenDesktop() const
    {
        if (!m_Window) return m_Data.FullscreenDesktop;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return ((flags & SDL_WINDOW_FULLSCREEN) != 0) && m_Data.FullscreenDesktop;
    }

    void SDLWindow::ToggleFullscreen()
    {
        if (IsFullscreen())
            SetFullscreen(false);
        else
            SetFullscreen(true);
    }

    // Window flags and properties
    void SDLWindow::SetFlags(WindowFlags flags)
    {
        m_Data.Flags = flags;
        ApplyWindowFlags();
    }

    void SDLWindow::SetResizable(bool resizable)
    {
        if (m_Window)
        {
            SDL_SetWindowResizable(m_Window, resizable ? true : false);
            m_Data.Resizable = resizable;
        }
    }

    void SDLWindow::SetBorderless(bool borderless)
    {
        if (m_Window)
        {
            SDL_SetWindowBordered(m_Window, borderless ? false : true);
            m_Data.Borderless = borderless;
        }
    }

    bool SDLWindow::IsBorderless() const
    {
        if (!m_Window) return m_Data.Borderless;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_BORDERLESS) != 0;
    }

    void SDLWindow::SetAlwaysOnTop(bool alwaysOnTop)
    {
        if (m_Window)
        {
            SDL_SetWindowAlwaysOnTop(m_Window, alwaysOnTop ? true : false);
            m_Data.AlwaysOnTop = alwaysOnTop;
        }
    }

    bool SDLWindow::IsAlwaysOnTop() const
    {
        if (!m_Window) return m_Data.AlwaysOnTop;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_ALWAYS_ON_TOP) != 0;
    }

    // Size constraints
    void SDLWindow::SetMinimumSize(uint32_t width, uint32_t height)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(width == 0 || width <= m_Data.Width, "Minimum width cannot be greater than current window width");
        LT_VERIFY(height == 0 || height <= m_Data.Height, "Minimum height cannot be greater than current window height");
        
        if (m_Window)
        {
            SDL_SetWindowMinimumSize(m_Window, static_cast<int>(width), static_cast<int>(height));
            m_Data.MinWidth = width;
            m_Data.MinHeight = height;
        }
    }

    void SDLWindow::SetMaximumSize(uint32_t width, uint32_t height)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(width == 0 || width >= m_Data.Width, "Maximum width cannot be less than current window width");
        LT_VERIFY(height == 0 || height >= m_Data.Height, "Maximum height cannot be less than current window height");
        
        if (m_Window)
        {
            SDL_SetWindowMaximumSize(m_Window, static_cast<int>(width), static_cast<int>(height));
            m_Data.MaxWidth = width;
            m_Data.MaxHeight = height;
        }
    }

    void SDLWindow::GetMinimumSize(uint32_t& width, uint32_t& height) const
    {
        width = m_Data.MinWidth;
        height = m_Data.MinHeight;
    }

    void SDLWindow::GetMaximumSize(uint32_t& width, uint32_t& height) const
    {
        width = m_Data.MaxWidth;
        height = m_Data.MaxHeight;
    }

    // VSync and rendering
    void SDLWindow::SetVSync(bool enabled)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (m_Window)
        {
            // In this engine, VSync is controlled via the active graphics context swap interval.
            if (m_Context)
            {
                const bool ok = m_Context->SetVSync(enabled);
                m_Data.VSync = ok ? m_Context->IsVSync() : enabled;
                LT_CORE_INFO("VSync set to: {} (requested {}, applied {})",
                    (m_Data.VSync ? "enabled" : "disabled"),
                    (enabled ? "enabled" : "disabled"),
                    (m_Data.VSync ? "enabled" : "disabled"));
            }
            else
            {
                // Fallback: store the requested value even if we can't apply it immediately.
                m_Data.VSync = enabled;
                LT_CORE_WARN("VSync requested but graphics context is not available yet");
            }
        }
    }

    void SDLWindow::SetHighDPI(bool enabled)
    {
        m_Data.HighDPI = enabled;
        // High DPI is typically set during window creation
    }

    bool SDLWindow::IsHighDPI() const
    {
        if (!m_Window) return m_Data.HighDPI;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0;
    }

    // Input focus and capture
    void SDLWindow::SetInputFocus()
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (m_Window)
            SDL_RaiseWindow(m_Window);
    }

    bool SDLWindow::HasInputFocus() const
    {
        if (!m_Window) return false;
        SDL_WindowFlags flags = SDL_GetWindowFlags(m_Window);
        return (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    }

    void SDLWindow::SetMouseCapture(bool capture)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        
        if (m_Window)
        {
            if (capture)
                SDL_CaptureMouse(true);
            else
                SDL_CaptureMouse(false);
        }
    }

    bool SDLWindow::IsMouseCaptured() const
    {
        return SDL_GetMouseState(nullptr, nullptr) != 0;
    }

    void SDLWindow::SetInputGrabbed(bool grabbed)
    {
        if (m_Window)
            SDL_SetWindowMouseGrab(m_Window, grabbed ? true : false);
    }

    bool SDLWindow::IsInputGrabbed() const
    {
        if (!m_Window) return false;
        return SDL_GetWindowMouseGrab(m_Window) == true;
    }

    // Display information
    int SDLWindow::GetDisplayIndex() const
    {
        if (!m_Window) return 0;
        return SDL_GetDisplayForWindow(m_Window);
    }

    DisplayMode SDLWindow::GetDisplayMode() const
    {
        if (!m_Window) return DisplayMode();
        
        const SDL_DisplayMode* mode = SDL_GetWindowFullscreenMode(m_Window);
        if (mode)
        {
            return DisplayMode(mode->w, mode->h, static_cast<uint32_t>(mode->refresh_rate), mode->format);
        }
        return DisplayMode();
    }

    std::vector<DisplayMode> SDLWindow::GetAvailableDisplayModes() const
    {
        std::vector<DisplayMode> modes;
        if (!m_Window) return modes;
        
        SDL_DisplayID displayID = SDL_GetDisplayForWindow(m_Window);
        int count;
        SDL_DisplayMode** displayModes = SDL_GetFullscreenDisplayModes(displayID, &count);
        
        if (displayModes)
        {
            for (int i = 0; i < count; ++i)
            {
                if (displayModes[i])
                {
                    modes.emplace_back(displayModes[i]->w, displayModes[i]->h, 
                                     static_cast<uint32_t>(displayModes[i]->refresh_rate), 
                                     displayModes[i]->format);
                }
            }
            SDL_free(displayModes);
        }
        
        return modes;
    }

    void SDLWindow::SetDisplayMode(const DisplayMode& mode)
    {
        if (!m_Window) return;
        
        SDL_DisplayMode sdlMode;
        sdlMode.w = static_cast<int>(mode.width);
        sdlMode.h = static_cast<int>(mode.height);
        sdlMode.refresh_rate = static_cast<float>(mode.refreshRate);
        sdlMode.format = static_cast<SDL_PixelFormat>(mode.format);
        
        SDL_SetWindowFullscreenMode(m_Window, &sdlMode);
    }

    float SDLWindow::GetDisplayScale() const
    {
        if (!m_Window) return 1.0f;
        
        SDL_DisplayID displayID = SDL_GetDisplayForWindow(m_Window);
        return SDL_GetDisplayContentScale(displayID);
    }

    void SDLWindow::GetDrawableSize(uint32_t& width, uint32_t& height) const
    {
        if (!m_Window)
        {
            width = height = 0;
            return;
        }
        
        int w, h;
        SDL_GetWindowSizeInPixels(m_Window, &w, &h);
        width = static_cast<uint32_t>(w);
        height = static_cast<uint32_t>(h);
    }

    void SDLWindow::GetWindowSize(int* width, int* height) const
    {
        if (m_Window)
            SDL_GetWindowSize(m_Window, width, height);
    }

    // Utility methods
    void SDLWindow::Flash()
    {
        if (m_Window)
            SDL_FlashWindow(m_Window, SDL_FLASH_BRIEFLY);
    }

    void SDLWindow::RequestAttention()
    {
        if (m_Window)
            SDL_FlashWindow(m_Window, SDL_FLASH_UNTIL_FOCUSED);
    }

    void SDLWindow::SetOpacity(float opacity)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        LT_VERIFY(opacity >= 0.0f && opacity <= 1.0f, "Opacity must be between 0.0 and 1.0");
        
        if (m_Window)
        {
            SDL_SetWindowOpacity(m_Window, opacity);
            m_Data.Opacity = opacity;
        }
    }

    float SDLWindow::GetOpacity() const
    {
        if (!m_Window) return m_Data.Opacity;
        
        return SDL_GetWindowOpacity(m_Window);
    }

    void SDLWindow::SetBrightness(float brightness)
    {
        m_Data.Brightness = brightness;
        // Note: SDL doesn't have a direct brightness API for windows
    }

    float SDLWindow::GetBrightness() const
    {
        return m_Data.Brightness;
    }

    // Window information
    std::string SDLWindow::GetWindowID() const
    {
        if (!m_Window) return "";
        return std::to_string(SDL_GetWindowID(m_Window));
    }

    bool SDLWindow::IsForeign() const
    {
        if (!m_Window) return false;
        SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(SDL_GetWindowFlags(m_Window));
        return (flags & SDL_WINDOW_EXTERNAL) != 0; // SDL3 renamed this
    }
    
    // Clipboard operations
    void SDLWindow::SetClipboardText(const std::string& text)
    {
        LT_VERIFY(!text.empty(), "Clipboard text cannot be empty");
        
        if (SDL_SetClipboardText(text.c_str()) != 0)
        {
            std::string errorMsg = fmt::format("Failed to set clipboard text: {}", SDL_GetError());
            PlatformError error(errorMsg, std::source_location::current());
            error.SetFunctionName("SDLWindow::SetClipboardText");
            error.SetClassName("SDLWindow");
            error.SetModuleName("Platform/SDL");
            error.AddContext("text_length", std::to_string(text.length()));
            
            LT_CORE_ERROR("{}", errorMsg);
            Error::LogError(error);
            LT_THROW_PLATFORM_ERROR(errorMsg);
        }
    }

    std::string SDLWindow::GetClipboardText() const
    {
        char* text = SDL_GetClipboardText();
        if (text)
        {
            std::string result(text);
            SDL_free(text);
            return result;
        }
        
        // If no text in clipboard, return empty string (not an error)
        return "";
    }

    bool SDLWindow::HasClipboardText() const
    {
        return SDL_HasClipboardText() == true;
    }

    // Cursor management
    void SDLWindow::SetCursor(void* cursor)
    {
        if (m_Window && cursor)
            SDL_SetCursor(static_cast<SDL_Cursor*>(cursor));
    }

    void SDLWindow::SetCursorVisible(bool visible)
    {
        m_DesiredCursorVisible = visible;
        if (m_CursorLocked)
        {
            // Relative mouse mode effectively hides the cursor. Keep user intent for unlock.
            SDL_HideCursor();
            return;
        }

        if (visible)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }

    bool SDLWindow::IsCursorVisible() const
    {
        return SDL_CursorVisible();
    }

    void SDLWindow::SetCursorLocked(bool locked)
    {
        LT_VERIFY(m_Window, "Window not initialized");

        const bool currentLocked = SDL_GetWindowRelativeMouseMode(m_Window);
        if (currentLocked == locked)
        {
            m_CursorLocked = currentLocked;
            return;
        }

        // SDL relative mouse mode constrains the cursor and reports relative movement.
        // SDL also hides the cursor in this mode (see SDL docs).
        const bool ok = SDL_SetWindowRelativeMouseMode(m_Window, locked);
        if (!ok)
        {
            LT_CORE_WARN("Failed to set relative mouse mode to {} (cursor lock)", locked ? "enabled" : "disabled");
            return;
        }

        m_CursorLocked = locked;

        if (m_CursorLocked)
        {
            SDL_HideCursor();
        }
        else
        {
            // Restore desired visibility state.
            SetCursorVisible(m_DesiredCursorVisible);
        }
    }

    bool SDLWindow::IsCursorLocked() const
    {
        if (!m_Window)
        {
            return m_CursorLocked;
        }

        // SDL is the source of truth when initialized.
        return SDL_GetWindowRelativeMouseMode(m_Window);
    }

    void SDLWindow::SetCursorPosition(int x, int y)
    {
        LT_VERIFY(m_Window, "Window not initialized");
        SDL_WarpMouseInWindow(m_Window, static_cast<float>(x), static_cast<float>(y));
        GetInputSystem().NotifyMouseWarped();
    }

    void SDLWindow::GetCursorPosition(int& x, int& y) const
    {
        float fx, fy;
        SDL_GetMouseState(&fx, &fy);
        x = static_cast<int>(fx);
        y = static_cast<int>(fy);
    }

    // Window hints (platform-specific)
    void SDLWindow::SetHint(const std::string& name, const std::string& value)
    {
        SDL_SetHint(name.c_str(), value.c_str());
    }

    std::string SDLWindow::GetHint(const std::string& name) const
    {
        const char* value = SDL_GetHint(name.c_str());
        return value ? std::string(value) : "";
    }

    // Helper methods
    void SDLWindow::UpdateWindowFlags()
    {
        if (!m_Window) return;
        
        SDL_WindowFlags currentFlags = static_cast<SDL_WindowFlags>(SDL_GetWindowFlags(m_Window));
        m_Data.Flags = ConvertFromSDLFags(currentFlags);
    }

    void SDLWindow::ApplyWindowFlags()
    {
        // Note: Most flags need to be set during window creation
        // This method is mainly for flags that can be changed at runtime
    }

    SDL_WindowFlags SDLWindow::ConvertToSDLFags(WindowFlags flags) const
    {
        SDL_WindowFlags sdlFlags = 0;
        
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::Fullscreen))
            sdlFlags |= SDL_WINDOW_FULLSCREEN;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::FullscreenDesktop))
            sdlFlags |= SDL_WINDOW_FULLSCREEN; // SDL3 uses same flag for both
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::Resizable))
            sdlFlags |= SDL_WINDOW_RESIZABLE;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::Hidden))
            sdlFlags |= SDL_WINDOW_HIDDEN;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::Borderless))
            sdlFlags |= SDL_WINDOW_BORDERLESS;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::AlwaysOnTop))
            sdlFlags |= SDL_WINDOW_ALWAYS_ON_TOP;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::AllowHighDPI))
            sdlFlags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::InputGrabbed))
            sdlFlags |= SDL_WINDOW_MOUSE_GRABBED;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::InputFocus))
            sdlFlags |= SDL_WINDOW_INPUT_FOCUS;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::MouseFocus))
            sdlFlags |= SDL_WINDOW_MOUSE_FOCUS;
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::Foreign))
            sdlFlags |= SDL_WINDOW_EXTERNAL; // SDL3 renamed this
        if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(WindowFlags::MouseCapture))
            sdlFlags |= SDL_WINDOW_MOUSE_CAPTURE;
        // Note: From my knowledge SDL_WINDOW_ALWAYS_ON_TOP_HINT and SDL_WINDOW_BYPASS_WINDOW_MANAGER don't exist in SDL3
            
        return sdlFlags;
    }

    WindowFlags SDLWindow::ConvertFromSDLFags(SDL_WindowFlags flags) const
    {
        WindowFlags windowFlags = WindowFlags::None;
        
        if (flags & SDL_WINDOW_FULLSCREEN)
            windowFlags |= WindowFlags::Fullscreen;
        // Note: SDL3 doesn't distinguish between fullscreen and fullscreen desktop
        if (flags & SDL_WINDOW_RESIZABLE)
            windowFlags |= WindowFlags::Resizable;
        if (flags & SDL_WINDOW_HIDDEN)
            windowFlags |= WindowFlags::Hidden;
        if (flags & SDL_WINDOW_BORDERLESS)
            windowFlags |= WindowFlags::Borderless;
        if (flags & SDL_WINDOW_ALWAYS_ON_TOP)
            windowFlags |= WindowFlags::AlwaysOnTop;
        if (flags & SDL_WINDOW_HIGH_PIXEL_DENSITY)
            windowFlags |= WindowFlags::AllowHighDPI;
        if (flags & SDL_WINDOW_MOUSE_GRABBED)
            windowFlags |= WindowFlags::InputGrabbed;
        if (flags & SDL_WINDOW_INPUT_FOCUS)
            windowFlags |= WindowFlags::InputFocus;
        if (flags & SDL_WINDOW_MOUSE_FOCUS)
            windowFlags |= WindowFlags::MouseFocus;
        if (flags & SDL_WINDOW_EXTERNAL)
            windowFlags |= WindowFlags::Foreign; // SDL3 renamed this
        if (flags & SDL_WINDOW_MOUSE_CAPTURE)
            windowFlags |= WindowFlags::MouseCapture;
            
        return windowFlags;
    }

    void SDLWindow::OnWindowConfigChanged(Events::WindowConfigChangedEvent& event)
    {
        // Handle window configuration changes
        const std::string& key = event.GetChangedKey();
        const ConfigValue& value = event.GetNewValue();
        
        // Convert value to string for logging
        std::string valueStr = std::visit([](const auto& v) -> std::string {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>)
                return v;
            else if constexpr (std::is_same_v<V, bool>)
                return v ? "true" : "false";
            else if constexpr (std::is_same_v<V, int> || std::is_same_v<V, float> || std::is_same_v<V, double> || std::is_same_v<V, size_t> || std::is_same_v<V, uint32_t>)
                return std::to_string(v);
            else
                return "unknown";
        }, value);
        
        LT_CORE_INFO("Window configuration changed: {} = {}", key, valueStr);
        
        // Apply the configuration change to the window
        if (key == "window.title")
        {
            try {
                std::string title = std::get<std::string>(value);
                SetTitle(title);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get string value for window.title");
            }
        }
        else if (key == "window.width" || key == "window.height")
        {
            // Get current size and update the changed dimension
            uint32_t currentWidth = m_Data.Width;
            uint32_t currentHeight = m_Data.Height;
            
            try {
                if (key == "window.width") {
                    uint32_t width = std::get<uint32_t>(value);
                    currentWidth = width;
                } else if (key == "window.height") {
                    uint32_t height = std::get<uint32_t>(value);
                    currentHeight = height;
                }
                
                SetSize(currentWidth, currentHeight);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get integer value for {}", key);
            }
        }
        else if (key == "window.fullscreen")
        {
            try {
                bool fullscreen = std::get<bool>(value);
                SetFullscreen(fullscreen);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.fullscreen");
            }
        }
        else if (key == "window.resizable")
        {
            try {
                bool resizable = std::get<bool>(value);
                SetResizable(resizable);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.resizable");
            }
        }
        else if (key == "window.vsync")
        {
            try {
                bool vsync = std::get<bool>(value);
                SetVSync(vsync);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.vsync");
            }
        }
        else if (key == "window.borderless")
        {
            try {
                bool borderless = std::get<bool>(value);
                SetBorderless(borderless);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.borderless");
            }
        }
        else if (key == "window.always_on_top")
        {
            try {
                bool alwaysOnTop = std::get<bool>(value);
                SetAlwaysOnTop(alwaysOnTop);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.always_on_top");
            }
        }
        else if (key == "window.position.x" || key == "window.position.y")
        {
            // Get current position and update the changed coordinate
            int currentX = m_Data.PositionX;
            int currentY = m_Data.PositionY;
            
            try {
                if (key == "window.position.x") {
                    currentX = std::get<int>(value);
                } else if (key == "window.position.y") {
                    currentY = std::get<int>(value);
                }
                
                SetPosition(currentX, currentY);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get integer value for {}", key);
            }
        }
        else if (key == "window.min_width" || key == "window.min_height")
        {
            // Get current min size and update the changed dimension
            uint32_t currentMinWidth = m_Data.MinWidth;
            uint32_t currentMinHeight = m_Data.MinHeight;
            
            try {
                if (key == "window.min_width") {
                    int minWidth = std::get<int>(value);
                    currentMinWidth = static_cast<uint32_t>(minWidth);
                } else if (key == "window.min_height") {
                    int minHeight = std::get<int>(value);
                    currentMinHeight = static_cast<uint32_t>(minHeight);
                }
                
                SetMinimumSize(currentMinWidth, currentMinHeight);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get integer value for {}", key);
            }
        }
        else if (key == "window.max_width" || key == "window.max_height")
        {
            // Get current max size and update the changed dimension
            uint32_t currentMaxWidth = m_Data.MaxWidth;
            uint32_t currentMaxHeight = m_Data.MaxHeight;
            
            try {
                if (key == "window.max_width") {
                    int maxWidth = std::get<int>(value);
                    currentMaxWidth = static_cast<uint32_t>(maxWidth);
                } else if (key == "window.max_height") {
                    int maxHeight = std::get<int>(value);
                    currentMaxHeight = static_cast<uint32_t>(maxHeight);
                }
                
                SetMaximumSize(currentMaxWidth, currentMaxHeight);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get integer value for {}", key);
            }
        }
        else if (key == "window.high_dpi")
        {
            try {
                bool highDpi = std::get<bool>(value);
                SetHighDPI(highDpi);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get boolean value for window.high_dpi");
            }
        }
        else if (key == "window.icon")
        {
            try {
                std::string iconPath = std::get<std::string>(value);
                SetIcon(iconPath);
            } catch (const std::bad_variant_access&) {
                LT_CORE_WARN("Failed to get string value for window.icon");
            }
        }
        
        LT_CORE_INFO("Window configuration change applied successfully");
    }
} 