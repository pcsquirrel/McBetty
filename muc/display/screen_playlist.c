/*
    screen.c - User Interface

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

#include "screen.h"
#include "window.h"
#include "keyboard.h"
#include "lcd.h"
#include "model.h"
#include "mpd.h"

/* ------------------------------------- Track List Menu ------------------------------------------ */
/* The row where the window list starts */
#define WL_START_ROW 0

/* The width of each window in pixels */
#define WL_WIDTH 128

/* The height of a small window */
#define WL_SMALL_HEIGHT 10

/* Number of entries in the window list 
	We have 160 pixels, so 1 title window, 10 normal windows and 1 big window will fit 
*/
#define WL_SIZE 12

/* The window list */
static struct Window win[WL_SIZE];

#define WIN_TXT_SIZE 32
/* Text buffer for the windows */
static char win_txt[WL_SIZE][WIN_TXT_SIZE];

static scroll_list playlist_list;

/* The height of a non-selected window in the scroll list */
#define WL_NORMAL_HEIGHT 13

/* The height of a selected window in the scroll list */
#define WL_HIGH_HEIGHT 18


/* We are called when the playlist names have potentially changed (i.e they have been read in from MPD) */
void
view_playlist_changed(){
	scroll_list_changed(&playlist_list);
};



static void
screen_enter(){

	lcd_fill(0x00);	
		
	/* We always want to start with the first playlist name, because the user might have got lost previously. */
	playlist_list.first_info_idx = 0;
	select(&playlist_list, 0);

	/* A lot of things might have changed since we were called last.
		So better reread the information from mpd 
	*/
	view_playlist_changed();
	
	screen_visible(PLAYLIST_SCREEN, 1);
	screen_redraw(PLAYLIST_SCREEN);	
};


void
playlist_screen_init(Screen *this_screen){
	int cur_start_row;
	
	this_screen->wl_size = WL_SIZE;
	this_screen->win_list = win;
	this_screen->screen_enter = screen_enter;
	
	cur_start_row = WL_START_ROW;	
	
	/* The first window is a help window */
	win_init(&win[0], cur_start_row, 0, WL_SMALL_HEIGHT, 128,0, win_txt[0]);
	win[0].font = SMALLFONT;
	win[0].fg_color = WHITE;
	win[0].bg_color = BLACK;
	win[0].flags |= WINFLG_CENTER;
	cur_start_row += WL_SMALL_HEIGHT;
	
	init_scroll_list(&playlist_list, &(win[1]), win_txt[1], WIN_TXT_SIZE, WL_SIZE - 1, &mpd_get_playlistname, cur_start_row);
	
	win_new_text(&win[0], "Choose a playlist");
};	



void
playlist_keypress(Screen *pl_screen, int cur_key, UserReq *req){
		
	switch (cur_key) {

		case KEY_OK:
			user_wants_playlist( playlist_list.sel_win );
			switch_screen(PLAYLIST_SCREEN, PLAYING_SCREEN);
			break;
			
		case KEY_Pplus:					
		case KEY_Up:
			if (playlist_list.sel_win > 0)
				sel_win_up(&playlist_list);
			else {					// We want to scroll past the upper end of the window list
				if ( info_idx(&playlist_list, 0)  > 0){		// We have information to show
					playlist_list.first_info_idx -= 1;
					view_playlist_changed();
				};
			};
			break;
			
		case KEY_Pminus:				
		case KEY_Down:
			if ( playlist_list.sel_win < (playlist_list.num_windows - 1) )
				sel_win_down(&playlist_list);
			else{					// We want to scroll past the lower end of the window list
				playlist_list.first_info_idx += 1;
				view_playlist_changed();
			};	
			break;
			
		default:
			break;
	};
};
