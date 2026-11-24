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

// ------------------------------------------------------------
// DEVICE LIFECYCLE INTERFACE
// ------------------------------------------------------------

class IEEDevice : public Bus::IEEBusDevice
{
public:
    virtual ~IEEDevice() {}

    // May allocate internal resources.
    virtual Bus::DeviceStatus Initialize() = 0;

    // Restores the device to its initial operational state.
    virtual Bus::DeviceStatus Reset() = 0;

    // Releases resources acquired during Initialize().
    // Must be safe after a partially failed Initialize().
    // If this function returns failure, it must also be safe to call
    // Shutdown() again: implementations must tolerate partially
    // released resources and must not double-free them on retry.
    virtual Bus::DeviceStatus Shutdown() = 0;
};

// ------------------------------------------------------------
// REGISTRY STATUS
// ------------------------------------------------------------

enum class DeviceStatus : std::uint8_t
{
    Ok = 0,

    InvalidArgument,
    DuplicateDeviceId,
    DeviceAlreadyRegistered,
    DeviceNotFound,
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
    Unknown = 0,
    Registered,
    Initializing,
    Initialized,
    Attaching,
    Attached,
    Running,
    Resetting,
    Stopping,
    Stopped,
    Faulted
};

// ------------------------------------------------------------
// RESULT
// ------------------------------------------------------------

struct DeviceResult
{
    DeviceStatus status;
    Bus::BusStatus busStatus;
    Bus::DeviceStatus busDeviceStatus;
    DeviceId deviceId;

    bool IsOk() const
    {
        return status == DeviceStatus::Ok;
    }
};

// ------------------------------------------------------------
// REGISTRATION DESCRIPTION
// ------------------------------------------------------------

struct DeviceRegistration
{
    DeviceId id;
    std::string name;

    // Non-owning pointer. The subsystem/composition root that creates
    // the device owns it and must keep it alive until the registry has
    // successfully stopped and detached every region for that device.
    IEEDevice* device;

    // Bus regions owned by this device.
    // Empty is allowed for devices without direct EE bus regions.
    std::vector<MemoryMap::RegionId> regions;

    // Dependencies must exist when Start() is called.
    // Registration order itself is not significant.
    std::vector<DeviceId> dependencies;

    DeviceRegistration();
};

// ------------------------------------------------------------
// EE DEVICE REGISTRY
// ------------------------------------------------------------

// ------------------------------------------------------------
// THREAD SAFETY CONTRACT
// ------------------------------------------------------------
//
// This class is NOT thread-safe.
// No internal mutex is used.
//
// The owner must externally serialize every call to:
// - RegisterDevice()
// - UnregisterDevice()
// - Start()
// - ResetAll()
// - Stop()
// - All inspection methods.
//
// Access to the associated EE_MemoryBus that can attach, detach,
// inspect, or dispatch through device endpoints must use the SAME
// external serialization policy.
//
// Do not race bus access against registry lifecycle operations.
//
// Device callbacks (Initialize, Reset, Shutdown, Read, Write) must
// not recursively call registry mutation/lifecycle methods.
//
// In particular, Initialize(), Reset(), and Shutdown() must not call:
// - RegisterDevice()
// - UnregisterDevice()
// - Start()
// - ResetAll()
// - Stop()
//
// The registry does not lock around callbacks. Violating this
// contract can cause data races, iterator invalidation, or
// inconsistent device lifecycle state.
//
// ------------------------------------------------------------
// OWNERSHIP AND DESTRUCTION CONTRACT
// ------------------------------------------------------------
//
// The registry owns Entry metadata only.
// It does NOT own registered device objects or EE_MemoryBus.
//
// The composition root/subsystem creates and owns every IEEDevice.
//
// EE_MemoryBus and every registered device must outlive this registry.
//
// Call Stop() explicitly and check its DeviceResult before destroying:
// - The registry
// - Any registered device
// - The EE_MemoryBus
//
// If Stop() fails, keep the registry, bus, and every registered
// device alive. Correct the underlying failure and call Stop() again.
//
// Destroying the registry without a successful explicit Stop(), or
// while lifecycle cleanup remains pending, is a fatal programming
// error. The destructor calls std::terminate().
//
// The destructor does NOT perform best-effort cleanup and cannot
// report cleanup failures.

class EE_DeviceRegistry final
{
public:
    explicit EE_DeviceRegistry(Bus::EE_MemoryBus& memoryBus);

    // A successful explicit Stop() is required before destruction.
    // Destruction with pending cleanup or without Stop confirmation
    // terminates the process.
    ~EE_DeviceRegistry() noexcept;

    EE_DeviceRegistry(const EE_DeviceRegistry&) = delete;
    EE_DeviceRegistry& operator=(const EE_DeviceRegistry&) = delete;

    // --------------------------------------------------------
    // REGISTRATION
    // --------------------------------------------------------

    DeviceResult RegisterDevice(
        const DeviceRegistration& registration);

    DeviceResult UnregisterDevice(DeviceId id);

    // --------------------------------------------------------
    // LIFECYCLE
    // --------------------------------------------------------

    // Initializes and attaches all devices in dependency order.
    DeviceResult Start();

    // Resets all running devices in dependency order.
    DeviceResult ResetAll();

    // Detaches and shuts down devices in reverse dependency order.
    // Can also retry cleanup after a previous failure.
    DeviceResult Stop();

    // --------------------------------------------------------
    // INSPECTION
    // --------------------------------------------------------

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

        // Set before Initialize() is called.
        // Remains true until Shutdown() succeeds.
        // This also tracks partially initialized devices.
        bool shutdownRequired;

        // Number of successfully attached regions.
        // Attachments are always made in registration order.
        std::size_t attachedRegionCount;

        Entry();
    };

    // --------------------------------------------------------
    // RESULT HELPERS
    // --------------------------------------------------------

    static DeviceResult MakeResult(
        DeviceStatus status,
        DeviceId id = 0);

    static DeviceResult MakeBusFailure(
        DeviceStatus status,
        DeviceId id,
        const Bus::BusResult& busResult);

    DeviceResult Remember(DeviceResult result);

    // --------------------------------------------------------
    // VALIDATION
    // --------------------------------------------------------

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

    // --------------------------------------------------------
    // DEPENDENCY RESOLUTION
    // --------------------------------------------------------

    bool BuildStartupOrder(
        std::vector<std::size_t>& order,
        DeviceResult& result) const;

    // --------------------------------------------------------
    // DEVICE OPERATIONS
    // --------------------------------------------------------

    DeviceResult InitializeAndAttach(Entry& entry);

    DeviceResult DetachEntry(Entry& entry);

    DeviceResult ShutdownEntry(Entry& entry);

    DeviceResult StopEntry(Entry& entry);

    DeviceResult StopEntriesInReverseOrder(
        const std::vector<std::size_t>& order);

    DeviceResult RollbackStartup(
        std::vector<std::size_t>& touchedEntries,
        DeviceResult originalFailure);

    // --------------------------------------------------------
    // STATE
    // --------------------------------------------------------

    bool HasPendingCleanup() const;

    Bus::EE_MemoryBus& m_memoryBus;

    // Owns Entry metadata only.
    // Device objects remain externally owned.
    std::vector<Entry> m_entries;

    // Entry indices in dependency-safe startup order.
    std::vector<std::size_t> m_startupOrder;

    bool m_started;
    bool m_faulted;

    // Becomes true only after Stop() explicitly succeeds.
    // Registration or a new startup attempt invalidates this
    // confirmation and requires another successful Stop().
    bool m_stopConfirmed;

    DeviceStatus m_lastStatus;
};

} // namespace DeviceRegistry
} // namespace EE
} // namespace ORBIT2SX
