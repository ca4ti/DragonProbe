option(USE_USBCDC_FOR_STDIO "Export an extra USB-CDC interface for stdio, instead of echoing it to a UART port (and requiring UART loopback for receiving stdio output on a host computer)." OFF)
set(FAMILY "rp2040" CACHE STRING "Board/MCU family, decides which drivers to use. Set to RP2040 by default.")
set(BOARD "raspberry_pi_pico" CACHE STRING "Board used, determines the pinout. Defaults to the Raspberry Pi Pico.")

# use directory name for project id
#get_filename_component(PROJECT "Dragon Probe" NAME)
set(PROJECT "DragonProbe")
set(PROJECT ${BOARD}-${PROJECT})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# TOP is absolute path to root directory of TinyUSB git repo
set(TOP "./tinyusb")
get_filename_component(TOP "${TOP}" REALPATH)

# Check for -DFAMILY=
if(FAMILY STREQUAL "rp2040")
  cmake_minimum_required(VERSION 3.12)

  option(USE_SYSTEMWIDE_PICOSDK "Use the systemwide Pico SDK instead of relying on the one from a deeply nested Git submodule (OFF by default)" OFF)
  option(PICO_NO_FLASH "Disable writing the compiled program to flash, and only load it to RAM. Useful for testing, but not much else (OFF by default)." OFF)
  option(PICO_COPY_TO_RAM "Run all code in RAM, while the program is also stored on flash. On bootup, everything will be copied to RAM (OFF by default)." OFF)

  if (USE_SYSTEMWIDE_PICOSDK)
    set(TOP "$ENV{PICO_SDK_PATH}/lib/tinyusb")
    get_filename_component(TOP "${TOP}" REALPATH)
    include(cmake/pico_sdk_import.cmake)
  else()
    set(PICO_SDK_PATH ${TOP}/hw/mcu/raspberrypi/pico-sdk)
    include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
  endif()

  include(${TOP}/hw/bsp/${FAMILY}/family.cmake) # tinyusb stuff

  family_get_project_name(PROJECT ${CMAKE_CURRENT_LIST_DIR})
  project(${PROJECT})
  family_initialize_project(PROJECT ${CMAKE_CURRENT_LIST_DIR}) # calls pico_sdk_init()
  #pico_sdk_init()

  #pico_set_program_name(${PROJECT} "${PROJECT}")
  #pico_set_program_version(${PROJECT} "0.1")

  add_executable(${PROJECT})

  # need uart stdio, usb is busy doing other stuff
  if(USE_USBCDC_FOR_STDIO)
    # we're going to manually implement this case
	#pico_enable_stdio_uart(${PROJECT} 0)
    target_compile_definitions(${PROJECT} PUBLIC USE_USBCDC_FOR_STDIO=1 PICO_STDIO_USB=1)
  else()
	  #pico_enable_stdio_uart(${PROJECT} 1)
  endif()
  pico_enable_stdio_uart(${PROJECT} 1)
  pico_enable_stdio_usb(${PROJECT} 0)
else()
  message(FATAL_ERROR "Invalid FAMILY '${FAMILY}' specified")
endif()

target_sources(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/no_malloc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/libco/libco.S
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/DAP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/JTAG_DP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/DAP_vendor.c
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/SWO.c
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/SW_DP.c
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Source/UART.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/unique.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/alloc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/modeset.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/thread.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tusb_plt.S
  ${CMAKE_CURRENT_SOURCE_DIR}/src/usb_descriptors.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/vnd_cfg.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_default/_default.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_default/cdc_serprog.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_default/tempsensor.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_default/vnd_i2ctinyusb.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_jscan/_jscan.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_jscan/jscan.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_sump/_sump.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/m_sump/cdc_sump.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/cdc_uart.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_jtag.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_swd.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_uart.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_swo.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/i2c_tinyusb.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/spi_serprog.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/tempsensor.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_jscan/jscan_hw.c
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_sump/sump_hw.c
)
if(USE_USBCDC_FOR_STDIO)
  target_sources(${PROJECT} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/cdc_stdio.c
  )
endif()
target_include_directories(${PROJECT} PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/
  ${CMAKE_CURRENT_SOURCE_DIR}/libco/
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Firmware/Include/
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Core/Include/
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/Driver/Include/
  ${CMAKE_CURRENT_SOURCE_DIR}/CMSIS-DAP/RTOS2/Include/
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/
  ${CMAKE_CURRENT_SOURCE_DIR}/bsp/default/
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror=implicit-function-declaration -Werror=return-type")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

add_custom_target(fix_db ALL WORKING_DIRECTORY ${OUTPUT_DIR}
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fix_clang_db.py")

if(FAMILY STREQUAL "rp2040")
  # NOTE: do NOT enable pico_runtime here, as it pulls in malloc!
  target_link_libraries(${PROJECT} pico_stdlib pico_unique_id hardware_spi
    hardware_i2c hardware_adc hardware_pio hardware_dma hardware_pwm
    pico_fix_rp2040_usb_device_enumeration
    tinyusb_device tinyusb_board tinyusb_additions)

  if(USE_USBCDC_FOR_STDIO)
    target_include_directories(${PROJECT} PUBLIC
      ${PICO_SDK_PATH}/src/rp2_common/pico_stdio_usb/include/
    )

    # extremely ugly hack to prevent the Pico SDK to declare *its* TinyUSB config
    # and also to modify the tinyusb board.h file a bit
    target_compile_definitions(${PROJECT} PUBLIC
      _PICO_STDIO_USB_TUSB_CONFIG_H=1
      BOARD_H_=1
    )

    target_link_libraries(${PROJECT} pico_stdio)
  endif()

  pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_jtag.pio)
  pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/dap_swd.pio)
  pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/swo_uart_rx.pio)
  pico_generate_pio_header(${PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/bsp/${FAMILY}/m_default/swo_manchester_encoding.pio)

  pico_add_extra_outputs(${PROJECT})

else()
  message(FATAL_ERROR "Invalid FAMILY '${FAMILY}' specified")
endif()
