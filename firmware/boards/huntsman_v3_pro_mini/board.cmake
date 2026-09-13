option(HUNTSMAN_BUILD_FIRMWARE "Build the LPC5528 application image" OFF)
option(HUNTSMAN_USB_ONLY "Build USB bring-up without optical or lighting initialization" ON)
option(HUNTSMAN_KEYBOARD_DIAGNOSTICS "Enable recovered keyboard engine and isolated CDC tests" OFF)
option(HUNTSMAN_TRAVEL_LIGHTING "Automatic optical scanning and production-mapped travel lighting" OFF)
option(HUNTSMAN_KEYBOARD_MODE "Standalone raw Schmitt keyboard with GUI configuration" OFF)
if(HUNTSMAN_KEYBOARD_MODE AND NOT HUNTSMAN_TRAVEL_LIGHTING)
    message(FATAL_ERROR "Keyboard mode requires the automatic scan/lighting application")
endif()
if(HUNTSMAN_TRAVEL_LIGHTING AND NOT HUNTSMAN_KEYBOARD_DIAGNOSTICS)
    message(FATAL_ERROR "Travel lighting requires the keyboard diagnostic application")
endif()
set(HUNTSMAN_PRODUCTION_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/../extracted_firmware/raw/Talia_T1_60%_7203_App_FW_v2.1.0_E888780F.bin"
    CACHE FILEPATH "Read-only hash-pinned production reference for offline audits")
# Build target: the model number this port reports in its build identity.
set(MT_BOARD_TARGET "RZ03-0499")

set(KEYBOARD_LOGIC_SOURCES
    ${MT_APP_SOURCES}
    firmware/boards/huntsman_v3_pro_mini/src/layout_port.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_console.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_scan.c
    firmware/boards/huntsman_v3_pro_mini/src/calibration_store.c
    firmware/boards/huntsman_v3_pro_mini/src/optical_key.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_layout.c
    firmware/boards/huntsman_v3_pro_mini/src/keyboard_reference_tables.c
    firmware/boards/huntsman_v3_pro_mini/src/lighting_reference_tables.c
)
add_library(midi_typist_app OBJECT ${MT_APP_SOURCES})
target_include_directories(midi_typist_app PUBLIC firmware/app/include)
target_compile_definitions(midi_typist_app PUBLIC MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204 MT_HID_USAGE_MAX=0x73)
target_compile_definitions(midi_typist_app PUBLIC MT_BUILD_VERSION="${PROJECT_VERSION}" MT_BUILD_TARGET="${MT_BOARD_TARGET}")
target_compile_options(midi_typist_app PRIVATE -Wall -Wextra -Werror)

# The archive joins portable objects with the selected board implementation.
set(HUNTSMAN_BOARD_SOURCES ${KEYBOARD_LOGIC_SOURCES})
list(REMOVE_ITEM HUNTSMAN_BOARD_SOURCES ${MT_APP_SOURCES})
add_library(huntsman_core STATIC
    firmware/boards/huntsman_v3_pro_mini/src/optical_scan.c
    $<TARGET_OBJECTS:midi_typist_app>
    ${HUNTSMAN_BOARD_SOURCES}
    firmware/boards/huntsman_v3_pro_mini/src/updater_protocol.c
)
target_include_directories(huntsman_core PUBLIC firmware/app/include firmware/boards/huntsman_v3_pro_mini/include)
target_compile_definitions(huntsman_core PUBLIC MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204 MT_HID_USAGE_MAX=0x73)
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
    add_test(NAME keyboard_gui COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_gui.py)
    add_test(NAME firmware_flasher COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_firmware_flasher.py)
    add_test(NAME image_reservation COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_image_reservation.py)
    add_test(NAME scan_display COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_scan_display.py)
    add_test(NAME last_key_stream COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_last_key_stream.py)
    add_test(NAME flash_dump COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_dump_flash.py)
    # Host ABI for comparison against executed production ARM instructions.
    add_library(keyboard_logic SHARED ${KEYBOARD_LOGIC_SOURCES})
    target_include_directories(keyboard_logic PUBLIC firmware/app/include firmware/boards/huntsman_v3_pro_mini/include)
    target_compile_definitions(keyboard_logic PRIVATE MT_KEY_CAPACITY=65 MT_LIGHT_FRAME_BYTES=204 MT_HID_USAGE_MAX=0x73)
    target_compile_options(keyboard_logic PRIVATE -Wall -Wextra -Werror)
    add_custom_target(audit-keyboard
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/keyboard_reference_tables.py
            ${HUNTSMAN_PRODUCTION_REFERENCE} --check ${CMAKE_CURRENT_SOURCE_DIR}/firmware/boards/huntsman_v3_pro_mini/src/keyboard_reference_tables.c
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_config.py
            $<TARGET_FILE:keyboard_logic> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_optical_key.py
            $<TARGET_FILE:keyboard_logic> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_scan.py
            $<TARGET_FILE:keyboard_logic> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        DEPENDS keyboard_logic USES_TERMINAL VERBATIM)
    add_custom_target(audit-lighting
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/lighting_reference_tables.py
            ${HUNTSMAN_PRODUCTION_REFERENCE} --check ${CMAKE_CURRENT_SOURCE_DIR}/firmware/boards/huntsman_v3_pro_mini/src/lighting_reference_tables.c
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_lighting.py
            $<TARGET_FILE:keyboard_logic> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        DEPENDS keyboard_logic USES_TERMINAL VERBATIM)
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
    ${NXP_USB}/device/class/usb_device_cdc_acm.c
    ${NXP_ROOT}/component/osa/fsl_os_abstraction_bm.c
    ${NXP_ROOT}/component/lists/fsl_component_generic_list.c
    firmware/boards/huntsman_v3_pro_mini/src/board.c
    firmware/platform/nxp_lpc55/src/debug.c
    firmware/platform/nxp_lpc55/src/usb_composite.c
    firmware/boards/huntsman_v3_pro_mini/src/usb_descriptors.c
    firmware/platform/nxp_lpc55/src/usb_errata.c
    firmware/platform/nxp_lpc55/src/usb_safe_memcpy.c
)
if(HUNTSMAN_KEYBOARD_DIAGNOSTICS)
    if(HUNTSMAN_KEYBOARD_MODE)
        set(OPTICAL_AUDIT tools/test_keyboard_mode_arm.py)
    else()
        set(OPTICAL_AUDIT tools/test_optical_bus_arm.py)
    endif()
    target_sources(huntsman_firmware PRIVATE firmware/boards/huntsman_v3_pro_mini/src/main_keyboard.c firmware/platform/nxp_lpc55/src/debug_rx.c
        firmware/boards/huntsman_v3_pro_mini/src/optical_bus.c firmware/boards/huntsman_v3_pro_mini/src/optical_transport.c firmware/boards/huntsman_v3_pro_mini/src/keyboard_live.c firmware/platform/nxp_lpc55/src/scan_stream.c
        ${NXP_DRIVERS}/lpc_dma/fsl_dma.c
        ${NXP_DRIVERS}/flexcomm/spi/fsl_spi_dma.c)
    target_compile_definitions(huntsman_firmware PRIVATE HUNTSMAN_KEYBOARD_DIAGNOSTICS=1)
    if(HUNTSMAN_KEYBOARD_MODE)
        target_compile_definitions(huntsman_firmware PRIVATE HUNTSMAN_KEYBOARD_MODE=1)
        target_sources(huntsman_firmware PRIVATE firmware/boards/huntsman_v3_pro_mini/src/flash_dump.c)
        target_include_directories(huntsman_firmware PRIVATE ${NXP_DRIVERS}/iap1)
        add_custom_target(audit-dump
            COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_flash_dump_arm.py $<TARGET_FILE:huntsman_firmware>
            DEPENDS huntsman_firmware USES_TERMINAL VERBATIM)
        add_custom_target(audit-calibration
            COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_calibration_arm.py
                $<TARGET_FILE:huntsman_firmware> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
            DEPENDS huntsman_firmware USES_TERMINAL VERBATIM)
        add_custom_target(audit-menu
            COMMAND python3 -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_menu_arm.py
                $<TARGET_FILE:huntsman_firmware> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
            DEPENDS huntsman_firmware USES_TERMINAL VERBATIM)
    endif()
    if(HUNTSMAN_TRAVEL_LIGHTING)
        target_sources(huntsman_firmware PRIVATE firmware/boards/huntsman_v3_pro_mini/src/travel_lighting.c firmware/boards/huntsman_v3_pro_mini/src/lighting_bus.c)
        target_compile_definitions(huntsman_firmware PRIVATE HUNTSMAN_TRAVEL_LIGHTING=1)
    endif()
elseif(HUNTSMAN_USB_ONLY)
    target_sources(huntsman_firmware PRIVATE firmware/boards/huntsman_v3_pro_mini/src/main_usb.c)
else()
    target_sources(huntsman_firmware PRIVATE firmware/boards/huntsman_v3_pro_mini/src/main.c firmware/boards/huntsman_v3_pro_mini/src/lighting.c firmware/boards/huntsman_v3_pro_mini/src/optical_hw.c)
endif()

if(HUNTSMAN_TRAVEL_LIGHTING)
    add_custom_target(audit-lighting
        COMMAND python3 -u ${CMAKE_CURRENT_SOURCE_DIR}/tools/lighting_reference_tables.py
            ${HUNTSMAN_PRODUCTION_REFERENCE} --check ${CMAKE_CURRENT_SOURCE_DIR}/firmware/boards/huntsman_v3_pro_mini/src/lighting_reference_tables.c
        COMMAND python3 -u ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_lighting_arm.py
            $<TARGET_FILE:huntsman_firmware> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        DEPENDS audit-keyboard USES_TERMINAL VERBATIM)
endif()
target_link_libraries(huntsman_firmware PRIVATE huntsman_core)

target_include_directories(huntsman_firmware PRIVATE
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
add_custom_target(audit-usb
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_arm.py
            $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_startup_arm.py
            $<TARGET_FILE:huntsman_firmware>
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_usb_chirp_arm.py
            $<TARGET_FILE:huntsman_firmware>
    DEPENDS huntsman_firmware
    USES_TERMINAL
    VERBATIM
)

if(HUNTSMAN_KEYBOARD_DIAGNOSTICS)
    add_custom_target(audit-keyboard
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_keyboard_console_arm.py
            $<TARGET_FILE:huntsman_firmware>
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/${OPTICAL_AUDIT}
            $<TARGET_FILE:huntsman_firmware> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_scan_stream_arm.py
            $<TARGET_FILE:huntsman_firmware> --reference ${HUNTSMAN_PRODUCTION_REFERENCE}
        DEPENDS audit-usb USES_TERMINAL VERBATIM)
endif()

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
