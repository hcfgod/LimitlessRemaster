using System;
using System.Globalization;
using System.Text;
using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class DebugApi
{
    private static readonly DebugApi s_Shared = new();

    public static DebugApi Shared => s_Shared;

    private DebugApi()
    {
    }

    public void Log(string? message)
    {
        unsafe
        {
            WriteLog(message, ScriptBridge.LogInfoIcall);
        }
    }

    public void Log(object? value)
    {
        Log(FormatArgument(value));
    }

    public void Log(string? message, params object?[] args)
    {
        unsafe
        {
            WriteLog(FormatMessage(message, args), ScriptBridge.LogInfoIcall);
        }
    }

    public void LogFormat(string? format, params object?[] args)
    {
        Log(format, args);
    }

    public void LogWarning(string? message)
    {
        unsafe
        {
            WriteLog(message, ScriptBridge.LogWarningIcall);
        }
    }

    public void LogWarning(object? value)
    {
        LogWarning(FormatArgument(value));
    }

    public void LogWarning(string? message, params object?[] args)
    {
        unsafe
        {
            WriteLog(FormatMessage(message, args), ScriptBridge.LogWarningIcall);
        }
    }

    public void LogWarningFormat(string? format, params object?[] args)
    {
        LogWarning(format, args);
    }

    public void LogError(string? message)
    {
        unsafe
        {
            WriteLog(message, ScriptBridge.LogErrorIcall);
        }
    }

    public void LogError(object? value)
    {
        LogError(FormatArgument(value));
    }

    public void LogError(string? message, params object?[] args)
    {
        unsafe
        {
            WriteLog(FormatMessage(message, args), ScriptBridge.LogErrorIcall);
        }
    }

    public void LogErrorFormat(string? format, params object?[] args)
    {
        LogError(format, args);
    }

    public void LogException(Exception? exception)
    {
        if (exception is null)
        {
            LogError("Exception: null");
            return;
        }

        LogError(exception.ToString());
    }

    public void Assert(bool condition)
    {
        if (condition)
            return;

        ThrowAssertionFailure("Assertion failed.");
    }

    public void Assert(bool condition, string? message)
    {
        if (condition)
            return;

        ThrowAssertionFailure(BuildAssertMessage(message));
    }

    public void Assert(bool condition, string? message, params object?[] args)
    {
        if (condition)
            return;

        ThrowAssertionFailure(BuildAssertMessage(FormatMessage(message, args)));
    }

    public void AssertFormat(bool condition, string? message, params object?[] args)
    {
        if (condition)
            return;

        ThrowAssertionFailure(BuildAssertMessage(FormatMessage(message, args)));
    }

    private static string FormatMessage(string? message, object?[]? args)
    {
        string safeMessage = message ?? string.Empty;
        if (args == null || args.Length == 0)
            return safeMessage;

        if (safeMessage.Contains("{}", StringComparison.Ordinal))
            return ReplaceAnonymousPlaceholders(safeMessage, args);

        try
        {
            return string.Format(CultureInfo.InvariantCulture, safeMessage, args);
        }
        catch (FormatException)
        {
            return AppendArguments(safeMessage, args, 0);
        }
    }

    private static string ReplaceAnonymousPlaceholders(string message, object?[] args)
    {
        StringBuilder builder = new();
        int startIndex = 0;
        int argumentIndex = 0;

        while (true)
        {
            int placeholderIndex = message.IndexOf("{}", startIndex, StringComparison.Ordinal);
            if (placeholderIndex < 0)
                break;

            builder.Append(message, startIndex, placeholderIndex - startIndex);
            if (argumentIndex < args.Length)
                builder.Append(FormatArgument(args[argumentIndex++]));
            else
                builder.Append("{}");

            startIndex = placeholderIndex + 2;
        }

        builder.Append(message, startIndex, message.Length - startIndex);
        return AppendArguments(builder.ToString(), args, argumentIndex);
    }

    private static string AppendArguments(string message, object?[] args, int startIndex)
    {
        if (startIndex >= args.Length)
            return message;

        StringBuilder builder = new(message);
        if (builder.Length > 0 && !char.IsWhiteSpace(builder[^1]))
            builder.Append(' ');

        for (int argumentIndex = startIndex; argumentIndex < args.Length; argumentIndex++)
        {
            if (argumentIndex > startIndex)
                builder.Append(' ');

            builder.Append(FormatArgument(args[argumentIndex]));
        }

        return builder.ToString();
    }

    private static string BuildAssertMessage(string? message)
    {
        if (string.IsNullOrWhiteSpace(message))
            return "Assertion failed.";

        return $"Assertion failed: {message}";
    }

    private static void ThrowAssertionFailure(string message)
    {
        throw new DebugAssertException(message);
    }

    private static string FormatArgument(object? value)
    {
        if (value is null)
            return "null";

        if (value is IFormattable formattable)
            return formattable.ToString(null, CultureInfo.InvariantCulture) ?? string.Empty;

        return value.ToString() ?? string.Empty;
    }

    private static unsafe void WriteLog(string? message, delegate*<NativeString, void> internalCall)
    {
        NativeString nativeMessage = message ?? string.Empty;
        try
        {
            internalCall(nativeMessage);
        }
        finally
        {
            nativeMessage.Dispose();
        }
    }
}
