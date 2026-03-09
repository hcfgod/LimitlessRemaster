using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class ParticleEmitter : EntityComponent
{
    internal ParticleEmitter(uint entityHandle)
        : base(entityHandle)
    {
    }

    public float SpawnRate
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpawnRateIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpawnRateIcall(EntityHandle, value); } }
    }

    public float LifetimeMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterLifetimeMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterLifetimeMinIcall(EntityHandle, value); } }
    }

    public float LifetimeMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterLifetimeMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterLifetimeMaxIcall(EntityHandle, value); } }
    }

    public bool Looping
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterLoopingIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterLoopingIcall(EntityHandle, value); } }
    }

    public float Duration
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterDurationIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterDurationIcall(EntityHandle, value); } }
    }

    public bool PlayOnStart
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterPlayOnStartIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterPlayOnStartIcall(EntityHandle, value); } }
    }

    public bool BurstEnabled
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterBurstEnabledIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterBurstEnabledIcall(EntityHandle, value); } }
    }

    public int BurstCount
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterBurstCountIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterBurstCountIcall(EntityHandle, value); } }
    }

    public Vector2 SpawnOffsetMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpawnOffsetMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpawnOffsetMinIcall(EntityHandle, value); } }
    }

    public Vector2 SpawnOffsetMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpawnOffsetMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpawnOffsetMaxIcall(EntityHandle, value); } }
    }

    public bool UseRadialSpawn
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterUseRadialSpawnIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterUseRadialSpawnIcall(EntityHandle, value); } }
    }

    public float SpawnRadiusMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpawnRadiusMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpawnRadiusMinIcall(EntityHandle, value); } }
    }

    public float SpawnRadiusMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpawnRadiusMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpawnRadiusMaxIcall(EntityHandle, value); } }
    }

    public float SpeedMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpeedMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpeedMinIcall(EntityHandle, value); } }
    }

    public float SpeedMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterSpeedMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterSpeedMaxIcall(EntityHandle, value); } }
    }

    public float AngleMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterAngleMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterAngleMinIcall(EntityHandle, value); } }
    }

    public float AngleMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterAngleMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterAngleMaxIcall(EntityHandle, value); } }
    }

    public bool RadialVelocity
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterRadialVelocityIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterRadialVelocityIcall(EntityHandle, value); } }
    }

    public float GravityModifier
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterGravityModifierIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterGravityModifierIcall(EntityHandle, value); } }
    }

    public float StartSizeMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterStartSizeMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterStartSizeMinIcall(EntityHandle, value); } }
    }

    public float StartSizeMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterStartSizeMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterStartSizeMaxIcall(EntityHandle, value); } }
    }

    public float EndSize
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterEndSizeIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterEndSizeIcall(EntityHandle, value); } }
    }

    public Vector4 StartColor
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterStartColorIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterStartColorIcall(EntityHandle, value); } }
    }

    public Vector4 EndColor
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterEndColorIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterEndColorIcall(EntityHandle, value); } }
    }

    public float StartRotationMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterStartRotationMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterStartRotationMinIcall(EntityHandle, value); } }
    }

    public float StartRotationMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterStartRotationMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterStartRotationMaxIcall(EntityHandle, value); } }
    }

    public float RotationSpeedMin
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterRotationSpeedMinIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterRotationSpeedMinIcall(EntityHandle, value); } }
    }

    public float RotationSpeedMax
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterRotationSpeedMaxIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterRotationSpeedMaxIcall(EntityHandle, value); } }
    }

    public string TextureKey
    {
        get
        {
            unsafe
            {
                NativeString nativeValue = ScriptBridge.GetParticleEmitterTextureKeyIcall(EntityHandle);
                try
                {
                    return nativeValue.ToString() ?? string.Empty;
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
        set
        {
            unsafe
            {
                NativeString nativeValue = value ?? string.Empty;
                try
                {
                    ScriptBridge.SetParticleEmitterTextureKeyIcall(EntityHandle, nativeValue);
                }
                finally
                {
                    nativeValue.Dispose();
                }
            }
        }
    }

    public int MaxParticles
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterMaxParticlesIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetParticleEmitterMaxParticlesIcall(EntityHandle, value); } }
    }

    public bool IsPlaying
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterIsPlayingIcall(EntityHandle); } }
    }

    public bool IsPaused
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterIsPausedIcall(EntityHandle); } }
    }

    public int AliveParticleCount
    {
        get { unsafe { return ScriptBridge.GetParticleEmitterAliveParticleCountIcall(EntityHandle); } }
    }

    public void Play()
    {
        unsafe
        {
            ScriptBridge.PlayParticleEmitterIcall(EntityHandle);
        }
    }

    public void Stop(bool clearParticles = true)
    {
        unsafe
        {
            ScriptBridge.StopParticleEmitterIcall(EntityHandle, clearParticles);
        }
    }

    public void Pause()
    {
        unsafe
        {
            ScriptBridge.PauseParticleEmitterIcall(EntityHandle);
        }
    }

    public void Resume()
    {
        unsafe
        {
            ScriptBridge.ResumeParticleEmitterIcall(EntityHandle);
        }
    }

    public void Emit(int count)
    {
        unsafe
        {
            ScriptBridge.EmitParticleEmitterIcall(EntityHandle, count);
        }
    }
}
