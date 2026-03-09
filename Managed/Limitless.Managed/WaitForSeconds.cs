namespace Limitless.Managed;

public readonly struct WaitForSeconds
{
    public WaitForSeconds(float seconds)
    {
        Seconds = seconds;
    }

    public float Seconds { get; }
}
