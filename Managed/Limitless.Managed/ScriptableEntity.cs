using Coral.Managed.Interop;

namespace Limitless.Managed;

public abstract class ScriptableEntity
{
    internal uint EntityId;

    public uint EntityHandle => EntityId;
    public Entity Entity => new(EntityId);
    public Transform Transform => new(EntityId);
    public Rigidbody2D Rigidbody2D => new(EntityId);

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

    public virtual void OnFixedUpdate(float fixedDeltaTime)
    {
    }

    public virtual void OnUpdate(float deltaTime)
    {
    }

    public virtual void OnCollisionEnter(Entity other)
    {
    }

    public virtual void OnCollisionStay(Entity other)
    {
    }

    public virtual void OnCollisionExit(Entity other)
    {
    }

    public virtual void OnTriggerEnter(Entity other)
    {
    }

    public virtual void OnTriggerStay(Entity other)
    {
    }

    public virtual void OnTriggerExit(Entity other)
    {
    }

    public virtual void OnDestroy()
    {
    }

    internal void DispatchCollisionEnterInternal(uint otherEntityHandle)
    {
        OnCollisionEnter(new Entity(otherEntityHandle));
    }

    internal void DispatchCollisionStayInternal(uint otherEntityHandle)
    {
        OnCollisionStay(new Entity(otherEntityHandle));
    }

    internal void DispatchCollisionExitInternal(uint otherEntityHandle)
    {
        OnCollisionExit(new Entity(otherEntityHandle));
    }

    internal void DispatchTriggerEnterInternal(uint otherEntityHandle)
    {
        OnTriggerEnter(new Entity(otherEntityHandle));
    }

    internal void DispatchTriggerStayInternal(uint otherEntityHandle)
    {
        OnTriggerStay(new Entity(otherEntityHandle));
    }

    internal void DispatchTriggerExitInternal(uint otherEntityHandle)
    {
        OnTriggerExit(new Entity(otherEntityHandle));
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
