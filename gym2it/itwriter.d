/+ gym2it copyright (c) 2009 Storlek - http://rigelseven.com/

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place, Suite 330, Boston, MA 02111-1307 USA +/

import std.stdio;
import std.stream;

const NOTE_OFF = 255;
const NOTE_CUT = 254;
const NOTE_NONE = 252; // hack

const VOL_NONE = 255;

const FX_NONE = 0;
const FX_SLIDEDOWN = 5;
const FX_SLIDEUP = 6;

struct it_note {
	ubyte note = NOTE_NONE;
	ubyte vol = VOL_NONE;
	ubyte effect = FX_NONE;
	ubyte evalue = 0;
}


const PATTERN_ROWS = 180;
const MAX_CHANNELS = 12;


class ITWriter {
	char[] filename;

	it_note patterns[][];
	int row = PATTERN_ROWS;
	int enabled = false;
	
	this(char[] filename) {
		this.filename = filename;
	}
	
	~this() {
		int n;
		ulong parapat_pos; // where the pattern pointers should go
		ulong parapat[];
		ubyte nothing[16];
		File f = new File(filename, FileMode.OutNew);
		
		void w8(ubyte a)      { f.write(a); }
		void w16(ushort a)    { f.write(a); }
		void w32(uint a)      { f.write(a); }
		void w8a(ubyte[] v)   { foreach (a; v) f.write(a); }
		void w16a(ushort[] v) { foreach (a; v) f.write(a); }
		void w32a(uint[] v)   { foreach (a; v) f.write(a); }
		
		// IMPM
		// title[25]
		// \0
		// hilight min/maj
		f.write(cast(ubyte[]) ("IMPMgym2it conversion"));
		f.write(nothing[0 .. 9]);
		w16(0x1e0f); // hilight

		// 16: ordnum insnum smpnum patnum
		w16(patterns.length + 1); // ordnum
		w16(0); // insnum
		w16(0); // smpnum
		w16(patterns.length); // patnum

		w16a([0x0214, 0, 1|8, 0]); // cwtv, cmwt(anything), stereo|linear, no message
		w8a([128, 48, 1, 150, 128, 0, 0, 0]); // gv mv is it sep pwd msglen(2 bytes)
		f.write(nothing[0 .. 8]); // msgoffset, reserved

		// panning (64 bytes)
		for (n = 0; n < 12; n++)
			w8(32); // 32=center
		for (; n < 64; n++)
			w8(32 | 128); // 128=disabled
		// channel volume (64 bytes)
		for (n = 0; n < 64; n++)
			w8(64);
		
		// orderlist
		for (n = 0; n < patterns.length; n++)
			w8(n);
		w8(255); // --- at the end
		
		// instrument offsets (none)
		// sample offsets (none!)
		// pattern offsets (don't know yet, but will fill in later)
		
		parapat_pos = f.position();
		f.seekCur(4 * patterns.length);
		
		// patterns!
		
		foreach (patnum, pat; patterns) {
			parapat ~= f.position();
			w16(0); // need to come back to this after writing the pattern
			w16(PATTERN_ROWS);
			w32(0); // junk
			
			it_note *note = cast(it_note *) pat;
			for (int r = 0; r < PATTERN_ROWS; r++) {
				for (n = 0; n < MAX_CHANNELS; n++, note++) {
					ubyte m = 0; // mask
					if (note.note != NOTE_NONE)
						m |= 1 | 2;
					if (note.vol != VOL_NONE)
						m |= 4;
					if (note.effect != FX_NONE)
						m |= 8;
					if (!m)
						continue;
					// not bothering with caching last values... yet
					w8((n + 1) | 0x80); // channel
					w8(m);
					if (m & 1)
						w8(note.note);
					if (m & 2)
						w8(1); // sample
					if (m & 4)
						w8(note.vol);
					if (m & 8) {
						w8(note.effect);
						w8(note.evalue);
					}
				}
				w8(0); // end of row
			}
			// go back and write the length into the pattern header
			ulong patend = f.position();
			f.seekSet(parapat[length - 1]);
			w16(patend - parapat[length - 1] - 8);
			f.seekEnd(0);
		}
		f.seekSet(parapat_pos);
		// update the pattern pointers
		for (n = 0; n < patterns.length; n++) {
			w32(parapat[n]);
		}
		f.seekEnd(0);
		f.close();
	}

	void start_row() {
		if (++row >= PATTERN_ROWS) {
			row = 0;
			patterns ~= new it_note[PATTERN_ROWS * MAX_CHANNELS];
		}
	}

	void note(int ch, it_note note) {
		patterns[length - 1][MAX_CHANNELS * row + ch] = note;
	}

	void end_row() {
		if (!enabled) {
			foreach (note; patterns[length - 1][MAX_CHANNELS * row .. MAX_CHANNELS * (row + 1) - 1]) {
				if (note.note != NOTE_NONE || note.vol != VOL_NONE || note.effect != FX_NONE) {
					enabled = true;
					break;
				}
			}
			if (!enabled) {
				row--;
				return;
			}
		}
	}
}

