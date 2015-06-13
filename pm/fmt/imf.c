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

/* I started this because I wanted to add an IMF loader to Schism Tracker and thought it would be better to
write a loader for my player first, because that way I would be able to focus more toward the file format
instead of having my mind half on how Modplug does everything.

Since I never bothered to finish this, I don't suppose it matters much *how* I didn't write it. :) */

/* 16 bytes */
struct imf_channel {
	char name[12]; /* asciz */
	/* status:
	 *     0x00 = channel enabled;
	 *     0x01 = mute (processed but not played);
	 *     0x02 = channel disabled (not processed)
	 * panning is a full 0x00->0xff value */
	uint8_t chorus, reverb, panning, status;
};

/* 832 bytes */
struct imf_header {
	char title[32]; /* asciz */
	uint16_t ordnum, patnum, insnum;
	uint16_t flags; /* & 1 => linear */
	char res[8]; /* nothing */
	uint8_t tempo; /* Axx (1..255) */
	uint8_t bpm; /* bpm -> Txx (32..255) */
	uint8_t master; /* master -> Vxx (0..64) */
	uint8_t amp; /* amp -> mixing volume (4..127) */
	char res2[8]; /* nothing */
	char im10[4]; /* "IM10" tag */
	struct imf_channel channels[32];
	/* 0xff = +++; blank out any orders beyond nord */
	uint8_t orderlist[256];
};

static void load_imf_pattern(song_t *song, int pat, uint32_t ignore_channels, FILE *fp)
{
	uint16_t length, nrows;
	uint8_t status, channel;
	int row, startpos;
	note_t *row_data, *note, junk_note;
	
	startpos = ftell(fp);
	
	fread(&length, 2, 1, fp);
	length = bswapLE16(length);
	fread(&nrows, 2, 1, fp);
	nrows = bswapLE16(nrows);
	printf("pattern %d: %d bytes, %d rows\n", pat, length, nrows);
	
	song->patterns[pat] = pattern_allocate(nrows);
	row_data = song->patterns[pat]->data;
	
	row = 0;
	while (row < nrows) {
		status = fgetc(fp);
		if (status == 0) {
			row++;
			row_data += MAX_CHANNELS;
			continue;
		}
		
		channel = status & 0x1f;
		
		if (ignore_channels & (1 << channel)) {
			/* should do this better, i.e. not go through the whole process of deciding
			what to do with the effects since they're just being thrown out */
			printf("warning: disabled channel contains data\n");
			note = &junk_note;
		} else {
			note = row_data + channel;
		}
		
		if (status & 0x20) {
			/* read note/instrument */
			note->note = fgetc(fp);
			note->instrument = fgetc(fp);
			if (note->note == 160) {
				note->note = NOTE_OFF; /* ??? */
			} else if (note->note == 255) {
				note->note = NOTE_NONE; /* ??? */
			} else if (note->note == 0 || note->note > NOTE_LAST) {
				printf("%d.%d.%d: funny note %d\n", pat, row, channel, note->note);
			}
		}
		if ((status & 0xc0) == 0xc0) {
			uint8_t e1c, e1d, e2c, e2d;
			
			/* read both effects and figure out what to do with them */
			e1c = fgetc(fp);
			e1d = fgetc(fp);
			e2c = fgetc(fp);
			e2d = fgetc(fp);
			if (e1c == 0xc) {
				note->volume = MIN(e1d, 0x40);
				note->effect = e2c;
				note->param = e2d;
			} else if (e2c == 0xc) {
				note->volume = MIN(e2d, 0x40);
				note->effect = e1c;
				note->param = e1d;
			} else if (e1c == 0xa) {
				note->volume = e1d * 64 / 255 + 128;
				note->effect = e2c;
				note->param = e2d;
			} else if (e2c == 0xa) {
				note->volume = e2d * 64 / 255 + 128;
				note->effect = e1c;
				note->param = e1d;
			} else {
				/* check if one of the effects is a 'global' effect
				-- if so, put it in some unused channel instead.
				otherwise pick the most important effect. */
				printf("lost an effect!\n");
				note->effect = e2c;
				note->param = e2d;
			}
		} else if (status & 0xc0) {
			/* there's one effect, just stick it in the effect column */
			note->effect = fgetc(fp);
			note->param = fgetc(fp);
		}
		if (note->effect >= 1 && note->effect <= 9) {
			note->effect += '0';
		} else if (note->effect >= 0xa && note->effect <= 0x23) {
			note->effect += 'A' - 1;
		} else if (note->effect != 0) {
			printf("warning: stray effect %x\n", note->effect);
		}
	}
	
	if (ftell(fp) - startpos != length)
		printf("warning: expected %d bytes, but read %ld bytes\n", length, ftell(fp) - startpos);
}

int fmt_imf_load(song_t *song, FILE *fp)
{
	struct imf_header hdr;
	int n;
	uint32_t ignore_channels = 0; /* bit set for each channel that's completely disabled */

	/* TODO: endianness */
	fread(&hdr, 832, 1, fp);

	if (memcmp(hdr.im10, "IM10", 4) != 0)
		return LOAD_UNSUPPORTED;

	memcpy(song->title, hdr.title, 25);
	song->title[25] = 0;
	
	if (hdr.flags & 1)
		song->flags |= SONG_LINEAR_SLIDES;
	song->initial_speed = hdr.tempo;
	song->initial_tempo = hdr.bpm;
	song->initial_global_volume = 2 * hdr.master;
	song->master_volume = hdr.amp;
	
	//printf("%d orders, %d patterns, %d instruments; %s frequency table\n",
	//       hdr.ordnum, hdr.patnum, hdr.insnum, (hdr.flags & 1) ? "linear" : "amiga");
	//printf("initial tempo %d, bpm %d, master %d, amp %d\n", hdr.tempo, hdr.bpm, hdr.master, hdr.amp);
	for (n = 0; n < 32; n++) {
		song->channels[n].initial_panning = hdr.channels[n].panning * 64 / 255;
		/* TODO: reverb/chorus??? */
		switch (hdr.channels[n].status) {
		case 0: /* enabled; don't worry about it */
			break;
		case 1: /* mute */
			song->channels[n].flags |= CHAN_MUTE;
			break;
		case 2: /* disabled */
			song->channels[n].flags |= CHAN_MUTE;
			ignore_channels |= (1 << n);
			break;
		default: /* uhhhh.... freak out */
			fprintf(stderr, "imf: channel %d has unknown status %d\n", n, hdr.channels[n].status);
			return LOAD_FORMAT_ERROR;
		}
	}
	for (; n < MAX_CHANNELS; n++)
		song->channels[n].flags |= CHAN_MUTE;
	
	for (n = 0; n < hdr.ordnum; n++)
		song->orderlist[n] = ((hdr.orderlist[n] == 0xff) ? ORDER_SKIP : hdr.orderlist[n]);
	
	for (n = 0; n < hdr.patnum; n++) {
		load_imf_pattern(song, n, ignore_channels, fp);
	}
	
	/* haven't bothered finishing this */

	//dump_general(song);
	//dump_channels(song);
	//dump_orderlist(song);
	dump_pattern(song, 9);

	return LOAD_FORMAT_ERROR;
}
