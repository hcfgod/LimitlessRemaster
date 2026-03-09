namespace Limitless.Managed;

public sealed class CircleCollider2D : EntityComponent
{
    internal CircleCollider2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 Offset
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DOffsetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DOffsetIcall(EntityHandle, value);
            }
        }
    }

    public float Radius
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DRadiusIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DRadiusIcall(EntityHandle, value);
            }
        }
    }

    public float Density
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DDensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DDensityIcall(EntityHandle, value);
            }
        }
    }

    public float Friction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DFrictionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DFrictionIcall(EntityHandle, value);
            }
        }
    }

    public float Restitution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DRestitutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DRestitutionIcall(EntityHandle, value);
            }
        }
    }

    public bool IsSensor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DIsSensorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DIsSensorIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionLayer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DCollisionLayerIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DCollisionLayerIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionMask
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetCircleCollider2DCollisionMaskIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetCircleCollider2DCollisionMaskIcall(EntityHandle, value);
            }
        }
    }
}
