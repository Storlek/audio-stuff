/*
 * copyright (c) 2009 Storlek - http://rigelseven.com/
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h> // for generating a sine

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

// IT Sample Format
typedef struct its {
        char imps[4]; // "IMPS"
        char filename[13]; // includes \0
        uint8_t gvl;
        uint8_t flags;
        uint8_t vol;
        char name[26]; // includes \0
        uint8_t cvt;
        uint8_t dfp;
        uint32_t length;
        uint32_t loopbegin;
        uint32_t loopend;
        uint32_t c5speed;
        uint32_t susloopbegin;
        uint32_t susloopend;
        uint32_t samplepointer;
        uint8_t vis;
        uint8_t vid;
        uint8_t vir;
        uint8_t vit;
} its_t;


#define FLG_EXISTS 1
#define FLG_16BIT  2
#define FLG_COMPR  8
#define CVT_SIGNED 1
#define CVT_DELTA  4



void pbits(int n, uint32_t v) {
        uint32_t mask = 1 << (n - 1);
        while (mask) {
                fputc((v & mask) ? '1' : '0', stderr);
                mask >>= 1;
        }
}


// get the 'raw' minimum width to store the value (assuming sign extend)
uint8_t _minwidth(int32_t value)
{
        int bits = 32;
        uint32_t mask = 1 << 31;

        if (value & mask) {
                do {
                        bits--;
                        mask >>= 1;
                } while (value & mask);
        } else {
                do {
                        bits--;
                        mask >>= 1;
                } while (mask && !(value & mask));
        }
        return bits + 1;
}

// check if a value is a "special" value in the given width
// 1 if it is, 0 if not
int ismarker_8(uint8_t width, int32_t value)
{
        value &= ((1 << width) - 1);
        if (width < 7) {
                // if it's the marker, make it wider
                if ((value & ((1 << width) - 1)) == (1 << (width - 1))) {
                        return 1;
                }
        } else if (width < 9) {
                // lower border for width chg
                uint8_t border = (0xFF >> (9 - width)) - 4;
                if (value > border && value <= (border + 8)) {
                        return 1;
                }
        }
        // marker for width 9 can't be a value, so don't worry about it
        return 0;
}

// If value is a marker, return width + 1, else return width
uint8_t minwidth_8(int32_t value)
{
        uint8_t width = _minwidth(value);
        return width + ismarker_8(width, value);
}


// How many bits are required to adjust from a given width
int cost_of_change_8[] = { 0, 1+3, 2+3, 3+3, 4+3, 5+3, 6+3, 7, 8, 9 };

// Determine the most cost-effective width. Used when shifting width downward only.
uint8_t find_best_width_down_8(uint8_t fromwidth, uint8_t minw0, int8_t *stream, uint16_t length)
{
        int n = 0;
        int coc = cost_of_change_8[fromwidth];
        int overhead; // minimal cost of not changing width, for each value
        int wcost; // total number of extra bits
        int width;

        if (fromwidth <= minw0) {
                fprintf(stderr, "what!\n");
                exit(30);
        }

        for (width = minw0; width < 9; width++) {
                overhead = fromwidth - width;
                wcost = 0;
                for (n = 0; n < length && minwidth_8(stream[n]) <= width; n++) {
                        wcost += overhead;
                        if (wcost > coc) {
                                return width; // better to change to the new width
                        }
                }
        }
        return fromwidth; // better to keep the old width
}

// Determine the most cost-effective width. Used when shifting width upward only.
uint8_t find_best_width_up_8(int8_t *stream, uint16_t lookahead)
{
        int n;
        int wcost[10] = {0}; // number of "wasted" bits (i.e. not used to encode values) for each width
        int realwidth[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}; // highest width used for the given start width
        int minw0 = minwidth_8(stream[0]);
        int minw;
        int swidth; // starting width (realwidth[swidth] gives final width)
        int bestwidth, bestcost; // calculated best width and its cost

        lookahead = MIN(lookahead, 12); // ??

        for (swidth = minw0; swidth < 10; swidth++) {
                for (n = 0; n < lookahead; n++) {
                        minw = minwidth_8(stream[n]);
                        if (minw > realwidth[swidth]) {
                                wcost[swidth] += cost_of_change_8[realwidth[swidth]];
                                // add this for more IT-like behavior for some circumstances:
                                //wcost[swidth] += cost_of_change_8[minw];
                                realwidth[swidth] = minw;
                        } else {
                                wcost[swidth] += realwidth[swidth] - minw;
                        }
                }
        }
        // find the most efficient width
        bestwidth = 0;
        bestcost = 0xffff;
        for (swidth = minw0; swidth <= realwidth[minw0]; swidth++) {
                if (wcost[swidth] < bestcost) {
                        bestcost = wcost[swidth];
                        bestwidth = swidth;
                }
        }
        return bestwidth;
}



int main(int argc, char **argv)
{
        its_t sample;
        int8_t *data, *in;
        int8_t last, this;
        unsigned int n, length, smpwidth;
        int it215;
        char *err = NULL;

        if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO)) {
                fprintf(stderr, "usage: %s [-5] < input.its > output.its\n", argv[0]);
                return 1;
        }

        it215 = (argc >= 2 && strcmp(argv[1], "-5") == 0);

        if (fread(&sample, sizeof(sample), 1, stdin) != 1) {
                err = ferror(stdin) ? "read error" : "truncated file";
        } else if (sample.flags & FLG_COMPR) {
                err = "sample is already compressed";
        } else if (sample.cvt & CVT_DELTA) {
                err = "delta-encoded input unsupported";
        } else if (sample.flags & FLG_16BIT) {
                err = "16-bit data unsupported";
        }
        if (err) {
                fprintf(stderr, "%s: %s\n", argv[1], err);
                return 2;
        }
        
        length = sample.length;

        smpwidth = (sample.flags & FLG_16BIT) ? 2 : 1;
        data = in = calloc(length, smpwidth);
        if (!data) {
                fprintf(stderr, "out of memory!\n");
                return 2;
        }

        if (fread(data, length, smpwidth, stdin) != smpwidth) {
                fprintf(stderr, "%s: read error\n", argv[1]);
                free(data);
                return 2;
        }

        sample.flags |= FLG_COMPR;
        if (it215)
                sample.cvt |= CVT_DELTA;

        if (fwrite(&sample, sizeof(sample), 1, stdout) != 1) {
                fprintf(stderr, "%s: write error\n", argv[2]);
                free(data);
                return 2;
        }


        // differentiate the sample data
        fprintf(stderr, "differentiating data\n");
        last = 0;
        for (n = 0; n < length; n++) {
                this = data[n]; // value in ...
                data[n] = this - last; // and delta out
                last = this;
        }

        if (it215) {
                fprintf(stderr, "... and again\n");
                last = 0;
                for (n = 0; n < length; n++) {
                        this = data[n]; // value in ...
                        data[n] = this - last; // and delta out
                        last = this;
                }
        }



        // pack it
        fprintf(stderr, "packing go!\n");

        // process the input in blocks of 0x8000 bytes of output
        while (length) {
                uint32_t obuf[0x8000 / 4];
                uint32_t *out = obuf;
                uint16_t osize = 0; // how big the compressed block is (in bytes)

                *out = 0; // the bit sequence that's going to the file
                uint8_t buffered = 0; // how many bits have been placed in this chunk
                uint8_t width = 9; // how many bits are being written at a time

                void writebits(uint8_t n, uint32_t bits, char *comment) {
                        bits &= ((1 << n) - 1);
                        *out |= (bits << buffered);
                        buffered += n;
                        if (comment) {
                                /*
                                fprintf(stderr, "> %d bits out: ", n);
                                pbits(9, bits);
                                fprintf(stderr, "          %s\n", comment);
                                */
                        }
                        // overflow?
                        if (buffered >= 32) {
                                /*
                                fprintf(stderr, "= %02x %02x %02x %02x\n",
                                        (*out) & 0xff,
                                        ((*out) >> 8) & 0xff,
                                        ((*out) >> 16) & 0xff,
                                        ((*out) >> 24) & 0xff);
                                */
                                out++;
                                osize += 4;
                                buffered -= 32;
                                // 'buffered' now holds the remaining bits that we DIDN'T write
                                // so we need to write those upper bits now
                                *out = bits >> (n - buffered);
                        }
                }


                void changewidth(uint8_t newwidth) {
                        if (width == 7 || width == 8) {
                                // value - border
                                // (and increment if >= old width)
                                writebits(width, (
                                          (0xff >> (9 - width)) - 4 // lower border
                                          + newwidth
                                          - (newwidth > width ? 1 : 0)
                                          ) & 0xff,
                                          "border");
                        } else if (width == 9) {
                                writebits(9, 0x100 | (newwidth - 1), "width");
                        } else {
                                writebits(width, 1 << (width - 1), "marker");
                                writebits(3, newwidth - (newwidth > width ? 2 : 1), "width");
                        }
                        width = newwidth;
                }

                for (n = 0; n < length && osize + ((buffered + 7) >> 3) < 0x8000; n++) {
                        int8_t value = in[n];
                        uint8_t minw = minwidth_8(value);
                        uint8_t neww = width;

                        if (minw < width) {
                                // Going down?
                                neww = find_best_width_down_8(width, minw, in + n, length - n);
                        } else if (minw > width) {
                                // Going up?
                                neww = find_best_width_up_8(in + n, length - n);
                        } else {
                                // No change.
                        }
                        if (neww != width) {
                                changewidth(neww);
                        }
                        if (width < minw) {
                                fprintf(stderr, "\nPROBLEM, should've changed width and didn't!\n");
                                exit(99); // should not happen
                        }
                        writebits(width, value & 0xff, "data");
                }
                osize += (buffered + 7) >> 3; // round up to the next highest byte
                fprintf(stderr, "... outputting compressed block, %d bytes\n", osize);
                if (fwrite(&osize, 2, 1, stdout) != 1
                    || fwrite(obuf, osize, 1, stdout) != 1) {
                        fprintf(stderr, "... or not! file error omg\n");
                }
                in += n;
                length -= n;
        }
        fflush(stdout);
        if (ferror(stdout)) {
                fprintf(stderr, "file error on write\n");
        } else {
                fprintf(stderr, "okey doke\n");
        }
        free(data);

        return 0;
}

