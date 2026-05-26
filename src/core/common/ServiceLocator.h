#ifndef SERVICELOCATOR_H
#define SERVICELOCATOR_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVariant>
#include <memory>

/**
 * Lightweight service locator for dependency management.
 * Replaces manual new/delete wiring in main.cpp.
 *
 * Usage:
 *   ServiceLocator locator;
 *   locator.registerService<ShareManager>(&ShareManager::instance());
 *   auto* sm = locator.service<ShareManager>();
 */
class ServiceLocator
{
public:
    ServiceLocator() = default;
    ~ServiceLocator() = default;

    ServiceLocator(const ServiceLocator&) = delete;
    ServiceLocator& operator=(const ServiceLocator&) = delete;

    /**
     * Register a service instance by type.
     * T should be the interface/base type.
     */
    template<typename T>
    void registerService(T* instance)
    {
        const char* key = typeid(T).name();
        m_services.insert(key, reinterpret_cast<void*>(instance));
    }

    /**
     * Retrieve a service instance by type.
     * Returns nullptr if not registered.
     */
    template<typename T>
    T* service() const
    {
        const char* key = typeid(T).name();
        auto it = m_services.constFind(key);
        if (it != m_services.constEnd()) {
            return reinterpret_cast<T*>(it.value());
        }
        return nullptr;
    }

    /**
     * Check if a service type is registered.
     */
    template<typename T>
    bool hasService() const
    {
        return m_services.contains(typeid(T).name());
    }

    /**
     * Unregister a service by type.
     */
    template<typename T>
    void unregisterService()
    {
        m_services.remove(typeid(T).name());
    }

    /**
     * Clear all registered services.
     */
    void clear()
    {
        m_services.clear();
    }

private:
    QMap<QByteArray, void*> m_services;
};

#endif // SERVICELOCATOR_H
