/*
    fonty.c - drawing text & font handling
    Copyright (C) 2007  Ch. Klippel <ck@mamalala.net>

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

#include "global.h"
#include "kernel.h"
#include "lcd.h"

#include "fonty.h"

#include <sys/types.h>

#include "fonts/pearl_font.h"
#include "fonts/smooth_font.h"
#include "fonts/misc_font.h"



/* The following variables are set accroding to font information in include files. */
uint8 font_height;
static const unsigned char *font_bits, *font_info; 
static const u_int16_t *char_pos;
static int bytes_per_col, max_char;

static int ic_space;			// inter character space (depends on font)
static enum FONT activefont;		// the currently active font


void 
init_font(){
	activefont = NO_FONT;	/* not yet set ! */
};

/* Set the current font.
	This is a reasonably fast routine, but it is better not called before every single character that is drawn.
	We do a little cache trick here and set the new font only if it has changed from before.
	The variable activefont must be initialized to NO_FONT by init_font() before using any routine here.
*/
void 
set_font(enum FONT f)
{	
	if (f == activefont)
		return;			// already correct
	
	switch(f) {

		case BIGFONT:
			activefont = BIGFONT;
			font_height = misc_font_height;
			font_bits = misc_font_bits;
			font_info = misc_font_info;
			char_pos = misc_font_cpos;
			bytes_per_col = 2;
			max_char = misc_font_maxchar;
			ic_space=2;
			break;
		
		case MEDIUMFONT:
			activefont = MEDIUMFONT;
			font_height = smooth_font_height;
			font_bits = smooth_font_bits;
			font_info = smooth_font_info;
			char_pos = smooth_font_cpos;
			bytes_per_col = 2;	
			max_char = smooth_font_maxchar;
			ic_space = 1;
			break;
		
		default:			// font not implemented, use the smallest
			
		case SMALLFONT:
			activefont = SMALLFONT;
			font_height = pearl_font_height;
			font_bits = pearl_font_bits;
			font_info = pearl_font_info;
			char_pos = pearl_font_cpos;
			bytes_per_col = 1;	
			max_char = pearl_font_maxchar;
			ic_space = 1;
			break;
	}
}

/* The fonts used are bitmap fonts. Each character can have a different width. The height of all characters is supposed to be 
	the same.
	They are derived from the Linux Font Project (in bdf or pcf format) or from linux console driver fonts.
	
	The fonts are included as C include files.
	
	The width of each character is counted in pixels and called bit_width.
	The fonts are stored with a fixed height between 1 and 16 pixels.
	The number of bytes needed per column is called bytes_per_col.
	Each character is stored as bytes_per_col * bit_width consecutive bytes. The first bytes_per_col bytes represent the first
	column of the first character, the next the second column and so on.
	If the font height is not a multiple of 8, the unneeded bits are in the MSB of the last byte per column and set to 0 
*/

/* The font format uses a compressed format to store the width of each character in the current font 
	Each byte stores 4 bits of width information, so we have a width from 0 to 15 pixels.
*/
// This gives the width of the specified character in the current font
#define char_width(c) (font_info[ (unsigned char) c])


/* The most important routine here!
	Draw a single character 

	c is color and m is mode
		 
	width tells us how many display columns we can use at most to draw the character
	
	tx and ty are already set to the current pixel start locations
	returns 0 iff the character did not fit completely (with inter character space) into the given width
	if it did fit, returns the number of columns drawn (with inter character space) 
	
*/

uint8_t 
draw_char(uint8_t start_row, uint8_t start_col, unsigned char ch, uint8_t fg_col, uint8_t bg_col, uint8_t width) {
	unsigned int w;
	unsigned int i, cpos, cnt;
	uint8 res = 0;
	
	// NOTE maybe print a ? or something
	if (ch > max_char) return 1;	/* We print nothing, character not encoded */
	
	cpos = char_pos[ch];		// Start of glyph bits in font_bits[]
	
	w = char_width(ch);		// Width of the character without inter character space
	if (w > width) w = width;	// w is now min(char_width(ch), width)

	// draw w columns into drawbuf
	// each column is copied (with correct color) to drawbuf directly from font_bits
	for (cnt = 0; cnt < w; cnt++, cpos+=bytes_per_col)
		store_buf(cnt, font_bits[cpos] | (font_bits[cpos+1] << 8), fg_col, bg_col);
	if ( (width - cnt) >= ic_space ){
		for (i=0; i<ic_space; i++)
			store_buf(cnt++, 0, fg_col, bg_col);		// inter character space
		res = cnt;
	};
	write_buf(start_row, start_col, cnt, font_height);
	return res;	
}

// TODO currently not used, we better wrap at white space boundaries. Maybe remove this routine.
/* Draw a text into a given rectangle 
	We wrap the text at character boundaries	
	If the variable cnt is TRUE, we do NOT draw the text, but simply count the height in pixels
	that we would draw.
*/
#if 0
int
draw_text (char *s, int start_row, int start_col, int width, int height, uint8 fg_col, uint8 bg_col, int cnt){
	char ch;
	int c = 0, r=0;
	int n = 0;			/* number of characters drawn on the current line */
	
	if (font_height > height) return 0;		// No room to draw anything
	
	while (*s ){
		/* can we put the next character on the line ? */
		ch = *s;
		if ( c + char_width(ch) <= width ){
			/* draw this character */
			if (0 == cnt)  draw_char(start_row + r, start_col + c, ch, fg_col, bg_col, width - c -1 );
			c += (char_width(ch) + ic_space);
			s++;
			n++;
		} else {
			/* character does not fit on this line, advance to next line */ 
			r += font_height;	/* We have drawn a line font_height pixels high */
			c = 0;
			n = 0;
			/* is there enough room for another line */
			if ((r + 2 + font_height) > height)
				break;
			r += 2;			/* inter line space */
		};
	};
	if (n > 0) r += font_height;
	return r;
};
#endif

static int
is_whitespace(char c){
	return (' ' == c);
};

/*
	Draw the next word in s on screen. We stop at the next white space character.

	We return the number of characters in the word.
	If the word did not fit in the given width, we return 0.
	
	If the variable cnt is not NULL, we do NOT draw the text, but simply count the width in pixels
	that we would draw and store the result in *cnt.
	If the word does not fit completely, cnt is returned as greater than width.
	If we are already at a whitespace character, we draw only this character.
*/
static int
draw_text_word(char *s, int start_row, int start_col, int width, int fg_col, int bg_col, int *cnt){
	int c=0;	
	char ch;	
	int n=0;
	
	while ( (ch = *s++) ){
		
		/* can we put the next character on the line ? */
		if ( c + char_width(ch) <= width ){
			
			/* draw this character */
			if (NULL == cnt)  draw_char(start_row, start_col + c, ch, fg_col, bg_col, width - c );
			
			c += (char_width(ch) + ic_space);
			n++;
			
			if ( ('\0' == *s) || is_whitespace(*s) ){
				/* This word did fit completely */
				break;
			}
		} else {
			/* This word did not fit completely */
			c = width + 1;
			n = 0;
			break;
		};
	};
	
	if (NULL != cnt)
		*cnt = c;
	return n;
};

/*
	Draw the next line in s on screen. We wrap at word boundaries.
	We return a pointer to the next character in s after the current line.
	
	If the variable cnt is not NULL, we do NOT draw the text, but simply count the width in pixels
	that we would draw and store the result in *cnt.
*/
static char * 
draw_text_line(char *s, int start_row, int start_col, int width, int fg_col, int bg_col, int *cnt){
	int c = 0;
	int cnt_wid;
	int n;
	
	while ( *s ){
		/* count how many pixels we will draw with the current word and remember start of next word */
		n = draw_text_word(s, start_row, start_col + c, width - c, fg_col, bg_col, &cnt_wid);
				
		/* can we put the current word on the line ? */
		if ( (c + cnt_wid) <= width ){
			
			/* shall we really draw this word ? */
			if (NULL == cnt)
				draw_text_word(s, start_row, start_col + c, width - c, fg_col, bg_col, NULL);
		
			c += cnt_wid;
			s = s + n;
		} else {
			/* word does not fit completely on this line, finished */
			break;
		};
	};
	
	if (cnt != NULL) *cnt = c;
	
	return s;
};


/* Draw a text into a given rectangle 
	We wrap the text at white space boundaries	
	If the variable cnt is TRUE, we do NOT draw the text, but simply count the height in pixels
	that we would draw.
*/
int
draw_text_space (char *s, int start_row, int start_col, int width, int height, int fg_col, int bg_col, int cnt){
	int r=0;
	int cnt_wid = 0;
	char *s2;
	int offset;
	
	if (font_height > height) return 0;		// No vertical space to draw anything
	
	while (*s ){

		/* count how many pixels we will draw on the current line and remember start of line break */
		s2 = draw_text_line(s, start_row + r, start_col, width, fg_col, bg_col, &cnt_wid);
		
		/* Shall we really draw ? */
		if (0 == cnt){
			/* If this is less than the line width, center the text horizontally */
			if (cnt_wid <= width)
				offset = (width - cnt_wid) / 2;
			else
				offset = 0;
		
			/* And now draw for real (and centered) */
			draw_text_line(s, start_row + r, start_col + offset, width - offset, fg_col, bg_col, NULL);
		};
		
		/* Go to the beginning of next line */
		s = s2;
		if (*s) s++;		// this is white space, skip it.
		
		r += font_height;	/* We have drawn a line font_height pixels high */
		cnt_wid = 0;		/* We have not drawn anything on this line */
		
		/* is there enough room vertically for another line */
		if ((r + 2 + font_height) > height)
			break;
		r += 2;			/* inter line space */
	};
	
	if (cnt_wid > 0) r += font_height;
	
	return r;
};




/* 
	Scroll a text horizontally. 
	Number of rows depend on font height.
*/
void
scroll(int start_row, int start_col, int width, int offset){
	lcd_scroll(start_row, start_col, width, font_height, offset);
};



/* We can fill this array with a number representation */
static char num_chars[12];

static const char hval[16] = "0123456789ABCDEF";

/* Stores the ASCII representation of the hex digits of val into character array s.
	Returns pointer to s.
	Generates exactly 2 digits
*/
char *
get_hex_digits(unsigned long v, char *s){
	s[0] = hval[ (v & 0xf0) >>4];
	s[1] = hval[ (v & 0x0f) ];
	return s;
};

/* Division by repeated subtraction
	Computes (*pu_longval % tval)
	and leaves remainder in *pu_longval
*/
static int 
div(unsigned int tval, unsigned int *pu_longval){
	int count = 0;
	while(*pu_longval >= tval){
		count++;
		*pu_longval -= tval;
	}
	return count;
};

/* Divide val by powers of 10 and store digits in *s 
	Gives exactly 10 digits.
	s will be null terminated
	returns a pointer to the first printable character in s
	if z is 1, all characters are printable.
	if z is 0, leading zeroes are suppressed
*/
char *
get_digits(unsigned int val, char *s, int z){
	unsigned int u_longval = val;
	char *s1 = s;
	int pos;
	
	*s++ = '0' + div(1000000000, &u_longval);
	*s++ = '0' + div(100000000, &u_longval);
	*s++ = '0' + div(10000000, &u_longval);
	*s++ = '0' + div(1000000, &u_longval);
	*s++ = '0' + div(100000, &u_longval);
	*s++ = '0' + div(10000, &u_longval);
	*s++ = '0' + div(1000, &u_longval);
	*s++ = '0' + div(100, &u_longval);
	*s++ = '0' + div(10, &u_longval);
	*s++ = '0' + u_longval;
	*s = 0;
	
	pos = 0;
	/* shall we omit leading zeroes? */
	if (z==0){
		for (pos=0; pos < 9; pos++)
			if (s1[pos] != '0')
				break; 
	};
	return &(s1[pos]);
}


/* Store a time value given in seconds in a string with exactly 5 characters.
	Leading 0's in the hour will be replaced by spaces.
	max. 99 hours
	s will be 0 terminated
*/
void
sec2hms(char *s, int sec){
	unsigned int rest = sec;
	
	if (sec < 0){
		strn_cpy(s, "--:--", 20);
		return;
	};
	
	if (sec >= 3600){		// an hour or more ? 
		*s = '0' + div(36000, &rest);
		if (*s == '0') 
			*s = ' ';
		s++;
	 
		*s++ = '0' + div(3600, &rest); 
		*s++ = 'h';
	};

	/* rest is between 0 and 60 minutes */
	*s++ = '0' + div(600, &rest);
	*s++ = '0' + div(60, &rest);

	if (sec < 3600){		// less than an hour? show the seconds
		/* rest is between 0 and 59 seconds */
		*s++ = ':';
		*s++ = '0' + div(10, &rest);
		*s++ = '0' + rest;
	}
	*s = '\0';
};

/* Convert a number to a string and append it to the given string oldstr.
	oldstr must have enough space
	Restricts max length of resulting string to max_len bytes (incl. \0).
*/ 
static void
append_num (char *oldstr, int num, int max_len){
	char *numstr;
	numstr = get_digits(num, num_chars, 0); 	
	str_cat_max(oldstr, numstr, max_len);
};

/* Concatenate a string and a number plus trailing newline into a new string 
	newstr must have enough space
	Max length of resulting string is COMPOSE_LEN.
*/
void 
compose_string(char *newstr, char *oldstr, int num, int max_len ){
	strn_cpy(newstr, oldstr, max_len);
	append_num(newstr, num, max_len);
	str_cat_max(newstr, "\n", max_len);
};

/* Concatenate a string and two numbers including space between numbers plus trailing newline into a new string 
	newstr must have enough space
	NOTE Max length of resulting string is COMPOSE_LEN.
*/
void 
compose_string2(char *newstr, char *oldstr, int num1, int num2, int max_len ){	
	strn_cpy(newstr, oldstr, max_len);
	append_num(newstr, num1, max_len);
	str_cat_max(newstr, " ", max_len);
	append_num(newstr, num2, max_len);
	str_cat_max(newstr, "\n", max_len);
};
