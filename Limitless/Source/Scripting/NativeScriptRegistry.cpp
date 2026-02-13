#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace Limitless
{
    namespace
    {
        std::unordered_map<std::string, NativeScriptRegistry::ScriptFactory>& GetFactoryMap()
        {
            static std::unordered_map<std::string, NativeScriptRegistry::ScriptFactory> factories;
            return factories;
        }

        std::mutex& GetRegistryMutex()
        {
            static std::mutex mutex;
            return mutex;
        }
    }

    void NativeScriptRegistry::RegisterScript(const std::string& className, ScriptFactory factory)
    {
        if (className.empty() || !factory)
            return;

        std::lock_guard<std::mutex> lock(GetRegistryMutex());
        auto& factories = GetFactoryMap();
        factories[className] = std::move(factory);
    }

    void NativeScriptRegistry::RegisterScript(const std::string& className, NativeScriptCreateFunction createFunction)
    {
        if (!createFunction)
            return;

        RegisterScript(className, [createFunction]() {
            return std::unique_ptr<ScriptableEntity>(createFunction());
        });
    }

    bool NativeScriptRegistry::HasScript(const std::string& className)
    {
        std::lock_guard<std::mutex> lock(GetRegistryMutex());
        const auto& factories = GetFactoryMap();
        return factories.find(className) != factories.end();
    }

    std::unique_ptr<ScriptableEntity> NativeScriptRegistry::CreateScript(const std::string& className)
    {
        std::lock_guard<std::mutex> lock(GetRegistryMutex());
        const auto& factories = GetFactoryMap();
        const auto found = factories.find(className);
        if (found == factories.end())
            return nullptr;
        return found->second();
    }

    std::vector<std::string> NativeScriptRegistry::GetRegisteredScriptNames()
    {
        std::lock_guard<std::mutex> lock(GetRegistryMutex());
        const auto& factories = GetFactoryMap();
        std::vector<std::string> names;
        names.reserve(factories.size());
        for (const auto& [name, factory] : factories)
        {
            (void)factory;
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    void NativeScriptRegistry::Clear()
    {
        std::lock_guard<std::mutex> lock(GetRegistryMutex());
        GetFactoryMap().clear();
    }
}
