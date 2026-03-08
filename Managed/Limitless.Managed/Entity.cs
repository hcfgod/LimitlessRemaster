using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class Entity
{
    public const uint NullHandle = 0xFFFFFFFFu;

    private readonly uint m_Handle;

    internal Entity(uint handle)
    {
        m_Handle = handle;
    }

    public uint Handle => m_Handle;
    public bool HasHandle => m_Handle != NullHandle;

    public bool IsAlive
    {
        get
        {
            unsafe
            {
                return ScriptBridge.EntityExistsIcall(m_Handle);
            }
        }
    }

    public bool HasTag
    {
        get
        {
            unsafe
            {
                return ScriptBridge.HasTagComponentIcall(m_Handle);
            }
        }
    }

    public bool HasTransform
    {
        get
        {
            unsafe
            {
                return ScriptBridge.HasTransformComponentIcall(m_Handle);
            }
        }
    }

    public bool HasRigidbody2D
    {
        get
        {
            unsafe
            {
                return ScriptBridge.HasRigidbody2DComponentIcall(m_Handle);
            }
        }
    }

    public string Tag
    {
        get
        {
            unsafe
            {
                NativeString nativeTag = ScriptBridge.GetTagIcall(m_Handle);
                try
                {
                    return nativeTag.ToString() ?? string.Empty;
                }
                finally
                {
                    nativeTag.Dispose();
                }
            }
        }
        set
        {
            unsafe
            {
                NativeString nativeTag = value ?? string.Empty;
                try
                {
                    ScriptBridge.SetTagIcall(m_Handle, nativeTag);
                }
                finally
                {
                    nativeTag.Dispose();
                }
            }
        }
    }

    public Transform Transform => new(m_Handle);
    public Rigidbody2D Rigidbody2D => new(m_Handle);

    public static Entity Null => new(NullHandle);

    public override string ToString()
    {
        return HasHandle ? $"Entity({m_Handle})" : "Entity(Null)";
    }
}
