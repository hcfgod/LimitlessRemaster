namespace Limitless.Managed;

public sealed class PointLight2D : EntityComponent
{
    internal PointLight2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DEnabledIcall(EntityHandle, value);
            }
        }
    }

    public Vector3 Color
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DColorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DColorIcall(EntityHandle, value);
            }
        }
    }

    public float Intensity
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DIntensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DIntensityIcall(EntityHandle, value);
            }
        }
    }

    public float Radius
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DRadiusIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DRadiusIcall(EntityHandle, value);
            }
        }
    }

    public float Falloff
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DFalloffIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DFalloffIcall(EntityHandle, value);
            }
        }
    }

    public bool CastShadows
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DCastShadowsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DCastShadowsIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowStrength
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DShadowStrengthIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DShadowStrengthIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowSoftness
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DShadowSoftnessIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DShadowSoftnessIcall(EntityHandle, value);
            }
        }
    }

    public int ShadowSamples
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DShadowSamplesIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DShadowSamplesIcall(EntityHandle, value);
            }
        }
    }

    public float ShadowBias
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPointLight2DShadowBiasIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPointLight2DShadowBiasIcall(EntityHandle, value);
            }
        }
    }
}
