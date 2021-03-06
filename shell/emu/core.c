// Author: skogaby
// Lots of examples were followed using other emulators found on github

#include <SDL/SDL.h>
#include <libgen.h>
#include <sys/time.h>
#include <sys/types.h>

#include "main.h"
#include "snes9x.h"
#include "soundux.h"
#include "memmap.h"
#include "apu.h"
#include "apu_blargg.h"
#include "cheats.h"
#include "display.h"
#include "gfx.h"
#include "cpuexec.h"
#include "spc7110.h"
#include "srtc.h"
#include "sa1.h"
#include "scaler.h"

#include "shared.h"
#include "menu.h"
#include "video_blit.h"
#include "input.h"
#include "sound_output.h"

char GameName_emu[512];

bool overclock_cycles = false;
bool reduce_sprite_flicker = true;
int one_c, slow_one_c, two_c;

static int32_t samples_to_play = 0;
static int32_t samples_per_frame = 0;
static int32_t samplerate = (((SNES_CLOCK_SPEED * 6) / (32 * ONE_APU_CYCLE)));

#ifdef USE_BLARGG_APU
static void S9xAudioCallback()
{
   size_t avail;
   /* Just pick a big buffer. We won't use it all. */
   static int16_t audio_buf[0x20000];

   S9xFinalizeSamples();
   avail = S9xGetSampleCount();
   S9xMixSamples(audio_buf, avail);
   Audio_Write(audio_buf, avail >> 1);
}
#endif

size_t retro_serialize_size(void)
{
   return sizeof(CPU) + sizeof(ICPU) + sizeof(PPU) + sizeof(DMA) +
          0x10000 + 0x20000 + 0x20000 + 0x8000 +
#ifndef USE_BLARGG_APU
          sizeof(APU) + sizeof(IAPU) + 0x10000 +
#else
          SPC_SAVE_STATE_BLOCK_SIZE +
#endif
          sizeof(SA1) + sizeof(s7r) + sizeof(rtc_f9);
}

void RTC_Save(char* path, uint_fast8_t state)
{
	FILE* savefp;
	
	/* If RTC is disabled then don't bother saving any RTC info */
	if (!Settings.SRTC) return;

	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			if (Settings.SPC7110RTC)
			{
				fread(&rtc_f9, sizeof(uint8_t), sizeof(rtc_f9), savefp);
				fclose(savefp);
				S9xUpdateRTC();
			}
			else
			{
				fread(&rtc, sizeof(uint8_t), sizeof(rtc), savefp);
				fclose(savefp);
				S9xSRTCPostLoadState();
			}
		}
	}
	else
	{
		savefp = fopen(path, "wb");
		if (savefp)
		{
			if (Settings.SPC7110RTC)
			{
				fwrite(&rtc_f9, sizeof(uint8_t), sizeof(rtc_f9), savefp);
				fclose(savefp);
				S9xUpdateRTC();
			}
			else
			{
				S9xSRTCPreSaveState();
				fwrite(&rtc, sizeof(uint8_t), sizeof(rtc), savefp);
				fclose(savefp);
			}
		}
	}
}

void SRAM_Save(char* path, uint_fast8_t state)
{
	FILE* savefp;
	
	/* If SRAM is not used then don't bother saving any SRAM file */
	if (Memory.SRAMMask == 0) return;

	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			fread(Memory.SRAM, sizeof(uint8_t), Memory.SRAMMask, savefp);
			fclose(savefp);
		}
	}
	else
	{
		savefp = fopen(path, "wb");
		if (savefp)
		{
			fwrite(Memory.SRAM, sizeof(uint8_t), Memory.SRAMMask, savefp);
			fclose(savefp);
		}
	}
}

void SaveState(char* path, uint_fast8_t state)
{
#ifndef USE_BLARGG_APU
	uint8_t* IAPU_RAM_current = IAPU.RAM;
	uintptr_t IAPU_RAM_offset;
#endif
	char* buffer;
	FILE* savefp;
	
#ifndef USE_BLARGG_APU
	buffer = malloc(sizeof(APU) + sizeof(IAPU) + 0x10000);
#else
	buffer = malloc(SPC_SAVE_STATE_BLOCK_SIZE);
#endif
	
	if (state == 1)
	{
		savefp = fopen(path, "rb");
		if (savefp)
		{
			S9xReset();
			
			fread(&CPU, sizeof(uint8_t), sizeof(CPU), savefp);
			fread(&ICPU, sizeof(uint8_t), sizeof(ICPU), savefp);
			fread(&PPU, sizeof(uint8_t), sizeof(PPU), savefp);
			fread(&DMA, sizeof(uint8_t), sizeof(DMA), savefp);
		   
			fread(Memory.VRAM, sizeof(uint8_t), 0x10000, savefp);
			fread(Memory.RAM, sizeof(uint8_t), 0x20000, savefp);
			fread(Memory.SRAM, sizeof(uint8_t), 0x20000, savefp);
			fread(Memory.FillRAM, sizeof(uint8_t), 0x8000, savefp);

			#ifndef USE_BLARGG_APU
				fread(&APU, sizeof(uint8_t), sizeof(APU), savefp);
				fread(&IAPU, sizeof(uint8_t), sizeof(IAPU), savefp);
				IAPU_RAM_offset = IAPU_RAM_current - IAPU.RAM;
				IAPU.PC += IAPU_RAM_offset;
				IAPU.DirectPage += IAPU_RAM_offset;
				IAPU.WaitAddress1 += IAPU_RAM_offset;
				IAPU.WaitAddress2 += IAPU_RAM_offset;
				IAPU.RAM = IAPU_RAM_current;
				fread(IAPU.RAM, sizeof(uint8_t), 0x10000, savefp);
			#else
				S9xAPULoadState(buffer);
				fread(buffer, sizeof(uint8_t), SPC_SAVE_STATE_BLOCK_SIZE, savefp);
			#endif

			SA1.Registers.PC = SA1.PC - SA1.PCBase;
			S9xSA1PackStatus();
			fread(&SA1, sizeof(uint8_t), sizeof(SA1), savefp);
			fread(&s7r, sizeof(uint8_t), sizeof(s7r), savefp);
			fread(&rtc_f9, sizeof(uint8_t), sizeof(rtc_f9), savefp);
			fclose(savefp);
			
			S9xFixSA1AfterSnapshotLoad();
			FixROMSpeed();
			IPPU.ColorsChanged = true;
			IPPU.OBJChanged = true;
			CPU.InDMA = false;
			S9xFixColourBrightness();
			S9xSA1UnpackStatus();
			#ifndef USE_BLARGG_APU
				S9xAPUUnpackStatus();
				S9xFixSoundAfterSnapshotLoad();
			#endif
			ICPU.ShiftedPB = ICPU.Registers.PB << 16;
			ICPU.ShiftedDB = ICPU.Registers.DB << 16;
			S9xSetPCBase(ICPU.ShiftedPB + ICPU.Registers.PC);
			S9xUnpackStatus();
			S9xFixCycles();
			S9xReschedule();
		}
	}
	else
	{
		savefp = fopen(path, "wb");
		if (savefp)
		{
			#ifdef LAGFIX
			S9xPackStatus();
				#ifndef USE_BLARGG_APU
					S9xAPUPackStatus();
				#endif
			#endif
			
			S9xUpdateRTC();
			S9xSRTCPreSaveState();
			fwrite(&CPU, sizeof(uint8_t), sizeof(CPU), savefp);
			fwrite(&ICPU, sizeof(uint8_t), sizeof(ICPU), savefp);
			fwrite(&PPU, sizeof(uint8_t), sizeof(PPU), savefp);
			fwrite(&DMA, sizeof(uint8_t), sizeof(DMA), savefp);
		   
			fwrite(Memory.VRAM, sizeof(uint8_t), 0x10000, savefp);
			fwrite(Memory.RAM, sizeof(uint8_t), 0x20000, savefp);
			fwrite(Memory.SRAM, sizeof(uint8_t), 0x20000, savefp);
			fwrite(Memory.FillRAM, sizeof(uint8_t), 0x8000, savefp);

			#ifndef USE_BLARGG_APU
				fwrite(&APU, sizeof(uint8_t), sizeof(APU), savefp);
				fwrite(&IAPU, sizeof(uint8_t), sizeof(IAPU), savefp);
				fwrite(IAPU.RAM, sizeof(uint8_t), 0x10000, savefp);
			#else
				S9xAPUSaveState(buffer);
				fwrite(buffer, sizeof(uint8_t), SPC_SAVE_STATE_BLOCK_SIZE, savefp);
			#endif

			SA1.Registers.PC = SA1.PC - SA1.PCBase;
			S9xSA1PackStatus();
			fwrite(&SA1, sizeof(uint8_t), sizeof(SA1), savefp);
			fwrite(&s7r, sizeof(uint8_t), sizeof(s7r), savefp);
			fwrite(&rtc_f9, sizeof(uint8_t), sizeof(rtc_f9), savefp);
			fclose(savefp);
		}
	}
	
	if (buffer) free(buffer);
}


#ifdef FRAMESKIP
static uint32_t Timer_Read(void) 
{
	/* Timing. */
	struct timeval tval;
  	gettimeofday(&tval, 0);
	return (((tval.tv_sec*1000000) + (tval.tv_usec)));
}
static long lastTick = 0, newTick;
static uint32_t SkipCnt = 0, video_frames = 0, FPS = 60, FrameSkip;
static const uint32_t TblSkip[4][4] = {
    {0, 0, 0, 0},
    {0, 0, 0, 1},
	{0, 0, 1, 1},
	{0, 1, 1, 1}
};
#endif

void Emulation_Run (void)
{
#ifndef USE_BLARGG_APU
	static int16_t audio_buf[2048];
#endif

#ifdef FRAMESKIP
	SkipCnt++;
	if (SkipCnt > 3) SkipCnt = 0;
	if (TblSkip[FrameSkip][SkipCnt]) IPPU.RenderThisFrame = false;
	else IPPU.RenderThisFrame = true;
#else
	IPPU.RenderThisFrame = true;
#endif
	
#ifdef USE_BLARGG_APU
	S9xSetSoundMute(false);
#endif
	Settings.HardDisableAudio = false;

	S9xMainLoop();

#ifndef USE_BLARGG_APU
	samples_to_play += samples_per_frame;

	if (samples_to_play > 512)
	{
		S9xMixSamples(audio_buf, samples_to_play * 2);
		Audio_Write(audio_buf, samples_to_play);
		samples_to_play = 0;
	}
#else
	S9xAudioCallback();
#endif


#ifdef FRAMESKIP
	if (IPPU.RenderThisFrame)
	{
#endif
		Update_Video_Ingame();
#ifdef FRAMESKIP
		IPPU.RenderThisFrame = false;
	}
	else
	{
		IPPU.RenderThisFrame = true;
	}
#endif

#ifdef FRAMESKIP
	video_frames++;
	newTick = Timer_Read();
	if ( (newTick) - (lastTick) > 1000000) 
	{
		FPS = video_frames;
		video_frames = 0;
		lastTick = newTick;
		if (FPS >= 60)
		{
			FrameSkip = 0;
		}
		else
		{
			FrameSkip = 60 / FPS;
			if (FrameSkip > 3) FrameSkip = 3;
		}
	}
#endif
}


bool Load_Game_Memory(char* game_path)
{
	uint64_t fps;
	CPU.Flags = 0;

	if (!LoadROM(game_path))
		return false;

	Settings.FrameTime = (Settings.PAL ? Settings.FrameTimePAL : Settings.FrameTimeNTSC);
	
   if (!Settings.PAL)
      fps = (SNES_CLOCK_SPEED * 6.0 / (SNES_CYCLES_PER_SCANLINE * SNES_MAX_NTSC_VCOUNTER));
   else
      fps = (SNES_CLOCK_SPEED * 6.0 / (SNES_CYCLES_PER_SCANLINE * SNES_MAX_PAL_VCOUNTER));
      
	samplerate = SOUND_OUTPUT_FREQUENCY;
	Settings.SoundPlaybackRate = samplerate;
   
#ifndef USE_BLARGG_APU
	samples_per_frame = samplerate / fps;
	S9xSetPlaybackRate(Settings.SoundPlaybackRate);
#endif
	return true;
}


void init_sfc_setting(void)
{
   memset(&Settings, 0, sizeof(Settings));
   Settings.JoystickEnabled = false;
   Settings.SoundPlaybackRate = samplerate;
   Settings.CyclesPercentage = 100;

   Settings.DisableSoundEcho = false;
   Settings.InterpolatedSound = true;
   Settings.APUEnabled = true;

   Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
   Settings.FrameTimePAL = 20000;
   Settings.FrameTimeNTSC = 16667;
   Settings.DisableMasterVolume = false;
   Settings.Mouse = true;
   Settings.SuperScope = true;
   Settings.MultiPlayer5 = true;
   Settings.ControllerOption = SNES_JOYPAD;
#ifdef USE_BLARGG_APU
   Settings.SoundSync = true;
#endif
   Settings.ApplyCheats = false;
   Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
}

void Init_SFC(void)
{
   init_sfc_setting();
   S9xInitMemory();
   S9xInitAPU();
   S9xInitDisplay();
   S9xInitGFX();
#ifdef USE_BLARGG_APU
   S9xInitSound(1000, 0); /* just give it a 1 second buffer */
   S9xSetSamplesAvailableCallback(S9xAudioCallback);
#else
   S9xInitSound();
#endif
   CPU.SaveStateVersion = 0;
}

void Deinit_SFC(void)
{
   if (Settings.SPC7110)
      Del7110Gfx();

   S9xDeinitGFX();
   S9xDeinitDisplay();
   S9xDeinitAPU();
   S9xDeinitMemory();
}



/* Main entrypoint of the emulator */
int main(int argc, char* argv[])
{
	int isloaded;
	
	printf("Starting Snes9x2005\n");
    
	if (argc < 2)
	{
		printf("Specify a ROM to load in memory\n");
		return 0;
	}
	
	snprintf(GameName_emu, sizeof(GameName_emu), "%s", basename(argv[1]));
	Init_SFC();
	
	Init_Video();
	Audio_Init();
	
	overclock_cycles = false;
	one_c = 4;
	slow_one_c = 5;
	two_c = 6;
	reduce_sprite_flicker = false;
	
	isloaded = Load_Game_Memory(argv[1]);
	if (!isloaded)
	{
		printf("Could not load ROM in memory\n");
		return 0;
	}
	
	Init_Configuration();
	
    // get the game ready
    while (!exit_snes)
    {
		switch(emulator_state)
		{
			case 0:
				Emulation_Run();
			break;
			case 1:
				Menu();
			break;
		}
    }
    
	Deinit_SFC();
    Audio_Close();
    Video_Close();

    return 0;
}
