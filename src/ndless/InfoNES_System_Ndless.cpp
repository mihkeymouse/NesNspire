// InfoNES port for the Texas Instruments CX II CAS
// Ported for the Nspire CX II CAS by Malik Idrees Hasan Khan
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libndls.h>
#include <keys.h>
#include <dirent.h>
#include "../InfoNES.h"
#include "../InfoNES_System.h"
#include "../InfoNES_pAPU.h"

// K6502 CPU registers (defined in K6502.cpp, not fully exposed via K6502.h)
extern WORD PC;
extern BYTE SP, F, A, X, Y;
extern BYTE IRQ_State, IRQ_Wiring, NMI_State, NMI_Wiring;
extern WORD g_wPassedClocks;

// PPU internal (defined in InfoNES.cpp, not in InfoNES.h)
extern int SpriteJustHit;

char rom_name[256];
char save_name[256];
char state_name[256];

static const WORD DefaultNesPalette[64] = {
   0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
   0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
   0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
   0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
   0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
   0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
   0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
   0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000
};
//more classic NTSC palette
static const WORD NtscNesPalette[64] = {
   0x294a, 0x006e, 0x0452, 0x1811, 0x200c, 0x2c06, 0x2800, 0x1c60,
   0x10a0, 0x04e0, 0x0100, 0x00e0, 0x00c7, 0x0000, 0x0000, 0x0000,
   0x4e53, 0x0538, 0x18dd, 0x2c7c, 0x4456, 0x504c, 0x4c84, 0x3ce0,
   0x2960, 0x15c0, 0x05e0, 0x01c5, 0x018f, 0x0000, 0x0000, 0x0000,
   0x77bd, 0x267d, 0x3dfd, 0x599d, 0x715d, 0x7576, 0x75ac, 0x6a24,
   0x52a0, 0x3b00, 0x2744, 0x1f2d, 0x1ed9, 0x1ce7, 0x0000, 0x0000,
   0x77bd, 0x573d, 0x5efd, 0x6add, 0x76bd, 0x76ba, 0x76d6, 0x7312,
   0x674f, 0x5b6f, 0x5792, 0x4f96, 0x535c, 0x5294, 0x0000, 0x0000
};

// NES colour palette in RGB555 format (64 colours, 4 banks)
WORD NesPalette[64] = {
   // Bank 0 — dark background colours
   0x39ce, 0x1071, 0x0015, 0x2013, 0x440e, 0x5402, 0x5000, 0x3c20,
   0x20a0, 0x0100, 0x0140, 0x00e2, 0x0ceb, 0x0000, 0x0000, 0x0000,
   // Bank 1 — light background colours
   0x5ef7, 0x01dd, 0x10fd, 0x401e, 0x5c17, 0x700b, 0x6ca0, 0x6521,
   0x45c0, 0x0240, 0x02a0, 0x0247, 0x0211, 0x0000, 0x0000, 0x0000,
   // Bank 2
   0x7fff, 0x1eff, 0x2e5f, 0x223f, 0x79ff, 0x7dd6, 0x7dcc, 0x7e67,
   0x7ae7, 0x4342, 0x2769, 0x2ff3, 0x03bb, 0x0000, 0x0000, 0x0000,
   // Bank 3 — light sprite colours
   0x7fff, 0x579f, 0x635f, 0x6b3f, 0x7f1f, 0x7f1b, 0x7ef6, 0x7f75,
   0x7f94, 0x73f4, 0x57d7, 0x5bf9, 0x4ffe, 0x0000, 0x0000, 0x0000
};

static bool quit_requested = false;
static bool lcd_active     = false;
static bool menu_prev      = false;
static bool fs_dec_prev    = false;
static bool fs_inc_prev    = false;
static bool ss_save_prev   = false;
static bool ss_load_prev   = false;
static bool palette_prev   = false;
static bool ntsc_palette_active = false;
static const WORD DEFAULT_FRAME_SKIP = 0;
static const WORD MAX_FRAME_SKIP = 3;
static const size_t CHRBUF_SIZE = 256 * 2 * 8 * 8;
static uint16_t rgb565_lut[0x8000];
static bool rgb565_lut_ready = false;

//Framebuffer - this buffer is not the hardware framebuffer but like a sort of shadow copy we fill and push to LCD

static uint16_t screen_buf[320 * 240];

static const uint16_t BrowserFontFg = 0xe7ff;
static const uint16_t BrowserFontDim = 0x75de;
static const uint16_t BrowserBg = 0x0026;
static const uint16_t BrowserPanel = 0x088c;
static const uint16_t BrowserAccent = 0x563f;
static const uint16_t BrowserHighlight = 0x1bb3;
static const uint16_t BrowserHighlightText = 0x0006;
static const uint16_t BrowserShadow = 0x0000;
static const uint16_t BrowserBorder = 0x114f;

static char program_dir[256];
static char log_path[256];
static FILE *log_fp = NULL;

struct RomBrowserEntry {
  char path[256];
  char file_name[128];
  char title[128];
  char format[16];
  char region[24];
  char console[24];
  char mirroring[24];
  int mapper;
  int prg_kb;
  int chr_kb;
  bool battery;
  bool trainer;
  bool has_state;
  bool has_sram;
  bool valid_header;
};

// ROM browser — up to 128 entries
static RomBrowserEntry rom_browser_entries[128];
static int rom_browser_count = 0;
static RomBrowserEntry selected_rom_entry;
static bool selected_rom_entry_valid = false;

// Logging — keep a single line-flushed handle to avoid repeated open/close churn.
static void log_message(const char *fmt, ...) {
  if (!log_path[0]) return;
  if (!log_fp) {
    log_fp = fopen(log_path, "a");
    if (!log_fp)
      return;
    setvbuf(log_fp, NULL, _IOLBF, 0);
  }
  va_list args;
  va_start(args, fmt);
  vfprintf(log_fp, fmt, args);
  va_end(args);
  fputc('\n', log_fp);
  fflush(log_fp);
}

// Extract the directory part from argv[0]
static void build_program_dir(const char *argv0) {
  strncpy(program_dir, argv0, sizeof(program_dir) - 1);
  program_dir[sizeof(program_dir) - 1] = '\0';
  char *sep = strrchr(program_dir, '/');
  if (!sep) sep = strrchr(program_dir, '\\');
  if (sep) *sep = '\0';
  else     strcpy(program_dir, ".");
}

static void build_log_path(void) {
  snprintf(log_path, sizeof(log_path), "%s/InfoNES.log", program_dir);
}

static void build_save_path(const char *rom_path) {
  strncpy(save_name, rom_path, sizeof(save_name) - 1);
  save_name[sizeof(save_name) - 1] = '\0';
  char *ext = strrchr(save_name, '.');
  if (ext) strcpy(ext, ".srm");
  else     strcat(save_name, ".srm");
}

static void build_state_path(const char *rom_path) {
  strncpy(state_name, rom_path, sizeof(state_name) - 1);
  state_name[sizeof(state_name) - 1] = '\0';
  char *ext = strrchr(state_name, '.');
  if (ext) strcpy(ext, ".sta");
  else     strcat(state_name, ".sta");
}

static void build_related_path(char *dest, size_t dest_size, const char *rom_path, const char *ext_name) {
  strncpy(dest, rom_path, dest_size - 1);
  dest[dest_size - 1] = '\0';
  char *ext = strrchr(dest, '.');
  if (ext) strcpy(ext, ext_name);
  else     strncat(dest, ext_name, dest_size - strlen(dest) - 1);
}

static bool file_exists(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return false;
  fclose(fp);
  return true;
}

static const char *guess_region_from_name(const char *name) {
  if (strstr(name, "(Europe)") || strstr(name, "(E)") || strstr(name, "(PAL)"))
    return "PAL";
  if (strstr(name, "(USA)") || strstr(name, "(U)") || strstr(name, "(World)") || strstr(name, "(JU)"))
    return "NTSC";
  if (strstr(name, "(Japan)") || strstr(name, "(J)"))
    return "NTSC-J";
  return "Unknown";
}

static void build_rom_title(char *dest, size_t dest_size, const char *file_name) {
  size_t len = strlen(file_name);
  if (len >= 8 && strcasecmp(file_name + len - 8, ".nes.tns") == 0)
    len -= 8;
  if (len >= dest_size)
    len = dest_size - 1;

  memcpy(dest, file_name, len);
  dest[len] = '\0';

  for (size_t i = 0; dest[i]; ++i) {
    if (dest[i] == '_' || dest[i] == '.')
      dest[i] = ' ';
  }
}

static void fill_rom_browser_entry(RomBrowserEntry *entry, const char *full_path, const char *file_name) {
  memset(entry, 0, sizeof(*entry));
  strncpy(entry->path, full_path, sizeof(entry->path) - 1);
  strncpy(entry->file_name, file_name, sizeof(entry->file_name) - 1);
  build_rom_title(entry->title, sizeof(entry->title), file_name);

  strcpy(entry->format, "Unknown");
  strcpy(entry->region, guess_region_from_name(file_name));
  strcpy(entry->console, "NES/Famicom");
  strcpy(entry->mirroring, "Unknown");
  entry->mapper = -1;

  char temp_path[256];
  build_related_path(temp_path, sizeof(temp_path), full_path, ".srm");
  entry->has_sram = file_exists(temp_path);
  build_related_path(temp_path, sizeof(temp_path), full_path, ".sta");
  entry->has_state = file_exists(temp_path);

  FILE *fp = fopen(full_path, "rb");
  if (!fp)
    return;

  struct NesHeader_tag header;
  bool ok = (fread(&header, sizeof(header), 1, fp) == 1);
  fclose(fp);

  if (!ok || memcmp(header.byID, "NES\x1a", 4) != 0)
    return;

  entry->valid_header = true;
  entry->mapper = (header.byInfo1 >> 4) | (header.byInfo2 & 0xf0);
  entry->prg_kb = header.byRomSize * 16;
  entry->chr_kb = header.byVRomSize * 8;
  entry->battery = (header.byInfo1 & 0x02) != 0;
  entry->trainer = (header.byInfo1 & 0x04) != 0;

  if ((header.byInfo2 & 0x0c) == 0x08) {
    strcpy(entry->format, "NES 2.0");
    switch (header.byReserve[0] & 0x03) {
      case 0: strcpy(entry->region, "NTSC"); break;
      case 1: strcpy(entry->region, "PAL"); break;
      case 2: strcpy(entry->region, "Multi"); break;
      case 3: strcpy(entry->region, "Dendy"); break;
    }
  } else {
    strcpy(entry->format, "iNES");
  }

  switch (header.byInfo2 & 0x03) {
    case 0: strcpy(entry->console, "NES/Famicom"); break;
    case 1: strcpy(entry->console, "Vs. System"); break;
    case 2: strcpy(entry->console, "PlayChoice-10"); break;
    default: strcpy(entry->console, "Extended"); break;
  }

  if (header.byInfo1 & 0x08)
    strcpy(entry->mirroring, "Four-screen");
  else if (header.byInfo1 & 0x01)
    strcpy(entry->mirroring, "Vertical");
  else
    strcpy(entry->mirroring, "Horizontal");
}

static int compare_rom_browser_entries(const void *lhs, const void *rhs) {
  const RomBrowserEntry *a = (const RomBrowserEntry *)lhs;
  const RomBrowserEntry *b = (const RomBrowserEntry *)rhs;
  return strcasecmp(a->title, b->title);
}

static void build_browser_summary(char *buffer, size_t buffer_size, int index) {
  const RomBrowserEntry *entry = &rom_browser_entries[index];
  int page = index / 5;
  int page_count = (rom_browser_count + 4) / 5;
  int start = page * 5;
  int end = start + 5;
  int offset = 0;

  if (end > rom_browser_count)
    end = rom_browser_count;

  offset += snprintf(buffer + offset, buffer_size - offset,
    "NesNspire ROM Browser\n"
    "Folder: %s\n"
    "ROM %d/%d  Page %d/%d\n\n",
    program_dir, index + 1, rom_browser_count, page + 1, page_count);

  for (int i = start; i < end && offset < (int)buffer_size; ++i) {
    offset += snprintf(buffer + offset, buffer_size - offset,
      "%c %s\n", (i == index) ? '>' : ' ', rom_browser_entries[i].title);
  }

  snprintf(buffer + offset, buffer_size - offset,
    "\nRegion: %s  Mapper: %d\n"
    "PRG: %d KB  CHR: %d KB\n"
    "Format: %s  Mirror: %s\n"
    "Save: %s%s%s\n",
    entry->region,
    entry->mapper,
    entry->prg_kb,
    entry->chr_kb,
    entry->format,
    entry->mirroring,
    entry->battery ? "Battery" : "None",
    entry->has_sram ? " +SRAM" : "",
    entry->has_state ? " +State" : "");
}

static void build_launch_details(char *buffer, size_t buffer_size, const RomBrowserEntry *entry) {
  snprintf(buffer, buffer_size,
    "Title: %s\n"
    "File: %s\n"
    "Region: %s\n"
    "Console: %s\n"
    "Format: %s\n"
    "Mapper: %d\n"
    "PRG ROM: %d KB\n"
    "CHR ROM: %d KB\n"
    "Mirroring: %s\n"
    "Battery Save: %s\n"
    "Trainer: %s\n"
    "Disk State: %s",
    entry->title,
    entry->file_name,
    entry->region,
    entry->console,
    entry->format,
    entry->mapper,
    entry->prg_kb,
    entry->chr_kb,
    entry->mirroring,
    entry->battery ? "Yes" : "No",
    entry->trainer ? "Yes" : "No",
    entry->has_state ? "Found" : "Not found");
}

static const unsigned char BrowserFont6x8[95][6] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x2F, 0x00, 0x00, 0x00 },
  { 0x00, 0x03, 0x00, 0x03, 0x00, 0x00 }, { 0x14, 0x34, 0x1E, 0x14, 0x00, 0x00 },
  { 0x24, 0x7A, 0x2F, 0x32, 0x00, 0x00 }, { 0x23, 0x1B, 0x37, 0x33, 0x30, 0x00 },
  { 0x3F, 0x2D, 0x33, 0x28, 0x00, 0x00 }, { 0x00, 0x00, 0x03, 0x00, 0x00, 0x00 },
  { 0x00, 0x3C, 0x42, 0x81, 0x00, 0x00 }, { 0x00, 0x81, 0x42, 0x3C, 0x00, 0x00 },
  { 0x00, 0x00, 0x07, 0x00, 0x00, 0x00 }, { 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00 },
  { 0x00, 0x40, 0x60, 0x00, 0x00, 0x00 }, { 0x00, 0x08, 0x08, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x20, 0x00, 0x00, 0x00 }, { 0x60, 0x18, 0x06, 0x01, 0x00, 0x00 },
  { 0x1C, 0x3A, 0x2E, 0x1C, 0x00, 0x00 }, { 0x00, 0x22, 0x3E, 0x20, 0x00, 0x00 },
  { 0x00, 0x26, 0x32, 0x2E, 0x00, 0x00 }, { 0x00, 0x22, 0x2A, 0x3E, 0x00, 0x00 },
  { 0x18, 0x16, 0x3E, 0x10, 0x00, 0x00 }, { 0x00, 0x2E, 0x2A, 0x1A, 0x00, 0x00 },
  { 0x1C, 0x2A, 0x2A, 0x1A, 0x00, 0x00 }, { 0x22, 0x1A, 0x06, 0x00, 0x00, 0x00 },
  { 0x36, 0x2A, 0x2A, 0x36, 0x00, 0x00 }, { 0x2C, 0x2A, 0x2A, 0x1C, 0x00, 0x00 },
  { 0x00, 0x00, 0x24, 0x00, 0x00, 0x00 }, { 0x00, 0x40, 0x64, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x18, 0x24, 0x00, 0x00 }, { 0x00, 0x14, 0x14, 0x14, 0x00, 0x00 },
  { 0x00, 0x24, 0x18, 0x00, 0x00, 0x00 }, { 0x00, 0x29, 0x09, 0x0E, 0x00, 0x00 },
  { 0x7C, 0x82, 0xBD, 0x9D, 0x3E, 0x00 }, { 0x20, 0x1C, 0x12, 0x3C, 0x00, 0x00 },
  { 0x00, 0x3E, 0x2A, 0x2A, 0x36, 0x00 }, { 0x1C, 0x22, 0x22, 0x00, 0x00, 0x00 },
  { 0x3E, 0x22, 0x22, 0x1C, 0x00, 0x00 }, { 0x00, 0x3E, 0x2A, 0x2A, 0x00, 0x00 },
  { 0x00, 0x3E, 0x0A, 0x0A, 0x00, 0x00 }, { 0x1C, 0x22, 0x2A, 0x3A, 0x00, 0x00 },
  { 0x3E, 0x08, 0x08, 0x3E, 0x00, 0x00 }, { 0x00, 0x22, 0x3E, 0x22, 0x00, 0x00 },
  { 0x00, 0x22, 0x22, 0x3E, 0x00, 0x00 }, { 0x00, 0x3E, 0x1C, 0x22, 0x00, 0x00 },
  { 0x00, 0x3E, 0x20, 0x20, 0x00, 0x00 }, { 0x38, 0x06, 0x06, 0x38, 0x00, 0x00 },
  { 0x3E, 0x0C, 0x10, 0x3E, 0x00, 0x00 }, { 0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00 },
  { 0x00, 0x3E, 0x0A, 0x0A, 0x06, 0x00 }, { 0x1C, 0x22, 0x62, 0x5C, 0x00, 0x00 },
  { 0x00, 0x3E, 0x0A, 0x36, 0x00, 0x00 }, { 0x00, 0x26, 0x2A, 0x2A, 0x32, 0x00 },
  { 0x02, 0x02, 0x3E, 0x02, 0x02, 0x00 }, { 0x3E, 0x20, 0x20, 0x3E, 0x00, 0x00 },
  { 0x00, 0x1E, 0x20, 0x1E, 0x00, 0x00 }, { 0x3E, 0x10, 0x10, 0x3E, 0x00, 0x00 },
  { 0x22, 0x14, 0x1C, 0x22, 0x00, 0x00 }, { 0x02, 0x04, 0x38, 0x04, 0x02, 0x00 },
  { 0x32, 0x2A, 0x26, 0x20, 0x00, 0x00 }, { 0x00, 0xFF, 0x81, 0x00, 0x00, 0x00 },
  { 0x00, 0x03, 0x0C, 0x30, 0x40, 0x00 }, { 0x00, 0x81, 0xFF, 0x00, 0x00, 0x00 },
  { 0x0C, 0x02, 0x0C, 0x00, 0x00, 0x00 }, { 0x80, 0x80, 0x80, 0x80, 0x00, 0x00 },
  { 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 }, { 0x00, 0x3C, 0x2C, 0x2C, 0x3C, 0x00 },
  { 0x00, 0x3F, 0x24, 0x24, 0x1C, 0x00 }, { 0x00, 0x38, 0x24, 0x24, 0x00, 0x00 },
  { 0x38, 0x24, 0x24, 0x3F, 0x00, 0x00 }, { 0x18, 0x2C, 0x2C, 0x2C, 0x00, 0x00 },
  { 0x04, 0x3F, 0x05, 0x01, 0x00, 0x00 }, { 0xFC, 0xB4, 0xB4, 0x7C, 0x00, 0x00 },
  { 0x00, 0x3F, 0x04, 0x3C, 0x00, 0x00 }, { 0x00, 0x24, 0x3D, 0x20, 0x00, 0x00 },
  { 0x00, 0x84, 0x84, 0xFD, 0x00, 0x00 }, { 0x00, 0x3F, 0x08, 0x14, 0x20, 0x00 },
  { 0x00, 0x21, 0x3F, 0x20, 0x00, 0x00 }, { 0x3C, 0x04, 0x3C, 0x04, 0x3C, 0x00 },
  { 0x00, 0x3C, 0x04, 0x3C, 0x00, 0x00 }, { 0x18, 0x24, 0x24, 0x18, 0x00, 0x00 },
  { 0x00, 0xFC, 0x24, 0x3C, 0x00, 0x00 }, { 0x38, 0x24, 0x24, 0xFC, 0x00, 0x00 },
  { 0x00, 0x3C, 0x04, 0x04, 0x0C, 0x00 }, { 0x00, 0x2C, 0x34, 0x34, 0x00, 0x00 },
  { 0x04, 0x3E, 0x24, 0x24, 0x00, 0x00 }, { 0x00, 0x3C, 0x20, 0x3C, 0x00, 0x00 },
  { 0x00, 0x1C, 0x20, 0x1C, 0x00, 0x00 }, { 0x30, 0x18, 0x38, 0x04, 0x00, 0x00 },
  { 0x24, 0x18, 0x18, 0x24, 0x00, 0x00 }, { 0x80, 0x9C, 0x60, 0x1C, 0x00, 0x00 },
  { 0x00, 0x24, 0x3C, 0x24, 0x00, 0x00 }, { 0x00, 0x08, 0xF7, 0x81, 0x00, 0x00 },
  { 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 }, { 0x00, 0x81, 0xF7, 0x08, 0x00, 0x00 },
  { 0x18, 0x08, 0x10, 0x18, 0x00, 0x00 }
};

static void browser_fill_rect(int x, int y, int w, int h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > 320) w = 320 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0)
    return;

  for (int yy = 0; yy < h; ++yy) {
    uint16_t *dst = &screen_buf[(y + yy) * 320 + x];
    for (int xx = 0; xx < w; ++xx)
      dst[xx] = color;
  }
}

static void browser_draw_hline(int x, int y, int w, uint16_t color) {
  browser_fill_rect(x, y, w, 1, color);
}

static void browser_draw_frame(int x, int y, int w, int h, uint16_t color) {
  browser_draw_hline(x, y, w, color);
  browser_draw_hline(x, y + h - 1, w, color);
  browser_fill_rect(x, y, 1, h, color);
  browser_fill_rect(x + w - 1, y, 1, h, color);
}

static void browser_draw_char(int x, int y, char ch, uint16_t color, int scale) {
  if (ch < 32 || ch > 126)
    ch = '?';

  const unsigned char *glyph = BrowserFont6x8[(int)ch - 32];
  for (int col = 0; col < 6; ++col) {
    unsigned char bits = glyph[col];
    for (int row = 0; row < 8; ++row) {
      if (!(bits & (1 << row)))
        continue;
      browser_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
      if (scale == 1)
        browser_fill_rect(x + col + 1, y + row, 1, 1, color);
    }
  }
}

static void browser_draw_text(int x, int y, const char *text, uint16_t color, int scale, int max_chars) {
  int drawn = 0;
  while (*text && (max_chars < 0 || drawn < max_chars)) {
    if (color != BrowserHighlightText)
      browser_draw_char(x + 1, y + 1, *text, BrowserShadow, scale);
    browser_draw_char(x, y, *text, color, scale);
    x += (6 * scale) + scale;
    ++text;
    ++drawn;
  }
}

static void browser_draw_label_value(int x, int y, const char *label, const char *value) {
  browser_draw_text(x, y, label, BrowserFontDim, 1, -1);
  browser_draw_text(x + ((int)strlen(label) * 7), y, value, BrowserFontFg, 1, -1);
}

static void browser_ellipsize(char *dest, size_t dest_size, const char *text, int max_chars) {
  size_t len = strlen(text);
  if ((int)len <= max_chars) {
    strncpy(dest, text, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return;
  }

  if (max_chars < 4)
    max_chars = 4;
  snprintf(dest, dest_size, "%.*s...", max_chars - 3, text);
}

static void render_rom_browser_screen(int selected_index) {
  const int row_height = 16;
  const int list_top = 34;
  const int list_bottom = 170;
  const int visible_rows = (list_bottom - list_top) / row_height;
  int start = selected_index - (visible_rows / 2);
  if (start < 0)
    start = 0;
  if (start > rom_browser_count - visible_rows)
    start = rom_browser_count - visible_rows;
  if (start < 0)
    start = 0;

  browser_fill_rect(0, 0, 320, 240, BrowserBg);
  browser_fill_rect(0, 0, 320, 26, BrowserAccent);
  browser_fill_rect(6, 32, 308, 136, BrowserPanel);
  browser_fill_rect(0, 176, 320, 64, BrowserPanel);
  browser_draw_frame(6, 32, 308, 136, BrowserBorder);
  browser_draw_hline(0, 176, 320, BrowserBorder);

  char header[32];
  snprintf(header, sizeof(header), "%d ROMs", rom_browser_count);
  browser_draw_text(10, 8, "NESNSPIRE ROM BROWSER", BrowserHighlightText, 1, -1);
  browser_draw_text(320 - ((int)strlen(header) * 7) - 10, 8, header, BrowserHighlightText, 1, -1);

  for (int row = 0; row < visible_rows; ++row) {
    int index = start + row;
    int y = list_top + row * row_height;
    if (index >= rom_browser_count)
      break;

    const RomBrowserEntry *entry = &rom_browser_entries[index];
    if (index == selected_index)
      browser_fill_rect(10, y - 1, 300, row_height - 2, BrowserHighlight);

    char title[40];
    browser_ellipsize(title, sizeof(title), entry->title, 31);
    browser_draw_text(16, y + 3, title, index == selected_index ? BrowserHighlightText : BrowserFontFg, 1, -1);
  }

  const RomBrowserEntry *selected = &rom_browser_entries[selected_index];
  char title[48];
  char mapper[16];
  char prg[16];
  char chr[16];
  char save[32];
  browser_ellipsize(title, sizeof(title), selected->title, 42);
  snprintf(mapper, sizeof(mapper), "%d", selected->mapper);
  snprintf(prg, sizeof(prg), "%d KB", selected->prg_kb);
  snprintf(chr, sizeof(chr), "%d KB", selected->chr_kb);
  snprintf(save, sizeof(save), "%s%s%s",
    selected->battery ? "BAT" : "NONE",
    selected->has_sram ? " SRAM" : "",
    selected->has_state ? " STA" : "");

  browser_draw_text(10, 180, title, BrowserAccent, 1, -1);
  browser_draw_text(10, 167, "UP/DOWN SCROLL  LEFT/RIGHT PAGE  ENTER BOOT  ESC EXIT", BrowserFontDim, 1, -1);
  browser_draw_label_value(10, 195, "Region: ", selected->region);
  browser_draw_label_value(116, 195, "Mapper: ", mapper);
  browser_draw_label_value(218, 195, "Format: ", selected->format);
  browser_draw_label_value(10, 210, "PRG: ", prg);
  browser_draw_label_value(88, 210, "CHR: ", chr);
  browser_draw_label_value(160, 210, "Mirror: ", selected->mirroring);
  browser_draw_label_value(10, 225, "Console: ", selected->console);
  browser_draw_label_value(176, 225, "Save: ", save);

  lcd_blit(screen_buf, SCR_320x240_565);
}

static void render_loading_screen(const RomBrowserEntry *entry) {
  char title[48];
  char mapper[16];
  char prg_chr[32];
  char save[32];

  browser_fill_rect(0, 0, 320, 240, BrowserBg);
  browser_fill_rect(0, 0, 320, 28, BrowserAccent);
  browser_fill_rect(12, 40, 296, 148, BrowserPanel);
  browser_draw_frame(12, 40, 296, 148, BrowserBorder);
  browser_draw_text(12, 8, "LOADING ROM", BrowserHighlightText, 1, -1);

  browser_ellipsize(title, sizeof(title), entry->title, 28);
  snprintf(mapper, sizeof(mapper), "%d", entry->mapper);
  snprintf(prg_chr, sizeof(prg_chr), "%d KB PRG  %d KB CHR", entry->prg_kb, entry->chr_kb);
  snprintf(save, sizeof(save), "%s%s%s",
    entry->battery ? "Battery" : "No battery",
    entry->has_sram ? " + SRAM" : "",
    entry->has_state ? " + State" : "");

  browser_draw_text(24, 56, title, BrowserAccent, 2, 24);
  browser_draw_label_value(24, 96, "Region: ", entry->region);
  browser_draw_label_value(24, 110, "Console: ", entry->console);
  browser_draw_label_value(24, 124, "Format: ", entry->format);
  browser_draw_label_value(24, 138, "Mapper: ", mapper);
  browser_draw_label_value(24, 152, "ROM: ", prg_chr);
  browser_draw_label_value(24, 166, "Save: ", save);

  lcd_blit(screen_buf, SCR_320x240_565);
}

static void init_rgb565_lut(void) {
  if (rgb565_lut_ready)
    return;

  for (unsigned i = 0; i < 0x8000; ++i) {
    uint16_t r = (i >> 10) & 0x1f;
    uint16_t g5 = (i >> 5) & 0x1f;
    uint16_t b = i & 0x1f;
    uint16_t g6 = (g5 << 1) | (g5 >> 4);
    rgb565_lut[i] = (uint16_t)((r << 11) | (g6 << 5) | b);
  }

  rgb565_lut_ready = true;
}

// ---- Save / Load state ----
// One in-RAM quick slot. Disk I/O is too slow for gameplay hotkeys.

struct QuickState {
  WORD PC;
  BYTE SP, F, A, X, Y;
  BYTE IRQ_State, IRQ_Wiring, NMI_State, NMI_Wiring;
  WORD g_wPassedClocks;

  BYTE PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7;
  BYTE PPU_Scr_V, PPU_Scr_V_Next;
  BYTE PPU_Scr_V_Byte, PPU_Scr_V_Byte_Next;
  BYTE PPU_Scr_V_Bit, PPU_Scr_V_Bit_Next;
  BYTE PPU_Scr_H, PPU_Scr_H_Next;
  BYTE PPU_Scr_H_Byte, PPU_Scr_H_Byte_Next;
  BYTE PPU_Scr_H_Bit, PPU_Scr_H_Bit_Next;
  BYTE PPU_Latch_Flag;
  WORD PPU_Addr, PPU_Temp, PPU_Increment, PPU_Scanline;
  BYTE PPU_NameTableBank, PPU_UpDown_Clip, byVramWriteEnable;
  WORD PPU_SP_Height;
  int SpriteJustHit;
  BYTE *ppu_bg_base;
  BYTE *ppu_sp_base;

  BYTE FrameIRQ_Enable;
  WORD FrameStep, FrameSkip, FrameCnt;

  BYTE *rom_bank[4];
  BYTE *sram_bank;
  BYTE *ppu_bank[16];

  BYTE RAM[RAM_SIZE];
  BYTE SRAM[SRAM_SIZE];
  BYTE PPURAM[PPURAM_SIZE];
  BYTE SPRRAM[SPRRAM_SIZE];
  BYTE APU_Reg[0x18];
  WORD NesPalette[64];
  WORD PalTable[32];
  BYTE ChrBuf[256 * 2 * 8 * 8];
  BYTE ChrBufUpdate;
  WORD WorkFrame[NES_DISP_WIDTH * NES_DISP_HEIGHT];
  bool ntsc_palette_active;

  DWORD PAD1_Latch, PAD2_Latch, PAD_System;
  DWORD PAD1_Bit, PAD2_Bit;
};

static QuickState quick_state;
static bool quick_state_valid = false;

enum PointerRegion {
  PTR_NONE = 0,
  PTR_ROM = 1,
  PTR_SRAM = 2,
  PTR_PPURAM = 3,
  PTR_CHRBUF = 4,
  PTR_VROM = 5
};

struct PtrRef {
  DWORD region;
  DWORD offset;
};

struct QuickStateFile {
  char magic[8];
  DWORD version;

  WORD PC;
  BYTE SP, F, A, X, Y;
  BYTE IRQ_State, IRQ_Wiring, NMI_State, NMI_Wiring;
  WORD g_wPassedClocks;

  BYTE PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7;
  BYTE PPU_Scr_V, PPU_Scr_V_Next;
  BYTE PPU_Scr_V_Byte, PPU_Scr_V_Byte_Next;
  BYTE PPU_Scr_V_Bit, PPU_Scr_V_Bit_Next;
  BYTE PPU_Scr_H, PPU_Scr_H_Next;
  BYTE PPU_Scr_H_Byte, PPU_Scr_H_Byte_Next;
  BYTE PPU_Scr_H_Bit, PPU_Scr_H_Bit_Next;
  BYTE PPU_Latch_Flag;
  WORD PPU_Addr, PPU_Temp, PPU_Increment, PPU_Scanline;
  BYTE PPU_NameTableBank, PPU_UpDown_Clip, byVramWriteEnable;
  WORD PPU_SP_Height;
  int SpriteJustHit;
  PtrRef ppu_bg_base;
  PtrRef ppu_sp_base;

  BYTE FrameIRQ_Enable;
  WORD FrameStep, FrameSkip, FrameCnt;

  PtrRef rom_bank[4];
  PtrRef sram_bank;
  PtrRef ppu_bank[16];

  BYTE RAM[RAM_SIZE];
  BYTE SRAM[SRAM_SIZE];
  BYTE PPURAM[PPURAM_SIZE];
  BYTE SPRRAM[SPRRAM_SIZE];
  BYTE APU_Reg[0x18];
  WORD NesPalette[64];
  WORD PalTable[32];
  BYTE ChrBufUpdate;
  BYTE ntsc_palette_active;

  DWORD PAD1_Latch, PAD2_Latch, PAD_System;
  DWORD PAD1_Bit, PAD2_Bit;
};

static bool encode_pointer(BYTE *ptr, PtrRef *out) {
  if (!ptr) {
    out->region = PTR_NONE;
    out->offset = 0;
    return true;
  }

  if (ROM && ptr >= ROM && ptr < ROM + (NesHeader.byRomSize * 0x4000)) {
    out->region = PTR_ROM;
    out->offset = (DWORD)(ptr - ROM);
    return true;
  }
  if (ptr >= SRAM && ptr < SRAM + SRAM_SIZE) {
    out->region = PTR_SRAM;
    out->offset = (DWORD)(ptr - SRAM);
    return true;
  }
  if (ptr >= PPURAM && ptr < PPURAM + PPURAM_SIZE) {
    out->region = PTR_PPURAM;
    out->offset = (DWORD)(ptr - PPURAM);
    return true;
  }
  if (ptr >= ChrBuf && ptr < ChrBuf + CHRBUF_SIZE) {
    out->region = PTR_CHRBUF;
    out->offset = (DWORD)(ptr - ChrBuf);
    return true;
  }
  if (VROM && ptr >= VROM && ptr < VROM + (NesHeader.byVRomSize * 0x2000)) {
    out->region = PTR_VROM;
    out->offset = (DWORD)(ptr - VROM);
    return true;
  }

  return false;
}

static BYTE *decode_pointer(const PtrRef *ref) {
  switch (ref->region) {
    case PTR_NONE:   return NULL;
    case PTR_ROM:    return ROM ? ROM + ref->offset : NULL;
    case PTR_SRAM:   return SRAM + ref->offset;
    case PTR_PPURAM: return PPURAM + ref->offset;
    case PTR_CHRBUF: return ChrBuf + ref->offset;
    case PTR_VROM:   return VROM ? VROM + ref->offset : NULL;
    default:         return NULL;
  }
}

static void apply_nes_palette(const WORD *palette) {
  memcpy(NesPalette, palette, sizeof(NesPalette));

  for (int i = 0; i < 32; ++i) {
    PalTable[i] = NesPalette[PPURAM[0x3f00 + i] & 0x3f];
  }

  WORD bg_color = NesPalette[PPURAM[0x3f00] & 0x3f] | 0x8000;
  PalTable[0x00] = bg_color;
  PalTable[0x04] = bg_color;
  PalTable[0x08] = bg_color;
  PalTable[0x0c] = bg_color;
  PalTable[0x10] = bg_color;
  PalTable[0x14] = bg_color;
  PalTable[0x18] = bg_color;
  PalTable[0x1c] = bg_color;
}

static void toggle_nes_palette(void) {
  ntsc_palette_active = !ntsc_palette_active;
  apply_nes_palette(ntsc_palette_active ? NtscNesPalette : DefaultNesPalette);
  log_message("Palette: %s", ntsc_palette_active ? "NTSC" : "default");
}

static void save_state(void) {
  quick_state.PC = PC;
  quick_state.SP = SP;
  quick_state.F = F;
  quick_state.A = A;
  quick_state.X = X;
  quick_state.Y = Y;
  quick_state.IRQ_State = IRQ_State;
  quick_state.IRQ_Wiring = IRQ_Wiring;
  quick_state.NMI_State = NMI_State;
  quick_state.NMI_Wiring = NMI_Wiring;
  quick_state.g_wPassedClocks = g_wPassedClocks;

  quick_state.PPU_R0 = PPU_R0;
  quick_state.PPU_R1 = PPU_R1;
  quick_state.PPU_R2 = PPU_R2;
  quick_state.PPU_R3 = PPU_R3;
  quick_state.PPU_R7 = PPU_R7;
  quick_state.PPU_Scr_V = PPU_Scr_V;
  quick_state.PPU_Scr_V_Next = PPU_Scr_V_Next;
  quick_state.PPU_Scr_V_Byte = PPU_Scr_V_Byte;
  quick_state.PPU_Scr_V_Byte_Next = PPU_Scr_V_Byte_Next;
  quick_state.PPU_Scr_V_Bit = PPU_Scr_V_Bit;
  quick_state.PPU_Scr_V_Bit_Next = PPU_Scr_V_Bit_Next;
  quick_state.PPU_Scr_H = PPU_Scr_H;
  quick_state.PPU_Scr_H_Next = PPU_Scr_H_Next;
  quick_state.PPU_Scr_H_Byte = PPU_Scr_H_Byte;
  quick_state.PPU_Scr_H_Byte_Next = PPU_Scr_H_Byte_Next;
  quick_state.PPU_Scr_H_Bit = PPU_Scr_H_Bit;
  quick_state.PPU_Scr_H_Bit_Next = PPU_Scr_H_Bit_Next;
  quick_state.PPU_Latch_Flag = PPU_Latch_Flag;
  quick_state.PPU_Addr = PPU_Addr;
  quick_state.PPU_Temp = PPU_Temp;
  quick_state.PPU_Increment = PPU_Increment;
  quick_state.PPU_Scanline = PPU_Scanline;
  quick_state.PPU_NameTableBank = PPU_NameTableBank;
  quick_state.PPU_UpDown_Clip = PPU_UpDown_Clip;
  quick_state.byVramWriteEnable = byVramWriteEnable;
  quick_state.PPU_SP_Height = PPU_SP_Height;
  quick_state.SpriteJustHit = SpriteJustHit;
  quick_state.ppu_bg_base = PPU_BG_Base;
  quick_state.ppu_sp_base = PPU_SP_Base;

  quick_state.FrameIRQ_Enable = FrameIRQ_Enable;
  quick_state.FrameStep = FrameStep;
  quick_state.FrameSkip = FrameSkip;
  quick_state.FrameCnt = FrameCnt;

  quick_state.rom_bank[0] = ROMBANK0;
  quick_state.rom_bank[1] = ROMBANK1;
  quick_state.rom_bank[2] = ROMBANK2;
  quick_state.rom_bank[3] = ROMBANK3;
  quick_state.sram_bank = SRAMBANK;
  for (int i = 0; i < 16; ++i)
    quick_state.ppu_bank[i] = PPUBANK[i];

  memcpy(quick_state.RAM, RAM, RAM_SIZE);
  memcpy(quick_state.SRAM, SRAM, SRAM_SIZE);
  memcpy(quick_state.PPURAM, PPURAM, PPURAM_SIZE);
  memcpy(quick_state.SPRRAM, SPRRAM, SPRRAM_SIZE);
  memcpy(quick_state.APU_Reg, APU_Reg, sizeof quick_state.APU_Reg);
  memcpy(quick_state.NesPalette, NesPalette, sizeof quick_state.NesPalette);
  memcpy(quick_state.PalTable, PalTable, sizeof quick_state.PalTable);
  memcpy(quick_state.ChrBuf, ChrBuf, sizeof quick_state.ChrBuf);
  quick_state.ChrBufUpdate = ChrBufUpdate;
  memcpy(quick_state.WorkFrame, WorkFrame, sizeof quick_state.WorkFrame);
  quick_state.ntsc_palette_active = ntsc_palette_active;

  quick_state.PAD1_Latch = PAD1_Latch;
  quick_state.PAD2_Latch = PAD2_Latch;
  quick_state.PAD_System = PAD_System;
  quick_state.PAD1_Bit = PAD1_Bit;
  quick_state.PAD2_Bit = PAD2_Bit;
  quick_state_valid = true;
}

static bool save_state_file(void) {
  QuickStateFile disk_state;
  memset(&disk_state, 0, sizeof(disk_state));
  memcpy(disk_state.magic, "NSSAVE1", 8);
  disk_state.version = 2;

  disk_state.PC = quick_state.PC;
  disk_state.SP = quick_state.SP;
  disk_state.F = quick_state.F;
  disk_state.A = quick_state.A;
  disk_state.X = quick_state.X;
  disk_state.Y = quick_state.Y;
  disk_state.IRQ_State = quick_state.IRQ_State;
  disk_state.IRQ_Wiring = quick_state.IRQ_Wiring;
  disk_state.NMI_State = quick_state.NMI_State;
  disk_state.NMI_Wiring = quick_state.NMI_Wiring;
  disk_state.g_wPassedClocks = quick_state.g_wPassedClocks;

  disk_state.PPU_R0 = quick_state.PPU_R0;
  disk_state.PPU_R1 = quick_state.PPU_R1;
  disk_state.PPU_R2 = quick_state.PPU_R2;
  disk_state.PPU_R3 = quick_state.PPU_R3;
  disk_state.PPU_R7 = quick_state.PPU_R7;
  disk_state.PPU_Scr_V = quick_state.PPU_Scr_V;
  disk_state.PPU_Scr_V_Next = quick_state.PPU_Scr_V_Next;
  disk_state.PPU_Scr_V_Byte = quick_state.PPU_Scr_V_Byte;
  disk_state.PPU_Scr_V_Byte_Next = quick_state.PPU_Scr_V_Byte_Next;
  disk_state.PPU_Scr_V_Bit = quick_state.PPU_Scr_V_Bit;
  disk_state.PPU_Scr_V_Bit_Next = quick_state.PPU_Scr_V_Bit_Next;
  disk_state.PPU_Scr_H = quick_state.PPU_Scr_H;
  disk_state.PPU_Scr_H_Next = quick_state.PPU_Scr_H_Next;
  disk_state.PPU_Scr_H_Byte = quick_state.PPU_Scr_H_Byte;
  disk_state.PPU_Scr_H_Byte_Next = quick_state.PPU_Scr_H_Byte_Next;
  disk_state.PPU_Scr_H_Bit = quick_state.PPU_Scr_H_Bit;
  disk_state.PPU_Scr_H_Bit_Next = quick_state.PPU_Scr_H_Bit_Next;
  disk_state.PPU_Latch_Flag = quick_state.PPU_Latch_Flag;
  disk_state.PPU_Addr = quick_state.PPU_Addr;
  disk_state.PPU_Temp = quick_state.PPU_Temp;
  disk_state.PPU_Increment = quick_state.PPU_Increment;
  disk_state.PPU_Scanline = quick_state.PPU_Scanline;
  disk_state.PPU_NameTableBank = quick_state.PPU_NameTableBank;
  disk_state.PPU_UpDown_Clip = quick_state.PPU_UpDown_Clip;
  disk_state.byVramWriteEnable = quick_state.byVramWriteEnable;
  disk_state.PPU_SP_Height = quick_state.PPU_SP_Height;
  disk_state.SpriteJustHit = quick_state.SpriteJustHit;

  if (!encode_pointer(quick_state.ppu_bg_base, &disk_state.ppu_bg_base) ||
      !encode_pointer(quick_state.ppu_sp_base, &disk_state.ppu_sp_base)) {
    log_message("Save state pointer encoding failed");
    return false;
  }

  disk_state.FrameIRQ_Enable = quick_state.FrameIRQ_Enable;
  disk_state.FrameStep = quick_state.FrameStep;
  disk_state.FrameSkip = quick_state.FrameSkip;
  disk_state.FrameCnt = quick_state.FrameCnt;

  for (int i = 0; i < 4; ++i) {
    if (!encode_pointer(quick_state.rom_bank[i], &disk_state.rom_bank[i])) {
      log_message("Save state ROM bank encoding failed");
      return false;
    }
  }
  if (!encode_pointer(quick_state.sram_bank, &disk_state.sram_bank)) {
    log_message("Save state SRAM bank encoding failed");
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    if (!encode_pointer(quick_state.ppu_bank[i], &disk_state.ppu_bank[i])) {
      log_message("Save state PPU bank encoding failed");
      return false;
    }
  }

  memcpy(disk_state.RAM, quick_state.RAM, sizeof(disk_state.RAM));
  memcpy(disk_state.SRAM, quick_state.SRAM, sizeof(disk_state.SRAM));
  memcpy(disk_state.PPURAM, quick_state.PPURAM, sizeof(disk_state.PPURAM));
  memcpy(disk_state.SPRRAM, quick_state.SPRRAM, sizeof(disk_state.SPRRAM));
  memcpy(disk_state.APU_Reg, quick_state.APU_Reg, sizeof(disk_state.APU_Reg));
  memcpy(disk_state.NesPalette, quick_state.NesPalette, sizeof(disk_state.NesPalette));
  memcpy(disk_state.PalTable, quick_state.PalTable, sizeof(disk_state.PalTable));
  disk_state.ChrBufUpdate = 0xff;
  disk_state.ntsc_palette_active = quick_state.ntsc_palette_active ? 1 : 0;
  disk_state.PAD1_Latch = quick_state.PAD1_Latch;
  disk_state.PAD2_Latch = quick_state.PAD2_Latch;
  disk_state.PAD_System = quick_state.PAD_System;
  disk_state.PAD1_Bit = quick_state.PAD1_Bit;
  disk_state.PAD2_Bit = quick_state.PAD2_Bit;

  FILE *fp = fopen(state_name, "wb");
  if (!fp) {
    log_message("Failed to open state file for write: %s", state_name);
    return false;
  }
  bool ok = (fwrite(&disk_state, sizeof(disk_state), 1, fp) == 1);
  fclose(fp);
  if (!ok) {
    log_message("Failed to write state file: %s", state_name);
    return false;
  }
  return true;
}

static void load_state(void) {
  if (!quick_state_valid)
    return;

  PC = quick_state.PC;
  SP = quick_state.SP;
  F = quick_state.F;
  A = quick_state.A;
  X = quick_state.X;
  Y = quick_state.Y;
  IRQ_State = quick_state.IRQ_State;
  IRQ_Wiring = quick_state.IRQ_Wiring;
  NMI_State = quick_state.NMI_State;
  NMI_Wiring = quick_state.NMI_Wiring;
  g_wPassedClocks = quick_state.g_wPassedClocks;

  PPU_R0 = quick_state.PPU_R0;
  PPU_R1 = quick_state.PPU_R1;
  PPU_R2 = quick_state.PPU_R2;
  PPU_R3 = quick_state.PPU_R3;
  PPU_R7 = quick_state.PPU_R7;
  PPU_Scr_V = quick_state.PPU_Scr_V;
  PPU_Scr_V_Next = quick_state.PPU_Scr_V_Next;
  PPU_Scr_V_Byte = quick_state.PPU_Scr_V_Byte;
  PPU_Scr_V_Byte_Next = quick_state.PPU_Scr_V_Byte_Next;
  PPU_Scr_V_Bit = quick_state.PPU_Scr_V_Bit;
  PPU_Scr_V_Bit_Next = quick_state.PPU_Scr_V_Bit_Next;
  PPU_Scr_H = quick_state.PPU_Scr_H;
  PPU_Scr_H_Next = quick_state.PPU_Scr_H_Next;
  PPU_Scr_H_Byte = quick_state.PPU_Scr_H_Byte;
  PPU_Scr_H_Byte_Next = quick_state.PPU_Scr_H_Byte_Next;
  PPU_Scr_H_Bit = quick_state.PPU_Scr_H_Bit;
  PPU_Scr_H_Bit_Next = quick_state.PPU_Scr_H_Bit_Next;
  PPU_Latch_Flag = quick_state.PPU_Latch_Flag;
  PPU_Addr = quick_state.PPU_Addr;
  PPU_Temp = quick_state.PPU_Temp;
  PPU_Increment = quick_state.PPU_Increment;
  PPU_Scanline = quick_state.PPU_Scanline;
  PPU_NameTableBank = quick_state.PPU_NameTableBank;
  PPU_UpDown_Clip = quick_state.PPU_UpDown_Clip;
  byVramWriteEnable = quick_state.byVramWriteEnable;
  PPU_SP_Height = quick_state.PPU_SP_Height;
  SpriteJustHit = quick_state.SpriteJustHit;
  PPU_BG_Base = quick_state.ppu_bg_base;
  PPU_SP_Base = quick_state.ppu_sp_base;

  FrameIRQ_Enable = quick_state.FrameIRQ_Enable;
  FrameStep = quick_state.FrameStep;
  FrameSkip = quick_state.FrameSkip;
  FrameCnt = quick_state.FrameCnt;

  ROMBANK0 = quick_state.rom_bank[0];
  ROMBANK1 = quick_state.rom_bank[1];
  ROMBANK2 = quick_state.rom_bank[2];
  ROMBANK3 = quick_state.rom_bank[3];
  SRAMBANK = quick_state.sram_bank;
  for (int i = 0; i < 16; ++i)
    PPUBANK[i] = quick_state.ppu_bank[i];

  memcpy(RAM, quick_state.RAM, RAM_SIZE);
  memcpy(SRAM, quick_state.SRAM, SRAM_SIZE);
  memcpy(PPURAM, quick_state.PPURAM, PPURAM_SIZE);
  memcpy(SPRRAM, quick_state.SPRRAM, SPRRAM_SIZE);
  memcpy(APU_Reg, quick_state.APU_Reg, sizeof quick_state.APU_Reg);
  memcpy(NesPalette, quick_state.NesPalette, sizeof quick_state.NesPalette);
  memcpy(PalTable, quick_state.PalTable, sizeof quick_state.PalTable);
  memcpy(ChrBuf, quick_state.ChrBuf, sizeof quick_state.ChrBuf);
  ChrBufUpdate = quick_state.ChrBufUpdate;
  memcpy(WorkFrame, quick_state.WorkFrame, sizeof quick_state.WorkFrame);
  ntsc_palette_active = quick_state.ntsc_palette_active;

  if (ChrBufUpdate)
    InfoNES_SetupChr();

  PAD1_Latch = quick_state.PAD1_Latch;
  PAD2_Latch = quick_state.PAD2_Latch;
  PAD_System = quick_state.PAD_System;
  PAD1_Bit = quick_state.PAD1_Bit;
  PAD2_Bit = quick_state.PAD2_Bit;
}

static bool load_state_file(void) {
  QuickStateFile disk_state;
  FILE *fp = fopen(state_name, "rb");
  if (!fp)
    return false;

  bool ok = (fread(&disk_state, sizeof(disk_state), 1, fp) == 1);
  fclose(fp);
  if (!ok || memcmp(disk_state.magic, "NSSAVE1", 8) != 0 || disk_state.version != 2) {
    log_message("Invalid state file: %s", state_name);
    return false;
  }

  quick_state.PC = disk_state.PC;
  quick_state.SP = disk_state.SP;
  quick_state.F = disk_state.F;
  quick_state.A = disk_state.A;
  quick_state.X = disk_state.X;
  quick_state.Y = disk_state.Y;
  quick_state.IRQ_State = disk_state.IRQ_State;
  quick_state.IRQ_Wiring = disk_state.IRQ_Wiring;
  quick_state.NMI_State = disk_state.NMI_State;
  quick_state.NMI_Wiring = disk_state.NMI_Wiring;
  quick_state.g_wPassedClocks = disk_state.g_wPassedClocks;

  quick_state.PPU_R0 = disk_state.PPU_R0;
  quick_state.PPU_R1 = disk_state.PPU_R1;
  quick_state.PPU_R2 = disk_state.PPU_R2;
  quick_state.PPU_R3 = disk_state.PPU_R3;
  quick_state.PPU_R7 = disk_state.PPU_R7;
  quick_state.PPU_Scr_V = disk_state.PPU_Scr_V;
  quick_state.PPU_Scr_V_Next = disk_state.PPU_Scr_V_Next;
  quick_state.PPU_Scr_V_Byte = disk_state.PPU_Scr_V_Byte;
  quick_state.PPU_Scr_V_Byte_Next = disk_state.PPU_Scr_V_Byte_Next;
  quick_state.PPU_Scr_V_Bit = disk_state.PPU_Scr_V_Bit;
  quick_state.PPU_Scr_V_Bit_Next = disk_state.PPU_Scr_V_Bit_Next;
  quick_state.PPU_Scr_H = disk_state.PPU_Scr_H;
  quick_state.PPU_Scr_H_Next = disk_state.PPU_Scr_H_Next;
  quick_state.PPU_Scr_H_Byte = disk_state.PPU_Scr_H_Byte;
  quick_state.PPU_Scr_H_Byte_Next = disk_state.PPU_Scr_H_Byte_Next;
  quick_state.PPU_Scr_H_Bit = disk_state.PPU_Scr_H_Bit;
  quick_state.PPU_Scr_H_Bit_Next = disk_state.PPU_Scr_H_Bit_Next;
  quick_state.PPU_Latch_Flag = disk_state.PPU_Latch_Flag;
  quick_state.PPU_Addr = disk_state.PPU_Addr;
  quick_state.PPU_Temp = disk_state.PPU_Temp;
  quick_state.PPU_Increment = disk_state.PPU_Increment;
  quick_state.PPU_Scanline = disk_state.PPU_Scanline;
  quick_state.PPU_NameTableBank = disk_state.PPU_NameTableBank;
  quick_state.PPU_UpDown_Clip = disk_state.PPU_UpDown_Clip;
  quick_state.byVramWriteEnable = disk_state.byVramWriteEnable;
  quick_state.PPU_SP_Height = disk_state.PPU_SP_Height;
  quick_state.SpriteJustHit = disk_state.SpriteJustHit;
  quick_state.ppu_bg_base = decode_pointer(&disk_state.ppu_bg_base);
  quick_state.ppu_sp_base = decode_pointer(&disk_state.ppu_sp_base);

  quick_state.FrameIRQ_Enable = disk_state.FrameIRQ_Enable;
  quick_state.FrameStep = disk_state.FrameStep;
  quick_state.FrameSkip = disk_state.FrameSkip;
  quick_state.FrameCnt = disk_state.FrameCnt;

  for (int i = 0; i < 4; ++i)
    quick_state.rom_bank[i] = decode_pointer(&disk_state.rom_bank[i]);
  quick_state.sram_bank = decode_pointer(&disk_state.sram_bank);
  for (int i = 0; i < 16; ++i)
    quick_state.ppu_bank[i] = decode_pointer(&disk_state.ppu_bank[i]);

  memcpy(quick_state.RAM, disk_state.RAM, sizeof(quick_state.RAM));
  memcpy(quick_state.SRAM, disk_state.SRAM, sizeof(quick_state.SRAM));
  memcpy(quick_state.PPURAM, disk_state.PPURAM, sizeof(quick_state.PPURAM));
  memcpy(quick_state.SPRRAM, disk_state.SPRRAM, sizeof(quick_state.SPRRAM));
  memcpy(quick_state.APU_Reg, disk_state.APU_Reg, sizeof(quick_state.APU_Reg));
  memcpy(quick_state.NesPalette, disk_state.NesPalette, sizeof(quick_state.NesPalette));
  memcpy(quick_state.PalTable, disk_state.PalTable, sizeof(quick_state.PalTable));
  InfoNES_MemorySet(quick_state.WorkFrame, 0, sizeof(quick_state.WorkFrame));
  quick_state.ChrBufUpdate = 0xff;
  InfoNES_MemorySet(quick_state.ChrBuf, 0, sizeof(quick_state.ChrBuf));
  quick_state.ntsc_palette_active = (disk_state.ntsc_palette_active != 0);
  quick_state.PAD1_Latch = disk_state.PAD1_Latch;
  quick_state.PAD2_Latch = disk_state.PAD2_Latch;
  quick_state.PAD_System = disk_state.PAD_System;
  quick_state.PAD1_Bit = disk_state.PAD1_Bit;
  quick_state.PAD2_Bit = disk_state.PAD2_Bit;

  for (int i = 0; i < 4; ++i) {
    if (!quick_state.rom_bank[i]) {
      log_message("Invalid ROM bank in state file");
      return false;
    }
  }
  if (!quick_state.sram_bank || !quick_state.ppu_bg_base || !quick_state.ppu_sp_base) {
    log_message("Invalid base pointers in state file");
    return false;
  }
  for (int i = 0; i < 16; ++i) {
    if (!quick_state.ppu_bank[i]) {
      log_message("Invalid PPU bank in state file");
      return false;
    }
  }

  quick_state_valid = true;
  return true;
}

//Match extension
static int has_nes_extension(const char *name) {
  size_t len = strlen(name);
  return (len >= 8 && strcasecmp(name + len - 8, ".nes.tns") == 0);
}

// Scan for ROMs
static void scan_rom_directory(void) {
  rom_browser_count = 0;
  DIR *dir = opendir(program_dir[0] ? program_dir : ".");
  if (!dir) {
    log_message("Failed to open ROM directory: %s", program_dir);
    return;
  }
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL && rom_browser_count < 128) {
    if (!has_nes_extension(entry->d_name)) continue;
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/%s", program_dir, entry->d_name);
    fill_rom_browser_entry(&rom_browser_entries[rom_browser_count], full_path, entry->d_name);
    ++rom_browser_count;
  }
  closedir(dir);
  qsort(rom_browser_entries, rom_browser_count, sizeof(rom_browser_entries[0]), compare_rom_browser_entries);
  log_message("ROM browser found %d ROM(s) in %s", rom_browser_count, program_dir);
}

// D-pad helpers - can use the Nspire DPAD or 8/2/4/6
static int is_up_pressed(void);
static int is_down_pressed(void);
static int is_left_pressed(void);
static int is_right_pressed(void);

// ROM choosing menu — returns 1 if a ROM was chosen, 0 if cancelled
static int choose_rom_from_browser(void) {
  scan_rom_directory();
  if (rom_browser_count == 0) {
    show_msgbox("NesNspire", "No ROM Files found");
    return 0; 
  }

  int index = 0;
  bool up_prev = false;
  bool down_prev = false;
  bool left_prev = false;
  bool right_prev = false;
  bool enter_prev = false;
  bool esc_prev = false;
  bool menu_prev_local = false;

  while (1) {
    render_rom_browser_screen(index);

    bool up_now = is_up_pressed();
    bool down_now = is_down_pressed();
    bool left_now = is_left_pressed();
    bool right_now = is_right_pressed();
    bool enter_now = isKeyPressed(KEY_NSPIRE_ENTER);
    bool esc_now = isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_HOME) || on_key_pressed();
    bool menu_now = isKeyPressed(KEY_NSPIRE_MENU);

    if (up_now && !up_prev && index > 0)
      --index;
    else if (down_now && !down_prev && index + 1 < rom_browser_count)
      ++index;
    else if (left_now && !left_prev) {
      index -= 10;
      if (index < 0)
        index = 0;
    } else if (right_now && !right_prev) {
      index += 10;
      if (index >= rom_browser_count)
        index = rom_browser_count - 1;
    } else if (menu_now && !menu_prev_local) {
      scan_rom_directory();
      if (rom_browser_count == 0) {
        show_msgbox("NESNspire", "No ROM Files found");
        return 0;
      }
      if (index >= rom_browser_count)
        index = rom_browser_count - 1;
    } else if (enter_now && !enter_prev) {
      strncpy(rom_name, rom_browser_entries[index].path, sizeof(rom_name) - 1);
      rom_name[sizeof(rom_name) - 1] = '\0';
      selected_rom_entry = rom_browser_entries[index];
      selected_rom_entry_valid = true;
      log_message("ROM selected: %s", rom_name);
      return 1;
    } else if (esc_now && !esc_prev) {
      log_message("ROM browser exited");
      return 0;
    }

    up_prev = up_now;
    down_prev = down_now;
    left_prev = left_now;
    right_prev = right_now;
    enter_prev = enter_now;
    esc_prev = esc_now;
    menu_prev_local = menu_now;
  }
}

// LCD suspend/resume — OS dialogs need the LCD back in OS mode
static void suspend_lcd_for_ui(void) {
  if (lcd_active) {
    lcd_init(SCR_TYPE_INVALID);
    lcd_active = false;
  }
}

static void resume_lcd_after_ui(void) {
  if (!lcd_active) {
    lcd_active = lcd_init(SCR_320x240_565);
    if (lcd_active)
      lcd_blit(screen_buf, SCR_320x240_565);
  }
}

// D-pad helpers - can use the Nspire DPAD or 8/2/4/6
static int is_up_pressed(void);
static int is_down_pressed(void);
static int is_left_pressed(void);
static int is_right_pressed(void);

static int is_up_pressed(void)    { return isKeyPressed(KEY_NSPIRE_UP)    || isKeyPressed(KEY_NSPIRE_8); }
static int is_down_pressed(void)  { return isKeyPressed(KEY_NSPIRE_DOWN)  || isKeyPressed(KEY_NSPIRE_2); }
static int is_left_pressed(void)  { return isKeyPressed(KEY_NSPIRE_LEFT)  || isKeyPressed(KEY_NSPIRE_4); }
static int is_right_pressed(void) { return isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6); }



static void show_formatted_message(const char *title, const char *fmt, va_list args) {
  char buffer[512];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  suspend_lcd_for_ui();
  show_msgbox(title, buffer);
  resume_lcd_after_ui();
}

// SRAM load/save — pretend to be a battery-backed cart save data (8 KB)
static void load_sram(void) {
  if (!ROM_SRAM) return;
  FILE *fp = fopen(save_name, "rb");
  if (!fp) {
    log_message("No SRAM file present, starting fresh");
    return; 
  }
  fread(SRAM, 1, SRAM_SIZE, fp);
  fclose(fp);
  log_message("SRAM loaded");
}

static void save_sram(void) {
  if (!ROM_SRAM) return;
  FILE *fp = fopen(save_name, "wb");
  if (!fp) {
    log_message("Failed to write SRAM file");
    return;
  }
  fwrite(SRAM, 1, SRAM_SIZE, fp);
  fclose(fp);
  log_message("SRAM saved");
}

int main(int argc, char *argv[]) {
  int rc = 0;

  assert_ndless_rev(2004);
  enable_relative_paths(argv);

  build_program_dir((argc > 0) ? argv[0] : ".");
  build_log_path();
  init_rgb565_lut();

  log_message("NesNspire");
  log_message("Program directory: %s", program_dir);

  lcd_active = lcd_init(SCR_320x240_565);
  if (!lcd_active) {
    log_message("LCD init failed");
    show_msgbox("NesNspire", "LCD init failed");
    return 1;
  }

  memset(screen_buf, 0, sizeof(screen_buf));
  lcd_blit(screen_buf, SCR_320x240_565);

  if (argc >= 2) {
    strncpy(rom_name, argv[1], sizeof(rom_name) - 1);
    rom_name[sizeof(rom_name) - 1] = '\0';
    log_message("ROM passed on argv: %s", rom_name);
    const char *file_name = strrchr(rom_name, '/');
    if (!file_name) file_name = strrchr(rom_name, '\\');
    if (file_name) ++file_name;
    else file_name = rom_name;
    fill_rom_browser_entry(&selected_rom_entry, rom_name, file_name);
    selected_rom_entry_valid = true;
  } else {
    if (!choose_rom_from_browser()) {
      log_message("No ROM selected");
      suspend_lcd_for_ui();
      return 0;
    }
  }

  quit_requested = false;
  menu_prev      = false;
  fs_dec_prev    = false;
  fs_inc_prev    = false;
  ss_save_prev   = false;
  ss_load_prev   = false;
  palette_prev   = false;
  ntsc_palette_active = false;
  apply_nes_palette(DefaultNesPalette);
  quick_state_valid = false;

  APU_Mute = 1;
  log_message("No speaker on Nspire"); // Yes I know about the Vogtinator mod 

  build_save_path(rom_name);
  build_state_path(rom_name);
  log_message("ROM path: %s", rom_name);
  log_message("SRAM path: %s", save_name);
  log_message("State path: %s", state_name);

  if (!selected_rom_entry_valid) {
    const char *file_name = strrchr(rom_name, '/');
    if (!file_name) file_name = strrchr(rom_name, '\\');
    if (file_name) ++file_name;
    else file_name = rom_name;
    fill_rom_browser_entry(&selected_rom_entry, rom_name, file_name);
    selected_rom_entry_valid = true;
  }

  render_loading_screen(&selected_rom_entry);
  log_message("Loading details: title=%s region=%s mapper=%d prg=%dKB chr=%dKB",
    selected_rom_entry.title,
    selected_rom_entry.region,
    selected_rom_entry.mapper,
    selected_rom_entry.prg_kb,
    selected_rom_entry.chr_kb);

  if (InfoNES_Load(rom_name) == 0) {
    log_message("ROM loaded");
    FrameSkip = DEFAULT_FRAME_SKIP;
    log_message("FrameSkip=%u", (unsigned)FrameSkip);
    load_sram();
    //Main Hoon Na!
    InfoNES_Main();
    save_sram();
  } else {
    log_message("ROM load failed");
    InfoNES_MessageBox("Failed to load ROM:\n%s", rom_name);
    rc = 1;
  }

  suspend_lcd_for_ui();
  log_message("Shutdown rc=%d", rc);
  if (log_fp) {
    fclose(log_fp);
    log_fp = NULL;
  }
  return rc;
}

//Core callbacks
//Called once per frame :  return -1 to exit the emulation loop
int InfoNES_Menu() {
  return quit_requested ? -1 : 0;
}

// Parse iNES header, allocate and fill PRG/CHR buffers
int InfoNES_ReadRom(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    log_message("Failed to open ROM: %s", filename);
    return -1;
  }

  fread(&NesHeader, sizeof(NesHeader), 1, fp);

  if (memcmp(NesHeader.byID, "NES\x1a", 4) != 0) {
    log_message("Bad ROM header: %s", filename);
    fclose(fp);
    return -1;
  }

  memset(SRAM, 0, SRAM_SIZE);

  if (NesHeader.byInfo1 & 4) // 512-byte trainer 
    fread(&SRAM[0x1000], 512, 1, fp);

  ROM = (BYTE *)malloc(NesHeader.byRomSize * 0x4000);
  if (!ROM) {
    log_message("ROM malloc failed");
    fclose(fp);
    return -1;
  }
  fread(ROM, 0x4000, NesHeader.byRomSize, fp);

  if (NesHeader.byVRomSize > 0) {
    VROM = (BYTE *)malloc(NesHeader.byVRomSize * 0x2000);
    if (!VROM) {
      log_message("VROM malloc failed");
      free(ROM); ROM = NULL;
      fclose(fp);
      return -1;
    }
    fread(VROM, 0x2000, NesHeader.byVRomSize, fp);
  }

  fclose(fp);
  log_message("ROM: PRG=%ux16KB CHR=%ux8KB info1=0x%02x info2=0x%02x",
    (unsigned)NesHeader.byRomSize, (unsigned)NesHeader.byVRomSize,
    (unsigned)NesHeader.byInfo1,   (unsigned)NesHeader.byInfo2);
  return 0;
}

void InfoNES_ReleaseRom() {
  free(ROM);  ROM  = NULL;
  free(VROM); VROM = NULL;
}

//Stretch to fit Nspire screen
void InfoNES_LoadFrame() {
  const uint16_t *lut = rgb565_lut;

  for (int y = 0; y < NES_DISP_HEIGHT; ++y) {
    WORD     *src = &WorkFrame[y * NES_DISP_WIDTH];
    uint16_t *dst = &screen_buf[y * 320];
    for (int x = 0; x < NES_DISP_WIDTH; x += 4) {
      uint16_t c0 = lut[src[x + 0] & 0x7fff];
      uint16_t c1 = lut[src[x + 1] & 0x7fff];
      uint16_t c2 = lut[src[x + 2] & 0x7fff];
      uint16_t c3 = lut[src[x + 3] & 0x7fff];
      dst[0] = c0;
      dst[1] = c0;
      dst[2] = c1;
      dst[3] = c2;
      dst[4] = c3;
      dst += 5;
    }
  }
  lcd_blit(screen_buf, SCR_320x240_565);
}

// Key mapping:
//   NES Right/Left/Down/Up  ← arrow keys or 6/4/2/8
//   NES Start  ← Enter      NES Select ← Esc
//   NES B      ← Shift       NES A      ← Ctrl
//
// Hotkeys (Doc = modifier):
//   Doc+1 / Doc+3  -> FrameSkip -/+ (clamped 0-3)
//   Menu           → toggle PPU clip
//   S              → save state
//   L              → load state
//   Home / ON      → quit
void InfoNES_PadState(DWORD *pad1, DWORD *pad2, DWORD *system) {
  DWORD pad1_val   = 0;
  DWORD system_val = 0;

  bool doc_pressed = isKeyPressed(KEY_NSPIRE_DOC);
  bool menu_dn     = isKeyPressed(KEY_NSPIRE_MENU);
  bool fs_dec_dn   = doc_pressed && isKeyPressed(KEY_NSPIRE_1);
  bool fs_inc_dn   = doc_pressed && isKeyPressed(KEY_NSPIRE_3);

  if (is_right_pressed())             pad1_val |= (1 << 7);
  if (is_left_pressed())              pad1_val |= (1 << 6);
  if (is_down_pressed())              pad1_val |= (1 << 5);
  if (is_up_pressed())                pad1_val |= (1 << 4);
  if (isKeyPressed(KEY_NSPIRE_ENTER)) pad1_val |= (1 << 3);
  if (isKeyPressed(KEY_NSPIRE_ESC))   pad1_val |= (1 << 2);
  if (isKeyPressed(KEY_NSPIRE_SHIFT)) pad1_val |= (1 << 1);
  if (isKeyPressed(KEY_NSPIRE_CTRL))  pad1_val |= (1 << 0);

  if (menu_dn && !menu_prev) {
    PPU_UpDown_Clip = PPU_UpDown_Clip ? 0 : 1;
    log_message("Clip: %s", PPU_UpDown_Clip ? "on" : "off");
  }
  menu_prev = menu_dn;

  if (fs_dec_dn && !fs_dec_prev && FrameSkip > 0) {
    --FrameSkip;
    log_message("FrameSkip=%u", (unsigned)FrameSkip);
  }
  fs_dec_prev = fs_dec_dn;

  if (fs_inc_dn && !fs_inc_prev && FrameSkip < MAX_FRAME_SKIP) {
    ++FrameSkip;
    log_message("FrameSkip=%u", (unsigned)FrameSkip);
  }
  fs_inc_prev = fs_inc_dn;

  bool ss_save_dn = isKeyPressed(KEY_NSPIRE_S);
  bool ss_load_dn = isKeyPressed(KEY_NSPIRE_L);
  bool palette_dn = isKeyPressed(KEY_NSPIRE_P);

  if (ss_save_dn && !ss_save_prev) {
    save_state();
    if (save_state_file())
      log_message("State saved");
  }
  ss_save_prev = ss_save_dn;

  if (ss_load_dn && !ss_load_prev) {
    if (!quick_state_valid)
      load_state_file();
    load_state();
    if (quick_state_valid)
      log_message("State loaded");
  }
  ss_load_prev = ss_load_dn;

  if (palette_dn && !palette_prev) {
    toggle_nes_palette();
  }
  palette_prev = palette_dn;

  if (isKeyPressed(KEY_NSPIRE_HOME) || on_key_pressed()) {
    system_val |= PAD_SYS_QUIT;
    quit_requested = true;
    log_message("Quit");
  }

  *pad1   = pad1_val;
  *pad2   = 0;
  *system = system_val;
}

void *InfoNES_MemoryCopy(void *dest, const void *src, int count) { memcpy(dest, src, count); return dest; }
void *InfoNES_MemorySet(void *dest, int c, int count)            { memset(dest, c, count);   return dest; }

void InfoNES_DebugPrint(char *message) { (void)message; }
void InfoNES_Wait() {}

// Like I said, no speaker on the nspire
void InfoNES_SoundInit(void) {}
int  InfoNES_SoundOpen(int s, int r) { log_message("SoundOpen %d %d (stubbed)", s, r); return 0; }
void InfoNES_SoundClose(void) {}
void InfoNES_SoundOutput(int s, BYTE *w1, BYTE *w2, BYTE *w3, BYTE *w4, BYTE *w5) {
  (void)s; (void)w1; (void)w2; (void)w3; (void)w4; (void)w5;
}

void InfoNES_MessageBox(char *message, ...) {
  va_list args;
  va_start(args, message);
  show_formatted_message("NesNspire", message, args);
  va_end(args);
}
