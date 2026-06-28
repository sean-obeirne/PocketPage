/*
 * ============================================================================
 *  not-cue-speed  —  LIVE "how fast can it go" e-paper demo for Cue
 * ============================================================================
 *  Board : ELEGOO ESP32 (WROOM-32)   Panel : Waveshare 2.13" e-Paper HAT V4
 *  Lib   : GxEPD2  (panel = GoodDisplay GDEY0213B74 / SSD1680, 122 x 250)
 *
 *  This is a SHOW, not a table of numbers. It drives the panel as fast as it
 *  physically can using back-to-back PARTIAL refreshes and animates:
 *      - a box bouncing around a window, and
 *      - a live "fps" + frame counter burned onto the screen,
 *  so you can watch the real maximum refresh cadence with your own eyes.
 *
 *  Why it looks the way it does:
 *      - Each partial refresh is rate-limited by the panel's update waveform
 *        (~0.45 s on this controller), so the box "steps" at ~2 frames/sec.
 *        That stepping IS the answer to "how fast can it go".
 *      - Partial updates leave faint ghosting; every CLEAN_EVERY frames we do
 *        one full refresh (the ~2 s white flash) to wipe it, then resume.
 *
 *  Wiring (Waveshare 2.13" HAT pad -> ELEGOO ESP32 pin). It is 4-line SPI, so
 *  leave the HAT's interface jumper at BS=0 (the factory default):
 *      VCC->3V3    GND->GND    DIN->GPIO23   CLK->GPIO18
 *      CS->GPIO5   DC->GPIO17  RST->GPIO16   BUSY->GPIO4
 *  DIN=MOSI and CLK=SCK on the ESP32 hardware VSPI bus; MISO is left unwired.
 * ============================================================================
 */

#include <GxEPD2_BW.h> // GxEPD2 black & white driver
#include <SPI.h>       // hardware SPI

// ---- Pin map (must match the wiring above) ---------------------------------
#define EPD_BUSY 4  // BUSY
#define EPD_RST 16  // RST  (reset)
#define EPD_DC 17   // DC   (data / command select)
#define EPD_CS 5    // CS   (chip select, VSPI SS)
#define EPD_SCK 18  // CLK  (VSPI SCK)
#define EPD_MOSI 23 // DIN  (VSPI MOSI)
#define EPD_MISO -1 // not connected on a write-only e-paper

// Waveshare 2.13" V4 panel == GoodDisplay GDEY0213B74 / SSD1680, 122x250.
GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

// ---- Animation region (the only rectangle refreshed each frame) ------------
static const int16_t PW_X = 3;
static const int16_t PW_Y = 70;
static const int16_t PW_W = 116;
static const int16_t PW_H = 152;

// ---- Bouncing box geometry -------------------------------------------------
static const int16_t BOX = 26;       // box side length, px
static const int16_t READOUT_H = 22; // top strip inside the window for text

// Bounce bounds derived from the window (box stays fully inside, below text).
static const int16_t MIN_X = PW_X + 2;
static const int16_t MAX_X = PW_X + PW_W - BOX - 2;
static const int16_t MIN_Y = PW_Y + READOUT_H;
static const int16_t MAX_Y = PW_Y + PW_H - BOX - 2;

// Every this many partial frames, do ONE full refresh to clear ghosting.
#define CLEAN_EVERY 40

// ---- Animation state -------------------------------------------------------
static int16_t bx = MIN_X, by = MIN_Y; // box position
static int16_t vx = 13, vy = 9;        // box velocity (px/frame)
static unsigned long frame = 0;        // frame counter
static float fpsAvg = 0.0f;            // rolling average fps

// Draw the static title/instructions once with a full refresh.
static void drawStaticTitle(void)
{
    const int16_t W = display.width();
    const int16_t H = display.height();
    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.drawRect(0, 0, W, H, GxEPD_BLACK);

        display.setFont();
        display.setTextSize(2);
        display.setCursor(8, 8);
        display.print("Cue");

        display.setTextSize(1);
        display.setCursor(8, 32);
        display.print("MAX SPEED");
        display.setCursor(8, 46);
        display.print("partial refresh");

        display.drawLine(4, 60, W - 4, 60, GxEPD_BLACK);

        // outline of the animation window
        display.drawRect(PW_X - 1, PW_Y - 1, PW_W + 2, PW_H + 2, GxEPD_BLACK);

        display.setCursor(6, H - 22);
        display.print("box steps = 1 frame");
        display.setCursor(6, H - 12);
        display.print("watch the cadence");
    } while (display.nextPage());
}

// One animation frame: redraw the window (readout + box) via a partial refresh.
// Returns the measured partial-refresh time in ms.
static unsigned long drawFrame(float instFps)
{
    unsigned long t0 = millis();
    display.setPartialWindow(PW_X, PW_Y, PW_W, PW_H);
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // live readout strip at the top of the window
        display.setFont();
        display.setTextSize(1);
        display.setCursor(PW_X + 4, PW_Y + 4);
        display.printf("f%lu", frame);
        display.setCursor(PW_X + 48, PW_Y + 4);
        display.printf("%.1ffps", (double)instFps);
        display.setCursor(PW_X + 4, PW_Y + 13);
        display.printf("avg %.2f fps", (double)fpsAvg);

        // the bouncing box
        display.fillRect(bx, by, BOX, BOX, GxEPD_BLACK);
    } while (display.nextPage());
    return millis() - t0;
}

// Advance the box and bounce it off the window walls.
static void stepBox(void)
{
    bx += vx;
    by += vy;
    if (bx <= MIN_X)
    {
        bx = MIN_X;
        vx = -vx;
    }
    if (bx >= MAX_X)
    {
        bx = MAX_X;
        vx = -vx;
    }
    if (by <= MIN_Y)
    {
        by = MIN_Y;
        vy = -vy;
    }
    if (by >= MAX_Y)
    {
        by = MAX_Y;
        vy = -vy;
    }
}

// ============================================================================
// ============================================================================
//  Software-only e-paper wiring diagnostic  (honest about its own limits)
// ----------------------------------------------------------------------------
//  What firmware CAN prove on this panel:
//    - SHORTS / solder bridges between control pins  (the library can't see these)
//    - the panel is powered AND fully wired, via a real SSD1680 SW_RESET (0x12)
//      answered on BUSY -> one pass proves VCC, GND, RES, BUSY, CS, DC, SCLK, SDI
//      ALL at once, with a measured BUSY-high time as hard evidence.
//  What it CANNOT do (MISO is not wired, so no read-back):
//    - read the chip ID, or split a "no answer" failure cleanly between RES and
//      the individual data lines -- every one of them is required just to deliver
//      0x12, and none reports back except through BUSY. So on failure we give an
//      ordered reseat list, not a single guaranteed wire.
//  SSD1680 BUSY polarity: HIGH = busy/working, LOW = idle/ready.
// ============================================================================

static const uint8_t CTRL_PINS[] = {EPD_RST, EPD_DC, EPD_CS, EPD_SCK, EPD_MOSI};
static const char *const CTRL_NAMES[] = {"RST", "DC", "CS", "SCLK", "SDI"};
static const uint8_t CTRL_N = 5;

// (1) Short scan: drive each control pin HIGH while every other control pin is a
// pulled-down input. If a neighbour reads HIGH, those two wires are bridged
// (solder blob / adjacent breadboard rows). A hard short easily overpowers the
// ~45k internal pulldown, so this is reliable and has near-zero false positives
// (the panel's control inputs are high-impedance and don't couple to each other).
// Captured summary of the most recent short scan, for the quiet watcher: the
// number of shorted pairs and a short "A<->B" label for the first one found.
static uint8_t g_shortCount = 0;
static char g_shortPair[24] = "";

static bool scanForShorts(bool verbose)
{
    bool clean = true;
    g_shortCount = 0;
    g_shortPair[0] = '\0';
    for (uint8_t i = 0; i < CTRL_N; i++)
    {
        for (uint8_t j = 0; j < CTRL_N; j++)
            if (j != i)
                pinMode(CTRL_PINS[j], INPUT_PULLDOWN);
        pinMode(CTRL_PINS[i], OUTPUT);
        digitalWrite(CTRL_PINS[i], HIGH);
        delayMicroseconds(500);
        for (uint8_t j = 0; j < CTRL_N; j++)
        {
            if (j == i)
                continue;
            if (digitalRead(CTRL_PINS[j]) == HIGH)
            {
                if (g_shortCount == 0)
                    snprintf(g_shortPair, sizeof(g_shortPair), "%s<->%s",
                             CTRL_NAMES[i], CTRL_NAMES[j]);
                g_shortCount++;
                if (verbose)
                    Serial.printf("[diag] SHORT: %s(GPIO%d) <-> %s(GPIO%d) bridged!\n",
                                  CTRL_NAMES[i], CTRL_PINS[i], CTRL_NAMES[j], CTRL_PINS[j]);
                clean = false;
            }
        }
        pinMode(CTRL_PINS[i], INPUT);
    }
    for (uint8_t i = 0; i < CTRL_N; i++)
        pinMode(CTRL_PINS[i], INPUT);
    if (verbose)
        Serial.printf("[diag] 1 shorts : %s\n", clean ? "none" : "FOUND -- fix first (see above)");
    return clean;
}

// BUSY drive detector. Read the pin with an internal pull-UP and again with a
// pull-DOWN. If the two reads DISAGREE, the pin just follows the pull => it is
// FLOATING (no external driver: open BUSY wire or unpowered panel). If they
// AGREE, an external push-pull source (a real powered panel) is overriding both
// pulls, and that common value is the driven level.
//   returns -1 = FLOATING,  0 = driven LOW,  1 = driven HIGH
// A bare digitalRead() can NOT be trusted on this pin: when floating it picks up
// crosstalk from the adjacent SPI pins and can momentarily read HIGH. Always go
// through here so noise can never masquerade as a panel.
static int busyDriven(void)
{
    pinMode(EPD_BUSY, INPUT_PULLUP);
    delayMicroseconds(80);
    int up = digitalRead(EPD_BUSY);
    pinMode(EPD_BUSY, INPUT_PULLDOWN);
    delayMicroseconds(80);
    int down = digitalRead(EPD_BUSY);
    if (up != down)
        return -1; // follows the pull -> floating
    return up;     // both agree -> externally driven at this level
}

// Clock one command byte to the panel on the wired SPI pins.
static void sendCmd(uint8_t cmd)
{
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(EPD_DC, LOW); // command phase
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(EPD_CS, HIGH);
    SPI.endTransaction();
}

// (2) Authoritative alive test. Sequence: hardware reset, wait for the power-on
// BUSY pulse to clear, then REQUIRE that BUSY is externally DRIVEN LOW (a real
// idle panel actively holds it low). Only then send SW_RESET (0x12) and require
// a fresh BUSY pulse, and finally confirm BUSY settles back to a DRIVEN low.
// The driven-idle gate is the key fix: with nothing connected BUSY is FLOATING,
// so we bail here and can never false-pass on crosstalk. A non-zero result thus
// proves power + RES + BUSY + every data line (CS/DC/SCLK/SDI).
//   *idleState <- -1 floating, 0 driven-low, 1 stuck-high
static uint32_t panelAliveUs(int *idleState)
{
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    pinMode(EPD_CS, OUTPUT);
    pinMode(EPD_DC, OUTPUT);
    pinMode(EPD_RST, OUTPUT);
    digitalWrite(EPD_CS, HIGH);

    // Hardware reset (datasheet: low pulse, then settle).
    digitalWrite(EPD_RST, HIGH);
    delay(10);
    digitalWrite(EPD_RST, LOW);
    delay(10);
    digitalWrite(EPD_RST, HIGH);
    delay(10);

    // Let the power-on-reset BUSY pulse clear (driven HIGH -> LOW), up to 100ms.
    uint32_t s0 = millis();
    while (busyDriven() == 1 && (millis() - s0) < 100)
    {
    }

    // GATE: BUSY must now be EXTERNALLY DRIVEN LOW. Floating (-1) or stuck HIGH
    // (1) means no real idle panel -> bail. THIS is what stops the test from
    // "passing" with nothing plugged in: a floating pin is never driven.
    int st = busyDriven();
    *idleState = st;
    if (st != 0)
    {
        SPI.end();
        return 0;
    }

    // From a genuinely driven-idle BUSY, send SW_RESET over the DATA lines and
    // require a fresh LOW->HIGH pulse (needs CS+DC+SCLK+SDI to actually arrive).
    pinMode(EPD_BUSY, INPUT_PULLDOWN);
    sendCmd(0x12);

    uint32_t t0 = micros();
    while (digitalRead(EPD_BUSY) == LOW && (micros() - t0) < 100000UL)
    {
    }
    if (digitalRead(EPD_BUSY) == LOW)
    {
        SPI.end();
        return 0; // no response to the command -> data line(s) missing
    }
    uint32_t rise = micros();
    while (digitalRead(EPD_BUSY) == HIGH && (micros() - rise) < 200000UL)
    {
    }
    uint32_t high = micros() - rise;

    // Confirm BUSY came back to a DRIVEN low. A real panel does; stray noise on a
    // floating pin would fail this, closing the last false-pass loophole.
    if (busyDriven() != 0)
    {
        SPI.end();
        return 0;
    }
    SPI.end();
    return high ? high : 1; // floor of 1us so "brief answer" != "silent"
}

// Compute a compact, single-line wiring status plus the one action to try next.
// Fills `out` with a short human-readable string. Returns true only when the
// panel is proven alive (no shorts + BUSY driven + SW-reset 0x12 answered).
static bool diagStatus(char *out, size_t n)
{
    bool noShorts = scanForShorts(false);
    int idle = -2; // panelAliveUs sets this: -1 float, 0 driven-low, 1 stuck-high
    uint32_t aliveUs = panelAliveUs(&idle);

    if (!noShorts)
        snprintf(out, n, "SHORT x%u (%s) -> clear the bridge first",
                 g_shortCount, g_shortPair);
    else if (aliveUs)
        snprintf(out, n, "PANEL ALIVE (BUSY %luus) -> starting demo",
                 (unsigned long)aliveUs);
    else if (idle < 0)
        snprintf(out, n, "BUSY floating -> reseat BUSY->GPIO4, or VCC/GND (no power)");
    else if (idle == 1)
        snprintf(out, n, "BUSY stuck HIGH -> BUSY shorted to 3V3? or panel fault");
    else
        snprintf(out, n, "power+BUSY OK, panel mute -> reseat RES,SDI,SCLK,CS,DC");

    return noShorts && (aliveUs != 0);
}

// One-shot, NON-BLOCKING wiring sanity print.
// IMPORTANT: the short-scan + BUSY probe assume the ESP pins connect to a *bare*
// SSD1680. The Waveshare 2.13" V4 is a HAT with onboard level-shifters + bias
// circuitry in the signal path, so the short-scan reports PHANTOM bridges (e.g.
// a count that flip-flops "RST<->CS x8 / x15" -- a real solder bridge never
// wavers). It can't pass on this hardware, so we run ONE informational pass and
// continue regardless: GxEPD2 drives the HAT directly, the known-good path.
static void waitForGoodWiring(void)
{
    char cur[80];
    bool alive = diagStatus(cur, sizeof(cur));
    Serial.printf("[diag] %s\n", cur);
    if (!alive)
        Serial.println(F("[diag] (info only on a buffered HAT -- starting the demo anyway)"));
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[speed] Cue e-paper MAX SPEED demo"));

    // --- Quick, non-blocking wiring sanity print, then drive the panel ------
    Serial.println(F("[diag] quick wiring check (info only on the buffered HAT)..."));
    waitForGoodWiring();

    // Init + remap hardware VSPI onto the wired pins.
    display.init(115200);
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.setRotation(0); // portrait 122 x 250
    Serial.printf("[speed] panel %d x %d, fastPartial=%d\n",
                  display.width(), display.height(),
                  (int)display.epd2.hasFastPartialUpdate);

    drawStaticTitle();
    Serial.println(F("[speed] animating as fast as the panel allows..."));
    Serial.println(F("[speed] watch the box step across the screen."));
}

void loop()
{
    // Periodic full-refresh clean to wipe ghosting, then redraw the title.
    if (frame > 0 && (frame % CLEAN_EVERY) == 0)
    {
        Serial.println(F("[speed] -- full refresh: clearing ghosting (~2s) --"));
        drawStaticTitle();
    }

    unsigned long dt = drawFrame(fpsAvg); // draw, measuring the refresh time
    float instFps = (dt > 0) ? 1000.0f / (float)dt : 0.0f;

    // Exponential moving average so the on-screen "avg" settles quickly.
    fpsAvg = (fpsAvg <= 0.0f) ? instFps : (fpsAvg * 0.8f + instFps * 0.2f);

    Serial.printf("[speed] frame %lu  %lu ms  %.1f fps  (avg %.2f)\n",
                  frame, dt, (double)instFps, (double)fpsAvg);

    stepBox();
    frame++;
}
