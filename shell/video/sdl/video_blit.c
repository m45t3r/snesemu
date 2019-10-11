/* Cygne
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Dox dox@space.pl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <SDL/SDL.h>
#include <sys/time.h>
#include <sys/types.h>
#include "main.h"
#include "snes9x.h"
#include "soundux.h"
#include "memmap.h"
#include "apu.h"
#include "cheats.h"
#include "display.h"
#include "gfx.h"
#include "cpuexec.h"
#include "spc7110.h"
#include "srtc.h"
#include "sa1.h"
#include "scaler.h"

#include "video_blit.h"
#include "scaler.h"
#include "config.h"


SDL_Surface *sdl_screen, *backbuffer;

uint32_t width_of_surface;
uint32_t* Draw_to_Virtual_Screen;

void Init_Video()
{
	SDL_Init( SDL_INIT_VIDEO );
	
	SDL_ShowCursor(0);
	
	sdl_screen = SDL_SetVideoMode(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION, 16, SDL_HWSURFACE);
	
	backbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 16, 0,0,0,0);

	Set_Video_InGame();
}

void Set_Video_Menu()
{
	/*if (sdl_screen->w != HOST_WIDTH_RESOLUTION)
	{
		sdl_screen = SDL_SetVideoMode(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION, 16, SDL_HWSURFACE);
	}*/
}

void Set_Video_InGame()
{
	/*switch(option.fullscreen) 
	{
		// Native
		#ifdef SUPPORT_NATIVE_RESOLUTION
        case 0:
			if (sdl_screen->w != INTERNAL_WSWAN_WIDTH) sdl_screen = SDL_SetVideoMode(IPPU.RenderedScreenWidth, IPPU.RenderedScreenHeight, 16, SDL_HWSURFACE);
			Draw_to_Virtual_Screen = sdl_screen->pixels;
			width_of_surface = sdl_screen->w;
        break;
        #endif
        default:
			if (sdl_screen->w != HOST_WIDTH_RESOLUTION) sdl_screen = SDL_SetVideoMode(HOST_WIDTH_RESOLUTION, HOST_HEIGHT_RESOLUTION, 16, SDL_HWSURFACE);
			Draw_to_Virtual_Screen = wswan_vs->pixels;
			width_of_surface = INTERNAL_WSWAN_WIDTH;
        break;
    }*/
}

void Video_Close()
{
	if (sdl_screen) SDL_FreeSurface(sdl_screen);
	if (backbuffer) SDL_FreeSurface(backbuffer);
	SDL_Quit();
}

void Update_Video_Menu()
{
	SDL_Flip(sdl_screen);
}

void Update_Video_Ingame()
{
	uint32_t *s, *d;
	uint32_t h, w;
	uint8_t PAL = !!(Memory.FillRAM[0x2133] & 4);
	
	SDL_LockSurface(sdl_screen);
	
	switch(option.fullscreen)
	{
		case 0:
		s = (uint32_t*) GFX.Screen;
		d = (uint32_t*) sdl_screen->pixels + ((sdl_screen->w - IPPU.RenderedScreenWidth)/4 + (sdl_screen->h - IPPU.RenderedScreenHeight) * 160) - (PAL ? 0 : 4*320);
		for(uint8_t y = 0; y < IPPU.RenderedScreenHeight; y++, s += IPPU.RenderedScreenWidth, d += sdl_screen->w/2) memmove(d, s, IPPU.RenderedScreenWidth * 2);

		break;
		case 1:
			upscale_256xXXX_to_320x240((uint32_t*) sdl_screen->pixels, (uint32_t*) GFX.Screen, SNES_WIDTH, PAL ? 240 : 224);
		break;
		case 2:
			if (IPPU.RenderedScreenHeight == 240) upscale_256x240_to_320x240_bilinearish((uint32_t*) sdl_screen->pixels, (uint32_t*) GFX.Screen, SNES_WIDTH, 239);
			else upscale_256x240_to_320x240_bilinearish((uint32_t*) sdl_screen->pixels + (160*8), (uint32_t*) GFX.Screen, SNES_WIDTH, 224);
		break;
	}
	//bitmap_scale(0, 0, IPPU.RenderedScreenWidth, IPPU.RenderedScreenHeight, sdl_screen->w, sdl_screen->h, SNES_WIDTH*2, 0, GFX.Screen, sdl_screen->pixels);
	
	SDL_UnlockSurface(sdl_screen);	
	SDL_Flip(sdl_screen);
}
