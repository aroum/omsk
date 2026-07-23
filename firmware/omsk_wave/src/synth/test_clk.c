#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include <stdio.h>


int main() {
  uint32_t f = clock_get_hz(clk_sys);
  printf("Clock: %u\n", f);
  return 0;
}
