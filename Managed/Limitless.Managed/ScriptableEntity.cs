using System;
using System.Collections;
using System.Collections.Generic;
using Coral.Managed.Interop;

namespace Limitless.Managed;

public abstract class ScriptableEntity
{
    internal uint EntityId;

    private sealed class CoroutineState
    {
        public CoroutineState(ulong identifier, IEnumerator routine)
        {
            Handle = new Coroutine(identifier);
            EnumeratorStack.Push(routine);
        }

        public Coroutine Handle { get; }
        public Stack<IEnumerator> EnumeratorStack { get; } = new();
        public bool WaitingForSeconds { get; set; }
        public bool WaitingForFrames { get; set; }
        public float RemainingWaitSeconds { get; set; }
        public int RemainingWaitFrames { get; set; }
        public ulong WaitingForCoroutineIdentifier { get; set; }
        public bool SkipWaitTickThisFrame { get; set; }
    }

    private readonly List<CoroutineState> m_ActiveCoroutines = new();
    private readonly List<CoroutineState> m_PendingCoroutineStarts = new();
    private readonly HashSet<ulong> m_PendingCoroutineStops = new();
    private ulong m_NextCoroutineIdentifier = 1;
    private bool m_IsAdvancingCoroutines;

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

    public Coroutine StartCoroutine(IEnumerator routine)
    {
        if (routine == null)
            return new Coroutine(0);

        ulong identifier = m_NextCoroutineIdentifier++;
        if (m_NextCoroutineIdentifier == 0)
            m_NextCoroutineIdentifier = 1;

        CoroutineState coroutineState = new(identifier, routine);
        if (!AdvanceCoroutine(coroutineState))
            return new Coroutine(0);

        coroutineState.SkipWaitTickThisFrame = true;
        if (m_IsAdvancingCoroutines)
            m_PendingCoroutineStarts.Add(coroutineState);
        else
            m_ActiveCoroutines.Add(coroutineState);

        return coroutineState.Handle;
    }

    public bool StopCoroutine(Coroutine? coroutine)
    {
        return coroutine != null && StopCoroutine(coroutine.Identifier);
    }

    public void StopAllCoroutines()
    {
        if (m_IsAdvancingCoroutines)
        {
            m_PendingCoroutineStarts.Clear();
            foreach (CoroutineState coroutineState in m_ActiveCoroutines)
                m_PendingCoroutineStops.Add(coroutineState.Handle.Identifier);
            return;
        }

        m_ActiveCoroutines.Clear();
        m_PendingCoroutineStarts.Clear();
        m_PendingCoroutineStops.Clear();
    }

    public bool IsCoroutineRunning(Coroutine? coroutine)
    {
        return coroutine != null && IsCoroutineRunning(coroutine.Identifier);
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

    internal void DispatchCreateInternal()
    {
        OnCreate();
    }

    internal void DispatchFixedUpdateInternal(float fixedDeltaTime)
    {
        OnFixedUpdate(fixedDeltaTime);
    }

    internal void DispatchUpdateInternal(float deltaTime)
    {
        OnUpdate(deltaTime);
        TickCoroutines(deltaTime);
    }

    internal void DispatchDestroyInternal()
    {
        StopAllCoroutines();
        OnDestroy();
        StopAllCoroutines();
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

    private bool StopCoroutine(ulong identifier)
    {
        if (identifier == 0)
            return false;

        bool existsInActive = m_ActiveCoroutines.Exists(coroutineState => coroutineState.Handle.Identifier == identifier);
        bool existsInPendingStarts = m_PendingCoroutineStarts.Exists(coroutineState => coroutineState.Handle.Identifier == identifier);
        if (!existsInActive && !existsInPendingStarts)
            return false;

        if (m_IsAdvancingCoroutines)
        {
            m_PendingCoroutineStops.Add(identifier);
            return true;
        }

        m_ActiveCoroutines.RemoveAll(coroutineState => coroutineState.Handle.Identifier == identifier);
        m_PendingCoroutineStarts.RemoveAll(coroutineState => coroutineState.Handle.Identifier == identifier);
        return true;
    }

    private bool IsCoroutineRunning(ulong identifier)
    {
        if (identifier == 0)
            return false;

        bool active = m_ActiveCoroutines.Exists(coroutineState => coroutineState.Handle.Identifier == identifier);
        if (!active)
            return m_PendingCoroutineStarts.Exists(coroutineState => coroutineState.Handle.Identifier == identifier);

        return !m_PendingCoroutineStops.Contains(identifier);
    }

    private void TickCoroutines(float deltaTime)
    {
        float safeDeltaTime = SanitizeWaitDurationSeconds(deltaTime);
        m_IsAdvancingCoroutines = true;

        for (int coroutineIndex = 0; coroutineIndex < m_ActiveCoroutines.Count;)
        {
            CoroutineState coroutineState = m_ActiveCoroutines[coroutineIndex];
            if (m_PendingCoroutineStops.Contains(coroutineState.Handle.Identifier))
            {
                m_ActiveCoroutines.RemoveAt(coroutineIndex);
                continue;
            }

            if (coroutineState.SkipWaitTickThisFrame)
            {
                coroutineState.SkipWaitTickThisFrame = false;
                coroutineIndex++;
                continue;
            }

            bool stillWaiting = false;
            if (coroutineState.WaitingForCoroutineIdentifier != 0)
            {
                stillWaiting = IsCoroutineRunning(coroutineState.WaitingForCoroutineIdentifier);
            }
            else if (coroutineState.WaitingForFrames)
            {
                if (coroutineState.RemainingWaitFrames > 0)
                    coroutineState.RemainingWaitFrames--;
                stillWaiting = coroutineState.RemainingWaitFrames > 0;
            }
            else if (coroutineState.WaitingForSeconds)
            {
                coroutineState.RemainingWaitSeconds = Math.Max(0.0f, coroutineState.RemainingWaitSeconds - safeDeltaTime);
                stillWaiting = coroutineState.RemainingWaitSeconds > 0.0f;
            }

            if (stillWaiting)
            {
                coroutineIndex++;
                continue;
            }

            ResetCoroutineWait(coroutineState);
            if (!AdvanceCoroutine(coroutineState))
            {
                m_ActiveCoroutines.RemoveAt(coroutineIndex);
                continue;
            }

            coroutineIndex++;
        }

        m_IsAdvancingCoroutines = false;

        if (m_PendingCoroutineStops.Count > 0)
        {
            m_ActiveCoroutines.RemoveAll(coroutineState => m_PendingCoroutineStops.Contains(coroutineState.Handle.Identifier));
            m_PendingCoroutineStarts.RemoveAll(coroutineState => m_PendingCoroutineStops.Contains(coroutineState.Handle.Identifier));
            m_PendingCoroutineStops.Clear();
        }

        if (m_PendingCoroutineStarts.Count > 0)
        {
            m_ActiveCoroutines.AddRange(m_PendingCoroutineStarts);
            m_PendingCoroutineStarts.Clear();
        }
    }

    private bool AdvanceCoroutine(CoroutineState coroutineState)
    {
        while (coroutineState.EnumeratorStack.Count > 0)
        {
            IEnumerator routine = coroutineState.EnumeratorStack.Peek();

            bool isStillRunning;
            try
            {
                isStillRunning = routine.MoveNext();
            }
            catch (Exception exception)
            {
                LogError($"Coroutine failed: {exception.Message}");
                return false;
            }

            if (!isStillRunning)
            {
                coroutineState.EnumeratorStack.Pop();
                continue;
            }

            if (TryApplyYieldInstruction(coroutineState, routine.Current))
                return true;
        }

        return false;
    }

    private bool TryApplyYieldInstruction(CoroutineState coroutineState, object? yieldedValue)
    {
        ResetCoroutineWait(coroutineState);

        switch (yieldedValue)
        {
            case null:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = 1;
                return true;
            case WaitForSeconds waitForSeconds:
                coroutineState.WaitingForSeconds = true;
                coroutineState.RemainingWaitSeconds = SanitizeWaitDurationSeconds(waitForSeconds.Seconds);
                return true;
            case WaitForFrames waitForFrames:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = Math.Max(1, waitForFrames.FrameCount);
                return true;
            case Coroutine childCoroutine:
                if (childCoroutine.IsValid && IsCoroutineRunning(childCoroutine))
                {
                    coroutineState.WaitingForCoroutineIdentifier = childCoroutine.Identifier;
                    return true;
                }
                return false;
            case IEnumerator nestedRoutine:
                coroutineState.EnumeratorStack.Push(nestedRoutine);
                return false;
            case sbyte frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount((int)frameCount);
                return true;
            case byte frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount((int)frameCount);
                return true;
            case short frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount((int)frameCount);
                return true;
            case ushort frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount((int)frameCount);
                return true;
            case int frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount(frameCount);
                return true;
            case uint frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount(frameCount);
                return true;
            case long frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount(frameCount);
                return true;
            case ulong frameCount:
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = ClampToPositiveFrameCount(frameCount);
                return true;
            case float waitSeconds:
                coroutineState.WaitingForSeconds = true;
                coroutineState.RemainingWaitSeconds = SanitizeWaitDurationSeconds(waitSeconds);
                return true;
            case double waitSeconds:
                coroutineState.WaitingForSeconds = true;
                coroutineState.RemainingWaitSeconds = SanitizeWaitDurationSeconds((float)waitSeconds);
                return true;
            case decimal waitSeconds:
                coroutineState.WaitingForSeconds = true;
                coroutineState.RemainingWaitSeconds = SanitizeWaitDurationSeconds((float)waitSeconds);
                return true;
            default:
                LogWarning($"Unsupported coroutine yield value '{yieldedValue.GetType().FullName}', treating as next frame wait.");
                coroutineState.WaitingForFrames = true;
                coroutineState.RemainingWaitFrames = 1;
                return true;
        }
    }

    private static void ResetCoroutineWait(CoroutineState coroutineState)
    {
        coroutineState.WaitingForSeconds = false;
        coroutineState.WaitingForFrames = false;
        coroutineState.RemainingWaitSeconds = 0.0f;
        coroutineState.RemainingWaitFrames = 0;
        coroutineState.WaitingForCoroutineIdentifier = 0;
    }

    private static float SanitizeWaitDurationSeconds(float durationSeconds)
    {
        if (!float.IsFinite(durationSeconds))
            return 0.0f;

        return Math.Max(0.0f, durationSeconds);
    }

    private static int ClampToPositiveFrameCount(uint frameCount)
    {
        return frameCount == 0 ? 1 : checked((int)Math.Min(frameCount, (uint)int.MaxValue));
    }

    private static int ClampToPositiveFrameCount(int frameCount)
    {
        return frameCount <= 0 ? 1 : frameCount;
    }

    private static int ClampToPositiveFrameCount(long frameCount)
    {
        if (frameCount <= 0)
            return 1;

        return checked((int)Math.Min(frameCount, int.MaxValue));
    }

    private static int ClampToPositiveFrameCount(ulong frameCount)
    {
        if (frameCount == 0)
            return 1;

        return checked((int)Math.Min(frameCount, (ulong)int.MaxValue));
    }
}
