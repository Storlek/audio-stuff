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

/* MTM is actually a pretty neat format, one of the first to have a 16-bit sample flag
(which was never supported by mmedit, and to my knowledge no other trackers can save as
MTM... hmm... should write an MTM saver for schism :) */

static void mtm_unpack_track(const uint8_t *b, note_t *note, int rows)
{
        int n;

        for (n = 0; n < rows; n++, note++, b += 3) {
                note->note = ((b[0] & 0xfc) ? ((b[0] >> 2) + 36) : NOTE_NONE);
                note->instrument = ((b[0] & 0x3) << 4) | (b[1] >> 4);
                note->volume = VOL_NONE;
                note->effect = b[1] & 0xf;
                note->param = b[2];
		/* From mikmod: volume slide up always overrides slide down */
		if (note->effect == 0xa && (note->param & 0xf0))
			note->param &= 0xf0;
                pt_import_effect(note);
        }
}

int fmt_mtm_load(song_t *song, FILE *fp)
{
        uint8_t b[192];
        uint16_t ntrk, nchan, nord, npat, nsmp;
        uint16_t comment_len;
        int n, pat, chan, smp, rows;
        note_t *note;
        uint16_t tmp;
        uint32_t tmplong;
        note_t **trackdata, *tracknote;
	sample_t *sample;

        fread(b, 3, 1, fp);
        if (memcmp(b, "MTM", 3))
                return LOAD_UNSUPPORTED;

        fgetc(fp);      /* version (don't care) */

        fread(song->title, 1, 20, fp);
        song->title[20] = 0;
        fread(&ntrk, 2, 1, fp);
        ntrk = bswapLE16(ntrk);
        npat = fgetc(fp);
        nord = fgetc(fp) + 1;

        fread(&comment_len, 2, 1, fp);
        comment_len = bswapLE16(comment_len);
        nsmp = fgetc(fp);
        fgetc(fp); /* attribute byte (unused) */
        rows = fgetc(fp); /* beats per track (translation: number of rows in every pattern) */
	if (rows != 64)
		TODO("test this file with other players (beats per track != 64)");
        nchan = fgetc(fp);
        for (n = 0; n < 32; n++)
                song->channels[n].initial_panning = SHORT_PANNING[fgetc(fp) & 0xf];
        for (n = nchan; n < MAX_CHANNELS; n++)
                song->channels[n].flags = CHAN_MUTE;
	dump_channels(song);

        for (n = 1, sample = song->samples + 1; n <= nsmp; n++, sample++) {
                fread(sample->title, 1, 22, fp);
                sample->title[22] = 0;
                fread(&tmplong, 4, 1, fp);
                sample->length = bswapLE32(tmplong);
                fread(&tmplong, 4, 1, fp);
                sample->loop_start = bswapLE32(tmplong);
                fread(&tmplong, 4, 1, fp);
                sample->loop_end = bswapLE32(tmplong);
                if ((sample->loop_end - sample->loop_start) > 2) {
                        sample->flags |= SAMP_LOOP;
                } else {
                        /* Both Impulse Tracker and Modplug do this */
                        sample->loop_start = 0;
                        sample->loop_end = 0;
                }
                sample->c5speed = MOD_FINETUNE_TABLE[fgetc(fp) & 0xf];
                sample->volume = fgetc(fp);
                sample->global_volume = 64;
                if (fgetc(fp) & 1) {
			TODO("double check 16 bit sample loading");
                        sample->flags |= SAMP_16BIT;
			sample->length >>= 1;
			sample->loop_start >>= 1;
			sample->loop_end >>= 1;
		}
		song->samples[n].vibrato_speed = 0;
		song->samples[n].vibrato_depth = 0;
		song->samples[n].vibrato_rate = 0;
		song->samples[n].vibrato_table = 0;
	}

        /* orderlist */
        fread(song->orderlist, 1, 128, fp);
	memset(song->orderlist + nord, ORDER_LAST, MAX_ORDERS - nord);

        /* tracks */
        trackdata = calloc(ntrk, sizeof(note_t *));
        for (n = 0; n < ntrk; n++) {
                fread(b, 3, rows, fp);
                trackdata[n] = calloc(rows, sizeof(note_t));
                mtm_unpack_track(b, trackdata[n], rows);
        }

        /* patterns */
        for (pat = 0; pat <= npat; pat++) {
                song->patterns[pat] = pattern_allocate(MAX(rows, 32));
                tracknote = trackdata[n];
                for (chan = 0; chan < 32; chan++) {
                        fread(&tmp, 2, 1, fp);
                        tmp = bswapLE16(tmp);
                        if (tmp == 0)
                                continue;
                        note = song->patterns[pat]->data + chan;
                        tracknote = trackdata[tmp - 1];
                        for (n = 0; n < rows; n++, tracknote++, note += MAX_CHANNELS)
                                *note = *tracknote;
                }
		if (rows < 32) {
			/* stick a pattern break on the first channel with an empty effect column */
			note = PATTERN_GET_ROW(song->patterns[pat]->data, rows - 1);
			while (note->effect || note->param)
				note++;
			note->effect = 'C';
		}
        }

        /* free willy */
        for (n = 0; n < ntrk; n++)
                free(trackdata[n]);
        free(trackdata);

        /* just skip the comment */
        fseek(fp, comment_len, SEEK_CUR);

        /* sample data */
        for (smp = 1; smp <= nsmp; smp++) {
                char *ptr;
                int bps = 1;    /* bytes per sample (i.e. bits / 8) */

                if (song->samples[smp].length == 0)
                        continue;
                if (song->samples[smp].flags & SAMP_16BIT)
                        bps = 2;
                ptr = malloc(bps * song->samples[smp].length);
                fread(ptr, bps, song->samples[smp].length, fp);
                song->samples[smp].data = (int8_t *) ptr;

                /* convert to signed */
                n = song->samples[smp].length;
                while (n-- > 0)
                        ptr[n] ^= 0x80;
        }

        /* set the rest of the stuff */
        song->flags = (SONG_OLD_EFFECTS | SONG_COMPAT_GXX | SONG_STEREO);

	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}

        /* done! */
        return LOAD_SUCCESS;
}
