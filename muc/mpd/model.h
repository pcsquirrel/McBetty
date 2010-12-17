/* model.h */

#ifndef MODEL_H
#define MODEL_H

/* mpd can be in one of those states TODO maybe add a state ERROR */
enum PLAYSTATE {UNKNOWN, STOP, PAUSE, PLAY};


/* MPD gives a 0-based answer on song. 
 * We need 4 extra values here:
  -1 for unknown (or don't care)
  -2 for MPD has no current song
  -3 for user wants next song
  -4 for user wants previous song
*/
#define PREV_SONG		-4
#define NEXT_SONG		-3
#define NO_SONG			-2
#define SONG_UNKNOWN	-1

/* These are the possible values for model_changed_flags */
#define VOLUME_CHANGED		(1<<0)
#define TITLE_CHANGED		(1<<1)
#define ARTIST_CHANGED		(1<<2)
#define TIME_CHANGED		(1<<3)
#define STATE_CHANGED		(1<<4)
#define PL_NAMES_CHANGED	(1<<5)
#define TRACKLIST_CHANGED	(1<<6)
#define POS_CHANGED			(1<<7)
#define RANDOM_CHANGED		(1<<8)
#define REPEAT_CHANGED		(1<<9)
#define SINGLE_CHANGED		(1<<10)
#define ERROR_FLAG			(1<<11)


/* Maximum length of a tracklist entry including trailing 0 
	We expect to have a lot of those entries, so we keep them small
*/
#define TL_ENTRY_LEN 64

// Length of artist and title strings each, some songs and some albums really have long titles
#define TITLE_LEN 150

struct STATUS {
	int playlistlength;			// -1 if unknown
	int volume;					// -1 if unknown
	int old_volume;				// -1 if unknown, volume before a mute command, used to unmute
	int pos; 					// current song position in the playlist. Starts with 0!
	enum PLAYSTATE state;
	int8_t	random;
	int8_t repeat;
	int8_t single;
	int songid;					// mpd's internal id of the current song
	char artist_buf[TITLE_LEN];	// If cur_artist points here, this is the artist tag, else irrelevant 
	char title_buf[TITLE_LEN];	// If cur_title points here, this is the title tag, else irrelevant 
	char *cur_artist;			// NULL if not known, else points to info in artist_buf;
	char *cur_title;			// NULL if not known, else points to info in title_buf;
	int time_elapsed;			// in seconds
	int time_total;				// in seconds
	int cur_playlist;			// index to currently loaded playlist (or -1), not used in mpd_status, only in user_status
	unsigned int last_response;	// system time when we last saw a response line from mpd (for error detection)
	unsigned int last_status;	// system time when we last got a valid status answer
};


int model_get_changed(void);
void model_reset_changed();

void start_reading_playlists(void);
void user_toggle_pause(void);
void user_set_state(enum PLAYSTATE s);

void user_wants_song(int p);
void mpd_want_song_add(int offs);
void user_wants_next();
void user_wants_prev();

int mpd_get_time_total();
int mpd_get_time_elapsed();
void user_wants_time_add(int offs);
void mpd_set_time(int elapsed, int total);
void mpd_inc_time();

void user_toggle_mute(void);
void mpd_want_volume_add(int chg);
int	mpd_get_volume();
void mpd_set_volume(int vol);
void user_wants_volume_add(int chg);

void user_wants_playlist(int idx);

void error_inc(int amount);
void error_dec(int amount);
int get_comm_error();


void user_song_unknown();
void model_store_track(char *title, char *artist, int track_no);
int tracklist_range_set(int start, int end);
int mpd_playlist_last();

int user_get_state();
int mpd_get_state();
void mpd_set_state(enum PLAYSTATE newstate);
void mpd_state_ok();
void mpd_state_ack();

void mpd_status_ok();

char *mpd_get_curtitle();
char *mpd_get_curartist();

void mpd_set_newplaylist();
void mpd_set_title(char *s);
void mpd_set_artist(char *s);
void mpd_set_seek();
void set_playlistlength(int n);


void mpd_set_playlistname(char *s);
char *mpd_get_playlistname(int idx);

void user_set_playlistlength(int n);
void mpd_load_ok();

void mpd_set_last_response(unsigned int time);
void mpd_check_mpd_dead();
void model_reset(struct STATUS *st);
int action_needed(UserReq *request);
void user_wants_state(enum PLAYSTATE s);

int mpd_get_single();
void mpd_set_single(int sgl);
void mpd_single_ok();
void user_toggle_single();

int mpd_get_repeat();
void mpd_set_repeat(int rpt);
void mpd_repeat_ok();
void user_toggle_repeat();

int mpd_get_random();
void mpd_set_random(int rnd);
void mpd_random_ok();
void user_toggle_random();

void mpd_set_pos(int newpos);
void user_set_pos(int pos);
int	user_get_pos();
void mpd_pos_ok();
void mpd_set_id(int newid);

#endif
