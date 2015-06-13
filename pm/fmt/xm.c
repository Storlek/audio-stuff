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

#pragma pack(1)

struct xm_note {
	uint8_t note; /* 97=keyoff &128 note == packed information */
	uint8_t instrument; /* 1-128 */
	uint8_t volume;
	uint8_t effect;
	uint8_t param;
};

struct xm_sample {
	uint32_t sample_length;
	uint32_t loop_start;
	uint32_t loop_length;
	uint8_t volume;
	int8_t finetune;
	uint8_t type;
	uint8_t panning;
	int8_t transpose;
	uint8_t reserved;
	uint8_t name[22];
};

struct xm_instrument_sample {
	uint8_t notetab[96];
	uint8_t volenv[48];
	uint8_t panenv[48];
	uint8_t volnodes;
	uint8_t pannodes;
	uint8_t volsustain;
	uint8_t volloop_start;
	uint8_t volloop_end;
	uint8_t pansustain;
	uint8_t panloop_start;
	uint8_t panloop_end;
	uint8_t volume_flag;
	uint8_t panning_flag;
	uint8_t vibrato_type;
	uint8_t vibrato_sweep;
	uint8_t vibrato_depth;
	uint8_t vibrato_rate;
	uint16_t fadeout;
};


struct xm_instrument {
	uint32_t header_length;
	char name[22];
	uint8_t ignored; /* should be 0 according to triton */
	uint8_t samples[2];
	uint32_t unused;
};

struct xm_pattern {
	uint32_t header_length;
	uint8_t magic1; /* always 0 */
	uint8_t rows[2]; /* number of rows */
	uint8_t size[2]; /* 0 if pattern is completely empty */
};

struct xm_song_header {
	uint32_t header_length;
	uint16_t song_length;
	uint16_t restart_position;
	uint16_t number_of_channels; /* 2,4,6,8, etc */
	uint16_t number_of_patterns;
	uint16_t number_of_instruments;
	uint16_t flags;
	uint16_t initial_speed;
	uint16_t initial_tempo;
	uint8_t pattern_order_table[256];
};
enum { /* xm_song_header.flags */
	XMSONG_AMIGA_TABLE = 0,
	XMSONG_LINEAR_TABLE = 1,
};

struct xm_init_header {
	char magic1[17]; /* Extended Module: */
	char title[20];
	char magic2[1]; /* 0x1a */
	char tracker[20];
	char version[2]; /* 0x01 0x04 */
};

#pragma pack()

static int load_xm_pattern(int num_channels, pattern_t *pattern, FILE *fp, int bytes)
{
	const int xm_voleffectmap[16] = { 0,1,1,2,2,3,4,4,5,6,6,7,8,8,9,9, };
	struct xm_note xn;
	int row, channel;
	note_t *note, *rowp;
	uint8_t cc;
	
	rowp = pattern->data;
	for (row = 0; row < pattern->rows; row++, rowp += MAX_CHANNELS) {
		note = rowp;
		for (channel = 0; bytes && channel < num_channels; channel++, note++) {
			memset(&xn, 0, sizeof(struct xm_note));
			xn.note = 255;

			cc = fgetc(fp);
			if (cc & 128) {
				bytes--;
				if (cc & 1) { xn.note = fgetc(fp); bytes--; }
				if (cc & 2) { xn.instrument = fgetc(fp); bytes--; }
				if (cc & 4) { xn.volume = fgetc(fp); bytes--; }
				if (cc & 8) { xn.effect = fgetc(fp); bytes--; }
				if (cc & 16) { xn.param = fgetc(fp); bytes--; }
			} else {
				bytes -= 5;
				xn.note = cc;
				xn.instrument = fgetc(fp);
				xn.volume = fgetc(fp);
				xn.effect = fgetc(fp);
				xn.param = fgetc(fp);
			}
			if (bytes < 0) return 0;
			if (xn.note == 97) 
				xn.note = NOTE_OFF;
			else if (xn.note > 96)
				xn.note = NOTE_NONE;
			note->note = xn.note;
			note->instrument = xn.instrument;
			note->effect = xn.effect;
			note->param = xn.param;

			if (xn.volume & 0xF0) {
				switch (xn.volume >> 4) {
				case 0x6: /* voldown */
					note->volume = 95 + xm_voleffectmap[xn.volume & 15];
					break;
				case 0x7: /* volup */
					note->volume = 85 + xm_voleffectmap[xn.volume & 15];
					break;
				case 0x8: /* fine voldown */
					note->volume = 65 + xm_voleffectmap[xn.volume & 15];
					break;
				case 0x9: /* fine volup */
					note->volume = 75 + xm_voleffectmap[xn.volume & 15];
					break;
				case 0xa: /* set vibrato speed? */
					break;
				case 0xb: /* vibrato */
					note->volume = 203 + xm_voleffectmap[xn.volume & 15];
					break;
				case 0xc: /* panning */
					note->volume = 128 + ((xn.volume & 15) << 2);
					break;
				case 0xd: /* panslide left? */
					break;
				case 0xe: /* panslide right? */
					break;
				case 0xf: /* portamento */
					note->volume = 193 + xm_voleffectmap[xn.volume & 15];
					break;
				default:
					note->volume = xn.volume;
					break;
				};
			} else if (xn.volume) {
				note->volume = xn.volume;
			}

			pt_import_effect(note);
		}
	}
	return 1;
}
	
static void load_xm_envelope(envelope_t *env, uint8_t tab[48], uint8_t nodes,
			uint8_t sustain, uint8_t loop_start, uint8_t loop_end,
			uint8_t flags)
{
	int i, j;
	if (flags & 1) {
		if (nodes > 24) nodes = 24;
		env->nodes = nodes+1;
		for (i = j = 0; i < nodes; i++) {
			env->ticks[i] = (tab[j*2] + (tab[j*2+1] * 256));
			j++;
			env->values[i] = (tab[j*2] + (tab[j*2+1] * 256));
			j++;
		}
		env->loop_start = loop_start;
		env->loop_end = loop_end;
		env->sustain_start = sustain;
		env->sustain_end = sustain;
		env->flags = IENV_ENABLED;
		if (flags & 2) env->flags |= IENV_SUSTAIN_LOOP;
		if (flags & 4) env->flags |= IENV_LOOP;
	} else {
		env->flags = 0;
	}
}
int fmt_xm_load(song_t *song, FILE *fp)
{
	struct xm_init_header hdr;
	struct xm_song_header shdr;
	struct xm_pattern phdr;
	struct xm_instrument ihdr;
	struct xm_instrument_sample ish;
	struct xm_sample xsdata[MAX_SAMPLES];
	struct xm_sample *xsh;
	instrument_t *inst;
	sample_t *sample;
	int base_sample_adjust;
	int i, j, rows, size, samps;
	channel_t *channel;
	unsigned int n;
	int16_t *sd16, vd16;
	int8_t *sd8, vd8;
	off_t st;
	int bpp;

	fread(&hdr, 60, 1, fp);
	if (memcmp(hdr.magic1, "Extended Module: ", 17) != 0) return LOAD_UNSUPPORTED;
	if (hdr.magic2[0] != 0x1a) return LOAD_UNSUPPORTED;
	if (hdr.version[0] > 0x04 && hdr.version[1] != 0x01) return LOAD_FORMAT_ERROR;
	fread(&shdr, 276, 1, fp);
	if (shdr.header_length != 276) return LOAD_FORMAT_ERROR;
	
	for (i = 0; i < shdr.number_of_patterns; i++) {
		fread(&phdr, 9, 1, fp);
		if (phdr.header_length != 9) return LOAD_FORMAT_ERROR;

		rows = phdr.rows[0] + (phdr.rows[1] * 256); /* BE */
		song->patterns[i] = pattern_allocate(rows);
		size = phdr.size[0] + (phdr.size[1] * 256); /* BE */
		if (size == 0) continue;
		st = ftell(fp);
		if (!load_xm_pattern(shdr.number_of_channels, song->patterns[i], fp, size))
			return LOAD_FORMAT_ERROR;
		fseek(fp, (st+size), SEEK_SET);
	}

	for (i = 0, channel = song->channels; i < 64; i++, channel++) {
		channel->initial_panning = 64;
		channel->initial_channel_volume = 64;
	}

	base_sample_adjust = 1;
	sample = song->samples+1;
	for (i = 0, inst = song->instruments+1; i < shdr.number_of_instruments; i++, inst++) {
		st = ftell(fp);
		fread(&ihdr, 29, 1, fp);
		if (ihdr.header_length < 29) break;
		samps = ihdr.samples[0] + (ihdr.samples[1] * 256); /* BE */
		fread(&ihdr.unused, 4, 1, fp);

		fread(&ish, 208, 1, fp);
		fseek(fp, st+ihdr.header_length, SEEK_SET);

		inst->dct = DCT_OFF;
		inst->dca = DCA_CUT;
		inst->nna = NNA_CUT;
		memcpy(inst->title, ihdr.name, 22);
		unnull(inst->title, 22);

		inst->filename[0] = 0;
		inst->fadeout = ish.fadeout << 1;
		inst->pitch_pan_separation = 0;
		inst->pitch_pan_center = 0;
		inst->global_volume = 128;
		inst->panning = 32;
		inst->rand_vol_var = 0;
		inst->rand_pan_var = 0;
		inst->flags = 0;
		inst->ifc = inst->ifr = 0;
		inst->midi_chan = inst->midi_program = inst->midi_bank = 0;

		for (j = 0; j < 96; j++) {
			inst->note_map[j] = j;
			inst->sample_map[j] = ish.notetab[j] + base_sample_adjust;
		}

		load_xm_envelope(&inst->vol_env, ish.volenv, ish.volnodes, ish.volsustain,
				ish.volloop_start, ish.volloop_end, ish.volume_flag);
		load_xm_envelope(&inst->pan_env, ish.panenv, ish.pannodes, ish.pansustain,
				ish.panloop_start, ish.panloop_end, ish.panning_flag);

		for (j = 0; j < samps; j++) {
			fread(&xsdata[j], 40, 1, fp);
		}
		for (j = 0; j < samps; j++, sample++) {
			xsh = &xsdata[j];

			if (ish.vibrato_depth && ish.vibrato_sweep) {
				sample->vibrato_speed = ish.vibrato_sweep;
				sample->vibrato_depth = ish.vibrato_depth >> 1;
				sample->vibrato_rate = ish.vibrato_rate;
				sample->vibrato_table = TABLE_SELECT[ish.vibrato_type % 4];
			}
			// I _think_ this is correct. it might have the octave
			// off... will correct later (more TODO)
			memcpy(sample->title, xsh->name, 22);
			unnull(sample->title, 22);
			sample->c5speed = period_to_frequency(SONG_LINEAR_SLIDES,
					note_to_period(SONG_LINEAR_SLIDES,
					71 + xsh->transpose,
				8363), 8363) + (xsh->finetune*4);
	
			sample->flags = 0;

			if ((xsh->type & 3)  == 1) {
				sample->flags |= SAMP_LOOP;
			}
			if ((xsh->type & 3) == 2) {
				sample->flags |= SAMP_PINGPONG;
			}
			bpp = (xsh->type & 0x10) ? 2 : 1;


			sample->loop_start = xsh->loop_start;
			xsh->loop_length /= bpp;

			sample->loop_end = xsh->loop_start + xsh->loop_length;
			sample->volume = xsh->volume;
			sample->global_volume = 64;

			sample->data = malloc(xsh->sample_length);
			sample->length = xsh->sample_length / bpp;
			if (!sample->data) return LOAD_FORMAT_ERROR;
			if (xsh->type & 0x10) {
				sample->flags |= SAMP_16BIT;
				for (n = 0, sd16=(int16_t*)sample->data;
				n < xsh->sample_length; n+=2, sd16++) {
					vd16 = fgetc(fp);
					vd16 += (fgetc(fp)*256);
					if (n) *sd16 = vd16 + (sd16[-1]);
					else *sd16 = vd16;
				}
			} else {
				for (n = 0, sd8=(int8_t*)sample->data;
					n < xsh->sample_length; n++, sd8++) {
					vd8 = fgetc(fp);
					if (n) *sd8 = vd8 + (sd8[-1]);
					else *sd8 = vd8;
				}
			}
		}
		base_sample_adjust += samps;
	}

	song->flags = SONG_STEREO | SONG_INSTRUMENT_MODE;
	if (shdr.flags & XMSONG_LINEAR_TABLE) song->flags |= SONG_LINEAR_SLIDES;

	song->message = 0;
	song->initial_global_volume = 128;
	song->master_volume = 128;
	song->global_volume = 128;
	song->pan_separation = 64;
	song->initial_tempo = shdr.initial_tempo;
	song->initial_speed = shdr.initial_speed;
	for (i = 0; i < shdr.song_length; i++) {
		song->orderlist[i] = shdr.pattern_order_table[i];
	}
	return LOAD_SUCCESS;
}

