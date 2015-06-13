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

/* bleah. I could have done this WAY better, but I didn't. */

/* this should be exactly 0xc0 bytes */
struct it_header {
	char impm[4], title[26];
	uint8_t highlight_minor, highlight_major;
	uint16_t ordnum, insnum, smpnum, patnum;
	uint16_t cwtv, cmwt, flags, special;
	uint8_t gv, mv, is, it, sep, pwd;
	uint16_t msg_length;
	uint32_t msg_offset, reserved;
	uint8_t chan_pan[64], chan_vol[64];
};
/* then...
 *      uint8_t orderlist[ordnum];
 *      uint32_t para_ins[insnum], para_smp[smpnum], para_pat[patnum];
 *      uint16_t mystery; */

struct it_sample {
	char imps[4], filename[13];
	uint8_t gvl, flag, vol;
	char name[26];
	uint8_t cvt, dfp;
	uint32_t length, loop_start, loop_end, c5speed;
	uint32_t susloop_start, susloop_end, sample_pointer;
	uint8_t vis, vid, vir, vit;
};

struct it_envelope  {
	uint8_t flags, num_nodes, loop_start, loop_end;
	uint8_t susloop_start, susloop_end;
	uint8_t packed_nodes[75];
	uint8_t padding;
};
struct it_notetrans {
	uint8_t note, sample;
};
struct it_instrument {
	char impi[4], filename[13];
	uint8_t nna, dct, dca;
	uint16_t fadeout;
	uint8_t pps, ppc, gv, dp, rv, rp;
	uint16_t trkvers;
	uint8_t num_samples, padding;
	char name[26];
	uint8_t ifc, ifr, mch, mpr;
	uint16_t midibank;
	struct it_notetrans notetrans[120];
	struct it_envelope vol_env, pan_env, pitch_env;
};

/* pattern mask variable bits */
enum {
	ITNOTE_NOTE = 1,
	ITNOTE_SAMPLE = 2,
	ITNOTE_VOLUME = 4,
	ITNOTE_EFFECT = 8,
	ITNOTE_SAME_NOTE = 16,
	ITNOTE_SAME_SAMPLE = 32,
	ITNOTE_SAME_VOLUME = 64,
	ITNOTE_SAME_EFFECT = 128,
};

static void load_it_pattern(pattern_t *pattern, FILE *fp)
{
	note_t last_note[64];
	note_t *note = pattern->data;
	int chan, row = 0;
	uint8_t last_mask[64] = { 0 };
	uint8_t chanvar, maskvar, c;

	while (row < pattern->rows) {
		chanvar = fgetc(fp);
		if (chanvar == 0) {
			row++;
			note += 64;
			continue;
		}
		chan = (chanvar - 1) & 63;
		if (chanvar & 128) {
			maskvar = fgetc(fp);
			last_mask[chan] = maskvar;
		} else {
			maskvar = last_mask[chan];
		}
		if (maskvar & ITNOTE_NOTE) {
			c = fgetc(fp);
			if (c == 255)
				c = NOTE_OFF;
			else if (c == 254)
				c = NOTE_CUT;
			else if (c > 119)
				c = NOTE_FADE;
			note[chan].note = c;
			last_note[chan].note = note[chan].note;
		}
		if (maskvar & ITNOTE_SAMPLE) {
			note[chan].instrument = fgetc(fp);
			last_note[chan].instrument = note[chan].instrument;
		}
		if (maskvar & ITNOTE_VOLUME) {
			note[chan].volume = fgetc(fp);
			last_note[chan].volume = note[chan].volume;
		}
		if (maskvar & ITNOTE_EFFECT) {
			c = fgetc(fp);
			if (c)
				c += 'A' - 1;
			note[chan].effect = c;
			note[chan].param = fgetc(fp);
			last_note[chan].effect = note[chan].effect;
			last_note[chan].param = note[chan].param;
		}
		if (maskvar & ITNOTE_SAME_NOTE)
			note[chan].note = last_note[chan].note;
		if (maskvar & ITNOTE_SAME_SAMPLE)
			note[chan].instrument = last_note[chan].instrument;
		if (maskvar & ITNOTE_SAME_VOLUME)
			note[chan].volume = last_note[chan].volume;
		if (maskvar & ITNOTE_SAME_EFFECT) {
			note[chan].effect = last_note[chan].effect;
			note[chan].param = last_note[chan].param;
		}
	}
}
static void __read_env(envelope_t *env, struct it_envelope *ee)
{
	int i;

	env->flags = 0;
	env->nodes = ee->num_nodes;
	env->loop_start = ee->loop_start;
	env->loop_end = ee->loop_end;
	env->sustain_start = ee->susloop_start;
	env->sustain_end = ee->susloop_end;
	if (ee->flags & 1) {
		env->flags |= IENV_ENABLED;
	}
	if (ee->flags & 2) {
		env->flags |= IENV_LOOP;
	}
	if (ee->flags & 4) {
		env->flags |= IENV_SUSTAIN_LOOP;
	}
	if (ee->flags & 8) {
		env->flags |= IENV_CARRY;
	}
	
	for (i = 0; i < 25; i++) {
		env->values[i] = ee->packed_nodes[i * 3];
		env->ticks[i] = ee->packed_nodes[(i * 3) + 1]
				| ((ee->packed_nodes[(i * 3) + 2]) << 8);
	}
}


int fmt_it_load(song_t *song, FILE *fp)
{
	struct it_header hdr;
	uint32_t para_smp[99], para_ins[99], para_pat[256];
	char *tmp;
	int n, pos;
	channel_t *channel;
	sample_t *sample;
	instrument_t *instrument;

	/* TODO: endianness */
	fread(&hdr, 0xc0, 1, fp);

	if (memcmp(hdr.impm, "IMPM", 4) != 0)
		return LOAD_UNSUPPORTED;

	if (hdr.cmwt > 0x216) {
		printf("Unsupported file version\n");
		return LOAD_FORMAT_ERROR;
	}
	
	fread(song->orderlist, 1, hdr.ordnum, fp);
	
	fread(para_ins, 4, hdr.insnum, fp);
	fread(para_smp, 4, hdr.smpnum, fp);
	fread(para_pat, 4, hdr.patnum, fp);
	
	song->flags = 0;
	if (hdr.flags & 1)
		song->flags |= SONG_STEREO;
	/* (hdr.flags & 2) no longer used (was vol0 optimizations) */
	if (hdr.flags & 4)
		song->flags |= SONG_INSTRUMENT_MODE;
	if (hdr.flags & 8)
		song->flags |= SONG_LINEAR_SLIDES;
	if (hdr.flags & 16)
		song->flags |= SONG_OLD_EFFECTS;
	if (hdr.flags & 32)
		song->flags |= SONG_COMPAT_GXX;
	
	if (hdr.special & 1) {
		fseek(fp, hdr.msg_offset, SEEK_SET);
		tmp = (char *)malloc(hdr.msg_length+1);
		if (tmp) {
			*tmp = 0;
			tmp[hdr.msg_length]=0;
			if (fread(tmp, hdr.msg_length, 1, fp) == 1)
				song->message = tmp;
		}
	}
	
	memcpy(song->title, hdr.title, 25);
	//unnull(song->title, 25);

	song->initial_speed = hdr.is;
	song->initial_tempo = hdr.it;
	song->pan_separation = hdr.sep;
	song->initial_global_volume = hdr.gv;
	song->master_volume = hdr.mv;
	if (hdr.cwtv >= 0x0213) {
		song->highlight_minor = hdr.highlight_minor;
		song->highlight_major = hdr.highlight_major;
	}
	if (hdr.cwtv < 0x0202)
		TODO("convert unsigned samples");

	for (n = 0, channel = song->channels; n < 64; n++, channel++) {
		if (hdr.chan_pan[n] & 128)
			channel->flags = CHAN_MUTE;
		if ((hdr.chan_pan[n] & 127) == 100) {
			channel->initial_panning = PAN_SURROUND;
		} else {
			channel->initial_panning = hdr.chan_pan[n] & 127;
		}
		channel->initial_channel_volume = hdr.chan_vol[n];
	}
	
	for (n = 0, instrument = song->instruments + 1; n < hdr.insnum; n++, instrument++) {
		struct it_instrument ihdr;
		
		fseek(fp, para_ins[n], SEEK_SET);
		fread(&ihdr, sizeof(ihdr), 1, fp);
		
		memcpy(instrument->title, ihdr.name, 25);
		unnull(instrument->title, 25);

		memcpy(instrument->filename, ihdr.filename, 12);
		//unnull(instrument->filename, 12);
		
		instrument->nna = ihdr.nna;
		instrument->dct = ihdr.dct;
		instrument->dca = ihdr.dca;
		instrument->fadeout = ihdr.fadeout;
		instrument->pitch_pan_separation = ihdr.pps;
		instrument->pitch_pan_center = ihdr.ppc;
		instrument->global_volume = ihdr.gv;
		instrument->panning = ihdr.dp & 127;
		if ((ihdr.dp & 128) == 0)
			instrument->flags |= INST_USE_PANNING;
		instrument->flags |= INST_INUSE;
		instrument->rand_vol_var = ihdr.rv;
		instrument->rand_pan_var = ihdr.rp;
		for (pos = 0; pos < 120; pos++) {
			instrument->note_map[pos] = ihdr.notetrans[pos].note;
			instrument->sample_map[pos] = ihdr.notetrans[pos].sample;
		}
		/* Ugh! This alone should be enough of a reason to use the same envelope structure
		in memory as in the IT file... the trouble is, 16-bit values are slow :/ */
		__read_env(&instrument->vol_env, &ihdr.vol_env);
		__read_env(&instrument->pan_env, &ihdr.pan_env);
		__read_env(&instrument->pitch_env, &ihdr.pitch_env);
		if (ihdr.pitch_env.flags & 128) instrument->flags |= INST_FILTER;
	}
	
	for (n = 0, sample = song->samples + 1; n < hdr.smpnum; n++, sample++) {
		struct it_sample shdr;
		uint8_t bps = 1;
		
		fseek(fp, para_smp[n], SEEK_SET);
		fread(&shdr, sizeof(shdr), 1, fp);

		memcpy(sample->title, shdr.name, 25);
		unnull(sample->title, 25);

		memcpy(sample->filename, shdr.filename, 12);
		//unnull(sample->filename, 12);

		sample->volume = shdr.vol;
		sample->length = shdr.length;
		sample->c5speed = shdr.c5speed;
		sample->loop_start = shdr.loop_start;
		sample->loop_end = shdr.loop_end;
		sample->global_volume = shdr.gvl;

		sample->vibrato_speed = shdr.vis;
		sample->vibrato_depth = shdr.vid;
		sample->vibrato_rate = shdr.vir;
		sample->vibrato_table = TABLE_SELECT[shdr.vit % 4];

		sample->flags = 0;
		if (shdr.flag & 2) {
			bps = 2;
			sample->flags |= SAMP_16BIT;
		}
		if (shdr.flag & 4)
			TODO("stereo samples");
		if (shdr.flag & 16)
			sample->flags |= SAMP_LOOP;
		if (shdr.flag & 32)
			TODO("sustain loop");
		if (shdr.flag & 64)
			sample->flags |= SAMP_PINGPONG;
		
		if (shdr.flag & 1) {
			uint8_t *ptr;
			uint32_t len = sample->length;
			
			/* read the data */
			fseek(fp, shdr.sample_pointer, SEEK_SET);
			ptr = calloc(len, bps);
			
			if (shdr.flag & 8) {
				int it215 = (shdr.cvt & 4) ? 1 : 0;
				if (shdr.flag & 2)
					itsex_decompress16(fp, ptr, len, it215);
				else
					itsex_decompress8(fp, ptr, len, it215);
			} else {
				fread(ptr, bps, len, fp);
			}
			
			sample->data = (int8_t *) ptr;
		}
		/* The signed/unsigned flag in cvt is unreliable -- I've found several IT files that seem
		to have it set randomly for some samples. Just check the cwt/v, and if it's < 0x202, then
		the samples are probably unsigned. */
		//if ((shdr.cvt & 1) == 0)
		//	TODO("convert unsigned data for sample %d", n);
		if (shdr.dfp & 128)
			TODO("default panning for sample %d", n);
	}
	for (n = 0; n < hdr.patnum; n++) {
		uint16_t tmp, len;

		if (para_pat[n] == 0)
			continue;

		fseek(fp, para_pat[n], SEEK_SET);
		fread(&len, 2, 1, fp);
		len = bswapLE16(len);
		fread(&tmp, 2, 1, fp);
		tmp = bswapLE16(tmp);
		fseek(fp, 4, SEEK_CUR);
		song->patterns[n] = pattern_allocate(tmp);
		load_it_pattern(song->patterns[n], fp);
		tmp = ftell(fp) - para_pat[n] - 8;
		if (len != tmp)
			printf("Warning: pattern %d: size mismatch (expected %d bytes, got %d bytes)\n",
			       n, len, tmp);
	}

	if (ferror(fp)) {
		return LOAD_FILE_ERROR;
	}

	return LOAD_SUCCESS;

}
