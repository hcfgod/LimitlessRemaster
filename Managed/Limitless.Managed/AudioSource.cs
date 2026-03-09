using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class AudioSource : EntityComponent
{
    internal AudioSource(uint entityHandle)
        : base(entityHandle)
    {
    }

    public string ClipKey
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAudioSourceClipKeyIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetAudioSourceClipKeyIcall, value);
            }
        }
    }

    public float Volume
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceVolumeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceVolumeIcall(EntityHandle, value);
            }
        }
    }

    public float Pitch
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourcePitchIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourcePitchIcall(EntityHandle, value);
            }
        }
    }

    public bool PlayOnStart
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourcePlayOnStartIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourcePlayOnStartIcall(EntityHandle, value);
            }
        }
    }

    public bool Loop
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceLoopIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceLoopIcall(EntityHandle, value);
            }
        }
    }

    public bool Muted
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceMutedIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceMutedIcall(EntityHandle, value);
            }
        }
    }

    public AudioPlaybackSpace PlaybackSpace
    {
        get
        {
            unsafe
            {
                return (AudioPlaybackSpace)ScriptBridge.GetAudioSourcePlaybackSpaceIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourcePlaybackSpaceIcall(EntityHandle, (int)value);
            }
        }
    }

    public string MixerGroup
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAudioSourceMixerGroupIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetAudioSourceMixerGroupIcall, value);
            }
        }
    }

    public float SpatialMinDistance
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceSpatialMinDistanceIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceSpatialMinDistanceIcall(EntityHandle, value);
            }
        }
    }

    public float SpatialMaxDistance
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceSpatialMaxDistanceIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceSpatialMaxDistanceIcall(EntityHandle, value);
            }
        }
    }

    public float SpatialRolloffExponent
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceSpatialRolloffExponentIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceSpatialRolloffExponentIcall(EntityHandle, value);
            }
        }
    }

    public float StereoPanStrength
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceStereoPanStrengthIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceStereoPanStrengthIcall(EntityHandle, value);
            }
        }
    }

    public AudioRolloffMode SpatialRolloffMode
    {
        get
        {
            unsafe
            {
                return (AudioRolloffMode)ScriptBridge.GetAudioSourceSpatialRolloffModeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceSpatialRolloffModeIcall(EntityHandle, (int)value);
            }
        }
    }

    public float DopplerFactor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceDopplerFactorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceDopplerFactorIcall(EntityHandle, value);
            }
        }
    }

    public bool EnableDirectionalAttenuation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceEnableDirectionalAttenuationIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceEnableDirectionalAttenuationIcall(EntityHandle, value);
            }
        }
    }

    public float DirectionalInnerAngleDegrees
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceDirectionalInnerAngleDegreesIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceDirectionalInnerAngleDegreesIcall(EntityHandle, value);
            }
        }
    }

    public float DirectionalOuterAngleDegrees
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceDirectionalOuterAngleDegreesIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceDirectionalOuterAngleDegreesIcall(EntityHandle, value);
            }
        }
    }

    public float DirectionalOuterVolume
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceDirectionalOuterVolumeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAudioSourceDirectionalOuterVolumeIcall(EntityHandle, value);
            }
        }
    }

    public string AttenuationCurveKey
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAudioSourceAttenuationCurveKeyIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetAudioSourceAttenuationCurveKeyIcall, value);
            }
        }
    }

    public bool IsPlaying
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAudioSourceIsPlayingIcall(EntityHandle);
            }
        }
    }

    public bool Play()
    {
        unsafe
        {
            return ScriptBridge.RequestAudioSourcePlayIcall(EntityHandle);
        }
    }

    public void Stop()
    {
        unsafe
        {
            ScriptBridge.StopAudioSourceIcall(EntityHandle);
        }
    }

    private unsafe string GetString(delegate*<uint, NativeString> getter)
    {
        NativeString nativeValue = getter(EntityHandle);
        try
        {
            return nativeValue.ToString() ?? string.Empty;
        }
        finally
        {
            nativeValue.Dispose();
        }
    }

    private unsafe void SetString(delegate*<uint, NativeString, void> setter, string value)
    {
        NativeString nativeValue = value ?? string.Empty;
        try
        {
            setter(EntityHandle, nativeValue);
        }
        finally
        {
            nativeValue.Dispose();
        }
    }
}
