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

/* <opinion humble="false">This is better than IT's and MPT's 669 loaders</opinion> */

int fmt_669_load(song_t *song, FILE *fp)
{
        uint8_t b[16];
        uint16_t npat, nsmp;
        int n, pat, chan, smp, row;
        note_t *note;
        uint16_t tmp;
        uint32_t tmplong;
        uint8_t patspeed[128], breakpos[128];
	
        fread(&tmp, 2, 1, fp);
        if (tmp != bswapLE16(0x6669) && tmp != bswapLE16(0x4e4a))
                return LOAD_UNSUPPORTED;
	
        /* The message is 108 bytes, split onto 3 lines of 36 bytes each.
	I'm just reading the first 25 bytes as the title and throwing out the rest... */
        fread(song->title, 1, 25, fp);
        song->title[25] = 0;
        fseek(fp, 83, SEEK_CUR);
	
	/* HACK: 669 authors seem to like using weird characters - unfortunately it screws up the console */
	for (n = 0; n < 25; n++)
		if ((song->title[n] > 0 && song->title[n] < 32) || song->title[n] > 126)
			song->title[n] = ' ';
	
        nsmp = fgetc(fp);
        if (nsmp > 64)
                return LOAD_UNSUPPORTED;
        npat = fgetc(fp);
        if (npat > 128)
                return LOAD_UNSUPPORTED;
        if (fgetc(fp) > 127)    /* loop order */
                return LOAD_UNSUPPORTED;

        /* orderlist */
        fread(song->orderlist, 1, 128, fp);

        /* stupid crap */
        fread(patspeed, 1, 128, fp);
        fread(breakpos, 1, 128, fp);

        /* samples */
        for (smp = 1; smp <= nsmp; smp++) {
                fread(b, 13, 1, fp);
		b[13] = 0; /* the spec says it's supposed to be ASCIIZ, but some 669's use all 13 chars */
                strcpy(song->samples[smp].title, (char *) b);
                b[12] = 0; /* ... filename field only has room for 12 chars though */
                strcpy(song->samples[smp].filename, (char *) b);
		
                fread(&tmplong, 4, 1, fp);
                song->samples[smp].length = bswapLE32(tmplong);
                fread(&tmplong, 4, 1, fp);
                song->samples[smp].loop_start = bswapLE32(tmplong);
                fread(&tmplong, 4, 1, fp);
                tmplong = bswapLE32(tmplong);
		/* FIXME: this cast sucks, and besides, sample length should be unsigned anyway */
                if (tmplong > (unsigned long) song->samples[smp].length)
                        tmplong = 0;
                else
                        song->samples[smp].flags |= SAMP_LOOP;
                song->samples[smp].loop_end = tmplong;

                song->samples[smp].c5speed = 8363;
                song->samples[smp].volume = 60;  /* ickypoo */
                song->samples[smp].global_volume = 64;  /* ickypoo */
		song->samples[smp].vibrato_speed = 0;
		song->samples[smp].vibrato_depth = 0;
		song->samples[smp].vibrato_rate = 0;
		song->samples[smp].vibrato_table = 0;
        }

        /* patterns */
        for (pat = 0; pat < npat; pat++) {
                uint8_t effect[8] = {
                        255, 255, 255, 255, 255, 255, 255, 255
                };
                uint8_t rows = breakpos[pat] + 1;
		
                song->patterns[pat] = pattern_allocate(CLAMP(rows, 32, 64));
		
                note = song->patterns[pat]->data;
                for (row = 0; row < rows; row++, note += 56) {
                        for (chan = 0; chan < 8; chan++, note++) {
                                fread(b, 3, 1, fp);

                                switch (b[0]) {
                                case 0xfe:     /* no note, only volume */
                                        note->volume = (b[1] & 0xf) << 2;
                                        break;
                                case 0xff:     /* no note or volume */
                                        break;
                                default:
                                        note->note = (b[0] >> 2) + 36;
                                        note->instrument = ((b[0] & 3) << 4 | (b[1] >> 4)) + 1;
                                        note->volume = (b[1] & 0xf) << 2;
                                        break;
                                }
                                /* (sloppily) import the stupid effect */
                                if (b[2] != 0xff)
                                        effect[chan] = b[2];
                                if (effect[chan] == 0xff)
                                        continue;
                                note->param = effect[chan] & 0xf;
                                switch (effect[chan] >> 4) {
                                default:
                                        /* oops. never mind. */
					//printf("ignoring effect %X\n", effect[chan]);
                                        note->param = 0;
                                        break;
                                case 0: /* A - portamento up */
                                        note->effect = 'F';
                                        break;
                                case 1: /* B - portamento down */
                                        note->effect = 'E';
                                        break;
                                case 2: /* C - port to note */
                                        note->effect = 'G';
                                        break;
                                case 3: /* D - frequency adjust (??) */
                                        note->effect = 'E';
                                        if (note->param)
                                                note->param |= 0xf0;
                                        else
                                                note->param = 0xf1;
                                        effect[chan] = 0xff;
                                        break;
                                case 4: /* E - frequency vibrato */
                                        note->effect = 'H';
                                        note->param |= 0x80;
                                        break;
                                case 5: /* F - set tempo */
					/* TODO: param 0 is a "super fast tempo" in extended mode (?) */
                                        if (note->param)
                                                note->effect = 'A';
                                        effect[chan] = 0xff;
                                        break;
				case 6: /* G - subcommands (extended) */
					switch (note->param) {
					case 0: /* balance fine slide left */
						TODO("test pan slide effect (P%dR%dC%d)", pat, row, chan);
						note->effect = 'P';
						note->param = 0x8F;
						break;
					case 1: /* balance fine slide right */
						TODO("test pan slide effect (P%dR%dC%d)", pat, row, chan);
						note->effect = 'P';
						note->param = 0xF8;
						break;
					default:
						/* oops, nothing again */
						note->param = 0;
					}
					break;
				case 7: /* H - slot retrig */
					TODO("test slot retrig (P%dR%dC%d)", pat, row, chan);
					note->effect = 'Q';
					break;
                                }
                        }
                }
                if (rows < 64) {
                        /* skip the rest of the rows beyond the break position */
                        fseek(fp, 3 * 8 * (64 - rows), SEEK_CUR);
                }

                /* handle the stupid pattern speed */
                note = song->patterns[pat]->data;
                for (chan = 0; chan < 9; chan++, note++) {
                        if (note->effect == 'A') {
                                break;
                        } else if (!note->effect) {
                                note->effect = 'A';
                                note->param = patspeed[pat];
                                break;
                        }
                }
                /* handle the break position */
                if (rows < 32) {
			printf("adding pattern break for pattern %d\n", pat);
                        note = PATTERN_GET_ROW(song->patterns[pat]->data, rows - 1);
                        for (chan = 0; chan < 9; chan++, note++) {
                                if (!note->effect) {
                                        note->effect = 'C';
                                        note->param = 0;
                                        break;
                                }
                        }
                }
        }

        /* sample data */
        for (smp = 1; smp <= nsmp; smp++) {
                uint8_t *ptr;
		
                if (song->samples[smp].length == 0)
                        continue;
                ptr = malloc(song->samples[smp].length);
                fread(ptr, 1, song->samples[smp].length, fp);
                song->samples[smp].data = (int8_t *) ptr;
                /* convert to signed */
                n = song->samples[smp].length;
                while (n-- > 0)
                        ptr[n] ^= 0x80;
        }
	
        /* set the rest of the stuff */
        song->initial_speed = 4;
        song->initial_tempo = 78;
        song->flags = (SONG_OLD_EFFECTS | SONG_STEREO | SONG_LINEAR_SLIDES);
	
        song->pan_separation = 64;
        for (n = 0; n < 8; n++)
                song->channels[n].initial_panning = (n & 1) ? 64 : 0;
        for (n = 8; n < 64; n++)
                song->channels[n].flags = CHAN_MUTE;
	
	TODO("linear slides");
	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}
	
        /* done! */
        return LOAD_SUCCESS;
}
