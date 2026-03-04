#pragma once

#include "Error.h"

#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace Limitless
{
    class ServiceRegistry
    {
    public:
        ServiceRegistry() = default;
        ~ServiceRegistry() = default;

        ServiceRegistry(const ServiceRegistry&) = delete;
        ServiceRegistry& operator=(const ServiceRegistry&) = delete;

        template<typename T>
        void Register(T& service)
        {
            std::lock_guard lock(m_Mutex);
            m_Services[std::type_index(typeid(T))] = &service;
        }

        template<typename T>
        void Unregister()
        {
            std::lock_guard lock(m_Mutex);
            m_Services.erase(std::type_index(typeid(T)));
        }

        template<typename T>
        T& Get()
        {
            std::lock_guard lock(m_Mutex);
            auto it = m_Services.find(std::type_index(typeid(T)));
            LT_VERIFY(it != m_Services.end(), "ServiceRegistry::Get<> -- service not registered");
            return *static_cast<T*>(it->second);
        }

        template<typename T>
        T* TryGet()
        {
            std::lock_guard lock(m_Mutex);
            auto it = m_Services.find(std::type_index(typeid(T)));
            return (it != m_Services.end()) ? static_cast<T*>(it->second) : nullptr;
        }

        template<typename T>
        bool Has() const
        {
            std::lock_guard lock(m_Mutex);
            return m_Services.count(std::type_index(typeid(T))) > 0;
        }

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<std::type_index, void*> m_Services;
    };

    ServiceRegistry& GetServices();
    void SetGlobalServiceRegistry(ServiceRegistry* registry);
}
