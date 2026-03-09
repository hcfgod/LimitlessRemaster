namespace Limitless.Managed;

public sealed class PolygonCollider2D : EntityComponent
{
    internal PolygonCollider2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 Offset
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DOffsetIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DOffsetIcall(EntityHandle, value);
            }
        }
    }

    public int PointCount
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DPointCountIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DPointCountIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 GetPoint(int index)
    {
        unsafe
        {
            return ScriptBridge.GetPolygonCollider2DPointIcall(EntityHandle, index);
        }
    }

    public void SetPoint(int index, Vector2 point)
    {
        unsafe
        {
            ScriptBridge.SetPolygonCollider2DPointIcall(EntityHandle, index, point);
        }
    }

    public Vector2[] GetPoints()
    {
        int count = PointCount;
        Vector2[] points = new Vector2[count];
        for (int i = 0; i < count; i++)
            points[i] = GetPoint(i);
        return points;
    }

    public float Density
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DDensityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DDensityIcall(EntityHandle, value);
            }
        }
    }

    public float Friction
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DFrictionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DFrictionIcall(EntityHandle, value);
            }
        }
    }

    public float Restitution
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DRestitutionIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DRestitutionIcall(EntityHandle, value);
            }
        }
    }

    public bool IsSensor
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DIsSensorIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DIsSensorIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionLayer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DCollisionLayerIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DCollisionLayerIcall(EntityHandle, value);
            }
        }
    }

    public ulong CollisionMask
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetPolygonCollider2DCollisionMaskIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetPolygonCollider2DCollisionMaskIcall(EntityHandle, value);
            }
        }
    }
}
