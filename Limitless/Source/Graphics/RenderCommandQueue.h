#pragma once

#include "RenderCommand.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Error.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

namespace Limitless
{
    // Forward declaration
    class GraphicsContext;

    // Render command queue statistics
    struct RenderQueueStats
    {
        uint64_t totalCommandsSubmitted = 0;
        uint64_t totalCommandsExecuted = 0;
        uint64_t totalCommandsDropped = 0;
        uint64_t totalExecutionTime = 0; // microseconds
        uint32_t currentQueueSize = 0;
        uint32_t maxQueueSize = 0;
        uint32_t averageCommandsPerFrame = 0;
        double averageExecutionTimePerCommand = 0.0;
        
        // Performance metrics
        uint64_t frameCount = 0;
        uint64_t totalFrameTime = 0; // microseconds
        double averageFrameTime = 0.0;
        double minFrameTime = std::numeric_limits<double>::max();
        double maxFrameTime = 0.0;
    };

    // Render command queue configuration
    struct RenderQueueConfig
    {
        uint32_t maxQueueSize = 16384; // Must be power of 2 for lock-free queue
        uint32_t maxCommandsPerFrame = 10000;
        uint32_t maxExecutionTimePerFrame = 16000; // microseconds (16ms)
        bool enableBatching = true;
        bool enablePrioritySorting = true;
        bool enableStatistics = true;
        bool enableDebugMarkers = false;
        uint32_t workerThreadCount = 1; // For multi-threaded rendering
    };

    // Render command queue using lock-free queue
    class RenderCommandQueue
    {
    public:
        explicit RenderCommandQueue(const RenderQueueConfig& config = RenderQueueConfig{});
        ~RenderCommandQueue();

        // Disable copy and assignment
        RenderCommandQueue(const RenderCommandQueue&) = delete;
        RenderCommandQueue& operator=(const RenderCommandQueue&) = delete;

        // Submit commands to the queue
        bool SubmitCommand(std::unique_ptr<RenderCommand> command);
        bool SubmitCommands(std::vector<std::unique_ptr<RenderCommand>> commands);
        
        // Submit commands with priority
        bool SubmitCommandWithPriority(std::unique_ptr<RenderCommand> command, RenderCommandPriority priority);
        
        // Submit commands for immediate execution (bypasses queue)
        void ExecuteImmediate(std::unique_ptr<RenderCommand> command);
        void ExecuteImmediate(std::vector<std::unique_ptr<RenderCommand>> commands);

        // Process and execute commands from the queue
        void ProcessCommands(GraphicsContext* context);
        void ProcessCommandsBatch(GraphicsContext* context, uint32_t maxCommands = 100);
        
        // Process commands with time limit
        void ProcessCommandsWithTimeLimit(GraphicsContext* context, uint32_t maxTimeMicroseconds);

        // Queue management
        void Clear();
        void Flush(); // Wait for all commands to be processed
        bool IsEmpty() const;
        bool IsFull() const;
        uint32_t GetSize() const;
        uint32_t GetMaxSize() const { return m_Config.maxQueueSize; }

        // Statistics
        RenderQueueStats GetStats() const;
        void ResetStats();
        
        // Configuration
        void SetConfig(const RenderQueueConfig& config);
        const RenderQueueConfig& GetConfig() const { return m_Config; }

        // Thread safety
        bool IsThreadSafe() const { return true; }
        
        // Performance monitoring
        void BeginFrame();
        void EndFrame();
        
        // Debug and profiling
        void EnableDebugMarkers(bool enable);
        void SetDebugCallback(std::function<void(const std::string&)> callback);

    private:
        // Command wrapper for queue storage
        struct QueuedCommand
        {
            std::unique_ptr<RenderCommand> command;
            RenderCommandPriority priority;
            std::chrono::high_resolution_clock::time_point submissionTime;
            uint64_t frameId;
            
            QueuedCommand() = default;
            
            QueuedCommand(std::unique_ptr<RenderCommand> cmd, RenderCommandPriority prio, uint64_t frame)
                : command(std::move(cmd))
                , priority(prio)
                , submissionTime(std::chrono::high_resolution_clock::now())
                , frameId(frame)
            {}
            
            // Move constructor
            QueuedCommand(QueuedCommand&& other) noexcept
                : command(std::move(other.command))
                , priority(other.priority)
                , submissionTime(other.submissionTime)
                , frameId(other.frameId)
            {}
            
            // Move assignment operator
            QueuedCommand& operator=(QueuedCommand&& other) noexcept
            {
                if (this != &other)
                {
                    command = std::move(other.command);
                    priority = other.priority;
                    submissionTime = other.submissionTime;
                    frameId = other.frameId;
                }
                return *this;
            }
            
            // Disable copy constructor and assignment
            QueuedCommand(const QueuedCommand&) = delete;
            QueuedCommand& operator=(const QueuedCommand&) = delete;
        };

        // Priority queue for sorting commands
        struct PriorityQueue
        {
            std::vector<std::unique_ptr<QueuedCommand>> commands;
            
            void Push(std::unique_ptr<QueuedCommand> command);
            std::unique_ptr<QueuedCommand> Pop();
            bool IsEmpty() const { return commands.empty(); }
            void Clear() { commands.clear(); }
            size_t Size() const { return commands.size(); }
        };

        // Internal methods
        void UpdateStatistics(const QueuedCommand& command, uint64_t executionTime);
        void SortCommandsByPriority(std::vector<std::unique_ptr<QueuedCommand>>& commands);
        void BatchCommands(std::vector<std::unique_ptr<QueuedCommand>>& commands);
        void ExecuteCommand(RenderCommand* command, GraphicsContext* context);
        
        // Error handling
        void HandleCommandError(const RenderCommand* command, const Error& error);
        
        // Debug and profiling
        void PushDebugGroup(const std::string& name);
        void PopDebugGroup();
        void InsertDebugMarker(const std::string& name);

    private:
        // Lock-free queue for command storage
        Concurrency::LockFreeMPMCQueue<QueuedCommand, 16384> m_Queue;
        
        // Configuration
        RenderQueueConfig m_Config;
        
        // Statistics (atomic for thread safety)
        mutable std::atomic<RenderQueueStats> m_Stats;
        
        // Frame tracking
        std::atomic<uint64_t> m_CurrentFrameId{0};
        std::chrono::high_resolution_clock::time_point m_FrameStartTime;
        
        // Debug and profiling
        std::function<void(const std::string&)> m_DebugCallback;
        std::vector<std::string> m_DebugGroupStack;
        
        // Thread safety
        mutable std::mutex m_StatsMutex;
        mutable std::mutex m_ConfigMutex;
    };

    // Render command executor for multi-threaded rendering
    class RenderCommandExecutor
    {
    public:
        explicit RenderCommandExecutor(GraphicsContext* context, uint32_t threadCount = 1);
        ~RenderCommandExecutor();

        // Disable copy and assignment
        RenderCommandExecutor(const RenderCommandExecutor&) = delete;
        RenderCommandExecutor& operator=(const RenderCommandExecutor&) = delete;

        // Start/stop execution
        void Start();
        void Stop();
        bool IsRunning() const { return m_Running.load(); }

        // Submit commands for execution
        void SubmitCommands(std::vector<std::unique_ptr<RenderCommand>> commands);
        
        // Wait for completion
        void WaitForCompletion();
        
        // Get execution statistics
        RenderQueueStats GetStats() const;

    private:
        // Worker thread function
        void WorkerThreadFunction(uint32_t threadId);
        
        // Process commands in worker thread
        void ProcessCommandsInThread(uint32_t threadId);

    private:
        GraphicsContext* m_Context;
        RenderCommandQueue m_Queue;
        std::vector<std::thread> m_WorkerThreads;
        std::atomic<bool> m_Running{false};
        std::atomic<bool> m_Shutdown{false};
        
        // Thread synchronization
        std::mutex m_WorkerMutex;
        std::condition_variable m_WorkerCondition;
        std::atomic<uint32_t> m_ActiveWorkers{0};
    };

    // Render command batch for efficient command grouping
    class RenderCommandBatch
    {
    public:
        explicit RenderCommandBatch(RenderCommandQueue& queue);
        ~RenderCommandBatch();

        // Add commands to batch
        void AddCommand(std::unique_ptr<RenderCommand> command);
        void AddCommands(std::vector<std::unique_ptr<RenderCommand>> commands);
        
        // Submit batch
        void Submit();
        void SubmitAndWait();
        
        // Clear batch
        void Clear();
        
        // Get batch size
        size_t GetSize() const { return m_Commands.size(); }
        bool IsEmpty() const { return m_Commands.empty(); }

    private:
        RenderCommandQueue& m_Queue;
        std::vector<std::unique_ptr<RenderCommand>> m_Commands;
    };

    // Utility functions for creating common command sequences
    namespace RenderCommands
    {
        // Create a clear command
        std::unique_ptr<ClearCommand> CreateClearCommand(
            bool clearColor = true, bool clearDepth = true, bool clearStencil = false,
            float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

        // Create a viewport command
        std::unique_ptr<SetViewportCommand> CreateViewportCommand(int x, int y, int width, int height);

        // Create a draw command sequence
        std::vector<std::unique_ptr<RenderCommand>> CreateDrawSequence(
            std::shared_ptr<Shader> shader,
            std::shared_ptr<VertexArray> vertexArray,
            uint32_t mode, uint32_t count, uint32_t first = 0);

        // Create an indexed draw command sequence
        std::vector<std::unique_ptr<RenderCommand>> CreateIndexedDrawSequence(
            std::shared_ptr<Shader> shader,
            std::shared_ptr<VertexArray> vertexArray,
            std::shared_ptr<IndexBuffer> indexBuffer,
            uint32_t mode, uint32_t count, uint32_t indexType, void* indices = nullptr);

        // Create a debug group command sequence
        std::vector<std::unique_ptr<RenderCommand>> CreateDebugGroupSequence(const std::string& name);
    };

} // namespace Limitless 