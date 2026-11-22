/* SPDX-License-Identifier: MPL-2.0 */
/* render a test of the engine to a WAV: render OUT.wav PATCHNAME|param=v,... [notes] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "juno.h"
static void wav(FILE *f, const float *x, uint32_t frames)
{
    uint32_t i, bytes = frames * 4u;
    uint32_t h[11] = {0x46464952u, 36u + bytes, 0x45564157u, 0x20746d66u, 16u, 0x00020001u, 44100u, 44100u * 4u,
                      0x00100004u, 0x61746164u, bytes};
    fwrite(h, 4, 11, f);
    for (i = 0; i < frames * 2u; i++) {
        float v = x[i] * 32767.0f;
        short s = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        fwrite(&s, 2, 1, f);
    }
}
int main(int argc, char **argv)
{
    juno_patch_t p = {{0}, "TEST", 0, 0, 0, {0}, {0}, {0}, {0}, 0, 0, 0, {0}, {0}};
    juno_perf_t perf = {6, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 64, 0};
    static float buf[44100 * 2 * 30];
    uint32_t pos = 0, i;
    char *spec, *tok;
    float peak = 0;
    clock_t c0;
    /* defaults: a plain saw patch */
    p.p[JP_DCO_RANGE] = 1; p.p[JP_SAW] = 1; p.p[JP_VCF_FREQ] = 90; p.p[JP_VCA_LEVEL] = 90; p.p[JP_ENV_S] = 127;
    p.p[JP_ENV_R] = 40; p.p[JP_HPF] = 1; p.p[JP_PWM_SRC] = 1; p.p[JP_LFO_RATE] = 64;
    if (argc < 3) return 1;
    spec = strdup(argv[2]);
    for (tok = strtok(spec, ","); tok; tok = strtok(0, ",")) {
        char *eq = strchr(tok, '=');
        int k;
        if (!eq) continue;
        *eq = 0;
        for (k = 0; k < JP_N; k++) if (!strcmp(tok, JUNO_PNAME[k])) p.p[k] = (uint8_t)atoi(eq + 1);
        if (!strcmp(tok, "voices")) perf.voices = atoi(eq + 1);
        if (!strcmp(tok, "mode")) p.mode = (uint8_t)atoi(eq + 1);
        if (!strcmp(tok, "drift")) p.drift = (uint8_t)atoi(eq + 1);
        if (!strcmp(tok, "unidet")) p.uni_detune = (uint8_t)atoi(eq + 1);
    }
    juno_init();
    juno_set_patch(&p);
    juno_set_perf(&perf);
    c0 = clock();
    /* the score: argv[3] = "n1 n2 n3 ..." played as a chord for 2 s then released 1 s; or a sequence with -seq */
    {
        const char *score = argc > 3 ? argv[3] : "48 55 60 64 67 72";
        int notes[16], nn = 0;
        char *s = strdup(score);
        for (tok = strtok(s, " "); tok && nn < 16; tok = strtok(0, " ")) notes[nn++] = atoi(tok);
        for (i = 0; i < (uint32_t)nn; i++) juno_note_on(notes[i], 100);
        for (i = 0; i < 2 * 44100 / 256; i++) { juno_render(buf + pos * 2, 256); pos += 256; }
        for (i = 0; i < (uint32_t)nn; i++) juno_note_off(notes[i]);
        for (i = 0; i < 44100 * 1.5 / 256; i++) { juno_render(buf + pos * 2, 256); pos += 256; }
    }
    fprintf(stderr, "cpu %.1f%% of real time (host)\n", 100.0 * (clock() - c0) / CLOCKS_PER_SEC / (pos / 44100.0));
    for (i = 0; i < pos * 2; i++) if (buf[i] > peak || -buf[i] > peak) peak = buf[i] > 0 ? buf[i] : -buf[i];
    fprintf(stderr, "peak %.3f\n", peak);
    FILE *f = fopen(argv[1], "wb");
    wav(f, buf, pos);
    fclose(f);
    return 0;
}
