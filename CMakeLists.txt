cmake_minimum_required(VERSION 3.20)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards/include/boards" CACHE STRING "Local board definitions")
set(PICO_BOARD meowkey_rp2350_usb CACHE STRING "Target RP2350 board")
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk" CACHE PATH "Path to pico-sdk")
set(MEOWKEY_BOOT_STAGE2 "boot2_generic_03h" CACHE STRING "RP2350 boot stage2 implementation")
set(MEOWKEY_FLASH_SIZE_BYTES "" CACHE STRING "Override flash size in bytes")
set(MEOWKEY_CREDENTIAL_CAPACITY 0 CACHE STRING "Maximum number of stored credentials; 0 means auto")
set(MEOWKEY_CREDENTIAL_STORE_SECTORS 16 CACHE STRING "Number of flash sectors reserved for credential storage")
set(MEOWKEY_BOARD_ID_MODE "none" CACHE STRING "Board identification mode: none, gpio, i2c-eeprom")
set(MEOWKEY_BOARD_ID_GPIO_PINS "" CACHE STRING "Comma-separated GPIO board ID pins, least significant bit first")
option(MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW "Interpret a low GPIO level as a logical 1 for board ID pins" ON)
set(MEOWKEY_BOARD_ID_I2C_PRESET "24c02" CACHE STRING "I2C board ID preset label for diagnostics")
set(MEOWKEY_BOARD_ID_I2C_INSTANCE 0 CACHE STRING "I2C instance for board ID EEPROM")
set(MEOWKEY_BOARD_ID_I2C_SDA_PIN 4 CACHE STRING "I2C SDA pin for board ID EEPROM")
set(MEOWKEY_BOARD_ID_I2C_SCL_PIN 5 CACHE STRING "I2C SCL pin for board ID EEPROM")
set(MEOWKEY_BOARD_ID_I2C_ADDRESS 0x50 CACHE STRING "I2C 7-bit address for board ID EEPROM")
set(MEOWKEY_BOARD_ID_I2C_MEM_OFFSET 0 CACHE STRING "I2C EEPROM offset for board ID bytes")
set(MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH 1 CACHE STRING "EEPROM internal address width in bytes")
set(MEOWKEY_BOARD_ID_I2C_READ_LENGTH 8 CACHE STRING "Number of board ID bytes to read from EEPROM")

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(MeowKey C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_DEFAULT_BOOT_STAGE2 "${MEOWKEY_BOOT_STAGE2}" CACHE STRING "Override RP2350 boot stage2" FORCE)
set(PICO_MBEDTLS_CONFIG_FILE "meowkey_mbedtls_config.h" CACHE STRING "Custom mbedtls config for embedded build" FORCE)

if (NOT "${MEOWKEY_FLASH_SIZE_BYTES}" STREQUAL "")
    set(PICO_FLASH_SIZE_BYTES "${MEOWKEY_FLASH_SIZE_BYTES}" CACHE STRING "Override flash size from build script" FORCE)
endif()

option(MEOWKEY_ENABLE_SIGNED_BOOT "Generate signed RP2350 images with a local key" OFF)
option(MEOWKEY_ENABLE_DEBUG_HID "Expose the development-only debug HID interface" ON)
set(_MEOWKEY_DANGEROUS_DEBUG_DEFAULT ON)
if (NOT MEOWKEY_ENABLE_DEBUG_HID)
    set(_MEOWKEY_DANGEROUS_DEBUG_DEFAULT OFF)
endif()
option(MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS "Compile dangerous debug credential-management commands" ${_MEOWKEY_DANGEROUS_DEBUG_DEFAULT})
set(MEOWKEY_VERSION_MAJOR 0 CACHE STRING "MeowKey firmware major version")
set(MEOWKEY_VERSION_MINOR 1 CACHE STRING "MeowKey firmware minor version")
set(MEOWKEY_VERSION_PATCH 0 CACHE STRING "MeowKey firmware patch version")
set(MEOWKEY_VERSION_LABEL "dev" CACHE STRING "Optional firmware version suffix")
set(MEOWKEY_UP_DEFAULT_SOURCE "none" CACHE STRING "Default user-presence source: none, bootsel, gpio")
set_property(CACHE MEOWKEY_UP_DEFAULT_SOURCE PROPERTY STRINGS none bootsel gpio)
set(MEOWKEY_UP_GPIO_PIN -1 CACHE STRING "Default dedicated user-presence GPIO pin; -1 keeps BOOTSEL as the default")
option(MEOWKEY_UP_GPIO_ACTIVE_LOW "Interpret the dedicated user-presence GPIO as active-low" ON)
set(MEOWKEY_UP_TAP_COUNT 2 CACHE STRING "Number of presses required for user presence")
set(MEOWKEY_UP_GESTURE_WINDOW_MS 750 CACHE STRING "Window for multi-tap user presence gestures")
set(MEOWKEY_UP_REQUEST_TIMEOUT_MS 8000 CACHE STRING "Maximum wait time for user presence requests")

if (MEOWKEY_CREDENTIAL_CAPACITY LESS 0)
    message(FATAL_ERROR "MEOWKEY_CREDENTIAL_CAPACITY must be 0 or greater")
endif()

if (MEOWKEY_CREDENTIAL_STORE_SECTORS LESS 1)
    message(FATAL_ERROR "MEOWKEY_CREDENTIAL_STORE_SECTORS must be at least 1")
endif()

if (MEOWKEY_CREDENTIAL_STORE_SECTORS LESS 4)
    message(FATAL_ERROR "MEOWKEY_CREDENTIAL_STORE_SECTORS must be at least 4 because MeowKey reserves 2 sectors for the sign-count journal and 2 sectors for A/B transactional slots")
endif()

math(EXPR MEOWKEY_STORE_DATA_SECTORS "${MEOWKEY_CREDENTIAL_STORE_SECTORS} - 2")
math(EXPR MEOWKEY_STORE_DATA_SECTORS_REMAINDER "${MEOWKEY_STORE_DATA_SECTORS} % 2")
if (NOT MEOWKEY_STORE_DATA_SECTORS_REMAINDER EQUAL 0)
    message(FATAL_ERROR "MEOWKEY_CREDENTIAL_STORE_SECTORS must leave an even number of data sectors after reserving the 2-sector sign-count journal")
endif()

string(TOLOWER "${MEOWKEY_BOARD_ID_MODE}" MEOWKEY_BOARD_ID_MODE_NORMALIZED)
if (MEOWKEY_BOARD_ID_MODE_NORMALIZED STREQUAL "none")
    set(MEOWKEY_BOARD_ID_MODE_CODE 0)
elseif (MEOWKEY_BOARD_ID_MODE_NORMALIZED STREQUAL "gpio")
    set(MEOWKEY_BOARD_ID_MODE_CODE 1)
elseif (MEOWKEY_BOARD_ID_MODE_NORMALIZED STREQUAL "i2c-eeprom")
    set(MEOWKEY_BOARD_ID_MODE_CODE 2)
else()
    message(FATAL_ERROR "Unsupported MEOWKEY_BOARD_ID_MODE='${MEOWKEY_BOARD_ID_MODE}'. Use none, gpio, or i2c-eeprom")
endif()

set(MEOWKEY_BOARD_ID_GPIO_PIN0 -1)
set(MEOWKEY_BOARD_ID_GPIO_PIN1 -1)
set(MEOWKEY_BOARD_ID_GPIO_PIN2 -1)
set(MEOWKEY_BOARD_ID_GPIO_PIN3 -1)
if (MEOWKEY_BOARD_ID_MODE_NORMALIZED STREQUAL "gpio")
    if ("${MEOWKEY_BOARD_ID_GPIO_PINS}" STREQUAL "")
        message(FATAL_ERROR "MEOWKEY_BOARD_ID_GPIO_PINS is required when MEOWKEY_BOARD_ID_MODE=gpio")
    endif()
    string(REPLACE "," ";" MEOWKEY_BOARD_ID_GPIO_PIN_LIST "${MEOWKEY_BOARD_ID_GPIO_PINS}")
    list(LENGTH MEOWKEY_BOARD_ID_GPIO_PIN_LIST MEOWKEY_BOARD_ID_GPIO_PIN_COUNT)
    if (MEOWKEY_BOARD_ID_GPIO_PIN_COUNT GREATER 4)
        message(FATAL_ERROR "MEOWKEY_BOARD_ID_GPIO_PINS supports up to 4 pins")
    endif()
    math(EXPR MEOWKEY_BOARD_ID_GPIO_LAST_INDEX "${MEOWKEY_BOARD_ID_GPIO_PIN_COUNT} - 1")
    if (MEOWKEY_BOARD_ID_GPIO_LAST_INDEX GREATER -1)
        foreach (MEOWKEY_BOARD_ID_GPIO_INDEX RANGE 0 ${MEOWKEY_BOARD_ID_GPIO_LAST_INDEX})
            list(GET MEOWKEY_BOARD_ID_GPIO_PIN_LIST ${MEOWKEY_BOARD_ID_GPIO_INDEX} MEOWKEY_BOARD_ID_GPIO_PIN_VALUE)
            string(STRIP "${MEOWKEY_BOARD_ID_GPIO_PIN_VALUE}" MEOWKEY_BOARD_ID_GPIO_PIN_VALUE)
            set(MEOWKEY_BOARD_ID_GPIO_PIN${MEOWKEY_BOARD_ID_GPIO_INDEX} "${MEOWKEY_BOARD_ID_GPIO_PIN_VALUE}")
        endforeach()
    endif()
endif()

if (MEOWKEY_BOARD_ID_MODE_NORMALIZED STREQUAL "i2c-eeprom")
    if (NOT (MEOWKEY_BOARD_ID_I2C_INSTANCE EQUAL 0 OR MEOWKEY_BOARD_ID_I2C_INSTANCE EQUAL 1))
        message(FATAL_ERROR "MEOWKEY_BOARD_ID_I2C_INSTANCE must be 0 or 1")
    endif()
    if (MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH LESS 1 OR MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH GREATER 2)
        message(FATAL_ERROR "MEOWKEY_BOARD_ID_I2C_MEM_ADDRESS_WIDTH must be 1 or 2")
    endif()
    if (MEOWKEY_BOARD_ID_I2C_READ_LENGTH LESS 1 OR MEOWKEY_BOARD_ID_I2C_READ_LENGTH GREATER 16)
        message(FATAL_ERROR "MEOWKEY_BOARD_ID_I2C_READ_LENGTH must be between 1 and 16")
    endif()
endif()

if (MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW)
    set(MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW_INT 1)
else()
    set(MEOWKEY_BOARD_ID_GPIO_ACTIVE_LOW_INT 0)
endif()

if (MEOWKEY_ENABLE_DEBUG_HID)
    set(MEOWKEY_ENABLE_DEBUG_HID_INT 1)
else()
    set(MEOWKEY_ENABLE_DEBUG_HID_INT 0)
endif()

if (MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS)
    if (NOT MEOWKEY_ENABLE_DEBUG_HID)
        message(FATAL_ERROR "MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS requires MEOWKEY_ENABLE_DEBUG_HID=ON")
    endif()
    set(MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS_INT 1)
else()
    set(MEOWKEY_ENABLE_DANGEROUS_DEBUG_COMMANDS_INT 0)
endif()

string(TOLOWER "${MEOWKEY_UP_DEFAULT_SOURCE}" MEOWKEY_UP_DEFAULT_SOURCE_NORMALIZED)
if (MEOWKEY_UP_DEFAULT_SOURCE_NORMALIZED STREQUAL "none")
    set(MEOWKEY_DEFAULT_UP_SOURCE_CODE 0)
elseif (MEOWKEY_UP_DEFAULT_SOURCE_NORMALIZED STREQUAL "bootsel")
    set(MEOWKEY_DEFAULT_UP_SOURCE_CODE 1)
elseif (MEOWKEY_UP_DEFAULT_SOURCE_NORMALIZED STREQUAL "gpio")
    set(MEOWKEY_DEFAULT_UP_SOURCE_CODE 2)
else()
    message(FATAL_ERROR "Unsupported MEOWKEY_UP_DEFAULT_SOURCE='${MEOWKEY_UP_DEFAULT_SOURCE}'. Use none, bootsel, or gpio")
endif()

if (MEOWKEY_DEFAULT_UP_SOURCE_CODE EQUAL 2 AND MEOWKEY_UP_GPIO_PIN LESS 0)
    message(FATAL_ERROR "MEOWKEY_UP_GPIO_PIN must be >= 0 when MEOWKEY_UP_DEFAULT_SOURCE=gpio")
endif()

if (MEOWKEY_UP_TAP_COUNT LESS 1 OR MEOWKEY_UP_TAP_COUNT GREATER 4)
    message(FATAL_ERROR "MEOWKEY_UP_TAP_COUNT must be between 1 and 4")
endif()

if (MEOWKEY_UP_GESTURE_WINDOW_MS LESS 100 OR MEOWKEY_UP_GESTURE_WINDOW_MS GREATER 5000)
    message(FATAL_ERROR "MEOWKEY_UP_GESTURE_WINDOW_MS must be between 100 and 5000")
endif()

if (MEOWKEY_UP_REQUEST_TIMEOUT_MS LESS 500 OR MEOWKEY_UP_REQUEST_TIMEOUT_MS GREATER 30000)
    message(FATAL_ERROR "MEOWKEY_UP_REQUEST_TIMEOUT_MS must be between 500 and 30000")
endif()

if (MEOWKEY_UP_GPIO_ACTIVE_LOW)
    set(MEOWKEY_UP_GPIO_ACTIVE_LOW_INT 1)
else()
    set(MEOWKEY_UP_GPIO_ACTIVE_LOW_INT 0)
endif()

if (MEOWKEY_ENABLE_DEBUG_HID)
    set(MEOWKEY_PRODUCT_NAME "MeowKey DEV UNSAFE")
    set(MEOWKEY_USB_PRODUCT_NAME "MeowKey DEV UNSAFE RP2350")
else()
    set(MEOWKEY_PRODUCT_NAME "MeowKey Hardened")
    set(MEOWKEY_USB_PRODUCT_NAME "MeowKey Hardened RP2350")
endif()

set(MEOWKEY_PROGRAM_VERSION "${MEOWKEY_VERSION_MAJOR}.${MEOWKEY_VERSION_MINOR}.${MEOWKEY_VERSION_PATCH}")
if (NOT "${MEOWKEY_VERSION_LABEL}" STREQUAL "")
    set(MEOWKEY_PROGRAM_VERSION "${MEOWKEY_PROGRAM_VERSION}-${MEOWKEY_VERSION_LABEL}")
endif()

set(MEOWKEY_BUILD_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MEOWKEY_BUILD_CONFIG_DIR}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/src/meowkey_build_config.h.in"
    "${MEOWKEY_BUILD_CONFIG_DIR}/meowkey_build_config.h"
    @ONLY
)

pico_sdk_init()

add_executable(meowkey
    src/board_id.c
    src/cbor.c
    src/client_pin.c
    src/credential_store.c
    src/ctap2.c
    src/ctap_hid.c
    src/diagnostics.c
    src/main.c
    src/mbedtls_compat.c
    src/user_presence.c
    src/usb_descriptors.c
    src/webauthn.c
)

add_executable(meowkey_probe EXCLUDE_FROM_ALL
    src/board_probe.c
    src/probe_main.c
)

target_include_directories(meowkey PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${MEOWKEY_BUILD_CONFIG_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/lib/mbedtls/include
    ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/lib/mbedtls/include/mbedtls
)

target_include_directories(meowkey_probe PRIVATE
    ${MEOWKEY_BUILD_CONFIG_DIR}
)

target_link_libraries(meowkey PRIVATE
    hardware_flash
    hardware_i2c
    pico_rand
    pico_stdlib
    pico_mbedtls_crypto
    pico_mbedtls_headers
    pico_status_led
    pico_unique_id
    tinyusb_device
    tinyusb_board
)

target_link_libraries(meowkey_probe PRIVATE
    hardware_i2c
    pico_stdlib
    pico_unique_id
)

target_compile_definitions(meowkey PRIVATE
    MBEDTLS_NO_PLATFORM_ENTROPY
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
)

target_compile_definitions(meowkey_probe PRIVATE
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
)

pico_set_program_name(meowkey "${MEOWKEY_PRODUCT_NAME}")
pico_set_program_version(meowkey "${MEOWKEY_PROGRAM_VERSION}")
pico_enable_stdio_uart(meowkey 0)
pico_enable_stdio_usb(meowkey 0)
pico_set_binary_version(meowkey
    MAJOR ${MEOWKEY_VERSION_MAJOR}
    MINOR ${MEOWKEY_VERSION_MINOR}
)

pico_set_program_name(meowkey_probe "MeowKey Probe")
pico_set_program_version(meowkey_probe "${MEOWKEY_PROGRAM_VERSION}")
pico_enable_stdio_uart(meowkey_probe 0)
pico_enable_stdio_usb(meowkey_probe 1)
pico_set_binary_version(meowkey_probe
    MAJOR ${MEOWKEY_VERSION_MAJOR}
    MINOR ${MEOWKEY_VERSION_MINOR}
)

if (MEOWKEY_ENABLE_SIGNED_BOOT)
    set(MEOWKEY_SIGNING_KEY "${CMAKE_CURRENT_LIST_DIR}/keys/meowkey-secureboot.pem")
    if (EXISTS "${MEOWKEY_SIGNING_KEY}")
        pico_set_otp_key_output_file(meowkey "${CMAKE_CURRENT_BINARY_DIR}/meowkey.otp.json")
        pico_sign_binary(meowkey "${MEOWKEY_SIGNING_KEY}")
    else()
        message(WARNING "MEOWKEY_ENABLE_SIGNED_BOOT is ON but keys/meowkey-secureboot.pem is missing; building an unsigned image")
    endif()
endif()

pico_add_extra_outputs(meowkey)
pico_add_extra_outputs(meowkey_probe)
