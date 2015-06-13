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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <ao/ao.h>

#include "waveforms.h"


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(v, l, h) MAX(MIN(v, h), l)

#define FRACBITS 11
#define FRACMASK ((1L << FRACBITS) - 1L)
#define FRACTION(n) ((n) * (1 << FRACBITS)) // for converting floats

#define SMPRATE 44100

#define NUM_CHANNELS 16
#define STACK_SIZE 1024

struct channel {
        // channel value is (pos >> FRACBITS) & 255
        // always ignore the upper 8 bits of pos.
        // pos/freq are fixed point.
        // there's really nothing to this struct...
        uint32_t r[8]; // channel registers (e.g. freq is r[CREG_FREQ])
};

struct state {
        uint8_t *data; // code base.
        uint32_t stack[1024];
        struct channel chan[NUM_CHANNELS]; // channel base

        uint32_t r[8]; // global registers (tl is reg[REG_TL], etc.)
        uint8_t flags;
};

// ------------------------------------------------------------------------------------------------------------
// this really sucks, because I threw it together as I was thinking of it - nothing here was really planned
// or given much consideration before being implemented.
// so it's kinda like x86.


// Registers: 4 bits
#define REG_R0 0        // General-purpose (some opcodes write to this)
#define REG_R1 1        // General-purpose
#define REG_R2 2        // General-purpose
#define REG_TL 3        // Tick length - how long a 'tick' opcode adds to delay
#define REG_FC 4        // Frame counter - sample frames to mix until next event-fetch
#define REG_CN 5        // Channel - affects get/set of channel regs
#define REG_SP 6        // Stack pointer - stack[sp] is current value
#define REG_IP 7        // Instruction pointer

// following are NOT accessible via r[], since they are part of the channel regs!
#define REG_EXT 8   // bit set if referring to a channel register (clear this prior to access, of course)

// after masking out REG_EXT, cregs are:
#define CREG_POS 1      // Channel data position (fixed-point; generally, don't mess with this)
#define CREG_VOL 2      // Channel volume
#define CREG_WAVE 3     // Channel waveform
#define CREG_FREQ 4     // Channel frequency
// others are undefined

// Flags.
#define FL_POS 0x1 // set if compare > 0
#define FL_NEG 0x2 // set if compare < 0
#define FL_TRACE 0x8 // dump all instructions prior to execution

// Top 4 bits of opcode
#define OPH_MISC 0x00 // nop, tick0, tick8, halt, stackops
#define OPH_MOV_IMM8 0x10 // target reg, next byte = value
#define OPH_MOV_IMM32 0x20 // target reg, next 4 bytes = value
#define OPH_STACK 0x40 // reg uses lower 3 bits, ext bit = 1 for push, 0 for pop
        // e.g. 'pop fc' = 0x4a (OPH_STACK | REG_EXT | REG_FC)
#define OPH_IO 0x60 // same as for stack. ext bit = 0 for read (stdin), 1 for write (stdout)
#define OPH_ADD_IMM8 0x80 // target reg, next byte = value
#define OPH_SUB_IMM8 0x90 // target reg, next byte = value
#define OPH_ARITH 0xd0 // what (add, sub, mul, shl, shr, or, and, xor); next byte = regs (hi=dest, lo=src)
#define OPH_FLOW 0xe0 // jmp, call, ret
#define OPH_EXTD 0xf0 // extension for some 2-byte opcodes

// Bottom 4 bits
#define OPL_MISC_NOP 0x0
#define OPL_MISC_TICK0 0x1
#define OPL_MISC_TICK8 0x2
#define OPL_MISC_HALT 0x4
#define OPL_MISC_DDUMP 0x5 // not actually an opcode for this -- needs to be written as 'db 5'
#define OPL_MISC_DTRACE 0x6 // likewise.
// Forthy stuff (1 byte)
#define OPL_MISC_STSWAP 0x8
#define OPL_MISC_STDUP 0x9
#define OPL_MISC_STROT 0xa
#define OPL_MISC_STDROP 0xb
#define OPL_MISC_STNIP 0xc
#define OPL_MISC_STTUCK 0xd
#define OPL_MISC_STOVER 0xe
#define OPL_MISC_STSWAP2 0xf

// reg/reg arithmetic - 2 bytes total (1 op 1 regs)
#define OPL_ARITH_AND 0x0
#define OPL_ARITH_XOR 0x1
#define OPL_ARITH_OR 0x2
#define OPL_ARITH_ADD 0x3
#define OPL_ARITH_SUB 0x4
#define OPL_ARITH_MUL 0x5
#define OPL_ARITH_SHL 0x6
#define OPL_ARITH_SHR 0x7
#define OPL_ARITH_CMP 0x8
#define OPL_ARITH_DIV 0x9
// these mov's don't belong here, but that's where they ended up lol
#define OPL_ARITH_MOV_RR 0xc // reg<-reg
#define OPL_ARITH_LDA_RR 0xd // reg<-*reg (load value from data[src] into dest)

// flow is 1 byte + data
#define OPL_FLOW_JMP8 0x0
#define OPL_FLOW_JMP32 0x1
#define OPL_FLOW_JZ8 0x2
#define OPL_FLOW_JZ32 0x3
#define OPL_FLOW_JNZ8 0x4
#define OPL_FLOW_JNZ32 0x5
#define OPL_FLOW_CALL8 0x6
#define OPL_FLOW_CALL32 0x7
#define OPL_FLOW_RET 0x8
#define OPL_FLOW_JL8 0x9
#define OPL_FLOW_JL32 0xa
#define OPL_FLOW_JG8 0xb
#define OPL_FLOW_JG32 0xc


// reg/immed arith 2-byte ops (add/sub are 1, but I might change that)
#define OPL_EXTD_ARITH8 0x0
#define OPL_EXTD_ARITH32 0x1
#define OPH_EXTD_ARITH_AND (OPL_ARITH_AND << 4)
#define OPH_EXTD_ARITH_XOR (OPL_ARITH_XOR << 4)
#define OPH_EXTD_ARITH_OR  (OPL_ARITH_OR << 4)
#define OPH_EXTD_ARITH_ADD (OPL_ARITH_ADD << 4)
#define OPH_EXTD_ARITH_SUB (OPL_ARITH_SUB << 4)
#define OPH_EXTD_ARITH_MUL (OPL_ARITH_MUL << 4)
#define OPH_EXTD_ARITH_SHL (OPL_ARITH_SHL << 4)
#define OPH_EXTD_ARITH_SHR (OPL_ARITH_SHR << 4)
#define OPH_EXTD_ARITH_CMP (OPL_ARITH_CMP << 4)
#define OPH_EXTD_ARITH_DIV (OPL_ARITH_DIV << 4)

// ------------------------------------------------------------------------------------------------------------

void mix(int16_t *obuf, int frames, struct channel *ch)
{
        // one cycle of the waveform has 256 data points, hence the multiplication
        // (note: freq is fixed point)
        uint32_t inc = ch->r[CREG_FREQ] * 256 / SMPRATE;

        // Here are a couple of lesser-known C operators.
        while (frames --> 0) {
                *obuf +++= waveforms[ch->r[CREG_WAVE]][(ch->r[CREG_POS] >> FRACBITS) & 255] * ch->r[CREG_VOL];
                ch->r[CREG_POS] += inc;
        }
}

// ------------------------------------------------------------------------------------------------------------

uint8_t read8(struct state *s)
{
        return s->data[s->r[REG_IP]++];
}

uint32_t read32(struct state *s)
{
        uint32_t r;
        r  = s->data[s->r[REG_IP]++];
        r |= s->data[s->r[REG_IP]++] << 8;
        r |= s->data[s->r[REG_IP]++] << 16;
        r |= s->data[s->r[REG_IP]++] << 24;
        return r;
}

// ------------------------------------------------------------------------------------------------------------

// these suck
#define _GET_R(s, reg) ((reg & REG_EXT) ? s->chan[s->r[REG_CN]].r : s->r)
#define SETREG(s, dest, op, value) ({                           \
        uint8_t _d = dest;                                      \
        uint32_t *_r = _GET_R(s, _d);                           \
        _d &= ~REG_EXT;                                         \
        _r[_d] op value;                                        \
        s->flags &= ~(FL_NEG | FL_POS);                         \
        if (_r[_d] & 0x80000000)                                \
                s->flags |= FL_NEG;                             \
        else if (_r[_d])                                        \
                s->flags |= FL_POS;                             \
})
#define GETREG(s, src) ({                                       \
        uint8_t _r = src;                                       \
        _GET_R(s, _r)[_r & ~REG_EXT];                           \
})
#define CMPREG(s, reg, value) ({                                \
        uint8_t _d = reg;                                       \
        uint32_t *_r = _GET_R(s, _d);                           \
        _d &= ~REG_EXT;                                         \
        int _v = _r[_d] - value;                                \
        s->flags &= ~(FL_NEG | FL_POS);                         \
        if (_v < 0)                                             \
                s->flags |= FL_NEG;                             \
        else if (_v > 0)                                        \
                s->flags |= FL_POS;                             \
})

void dumpstate(struct state *s)
{
        int n, o;

        fprintf(stderr,
                "\nregs\tIP:%08X SP:%08X FC:%08X TL:%08X\n\tR0:%08X R1:%08X R2:%08X CN:%08X\n",
                s->r[REG_IP], s->r[REG_SP], s->r[REG_FC], s->r[REG_TL],
                s->r[REG_R0], s->r[REG_R1], s->r[REG_R2], s->r[REG_CN]);

        for (n = 0; n < NUM_CHANNELS; n++) {
                uint32_t *cr = s->chan[n].r;
                fprintf(stderr, "c[%d]\tPOS:%08X VOL:%08X WAVE:%08X FREQ:%08X\n", n,
                        cr[CREG_POS], cr[CREG_VOL], cr[CREG_WAVE], cr[CREG_FREQ]);
        }
        // dump the stack
        fprintf(stderr, "stack\t");
        n = MIN(STACK_SIZE, s->r[REG_SP]);
        while (n --> 0)
                fprintf(stderr, "%08X ", s->stack[n]);
        fprintf(stderr, "-\n\n");
}


// ret 1 = ok, 0 = invalid (TODO this is an appropriate place for longjmp)
int arithop(struct state *s, uint8_t op, uint8_t reg, uint32_t val)
{
        switch (op) {
        case OPL_ARITH_AND:
                SETREG(s, reg, &=, val);
                return 1;
        case OPL_ARITH_XOR:
                SETREG(s, reg, ^=, val);
                return 1;
        case OPL_ARITH_OR:
                SETREG(s, reg, |=, val);
                return 1;
        case OPL_ARITH_ADD:
                SETREG(s, reg, +=, val);
                return 1;
        case OPL_ARITH_SUB:
                SETREG(s, reg, -=, val);
                return 1;
        case OPL_ARITH_SHL:
                SETREG(s, reg, <<=, val);
                return 1;
        case OPL_ARITH_SHR:
                SETREG(s, reg, >>=, val);
                return 1;
        case OPL_ARITH_CMP:
                CMPREG(s, reg, val);
                return 1;
        case OPL_ARITH_MUL:
                SETREG(s, reg, *=, val);
                return 1;
        case OPL_ARITH_DIV:
                SETREG(s, reg, /=, val);
                return 1;
        // These should be somewhere else :[
        case OPL_ARITH_MOV_RR:
                SETREG(s, reg, =, val);
                return 1;
        case OPL_ARITH_LDA_RR:
                SETREG(s, reg, =, s->data[val]);
                return 1;
        default:
                return 0;
        }
}

void event_fetch(struct state *s)
{
        uint8_t op = read8(s);
        uint8_t hi = op & 0xf0, lo = op & 0xf;
        uint32_t tmp;

        if (s->flags & FL_TRACE)
                printf("IP:%08X\t%02X\n", s->r[REG_IP] - 1, op);

        switch (hi) {
        case OPH_MISC:
                switch (lo) {
                case OPL_MISC_NOP:                      // nop
                        return;
                case OPL_MISC_TICK0:                    // tick
                        s->r[REG_FC] += s->r[REG_TL];
                        return;
                case OPL_MISC_TICK8:                    // tick 10
                        s->r[REG_FC] += read8(s) * s->r[REG_TL];
                        return;
                case OPL_MISC_HALT:
                        exit(0);

                case OPL_MISC_DDUMP:
                        dumpstate(s);
                        return;
                case OPL_MISC_DTRACE:
                        s->flags ^= FL_TRACE;
                        return;

                case OPL_MISC_STSWAP:                   // swap
                        {
                        uint32_t *st = s->stack + s->r[REG_SP];
                        tmp = st[-1];
                        st[-1] = st[-2];
                        st[-2] = tmp;
                        }
                        return;
                case OPL_MISC_STDUP:
                        tmp = s->stack[s->r[REG_SP] - 1];
                        s->stack[s->r[REG_SP]++] = tmp;
                        return;
                case OPL_MISC_STDROP:                   // drop
                        s->r[REG_SP]--;
                        return;
                /*
                case OPL_MISC_STROT:                    // rot
                case OPL_MISC_STNIP:                    // nip
                case OPL_MISC_STTUCK:                   // tuck
                case OPL_MISC_STOVER:                   // over
                case OPL_MISC_STSWAP2:                  // swap2
                */
                default:
                        break;
                }
                break;

        case OPH_MOV_IMM8:                              // mov r2, 37
                SETREG(s, lo, =, read8(s));
                return;
        case OPH_MOV_IMM32:                             // mov r2, 7654321
                SETREG(s, lo, =, read32(s));
                return;
        case OPH_STACK:                                 // push r2; pop r2
                if (lo & REG_EXT)
                        s->stack[s->r[REG_SP]++] = s->r[lo & ~REG_EXT]; // push
                else
                        s->r[lo] = s->stack[--s->r[REG_SP]]; // pop
                return;
        case OPH_IO:
                if (lo & REG_EXT)
                        putchar(s->r[lo & ~REG_EXT]);   // putc r2
                else
                        SETREG(s, lo, =, getchar());    // getc r2
                return;
        case OPH_ADD_IMM8:                              // add r2, 37
                arithop(s, OPL_ARITH_ADD, lo, read8(s));
                return;
        case OPH_SUB_IMM8:                              // sub r2, 37
                arithop(s, OPL_ARITH_SUB, lo, read8(s));
                return;

        case OPH_ARITH:
                tmp = read8(s); // lo = dest reg, hi = src reg
                if (arithop(s, lo, tmp & 0xf, GETREG(s, tmp >> 4)))
                        return;
                break;

        case OPH_FLOW:
                // TODO jmps should be relative to the current location, but that makes the assembler
                // slightly more complex, and I just want working code first
                switch (lo) {
                case OPL_FLOW_JMP8:
                        s->r[REG_IP] = read8(s);
                        return;
                case OPL_FLOW_JMP32:
                        s->r[REG_IP] = read32(s);
                        return;
                case OPL_FLOW_JZ8:
                        tmp = read8(s);
                        if (!(s->flags & (FL_POS | FL_NEG)))
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JZ32:
                        tmp = read32(s);
                        if (!(s->flags & (FL_POS | FL_NEG)))
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JNZ8:
                        tmp = read8(s);
                        if (s->flags & (FL_POS | FL_NEG))
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JNZ32:
                        tmp = read32(s);
                        if (s->flags & (FL_POS | FL_NEG))
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_CALL8:
                        tmp = read8(s);
                        s->stack[s->r[REG_SP]++] = s->r[REG_IP];
                        s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_CALL32:
                        tmp = read32(s);
                        s->stack[s->r[REG_SP]++] = s->r[REG_IP];
                        s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_RET:
                        s->r[REG_IP] = s->stack[--s->r[REG_SP]];
                        return;
                case OPL_FLOW_JL8:
                        tmp = read8(s);
                        if (s->flags & FL_NEG)
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JL32:
                        tmp = read32(s);
                        if (s->flags & FL_NEG)
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JG8:
                        tmp = read8(s);
                        if (s->flags & FL_POS)
                                s->r[REG_IP] = tmp;
                        return;
                case OPL_FLOW_JG32:
                        tmp = read32(s);
                        if (s->flags & FL_POS)
                                s->r[REG_IP] = tmp;
                        return;
                default:
                        break;
                }
                break;

        case OPH_EXTD:
                switch (lo) {
                case OPL_EXTD_ARITH8:
                        // NOTE: shorter opcodes exist for add/sub with imm8 operand
                        tmp = read8(s); // hi=op lo=reg
                        if (arithop(s, tmp >> 4, tmp & 0xf, read8(s)))
                                return;
                        break;
                case OPL_EXTD_ARITH32:
                        tmp = read8(s); // hi=op lo=reg
                        if (arithop(s, tmp >> 4, tmp & 0xf, read32(s)))
                                return;
                        break;
                default:
                        break;
                }
                break;

        default:
                break;
        }

        fprintf(stderr, "*** Undefined opcode\n");
        dumpstate(s);
        exit(-1);
}

// ------------------------------------------------------------------------------------------------------------

ao_device *open_audio(const char *name)
{
        ao_sample_format fmt = {16, SMPRATE, 1, AO_FMT_NATIVE, NULL};
        char *ext;
        int id;

        if (name) {
                ext = strrchr(name, '.');
                id = ao_driver_id(ext ? (ext + 1) : name);
        } else {
                ext = NULL;
                id = ao_default_driver_id();
        }
        return ext
                ? ao_open_file(id, name, 1, &fmt, NULL)
                : ao_open_live(id, &fmt, NULL)
                ;
}

uint8_t *load_bytecode(const char *filename)
{
        FILE *fp;
        uint8_t *buf;
        struct stat st;
        long len;

        fp = fopen(filename, "rb");
        if (!fp) {
                perror(filename);
                return NULL;
        }
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        buf = malloc(len);
        if (!buf || fread(buf, len, 1, fp) != 1) {
                perror(filename);
                fclose(fp);
                free(buf);
                return NULL;
        }
        fclose(fp);
        return buf;
}

#define OBUF_FRAMES 4096
#define BYTES_PER_FRAME 2
int main(int argc, char **argv)
{
        struct state s = {};
        int16_t obuf[OBUF_FRAMES];
        ao_device *dev;
        int32_t tmp;

        uint8_t *prog;

        if (argc != 2 && argc != 3) {
                fprintf(stderr, "usage: %s <filename> [device|filename.ext]\n", argv[0]);
                return 1;
        }

        prog = load_bytecode(argv[1]);
        if (!prog)
                return 2;


        ao_initialize();
        dev = open_audio(argc == 3 ? argv[2] : NULL);
        if (!dev) {
                fprintf(stderr, "error opening audio device\n");
                return 3;
        }

        s.data = prog;


        for (;;) {
                event_fetch(&s);
                //stackdump(&s);
                while (s.r[REG_FC]) {
                        uint32_t frames = MIN(s.r[REG_FC], OBUF_FRAMES);
                        uint32_t bytes = frames * BYTES_PER_FRAME;
                        int n;

                        memset(obuf, 0, bytes);
                        for (n = 0; n < NUM_CHANNELS; n++)
                                mix(obuf, frames, s.chan + n);
                        ao_play(dev, (char *) obuf, bytes); // stupid cast.

                        s.r[REG_FC] -= frames;
                }
        }

        ao_close(dev);
        ao_shutdown();

        return 0;
}

