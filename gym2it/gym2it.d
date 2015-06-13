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

import std.c.math;
import std.stdio;
import std.stream;
import std.cstream;

import itwriter;


const YM_KEYON = 1, YM_KEYOFF = 2;

struct ym_chan {
	bool on, laston;
	ubyte msb, octave;
	uint freq;
	real note, lastnote;
	uint keystate; // 0, YM_KEYON, YM_KEYOFF
}

struct ym_state {
	ym_chan chan[12];
}

ym_state ym;



real freq_to_note(uint freq, ubyte octave)
{
	if (!freq)
		return NOTE_NONE;
	return 12 * log2l(cast(real) (freq * (1 << octave)) / (4396.0 / 8));
}


void set(int ch, ubyte val)
{
	bool newon = cast(bool)(val);
	if (ym.chan[ch].on != newon) {
		ym.chan[ch].on = newon;
		ym.chan[ch].keystate = newon ? YM_KEYON : YM_KEYOFF;
	}
}

void ym_write(ubyte port, ubyte r, ubyte d)
{
	ubyte ch;
	bool setmsb = false;

	switch (r) {
	case 0x28: // key on/off
		ubyte k = (d >> 4);
		ch = 3 * port + (d & 0x7);

		if (ch == 2) {
			set( 2, k & 1);
			set( 6, k & 2); // 3 op2
			set( 7, k & 4); // 3 op3
			set( 8, k & 8); // 3 op4
		} else if (ch == 5) {
			set( 5, k & 1);
			set( 9, k & 2); // 6 op2
			set(10, k & 4); // 6 op3
			set(11, k & 8); // 6 op4
		} else {
			// enable the channel if any bits are set, I guess?
			set(ch, k);
		}

		return;

	// frequency LSB:
	case 0xa0: // [0/3] ch1
	case 0xa1: // [1/4] ch2
	case 0xa2: // [2/5] ch3 (but only op1 in "special mode")
		ch = 3 * port + r - 0xa0;
		break;

	case 0xa8: // [6/9] ch3op2
	case 0xa9: // [7/10] ch3op3
	case 0xaa: // [8/11] ch3op4
		ch = 3 * port + r - 0xa8 + 6;
		break;

	// frequency MSB:
	case 0xa4: // [0/3] ch1
	case 0xa5: // [1/4] ch2
	case 0xa6: // [2/5] ch3 (but only op1 in "special mode")
		ch = 3 * port + r - 0xa4;
		setmsb = true;
		break;

	case 0xac: // [6/9] ch3op2
	case 0xad: // [7/10] ch3op3
	case 0xae: // [8/11] ch3op4
		ch = 3 * port + r - 0xac + 6;
		setmsb = true;
	
	default: // something irrelevant
		break;
	}

	if (setmsb) {
		ym.chan[ch].msb = (d & 0x7);
		ym.chan[ch].octave = (d >> 3) & 0x7;
		ym.chan[ch].freq = ym.chan[ch].msb << 8;
		/+
		if (ym.chan[ch].on) {
			writefln("set msb for channel %d: octave=%d msb=%d", ch,
				ym.chan[ch].octave, ym.chan[ch].msb);
		}
		+/
	} else {
		ym.chan[ch].freq = (ym.chan[ch].msb << 8) | d;
		//ym.chan[ch].freq *= ym.chan[ch].octave;
		/+
		if (ym.chan[ch].on)
			//writefln("set lsb for channel %d: lsb=%d", ch, d);
		+/
	}
}


/+
speed 1, tempo 150 = 60hz
180 rows = 3 seconds per pattern
+/

int main(char[][] args)
{
	ubyte op, r, d;
	
	if (args.length != 3) {
		derr.writefln("usage: ", args[0], " filename.gym output.it");
		return 1;
	}

	File gymin = new File(args[1], FileMode.In);
	ITWriter itout = new ITWriter(args[2]);

	for (;;) {
		try
			gymin.read(op);
		catch (ReadException)
			break;

		switch (op) {
		case 0: // nop (delay 1/60 sec)
			itout.start_row();

			foreach (ch, inout chan; ym.chan) {
				chan.note = freq_to_note(chan.freq, chan.octave);
				int slide = 0;
				it_note note;

				if (chan.keystate == YM_KEYON) {
					uint truefreq = 0;
					note.note = cast(ubyte) chan.note;
					slide = cast(uint) (16 * (chan.note - cast(real) note.note));
					if (chan.freq || slide)
						writefln("freq %d = note %d + FF%X", chan.freq, note.note, slide);
				} else if (chan.keystate == YM_KEYOFF) {
					note.note = NOTE_OFF;
				} else {
					real offset = chan.note - chan.lastnote;

					note.note = NOTE_NONE;
					slide = cast(int) (16 * offset);
					if (slide)
						writefln("offset ", offset, " = slide ", slide);
				}

				note.vol = chan.on == chan.laston ? VOL_NONE : chan.on ? 64 : 0;

				if (slide) {
					if (slide < 0) {
						slide = -slide;
						note.effect = FX_SLIDEDOWN;
					} else {
						note.effect = FX_SLIDEUP;
					}
					if (slide > 15) {
						derr.writefln("slide is too big! (%d)", slide);
						slide = 15;
					}
					note.evalue = 0xF0 | slide;
				}

				itout.note(ch, note);

				chan.laston = chan.on;
				chan.keystate = 0;
				chan.lastnote = chan.note;
			}

			itout.end_row();
			break;

		case 1: // on YM port 0 register R, write value D
		case 2: // on YM port 1 register R, write value D
			gymin.read(r);
			gymin.read(d);
			ym_write(op - 1, r, d);
			break;

		case 3: // on PSG, write D
			gymin.read(d);
			break;

		default:
			derr.writefln("error: unexpected opcode %02x", op);
			return 1;
		}
	}

	return 0;
}

