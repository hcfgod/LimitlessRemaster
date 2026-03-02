#include "Scene/SceneSystemScheduler.h"

#include "Core/Concurrency/JobSystem.h"

#include <algorithm>

namespace Limitless
{
    bool SceneSystemScheduler::HasHazard(const SceneSystemAccess& left, const SceneSystemAccess& right)
    {
        const uint64_t writeWrite = left.Writes & right.Writes;
        const uint64_t writeRead = left.Writes & right.Reads;
        const uint64_t readWrite = left.Reads & right.Writes;
        return writeWrite != 0 || writeRead != 0 || readWrite != 0;
    }

    void SceneSystemScheduler::Run(Concurrency::JobSystem& jobSystem, std::vector<ScheduledSceneSystem>& systems)
    {
        if (systems.empty())
            return;

        std::vector<size_t> pending;
        pending.reserve(systems.size());
        for (size_t index = 0; index < systems.size(); ++index)
            pending.push_back(index);

        while (!pending.empty())
        {
            std::vector<size_t> batch;
            std::vector<size_t> nextPending;
            bool batchContainsSerial = false;
            batch.reserve(pending.size());
            nextPending.reserve(pending.size());

            for (size_t pendingIndex : pending)
            {
                const auto& candidate = systems[pendingIndex];

                if (!candidate.Execute)
                    continue;

                bool conflictsBatch = batchContainsSerial || (!candidate.AllowParallel && !batch.empty());
                if (!conflictsBatch)
                {
                    for (size_t batchIndex : batch)
                    {
                        if (HasHazard(candidate.Access, systems[batchIndex].Access))
                        {
                            conflictsBatch = true;
                            break;
                        }
                    }
                }

                if (!conflictsBatch)
                {
                    batch.push_back(pendingIndex);
                    if (!candidate.AllowParallel)
                        batchContainsSerial = true;
                }
                else
                    nextPending.push_back(pendingIndex);
            }

            if (batch.empty())
            {
                // Safety fallback: force progress with one system.
                batch.push_back(pending.front());
                const auto forcedIt = std::find(nextPending.begin(), nextPending.end(), pending.front());
                if (forcedIt != nextPending.end())
                    nextPending.erase(forcedIt);
            }

            if (!jobSystem.IsInitialized())
            {
                for (size_t batchIndex : batch)
                    systems[batchIndex].Execute();
            }
            else if (batch.size() == 1)
            {
                systems[batch.front()].Execute();
            }
            else
            {
                Concurrency::WaitGroup waitGroup;
                for (size_t batchIndex : batch)
                {
                    waitGroup.Add(1);
                    jobSystem.Submit([&systems, &waitGroup, batchIndex]() {
                        systems[batchIndex].Execute();
                        waitGroup.Done();
                    });
                }
                waitGroup.Wait();
            }

            pending = std::move(nextPending);
        }
    }
}
