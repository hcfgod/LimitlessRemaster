namespace Limitless.Managed;

public sealed class BoxCollider2D : EntityComponent
{
    internal BoxCollider2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 Offset
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DOffsetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DOffsetIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 Size
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DSizeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DSizeIcall(EntityHandle, value);
            }
        }
    }

    public float Density
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DDensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DDensityIcall(EntityHandle, value);
            }
        }
    }

    public float Friction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DFrictionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DFrictionIcall(EntityHandle, value);
            }
        }
    }

    public float Restitution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DRestitutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DRestitutionIcall(EntityHandle, value);
            }
        }
    }

    public bool IsSensor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DIsSensorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DIsSensorIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionLayer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DCollisionLayerIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DCollisionLayerIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionMask
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetBoxCollider2DCollisionMaskIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetBoxCollider2DCollisionMaskIcall(EntityHandle, value);
            }
        }
    }
}
