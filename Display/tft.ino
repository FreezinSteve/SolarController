/*
  Solar controller display pages routines
*/
/*
   Needs Font 2 (also Font 4 if using large scale label)

  Make sure all the display driver and pin comnenctions are correct by
  editting the User_Setup.h file in the TFT_eSPI library folder.

  #########################################################################
  ###### DON'T FORGET TO UPDATE THE User_Setup.h FILE IN THE LIBRARY ######
  #########################################################################
*/
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include "wifi_icon.h"
#include "flame_icon.h"
#include "sun_icon.h"
#include "boost_icon.h"
#include "snow_icon.h"
#include "no_blue_icon.h"

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

#define TFT_GREY 0x5AEB

float ltx = 0;    // Saved x coord of bottom of needle
uint16_t osx = 120, osy = 120; // Saved x & y coords

int old_analog =  -999; // Value last displayed


const byte panelSpacing = 60;
const byte meterTop = 20;
const char* meterLabel = "HOT WATER %";
const byte meterFontSize = 4;
const byte meterLabelTop = 90;
const byte iconLeft = 176;
const byte iconStart = 155;
const byte iconOffset = 54;
bool tftInit = false;

void initTFT()
{
  //================================================
  // Init TFT
  if (!tftInit)
  {
    tft.init();
    tft.setRotation(0);
    tftInit = true;
  }
  
  tft.fillScreen(TFT_BLACK);
  if (page == 0)
  {    
    analogMeter(); // Draw analogue meter  
    // Draw main cylinder, solar cylinder, panel temp  
    plotLinear("MAIN", 0, 160);
    plotLinear("SOLAR", 1 * panelSpacing, 160);
    plotLinear("PANEL", 2 * panelSpacing, 160);
  }
  else if (page == 1)
  {
    
  }
}

// #########################################################################
//  Draw the analogue meter on the screen
// #########################################################################
void analogMeter()
{
  // Meter outline
  tft.fillRect(0, meterTop, 239, 126, TFT_GREY);
  tft.fillRect(5, 3 + meterTop, 230, 119, TFT_WHITE);

  tft.setTextColor(TFT_BLACK);  // Text colour

  // Draw ticks every 5 degrees from -50 to +50 degrees (100 deg. FSD swing)
  for (int i = -50; i < 51; i += 5) {
    // Long scale tick length
    int tl = 15;

    // Coodinates of tick to draw
    float sx = cos((i - 90) * 0.0174532925);
    float sy = sin((i - 90) * 0.0174532925);
    uint16_t x0 = sx * (100 + tl) + 120;
    uint16_t y0 = sy * (100 + tl) + 140 + meterTop;
    uint16_t x1 = sx * 100 + 120;
    uint16_t y1 = sy * 100 + 140 + meterTop;

    // Coordinates of next tick for zone fill
    float sx2 = cos((i + 5 - 90) * 0.0174532925);
    float sy2 = sin((i + 5 - 90) * 0.0174532925);
    int x2 = sx2 * (100 + tl) + 120;
    int y2 = sy2 * (100 + tl) + 140 + meterTop;
    int x3 = sx2 * 100 + 120;
    int y3 = sy2 * 100 + 140 + meterTop;

    // Cold
    if (i >= -50 && i < -40) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_BLUE);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_BLUE);
    }

    // Warm
    if (i >= -40 && i < -25) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_YELLOW);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_YELLOW);
    }

    // Hot
    if (i >= -25 && i < 25) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_ORANGE);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_ORANGE);
    }

    // Very hot
    if (i >= 25 && i < 50) {
      tft.fillTriangle(x0, y0, x1, y1, x2, y2, TFT_RED);
      tft.fillTriangle(x1, y1, x2, y2, x3, y3, TFT_RED);
    }

    // Short scale tick length
    if (i % 25 != 0) tl = 8;

    // Recalculate coords incase tick lenght changed
    x0 = sx * (100 + tl) + 120;
    y0 = sy * (100 + tl) + 140 + meterTop;
    x1 = sx * 100 + 120;
    y1 = sy * 100 + 140 + meterTop;

    // Draw tick
    tft.drawLine(x0, y0, x1, y1, TFT_BLACK);

    // Check if labels should be drawn, with position tweaks
    if (i % 25 == 0) {
      // Calculate label positions
      x0 = sx * (100 + tl + 10) + 120;
      y0 = sy * (100 + tl + 10) + 140 + meterTop;
      switch (i / 25) {
        case -2: tft.drawCentreString("0", x0, y0 - 12, 2); break;
        case -1: tft.drawCentreString("25", x0, y0 - 9, 2); break;
        case 0: tft.drawCentreString("50", x0, y0 - 6, 2); break;
        case 1: tft.drawCentreString("75", x0, y0 - 9, 2); break;
        case 2: tft.drawCentreString("100", x0, y0 - 12, 2); break;
      }
    }

    // Now draw the arc of the scale
    sx = cos((i + 5 - 90) * 0.0174532925);
    sy = sin((i + 5 - 90) * 0.0174532925);
    x0 = sx * 100 + 120;
    y0 = sy * 100 + 140 + meterTop;
    // Draw scale arc, don't draw the last part
    if (i < 50) tft.drawLine(x0, y0, x1, y1, TFT_BLACK);
  }

  //tft.drawString("%", 5 + 230 - 40, 119 - 20 + meterTop, meterFontSize); // Units at bottom right
  tft.drawCentreString(meterLabel, 120, meterLabelTop + meterTop, meterFontSize);
  tft.drawRect(5, meterTop + 3, 230, 119 + meterTop, TFT_BLACK); // Draw bezel line

  plotNeedle(0, 0); // Put meter needle at 0
}

// #########################################################################
// Update needle position
// This function is blocking while needle moves, time depends on ms_delay
// 10ms minimises needle flicker if text is drawn within needle sweep area
// Smaller values OK if text not in sweep area, zero for instant movement but
// does not look realistic... (note: 100 increments for full scale deflection)
// #########################################################################
void plotNeedle(int value, byte ms_delay)
{

  if (value < -10) value = -10; // Limit value to emulate needle end stops
  if (value > 110) value = 110;

  // Move the needle util new value reached
  while (!(value == old_analog)) {
    if (old_analog < value) old_analog++;
    else old_analog--;

    if (ms_delay == 0) old_analog = value; // Update immediately id delay is 0

    float sdeg = map(old_analog, -10, 110, -150, -30); // Map value to angle
    // Calculate tip of needle coords
    float sx = cos(sdeg * 0.0174532925);
    float sy = sin(sdeg * 0.0174532925);

    // Calculate x delta of needle start (does not start at pivot point)
    float tx = tan((sdeg + 90) * 0.0174532925);

    // Erase old needle image
    tft.drawLine(120 + 20 * ltx - 1, 140 - 20 + meterTop, osx - 1, osy, TFT_WHITE);
    tft.drawLine(120 + 20 * ltx, 140 - 20 + meterTop, osx, osy, TFT_WHITE);
    tft.drawLine(120 + 20 * ltx + 1, 140 - 20 + meterTop, osx + 1, osy, TFT_WHITE);

    // Re-plot text under needle
    tft.setTextColor(TFT_BLACK);
    tft.drawCentreString(meterLabel, 120, meterLabelTop + meterTop, meterFontSize);
    // Store new needle end coords for next erase
    ltx = tx;
    osx = sx * 98 + 120;
    osy = sy * 98 + 140 + meterTop;

    // Draw the needle in the new postion, magenta makes needle a bit bolder
    // draws 3 lines to thicken needle
    tft.drawLine(120 + 20 * ltx - 1, 140 - 20 + meterTop, osx - 1, osy, TFT_RED);
    tft.drawLine(120 + 20 * ltx, 140 - 20 + meterTop, osx, osy, TFT_MAGENTA);
    tft.drawLine(120 + 20 * ltx + 1, 140 - 20 + meterTop, osx + 1, osy, TFT_RED);

    // Slow needle down slightly as it approaches new postion
    if (abs(old_analog - value) < 10) ms_delay += ms_delay / 5;

    // Wait before next update
    delay(ms_delay);
  }
}

// #########################################################################
//  Draw a linear meter on the screen
// #########################################################################
void plotLinear(char *label, int x, int y)
{
  int w = 36;
  tft.drawRect(x, y, w, 155, TFT_GREY);
  tft.fillRect(x + 2, y + 19, w - 3, 155 - 38, TFT_WHITE);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(label, x + w / 2, y + 2, 2);

  for (int i = 0; i < 110; i += 10)
  {
    tft.drawFastHLine(x + 20, y + 27 + i, 6, TFT_BLACK);
  }

  for (int i = 0; i < 110; i += 50)
  {
    tft.drawFastHLine(x + 20, y + 27 + i, 9, TFT_BLACK);
  }

  tft.fillTriangle(x + 3, y + 127, x + 3 + 16, y + 127, x + 3, y + 127 - 5, TFT_RED);
  tft.fillTriangle(x + 3, y + 127, x + 3 + 16, y + 127, x + 3, y + 127 + 5, TFT_RED);

  tft.drawCentreString("---", x + w / 2, y + 155 - 18, 2);
}

// #########################################################################
//  Adjust 6 linear meter pointer positions
// #########################################################################
void plotPointer(void)
{
  int dy = 187;
  byte pw = 16;

  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  // Move the 3 pointers one pixel towards new value
  for (int i = 0; i < VERT_METER_COUNT; i++)
  {
    char buf[8]; dtostrf(value[i], 4, 0, buf);
    tft.drawRightString(buf, i * panelSpacing + 36 - 5, 187 - 27 + 155 - 18, 2);

    int dx = 3 + panelSpacing * i;
    int clippedVal = value[i];
    if (clippedVal < 0) clippedVal = 0; // Limit value to emulate needle end stops
    if (clippedVal > 100) clippedVal = 100;

    while (!(clippedVal == old_value[i])) {
      dy = 187 + 100 - old_value[i];
      if (old_value[i] > clippedVal)
      {
        tft.drawLine(dx, dy - 5, dx + pw, dy, TFT_WHITE);
        old_value[i]--;
        tft.drawLine(dx, dy + 6, dx + pw, dy + 1, TFT_RED);
      }
      else
      {
        tft.drawLine(dx, dy + 5, dx + pw, dy, TFT_WHITE);
        old_value[i]++;
        tft.drawLine(dx, dy - 6, dx + pw, dy - 1, TFT_RED);
      }
    }
  }
}

void setStatusLabel(char* label, bool blank)
{
  if (blank) {
    tft.fillRect(120, 0, 239, 20, TFT_BLACK);
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawRightString(label, 237, 0, 2);
}

//=============================================================
// Show an error window for an Over-the-air update
//=============================================================
void showOTAError(ota_error_t error)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  if (error == OTA_AUTH_ERROR)
  {
    tft.drawCentreString("Auth Failed", 120, 160, 4);
  }
  else if (error == OTA_BEGIN_ERROR)
  {
    tft.drawCentreString("Begin Failed", 120, 160, 4);
  }
  else if (error == OTA_CONNECT_ERROR)
  {
    tft.drawCentreString("Connect Failed", 120, 160, 4);
  }
  else if (error == OTA_RECEIVE_ERROR)
  {
    tft.drawCentreString("Receive Failed", 120, 160, 4);
  }
  else if (error == OTA_END_ERROR)
  {
    tft.drawCentreString("End Failed", 120, 160, 4);
  }
}

void drawWiFiIcon(byte top, byte left)
{
  drawIcon(wifi_icon, 0, 0, 18, 18);
}

void drawStatusIcons()
{
  int top = iconStart;
  int count = 0;
  if (unoError)
  {
    drawIcon(no_blue_icon, iconLeft, top, 48, 48);
    top += iconOffset;
    count++;
  }
  else
  {
    if (value[2] < 5) // Panel temperature
    {
      drawIcon(snow_icon, iconLeft, top, 48, 48);
      top += iconOffset;
      count++;
    }
    else if (solarOn == 1)
    {
      drawIcon(sun_icon, iconLeft, top, 48, 48);
      top += iconOffset;
      count++;
    }

    if (wetbackOn == 1)
    {
      drawIcon(flame_icon, iconLeft, top, 48, 48);
      top += iconOffset;
      count++;
    }

    if ((boostState > 0 && boostCount == 0) || (boostState == 0 && boostCount > 0))
    {
      drawIcon(boost_icon, iconLeft, top, 48, 48);
      top += iconOffset;
      count++;
    }
  }
  // Blank out unused icon spaces
  while (count < 3)
  {
    tft.fillRect(iconLeft, top, 48, 48, TFT_BLACK);
    top += iconOffset;
    count++;
  }
}

//====================================================================================
// This is the function to draw the icon stored as an array in program memory (FLASH)
//====================================================================================
// To speed up rendering we use a pixel buffer
#define BUFF_SIZE 96

// Draw array "icon" of defined width and height at coordinate x,y
// Maximum icon size is 255x255 pixels to avoid integer overflow

void drawIcon(const unsigned short * icon, int16_t x, int16_t y, uint16_t width, uint16_t height) {

  uint16_t  pix_buffer[BUFF_SIZE];   // Pixel buffer (16 bits per pixel)

  // Set up a window the right size to stream pixels into
  tft.setWindow(x, y, x + width - 1, y + height - 1);

  // Work out the number whole buffers to send
  uint32_t nb = ((uint32_t)height * width) / BUFF_SIZE;

  // Fill and send "nb" buffers to TFT
  for (int i = 0; i < nb; i++) {
    for (int j = 0; j < BUFF_SIZE; j++) {
      pix_buffer[j] = pgm_read_word_far(&icon[i * BUFF_SIZE + j]);
    }
    tft.pushColors(pix_buffer, BUFF_SIZE);
  }

  // Work out number of pixels not yet sent
  uint32_t np = ((uint32_t)height * width) % BUFF_SIZE;

  // Send any partial buffer left over
  if (np) {
    for (int i = 0; i < np; i++) pix_buffer[i] = pgm_read_word_far(&icon[nb * BUFF_SIZE + i]);
    tft.pushColors(pix_buffer, np);
  }
}

void showSSID(const char* ssid)
{
  tft.setTextColor(TFT_BLACK, TFT_BLACK);
  tft.fillRect(0, 0, 240, 20, TFT_BLACK);
  // Draw wifi icon top LHS
  drawWiFiIcon(0, 0);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(ssid, 20, 0, 2);
}

void showReceiveProgram()
{    
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.drawCentreString("Receiving Program", 120, 160, 4);
}

void showConnStatus(const char* stat)
{
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString(stat, 0, 0, 2);
}

