# LimitlessRemaster

A modern, enterprise-grade C++ engine with comprehensive advanced systems, extended Window API, and robust CI/CD setup.

## 🚀 **What's New in This Version**

### **Advanced Systems Added:**
- **🔧 Configuration Management System** - Centralized JSON-based configuration with validation and hot reloading
- **📡 Event System** - Observer pattern implementation with priority handling and filtering
monitoring
- **🪟 Extended Window API** - Comprehensive window management with advanced features and cross-platform support

### **Core Improvements:**
- **🛡️ Enhanced Memory Safety** - Smart pointers throughout, RAII patterns, exception safety
- **🔧 Better Architecture** - Modular design, loose coupling, configuration-driven systems
- **📚 Comprehensive Documentation** - Detailed guides for all systems and best practices
- **🌐 Cross-Platform Excellence** - Native support for Windows, macOS, and Linux with platform-specific optimizations

## 🏗️ **Project Structure**

```
LimitlessRemaster/
├── Limitless/          # Core engine library with advanced systems
│   ├── Source/
│   │   ├── Core/       # Core systems (Logging, Config, Resources, Events, etc.)
│   │   │   └── Debug/  # Debug and profiling systems
│   │   └── Platform/   # Platform abstraction (SDL-based with extended Window API)
│   │       └── SDL/    # SDL3 implementation
│   └── Vendor/         # Third-party dependencies
├── Sandbox/           # Example application demonstrating all systems
├── Test/              # Comprehensive unit tests for all systems
├── Scripts/           # Build scripts for all platforms
├── Vendor/            # Third-party dependencies
├── .github/workflows/ # GitHub Actions CI/CD
└── docs/              # Comprehensive documentation
```

## 🎯 **Key Features**

### **Core Systems**
- **Modern C++20** - Latest language features and best practices
- **Cross-Platform** - Windows, macOS, Linux support with native optimizations
- **Comprehensive Logging** - Multi-level logging with file rotation and conditional logging
- **Error Handling** - Structured error management with custom exceptions and recovery
- **Performance Monitoring** - Real-time performance tracking and analysis with profiling

### **Advanced Systems**
- **Configuration Management** - Type-safe configuration with validation, hot reloading, and environment/command-line support
- **Event System** - Event-driven architecture with priority handling, filtering, and deferred processing

### **Extended Window API**
- **Comprehensive Window Management** - Full control over window properties, state, and behavior
- **Advanced Display Support** - Multi-monitor support, display mode management, and High DPI
- **Input Management** - Mouse capture, keyboard focus, and input grabbing
- **Window States** - Minimize, maximize, fullscreen, and custom window states
- **Visual Effects** - Opacity, brightness, gamma correction, and window flashing
- **Clipboard Operations** - Cross-platform clipboard text management
- **Cursor Management** - Custom cursors, visibility control, and position management
- **Event Callbacks** - Comprehensive event system for window state changes
- **Platform Hints** - Platform-specific window behavior customization

### **Development Tools**
- **Comprehensive Testing** - Unit tests for all systems using doctest with proper isolation
- **CI/CD Pipeline** - Automated builds and tests across all platforms with artifact preservation
- **Code Quality** - Clang-format, clang-tidy, and static analysis integration
- **Documentation** - Detailed guides, examples, and API documentation for all systems

## 📦 **Dependencies**

- **Premake5**: Build system generator with cross-platform support
- **doctest**: Modern, header-only unit testing framework
- **spdlog**: High-performance logging library with rotation and formatting
- **nlohmann/json**: JSON library for configuration and serialization
- **SDL3**: Cross-platform windowing, input, and multimedia support

## 🚀 **Quick Start**

### **Building**

#### Windows
```batch
Scripts\build-windows.bat [Debug|Release|Dist] [x64|ARM64]
```

#### Unix/Linux/macOS
```bash
Scripts/build-unix.sh --config Debug --compiler gcc
Scripts/build-unix.sh --config Release --compiler clang
```

#### Using Premake Directly
```bash
# Generate Visual Studio solution
Vendor/Premake/premake5 vs2022

# Generate Makefiles
Vendor/Premake/premake5 gmake2

# Build with make
make -j$(nproc) config=Debug_x64
```

### **Running Tests**
```bash
# Run all tests
./Build/Debug_x64/Test/Test --success

# Run with verbose output
./Build/Debug_x64/Test/Test --success --verbose

# Run specific test suites
./Build/Debug_x64/Test/Test --success --test-suite="Advanced Systems Tests"
```

### **Basic Usage**

```cpp
#include "Limitless.h"

class MyApp : public Limitless::Application
{
public:
    bool Initialize() override
    {
        // Configuration is already initialized
        auto& config = GetConfigManager();
        
        // Configure window with advanced features
        config.SetValue("window.width", 1920);
        config.SetValue("window.height", 1080);
        config.SetValue("window.fullscreen", false);
        config.SetValue("window.borderless", false);
        config.SetValue("window.always_on_top", false);
        config.SetValue("window.high_dpi", true);
        config.SetValue("window.min_width", 800);
        config.SetValue("window.min_height", 600);
        config.SetValue("window.position.x", 100);
        config.SetValue("window.position.y", 100);
        
        // Configure graphics
        config.SetValue("graphics.vsync", true);
        config.SetValue("graphics.antialiasing", 4);
    
        // Register event handlers
        auto& eventSystem = GetEventSystem();
        eventSystem.AddCallback(Limitless::EventType::KeyPressed, [this](Limitless::Event& event) {
            // Handle key press
        });
        
        // Set up window callbacks
        auto& window = GetWindow();
        window.SetResizeCallback([this](uint32_t width, uint32_t height) {
            // Handle window resize
        });
        
        window.SetFocusCallback([this](bool focused) {
            // Handle focus changes
        });
        
        return true;
    }
    
    void OnUpdate(float deltaTime) override
    {
        // Process events
        GetEventSystem().ProcessEvents();
    }
    
    void OnRender() override
    {
        // Rendering code
    }
};

// Entry point
int main()
{
    MyApp app;
    app.Run();
    return 0;
}
```

### **Advanced Window Usage**

```cpp
// Create window with advanced properties
Limitless::WindowProps props;
props.Title = "My Game";
props.Width = 1920;
props.Height = 1080;
props.Fullscreen = false;
props.Resizable = true;
props.Borderless = false;
props.AlwaysOnTop = false;
props.HighDPI = true;
props.PositionX = 100;
props.PositionY = 100;
props.MinWidth = 800;
props.MinHeight = 600;
props.Flags = Limitless::WindowFlags::Resizable | 
              Limitless::WindowFlags::AllowHighDPI;

auto window = Limitless::Window::Create(props);

// Advanced window operations
window->CenterOnScreen();
window->SetOpacity(0.9f);
window->SetBrightness(1.2f);
window->Flash();
window->RequestAttention();

// Display management
auto displayModes = window->GetAvailableDisplayModes();
auto currentMode = window->GetDisplayMode();
float scale = window->GetDisplayScale();

// Input management
window->SetInputFocus();
window->SetMouseCapture(true);
window->SetInputGrabbed(true);

// Clipboard operations
window->SetClipboardText("Hello, World!");
std::string text = window->GetClipboardText();

// Event callbacks
window->SetResizeCallback([](uint32_t width, uint32_t height) {
    // Handle resize
});

window->SetMoveCallback([](int x, int y) {
    // Handle move
});

window->SetFocusCallback([](bool focused) {
    // Handle focus
});

// GLM Integration Ready - Simple parameter passing works great with GLM
uint32_t width, height;
window->GetSize(width, height);
// Can easily convert to glm::ivec2 or glm::uvec2

int x, y;
window->GetPosition(x, y);
// Can easily convert to glm::ivec2

// Size constraints
uint32_t minWidth, minHeight, maxWidth, maxHeight;
window->GetMinimumSize(minWidth, minHeight);
window->GetMaximumSize(maxWidth, maxHeight);
```

## 📚 **Documentation**

### **System Guides**
- **[Logging and Error Handling Guide](LOGGING_GUIDE.md)** - Detailed logging system documentation
- **[Configuration Management](docs/CONFIGURATION_GUIDE.md)** - Configuration system usage and best practices
- **[Event System](docs/EVENT_GUIDE.md)** - Event-driven architecture and patterns
- **[Window API Guide](docs/WINDOW_API_GUIDE.md)** - Comprehensive window management and advanced features

### **API Reference**
- **[Core API](docs/API/CORE.md)** - Core system interfaces and classes
- **[Configuration API](docs/API/CONFIG.md)** - Configuration management interfaces
- **[Event API](docs/API/EVENT.md)** - Event system interfaces
- **[Window API](docs/API/WINDOW.md)** - Extended window management interfaces

## 🔧 **Continuous Integration**

This project includes comprehensive GitHub Actions CI/CD workflows:

### **Windows CI/CD (`test-windows.yml`)**
- **Robust Premake5 Setup**: Uses beta2 version with fallback logic (vs2022 → vs2019 → vs2017)
- **Comprehensive Error Handling**: Proper exit code checking and detailed error messages
- **Build Verification**: MSBuild with detailed output and project reference building
- **Test Execution**: Automatic test running with proper error handling
- **Artifact Upload**: Preserves build outputs and solution files
- **Platform Support**: x64 and ARM64 architecture support

### **Linux CI/CD (`test-linux.yml`)**
- **Efficient SDL3 Installation**: Package manager first, source build fallback
- **Comprehensive Dependencies**: All required system libraries installed
- **Multi-threaded Build**: Uses `$(nproc)` for optimal build performance
- **Robust Error Handling**: Proper exit code checking and fallback mechanisms
- **Test Integration**: Automatic test discovery and execution
- **Artifact Management**: Preserves build outputs and Makefiles

### **macOS CI/CD (`test-macos.yml`)**
- **Dual Architecture Support**: Tests both ARM64 and x64 builds
- **Homebrew Integration**: Clean SDL3 installation via Homebrew
- **Comprehensive Testing**: Separate test execution for each architecture
- **Performance Optimization**: Uses `$(sysctl -n hw.ncpu)` for optimal CPU usage
- **Detailed Output**: Separate build and test reporting for each architecture
- **Artifact Management**: Preserves all build outputs

### **Features**
- **Multi-platform**: Windows, macOS, Linux
- **Multi-compiler**: MSVC, GCC, Clang
- **Multi-architecture**: x64, ARM64
- **Code Quality**: Formatting, static analysis
- **Security**: Memory error detection
- **Caching**: Faster incremental builds
- **Artifacts**: Downloadable build outputs

## 🎯 **Use Cases**

### **Game Development**
- **Advanced Window Management**: Full control over window properties, multi-monitor support
- **Event System**: Input handling and game logic communication with filtering
- **Configuration**: Game settings and user preferences with hot reloading

### **Application Development**
- **Configuration**: Application settings and user preferences with validation
- **Logging**: Comprehensive application logging with rotation and filtering
- **Window Management**: Professional window behavior and user experience

## 🚀 **Performance**

### **Optimizations**
- **Memory Management**: RAII patterns and smart pointers with automatic cleanup
- **Event Filtering**: Efficient event processing with priority handling
- **Window Management**: Optimized window operations with hardware acceleration

### **Benchmarks**
- **Configuration Access**: < 1μs per access with caching
- **Event Dispatch**: < 10μs per event with filtering
- **Window Operations**: Hardware-accelerated window management

## 🤝 **Contributing**

1. **Fork** the repository
2. **Create** a feature branch
3. **Make** your changes with comprehensive tests
4. **Ensure** all tests pass and code is formatted
5. **Submit** a pull request

### **Development Guidelines**
- Follow the existing code style and patterns
- Add comprehensive tests for new features
- Update documentation for API changes
- Use the advanced systems appropriately
- Follow performance best practices
- Ensure cross-platform compatibility

## 📄 **License**

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 **Acknowledgments**

- **SDL3** for cross-platform windowing, input, and multimedia support
- **spdlog** for high-performance logging with advanced features
- **doctest** for modern, header-only unit testing
- **nlohmann/json** for JSON processing and serialization
- **Premake5** for build system generation with cross-platform support

## 📞 **Support**

- **Issues**: Report bugs and request features on GitHub
- **Discussions**: Ask questions and share ideas
- **Documentation**: Comprehensive guides and examples
- **Examples**: Sandbox application demonstrating all features

---

**LimitlessRemaster** - A modern, enterprise-grade C++ engine with comprehensive advanced systems and extended Window API for the future of game and application development.
