bool programa = 0;
void fillScreen(uint16_t color) {
  for (int i = 0; i < TXCOUNT; i++) {
    //vga_data_array[i] = color;
    vga_data_array_next[i] = color;
    //drawPixel(i, 0, color);
  }
  // for (int i = 0; i < TXCOUNT; i++) {

  //   // vga_data_array[i] = color;
  //   vga_data_array_next[i] = color;
  //   //drawPixel(0, i, color);
  // }
}
// void escribir() {
//   setTextColor2(RED, BLUE);
//   setTextCursor(100, 100);
//   setTextSize(2);

//   char statusTemp[4];
//   itoa(currentFrame, statusTemp, 10);
//   writeString(statusTemp);
// }

void clearScreen() {
  for (int i = 0; i < TXCOUNT; i++) {
    // vga_data_array_next[i] = 0;
  }
}

void nextFrame() {
  for (int i = 0; i < TXCOUNT; i++) {
    vga_data_array[i] = vga_data_array_next[i];
  }
}

void draw() {
  //CODE HERE RUNS AT FRAMERATE
  if (currentTime % 5000 <= 50) {
    programa = !programa;  //changes the example program
                           // debug.println("....");
  }

  // if (programa == 1) {
  //   test_color = createColor(0, 0, 255);
  //   test_color = mapColor(test_color);
  //   debug.println("BLUE");
  //   //  tunnel();           //example
  // } else if (programa == 0) {
  //   test_color = createColor(0, 255, 0);
  //   test_color = mapColor(test_color);
  //   debug.println("GREEN");
  //   //    uint16_t green = createColor(0, 255, 0);
  //   //   asciiHorizontal();
  //   //example
  // }
  //  escribir();         //example
  //  debug.println("ar");

  //vga_data_array[i] = color;
  // vga_data_array_next[i] = color;

  //}
  fillScreen(test_color);

  nextFrame();    //copies temporary buffer to the vga output buffer
                  //      debug.println("beer");
  clearScreen();  //deletes temporary buffer, then next frame will be black

  //    debug.println("keeer");
}


