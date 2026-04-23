// InfoNES port for the Texas Instruments CX II CAS
// Ported for the Nspire CX II CAS by Malik Idrees Hasan Khan
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <libndls.h>
#include <keys.h>
#include <dirent.h>
#include "../InfoNES.h"
#include "../InfoNES_System.h"
#include "../InfoNES_pAPU.h"

char rom_name[256];
char save_name[256];

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

//Framebuffer - this buffer is not the hardware framebuffer but like a sort of shadow copy we fill and push to LCD

static uint16_t screen_buf[320 * 240];

static char program_dir[256];
static char log_path[256];

// ROM browser — up to 128 entries
static char rom_browser_paths[128][256];
static char rom_browser_names[128][128];
static int  rom_browser_count = 0;

// Logging — opens, appends, and closes the file every call - slow, yes but gurantees logs incase of mid-call crash
static void log_message(const char *fmt, ...) {
  if (!log_path[0]) return;
  FILE *fp = fopen(log_path, "a");
  if (!fp) return;
  va_list args;
  va_start(args, fmt);
  vfprintf(fp, fmt, args);
  va_end(args);
  fputc('\n', fp);
  fclose(fp);
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
    strncpy(rom_browser_names[rom_browser_count], entry->d_name,
            sizeof(rom_browser_names[0]) - 1);
    rom_browser_names[rom_browser_count][sizeof(rom_browser_names[0]) - 1] = '\0';
    snprintf(rom_browser_paths[rom_browser_count],
             sizeof(rom_browser_paths[0]),
             "%s/%s", program_dir, entry->d_name);
    ++rom_browser_count;
  }
  closedir(dir);
  log_message("ROM browser found %d ROM(s) in %s", rom_browser_count, program_dir);
}

// ROM choosing menu — returns 1 if a ROM was chosen, 0 if cancelled
static int choose_rom_from_browser(void) {
  scan_rom_directory();
  if (rom_browser_count == 0) {
    show_msgbox("InfoNES", "No ROM Files found");
    return 0; 
  }

  int index = 0;
  while (1) {
    char message[512];
    snprintf(message, sizeof(message),
      "NesNspire\n"
      "A port of InfoNES for the Nspire CX II CAS\n"
      "Malik Idrees Hasan Khan\n\n"
      "Folder:\n%s\n\n"
      "ROM %d/%d\n%s\n\n"
      "Press HOME to cancel",
      program_dir, index + 1, rom_browser_count, rom_browser_names[index]);

    unsigned choice = show_msgbox_3b("NesNspire", message, "Prev", "Load ROM", "Next");
    if (choice == 1) {
      index = (index == 0) ? (rom_browser_count - 1) : (index - 1);
    } else if (choice == 2) {
      strncpy(rom_name, rom_browser_paths[index], sizeof(rom_name) - 1);
      rom_name[sizeof(rom_name) - 1] = '\0';
      log_message("ROM selected: %s", rom_name);
      return 1;
    } else if (choice == 3) {
      index = (index + 1) % rom_browser_count;
    } else {
      log_message("ROM browser exited");
      return 0;
    }
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

// Convert RGB555 to RGB565 , give the greens their extra bit
static uint16_t infones_to_rgb565(WORD pixel) {
  uint16_t rgb555 = pixel & 0x7fff;
  uint16_t r  = (rgb555 >> 10) & 0x1f;
  uint16_t g5 = (rgb555 >>  5) & 0x1f;
  uint16_t b  =  rgb555        & 0x1f;
  uint16_t g6 = (g5 << 1) | (g5 >> 4);
  return (uint16_t)((r << 11) | (g6 << 5) | b);
}

// D-pad helpers - can use the Nspire DPAD or 8/2/4/6
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
  refresh_osscr(); 
  log_message("SRAM saved");
}

int main(int argc, char *argv[]) {
  int rc = 0;

  assert_ndless_rev(2004);
  enable_relative_paths(argv);

  build_program_dir((argc > 0) ? argv[0] : ".");
  build_log_path();

  log_message("NesNspire");
  log_message("Program directory: %s", program_dir);

  if (argc >= 2) {
    strncpy(rom_name, argv[1], sizeof(rom_name) - 1);
    rom_name[sizeof(rom_name) - 1] = '\0';
    log_message("ROM passed on argv: %s", rom_name);
  } else {
    if (!choose_rom_from_browser()) {
      log_message("No ROM selected");
      return 0;
    }
  }

  lcd_active = lcd_init(SCR_320x240_565);
  if (!lcd_active) {
    log_message("LCD init failed");
    show_msgbox("NesNspire", "LCD init failed");
    return 1;
  }

  memset(screen_buf, 0, sizeof(screen_buf));
  lcd_blit(screen_buf, SCR_320x240_565);

  quit_requested = false;
  menu_prev      = false;
  fs_dec_prev    = false;
  fs_inc_prev    = false;

  APU_Mute = 1;
  log_message("No speaker on Nspire"); // Yes I know about the Vogtinator mod 

  build_save_path(rom_name);
  log_message("ROM path: %s", rom_name);
  log_message("SRAM path: %s", save_name);

  if (InfoNES_Load(rom_name) == 0) {
    log_message("ROM loaded");
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
  for (int y = 0; y < NES_DISP_HEIGHT; ++y) {
    WORD     *src = &WorkFrame[y * NES_DISP_WIDTH];
    uint16_t *dst = &screen_buf[y * 320];
    for (int x = 0; x < 320; ++x)
      dst[x] = infones_to_rgb565(src[(x * NES_DISP_WIDTH) / 320]);
  }
  lcd_blit(screen_buf, SCR_320x240_565);
}

// Key mapping:
//   NES Right/Left/Down/Up  ← arrow keys or 6/4/2/8
//   NES Start  ← Enter      NES Select ← Esc
//   NES B      ← Shift       NES A      ← Ctrl
//
// Hotkeys (Doc = modifier):
//   Doc+1 / Doc+3  → FrameSkip -/+  (clamped 0–5)
//   Menu           → toggle PPU clip
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

  if (fs_inc_dn && !fs_inc_prev && FrameSkip < 5) {
    ++FrameSkip;
    log_message("FrameSkip=%u", (unsigned)FrameSkip);
  }
  fs_inc_prev = fs_inc_dn;

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
