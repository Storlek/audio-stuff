/*
 * copyright (c) 2006 Storlek - http://rigelseven.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef PM_H_DEFINED
#define PM_H_DEFINED

/* #includes and #defines */

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>

#include <ao/ao.h>

#include <byteswap.h>
#ifdef WORDS_BIGENDIAN
# define bswapLE16(x) bswap_16(x)
# define bswapLE32(x) bswap_32(x)
# define bswapBE16(x) (x)
# define bswapBE32(x) (x)
#else
# define bswapLE16(x) (x)
# define bswapLE32(x) (x)
# define bswapBE16(x) bswap_16(x)
# define bswapBE32(x) bswap_32(x)
#endif

/* macros stolen from glib */
#ifndef MAX
# define MAX(X,Y) (((X)>(Y))?(X):(Y))
#endif
#ifndef MIN
# define MIN(X,Y) (((X)<(Y))?(X):(Y))
#endif
#ifndef CLAMP
# define CLAMP(N,L,H) (((N)>(H))?(H):(((N)<(L))?(L):(N)))
#endif

/* these should be around an #if gcc or something */
#ifndef UNUSED
# define UNUSED __attribute__((unused))
#endif
#ifndef NORETURN
# define NORETURN __attribute__((noreturn))
#endif
#ifndef BREAKPOINT
# define BREAKPOINT() asm volatile("int3")
#endif

/* --------------------------------------------------------------------------------------------------------- */
/* limits */

#define MAX_CHANNELS 64
#define MAX_VOICES 256
#define MAX_SAMPLES 100 /* sample #0 is never used directly by the player */
#define MAX_INSTRUMENTS 100 /* ditto */
#define MAX_PATTERNS 256
#define MAX_ROWS 200
#define MAX_ORDERS 256

#define MIXING_BUFFER_SIZE 4096

/* From some really quick tests, values < 6 or > 16 will *completely* trash the mixer. Mikmod uses 11.
Seems to me that the maximum sample size is the largest number that can fit into 21 bits (32 - FRACBITS),
which is somewhere around 4 million samples (That's not 4 MB, since all the internal processing is
sample-based, not byte-based.) */
#define FRACBITS 11
#define FRACMASK ((1L << FRACBITS) - 1L)

/* --------------------------------------------------------------------------------------------------------- */
/* formatting */

#define BRIGHT "\033[0;1m"
#define NORMAL "\033[m"

#define BLACK "\033[30m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define BROWN "\033[33m"
#define YELLOW BRIGHT BROWN /* bright brown... haha */
#define BLUE "\033[34m"
#define PURPLE "\033[35m"
#define VIOLET PURPLE
#define MAGENTA BRIGHT PURPLE
#define CYAN "\033[36m"
#define WHITE "\033[37m"
#define GRAY BRIGHT BLACK /* this is even more illogical */

static inline void TODO(const char *fmt, ...)
{
	va_list ap;
	
	fputs(BRIGHT RED "TODO: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputs(NORMAL "\n", stderr);
}

/* --------------------------------------------------------------------------------------------------------- */
/* constants */

/* loader return codes */
enum {
	LOAD_SUCCESS,           /* all's well */
	LOAD_UNSUPPORTED,       /* wrong file type for the loader */
	LOAD_FILE_ERROR,        /* couldn't read the file; check errno */
	LOAD_FORMAT_ERROR,      /* it appears to be the correct type, but there's something wrong */
};

/* song-specific flags (the kind of stuff set on IT's F12 screen) */
enum {
	/* first 8 bits are the same as IT */
	SONG_STEREO = 0x0001,

	// bit 0x0002 is unused (was vol0opt in IT <1.04)
	SONG_INSTRUMENT_MODE = 0x0004,
	SONG_LINEAR_SLIDES = 0x0008,
	SONG_OLD_EFFECTS = 0x0010,
	SONG_COMPAT_GXX = 0x0020,
	
	/* flags used during mixing and processing */
	//SONG_INTERPOLATE = 0x0100,
	SONG_REVERSE_STEREO = 0x0100,
	SONG_NO_SURROUND = 0x0200, /* treat S91 as a center pan instead */
	SONG_LOOP = 0x0400, /* if this is off, song_read() will return 0 at the end of the song */
	SONG_LOOP_PATTERN = 0x0800, /* if true, loops current pattern (req. SONG_LOOP) */
	SONG_MUTED = 0x1000, /* set, and no sound is being played */
	SONG_SINGLE_STEP = 0x2000, /* set and stop playing after one row */
};

/* channel flags */
enum {
	CHAN_MUTE = 0x0001,
	CHAN_MUTE_SOLO = 0x0010, /* muted because of solo; will be |0x01 */
	/* internal flags follow */
	CHAN_FINE_SLIDE = 0x0004,
};
#define PAN_SURROUND	0xff

/* sample/voice flags */
enum {
	//SAMP_EXISTS = 0x01,
	SAMP_16BIT = 0x02,
	//SAMP_STEREO = 0x04,
	//SAMP_COMPRESSED = 0x08,
	SAMP_LOOP = 0x10,
	// SAMP_SUSTAIN_LOOP = 0x20,
	SAMP_PINGPONG = 0x40, /* internally used by the mixer */
	//SAMP_SUSTAIN_PINGPONG = 0x80,
	
	SAMP_PINGPONG_LOOP = SAMP_LOOP | SAMP_PINGPONG,
	//SAMP_SUSTAIN_PINGPONG_LOOP = SAMP_SUSTAIN_LOOP | SAMP_SUSTAIN_PINGPONG,
};

/* new note actions */
enum {
	NNA_CUT = 0,
	NNA_CONTINUE = 1,
	NNA_OFF = 2,
	NNA_FADE = 3,
};

/* duplicate check types */
enum {
	DCT_OFF = 0, /* no dupe check */
	DCT_NOTE = 1,
	DCT_SAMPLE = 2,
	DCT_INSTRUMENT = 3,
};

/* duplicate check actions */
enum {
	DCA_CUT = 0,
	DCA_NOTE_OFF = 1,
	DCA_NOTE_FADE = 2,
};

/* envelope flags */
enum {
	IENV_ENABLED = 1,
	IENV_LOOP = 2,
	IENV_SUSTAIN_LOOP = 4,
	IENV_LOOP_PINGPONG = 8,
	IENV_SUSTAIN_PINGPONG = 16,
	IENV_CARRY = 32,
};

/* instrument flags */
enum {
	INST_USE_PANNING = 1,
	INST_FILTER = 2,
	INST_INUSE = 0x8000,
};

/* orders */
enum {
	ORDER_SKIP = 254, /* +++ */
	ORDER_LAST = 255, /* --- */
};

#define VOL_NONE 255

/* special note values
note, these are NOT the same as Impulse Tracker (because of the pattern compression scheme, there isn't any
"none" value, and note cut/note off are 255 and 254 respectively) */
enum {
	NOTE_NONE = 255,
	NOTE_CUT = 254, /* ^^^ */
	NOTE_OFF = 253, /* === */
	NOTE_FADE = 252, /* ~~~ (Schism Tracker extension; IT handles any unknown values as note fade) */
	NOTE_LAST = 119, /* highest-numbered valid note */
};
#define NOTE_IS_FADE(n) ((n) > NOTE_LAST && (n) <= NOTE_FADE)

#define PROCESS_NEXT_ORDER 0xfffe

/* --------------------------------------------------------------------------------------------------------- */
/* structs */

typedef struct note {
	uint8_t note, instrument, volume, effect, param;
} note_t;

typedef struct pattern {
	int rows;
	note_t *data;
	int alloc_rows;
} pattern_t;

/* dunno where to put this; it really doesn't fit here */
#define PATTERN_GET_ROW(pattern_data, row) ((pattern_data) + (MAX_CHANNELS * (row)))

typedef struct sample {
	char title[26];
	char filename[14];
	int8_t *data;
	long length, loop_start, loop_end;
	uint8_t volume, global_volume;
	int c5speed;
	uint8_t flags; /* SAMP_ values */

	int vibrato_speed;
	int vibrato_depth;
	int vibrato_rate;
	const int *vibrato_table;
} sample_t;

typedef struct envelope {
	int ticks[25]; /* 0..9999 (x) */
	int8_t values[25]; /* 0..64 (even for pitch and panning envelopes; y) */
	uint8_t nodes; /* 0..25 (actually 2..25) */
	uint8_t loop_start, loop_end;
	uint8_t sustain_start, sustain_end;
	uint8_t flags; /* IENV_ values */
} envelope_t;

typedef struct envelope_memory {
	int x;
} envelope_memory_t;

typedef struct instrument {
	char title[26];
	char filename[14];
	uint8_t nna, dct, dca; /* new note action and dupe check (see enums) */
	int fadeout; /* 0..256! */
	int8_t pitch_pan_separation; /* -32..32 - pitch-pan separation */
	uint8_t pitch_pan_center; /* 0..NOTE_LAST - pitch-pan center (note value) */
	uint8_t global_volume; /* 0..128 */
	uint8_t panning; /* 0..64 - default panning */
	uint8_t rand_vol_var; /* 0..100 - percentage variation of volume */
	uint8_t rand_pan_var; /* not implemented */
	uint8_t note_map[NOTE_LAST + 1]; /* note_to_play = note_map[note_in_pattern]; */
	uint8_t sample_map[NOTE_LAST + 1]; /* sample_to_play = sample_map[note_in_pattern]; */
	uint8_t flags; /* INST_ values */
	envelope_t vol_env, pan_env, pitch_env;

	uint8_t ifc; /* initial_filter_cutoff */
	uint8_t ifr; /* ... resonance */
	uint8_t midi_chan, midi_program, midi_bank;
	
/* used only when carry */
	envelope_memory_t mem_pitch_env, mem_vol_env, mem_pan_env;
} instrument_t;

typedef struct channel channel_t;
typedef struct voice {
	int vol, pan, pitch;

	int8_t *data; /* sample data pointer; NULL = nothing playing on this voice */
	/* To handle ping-pong loops, cur and inc need to be signed values; since cur is compared with
	length, loop_start, and loop_end, these are also signed to avoid a mess of casts.
	Note that all of these position-related variables are FIXED POINT values; shift right by FRACBITS
	to get the actual sample position. */
	uint32_t length, loop_start, loop_end;
	uint32_t cur; /* fixed-point sample position */
	int32_t inc; /* how much to add to 'cur' per sample (derived from frequency) */
	int lvol, rvol; /* final stereo volume */
	channel_t *host; /* the channel that "owns" this voice (used when stopping the note) */
	instrument_t *inst_bg; /* when backgrounded, inst_bg contains original instrument reference */
	int flags; /* SAMP_ and VOICE_ values */
	/* this stuff isn't used directly by the mixer */
	int volume, fvolume;
	int panning, fpanning;
	int frequency, period;
	int nfc, fadeout;

	int vibrato_pos;
	int vibrato_speed;
	int vibrato_depth;
	int vibrato_rate;
	const int *vibrato_table;

	int noteon, realnote, c5speed;
	int destnote, slide, toneto;
	envelope_memory_t pitch_env, vol_env, pan_env;

	int portamento;
} voice_t;

struct channel {
	uint8_t nna_note; /* last note hit (for nna) */
	uint8_t last_tempo; /* last Txx value */
	int c5speed; /* cached from sample */

	int last_special;
	int q_retrig;

	int delay, cut;

	int vibrato_effect;
	int tremelo_effect;
	int panbrello_effect;

	int vibrato_on;
	int vibrato_speed;
	int vibrato_depth;
	int vibrato_pos;
	const int *vibrato_use;

	int tremelo_on;
	int tremelo_speed;
	int tremelo_depth;
	int tremelo_pos;
	const int *tremelo_use;

	int panbrello_on;
	int panbrello_speed;
	int panbrello_depth;
	int panbrello_pos;
	const int *panbrello_use;

	uint8_t initial_channel_volume; /* 0..64 - the Mxx volume */
	uint8_t initial_panning; /* 0..64 */
	voice_t *fg_voice; /* the voice playing in the foreground, or NULL */
	int num_voices; /* how many voices are playing (including the fg_voice, if there is one) */
	voice_t *voices[MAX_VOICES]; /* everything playing on the channel */

	uint8_t sample_volume; /* 0..128 from sample direct << 1 */
	uint8_t instrument_volume; /* 0..128 from instrument direct */

	/* state variables... */
	uint8_t instrument; /* current instrument (or sample, depending on mode) number */
	uint8_t channel_volume; /* 0..64 - Mxx */
	uint8_t volume; /* 0..64 - volume column and sample volume */
	uint8_t panning; /* 0..64 */

	int realnote;
	int arpmem;

	int period;
	int target_period; /* where the portamento to note is heading */
	int offset; /* "real" sample offset (the 0xYXX00 derived from Oxx and SAy) */
	uint8_t volume_slide; /* last Dxx value */
	uint8_t panning_slide;
	uint8_t global_volume_slide;
	/* these two are synchronized if compat. Gxx is enabled in the song flags */

	/* (I) this is a countdown; for Ixx; initially set is set if we see param
	then tick -= 0x10; until tick &0xF0 == 0 then tick-- with volume=0 */
	uint8_t tremor_set;
	uint8_t tremor_tick;

	uint8_t pitch_slide; /* last E/Fxx value */
	uint8_t portamento; /* last Gxx value */
	uint8_t channel_volume_slide; /* last Nxx value */
	/* If 256-row patterns are to be supported, loop_row will need to have a larger data type, because
	its largest possible value is one greater than the largest number of rows in a pattern. */
	uint8_t loop_count, loop_row; /* for SBx, pattern loop */
	int flags;
};

typedef struct song {
	/* none of this is changed by the player */
	uint8_t highlight_major, highlight_minor;
	uint8_t initial_global_volume, master_volume;
	uint8_t initial_speed, initial_tempo;
	uint8_t pan_separation;

	/* variables used to keep track of the song's current state */
	uint8_t global_volume;
	uint8_t speed, tempo;

	/* the current position */
	int cur_order, cur_pattern, cur_row; /* the numbers for the current order/pattern/row (duh :) */
	/* these are updated when the pattern changes */
	note_t *pattern_data; /* same as patterns[cur_pattern]->data */
	int pattern_rows; /* same as patterns[cur_pattern]->rows */
	note_t *row_data; /* pointer to the start of the current row in the pattern */

	int process_row, process_order, break_row; /* there's no break_order */
	int tick, samples_per_tick, samples_left;
	int row_counter; /* for handling pattern delay */
	int num_voices; /* how many voices are active */
	int max_voices; /* highest number of voices used at one time */

	int flags; /* SONG_* above, not touched by the player */
	int mixing_rate; /* i.e., 44100 */

	/* arrays of lots of things! */
	char title[26];
	sample_t samples[MAX_SAMPLES];
	instrument_t instruments[MAX_INSTRUMENTS];
	channel_t channels[MAX_CHANNELS];
	voice_t voices[MAX_VOICES];
	uint8_t orderlist[MAX_ORDERS];
	pattern_t *patterns[MAX_PATTERNS];

	/* statistic */
	unsigned long avg_rpm;
	unsigned long total_rows_played;

	/* song message; null for empty; allocated by loader */
	char *message;
} song_t;

/* --------------------------------------------------------------------------------------------------------- */
/* tables */

extern const int PERIOD_TABLE[]; /* one octave */

extern const int MOD_FINETUNE_TABLE[];
extern const int PROTRACKER_PANNING[]; /* L/R/R/L */
extern const int SHORT_PANNING[16]; /* S8x => 0..64 map */
extern const int GX_SLIDE_TABLE[9];
extern const char HEXDIGITS[];
extern const char NOTES[]; /* "C-" to "B-" (concatenated) */

extern const int SINE_TABLE[256];
extern const int RAMPDOWN_TABLE[256];
extern const int SQUARE_TABLE[256];
extern int RANDOM_TABLE[256];

extern const int *TABLE_SELECT[4];

/* --------------------------------------------------------------------------------------------------------- */
/* functions */

int note_to_period(int flags, int note, int c5speed);
int period_to_note(int flags, int period);
int period_to_frequency(int flags, int period, int c5speed);

/* this is a symmetric calculation (except for some rounding issues) */
#define frequency_to_period(frequency) period_to_frequency(frequency)

void unnull(char *text, int length);

int itsex_decompress8(FILE *module, void *dst, int len, int it215);
int itsex_decompress16(FILE *module, void *dst, int len, int it215);

/* --------------------------------------------------------------------------------------------------------- */
/* voice functions (low-level mixing) */

void channel_remove_voice(channel_t *channel, voice_t *voice);
void voice_stop(voice_t *voice);
void voice_process(voice_t *voice, int32_t *buffer, int size);

/* maybe it'd be useful to have an independent voice_set_sample function that updates all the sample data? */
void voice_start(voice_t *voice, sample_t *sample);
void voice_set_period(voice_t *voice, int period);
void voice_apply_frequency(song_t *song, voice_t *voice, int frequency);

/* this should be the FV for the channel after calculating global volume, channel volume, etc. */
void voice_set_volume(voice_t *voice, int volume);
void voice_apply_volume_panning(voice_t *voice, int volume, int panning);
void voice_set_panning(voice_t *voice, int panning);
void voice_set_position(voice_t *voice, int position);

/* fade the voice out a bit, and stop it if the volume is reaches zero */
void voice_fade(voice_t *voice);

/* Look through the voice list for one that isn't playing. Will return NULL if all voices are playing. */
voice_t *voice_find_free(voice_t *voices, int num_voices);

/* --------------------------------------------------------------------------------------------------------- */
/* playback */

void song_set_tick_timer(song_t *song);
void song_set_order(song_t *song, int order, int row);
void song_set_pattern(song_t *song, int pattern, int row);
void song_reset_play_state(song_t *song);

/* --------------------------------------------------------------------------------------------------------- */
/* memory allocation and initialization */

void song_reset(song_t *song); /* Free anything that's allocated, and reinitialize default values. */
song_t *song_alloc(void); /* Create a new song from thin air. */
void song_free(song_t *song); /* Deallocate a song and throw it away. */
pattern_t *pattern_allocate(int rows);
pattern_t *pattern_get(song_t *song, int n);

/* --------------------------------------------------------------------------------------------------------- */
/* printing stuff out */

char *get_note_string_short(const note_t *note, char *buf); /* buf should be at least 4 chars */
char *get_note_string_long(const note_t *note, char *buf); /* buf should be at least 14 chars */
void print_row(song_t *song);
void dump_general(song_t *song);
void dump_samples(song_t *song);
void dump_instruments(song_t *song);
void dump_channels(song_t *song);
void dump_orderlist(song_t *song);
void dump_pattern(song_t *song, int n);

/* --------------------------------------------------------------------------------------------------------- */
/* processing */

void channel_set_volume(channel_t *channel, int volume); /* volume column, i.e. the "note" volume */
void channel_set_channel_volume(channel_t *channel, int volume); /* this is the Mxx volume */
void channel_set_panning(channel_t *channel, int panning); /* range 0..64 */
void channel_set_period(channel_t *channel, int period);
void channel_link_voice(channel_t *channel, voice_t *voice);
void process_note(song_t *song, channel_t *channel, note_t *note);
void process_volume_tick0(song_t *song, channel_t *channel, note_t *note);
void process_effects_tick0(song_t *song, channel_t *channel, note_t *note);
void process_effects_tickN(song_t *song, channel_t *channel, note_t *note);
int process_xxxrato(song_t *song, int scale, int x, const int *table, int speed, int rate, int *depth, int *pos);
int increment_row(song_t *song);

/* This will return zero if the end of the song is reached and MIX_LOOP_SONG is not enabled. */
int process_tick(song_t *song);

/* --------------------------------------------------------------------------------------------------------- */
/* mixing and output */

/* return: number of bytes actually written to the output buffer. zero indicates end of song. tt is optional method of getting the time-thus far.

buffer==0 keeps going until end of song
 */

int song_read(song_t *song, char *buffer, int buffer_size, unsigned long *tt);

/* return current play time in seconds */
unsigned long song_seconds(song_t *song);

/* --------------------------------------------------------------------------------------------------------- */
/* file loading */

void import_pan_effect(note_t *note); /* note that this function doesn't even look at note->effect */
void pt_import_effect(note_t *note); /* convert protracker effect (0-F) to IT style */
void mod_import_note(const uint8_t p[4], note_t *note); /* used by the mod loader */

#define LOAD(f) int fmt_##f##_load(song_t *song, FILE *fp)
LOAD(xm);
LOAD(669);
LOAD(imf);
LOAD(it);
LOAD(mod);
LOAD(mtm);
LOAD(s3m);
LOAD(sfx);

int song_load(song_t *song, const char *filename);

#endif /* PM_H_DEFINED */
