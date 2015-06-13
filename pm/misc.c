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

#include "pm.h"

/* --------------------------------------------------------------------------------------------------------- */
/* functions */

int note_to_period(int flags, int note, int c5speed)
{
	if (!c5speed)
		return INT_MAX;
	if (flags & SONG_LINEAR_SLIDES) { /* c5speed isn't used here... */
		return (PERIOD_TABLE[note % 12] << 5) >> (note / 12);
	}
	return (8363 * 32 * PERIOD_TABLE[note % 12] >> (note / 12)) / c5speed;
}

int period_to_note(UNUSED int flags, int period)
{
	int n;
	
	if (!period)
		return NOTE_NONE;
	for (n = 0; n <= NOTE_LAST; n++) {
		/* Essentially, this is just doing a note_to_period(n, 8363), but with less
		computation since there's no c5speed to deal with. */
		if (period >= (32 * PERIOD_TABLE[n % 12] >> (n / 12)))
			return n;
	}
	return NOTE_NONE;
}
#include <math.h>

int period_to_frequency(int flags, int period, int c5speed)
{
	/* A few divisors:
	 *     14187578 - PAL - MP uses this for MOD/MTM/669
	 *     14318180 - NTSC - MP uses this for XM/MT2
	 *     14317456 - NTSC (2?) = 8363 * 1712 (middle C period)
	 *     14317056 - ST3 style (I'm guessing this is a typo or something) */
	if (period <= 0)
		return INT_MAX;
	if (flags & SONG_LINEAR_SLIDES) {
		return c5speed  * 1712 / period;
	}
	return 14317456 / period;
}

void unnull(char *text, int length)
{
	text[length] = 0;
	while (length-- > 0)
		if (text[length] == 0)
			text[length] = 32;
}

/* --------------------------------------------------------------------------------------------------------- */
/* memory allocation and initialization */

/* Deallocate anything that needs deallocated, but don't free the song itself. */
static song_t *_song_free(song_t *song)
{
	int n;

	for (n = 1; n < MAX_SAMPLES; n++) {
		if (song->samples[n].data)
			free(song->samples[n].data);
	}

	for (n = 0; n < MAX_PATTERNS; n++) {
		if (song->patterns[n]) {
			if (song->patterns[n]->data)
				free(song->patterns[n]->data);
			free(song->patterns[n]);
		}
	}
	if (song->message) {
		free(song->message);
		song->message = 0;
	}

	return song;
}

static void _init_envelope(envelope_t *e, int n)
{
	e->ticks[0] = 0;
	e->values[0] = n;
	e->ticks[1] = 100;
	e->values[1] = n;
	e->nodes = 2;
}

/* Set up anything that's not initially zero, such as sample c5speeds and channel volumes. */
static song_t *_song_reset(song_t *song)
{
	int n, t;
	instrument_t *i;
	
	for (n = 1, i = song->instruments + 1; n < MAX_INSTRUMENTS; n++, i++) {
		i->pitch_pan_center = 60; /* C-5 */
		i->global_volume = 128;
		i->panning = 32;
		for (t = 0; t <= NOTE_LAST; t++)
			i->note_map[t] = t;
		_init_envelope(&i->vol_env, 64);
		_init_envelope(&i->pan_env, 32);
		_init_envelope(&i->pitch_env, 32);
	}
	
	for (n = 1; n < MAX_SAMPLES; n++) {
		song->samples[n].c5speed = 8363;
		song->samples[n].volume = 64;
	}

	for (n = 0; n < MAX_CHANNELS; n++) {
		song->channels[n].initial_channel_volume = 64;
		song->channels[n].initial_panning = 32;
	}

	memset(song->orderlist, ORDER_LAST, MAX_ORDERS);

	song->highlight_major = 16;
	song->highlight_minor = 4;
	song->initial_global_volume = 128;
	song->master_volume = 48;
	song->initial_speed = 6;
	song->initial_tempo = 125;
	song->pan_separation = 128;
	song->flags = (SONG_STEREO | SONG_LINEAR_SLIDES);

	song_reset_play_state(song);

	return song;
}

void song_reset(song_t *song)
{
	int rate = song->mixing_rate; /* blah */
	
	_song_free(song); /* deallocate patterns, samples, etc. */
	memset(song, 0, sizeof(song_t));
	_song_reset(song);
	song->mixing_rate = rate;
}

song_t *song_alloc(void)
{
	return _song_reset(calloc(1, sizeof(song_t)));
}

void song_free(song_t *song)
{
	free(_song_free(song));
}

pattern_t *pattern_allocate(int rows)
{
	int n;
	pattern_t *pattern;
	note_t *note;
	
	pattern = malloc(sizeof(pattern_t));
	pattern->data = calloc(MAX_CHANNELS * rows, sizeof(note_t));
	pattern->alloc_rows = pattern->rows = rows;
	
	for (n = MAX_CHANNELS * rows, note = pattern->data; n; n--, note++) {
		note->note = NOTE_NONE;
		note->volume = VOL_NONE;
	}
	return pattern;
}

pattern_t *pattern_get(song_t *song, int n)
{
	/* "Note that if the (long) offset to a pattern = 0, then the
	pattern is assumed to be a 64 row empty pattern." [ittech] */
	if (song->patterns[n] == NULL)
		song->patterns[n] = pattern_allocate(64);
	return song->patterns[n];
}
