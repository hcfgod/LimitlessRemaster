#include "RenderCommandQueue.h"
#include "GraphicsContext.h"
#include "GraphicsEnums.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"
#include <algorithm>
#include <numeric>
#include <sstream>

namespace Limitless
{
    // RenderCommandQueue implementation
    RenderCommandQueue::RenderCommandQueue(const RenderQueueConfig& config)
        : m_Config(config)
        , m_Stats{}
        , m_FrameStartTime(std::chrono::high_resolution_clock::now())
    {
        // Validate configuration
        if (m_Config.maxQueueSize == 0 || (m_Config.maxQueueSize & (m_Config.maxQueueSize - 1)) != 0)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Queue size must be a power of 2");
        }
    }

    RenderCommandQueue::~RenderCommandQueue()
    {
        // Ensure all commands are processed
        Flush();
    }

    bool RenderCommandQueue::SubmitCommand(std::unique_ptr<RenderCommand> command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command");
            return false;
        }

        QueuedCommand queuedCommand(
            std::move(command), 
            RenderCommandPriority::Normal, 
            m_CurrentFrameId.load()
        );

        if (m_Queue.TryPush(std::move(queuedCommand)))
        {
            if (m_Config.enableStatistics)
            {
                auto stats = m_Stats.load();
                stats.totalCommandsSubmitted++;
                m_Stats.store(stats);
            }

            return true;
        }

        // Queue is full
        if (m_Config.enableStatistics)
        {
            auto stats = m_Stats.load();
            stats.totalCommandsDropped++;
            m_Stats.store(stats);
        }
        
        LT_CORE_WARN("Render command queue is full, command dropped");
        return false;
    }

    bool RenderCommandQueue::SubmitCommands(std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        bool allSubmitted = true;
        
        for (auto& command : commands)
        {
            if (!SubmitCommand(std::move(command)))
            {
                allSubmitted = false;
            }
        }
        
        return allSubmitted;
    }

    bool RenderCommandQueue::SubmitCommandWithPriority(std::unique_ptr<RenderCommand> command, RenderCommandPriority priority)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command with priority");
            return false;
        }

        QueuedCommand queuedCommand(
            std::move(command), 
            priority, 
            m_CurrentFrameId.load()
        );

        if (m_Queue.TryPush(std::move(queuedCommand)))
        {
            if (m_Config.enableStatistics)
            {
                auto stats = m_Stats.load();
                stats.totalCommandsSubmitted++;
                m_Stats.store(stats);
            }
            return true;
        }

        // Queue is full
        if (m_Config.enableStatistics)
        {
            auto stats = m_Stats.load();
            stats.totalCommandsDropped++;
            m_Stats.store(stats);
        }
        
        LT_CORE_WARN("Render command queue is full, priority command dropped");
        return false;
    }

    void RenderCommandQueue::ExecuteImmediate(std::unique_ptr<RenderCommand> command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to execute null command immediately");
            return;
        }

        try
        {
            auto startTime = std::chrono::high_resolution_clock::now();
            command->Execute(nullptr); // Context will be set by caller
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            
            if (m_Config.enableStatistics)
            {
                auto stats = m_Stats.load();
                stats.totalCommandsExecuted++;
                stats.totalExecutionTime += executionTime;
                m_Stats.store(stats);
            }
        }
        catch (const Error& error)
        {
            HandleCommandError(command.get(), error);
        }
        catch (const std::exception& e)
        {
            LT_CORE_ERROR("Exception during immediate command execution: {}", e.what());
        }
    }

    void RenderCommandQueue::ExecuteImmediate(std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        for (auto& command : commands)
        {
            ExecuteImmediate(std::move(command));
        }
    }

    void RenderCommandQueue::ProcessCommands(GraphicsContext* context)
    {
        if (!context)
        {
            LT_CORE_ERROR("Cannot process commands with null graphics context");
            return;
        }

        std::vector<std::unique_ptr<QueuedCommand>> commands;
        commands.reserve(100); // Pre-allocate space

        // Collect commands from queue
        while (commands.size() < m_Config.maxCommandsPerFrame)
        {
            auto command = m_Queue.TryPop();
            if (!command)
                break;
            
            commands.push_back(std::make_unique<QueuedCommand>(std::move(command.value())));
        }

        if (commands.empty())
            return;

        // Sort by priority if enabled
        if (m_Config.enablePrioritySorting)
        {
            SortCommandsByPriority(commands);
        }

        // Batch commands if enabled
        if (m_Config.enableBatching)
        {
            BatchCommands(commands);
        }

        // Execute commands
        for (auto& queuedCommand : commands)
        {
            // Additional safety checks
            if (!queuedCommand)
            {
                LT_CORE_WARN("Encountered null queuedCommand in ProcessCommands");
                continue;
            }
            
            if (!queuedCommand->command)
            {
                LT_CORE_WARN("Encountered queuedCommand with null command in ProcessCommands");
                continue;
            }

            auto startTime = std::chrono::high_resolution_clock::now();
            
            try
            {
                ExecuteCommand(queuedCommand->command.get(), context);
            }
            catch (const Error& error)
            {
                HandleCommandError(queuedCommand->command.get(), error);
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception during command execution: {}", e.what());
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            
            UpdateStatistics(*queuedCommand, executionTime);
        }
    }

    void RenderCommandQueue::ProcessCommandsBatch(GraphicsContext* context, uint32_t maxCommands)
    {
        if (!context)
        {
            LT_CORE_ERROR("Cannot process commands with null graphics context");
            return;
        }

        std::vector<std::unique_ptr<QueuedCommand>> commands;
        commands.reserve(maxCommands);

        // Collect commands from queue
        for (uint32_t i = 0; i < maxCommands; ++i)
        {
            auto command = m_Queue.TryPop();
            if (!command)
                break;
            
            commands.push_back(std::make_unique<QueuedCommand>(std::move(command.value())));
        }

        if (commands.empty())
            return;

        // Sort by priority if enabled
        if (m_Config.enablePrioritySorting)
        {
            SortCommandsByPriority(commands);
        }

        // Execute commands
        for (auto& queuedCommand : commands)
        {
            // Additional safety checks
            if (!queuedCommand)
            {
                LT_CORE_WARN("Encountered null queuedCommand in ProcessCommandsBatch");
                continue;
            }
            
            if (!queuedCommand->command)
            {
                LT_CORE_WARN("Encountered queuedCommand with null command in ProcessCommandsBatch");
                continue;
            }

            auto startTime = std::chrono::high_resolution_clock::now();
            
            try
            {
                ExecuteCommand(queuedCommand->command.get(), context);
            }
            catch (const Error& error)
            {
                HandleCommandError(queuedCommand->command.get(), error);
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception during batch command execution: {}", e.what());
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            
            UpdateStatistics(*queuedCommand, executionTime);
        }
    }

    void RenderCommandQueue::ProcessCommandsWithTimeLimit(GraphicsContext* context, uint32_t maxTimeMicroseconds)
    {
        if (!context)
        {
            LT_CORE_ERROR("Cannot process commands with null graphics context");
            return;
        }

        auto startTime = std::chrono::high_resolution_clock::now();
        uint32_t processedCommands = 0;

        while (processedCommands < m_Config.maxCommandsPerFrame)
        {
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime).count();
            
            if (elapsedTime >= maxTimeMicroseconds)
                break;

            auto command = m_Queue.TryPop();
            if (!command)
                break;

            auto commandStartTime = std::chrono::high_resolution_clock::now();
            
            try
            {
                ExecuteCommand(command->command.get(), context);
            }
            catch (const Error& error)
            {
                HandleCommandError(command->command.get(), error);
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("Exception during time-limited command execution: {}", e.what());
            }

            auto commandEndTime = std::chrono::high_resolution_clock::now();
            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(commandEndTime - commandStartTime).count();
            
            UpdateStatistics(*command, executionTime);
            processedCommands++;
        }
    }

    void RenderCommandQueue::Clear()
    {
        m_Queue.Clear();
        
        if (m_Config.enableStatistics)
        {
            auto stats = m_Stats.load();
            stats.currentQueueSize = 0;
            m_Stats.store(stats);
        }
    }

    void RenderCommandQueue::Flush()
    {
        // Process all remaining commands
        while (!m_Queue.IsEmpty())
        {
            auto command = m_Queue.TryPop();
            if (command && command->command)
            {
                // Note: This requires a valid context, so it's up to the caller to ensure one is available
                LT_CORE_WARN("Flushing command without context: {}", command->command->GetName());
            }
        }
    }

    bool RenderCommandQueue::IsEmpty() const
    {
        return m_Queue.IsEmpty();
    }

    bool RenderCommandQueue::IsFull() const
    {
        return m_Queue.IsFull();
    }

    uint32_t RenderCommandQueue::GetSize() const
    {
        return static_cast<uint32_t>(m_Queue.GetSize());
    }

    RenderQueueStats RenderCommandQueue::GetStats() const
    {
        auto stats = m_Stats.load();
        stats.currentQueueSize = GetSize();
        return stats;
    }

    void RenderCommandQueue::ResetStats()
    {
        m_Stats.store(RenderQueueStats{});
    }

    void RenderCommandQueue::SetConfig(const RenderQueueConfig& config)
    {
        std::lock_guard<std::mutex> lock(m_ConfigMutex);
        m_Config = config;
    }

    void RenderCommandQueue::BeginFrame()
    {
        m_FrameStartTime = std::chrono::high_resolution_clock::now();
        m_CurrentFrameId.fetch_add(1, std::memory_order_relaxed);
    }

    void RenderCommandQueue::EndFrame()
    {
        auto frameEndTime = std::chrono::high_resolution_clock::now();
        auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(frameEndTime - m_FrameStartTime).count();
        
        if (m_Config.enableStatistics)
        {
            auto stats = m_Stats.load();
            stats.frameCount++;
            stats.totalFrameTime += frameTime;
            stats.averageFrameTime = static_cast<double>(stats.totalFrameTime) / stats.frameCount;
            
            if (frameTime < stats.minFrameTime)
                stats.minFrameTime = frameTime;
            if (frameTime > stats.maxFrameTime)
                stats.maxFrameTime = frameTime;
            
            m_Stats.store(stats);
        }
    }

    void RenderCommandQueue::EnableDebugMarkers(bool enable)
    {
        std::lock_guard<std::mutex> lock(m_ConfigMutex);
        m_Config.enableDebugMarkers = enable;
    }

    void RenderCommandQueue::SetDebugCallback(std::function<void(const std::string&)> callback)
    {
        m_DebugCallback = std::move(callback);
    }

    // PriorityQueue implementation
    void RenderCommandQueue::PriorityQueue::Push(std::unique_ptr<QueuedCommand> command)
    {
        commands.push_back(std::move(command));
        std::push_heap(commands.begin(), commands.end(), 
            [](const auto& a, const auto& b) {
                return static_cast<int>(a->priority) < static_cast<int>(b->priority);
            });
    }

    std::unique_ptr<RenderCommandQueue::QueuedCommand> RenderCommandQueue::PriorityQueue::Pop()
    {
        if (commands.empty())
            return nullptr;

        std::pop_heap(commands.begin(), commands.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a->priority) < static_cast<int>(b->priority);
            });

        auto command = std::move(commands.back());
        commands.pop_back();
        return command;
    }

    // Internal methods
    void RenderCommandQueue::UpdateStatistics(const QueuedCommand& command, uint64_t executionTime)
    {
        if (!m_Config.enableStatistics)
            return;

        auto stats = m_Stats.load();
        stats.totalCommandsExecuted++;
        stats.totalExecutionTime += executionTime;
        
        if (stats.totalCommandsExecuted > 0)
        {
            stats.averageExecutionTimePerCommand = static_cast<double>(stats.totalExecutionTime) / stats.totalCommandsExecuted;
        }
        
        m_Stats.store(stats);
    }

    void RenderCommandQueue::SortCommandsByPriority(std::vector<std::unique_ptr<QueuedCommand>>& commands)
    {
        std::sort(commands.begin(), commands.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a->priority) > static_cast<int>(b->priority);
            });
    }

    void RenderCommandQueue::BatchCommands(std::vector<std::unique_ptr<QueuedCommand>>& commands)
    {
        // For now, we'll implement a simple batching approach that doesn't modify the original vector
        // This prevents the null command issue while still allowing for future batching optimizations
        
        // Group commands by type for potential batching
        std::vector<std::unique_ptr<QueuedCommand>> clearCommands;
        std::vector<std::unique_ptr<QueuedCommand>> drawCommands;
        std::vector<std::unique_ptr<QueuedCommand>> otherCommands;
        
        for (auto& command : commands)
        {
            if (!command || !command->command)
                continue;
                
            // Simple type-based batching - can be expanded later
            if (command->command->GetName() == "Clear")
            {
                clearCommands.push_back(std::move(command));
            }
            else if (command->command->GetName().find("Draw") != std::string::npos)
            {
                drawCommands.push_back(std::move(command));
            }
            else
            {
                otherCommands.push_back(std::move(command));
            }
        }
        
        // Restore commands to original vector in batched order
        commands.clear();
        
        // Add clear commands first
        for (auto& cmd : clearCommands)
        {
            commands.push_back(std::move(cmd));
        }
        
        // Add draw commands
        for (auto& cmd : drawCommands)
        {
            commands.push_back(std::move(cmd));
        }
        
        // Add other commands
        for (auto& cmd : otherCommands)
        {
            commands.push_back(std::move(cmd));
        }
    }

    void RenderCommandQueue::ExecuteCommand(RenderCommand* command, GraphicsContext* context)
    {
        if (!command || !context)
            return;

        // Push debug group if enabled
        if (m_Config.enableDebugMarkers)
        {
            PushDebugGroup(command->GetName());
        }

        // Execute the command
        command->Execute(context);

        // Pop debug group if enabled
        if (m_Config.enableDebugMarkers)
        {
            PopDebugGroup();
        }
    }

    void RenderCommandQueue::HandleCommandError(const RenderCommand* command, const Error& error)
    {
        std::string commandName = command ? command->GetName() : "Unknown";
        LT_CORE_ERROR("Render command error in '{}': {}", commandName, error.GetErrorMessage());
        
        if (m_DebugCallback)
        {
            std::stringstream ss;
            ss << "Render command error in '" << commandName << "': " << error.GetErrorMessage();
            m_DebugCallback(ss.str());
        }
    }

    void RenderCommandQueue::PushDebugGroup(const std::string& name)
    {
        m_DebugGroupStack.push_back(name);
        
        if (m_DebugCallback)
        {
            m_DebugCallback("PushDebugGroup: " + name);
        }
    }

    void RenderCommandQueue::PopDebugGroup()
    {
        if (!m_DebugGroupStack.empty())
        {
            std::string name = m_DebugGroupStack.back();
            m_DebugGroupStack.pop_back();
            
            if (m_DebugCallback)
            {
                m_DebugCallback("PopDebugGroup: " + name);
            }
        }
    }

    void RenderCommandQueue::InsertDebugMarker(const std::string& name)
    {
        if (m_DebugCallback)
        {
            m_DebugCallback("DebugMarker: " + name);
        }
    }

    // RenderCommandExecutor implementation
    RenderCommandExecutor::RenderCommandExecutor(GraphicsContext* context, uint32_t threadCount)
        : m_Context(context)
        , m_Queue(RenderQueueConfig{})
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (threadCount == 0)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Thread count must be greater than 0");
        }
    }

    RenderCommandExecutor::~RenderCommandExecutor()
    {
        Stop();
    }

    void RenderCommandExecutor::Start()
    {
        if (m_Running.load())
            return;

        m_Running.store(true);
        m_Shutdown.store(false);

        // Start worker threads
        for (uint32_t i = 0; i < m_WorkerThreads.size(); ++i)
        {
            m_WorkerThreads[i] = std::thread(&RenderCommandExecutor::WorkerThreadFunction, this, i);
        }
    }

    void RenderCommandExecutor::Stop()
    {
        if (!m_Running.load())
            return;

        m_Shutdown.store(true);
        m_Running.store(false);

        // Notify all worker threads
        m_WorkerCondition.notify_all();

        // Wait for all threads to finish
        for (auto& thread : m_WorkerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    void RenderCommandExecutor::SubmitCommands(std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        m_Queue.SubmitCommands(std::move(commands));
    }

    void RenderCommandExecutor::WaitForCompletion()
    {
        m_Queue.Flush();
    }

    RenderQueueStats RenderCommandExecutor::GetStats() const
    {
        return m_Queue.GetStats();
    }

    void RenderCommandExecutor::WorkerThreadFunction(uint32_t threadId)
    {
        m_ActiveWorkers.fetch_add(1);

        while (m_Running.load() && !m_Shutdown.load())
        {
            ProcessCommandsInThread(threadId);
            
            // Wait for more work
            std::unique_lock<std::mutex> lock(m_WorkerMutex);
            m_WorkerCondition.wait_for(lock, std::chrono::milliseconds(1));
        }

        m_ActiveWorkers.fetch_sub(1);
    }

    void RenderCommandExecutor::ProcessCommandsInThread(uint32_t threadId)
    {
        // Process commands in batches
        m_Queue.ProcessCommandsBatch(m_Context, 100);
    }

    // RenderCommandBatch implementation
    RenderCommandBatch::RenderCommandBatch(RenderCommandQueue& queue)
        : m_Queue(queue)
    {
    }

    RenderCommandBatch::~RenderCommandBatch()
    {
        // Submit any remaining commands
        if (!m_Commands.empty())
        {
            Submit();
        }
    }

    void RenderCommandBatch::AddCommand(std::unique_ptr<RenderCommand> command)
    {
        if (command)
        {
            m_Commands.push_back(std::move(command));
        }
    }

    void RenderCommandBatch::AddCommands(std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        for (auto& command : commands)
        {
            AddCommand(std::move(command));
        }
    }

    void RenderCommandBatch::Submit()
    {
        if (!m_Commands.empty())
        {
            m_Queue.SubmitCommands(std::move(m_Commands));
            m_Commands.clear();
        }
    }

    void RenderCommandBatch::SubmitAndWait()
    {
        Submit();
        m_Queue.Flush();
    }

    void RenderCommandBatch::Clear()
    {
        m_Commands.clear();
    }

    // RenderCommands utility functions
    namespace RenderCommands
    {
        std::unique_ptr<ClearCommand> CreateClearCommand(
            bool clearColor, bool clearDepth, bool clearStencil,
            float r, float g, float b, float a)
        {
            ClearCommand::ClearFlags flags;
            flags.color = clearColor;
            flags.depth = clearDepth;
            flags.stencil = clearStencil;
            
            return std::make_unique<ClearCommand>(flags, r, g, b, a);
        }

        std::unique_ptr<SetViewportCommand> CreateViewportCommand(int x, int y, int width, int height)
        {
            return std::make_unique<SetViewportCommand>(x, y, width, height);
        }

        std::vector<std::unique_ptr<RenderCommand>> CreateDrawSequence(
            std::shared_ptr<Shader> shader,
            std::shared_ptr<VertexArray> vertexArray,
            DrawMode mode, uint32_t count, uint32_t first)
        {
            std::vector<std::unique_ptr<RenderCommand>> commands;
            
            if (shader)
                commands.push_back(std::make_unique<BindShaderCommand>(shader));
            
            if (vertexArray)
                commands.push_back(std::make_unique<BindVertexArrayCommand>(vertexArray));
            
            commands.push_back(std::make_unique<DrawArraysCommand>(mode, first, count));
            
            return commands;
        }

        std::vector<std::unique_ptr<RenderCommand>> CreateIndexedDrawSequence(
            std::shared_ptr<Shader> shader,
            std::shared_ptr<VertexArray> vertexArray,
            std::shared_ptr<IndexBuffer> indexBuffer,
            DrawMode mode, uint32_t count, IndexType indexType, void* indices)
        {
            std::vector<std::unique_ptr<RenderCommand>> commands;
            
            if (shader)
                commands.push_back(std::make_unique<BindShaderCommand>(shader));
            
            if (vertexArray)
                commands.push_back(std::make_unique<BindVertexArrayCommand>(vertexArray));
            
            if (indexBuffer)
                commands.push_back(std::make_unique<BindIndexBufferCommand>(indexBuffer));
            
            commands.push_back(std::make_unique<DrawIndexedCommand>(mode, count, indexType, indices));
            
            return commands;
        }

        std::vector<std::unique_ptr<RenderCommand>> CreateDebugGroupSequence(const std::string& name)
        {
            std::vector<std::unique_ptr<RenderCommand>> commands;
            
            commands.push_back(std::make_unique<PushDebugGroupCommand>(name));
            // Note: PopDebugGroupCommand should be added when the group ends
            
            return commands;
        }
    }

} // namespace Limitless 