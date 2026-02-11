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
        , m_FrameStartTime(std::chrono::high_resolution_clock::now())
    {
        // Validate configuration
        if (m_Config.maxQueueSize == 0 || (m_Config.maxQueueSize & (m_Config.maxQueueSize - 1)) != 0)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Queue size must be a power of 2");
        }

        // Enforce the fixed underlying capacity at runtime. The queue's backing storage is a
        // compile-time ring buffer; this contract prevents a config value that implies a larger capacity.
        if (m_Config.maxQueueSize > kQueueCapacity)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Queue maxQueueSize exceeds fixed capacity");
        }
    }

    RenderCommandQueue::~RenderCommandQueue()
    {
        // At destruction time we cannot assume a valid GraphicsContext is available, so we
        // cannot safely execute queued GPU commands here. Instead, discard any remaining work
        // loudly so shutdown bugs don't get hidden.
        const uint32_t pending = GetSize();
        if (pending != 0)
        {
            LT_CORE_ERROR("RenderCommandQueue destroyed with {} pending command(s). Pending commands will be discarded.", pending);
        }
        Clear();
    }

    bool RenderCommandQueue::SubmitCommand(std::unique_ptr<RenderCommand> command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command");
            return false;
        }

        RenderCommand* raw = command.release();
        return SubmitCommand(UniqueRenderCommand(raw, RenderCommandDeleter{ [](RenderCommand* c) { delete c; } }));
    }

    bool RenderCommandQueue::SubmitCommand(UniqueRenderCommand command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command");
            return false;
        }

        // Enforce configured maxQueueSize (which may be <= kQueueCapacity).
        uint32_t size = m_ApproxSize.load(std::memory_order_relaxed);
        for (;;)
        {
            if (size >= m_Config.maxQueueSize)
            {
                if (m_Config.enableStatistics)
                {
                    std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                    m_Stats.totalCommandsDropped++;
                }
                LT_CORE_WARN("Render command queue is full (maxQueueSize={}), command dropped", m_Config.maxQueueSize);
                return false;
            }

            if (m_ApproxSize.compare_exchange_weak(
                    size, size + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                break;
            }
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
                std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                m_Stats.totalCommandsSubmitted++;
            }

            return true;
        }

        // If push failed, roll back our size reservation.
        m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);

        // Queue is full
        if (m_Config.enableStatistics)
        {
            std::lock_guard<std::mutex> statsLock(m_StatsMutex);
            m_Stats.totalCommandsDropped++;
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

    bool RenderCommandQueue::SubmitCommands(std::vector<UniqueRenderCommand> commands)
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

        RenderCommand* raw = command.release();
        return SubmitCommandWithPriority(UniqueRenderCommand(raw, RenderCommandDeleter{ [](RenderCommand* c) { delete c; } }), priority);
    }

    bool RenderCommandQueue::SubmitCommandWithPriority(UniqueRenderCommand command, RenderCommandPriority priority)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to submit null command with priority");
            return false;
        }

        // Enforce configured maxQueueSize (which may be <= kQueueCapacity).
        uint32_t size = m_ApproxSize.load(std::memory_order_relaxed);
        for (;;)
        {
            if (size >= m_Config.maxQueueSize)
            {
                if (m_Config.enableStatistics)
                {
                    std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                    m_Stats.totalCommandsDropped++;
                }
                LT_CORE_WARN("Render command queue is full (maxQueueSize={}), priority command dropped", m_Config.maxQueueSize);
                return false;
            }

            if (m_ApproxSize.compare_exchange_weak(
                    size, size + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                break;
            }
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
                std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                m_Stats.totalCommandsSubmitted++;
            }
            return true;
        }

        // If push failed, roll back our size reservation.
        m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);

        // Queue is full
        if (m_Config.enableStatistics)
        {
            std::lock_guard<std::mutex> statsLock(m_StatsMutex);
            m_Stats.totalCommandsDropped++;
        }
        
        LT_CORE_WARN("Render command queue is full, priority command dropped");
        return false;
    }

    void RenderCommandQueue::ExecuteImmediate(GraphicsContext* context, std::unique_ptr<RenderCommand> command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to execute null command immediately");
            return;
        }

        if (!context)
        {
            LT_CORE_ERROR("Attempted to execute command immediately with null graphics context");
            return;
        }

        try
        {
            auto startTime = std::chrono::high_resolution_clock::now();
            command->Execute(context);
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            
            if (m_Config.enableStatistics)
            {
                std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                m_Stats.totalCommandsExecuted++;
                m_Stats.totalExecutionTime += executionTime;
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

    void RenderCommandQueue::ExecuteImmediate(GraphicsContext* context, UniqueRenderCommand command)
    {
        if (!command)
        {
            LT_CORE_WARN("Attempted to execute null command immediately");
            return;
        }

        if (!context)
        {
            LT_CORE_ERROR("Attempted to execute command immediately with null graphics context");
            return;
        }

        try
        {
            auto startTime = std::chrono::high_resolution_clock::now();
            command->Execute(context);
            auto endTime = std::chrono::high_resolution_clock::now();

            auto executionTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

            if (m_Config.enableStatistics)
            {
                std::lock_guard<std::mutex> statsLock(m_StatsMutex);
                m_Stats.totalCommandsExecuted++;
                m_Stats.totalExecutionTime += executionTime;
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

    void RenderCommandQueue::ExecuteImmediate(GraphicsContext* context, std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        for (auto& command : commands)
        {
            ExecuteImmediate(context, std::move(command));
        }
    }

    void RenderCommandQueue::ExecuteImmediate(GraphicsContext* context, std::vector<UniqueRenderCommand> commands)
    {
        for (auto& command : commands)
        {
            ExecuteImmediate(context, std::move(command));
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
            
            m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
            commands.push_back(std::make_unique<QueuedCommand>(std::move(command.value())));
        }

        if (commands.empty())
            return;

        m_InFlightExecutions.fetch_add(static_cast<uint32_t>(commands.size()), std::memory_order_relaxed);

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
                m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
            
            if (!queuedCommand->command)
            {
                LT_CORE_WARN("Encountered queuedCommand with null command in ProcessCommands");
                m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
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

            // Mark one command as fully executed.
            m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
        }

        // If we became idle, wake any Flush() waiters.
        if (m_Queue.IsEmpty() && m_ApproxSize.load(std::memory_order_relaxed) == 0 &&
            m_InFlightExecutions.load(std::memory_order_relaxed) == 0)
        {
            std::lock_guard<std::mutex> lock(m_IdleMutex);
            m_IdleCV.notify_all();
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
            
            m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
            commands.push_back(std::make_unique<QueuedCommand>(std::move(command.value())));
        }

        if (commands.empty())
            return;

        m_InFlightExecutions.fetch_add(static_cast<uint32_t>(commands.size()), std::memory_order_relaxed);

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
                m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
            
            if (!queuedCommand->command)
            {
                LT_CORE_WARN("Encountered queuedCommand with null command in ProcessCommandsBatch");
                m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
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

            // Mark one command as fully executed.
            m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
        }

        // If we became idle, wake any Flush() waiters.
        if (m_Queue.IsEmpty() && m_ApproxSize.load(std::memory_order_relaxed) == 0 &&
            m_InFlightExecutions.load(std::memory_order_relaxed) == 0)
        {
            std::lock_guard<std::mutex> lock(m_IdleMutex);
            m_IdleCV.notify_all();
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

            m_ApproxSize.fetch_sub(1, std::memory_order_relaxed);
            m_InFlightExecutions.fetch_add(1, std::memory_order_relaxed);

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
            m_InFlightExecutions.fetch_sub(1, std::memory_order_relaxed);
            processedCommands++;
        }

        // If we became idle, wake any Flush() waiters.
        if (m_Queue.IsEmpty() && m_ApproxSize.load(std::memory_order_relaxed) == 0 &&
            m_InFlightExecutions.load(std::memory_order_relaxed) == 0)
        {
            std::lock_guard<std::mutex> lock(m_IdleMutex);
            m_IdleCV.notify_all();
        }
    }

    void RenderCommandQueue::Clear()
    {
        m_Queue.Clear();
        m_ApproxSize.store(0, std::memory_order_relaxed);
        
        if (m_Config.enableStatistics)
        {
            std::lock_guard<std::mutex> statsLock(m_StatsMutex);
            m_Stats.currentQueueSize = 0;
        }

        // Clearing discards work; if callers are waiting in Flush(), unblock them.
        {
            std::lock_guard<std::mutex> lock(m_IdleMutex);
            m_IdleCV.notify_all();
        }
    }

    void RenderCommandQueue::Flush()
    {
        // Contract: wait until the queue becomes idle (empty + nothing executing).
        // NOTE: Flush does NOT execute commands by itself; it relies on the context-owning thread
        // (or the render thread) continuing to call ProcessCommands*().
        std::unique_lock<std::mutex> lock(m_IdleMutex);
        m_IdleCV.wait(lock, [this]() {
            return m_Queue.IsEmpty() &&
                   m_ApproxSize.load(std::memory_order_relaxed) == 0 &&
                   m_InFlightExecutions.load(std::memory_order_relaxed) == 0;
        });
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
        return m_ApproxSize.load(std::memory_order_relaxed);
    }

    RenderQueueStats RenderCommandQueue::GetStats() const
    {
        std::lock_guard<std::mutex> statsLock(m_StatsMutex);
        RenderQueueStats stats = m_Stats;
        stats.currentQueueSize = GetSize();
        return stats;
    }

    void RenderCommandQueue::ResetStats()
    {
        std::lock_guard<std::mutex> statsLock(m_StatsMutex);
        m_Stats = RenderQueueStats{};
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
            std::lock_guard<std::mutex> statsLock(m_StatsMutex);
            m_Stats.frameCount++;
            m_Stats.totalFrameTime += frameTime;
            m_Stats.averageFrameTime = static_cast<double>(m_Stats.totalFrameTime) / static_cast<double>(m_Stats.frameCount);

            if (static_cast<double>(frameTime) < m_Stats.minFrameTime)
                m_Stats.minFrameTime = static_cast<double>(frameTime);
            if (static_cast<double>(frameTime) > m_Stats.maxFrameTime)
                m_Stats.maxFrameTime = static_cast<double>(frameTime);
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

        std::lock_guard<std::mutex> statsLock(m_StatsMutex);
        m_Stats.totalCommandsExecuted++;
        m_Stats.totalExecutionTime += executionTime;

        if (m_Stats.totalCommandsExecuted > 0)
        {
            m_Stats.averageExecutionTimePerCommand =
                static_cast<double>(m_Stats.totalExecutionTime) / static_cast<double>(m_Stats.totalCommandsExecuted);
        }
    }

    void RenderCommandQueue::SortCommandsByPriority(std::vector<std::unique_ptr<QueuedCommand>>& commands)
    {
        // IMPORTANT:
        // Render commands are order-dependent. When priorities are equal, we MUST preserve
        // the original relative order (bind/state must remain before draw calls).
        //
        // Using std::sort here can reorder equal-priority commands arbitrarily, which can
        // produce invalid OpenGL state and even crash some drivers.
        std::stable_sort(commands.begin(), commands.end(),
            [](const auto& a, const auto& b) {
                return static_cast<int>(a->priority) > static_cast<int>(b->priority);
            });
    }

    void RenderCommandQueue::BatchCommands(std::vector<std::unique_ptr<QueuedCommand>>& commands)
    {
        // IMPORTANT:
        // Render commands are stateful and order-dependent. Clear operates on the currently
        // bound framebuffer, so it must execute AFTER BindFramebuffer, not before. Reordering
        // Clear to the front would clear the default framebuffer (0) instead of the target FBO,
        // causing accumulation (e.g. EditorLayer viewport quads multiplying when moving camera).
        // Preserve original submission order - no reordering.
        (void)commands;
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

        // NOTE: This type is intentionally restricted today. See header comments in RenderCommandQueue.h.
        (void)threadCount;
    }

    RenderCommandExecutor::~RenderCommandExecutor()
    {
        Stop();
    }

    void RenderCommandExecutor::Start()
    {
        LT_THROW_ERROR(
            ErrorCode::PlatformNotSupported,
            "RenderCommandExecutor is not supported for the current OpenGL-first backend. "
            "Use RenderCommandQueue and process commands on the GraphicsContext-owning thread.");
    }

    void RenderCommandExecutor::Stop()
    {
        // No-op: executor threads are disabled (see Start()).
    }

    void RenderCommandExecutor::SubmitCommands(std::vector<std::unique_ptr<RenderCommand>> commands)
    {
        m_Queue.SubmitCommands(std::move(commands));
        m_WorkerCondition.notify_all();
    }

    void RenderCommandExecutor::WaitForCompletion()
    {
        LT_THROW_ERROR(
            ErrorCode::PlatformNotSupported,
            "RenderCommandExecutor::WaitForCompletion is not supported while multi-threaded GPU execution is disabled. "
            "Use RenderCommandQueue::Flush / ProcessCommands on the GraphicsContext-owning thread.");
    }

    RenderQueueStats RenderCommandExecutor::GetStats() const
    {
        return m_Queue.GetStats();
    }

    void RenderCommandExecutor::WorkerThreadFunction(uint32_t threadId)
    {
        (void)threadId;
        LT_THROW_ERROR(ErrorCode::PlatformNotSupported, "RenderCommandExecutor worker threads are disabled");
    }

    void RenderCommandExecutor::ProcessCommandsInThread(uint32_t threadId)
    {
        (void)threadId;
        LT_THROW_ERROR(ErrorCode::PlatformNotSupported, "RenderCommandExecutor worker threads are disabled");
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