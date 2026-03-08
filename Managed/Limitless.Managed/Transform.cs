namespace Limitless.Managed;

public sealed class Transform
{
    private readonly uint m_EntityHandle;

    internal Transform(uint entityHandle)
    {
        m_EntityHandle = entityHandle;
    }

    public Vector3 Position
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformPositionIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformPositionIcall(m_EntityHandle, value);
            }
        }
    }

    public Vector3 Rotation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformRotationIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformRotationIcall(m_EntityHandle, value);
            }
        }
    }

    public Vector3 Scale
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetTransformScaleIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetTransformScaleIcall(m_EntityHandle, value);
            }
        }
    }
}
