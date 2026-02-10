#pragma once

// -----------------------------------------------------------------------------
// PrecompiledHeader.h
//
// Engine-wide precompiled header for faster iteration builds.
//
// Rules:
// - Only include stable, widely-used headers here.
// - Do NOT include third-party implementation headers or platform-specific headers
//   that frequently change.
// - Vendor and C translation units must not rely on this header (they are built
//   with PCH disabled in Premake).
// -----------------------------------------------------------------------------

// Standard library
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// Common third-party headers used broadly across the engine interface surface.
// Keep these to headers that are stable and frequently used.
#include <glm/glm.hpp>

