#include "SandboxApp.h"
#include <iostream>
#include <random>

namespace Limitless
{
	SandboxApp::SandboxApp()
	{
		//LT_INFO("SandboxApp Constructor");
	}

	SandboxApp::~SandboxApp()
	{
		//LT_INFO("SandboxApp Destructor");
	}

	bool SandboxApp::Initialize()
	{
		//LT_INFO("SandboxApp Initialize");
		
		// Demonstrate platform detection
		//DemonstratePlatformDetection();
		
		// Demonstrate error handling
		//DemonstrateErrorHandling();
		
		// Demonstrate AsyncIO functionality
		//DemonstrateAsyncIO();
		
		// Demonstrate Performance Monitoring
		//DemonstratePerformanceMonitoring();
		
		return true;
	}

	void SandboxApp::Shutdown()
	{
		//LT_INFO("SandboxApp Shutdown");
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
		
		// Test LT_ASSERT
		LT_INFO("Testing LT_ASSERT...");
		try
		{
			LT_ASSERT(true, "This assertion should pass");
			LT_INFO("Assertion passed");
		}
		catch (const Error& error)
		{
			LT_INFO("Unexpected assertion failure: {}", error.GetErrorMessage());
		}
		
		try
		{
			LT_ASSERT(false, "This assertion should fail");
		}
		catch (const Error& error)
		{
			LT_INFO("Assertion failed as expected: {}", error.GetErrorMessage());
		}
		
		// Test LT_THROW macros
		LT_INFO("Testing LT_THROW macros...");
		
		try
		{
			LT_THROW_SYSTEM_ERROR("Test system error");
		}
		catch (const SystemError& error)
		{
			LT_INFO("System error thrown as expected: {}", error.GetErrorMessage());
		}
		
		try
		{
			LT_THROW_PLATFORM_ERROR("Test platform error");
		}
		catch (const PlatformError& error)
		{
			LT_INFO("Platform error thrown as expected: {}", error.GetErrorMessage());
		}
		
		try
		{
			LT_THROW_GRAPHICS_ERROR("Test graphics error");
		}
		catch (const GraphicsError& error)
		{
			LT_INFO("Graphics error thrown as expected: {}", error.GetErrorMessage());
		}
		
		// Test LT_TRY macros
		LT_INFO("Testing LT_TRY macros...");
		
		auto tryMacroResult = LT_TRY(42 + 123);
		LT_INFO("LT_TRY success result: {}", tryMacroResult.GetValue());
		
		auto tryMacroErrorResult = LT_TRY([]() -> int {
			LT_THROW_ERROR(ErrorCode::Timeout, "Test timeout error");
		}());
		LT_INFO("LT_TRY error result: {}", tryMacroErrorResult.GetError().GetErrorMessage());
		
		// Test LT_RETURN_IF_ERROR
		LT_INFO("Testing LT_RETURN_IF_ERROR...");
		
		auto testFunction = [](bool shouldFail) -> Result<int> {
			if (shouldFail)
			{
				return Result<int>(ErrorCode::InvalidState, "Function failed");
			}
			return Result<int>(42);
		};
		
		auto result1 = testFunction(false);
		LT_INFO("Function success result: {}", result1.GetValue());
		
		auto result2 = testFunction(true);
		LT_INFO("Function error result: {}", result2.GetError().GetErrorMessage());
		
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

	void SandboxApp::DemonstrateAsyncIO()
	{
		LT_INFO("=== AsyncIO Demo ===");
		
		auto& asyncIO = Limitless::Async::GetAsyncIO();
		
		// Check if AsyncIO is initialized
		LT_INFO("AsyncIO initialized: {}", asyncIO.IsInitialized());
		LT_INFO("AsyncIO thread count: {}", asyncIO.GetThreadCount());
		
		// Test file operations
		const std::string testFile = "async_test.txt";
		const std::string testContent = "Hello from AsyncIO! This is a test file created asynchronously.\n";
		
		// Write file asynchronously
		LT_INFO("Writing file asynchronously: {}", testFile);
		auto writeTask = asyncIO.WriteFileAsync(testFile, testContent);
		writeTask.Wait(); // Wait for completion
		LT_INFO("File write completed");
		
		// Read file asynchronously
		LT_INFO("Reading file asynchronously: {}", testFile);
		auto readTask = asyncIO.ReadFileAsync(testFile);
		std::string readContent = readTask.Get();
		LT_INFO("File read completed. Content: {}", readContent);
		
		// Check if file exists
		auto existsTask = asyncIO.FileExistsAsync(testFile);
		bool exists = existsTask.Get();
		LT_INFO("File exists: {}", exists);
		
		// Get file size
		auto sizeTask = asyncIO.GetFileSizeAsync(testFile);
		size_t fileSize = sizeTask.Get();
		LT_INFO("File size: {} bytes", fileSize);
		
		// Append to file
		const std::string appendContent = "This line was appended asynchronously.\n";
		auto appendTask = asyncIO.AppendFileAsync(testFile, appendContent);
		appendTask.Wait();
		LT_INFO("File append completed");
		
		// Read the updated file
		auto readUpdatedTask = asyncIO.ReadFileAsync(testFile);
		std::string updatedContent = readUpdatedTask.Get();
		LT_INFO("Updated file content: {}", updatedContent);
		
		// Test configuration operations
		LT_INFO("Testing configuration operations...");
		
		nlohmann::json testConfig;
		testConfig["test"] = "value";
		testConfig["number"] = 42;
		testConfig["array"] = {1, 2, 3, 4, 5};
		
		const std::string configFile = "async_test_config.json";
		
		// Save config asynchronously
		auto saveConfigTask = asyncIO.SaveConfigAsync(configFile, testConfig);
		saveConfigTask.Wait();
		LT_INFO("Config saved asynchronously");
		
		// Load config asynchronously
		auto loadConfigTask = asyncIO.LoadConfigAsync(configFile);
		nlohmann::json loadedConfig = loadConfigTask.Get();
		LT_INFO("Config loaded asynchronously: {}", loadedConfig.dump());
		
		// Test directory operations
		LT_INFO("Testing directory operations...");
		
		const std::string testDir = "async_test_dir";
		
		// Create directory
		auto createDirTask = asyncIO.CreateDirectoryAsync(testDir);
		bool dirCreated = createDirTask.Get();
		LT_INFO("Directory created: {}", dirCreated);
		
		// List directory
		auto listDirTask = asyncIO.ListDirectoryAsync(".");
		std::vector<std::string> files = listDirTask.Get();
		LT_INFO("Directory listing (first 5 files):");
		for (size_t i = 0; i < std::min(files.size(), size_t(5)); ++i)
		{
			LT_INFO("  {}", files[i]);
		}
		
		// Test concurrent operations
		LT_INFO("Testing concurrent operations...");
		
		std::vector<Limitless::Async::Task<std::string>> readTasks;
		std::vector<Limitless::Async::Task<void>> writeTasks;
		
		// Create multiple read/write tasks
		for (int i = 0; i < 5; ++i)
		{
			std::string filename = "concurrent_test_" + std::to_string(i) + ".txt";
			std::string content = "Concurrent test file " + std::to_string(i) + "\n";
			
			writeTasks.push_back(asyncIO.WriteFileAsync(filename, content));
			readTasks.push_back(asyncIO.ReadFileAsync(filename));
		}
		
		// Wait for all write tasks to complete
		for (auto& task : writeTasks)
		{
			task.Wait();
		}
		LT_INFO("All write tasks completed");
		
		// Wait for all read tasks to complete
		for (auto& task : readTasks)
		{
			std::string content = task.Get();
			LT_INFO("Read task completed: {}", content.substr(0, content.find('\n')));
		}
		
		// Cleanup test files
		LT_INFO("Cleaning up test files...");
		asyncIO.DeleteFileAsync(testFile).Wait();
		asyncIO.DeleteFileAsync(configFile).Wait();
		asyncIO.DeleteDirectoryAsync(testDir).Wait();
		
		for (int i = 0; i < 5; ++i)
		{
			std::string filename = "concurrent_test_" + std::to_string(i) + ".txt";
			asyncIO.DeleteFileAsync(filename).Wait();
		}
		
		LT_INFO("AsyncIO demo completed successfully!");
		LT_INFO("=== End AsyncIO Demo ===");
	}

	void SandboxApp::DemonstratePerformanceMonitoring()
	{
		LT_INFO("=== Performance Monitoring Demo ===");
		
		// Initialize performance monitor
		auto& monitor = PerformanceMonitor::GetInstance();
		monitor.Initialize();
		monitor.SetLoggingEnabled(true);
		
		// Set up metrics callback
		monitor.SetMetricsCallback([](const PerformanceMetrics& metrics) {
			LT_INFO("=== Performance Metrics ===");
			LT_INFO("Frame: {} ({} FPS avg)", metrics.frameCount, metrics.fpsAvg);
			LT_INFO("Memory: {:.2f}MB current, {:.2f}MB peak", 
				   metrics.currentMemory / (1024.0 * 1024.0),
				   metrics.peakMemory / (1024.0 * 1024.0));
			LT_INFO("CPU: {:.1f}% usage", metrics.cpuUsage);
		});
		
		// Set collection interval to 1 second
		monitor.SetMetricsCollectionInterval(1.0);
		
		LT_INFO("Performance monitor initialized");
		
		// Demonstrate frame timing
		LT_INFO("Demonstrating frame timing...");
		for (int i = 0; i < 10; ++i)
		{
			LT_PERF_BEGIN_FRAME();
			
			// Simulate some frame processing
			std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
			
			LT_PERF_END_FRAME();
			
			double frameTime = monitor.GetFrameTime();
			double fps = monitor.GetFPS();
			LT_INFO("Frame {}: {:.2f}ms ({:.1f} FPS)", i, frameTime, fps);
		}
		
		// Demonstrate performance counters
		LT_INFO("Demonstrating performance counters...");
		
		auto* sortCounter = monitor.CreateCounter("DataSort");
		auto* computeCounter = monitor.CreateCounter("DataCompute");
		
		// Test data sorting
		{
			LT_PERF_COUNTER("DataSort");
			std::vector<int> data(10000);
			std::iota(data.begin(), data.end(), 0);
			std::random_device rd;
			std::mt19937 gen(rd());
			std::shuffle(data.begin(), data.end(), gen);
			std::sort(data.begin(), data.end());
		}
		
		// Test data computation
		{
			LT_PERF_COUNTER("DataCompute");
			double sum = 0.0;
			for (int i = 0; i < 100000; ++i)
			{
				sum += std::sqrt(i) * std::log(i + 1);
			}
		}
		
		LT_INFO("Sort counter: {:.2f}ms", sortCounter->GetLastValue());
		LT_INFO("Compute counter: {:.2f}ms", computeCounter->GetLastValue());
		
		// Demonstrate memory tracking
		LT_INFO("Demonstrating memory tracking...");
		
		auto* tracker = monitor.GetMemoryTracker();
		tracker->Reset();
		
		// Simulate memory allocations
		std::vector<std::vector<int>> allocations;
		for (int i = 0; i < 50; ++i)
		{
			size_t size = (i + 1) * 1024; // 1KB to 50KB
			allocations.emplace_back(size / sizeof(int));
			LT_PERF_TRACK_MEMORY(size);
		}
		
		LT_INFO("Memory allocated: {:.2f}MB", 
			   tracker->GetCurrentMemory() / (1024.0 * 1024.0));
		
		// Deallocate some memory
		for (int i = 0; i < 25; ++i)
		{
			size_t size = (i + 1) * 1024;
			allocations.pop_back();
			LT_PERF_UNTrack_MEMORY(size);
		}
		
		LT_INFO("Memory after deallocation: {:.2f}MB", 
			   tracker->GetCurrentMemory() / (1024.0 * 1024.0));
		LT_INFO("Peak memory: {:.2f}MB", 
			   tracker->GetPeakMemory() / (1024.0 * 1024.0));
		
		// Clean up remaining allocations
		for (auto& alloc : allocations)
		{
			LT_PERF_UNTrack_MEMORY(alloc.size() * sizeof(int));
		}
		
		// Demonstrate metrics collection
		LT_INFO("Demonstrating metrics collection...");
		
		auto metrics = monitor.CollectMetrics();
		LT_INFO("Comprehensive metrics:");
		LT_INFO("  Frame count: {}", metrics.frameCount);
		LT_INFO("  Average FPS: {:.1f}", metrics.fpsAvg);
		LT_INFO("  Average frame time: {:.2f}ms", metrics.frameTimeAvg);
		LT_INFO("  Current memory: {:.2f}MB", metrics.currentMemory / (1024.0 * 1024.0));
		LT_INFO("  CPU usage: {:.1f}%", metrics.cpuUsage);
		LT_INFO("  CPU cores: {}", metrics.cpuCoreCount);
		
		// Demonstrate performance reporting
		LT_INFO("Demonstrating performance reporting...");
		
		std::string metricsStr = monitor.GetMetricsString();
		LT_INFO("Formatted metrics:\n{}", metricsStr);
		
		// Save metrics to file
		monitor.SaveMetricsToFile("sandbox_performance_report.txt");
		LT_INFO("Performance report saved to 'sandbox_performance_report.txt'");
		
		// Shutdown performance monitor
		monitor.Shutdown();
		
		LT_INFO("Performance monitoring demo completed!");
		LT_INFO("=== End Performance Monitoring Demo ===");
	}
}

// Define the CreateApplication function that the entry point expects
Limitless::Application* CreateApplication()
{
	return new Limitless::SandboxApp();
}