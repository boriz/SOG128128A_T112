// Bubbles.ino
// Simple bubbles demo on SH1107 128x128 OLED via SPI
//
// Vpp=15V, Iref=16.625uA, RRef=750K
// SPI CLK  - GPIO_18
// SPI MOSI - GPIO_23
// CS	GPIO - 16
// A0 (D/C) - GPIO 17
// RES - GPIO 21

#include <SPI.h>

const int8_t _a0  = 17;   // D/C (A0)
const int8_t _cs  = 16;   // Chip select (active low)
const int8_t _rst = 21;   // Reset (active low)

static constexpr uint8_t OLED_W = 128;
static constexpr uint8_t OLED_H = 128;
static constexpr uint8_t OLED_PAGES = OLED_H / 8;

static SPISettings oledSpi(8000000, MSBFIRST, SPI_MODE0); // SH1107 SPI typically mode 0

struct Circle
{
  int16_t x;
  int16_t y;
  uint8_t r;
  int8_t  vy;
};

static uint8_t frameBuf[OLED_PAGES][OLED_W];

static void oledBufferClear();
static void oledRenderBuffer();
static void setPixel(int16_t x, int16_t y);
static void drawCircleOutline(int16_t cx, int16_t cy, uint8_t r);
static void initCircles(Circle* circles, uint8_t count);
static void stepCircles(Circle* circles, uint8_t count);

// --- Low-level helpers -------------------------------------------------------

static inline void csLow()  
{ 
  digitalWrite(_cs, LOW);  
}


static inline void csHigh() 
{
  digitalWrite(_cs, HIGH); 
}


static void oledWriteCommand(uint8_t cmd)
{
  SPI.beginTransaction(oledSpi);
  csLow();
  digitalWrite(_a0, LOW);           // A0=0 => command
  SPI.transfer(cmd);
  csHigh();
  SPI.endTransaction();
}


static void oledWriteData(const uint8_t* data, size_t n)
{
  SPI.beginTransaction(oledSpi);
  csLow();
  digitalWrite(_a0, HIGH);          // A0=1 => data
  while (n--) SPI.transfer(*data++);
  csHigh();
  SPI.endTransaction();
}

static void oledReset()
{
  digitalWrite(_rst, HIGH);
  delay(10);
  digitalWrite(_rst, LOW);
  delay(50);
  digitalWrite(_rst, HIGH);
  delay(100);
}

// --- SH1107 init, adjusted for 128x128 --------
//
// Notes:
// - SH1107 supports max 128x128
// - Memory addressing mode command is 0x20/0x21 (page/vertical) 
static void OLED_SetInitCode()
{
  oledWriteCommand(0xAE); // Display OFF

  // Set memory addressing mode: Page addressing (0x20) 
  oledWriteCommand(0x20);

  // Set column address = 0 (high nibble then low nibble style)
  oledWriteCommand(0x10); // high column = 0
  oledWriteCommand(0x00); // low  column = 0

  // Set page start address (0xB0 .. 0xBF)
  oledWriteCommand(0xB0);

  // Set multiplex ratio to 0x7F => 128MUX (1/128 duty)
  oledWriteCommand(0xA8);
  oledWriteCommand(0x7F);

  // Segment remap & COM scan direction (vendor demo used A0 + C0)
  oledWriteCommand(0xA0); // Segment remap normal
  oledWriteCommand(0xC0); // COM scan direction normal

  // Contrast
  oledWriteCommand(0x81);
  oledWriteCommand(0xA0);

  // DC-DC Control Mode Set
  oledWriteCommand(0xAD);
  oledWriteCommand(0x80); // DC-DC on when display on (POR behavior)

  // Entire display ON follows RAM (A4), normal display (A6)
  oledWriteCommand(0xA4);
  oledWriteCommand(0xA6);

  // Display clock divide / oscillator
  oledWriteCommand(0xD5);
  oledWriteCommand(0x50);

  // Pre-charge period
  oledWriteCommand(0xD9);
  oledWriteCommand(0x25);

  // VCOMH
  oledWriteCommand(0xDB);
  oledWriteCommand(0x30);

  // Clear and turn on
  // (We clear explicitly below with full 128 bytes per page.)
  oledWriteCommand(0xAF); // Display ON
}



void setup()
{
  Serial.begin(115200);
  delay(500);
  
  Serial.println("Configured SPI pins:");
  Serial.print("MOSI: ");
  Serial.println(MOSI);
  Serial.print("SCK: ");
  Serial.println(SCK);
  Serial.print("CS: ");
  Serial.println(_cs);
  Serial.print("A0: ");
  Serial.println(_a0);
  Serial.print("RST: ");
  Serial.println(_rst);
  
  pinMode(_a0, OUTPUT);
  pinMode(_cs, OUTPUT);
  pinMode(_rst, OUTPUT);

  csHigh();
  digitalWrite(_a0, LOW);
  digitalWrite(_rst, HIGH);

  SPI.begin();              // use default SPI pins

  oledReset();
  OLED_SetInitCode();

  oledClear();

  Serial.println("Setup End");
}


void loop()
{
  static bool inited = false;
  static uint32_t lastFrameMs = 0;
  static Circle circles[6];

  if (!inited)
  {
    randomSeed(micros());
    initCircles(circles, sizeof(circles) / sizeof(circles[0]));
    inited = true;
  }

  const uint32_t now = millis();
  if (now - lastFrameMs < 30) return;
  lastFrameMs = now;

  oledBufferClear();
  const uint8_t count = sizeof(circles) / sizeof(circles[0]);
  for (uint8_t i = 0; i < count; i++)
  {
    drawCircleOutline(circles[i].x, circles[i].y, circles[i].r);
  }
  oledRenderBuffer();
  stepCircles(circles, count);
}



// --- Drawing helpers ---------------------------------------------------------

static void oledSetPageCol(uint8_t page, uint8_t col)
{
  // page: 0..15
  oledWriteCommand(0xB0 | (page & 0x0F));

  // column set: low nibble (0x00..0x0F) + high nibble (0x10..0x1F)
  oledWriteCommand(0x00 | (col & 0x0F));
  oledWriteCommand(0x10 | ((col >> 4) & 0x0F));
}


static void oledClear()
{
  uint8_t zeros[OLED_W];
  memset(zeros, 0x00, sizeof(zeros));

  for (uint8_t p = 0; p < OLED_PAGES; p++)
  {
    oledSetPageCol(p, 0);
    oledWriteData(zeros, sizeof(zeros));  // 128 bytes per page for 128 columns
  }
}

// --- Falling circles demo ---------------------------------------------------

static void oledBufferClear()
{
  for (uint8_t p = 0; p < OLED_PAGES; p++)
  {
    memset(frameBuf[p], 0x00, OLED_W);
  }
}


static void oledRenderBuffer()
{
  for (uint8_t p = 0; p < OLED_PAGES; p++)
  {
    oledSetPageCol(p, 0);
    oledWriteData(frameBuf[p], OLED_W);
  }
}


static void setPixel(int16_t x, int16_t y)
{
  if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
  uint8_t page = y >> 3;
  uint8_t bit = y & 0x07;
  frameBuf[page][x] |= (1u << bit);
}


static void drawCircleOutline(int16_t cx, int16_t cy, uint8_t r)
{
  int16_t x = r;
  int16_t y = 0;
  int16_t err = 0;

  while (x >= y)
  {
    setPixel(cx + x, cy + y);
    setPixel(cx + y, cy + x);
    setPixel(cx - y, cy + x);
    setPixel(cx - x, cy + y);
    setPixel(cx - x, cy - y);
    setPixel(cx - y, cy - x);
    setPixel(cx + y, cy - x);
    setPixel(cx + x, cy - y);

    y++;
    if (err <= 0)
    {
      err += 2 * y + 1;
    }
    else
    {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}


static void initCircles(Circle* circles, uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    circles[i].r = random(3, 10);
    circles[i].x = random(circles[i].r, OLED_W - circles[i].r);
    circles[i].y = random(-OLED_H, 0);
    circles[i].vy = random(1, 4);
  }
}


static void stepCircles(Circle* circles, uint8_t count)
{
  for (uint8_t i = 0; i < count; i++)
  {
    circles[i].y += circles[i].vy;
    if (circles[i].y - circles[i].r > OLED_H - 1)
    {
      circles[i].r = random(3, 10);
      circles[i].x = random(circles[i].r, OLED_W - circles[i].r);
      circles[i].y = -circles[i].r - random(0, OLED_H);
      circles[i].vy = random(1, 4);
    }
  }
}
