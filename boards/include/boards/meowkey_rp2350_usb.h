/*
 * MeowKey custom board definition for the no-logo RP2350 USB board.
 *
 * Derived from seller documentation:
 * - RP2350A
 * - 16 MB flash
 * - On-board WS2812 status LED data on GPIO22
 * - USB-A male form factor
 */

#ifndef _BOARDS_MEOWKEY_RP2350_USB_H
#define _BOARDS_MEOWKEY_RP2350_USB_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define MEOWKEY_RP2350_USB

#define PICO_RP2350A 1

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif

#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif

#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 22
#endif

// Keep the status LED visible without being eye-searing.
#ifndef PICO_DEFAULT_COLORED_STATUS_LED_ON_COLOR
#define PICO_DEFAULT_COLORED_STATUS_LED_ON_COLOR 0x00040404
#endif

// Board-level LED policy overrides for src/main.c.
#ifndef MEOWKEY_LED_ENABLE_ACTIVE_STATE
#define MEOWKEY_LED_ENABLE_ACTIVE_STATE 1
#endif

#define PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 4
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
