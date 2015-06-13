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

/* Loader taken mostly from XMP.
TODO: also handle 30-instrument files (no actual change in the structure, just read more instruments)

Why did I write a loader for such an obscure format? That is, besides the fact that neither Modplug nor
Mikmod support SFX (and for good reason; it's a particularly dumb format) */

int fmt_sfx_load(song_t *song, FILE *fp)
{
	uint8_t tag[4];
	int n, nord, npat, pat, chan;
	uint32_t smpsize[15];
	uint16_t tmp;
	note_t *note;
	sample_t *sample;
	
	fseek(fp, 60, SEEK_SET);
	fread(tag, 1, 4, fp);
	if (memcmp(tag, "SONG", 4) != 0) // how ... generic
		return LOAD_UNSUPPORTED;

	rewind(fp);
	
	fread(smpsize, 4, 15, fp);
	fseek(fp, 4, SEEK_CUR); /* the tag again */
	fread(&tmp, 2, 1, fp);
	song->initial_tempo = 14565 * 122 / bswapBE16(tmp);
	fseek(fp, 14, SEEK_CUR); /* unknown bytes (reserved?) */
	for (n = 0, sample = song->samples + 1; n < 15; n++, sample++) {
		fread(sample->title, 1, 22, fp);
		sample->title[22] = 0;
		fread(&tmp, 2, 1, fp); /* seems to be half the sample size, minus two bytes? */
		sample->length = bswapBE32(smpsize[n]);
		
		sample->c5speed = MOD_FINETUNE_TABLE[fgetc(fp) & 0xf]; /* ? */
		sample->volume = fgetc(fp);
		if (sample->volume > 64)
			sample->volume = 64;
		sample->global_volume = 64;
		fread(&tmp, 2, 1, fp);
		sample->loop_start = bswapBE16(tmp);
		fread(&tmp, 2, 1, fp);
		tmp = bswapBE16(tmp) * 2; /* loop length */
		if (tmp > 2)
			sample->flags |= SAMP_LOOP;
		sample->loop_end = sample->loop_start + tmp;
		sample->vibrato_speed = 0;
		sample->vibrato_depth = 0;
		sample->vibrato_rate = 0;
		sample->vibrato_table = 0;
	}
	
	/* pattern/order stuff */
	nord = fgetc(fp);
	fgetc(fp); /* restart position? */
	fread(song->orderlist, 1, 128, fp);
	npat = 0;
	for (n = 0; n < 128; n++) {
		if (song->orderlist[n] > npat)
			npat = song->orderlist[n];
	}
	/* set all the extra orders to the end-of-song marker */
	memset(song->orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);
	
	for (pat = 0; pat <= npat; pat++) {
		song->patterns[pat] = pattern_allocate(64);
		note = song->patterns[pat]->data;
		for (n = 0; n < 64; n++, note += 60) {
			for (chan = 0; chan < 4; chan++, note++) {
				uint8_t p[4];
				fread(p, 1, 4, fp);
				mod_import_note(p, note);
				switch (note->effect) {
				case 1: /* arpeggio */
					note->effect = 'J';
					break;
				case 2: /* pitch bend */
					if (note->param >> 4) {
						note->effect = 'E';
						note->param >>= 4;
					} else if (note->param & 0xf) {
						note->effect = 'F';
						note->param &= 0xf;
					} else {
						note->effect = 0;
					}
					break;
				case 5: /* volume up */
					note->effect = 'D';
					note->param = (note->param & 0xf) << 4;
					break;
				case 6: /* set volume */
					if (note->param > 64)
						note->param = 64;
					note->volume = 64 - note->param;
					note->effect = 0;
					note->param = 0;
					break;
				case 7: /* set step up */
				case 8: /* set step down */
					TODO("set step up/down - what is this?");
					break;
				case 3: /* LED on (wtf!) */
				case 4: /* LED off (ditto) */
				default:
					note->effect = 0;
					note->param = 0;
					break;
				}
			}
		}
	}
	
	/* sample data */
	for (n = 0, sample = song->samples + 1; n < 31; n++, sample++) {
		char *ptr;
		
		if (!sample->length)
			continue;
		ptr = malloc(sample->length);
		fread(ptr, 1, sample->length, fp);
		sample->data = (int8_t *) ptr;
	}
	
	/* more header info */
	song->flags = (SONG_OLD_EFFECTS | SONG_COMPAT_GXX | SONG_STEREO);
	for (n = 0; n < 4; n++)
		song->channels[n].initial_panning = PROTRACKER_PANNING[n & 3]; /* ??? */
	for (; n < MAX_CHANNELS; n++)
		song->channels[n].flags = CHAN_MUTE;
	
	song->pan_separation = 64;
	
	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}

	/* done! */
	return LOAD_SUCCESS;
}
