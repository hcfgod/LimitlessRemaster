using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class AnimationEventReceiver : EntityComponent
{
    internal AnimationEventReceiver(uint entityHandle)
        : base(entityHandle)
    {
    }

    public bool Enabled
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimationEventReceiverEnabledIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetAnimationEventReceiverEnabledIcall(EntityHandle, value);
            }
        }
    }

    public int EventCount
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetAnimationEventReceiverDispatchedEventCountIcall(EntityHandle);
            }
        }
    }

    public AnimationEvent GetEvent(int index)
    {
        unsafe
        {
            NativeString name = ScriptBridge.GetAnimationEventReceiverEventNameIcall(EntityHandle, index);
            NativeString stringPayload = ScriptBridge.GetAnimationEventReceiverEventStringPayloadIcall(EntityHandle, index);
            try
            {
                return new AnimationEvent(
                    name.ToString() ?? string.Empty,
                    stringPayload.ToString() ?? string.Empty,
                    ScriptBridge.GetAnimationEventReceiverEventFloatPayloadIcall(EntityHandle, index),
                    ScriptBridge.GetAnimationEventReceiverEventIntegerPayloadIcall(EntityHandle, index),
                    ScriptBridge.GetAnimationEventReceiverEventBooleanPayloadIcall(EntityHandle, index),
                    ScriptBridge.GetAnimationEventReceiverEventTimeSecondsIcall(EntityHandle, index),
                    ScriptBridge.GetAnimationEventReceiverEventNormalizedTimeIcall(EntityHandle, index));
            }
            finally
            {
                name.Dispose();
                stringPayload.Dispose();
            }
        }
    }

    public AnimationEvent[] GetEvents()
    {
        int count = EventCount;
        AnimationEvent[] events = new AnimationEvent[count];
        for (int index = 0; index < count; ++index)
            events[index] = GetEvent(index);
        return events;
    }
}
