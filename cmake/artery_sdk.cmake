# Pinned official AT32F402/405 BSP, untouched upstream source and licenses.
set(ARTERY_SDK "${CMAKE_SOURCE_DIR}/third_party/artery")
if(NOT EXISTS "${ARTERY_SDK}/libraries/drivers/src/at32f402_405_crm.c")
    message(FATAL_ERROR "Initialize official SDK: git submodule update --init third_party/artery")
endif()
add_library(artery_peripherals STATIC)
foreach(module crm gpio adc dma spi flash pwc usb)
    target_sources(artery_peripherals PRIVATE "${ARTERY_SDK}/libraries/drivers/src/at32f402_405_${module}.c")
endforeach()
target_sources(artery_peripherals PRIVATE
    "${ARTERY_SDK}/libraries/cmsis/cm4/device_support/system_at32f402_405.c")
target_include_directories(artery_peripherals PUBLIC
    "${CMAKE_SOURCE_DIR}/firmware/app/include"
    "${CMAKE_SOURCE_DIR}/firmware/platform/artery_at32f405/include"
    "${ARTERY_SDK}/libraries/drivers/inc"
    "${ARTERY_SDK}/libraries/cmsis/cm4/device_support"
    "${ARTERY_SDK}/libraries/cmsis/cm4/core_support")
target_compile_definitions(artery_peripherals PUBLIC AT32F405RCT7 HEXT_VALUE=12000000)
target_compile_options(artery_peripherals PUBLIC
    -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections)
target_link_options(artery_peripherals INTERFACE -mcpu=cortex-m4 -mthumb -mfloat-abi=soft)
add_library(artery_usb STATIC)
foreach(module usb_core usbd_core usbd_int usbd_sdr)
    target_sources(artery_usb PRIVATE "${ARTERY_SDK}/middlewares/usb_drivers/src/${module}.c")
endforeach()
target_include_directories(artery_usb PUBLIC "${ARTERY_SDK}/middlewares/usb_drivers/inc")
target_link_libraries(artery_usb PUBLIC artery_peripherals)
