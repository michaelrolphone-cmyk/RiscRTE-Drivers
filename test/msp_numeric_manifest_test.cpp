/* Actual pinned Runtime::manifest, identity and update-version checks.
 * -fno-access-control exposes the private parser for the test; production
 * Runtime source is compiled unchanged, not copied or regex-emulated. */
#include "bootstrap/Runtime.h"
#include "runtime/update/Version.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
static bool owner() { return true; }
static bool health(risc_runtime_health_v1*) { return false; }
static void delay(uint32_t) { assert(!"manifest test must not wait"); }
static bool logLine(const char*) { return true; }
int main(int argc,char**argv) {
  assert(argc>=3);
  for(int i=1;i<argc;++i) {
    std::ifstream input(argv[i]); assert(input.good());
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    JsonDocument doc;
    assert(RiscBoot::parse(bytes.data(),bytes.size(),doc));
    RiscBoot::Runtime runtime({owner,health,delay,logLine});
    RiscBoot::Runtime::Driver driver{};
    std::strcpy(driver.elf,"/candidate/manifest.json");
    assert(runtime.manifest(doc.as<JsonObjectConst>(),driver));
    RuntimePackages::Identity id{};
    assert(RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver,
        doc["id"],doc["version"],doc["file_name"],false,&id));
    uint32_t parts[3]{};
    assert(RiscUpdate::parseVersion(doc["version"],parts));
    std::string version=doc["version"].as<const char*>();
    /* Same actual manifest, only version changed: retain the exact rejected
     * format from ed7886d plus build metadata and malformed numeric controls. */
    for(const auto& bad: {version+"-checked-consumer.1",version+"+local",std::string("0.1"),std::string("0.1.4294967296")}) {
      doc["version"]=bad; driver={}; std::strcpy(driver.elf,"/candidate/manifest.json");
      assert(!runtime.manifest(doc.as<JsonObjectConst>(),driver));
      assert(!std::strcmp(runtime.error(),"invalid driver version"));
      assert(!RuntimePackages::makeIdentity(RuntimePackages::Kind::Driver,
          doc["id"],doc["version"],doc["file_name"],false,&id));
      assert(!RiscUpdate::parseVersion(bad.c_str(),parts));
    }
    std::printf("PASS actual pinned Runtime manifest/identity/version: %s %s; prerelease/build/malformed controls rejected\n",argv[i],version.c_str());
  }
}
