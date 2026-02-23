#pragma once

#include "Scripting/ScriptableEntity.h"
#include "Scripting/ScriptCoreApi.h"

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Limitless
{
    class NativeScriptRegistry
    {
    public:
        using ScriptFactory = std::function<std::unique_ptr<ScriptableEntity>()>;

        static void RegisterScript(const std::string& className, ScriptFactory factory);
        static void RegisterScript(const std::string& className, NativeScriptCreateFunction createFunction);

        template<typename ScriptType>
        static void RegisterScript(const std::string& className)
        {
            static_assert(std::is_base_of_v<ScriptableEntity, ScriptType>, "ScriptType must derive from ScriptableEntity");
            RegisterScript(className, []() {
                return std::make_unique<ScriptType>();
            });
        }

        static bool HasScript(const std::string& className);
        static std::unique_ptr<ScriptableEntity> CreateScript(const std::string& className);
        static std::vector<std::string> GetRegisteredScriptNames();
        static void SetExecutionBlocked(bool blocked);
        static bool IsExecutionBlocked();
        static void Clear();
    };
}
