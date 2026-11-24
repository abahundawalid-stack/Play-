#pragma once

#include "../memory_bus/EE_MemoryBus.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ORBIT2SX
{
namespace EE
{
namespace DeviceRegistry
{

using DeviceId = std::uint32_t;

enum class DeviceStatus : std::uint8_t
{
    Ok = 0,
    InvalidArgument,
    DuplicateDeviceId,
    DeviceNotFound,
    DeviceAlreadyRegistered,
    RegistryAlreadyStarted,
    RegistryNotStarted,
    RegistryFaulted,
    InvalidRegion,
    RegionNotDeviceOwned,
    RegionAlreadyRegistered,
    RegionAlreadyAttached,
    MissingDependency,
    DuplicateDependency,
    SelfDependency,
    DependencyInUse,
    DependencyCycle,
    InitializationFailed,
    ResetFailed,
    ShutdownFailed,
    BusAttachmentFailed,
    InternalError
};

enum class DeviceState : std::uint8_t
{
    Registered = 0,
    Initializing,
    Initialized,
    Attached,
    Running,
    Stopping,
    Stopped,
    Faulted
};

struct DeviceResult
{
    DeviceStatus status;
    Bus::BusStatus busStatus;
    Bus::DeviceStatus busDeviceStatus;
    DeviceId deviceId;
};

struct DeviceRegistration
{
    DeviceId id;
    std::string name;

    // Non-owning pointer. The device object must outlive this registry entry.
    class IEEDevice* device;

    // Regions exposed by this device through EE_MemoryBus.
    std::vector<MemoryMap::RegionId> regions;

    // Dependencies must be registered before Start().
    std::vector<DeviceId> dependencies;

    DeviceRegistration();
};

class IEEDevice : public Bus::IEEBusDevice
{
public:
    virtual ~IEEDevice() {}

    // Called once when the registry starts.
    virtual Bus::DeviceStatus Initialize() = 0;

    // Called by ResetAll() in dependency order.
    virtual Bus::DeviceStatus Reset() = 0;

    // Called during rollback or registry shutdown.
    virtual Bus::DeviceStatus Shutdown() = 0;
};

class EE_DeviceRegistry final
{
public:
    explicit EE_DeviceRegistry(Bus::EE_MemoryBus& memoryBus);

    ~EE_DeviceRegistry();

    EE_DeviceRegistry(const EE_DeviceRegistry&) = delete;
    EE_DeviceRegistry& operator=(const EE_DeviceRegistry&) = delete;

    // Registration is allowed only while the registry is stopped.
    DeviceResult RegisterDevice(const DeviceRegistration& registration);

    // Removal is allowed only while the registry is stopped.
    DeviceResult UnregisterDevice(DeviceId id);

    // Initializes devices, attaches bus endpoints and enters Running state.
    // Startup follows dependency order.
    DeviceResult Start();

    // Resets all devices in dependency order.
    DeviceResult ResetAll();

    // Stops devices in reverse dependency order.
    // All devices are processed even if one shutdown operation fails.
    DeviceResult Stop();

    bool IsStarted() const;
    bool IsFaulted() const;

    std::size_t GetDeviceCount() const;

    bool ContainsDevice(DeviceId id) const;

    DeviceState GetDeviceState(DeviceId id) const;

    const char* GetDeviceName(DeviceId id) const;

    DeviceStatus GetLastStatus() const;

private:
    struct Entry
    {
        DeviceId id;
        std::string name;
        IEEDevice* device;
        std::vector<MemoryMap::RegionId> regions;
        std::vector<DeviceId> dependencies;

        DeviceState state;
        bool initialized;
        std::size_t attachedRegionCount;

        Entry();
    };

    static DeviceResult MakeResult(
        DeviceStatus status,
        DeviceId id = 0);

    static DeviceResult MakeBusFailure(
        DeviceStatus status,
        DeviceId id,
        const Bus::BusResult& busResult);

    static bool IsValidDeviceId(DeviceId id);

    static bool IsValidRegion(MemoryMap::RegionId region);

    static bool ContainsId(
        const std::vector<DeviceId>& values,
        DeviceId id);

    static bool ContainsRegion(
        const std::vector<MemoryMap::RegionId>& values,
        MemoryMap::RegionId region);

    std::size_t FindEntryIndex(DeviceId id) const;

    bool ValidateRegistration(
        const DeviceRegistration& registration,
        DeviceResult& result) const;

    bool BuildStartupOrder(
        std::vector<std::size_t>& order,
        DeviceResult& result) const;

    DeviceResult InitializeAndAttach(Entry& entry);

    DeviceResult RollbackStartup(
        const std::vector<std::size_t>& initializedEntries,
        DeviceResult originalFailure);

    DeviceResult StopEntry(Entry& entry);

    DeviceResult DetachEntry(Entry& entry);

    DeviceResult ShutdownEntry(Entry& entry);

    Bus::EE_MemoryBus& m_memoryBus;

    std::vector<Entry> m_entries;
    std::vector<std::size_t> m_startupOrder;

    bool m_started;
    bool m_faulted;

    DeviceStatus m_lastStatus;
};

} // namespace DeviceRegistry
} // namespace EE
} // namespace ORBIT2SX
