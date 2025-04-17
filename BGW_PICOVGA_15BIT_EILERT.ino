/**
   CODIGO ORIGINAL BY Hunter Adams (vha3@cornell.edu)
   Modificado by San Tarcisio (https://www.instagram.com/san_tarcisio/)
   
   vvvvvvvvvvvvvvvvvvvvv

   HARDWARE CONNECTIONS
    - GPIO 16 ---> VGA Hsync
    - GPIO 17 ---> VGA Vsync
    - GPIO 18 ---> 330 ohm resistor ---> VGA Red
    - GPIO 19 ---> 330 ohm resistor ---> VGA Green
    - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
    - RP2040 GND ---> VGA GND

   RESOURCES USED
    - PIO state machines 0, 1, and 2 on PIO instance 0
    - DMA channels 0, 1, 2, and 3
    - 153.6 kBytes of RAM (for pixel color data)

*/
// Ensure the same resolution definitions as in vga_graphics.h.
/*
  https://vanhunteradams.com/Pico/VGA/VGA.html
*/

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#define H_ACTIVE 655      // (active + frontporch - 1) - one red_counter delay for mov
#define V_ACTIVE 479      // (active - 1)
#define rgb15_ACTIVE 319  // (horizontal active)/2 - 1
//#define rgb15_ACTIVE 639  // (horizontal active)/2 - 1
// Ensure the same resolution definitions as in vga_graphics.h.
// #define SCREEN_WIDTH 640
// #define SCREEN_HEIGHT 240  // Logical height (output is doubled to 480)
const uint16_t SCREEN_WIDTH = 640;
const uint16_t SCREEN_HEIGHT = 240;  // Logical height (output is doubled to 480)
#define debug Serial
// Our assembled programs:
// Each gets the name <pio_filename.pio.h>
#include "hsync.pio.h"
#include "vsync.pio.h"
#include "rgb.pio.h"
// Header file
//#include "vga_graphics.h"
// Font file
//#include "glcdfont.h"

// VGA timing constants
#define RGB15(red, green, blue) (((red)&0x1F) | (((green)&0x1F) << 6) | (((blue)&0x1F) << 11))

// Length of the pixel array, and number of DMA transfers
const int pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
const int TXCOUNT = pixels / 2;  // Total pixels / 2. The total size of the buffer
//const int TXCOUNT = pixels;  // Total pixels / 2. The total size of the buffer
//const int DMATXCOUNT = SCREEN_WIDTH;
const int DMATXCOUNT = SCREEN_WIDTH / 2;
const int QVGALastLine = 240;  //La cantidad de lineas de los gráficos. Se usa para pasar la info al PIO (si se cambia se mueve la imagen)

// Pixel color array that is DMA's to the PIO machines and
// a pointer to the ADDRESS of this color array.
// Note that this array is automatically initialized to all 0's (black)
//extern uint16_t vga_data_array[TXCOUNT];
uint16_t vga_data_array[TXCOUNT];
volatile uint16_t* address_pointer_array = &vga_data_array[0];
uint16_t vga_data_array_next[TXCOUNT];

// address_pointer_array = &vga_data_array[DMATXCOUNT * (currentScanLine + 1 >> 1)];
//volatile uint16_t* address_pointer_array[TXCOUNT];
//extern uint16_t vga_data_array_next[TXCOUNT];

// uint16_t vga_data_array_next[TXCOUNT];
//#include "vga_graphics.h"  // VGA graphics library
//#include "vga_graphics.cpp"

// NEW: Pin assignments for Pimoroni Pico VGA Demo Base board (15-bit color)
// Red channel: 5 bits on pins 0, 1, 2, 3, 4

//TIMER
const byte frameRate = 20;
const unsigned long FRAME_INTERVAL = 10000 / frameRate;  // Intervalo de tiempo para cada frame
unsigned long previousFrameTime = 0;                     // Tiempo previo para el inicio de cada ciclo
unsigned long currentTime;


#define VGA_RED_PIN0 0
#define VGA_RED_PIN1 1
#define VGA_RED_PIN2 2
#define VGA_RED_PIN3 3
#define VGA_RED_PIN4 4

// Green channel: 5 bits on pins 6, 7, 8, 9, 10
#define VGA_GREEN_PIN0 6
#define VGA_GREEN_PIN1 7
#define VGA_GREEN_PIN2 8
#define VGA_GREEN_PIN3 9
#define VGA_GREEN_PIN4 10

// Blue channel: 5 bits on pins 11, 12, 13, 14, 15
#define VGA_BLUE_PIN0 11
#define VGA_BLUE_PIN1 12
#define VGA_BLUE_PIN2 13
#define VGA_BLUE_PIN3 14
#define VGA_BLUE_PIN4 15

// Sync signals: HSYNC and VSYNC on pins 16 and 17
#define VGA_HSYNC_PIN 16
#define VGA_VSYNC_PIN 17


// Bit masks for drawPixel routine
#define TOPMASK 0b11000111
#define BOTTOMMASK 0b11111000

// For drawLine
#define swap(a, b) \
  { \
    short t = a; \
    a = b; \
    b = t; \
  }

// For writing text
#define tabspace 4  // number of spaces for a tab

// For accessing the font library
#define pgm_read_byte(addr) (*(const uint16_t*)(addr))

// For drawing uint16_tacters
unsigned short cursor_y, cursor_x, textsize;
uint16_t textcolor, textbgcolor, wrap;
uint16_t test_color = 0;


// uint16_t createColor(uint8_t r, uint8_t g, uint8_t b) {
//   // Convert from 8-bit (0–255) to 5-bit (0–31)
//   uint16_t red = (r >> 3) & 0x1F;
//   uint16_t green = (g >> 3) & 0x1F;
//   uint16_t blue = (b >> 3) & 0x1F;
//   // Pack: red in bits 14-10, green in bits 9-5, blue in bits 4-0.
//   //return (red << 10) | (green << 5) | blue;
//   return (red) | (green << 5) | (blue << 10);
// }





uint16_t createColor(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t red = (r >> 3) & 0x1F;    // 5-bit
  uint16_t green = (g >> 3) & 0x1F;  // 5-bit
  uint16_t blue = (b >> 3) & 0x1F;   // 5-bit
                                     // Pack: [B4..B0] -> bits 15–11, [G4..G0] -> bits 10–6, [R4..R0] -> bits 4–0.
                                     //uint16_t output = mapColor((red) | (green << 6) | (blue << 11));
                                     //uint16_t output = ((red) | (green << 6) | (blue << 11));
                                     // return output;
  return (red) |                     // bits 4–0  (GP0–GP4)
         (green << 6) |              // bits 10–6 (GP6–GP10, note bit5 skipped)
         (blue << 11);               // bits 15–11 (GP11–GP15)
}

uint16_t mapColor(uint16_t color) {
  // Extract logical channels.
  uint8_t red = color & 0x1F;
  uint8_t green = (color >> 5) & 0x1F;
  uint8_t blue = (color >> 10) & 0x1F;
  uint16_t out = 0;
  // Map red (logical red in bits 0–4) to physical GPIO0–4.
  for (int i = 0; i < 5; i++) {
    if (red & (1 << i))
      out |= (1 << i);  // Red goes to GPIO0-4 (bits 0-4)
  }
  // Map green to physical GPIO6–10.
  for (int i = 0; i < 5; i++) {
    if (green & (1 << i))
      out |= (1 << (6 + i));
  }
  // Map blue to physical GPIO11–15.
  for (int i = 0; i < 5; i++) {
    if (blue & (1 << i))
      out |= (1 << (11 + i));
  }
  return out;
}

// DMA channels - 0 sends color data, 1 reconfigures and restarts 0
uint16_t dma_chan = dma_claim_unused_channel(true);
uint16_t dma_cb = dma_claim_unused_channel(true);

//uint32_t SaveDividerState;       // saved integer divider state
volatile uint32_t currentFrame;  // frame counter
volatile int currentScanLine;    // current processed scan line 0... (next displayed scan line)
// void startDMAForLine(unsigned char line) {
//   //volatile unsigned char* line_ptr = &vga_data_array[(line / 2) * SCREEN_WIDTH];
//   volatile uint16_t* line_ptr = &vga_data_array[(line / 2) * SCREEN_WIDTH];
//   dma_channel_set_read_addr(dma_chan, line_ptr, true);
// }

// QVGA DMA handler - called on end of every scanline
void __not_in_flash_func(QVgaLine)() {
  // Clear the interrupt request for DMA control channel
  dma_hw->ints0 = (1u << dma_chan);
  // startDMAForLine(currentScanLine);
  // update DMA control channel and run it

  // save integer divider state
  //hw_divider_save_state(&SaveDividerState);

  // increment scanline (1..)
  currentScanLine++;                       // new current scanline
  if (currentScanLine >= SCREEN_HEIGHT) {  // last scanline?
    currentFrame++;                        // increment frame counter
    currentScanLine = 0;                   // restart scanline
  }


  address_pointer_array = &vga_data_array[DMATXCOUNT * (currentScanLine + 1 >> 1)];
}
#include "do_stuff.h"

// restore integer divider state
//hw_divider_restore_state(&SaveDividerState);

void initVGA() {
  // Choose which PIO instance to use (there are two instances, each with 4 state machines)
  PIO pio = pio0;

  // Our assembled program needs to be loaded into this PIO's instruction
  // memory. This SDK function will find a location (offset) in the
  // instruction memory where there is enough space for our program. We need
  // to remember these locations!
  //
  // We only have 32 instructions to spend! If the PIO programs contain more than
  // 32 instructions, then an error message will get thrown at these lines of code.
  //
  // The program name comes from the .program part of the pio file
  // and is of the form <program name_program>
  uint hsync_offset = pio_add_program(pio, &hsync_program);
  uint vsync_offset = pio_add_program(pio, &vsync_program);
  uint rgb15_offset = pio_add_program(pio, &rgb15_program);

  // Manually select a few state machines from pio instance pio0.
  uint hsync_sm = 0;
  uint vsync_sm = 1;
  uint rgb15_sm = 2;

  // Call the initialization functions that are defined within each PIO file.
  // Why not create these programs here? By putting the initialization function in
  // the pio file, then all information about how to use/setup that state machine
  // is consolidated in one place. Here in the C, we then just import and use it.
  hsync_program_init(pio, hsync_sm, hsync_offset, VGA_HSYNC_PIN);
  vsync_program_init(pio, vsync_sm, vsync_offset, VGA_VSYNC_PIN);
  rgb15_program_init(pio, rgb15_sm, rgb15_offset, VGA_RED_PIN0);
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // ============================================= CONFIG =============================================
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // Channel Zero (sends color data to PIO VGA machine)
  dma_channel_config c0 = dma_channel_get_default_config(dma_chan);  // default configs
  channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);           // 8-bit txfers.
  // channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);  // 8-bit txfers.
  channel_config_set_read_increment(&c0, true);    // yes read incrementing
  channel_config_set_write_increment(&c0, false);  // no write incrementing
  channel_config_set_dreq(&c0, DREQ_PIO0_TX2);     // DREQ_PIO0_TX2 pacing (FIFO)
  channel_config_set_chain_to(&c0, dma_cb);        // chain to other channel
    /////////////////////////////////////////////////////////////////////////////////////////////////////
  // Channel One (reconfigures the first channel)
  dma_channel_config c1 = dma_channel_get_default_config(dma_cb);  // default configs
  channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);         // 32-bit txfers. 32x5
  channel_config_set_read_increment(&c1, false);                   // no read incrementing
  channel_config_set_write_increment(&c1, false);                  // no write incrementing
  channel_config_set_chain_to(&c1, dma_chan);                      // chain to other channel
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // ============================================= CONFIG =============================================
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  dma_channel_configure(
    dma_chan,             // Channel to be configured
    &c0,                  // The configuration we just created
    &pio->txf[rgb15_sm],  // write address (RGB PIO TX FIFO)
    &vga_data_array,      // The initial read address (pixel color array)
    DMATXCOUNT,           // Number of transfers; in this case each is 1 byte.
    false                 // Don't start immediately.
  );
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  dma_channel_configure(
    dma_cb,                           // Channel to be configured
    &c1,                              // The configuration we just created
    &dma_hw->ch[dma_chan].read_addr,  // Write address (channel 0 read address)
    &address_pointer_array,           // Read address (POINTER TO AN ADDRESS)
    4,                                // Number of transfers, in this case each is 4 byte
    false                             // Don't start immediately.
  );
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // enable DMA channel IRQ0
  dma_channel_set_irq0_enabled(dma_chan, true);
  // set DMA IRQ handler
  irq_set_exclusive_handler(DMA_IRQ_0, QVgaLine);
  irq_set_enabled(DMA_IRQ_0, true);
  // set highest IRQ priority
  irq_set_priority(DMA_IRQ_0, 0);
  /////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  // Initialize PIO state machine counters. This passes the information to the state machines
  // that they retrieve in the first 'pull' instructions, before the .wrap_target directive
  // in the assembly. Each uses these values to initialize some counting registers.
  pio_sm_put_blocking(pio, hsync_sm, H_ACTIVE);
  pio_sm_put_blocking(pio, vsync_sm, V_ACTIVE);
  pio_sm_put_blocking(pio, rgb15_sm, rgb15_ACTIVE);
  // Start the two pio machine IN SYNC
  // Note that the RGB state machine is running at full speed,
  // so synchronization doesn't matter for that one. But, we'll
  // start them all simultaneously anyway.
  pio_enable_sm_mask_in_sync(pio, ((1u << hsync_sm) | (1u << vsync_sm) | (1u << rgb15_sm)));
  // Start DMA channel 0. Once started, the contents of the pixel color array
  // will be continously DMA's to the PIO machines that are driving the screen.
  // To change the contents of the screen, we need only change the contents
  // of that array.
  dma_start_channel_mask((1u << dma_chan));
}
// A function for drawing a pixel with a specified color.
// Note that because information is passed to the PIO state machines through
// a DMA channel, we only need to modify the contents of the array and the
// pixels will be automatically updated on the screen.
void drawPixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  vga_data_array_next[y * SCREEN_WIDTH + x] = color;
  // Which pixel is it?
  uint16_t pixel = ((SCREEN_WIDTH * y) + x);

  // Is this pixel stored in the first 3 bits
  // of the vga data array index, or the second
  // 3 bits? Check, then mask.
  // if (pixel & 1) {
  //   vga_data_array_next[pixel >> 1] = (vga_data_array_next[pixel >> 1] & TOPMASK) | (color << 3);
  // } else {
  //   vga_data_array_next[pixel >> 1] = (vga_data_array_next[pixel >> 1] & BOTTOMMASK) | (color);
  // }
}
uint16_t e_to_s(uint8_t r, uint8_t g, uint8_t b) {
    // Convert 8-bit channels to RGB565
    uint16_t rgb565 = ((r & 0xF8) << 8) |
                      ((g & 0xFC) << 3) |
                      ((b & 0xF8) >> 3);
    return rgb565;
}

// Convert a 16-bit RGB565 color into a 16-bit bit pattern for GPIO0–15 output
uint16_t s_to_s(uint16_t rgb565) {
    uint8_t r = (rgb565 >> 11) & 0x1F;  // R5
    uint8_t g = (rgb565 >> 5) & 0x3F;   // G6
    uint8_t b = rgb565 & 0x1F;          // B5

    g = (g + 1) >> 1; // Convert G6 → G5 with rounding

    return (r)           // Red → GPIO 0–4
         | (g << 6)      // Green → GPIO 6–10 (bit 5 skipped!)
         | (b << 11);    // Blue → GPIO 11–15
}
void setup() {
  debug.begin(115200);
  delay(2000);
  initVGA();
  debug.println("Started");
  delay(5000);
  // initTunnel();
  // Test drawing routines.
  // Fill the screen with blue.


  // Draw a red pixel at (100,100) for testing.
  // uint16_t red = createColor(255, 0, 0);
  //    drawPixel(100, 100, red);
}
int program_counter = 1;
int red_counter = 0;
int green_counter = 0;
int blue_counter = 0;
int color_max = 255;
bool red_dir = 1;
bool green_dir = 1;
bool blue_dir = 1;
int program_counter_max = 4;
int program_counter_min = 1;
// int program_counter_period = 2000;
int program_counter_period = 50;
unsigned long program_counter_last = 0;
int counter1;
int counter2;
uint16_t conv_color;
int bootsel_count = 0;
void rgb_change() {
  if (millis() > program_counter_last + program_counter_period) {
    // debug.println("Hello");
    program_counter_last = millis();
    // program_counter++;
    //  red_counter=4;
    if (program_counter > program_counter_max | program_counter == 0) {
      program_counter = 1;
    }
    // if (program_counter == 0) {
    //   program_counter = 1;
    // }
    switch (program_counter) {
      case 1:
        if (red_counter >= color_max | red_counter < 0) {
          // red_counter = 0;
          red_dir = !red_dir;
          // program_counter++;
        }
        if (red_dir == 1) {
          red_counter++;
        }
        if (red_dir == 0) {
          red_counter--;
        }
        break;
      case 2:
        if (green_counter >= color_max | green_counter < 0) {
          // green_counter = 0;
          green_dir = !green_dir;
          // program_counter++;
        }
        if (green_dir == 1) {
          green_counter++;
        }
        if (green_dir == 0) {
          green_counter--;
        }
        break;
      case 3:

        if (blue_counter >= color_max | blue_counter < 0) {
          // blue_counter = 0;
          blue_dir = !blue_dir;
          // program_counter++;
        }
        if (blue_dir == 1) {
          blue_counter++;
        }
        if (blue_dir == 0) {
          blue_counter--;
        }
        break;
      case 4:

        // blue_counter++;
        // red_counter++;
        // green_counter++;
        // if (red_counter >= color_max | red_counter < 0) {
        //   // red_counter = 0;
        //   red_dir = !red_dir;
        //   // program_counter++;
        // }
        // if (red_dir == 1) {
        //   red_counter++;
        // }
        // if (red_dir == 0) {
        //   red_counter--;
        // }
        // if (green_counter >= color_max | green_counter < 0) {
        //   // green_counter = 0;
        //   green_dir = !green_dir;
        //   // program_counter++;
        // }
        // if (green_dir == 1) {
        //   green_counter++;
        // }
        // if (green_dir == 0) {
        //   green_counter--;
        // }
        // if (blue_counter >= color_max | blue_counter < 0) {
        //   // blue_counter = 0;
        //   blue_dir = !blue_dir;
        //   // program_counter++;
        // }
        // if (blue_dir == 1) {
        //   blue_counter++;
        // }
        // if (blue_dir == 0) {
        //   blue_counter--;
        // }
        //  conv_color = createColor(255, 255, 255);
        //debug.print("ALL! --- ");
        break;
      case 5:
        //  conv_color = createColor(0, 0, 0);
        program_counter = 1;
        debug.print("SKIP! --- ");
        break;
    }
       debug.print("PROGRAM: ");
          debug.print(program_counter);
    debug.print("     RED:");
    debug.print(red_counter);
    debug.print("     GREEN:");
    debug.print(green_counter);
    debug.print("     BLUE:");
    debug.print(blue_counter);
        debug.print("                COLORCODE: ");
    debug.print(conv_color);
    debug.print(" -> ");
    //test_color = RGB15(red_counter, green_counter, blue_counter);
    conv_color = createColor(red_counter, green_counter, blue_counter);
    // test_color = conv_color;
    test_color = mapColor(conv_color);
    debug.println(test_color);
  }
}
unsigned long bootsel_time_now = 0;
unsigned long bootsel_time_prev;
unsigned long bootsel_time_period_pause = 1200;  //how long you need to hold the bootsel button to execute function, 2000=3sec
unsigned long bootsel_time_period_reset = 2400;  //how long you need to hold the bootsel button to execute function, 2000=3sec
void loop() {
  rgb_change();
  if (BOOTSEL) {

    program_counter++;
    if (program_counter == 4) {
      program_counter = 1;
    }

    // Serial.printf("\a\aYou pressed BOOTSEL %d times!\n", ++bootsel_count);
    // Wait for BOOTSEL to be released
    bootsel_time_prev = millis();
    while (BOOTSEL) {
      //  delay(1);
      bootsel_time_now = millis();
      if (program_counter != 4) {
        if (bootsel_time_now > bootsel_time_prev + bootsel_time_period_pause) {  //count how long you hold the bootsel button
          bootsel_time_prev = bootsel_time_now;
          debug.println("you held bootsel for an amount of time...");
          debug.println("PAUSE program..");
          program_counter = 4;
        }
      }
      if (bootsel_time_now > bootsel_time_prev + bootsel_time_period_reset) {  //count how long you hold the bootsel button
        bootsel_time_prev = bootsel_time_now;
        debug.println("you held bootsel for an amount of time...");
        debug.println("reset the program counter and RGB counters...");
        red_counter = 0;
        green_counter = 0;
        blue_counter = 0;
        program_counter = 0;
      }
    }
  }
  //CODE HERE RUNS AT CPU SPEED
  currentTime = millis();  // Obtener el tiempo actual
  // Ejecutar draw() una sola vez al comienzo de cada ciclo
  if (currentTime - previousFrameTime >= FRAME_INTERVAL) {
    previousFrameTime = currentTime;
    //    debug.println("OK");
    draw();
  }
  //     debug.println("running");
}
