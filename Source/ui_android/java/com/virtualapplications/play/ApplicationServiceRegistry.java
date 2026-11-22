package com.virtualapplications.play;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;

/**
 * Application-scoped typed service registry.
 * The registry does not assume ownership of registered service instances.
 */
public final class ApplicationServiceRegistry
{
    private final ConcurrentMap<Class<?>, Object> services =
        new ConcurrentHashMap<>();

    public <T> void register(Class<T> serviceType, T service)
    {
        if(serviceType == null || service == null)
            throw new IllegalArgumentException(
                "Service type and instance must not be null.");

        if(!serviceType.isInstance(service))
            throw new IllegalArgumentException(
                "Service instance does not match its service type.");

        Object previous = services.putIfAbsent(serviceType, service);
        if(previous != null && previous != service)
            throw new IllegalStateException(
                "A different service is already registered for " +
                serviceType.getName());
    }

    /** Registers an application component in the same typed registry. */
    public <T> void registerComponent(Class<T> componentType, T component)
    {
        register(componentType, component);
    }

    public <T> T getComponent(Class<T> componentType)
    {
        return get(componentType);
    }

    public <T> T get(Class<T> serviceType)
    {
        T service = getOrNull(serviceType);
        if(service == null)
            throw new IllegalStateException(
                "No service registered for " + serviceType.getName());
        return service;
    }

    public <T> T getOrNull(Class<T> serviceType)
    {
        if(serviceType == null)
            throw new IllegalArgumentException(
                "Service type must not be null.");

        Object service = services.get(serviceType);
        return service == null ? null : serviceType.cast(service);
    }

    public boolean contains(Class<?> serviceType)
    {
        return serviceType != null && services.containsKey(serviceType);
    }

    void clear()
    {
        services.clear();
    }
}
