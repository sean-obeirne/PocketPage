/*
 * ============================================================================
 *  PocketPage  —  BUTTON TEST  (verify the 3 Cherry MX keys before the SD card)
 * ============================================================================
 *  Board : ELEGOO ESP32 (ESP32-WROOM-32)
 *  Panel : Waveshare 2.13" e-Paper HAT V4 (GDEY0213B74 / SSD1680, 122 x 250)
 *
 *  Each keypress stamps a filled square into that key's column on the e-paper
 *  AND prints a line to serial. Two independent confirmation channels: if a
 *  button is mis-wired, serial tells you immediately which one is silent.
 *
 *      NEXT -> left column      MENU -> middle column      LAST -> right column
 *
 *  Wiring (panel pad -> ESP32 pin); DIN is on GPIO13 (GPIO23's driver is dead):
 *      VCC->3V3  GND->GND  BUSY->4  RST->16  DC->17  CS->5  CLK->18  DIN->13
 *  Keys (each switch: one leg -> GPIO, other leg -> GND; internal pull-ups):
 *      NEXT->25   MENU->26   LAST->27
 * ============================================================================
 */

#include <GxEPD2_BW.h>
#include <SPI.h>

// ---- e-paper pins (DIN remapped 23 -> 13) ----------------------------------
#define EPD_BUSY 4
#define EPD_RST 16
#define EPD_DC 17
#define EPD_CS 5
#define EPD_SCK 18
#define EPD_MOSI 13
#define EPD_MISO -1

// ---- Cherry MX keys (active-low, internal pull-up) -------------------------
#define BTN_NEXT 25
#define BTN_MENU 26
#define BTN_LAST 27

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

// ---- Debounced, edge-triggered keys ----------------------------------------
struct Key
{
    uint8_t pin;
    int stable;
    int lastRaw;
    uint32_t tEdge;
};
static Key keys[3] = {{BTN_NEXT, HIGH, HIGH, 0}, {BTN_MENU, HIGH, HIGH, 0}, {BTN_LAST, HIGH, HIGH, 0}};
static const char *const NAMES[3] = {"NEXT", "MENU", "LAST"};

// Returns true exactly ONCE per physical press (debounced falling edge).
static bool keyPressed(Key &k)
{
    int raw = digitalRead(k.pin);
    if (raw != k.lastRaw)
    {
        k.lastRaw = raw;
        k.tEdge = millis();
    }
    if ((millis() - k.tEdge) > 25 && raw != k.stable)
    {
        k.stable = raw;
        if (k.stable == LOW)
            return true;
    }
    return false;
}

// ---- Layout (portrait 122 x 250) -------------------------------------------
static const int16_t SCR_W = 122;
static const int16_t SCR_H = 250;
static const int16_t COL_X[3] = {1, 41, 81}; // left edge of each ~40px column
static const int16_t AREA_Y = 30;            // top of the square-stacking area
static const int16_t SQ = 16;                // square side, px
static const int16_t SQ_STEP = 20;           // vertical pitch between squares
static const int MAX_SQ = 10;                // squares per column before wrap

static int counts[3] = {0, 0, 0};
static unsigned long total = 0;

static void drawScreen(bool full)
{
    if (full)
        display.setFullWindow();
    else
        display.setPartialWindow(0, 0, SCR_W, SCR_H);
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont();
        display.setTextSize(1);

        display.setCursor(4, 2);
        display.print("BUTTON TEST");
        display.drawLine(0, 12, SCR_W, 12, GxEPD_BLACK);

        for (int c = 0; c < 3; c++)
        {
            int16_t x = COL_X[c];
            if (c > 0)
                display.drawLine(x - 1, 12, x - 1, SCR_H, GxEPD_BLACK); // column divider

            display.setCursor(x + 8, 15); // column label
            display.print(NAMES[c]);
            display.setCursor(x + 12, 24); // running count, e.g. "x3"
            display.printf("x%d", counts[c]);

            int drawn = counts[c] % (MAX_SQ + 1); // wrap (clears) past a full column
            for (int i = 0; i < drawn; i++)
                display.fillRect(x + 12, AREA_Y + i * SQ_STEP, SQ, SQ, GxEPD_BLACK);
        }
    } while (display.nextPage());
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[btntest] press NEXT / MENU / LAST -- each adds a square + a serial line"));
    Serial.println(F("[btntest] if a key prints nothing here, that key's wiring is the problem"));

    for (int i = 0; i < 3; i++)
        pinMode(keys[i].pin, INPUT_PULLUP);

    display.init(115200);
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.setRotation(0);

    drawScreen(true);
    Serial.println(F("[btntest] ready."));
}

void loop()
{
    bool changed = false;
    for (int i = 0; i < 3; i++)
    {
        if (keyPressed(keys[i]))
        {
            counts[i]++;
            total++;
            Serial.printf("[btn] %s pressed  (count=%d)\n", NAMES[i], counts[i]);
            changed = true;
        }
    }

    if (changed)
        drawScreen((total % 10) == 0); // periodic full refresh clears ghosting

    delay(5);
}
