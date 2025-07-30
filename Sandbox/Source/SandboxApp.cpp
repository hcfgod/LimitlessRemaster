#include "SandboxApp.h"
#include <iostream>

namespace Limitless
{
	SandboxApp::SandboxApp()
	{
		LT_INFO("SandboxApp Constructor");
	}

	SandboxApp::~SandboxApp()
	{
		LT_INFO("SandboxApp Destructor");
	}

	bool SandboxApp::Initialize()
	{
		LT_INFO("SandboxApp Initialize");
		
		// Demonstrate platform detection
		DemonstratePlatformDetection();
		
		// Demonstrate error handling
		DemonstrateErrorHandling();
		
		return true;
	}

	void SandboxApp::Shutdown()
	{
		LT_INFO("SandboxApp Shutdown");
	}

	void SandboxApp::DemonstratePlatformDetection()
	{
		LT_INFO("=== Platform Detection Demo ===");
		
		const auto& platformInfo = PlatformDetection::GetPlatformInfo();
		
		LT_INFO("Platform: {} ({})", platformInfo.platformName, PlatformDetection::GetPlatformString());
		LT_INFO("Architecture: {} ({})", platformInfo.architectureName, PlatformDetection::GetArchitectureString());
		LT_INFO("Compiler: {} {} ({})", platformInfo.compilerName, platformInfo.compilerVersion, PlatformDetection::GetCompilerString());
		LT_INFO("OS: {} {} ({})", platformInfo.osName, platformInfo.osVersion, PlatformDetection::GetOSString());
		
		LT_INFO("System Capabilities:");
		LT_INFO("  CPU Cores: {}", platformInfo.capabilities.cpuCount);
		LT_INFO("  Total Memory: {} MB", platformInfo.capabilities.totalMemory / (1024 * 1024));
		LT_INFO("  Available Memory: {} MB", platformInfo.capabilities.availableMemory / (1024 * 1024));
		
		LT_INFO("  CPU Features:");
		LT_INFO("    SSE2: {}", platformInfo.capabilities.hasSSE2 ? "Yes" : "No");
		LT_INFO("    SSE3: {}", platformInfo.capabilities.hasSSE3 ? "Yes" : "No");
		LT_INFO("    SSE4.1: {}", platformInfo.capabilities.hasSSE4_1 ? "Yes" : "No");
		LT_INFO("    SSE4.2: {}", platformInfo.capabilities.hasSSE4_2 ? "Yes" : "No");
		LT_INFO("    AVX: {}", platformInfo.capabilities.hasAVX ? "Yes" : "No");
		LT_INFO("    AVX2: {}", platformInfo.capabilities.hasAVX2 ? "Yes" : "No");
		LT_INFO("    AVX512: {}", platformInfo.capabilities.hasAVX512 ? "Yes" : "No");
		LT_INFO("    NEON: {}", platformInfo.capabilities.hasNEON ? "Yes" : "No");
		LT_INFO("    AltiVec: {}", platformInfo.capabilities.hasAltiVec ? "Yes" : "No");
		
		LT_INFO("  Graphics APIs:");
		LT_INFO("    OpenGL: {}", platformInfo.capabilities.hasOpenGL ? "Yes" : "No");
		LT_INFO("    Vulkan: {}", platformInfo.capabilities.hasVulkan ? "Yes" : "No");
		LT_INFO("    Metal: {}", platformInfo.capabilities.hasMetal ? "Yes" : "No");
		LT_INFO("    DirectX: {}", platformInfo.capabilities.hasDirectX ? "Yes" : "No");
		
		LT_INFO("Paths:");
		LT_INFO("  Executable: {}", platformInfo.executablePath);
		LT_INFO("  Working Directory: {}", platformInfo.workingDirectory);
		LT_INFO("  User Data: {}", platformInfo.userDataPath);
		LT_INFO("  Temp: {}", platformInfo.tempPath);
		LT_INFO("  System: {}", platformInfo.systemPath);
		
		// Demonstrate platform-specific checks
		LT_INFO("Platform Checks:");
		LT_INFO("  Is Windows: {}", PlatformDetection::IsWindows() ? "Yes" : "No");
		LT_INFO("  Is macOS: {}", PlatformDetection::IsMacOS() ? "Yes" : "No");
		LT_INFO("  Is Linux: {}", PlatformDetection::IsLinux() ? "Yes" : "No");
		LT_INFO("  Is x64: {}", PlatformDetection::IsX64() ? "Yes" : "No");
		LT_INFO("  Is ARM64: {}", PlatformDetection::IsARM64() ? "Yes" : "No");
		LT_INFO("  Is MSVC: {}", PlatformDetection::IsMSVC() ? "Yes" : "No");
		LT_INFO("  Is GCC: {}", PlatformDetection::IsGCC() ? "Yes" : "No");
		LT_INFO("  Is Clang: {}", PlatformDetection::IsClang() ? "Yes" : "No");
		
		// Demonstrate platform utilities
		LT_INFO("Platform Utilities:");
		LT_INFO("  Path Separator: '{}'", PlatformUtils::GetPathSeparator());
		LT_INFO("  Process ID: {}", PlatformUtils::GetCurrentProcessId());
		LT_INFO("  Thread ID: {}", PlatformUtils::GetCurrentThreadId());
		LT_INFO("  High Resolution Time: {} μs", PlatformUtils::GetHighResolutionTime());
		LT_INFO("  System Time: {} ms", PlatformUtils::GetSystemTime());
		LT_INFO("  Console Available: {}", PlatformUtils::IsConsoleAvailable() ? "Yes" : "No");
		
		// Test path utilities
		std::string testPath = PlatformUtils::JoinPath("C:\\test", "file.txt");
		LT_INFO("  Join Path: {}", testPath);
		LT_INFO("  Directory: {}", PlatformUtils::GetDirectoryName(testPath));
		LT_INFO("  Filename: {}", PlatformUtils::GetFileName(testPath));
		LT_INFO("  Extension: {}", PlatformUtils::GetFileExtension(testPath));
		
		LT_INFO("=== End Platform Detection Demo ===");
	}

	void SandboxApp::DemonstrateErrorHandling()
	{
		LT_INFO("=== Error Handling Demo ===");
		
		// Test different error severities
		LT_INFO("Testing error severities...");
		
		try
		{
			// Info level error
			Error infoError(ErrorCode::Cancelled, "Operation was cancelled", std::source_location::current(), ErrorSeverity::Info);
			infoError.SetFunctionName("DemonstrateErrorHandling");
			infoError.SetClassName("SandboxApp");
			infoError.AddContext("TestContext", "InfoLevelTest");
			Error::LogError(infoError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		try
		{
			// Warning level error
			Error warningError(ErrorCode::FileExists, "File already exists", std::source_location::current(), ErrorSeverity::Warning);
			warningError.SetFunctionName("DemonstrateErrorHandling");
			warningError.SetClassName("SandboxApp");
			warningError.AddContext("TestContext", "WarningLevelTest");
			Error::LogError(warningError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		try
		{
			// Error level error
			Error errorError(ErrorCode::FileNotFound, "Configuration file not found", std::source_location::current(), ErrorSeverity::Error);
			errorError.SetFunctionName("DemonstrateErrorHandling");
			errorError.SetClassName("SandboxApp");
			errorError.AddContext("TestContext", "ErrorLevelTest");
			errorError.AddContext("FileName", "config.json");
			Error::LogError(errorError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		// Test specific error types
		LT_INFO("Testing specific error types...");
		
		try
		{
			        SystemError systemError("Failed to open system file", std::source_location::current());
			systemError.SetSystemErrorCode(ErrorHandling::GetLastSystemError());
			Error::LogError(systemError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		try
		{
			        PlatformError platformError("Platform-specific operation failed", std::source_location::current());
			platformError.SetFunctionName("DemonstrateErrorHandling");
			Error::LogError(platformError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		try
		{
			        GraphicsError graphicsError("Failed to create graphics context", std::source_location::current());
			graphicsError.SetFunctionName("DemonstrateErrorHandling");
			Error::LogError(graphicsError);
		}
		catch (const std::exception& e)
		{
			LT_ERROR("Unexpected exception: {}", e.what());
		}
		
		// Test Result class
		LT_INFO("Testing Result class...");
		
		// Success result
		Result<int> successResult(42);
		LT_INFO("Success result: {}", successResult.GetValue());
		LT_INFO("Is success: {}", successResult.IsSuccess());
		
		// Error result
		Result<int> errorResult(ErrorCode::FileNotFound, "File not found");
		LT_INFO("Is failure: {}", errorResult.IsFailure());
		LT_INFO("Error message: {}", errorResult.GetError().GetErrorMessage());
		
		// Safe value access
		if (int* value = successResult.GetValuePtr())
		{
			LT_INFO("Safe value access: {}", *value);
		}
		
		if (int* value = errorResult.GetValuePtr())
		{
			LT_INFO("This should not print");
		}
		else
		{
			LT_INFO("Value pointer is null (expected for error result)");
		}
		
		// Value or default
		LT_INFO("Value or default (success): {}", successResult.GetValueOr(0));
		LT_INFO("Value or default (error): {}", errorResult.GetValueOr(0));
		
		// Test Try wrapper
		LT_INFO("Testing Try wrapper...");
		
		auto tryResult = ErrorHandling::Try([]() -> int {
			return 123;
		});
		LT_INFO("Try success result: {}", tryResult.GetValue());
		
		auto tryErrorResult = ErrorHandling::Try([]() -> int {
			        throw Error(ErrorCode::InvalidArgument, "Test error from Try wrapper", std::source_location::current());
		});
		LT_INFO("Try error result: {}", tryErrorResult.GetError().GetErrorMessage());
		
		// Test assertions and verifications
		LT_INFO("Testing assertions and verifications...");
		
		// This should not throw
		LT_VERIFY(true, "This verification should pass");
		LT_INFO("Verification passed");
		
		// This should throw
		try
		{
			LT_VERIFY(false, "This verification should fail");
		}
		catch (const Error& error)
		{
			LT_INFO("Verification failed as expected: {}", error.GetErrorMessage());
		}
		
		// Test error code utilities
		LT_INFO("Testing error code utilities...");
		LT_INFO("Error code string: {}", ErrorHandling::GetErrorCodeString(ErrorCode::FileNotFound));
		LT_INFO("Error code description: {}", ErrorHandling::GetErrorCodeDescription(ErrorCode::FileNotFound));
		LT_INFO("Error code severity: {}", static_cast<int>(ErrorHandling::GetErrorCodeSeverity(ErrorCode::FileNotFound)));
		
		// Test system error utilities
		LT_INFO("Testing system error utilities...");
		int lastError = ErrorHandling::GetLastSystemError();
		LT_INFO("Last system error: {} ({})", lastError, ErrorHandling::GetSystemErrorString(lastError));
		
		LT_INFO("=== End Error Handling Demo ===");
	}
}

// Define the CreateApplication function that the entry point expects
Limitless::Application* CreateApplication()
{
	return new Limitless::SandboxApp();
}