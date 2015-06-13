#!/usr/bin/dmd -run

import std.stdio;
import std.string;
import std.stream;
import std.format;

char[] cur_filename = null;

void report(...)
{
	if (cur_filename) {
		writefln("%s", cur_filename);
		cur_filename = null;
	}
	void putc(dchar c) { fputc(c, stdout); }
	putc('\t');
	std.format.doFormat(&putc, _arguments, _argptr);
	putc('\n');
}

int check_it(char[] arg)
{
	ubyte[4] tag;
	ubyte hlmin, hlmaj;
	ushort s;
	uint i;
	ushort ordnum, insnum, smpnum, patnum, cwtv, cmwt;
	uint[] para_ins, para_smp, para_pat;

	cur_filename = arg;
	File f = new File(arg, FileMode.In);

	f.read(tag);
	if (tag != cast(ubyte[]) "IMPM") {
		writefln("%s: not an IT file", arg);
		f.close();
		return 1;
	}

	f.seekSet(0x1E);
	f.read(hlmin);
	f.read(hlmaj);
	f.read(ordnum);
	f.read(insnum);
	f.read(smpnum);
	f.read(patnum);
	f.read(cwtv);
	f.read(cmwt);
	
	f.seekSet(0xc0 + ordnum + 4 * insnum + 4 * smpnum);
	for (int n = 0; n < patnum; n++) {
		f.read(i);
		para_pat ~= i;
	}

	if (f.position() != 0xc0 + ordnum + 4 * (insnum + smpnum + patnum)) {
		writefln("%s: truncated file!", arg);
		f.close();
		return 2;
	}

	//if ((hlmaj || hlmin) != (cwtv >= 0x0213))
	//	report("highlight=%d/%d, cwtv=%04X", hlmaj, hlmin, cwtv);
	//if (cwtv >= 0x0213 && (!hlmaj || !hlmin))
	//	report("highlight=%d/%d, cwtv=%04X", hlmaj, hlmin, cwtv);

	foreach (pat, pos; para_pat) {
		if (!pos)
			continue; // blank pattern
		
		f.seekSet(pos);
		
		ushort packlen, rows;
		f.read(packlen);
		f.read(rows);
		f.read(i); // unused 4 bytes
		
		//if (rows < 32 || rows > 200)
		//	report("pat%d: %d rows", pat, rows);

		int readlen = 0;
		ubyte nextbyte() {
			readlen++;
			return f.getc();
		}
		
		int row = 0;
		ubyte[128] mask;
		while (row < rows && readlen < packlen) {
			ubyte channelvar = nextbyte();
			if (!channelvar) {
				row++;
				continue;
			}
			int chn = channelvar & 127;
			if (channelvar & 128)
				mask[chn] = nextbyte();
			ubyte m = mask[chn];
			if (chn == 0 || chn > 64) {
			//	report("pat%d row%d: weird channel=%d", pat, row, chn);
			}

			if (m & 1) {
				ubyte note = nextbyte();
				if (119 < note && note < 253 && note != 246) {
				//	report("pat%d row%d chn%d: weird note=%d", pat, row, chn, note);
				}
			}

			if (m & 2) {
				ubyte inst = nextbyte();
				if (inst == 0 || inst > 99) {
				//	report("pat%d row%d chn%d: weird inst=%d", pat, row, chn, inst);
				}
			}

			if (m & 4) {
				ubyte vol = nextbyte();
				if ((vol > 124 && vol < 128) || vol > 212) {
				//	report("pat%d row%d chn%d: weird vol=%d", pat, row, chn, vol);
				} else if (false
					//|| (vol <= 64) // volume
					//|| (vol >= 128 && vol <= 192) // panning
					//|| (vol >= 65 && vol <= 74) // Ax
					//|| (vol >= 75 && vol <= 84) // Bx
					//|| (vol >= 85 && vol <= 94) // Cx
					//|| (vol >= 95 && vol <= 104) // Dx
					//|| (vol >= 105 && vol <= 114) // Ex
					//|| (vol >= 115 && vol <= 124) // Fx
					//|| (vol >= 193 && vol <= 202) // Gx
					//|| (vol >= 203 && vol <= 212) // Hx
				) {
				//	report("pat%d row%d chn%d: vol=%d", pat, row, chn, vol);
				}
			}

			if (m & 8) {
				ubyte effect = nextbyte();
				ubyte param = nextbyte();
				if (effect > 26) {
					//report("pat%d row%d chn%d: effect#=%d", pat, row, chn, effect);
				} else if (effect) {
					effect += 64; // 1 -> 'A'
					
					if (false // to line things up...
						// Zxx before it did anything (denonde.it)
						//|| (effect == 'Z' && cwtv < 0x0214)
						// S9x aside from S91
						//|| (effect == 'S' && (param >> 4) == 9 && param != 0x91)
						// S0x/S1x/S2x (unused)
						//|| (effect == 'S' && param != 0 && (param >> 4) <= 2)
						// S3x/S4x/S5x (set waveform) with weird values
						//|| (effect == 'S' && (param >> 4) <= 5 && (param & 0xf) > 3)
						// S7x in general.
						//|| (effect == 'S' && (param >> 4) == 7)
						// S7D, S7E, S7F (unused)
						//|| (effect == 'S' && param > 0x7C && param <= 0x7F)
						// heavy global volume
						//|| (effect == 'V' && param > 0x80)
						// heavy channel volume
						//|| (effect == 'M' && param > 0x40)
						// meaningless speed zero
						//|| (effect == 'A' && param == 0)
					) {
						report("pat%d row%d chn%d: %s%02X",
							pat, row, chn, cast(char) effect, param);
					}
				}
			}
		}
		if (packlen != readlen || row != rows)
			report("pat%d: expected %db/%dr got %db/%dr", pat, packlen, rows, readlen, row);
	}

	f.close();
	return 0;
}

int main(char[][] args)
{
	int ret = 0;
	foreach (arg; args[1..length]) {
		int r = check_it(arg);
		if (r > ret)
			ret = r;
	}
	return ret;
}

