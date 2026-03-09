namespace Limitless.Managed;

public sealed class CapsuleCollider2D : EntityComponent
{
    internal CapsuleCollider2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 Offset
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DOffsetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DOffsetIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Size
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DSizeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DSizeIcall(EntityHandle, value);
            }
        }
    }

    public CapsuleCollider2DOrientation Direction
    {
        get
        {
            unsafe
            {
                return (CapsuleCollider2DOrientation)ScriptBridge.GetCapsuleCollider2DDirectionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DDirectionIcall(EntityHandle, (int)value);
            }
        }
    }

    public float Density
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DDensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DDensityIcall(EntityHandle, value);
            }
        }
    }

    public float Friction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DFrictionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DFrictionIcall(EntityHandle, value);
            }
        }
    }

    public float Restitution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DRestitutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DRestitutionIcall(EntityHandle, value);
            }
        }
    }

    public bool IsSensor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DIsSensorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DIsSensorIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionLayer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DCollisionLayerIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DCollisionLayerIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionMask
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCapsuleCollider2DCollisionMaskIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCapsuleCollider2DCollisionMaskIcall(EntityHandle, value);
            }
        }
    }
}
