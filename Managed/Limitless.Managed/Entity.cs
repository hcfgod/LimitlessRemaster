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

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEntityEnabledIcall(m_Handle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEntityEnabledIcall(m_Handle, value);
            }
        }
    }

    public bool IsEnabledInHierarchy
    {
        get
        {
            unsafe
            {
                return ScriptBridge.IsEntityEnabledInHierarchyIcall(m_Handle);
            }
        }
    }

    public Entity Parent
    {
        get
        {
            unsafe
            {
                return new Entity(ScriptBridge.GetParentIcall(m_Handle));
            }
        }
        set
        {
            SetParent(value);
        }
    }

    public int ChildCount
    {
        get
        {
            unsafe
            {
                return checked((int)ScriptBridge.GetChildCountIcall(m_Handle));
            }
        }
    }

    public string Tag
    {
        get
        {
            unsafe
            {
                if (!ScriptBridge.HasTagComponentIcall(m_Handle))
                    return string.Empty;

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

    public bool CompareTag(string tag)
    {
        return string.Equals(Tag, tag ?? string.Empty, System.StringComparison.Ordinal);
    }

    public byte Layer
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetEntityLayerIcall(m_Handle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetEntityLayerIcall(m_Handle, value);
            }
        }
    }

    public void SetParent(Entity? parent)
    {
        unsafe
        {
            uint parentHandle = parent?.Handle ?? NullHandle;
            ScriptBridge.SetParentIcall(m_Handle, parentHandle);
        }
    }

    public Entity GetChild(int index)
    {
        if (index < 0)
            throw new System.ArgumentOutOfRangeException(nameof(index));

        unsafe
        {
            return new Entity(ScriptBridge.GetChildAtIcall(m_Handle, checked((uint)index)));
        }
    }

    public Entity[] GetChildren()
    {
        int count = ChildCount;
        Entity[] children = new Entity[count];
        for (int i = 0; i < count; i++)
            children[i] = GetChild(i);
        return children;
    }

    public bool Destroy()
    {
        unsafe
        {
            return ScriptBridge.DestroyEntityIcall(m_Handle);
        }
    }

    public bool HasComponent<T>() where T : EntityComponent
    {
        return EntityComponentResolver.HasComponent<T>(m_Handle);
    }

    public T? GetComponent<T>() where T : EntityComponent
    {
        return EntityComponentResolver.GetComponent<T>(m_Handle);
    }

    public bool TryGetComponent<T>(out T component) where T : EntityComponent
    {
        return EntityComponentResolver.TryGetComponent(m_Handle, out component);
    }

    public Transform Transform => new(m_Handle);

    public static Entity Create(string name = "Entity")
    {
        unsafe
        {
            NativeString nativeName = name ?? string.Empty;
            try
            {
                return new Entity(ScriptBridge.CreateEntityIcall(nativeName));
            }
            finally
            {
                nativeName.Dispose();
            }
        }
    }

    public static Entity FindEntityByTag(string tag)
    {
        unsafe
        {
            NativeString nativeTag = tag ?? string.Empty;
            try
            {
                uint entityHandle = ScriptBridge.FindEntityByTagIcall(nativeTag);
                return new Entity(entityHandle);
            }
            finally
            {
                nativeTag.Dispose();
            }
        }
    }

    public static Entity Null => new(NullHandle);

    public override string ToString()
    {
        return HasHandle ? $"Entity({m_Handle})" : "Entity(Null)";
    }
}
