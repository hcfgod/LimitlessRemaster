#pragma once

#include "EnTT/entt.hpp"

#include <glm/glm.hpp>
#include <cstdint>

namespace Limitless
{
    struct ParticleEmitterComponent;

    /// Tick all active particle emitters: kill dead particles, apply physics,
    /// spawn new particles, and handle looping/duration logic.
    /// When editModePreview is true, PlayOnStart is ignored (emitters must be
    /// started manually via the inspector preview buttons).
    void UpdateParticleEmitterSystem(entt::registry& registry, float deltaTime, bool editModePreview = false);

    /// Initialize emitters that have PlayOnStart enabled. Called once when the
    /// scene begins playing (enter Play Mode or runtime startup).
    void InitializeParticleEmitters(entt::registry& registry);

    /// Allocate the runtime pool and begin emitting particles.
    void ParticleEmitterPlay(ParticleEmitterComponent& emitter);

    /// Stop emitting. If clearParticles is true, all alive particles are removed
    /// immediately; otherwise they are allowed to die naturally.
    void ParticleEmitterStop(ParticleEmitterComponent& emitter, bool clearParticles = true);

    /// Toggle pause state. Paused emitters freeze all particle motion and spawning.
    void ParticleEmitterPause(ParticleEmitterComponent& emitter);
    void ParticleEmitterResume(ParticleEmitterComponent& emitter);

    /// Immediately spawn a burst of particles (clamped by MaxParticles).
    void ParticleEmitterEmit(ParticleEmitterComponent& emitter, uint32_t count, const glm::vec2& worldPosition);
}
