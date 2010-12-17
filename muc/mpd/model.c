/* model.c */

/* ------------------------------- Our MODEL ----------------------------------------------

	We keep 2 models here.
	mpd_status keeps track of the state of mpd (current song etc.)
	user_status keeps track of the user intentions (which song shall be current song etc.)


  Information about status is stored in this structure. 
	We need to indicate that we do not know some things in here.
	For volume, song and song_id we use -1 to indicate UNKNOWN.
	The state has its own UNKNOWN value.
	cur_artist ard cur_title are NULL if UNKNOWN.
	
	We make sure that the information here is consistent, i.e. if for example songid is set,
	the variables pos, time_total etc. reflect the information about that particular song and not
	about some earlier song.
	
*/

/* Functions starting with user reflect what the user wants.
	We reflect the necessary changes in the user_model variable.
	We return very quickly from theses routines, because they are called from the GUI-Handlers.
	Later we generate the necessary commands to communicate our wishes to MPD (see model_needs_action() ) 
*/


#include "global.h"
#include "fonty.h"
#include "mpd.h"
#include "screen.h"
#include "model.h"

/* The currently known status of mpd. */
static struct STATUS mpd_status;

/* The status as the user wants it to be */
static struct STATUS user_status;


/* ===================== Info about changes ====================================== */

static uint32 model_changed_flags;			// Flags which are set when (part of) the model has changed

int
model_get_changed(){
	return model_changed_flags;
};

/* Setting flags indicating something has changed */
static inline void
model_changed(uint32 change){
	model_changed_flags |= change;	
};

void
model_reset_changed(){
	model_changed_flags = 0;
};


/* ================ Playlist variables, keeping info about all known playlists =========================
	We maintain a list of all available playlists.
	This only works if there are not too many playlists.
	Else we would have to maintain a cache of those playlists.
	But we can not query MPD for info about a specific playlist.
	The would have to be done by mpdtool. TODO !
	So for the moment we stick with a limited number of playlists.
*/

/* Maxumum number of playlist names that we want to know */
#define PL_LIM 30

/* Maximum index into pl_name[] */
#define PL_MAX (PL_LIM - 1)

/* Maximum length of a playlist name including trailing 0 */
#define PL_NAME_LENGTH 40

/* The names of all the known playlists */
static char pl_name_entry[PL_LIM][PL_NAME_LENGTH];

/* Pointers to the playlist names or NULL if unknown */
static char *pl_name[PL_LIM];

/* Number of playlist names currently known to us. */
static int pl_num;

/* -------------------------------------- Playlist functions ---------------------------------------- */

/* Given an index starting from 0, we return the corresponding playlist name entry.
	If we have no info or the playlist does not exist we return "".
*/
char *
mpd_get_playlistname(int idx){
	if ((idx >= 0) && (idx < pl_num) )
		return pl_name[idx];
	else
		return "";
};

/* This is called before controller begins getting playlist names from MPD. */
void
start_reading_playlists(){
	if (pl_num != 0){
		pl_num = 0;
		model_changed(PL_NAMES_CHANGED);
	}
};

// NOTE this is a bit tricky. We can be called multiple times and we really do not know
//		where to store the current playlist name, because playlists are not numbered in the current MPD.
//		We rely on the fact that before calling this routine, the variable pl_num has been reset to 0 (see function start_reading_playlists) !
void
mpd_set_playlistname(char *s){
	if (pl_num < PL_LIM){ 
		/* No use to store playlists with too long names, because we can then later not load the shortened names. */
		if (str_len(s) > PL_NAME_LENGTH-1) return;
		
		strn_cpy(pl_name_entry[pl_num], s, PL_NAME_LENGTH-1);
		pl_name[pl_num] = pl_name_entry[pl_num];
		
		model_changed(PL_NAMES_CHANGED);
		pl_num++;
	};
};	


/* ============== Tracklist variables, keeping info about songs in the current playlist. ==============
* Track lists can be very long (100's of songs). So we only cache a few of them and request
	more information from MPD if we need it.
*/

/* Number of tracklist entries that we do store */
#define TL_LIM 20

/* Maximum index into our tracklist array */
#define TL_MAX (TL_LIM - 1)

/* Array which contains strings with concatenation of Title and Artist tags in the current playlist 
	This is a ring buffer.
	The buffer starts at tl_first_entry and contains TL_LIM entries.
	Not all entries may be filled with information.
	The first entry (at tl_first_entry) corresponds to track number tl_begin in the current playlist.
*/
static char tracklist_entry[TL_LIM][TL_ENTRY_LEN];

/* Pointers to tracklist_entries 
	We do this indirection to allow fast scrolling of the list and to set a NULL value for unknown values.
*/
static char *tracklist[TL_LIM];

/* Our tracklist potentially contains infos about tracks with track no. from tl_first_pos to tl_last_pos 
	pos is the position in the tracklist as seen by MPD (starts with 0 for first song in MPD playlist).
*/
static int tl_first_pos;

/* 
	Last track number in our tracklist 
*/
#define tl_last_pos (tl_first_pos + TL_MAX)

/* At which position does our list start, i.e. which internal ring buffer index corresponds to tl_first_pos? */
static int tl_first_idx;


/* -------------------------------------- Tracklist functions ---------------------------------------- */
	
/* Given a track pos, we return the index into tracklist[] corresponding to this number.
	If the number is outside of our list, we return -1.
	The track infos are stored in a ring buffer. The variable tl_first_idx tells us what tl_index(tl_first_pos) is.
*/
static int
tl_index(int track_pos){
	int idx;

	if ( (track_pos < tl_first_pos) || (track_pos > tl_last_pos) )
		return -1;
		
	idx = tl_first_idx + track_pos - tl_first_pos;
	if (idx >= TL_LIM)
		idx -= TL_LIM;	
	return idx;
};

/* We are given a track number. We return the corresponding tracklist entry 
	or NULL if the track is outside our list.
	NULL might also mean we do not have information about that track.
*/
static char *
tl_get_entry(int track_pos){
	int i = tl_index(track_pos);
	if (i < 0) return NULL;
	return tracklist[i];
};

/* The tracklist screen will ask us for the track entry of a specific track_no. 
	We return it if available, else we return "" 
	The track numbering starts with 0 as MPD does.
*/
char *
track_info(int track_pos){
	char *s;
	s = tl_get_entry(track_pos);
	if (NULL == s) return "";
	return s;
};

/* 
	Given title, artist and track number, store this info in our tracklist (if it fits) 
*/
static void
tl_store_track(char *title, char *artist, int track_pos){
	int idx;
	char *entry;
	
	idx = tl_index(track_pos);
	if (idx < 0)
		return;

	entry = tracklist_entry[idx];
	
	strn_cpy(entry, title, TL_ENTRY_LEN); 
	str_cat_max(entry, " - ", TL_ENTRY_LEN);
	str_cat_max(entry, artist, TL_ENTRY_LEN);
	tracklist[idx] = entry;
};

/* Returns track_no of the first song in our tracklist, about which we have no information.
	Returns -1 if every track has an associated string. 
*/ 
static int 
first_unknown_track(){
	int pos;

	/* Search through our tracklist. Returns first track which has no info */
	for (pos=tl_first_pos; pos <= tl_last_pos; pos++)
		if (tl_get_entry(pos) == NULL)
			return pos;
	return -1;
};

/* All the songs starting at track_pos have been changed and are currently unknown. */
static void 
tracks_unknown(int track_pos){
	int i;
	
	if (track_pos > tl_last_pos) 
		return;				/* not in our model anyway */
	
	track_pos = max(tl_first_pos, track_pos);		/* Need only start at begin of database */

	for (i=track_pos; i <= tl_last_pos; i++)
		tracklist[tl_index(i)] = NULL;
};


static void
init_tracklist(){
	tl_first_idx = 0;
	tl_first_pos = 0;
	tracks_unknown(0);
};

	
/* We are shifting the start of our tracklist up by 1 song position.
	We are not changing most of the information in tracklist[], only the start (and end) of the array.
	We are setting the information of the previous first track to NULL, because it is no longer valid.
*/
static void
tl_shift_up_1(){
	tl_first_pos++;
	tracklist[tl_first_idx] = NULL;
	if (++tl_first_idx > TL_MAX)
		tl_first_idx = 0;
};

/* We are shifting the start of our tracklist down by 1 song 
	We are not changing most of the information in tracklist[], only the start (and end) of the array.
	We are setting the information of the new first track no. to NULL, because we do not know it.
*/
static void
tl_shift_down_1(){
	tl_first_pos--;
	if (--tl_first_idx < 0)
		tl_first_idx = TL_MAX;
	tracklist[tl_first_idx] = NULL;
};

/* We are shifting the start of our tracklist up by diff songs.
	If we leave the current playlist range too much, all our information is worthless
	and we forget it.
	Else we keep that part of info that we already gathered.
*/
static void
tl_shift_up(int diff){
	if (diff > TL_MAX){
		tracks_unknown(tl_first_pos);		// all infos are invalid.
		tl_first_pos += diff;
	} else {
		for (;diff > 0; diff --)
			tl_shift_up_1();
	};
};


/* We are shifting the start of our tracklist down by diff songs.
	If we leave the current playlist range too much, all our information is worthless
	and we forget it.
	Else we keep that part of info that we already gathered.
*/
static void
tl_shift_down(int diff){
	if (diff > TL_MAX){
		tracks_unknown(tl_first_pos);		// all infos are invalid.
		tl_first_pos -= diff;
	} else {
		for (;diff > 0; diff --)
			tl_shift_down_1();
	};
};


/* The user has requested a certain range for our tracklist (maybe by scrolling or loading a new playlist).
	Given are the first and last track that the user wants info to be available for.
	These are usually the tracks that the user wants to be shown on screen.
	We adjust the given values, so that the user can not scroll past the end of the current playlist.
	We return the start value that we really used.
	
	We adjust the range of our internal tracklist accordingly, trying to keep as much information
	valid as we can, so we do not have to reread everything.
*/
int
tracklist_range_set(int start_pos, int end_pos){
	
	if ( (mpd_status.playlistlength != -1 ) && (end_pos >= mpd_status.playlistlength) ){
		int num_entries = end_pos - start_pos + 1;	// The user wants this many entries
		end_pos =  mpd_status.playlistlength -1;
		start_pos = end_pos - num_entries + 1;
	};
	
	if (start_pos < 0 ) start_pos = 0;
	
	if (end_pos > tl_last_pos)			// user wants more information than we currently have
		tl_shift_up(end_pos - tl_last_pos);
	else
		if (start_pos < tl_first_pos)		// user wants information from earlier tracks than we have
			tl_shift_down(tl_first_pos - start_pos);
	
	return start_pos;
};

/* ================================== End of tracklist functions ============================================ */



/* -------------------------------- Retrieving information from the model ---------------------------------- */

// Maximum number of seconds that we wait for MPD to send anything to us before raising an error
// Currently we rely on the fact that we send a "status" command every 20 seconds,
// so we should get our regular responses from MPD
#define MAX_MPD_TIMEOUT 34
#define STATUS_SYNC_TIME 25

/* TODO
	If we let the SCART software and mpdtool each send regular radio packets to us, we can detect much better
	what kind of communication problem we have.
	But that does not fit the current communication protocol. Every communication is initiated by Betty.
	Possible solution:
	Betty sends status commands in regular intervals (already implemented).
	If they go through to MPD and we get an answer back, everything is ok.
	If MPD is not responding, mpdtool does detect that.
	In this case mpdtool sends an "ACK" answer with an appropriate error number.
	If mpdtool does not respond anymore, the scart adapter has to send something back.
	(Question: Can the scart adapter even recognize a dead mpdtool ?)
	And if we get nothing back after some time, either our own radio link is dead or
	the scart adapter is hung up.
*/

void
model_check_mpd_dead(){
	if ((system_time() - mpd_status.last_response) > MAX_MPD_TIMEOUT * TICKS_PER_SEC)
		set_error(MPD_DEAD);
	else
		clr_error(MPD_DEAD);
}


/* Returns TRUE iff there are one or more variables in mpd_status that are unknown and which we can get
	be issuing a status command.
	We also issue a STATUS command after some time, just to make sure everything is as we expect it
	
	TODO
	We should get more information from status command.
	Not only is the information valuable that we get, but also when certain information is missing.
	For example when there is no current song, we do not get song:, songid:, time:, bitrate: and audio: info.
	Even if we assume that there might be communication problems and some lines are missing (occurs rarely),
	we can deduce that there is no current song if all of that info is missing but other infos are present.
	
*/
static int
need_status(){

	/* If the following variables are unknown, we must definitely issue a STATUS command */
	if ( (mpd_status.playlistlength == -1) || (mpd_status.volume == -1) || (mpd_status.state == UNKNOWN) ) 
		return 1;
	
	/* We use status when we want to know the current song number */	
	if ( mpd_status.pos == SONG_UNKNOWN) { 
		/* Makes only sense if we have a current playlist with some songs in it */
		if (mpd_status.playlistlength > 0) 
			/* TODO We will not always get the pos when mpd has stopped 
				If there is a current song, we will get it.
				But if the playlist has finished, there is no current song and we do not get that information
			*/
			if (mpd_status.state != STOP) 
				return 1;
	};

	if ((mpd_status.random == -1) || (mpd_status.repeat == -1) )
		return 1;

	/* The remaining values are not given to us, when there is no current song */
	if (mpd_status.pos == NO_SONG) return 0;
		
	/* MPD gives us no time information, if the state is stopped. Stupid, but true. */
	if (mpd_status.state == STOP) return 0;
	

	return ( (mpd_status.time_elapsed == -1)
			|| (mpd_status.time_total == -1)
		  );		
};


/* Here we determine if user_status and mpd_status agree and if we need any information from mpd or need to send some command to mpd
	We return a command (NO_CMD if no action is needed).
	If the command has an argument, we store it in req->arg.
	The if-clauses here are in a specific order, because some information is necessary for other actions to be useful.
*/
static enum USER_CMD
model_needs_action(UserReq *req){	
	int pos;
	
	/* Most of the following commands only work if we have loaded the correct playlist.
		So check that first.
	*/ 

	if (user_status.cur_playlist != -1){
		/* It would be more logical to return a char* here, but easier to return the index to it because UserReq has no char * field. */
		req->arg = user_status.cur_playlist;
		return LOAD_CMD;
	};
	
	/* mpd playing state and the user state should match */
	if ( (user_status.state != UNKNOWN) && (mpd_status.state != user_status.state) ){
		switch(user_status.state){
			case PLAY:
				/* The user definitively wants to play something */
				if (mpd_status.state == PAUSE)
					return PAUSE_OFF;

				/* Only issue a play command if the playlist is not empty 
					We might just have issued a CLEAR command, which stopped MPD, but user wants playing.
					Issue PLAY command after we have loaded the new playlist and determined it is not empty.
				*/
				if (mpd_status.playlistlength > 0){
					clr_error(PLAYLIST_EMPTY);
					return PLAY_CMD;
				} else
					if (mpd_status.playlistlength == 0)
						set_error(PLAYLIST_EMPTY);
				
				break;
				
			case PAUSE:
				return PAUSE_ON;
				
			case STOP:
				return STOP_CMD;
				
			default:
				break;
		};
	};

	/* Does the user want a specific volume that is not currently set ? */
	if ( (user_status.volume != -1) && (user_status.volume != mpd_status.volume)){
		req->arg = user_status.volume;
		return VOLUME_NEW;
	};
	
	/* Maybe the user wants to change something about the current song */
	if ( (user_status.pos != SONG_UNKNOWN) && (user_status.pos != mpd_status.pos) ){
		/* A specific song from the playlist? */ 
		if (user_status.pos >= 0) {
			req->arg = user_status.pos;
			return SEL_SONG;
		};
		if (user_status.pos == PREV_SONG)
			return PREV_CMD;
		if (user_status.pos == NEXT_SONG)
			return NEXT_CMD;
	};
	
	if ((user_status.random != -1) && (mpd_status.random != user_status.random) ){
		req->arg = user_status.random;
		return RANDOM_CMD;
	};

	if ((user_status.repeat != -1) && (mpd_status.repeat != user_status.repeat) ){
		req->arg = user_status.repeat;
		return REPEAT_CMD;
	};
	
	if ((user_status.single != -1) && (mpd_status.single != user_status.single) ){
		req->arg = user_status.single;
		return SINGLE_CMD;
	};

	/* We ask for status before asking for current song, because only after status do we know if there is any current song */
	if (need_status())
		return STATUS_CMD;

	if ((mpd_status.cur_artist == NULL) || (mpd_status.cur_title == NULL)) {
			debug_out("here ",0);
		return CUR_SONG_CMD;
	};
	
	if (user_status.time_elapsed > mpd_status.time_elapsed ){
		req->arg = user_status.time_elapsed;
		return FORWARD_CMD;
	};
	
	if ( (user_status.time_elapsed >= 0) & (user_status.time_elapsed < mpd_status.time_elapsed )){
		req->arg = user_status.time_elapsed;
		return REWIND_CMD;	
	};
	
	pos = first_unknown_track();	
	if ( (pos >= 0) && (pos < mpd_status.playlistlength) ) {
		req->arg = pos;
		return PLINFO_CMD;
	};
	
	/* TODO the available playlists may change by an outside action (another client created/deleted one etc.)
			So we should regularily (every 5 minutes or so) reread this information
	*/ 
	if ( pl_num < 0 )					/* We have no idea about available playlists */
		return LSINFO_CMD;

	/* Regular Synchronization */
	if ( (system_time() - mpd_status.last_status) > STATUS_SYNC_TIME * TICKS_PER_SEC )
		return STATUS_CMD; 
	
	return NO_CMD;
};

/* This function returns TRUE (<> 0) iff the model needs some interaction with mpd,
	i.e. some commands have to be sent to mpd and the response has to be interpreted.
	In effect, this routine checks if user model and mpd model disagree.
	If they disagree, we store the needed action in *request.
*/
int
action_needed(UserReq *request){
	request->cmd = model_needs_action(request);
	return (request->cmd != NO_CMD);
}



/* ------------------------------------- Tracklist -------------------------------------- */

void
model_store_track(char *title, char *artist, int track_no){
	tl_store_track(title, artist, track_no);
	model_changed(TRACKLIST_CHANGED);
};


/* 
	Returns pos of the last track in the current playlist
	Is < 0 if the playlistlength is unknown.
*/
int mpd_playlist_last(){
	return mpd_status.playlistlength - 1;
};


/* ------------------ Elapsed and total time ------------------------------- */
int
mpd_get_time_total(){
	return mpd_status.time_total;
};

int
mpd_get_time_elapsed(){
	return mpd_status.time_elapsed;
};

/* We have (new) timing information from mpd */
void
mpd_set_time(int elapsed, int total){
	if ( (mpd_status.time_elapsed != elapsed) || (mpd_status.time_total != total) )
		model_changed(TIME_CHANGED);
	mpd_status.time_elapsed = elapsed;
	mpd_status.time_total = total;
	/* Here we check if MPD is close to the specific time wanted by the user.
		If it is closely within the real time, we set users wish to don't care,
		else we leave it as it is (and later try to skip forward or backward)
	*/
	if (abs(user_status.time_elapsed - mpd_status.time_elapsed) < 3)
		user_status.time_elapsed = -1;		// wish fulfilled
};

/* Time flies by when you're the driver of a train :-)
	We increment the elapsed time by 1 second if possible.
	This is done because we expect mpd to behave nicely.
	When we issue the next status command, time is synchronized with MPD
*/
void
mpd_inc_time(){
	/* Only increase time if we are playing something */
	if (mpd_status.state == PLAY){
		if (mpd_status.time_elapsed >= 0){		// We have a time that we can increment 

			/* If time is up, we set the mpd values to -1 to tell controller that we need info */
			if (mpd_status.time_elapsed >= mpd_status.time_total){
				/* We assume the current song has ended playing.
					We then don't know very much, because playlist might have ended. */
				mpd_set_pos(SONG_UNKNOWN);
			} else {
				/* Time is not up. We can increment elapsed time */
				mpd_status.time_elapsed++;
				model_changed(TIME_CHANGED);
			};
		};
	};
};

/* The user wants to skip forward or backward in time a few seconds. */
void
user_wants_time_add(int offs){
	int tmp;
	if (mpd_status.time_elapsed == -1) return;
	tmp = mpd_status.time_elapsed + offs;
	if (tmp < 0) tmp = 0;
	if (mpd_status.time_total != -1)
		if (tmp > mpd_status.time_total) 
			tmp = mpd_status.time_total;
	user_status.time_elapsed = tmp;
};


/* ------------------------------ Playing state (play, pause, stop) -------------------------- */
/* Used by view_state_changed to show the current playing state */
int
mpd_get_state(){
	return mpd_status.state;
};

/* Sets mpd to a certain playing state (i.e. UNKNOWN,PLAY,PAUSE,STOP) 
	MPD handles the STOP state somewhat strangely.
	The STOP state automatically resets the playing time of the current song to 0!
	And after the playlist is finished, the STOP state is entered and there is no current song.
	But MPD reacts in that situation to a PLAY command with OK and plays the first song.
	We reflect that here in our model.
	Called after a successful state changing command 
*/
void
mpd_set_state(enum PLAYSTATE newstate){
	if (newstate == mpd_status.state) return;	/* Not new */
	
	mpd_status.state = newstate;
	
	/* STOP resets elapsed time */
	if (newstate == STOP)
		mpd_set_time(0, mpd_status.time_total);
	
	/* A successful PLAY command even if there was no current song ? Current song is now unknown ! */
	if ((newstate == PLAY) && (mpd_status.pos == NO_SONG))
		mpd_set_pos(SONG_UNKNOWN);
					
	/* This is important to do, because sometimes mpd changes state itself, for example when the playlist finished.
		Then we do not want to repeatedly issue a useless "PLAY" command because the user some time back wanted to play 
		TODO well maybe better to issue a notification in this case so the user knows why playing stopped !
	*/
	if (mpd_status.state == user_status.state)		// wish fulfilled
		user_status.state = -1;
	
	/* We only set the model_changed flags when we know the status, because else we have nothing to show the user.
		We will get a definitive playstate later with a status command (we hope so).
	*/
	if (newstate != UNKNOWN)
		model_changed(STATE_CHANGED);
};

/* We got an OK answer after a state changing command */
void
mpd_state_ok(){
	mpd_set_state(user_status.state);
};

/* We got an ACK answer after a state changing command */
void
mpd_state_ack(){
		user_wants_state(-1);	// don't try to issue the same command again and again
		// TODO here some error handling, maybe message to the user
};

void
user_wants_state(enum PLAYSTATE s){
	user_status.state = s;
};

/* NOTE here we ask for information from user_state. Where is that needed? */
int
user_get_state(){
	return user_status.state;
};

/* The user has hit the pause toggle key.
	If we know the current state of mpd, we assume the user wants to toggle that.
 	If current mpd playing state is unknown, we simply do nothing.
	User has to press the key again when mpd_status.state is known.
	Almost all of the time mpd_status.state should be known!
*/
void
user_toggle_pause(){
	if ( (mpd_status.state == PAUSE) || (mpd_status.state == STOP) )
		user_status.state = PLAY;
	else if (mpd_status.state == PLAY)
		user_status.state = PAUSE;
};





/* ---------------------------- Random mode ------------------------------- */
int
mpd_get_random(){
	return mpd_status.random;
};

/* This function is called when we know about the random status by a "status" command or similar.
*/
void
mpd_set_random(int rnd){
	mpd_status.random = rnd;
};

/* This function is called if we successfully issued a "random x" command and got "OK".
	We can then safely assume that random is set as the user_status wanted it.
*/
void
mpd_random_ok(){
	mpd_status.random = user_status.random;
	user_status.random = -1;
	model_changed(RANDOM_CHANGED);
};

void
user_toggle_random(){
	if (mpd_status.random == 0)
		user_status.random = 1;
	else if (mpd_status.random == 1)
		user_status.random = 0;
};

/* ---------------------------- Repeat mode ------------------------------------------ */
int
mpd_get_repeat(){
	return mpd_status.repeat;
};

void
mpd_set_repeat(int rpt){
	mpd_status.repeat = rpt;
};

void
mpd_repeat_ok(){
	mpd_status.repeat = user_status.repeat;
	user_status.repeat = -1;
	model_changed(REPEAT_CHANGED);
};

void
user_toggle_repeat(){
	if (mpd_status.repeat == 0)
		user_status.repeat = 1;
	else if (mpd_status.repeat == 1)
		user_status.repeat = 0;
};


/* ------------------------------------- Single mode -------------------------------------------- */
/*
	TODO 
	We see a pattern here. For each variable of the model we need at least 4 functions.
	- Get the current value of that variable
	- Set the current value of that variable to a certain value (incl. UNKNOWN and NOTAVAIL )
	- A function which is called after a successful command has been sent to mpd which changed that 
	  variable.
	- A function which can be called if the user wants to change the variable in a certain way.
	Check if we can organize the other functions similarily.
*/	
int
mpd_get_single(){
	return mpd_status.single;
};

void
mpd_set_single(int sgl){
	mpd_status.single = sgl;
};

void
mpd_single_ok(){
	mpd_status.single = user_status.single;
	user_status.single = -1;
	model_changed(SINGLE_CHANGED);
};

void
user_toggle_single(){
	if (mpd_status.single == 0)
		user_status.single = 1;
	else if (mpd_status.single == 1)
		user_status.single = 0;
};


/* ------------------------------------------ Current song information ------------------------------- */

/* 
	NOTE here we will return -1 or -2 if there is no current song (-1) or it is unknown (-2).
		caller has to handle those values
*/
int
mpd_get_song(){
	return mpd_status.pos;
};


/* Returns a string with the current title or NULL if unknown. Tracks with no title return "". */
char *
mpd_get_curtitle(){
	return mpd_status.cur_title;
};

/* Returns a string with the current artist or NULL if unknown. Tracks with no artist return "". */
char *
mpd_get_curartist(){
	return mpd_status.cur_artist;
};

/* We have got (maybe new) information about the current song pos */ 
void
mpd_set_pos(int newpos){
	if (mpd_status.pos == user_status.pos)		// wish fulfilled
		user_status.pos = -1;
	if (mpd_status.pos == newpos) return;		// nothing new

	mpd_status.pos = newpos;

	if (newpos == NO_SONG){ 
		mpd_set_artist("");
		mpd_set_title("");
		mpd_status.songid = UNKNOWN;				// NOTE songid is currently not used
		mpd_set_time(-1, -1);	
		mpd_set_state(STOP);
	} else { 
		mpd_set_artist(NULL);				/* We do not yet know which artist and title we have */
		mpd_set_title(NULL);
		mpd_status.songid = UNKNOWN;				// NOTE songid is currently not used
		mpd_set_time(0, -1);				/* The 0 is just a guess. But we have to get total time anyway. */
	};
	model_changed(POS_CHANGED);				// this is (maybe) important to know for track list handling */
};

/* Called after a succesful NEXT or PREVIOUS command. */
void
mpd_pos_ok(){
	/* We do not really know which song is the current one. There may not even be one or it is random.
		Mark mpd.pos as UNKNOWN to read the info soon.
	*/
	mpd_set_pos(SONG_UNKNOWN);
	user_song_unknown();	/* wish fulfilled */
};

void
user_set_pos(int pos){
	user_status.pos = pos;
};

	
int	
user_get_pos(){
	return user_status.pos;
};

/* We are given either NULL or a string with the artist tag, NULL meaning artist unknown */
// TODO how can artist unknown happen ? Should we better store an empty string then ?
void
mpd_set_artist(char *s){
	int length;
	
	if ( (s == NULL) ){
		if (mpd_status.cur_artist != NULL){
			mpd_status.cur_artist = NULL;		
			model_changed(ARTIST_CHANGED);
		};
	} else {
		if (strn_cpy_cmp(mpd_status.artist_buf, s, TITLE_LEN, &length) == 0)	
			model_changed(ARTIST_CHANGED);
		mpd_status.cur_artist = mpd_status.artist_buf;
	}; 
};

/* We are given either NULL or a string with the title tag, NULL meaning title unknown */
// TODO how can title unknown happen ? Should we better store an empty string then ?
void
mpd_set_title(char *s){
	int length;
	
	if (s == NULL){
		if (mpd_status.cur_title != NULL){
			mpd_status.cur_title = NULL;
			model_changed(TITLE_CHANGED);
		};
	} else {
		if (strn_cpy_cmp(mpd_status.title_buf, s, TITLE_LEN, &length) == 0)	
			model_changed(TITLE_CHANGED);
		mpd_status.cur_title = mpd_status.title_buf;
	}; 
};

// TODO not fully implemented
/* We have got (maybe new) information about the current song id */ 
void
mpd_set_id(int newid){
	if (mpd_status.songid == user_status.songid)		// wish fulfilled
		user_status.songid = -1;
	if (mpd_status.songid == newid) return;		// nothing new

	mpd_status.songid = newid;

};


void
user_song_unknown(){
	user_status.pos = SONG_UNKNOWN;
	user_status.state = UNKNOWN;
	user_status.cur_artist=NULL;
	user_status.cur_title=NULL;
	user_status.songid = -1;
	user_status.time_elapsed = -1;
	user_status.time_total = -1;
};

/* user wants a specific song pos in the playlist */
void
user_wants_song(int p){
	/* We make sure that the wish makes sense. */
	if (p < 0) return;
	if ((mpd_status.playlistlength >= 0) && (p > mpd_status.playlistlength - 1)){
		set_error(END_OF_PLAYLIST);
		return;
	} else
		clr_error(END_OF_PLAYLIST);
	user_status.pos = p;
};

void
user_wants_next(){
	user_status.pos = NEXT_SONG;
};

void
user_wants_prev(){
	user_status.pos = PREV_SONG;
};


/* -------------------------------------- Volume and Mute ---------------------------------------- */
int	
mpd_get_volume(){
	return mpd_status.volume;
};

/* We have some information about the current mpd volume.
	Update mpd_status and if necessary user_status
*/
void
mpd_set_volume(int vol){
	if (vol != mpd_status.volume){
		mpd_status.volume = vol;
		model_changed(VOLUME_CHANGED);
	}
};

/* Changes the internal user volume by adding a value to mpd_status.volume 
	If mpd_status.volume is unknown at this point, wished volume will not be set.
*/
void
user_wants_volume_add(int chg){
	if (mpd_status.volume == -1) return;
	user_status.volume = max(0, min(100, mpd_status.volume + chg));
};

/* User wants to toggle the mute status, i.e. if volume was 0, reset it to old value, else set it to 0. 
	volume == 0 is treated as MUTE 
*/
void
user_toggle_mute(){
	if ( mpd_status.volume != 0 ){
		mpd_status.old_volume = mpd_status.volume;		// remember the current volume
		user_status.volume = 0;
	} else
		if (mpd_status.old_volume > 0)
			user_status.volume = mpd_status.old_volume;
		else
			user_status.volume = 15;			// we use a default here in case we have no idea of old volume
};


/* -------------------------------------- current playlist -------------------------------------------------- */


/* The length of our playlist has changed. We use the given number if it is >= 0.
	Else it means we do not know the current playlistlength (maybe no playlist at all).
	We adjust the information in our tracklist accordingly and maybe current_song as well.

	TODO we really need a function that is different from this which is called clr_playlist
		because we try to do here more than we know.
*/
void
set_playlistlength(int n){
	if (n < 0) {
		mpd_status.playlistlength = -1;
		return;
	};
	
	if (mpd_status.playlistlength == n)
		return;								// no change
	
	mpd_status.playlistlength = n;	
	
	/* If the playlist is empty, there can be no current song.*/
	if (n == 0){
		mpd_set_pos(NO_SONG);
		if (user_status.state == PLAY)
			model_changed(ERROR_FLAG);
	};
	
	// TODO Here we should issue TL_CHANGED flags, if appropriate. */
	
	
	
};


/* Called after a successful LOAD command. 
	Currently we interpret the LOAD as a CLEAR_AND_LOAD, i.e. the old playlist is cleared and the new one loaded.
	We assume the user wish has been fulfilled 
	We do not care what playlist was loaded.
	But we assume the user wants to play the first song of the playlist.
*/
void
mpd_load_ok(){
	user_status.cur_playlist = -1;			// wish fulfilled
	user_status.playlistlength = -1;
	
// The following lines are useful if we implement an APPEND_PLAYLIST command.
#if 0
	/* After loading a playlist, MPD does not change its state, but the playlist has been expanded */
	/* If we knew the previous playlistlength, we know which tracks we must update */
		
	if (mpd_status.playlistlength >= 0)
			tracks_unknown(mpd_status.playlistlength);	// all songs after the old ones are unknown
		else
#endif			

	set_playlistlength(-1);				// the new playlistlength is unknown
	tracks_unknown(0);					// update all tracks
	mpd_set_state(STOP);				// mpd changes its state to STOP after a CLEAR_AND_LOAD command!
};	


/* The user wants a new playlist. 
	We store a reference to the resulting string, so its contents must be kept valid !
	We assume the user really wants three things.
	First he wants the current playlist to be emptied.
	Then he wants to load the new playlist.
	And then he wants to play the first song in this playlist.
	This is a bit different than MPD interprets the LOAD command.
	So we set three wishes here at once. 
	The need_action() routine above must make sure to handle the CLEAR and LOAD request first.
*/
void 
user_wants_playlist(int idx){
	if ((idx < 0) || (idx >= pl_num))
		return;
	
	/* The user explicitly requested this, so we do a complete reload of the new playlist
		without checking if it is already loaded.
		We have no easy way to know which playlist is loaded, because mpd does no bookkeeping about playlist names.
		We can only detect that the "LOAD" command worked correctly by waiting for its "OK".
	*/	
	
	user_status.playlistlength = -1;	// User doesn't care about length of list. */

	user_status.cur_playlist = idx;		// We want to load this new list 
	
	/* User wants first song ... */
	user_wants_song(0);
	/* ... to be played */
	user_status.state = PLAY;
};



/* Set the time that mpd last communicated with us */
void
model_set_last_response(unsigned int t){
	mpd_status.last_response = t;
};

/* We got a valid response to a "status" command.
	Remember the time so we can regularily do status commands.
*/
void
mpd_status_ok() {
	mpd_status.last_status = system_time();
};

/* ----------------------------------------------- Initialization -------------------------------------------------- */
/* sets all values in st to UNKNOWN */
void
model_reset(struct STATUS *st){
	st->playlistlength = -1;
	st->old_volume = -1;
	st->volume = -1;
	st->state = UNKNOWN;
	st->cur_playlist = -1;
	st->last_response = 0;
	st->last_status = 0;
	st->pos = SONG_UNKNOWN;
	st->songid = -1;
	st->cur_title = NULL;
	st->cur_artist = NULL;
	st->artist_buf[0] = '\0';
	st->title_buf[0] = '\0';
	st->random = -1;
	st->repeat = -1;
	st->single = -1;
};

/* Initialize our model
	We assume that we know nothing at the start
*/
void 
model_init(){
	model_reset(&mpd_status);
	model_reset(&user_status);
	user_song_unknown();
	
	pl_num = -1;		/* No info about playlists. */
	
	/* We assume that the user wants to play immideately */
	user_status.state = PLAY;
	
	/* We have just reset all infos. So the view has to be updated. */
	model_changed_flags = VOLUME_CHANGED | TITLE_CHANGED | ARTIST_CHANGED | TIME_CHANGED | RANDOM_CHANGED
							| REPEAT_CHANGED | SINGLE_CHANGED;
	
	init_tracklist();
	

};
