#pragma once

#include "Graphics/GraphicsContext.h"
#include "Platform/Platform.h"
#include <vector>
#include <optional>
#include <string>
#include <mutex>

namespace Limitless {

// Graphics API version information
struct GraphicsAPIVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string versionString;
    std::string vendor;
    std::string renderer;
    
    bool IsValid() const { return major > 0; }
    bool operator<(const GraphicsAPIVersion& other) const;
    bool operator==(const GraphicsAPIVersion& other) const;
    std::string ToString() const;
};

// Graphics API capability information
struct GraphicsAPICapabilities {
    GraphicsAPI api;
    GraphicsAPIVersion version;
    bool isSupported = false;
    bool isAvailable = false;
    std::string errorMessage;
    
    // API-specific capabilities
    struct OpenGLCaps {
        bool hasGLSL = false;
        bool hasGeometryShaders = false;
        bool hasTessellationShaders = false;
        bool hasComputeShaders = false;
        bool hasMultiDrawIndirect = false;
        bool hasInstancedRendering = false;
        int maxTextureSize = 0;
        int maxTextureUnits = 0;
        int maxVertexAttribs = 0;
        int maxUniformBlockSize = 0;
        int maxUniformBufferBindings = 0;
    };
    
    struct VulkanCaps {
        bool hasValidationLayers = false;
        bool hasRayTracing = false;
        bool hasMeshShaders = false;
        int maxPushConstantsSize = 0;
        int maxDescriptorSetCount = 0;
        int maxUniformBufferRange = 0;
    };
    
    struct DirectXCaps {
        bool hasRayTracing = false;
        bool hasMeshShaders = false;
        bool hasVariableRateShading = false;
        int maxTextureDimension = 0;
        int maxVertexShaderSamplers = 0;
        int maxPixelShaderSamplers = 0;
    };
    
    struct MetalCaps {
        bool hasRayTracing = false;
        bool hasMeshShaders = false;
        bool hasIndirectCommandBuffers = false;
        int maxTextureDimension = 0;
        int maxVertexBufferSize = 0;
        int maxFragmentBufferSize = 0;
    };
    
    std::optional<OpenGLCaps> openGLCaps;
    std::optional<VulkanCaps> vulkanCaps;
    std::optional<DirectXCaps> directXCaps;
    std::optional<MetalCaps> metalCaps;
};

// Graphics API detection and selection system
class GraphicsAPIDetector {
public:
    // Initialize the detector (call once at startup) - thread-safe and idempotent
    static void Initialize();
    
    // Set preferred graphics API (overrides automatic selection)
    static void SetPreferredAPI(GraphicsAPI api);
    
    // Get the preferred graphics API (if set)
    static std::optional<GraphicsAPI> GetPreferredAPI();
    
    // Clear preferred API setting (revert to automatic selection)
    static void ClearPreferredAPI();
    
    // Detect all available graphics APIs on the current platform
    static std::vector<GraphicsAPICapabilities> DetectAvailableAPIs();
    
    // Get the best available graphics API for the current platform
    static std::optional<GraphicsAPI> GetBestAPI();
    
    // Get a specific graphics API if available
    static std::optional<GraphicsAPICapabilities> GetAPI(GraphicsAPI api);
    
    // Check if a specific graphics API is supported
    static bool IsAPISupported(GraphicsAPI api);
    
    // Get the recommended graphics API for the current platform
    static GraphicsAPI GetRecommendedAPI();
    
    // Get the fallback graphics API if the preferred one fails
    static GraphicsAPI GetFallbackAPI();
    
    // Get API priority list for the current platform
    static std::vector<GraphicsAPI> GetAPIPriorityList();
    
    // Validate graphics API selection
    static bool ValidateAPISelection(GraphicsAPI api);
    
    // Get detailed information about a graphics API
    static std::string GetAPIInfo(GraphicsAPI api);
    
    // Get system requirements for a graphics API
    static std::string GetSystemRequirements(GraphicsAPI api);
    
    // Check if the current system meets the requirements for a graphics API
    static bool MeetsRequirements(GraphicsAPI api);
    
    // Get error message if API is not supported
    static std::string GetUnsupportedReason(GraphicsAPI api);
    
    // Refresh detection (useful for hot reloading)
    static void Refresh();
    
    // Update OpenGL information with actual context data
    static void UpdateOpenGLInfo(const std::string& version, const std::string& vendor, const std::string& renderer);
    
    // Get detection status
    static bool IsInitialized() { return s_Initialized; }
    
    // Get detection results
    static const std::vector<GraphicsAPICapabilities>& GetDetectionResults() { return s_DetectionResults; }
    
    // Get comprehensive detection information for debugging
    static std::string GetDetectionReport();
    
    // Get best supported OpenGL version for context creation
    static std::pair<int, int> GetBestSupportedOpenGLVersion();
    
    // Version comparison helpers (public for context creation)
    static bool IsVersionSupported(int major, int minor, int requiredMajor, int requiredMinor);
    static GraphicsAPIVersion ParseVersionString(const std::string& versionString);

private:
    static bool s_Initialized;
    static std::vector<GraphicsAPICapabilities> s_DetectionResults;
    static std::mutex s_InitMutex;
    static std::optional<GraphicsAPI> s_PreferredAPI;
    
    // Platform-specific detection methods
    static void DetectOpenGL();
    static void DetectVulkan();
    static void DetectDirectX();
    static void DetectMetal();
    
    // Capability detection methods
    static void DetectOpenGLCapabilities(GraphicsAPICapabilities& caps);
    static void DetectVulkanCapabilities(GraphicsAPICapabilities& caps);
    static void DetectDirectXCapabilities(GraphicsAPICapabilities& caps);
    static void DetectMetalCapabilities(GraphicsAPICapabilities& caps);
    
    // Platform-specific API priority lists
    static std::vector<GraphicsAPI> GetWindowsAPIPriority();
    static std::vector<GraphicsAPI> GetMacOSAPIPriority();
    static std::vector<GraphicsAPI> GetLinuxAPIPriority();
    static std::vector<GraphicsAPI> GetAndroidAPIPriority();
    static std::vector<GraphicsAPI> GetIOSAPIPriority();
};

// Graphics API selection helper
class GraphicsAPISelector {
public:
    // Selection criteria
    enum class SelectionCriteria {
        Performance,     // Choose the fastest API
        Compatibility,   // Choose the most compatible API
        Features,        // Choose the API with most features
        Stability,       // Choose the most stable API
        PowerEfficiency  // Choose the most power-efficient API
    };
    
    // Select the best graphics API based on criteria
    static std::optional<GraphicsAPI> SelectAPI(SelectionCriteria criteria = SelectionCriteria::Performance);
    
    // Select API with custom priority list
    static std::optional<GraphicsAPI> SelectAPI(const std::vector<GraphicsAPI>& priorityList);
    
    // Get selection recommendation with reasoning
    struct SelectionRecommendation {
        GraphicsAPI recommendedAPI;
        std::string reasoning;
        std::vector<GraphicsAPI> alternatives;
        std::string warnings;
    };
    
    static SelectionRecommendation GetRecommendation(SelectionCriteria criteria = SelectionCriteria::Performance);
    
    // Validate selection against system requirements
    static bool ValidateSelection(GraphicsAPI api, std::string& errorMessage);
    
    // Get performance comparison between available APIs
    static std::string GetPerformanceComparison();
    
    // Get feature comparison between available APIs
    static std::string GetFeatureComparison();
};

} // namespace Limitless 