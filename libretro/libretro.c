
/*
 * Copyright (C) 2017 FIX94
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <malloc.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "mapper.h"
#include "cpu.h"
#include "ppu.h"
#include "mem.h"
#include "input.h"
#include "fm2play.h"
#include "apu.h"
#include "audio.h"
#include "audio_fds.h"
#include "audio_vrc7.h"
#include "libretro.h"

#define AUDIO_DECIMATION_RATE 16

#define DEBUG_HZ 0
#define DEBUG_MAIN_CALLS 0
#define DEBUG_KEY 0
#define DEBUG_LOAD_INFO 1

static const char *VERSION_STRING = "fixNES Alpha v0.8.2";

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;

static void nesEmuDisplayFrame(void);
static void nesEmuMainLoop(void);
static void nesEmuDeinit(void);
static void nesEmuFdsSetup(uint8_t *src, uint8_t *dst);

static void nesEmuHandleKeyDown(unsigned char key, int x, int y);
static void nesEmuHandleKeyUp(unsigned char key, int x, int y);
static void nesEmuHandleSpecialDown(int key, int x, int y);
static void nesEmuHandleSpecialUp(int key, int x, int y);

static uint8_t *emuNesROM = NULL;
static char *emuSaveName = NULL;
static uint8_t *emuPrgRAM = NULL;
static uint32_t emuPrgRAMsize = 0;
//used externally
uint8_t *textureImage = NULL;
bool nesPause = false;
bool ppuDebugPauseFrame = false;
bool doOverscan = true;
bool nesPAL = false;
bool nesEmuNSFPlayback = false;

static bool inPause = false;
static bool inOverscanToggle = false;
static bool inResize = false;
static bool inDiskSwitch = false;

#define DOTS 341

#define VISIBLE_DOTS 256
#define VISIBLE_LINES 240

static const uint32_t visibleImg = VISIBLE_DOTS * VISIBLE_LINES * 4;
static bool emuSaveEnabled = false;
static bool emuFdsHasSideB = false;
static uint16_t mainLoopRuns;
static uint16_t mainLoopPos;
static uint16_t ppuCycleTimer;
static uint16_t cpuCycleTimer;
//from input.c
extern uint8_t inValReads[8];

int load_game(const char *filename)
{
   if (!filename)
      return EXIT_FAILURE;

   puts(VERSION_STRING);

   if ((strstr(filename, ".nes") != NULL || strstr(filename, ".NES") != NULL))
   {
      nesPAL = (strstr(filename, "(E)") != NULL);
      FILE *nesF = fopen(filename, "rb");

      if (!nesF) return EXIT_SUCCESS;

      fseek(nesF, 0, SEEK_END);
      size_t fsize = ftell(nesF);
      rewind(nesF);
      emuNesROM = malloc(fsize);
      fread(emuNesROM, 1, fsize, nesF);
      fclose(nesF);
      uint8_t mapper = ((emuNesROM[6] & 0xF0) >> 4) | ((emuNesROM[7] & 0xF0));
      emuSaveEnabled = (emuNesROM[6] & (1 << 1)) != 0;
      bool trainer = (emuNesROM[6] & (1 << 2)) != 0;
      uint32_t prgROMsize = emuNesROM[4] * 0x4000;
      uint32_t chrROMsize = emuNesROM[5] * 0x2000;
      emuPrgRAMsize = emuNesROM[8] * 0x2000;

      if (emuPrgRAMsize == 0) emuPrgRAMsize = 0x2000;

      emuPrgRAM = malloc(emuPrgRAMsize);
      uint8_t *prgROM = emuNesROM + 16;

      if (trainer)
      {
         memcpy(emuPrgRAM + 0x1000, prgROM, 0x200);
         prgROM += 512;
      }

      uint8_t *chrROM = NULL;

      if (chrROMsize)
      {
         chrROM = emuNesROM + 16 + prgROMsize;

         if (trainer) chrROM += 512;
      }

      apuInitBufs();
      cpuInit();
      ppuInit();
      memInit();
      apuInit();
      inputInit();
#if DEBUG_LOAD_INFO
      printf("Read in %s\n", filename);
      printf("Used Mapper: %i\n", mapper);
      printf("PRG: 0x%x bytes PRG RAM: 0x%x bytes CHR: 0x%x bytes\n", prgROMsize, emuPrgRAMsize,
             chrROMsize);
#endif

      if (!mapperInit(mapper, prgROM, prgROMsize, emuPrgRAM, emuPrgRAMsize, chrROM, chrROMsize))
      {
         printf("Mapper init failed!\n");
         free(emuNesROM);
         emuNesROM = NULL;
         return EXIT_SUCCESS;
      }

      if (emuNesROM[6] & 8)
         ppuSetNameTbl4Screen();
      else if (emuNesROM[6] & 1)
         ppuSetNameTblVertical();
      else
         ppuSetNameTblHorizontal();

#if DEBUG_LOAD_INFO
      printf("Trainer: %i Saving: %i VRAM Mode: %s\n", trainer, emuSaveEnabled,
             (emuNesROM[6] & 1) ? "Vertical" :
             ((!(emuNesROM[6] & 1)) ? "Horizontal" : "4-Screen"));
#endif

      if (emuSaveEnabled)
      {
         emuSaveName = strdup(filename);
         memcpy(emuSaveName + strlen(emuSaveName) - 3, "sav", 3);
         FILE *save = fopen(emuSaveName, "rb");

         if (save)
         {
            fread(emuPrgRAM, 1, emuPrgRAMsize, save);
            fclose(save);
         }
      }
   }
   else if ((strstr(filename, ".nsf") != NULL || strstr(filename, ".NSF") != NULL))
   {
      FILE *nesF = fopen(filename, "rb");

      if (!nesF) return EXIT_SUCCESS;

      fseek(nesF, 0, SEEK_END);
      size_t fsize = ftell(nesF);
      rewind(nesF);
      emuNesROM = malloc(fsize);
      fread(emuNesROM, 1, fsize, nesF);
      fclose(nesF);
      emuPrgRAMsize = 0x2000;
      emuPrgRAM = malloc(emuPrgRAMsize);

      if (!mapperInitNSF(emuNesROM, fsize, emuPrgRAM, emuPrgRAMsize))
      {
         printf("NSF init failed!\n");
         free(emuNesROM);
         return EXIT_SUCCESS;
      }

      nesEmuNSFPlayback = true;
   }
   else if ((strstr(filename, ".fds") != NULL || strstr(filename, ".FDS") != NULL
             || strstr(filename, ".qd") != NULL || strstr(filename, ".QD") != NULL))
   {
      emuSaveName = strdup(filename);
      memcpy(emuSaveName + strlen(emuSaveName) - 3, "sav", 3);
      bool saveValid = false;
      FILE *save = fopen(emuSaveName, "rb");

      if (save)
      {
         fseek(save, 0, SEEK_END);
         size_t saveSize = ftell(save);

         if (saveSize == 0x10000 || saveSize == 0x20000)
         {
            emuNesROM = malloc(saveSize);
            rewind(save);
            fread(emuNesROM, 1, saveSize, save);
            saveValid = true;

            if (saveSize == 0x20000)
               emuFdsHasSideB = true;
         }
         else
            printf("Save file ignored\n");

         fclose(save);
      }

      if (!saveValid)
      {
         FILE *nesF = fopen(filename, "rb");

         if (!nesF) return EXIT_SUCCESS;

         fseek(nesF, 0, SEEK_END);
         size_t fsize = ftell(nesF);
         rewind(nesF);
         uint8_t *nesFread = malloc(fsize);
         fread(nesFread, 1, fsize, nesF);
         fclose(nesF);
         uint8_t *fds_src;
         uint32_t fds_src_len;

         if (nesFread[0] == 0x46 && nesFread[1] == 0x44 && nesFread[2] == 0x53)
         {
            fds_src = nesFread + 0x10;
            fds_src_len = fsize - 0x10;
         }
         else
         {
            fds_src = nesFread;
            fds_src_len = fsize;
         }

         bool fds_no_crc = (fds_src[0x38] == 0x02 && fds_src[0x3A] == 0x03
                            && fds_src[0x3A] != 0x02 && fds_src[0x3E] != 0x03);

         if (fds_no_crc)
         {
            if (fds_src_len == 0x1FFB8)
            {
               emuFdsHasSideB = true;
               emuNesROM = malloc(0x20000);
               memset(emuNesROM, 0, 0x20000);
               nesEmuFdsSetup(fds_src, emuNesROM); //setup individually
               nesEmuFdsSetup(fds_src + 0xFFDC, emuNesROM + 0x10000);
            }
            else if (fds_src_len == 0xFFDC)
            {
               emuNesROM = malloc(0x10000);
               memset(emuNesROM, 0, 0x10000);
               nesEmuFdsSetup(fds_src, emuNesROM);
            }
         }
         else
         {
            if (fds_src_len == 0x20000)
            {
               emuFdsHasSideB = true;
               emuNesROM = malloc(0x20000);
               memcpy(emuNesROM, fds_src, 0x20000);
            }
            else if (fds_src_len == 0x10000)
            {
               emuNesROM = malloc(0x10000);
               memcpy(emuNesROM, fds_src, 0x10000);
            }
         }

         free(nesFread);
      }

      emuPrgRAMsize = 0x8000;
      emuPrgRAM = malloc(emuPrgRAMsize);
      apuInitBufs();
      cpuInit();
      ppuInit();
      memInit();
      apuInit();
      inputInit();

      if (!mapperInitFDS(emuNesROM, emuFdsHasSideB, emuPrgRAM, emuPrgRAMsize))
      {
         printf("FDS init failed!\n");
         free(emuNesROM);
         emuNesROM = NULL;
         return EXIT_SUCCESS;
      }
   }

   if (emuNesROM == NULL)
      return EXIT_SUCCESS;

#if WINDOWS_BUILD
#if DEBUG_HZ
   emuFrameStart = GetTickCount();
#endif
#if DEBUG_MAIN_CALLS
   emuMainFrameStart = GetTickCount();
#endif
#endif
   textureImage = malloc(visibleImg);
   memset(textureImage, 0, visibleImg);
   //make sure image is visible
   uint32_t i;

   for (i = 0; i < visibleImg; i += 4)
      textureImage[i + 3] = 0xFF;

   cpuCycleTimer = nesPAL ? 16 : 12;
   //do one scanline per idle loop
   ppuCycleTimer = nesPAL ? 5 : 4;
   mainLoopRuns = nesPAL ? DOTS * ppuCycleTimer : DOTS * ppuCycleTimer;
   mainLoopPos = mainLoopRuns;
   return EXIT_SUCCESS;
}

static volatile bool emuRenderFrame = false;

static void nesEmuDeinit(void)
{
   //printf("\n");
   emuRenderFrame = false;
   apuDeinitBufs();

   if (emuNesROM != NULL)
   {
      if (!nesEmuNSFPlayback && fdsEnabled)
      {
         FILE *save = fopen(emuSaveName, "wb");

         if (save)
         {
            if (emuFdsHasSideB)
               fwrite(emuNesROM, 1, 0x20000, save);
            else
               fwrite(emuNesROM, 1, 0x10000, save);

            fclose(save);
         }
      }

      free(emuNesROM);
   }

   emuNesROM = NULL;

   if (emuPrgRAM != NULL)
   {
      if (emuSaveEnabled)
      {
         FILE *save = fopen(emuSaveName, "wb");

         if (save)
         {
            fwrite(emuPrgRAM, 1, emuPrgRAMsize, save);
            fclose(save);
         }
      }

      free(emuPrgRAM);
   }

   emuPrgRAM = NULL;

   if (textureImage != NULL)
      free(textureImage);

   textureImage = NULL;
   //printf("Bye!\n");
}

//used externally
bool emuSkipVsync = false;
bool emuSkipFrame = false;

//static uint32_t mCycles = 0;
static bool emuApuDoCycle = false;

static uint16_t mainClock = 1;
static uint16_t ppuClock = 1;
static uint16_t vrc7Clock = 1;

static void nesEmuMainLoop(void)
{
   do
   {
      if ((!emuSkipVsync && emuRenderFrame) || nesPause)
      {
         return;
      }

      if (mainClock == cpuCycleTimer)
      {
         //runs every second cpu clock
         if (emuApuDoCycle && !apuCycle())
         {
            return;
         }

         emuApuDoCycle ^= true;
         //runs every cpu cycle
         apuClockTimers();

         //main CPU clock
         if (!cpuCycle())
            exit(EXIT_SUCCESS);

         //mapper related irqs
         if (mapperCycle != NULL)
            mapperCycle();

         //mCycles++;
         //channel timer updates
         apuLenCycle();
         mainClock = 1;
      }
      else
         mainClock++;

      if (ppuClock == ppuCycleTimer)
      {
         if (!ppuCycle())
            exit(EXIT_SUCCESS);

         if (!nesEmuNSFPlayback && ppuDrawDone())
         {
            //printf("%i\n",mCycles);
            //mCycles = 0;
            emuRenderFrame = true;

            if (fm2playRunning())
               fm2playUpdate();

            if (ppuDebugPauseFrame)
               nesPause = true;
         }

         ppuClock = 1;
      }
      else
         ppuClock++;

      if (fdsEnabled)
         fdsAudioMasterUpdate();

      if (vrc7enabled)
      {
         if (vrc7Clock == 432)
         {
            vrc7AudioCycle();
            vrc7Clock = 1;
         }
         else
            vrc7Clock++;
      }
   }
   while (mainLoopPos--);

   mainLoopPos = mainLoopRuns;
#if (WINDOWS_BUILD && DEBUG_MAIN_CALLS)
   emuMainTimesCalled++;
   DWORD end = GetTickCount();
   emuMainTotalElapsed += end - emuMainFrameStart;

   if (emuMainTotalElapsed >= 1000)
   {
      printf("\r%i calls, %i skips   ", emuMainTimesCalled, emuMainTimesSkipped);
      emuMainTimesCalled = 0;
      emuMainTimesSkipped = 0;
      emuMainTotalElapsed = 0;
   }

   emuMainFrameStart = end;
#endif
}

extern bool fdsSwitch;

static void nesEmuFdsSetup(uint8_t *src, uint8_t *dst)
{
   memcpy(dst, src, 0x38);
   memcpy(dst + 0x3A, src + 0x38, 2);
   uint16_t cDiskPos = 0x3E;
   uint16_t cROMPos = 0x3A;

   do
   {
      if (src[cROMPos] != 0x03)
         break;

      memcpy(dst + cDiskPos, src + cROMPos, 0x10);
      uint16_t copySize = (*(uint16_t *)(src + cROMPos + 0xD)) + 1;
      cDiskPos += 0x12;
      cROMPos += 0x10;
      memcpy(dst + cDiskPos, src + cROMPos, copySize);
      cDiskPos += copySize + 2;
      cROMPos += copySize;
   }
   while (cROMPos < 0xFFDC && cDiskPos < 0xFFFF);

   printf("%04x -> %04x\n", cROMPos, cDiskPos);
}

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "fixNES";
   info->library_version = "alpha-v0.8.2";
   info->need_fullpath = true;
   info->block_extract = false;
   info->valid_extensions = "nes|fds|qd|nsf";
}

#define DOTS 341
#define VISIBLE_DOTS 256
#define VISIBLE_LINES 240

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width  = VISIBLE_DOTS;
   info->geometry.base_height = VISIBLE_LINES;
   info->geometry.max_width   = VISIBLE_DOTS;
   info->geometry.max_height  = VISIBLE_LINES;
   info->timing.fps           = 60;
   info->timing.sample_rate   = (float)apuGetFrequency() / (float)AUDIO_DECIMATION_RATE;
}

void retro_init(void)
{
   struct retro_log_callback log;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
}

void retro_deinit()
{
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}
void retro_set_audio_sample(retro_audio_sample_t cb) { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}
void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}
void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_reset()
{
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   return false;
}
void retro_cheat_reset()
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}


bool retro_load_game(const struct retro_game_info *info)
{
   struct retro_input_descriptor desc[] =
   {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Start" },

      { 0 },
   };

   if (load_game(info->path) != EXIT_SUCCESS)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      log_cb(RETRO_LOG_ERROR, "[Gambatte]: XRGB8888 is not supported.\n");
      return false;
   }

   return true;
}


bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info,
                             size_t num_info)

{
   return false;
}

void retro_unload_game()
{
   nesEmuDeinit();
}

unsigned retro_get_region()
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

int audioUpdate()
{
   static int16_t buffer[512 * 2];
   static int pos = 0;
   float* buffer_in = (float*)apuGetBuf();
   int16_t* out_ptr = buffer;
   int samples = apuGetBufSize() / sizeof(float);
   while (pos < samples)
   {
      int val = buffer_in[pos] * 0x4000;
      *(out_ptr++) = val;
      *(out_ptr++) = val;
      pos += AUDIO_DECIMATION_RATE;
      if (out_ptr == &buffer[512 * 2])
      {
         audio_batch_cb(buffer, 512);
         out_ptr = buffer;
      }
   }
   pos -= samples;

   if (out_ptr > buffer)
      audio_batch_cb(buffer, (out_ptr - buffer) / 2);

   return 1;
}

void retro_run()
{
   input_poll_cb();

   inValReads[BUTTON_A]      = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   inValReads[BUTTON_B]      = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   inValReads[BUTTON_SELECT] = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   inValReads[BUTTON_START]  = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   inValReads[BUTTON_RIGHT]  = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   inValReads[BUTTON_LEFT]   = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   inValReads[BUTTON_UP]     = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   inValReads[BUTTON_DOWN]   = !!input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);

   while(!emuRenderFrame)
      nesEmuMainLoop();

   video_cb(textureImage, VISIBLE_DOTS, VISIBLE_LINES, VISIBLE_DOTS * 4);

   emuRenderFrame = false;
}

unsigned retro_api_version()
{
   return RETRO_API_VERSION;
}
