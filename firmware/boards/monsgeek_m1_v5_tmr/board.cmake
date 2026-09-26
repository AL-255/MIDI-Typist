# M1 libraries and an experimental application with explicit guarded IAP entry.
# Runtime transport/power recovery remains incomplete. Native build runs this board
# against the real shared application; ARM build uses the pinned official SDK.
# Bluetooth/2.4 GHz are unverified, so the radio HAL and peer scheduler are only
# built when this option is enabled. The default artifact is a USB-only keyboard
# that can be redistributed without the unverified feature; development and audit
# builds pass -DMT_M1_WIRELESS=ON.
option(MT_M1_WIRELESS "Build the unverified M1 Bluetooth/2.4 GHz stack" OFF)
if(MT_M1_WIRELESS)
    set(MT_M1_WIRELESS_SOURCES ${MT_BOARD_DIR}/src/m1_radio_hal.c ${MT_BOARD_DIR}/src/m1_wireless.c)
else()
    set(MT_M1_WIRELESS_SOURCES ${MT_BOARD_DIR}/src/m1_wireless_off.c)
endif()
add_library(midi_typist_app STATIC ${MT_APP_SOURCES})
target_include_directories(midi_typist_app PUBLIC firmware/app/include)
target_compile_definitions(midi_typist_app PUBLIC MT_KEY_CAPACITY=82 MT_LIGHT_FRAME_BYTES=246)
target_compile_options(midi_typist_app PRIVATE -Wall -Wextra -Werror)
add_library(m1_board STATIC ${MT_BOARD_DIR}/src/m1_board.c ${MT_BOARD_DIR}/src/m1_scan.c
    ${MT_BOARD_DIR}/src/m1_lighting_encode.c ${MT_BOARD_DIR}/src/m1_battery.c
    ${MT_BOARD_DIR}/src/m1_controls.c ${MT_BOARD_DIR}/src/m1_power.c
    ${MT_BOARD_DIR}/src/m1_radio_packet.c ${MT_BOARD_DIR}/src/m1_radio_keyboard.c
    ${MT_BOARD_DIR}/src/m1_wake.c ${MT_BOARD_DIR}/src/m1_factory.c)
target_include_directories(m1_board PUBLIC ${MT_BOARD_DIR}/include firmware/app/include)
target_compile_definitions(m1_board PUBLIC MT_KEY_CAPACITY=82 MT_LIGHT_FRAME_BYTES=246
    MT_M1_WIRELESS=$<BOOL:${MT_M1_WIRELESS}>)
target_compile_options(m1_board PRIVATE -Wall -Wextra -Werror)
target_link_libraries(m1_board PUBLIC midi_typist_app)
# Archive-level cycle: board calibration uses shared normalization; application
# uses board layout callbacks. CMake rescans both archives without duplicating
# application objects inside each consumer. INTERFACE keeps board includes out
# of the application's own compilation.
target_link_libraries(midi_typist_app INTERFACE m1_board)
add_library(midi_typist_services STATIC
    firmware/services/src/midi_control.c firmware/services/src/scan_stream.c
    firmware/services/src/device_store.c)
target_compile_definitions(midi_typist_services PUBLIC
    MT_STORE_PAGE_SIZE=2048 MT_STORE_MAGIC="M1P2" MT_STORE_INVALID_READ=0)
target_include_directories(midi_typist_services PUBLIC firmware/services/include)
target_link_libraries(midi_typist_services PUBLIC midi_typist_app)
target_compile_options(midi_typist_services PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(midi_typist_app PUBLIC
    MT_BUILD_VERSION="${PROJECT_VERSION}" MT_BUILD_TARGET="MG-M1V5TMR")
if(CMAKE_CROSSCOMPILING)
    set(at32 ${CMAKE_CURRENT_SOURCE_DIR}/third_party/artery)
    if(NOT EXISTS ${at32}/libraries/drivers/src/at32f402_405_adc.c)
        message(FATAL_ERROR "Initialize the pinned SDK: git submodule update --init third_party/artery")
    endif()
    execute_process(COMMAND git -C ${at32} rev-parse HEAD OUTPUT_VARIABLE at32_revision OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT at32_revision STREQUAL "5dd9d55a2ce9ffa8fe0cb2652ac142920f2094a3")
        message(FATAL_ERROR "Artery SDK revision differs from the reviewed pin")
    endif()
    add_library(at32_sdk STATIC)
    target_sources(at32_sdk PRIVATE ${at32}/libraries/cmsis/cm4/device_support/system_at32f402_405.c)
    foreach(module adc crm dma gpio spi tmr ertc exint pwc usb flash)
        target_sources(at32_sdk PRIVATE ${at32}/libraries/drivers/src/at32f402_405_${module}.c)
    endforeach()
    set_source_files_properties(${at32}/libraries/drivers/src/at32f402_405_flash.c
        PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_SOURCE_DIR}/firmware/platform/at32f405/include/m1_flash_sdk_config.h")
    foreach(module usb_core usbd_core usbd_sdr usbd_int)
        target_sources(at32_sdk PRIVATE ${at32}/middlewares/usb_drivers/src/${module}.c)
    endforeach()
    target_include_directories(at32_sdk PUBLIC firmware/platform/at32f405/include firmware/app/include)
    target_include_directories(at32_sdk SYSTEM PUBLIC
        ${at32}/libraries/cmsis/cm4/core_support
        ${at32}/libraries/cmsis/cm4/device_support ${at32}/libraries/drivers/inc
        ${at32}/middlewares/usb_drivers/inc)
    # SDK family selector; not a claim that this package/density was measured.
    target_compile_definitions(at32_sdk PUBLIC AT32F405RCT7 HEXT_VALUE=12000000)
    add_library(m1_storage STATIC ${MT_BOARD_DIR}/src/m1_storage.c)
    target_include_directories(m1_storage PUBLIC ${MT_BOARD_DIR}/include)
    target_link_libraries(m1_storage PUBLIC midi_typist_services m1_board at32_sdk)
    target_compile_options(m1_storage PRIVATE -Wall -Wextra -Werror)
    # The storage writer's quiescence gate asks the radio module whether its bus
    # is idle, so this standalone audit needs the same radio source as the
    # firmware configuration it runs against.
    add_executable(m1_storage_audit tests/m1_storage_audit.c ${MT_M1_WIRELESS_SOURCES})
    set_target_properties(m1_storage_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_storage_audit PRIVATE m1_storage)
    target_link_options(m1_storage_audit PRIVATE -nostartfiles --specs=nosys.specs
        # The audit asks the artifact which feature set it was built with.
        -Wl,--undefined=m1_wireless_supported
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        -Wl,-L,${CMAKE_SOURCE_DIR} -T${CMAKE_SOURCE_DIR}/tests/m1_storage_audit.ld)
    set_property(TARGET m1_storage_audit APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/m1_storage_audit.ld ${MT_BOARD_DIR}/linker/storage_ram.ld)
    add_library(m1_hal STATIC ${MT_BOARD_DIR}/src/m1_hal.c ${MT_BOARD_DIR}/src/m1_lighting_hal.c
        ${MT_BOARD_DIR}/src/m1_encoder_hal.c
        ${MT_BOARD_DIR}/src/m1_clock.c ${MT_BOARD_DIR}/src/m1_time.c ${MT_BOARD_DIR}/src/m1_startup.c ${MT_BOARD_DIR}/src/m1_factory_hal.c
        ${MT_BOARD_DIR}/src/m1_battery_hal.c ${MT_BOARD_DIR}/src/m1_sleep.c ${MT_BOARD_DIR}/src/m1_sleep_time.c ${MT_BOARD_DIR}/src/m1_power_gpio.c
        ${MT_M1_WIRELESS_SOURCES} ${MT_BOARD_DIR}/src/m1_usb_power.c
        ${MT_BOARD_DIR}/src/m1_usb_class.c ${MT_BOARD_DIR}/src/m1_usb_descriptors.c
        ${MT_BOARD_DIR}/src/m1_usb_hal.c)
    target_link_libraries(m1_hal PUBLIC m1_board at32_sdk)
    target_compile_options(m1_hal PRIVATE -Wall -Wextra -Werror)
    target_link_options(m1_hal INTERFACE -Wl,--wrap=usbd_endpoint_request -Wl,--wrap=usbd_device_request
        -Wl,--wrap=usb_global_init -Wl,--wrap=usb_connect)
    add_library(m1_live STATIC ${MT_BOARD_DIR}/src/m1_live.c ${MT_BOARD_DIR}/src/m1_transport.c
        ${MT_BOARD_DIR}/src/m1_runtime_power.c ${MT_BOARD_DIR}/src/m1_source.c)
    add_library(m1_save STATIC ${MT_BOARD_DIR}/src/m1_save.c)
    target_link_libraries(m1_save PUBLIC m1_hal)
    target_compile_options(m1_save PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(m1_live PUBLIC m1_save m1_hal m1_storage midi_typist_services)
    target_compile_options(m1_live PRIVATE -Wall -Wextra -Werror)
    add_library(m1_boot STATIC ${MT_BOARD_DIR}/src/m1_boot.c ${MT_BOARD_DIR}/src/m1_diagnostics.c)
    target_link_libraries(m1_boot PUBLIC m1_live)
    target_compile_options(m1_boot PRIVATE -Wall -Wextra -Werror)
    # Application-only experimental image; never package boot/factory pages.
    add_executable(m1_development ${MT_BOARD_DIR}/src/m1_entry.c ${MT_BOARD_DIR}/src/m1_main.c)
    set_target_properties(m1_development PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_development PRIVATE m1_boot)
    target_compile_options(m1_development PRIVATE -Wall -Wextra -Werror -ffreestanding -fno-builtin)
    target_link_options(m1_development PRIVATE -nostartfiles --specs=nosys.specs
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        -Wl,-L,${CMAKE_SOURCE_DIR} -Wl,-Map,${CMAKE_BINARY_DIR}/m1_development.map
        -T${MT_BOARD_DIR}/linker/application.ld)
    set_property(TARGET m1_development APPEND PROPERTY LINK_DEPENDS
        ${MT_BOARD_DIR}/linker/application.ld ${MT_BOARD_DIR}/linker/storage_ram.ld)
    add_custom_command(TARGET m1_development POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary --gap-fill=0xff
            $<TARGET_FILE:m1_development> ${CMAKE_BINARY_DIR}/m1_development.bin
        BYPRODUCTS ${CMAKE_BINARY_DIR}/m1_development.bin VERBATIM)
    add_executable(m1_boot_audit tests/m1_hal_audit.c tests/m1_boot_audit.c)
    set_target_properties(m1_boot_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_boot_audit PRIVATE m1_boot)
    target_link_options(m1_boot_audit PRIVATE -nostartfiles --specs=nosys.specs
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        -Wl,--wrap=m1_storage_read -Wl,--wrap=m1_storage_write
        # The audit asks the artifact which feature set it was built with.
        -Wl,--undefined=m1_wireless_supported
        -Wl,--wrap=m1_storage_erase -Wl,--wrap=m1_storage_check_recovery
        -T${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    set_property(TARGET m1_boot_audit APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    add_executable(m1_hal_audit tests/m1_hal_audit.c)
    set_target_properties(m1_hal_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_hal_audit PRIVATE m1_hal midi_typist_app)
    target_link_options(m1_hal_audit PRIVATE -nostartfiles --specs=nosys.specs
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        # The audit asks the artifact whether the wireless feature was built in;
        # keep the capability answer reachable for a build that links no
        # wireless consumer of it.
        -Wl,--undefined=m1_wireless_supported
        -T${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    set_property(TARGET m1_hal_audit APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    add_executable(m1_usb_audit tests/m1_usb_audit.c)
    set_target_properties(m1_usb_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_usb_audit PRIVATE m1_hal midi_typist_services)
    target_link_options(m1_usb_audit PRIVATE -nostartfiles --specs=nosys.specs
        # The audit asks the artifact which feature set it was built with.
        -Wl,--undefined=m1_wireless_supported
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        # The audit asks the artifact which feature set it was built with.
        -Wl,--undefined=m1_wireless_supported
        -Wl,-e,m1_test_usb_init -T${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    set_property(TARGET m1_usb_audit APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    add_executable(m1_live_audit tests/m1_usb_audit.c tests/m1_live_audit.c)
    set_target_properties(m1_live_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_live_audit PRIVATE m1_live)
    target_link_options(m1_live_audit PRIVATE -nostartfiles --specs=nosys.specs
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        -Wl,-e,m1_test_usb_init -T${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    set_property(TARGET m1_live_audit APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    foreach(symbol m1_hal_periodic_active m1_hal_frame m1_hal_service m1_hal_errors m1_hal_queue_state
        m1_battery_hal_service m1_battery_hal_status m1_lighting_service m1_lighting_ready
        m1_lighting_healthy m1_lighting_errors m1_lighting_offer m1_storage_read m1_storage_write
        m1_storage_erase m1_storage_check_recovery)
        target_link_options(m1_live_audit PRIVATE -Wl,--wrap=${symbol})
    endforeach()
    add_executable(m1_save_audit tests/m1_save_audit.c)
    set_target_properties(m1_save_audit PROPERTIES SUFFIX ".elf")
    target_link_libraries(m1_save_audit PRIVATE m1_save)
    target_link_options(m1_save_audit PRIVATE -nostartfiles --specs=nosys.specs
        # The audit asks the artifact which feature set it was built with.
        -Wl,--undefined=m1_wireless_supported
        -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Wl,--gc-sections
        -T${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    set_property(TARGET m1_save_audit APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tests/m1_hal_audit.ld)
    foreach(symbol m1_live_transport m1_usb_ready m1_usb_drained
        m1_wireless_mode m1_wireless_ready m1_wireless_local_idle m1_wireless_healthy)
        target_link_options(m1_save_audit PRIVATE -Wl,--wrap=${symbol})
    endforeach()
    foreach(target midi_typist_app midi_typist_services m1_board at32_sdk m1_hal m1_live m1_boot m1_development m1_boot_audit m1_hal_audit m1_usb_audit m1_live_audit m1_storage m1_storage_audit m1_save m1_save_audit)
        target_compile_options(${target} PRIVATE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections)
    endforeach()
else()
    enable_testing()
    add_executable(m1_encoder_tests tests/test_keyboard_encoder.c)
    target_link_libraries(m1_encoder_tests PRIVATE midi_typist_app)
    target_compile_options(m1_encoder_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME m1_encoder COMMAND m1_encoder_tests)
    add_executable(m1_device_store_tests tests/test_device_store.c)
    target_link_libraries(m1_device_store_tests PRIVATE midi_typist_services m1_board)
    target_compile_options(m1_device_store_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME m1_device_store COMMAND m1_device_store_tests)
    add_executable(m1_board_tests tests/test_m1_board.c)
    target_link_libraries(m1_board_tests PRIVATE m1_board midi_typist_app)
    target_compile_options(m1_board_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    add_test(NAME m1_board COMMAND m1_board_tests)
    add_executable(m1_telemetry_fixture tests/m1_telemetry_fixture.c)
    target_link_libraries(m1_telemetry_fixture PRIVATE midi_typist_services m1_board)
    target_compile_options(m1_telemetry_fixture PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME m1_telemetry COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/test_m1_telemetry.py $<TARGET_FILE:m1_telemetry_fixture>)
endif()
