/*
        Copyright 2019 Benjamin Vedder	benjamin@vedder.se

        This file is part of the VESC firmware.

        The VESC firmware is free software: you can redistribute it and/or
   modify it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "app.h"
#include "ch.h"
#include "hal.h"

// Some useful includes
#include "comm_can.h"
#include "commands.h"
#include "encoder/encoder.h"
#include "hw.h"
#include "mc_interface.h"
#include "terminal.h"
#include "timeout.h"
#include "utils_math.h"

#include "drv8301.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Threads
static THD_FUNCTION(my_thread, arg);
static THD_WORKING_AREA(my_thread_wa, 1024);

// Private functions
static void pwm_callback(void);
static void set_cs_low(int argc, const char **argv);
static void set_cs_high(int argc, const char **argv);

// Private variables
static volatile bool stop_now = true;
static volatile bool is_running = false;

// Called when the custom application is started. Start our
// threads here and set up callbacks.
void app_custom_start(void) {
  mc_interface_set_pwm_callback(pwm_callback);

  stop_now = false;
  chThdCreateStatic(my_thread_wa, sizeof(my_thread_wa), NORMALPRIO, my_thread,
                    NULL);

  // Terminal commands for the VESC Tool terminal can be registered.
  terminal_register_command_callback("spi_cs_low", "Set DRV SPI CS low", "",
                                     set_cs_low);
  terminal_register_command_callback("spi_cs_high", "Set DRV SPI CS high", "",
                                     set_cs_high);
}

// Called when the custom application is stopped. Stop our threads
// and release callbacks.
void app_custom_stop(void) {
  mc_interface_set_pwm_callback(0);
  terminal_unregister_callback(set_cs_low);
  terminal_unregister_callback(set_cs_high);

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

  // Example of using the experiment plot
  //	chThdSleepMilliseconds(8000);
  //	commands_init_plot("Sample", "Voltage");
  //	commands_plot_add_graph("Temp Fet");
  //	commands_plot_add_graph("Input Voltage");
  //	float samp = 0.0;
  //
  //	for(;;) {
  //		commands_plot_set_graph(0);
  //		commands_send_plot_points(samp,
  // mc_interface_temp_fet_filtered());
  // commands_plot_set_graph(1); 		commands_send_plot_points(samp,
  // GET_INPUT_VOLTAGE()); 		samp++;
  // chThdSleepMilliseconds(10);
  //	}

  for (;;) {
    // Check if it is time to stop.
    if (stop_now) {
      is_running = false;
      return;
    }

    timeout_reset(); // Reset timeout if everything is OK.

    // Run your logic here. A lot of functionality is available in
    // mc_interface.h.

    chThdSleepMilliseconds(10);
  }
}

static void pwm_callback(void) {
  // Called for every control iteration in interrupt context.
}

// Callback function for the terminal command with arguments.
static void set_cs_low(int argc, const char **argv) {
  (void)argc;
  (void)argv;
  palClearPad(DRV8301_CS_GPIO, DRV8301_CS_PIN);
  commands_printf("Set CS low");
}
static void set_cs_high(int argc, const char **argv) {
  (void)argc;
  (void)argv;
  palSetPad(DRV8301_CS_GPIO, DRV8301_CS_PIN);
  commands_printf("Set CS high");
}
