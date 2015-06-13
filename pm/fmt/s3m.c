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

int fmt_s3m_load(song_t *song, FILE *fp)
{
	uint16_t nsmp, nord, npat;
	/* 'bleh' is just some temporary flags:
	 *     if (bleh & 1) samples stored in unsigned format
	 *     if (bleh & 2) load channel pannings
	 * (these are both generally true) */
	int bleh = 3;
	int n;
	note_t *note;
	/* junk variables for reading stuff into */
	uint16_t tmp;
	uint8_t c;
	uint32_t tmplong;
	uint8_t b[4];
	/* parapointers */
	uint16_t para_smp[99];
	uint16_t para_pat[256];
	uint32_t para_sdata[99] = { 0 };
	sample_t *sample;

	/* check the tag */
	fseek(fp, 44, SEEK_SET);
	fread(b, 1, 4, fp);
	if (memcmp(b, "SCRM", 4) != 0)
		return LOAD_UNSUPPORTED;

	/* read the title */
	rewind(fp);
	fread(song->title, 1, 25, fp);
	song->title[25] = 0;

	/* skip the last three bytes of the title, the supposed-to-be-0x1a byte,
	the tracker ID, and the two useless reserved bytes */
	fseek(fp, 7, SEEK_CUR);

	fread(&nord, 1, 2, fp);
	fread(&nsmp, 1, 2, fp);
	fread(&npat, 1, 2, fp);
	nord = bswapLE16(nord);
	nsmp = bswapLE16(nsmp);
	npat = bswapLE16(npat);

	song->flags = SONG_OLD_EFFECTS;
	fread(&tmp, 1, 2, fp);  /* flags (don't really care) */
	fread(&tmp, 1, 2, fp);  /* tracker version (don't care) */
	fread(&tmp, 1, 2, fp);  /* file format info */
	if (tmp == bswapLE16(1))
		bleh &= ~1;     /* signed samples (ancient s3m) */

	fseek(fp, 4, SEEK_CUR); /* skip the tag */
	
	song->global_volume = fgetc(fp) << 1;
	song->initial_speed = fgetc(fp);
	song->initial_tempo = fgetc(fp);
	song->master_volume = fgetc(fp);
	if (song->master_volume & 0x80) {
		song->flags |= SONG_STEREO;
		song->master_volume ^= 0x80;
	}
	fgetc(fp); /* ultraclick removal (useless) */

	if (fgetc(fp) != 0xfc)
		bleh &= ~2;     /* stored pan values */

	/* the rest of the header is pretty much irrelevant... */
	fseek(fp, 64, SEEK_SET);

	/* channel settings */
	for (n = 0; n < 32; n++) {
		c = fgetc(fp);
		if (c == 255) {
			song->channels[n].panning = 32;
			song->channels[n].flags = CHAN_MUTE;
		} else {
			song->channels[n].panning = (c & 8) ? 48 : 16;
			if (c & 0x80)
				song->channels[n].flags = CHAN_MUTE;
		}
		song->channels[n].volume = 64;
	}
	for (; n < 64; n++) {
		song->channels[n].panning = 32;
		song->channels[n].volume = 64;
		song->channels[n].flags = CHAN_MUTE;
	}

	/* orderlist */
	fread(song->orderlist, 1, nord, fp);
	memset(song->orderlist + nord, 255, 256 - nord);

	/* load the parapointers */
	fread(para_smp, 2, nsmp, fp);
	fread(para_pat, 2, npat, fp);
#ifdef WORDS_BIGENDIAN
	swab(para_smp, para_smp, 2 * nsmp);
	swab(para_pat, para_pat, 2 * npat);
#endif

	/* default pannings */
	if (bleh & 2) {
		for (n = 0; n < 32; n++) {
			c = fgetc(fp);
			if (c & 0x20)
				song->channels[n].panning = ((c & 0xf) << 2) + 2;
		}
	}

	/* samples */
	for (n = 0, sample = song->samples + 1; n < nsmp; n++, sample++) {
		uint8_t type;

		fseek(fp, para_smp[n] << 4, SEEK_SET);

		type = fgetc(fp);
		fread(sample->filename, 1, 12, fp);
		sample->filename[12] = 0;

		fread(b, 3, 1, fp);
		fread(&tmplong, 4, 1, fp);
		if (type == 1) {
			para_sdata[n] = b[1] | (b[2] << 8) | (b[0] << 16);
			sample->length = bswapLE32(tmplong);
		}
		fread(&tmplong, 4, 1, fp);
		sample->loop_start = bswapLE32(tmplong);
		fread(&tmplong, 4, 1, fp);
		sample->loop_end = bswapLE32(tmplong);
		sample->volume = fgetc(fp);
		sample->global_volume = 64;
		fgetc(fp);      /* unused byte */
		fgetc(fp);      /* packing info (never used) */
		c = fgetc(fp);  /* flags */
		if (c & 1)
			sample->flags |= SAMP_LOOP;
		if (c & 4)
			sample->flags |= SAMP_16BIT;
		/* another flag which i'm just ignoring: 2 => stereo
		(I don't think any S3Ms use this anyway) */
		fread(&tmplong, 4, 1, fp);
		sample->c5speed = bswapLE32(tmplong);
		fseek(fp, 12, SEEK_CUR);        /* wasted space */
		fread(sample->title, 1, 25, fp);
		sample->title[25] = 0;

		sample->vibrato_speed = 0;
		sample->vibrato_depth = 0;
		sample->vibrato_rate = 0;
		sample->vibrato_table = 0;
	}
	
	/* sample data */
	for (n = 0, sample = song->samples + 1; n < nsmp; n++, sample++) {
		char *ptr;
		uint32_t len = sample->length;
		int bps = 1;    /* bytes per sample (i.e. bits / 8) */
		
		if (!para_sdata[n])
			continue;

		fseek(fp, para_sdata[n] << 4, SEEK_SET);
		if (sample->flags & SAMP_16BIT)
			bps = 2;
		ptr = malloc(bps * len);
		fread(ptr, bps, len, fp);
		sample->data = (int8_t *) ptr;

		if (bleh & 1) {
			/* convert to signed */
			uint32_t pos = len;
			if (bps == 2)
				while (pos-- > 0)
					ptr[2 * pos + 1] ^= 0x80;
			else
				while (pos-- > 0)
					ptr[pos] ^= 0x80;
		}
#ifdef WORDS_BIGENDIAN
		if (bps == 2)
			swab(ptr, ptr, 2 * len);
#endif
	}
	
	/* patterns */
	for (n = 0; n < npat; n++) {
		int row = 0;
		
		/* The +2 is because the first two bytes are the length of the packed
		data, which is superfluous for the way I'm reading the patterns. */
		fseek(fp, (para_pat[n] << 4) + 2, SEEK_SET);
		
		song->patterns[n] = pattern_allocate(64);
		
		while (row < 64) {
			uint8_t mask = fgetc(fp);

			if (!mask) {
				/* done with the row */
				row++;
				continue;
			}
			note = PATTERN_GET_ROW(song->patterns[n]->data, row) + (mask & 31);
			if (mask & 32) {
				/* note/instrument */
				note->note = fgetc(fp);
				note->instrument = fgetc(fp);
				if (note->instrument > 99)
					note->instrument = 0;
				if (note->note < 254)
					note->note = (note->note >> 4) * 12 + (note->note & 0xf) + 12;
			}
			if (mask & 64) {
				/* volume */
				note->volume = fgetc(fp);
				if (note->volume > 64)
					note->volume = VOL_NONE;
			}
			if (mask & 128) {
				note->effect = fgetc(fp);
				if (note->effect > 26)
					note->effect = 0;
				else if (note->effect)
					note->effect += 'A' - 1;
				note->param = fgetc(fp);
				switch (note->effect) {
				case 'C':
					note->param = 10 * (note->param >> 4) + (note->param & 0xf);
					break;
				case 'V':
					note->param <<= 1;
					break;
				case 'X':
					import_pan_effect(note);
					break;
				case 'S':
					/* convert old SAx to S8x */
					if ((note->param >> 4) == 0xa)
						note->param = 0x80 | ((note->param & 0xf) ^ 8);
					break;
				}
			}
			/* ... next note, same row */
		}
	}
	
	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}
	/* done! */
	return LOAD_SUCCESS;
}
