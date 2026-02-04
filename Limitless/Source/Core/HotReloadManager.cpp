#include "HotReloadManager.h"
#include "Core/Debug/Log.h"
#include "Core/ConfigManager.h"
#include "Core/FileWatcher.h"
#include "EventSystem.h"
#include <iostream>
#include <optional>
#include <limits>
#include <cmath>
#include <type_traits>

namespace Limitless
{
    HotReloadManager& HotReloadManager::GetInstance()
    {
        static HotReloadManager instance;
        return instance;
    }

    void HotReloadManager::Initialize()
    {
        // Subscribe to configuration changes
        auto& config = Limitless::ConfigManager::GetInstance();
        
        // Set up callbacks for configuration changes
        config.RegisterChangeCallback("logging.level", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.file_enabled", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.console_enabled", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });
        config.RegisterChangeCallback("logging.pattern", [this](const std::string& key, const ConfigValue& value) { OnLoggingConfigChanged(key, value); });

        // Window hot reload keys (applied on the main thread via HotReloadManager::Update()).
        config.RegisterChangeCallback(Config::Window::WIDTH, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::HEIGHT, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::TITLE, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::FULLSCREEN, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::RESIZABLE, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::VSYNC, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::POSITION_X, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::POSITION_Y, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::BORDERLESS, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::ALWAYS_ON_TOP, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::MIN_WIDTH, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::MIN_HEIGHT, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::MAX_WIDTH, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::MAX_HEIGHT, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::HIGH_DPI, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        config.RegisterChangeCallback(Config::Window::ICON, [this](const std::string& key, const ConfigValue& value) { OnWindowConfigChanged(key, value); });
        
        LT_CORE_INFO("HotReloadManager: Initialized with configuration change callbacks");
    }

    void HotReloadManager::Shutdown()
    {
        if (m_Enabled) { EnableHotReload(false); }
        LT_CORE_INFO("HotReloadManager: Shutdown complete");
    }

    void HotReloadManager::EnableHotReload(bool enable)
    {
        if (m_Enabled == enable) return;
        m_Enabled = enable;
        auto& config = ConfigManager::GetInstance();
        config.EnableHotReload(enable);
        LT_CORE_INFO("HotReloadManager: Hot reload {}", (enable ? "enabled" : "disabled"));
    }

    void HotReloadManager::OnLoggingConfigChanged(const std::string& key, const ConfigValue& value)
    {
        LT_CORE_INFO("HotReloadManager: Logging configuration changed - {}", key);

        // Config hot reload runs on the file watcher thread.
        // Queue work for the main thread to keep logging reinit safe and deterministic.
        (void)value;
        {
            std::scoped_lock lock(m_PendingMutex);
            m_PendingLoggingReinitialize = true;
        }
    }

    void HotReloadManager::OnWindowConfigChanged(const std::string& key, const ConfigValue& value)
    {
        LT_CORE_INFO("HotReloadManager: Window configuration changed - {}", key);

        // Queue the change for the main thread. Window APIs must not be called from the file watcher thread.
        {
            std::scoped_lock lock(m_PendingMutex);
            m_PendingWindowChanges.push_back(PendingConfigChange{ key, value });
        }
    }

    void HotReloadManager::Update()
    {
        if (!m_Enabled)
            return;

        std::vector<PendingConfigChange> windowChanges;
        bool shouldReinitializeLogging = false;
        {
            std::scoped_lock lock(m_PendingMutex);
            windowChanges.swap(m_PendingWindowChanges);
            shouldReinitializeLogging = m_PendingLoggingReinitialize;
            m_PendingLoggingReinitialize = false;
        }

        if (shouldReinitializeLogging)
        {
            ReinitializeLogging();
        }

        if (!m_Window)
        {
            if (!windowChanges.empty())
            {
                LT_CORE_WARN("HotReloadManager: Received window config changes but no window is registered");
            }
            return;
        }

        for (const auto& change : windowChanges)
        {
            ApplyWindowConfigChange(change.key, change.value);
        }
    }

    namespace
    {
        std::optional<bool> TryGetBool(const ConfigValue& value)
        {
            return std::visit([](const auto& v) -> std::optional<bool> {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, bool>) return v;
                return std::nullopt;
            }, value);
        }

        std::optional<std::string> TryGetString(const ConfigValue& value)
        {
            return std::visit([](const auto& v) -> std::optional<std::string> {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, std::string>) return v;
                return std::nullopt;
            }, value);
        }

        template<typename T>
        std::optional<T> TryGetIntegralClamped(const ConfigValue& value)
        {
            static_assert(std::is_integral_v<T>);

            return std::visit([](const auto& v) -> std::optional<T> {
                using V = std::decay_t<decltype(v)>;
                constexpr long long minV = static_cast<long long>(std::numeric_limits<T>::min());
                constexpr long long maxV = static_cast<long long>(std::numeric_limits<T>::max());

                if constexpr (std::is_same_v<V, int>)
                {
                    long long vv = static_cast<long long>(v);
                    if (vv < minV || vv > maxV) return std::nullopt;
                    return static_cast<T>(v);
                }
                else if constexpr (std::is_same_v<V, size_t>)
                {
                    if (v > static_cast<size_t>(maxV)) return std::nullopt;
                    return static_cast<T>(v);
                }
                else if constexpr (std::is_same_v<V, uint32_t>)
                {
                    long long vv = static_cast<long long>(v);
                    if (vv < minV || vv > maxV) return std::nullopt;
                    return static_cast<T>(v);
                }
                else if constexpr (std::is_same_v<V, float> || std::is_same_v<V, double>)
                {
                    double dv = static_cast<double>(v);
                    if (!std::isfinite(dv)) return std::nullopt;
                    if (dv < static_cast<double>(minV) || dv > static_cast<double>(maxV)) return std::nullopt;
                    return static_cast<T>(dv);
                }
                else
                {
                    return std::nullopt;
                }
            }, value);
        }
    }

    void HotReloadManager::ApplyWindowConfigChange(const std::string& key, const ConfigValue& value)
    {
        if (!m_Window)
            return;

        const std::string valueStr = std::visit([](const auto& v) -> std::string {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>)
                return v;
            else if constexpr (std::is_same_v<V, bool>)
                return v ? "true" : "false";
            else
                return std::to_string(v);
        }, value);

        LT_CORE_INFO("HotReloadManager: Applying window config diff: {} = {}", key, valueStr);

        if (key == Config::Window::TITLE)
        {
            if (auto title = TryGetString(value))
                m_Window->SetTitle(*title);
            else
                LT_CORE_WARN("HotReloadManager: window.title type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::WIDTH || key == Config::Window::HEIGHT)
        {
            uint32_t width = m_Window->GetWidth();
            uint32_t height = m_Window->GetHeight();

            if (key == Config::Window::WIDTH)
            {
                if (auto w = TryGetIntegralClamped<uint32_t>(value)) width = *w;
                else { LT_CORE_WARN("HotReloadManager: window.width type mismatch"); return; }
            }
            else
            {
                if (auto h = TryGetIntegralClamped<uint32_t>(value)) height = *h;
                else { LT_CORE_WARN("HotReloadManager: window.height type mismatch"); return; }
            }

            m_Window->SetSize(width, height);
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::FULLSCREEN)
        {
            if (auto fullscreen = TryGetBool(value))
                m_Window->SetFullscreen(*fullscreen);
            else
                LT_CORE_WARN("HotReloadManager: window.fullscreen type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::RESIZABLE)
        {
            if (auto resizable = TryGetBool(value))
                m_Window->SetResizable(*resizable);
            else
                LT_CORE_WARN("HotReloadManager: window.resizable type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::VSYNC)
        {
            if (auto vsync = TryGetBool(value))
                m_Window->SetVSync(*vsync);
            else
                LT_CORE_WARN("HotReloadManager: window.vsync type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::BORDERLESS)
        {
            if (auto borderless = TryGetBool(value))
                m_Window->SetBorderless(*borderless);
            else
                LT_CORE_WARN("HotReloadManager: window.borderless type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::ALWAYS_ON_TOP)
        {
            if (auto alwaysOnTop = TryGetBool(value))
                m_Window->SetAlwaysOnTop(*alwaysOnTop);
            else
                LT_CORE_WARN("HotReloadManager: window.always_on_top type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::POSITION_X || key == Config::Window::POSITION_Y)
        {
            int x = 0, y = 0;
            m_Window->GetPosition(x, y);

            if (key == Config::Window::POSITION_X)
            {
                if (auto nx = TryGetIntegralClamped<int>(value)) x = *nx;
                else { LT_CORE_WARN("HotReloadManager: window.position.x type mismatch"); return; }
            }
            else
            {
                if (auto ny = TryGetIntegralClamped<int>(value)) y = *ny;
                else { LT_CORE_WARN("HotReloadManager: window.position.y type mismatch"); return; }
            }

            m_Window->SetPosition(x, y);
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::MIN_WIDTH || key == Config::Window::MIN_HEIGHT)
        {
            uint32_t minWidth = 0, minHeight = 0;
            m_Window->GetMinimumSize(minWidth, minHeight);

            if (key == Config::Window::MIN_WIDTH)
            {
                if (auto w = TryGetIntegralClamped<uint32_t>(value)) minWidth = *w;
                else { LT_CORE_WARN("HotReloadManager: window.min_width type mismatch"); return; }
            }
            else
            {
                if (auto h = TryGetIntegralClamped<uint32_t>(value)) minHeight = *h;
                else { LT_CORE_WARN("HotReloadManager: window.min_height type mismatch"); return; }
            }

            m_Window->SetMinimumSize(minWidth, minHeight);
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::MAX_WIDTH || key == Config::Window::MAX_HEIGHT)
        {
            uint32_t maxWidth = 0, maxHeight = 0;
            m_Window->GetMaximumSize(maxWidth, maxHeight);

            if (key == Config::Window::MAX_WIDTH)
            {
                if (auto w = TryGetIntegralClamped<uint32_t>(value)) maxWidth = *w;
                else { LT_CORE_WARN("HotReloadManager: window.max_width type mismatch"); return; }
            }
            else
            {
                if (auto h = TryGetIntegralClamped<uint32_t>(value)) maxHeight = *h;
                else { LT_CORE_WARN("HotReloadManager: window.max_height type mismatch"); return; }
            }

            m_Window->SetMaximumSize(maxWidth, maxHeight);
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::HIGH_DPI)
        {
            if (auto highDpi = TryGetBool(value))
                m_Window->SetHighDPI(*highDpi);
            else
                LT_CORE_WARN("HotReloadManager: window.high_dpi type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        if (key == Config::Window::ICON)
        {
            if (auto icon = TryGetString(value))
                m_Window->SetIcon(*icon);
            else
                LT_CORE_WARN("HotReloadManager: window.icon type mismatch");
            LT_CORE_INFO("HotReloadManager: Window config applied successfully: {}", key);
            return;
        }

        LT_CORE_DEBUG("HotReloadManager: Unhandled window config key: {}", key);
    }

    void HotReloadManager::ReinitializeLogging()
    {
        LT_CORE_INFO("HotReloadManager: Reinitializing logging system...");
        Log::Shutdown();
        Log::InitFromConfig();
        LT_CORE_INFO("HotReloadManager: Logging system reinitialized");
    }

    void HotReloadManager::SetWindow(Window* window)
    {
        m_Window = window;
    }
}