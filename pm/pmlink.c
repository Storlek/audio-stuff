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

/* this file handles all of the modplug-specific stuff for schism-tracker
by providing the same API.

this file is not (otherwise) part of pm
	-mrsb
*/
#include "pm.h"

/* XXX */
void clear_cached_waveform(UNUSED int x) {}
int instrument_get_current(void) { return 0; }
int sample_get_current(void) { return 0; }

enum song_mode {
	MODE_STOPPED,
	MODE_SINGLE_STEP,
	MODE_PATTERN_LOOP,
	MODE_PLAYING,
};



typedef sample_t song_sample;
typedef instrument_t song_instrument;
typedef channel_t song_channel;
typedef voice_t song_mix_channel;
typedef note_t song_note;

static song_t *pmsong = 0;
static int playing = 0;

void song_init_audio(void)
{
/*TODO*/
}
void song_init_modplug(void)
{
/*TODO*/
}
void song_initialise(void)
{
/*TODO*/
}


void song_get_vu_meter(UNUSED int *left, UNUSED int *right)
{
/*TODO*/
}
void song_stop(void)
{
/* TODO:silence */
	playing = 0;
	if (pmsong) song_reset_play_state(pmsong);
}

enum song_mode song_get_mode(void)
{
	if (!pmsong) return MODE_STOPPED;
	if (!playing) return MODE_STOPPED;
	if (pmsong->flags & SONG_SINGLE_STEP) return MODE_SINGLE_STEP;
	if (pmsong->flags & SONG_LOOP_PATTERN) return MODE_PATTERN_LOOP;
	return MODE_PLAYING;
}


void song_loop_pattern(int pattern, int row)
{
	song_stop();
	if (!pmsong) return;

	pmsong->flags |= SONG_LOOP_PATTERN|SONG_LOOP;
	song_set_pattern(pmsong, pattern, row);
}
void song_start_at_order(int order, int row)
{
	song_stop();
	if (!pmsong) return;

	pmsong->flags &= ~SONG_LOOP_PATTERN;
	pmsong->flags |= SONG_LOOP;
	song_set_order(pmsong, order, row);
}
void song_start(void)
{
	song_stop();
	if (!pmsong) return;
	pmsong->flags &= ~SONG_LOOP_PATTERN;
	pmsong->flags |= SONG_LOOP;
	playing = 1;
}
void song_start_at_pattern(int pattern, int row)
{
	song_stop();
	if (!pmsong) return;

	pmsong->flags &= ~SONG_LOOP_PATTERN;
	pmsong->flags |= SONG_LOOP;
	song_set_pattern(pmsong, pattern, row);
	playing = 1;
}
void song_single_step(int pattern, int row)
{
	if (!pmsong) return;
	pmsong->flags &= ~SONG_LOOP_PATTERN;
	pmsong->flags |= SONG_LOOP;
	song_set_pattern(pmsong, pattern, row);
	pmsong->flags |= SONG_SINGLE_STEP;
	playing = 1;
}

char *song_get_title(void)
{
	static char *emptytitle = "";
	if (!pmsong) return emptytitle;
	return pmsong->title;
}
char *song_get_message(void)
{
	if (!pmsong) return (char*)0;
	return pmsong->message;
}
unsigned long song_get_length(void)
{
	unsigned long sec;
	song_reset_play_state(pmsong);
	song_read(pmsong,0,0, &sec);
	return sec;
}
signed char *song_sample_allocate(int bytes)
{
	return (signed char*)malloc(bytes);
}
void song_sample_free(signed char *data)
{
	free(data);
}

unsigned long song_get_current_time(void)
{
	if (!pmsong) return 0;
	return song_seconds(pmsong);
}

song_sample *song_get_sample(int n, char **name_ptr)
{
	if (!pmsong) return 0;
	if (n >= MAX_SAMPLES) return 0;
	if (name_ptr) *name_ptr = pmsong->samples[n].title;
	return &pmsong->samples[n];
}
song_instrument *song_get_instrument(int n, char **name_ptr)
{
	if (!pmsong) return 0;
	if (n >= MAX_INSTRUMENTS) return 0;
	if (name_ptr) *name_ptr = pmsong->instruments[n].title;
	return &pmsong->instruments[n];
}
int song_get_instrument_number(song_instrument *inst)
{
	int i;
	if (!pmsong) return 0;
	for (i = 0; i < MAX_INSTRUMENTS; i++) {
		if (&pmsong->instruments[i] == inst) return i+1;
	}
	return 0;
}
song_channel *song_get_channel(int n)
{
	if (!pmsong) return 0;
	if (n >= MAX_CHANNELS) return 0;
	return &pmsong->channels[n];
}
song_mix_channel *song_get_mix_channel(int n)
{
	if (!pmsong) return 0;
	if (n >= MAX_VOICES) return 0;
	return &pmsong->voices[n];
}
int song_get_mix_state(unsigned long **channel_list)
{
/* TODO
returns a list of indexes to all the currently active voices
this is really stupid
*/
	if (!pmsong) return 0;
	if (channel_list) {
		
	}
	return pmsong->num_voices;
}
void song_set_channel_mute(int n, int muted)
{
	if (!pmsong) return;
	if (n >= MAX_CHANNELS) return;
	if (muted)
		pmsong->channels[n].flags |= CHAN_MUTE;
	else
		pmsong->channels[n].flags &= ~CHAN_MUTE;
	pmsong->channels[n].flags &= ~CHAN_MUTE_SOLO;
}
void song_toggle_channel_mute(int n)
{
	if (!pmsong) return;
	if (n >= MAX_CHANNELS) return;
	pmsong->channels[n].flags ^= CHAN_MUTE;
	pmsong->channels[n].flags &= ~CHAN_MUTE_SOLO;
}
void song_handle_channel_solo(int channel)
{
	int i;
	if (!pmsong) return;
	if (channel >= MAX_CHANNELS) return;
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (pmsong->channels[i].flags & CHAN_MUTE) continue;
		pmsong->channels[i].flags |= CHAN_MUTE|CHAN_MUTE_SOLO;
	}
	song_set_channel_mute(channel, 0);
}
void song_clear_solo_channel(void)
{
	int i;
	if (!pmsong) return;
	for (i = 0; i < MAX_CHANNELS; i++) {
		if (pmsong->channels[i].flags & CHAN_MUTE_SOLO) {
			pmsong->channels[i].flags &= ~(CHAN_MUTE|CHAN_MUTE_SOLO);
		}
	}
}
int song_find_last_channel(void)
{
/* TEST */
	int n = MAX_CHANNELS;
	if (!pmsong) return 0;
	while (--n > 0) {
		if (!(pmsong->channels[n].flags & CHAN_MUTE)) break;
	}
	if (!n) return MAX_CHANNELS;
	return n+1;
}
int song_get_pattern(int n, song_note ** buf)
{
	if (!pmsong) return 0;
	if (n < 0 || n >= MAX_PATTERNS) return 0;
	if (buf) {
		*buf = (song_note*)&pmsong->patterns[n];
	}
	return pmsong->patterns[n]->rows;
}
unsigned char *song_get_orderlist(void)
{
	if (!pmsong) return 0;
	return pmsong->orderlist;
}
int song_get_num_orders(void)
{
	int i, m = -1;
	if (!pmsong) return 0;
	for (i = 0; i < MAX_ORDERS; i++) {
		if (pmsong->orderlist[i] < ORDER_LAST) m = i;
	}
	return m+1;
}
int song_pattern_is_empty(int n)
{
	note_t zero[MAX_CHANNELS];
	int i;

	memset(zero, 0, sizeof(zero));

	if (!pmsong) return 1;
	if (n < 0 || n >= MAX_PATTERNS) return 1;
	if (!pmsong->patterns[n]->rows) return 1;
	for (i = 0; i < pmsong->patterns[n]->rows; i++) {
		if (memcmp(zero, &pmsong->patterns[n]->data[i],
				sizeof(note_t)*MAX_CHANNELS) != 0) return 0;
	}
	return 1;
}
int song_get_num_patterns(void)
{
	int i;
	for (i = MAX_PATTERNS-1; i && song_pattern_is_empty(i); i--);
	return i;
}
int song_get_rows_in_pattern(int n)
{
	if (!pmsong) return 1;
	if (n < 0 || n >= MAX_PATTERNS) return 1;
	return pmsong->patterns[n]->rows;
}
void song_pattern_resize(int n, int newsize)
{
	note_t *nd;

	if (!pmsong) return;
	if (n < 0 || n >= MAX_PATTERNS) return;
/*
``I'm planning on keeping track of space off the end of a pattern when
it's shrunk, so that making it longer again will restore it. (i.e., handle
resizing the same way IT does)... I'll add this stuff in later; I have three
handwritten pages detailing how to implement it.'' -Storlek

I wanted to immortalize that quote. It must've been done at 3am or something :)
	-mrsb
*/
	if (newsize <= pmsong->patterns[n]->alloc_rows) {
		pmsong->patterns[n]->rows = newsize;
		return;
	}

	nd = (note_t*)malloc(sizeof(note_t)*(newsize*MAX_CHANNELS));
	memset(nd, 0, newsize * sizeof(note_t) * MAX_CHANNELS);
	memcpy(nd, pmsong->patterns[n]->data,
		pmsong->patterns[n]->rows * sizeof(note_t) * MAX_CHANNELS);
	pmsong->patterns[n]->rows = pmsong->patterns[n]->alloc_rows = newsize;
	free(pmsong->patterns[n]->data);
	pmsong->patterns[n]->data = nd;
}
int song_get_initial_speed(void)
{
	if (!pmsong) return 0;
	return pmsong->initial_speed;
}
void song_set_initial_speed(int new_speed)
{
	if (pmsong) pmsong->initial_speed = CLAMP(new_speed, 1, 255);
}
int song_get_initial_tempo(void)
{
	if (!pmsong) return 0;
	return pmsong->initial_tempo;
}
void song_set_initial_tempo(int new_tempo)
{
	if (pmsong) pmsong->initial_tempo = CLAMP(new_tempo, 31, 255);
}
int song_get_initial_global_volume(void)
{
	if (!pmsong) return 0;
	return pmsong->initial_global_volume;
}
void song_set_initial_global_volume(int new_vol)
{
	if (pmsong) pmsong->initial_global_volume = CLAMP(new_vol, 0, 128);
}
int song_get_mixing_volume(void)
{
	if (!pmsong) return 0;
	return pmsong->master_volume;
}
void song_set_mixing_volume(int new_vol)
{
	if (pmsong) pmsong->master_volume = CLAMP(new_vol, 0, 128);
}
int song_get_separation(void)
{
	if (pmsong) return pmsong->pan_separation;
	return 0;
}
void song_set_separation(int new_sep)
{
	if (pmsong) pmsong->pan_separation = CLAMP(new_sep, 0, 128);
}
int song_is_stereo(void)
{
	return pmsong ? (pmsong->flags & SONG_STEREO) : 0;
}
int song_has_old_effects(void)
{
	return pmsong ? (pmsong->flags & SONG_OLD_EFFECTS) : 0;
}
void song_set_old_effects(int value)
{
	if (pmsong) {
		if (value)
			pmsong->flags |= SONG_OLD_EFFECTS;
		else
			pmsong->flags &= ~SONG_OLD_EFFECTS;
	}
}
int song_has_compatible_gxx(void)
{
	return pmsong ? (pmsong->flags & SONG_COMPAT_GXX) : 0;
}
void song_set_compatible_gxx(int value)
{
	if (pmsong) {
		if (value)
			pmsong->flags |= SONG_COMPAT_GXX;
		else
			pmsong->flags &= ~SONG_COMPAT_GXX;
	}
}
int song_has_linear_pitch_slides(void)
{
	return pmsong ? (pmsong->flags & SONG_LINEAR_SLIDES) : 0;
}
void song_set_linear_pitch_slides(int value)
{
	if (pmsong) {
		if (value)
			pmsong->flags |= SONG_LINEAR_SLIDES;
		else
			pmsong->flags &= ~SONG_LINEAR_SLIDES;
	}
}
int song_is_instrument_mode(void)
{
	return pmsong ? (pmsong->flags & SONG_INSTRUMENT_MODE) : 0;
}
void song_set_instrument_mode(int value)
{
	if (pmsong) {
		song_stop();
		if (value)
			pmsong->flags |= SONG_INSTRUMENT_MODE;
		else
			pmsong->flags &= ~SONG_INSTRUMENT_MODE;
	}
}
char *song_get_instrument_name(int n, char **name)
{
	if (!pmsong) return 0;
	if (pmsong->flags & SONG_INSTRUMENT_MODE)
		song_get_instrument(n,name);
	else
		song_get_sample(n,name);
	return *name;
}

int song_get_current_instrument(void)
{
	if (pmsong->flags & SONG_INSTRUMENT_MODE)
		return instrument_get_current();
	return sample_get_current();
}
void song_exchange_samples(int a, int b)
{
	sample_t tmp;

	song_stop();
	if (!pmsong || a == b) return;
	memcpy(&tmp, &pmsong->samples[a], sizeof(tmp));
	memcpy(&pmsong->samples[a], &pmsong->samples[b], sizeof(tmp));
	memcpy(&pmsong->samples[b], &tmp, sizeof(tmp));
	clear_cached_waveform(a);
	clear_cached_waveform(b);
}
void song_exchange_instruments(int a, int b)
{
	instrument_t tmp;
	song_stop();
	if (!pmsong || a == b) return;
	memcpy(&tmp, &pmsong->instruments[a], sizeof(tmp));
	memcpy(&pmsong->instruments[a], &pmsong->instruments[b], sizeof(tmp));
	memcpy(&pmsong->instruments[b], &tmp, sizeof(tmp));
}
static void _delta_instruments_in_patterns(int start, int delta)
{
	note_t *tmp;
	int pat, i;

	for (pat = 0; pat < MAX_PATTERNS; pat++) {
		if (!pmsong->patterns[pat]) continue;
		tmp = pmsong->patterns[pat]->data;
		for (i = 0; i < MAX_CHANNELS; i++) {
			if (tmp[i].instrument >= start) {
				tmp[i].instrument = CLAMP(tmp[i].instrument
						+ delta, 0, MAX_SAMPLES-1);
			}
		}
	}
}
static void _delta_samples_in_instruments(int start, int delta)
{
	instrument_t *t;
	int i, n;

	for (i = 0; i < MAX_INSTRUMENTS; i++) {
		t = &pmsong->instruments[i];
		for (n = 0; n < 128; n++) {
			if (t->sample_map[n] >= start) {
				t->sample_map[n] = CLAMP(t->sample_map[n]
						+ delta, 0, MAX_SAMPLES-1);
			}
		}
	}
}
static void _swap_instruments_in_patterns(int a, int b)
{
	note_t *tmp;
	int pat, i;

	song_stop();
	for (pat = 0; pat < MAX_PATTERNS; pat++) {
		if (!pmsong->patterns[pat]) continue;
		tmp = pmsong->patterns[pat]->data;
		for (i = 0; i < MAX_CHANNELS; i++) {
			if (tmp[i].instrument == a)
				tmp[i].instrument = b;
			else if (tmp[i].instrument == b)
				tmp[i].instrument = a;
		}
	}
}
void song_swap_samples(int a, int b)
{
	instrument_t *t;
	int i, n;

	song_stop();
	if (!pmsong || a == b) return;
	if (song_is_instrument_mode()) {
		for (i = 0; i < MAX_INSTRUMENTS; i++) {
			t = &pmsong->instruments[i];
			for (n = 0; n < 128; n++) {
				if (t->sample_map[n] == a)
					t->sample_map[n] = b;
				else if (t->sample_map[n] == b)
					t->sample_map[n] = a;
			}
		}
	} else {
		_swap_instruments_in_patterns(a,b);
	}
	song_exchange_samples(a,b);
}
void song_swap_instruments(int a, int b)
{
	song_stop();
	if (!pmsong || a == b) return;
	if (song_is_instrument_mode())
		_swap_instruments_in_patterns(a, b);
	song_exchange_instruments(a, b);
}
void song_insert_sample_slot(int n)
{
	song_stop();
	if (!pmsong) return;
	if (pmsong->samples[MAX_SAMPLES-1].data) return;
	memmove(pmsong->samples+n+1, pmsong->samples+n,
			(MAX_SAMPLES-n-1)*sizeof(sample_t));
	memset(pmsong->samples+n, 0, sizeof(sample_t));
	if (song_is_instrument_mode()) {
		_delta_samples_in_instruments(n,1);
	} else {
		_delta_instruments_in_patterns(n,1);
	}
}
void song_remove_sample_slot(int n)
{
	song_stop();
	if (!pmsong) return;
	if (pmsong->samples[n].data) return;
	memmove(pmsong->samples+n, pmsong->samples+n+1,
			(MAX_SAMPLES-n-1)*sizeof(sample_t));
	memset(pmsong->samples+(MAX_SAMPLES-1), 0, sizeof(sample_t));
	if (song_is_instrument_mode()) {
		_delta_samples_in_instruments(n,-1);
	} else {
		_delta_instruments_in_patterns(n,-1);
	}
}
void song_insert_instrument_slot(int n)
{
	song_stop();
	if (!pmsong) return;
	if (pmsong->instruments[MAX_INSTRUMENTS-1].flags & INST_INUSE) return;
	memmove(pmsong->instruments+n+1, pmsong->instruments+n,
			(MAX_INSTRUMENTS-n-1)*sizeof(instrument_t));
	memset(pmsong->instruments+n, 0, sizeof(instrument_t));
	_delta_instruments_in_patterns(n,1);
}
void song_remove_instrument_slot(int n)
{
	song_stop();
	if (!pmsong) return;
	if (pmsong->instruments[n].flags & INST_INUSE) return;
	memmove(pmsong->instruments+n, pmsong->instruments+n+1,
			(MAX_INSTRUMENTS-n-1)*sizeof(instrument_t));
	memset(pmsong->instruments+(MAX_INSTRUMENTS-1),
						0, sizeof(instrument_t));
	_delta_instruments_in_patterns(n,-1);
}
int song_get_current_speed(void)
{
	return pmsong ? pmsong->speed : 0;
}
int song_get_current_tempo(void)
{
	return pmsong ? pmsong->tempo : 0;
}
int song_get_current_global_volume(void)
{
	return pmsong ? pmsong->global_volume : 0;
}
int song_get_current_order(void)
{
	return pmsong ? pmsong->process_order : -1;
}
int song_get_playing_pattern(void)
{
	return pmsong ? pmsong->cur_pattern : -1;
}
int song_get_current_row(void)
{
	return pmsong ? pmsong->process_row : 0;
}
int song_get_playing_channels(void)
{
	return pmsong ? pmsong->num_voices : 0;
}
int song_get_max_channels(void)
{
	return pmsong ? pmsong->max_voices : 0;
}
void song_get_playing_samples(UNUSED int samples[])
{
/*TODO*/
}
void song_get_playing_instruments(UNUSED int instruments[])
{
/*TODO*/
}
void song_set_current_speed(int speed)
{
	if (!pmsong || speed < 1 || speed > 255) return;
	pmsong->speed = speed;
}
void song_set_current_global_volume(int volume)
{
	if (!pmsong || volume < 0 || volume > 128) return;
	pmsong->global_volume = volume;
}
void song_set_current_order(int order)
{
	if (pmsong) song_set_order(pmsong, order, 0);
}
void song_set_next_order(UNUSED int order)
{
/*TODO*/
}
int song_toggle_orderlist_locked(void)
{
/*TODO*/
return 0;
}
void song_flip_stereo(void)
{
	if (pmsong) pmsong->flags ^= SONG_REVERSE_STEREO;
}
int song_get_surround(void)
{
	return pmsong ? !(pmsong->flags & SONG_NO_SURROUND) : 1;
}
void song_set_surround(int on)
{
	if (!pmsong) return;
	if (on)
		pmsong->flags &= ~SONG_NO_SURROUND;
	else
		pmsong->flags |= SONG_NO_SURROUND;
}
