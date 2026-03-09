using System;

namespace Limitless.Managed;

public sealed class DebugAssertException : Exception
{
    public DebugAssertException()
        : base("Assertion failed.")
    {
    }

    public DebugAssertException(string message)
        : base(message)
    {
    }

    public DebugAssertException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
