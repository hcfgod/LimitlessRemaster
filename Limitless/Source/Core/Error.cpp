#include "Error.h"
#include <spdlog/fmt/fmt.h>
#include <sstream>
#include <iostream>

namespace Limitless
{
    Error::Error(ErrorCode code, const std::string& message, const std::source_location& location)
        : m_Code(code), m_Message(message)
    {
        // Format location string
        std::ostringstream oss;
        oss << location.file_name() << ":" << location.line() << ":" << location.column() 
            << " in " << location.function_name();
        m_Location = oss.str();
    }

    const char* Error::what() const noexcept
    {
        if (m_WhatBuffer.empty())
        {
            m_WhatBuffer = ToString();
        }
        return m_WhatBuffer.c_str();
    }

    std::string Error::ToString() const
    {
        std::ostringstream oss;
        oss << "Error " << static_cast<int>(m_Code) << ": " << m_Message;
        if (!m_Location.empty())
        {
            oss << " at " << m_Location;
        }
        return oss.str();
    }

    namespace ErrorHandling
    {
        static ErrorHandler s_ErrorHandler = DefaultErrorHandler;

        void SetErrorHandler(ErrorHandler handler)
        {
            s_ErrorHandler = handler ? handler : DefaultErrorHandler;
        }

        ErrorHandler GetErrorHandler()
        {
            return s_ErrorHandler;
        }

        void DefaultErrorHandler(const Error& error)
        {
            // Fallback to cerr if logger is not available
            std::cerr << "ERROR: " << error.ToString() << std::endl;
        }

        void Assert(bool condition, const std::string& message, const std::source_location& location)
        {
            if (!condition)
            {
                Error error(ErrorCode::Unknown, "Assertion failed: " + message, location);
                s_ErrorHandler(error);
                throw error;
            }
        }
    }
}