// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

// The assembled pio program
#include "grug_ws2812b.pio.h"

#define NUM_PIXELS 64

// Pin assigments for LED data pin, DIN
#define OUT_PIN 2

// hsv_to_rgb: a minor modification of this:
// https://www.programmingalgorithms.com/algorithm/hsv-to-rgb/c/

uint32_t hsv_to_rgb(double h, double s, double v)
{
  double r = 0, g = 0, b = 0;
  unsigned char R, G, B;

  if (s == 0) {
    r = v;
    g = v;
    b = v;
  } else {
    int i;
    double f, p, q, t;

    if (h == 360)
      h = 0;
    else
      h = h / 60;

    i = (int) trunc(h);
    f = h - i;

    p = v * (1.0 - s);
    q = v * (1.0 - (s * f));
    t = v * (1.0 - (s * (1.0 - f)));

    switch (i) {

    case 0:
      r = v;
      g = t;
      b = p;
      break;

    case 1:
      r = q;
      g = v;
      b = p;
      break;

    case 2:
      r = p;
      g = v;
      b = t;
      break;

    case 3:
      r = p;
      g = q;
      b = v;
      break;

    case 4:
      r = t;
      g = p;
      b = v;
      break;

    default:
      r = v;
      g = p;
      b = q;
      break;
    }

  }

  R = r * 255;
  G = g * 255;
  B = b * 255;

  return (G << 24) | (R << 16) | (B << 8);
}


void prepare_led_data(uint32_t *data, double h, double s, double v)
{
  uint32_t i;
  
  data[0] = (NUM_PIXELS*24 - 1) << 8;  // Tells PIO the number of LEDs
  for (i = 0; i < NUM_PIXELS; i++) {
    data[i+1] = hsv_to_rgb(h, s, v);
    h += 360.0/NUM_PIXELS;
    if (h >= 360.0) h = 0.0;
  }
}


bool timer_callback(struct repeating_timer *t)
{
  struct {
    uint32_t dma_chan;
    uint32_t *data;
  } *u;

  u = t->user_data;    
  dma_channel_set_read_addr(u->dma_chan, u->data, true);

  return true; // Continue repeating timer
}  


int main()
{
  PIO pio = pio0;
  uint32_t led_vals[NUM_PIXELS + 1];
  uint32_t dma_chan_led;
  dma_channel_config c;
  struct repeating_timer timer;
  struct {
    uint32_t dma_chan;
    uint32_t *data;
  } u;
  double h = 0.0, s = 1.0, v = 0.1;

  // Set CPU clock speed to 128 MHz giving easy division to PIO clock of 8MHz.
  set_sys_clock_khz(128000, false);
  
  stdio_init_all();
  while (stdio_usb_connected() == false) {}
  (void) getchar(); // Wait for user to enter a character.  Gives time to connect terminal.
  printf("starting grug_ws2812b\n");

  // Start the PIO program
  uint offset = pio_add_program(pio, &grug_ws2812b_program);
  uint sm = pio_claim_unused_sm(pio, true);
  grug_ws2812b_program_init(pio, sm, offset, OUT_PIN);
  
  // Initialize DMA
  dma_chan_led = dma_claim_unused_channel(true);

  c = dma_channel_get_default_config(dma_chan_led);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  dma_channel_configure(dma_chan_led, &c, &pio->txf[sm], led_vals,
			NUM_PIXELS + 1, false);
 
  // Prepare first buffer of data
  prepare_led_data(led_vals, h, s, v);

  // Initialize repeating timer. Each callback will start a DMA.
  u.dma_chan = dma_chan_led;
  u.data = led_vals;
  add_repeating_timer_ms(17, timer_callback, &u, &timer);

  while (true) {

    // Change LED values
    printf("Enter h, s, and v : ");
    scanf("%lf %lf %lf", &h, &s, &v);
    printf("\nStarting h: %lf, s = %lf, v = %lf\n", h, s, v);
    prepare_led_data(led_vals, h, s, v);
  }

  return 0;
}
