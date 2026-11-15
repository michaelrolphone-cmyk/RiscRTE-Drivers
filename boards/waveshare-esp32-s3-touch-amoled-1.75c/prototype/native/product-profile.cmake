# Extend the established board selection, preserving the same native backend.
include("${CMAKE_CURRENT_LIST_DIR}/../../native/product-profile.cmake")
if(DEFINED RISC_BADGE_SPIFFS_COMPONENT)
    if(NOT EXISTS "${RISC_BADGE_SPIFFS_COMPONENT}/CMakeLists.txt")
        message(FATAL_ERROR "Missing explicitly staged SPIFFS component")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${RISC_BADGE_SPIFFS_COMPONENT}")
endif()
set(RISC_NATIVE_PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/partitions.csv")
set(RISC_NATIVE_APPDATA ON)
list(APPEND RISC_NATIVE_DEFINITIONS
    RISC_PAIRED_APP_DATA=1 RISC_NATIVE_APP_DATA_MOUNT=1
    RISC_NATIVE_STANDALONE_APP_DATA=1 RISC_NATIVE_APP_DATA_OFFSET=0x1f00000
    RISC_NATIVE_APP_DATA_BYTES=0x100000 RISC_NATIVE_FLASH_BYTES=0x2000000)

# Explicit copied/revision-checked preferences over the existing namespace KV.
# This is not the separate atomic key-value namespace owner.
option(RISC_BADGE_QUICK_ACTIONS "Enable selected Home Quick preference broker" OFF)
if(RISC_BADGE_QUICK_ACTIONS)
    list(APPEND RISC_NATIVE_DEFINITIONS
        RISC_RUNTIME_PREFERENCE_STORE=1 RISC_RUNTIME_ATOMIC_KEY_VALUE=0)
endif()

option(RISC_BADGE_PERFORMANCE_TRACE "Bounded RAM performance recorder" OFF)
if(RISC_BADGE_PERFORMANCE_TRACE)
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_PERFORMANCE_TRACE=1)
endif()

# This prototype composes only the complete-frame CO5300 facade.
# Native-only batching: generic backend consumers retain their default pair API.
# Store/caller qualification must remain tied to this selected composition.
set(RISC_BADGE_STRIPE_ROWS "4" CACHE STRING "Complete-frame native stripe rows")
if(NOT RISC_BADGE_STRIPE_ROWS MATCHES "^(4|8|16|32)$")
    message(FATAL_ERROR "Unsupported complete-frame stripe rows")
endif()
list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_STRIPE_ROWS=${RISC_BADGE_STRIPE_ROWS})
option(RISC_BADGE_QUEUED_PIXELS "Optional double-buffer queued pixels" OFF)
if(RISC_BADGE_QUEUED_PIXELS)
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_QUEUED_PIXELS=1)
else()
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_QUEUED_PIXELS=0)
endif()

# Explicit native optimization; generic products keep their default disabled.
option(RISC_BADGE_REUSE_UNCHANGED_STRIPES "Optional exact completed-panel shadow" OFF)
if(RISC_BADGE_REUSE_UNCHANGED_STRIPES)
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_REUSE_UNCHANGED=1)
else()
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_REUSE_UNCHANGED=0)
endif()

option(RISC_BADGE_REUSE_COLUMNS "Optional completed CASET register reuse" OFF)
if(RISC_BADGE_REUSE_COLUMNS)
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_REUSE_COLUMNS=1)
else()
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_CO5300_REUSE_COLUMNS=0)
endif()

option(RISC_BADGE_TIME_SAMPLE "Read-only network time sample capability" OFF)
if(RISC_BADGE_TIME_SAMPLE)
    list(APPEND RISC_NATIVE_DEFINITIONS RISC_ENABLE_TIME_SAMPLE=1)
endif()
