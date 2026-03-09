using Coral.Managed.Interop;

namespace Limitless.Managed;

public abstract class ScriptableEntity
{
    internal uint EntityId;

    public uint EntityHandle => EntityId;
    public Entity Entity => new(EntityId);
    public Transform Transform => new(EntityId);
    public Camera? Camera => Entity.GetComponent<Camera>();

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

    protected bool HasComponent<T>() where T : EntityComponent
    {
        return Entity.HasComponent<T>();
    }

    protected T? GetComponent<T>() where T : EntityComponent
    {
        return Entity.GetComponent<T>();
    }

    protected bool TryGetComponent<T>(out T component) where T : EntityComponent
    {
        return Entity.TryGetComponent(out component);
    }

    protected static Entity FindEntityByTag(string tag)
    {
        return Entity.FindEntityByTag(tag);
    }

    protected Entity CreateEntity(string name = "Entity")
    {
        return Entity.Create(name);
    }

    protected bool DestroyEntity(Entity entity)
    {
        return entity != null && entity.Destroy();
    }

    protected Entity GetParent()
    {
        return Entity.Parent;
    }

    protected Entity GetParent(Entity entity)
    {
        return entity?.Parent ?? Entity.Null;
    }

    protected void SetParent(Entity? parent)
    {
        Entity.SetParent(parent);
    }

    protected Entity[] GetChildren()
    {
        return Entity.GetChildren();
    }

    protected Entity[] GetChildren(Entity entity)
    {
        return entity?.GetChildren() ?? System.Array.Empty<Entity>();
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
