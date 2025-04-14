/**
 *   CODIGO ORIGINAL BY Hunter Adams (vha3@cornell.edu)
 *   Modificado by San Tarcisio (https://www.instagram.com/san_tarcisio/)
 * 
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 330 ohm resistor ---> VGA Red
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - RP2040 GND ---> VGA GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels 0, 1, 2, and 3
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 * NOTE
 *  - This is a translation of the display primitives
 *    for the PIC32 written by Bruce Land and students
 *
 */
// Ensure the same resolution definitions as in vga_graphics.h.
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 240  // Logical height (output is doubled to 480)

// Give the I/O pins that we're using some names that make sense - usable in main()
//enum vga_pins {HSYNC=16, VSYNC, RED_PIN, GREEN_PIN, BLUE_PIN} ;

// We can only produce 8 (3-bit) colors, so let's give them readable names - usable in main()
//enum colors {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE} ;

// Pixel color array that is DMA's to the PIO machines and
// a pointer to the ADDRESS of this color array.
// Note that this array is automatically initialized to all 0's (black)



// extern uint16_t vga_data_array[];
// extern uint16_t vga_data_array_next[];
extern volatile uint32_t currentFrame;  // frame counter


// Drawing function prototypes using full 15-bit colors stored in 16-bit words.
void drawPixel(int x, int y, uint16_t color);
void fillScreen(uint16_t color);

// Utility: Convert 8-bit-per-channel RGB (0–255) to a 15-bit (5-5-5) color.
// Format: Bits [14:10] = Red, [9:5] = Green, [4:0] = Blue.
uint16_t createColor(uint8_t r, uint8_t g, uint8_t b);
// VGA primitives - usable in main
void initVGA(void) ;
void clearScreen(void);
void nextFrame(void);
void drawVLine(short x, short y, short h, char color) ;
void drawHLine(short x, short y, short w, char color) ;
void drawLine(short x0, short y0, short x1, short y1, char color) ;
void drawRect(short x, short y, short w, short h, char color);
void drawRectCenter(short x, short y, short w, short h, char color);
void drawCircle(short x0, short y0, short r, char color) ;
void drawCircleHelper( short x0, short y0, short r, unsigned char cornername, char color) ;
void fillCircle(short x0, short y0, short r, char color) ;
void fillCircleHelper(short x0, short y0, short r, unsigned char cornername, short delta, char color) ;
void drawRoundRect(short x, short y, short w, short h, short r, char color) ;
void fillRoundRect(short x, short y, short w, short h, short r, char color) ;
void fillRect(short x, short y, short w, short h, char color) ;
void drawChar(short x, short y, unsigned char c, char color, char bg, unsigned char size) ;
void setTextCursor(short x, short y);
void setTextColor(char c);
void setTextColor2(char c, char bg);
void setTextSize(unsigned char s);
void setTextWrap(char w);
void tft_write(unsigned char c) ;
void writeString(char* str) ;

//conways
int getPixel(short x, short y) ;
