option(HUNTSMAN_BUILD_FIRMWARE "Build the LPC5528 application image" OFF)
set(HUNTSMAN_PRODUCTION_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin"
    CACHE FILEPATH "Read-only hash-pinned production reference for offline audits")
# Build target: the model number this port reports in its build identity.
set(MT_BOARD_TARGET "RZ03-0499")

# The audit targets share the complete suite manifest (tools/run_tests.py).
function(mt_audit_target name)
    if(HUNTSMAN_BUILD_FIRMWARE)
        set(dependency huntsman_firmware)
        set(artifact --elf $<TARGET_FILE:huntsman_firmware>)
    else()
        set(dependency keyboard_logic)
        set(artifact --library $<TARGET_FILE:keyboard_logic>)
    endif()
    set(groups)
    foreach(group IN LISTS ARGN)
        list(APPEND groups --group ${group})
    endforeach()
    add_custom_target(${name}
        COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_tests.py
            --no-build ${artifact} --reference ${HUNTSMAN_PRODUCTION_REFERENCE} ${groups}
        DEPENDS ${dependency} VERBATIM)
endfunction()

set(KEYBOARD_LOGIC_SOURCES
    ${MT_APP_SOURCES}
    firmware/boards/huntsman_v3_pro_mini/src/layout_port.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_scan.c
    firmware/boards/huntsman_v3_pro_mini/src/device_store.c
    firmware/boards/huntsman_v3_pro_mini/src/optical_key.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_layout.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_reference_tables.c
    firmware/boards/huntsman_v3_pro_mini/src/lighting_reference_tables.c
)
add_library(midi_typist_app OBJECT ${MT_APP_SOURCES})
target_include_directories(midi_typist_app PUBLIC firmware/app/include)
target_compile_definitions(midi_typist_app PUBLIC MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204)
target_compile_definitions(midi_typist_app PUBLIC MT_BUILD_VERSION="${PROJECT_VERSION}" MT_BUILD_TARGET="${MT_BOARD_TARGET}")
target_compile_options(midi_typist_app PRIVATE -Wall -Wextra -Werror)

# The archive joins portable objects with the selected board implementation.
set(HUNTSMAN_BOARD_SOURCES ${KEYBOARD_LOGIC_SOURCES})
list(REMOVE_ITEM HUNTSMAN_BOARD_SOURCES ${MT_APP_SOURCES})
add_library(huntsman_core STATIC
    $<TARGET_OBJECTS:midi_typist_app>
    ${HUNTSMAN_BOARD_SOURCES}
    firmware/boards/huntsman_v3_pro_mini/src/updater_protocol.c
)
target_include_directories(huntsman_core PUBLIC firmware/app/include firmware/boards/huntsman_v3_pro_mini/include)
target_compile_definitions(huntsman_core PUBLIC MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204)
target_compile_definitions(huntsman_core PUBLIC MT_BUILD_VERSION="${PROJECT_VERSION}" MT_BUILD_TARGET="${MT_BOARD_TARGET}")
target_compile_options(huntsman_core PRIVATE -Wall -Wextra -Werror)

if(NOT HUNTSMAN_BUILD_FIRMWARE)
    enable_testing()
    # Independent port: no Huntsman sources, include paths, SDK or reference data.
    add_executable(portable_app_tests ${MT_APP_SOURCES}
        firmware/boards/synthetic/src/synthetic_board.c tests/test_portable_app.c)
    target_include_directories(portable_app_tests PRIVATE firmware/app/include firmware/boards/synthetic/include)
    target_compile_definitions(portable_app_tests PRIVATE MT_KEY_CAPACITY=128 MT_LIGHT_FRAME_BYTES=384)
    target_compile_options(portable_app_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME portable_app COMMAND portable_app_tests)
    add_test(NAME portability_architecture COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_portability.py)
    add_test(NAME defaults COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_defaults.py)
    add_executable(core_tests tests/test_core.c)
    target_link_libraries(core_tests PRIVATE huntsman_core)
    target_compile_options(core_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME core_tests COMMAND core_tests)
    add_executable(keyboard_raw_tests tests/test_keyboard_raw.c)
    target_link_libraries(keyboard_raw_tests PRIVATE huntsman_core)
    target_compile_options(keyboard_raw_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME keyboard_raw COMMAND keyboard_raw_tests)
    add_executable(keyboard_menu_tests tests/test_keyboard_menu.c)
    target_link_libraries(keyboard_menu_tests PRIVATE huntsman_core)
    target_compile_options(keyboard_menu_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME keyboard_menu COMMAND keyboard_menu_tests)
    add_executable(keyboard_midi_tests tests/test_keyboard_midi.c)
    target_link_libraries(keyboard_midi_tests PRIVATE huntsman_core)
    target_compile_options(keyboard_midi_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME keyboard_midi COMMAND keyboard_midi_tests)
    add_executable(calibration_tests tests/test_calibration.c)
    target_link_libraries(calibration_tests PRIVATE huntsman_core)
    target_compile_options(calibration_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME calibration COMMAND calibration_tests)
    add_executable(device_store_tests tests/test_device_store.c)
    target_link_libraries(device_store_tests PRIVATE huntsman_core)
    target_compile_options(device_store_tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME device_store COMMAND device_store_tests)
    add_test(NAME keyboard_gui COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_gui.py)
    add_test(NAME latest_only COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_latest_only.py)
    add_test(NAME build_identity COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_build_identity.py)
    add_test(NAME device_flashing COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_device_flashing.py)
    add_test(NAME monsgeek_identity COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_monsgeek_identity.py)
    add_test(NAME keyboard_boards COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_boards.py)
    add_test(NAME image_reservation COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_image_reservation.py)
    add_test(NAME midi_sysex COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_midi_sysex.py)
    # Host ABI for comparison against executed production ARM instructions.
    add_library(keyboard_logic SHARED ${KEYBOARD_LOGIC_SOURCES})
    target_include_directories(keyboard_logic PUBLIC firmware/app/include firmware/boards/huntsman_v3_pro_mini/include)
    target_compile_definitions(keyboard_logic PRIVATE MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204)
    target_compile_options(keyboard_logic PRIVATE -Wall -Wextra -Werror)
    mt_audit_target(audit-keyboard reference-keyboard)
    mt_audit_target(audit-lighting reference-lighting)
    return()
endif()

set(NXP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nxp)
set(NXP_DRIVERS ${NXP_ROOT}/core/drivers)
set(NXP_DEVICE ${NXP_ROOT}/devices/LPC5528)
set(NXP_USB ${NXP_ROOT}/usb)

add_executable(huntsman_firmware
    ${NXP_DEVICE}/gcc/startup_LPC5528.S
    ${NXP_DEVICE}/system_LPC5528.c
    ${NXP_DRIVERS}/common/fsl_common.c
    ${NXP_DRIVERS}/common/fsl_common_arm.c
    ${NXP_DRIVERS}/ctimer/fsl_ctimer.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_clock.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_power.c
    ${NXP_ROOT}/devices/LPC55S69/drivers/fsl_reset.c
    ${NXP_DRIVERS}/flexcomm/fsl_flexcomm.c
    ${NXP_DRIVERS}/flexcomm/i2c/fsl_i2c.c
    ${NXP_DRIVERS}/flexcomm/spi/fsl_spi.c
    ${NXP_DRIVERS}/lpc_gpio/fsl_gpio.c
    ${NXP_USB}/phy/usb_phy.c
    ${NXP_USB}/device/usb_device_dci.c
    ${NXP_USB}/device/usb_device_ch9.c
    ${NXP_USB}/device/usb_device_lpcip3511.c
    ${NXP_USB}/device/class/usb_device_class.c
    ${NXP_USB}/device/class/usb_device_hid.c
    ${NXP_ROOT}/component/osa/fsl_os_abstraction_bm.c
    ${NXP_ROOT}/component/lists/fsl_component_generic_list.c
    firmware/boards/huntsman_v3_pro_mini/src/board.c
    firmware/platform/nxp_lpc55/src/debug.c
    firmware/services/src/midi_control.c
    firmware/platform/nxp_lpc55/src/usb_composite.c
    firmware/boards/huntsman_v3_pro_mini/src/usb_descriptors.c
    firmware/platform/nxp_lpc55/src/usb_errata.c
    firmware/platform/nxp_lpc55/src/usb_safe_memcpy.c
)
target_sources(huntsman_firmware PRIVATE
    firmware/boards/huntsman_v3_pro_mini/src/main_keyboard.c
    firmware/boards/huntsman_v3_pro_mini/src/optical_bus.c
    firmware/boards/huntsman_v3_pro_mini/src/optical_transport.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_live.c
    firmware/services/src/scan_stream.c
    firmware/boards/huntsman_v3_pro_mini/src/flash_dump.c
    firmware/boards/huntsman_v3_pro_mini/src/travel_lighting.c
    firmware/boards/huntsman_v3_pro_mini/src/lighting_bus.c
    ${NXP_DRIVERS}/lpc_dma/fsl_dma.c
    ${NXP_DRIVERS}/flexcomm/spi/fsl_spi_dma.c)
target_include_directories(huntsman_firmware PRIVATE ${NXP_DRIVERS}/iap1)
mt_audit_target(audit-dump dump)
mt_audit_target(audit-calibration calibration)
mt_audit_target(audit-menu menu)
mt_audit_target(audit-lighting lighting)
target_link_libraries(huntsman_firmware PRIVATE huntsman_core)

target_include_directories(huntsman_firmware PRIVATE
    firmware/services/include
    firmware/app/include
    firmware/boards/huntsman_v3_pro_mini/include
    firmware/platform/nxp_lpc55/include
    ${NXP_DEVICE}
    ${NXP_ROOT}/devices/periph
    ${NXP_ROOT}/devices/LPC55S69/drivers
    ${NXP_ROOT}/cmsis/Include
    ${NXP_DRIVERS}/common
    ${NXP_DRIVERS}/ctimer
    ${NXP_DRIVERS}/flexcomm
    ${NXP_DRIVERS}/flexcomm/i2c
    ${NXP_DRIVERS}/flexcomm/spi
    ${NXP_DRIVERS}/lpc_gpio
    ${NXP_DRIVERS}/lpc_dma
    ${NXP_USB}/include
    ${NXP_USB}/phy
    ${NXP_USB}/device
    ${NXP_USB}/device/class
    ${NXP_ROOT}/component/osa
    ${NXP_ROOT}/component/osa/config
    ${NXP_ROOT}/component/lists
)

target_compile_definitions(huntsman_firmware PRIVATE
    CPU_LPC5528JBD100
    __STARTUP_CLEAR_BSS
    __START=main
    SDK_DEBUGCONSOLE=0
)

set(MCU_FLAGS
    -mcpu=cortex-m33
    -mthumb
    # USB SRAM (0x40100000) is Device memory. GCC must not merge adjacent
    # byte fields into unaligned halfword/word accesses in NXP USB state.
    -mno-unaligned-access
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
)
target_compile_options(huntsman_core PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections)
target_compile_options(midi_typist_app PRIVATE ${MCU_FLAGS} -ffunction-sections -fdata-sections)
target_compile_options(huntsman_firmware PRIVATE
    ${MCU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Werror=implicit-function-declaration
)

target_link_options(huntsman_firmware PRIVATE
    ${MCU_FLAGS}
    -nostartfiles
    -Wl,--gc-sections
    # Observe EP0 status completion without modifying the vendor stack.
    -Wl,--wrap=USB_DeviceNotificationTrigger
    # Prebuilt newlib memcpy has unaligned halfword tails despite MCU_FLAGS.
    -Wl,--wrap=memcpy
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.map
    -T${CMAKE_CURRENT_SOURCE_DIR}/firmware/boards/huntsman_v3_pro_mini/linker/lpc5528_application.ld
)
target_link_libraries(huntsman_firmware PRIVATE c gcc nosys)

set_target_properties(huntsman_firmware PROPERTIES SUFFIX .elf)
set_property(TARGET huntsman_firmware APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/firmware/boards/huntsman_v3_pro_mini/linker/lpc5528_application.ld)

# Explicit, offline-only target; optional Python dependencies are not needed
# to build firmware. This target never opens a USB device or runs the updater.
mt_audit_target(audit-usb usb)

mt_audit_target(audit-keyboard keyboard)

add_custom_command(TARGET huntsman_firmware POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill 0xFF --pad-to 0x20020000
            $<TARGET_FILE:huntsman_firmware> ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:huntsman_firmware> ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_image.py
            --elf $<TARGET_FILE:huntsman_firmware>
            --bin ${CMAKE_CURRENT_BINARY_DIR}/huntsman_firmware.bin
    VERBATIM
)
