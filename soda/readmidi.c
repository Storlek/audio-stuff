/*
 * copyright (c) 2010 Storlek - http://rigelseven.com/
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

#define _GNU_SOURCE /* for asprintf */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <byteswap.h>

#define MIN(a,b) (((a)<(b))?(a):(b))

/* slurp */
static inline int fpeek(FILE *fp) {
    int c = fgetc(fp);
    if (c != EOF)
        c = ungetc(c, fp);
    return c;
}
/* /slurp */

#define bswapBE16(x) bswap_16(x)
#define bswapBE32(x) bswap_32(x)



#pragma pack(push, 1)
struct mthd {
        //char tag[4]; // MThd <read separately>
        uint32_t header_length;
        uint16_t format; // 0 = single-track, 1 = multi-track, 2 = multi-song
        uint16_t num_tracks; // number of track chunks
        uint16_t division; // delta timing value: positive = units/beat; negative = smpte compatible units (?)
};
struct mtrk {
        char tag[4]; // MTrk
        uint32_t length; // number of bytes of track data following
};
#pragma pack(pop)


// for debug/display
static const char notes[] = "C-C#D-D#E-F-F#G-G#A-A#B-";
#define NOTE(x) notes[((x)%12)*2], notes[((x)%12)*2+1], ((x)/12)


static unsigned int read_varint(FILE *fp)
{
        int b;
        unsigned int v = 0;

        // This will fail tremendously if a value overflows. I don't care.
        do {
                b = fgetc(fp);
                if (b == EOF) {
                        printf("truncated variable-length number\n");
                        return 0; // I suppose!
                }
                v <<= 7;
                v |= b & 0x7f;
        } while (b & 0x80);
        return v;
}

double note_freq(int note_num)
{
        // middle C is note #60, and the A below that (220 hz) is #57
        return pow(2, (note_num - 57) / 12.0) * 220;
}




struct event {
        unsigned int delta;
        char *data;
        struct event *next;
};

struct event *alloc_event(unsigned int delta, char *data, struct event *next)
{
        struct event *ev = malloc(sizeof(struct event));
        if (!ev) {
                perror("malloc");
                free(data);
                return NULL;
        }
        ev->delta = delta;
        ev->data = data;
        ev->next = next;
        return ev;
}


#define CHANNELS_PER_TRACK 16

int main(int argc, char **argv)
{
        struct mthd mthd;
        struct mtrk mtrk;
        FILE *fp;
        unsigned char buf[32];
        char *data;
        long nextpos;
        int c, trknum;
        struct event *event_queue, *cur, *prev, *new;

        if (argc != 2) {
                fprintf(stderr, "usage: %s filename.mid > output.s\n", argv[0]);
                return 2;
        }
        fp = fopen(argv[1], "rb");
        if (!fp) {
                perror(argv[1]);
                return 1;
        }

        fread(buf, 1, 4, fp);
        if (memcmp(buf, "RIFF", 4) == 0) {
                // Stupid MS crap.
                fseek(fp, 16, SEEK_CUR);
                fread(buf, 1, 4, fp);
        }
        if (memcmp(buf, "MThd", 4) != 0) {
                fprintf(stderr, "invalid file header (not a .mid file?)\n");
                fclose(fp);
                return 1;
        }
        if (fread(&mthd, sizeof(mthd), 1, fp) != 1) {
                fprintf(stderr, "short read on file header\n");
                fclose(fp);
                return 1;
        }
        mthd.header_length = bswapBE32(mthd.header_length);
        mthd.format = bswapBE16(mthd.format);
        mthd.num_tracks = bswapBE16(mthd.num_tracks);
        mthd.division = bswapBE16(mthd.division);
        fseek(fp, mthd.header_length - 6, SEEK_CUR); // account for potential weirdness

        printf("; MThd:\n; header_length = %d\n; format = %d\n; num_tracks = %d\n; division = %d ppqn\n",
                mthd.header_length, mthd.format, mthd.num_tracks, mthd.division);
        /*
        A06 gives 4 row/beat = 24 tick/beat
        T7D = 125 beat/min × 24 tick/beat => 3000 tick/min => 50 tick/sec (i.e. PAL)
        TFF = 255 beat/min => 6120 tick/min => 102 tick/sec

        pulses_per_tick = pulses_per_minute / ticks_per_minute
        // tempo change is sys msg FF 51 03 aa bb cc
        // convert to bpm with 60000000 / 0xaabbcc

        assuming 44100 samples/second, calculate samples per 'pulse' (midi delta-time value)
        ppqn pulses/quarter, and bpm quarter/minute
        thus => (ppqn × bpm) pulses/minute

        pulse/quarter × BPM quarter/min = pulse/min
        pulse/min ÷ 60 sec/min = pulse/sec
        => pulse/quarter × (BPM ÷ 60 quarter/sec) = pulse/sec
        pulse/sec ÷ tick/sec = pulse/tick

        pulse/tick bottoms out at bpm*ppqn=6120 (=> 1.0)
        ppqn*bpm values I have seen:
                bwv565          ppqn=480 bpm=[10, 85]   0.7843 - 6.6667
                chrome          ppqn=120 bpm=65         1.2745
                prtytime        ppqn=384 bpm=142        8.9098

        44100 sample/sec ÷ pulse/sec = sample/pulse
        (there might be some rounding error here, but this is just a test anyway)

        first event is always going to be the initial tick length setting. the file probably
        has its own bpm, but if not, the default is supposed to be 120, and the list needs
        some content to start with anyway, so here it is.
        */
        asprintf(&data, "mov tl, %d\n", 44100 / ((mthd.division * 120) / 60));
        event_queue = alloc_event(0, data, NULL);


        for (trknum = 0; trknum < mthd.num_tracks; trknum++) {
                unsigned int delta, vlen;
                int rs = 0; // running status byte
                int status; // THIS status byte (as opposed to rs)
                unsigned char hi, lo, cn, x, y;
                unsigned char vol = 0;
                unsigned int bpm; // stupid
                int found_end = 0;

                cur = event_queue;
                prev = NULL;

                if (fread(&mtrk, sizeof(mtrk), 1, fp) != 1) {
                        fprintf(stderr, "short read on track header\n");
                        break;
                }
                if (memcmp(mtrk.tag, "MTrk", 4) != 0) {
                        fprintf(stderr, "invalid track header (mangled file?)\n");
                        break;
                }
                mtrk.length = bswapBE32(mtrk.length);
                nextpos = ftell(fp) + mtrk.length; // where this track is supposed to end

                while (!found_end && ftell(fp) < nextpos) {
                        delta = read_varint(fp); // delta-time

                        // get status byte, if there is one
                        if (fpeek(fp) & 0x80) {
                                status = fgetc(fp);
                        } else if (rs & 0x80) {
                                status = rs;
                        } else {
                                // garbage?
                                fprintf(stderr, "expected status byte, got junk\n");
                                continue;
                        }

                        data = NULL;
                        hi = status >> 4;
                        lo = status & 0xf;
                        cn = lo; //or: trknum * CHANNELS_PER_TRACK + lo % CHANNELS_PER_TRACK;

                        switch (hi) {
                        case 0x8: // note off - x, y
                                rs = status;
                                x = fgetc(fp);
                                y = fgetc(fp);

                                asprintf(&data, "; note off channel=%d note=%c%c%d velocity=%d\n"
                                                "mov cn, %d\n"
                                                "mov freq, 0\n",
                                                lo, NOTE(x), y, cn);
                                break;
                        case 0x9: // note on - x, y (velocity zero = note off)
                                rs = status;
                                x = fgetc(fp);
                                y = fgetc(fp);

                                // XXX redundant & wasteful...
                                if (lo == 9) {} else // ignore percussion for now
                                if (y == 0) {
                                        asprintf(&data, "; note on channel=%d note=%c%c%d velocity=%d\n"
                                                        "mov cn, %d\n"
                                                        "mov freq, 0\n",
                                                        lo, NOTE(x), y, cn);
                                } else {
                                        asprintf(&data, "; note on channel=%d note=%c%c%d velocity=%d\n"
                                                        "mov cn, %d\n"
                                                        "mov freq, %.4lf\n"
                                                        "mov vol, %d\n",
                                                        lo, NOTE(x), y, cn, note_freq(x), 2 * y / 7);
                                }
                                break;
                        case 0xa: // polyphonic key pressure (aftertouch) - x, y
                                rs = status;
                                x = fgetc(fp);
                                y = fgetc(fp);
                                // TODO polyphonic aftertouch channel=lo note=x pressure=y
                                break;
                        case 0xb: // controller OR channel mode - x, y
                                rs = status;
                                // controller if first data byte 0-119
                                // channel mode if first data byte 120-127
                                x = fgetc(fp);
                                y = fgetc(fp);
                                // TODO controller change channel=lo controller=x value=y
                                break;
                        case 0xc: // program change - x (instrument/voice selection)
                                rs = status;
                                x = fgetc(fp);
                                // TODO program change channel=lo program=x
                                break;
                        case 0xd: // channel pressure (aftertouch) - x
                                rs = status;
                                x = fgetc(fp);
                                // TODO channel aftertouch channel=lo pressure=x
                                break;
                        case 0xe: // pitch bend - x, y
                                rs = status;
                                x = fgetc(fp);
                                y = fgetc(fp);
                                // TODO pitch bend channel=lo lsb=x msb=y
                                break;
                        case 0xf: // system messages
                                switch (lo) {
                                case 0xf: // meta-event (text and stuff)
                                        // tempo change is sys msg FF 51 03 aa bb cc
                                        // convert to bpm with 60000000 / 0xaabbcc

                                        x = fgetc(fp); // type
                                        vlen = read_varint(fp); // value length
                                        switch (x) {
                                        case 0x1: // text
                                        case 0x2: // copyright
                                        case 0x3: // track name
                                        case 0x4: // instrument name
                                        case 0x5: // lyric
                                        case 0x6: // marker
                                        case 0x7: // cue point
                                                y = MIN(vlen, sizeof(buf) - 1);
                                                fread(buf, 1, y, fp);
                                                buf[y] = 0;
                                                printf("; Track %d text(%02X): %s\n", trknum, x, buf);
                                                vlen -= y;
                                                break;

                                        case 0x20: // MIDI channel (FF 20 len* cc)
                                                // specifies which midi-channel sysexes are assigned to
                                        case 0x21: // MIDI port (FF 21 len* pp)
                                                // specifies which port/bus this track's events are routed to
                                                break;

                                        case 0x2f:
                                                found_end = 1;
                                                break;
                                        case 0x51: // set tempo
                                                // read another stupid kind of variable length number
                                                // hopefully this fits into 4 bytes - if not, too bad!
                                                memset(buf, 0, 4);
                                                y = MIN(vlen, 4);
                                                x = 4 - y;
                                                fread(buf + x, 1, y, fp);
                                                bpm = buf[0] << 24 | (buf[1] << 16) | (buf[2] << 8) | buf[3];
                                                if (!bpm)
                                                        bpm = 1; // don't divide by zero
                                                bpm = 60000000 / bpm; // what is this? friggin' magic?
                                                asprintf(&data, "; tempo change %02X %02X %02X %02X (%d bpm)\n"
                                                                "mov tl, %d\n",
                                                                buf[0], buf[1], buf[2], buf[3], bpm,
                                                                44100 / ((mthd.division * bpm) / 60));
                                                vlen -= y;
                                                break;
                                        case 0x54: // SMPTE offset (what time in the song this track starts)
                                                // (what the hell?)
                                                break;
                                        case 0x58: // time signature (FF 58 len* nn dd cc bb)
                                        case 0x59: // key signature (FF 59 len* sf mi)
                                                // TODO care? don't care?
                                                break;
                                        case 0x7f: // some proprietary crap
                                                break;

                                        default:
                                                // some mystery shit
                                                fprintf(stderr, "what is meta FF %02X?\n", x);
                                                break;
                                        }
                                        fseek(fp, vlen, SEEK_CUR);
                                        break;
                                case 0x0: // sysex
                                case 0x1 ... 0x7: // syscommon
                                        rs = 0; // clear running status
                                case 0x8 ... 0xe: // sysrt
                                        // 0xf0 - sysex
                                        // 0xf1-0xf7 - common
                                        // 0xf8-0xff - sysrt
                                        // sysex and common cancel running status
                                        // TODO handle these, or at least skip them coherently
                                        fprintf(stderr, "sysex/sysrt: %02X ...\n", status);
                                        break;
                                }
                        }

                        // add this event
                        // yes even if data == NULL, otherwise we'll fuck all the timings up
                        while (cur && delta > cur->delta) {
                                delta -= cur->delta;
                                prev = cur;
                                cur = cur->next;
                        }
                        // I'm fairly sure this can be condensed and simplified, but I'm tired and it works,
                        // so I'm not messing with it. OH GOD HOW DID THIS GET HERE I AM NOT GOOD WITH POINTERS
                        if (!cur) {
                                // ran off the end, add after prev.
                                new = alloc_event(delta, data, NULL);
                                prev->next = new;
                                prev = prev->next;
                        } else if (delta == cur->delta) {
                                // adjacent to current event
                                new = alloc_event(0, data, cur->next);
                                cur->next = new;
                                prev = cur;
                                cur = new;
                        } else {
                                // stuff between prev and cur, and adjust cur delta
                                new = alloc_event(delta, data, cur);
                                cur->delta -= delta;
                                prev->next = new;
                                prev = prev->next;
                        }
                }
                if (ftell(fp) != nextpos) {
                        fprintf(stderr, "track ended %ld bytes from boundary\n", ftell(fp) - nextpos);
                        fseek(fp, nextpos, SEEK_SET);
                }
        }

        // now dump it out and free everything.
        cur = event_queue;
        while (cur) {
                while (cur->delta) {
                        int x = MIN(cur->delta, 127);
                        printf("tick %d\n", x);
                        cur->delta -= x;
                }

                if (cur->data) {
                        fputs(cur->data, stdout);
                        free(cur->data);
                }
                prev = cur;
                cur = cur->next;
                free(prev);
        }

        //puts("jmp 0");
        puts("halt");

        fclose(fp);
        return 0;
}

