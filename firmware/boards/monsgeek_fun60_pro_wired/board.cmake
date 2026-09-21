# HAL/library integration; deliberately no flashable target until USB, storage
# and the complete application lifecycle are integrated and audited.
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Use cmake/arm-none-eabi.cmake for the FUN60 HAL")
endif()
include(cmake/artery_sdk.cmake)
add_library(fun60_hal STATIC
    ${MT_BOARD_DIR}/src/hardware.c
    ${MT_BOARD_DIR}/src/fun60_matrix.c
    ${MT_BOARD_DIR}/src/layout_port.c
    ${MT_BOARD_DIR}/src/usb_descriptors.c
    ${MT_BOARD_DIR}/src/flash_store.c
    firmware/platform/artery_at32f405/src/usb_composite.c
    ${MT_APP_SOURCES} ${MT_CONTROL_SOURCES} ${MT_STORE_SOURCES} ${MT_TELEMETRY_SOURCES})
target_link_libraries(fun60_hal PUBLIC artery_usb)
target_include_directories(fun60_hal PUBLIC ${MT_BOARD_DIR}/include firmware/app/include)
target_compile_definitions(fun60_hal PUBLIC MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=183 MT_HID_USAGE_MAX=0x73
    MT_BUILD_VERSION="${PROJECT_VERSION}" MT_BUILD_TARGET="monsgeek_fun60_pro_wired")
target_compile_options(fun60_hal PRIVATE -Os -Wall -Wextra -Werror)
target_link_options(fun60_hal INTERFACE
    -Wl,--wrap=usbd_device_request,--wrap=usbd_endpoint_request)
message(STATUS "FUN60: HAL library only; no flashable application is produced")

# Explicit offline test ELF: no vectors/header/startup, never a flash candidate.
add_executable(fun60_usb_audit EXCLUDE_FROM_ALL tests/fun60_usb_probe.c)
target_link_libraries(fun60_usb_audit PRIVATE fun60_hal c gcc nosys)
target_link_options(fun60_usb_audit PRIVATE -nostartfiles
    -Wl,-Ttext=0x08005200,-Tdata=0x20000000,--gc-sections,-e,at32_usb_init
    -Wl,-u,at32_usb_keyboard,-u,at32_usb_midi,-u,at32_usb_service
    -Wl,-u,midi_control_service,-u,scan_stream_service
    -Wl,-u,probe_bind,-u,probe_speed,-u,probe_connected,-u,probe_request)
set_target_properties(fun60_usb_audit PROPERTIES SUFFIX .elf)

add_executable(fun60_flash_audit EXCLUDE_FROM_ALL tests/fun60_flash_probe.c)
target_link_libraries(fun60_flash_audit PRIVATE fun60_hal c gcc nosys)
target_link_options(fun60_flash_audit PRIVATE -nostartfiles -Wl,--gc-sections
    -Wl,-u,fun60_flash_read,-u,fun60_flash_write,-u,fun60_flash_erase
    -T${MT_BOARD_DIR}/linker/application.ld)
set_property(TARGET fun60_flash_audit APPEND PROPERTY LINK_DEPENDS ${MT_BOARD_DIR}/linker/application.ld)
set_target_properties(fun60_flash_audit PROPERTIES SUFFIX .elf)
