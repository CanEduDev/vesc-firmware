/*
    Copyright 2025 CanEduDev AB

    This file is part of the VESC firmware.

    The VESC firmware is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    The VESC firmware is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
    more details.

    You should have received a copy of the GNU General Public License along with
    this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "app.h"
#include "chthreads.h"
#include "hal.h"

// Some useful includes
#include "commands.h"
#include "mc_interface.h"
#include "nvic.h"
#include "terminal.h"
#include "timeout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Threads
static THD_FUNCTION(my_thread, arg);
static THD_WORKING_AREA(my_thread_wa, 1024);

// Private functions
static void pwm_callback(void);
static void terminal_test(int argc, const char **argv);

// Private variables
static volatile bool stop_now = true;
static volatile bool is_running = false;

#define SPI1_IRQ_PRIORITY 10

static mailbox_t spi_opcode_mailbox;

// Shared data between thread and ISR - protect with critical sections if
// needed, but simple flags/counters accessed atomically might be okay.
static uint16_t spi_response_buffer[4];
static volatile uint8_t spi_response_len = 0;
static volatile uint8_t spi_response_idx = 0;
static volatile bool spi_response_ready = false;

// Called when the custom application is started. Start our
// threads here and set up callbacks.
void app_custom_start(void) {
  // Start PPM app. TODO: how to switch between PPM and CAN app during runtime?
  app_ppm_start();

  mc_interface_set_pwm_callback(pwm_callback);

  // SPI
  rccEnableSPI1(FALSE);

  palSetPadMode(HW_SPI_PORT_NSS, HW_SPI_PIN_NSS,
                PAL_MODE_ALTERNATE(HW_SPI_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST |
                    PAL_STM32_PUDR_PULLUP);
  palSetPadMode(HW_SPI_PORT_SCK, HW_SPI_PIN_SCK,
                PAL_MODE_ALTERNATE(HW_SPI_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST |
                    PAL_STM32_PUDR_PULLDOWN);
  palSetPadMode(HW_SPI_PORT_MOSI, HW_SPI_PIN_MOSI,
                PAL_MODE_ALTERNATE(HW_SPI_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST);
  palSetPadMode(HW_SPI_PORT_MISO, HW_SPI_PIN_MISO,
                PAL_MODE_ALTERNATE(HW_SPI_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST |
                    PAL_STM32_OTYPE_PUSHPULL);

  // ChibiOS 3.0.5 doesn't support SPI in slave mode, so we roll our own.
  SPI_TypeDef *spi = HW_SPI_DEV.spi;
  spi->CR1 &= ~SPI_CR1_SPE;

  spi->CR2 = SPI_CR2_RXNEIE | SPI_CR2_ERRIE;

  spi->CR1 |= SPI_CR1_SPE;

  nvicEnableVector(SPI1_IRQn, SPI1_IRQ_PRIORITY);

  stop_now = false;
  chThdCreateStatic(my_thread_wa, sizeof(my_thread_wa), NORMALPRIO, my_thread,
                    NULL);

  // Terminal commands for the VESC Tool terminal can be registered.
  terminal_register_command_callback("custom_cmd", "Print the number d", "[d]",
                                     terminal_test);
}

// Called when the custom application is stopped. Stop our threads
// and release callbacks.
void app_custom_stop(void) {
  mc_interface_set_pwm_callback(0);
  terminal_unregister_callback(terminal_test);

  stop_now = true;
  while (is_running) {
    chThdSleepMilliseconds(1);
  }
}

void app_custom_configure(app_configuration *conf) { (void)conf; }

static THD_FUNCTION(my_thread, arg) {
  (void)arg;

  chRegSetThreadName("App Custom");

  is_running = true;

  // msg_t opcode_msg;
  // uint8_t opcode;

  SPI_TypeDef *spi = HW_SPI_DEV.spi;

  for (;;) {
    // Check if it is time to stop.
    if (stop_now) {
      is_running = false;
      return;
    }

    timeout_reset(); // Reset timeout if everything is OK.

    // if (spi->SR & SPI_SR_RXNE) {
    //   uint8_t opcode = spi->DR;
    //   switch (opcode) {

    //  case 0x01:
    //    while (!(spi->SR & SPI_SR_TXE)) {
    //    }
    //    spi->DR = 0xA;
    //    break;
    //  }
    //}

    chThdSleepMilliseconds(1);

    // Run your logic here. A lot of functionality is available in
    // mc_interface.h.

    // if (chMBFetch(&spi_opcode_mailbox, &opcode_msg, MS2ST(100)) == MSG_OK) {
    //   opcode = (uint8_t)opcode_msg;

    //  // --- Process the opcode and prepare response ---
    //  spi_response_len = 0; // Default: no response
    //  switch (opcode) {
    //  case 0x01:                       // Example: Read Status
    //    spi_response_buffer[0] = 0xAABB; // Some status
    //    spi_response_len = 1;
    //    break;
    //  case 0x02: // Example: Read Sensor
    //    // sensor_value = read_some_sensor(); // Might block
    //    // spi_response_buffer[0] = (sensor_value >> 8) & 0xFF;
    //    // spi_response_buffer[1] = sensor_value & 0xFF;
    //    // spi_response_len = 2;
    //    break;
    //  // Add other opcodes
    //  default:
    //    // Unknown opcode, maybe prepare an error response?
    //    spi_response_buffer[0] = 0xFF; // Error code
    //    spi_response_len = 1;
    //    break;
    //  }

    //  // --- Make response ready for transmission via TXE interrupt ---
    //  if (spi_response_len > 0) {
    //    spi_response_idx = 0; // Start from the beginning of the buffer
    //    spi_response_ready = true;

    //    // Critical section to safely enable TXEIE
    //    chSysLock();
    //    spi->CR2 |= SPI_CR2_TXEIE; // Enable TXE interrupt
    //    chSysUnlock();
    //  }
    //}
  }
}

static void pwm_callback(void) {
  // Called for every control iteration in interrupt context.
}

// Callback function for the terminal command with arguments.
static void terminal_test(int argc, const char **argv) {
  if (argc == 2) {
    int d = -1;
    sscanf(argv[1], "%d", &d);

    commands_printf("You have entered %d", d);

    // For example, read the ADC inputs on the COMM header.
    commands_printf("ADC1: %.2f V ADC2: %.2f V", (double)ADC_VOLTS(ADC_IND_EXT),
                    (double)ADC_VOLTS(ADC_IND_EXT2));
  } else {
    commands_printf("This command requires one argument.\n");
  }
}

OSAL_IRQ_HANDLER(SPI1_IRQHandler) {

  CH_IRQ_PROLOGUE();
  //
  //   // Use the peripheral pointer (assuming SPID1 is initialized)
  SPI_TypeDef *spi = HW_SPI_DEV.spi;
  //
  // Check for Receive Buffer Not Empty
  if (spi->SR & SPI_SR_RXNE) {
    // --- Action for received data ---
    uint8_t rx = spi->DR; // Reading DR clears the RXNE flag
    if (spi->SR & SPI_SR_TXE) {
      spi->DR = rx + 1;
    }

    // --- Send opcode to the processing thread ---
    //     // Non-blocking post (important!) Check return code in thread if
    //     needed.
    //     // If mailbox is full, the oldest message might be dropped, or it
    //     returns
    //     // Q_FULL. Consider error handling strategy (e.g., log error, discard
    //     // opcode).
    //     (void)chMBPostI(&spi_opcode_mailbox, msg);
  }
  //
  //   if ((sr & SPI_SR_TXE) && (spi->CR2 & SPI_CR2_TXEIE)) {
  //     if (spi_response_ready && spi_response_idx < spi_response_len) {
  //       // Send next byte/word
  //       spi->DR = spi_response_buffer[spi_response_idx++];
  //
  //       // Check if last byte/word was sent
  //       if (spi_response_idx >= spi_response_len) {
  //         spi_response_ready = false; // Mark response as sent
  //         // Disable TXE interrupt ONLY IF response is fully sent
  //         spi->CR2 &= ~SPI_CR2_TXEIE;
  //       }
  //     } else {
  //       // TXE fired but we have nothing ready (or already sent)?
  //       // This can happen if master keeps clocking. Disable TXEIE to prevent
  //       // continuous interrupts if nothing more is expected immediately.
  //       spi->CR2 &= ~SPI_CR2_TXEIE;
  //       spi_response_ready = false; // Ensure state is clean
  //     }
  //   }
  //
  //   // Check for Overrun Error (after reading DR if RXNE was also set)
  //   if (sr & SPI_SR_OVR) {
  //     // Clear OVR flag: Reading DR (already done if RXNE was set) and
  //     reading SR (void)spi->DR; // Ensure DR is read if RXNE wasn't set
  //     (void)spi->SR;
  //     // Read SR clears OVR
  //   }
  //
  CH_IRQ_EPILOGUE();
}
