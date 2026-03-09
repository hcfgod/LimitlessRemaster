namespace Limitless.Managed;

public sealed class Coroutine
{
    internal Coroutine(ulong identifier)
    {
        Identifier = identifier;
    }

    public ulong Identifier { get; }

    public bool IsValid => Identifier != 0;

    public override string ToString()
    {
        return IsValid ? $"Coroutine({Identifier})" : "Coroutine(Invalid)";
    }
}
