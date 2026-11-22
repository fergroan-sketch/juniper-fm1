/* SPDX-License-Identifier: MPL-2.0 */
/* JUNIPER FM-1: a polysynth voice modelled on the Roland Juno-106's published design (its owner's manual and service
 * notes: one DCO per voice with saw, pulse/PWM, sub-octave square and noise; a high-pass after the voices; a resonant
 * four-pole low-pass per voice; one ADSR; one LFO with delay; a VCA in ENV or GATE mode; the stereo BBD chorus), with
 * performance features after superOS-106's (voice modes, chord memory, arpeggiator, velocity, a second envelope for
 * the filter, per-voice detune / vibrato / PWM, drift, scales, LFO sync to MIDI clock). Unofficial: not affiliated with
 * Roland or superOS. Pure C, single-precision float, no allocation: runs on the FM-1 and on a computer alike. */
#pragma once
#include <stdint.h>

#define JUNO_SR 44100.0f
#define JUNO_MAXV 10                    /* voices the engine has; the player picks how many are used (6 as the Juno) */
#define JUNO_NVP 6                      /* per-voice settings: six, as the Juno's voices (voice 7 uses voice 1's...) */
#define JUNO_NAME_LEN 12

/* the panel: every slider 0..127 as the Juno-106 keeps them, the switches by position */
enum { JP_LFO_RATE, JP_LFO_DELAY,
       JP_DCO_RANGE,                     /* 0 16', 1 8', 2 4' */
       JP_DCO_LFO, JP_DCO_PWM,
       JP_PWM_SRC,                       /* 0 LFO, 1 MANUAL, 2 ENV (the Juno-60's third position) */
       JP_SAW, JP_PULSE,                 /* 0 off, 1 on */
       JP_SUB, JP_NOISE,
       JP_HPF,                           /* 0 bass boost, 1 flat, 2, 3 */
       JP_VCF_FREQ, JP_VCF_RES, JP_VCF_ENV,
       JP_VCF_POL,                       /* 0 +, 1 - */
       JP_VCF_LFO, JP_VCF_KYBD,
       JP_VCA_MODE,                      /* 0 ENV, 1 GATE */
       JP_VCA_LEVEL,
       JP_ENV_A, JP_ENV_D, JP_ENV_S, JP_ENV_R,
       JP_CHORUS,                        /* 0 off, 1 I, 2 II, 3 I+II */
       JP_N };

/* the voice modes (superOS-106's, and its CC 22 numbers) */
enum { VM_POLY1, VM_POLY2, VM_MONO, VM_CHORD, VM_UNISON, VM_DUAL, VM_N };

/* a patch: the panel and its extras, as superOS-106 keeps them beside each patch. 80 bytes */
typedef struct {
    uint8_t p[JP_N];
    char name[JUNO_NAME_LEN];
    uint8_t mode;                        /* VM_* */
    uint8_t uni_detune;                  /* 0..15: voice 2/3 one step up/down, 4/5 two, 6 three up */
    uint8_t env2_on;                     /* the filter on its own envelope */
    uint8_t env2[4];                     /* its A D S R, 0..127 */
    int8_t vdet[JUNO_NVP];               /* per voice: detune, -64..63 in 1/128 semitone */
    int8_t vlfo[JUNO_NVP];               /* per voice: vibrato depth, -64..63 (63: about a semitone) */
    uint8_t vpwm[JUNO_NVP];              /* per voice: PWM from the detune LFO, 0..7 */
    uint8_t det_lfo;                     /* the detune LFO: 0 the panel LFO, 1..63 its own speed */
    uint8_t drift;                       /* 0..15 */
    uint8_t chord_n;                     /* chord memory: notes, 0 none */
    int8_t chord[6];                     /* their offsets from the lowest, in semitones */
    uint8_t rsv[80 - JP_N - JUNO_NAME_LEN - 3 - 4 - 3 * JUNO_NVP - 3 - 6];
} juno_patch_t;

/* the performance settings, kept apart from the patches */
typedef struct {
    uint8_t voices;                      /* 1 .. JUNO_MAXV */
    uint8_t bend_dco;                    /* semitones, 0..12 */
    uint8_t bend_vcf;                    /* 0..127: up to +-3.5 octaves */
    uint8_t vel_route;                   /* 0 off, 1 amp, 2 filter, 3 both */
    uint8_t vel_depth;                   /* 0..15 */
    uint8_t porta;                       /* 0..127 (an addition) */
    uint8_t scale;                       /* 0 equal, 1 Arabic, 2..5 user 1..4 */
    uint8_t lfo_sync;                    /* 0 off, 1 1/16, 2 1/8, 3 beat, 4 two beats, 5 bar */
    uint8_t arp_order;                   /* 0 up, 1 down, 2 up and down, 3 as played */
    uint8_t arp_range;                   /* 0..2: one to three octaves */
    uint8_t arp_first;                   /* 0..3 */
    uint8_t arp_rate;                    /* 0..127: 2 to 32 steps a second (internal clock) */
    uint8_t arp_gate;                    /* 0..127: 1..100% of the step */
    uint8_t clock_src;                   /* 0 internal, 1 MIDI clock, 2 auto (internal until MIDI clock comes) */
} juno_perf_t;

extern const uint8_t JUNO_MAX[JP_N];     /* each parameter's top value */
extern const char *const JUNO_PNAME[JP_N];

void juno_init(void);
void juno_set_patch(const juno_patch_t *p);   /* the patch the engine plays (main loop) */
void juno_set_param(int i, int v);       /* a panel parameter: main loop or MIDI */
int juno_param(int i);
juno_patch_t *juno_patch(void);          /* the engine's own copy: extras are changed here, then juno_patch_changed */
void juno_patch_changed(void);
void juno_set_perf(const juno_perf_t *f);
void juno_set_user_scale(int i, const uint8_t v[12]);   /* 64 = in tune, 1/128 semitone a step */

/* events: from the main loop or MIDI; queued and applied at the start of the next render */
void juno_note_on(int note, int vel);
void juno_note_off(int note);
void juno_all_off(void);                 /* release everything (and the arpeggiator's hold) */
void juno_panic(void);                   /* silence now */
void juno_bend(int v);                   /* -8192 .. 8191 */
void juno_mod(int v);                    /* mod wheel 0..127: LFO to the DCO, as the bender lever pushed */
void juno_sustain(int on);
void juno_arp(int state);                /* 0 off, 1 on, 2 on with hold */
void juno_chord_learn(void);             /* chord memory: the next chord played (all keys up) is kept */
void juno_clock(int rt);                 /* MIDI real time: 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop */

/* the render: n frames as floats about +-1 (out: L R L R ...) */
void juno_render(float *out, uint32_t n);

/* for the screen */
float juno_voice_level(int v);
int juno_voice_note(int v);
int juno_arp_state(void);
int juno_learning(void);                 /* 1: waiting for a chord to learn */
int juno_arp_step_lit(void);             /* 1 for a moment at each arpeggiator step */
int juno_clock_running(void);            /* MIDI clock is coming in */
