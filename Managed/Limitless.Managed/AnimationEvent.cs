namespace Limitless.Managed;

public readonly struct AnimationEvent
{
    public string Name { get; }
    public string StringPayload { get; }
    public float FloatPayload { get; }
    public int IntegerPayload { get; }
    public bool BooleanPayload { get; }
    public float TimeSeconds { get; }
    public float NormalizedTime { get; }

    public AnimationEvent(
        string name,
        string stringPayload,
        float floatPayload,
        int integerPayload,
        bool booleanPayload,
        float timeSeconds,
        float normalizedTime)
    {
        Name = name;
        StringPayload = stringPayload;
        FloatPayload = floatPayload;
        IntegerPayload = integerPayload;
        BooleanPayload = booleanPayload;
        TimeSeconds = timeSeconds;
        NormalizedTime = normalizedTime;
    }
}
