using Coral.Managed.Interop;

namespace Limitless.Managed;

public abstract class ScriptableEntity
{
    internal uint EntityId;

    public uint EntityHandle => EntityId;
    public Entity Entity => new(EntityId);
    public Transform Transform => new(EntityId);

    public bool IsEntityAlive
    {
        get
        {
            unsafe
            {
                return ScriptBridge.EntityExistsIcall(EntityId);
            }
        }
    }

    protected Entity FindEntityByTag(string tag)
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

    public virtual void OnCreate()
    {
    }

    public virtual void OnUpdate(float deltaTime)
    {
    }

    public virtual void OnDestroy()
    {
    }

    protected void LogInfo(string message)
    {
        unsafe
        {
            NativeString nativeMessage = message ?? string.Empty;
            try
            {
                ScriptBridge.LogInfoIcall(nativeMessage);
            }
            finally
            {
                nativeMessage.Dispose();
            }
        }
    }

    protected void LogWarning(string message)
    {
        unsafe
        {
            NativeString nativeMessage = message ?? string.Empty;
            try
            {
                ScriptBridge.LogWarningIcall(nativeMessage);
            }
            finally
            {
                nativeMessage.Dispose();
            }
        }
    }

    protected void LogError(string message)
    {
        unsafe
        {
            NativeString nativeMessage = message ?? string.Empty;
            try
            {
                ScriptBridge.LogErrorIcall(nativeMessage);
            }
            finally
            {
                nativeMessage.Dispose();
            }
        }
    }
}
