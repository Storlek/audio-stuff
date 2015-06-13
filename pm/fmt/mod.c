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

/* loads everything but old 15-instrument mods... yes, even FLT8 and WOW files */

int fmt_mod_load(song_t *song, FILE *fp)
{
	uint8_t tag[4];
	int n, npat, pat, chan, nchan, nord;
	note_t *note;
	uint16_t tmp;
	int startrekker = 0;
	int test_wow = 0;
	long samplesize = 0;

	/* check the tag (and set the number of channels) -- this is ugly, so don't look */
	fseek(fp, 1080, SEEK_SET);
	fread(tag, 1, 4, fp);
	if (!memcmp(tag, "M.K.", 4)) {
		/* M.K. = Protracker etc., or Mod's Grave (*.wow) */
		nchan = 4;
		test_wow = 1;
	} else if (!memcmp(tag, "M!K!", 4) || !memcmp(tag, "M&K!", 4)
		   || !memcmp(tag, "N.T.", 4) || !memcmp(tag, "FLT4", 4)) {
		/* M!K! = Protracker
		 * N.T., M&K! = Noisetracker
		 * FLT4 = Startrekker
		 * I've never seen any of these except "M!K!"... */
		nchan = 4;
	} else if (!memcmp(tag, "FLT8", 4)) {
		nchan = 8;
		startrekker = 1;
	} else if (!memcmp(tag, "OCTA", 4) || !memcmp(tag, "CD81", 4)) {
		/* OCTA = Amiga Oktalyzer
		 * CD81 = Atari Oktalyser; Falcon */
		nchan = 8;
	} else if (tag[0] > '0' && tag[0] <= '9' && !memcmp(tag + 1, "CHN", 3)) {
		/* nCHN = Fast Tracker (if n is even) or TakeTracker (if n = 5, 7, or 9) */
		nchan = tag[0] - '0';
	} else if (tag[0] > '0' && tag[0] <= '9' && tag[1] >= '0' && tag[1] <= '9'
		   && tag[2] == 'C' && (tag[3] == 'H' || tag[3] == 'N')) {
		/* nnCH = Fast Tracker (if n is even and <= 32) or TakeTracker (if n = 11, 13, 15) */
		nchan = 10 * (tag[0] - '0') + (tag[1] - '0');
	} else if (!memcmp(tag, "TDZ", 3) && tag[3] > '0' && tag[3] <= '9') {
		/* TDZ[1-3] = TakeTracker */
		nchan = tag[3] - '0';
	} else {
		return LOAD_UNSUPPORTED;
	}
	
	/* suppose the tag is 90CH :) */
	if (nchan > 64) {
		//fprintf(stderr, "%s: Too many channels!\n", filename);
		return LOAD_FORMAT_ERROR;
	}

	/* read the title */
	rewind(fp);
	fread(song->title, 1, 20, fp);
	song->title[20] = 0;
	
	/* sample headers */
	for (n = 1; n < 32; n++) {
		fread(song->samples[n].title, 1, 22, fp);
		song->samples[n].title[22] = 0;
		
		fread(&tmp, 1, 2, fp);
		song->samples[n].length = bswapBE16(tmp) * 2;
		
		/* this is only necessary for the wow test... */
		samplesize += song->samples[n].length;
		
		song->samples[n].c5speed = MOD_FINETUNE_TABLE[fgetc(fp) & 0xf];
		
		song->samples[n].volume = fgetc(fp);
		if (song->samples[n].volume > 64)
			song->samples[n].volume = 64;
		song->samples[n].global_volume = 64;
		
		fread(&tmp, 1, 2, fp);
		song->samples[n].loop_start = bswapBE16(tmp) * 2;
		fread(&tmp, 1, 2, fp);
		tmp = bswapBE16(tmp) * 2;
		if (tmp > 2)
			song->samples[n].flags |= SAMP_LOOP;
		song->samples[n].loop_end = song->samples[n].loop_start + tmp;
		song->samples[n].vibrato_speed = 0;
		song->samples[n].vibrato_depth = 0;
		song->samples[n].vibrato_rate = 0;
		song->samples[n].vibrato_table = 0;
	}
	
	/* pattern/order stuff */
	nord = fgetc(fp);
	fgetc(fp); /* restart position (don't care) */
	fread(song->orderlist, 1, 128, fp);
	npat = 0;
	if (startrekker && nchan == 8) {
		/* from mikmod: if the file says FLT8, but the orderlist
		has odd numbers, it's probably really an FLT4 */
		for (n = 0; n < 128; n++) {
			if (song->orderlist[n] & 1) {
				nchan = 4;
				break;
			}
		}
	}
	if (startrekker && nchan == 8) {
		for (n = 0; n < 128; n++)
			song->orderlist[n] >>= 1;
	}
	for (n = 0; n < 128; n++) {
		if (song->orderlist[n] > npat)
			npat = song->orderlist[n];
	}
	/* set all the extra orders to the end-of-song marker */
	memset(song->orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);
	
	/* hey, is this a wow file? */
	if (test_wow) {
		fseek(fp, 0, SEEK_END);
		if (ftell(fp) >= 2048 * npat + samplesize + 3132)
			nchan = 8;
	}
	
	fseek(fp, 1084, SEEK_SET);
	
	/* pattern data */
	if (startrekker && nchan == 8) {
		for (pat = 0; pat <= npat; pat++) {
			song->patterns[pat] = pattern_allocate(64);
			note = song->patterns[pat]->data;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					fread(p, 1, 4, fp);
					mod_import_note(p, note);
					pt_import_effect(note);
				}
			}
			note = song->patterns[pat]->data + 4;
			for (n = 0; n < 64; n++, note += 60) {
				for (chan = 0; chan < 4; chan++, note++) {
					uint8_t p[4];
					fread(p, 1, 4, fp);
					mod_import_note(p, note);
					pt_import_effect(note);
				}
			}
		}
	} else {
		for (pat = 0; pat <= npat; pat++) {
			song->patterns[pat] = pattern_allocate(64);
			note = song->patterns[pat]->data;
			for (n = 0; n < 64; n++, note += 64 - nchan) {
				for (chan = 0; chan < nchan; chan++, note++) {
					uint8_t p[4];
					fread(p, 1, 4, fp);
					mod_import_note(p, note);
					pt_import_effect(note);
				}
			}
		}
	}
	
	/* sample data */
	for (n = 1; n < 32; n++) {
		char *ptr;
		
		if (song->samples[n].length == 0)
			continue;
		ptr = malloc(song->samples[n].length);
		fread(ptr, 1, song->samples[n].length, fp);
		song->samples[n].data = (int8_t *) ptr;
	}
	
	/* set some other header info that's always the same for .mod files */
	song->flags = (SONG_OLD_EFFECTS | SONG_COMPAT_GXX | SONG_STEREO);
	for (n = 0; n < nchan; n++)
		song->channels[n].initial_panning = PROTRACKER_PANNING[n & 3];
	for (; n < MAX_CHANNELS; n++)
		song->channels[n].flags = CHAN_MUTE;
	
	song->pan_separation = 64;

	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}
	
	/* done! */
	return LOAD_SUCCESS;
}
