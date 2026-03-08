namespace Limitless.Managed;

public struct RaycastHit2D
{
    internal int HasHitValue;
    internal uint HitEntityHandle;
    public Vector2 Point;
    public Vector2 Normal;
    public float Fraction;

    public bool HasHit => HasHitValue != 0;
    public uint EntityHandle => HitEntityHandle;
    public Entity Entity => new(HitEntityHandle);

    public override string ToString()
    {
        return HasHit
            ? $"Hit(Entity={HitEntityHandle}, Point={Point}, Normal={Normal}, Fraction={Fraction})"
            : "NoHit";
    }
}
