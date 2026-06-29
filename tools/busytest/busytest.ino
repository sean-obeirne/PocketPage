/*
 * ============================================================================
 *  busytest  —  direct BUSY-line probe for the PocketPage panel
 * ============================================================================
 *  NO GxEPD2. Just raw GPIO + SPI, to answer ONE question with hard evidence:
 *
 *      Can THIS ESP32 see the panel's BUSY line (GPIO4) respond to a reset?
 *
 *  Why this exists: the panel was swapped (still frozen) and every wire passed
 *  continuity, yet the e-paper never runs its refresh waveform (29 ms/frame).
 *  The one thing never measured directly is the BUSY signal as the ESP32 reads
 *  it. This strips away the library and the phantom-short scan and just probes
 *  BUSY around a hardware reset + SW_RESET (0x12).
 *
 *  Wiring (identical to the e-reader):
 *      BUSY=4  RST=16  DC=17  CS=5  SCLK=18  MOSI=23  MISO=19
 *
 *  Read the VERDICT line each cycle:
 *    - "BUSY RESPONDED"      -> board+panel BUSY path is GOOD; fault is elsewhere
 *    - "BUSY FLOATING ..."   -> ESP32 GPIO4 isn't seeing the panel  -> BOARD suspect
 *    - "BUSY never pulses"   -> panel reset OK but command path mute -> BOARD/data suspect
 *  SSD1680 BUSY polarity: HIGH = busy/working, LOW = idle/ready.
 * ============================================================================
 */

#include <SPI.h>

#define EPD_BUSY 4
#define EPD_RST 16
#define EPD_DC 17
#define EPD_CS 5
#define EPD_SCK 18
#define EPD_MOSI 13
#define EPD_MISO 19

// Is BUSY externally driven, or just following the internal pull?
// Read once with a pull-up and once with a pull-down: if the two reads DISAGREE
// the pin only follows the pull -> nothing is driving it -> FLOATING. If they
// AGREE, a real push-pull source (a live panel/HAT buffer) is overriding both.
//   returns -1 = FLOATING, 0 = driven LOW (idle), 1 = driven HIGH (busy)
static int busyDriven()
{
    pinMode(EPD_BUSY, INPUT_PULLUP);
    delayMicroseconds(120);
    int up = digitalRead(EPD_BUSY);
    pinMode(EPD_BUSY, INPUT_PULLDOWN);
    delayMicroseconds(120);
    int down = digitalRead(EPD_BUSY);
    if (up != down)
        return -1;
    return up;
}

// Clock one command byte to the panel on the wired SPI pins.
static void sendCmd(uint8_t c)
{
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(EPD_DC, LOW); // command phase
    digitalWrite(EPD_CS, LOW);
    SPI.transfer(c);
    digitalWrite(EPD_CS, HIGH);
    SPI.endTransaction();
}

// Wiggle ONE command line so a multimeter can prove the wire + BOTH solder joints
// carry signal all the way to the panel pad. Drives the ESP pin HIGH (~3.3V) then
// LOW (~0V), ~2.5s each, announcing which PANEL pad to measure. A good wire makes
// that pad swing 3.3V <-> 0V; a dead joint leaves it stuck -> that's the culprit.
static void scanLine(uint8_t pin, const char *espName, const char *panelPad)
{
    pinMode(pin, OUTPUT);
    Serial.printf("[scan] %-5s (%s):  put meter DC-V: black=GND  red=PANEL '%s'\n",
                  espName, "drive", panelPad);
    for (int i = 0; i < 2; i++)
    {
        digitalWrite(pin, HIGH);
        Serial.printf("       -> driving HIGH  (pad '%s' should read ~3.3V)\n", panelPad);
        delay(2500);
        digitalWrite(pin, LOW);
        Serial.printf("       -> driving LOW   (pad '%s' should read ~0V)\n", panelPad);
        delay(2500);
    }
    Serial.printf("       (if '%s' never moved off one level, THIS joint is open)\n", panelPad);
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("[busytest] direct BUSY probe (no GxEPD2) -- watch the VERDICT line"));

    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_DC, OUTPUT);
    digitalWrite(EPD_DC, HIGH);
    pinMode(EPD_RST, OUTPUT);
    digitalWrite(EPD_RST, HIGH);
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
}

void loop()
{
    // Re-arm the SPI bus + control pins each pass (the line-walk below hands SPI
    // off to plain GPIO, so we restore it here at the top of every cycle).
    pinMode(EPD_CS, OUTPUT);
    digitalWrite(EPD_CS, HIGH);
    pinMode(EPD_DC, OUTPUT);
    digitalWrite(EPD_DC, HIGH);
    pinMode(EPD_RST, OUTPUT);
    digitalWrite(EPD_RST, HIGH);
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);

    // (1) Resting state of BUSY before we poke anything.
    int rest = busyDriven();
    const char *r = (rest < 0) ? "FLOATING" : (rest == 0 ? "driven-LOW(idle)" : "driven-HIGH");

    // (2) Hardware reset pulse (datasheet: low pulse, then settle).
    digitalWrite(EPD_RST, HIGH);
    delay(10);
    digitalWrite(EPD_RST, LOW);
    delay(10);
    digitalWrite(EPD_RST, HIGH);
    delay(20);

    // (3) Send SW_RESET and time how long BUSY asserts HIGH. A live panel that
    //     received the command over CS/DC/SCLK/MOSI raises BUSY while it resets.
    pinMode(EPD_BUSY, INPUT);
    sendCmd(0x12);
    uint32_t t0 = micros();
    bool wentHigh = false;
    while (micros() - t0 < 150000UL)
    {
        if (digitalRead(EPD_BUSY) == HIGH)
        {
            wentHigh = true;
            break;
        }
    }
    uint32_t hi = 0;
    if (wentHigh)
    {
        uint32_t h0 = micros();
        while (digitalRead(EPD_BUSY) == HIGH && micros() - h0 < 400000UL)
        {
        }
        hi = micros() - h0;
    }

    Serial.printf("[busytest] rest=%-16s  reset-response=%s",
                  r, wentHigh ? "BUSY went HIGH" : "BUSY stayed flat");
    if (wentHigh)
        Serial.printf(" (%lu us)", (unsigned long)hi);
    Serial.println();

    if (wentHigh)
    {
        Serial.println(F("[busytest] VERDICT: BUSY RESPONDED -> command path GOOD. Re-flash the reader!"));
        Serial.println(F("[busytest] ---"));
        delay(1500);
        return;
    }

    // BUSY flat = the panel is alive (rest proved that) but the 0x12 command never
    // arrived. It rides on exactly 4 wires. Walk each one so the meter names the
    // dead joint. Hand off SPI so we can drive these pins as plain GPIO.
    Serial.println();
    Serial.println(F("[busytest] BUSY never pulsed -> command NOT received by a LIVE panel."));
    Serial.println(F("[busytest] Walking the 4 command lines. Meter DC-V, black on GND,"));
    Serial.println(F("[busytest] red on the named PANEL pad. The one that won't SWING is broken:"));
    Serial.println();
    SPI.end();
    scanLine(EPD_SCK, "SCLK", "CLK");  // D18 - just reworked
    scanLine(EPD_MOSI, "MOSI", "DIN"); // D23 - just reworked
    scanLine(EPD_CS, "CS", "CS");
    scanLine(EPD_DC, "DC", "DC");
    Serial.println(F("[busytest] === walk done. Fix the pad that never moved, then re-run. ==="));
    Serial.println(F("[busytest] ---"));
    // restore safe idle levels before the next BUSY attempt
    digitalWrite(EPD_CS, HIGH);
    digitalWrite(EPD_DC, HIGH);
    delay(800);
}
