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

#define MIXING_RATE 44100

/* --------------------------------------------------------------------------------------------------------- */
/* printing stuff out */

char *get_note_string_short(const note_t *note, char *buf)
{
	int n;

	switch (note->note) {
	case NOTE_NONE:
		break;
	case NOTE_CUT:
		buf[0] = buf[1] = buf[2] = '^';
		return buf;
	case NOTE_OFF:
		buf[0] = buf[1] = buf[2] = '=';
		return buf;
	default:
		if (NOTE_IS_FADE(note->note)) {
			buf[0] = buf[1] = buf[2] = '~';
			return buf;
		}
		n = note->note;
		buf[2] = n / 12 + '0';
		n %= 12;
		n <<= 1;
		buf[0] = NOTES[n];
		buf[1] = NOTES[n + 1];
		return buf;
	}
	if (note->instrument) {
		n = note->instrument;
		buf[0] = ' ';
		buf[1] = n / 10 + '0';
		buf[2] = n % 10 + '0';
	} else if (note->volume != VOL_NONE) {
		n = note->volume;
		buf[0] = '$';
		buf[1] = HEXDIGITS[n >> 4];
		buf[2] = HEXDIGITS[n & 0xf];
	} else if (note->effect || note->param) {
		buf[0] = note->effect ? : '.';
		buf[1] = HEXDIGITS[note->param >> 4];
		buf[2] = HEXDIGITS[note->param & 0xf];
	} else {
		buf[0] = buf[1] = buf[2] = '.';
	}
	return buf;
}

char *get_note_string_long(const note_t *note, char *buf)
{
	int n;

	strcpy(buf, "--- -- -- -00");
	switch (note->note) {
	case NOTE_NONE:
		break;
	case NOTE_CUT:
		buf[0] = buf[1] = buf[2] = '^';
		break;
	case NOTE_OFF:
		buf[0] = buf[1] = buf[2] = '=';
		break;
	default:
		if (NOTE_IS_FADE(note->note)) {
			buf[0] = buf[1] = buf[2] = '~';
			break;
		}
		n = note->note;
		buf[2] = n / 12 + '0';
		n %= 12;
		n <<= 1;
		buf[0] = NOTES[n];
		buf[1] = NOTES[n + 1];
		break;
	}
	if (note->instrument) {
		n = note->instrument;
		buf[4] = n / 10 + '0';
		buf[5] = n % 10 + '0';
	}
	if (note->volume != VOL_NONE) {
		n = note->volume;
		buf[6] = '$';
		buf[7] = HEXDIGITS[n >> 4];
		buf[8] = HEXDIGITS[n & 0xf];
	}
	if (note->effect) {
		buf[10] = note->effect;
	}
	if (note->param) {
		buf[11] = HEXDIGITS[note->param >> 4];
		buf[12] = HEXDIGITS[note->param & 0xf];
	}
	return buf;
}

void print_row(song_t *song)
{
	int n;
	char buf[16];
	note_t *note;
	const char *sep;
	
	if (song->cur_row % song->highlight_major == 0) {
		sep = MAGENTA "|" YELLOW;
	} else if (song->cur_row % song->highlight_minor == 0) {
		sep = MAGENTA "|" BRIGHT BLUE;
	} else {
		sep = MAGENTA "|" BRIGHT GREEN;
	}
	
	printf(GRAY "%02X.%02X C%03d/%03d", song->cur_pattern, song->cur_row, song->num_voices, song->max_voices);
	
#if 1
	/* this works with a 128x48 framebuffer */
	for (n = 0, note = song->row_data; n < 8; n++, note++) {
		printf("%s%s", sep, get_note_string_long(note, buf));
	}
#else
	/* for little 80x25 terminals */
	for (n = 0, note = song->row_data; n < 4; n++, note++) {
		printf(" %s %s", sep, get_note_string_long(note, buf));
	}
#endif

	fputs(NORMAL "\n", stdout);
}

void dump_general(song_t *song)
{
//	unsigned long sec;
//	song_read(song, 0, 0, &sec);
	printf("Song information:\n");
	printf("\tTitle: \"%s\"\n", song->title);
//	printf("\tESTIMATED time: %lu\n", sec);
	printf("\tTempo=%d Speed=%d GV=%d MV=%d PanSep=%d Highlight=%d/%d\n",
	       song->initial_tempo, song->initial_speed, song->initial_global_volume, song->master_volume,
	       song->pan_separation, song->highlight_minor, song->highlight_major);
	printf("\tFlags:");
	if (song->flags) {
		if (song->flags & SONG_OLD_EFFECTS)
			printf(" old_effects");
		if (song->flags & SONG_COMPAT_GXX)
			printf(" compat_gxx");
		if (song->flags & SONG_INSTRUMENT_MODE)
			printf(" instruments");
		if (song->flags & SONG_STEREO)
			printf(" stereo");
		if (song->flags & SONG_LINEAR_SLIDES)
			printf(" linear_slides");
	} else {
		printf(" (none)");
	}
	printf("\n");
}

void dump_samples(song_t *song)
{
	int n;
	sample_t *s;
	
	printf("Samples:\n");
	printf("\t #  Title                      Filename      DataPtr.  Length  LStart  L.End.  Vl  C5Spd  Flags\n");
	printf("\t--  -------------------------  ------------  --------  ------  ------  ------  --  -----  -----\n");
	for (n = 1, s = song->samples + 1; n < MAX_SAMPLES; n++, s++) {
		printf("\t%2d  %-25s  %-12s  %6ld  %6ld  %6ld  %2d  %5d ",
		       n, s->title, s->filename, //(int) s->data,
		       s->length, s->loop_start, s->loop_end,
		       s->volume, s->c5speed);
		if (s->flags & SAMP_LOOP) {
			printf(" L");
			if (s->flags & SAMP_PINGPONG)
				printf("P");
		}
		if (s->flags & SAMP_16BIT)
			printf(" 16");
		printf("\n");
	}
}

void dump_instruments(song_t *song)
{
	int n;
	instrument_t *i;
	const char *nna[4] = {"Off", "Cut", "Cont", "Fade"};
	const char *dct[4] = {"Off", "Note", "Samp", "Ins"};
	const char *dca[3] = {"Cut", "Off", "Fade"};
	
	printf("Instruments:\n");
	printf("\t #  Title                      Filename      NNA. DCT. DCA. Fade PPS PPC GVl Pn RV%% RP  Flags\n");
	printf("\t--  -------------------------  ------------  ---- ---- ---- ---- --- --- --- -- --- --  -----\n");
	for (n = 1, i = song->instruments + 1; n < MAX_INSTRUMENTS; n++, i++) {
		printf("\t%2d  %-25s  %-12s  %-4s %-4s %-4s %4d %3d %3d %3d %2d %3d %2d ",
		       n, i->title, i->filename, nna[i->nna], dct[i->dct], dca[i->dca],
		       i->fadeout, i->pitch_pan_separation, i->pitch_pan_center,
		       i->global_volume, i->panning, i->rand_vol_var, i->rand_pan_var);
		if (i->flags & INST_USE_PANNING)
			printf(" Pan");
		printf("\n");
	}
}

void dump_channels(song_t *song)
{
	int r, c, n;
	channel_t *ch;
	
	printf("Channels:\n");
	for (r = 0; r < 8; r++) {
		printf("\t");
		for (c = 0; c < 8; c++) {
			n = 8 * c + r;
			ch = song->channels + n;
			
			if (ch->flags & CHAN_MUTE)
				printf("%02d -- %02d   ", n + 1, ch->initial_channel_volume);
			else if (ch->initial_panning == PAN_SURROUND)
				printf("%02d Su %02d   ", n + 1, ch->initial_channel_volume);
			else if (ch->initial_panning == 0)
				printf("%02d L  %02d   ", n + 1, ch->initial_channel_volume);
			else if (ch->initial_panning == 64)
				printf("%02d  R %02d   ", n + 1, ch->initial_channel_volume);
			else
				printf("%02d %02d %02d   ", n + 1, ch->initial_panning,
				       ch->initial_channel_volume);
		}
		printf("\n");
	}
}

void dump_orderlist(song_t *song)
{
	int n, i, c;
	
	printf("Orderlist:\n");
	for (n = 0; n < 32; n++) {
		printf("\t");
		for (i = 0; i < 8; i++) {
			c = 32 * i + n;
			switch (song->orderlist[c]) {
			case ORDER_LAST:
				printf("%03d ---   ", c);
				break;
			case ORDER_SKIP:
				printf("%03d +++   ", c);
				break;
			default:
				printf("%03d %03d   ", c, song->orderlist[c]);
				break;
			}
		}
		printf("\n");
	}
}

void dump_pattern(song_t *song, int n)
{
	pattern_t *pattern;
	note_t *note;
	char buf[16];
	int i, j;
	
	printf("Pattern %d:\n", n);
	pattern = pattern_get(song, n);
	note = pattern->data;
	for (i = 0; i < pattern->rows; i++) {
		printf("\t%03d", i);
		for (j = 0; j < MAX_CHANNELS; j++, note++)
			printf("   %s", get_note_string_long(note, buf));
		printf("\n");
	}
}

/* --------------------------------------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	song_t *song;
	int8_t buf[2048];
	int r, n;
	ao_device *dev;
	ao_sample_format fmt = {16, MIXING_RATE, 2, AO_FMT_NATIVE, NULL};

	if (argc < 2) {
		printf("usage: %s <filename>...\n", argv[0]);
		exit(1);
	}

	ao_initialize();
	song = song_alloc();
	
	/* should have an audio setup function that sets this instead of messing with the song directly */
	song->mixing_rate = MIXING_RATE;

	for (n = 1; n < argc; n++) {
		printf("%s\n", argv[n]);
		if (!song_load(song, argv[n]))
			continue;

		dump_general(song);
		//dump_samples(song);
		//dump_instruments(song);
		//dump_channels(song);
		//dump_orderlist(song);
		//dump_pattern(song, 12);

		song_reset_play_state(song);

		dev = ao_open_live(ao_default_driver_id(), &fmt, NULL);
		//dev = ao_open_file(ao_driver_id("au"), "output.au", 1, &fmt, NULL);

		while ((r = song_read(song, (char *) buf, sizeof(buf), 0)) != 0)
			ao_play(dev, (char *) buf, r);
		ao_close(dev);
	}

	song_free(song);
	ao_shutdown();
	return 0;
}
