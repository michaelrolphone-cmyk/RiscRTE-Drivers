# Build selection only. Runtime owns admission, native hardware and lifecycle;
# the external boot graph owns PMIC observation and CST9217 reset authority.
include_guard(GLOBAL)
get_filename_component(badge_board "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
get_filename_component(badge_drivers "${badge_board}/../.." REALPATH)
set(RISC_NATIVE_TARGET "waveshare-esp32-s3-touch-amoled-1.75c")
set(RISC_NATIVE_PARTITIONS "${CMAKE_CURRENT_LIST_DIR}/partitions.csv")
set(RISC_NATIVE_APPDATA OFF)
set(RISC_NATIVE_IQ OFF)
list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
# Shared Home declares 20 requirements, including unavailable optional services.
# This is declaration capacity only; live application grant capacity stays 16.
list(APPEND RISC_NATIVE_DEFINITIONS RISC_STAGE_LOGS=1 RISC_APP_REQUIREMENT_ROWS=24)
list(APPEND RISC_NATIVE_EXTRA_SOURCES
    "${badge_drivers}/components/co5300/native/Co5300Display.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/BadgeIdentity.cpp")
list(APPEND RISC_NATIVE_INCLUDE_DIRECTORIES "${badge_drivers}/components/co5300/include")
list(APPEND RISC_NATIVE_COMPILE_OPTIONS "SHELL:-include \"${badge_board}/display-profile.h\"")
list(APPEND RISC_NATIVE_LINK_OPTIONS
    "-Wl,-u,risc_co5300_native_display_anchor"
    "-Wl,-u,risc_badge_native_identity")
