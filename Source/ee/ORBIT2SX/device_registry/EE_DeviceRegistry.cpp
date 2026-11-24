#include "EE_DeviceRegistry.h"

#include <algorithm>
#include <limits>

namespace ORBIT2SX
{
namespace EE
{
namespace DeviceRegistry
{

DeviceRegistration::DeviceRegistration()
    : id(0),
      name(),
      device(nullptr),
      regions(),
      dependencies()
{
}

EE_DeviceRegistry::Entry::Entry()
    : id(0),
      name(),
      device(nullptr),
      regions(),
      dependencies(),
      state(DeviceState::Registered),
      initialized(false),
      attachedRegionCount(0)
{
}

EE_DeviceRegistry::EE_DeviceRegistry(Bus::EE_MemoryBus& memoryBus)
    : m_memoryBus(memoryBus),
      m_entries(),
      m_startupOrder(),
      m_started(false),
      m_faulted(false),
      m_lastStatus(DeviceStatus::Ok)
{
}

EE_DeviceRegistry::~EE_DeviceRegistry()
{
    // Registered device objects must outlive this registry.
    // Stop() detaches endpoints before invoking device shutdown.
    if(m_started || m_faulted)
    {
        Stop();
    }
}

DeviceResult EE_DeviceRegistry::MakeResult(
    DeviceStatus status,
    DeviceId id)
{
    DeviceResult result;
    result.status = status;
    result.busStatus = Bus::BusStatus::Ok;
    result.busDeviceStatus = Bus::DeviceStatus::Ok;
    result.deviceId = id;
    return result;
}

DeviceResult EE_DeviceRegistry::MakeBusFailure(
    DeviceStatus status,
    DeviceId id,
    const Bus::BusResult& busResult)
{
    DeviceResult result;
    result.status = status;
    result.busStatus = busResult.status;
    result.busDeviceStatus = busResult.deviceStatus;
    result.deviceId = id;
    return result;
}

bool EE_DeviceRegistry::IsValidDeviceId(DeviceId id)
{
    return id != 0;
}

bool EE_DeviceRegistry::IsValidRegion(
    MemoryMap::RegionId region)
{
    return MemoryMap::EE_MemoryMap::FindRegion(region) != nullptr;
}

bool EE_DeviceRegistry::ContainsId(
    const std::vector<DeviceId>& values,
    DeviceId id)
{
    return std::find(values.begin(), values.end(), id) != values.end();
}

bool EE_DeviceRegistry::ContainsRegion(
    const std::vector<MemoryMap::RegionId>& values,
    MemoryMap::RegionId region)
{
    return std::find(values.begin(), values.end(), region) != values.end();
}

std::size_t EE_DeviceRegistry::FindEntryIndex(DeviceId id) const
{
    for(std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if(m_entries[i].id == id)
        {
            return i;
        }
    }

    return m_entries.size();
}

bool EE_DeviceRegistry::ValidateRegistration(
    const DeviceRegistration& registration,
    DeviceResult& result) const
{
    for(std::size_t i = 0; i < registration.dependencies.size(); ++i)
    {
        const DeviceId dependency = registration.dependencies[i];

        if(dependency == registration.id)
        {
            result = MakeResult(
                DeviceStatus::SelfDependency,
                registration.id);
            return false;
        }

        for(std::size_t previous = 0; previous < i; ++previous)
        {
            if(registration.dependencies[previous] == dependency)
            {
                result = MakeResult(
                    DeviceStatus::DuplicateDependency,
                    registration.id);
                return false;
            }
        }
    }

    if(FindEntryIndex(registration.id) != m_entries.size())
    {
        result = MakeResult(
            DeviceStatus::DuplicateDeviceId,
            registration.id);
        return false;
    }

    for(std::size_t i = 0; i < registration.regions.size(); ++i)
    {
        const MemoryMap::RegionId region = registration.regions[i];

        if(!IsValidRegion(region))
        {
            result = MakeResult(
                DeviceStatus::InvalidRegion,
                registration.id);
            return false;
        }

        const MemoryMap::RegionDescriptor* descriptor =
            MemoryMap::EE_MemoryMap::FindRegion(region);

        if(descriptor == nullptr ||
           descriptor->target != MemoryMap::MappingTarget::DeviceOwned)
        {
            result = MakeResult(
                DeviceStatus::RegionNotDeviceOwned,
                registration.id);
            return false;
        }

        for(std::size_t previous = 0; previous < i; ++previous)
        {
            if(registration.regions[previous] == region)
            {
                result = MakeResult(
                    DeviceStatus::RegionAlreadyRegistered,
                    registration.id);
                return false;
            }
        }

        if(m_memoryBus.IsDeviceAttached(region))
        {
            result = MakeResult(
                DeviceStatus::RegionAlreadyAttached,
                registration.id);
            return false;
        }

        for(std::size_t j = 0; j < m_entries.size(); ++j)
        {
            if(ContainsRegion(m_entries[j].regions, region))
            {
                result = MakeResult(
                    DeviceStatus::RegionAlreadyRegistered,
                    registration.id);
                return false;
            }
        }
    }

    for(std::size_t i = 0; i < registration.dependencies.size(); ++i)
    {
        const DeviceId dependency = registration.dependencies[i];

        if(dependency == registration.id)
        {
            result = MakeResult(
                DeviceStatus::SelfDependency,
                registration.id);
            return false;
        }

        if(ContainsId(
               std::vector<DeviceId>(
                   registration.dependencies.begin(),
                   registration.dependencies.begin() + i),
               dependency))
        {
            result = MakeResult(
                DeviceStatus::DuplicateDependency,
                registration.id);
            return false;
        }

        if(FindEntryIndex(dependency) == m_entries.size())
        {
            result = MakeResult(
                DeviceStatus::MissingDependency,
                registration.id);
            return false;
        }
    }

    result = MakeResult(DeviceStatus::Ok, registration.id);
    return true;
}

DeviceResult EE_DeviceRegistry::RegisterDevice(
    const DeviceRegistration& registration)
{
    if(m_started || m_faulted)
    {
        return MakeResult(
            DeviceStatus::RegistryAlreadyStarted,
            registration.id);
    }

    DeviceResult validationResult = MakeResult(DeviceStatus::Ok);

    if(!ValidateRegistration(registration, validationResult))
    {
        m_lastStatus = validationResult.status;
        return validationResult;
    }

    Entry entry;
    entry.id = registration.id;
    entry.name = registration.name;
    entry.device = registration.device;
    entry.regions = registration.regions;
    entry.dependencies = registration.dependencies;
    entry.state = DeviceState::Registered;

    try
    {
        m_entries.push_back(entry);
    }
    catch(...)
    {
        m_lastStatus = DeviceStatus::InternalError;
        return MakeResult(
            DeviceStatus::InternalError,
            registration.id);
    }

    m_lastStatus = DeviceStatus::Ok;
    return MakeResult(DeviceStatus::Ok, registration.id);
}

DeviceResult EE_DeviceRegistry::UnregisterDevice(DeviceId id)
{
    if(m_started || m_faulted)
    {
        return MakeResult(
            DeviceStatus::RegistryAlreadyStarted,
            id);
    }

    const std::size_t index = FindEntryIndex(id);

    if(index == m_entries.size())
    {
        return MakeResult(DeviceStatus::DeviceNotFound, id);
    }

    for(std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if(i == index)
        {
            continue;
        }

        if(ContainsId(m_entries[i].dependencies, id))
        {
            return MakeResult(DeviceStatus::DependencyInUse, id);
        }
    }

    m_entries.erase(m_entries.begin() + index);
    m_lastStatus = DeviceStatus::Ok;

    return MakeResult(DeviceStatus::Ok, id);
}

bool EE_DeviceRegistry::BuildStartupOrder(
    std::vector<std::size_t>& order,
    DeviceResult& result) const
{
    order.clear();

    const std::size_t count = m_entries.size();

    std::vector<bool> scheduled;

    try
    {
        scheduled.assign(count, false);
        order.reserve(count);
    }
    catch(...)
    {
        result = MakeResult(DeviceStatus::InternalError);
        return false;
    }

    while(order.size() < count)
    {
        bool progress = false;

        for(std::size_t i = 0; i < count; ++i)
        {
            if(scheduled[i])
            {
                continue;
            }

            const Entry& entry = m_entries[i];
            bool dependenciesReady = true;

            for(std::size_t d = 0; d < entry.dependencies.size(); ++d)
            {
                const std::size_t dependencyIndex =
                    FindEntryIndex(entry.dependencies[d]);

                if(dependencyIndex == count)
                {
                    result = MakeResult(
                        DeviceStatus::MissingDependency,
                        entry.id);
                    return false;
                }

                if(!scheduled[dependencyIndex])
                {
                    dependenciesReady = false;
                    break;
                }
            }

            if(dependenciesReady)
            {
                scheduled[i] = true;
                order.push_back(i);
                progress = true;
            }
        }

        if(!progress)
        {
            result = MakeResult(DeviceStatus::DependencyCycle);
            return false;
        }
    }

    result = MakeResult(DeviceStatus::Ok);
    return true;
}

DeviceResult EE_DeviceRegistry::InitializeAndAttach(Entry& entry)
{
    entry.state = DeviceState::Initializing;

    const Bus::DeviceStatus initializeStatus =
        entry.device->Initialize();

    if(initializeStatus != Bus::DeviceStatus::Ok)
    {
        // Initialization may have partially allocated resources.
        entry.device->Shutdown();

        entry.state = DeviceState::Faulted;

        DeviceResult result =
            MakeResult(DeviceStatus::InitializationFailed, entry.id);

        result.busDeviceStatus = initializeStatus;
        return result;
    }

    entry.initialized = true;
    entry.state = DeviceState::Initialized;

    for(std::size_t i = 0; i < entry.regions.size(); ++i)
    {
        const Bus::BusResult busResult =
            m_memoryBus.AttachDevice(
                entry.regions[i],
                entry.device);

        if(busResult.status != Bus::BusStatus::Ok)
        {
            const DeviceResult detachResult = DetachEntry(entry);

            // Never destroy or shut down a device while an endpoint
            // belonging to it may still be attached to the bus.
            if(detachResult.status != DeviceStatus::Ok)
            {
                entry.state = DeviceState::Faulted;
                return detachResult;
            }

            const DeviceResult shutdownResult = ShutdownEntry(entry);

            if(shutdownResult.status != DeviceStatus::Ok)
            {
                entry.state = DeviceState::Faulted;
                return shutdownResult;
            }

            entry.state = DeviceState::Faulted;

            return MakeBusFailure(
                DeviceStatus::BusAttachmentFailed,
                entry.id,
                busResult);
        }

        ++entry.attachedRegionCount;
    }

    entry.state = entry.regions.empty()
        ? DeviceState::Initialized
        : DeviceState::Attached;

    return MakeResult(DeviceStatus::Ok, entry.id);
}

DeviceResult EE_DeviceRegistry::RollbackStartup(
    const std::vector<std::size_t>& initializedEntries,
    DeviceResult originalFailure)
{
    DeviceResult cleanupFailure = MakeResult(DeviceStatus::Ok);

    for(std::size_t i = initializedEntries.size(); i > 0; --i)
    {
        Entry& entry = m_entries[initializedEntries[i - 1]];

        const DeviceResult result = StopEntry(entry);

        if(result.status != DeviceStatus::Ok &&
           cleanupFailure.status == DeviceStatus::Ok)
        {
            cleanupFailure = result;
        }
    }

    m_started = false;
    m_startupOrder.clear();

    if(cleanupFailure.status != DeviceStatus::Ok)
    {
        m_faulted = true;
        m_lastStatus = cleanupFailure.status;
        return cleanupFailure;
    }

    m_faulted = false;
    m_lastStatus = originalFailure.status;

    return originalFailure;
}

DeviceResult EE_DeviceRegistry::Start()
{
    if(m_started)
    {
        return MakeResult(DeviceStatus::RegistryAlreadyStarted);
    }

    if(m_faulted)
    {
        return MakeResult(DeviceStatus::RegistryFaulted);
    }

    std::vector<std::size_t> startupOrder;
    DeviceResult orderResult = MakeResult(DeviceStatus::Ok);

    if(!BuildStartupOrder(startupOrder, orderResult))
    {
        m_lastStatus = orderResult.status;
        return orderResult;
    }

    std::vector<std::size_t> initializedEntries;

    try
    {
        initializedEntries.reserve(startupOrder.size());
    }
    catch(...)
    {
        m_lastStatus = DeviceStatus::InternalError;
        return MakeResult(DeviceStatus::InternalError);
    }

    for(std::size_t i = 0; i < startupOrder.size(); ++i)
    {
        Entry& entry = m_entries[startupOrder[i]];

        const DeviceResult result = InitializeAndAttach(entry);

        if(result.status != DeviceStatus::Ok)
        {
            return RollbackStartup(initializedEntries, result);
        }

        initializedEntries.push_back(startupOrder[i]);
    }

    m_startupOrder = startupOrder;
    m_started = true;
    m_faulted = false;

    for(std::size_t i = 0; i < m_startupOrder.size(); ++i)
    {
        Entry& entry = m_entries[m_startupOrder[i]];
        entry.state = DeviceState::Running;
    }

    m_lastStatus = DeviceStatus::Ok;
    return MakeResult(DeviceStatus::Ok);
}

DeviceResult EE_DeviceRegistry::ResetAll()
{
    if(m_faulted)
    {
        return MakeResult(DeviceStatus::RegistryFaulted);
    }

    if(!m_started)
    {
        return MakeResult(DeviceStatus::RegistryNotStarted);
    }

    for(std::size_t i = 0; i < m_startupOrder.size(); ++i)
    {
        Entry& entry = m_entries[m_startupOrder[i]];

        const Bus::DeviceStatus status = entry.device->Reset();

        if(status != Bus::DeviceStatus::Ok)
        {
            entry.state = DeviceState::Faulted;
            m_faulted = true;
            m_lastStatus = DeviceStatus::ResetFailed;

            DeviceResult result =
                MakeResult(DeviceStatus::ResetFailed, entry.id);

            result.busDeviceStatus = status;
            return result;
        }
    }

    m_lastStatus = DeviceStatus::Ok;
    return MakeResult(DeviceStatus::Ok);
}

DeviceResult EE_DeviceRegistry::DetachEntry(Entry& entry)
{
    DeviceResult firstFailure = MakeResult(DeviceStatus::Ok, entry.id);

    while(entry.attachedRegionCount > 0)
    {
        const std::size_t index = entry.attachedRegionCount - 1;

        const Bus::BusResult result =
            m_memoryBus.DetachDevice(entry.regions[index]);

        if(result.status != Bus::BusStatus::Ok)
        {
            if(firstFailure.status == DeviceStatus::Ok)
            {
                firstFailure = MakeBusFailure(
                    DeviceStatus::BusAttachmentFailed,
                    entry.id,
                    result);
            }

            // Keep the count so a later Stop() can retry.
            break;
        }

        --entry.attachedRegionCount;
    }

    if(firstFailure.status != DeviceStatus::Ok)
    {
        entry.state = DeviceState::Faulted;
    }

    return firstFailure;
}

DeviceResult EE_DeviceRegistry::ShutdownEntry(Entry& entry)
{
    if(!entry.initialized)
    {
        entry.state = DeviceState::Stopped;
        return MakeResult(DeviceStatus::Ok, entry.id);
    }

    entry.state = DeviceState::Stopping;

    const Bus::DeviceStatus status = entry.device->Shutdown();

    if(status != Bus::DeviceStatus::Ok)
    {
        entry.state = DeviceState::Faulted;

        DeviceResult result =
            MakeResult(DeviceStatus::ShutdownFailed, entry.id);

        result.busDeviceStatus = status;
        return result;
    }

    entry.initialized = false;
    entry.state = DeviceState::Stopped;

    return MakeResult(DeviceStatus::Ok, entry.id);
}

DeviceResult EE_DeviceRegistry::StopEntry(Entry& entry)
{
    const DeviceResult detachResult = DetachEntry(entry);

    if(detachResult.status != DeviceStatus::Ok)
    {
        return detachResult;
    }

    return ShutdownEntry(entry);
}

DeviceResult EE_DeviceRegistry::Stop()
{
    DeviceResult firstFailure = MakeResult(DeviceStatus::Ok);

    // Stop in reverse startup order so dependents stop before dependencies.
    for(std::size_t i = m_startupOrder.size(); i > 0; --i)
    {
        Entry& entry = m_entries[m_startupOrder[i - 1]];

        const DeviceResult result = StopEntry(entry);

        if(result.status != DeviceStatus::Ok &&
           firstFailure.status == DeviceStatus::Ok)
        {
            firstFailure = result;
        }
    }

    // Also clean up any entry left partially initialized after a failed start.
    for(std::size_t i = 0; i < m_entries.size(); ++i)
    {
        Entry& entry = m_entries[i];

        if(entry.initialized || entry.attachedRegionCount > 0)
        {
            const DeviceResult result = StopEntry(entry);

            if(result.status != DeviceStatus::Ok &&
               firstFailure.status == DeviceStatus::Ok)
            {
                firstFailure = result;
            }
        }
    }

    m_started = false;
    m_startupOrder.clear();

    m_faulted = firstFailure.status != DeviceStatus::Ok;
    m_lastStatus = firstFailure.status;

    if(m_faulted)
    {
        return firstFailure;
    }

    return MakeResult(DeviceStatus::Ok);
}

bool EE_DeviceRegistry::IsStarted() const
{
    return m_started;
}

bool EE_DeviceRegistry::IsFaulted() const
{
    return m_faulted;
}

std::size_t EE_DeviceRegistry::GetDeviceCount() const
{
    return m_entries.size();
}

bool EE_DeviceRegistry::ContainsDevice(DeviceId id) const
{
    return FindEntryIndex(id) != m_entries.size();
}

DeviceState EE_DeviceRegistry::GetDeviceState(DeviceId id) const
{
    const std::size_t index = FindEntryIndex(id);

    if(index == m_entries.size())
    {
        return DeviceState::Faulted;
    }

    return m_entries[index].state;
}

const char* EE_DeviceRegistry::GetDeviceName(DeviceId id) const
{
    const std::size_t index = FindEntryIndex(id);

    if(index == m_entries.size())
    {
        return nullptr;
    }

    return m_entries[index].name.c_str();
}

DeviceStatus EE_DeviceRegistry::GetLastStatus() const
{
    return m_lastStatus;
}

} // namespace DeviceRegistry
} // namespace EE
} // namespace ORBIT2SX
