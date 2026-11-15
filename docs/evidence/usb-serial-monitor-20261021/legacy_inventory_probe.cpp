// Qualifies actual legacy shared objects against the historical Reader consumer.
// Host-only graph stubs never start a provider or perform physical I/O.
#include "runtime/drivers/InstalledSerialInventory.h"
#include "runtime/drivers/InstalledSerialSession.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
namespace { const risc_driver_v2* driver; unsigned acquired, released; }
namespace RuntimeInstalledProviders {
bool prepare() { return true; }
bool nextProvider(const char* cap, uint32_t api, size_t* cursor, char* id, size_t capacity) {
  assert(!std::strcmp(cap, "serial.port") && api == 1 && cursor && id);
  if ((*cursor)++) return false;
  std::snprintf(id, capacity, "%s", driver->driver_id);
  return true;
}
bool acquire(const char* id, const char* cap, uint32_t api, Lease* out) {
  assert(!std::strcmp(id, driver->driver_id) && !std::strcmp(cap, "serial.port") && api == 1);
  *out = {{1, ++acquired}, driver->capability};
  return true;
}
bool release(Lease* lease) { assert(lease && lease->grant.slot); ++released; *lease = {}; return true; }
bool recoverFailedProvider(const char*, const char*, uint32_t) { assert(false); return false; }
}
int main(int argc, char** argv) {
  assert(argc > 1);
  for (int i = 1; i < argc; ++i) {
    void* lib = dlopen(argv[i], RTLD_NOW); assert(lib);
    auto get = reinterpret_cast<risc_driver_get_v2_fn>(dlsym(lib, "t5_driver_get")); assert(get);
    driver = get(2); assert(driver && !std::strcmp(driver->capability_id, "serial.port"));
    const auto* api = static_cast<const risc_serial_port_api_v1*>(driver->capability);
    assert(api->struct_size == sizeof(risc_serial_port_api_v1));
    acquired = released = 0;
    RuntimeDevices::Registry registry;
    RuntimeInstalledProviders::InstalledSerialInventory inventory(registry);
    assert(inventory.refresh() == RuntimeInstalledProviders::InstalledSerialInventory::Result::Updated);
    assert(acquired == 1 && released == 1);
    assert(!inventory.retainedProviders() && !inventory.publishedDevices() && !registry.count());
    RuntimeInstalledProviders::InstalledSerialSession session;
    assert(session.bind(api, 1, 1) && !session.hasEndpoints());
    std::printf("%s: rejected by real inventory, zero devices, no stream endpoints PASS\n", driver->driver_id);
    assert(dlclose(lib) == 0);
  }
}
