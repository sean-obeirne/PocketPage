/*
 * ============================================================================
 *  PocketPage  —  e-paper e-reader  (ESP32 + Waveshare 2.13" V4 + microSD)
 * ============================================================================
 *  Board : ELEGOO ESP32 (ESP32-WROOM-32)
 *  Panel : Waveshare 2.13" e-Paper HAT V4 (GDEY0213B74 / SSD1680, 122 x 250)
 *  Store : microSD module on the shared SPI bus
 *
 *  Reads plain-text (.txt) files from the SD card and paginates them onto the
 *  e-paper. Three Cherry MX keys:
 *
 *      LIBRARY (on boot + MENU)          READING
 *      ------------------------          ---------------------------
 *      MENU -> move selection UP         NEXT -> next page
 *      LAST -> move selection DOWN       LAST -> previous page
 *      NEXT -> open book (resume page)   MENU -> back to the library
 *
 *  Each book's last page is saved to "/.ppprog" on the card, so opening a book
 *  jumps straight back to where you left off (survives power-off).
 *
 *  Pagination streams from the file (seek + word-wrap), so books of any size
 *  work within the ESP32's RAM. Each page turn is one full e-paper refresh.
 *
 *  ---- Wiring (peripheral pad -> ESP32 pin) ---------------------------------
 *  e-paper : VCC->3V3  GND->GND  BUSY->4  RST->16  DC->17  CS->5
 *            CLK->18   DIN->13
 *  microSD : 3V3->3V3  GND->GND  CLK->18  MOSI->13  MISO->19  CS->21
 *            (CLK + MOSI are shared with the panel on the SPI bus.)
 *  keys    : NEXT->25  MENU->26  LAST->27  (each switch: leg->GPIO, leg->GND;
 *            internal pull-ups, no external resistors.)
 *  NOTE: DIN/MOSI is GPIO13 (the original GPIO23 output driver is damaged).
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
#define EPD_MISO 19 // wired so the SD card can read back

// ---- microSD chip-select (CLK/MOSI/MISO shared with the panel) -------------
#define SD_CS 21

// ---- Cherry MX keys (active-low, internal pull-up) -------------------------
#define BTN_NEXT 25
#define BTN_MENU 26
#define BTN_LAST 27

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(/*CS=*/EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RST, /*BUSY=*/EPD_BUSY));

// ---- Layout (portrait 122 x 250, classic 6x8 GFX font at size 1) -----------
static const int16_t SCR_W = 122;
static const int16_t SCR_H = 250;
static const int16_t MARGIN_X = 3;
static const int16_t TOP_Y = 2;
static const int16_t LINE_H = 10;
static const int COLS = 19; // (122 - 2*MARGIN_X) / 6 px per glyph
static const int ROWS = 22; // text rows per page
static const int16_t FOOTER_Y = SCR_H - 10;

// ---- Progress file (per-book resume) ---------------------------------------
static const char *PROG_PATH = "/.ppprog";

// ---- Reading state ---------------------------------------------------------
static const int MAX_PAGES = 4000;
static File g_book;
static String g_bookPath;
static uint32_t g_bookSize = 0;
static uint32_t g_pageStart[MAX_PAGES];
static int g_pageIndex = 0;
static uint32_t g_nextOffset = 0;
static String g_lines[ROWS];
static int g_lineCount = 0;

// ---- Library (menu) state --------------------------------------------------
static const int MAX_FILES = 48;
static String g_files[MAX_FILES];
static int g_progPage[MAX_FILES]; // saved last-read page per file (aligned)
static int g_fileCount = 0;
static int g_menuSel = 0;
static int g_curFile = -1; // index of the open book

enum Mode
{
    MODE_READ,
    MODE_MENU
};
static Mode g_mode = MODE_MENU;
static bool g_sdReady = false;

// ---- Debounced, edge-triggered keys ----------------------------------------
struct Key
{
    uint8_t pin;
    int stable;
    int lastRaw;
    uint32_t tEdge;
};
static Key kNext = {BTN_NEXT, HIGH, HIGH, 0};
static Key kMenu = {BTN_MENU, HIGH, HIGH, 0};
static Key kLast = {BTN_LAST, HIGH, HIGH, 0};

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

// ---- Text layout -----------------------------------------------------------
// Read exactly one display line (<= cols chars) from the file at its current
// position, word-wrapping on spaces so the file position always marks the
// precise start of the next line. Page offsets therefore stay exact.
static String readDisplayLine(File &f, int cols)
{
    String line;
    while ((int)line.length() < cols)
    {
        int c = f.peek();
        if (c < 0)
            return line; // EOF
        if (c == '\r')
        {
            f.read();
            continue;
        }
        if (c == '\n')
        {
            f.read();
            return line; // hard line break
        }
        line += (char)f.read();
    }
    int c = f.peek();
    if (c >= 0 && c != '\n' && c != '\r' && c != ' ')
    {
        int sp = line.lastIndexOf(' ');
        if (sp >= 0)
        {
            int pushback = line.length() - sp - 1;
            f.seek(f.position() - pushback);
            return line.substring(0, sp);
        }
        return line; // single over-long word: hard wrap
    }
    if (c == ' ')
        f.read(); // drop the break space
    return line;
}

// Lay out one page starting at file offset `start`. Fills g_lines/g_lineCount
// and returns the file offset where the NEXT page begins.
static uint32_t paginate(uint32_t start)
{
    g_book.seek(start);
    g_lineCount = 0;
    while (g_lineCount < ROWS && g_book.peek() >= 0)
        g_lines[g_lineCount++] = readDisplayLine(g_book, COLS);
    return g_book.position();
}

// ---- Progress persistence --------------------------------------------------
// File format: one "<path>\t<pageIndex>" line per book with a nonzero page.
static void loadProgress()
{
    for (int i = 0; i < MAX_FILES; i++)
        g_progPage[i] = 0;
    File f = SD.open(PROG_PATH, FILE_READ);
    if (!f)
        return;
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        int tab = line.indexOf('\t');
        if (tab < 0)
            continue;
        String path = line.substring(0, tab);
        int page = line.substring(tab + 1).toInt();
        for (int i = 0; i < g_fileCount; i++)
            if (g_files[i] == path)
            {
                g_progPage[i] = (page < 0) ? 0 : page;
                break;
            }
    }
    f.close();
}

static void saveProgress()
{
    SD.remove(PROG_PATH); // rewrite fresh (small file, a handful of books)
    File f = SD.open(PROG_PATH, FILE_WRITE);
    if (!f)
        return;
    for (int i = 0; i < g_fileCount; i++)
        if (g_progPage[i] > 0)
            f.printf("%s\t%d\n", g_files[i].c_str(), g_progPage[i]);
    f.close();
}

// ---- Rendering -------------------------------------------------------------
static void drawMessage(const char *l1, const char *l2, const char *l3)
{
    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont();
        display.setTextSize(1);
        int16_t y = 24;
        const char *ls[3] = {l1, l2, l3};
        for (int i = 0; i < 3; i++)
            if (ls[i])
            {
                display.setCursor(MARGIN_X, y);
                display.print(ls[i]);
                y += LINE_H;
            }
    } while (display.nextPage());
}

static void showPage()
{
    if (!g_book)
    {
        drawMessage("No book open.", "Press MENU.", nullptr);
        return;
    }
    // Lay out the current page FIRST (reads SD), then draw it. Keeping SD access
    // out of the e-paper paged-draw loop means the two SPI devices never
    // interleave on the shared bus.
    g_nextOffset = paginate(g_pageStart[g_pageIndex]);
    if (g_pageIndex + 1 < MAX_PAGES)
        g_pageStart[g_pageIndex + 1] = g_nextOffset;
    bool atEnd = (g_nextOffset >= g_bookSize);

    char footer[24];
    snprintf(footer, sizeof(footer), "p.%d%s", g_pageIndex + 1, atEnd ? "  (end)" : "");

    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont();
        display.setTextSize(1);
        for (int r = 0; r < g_lineCount; r++)
        {
            display.setCursor(MARGIN_X, TOP_Y + r * LINE_H);
            display.print(g_lines[r]);
        }
        display.drawLine(0, FOOTER_Y - 3, SCR_W, FOOTER_Y - 3, GxEPD_BLACK);
        display.setCursor(MARGIN_X, FOOTER_Y);
        display.print(footer);
    } while (display.nextPage());

    Serial.printf("[read] %s  page %d  (%lu/%lu bytes)\n",
                  g_bookPath.c_str(), g_pageIndex + 1,
                  (unsigned long)g_nextOffset, (unsigned long)g_bookSize);
}

static void showMenu()
{
    const int visible = 20;
    int first = 0;
    if (g_menuSel >= visible)
        first = g_menuSel - visible + 1;

    display.setFullWindow();
    display.firstPage();
    do
    {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont();
        display.setTextSize(1);

        display.setCursor(MARGIN_X, TOP_Y);
        display.print("PocketPage library");
        display.drawLine(0, TOP_Y + 9, SCR_W, TOP_Y + 9, GxEPD_BLACK);

        if (g_fileCount == 0)
        {
            display.setCursor(MARGIN_X, 28);
            display.print("No .txt files");
            display.setCursor(MARGIN_X, 28 + LINE_H);
            display.print("found on the SD.");
            display.setCursor(MARGIN_X, 28 + 3 * LINE_H);
            display.print("Add some & press");
            display.setCursor(MARGIN_X, 28 + 4 * LINE_H);
            display.print("any key to rescan.");
        }
        else
        {
            int16_t y = TOP_Y + 14;
            for (int i = first; i < g_fileCount && i < first + visible; i++)
            {
                String name = g_files[i];
                if (name.startsWith("/"))
                    name = name.substring(1);
                if ((int)name.length() > COLS)
                    name = name.substring(0, COLS);
                if (i == g_menuSel)
                {
                    display.fillRect(0, y - 1, SCR_W, LINE_H, GxEPD_BLACK);
                    display.setTextColor(GxEPD_WHITE);
                }
                else
                {
                    display.setTextColor(GxEPD_BLACK);
                }
                display.setCursor(MARGIN_X, y);
                display.print(name);
                y += LINE_H;
            }
        }

        display.setTextColor(GxEPD_BLACK);
        display.drawLine(0, FOOTER_Y - 3, SCR_W, FOOTER_Y - 3, GxEPD_BLACK);
        display.setCursor(MARGIN_X, FOOTER_Y);
        display.print("MENUup LASTdn NEXTok");
    } while (display.nextPage());
}

// ---- SD / library ----------------------------------------------------------
static void buildFileList()
{
    g_fileCount = 0;
    File root = SD.open("/");
    if (!root)
        return;
    for (File e = root.openNextFile(); e && g_fileCount < MAX_FILES; e = root.openNextFile())
    {
        if (!e.isDirectory())
        {
            String n = e.name();
            String full = n.startsWith("/") ? n : (String("/") + n);
            String lower = full;
            lower.toLowerCase();
            if (lower.endsWith(".txt"))
                g_files[g_fileCount++] = full;
        }
        e.close();
    }
    root.close();
}

// Open a book by index and jump to its saved page (rebuilding the page-offset
// table from the start so backward navigation still works).
static void openBook(int idx)
{
    if (idx < 0 || idx >= g_fileCount)
        return;
    if (g_book)
        g_book.close();
    g_book = SD.open(g_files[idx].c_str(), FILE_READ);
    if (!g_book)
    {
        Serial.printf("[read] open failed: %s\n", g_files[idx].c_str());
        drawMessage("Could not open", g_files[idx].c_str(), nullptr);
        return;
    }
    g_bookPath = g_files[idx];
    g_bookSize = g_book.size();
    g_curFile = idx;

    int want = g_progPage[idx];
    g_pageIndex = 0;
    g_pageStart[0] = 0;
    while (g_pageIndex < want && g_pageIndex + 1 < MAX_PAGES)
    {
        uint32_t nxt = paginate(g_pageStart[g_pageIndex]);
        if (nxt >= g_bookSize)
            break; // saved page is past EOF (file changed) -> clamp
        g_pageStart[g_pageIndex + 1] = nxt;
        g_pageIndex++;
    }

    g_mode = MODE_READ;
    showPage();
    Serial.printf("[read] opened %s at page %d\n", g_bookPath.c_str(), g_pageIndex + 1);
}

static void rememberPage()
{
    if (g_curFile < 0)
        return;
    g_progPage[g_curFile] = g_pageIndex;
    saveProgress();
}

static void nextPage()
{
    if (g_nextOffset >= g_bookSize)
        return; // already last page
    if (g_pageIndex + 1 >= MAX_PAGES)
        return;
    g_pageIndex++;
    showPage();
    rememberPage();
}

static void prevPage()
{
    if (g_pageIndex == 0)
        return;
    g_pageIndex--;
    showPage();
    rememberPage();
}

static void openMenu()
{
    buildFileList();
    loadProgress();
    g_menuSel = (g_curFile >= 0 && g_curFile < g_fileCount) ? g_curFile : 0;
    g_mode = MODE_MENU;
    showMenu();
}

// Mount the card at a reduced 1 MHz clock (reliable over the long shared leads).
static bool mountSD()
{
    SD.end();
    digitalWrite(SD_CS, HIGH);
    if (!SD.begin(SD_CS, SPI, 1000000))
        return false;
    if (SD.cardType() == CARD_NONE)
    {
        SD.end();
        return false;
    }
    Serial.printf("[sd] mounted, %.0f MB\n", SD.cardSize() / (1024.0 * 1024.0));
    return true;
}

static void startLibrary()
{
    buildFileList();
    loadProgress();
    Serial.printf("[sd] %d .txt file(s) found\n", g_fileCount);
    g_menuSel = 0;
    g_mode = MODE_MENU;
    showMenu(); // boot lands in the library, per the requested UX
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println(F("[pocketpage] booting e-reader"));

    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_MENU, INPUT_PULLUP);
    pinMode(BTN_LAST, INPUT_PULLUP);

    display.init(115200);
    SPI.end();
    SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
    display.setRotation(0);

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (mountSD())
    {
        g_sdReady = true;
        startLibrary();
    }
    else
    {
        g_sdReady = false;
        Serial.println(F("[sd] mount FAILED -- auto-retrying (no reflash needed)"));
        drawMessage("SD not found.", "Check card+wiring.", "Auto-retrying...");
    }
}

void loop()
{
    // Retry the mount quietly until the card appears, then enter the library.
    if (!g_sdReady)
    {
        static uint32_t tLast = 0;
        if (millis() - tLast > 1500)
        {
            tLast = millis();
            if (mountSD())
            {
                g_sdReady = true;
                startLibrary();
            }
        }
        delay(10);
        return;
    }

    bool n = keyPressed(kNext);
    bool m = keyPressed(kMenu);
    bool l = keyPressed(kLast);

    if (g_mode == MODE_READ)
    {
        if (m)
            openMenu(); // MENU -> back to the library
        else if (n)
            nextPage(); // NEXT -> next page
        else if (l)
            prevPage(); // LAST -> previous page
    }
    else // MODE_MENU (library)
    {
        if (g_fileCount == 0)
        {
            if (n || m || l)
                openMenu(); // rescan the card
        }
        else if (m) // MENU -> selection UP
        {
            g_menuSel = (g_menuSel - 1 + g_fileCount) % g_fileCount;
            showMenu();
        }
        else if (l) // LAST -> selection DOWN
        {
            g_menuSel = (g_menuSel + 1) % g_fileCount;
            showMenu();
        }
        else if (n) // NEXT -> open the highlighted book at its saved page
        {
            openBook(g_menuSel);
        }
    }

    delay(5);
}
