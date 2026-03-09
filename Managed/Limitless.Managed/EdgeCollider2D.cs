namespace Limitless.Managed;

public sealed class EdgeCollider2D : EntityComponent
{
    internal EdgeCollider2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 Offset
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DOffsetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DOffsetIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 PointA
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DPointAIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DPointAIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 PointB
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DPointBIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DPointBIcall(EntityHandle, value);
            }
        }
    }

    public float Friction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DFrictionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DFrictionIcall(EntityHandle, value);
            }
        }
    }

    public float Restitution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DRestitutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DRestitutionIcall(EntityHandle, value);
            }
        }
    }

    public bool IsSensor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DIsSensorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DIsSensorIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionLayer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DCollisionLayerIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DCollisionLayerIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionMask
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEdgeCollider2DCollisionMaskIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEdgeCollider2DCollisionMaskIcall(EntityHandle, value);
            }
        }
    }
}
