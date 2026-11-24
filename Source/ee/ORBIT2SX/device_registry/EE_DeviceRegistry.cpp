#include "EE_DeviceRegistry.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace ORBIT2SX
{
namespace EE
{
namespace DeviceRegistry
{

// ============================================================
// CONSTRUCTION
// ============================================================

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
      shutdownRequired(false),
      attachedRegionCount(0)
{
}

EE_DeviceRegistry::EE_DeviceRegistry(
    Bus::EE_MemoryBus& memoryBus)
    : m_memoryBus(memoryBus),
      m_entries(),
      m_startupOrder(),
      m_started(false),
      m_faulted(false),
      m_stopConfirmed(false),
      m_lastStatus(DeviceStatus::Ok)
{
}

EE_DeviceRegistry::~EE_DeviceRegistry() noexcept
{
    // Destruction is permitted only after an explicit successful Stop().
    //
    // Never attempt cleanup here:
    // - Stop() can fail.
    // - The destructor cannot return a DeviceResult.
    // - The bus may still contain pointers to registered devices.
    //
    // Terminate rather than silently destroy registry metadata while
    // external resources or bus attachments may still be alive.

    if(!m_stopConfirmed || HasPendingCleanup())
    {
        std::terminate();
    }
}

// ============================================================
// RESULT HELPERS
// ============================================================

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

DeviceResult EE_DeviceRegistry::Remember(DeviceResult result)
{
    m_lastStatus = result.status;
    return result;
}

// ============================================================
// BASIC VALIDATION
// ============================================================

bool EE_DeviceRegistry::IsValidDeviceId(DeviceId id)
{
    // ID zero is reserved as invalid.
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
    return std::find(
        values.begin(),
        values.end(),
        id) != values.end();
}

bool EE_DeviceRegistry::ContainsRegion(
    const std::vector<MemoryMap::RegionId>& values,
    MemoryMap::RegionId region)
{
    return std::find(
        values.begin(),
        values.end(),
        region) != values.end();
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

// ============================================================
// REGISTRATION VALIDATION
// ============================================================

bool EE_DeviceRegistry::ValidateRegistration(
    const DeviceRegistration& registration,
    DeviceResult& result) const
{
    // --------------------------------------------------------
    // Required fields
    // --------------------------------------------------------

    if(!IsValidDeviceId(registration.id) ||
       registration.name.empty() ||
       registration.device == nullptr)
    {
        result = MakeResult(
            DeviceStatus::InvalidArgument,
            registration.id);

        return false;
    }

    for(std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if(m_entries[i].device == registration.device)
        {
            result = MakeResult(
                DeviceStatus::DeviceAlreadyRegistered,
                registration.id);

            return false;
        }
    }
    
    // --------------------------------------------------------
    // Unique device ID
    // --------------------------------------------------------

    if(FindEntryIndex(registration.id) != m_entries.size())
    {
        result = MakeResult(
            DeviceStatus::DuplicateDeviceId,
            registration.id);

        return false;
    }

    // --------------------------------------------------------
    // Validate device-owned bus regions
    // --------------------------------------------------------

    for(std::size_t i = 0; i < registration.regions.size(); ++i)
    {
        const MemoryMap::RegionId region =
            registration.regions[i];

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
           descriptor->target !=
               MemoryMap::MappingTarget::DeviceOwned)
        {
            result = MakeResult(
                DeviceStatus::RegionNotDeviceOwned,
                registration.id);

            return false;
        }

        // Duplicate region in the same registration.
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

        // The bus must not already have an endpoint for this region.
        if(m_memoryBus.IsDeviceAttached(region))
        {
            result = MakeResult(
                DeviceStatus::RegionAlreadyAttached,
                registration.id);

            return false;
        }

        // A region can be owned by only one registry entry.
        for(std::size_t existing = 0;
            existing < m_entries.size();
            ++existing)
        {
            if(ContainsRegion(m_entries[existing].regions, region))
            {
                result = MakeResult(
                    DeviceStatus::RegionAlreadyRegistered,
                    registration.id);

                return false;
            }
        }
    }

    // --------------------------------------------------------
    // Validate dependency declarations
    // --------------------------------------------------------

    for(std::size_t i = 0;
        i < registration.dependencies.size();
        ++i)
    {
        const DeviceId dependency =
            registration.dependencies[i];

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

        // Missing dependencies are checked at Start().
        // This permits forward registration references.
    }

    result = MakeResult(DeviceStatus::Ok, registration.id);
    return true;
}

// ============================================================
// REGISTER DEVICE
// ============================================================

DeviceResult EE_DeviceRegistry::RegisterDevice(
    const DeviceRegistration& registration)
{
    if(m_started)
    {
        return Remember(
            MakeResult(
                DeviceStatus::RegistryAlreadyStarted,
                registration.id));
    }

    if(m_faulted)
    {
        return Remember(
            MakeResult(
                DeviceStatus::RegistryFaulted,
                registration.id));
    }

    DeviceResult validationResult =
        MakeResult(DeviceStatus::Ok);

    if(!ValidateRegistration(registration, validationResult))
    {
        return Remember(validationResult);
    }

    try
    {
        Entry entry;

        entry.id = registration.id;
        entry.name = registration.name;
        entry.device = registration.device;
        entry.regions = registration.regions;
        entry.dependencies = registration.dependencies;

        entry.state = DeviceState::Registered;
        entry.shutdownRequired = false;
        entry.attachedRegionCount = 0;

        m_entries.push_back(std::move(entry));
    }
    catch(...)
    {
        return Remember(
            MakeResult(
                DeviceStatus::InternalError,
                registration.id));
    }

    return Remember(
        MakeResult(DeviceStatus::Ok, registration.id));
}

// ============================================================
// UNREGISTER DEVICE
// ============================================================

DeviceResult EE_DeviceRegistry::UnregisterDevice(DeviceId id)
{
    if(m_started)
    {
        return Remember(
            MakeResult(
                DeviceStatus::RegistryAlreadyStarted,
                id));
    }

    if(m_faulted)
    {
        return Remember(
            MakeResult(
                DeviceStatus::RegistryFaulted,
                id));
    }

    const std::size_t index = FindEntryIndex(id);

    if(index == m_entries.size())
    {
        return Remember(
            MakeResult(DeviceStatus::DeviceNotFound, id));
    }

    const Entry& target = m_entries[index];

    // Never unregister an entry with pending lifecycle cleanup.
    if(target.shutdownRequired ||
       target.attachedRegionCount != 0)
    {
        return Remember(
            MakeResult(DeviceStatus::RegistryFaulted, id));
    }

    // A device cannot be removed while another device depends on it.
    for(std::size_t i = 0; i < m_entries.size(); ++i)
    {
        if(i == index)
        {
            continue;
        }

        if(ContainsId(m_entries[i].dependencies, id))
        {
            return Remember(
                MakeResult(DeviceStatus::DependencyInUse, id));
        }
    }

    m_entries.erase(m_entries.begin() + index);

    return Remember(
        MakeResult(DeviceStatus::Ok, id));
}

// ============================================================
// DEPENDENCY ORDER
// ============================================================

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

    // Validate that every dependency exists before scheduling.
    for(std::size_t i = 0; i < count; ++i)
    {
        const Entry& entry = m_entries[i];

        for(std::size_t d = 0;
            d < entry.dependencies.size();
            ++d)
        {
            const DeviceId dependency = entry.dependencies[d];

            if(FindEntryIndex(dependency) == count)
            {
                result = MakeResult(
                    DeviceStatus::MissingDependency,
                    entry.id);

                return false;
            }
        }
    }

    // Stable Kahn-style topological ordering.
    // Registration order is used as a deterministic tie-breaker.
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

            for(std::size_t d = 0;
                d < entry.dependencies.size();
                ++d)
            {
                const std::size_t dependencyIndex =
                    FindEntryIndex(entry.dependencies[d]);

                if(dependencyIndex == count ||
                   !scheduled[dependencyIndex])
                {
                    dependenciesReady = false;
                    break;
                }
            }

            if(!dependenciesReady)
            {
                continue;
            }

            scheduled[i] = true;
            order.push_back(i);
            progress = true;
        }

        if(!progress)
        {
            result = MakeResult(
                DeviceStatus::DependencyCycle);

            return false;
        }
    }

    result = MakeResult(DeviceStatus::Ok);
    return true;
}

// ============================================================
// INITIALIZE AND ATTACH
// ============================================================

DeviceResult EE_DeviceRegistry::InitializeAndAttach(Entry& entry)
{
    entry.state = DeviceState::Initializing;

    // Mark cleanup as required BEFORE calling device code.
    // Initialize() may fail after allocating partial resources.
    entry.shutdownRequired = true;

    Bus::DeviceStatus initializeStatus =
        Bus::DeviceStatus::InternalError;

    try
    {
        initializeStatus = entry.device->Initialize();
    }
    catch(...)
    {
        initializeStatus = Bus::DeviceStatus::InternalError;
    }

    if(initializeStatus != Bus::DeviceStatus::Ok)
    {
        entry.state = DeviceState::Faulted;

        DeviceResult result =
            MakeResult(
                DeviceStatus::InitializationFailed,
                entry.id);

        result.busDeviceStatus = initializeStatus;

        // RollbackStartup() will invoke Shutdown().
        return result;
    }

    entry.state = DeviceState::Initialized;

    // --------------------------------------------------------
    // Attach all device-owned regions
    // --------------------------------------------------------

    for(std::size_t i = 0; i < entry.regions.size(); ++i)
    {
        entry.state = DeviceState::Attaching;

        const Bus::BusResult busResult =
            m_memoryBus.AttachDevice(
                entry.regions[i],
                entry.device);

        if(busResult.status != Bus::BusStatus::Ok)
        {
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

// ============================================================
// DETACH DEVICE REGIONS
// ============================================================

DeviceResult EE_DeviceRegistry::DetachEntry(Entry& entry)
{
    while(entry.attachedRegionCount > 0)
    {
        const std::size_t index =
            entry.attachedRegionCount - 1;

        const Bus::BusResult busResult =
            m_memoryBus.DetachDevice(entry.regions[index]);

        if(busResult.status != Bus::BusStatus::Ok)
        {
            entry.state = DeviceState::Faulted;

            return MakeBusFailure(
                DeviceStatus::BusAttachmentFailed,
                entry.id,
                busResult);
        }

        --entry.attachedRegionCount;
    }

    return MakeResult(DeviceStatus::Ok, entry.id);
}

// ============================================================
// SHUTDOWN DEVICE
// ============================================================

DeviceResult EE_DeviceRegistry::ShutdownEntry(Entry& entry)
{
    if(!entry.shutdownRequired)
    {
        entry.state = DeviceState::Stopped;
        return MakeResult(DeviceStatus::Ok, entry.id);
    }

    // A device must never be shut down while its endpoint remains
    // attached to the memory bus.
    if(entry.attachedRegionCount != 0)
    {
        entry.state = DeviceState::Faulted;

        return MakeResult(
            DeviceStatus::BusAttachmentFailed,
            entry.id);
    }

    entry.state = DeviceState::Stopping;

    Bus::DeviceStatus shutdownStatus =
        Bus::DeviceStatus::InternalError;

    try
    {
        shutdownStatus = entry.device->Shutdown();
    }
    catch(...)
    {
        shutdownStatus = Bus::DeviceStatus::InternalError;
    }

    if(shutdownStatus != Bus::DeviceStatus::Ok)
    {
        entry.state = DeviceState::Faulted;

        DeviceResult result =
            MakeResult(
                DeviceStatus::ShutdownFailed,
                entry.id);

        result.busDeviceStatus = shutdownStatus;

        // Keep shutdownRequired=true so Stop() can retry.
        return result;
    }

    entry.shutdownRequired = false;
    entry.state = DeviceState::Stopped;

    return MakeResult(DeviceStatus::Ok, entry.id);
}

// ============================================================
// STOP ONE ENTRY
// ============================================================

DeviceResult EE_DeviceRegistry::StopEntry(Entry& entry)
{
    // Detach first. If detachment fails, do not shut down the
    // object because the bus may still hold its pointer.
    const DeviceResult detachResult = DetachEntry(entry);

    if(!detachResult.IsOk())
    {
        return detachResult;
    }

    return ShutdownEntry(entry);
}

// ============================================================
// STARTUP ROLLBACK
// ============================================================

DeviceResult EE_DeviceRegistry::StopEntriesInReverseOrder(
    const std::vector<std::size_t>& order)
{
    std::vector<bool> blocked;

    try
    {
        blocked.assign(m_entries.size(), false);
    }
    catch(...)
    {
        return MakeResult(DeviceStatus::InternalError);
    }

    DeviceResult firstFailure =
        MakeResult(DeviceStatus::Ok);

    // Stop dependents before their dependencies.
    for(std::size_t i = order.size(); i > 0; --i)
    {
        const std::size_t entryIndex = order[i - 1];

        if(entryIndex >= m_entries.size())
        {
            if(firstFailure.IsOk())
            {
                firstFailure =
                    MakeResult(DeviceStatus::InternalError);
            }

            continue;
        }

        Entry& entry = m_entries[entryIndex];

        // A dependent that could not stop still needs its
        // dependencies. Those dependencies must remain alive.
        if(blocked[entryIndex])
        {
            for(std::size_t d = 0;
                d < entry.dependencies.size();
                ++d)
            {
                const std::size_t dependencyIndex =
                    FindEntryIndex(entry.dependencies[d]);

                if(dependencyIndex < m_entries.size())
                {
                    blocked[dependencyIndex] = true;
                }
            }

            continue;
        }

        const DeviceResult result = StopEntry(entry);

        if(!result.IsOk())
        {
            if(firstFailure.IsOk())
            {
                firstFailure = result;
            }

            // Propagate the block to direct dependencies.
            // It propagates transitively when each blocked
            // dependency is encountered later in this loop.
            for(std::size_t d = 0;
                d < entry.dependencies.size();
                ++d)
            {
                const std::size_t dependencyIndex =
                    FindEntryIndex(entry.dependencies[d]);

                if(dependencyIndex < m_entries.size())
                {
                    blocked[dependencyIndex] = true;
                }
            }
        }
    }

    return firstFailure;
}

DeviceResult EE_DeviceRegistry::RollbackStartup(
    std::vector<std::size_t>& touchedEntries,
    DeviceResult originalFailure)
{
    const DeviceResult cleanupFailure =
        StopEntriesInReverseOrder(touchedEntries);

    m_started = false;

    if(!cleanupFailure.IsOk())
    {
        // Preserve the original dependency order so that a later
        // Stop() retry can safely resume cleanup.
        m_startupOrder.swap(touchedEntries);

        m_faulted = true;

        return cleanupFailure;
    }

    m_startupOrder.clear();
    m_faulted = false;

    return originalFailure;
}

// ============================================================
// START ALL DEVICES
// ============================================================

DeviceResult EE_DeviceRegistry::Start()
{
    if(m_started)
    {
        return Remember(
            MakeResult(DeviceStatus::RegistryAlreadyStarted));
    }

    if(m_faulted)
    {
        return Remember(
            MakeResult(DeviceStatus::RegistryFaulted));
    }

    std::vector<std::size_t> startupOrder;

    DeviceResult orderResult =
        MakeResult(DeviceStatus::Ok);

    if(!BuildStartupOrder(startupOrder, orderResult))
    {
        return Remember(orderResult);
    }

    std::vector<std::size_t> touchedEntries;

    try
    {
        // Reserve before invoking any device callback.
        // This prevents allocation failure during rollback tracking.
        touchedEntries.reserve(startupOrder.size());
    }
    catch(...)
    {
        return Remember(
            MakeResult(DeviceStatus::InternalError));
    }

    for(std::size_t i = 0; i < startupOrder.size(); ++i)
    {
        const std::size_t entryIndex = startupOrder[i];

        // Capacity was reserved above, so this push does not allocate.
        touchedEntries.push_back(entryIndex);

        Entry& entry = m_entries[entryIndex];

        const DeviceResult initializeResult =
            InitializeAndAttach(entry);

        if(!initializeResult.IsOk())
        {
            return Remember(
                RollbackStartup(
                    touchedEntries,
                    initializeResult));
        }
    }

    // Commit startup order only after every device succeeds.
    m_startupOrder.swap(startupOrder);

    for(std::size_t i = 0; i < m_startupOrder.size(); ++i)
    {
        Entry& entry = m_entries[m_startupOrder[i]];
        entry.state = DeviceState::Running;
    }

    m_started = true;
    m_faulted = false;

    return Remember(
        MakeResult(DeviceStatus::Ok));
}

// ============================================================
// RESET ALL DEVICES
// ============================================================

DeviceResult EE_DeviceRegistry::ResetAll()
{
    if(m_faulted)
    {
        return Remember(
            MakeResult(DeviceStatus::RegistryFaulted));
    }

    if(!m_started)
    {
        return Remember(
            MakeResult(DeviceStatus::RegistryNotStarted));
    }

    for(std::size_t i = 0; i < m_startupOrder.size(); ++i)
    {
        Entry& entry = m_entries[m_startupOrder[i]];

        entry.state = DeviceState::Resetting;

        Bus::DeviceStatus resetStatus =
            Bus::DeviceStatus::InternalError;

        try
        {
            resetStatus = entry.device->Reset();
        }
        catch(...)
        {
            resetStatus = Bus::DeviceStatus::InternalError;
        }

        if(resetStatus != Bus::DeviceStatus::Ok)
        {
            entry.state = DeviceState::Faulted;
            m_faulted = true;

            DeviceResult result =
                MakeResult(
                    DeviceStatus::ResetFailed,
                    entry.id);

            result.busDeviceStatus = resetStatus;

            return Remember(result);
        }

        entry.state = DeviceState::Running;
    }

    return Remember(
        MakeResult(DeviceStatus::Ok));
}

// ============================================================
// STOP ALL DEVICES
// ============================================================

DeviceResult EE_DeviceRegistry::Stop()
{
    const DeviceResult stopResult =
        StopEntriesInReverseOrder(m_startupOrder);

    m_started = false;

    if(!stopResult.IsOk())
    {
        // Keep startup order for a safe retry.
        m_faulted = true;
        return Remember(stopResult);
    }

    m_startupOrder.clear();
    m_faulted = false;

    return Remember(
        MakeResult(DeviceStatus::Ok));
}
    
// ============================================================
// INSPECTION API
// ============================================================

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
        return DeviceState::Unknown;
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

    // The returned pointer remains valid only until the registry
    // mutates its entry collection or the corresponding entry.
    return m_entries[index].name.c_str();
}

DeviceStatus EE_DeviceRegistry::GetLastStatus() const
{
    return m_lastStatus;
}

} // namespace DeviceRegistry
} // namespace EE
} // namespace ORBIT2SX
