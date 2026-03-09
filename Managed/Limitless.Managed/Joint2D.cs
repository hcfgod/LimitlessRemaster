namespace Limitless.Managed;

public sealed class Joint2D : EntityComponent
{
    internal Joint2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Joint2DType Type
    {
        get
        {
            unsafe
            {
                return (Joint2DType)ScriptBridge.GetJoint2DTypeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DTypeIcall(EntityHandle, (int)value);
            }
        }
    }

    public Entity ConnectedEntity
    {
        get
        {
            unsafe
            {
                return new Entity(ScriptBridge.GetJoint2DConnectedEntityIcall(EntityHandle));
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DConnectedEntityIcall(EntityHandle, value?.Handle ?? Entity.NullHandle);
            }
        }
    }

    public bool CollideConnected
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DCollideConnectedIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DCollideConnectedIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 AnchorA
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DAnchorAIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DAnchorAIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 AnchorB
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DAnchorBIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DAnchorBIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Axis
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DAxisIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DAxisIcall(EntityHandle, value);
            }
        }
    }

    public bool EnableLimit
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DEnableLimitIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DEnableLimitIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Limits
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DLimitsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DLimitsIcall(EntityHandle, value);
            }
        }
    }

    public bool EnableMotor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DEnableMotorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DEnableMotorIcall(EntityHandle, value);
            }
        }
    }

    public float MotorSpeed
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DMotorSpeedIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DMotorSpeedIcall(EntityHandle, value);
            }
        }
    }

    public float MaxMotorForceOrTorque
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DMaxMotorForceOrTorqueIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DMaxMotorForceOrTorqueIcall(EntityHandle, value);
            }
        }
    }

    public bool EnableSpring
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DEnableSpringIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DEnableSpringIcall(EntityHandle, value);
            }
        }
    }

    public float Hertz
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DHertzIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DHertzIcall(EntityHandle, value);
            }
        }
    }

    public float DampingRatio
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetJoint2DDampingRatioIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetJoint2DDampingRatioIcall(EntityHandle, value);
            }
        }
    }
}
