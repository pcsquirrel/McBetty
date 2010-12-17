/*
    global.h
    Copyright (C) 2007  Colibri <colibri_dvb@lycos.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GLOBAL_H
#define GLOBAL_H

#define VERSION "0.8.2"

/* This device address is used for radio communication.
	Betty and SCART-Adapter must have equal addresses  !
*/
#define DEVICE_ADDRESS	0x01

// Used by "pt.h"
// selects which kind of pt implementation we use
#define LC_INCLUDE "lc-addrlabels.h"

#include "pt.h"

/* included here so that all routines have access to debug_out */
#include "serial.h"


/* Frequency of PCLK in Hz 
		Here: 15 MHz
		When processor speed is changed, make sure to keep PCLK constant! 
*/
#define PCLK 15000000

typedef unsigned char BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;


// TODO use only c99 types here
typedef unsigned char uint8;
typedef unsigned char uint8_t;
typedef unsigned short uint16;
typedef unsigned long uint32_t;
typedef unsigned long uint32;

typedef signed char int8_t;

#define false	0
#define FALSE	0
#define true	1
#define TRUE	1

#define NULL	0

#define	SPEED_30	0
#define	SPEED_60	1

/* P0.4 directly controls the LCD backlight LEDs. It can be PWMed to dim the light. */
#define BACKLIGHT_PWM_PIN (1<<4)
/* P0.11 is 1, if sound output is enabled and 0 otherwise */
#define SOUND_ENABLE_PIN (1<<11)

#define EINT0 (1<<0)
#define EINT1 (1<<1)
#define EINT2 (1<<2)
#define EINT3 (1<<3)

// PCONP bits
#define PCSPI1	10
#define PCSSP	21

#define EOT 0x04

/* Possible error flags */
#define END_OF_PLAYLIST		(1<<0)
#define PLAYLIST_EMPTY		(1<<1)
#define MPD_DEAD			(1<<2)


int max(int a, int b);
int min(int a, int b);
int abs(int a);
int str_len( char *s);
int strstart( char *s1, char *s2);
char *strn_cpy(char *dest, const char *src, int n);
char *strncat(char *dest, const char *src, int n);
char *str_cat_max(char *dest, const char *src, int n);
int strn_cpy_cmp(char *str1, char *str2, int n, int *length);
int atoi(const char *s);

char *strchr(const char *s, int c);
void rand_seed(int s);
int rand(void);

#endif