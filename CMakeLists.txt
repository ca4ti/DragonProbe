# use directory name for project id
get_filename_component(PROJECT ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(PROJECT ${BOARD}-${PROJECT})

# TOP is absolute path to root directory of TinyUSB git repo
set(TOP "./tinyusb")
get_filename_component(TOP "${TOP}" REALPATH)

option(USE_SYSTEMWIDE_PICOSDK "Use the systemwide Pico SDK instead of relying on the one from a deeply nested Git submodule (OFF by default)" OFF)
# OPTION: PICO_NO_FLASH: load only into RAM (useful for testing)
# OPTION: PICO_COPY_TO_RAM: place the code/rodata into RAM at startup (while still stored in flash)
# TODO: add option for FAMILY and BOARD

# Check for -DFAMILY=
if(FAMILY STREQUAL "rp2040")
  cmake_minimum_required(VERSION 3.12)

  if (USE_SYSTEMWIDE_PICOSDK)
    include(pico_sdk_import.cmake)
  else()
    set(PICO_SDK_PATH ${TOP}/hw/mcu/raspberrypi/pico-sdk)
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
  endif()

  project(${PROJECT})
  pico_sdk_init()
  add_executable(${PROJECT})

  include(${TOP}/hw/bsp/${FAMILY}/family.cmake)

  # need uart stdio, usb is busy doing other stuff
  pico_enable_stdio_uart(${PROJECT} 1)
  pico_enable_stdio_usb(${PROJECT} 0)

  # Example source
  target_sources(${PROJECT} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Source/DAP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Source/JTAG_DP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Source/DAP_vendor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Source/SWO.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Source/SW_DP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/cdc_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/spi_serprog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cdc_serprog.c
  )

 # Example include
  target_include_directories(${PROJECT} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/DAP/Firmware/Include/
    ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS_5/CMSIS/Core/Include/
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/default/
  )

  # Example defines
  target_compile_definitions(${PROJECT} PUBLIC
  )

  target_link_libraries(${PROJECT} pico_stdlib pico_unique_id hardware_spi
    pico_fix_rp2040_usb_device_enumeration)

  pico_add_extra_outputs(${PROJECT})

else()
  message(FATAL_ERROR "Invalid FAMILY specified")
endif()
