namespace Limitless.Managed;

public sealed class DirectionalLight2D : EntityComponent
{
    internal DirectionalLight2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DEnabledIcall(EntityHandle, value);
            }
        }
    }

    public Vector3 Color
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DColorIcall(EntityHandle, value);
            }
        }
    }

    public float Intensity
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DIntensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DIntensityIcall(EntityHandle, value);
            }
        }
    }

    public bool UseEntityRotation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DUseEntityRotationIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DUseEntityRotationIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Direction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DDirectionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DDirectionIcall(EntityHandle, value);
            }
        }
    }

    public bool CastShadows
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DCastShadowsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DCastShadowsIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowStrength
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DShadowStrengthIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DShadowStrengthIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowSoftness
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DShadowSoftnessIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DShadowSoftnessIcall(EntityHandle, value);
            }
        }
    }

    public int ShadowSamples
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DShadowSamplesIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DShadowSamplesIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowDistance
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DShadowDistanceIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DShadowDistanceIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowBias
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetDirectionalLight2DShadowBiasIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetDirectionalLight2DShadowBiasIcall(EntityHandle, value);
            }
        }
    }
}
