using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class Animator : EntityComponent
{
    internal Animator(uint entityHandle)
        : base(entityHandle)
    {
    }

    public string ControllerKey
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAnimatorControllerKeyIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetAnimatorControllerKeyIcall, value);
            }
        }
    }

    public string DefaultClipKey
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAnimatorDefaultClipKeyIcall);
            }
        }
        set
        {
            unsafe
            {
                SetString(ScriptBridge.SetAnimatorDefaultClipKeyIcall, value);
            }
        }
    }

    public float PlaybackSpeed
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorPlaybackSpeedIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimatorPlaybackSpeedIcall(EntityHandle, value);
            }
        }
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimatorEnabledIcall(EntityHandle, value);
            }
        }
    }

    public bool ApplyToSprite
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorApplyToSpriteIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimatorApplyToSpriteIcall(EntityHandle, value);
            }
        }
    }

    public bool ApplyToTransform
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorApplyToTransformIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimatorApplyToTransformIcall(EntityHandle, value);
            }
        }
    }

    public bool AutoPlay
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorAutoPlayIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimatorAutoPlayIcall(EntityHandle, value);
            }
        }
    }

    public string CurrentStateName
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAnimatorCurrentStateNameIcall);
            }
        }
    }

    public string CurrentClipKey
    {
        get
        {
            unsafe
            {
                return GetString(ScriptBridge.GetAnimatorCurrentClipKeyIcall);
            }
        }
    }

    public float StateTimeSeconds
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorStateTimeSecondsIcall(EntityHandle);
            }
        }
    }

    public float CurrentStateDurationSeconds
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimatorCurrentStateDurationSecondsIcall(EntityHandle);
            }
        }
    }

    public bool PlayState(string stateName, bool restartIfSameState = true)
    {
        unsafe
        {
            NativeString nativeValue = stateName ?? string.Empty;
            try
            {
                return ScriptBridge.PlayAnimatorStateIcall(EntityHandle, nativeValue, restartIfSameState);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool PlayClip(string clipKey, bool restartIfSameClip = true)
    {
        unsafe
        {
            NativeString nativeValue = clipKey ?? string.Empty;
            try
            {
                return ScriptBridge.PlayAnimatorClipIcall(EntityHandle, nativeValue, restartIfSameClip);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool SetBool(string parameterName, bool value)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.SetAnimatorBoolParameterIcall(EntityHandle, nativeValue, value);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool GetBool(string parameterName, bool fallback = false)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.GetAnimatorBoolParameterIcall(EntityHandle, nativeValue, fallback);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool SetFloat(string parameterName, float value)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.SetAnimatorFloatParameterIcall(EntityHandle, nativeValue, value);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public float GetFloat(string parameterName, float fallback = 0.0f)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.GetAnimatorFloatParameterIcall(EntityHandle, nativeValue, fallback);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool SetInteger(string parameterName, int value)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.SetAnimatorIntegerParameterIcall(EntityHandle, nativeValue, value);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public int GetInteger(string parameterName, int fallback = 0)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.GetAnimatorIntegerParameterIcall(EntityHandle, nativeValue, fallback);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool SetTrigger(string parameterName)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.SetAnimatorTriggerParameterIcall(EntityHandle, nativeValue);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public bool ResetTrigger(string parameterName)
    {
        unsafe
        {
            NativeString nativeValue = parameterName ?? string.Empty;
            try
            {
                return ScriptBridge.ResetAnimatorTriggerParameterIcall(EntityHandle, nativeValue);
            }
            finally
            {
                nativeValue.Dispose();
            }
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
