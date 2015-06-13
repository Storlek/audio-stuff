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

/* --------------------------------------------------------------------------------------------------------- */
/* some functions to convert notes and effects */

void import_pan_effect(note_t *note)
{
	/* note that this function doesn't even look at note->effect */
	if (note->param < 0x80) {
		/* 0 -> 0x7f */
		note->effect = 'X';
		note->param <<= 1;
	} else if (note->param == 0xa4) {
		/* surround */
		note->effect = 'S';
		note->param = 0x91;
	} else {
		note->effect = 'X';
		note->param = 0xff;
	}
}

/* change a protracker effect to IT style */
void pt_import_effect(note_t *note)
{
	uint8_t effects[16] = ".FEGHLKR.ODB....";
	uint8_t ext_trans[16] = { 0, 0, 0, 0x10, 0x30, 0x20, 0xb0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0 };
	
	switch (note->effect) {
	case 0xa:
		/* convert to D0x or Dx0 -- no fine slides */
		if (note->param & 0xf)
			note->param &= 0xf;
		else
			note->param &= 0xf0;
		/* fall through */
	default:
		note->effect = effects[note->effect & 15];
		break;
	case 0x0:
		note->effect = note->param ? 'J' : 0;
		break;
	case 0x8:
		import_pan_effect(note);
		break;
	case 0xc:
		note->volume = MIN(note->param, 0x40);
		note->effect = note->param = 0;
		break;
	case 0xd:
		note->effect = 'C';
		note->param = (10 * (note->param >> 4) + (note->param & 0xf));
		break;
	case 0xe:
		note->effect = 'S';
		/* convert Exy */
		switch (note->param >> 4) {
		case 0x1:      /* E1x => FFx */
			note->effect = 'F';
			note->param = 0xf0 | (note->param & 0xf);
			break;
		case 0x2:      /* E2x => EFx */
			note->effect = 'E';
			note->param = 0xf0 | (note->param & 0xf);
			break;
		case 0x9:      /* E9x => Q0x */
			note->effect = 'Q';
			note->param = note->param & 0xf;
			break;
		case 0xa:      /* EAx => DxF */
			note->effect = 'D';
			note->param = (note->param << 4) | 0xf;
			break;
		case 0xb:      /* EBx => DFx */
			note->effect = 'D';
			note->param = 0xf0 | (note->param & 0xf);
			break;
		default:
			if (ext_trans[note->param >> 4] != 0)
				note->param = ext_trans[note->param >> 4] | (note->param & 0xf);
			break;
		}
		break;
	case 0xf:
		note->effect = (note->param < 0x20) ? 'A' : 'T';
		break;
	case 0x10: /* XM 'G" */
		note->effect = 'V';
		note->param <<= 1;
		break;
	case 0x11: /* XM 'H' */
		note->effect = 'W';
		break;
	case 0x14: /* XM 'K' (keyoff) */
		/* we use past note off because it doesn't effect current */
		note->effect = 'S';
		note->param = 0x71;
		break;
	case 0x15: /* XM 'L' set envelope position */
		/* no idea... */
		break;
	case 0x19: /* XM 'P' panning slide */
		note->effect = 'P';
		break;
	case 0x1b: /* XM 'R' retrig */
		note->effect = 'R';
		break;
	case 0x1d: /* XM 'T' tremor */
		note->effect = 'I';
		break;
	case 0x21: /* XM 'X' extrafine portamento */
		if ((note->param & 0xf0) == 0x10) {
			note->effect = 'F';
			note->param = 0xe0 | (note->param & 15);
		} else if ((note->param & 0xf0) == 0x20) {
			note->effect = 'E';
			note->param = 0xe0 | (note->param & 15);
		}
		break;
	}
}

static int _mod_period_to_note(int period)
{
	int n;

	if (period)
		for (n = 0; n <= NOTE_LAST; n++)
			if (period >= (32 * PERIOD_TABLE[n % 12] >> (n / 12 + 2)))
				return n;
	return NOTE_NONE;
}

void mod_import_note(const uint8_t p[4], note_t *note)
{
	note->note = _mod_period_to_note(((p[0] & 0xf) << 8) + p[1]);
	note->instrument = (p[0] & 0xf0) + (p[2] >> 4);
	note->volume = VOL_NONE;
	note->effect = p[2] & 0xf;
	note->param = p[3];
}

/* --------------------------------------------------------------------------------------------------------- */

int song_load(song_t *song, const char *filename)
{
	typedef int (*load_func) (song_t *song, FILE *fp);
	load_func funcs[] = {
		// order: 669 mod (s3m,far) (xm,it,mt2,mtm,ntk,sid,mdl) ult liq ams f2r dtm ogg stm mp3
		fmt_xm_load,
		fmt_669_load,
		fmt_mod_load,
		fmt_s3m_load,
		fmt_it_load, fmt_mtm_load,
		fmt_imf_load, fmt_sfx_load,
		NULL
	};
	load_func *load;
	FILE *fp = fopen(filename, "rb");
	
	if (!fp) {
		perror(filename);
		return 0;
	}
	song_reset(song);
	for (load = funcs; *load; load++) {
		switch ((*load) (song, fp)) {
		case LOAD_UNSUPPORTED:
			break;
		case LOAD_SUCCESS:
			fclose(fp);
			return 1;
		case LOAD_FILE_ERROR:
			perror(filename);
			fclose(fp);
			return 0;
		case LOAD_FORMAT_ERROR:
			fprintf(stderr, "%s: format error\n", filename);
			fclose(fp);
			return 0;
		}
		rewind(fp);
	}
	fprintf(stderr, "%s: unsupported file type\n", filename);
	fclose(fp);
	return 0;
}
