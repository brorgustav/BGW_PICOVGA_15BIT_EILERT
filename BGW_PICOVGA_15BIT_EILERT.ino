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

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
// Our assembled programs:
// Each gets the name <pio_filename.pio.h>
#include "hsync.pio.h"
#include "vsync.pio.h"
#include "rgb.pio.h"
// Header file
#include "vga_graphics.h"
// Font file
#include "glcdfont.h"
#include <Arduino.h>
// VGA timing constants
#define H_ACTIVE 655      // (active + frontporch - 1) - one cycle delay for mov
#define V_ACTIVE 479      // (active - 1)
#define rgb15_ACTIVE 319  // (horizontal active)/2 - 1
// Ensure the same resolution definitions as in vga_graphics.h.
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 240  // Logical height (output is doubled to 480)

// Length of the pixel array, and number of DMA transfers
const unsigned int pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
const uint16_t TXCOUNT = pixels / 2;  // Total pixels / 2. The total size of the buffer
const uint16_t DMATXCOUNT = SCREEN_WIDTH / 2;
const int QVGALastLine = 240;  //La cantidad de lineas de los gráficos. Se usa para pasar la info al PIO (si se cambia se mueve la imagen)

// Pixel color array that is DMA's to the PIO machines and
// a pointer to the ADDRESS of this color array.
// Note that this array is automatically initialized to all 0's (black)
//extern uint16_t vga_data_array[TXCOUNT];
uint16_t vga_data_array[TXCOUNT];
uint16_t vga_data_array_next[TXCOUNT];
// address_pointer_array = &vga_data_array[DMATXCOUNT * (currentScanLine + 1 >> 1)];
volatile uint16_t* address_pointer_array[TXCOUNT];
//extern uint16_t vga_data_array_next[TXCOUNT];

// uint16_t vga_data_array_next[TXCOUNT];
//#include "vga_graphics.h"  // VGA graphics library
//#include "vga_graphics.cpp"

// NEW: Pin assignments for Pimoroni Pico VGA Demo Base board (15-bit color)
// Red channel: 5 bits on pins 0, 1, 2, 3, 4
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
uint16_t testcolor=0;

uint16_t createColor(uint8_t r, uint8_t g, uint8_t b) {
  // Convert from 8-bit (0–255) to 5-bit (0–31)
  uint16_t red = (r >> 3) & 0x1F;
  uint16_t green = (g >> 3) & 0x1F;
  uint16_t blue = (b >> 3) & 0x1F;
  // Pack: red in bits 14-10, green in bits 9-5, blue in bits 4-0.
  return (red << 10) | (green << 5) | blue;
}

// DMA channels - 0 sends color data, 1 reconfigures and restarts 0
uint16_t dma_chan = dma_claim_unused_channel(true);
uint16_t dma_cb = dma_claim_unused_channel(true);

uint32_t SaveDividerState;       // saved integer divider state
volatile uint32_t currentFrame;  // frame counter

volatile int currentScanLine;  // current processed scan line 0... (next displayed scan line)
void startDMAForLine(uint16_t line) {
  volatile uint16_t* line_ptr = &vga_data_array[(line / 2) * SCREEN_WIDTH];
  dma_channel_set_read_addr(dma_chan, line_ptr, true);
}

// QVGA DMA handler - called on end of every scanline
void __not_in_flash_func(QVgaLine)() {
  // Clear the interrupt request for DMA control channel
  dma_hw->ints0 = (1u << dma_chan);
  startDMAForLine(currentScanLine);
  // update DMA control channel and run it

  // save integer divider state
  //hw_divider_save_state(&SaveDividerState);

  // increment scanline (1..)
  currentScanLine++;                       // new current scanline
  if (currentScanLine >= SCREEN_HEIGHT) {  // last scanline?
    currentFrame++;                        // increment frame counter
    currentScanLine = 0;                   // restart scanline
  }
}


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
  // ============================== PIO DMA Channels =================================================
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  // Channel Zero (sends color data to PIO VGA machine)
  dma_channel_config c0 = dma_channel_get_default_config(dma_chan);  // default configs
  channel_config_set_transfer_data_size(&c0, DMA_SIZE_8);            // 8-bit txfers
  channel_config_set_read_increment(&c0, true);                      // yes read incrementing
  channel_config_set_write_increment(&c0, false);                    // no write incrementing
  channel_config_set_dreq(&c0, DREQ_PIO0_TX2);                       // DREQ_PIO0_TX2 pacing (FIFO)
  channel_config_set_chain_to(&c0, dma_cb);                          // chain to other channel

  dma_channel_configure(
    dma_chan,             // Channel to be configured
    &c0,                  // The configuration we just created
    &pio->txf[rgb15_sm],  // write address (RGB PIO TX FIFO)
    vga_data_array,       // The initial read address (pixel color array)
    DMATXCOUNT,           // Number of transfers; in this case each is 1 byte.
    false                 // Don't start immediately.
  );

  // Channel One (reconfigures the first channel)
  dma_channel_config c1 = dma_channel_get_default_config(dma_cb);  // default configs
  channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);         // 32-bit txfers
  channel_config_set_read_increment(&c1, false);                   // no read incrementing
  channel_config_set_write_increment(&c1, false);                  // no write incrementing
  channel_config_set_chain_to(&c1, dma_chan);                      // chain to other channel

  dma_channel_configure(
    dma_cb,                           // Channel to be configured
    &c1,                              // The configuration we just created
    &dma_hw->ch[dma_chan].read_addr,  // Write address (channel 0 read address)
    &address_pointer_array,           // Read address (POINTER TO AN ADDRESS)
    TXCOUNT,                          // Number of transfers, in this case each is 4 byte
    false                             // Don't start immediately.
  );

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



void fillScreen(uint16_t color) {
  for (int i = 0; i < SCREEN_WIDTH; i++) {
    //vga_data_array_next[i] = color;
    drawPixel(i, 0, color);
  }
  for (int i = 0; i < SCREEN_HEIGHT; i++) {
    //vga_data_array_next[i] = color;
    drawPixel(0, i, color);
  }
}
// A function for drawing a pixel with a specified color.
// Note that because information is passed to the PIO state machines through
// a DMA channel, we only need to modify the contents of the array and the
// pixels will be automatically updated on the screen.
void drawPixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
  vga_data_array[y * SCREEN_WIDTH + x] = color;
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

void clearScreen() {
  for (int i = 0; i < TXCOUNT; i++) {
    vga_data_array_next[i] = 0;
  }
}

void nextFrame() {
  for (uint16_t i = 0; i < TXCOUNT; i++) {
    vga_data_array[i] = vga_data_array_next[i];
  }
}

//TIMER
const byte frameRate = 20;
const unsigned long FRAME_INTERVAL = 1000 / frameRate;  // Intervalo de tiempo para cada frame
unsigned long previousFrameTime = 0;                    // Tiempo previo para el inicio de cada ciclo
unsigned long currentTime;

void setup() {
  initVGA();
  Serial.begin(115200);
  // initTunnel();
  // Test drawing routines.
  // Fill the screen with blue.


  // Draw a red pixel at (100,100) for testing.
  // uint16_t red = createColor(255, 0, 0);
  //    drawPixel(100, 100, red);
}

void loop() {
  //CODE HERE RUNS AT CPU SPEED

  currentTime = millis();  // Obtener el tiempo actual
  // Ejecutar draw() una sola vez al comienzo de cada ciclo
  if (currentTime - previousFrameTime >= FRAME_INTERVAL) {
    previousFrameTime = currentTime;

    draw();
  }
}

void draw() {
  //CODE HERE RUNS AT FRAMERATE
  static bool programa;
  if (currentTime % 5000 <= 50) {
    programa = !programa;  //changes the example program
  }

  if (programa == 1) {
    testcolor = createColor(0, 0, 255);
    fillScreen(testcolor);
    //  tunnel();           //example
  } else {
    testcolor = createColor(255, 0, 0);
        fillScreen(testcolor);
    //    uint16_t green = createColor(0, 255, 0);
    //   asciiHorizontal();
    //example
  }
  //  escribir();         //example
  nextFrame();    //copies temporary buffer to the vga output buffer
  clearScreen();  //deletes temporary buffer, then next frame will be black
}

// void escribir() {
//   setTextColor2(RED, BLUE);
//   setTextCursor(100, 100);
//   setTextSize(2);

//   char statusTemp[4];
//   itoa(currentFrame, statusTemp, 10);
//   writeString(statusTemp);
// }