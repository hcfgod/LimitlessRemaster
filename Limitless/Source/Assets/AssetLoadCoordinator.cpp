#include "Assets/AssetLoadCoordinator.h"

namespace Limitless::Assets
{
    std::atomic<uint64_t> AssetLoadCoordinator::s_Generation{1};
}

