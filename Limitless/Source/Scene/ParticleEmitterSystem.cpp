#include "Scene/ParticleEmitterSystem.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/RenderingComponents.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/random.hpp>

#include <algorithm>
#include <cmath>

namespace Limitless
{
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    namespace
    {
        constexpr float kGravityAcceleration = 9.81f;

        uint32_t ClampParticleCapacity(uint32_t requestedCapacity)
        {
            return std::clamp(requestedCapacity, 1u, ParticleEmitterComponent::kMaxParticlesCap);
        }

        void EnsureRuntimeCapacity(ParticleEmitterComponent& emitter, ParticleEmitterRuntime& runtime)
        {
            const uint32_t targetCapacity = ClampParticleCapacity(emitter.MaxParticles);
            emitter.MaxParticles = targetCapacity;
            if (runtime.Positions.size() == static_cast<size_t>(targetCapacity))
                return;

            ParticleEmitterRuntime resizedRuntime;
            resizedRuntime.Allocate(targetCapacity);
            resizedRuntime.SpawnAccumulator = runtime.SpawnAccumulator;
            resizedRuntime.ElapsedTime = runtime.ElapsedTime;
            resizedRuntime.BurstFired = runtime.BurstFired;

            const uint32_t copyCount = std::min(runtime.AliveCount, targetCapacity);
            for (uint32_t index = 0; index < copyCount; ++index)
            {
                resizedRuntime.Positions[index] = runtime.Positions[index];
                resizedRuntime.Velocities[index] = runtime.Velocities[index];
                resizedRuntime.Lifetimes[index] = runtime.Lifetimes[index];
                resizedRuntime.MaxLifetimes[index] = runtime.MaxLifetimes[index];
                resizedRuntime.StartSizes[index] = runtime.StartSizes[index];
                resizedRuntime.EndSizes[index] = runtime.EndSizes[index];
                resizedRuntime.StartColors[index] = runtime.StartColors[index];
                resizedRuntime.EndColors[index] = runtime.EndColors[index];
                resizedRuntime.Rotations[index] = runtime.Rotations[index];
                resizedRuntime.RotationSpeeds[index] = runtime.RotationSpeeds[index];
            }
            resizedRuntime.AliveCount = copyCount;

            runtime = std::move(resizedRuntime);
        }

        /// Spawn a single particle at the emitter's world position.
        void SpawnParticle(ParticleEmitterComponent& emitter, ParticleEmitterRuntime& runtime,
                           const glm::vec2& worldPosition)
        {
            if (runtime.AliveCount >= emitter.MaxParticles)
                return;

            const uint32_t index = runtime.AliveCount;

            const float angleMin = std::min(emitter.AngleMin, emitter.AngleMax);
            const float angleMax = std::max(emitter.AngleMin, emitter.AngleMax);
            const auto randomDirection = [&]() -> glm::vec2 {
                const float angleDeg = glm::linearRand(angleMin, angleMax);
                const float angleRad = glm::radians(angleDeg);
                return glm::vec2(std::cos(angleRad), std::sin(angleRad));
            };

            glm::vec2 localSpawnOffset(0.0f);
            if (emitter.UseRadialSpawn)
            {
                const float radiusMin = std::max(0.0f, std::min(emitter.SpawnRadiusMin, emitter.SpawnRadiusMax));
                const float radiusMax = std::max(radiusMin, std::max(emitter.SpawnRadiusMin, emitter.SpawnRadiusMax));
                const float spawnAngleRad = glm::linearRand(0.0f, glm::two_pi<float>());
                const float spawnRadius = glm::linearRand(radiusMin, radiusMax);
                localSpawnOffset = glm::vec2(std::cos(spawnAngleRad), std::sin(spawnAngleRad)) * spawnRadius;
            }
            else
            {
                const float minX = std::min(emitter.SpawnOffsetMin.x, emitter.SpawnOffsetMax.x);
                const float maxX = std::max(emitter.SpawnOffsetMin.x, emitter.SpawnOffsetMax.x);
                const float minY = std::min(emitter.SpawnOffsetMin.y, emitter.SpawnOffsetMax.y);
                const float maxY = std::max(emitter.SpawnOffsetMin.y, emitter.SpawnOffsetMax.y);
                localSpawnOffset = glm::vec2(glm::linearRand(minX, maxX), glm::linearRand(minY, maxY));
            }

            runtime.Positions[index] = worldPosition + localSpawnOffset;

            glm::vec2 direction = randomDirection();
            if (emitter.RadialVelocity)
            {
                const float spawnLengthSquared = glm::dot(localSpawnOffset, localSpawnOffset);
                if (spawnLengthSquared > 0.000001f)
                    direction = glm::normalize(localSpawnOffset);
            }
            const float speedMin = std::max(0.0f, std::min(emitter.SpeedMin, emitter.SpeedMax));
            const float speedMax = std::max(speedMin, std::max(emitter.SpeedMin, emitter.SpeedMax));
            const float speed = glm::linearRand(speedMin, speedMax);
            runtime.Velocities[index] = direction * speed;

            // Lifetime
            const float lifetimeMin = std::max(0.01f, std::min(emitter.LifetimeMin, emitter.LifetimeMax));
            const float lifetimeMax = std::max(lifetimeMin, std::max(emitter.LifetimeMin, emitter.LifetimeMax));
            const float lifetime = glm::linearRand(lifetimeMin, lifetimeMax);
            runtime.Lifetimes[index]    = lifetime;
            runtime.MaxLifetimes[index] = lifetime;

            // Size
            const float startSizeMin = std::max(0.001f, std::min(emitter.StartSizeMin, emitter.StartSizeMax));
            const float startSizeMax = std::max(startSizeMin, std::max(emitter.StartSizeMin, emitter.StartSizeMax));
            runtime.StartSizes[index] = glm::linearRand(startSizeMin, startSizeMax);
            runtime.EndSizes[index]   = emitter.EndSize;

            // Color
            runtime.StartColors[index] = emitter.StartColor;
            runtime.EndColors[index]   = emitter.EndColor;

            // Rotation
            runtime.Rotations[index]      = glm::linearRand(emitter.StartRotationMin, emitter.StartRotationMax);
            runtime.RotationSpeeds[index] = glm::linearRand(emitter.RotationSpeedMin, emitter.RotationSpeedMax);

            ++runtime.AliveCount;
        }

        /// Swap particle at `index` with the last alive particle, then decrement alive count.
        void KillParticle(ParticleEmitterRuntime& runtime, uint32_t index)
        {
            const uint32_t last = runtime.AliveCount - 1;
            if (index != last)
            {
                runtime.Positions[index]      = runtime.Positions[last];
                runtime.Velocities[index]     = runtime.Velocities[last];
                runtime.Lifetimes[index]      = runtime.Lifetimes[last];
                runtime.MaxLifetimes[index]   = runtime.MaxLifetimes[last];
                runtime.StartSizes[index]     = runtime.StartSizes[last];
                runtime.EndSizes[index]       = runtime.EndSizes[last];
                runtime.StartColors[index]    = runtime.StartColors[last];
                runtime.EndColors[index]      = runtime.EndColors[last];
                runtime.Rotations[index]      = runtime.Rotations[last];
                runtime.RotationSpeeds[index] = runtime.RotationSpeeds[last];
            }
            --runtime.AliveCount;
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    void ParticleEmitterPlay(ParticleEmitterComponent& emitter)
    {
        const uint32_t capacity = ClampParticleCapacity(emitter.MaxParticles);
        emitter.MaxParticles = capacity;

        if (!emitter.RuntimeState)
            emitter.RuntimeState = std::make_unique<ParticleEmitterRuntime>();

        // Re-allocate only if capacity changed
        if (emitter.RuntimeState->Positions.size() != static_cast<size_t>(capacity))
            emitter.RuntimeState->Allocate(capacity);
        else
            emitter.RuntimeState->Reset();

        emitter.Playing = true;
        emitter.Paused  = false;
    }

    void ParticleEmitterStop(ParticleEmitterComponent& emitter, bool clearParticles)
    {
        emitter.Playing = false;
        if (clearParticles && emitter.RuntimeState)
            emitter.RuntimeState->AliveCount = 0;
    }

    void ParticleEmitterPause(ParticleEmitterComponent& emitter)
    {
        emitter.Paused = true;
    }

    void ParticleEmitterResume(ParticleEmitterComponent& emitter)
    {
        emitter.Paused = false;
    }

    void ParticleEmitterEmit(ParticleEmitterComponent& emitter, uint32_t count,
                             const glm::vec2& worldPosition)
    {
        if (!emitter.RuntimeState)
        {
            emitter.RuntimeState = std::make_unique<ParticleEmitterRuntime>();
            emitter.RuntimeState->Allocate(ClampParticleCapacity(emitter.MaxParticles));
        }

        auto& runtime = *emitter.RuntimeState;
        EnsureRuntimeCapacity(emitter, runtime);
        for (uint32_t i = 0; i < count && runtime.AliveCount < emitter.MaxParticles; ++i)
            SpawnParticle(emitter, runtime, worldPosition);
    }

    void InitializeParticleEmitters(entt::registry& registry)
    {
        auto view = registry.view<ParticleEmitterComponent>();
        for (entt::entity entity : view)
        {
            auto& emitter = view.get<ParticleEmitterComponent>(entity);
            if (emitter.PlayOnStart)
                ParticleEmitterPlay(emitter);
        }
    }

    void UpdateParticleEmitterSystem(entt::registry& registry, float deltaTime, bool editModePreview)
    {
        auto view = registry.view<ParticleEmitterComponent, TransformComponent>();
        for (entt::entity entity : view)
        {
            auto& emitter   = view.get<ParticleEmitterComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            // Lazy PlayOnStart: auto-play the first time we encounter an unstarted emitter.
            // Skipped in edit-mode preview so emitters only start via inspector buttons.
            if (!editModePreview && emitter.PlayOnStart && !emitter.Playing && !emitter.RuntimeState)
                ParticleEmitterPlay(emitter);

            if (emitter.Paused)
                continue;

            if (!emitter.RuntimeState)
                continue;

            // Emitters that are not playing can still have alive particles draining
            auto& runtime = *emitter.RuntimeState;
            EnsureRuntimeCapacity(emitter, runtime);
            if (!emitter.Playing && runtime.AliveCount == 0)
                continue;

            const glm::vec2 emitterWorldPos = glm::vec2(transform.Position.x, transform.Position.y);

            // ---- 1. Update alive particles ----
            const float gravityDelta = emitter.GravityModifier * kGravityAcceleration * deltaTime;

            for (uint32_t i = 0; i < runtime.AliveCount; )
            {
                runtime.Lifetimes[i] -= deltaTime;
                if (runtime.Lifetimes[i] <= 0.0f)
                {
                    KillParticle(runtime, i);
                    // Don't increment i; the swapped particle needs processing
                    continue;
                }

                runtime.Positions[i]    += runtime.Velocities[i] * deltaTime;
                runtime.Velocities[i].y -= gravityDelta;
                runtime.Rotations[i]    += runtime.RotationSpeeds[i] * deltaTime;

                ++i;
            }

            // ---- 2. Spawn new particles (only while Playing) ----
            if (emitter.Playing)
            {
                // Burst emission at the start of each cycle
                if (emitter.BurstEnabled && !runtime.BurstFired)
                {
                    for (uint32_t b = 0; b < emitter.BurstCount && runtime.AliveCount < emitter.MaxParticles; ++b)
                        SpawnParticle(emitter, runtime, emitterWorldPos);
                    runtime.BurstFired = true;
                }

                // Continuous emission
                runtime.SpawnAccumulator += emitter.SpawnRate * deltaTime;
                while (runtime.SpawnAccumulator >= 1.0f && runtime.AliveCount < emitter.MaxParticles)
                {
                    SpawnParticle(emitter, runtime, emitterWorldPos);
                    runtime.SpawnAccumulator -= 1.0f;
                }

                // ---- 3. Duration / looping ----
                runtime.ElapsedTime += deltaTime;
                if (runtime.ElapsedTime >= emitter.Duration)
                {
                    if (emitter.Looping)
                    {
                        runtime.ElapsedTime = 0.0f;
                        runtime.BurstFired = false;
                    }
                    else
                    {
                        // Stop spawning but let alive particles drain naturally
                        emitter.Playing = false;
                    }
                }
            }
        }
    }
}
