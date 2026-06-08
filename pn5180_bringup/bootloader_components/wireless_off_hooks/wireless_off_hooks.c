#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"

#define KSIG_C6_CHIP_PU_GPIO 54

void bootloader_hooks_include(void) {}

static void hold_c6_chip_pu_low(void) {
  gpio_ll_set_level(&GPIO, KSIG_C6_CHIP_PU_GPIO, 0);
  gpio_ll_pullup_dis(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_pulldown_en(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_input_disable(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_od_disable(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_matrix_out_default(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_set_output_enable_ctrl(&GPIO, KSIG_C6_CHIP_PU_GPIO, false, false);
  gpio_ll_func_sel(&GPIO, KSIG_C6_CHIP_PU_GPIO, PIN_FUNC_GPIO);
  gpio_ll_output_enable(&GPIO, KSIG_C6_CHIP_PU_GPIO);
  gpio_ll_set_level(&GPIO, KSIG_C6_CHIP_PU_GPIO, 0);
}

void bootloader_before_init(void) { hold_c6_chip_pu_low(); }

void bootloader_after_init(void) { hold_c6_chip_pu_low(); }
#endif
