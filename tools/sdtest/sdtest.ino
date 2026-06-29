/*
 * ============================================================================
 *  sdtest  —  confirm the microSD wiring + card before rebuilding the e-reader
 * ============================================================================
 *  Mounts the SD card on the SHARED SPI bus and reports the result on BOTH the
 *  e-paper and serial. This is the SD equivalent of busytest: prove the card is
 *  readable before building the full reader on top of it.
 *
 *  SHARED BUS NOTE: the panel and SD share SCK(18) + MOSI(13). MOSI is GPIO13
 *  now (GPIO23's driver died), so the SD's MOSI pad must go to GPIO13 too. The
 *  card adds its own MISO(19) and CS(21).
 *
 *  Wiring (SD pad -> ESP32 pin):
 *      3V3->3V3  GND->GND  CLK->18  MOSI->13  MISO->19  CS->21
 *  Panel stays: BUSY->4 RST->16 DC->17 CS->5 CLK->18 DIN->13
 * ============================================================================
 */

#include <GxEPD2_BW.h>
#include <SPI.h>
#include "FS.h"
#include "SD.h"

// ---- e-paper pins (DIN remapped 23 -> 13) ----------------------------------
#define EPD_BUSY 4
#define EPD_RST 16
#define EPD_DC 17
#define EPD_CS 5
#define EPD_SCK 18
#define EPD_MOSI 13
#define EPD_MISO 19 // now wired -- the SD card reads back on this line

// ---- microSD chip-select (CLK/MOSI/MISO shared with the panel) -------------
#define SD_CS 21

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

// Results gathered from the SD card BEFORE drawing (never touch SD inside the
// e-paper paged-draw loop -- they share the bus).
static bool g_mounted = false;
static const char *g_type = "?";
static float g_sizeMB = 0;
static int g_fileCount = 0;
static String g_files[6];
static int g_filesShown = 0;

static void scanCard()
{
    SD.end();
    digitalWrite(SD_CS, HIGH);
    // Reduced 1 MHz clock: reliable over the long soldered leads shared with the panel.
    if (!SD.begin(SD_CS, SPI, 1000000))
    {
        g_mounted = false;
        return;
    }
    uint8_t t = SD.cardType();
    if (t == CARD_NONE)
    {
        g_mounted = false;
        SD.end();
        return;
    }
    g_mounted = true;
    g_type = (t == CARD_MMC) ? "MMC" : (t == CARD_SD) ? "SDSC" : (t == CARD_SDHC) ? "SDHC/XC" : "?";
    g_sizeMB = SD.cardSize() / (1024.0 * 1024.0);

    File root = SD.open("/");
    if (root)
    {
        for (File e = root.openNextFile(); e; e = root.openNextFile())
        {
            if (!e.isDirectory())
            {
                String n = e.name();
                String lower = n;
                lower.toLowerCase();
                if (lower.endsWith(".txt"))
                {
                    if (g_filesShown < 6)
                        g_files[g_filesShown++] = n;
                    g_fileCount++;
                }
            }
            e.close();
        }
        root.close();
    }
}

static void drawResult()
{
    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont();
        display.setTextSize(1);

        display.setCursor(4, 2);
        display.print("SD CARD TEST");
        display.drawLine(0, 12, 122, 12, GxEPD_BLACK);

        int16_t y = 20;
        if (!g_mounted)
        {
            display.setTextSize(2);
            display.setCursor(4, y);
            display.print("MOUNT");
            display.setCursor(4, y + 18);
            display.print("FAILED");
            display.setTextSize(1);
            display.setCursor(4, y + 44);
            display.print("Check MOSI->13,");
            display.setCursor(4, y + 54);
            display.print("MISO->19, CS->21,");
            display.setCursor(4, y + 64);
            display.print("3V3, GND. FAT32?");
        }
        else
        {
            display.setCursor(4, y);
            display.printf("OK  %s", g_type);
            display.setCursor(4, y + 12);
            display.printf("%.0f MB", g_sizeMB);
            display.setCursor(4, y + 26);
            display.printf(".txt files: %d", g_fileCount);
            display.drawLine(0, y + 36, 122, y + 36, GxEPD_BLACK);
            int16_t fy = y + 42;
            for (int i = 0; i < g_filesShown; i++)
            {
                String n = g_files[i];
                if (n.startsWith("/"))
                    n = n.substring(1);
                if (n.length() > 19)
                    n = n.substring(0, 19);
                display.setCursor(2, fy);
                display.print(n);
                fy += 10;
            }
            if (g_fileCount > g_filesShown)
            {
                display.setCursor(2, fy);
                display.printf("...+%d more", g_fileCount - g_filesShown);
            }
        }
    } while (display.nextPage());
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[sdtest] mounting microSD on the shared SPI bus..."));

    display.init(115200);
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.setRotation(0);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    scanCard();

    if (g_mounted)
    {
        Serial.printf("[sdtest] OK: %s, %.0f MB, %d .txt file(s)\n", g_type, g_sizeMB, g_fileCount);
        for (int i = 0; i < g_filesShown; i++)
            Serial.printf("         - %s\n", g_files[i].c_str());
    }
    else
    {
        Serial.println(F("[sdtest] MOUNT FAILED -- check MOSI->13, MISO->19, CS->21, 3V3, GND; card FAT32?"));
    }

    drawResult();
    Serial.println(F("[sdtest] result shown on the panel. Done."));
}

void loop()
{
    delay(1000);
}
