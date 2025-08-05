#include "Graphics/GraphicsAPIDetector.h"
#include "Platform/Platform.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <mutex> // Added for thread-safe initialization

namespace Limitless {

// Static member initialization
bool GraphicsAPIDetector::s_Initialized = false;
std::vector<GraphicsAPICapabilities> GraphicsAPIDetector::s_DetectionResults;
std::mutex GraphicsAPIDetector::s_InitMutex; // Added for thread-safe initialization
std::optional<GraphicsAPI> GraphicsAPIDetector::s_PreferredAPI;

// GraphicsAPIVersion implementation
bool GraphicsAPIVersion::operator<(const GraphicsAPIVersion& other) const {
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    return patch < other.patch;
}

bool GraphicsAPIVersion::operator==(const GraphicsAPIVersion& other) const {
    return major == other.major && minor == other.minor && patch == other.patch;
}

std::string GraphicsAPIVersion::ToString() const {
    if (patch > 0) {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
    return std::to_string(major) + "." + std::to_string(minor);
}

// GraphicsAPIDetector implementation
void GraphicsAPIDetector::Initialize() {
    std::lock_guard<std::mutex> lock(s_InitMutex);
    
    if (s_Initialized) {
        std::cout << "Graphics API Detection already initialized." << std::endl;
        return;
    }
    
    // Detect all available APIs (lightweight detection without SDL video)
    s_DetectionResults = DetectAvailableAPIs();
    s_Initialized = true;
    
    std::cout << "Graphics API Detection initialized. Found " << s_DetectionResults.size() << " available APIs." << std::endl;
}

std::vector<GraphicsAPICapabilities> GraphicsAPIDetector::DetectAvailableAPIs() {
    // Clear previous results
    s_DetectionResults.clear();
    
    // Detect OpenGL
    DetectOpenGL();
    
    // Detect Vulkan
    DetectVulkan();
    
    // Detect DirectX (Windows only)
    if (PlatformDetection::IsWindows()) {
        DetectDirectX();
    }
    
    // Detect Metal (macOS only)
    if (PlatformDetection::IsMacOS()) {
        DetectMetal();
    }
    
    return s_DetectionResults;
}

void GraphicsAPIDetector::DetectOpenGL() {
    GraphicsAPICapabilities caps;
    caps.api = GraphicsAPI::OpenGL;
    
    // We'll assume OpenGL is available on most modern systems
    // The actual version and capabilities will be determined when the context is created
    
    bool openGLSupported = true; // Assume available, let context creation handle actual detection
    
    // Set reasonable defaults for version information
    // These will be updated when the actual context is created
    caps.version.major = 4;
    caps.version.minor = 5;
    caps.version.patch = 0;
    caps.version.vendor = "Unknown"; // Will be set when context is actually created
    caps.version.renderer = "Unknown"; // Will be set when context is actually created
    caps.version.versionString = "4.5.0";
    
    caps.isSupported = openGLSupported;
    caps.isAvailable = openGLSupported;
    
    // Set basic capabilities based on assumed version
    DetectOpenGLCapabilities(caps);
    
    s_DetectionResults.push_back(caps);
}

void GraphicsAPIDetector::DetectOpenGLCapabilities(GraphicsAPICapabilities& caps) {
    caps.openGLCaps = GraphicsAPICapabilities::OpenGLCaps();
    auto& glCaps = caps.openGLCaps.value();
    
    // Set capabilities based on OpenGL version (without creating context)
    // These will be refined when the actual context is created
    glCaps.hasGLSL = IsVersionSupported(caps.version.major, caps.version.minor, 2, 0);
    glCaps.hasGeometryShaders = IsVersionSupported(caps.version.major, caps.version.minor, 3, 2);
    glCaps.hasTessellationShaders = IsVersionSupported(caps.version.major, caps.version.minor, 4, 0);
    glCaps.hasComputeShaders = IsVersionSupported(caps.version.major, caps.version.minor, 4, 3);
    glCaps.hasMultiDrawIndirect = IsVersionSupported(caps.version.major, caps.version.minor, 4, 3);
    glCaps.hasInstancedRendering = IsVersionSupported(caps.version.major, caps.version.minor, 3, 3);
    
    // Set reasonable defaults for hardware limits
    // These will be updated when the actual context is created
    glCaps.maxTextureSize = 16384; // Common modern limit
    glCaps.maxTextureUnits = 16;   // Common modern limit
    glCaps.maxVertexAttribs = 16;  // Common modern limit
    glCaps.maxUniformBlockSize = 16384; // Common modern limit
    glCaps.maxUniformBufferBindings = 84; // Common modern limit
}

void GraphicsAPIDetector::DetectVulkan() {
    GraphicsAPICapabilities caps;
    caps.api = GraphicsAPI::Vulkan;
    
    // TODO: Implement Vulkan detection
    // This would involve:
    // 1. Loading Vulkan loader library
    // 2. Enumerating available physical devices
    // 3. Checking Vulkan version support
    // 4. Detecting available extensions and features
    
    caps.isSupported = false;
    caps.errorMessage = "Vulkan detection not yet implemented";
    s_DetectionResults.push_back(caps);
}

void GraphicsAPIDetector::DetectDirectX() {
    GraphicsAPICapabilities caps;
    caps.api = GraphicsAPI::DirectX;
    
#ifdef LT_PLATFORM_WINDOWS
    // TODO: Implement DirectX detection
    // This would involve:
    // 1. Creating DXGI factory
    // 2. Enumerating adapters
    // 3. Checking DirectX version support
    // 4. Detecting available features
#endif
    
    caps.isSupported = false;
    caps.errorMessage = "DirectX detection not yet implemented";
    s_DetectionResults.push_back(caps);
}

void GraphicsAPIDetector::DetectMetal() {
    GraphicsAPICapabilities caps;
    caps.api = GraphicsAPI::Metal;
    
#ifdef LT_PLATFORM_MACOS
    // TODO: Implement Metal detection
    // This would involve:
    // 1. Creating MTLDevice
    // 2. Checking Metal version support
    // 3. Detecting available features
#endif
    
    caps.isSupported = false;
    caps.errorMessage = "Metal detection not yet implemented";
    s_DetectionResults.push_back(caps);
}

std::optional<GraphicsAPI> GraphicsAPIDetector::GetBestAPI() {
    if (!s_Initialized) Initialize();
    
    // Check if a preferred API is set
    if (s_PreferredAPI.has_value()) {
        GraphicsAPI preferred = s_PreferredAPI.value();
        if (IsAPISupported(preferred)) {
            std::cout << "Using preferred graphics API: " << GraphicsAPIToString(preferred) << std::endl;
            return preferred;
        } else {
            std::cout << "Preferred graphics API " << GraphicsAPIToString(preferred) 
                      << " is not supported, falling back to automatic selection" << std::endl;
            // Clear the preferred API since it's not supported
            s_PreferredAPI = std::nullopt;
        }
    }
    
    auto priorityList = GetAPIPriorityList();
    for (const auto& api : priorityList) {
        if (IsAPISupported(api)) {
            return api;
        }
    }
    
    return std::nullopt;
}

std::optional<GraphicsAPICapabilities> GraphicsAPIDetector::GetAPI(GraphicsAPI api) {
    if (!s_Initialized) Initialize();
    
    for (const auto& caps : s_DetectionResults) {
        if (caps.api == api) {
            return caps;
        }
    }
    
    return std::nullopt;
}

bool GraphicsAPIDetector::IsAPISupported(GraphicsAPI api) {
    auto caps = GetAPI(api);
    return caps.has_value() && caps->isSupported && caps->isAvailable;
}

GraphicsAPI GraphicsAPIDetector::GetRecommendedAPI() {
    if (PlatformDetection::IsWindows()) {
        // On Windows, prefer DirectX, then Vulkan, then OpenGL
        if (IsAPISupported(GraphicsAPI::DirectX)) {
            return GraphicsAPI::DirectX;
        }
        if (IsAPISupported(GraphicsAPI::Vulkan)) {
            return GraphicsAPI::Vulkan;
        }
        return GraphicsAPI::OpenGL;
    } else if (PlatformDetection::IsMacOS()) {
        // On macOS, prefer Metal, then OpenGL
        if (IsAPISupported(GraphicsAPI::Metal)) {
            return GraphicsAPI::Metal;
        }
        return GraphicsAPI::OpenGL;
    } else if (PlatformDetection::IsLinux()) {
        // On Linux, prefer Vulkan, then OpenGL
        if (IsAPISupported(GraphicsAPI::Vulkan)) {
            return GraphicsAPI::Vulkan;
        }
        return GraphicsAPI::OpenGL;
    }
    
    return GraphicsAPI::OpenGL; // Default fallback
}

GraphicsAPI GraphicsAPIDetector::GetFallbackAPI() {
    // OpenGL is typically the most widely supported fallback
    return GraphicsAPI::OpenGL;
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetAPIPriorityList() {
    if (PlatformDetection::IsWindows()) {
        return GetWindowsAPIPriority();
    } else if (PlatformDetection::IsMacOS()) {
        return GetMacOSAPIPriority();
    } else if (PlatformDetection::IsLinux()) {
        return GetLinuxAPIPriority();
    } else if (PlatformDetection::IsAndroid()) {
        return GetAndroidAPIPriority();
    } else if (PlatformDetection::IsIOS()) {
        return GetIOSAPIPriority();
    }
    
    return { GraphicsAPI::OpenGL };
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetWindowsAPIPriority() {
    return {
        GraphicsAPI::DirectX,
        GraphicsAPI::Vulkan,
        GraphicsAPI::OpenGL
    };
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetMacOSAPIPriority() {
    return {
        GraphicsAPI::Metal,
        GraphicsAPI::OpenGL
    };
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetLinuxAPIPriority() {
    return {
        GraphicsAPI::Vulkan,
        GraphicsAPI::OpenGL
    };
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetAndroidAPIPriority() {
    return {
        GraphicsAPI::Vulkan,
        GraphicsAPI::OpenGL
    };
}

std::vector<GraphicsAPI> GraphicsAPIDetector::GetIOSAPIPriority() {
    return {
        GraphicsAPI::Metal,
        GraphicsAPI::OpenGL
    };
}

bool GraphicsAPIDetector::ValidateAPISelection(GraphicsAPI api) {
    return IsAPISupported(api) && MeetsRequirements(api);
}

std::string GraphicsAPIDetector::GetAPIInfo(GraphicsAPI api) {
    auto caps = GetAPI(api);
    if (!caps) {
        return "API not detected";
    }
    
    std::stringstream ss;
    ss << "API: " << GraphicsAPIToString(api) << "\n";
    ss << "Version: " << caps->version.ToString() << "\n";
    ss << "Vendor: " << caps->version.vendor << "\n";
    ss << "Renderer: " << caps->version.renderer << "\n";
    ss << "Supported: " << (caps->isSupported ? "Yes" : "No") << "\n";
    ss << "Available: " << (caps->isAvailable ? "Yes" : "No") << "\n";
    
    if (!caps->errorMessage.empty()) {
        ss << "Error: " << caps->errorMessage << "\n";
    }
    
    return ss.str();
}

std::string GraphicsAPIDetector::GetSystemRequirements(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::OpenGL:
            return "OpenGL 3.3+ (Core Profile) or OpenGL 4.0+ recommended";
        case GraphicsAPI::Vulkan:
            return "Vulkan 1.0+ with appropriate drivers";
        case GraphicsAPI::DirectX:
            return "DirectX 12+ with Windows 10+";
        case GraphicsAPI::Metal:
            return "Metal 2.0+ with macOS 10.14+";
        default:
            return "Unknown API requirements";
    }
}

bool GraphicsAPIDetector::MeetsRequirements(GraphicsAPI api) {
    auto caps = GetAPI(api);
    if (!caps || !caps->isSupported) {
        return false;
    }
    
    switch (api) {
        case GraphicsAPI::OpenGL:
            // Require OpenGL 3.3+ for core profile features
            return IsVersionSupported(caps->version.major, caps->version.minor, 3, 3);
        case GraphicsAPI::Vulkan:
            // Require Vulkan 1.0+
            return IsVersionSupported(caps->version.major, caps->version.minor, 1, 0);
        case GraphicsAPI::DirectX:
            // Require DirectX 12+
            return IsVersionSupported(caps->version.major, caps->version.minor, 12, 0);
        case GraphicsAPI::Metal:
            // Require Metal 2.0+
            return IsVersionSupported(caps->version.major, caps->version.minor, 2, 0);
        default:
            return false;
    }
}

std::string GraphicsAPIDetector::GetUnsupportedReason(GraphicsAPI api) {
    auto caps = GetAPI(api);
    if (!caps) {
        return "API not detected on this system";
    }
    
    if (!caps->isSupported) {
        return caps->errorMessage;
    }
    
    if (!MeetsRequirements(api)) {
        return "API version does not meet minimum requirements";
    }
    
    return "API is supported and meets requirements";
}

void GraphicsAPIDetector::Refresh() {
    s_DetectionResults.clear();
    s_DetectionResults = DetectAvailableAPIs();
}

void GraphicsAPIDetector::UpdateOpenGLInfo(const std::string& version, const std::string& vendor, const std::string& renderer) {
    // Find and update the OpenGL capabilities with actual context information
    for (auto& caps : s_DetectionResults) {
        if (caps.api == GraphicsAPI::OpenGL) {
            caps.version = ParseVersionString(version);
            caps.version.vendor = vendor;
            caps.version.renderer = renderer;
            
            // Update capabilities based on actual version
            DetectOpenGLCapabilities(caps);
            break;
        }
    }
}

std::pair<int, int> GraphicsAPIDetector::GetBestSupportedOpenGLVersion() {
    if (!s_Initialized) Initialize();
    
    auto openGLCaps = GetAPI(GraphicsAPI::OpenGL);
    if (!openGLCaps || !openGLCaps->isSupported) {
        return {3, 3}; // Fallback to minimum supported version
    }
    
    // Try to detect the best supported version by attempting to create contexts
    // Start with a conservative version and work up
    std::pair<int, int> versions[] = {
        {3, 3}, {3, 4}, {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}
    };
    
    // For now, return a conservative version that should work on most systems
    // The actual version will be determined when the context is created
    return {3, 3}; // Start with OpenGL 3.3 which is widely supported
}

bool GraphicsAPIDetector::IsVersionSupported(int major, int minor, int requiredMajor, int requiredMinor) {
    if (major > requiredMajor) return true;
    if (major < requiredMajor) return false;
    return minor >= requiredMinor;
}

GraphicsAPIVersion GraphicsAPIDetector::ParseVersionString(const std::string& versionString) {
    GraphicsAPIVersion version;
    version.versionString = versionString;
    
    std::istringstream iss(versionString);
    char dot1, dot2;
    
    if (iss >> version.major >> dot1 >> version.minor) {
        if (dot1 == '.' && iss >> dot2 >> version.patch) {
            // Successfully parsed major.minor.patch
        } else {
            // Successfully parsed major.minor
            version.patch = 0;
        }
    }
    
    return version;
}

void GraphicsAPIDetector::SetPreferredAPI(GraphicsAPI api) {
    std::lock_guard<std::mutex> lock(s_InitMutex);
    s_PreferredAPI = api;
    std::cout << "Set preferred graphics API to: " << GraphicsAPIToString(api) << std::endl;
}

std::optional<GraphicsAPI> GraphicsAPIDetector::GetPreferredAPI() {
    std::lock_guard<std::mutex> lock(s_InitMutex);
    return s_PreferredAPI;
}

void GraphicsAPIDetector::ClearPreferredAPI() {
    std::lock_guard<std::mutex> lock(s_InitMutex);
    s_PreferredAPI = std::nullopt;
    std::cout << "Cleared preferred graphics API setting" << std::endl;
}

std::string GraphicsAPIDetector::GetDetectionReport() {
    if (!s_Initialized) {
        return "Graphics API Detection not initialized";
    }
    
    std::stringstream report;
    report << "=== Graphics API Detection Report ===\n";
    report << "Initialized: " << (s_Initialized ? "Yes" : "No") << "\n";
    
    if (s_PreferredAPI.has_value()) {
        report << "Preferred API: " << GraphicsAPIToString(s_PreferredAPI.value()) << "\n";
    } else {
        report << "Preferred API: None (automatic selection)\n";
    }
    
    report << "\nDetected APIs:\n";
    for (const auto& caps : s_DetectionResults) {
        report << "  " << GraphicsAPIToString(caps.api) << ":\n";
        report << "    Version: " << caps.version.ToString() << "\n";
        report << "    Vendor: " << caps.version.vendor << "\n";
        report << "    Renderer: " << caps.version.renderer << "\n";
        report << "    Supported: " << (caps.isSupported ? "Yes" : "No") << "\n";
        report << "    Available: " << (caps.isAvailable ? "Yes" : "No") << "\n";
        if (!caps.errorMessage.empty()) {
            report << "    Error: " << caps.errorMessage << "\n";
        }
    }
    
    auto bestAPI = GetBestAPI();
    if (bestAPI) {
        report << "\nBest API: " << GraphicsAPIToString(bestAPI.value()) << "\n";
    } else {
        report << "\nBest API: None found\n";
    }
    
    report << "\nPlatform Priority List:\n";
    auto priorityList = GetAPIPriorityList();
    for (size_t i = 0; i < priorityList.size(); ++i) {
        report << "  " << (i + 1) << ". " << GraphicsAPIToString(priorityList[i]) << "\n";
    }
    
    return report.str();
}

// GraphicsAPISelector implementation
std::optional<GraphicsAPI> GraphicsAPISelector::SelectAPI(SelectionCriteria criteria) {
    auto availableAPIs = GraphicsAPIDetector::GetDetectionResults();
    std::vector<GraphicsAPI> supportedAPIs;
    
    for (const auto& caps : availableAPIs) {
        if (caps.isSupported && caps.isAvailable) {
            supportedAPIs.push_back(caps.api);
        }
    }
    
    if (supportedAPIs.empty()) {
        return std::nullopt;
    }
    
    // For now, return the first supported API
    // TODO: Implement proper selection logic based on criteria
    return supportedAPIs[0];
}

std::optional<GraphicsAPI> GraphicsAPISelector::SelectAPI(const std::vector<GraphicsAPI>& priorityList) {
    for (const auto& api : priorityList) {
        if (GraphicsAPIDetector::IsAPISupported(api)) {
            return api;
        }
    }
    
    return std::nullopt;
}

GraphicsAPISelector::SelectionRecommendation GraphicsAPISelector::GetRecommendation(SelectionCriteria criteria) {
    SelectionRecommendation recommendation;
    
    auto selectedAPI = SelectAPI(criteria);
    if (selectedAPI) {
        recommendation.recommendedAPI = selectedAPI.value();
        recommendation.reasoning = "Selected based on " + std::to_string(static_cast<int>(criteria)) + " criteria";
    } else {
        recommendation.recommendedAPI = GraphicsAPI::OpenGL; // Default fallback
        recommendation.reasoning = "No suitable API found, using fallback";
        recommendation.warnings = "No supported graphics APIs detected on this system";
    }
    
    return recommendation;
}

bool GraphicsAPISelector::ValidateSelection(GraphicsAPI api, std::string& errorMessage) {
    if (!GraphicsAPIDetector::IsAPISupported(api)) {
        errorMessage = GraphicsAPIDetector::GetUnsupportedReason(api);
        return false;
    }
    
    if (!GraphicsAPIDetector::MeetsRequirements(api)) {
        errorMessage = "API does not meet minimum system requirements";
        return false;
    }
    
    return true;
}

std::string GraphicsAPISelector::GetPerformanceComparison() {
    // TODO: Implement performance comparison
    return "Performance comparison not yet implemented";
}

std::string GraphicsAPISelector::GetFeatureComparison() {
    // TODO: Implement feature comparison
    return "Feature comparison not yet implemented";
}

} // namespace Limitless 