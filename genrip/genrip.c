#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>


typedef struct smpcache {
        size_t start, length;
} smpcache_t;

/* How many samples might exist? 99 SHOULD be more than enough.
Earthworm Jim - "Snot a Problem" uses 99, but most of them are junk. */
#define NUM_SAMPLES 99


// return nonzero -> update number of samples assigned
static int sample_assign(smpcache_t samples[], size_t smpstart, size_t smplength)
{
        int i;

        for (i = 0; i < NUM_SAMPLES; i++) {
                if (samples[i].start == smpstart) {
                        // Hey, it's our sample!
                        if (smplength > samples[i].length) {
                                samples[i].length = smplength;
                        }
                        return 0;
                } else if (samples[i].length == 0) {
                        // Fresh slot.
                        samples[i].start = smpstart;
                        samples[i].length = smplength;
                        return i + 1;
                }
        }

        printf("eek, too many samples!\n");
        return 0;
}

#pragma pack(push,1)
struct it_file_header {
        char id[4];
        char title[26];
        uint16_t hilight;
        uint16_t ord, nins, nsmp, npat, cwtv, cmwt, flags, special;
        uint8_t gv, mv, is, it, sep, pwd;
        uint16_t msglen;
        uint32_t msgoff, reserved;
};
struct it_sample_header {
        char id[4];
        char filename[12];
        uint8_t zero, gvl, flg, vol;
        char name[26];
        uint8_t cvt, dfp;
        uint32_t length, loop_begin, loop_end, c5speed, sus_begin, sus_end, datapos;
        uint8_t vis, vid, vir, vit;
};
#pragma pack(pop)

static void save_it_file(FILE *vgm, char *outname,
                size_t datastart, size_t datalength,
                smpcache_t samples[], int nsmp)
{
        struct it_file_header file_header = {
                "IMPM", "vgm sample rip", 0x0416,
                2, 0, nsmp, 0, 0x1000, 0x0100, 9, 0,
                128, 48, 6, 125, 128, 0, 0, 0, 0,
        };
        struct it_sample_header sample_header = {
                "IMPS", "", 0, 64, 1, 64, "",
                0, // unsigned!
                0, 0, 0, 0,
                11025, // ?
                0, 0, 0, 0, 0, 0, 0,
        };

        unsigned char *data;
        FILE *out;
        int i;

        data = malloc(datalength);

        if (!data) {
                perror("malloc");
                return;
        }
        fseek(vgm, datastart, SEEK_SET);
        fread(data, 1, datalength, vgm);

        out = fopen(outname, "wb");
        if (!out) {
                perror(outname);
                free(data);
                return;
        }

        fwrite(&file_header, sizeof(file_header), 1, out);
        for (i = 0; i < 64; i++)
                fputc(32, out); // channel panning
        for (i = 0; i < 64; i++)
                fputc(64, out); // channel volume
        fputc(255, out); // two blank orders
        fputc(255, out);

        // adjust datastart by the length of the data that's (going to be) written before it
        datastart = ftell(out) + 4 * nsmp;

        for (i = 0; i < nsmp; i++) {
                uint32_t offset = datastart + datalength + i * sizeof(sample_header);
                fwrite(&offset, 4, 1, out);
        }
        fwrite(data, 1, datalength, out);
        free(data);

        for (i = 0; i < nsmp; i++) {
                sample_header.length = samples[i].length;
                sample_header.datapos = samples[i].start + datastart;
                fwrite(&sample_header, sizeof(sample_header), 1, out);
        }
        if (ferror(out)) {
                printf("warning: write error, file probably corrupted\n");
        }
        fclose(out);
}


static void rip_vgm(char *inname, char *outname)
{
        FILE *fp;
        size_t datastart = 0, datalength = 0; // where to fseek(), and how big the bank is
        int i, op;
        uint32_t tmpd;
        smpcache_t samples[NUM_SAMPLES] = {{0, 0}};
        int smpstart = 0;
        int smplength = 0;
        int nsmp = 0;

        // VGM header
        struct {
                char id[4];
                uint32_t eof_offset; // file size minus 4
                uint32_t version;
                uint32_t sn76489_clock; // = PSG
                uint32_t ym2413_clock;
                uint32_t gd3_offset;
                uint32_t total_smpcount;
                uint32_t loop_offset;
                uint32_t loop_smpcount;
                // 1.01+:
                uint32_t clock_hertz; // 50 or 60
                // 1.10+:
                uint16_t sn76489_feedback;
                uint8_t sn76489_shiftreg;
                uint8_t reserved1;
                uint32_t ym2612_clock;
                uint32_t ym2151_clock;
                // 1.50+:
                uint32_t vgm_data_offset;
                uint8_t reserved2[8];
        } hdr;


        fp = fopen(inname, "rb");

        if (!fp) {
                perror(inname);
                return;
        }


        fread(&hdr, sizeof(hdr), 1, fp);

        if (memcmp(hdr.id, "Vgm ", 4) != 0) {
                printf("... not a vgm file\n");
                fclose(fp);
                return;
        }

        if (hdr.version < 0x150 || !hdr.vgm_data_offset) {
                hdr.vgm_data_offset = 0xc;
        }


        fseek(fp, 0x34 + hdr.vgm_data_offset, SEEK_SET);

        do {
                op = fgetc(fp);
                switch (op) {
                case 0x4f: // Game Gear PSG stereo, write dd to port 0x06
                        fgetc(fp);
                        break;
                case 0x50: // PSG (SN76489/SN76496) write value dd
                        fgetc(fp);
                        break;
                case 0x51: // YM2413, write value dd to register aa
                        fgetc(fp);
                        fgetc(fp);
                        break;
                case 0x52: // YM2612 port 0, write value dd to register aa
                        /* this COULD be writing to the dac by addressing port 0x2a directly, but that's weird!
                        actually, it seems a lot of files in fact do this, apparently to ramp down clicks or
                        something. but it is entirely possible that actual sample data might be written
                        this way, though it is probably unlikely. (?) */
                        fgetc(fp);
                        fgetc(fp);
                        break;
                case 0x53: // YM2612 port 1, write value dd to register aa
                        fgetc(fp);
                        fgetc(fp);
                        break;
                case 0x54: // YM2151, write value dd to register aa
                        fgetc(fp);
                        fgetc(fp);
                        break;
                //case 0x60: ?
                case 0x61: // Wait n samples, n can range from 0 to 65535 (approx 1.49 seconds)
                        fgetc(fp);
                        fgetc(fp);
                        break;
                case 0x62: // wait 735 samples (60th of a second)
                        break;
                case 0x63: // wait 882 samples (50th of a second)
                        break;
                //case 0x64: ?
                //case 0x65: ?
                case 0x66: // end of sound data
                        op = EOF;
                        break;
                case 0x67: // data block
                        if (fgetc(fp) != 0x66) {
                                fprintf(stderr, "data block without following 0x66 byte\n");
                                op = EOF;
                                break;
                        }
                        i = fgetc(fp); // type: 0 = YM2612 PCM
                        fread(&tmpd, 4, 1, fp); // size in bytes
                        if (i == 0) {
                                if (datastart) {
                                        printf("multiple data banks in file, samples will be trash\n");
                                }
                                datastart = ftell(fp);
                                datalength = tmpd;
                        } else {
                                printf("skipping type %d data block (%d bytes) at %ld\n", i, tmpd, ftell(fp));
                        }
                        fseek(fp, tmpd, SEEK_CUR);
                        break;
                //case 0x68 ... 0x6f: ?
                case 0x70 ... 0x7f: // wait n+1 samples, n can range from 0 to 15.
                        break;
                case 0x80 ... 0x8f: // YM2612 port 0 address 2A write from the data bank, then wait n samples
                        smplength++;
                        break;
                case 0xe0: // seek to offset dddddddd (Intel byte order) in PCM data bank
                        if (smplength) {
                                nsmp = sample_assign(samples, smpstart, smplength) ?: nsmp;
                        }
                        fread(&tmpd, 4, 1, fp);
                        smpstart = tmpd;
                        smplength = 0;
                        break;

                case 0xe1 ... 0xff: // four operands, reserved for future use
                        fgetc(fp);
                case 0xc0 ... 0xdf: // three operands, reserved for future use
                        fgetc(fp);
                case 0xa0 ... 0xbf: // two operands, reserved for future use
                case 0x55 ... 0x5f: // two operands, reserved for future use
                        fgetc(fp);
                case 0x30 ... 0x4e: // one operand, reserved for future use
                        fgetc(fp);
                        break;

                case EOF:
                        break;
                default:
                        fprintf(stderr, "unknown opcode %02x\n", op);
                        op = EOF;
                        break;
                }
        } while (op != EOF);

        // make sure the last sample played is in our list
        if (smplength) {
                nsmp = sample_assign(samples, smpstart, smplength) ?: nsmp;
        }

        if (nsmp) {
                printf("%d samples in file\n", nsmp);
                save_it_file(fp, outname, datastart, datalength, samples, nsmp);
        } else {
                printf("no samples found!\n");
        }

        fclose(fp);
}

int main(int argc, char **argv)
{
        int i;

        if (argc < 2) {
                printf("usage: %s file.vgm ...\n", argv[0]);
                return 0;
        }

        for (i = 1; i < argc; i++) {
                char *it = malloc(strlen(argv[i]) + 4);
                if (!it) {
                        perror("malloc");
                        return 1;
                }
                puts(argv[i]);
                strcpy(it, argv[i]);
                strcat(it, ".it");
                rip_vgm(argv[i], it);
                free(it);
        }

        return 0;
}

