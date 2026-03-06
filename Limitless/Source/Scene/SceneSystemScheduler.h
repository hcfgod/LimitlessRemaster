#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Limitless::Concurrency
{
    class JobSystem;
}

namespace Limitless
{
    enum class SceneSystemAccessComponent : uint64_t
    {
        None = 0,
        Transform = 1ull << 0,
        Hierarchy = 1ull << 1,
        Rigidbody2D = 1ull << 2,
        Animator = 1ull << 3,
        ParticleEmitter = 1ull << 4,
        NativeScript = 1ull << 5,
        BoxCollider2D = 1ull << 6,
        CircleCollider2D = 1ull << 7,
        Joint2D = 1ull << 8,
        Rendering2D = 1ull << 9,
        Lighting2D = 1ull << 10,
        UI = 1ull << 11,
        Audio = 1ull << 12,
        Camera = 1ull << 13,
        Tilemap = 1ull << 14,
        Metadata = 1ull << 15,
        PolygonCollider2D = 1ull << 16,
        EdgeCollider2D = 1ull << 17,
        CapsuleCollider2D = 1ull << 18
    };

    constexpr uint64_t ToAccessMask(SceneSystemAccessComponent component)
    {
        return static_cast<uint64_t>(component);
    }

    struct SceneSystemAccess
    {
        uint64_t Reads = 0;
        uint64_t Writes = 0;
    };

    struct ScheduledSceneSystem
    {
        const char* Name = "UnnamedSceneSystem";
        SceneSystemAccess Access{};
        std::function<void()> Execute;
        bool AllowParallel = true;
    };

    class SceneSystemScheduler
    {
    public:
        static void Run(Concurrency::JobSystem& jobSystem, std::vector<ScheduledSceneSystem>& systems);

    private:
        static bool HasHazard(const SceneSystemAccess& left, const SceneSystemAccess& right);
    };
}
