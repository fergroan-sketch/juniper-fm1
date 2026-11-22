/* SPDX-License-Identifier: MPL-2.0 */
/* JUNIPER FM-1's engine (juno.h).
 *
 * The voice is modelled on the Juno-106 as its service notes and owner's manual describe it: a DCO (a clocked
 * counter resetting a ramp; the pulse a comparator on that ramp; the sub-octave a flip-flop on its reset), the
 * IR3109-style four-pole OTA low-pass, a CPU-made envelope, one LFO with delay, a global high-pass after the voices,
 * and the MN3009 BBD chorus. The service notes' specifications set the ranges: LFO 0.1..30 Hz, delay 0..3 s; DCO LFO
 * +-400 cents; cutoff 5 Hz..50 kHz, envelope +-14 octaves, LFO and bender +-3.5 octaves, key follow +3/-2 octaves over
 * the keyboard; attack 1.5 ms..3 s, decay and release 1.5 ms..12 s. No code, tables or data from any other emulation.
 *
 * Signal flow, per voice:  DCO (saw + pulse + sub + noise) -> VCF (4-pole, resonant) -> VCA
 * then summed:              -> HPF -> chorus (stereo) -> out
 *
 * Control-rate parts (envelopes, LFOs, cutoff, pitch, the arpeggiator) run every CR samples, as the Juno's CPU
 * computed its envelopes at a few ms; cutoff and gain are ramped across each block, so nothing steps. */
#include "juno.h"
#include "fastmath.h"
#include "plat.h"                        /* PLAT_POOL: the big buffers in the pool RAM */

#define CR 32                           /* control-rate block: 0.73 ms */
#define INV_SR (1.0f / JUNO_SR)
#define CR_DT ((float)CR * INV_SR)

const uint8_t JUNO_MAX[JP_N] = {
    [JP_LFO_RATE] = 127, [JP_LFO_DELAY] = 127, [JP_DCO_RANGE] = 2, [JP_DCO_LFO] = 127, [JP_DCO_PWM] = 127,
    [JP_PWM_SRC] = 2, [JP_SAW] = 1, [JP_PULSE] = 1, [JP_SUB] = 127, [JP_NOISE] = 127, [JP_HPF] = 3,
    [JP_VCF_FREQ] = 127, [JP_VCF_RES] = 127, [JP_VCF_ENV] = 127, [JP_VCF_POL] = 1, [JP_VCF_LFO] = 127,
    [JP_VCF_KYBD] = 127, [JP_VCA_MODE] = 1, [JP_VCA_LEVEL] = 127, [JP_ENV_A] = 127, [JP_ENV_D] = 127,
    [JP_ENV_S] = 127, [JP_ENV_R] = 127, [JP_CHORUS] = 3,
};
const char *const JUNO_PNAME[JP_N] = {
    "LFO RATE", "LFO DELAY", "RANGE", "DCO LFO", "PWM", "PWM MODE", "SAW", "PULSE", "SUB", "NOISE", "HPF",
    "FREQ", "RES", "VCF ENV", "ENV POL", "VCF LFO", "KYBD", "VCA MODE", "LEVEL", "ATTACK", "DECAY", "SUSTAIN",
    "RELEASE", "CHORUS",
};

/* ------------------------------------------------------------------ state --- */
typedef struct {
    int stage;                           /* 0 idle, 1 attack, 2 decay/sustain, 3 release */
    float level;
} env_t;

typedef struct {
    int note;                            /* the pitch it plays (MIDI note), -1 free */
    int key;                             /* the key that started it (chord memory and dual: one key, several voices) */
    uint8_t gate, held;                  /* gate: the envelopes in A/D/S; held: its key is down (sustain aside) */
    uint32_t age;                        /* when it was given its note: the oldest is stolen first */
    float vel;                           /* 0..1 */
    float pitch, pitch_target;           /* semitones (MIDI note), glided by portamento */
    float uni;                           /* unison spread: semitones */
    float drift, drift_to;               /* drift: semitones now, and where it wanders to */
    uint32_t drift_t;                    /* control blocks until the next wander */
    float phase, inc;                    /* DCO: phase 0..1, per sample */
    float sub;                           /* the sub flip-flop: +1 / -1 */
    float pw;                            /* pulse width now, 0..1 of the cycle */
    env_t env, env2;
    float gate_amp;                      /* GATE mode's own quick ramp */
    float s[4];                          /* the VCF's four integrators */
    float g0, g1;                        /* the VCF's coefficient, at the block's start and end (ramped) */
    float a0, a1;                        /* the VCA's gain, the same */
    uint32_t rng;
} voice_t;

static PLAT_POOL voice_t V[JUNO_MAXV];
static juno_patch_t P;
static juno_perf_t F = {6, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 64, 2};
static uint32_t clock_age;
static int next_voice;                   /* POLY 1: the voices in turn, so release tails overlap */
static float tune[12];                   /* the scale: each pitch class's offset in semitones */
static uint8_t user_scale[4][12];

/* the panel LFO: one for all voices, a triangle, with a delay that restarts when a note comes after all keys are up */
static float lfo_phase, lfo_val, lfo_amt;   /* lfo_amt: the delay's fade-in, 0..1 */
static uint32_t lfo_delay_t;
static float dlfo_phase, dlfo_val;       /* the detune LFO (its own speed), a triangle */
static float bend, modw;                 /* -1..1, 0..1 */
static int sustain;

/* MIDI clock: for LFO sync and the arpeggiator */
static uint32_t sample_t;                /* samples since the start */
static uint32_t tick_last, tick_count;   /* when the last tick came; ticks since Start */
static float tick_len;                   /* samples a tick, smoothed (0: not known yet) */
static int clock_seen;                   /* a tick came in the last second */

/* events from the main loop: a single-producer ring, read in the render */
#define EVQ 128
static volatile uint32_t evq[EVQ];
static volatile uint32_t ev_w, ev_r;
enum { EV_ON = 1, EV_OFF, EV_ALLOFF, EV_PANIC, EV_BEND, EV_MOD, EV_SUS, EV_ARP, EV_LEARN, EV_RT };

static void ev_push(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t w = ev_w;
    if (w - ev_r >= EVQ)
        return;                          /* full: dropped */
    evq[w % EVQ] = type | (a & 0xFFFFu) << 8 | (b & 0xFFu) << 24;
    ev_w = w + 1u;
}

/* HPF (global) and chorus */
static float hp_x1, hp_y1, hp_shelf;
#define CHO_N 1024                       /* 23 ms of delay line: more than the chorus needs */
static PLAT_POOL float cho_buf[CHO_N];
static uint32_t cho_w;
static float cho_phase, cho_lp[4];       /* the BBD's anti-alias / reconstruction filters, L and R (two poles) */
static float cho_mix;                    /* 0..1: the wet path faded in and out as the mode changes */

/* --------------------------------------------------------------- curves --- */
static float slider(int i) { return (float)P.p[i] * (1.0f / 127.0f); }
static float lfo_hz(void) { return 0.1f * fm_exp2f(slider(JP_LFO_RATE) * 8.229f); }   /* 0.1 .. 30 Hz */
static float lfo_delay_s(void) { float d = slider(JP_LFO_DELAY); return 3.0f * d * d; }
/* envelope times in seconds: 1.5 ms .. top, exponential on the slider */
static float env_time(int v, float top)
{
    return 0.0015f * fm_exp2f((float)v * (1.0f / 127.0f) * fm_log2f(top / 0.0015f));
}

/* the per-control-block coefficient of an exponential approach that is 99% there in t */
static float env_coef(float t)
{
    float k = CR_DT * 4.6f / t;
    return k >= 1.0f ? 1.0f : 1.0f - fm_expf(-k);
}

static void scale_set(void)
{
    int i;
    for (i = 0; i < 12; i++)
        tune[i] = 0.0f;
    if (F.scale == 1) {                  /* Arabic (maqam Rast on C): E and B a quarter tone down */
        tune[4] = -0.5f;
        tune[11] = -0.5f;
    } else if (F.scale >= 2 && F.scale <= 5) {
        for (i = 0; i < 12; i++)
            tune[i] = ((float)user_scale[F.scale - 2][i] - 64.0f) * (1.0f / 128.0f);
    }
}

/* ---------------------------------------------------------------- noise --- */
static inline float rnd(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return (float)(int32_t)*s * (1.0f / 2147483648.0f);
}

/* the polyBLEP correction for a step of height h: t, the time since it (0..1 samples), or for the sample before it,
 * t = 1 - the time to it */
static inline float step_after(float h, float t) { return -0.5f * h * (1.0f - t) * (1.0f - t); }
static inline float step_before(float h, float t) { return 0.5f * h * t * t; }

/* cheap tanh for the filter's input: Pade (3,2), exact enough below 3, held at +-1 beyond */
static inline float sat(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---------------------------------------------------------------- voices --- */
static int nvoices(void)
{
    if (P.mode == VM_MONO)
        return 1;
    return F.voices < 1 ? 1 : F.voices > JUNO_MAXV ? JUNO_MAXV : F.voices;
}

static void voice_start(voice_t *v, int key, int note, float vel, int retrigger)
{
    v->key = key;
    v->note = note;
    v->held = 1;
    v->age = ++clock_age;
    v->vel = vel;
    v->pitch_target = (float)note;
    if (F.porta == 0 || v->env.stage == 0)
        v->pitch = (float)note;
    if (retrigger || v->env.stage == 0 || v->env.stage == 3) {
        v->gate = 1;
        v->env.stage = 1;                /* the attack from where it is, as the Juno does: no click at retrigger */
        v->env2.stage = 1;
    }
}

static void voice_release(voice_t *v)
{
    v->held = 0;
    if (!sustain) {
        v->gate = 0;
        if (v->env.stage)
            v->env.stage = 3;
        if (v->env2.stage)
            v->env2.stage = 3;
    }
}

static int held_count(void)
{
    int i, n = 0;
    for (i = 0; i < JUNO_MAXV; i++)
        n += V[i].held;
    return n;
}

/* a free voice among the first n: POLY 1 in turn from the last one given, POLY 2 the lowest; else the oldest
 * released, else the oldest */
static int voice_pick(int n, int in_turn)
{
    int k, best = -1;
    uint32_t oldest = 0xFFFFFFFFu;
    for (k = 0; k < n; k++) {
        int j = in_turn ? (next_voice + k) % n : k;
        if (V[j].env.stage == 0 && V[j].a0 == 0.0f) {
            best = j;
            break;
        }
    }
    if (best < 0)
        for (k = 0; k < n; k++)
            if (!V[k].held && V[k].age < oldest) {
                oldest = V[k].age;
                best = k;
            }
    if (best < 0)
        for (k = 0; k < n; k++)
            if (V[k].age < oldest) {
                oldest = V[k].age;
                best = k;
            }
    if (in_turn)
        next_voice = (best + 1) % n;
    return best;
}

/* mono and unison keep the keys held: a release returns to the highest one still down (superOS-106's note memory) */
static int8_t keys_down[16];
static int nkeys;

static void keys_add(int note)
{
    int i;
    for (i = 0; i < nkeys; i++)
        if (keys_down[i] == note)
            return;
    if (nkeys < 16)
        keys_down[nkeys++] = (int8_t)note;
}
static void keys_remove(int note)
{
    int i, j;
    for (i = 0; i < nkeys; i++)
        if (keys_down[i] == note) {
            for (j = i; j < nkeys - 1; j++)
                keys_down[j] = keys_down[j + 1];
            nkeys--;
            return;
        }
}

/* chord memory: the chord being learnt */
static int learning;
static int8_t learn_notes[6];
static int learn_n, learn_down;

static void learn_key(int note, int on)
{
    int i;
    if (on) {
        learn_down++;
        if (learn_n < 6) {
            for (i = 0; i < learn_n; i++)
                if (learn_notes[i] == note)
                    return;
            learn_notes[learn_n++] = (int8_t)note;
        }
        return;
    }
    if (--learn_down > 0 || learn_n == 0)
        return;
    {                                    /* all keys up: the chord, from its lowest note */
        int lo = 127, a, b;
        for (i = 0; i < learn_n; i++)
            if (learn_notes[i] < lo)
                lo = learn_notes[i];
        for (i = 0; i < learn_n; i++)
            learn_notes[i] = (int8_t)(learn_notes[i] - lo);
        for (a = 0; a < learn_n; a++)    /* in order, lowest first */
            for (b = a + 1; b < learn_n; b++)
                if (learn_notes[b] < learn_notes[a]) {
                    int8_t t = learn_notes[a];
                    learn_notes[a] = learn_notes[b];
                    learn_notes[b] = t;
                }
        for (i = 0; i < 6; i++)
            P.chord[i] = (int8_t)(i < learn_n ? learn_notes[i] : 0);
        P.chord_n = (uint8_t)learn_n;
        learning = 0;
        learn_down = 0;
    }
}

/* the voice modes: a key down and up, from the keyboard, MIDI or the arpeggiator */
static void play_on(int note, int vel)
{
    int i, n = nvoices();
    float fv = (float)vel * (1.0f / 127.0f);
    if (held_count() == 0 && nkeys == 0)
        lfo_delay_t = 0;                 /* the LFO's delay starts again */
    if (learning)
        learn_key(note, 1);
    switch (P.mode) {
    case VM_MONO:
    case VM_UNISON:
        keys_add(note);
        for (i = 0; i < n; i++)
            voice_start(&V[i], note, note, fv, 1);
        return;
    case VM_DUAL: {                      /* a note on a pair of voices: up to n/2 notes, more are not played */
        int pairs = n / 2, k;
        for (k = 0; k < pairs; k++)
            if (V[2 * k].key == note && V[2 * k].env.stage) {
                voice_start(&V[2 * k], note, note, fv, 1);
                voice_start(&V[2 * k + 1], note, note, fv, 1);
                return;
            }
        for (k = 0; k < pairs; k++)
            if (!V[2 * k].held) {
                voice_start(&V[2 * k], note, note, fv, 1);
                voice_start(&V[2 * k + 1], note, note, fv, 1);
                return;
            }
        return;
    }
    case VM_POLY2:
        for (i = 0; i < n; i++)          /* the same note again: its own voice */
            if (V[i].key == note && V[i].env.stage) {
                voice_start(&V[i], note, note, fv, 1);
                return;
            }
        voice_start(&V[voice_pick(n, 0)], note, note, fv, 1);
        return;
    case VM_CHORD:
        if (P.chord_n && !learning) {    /* the chord from this key; the last chord cut */
            int k;
            for (i = 0; i < n; i++)
                if (V[i].held)
                    voice_release(&V[i]);
            for (k = 0; k < P.chord_n; k++) {
                int j = voice_pick(n, 1);
                voice_start(&V[j], note, note + P.chord[k], fv, 1);
            }
            return;
        }
        voice_start(&V[voice_pick(n, 1)], note, note, fv, 1);   /* no chord yet: single notes */
        return;
    default:                             /* POLY 1 */
        voice_start(&V[voice_pick(n, 1)], note, note, fv, 1);
        return;
    }
}

static void play_off(int note)
{
    int i, n = nvoices();
    if (learning)
        learn_key(note, 0);
    if (P.mode == VM_MONO || P.mode == VM_UNISON) {
        int sounding = V[0].held ? V[0].key : -1;
        keys_remove(note);
        if (note == sounding && nkeys > 0) {   /* back to the highest key still down: the pitch only */
            int hi = 0, k;
            for (k = 1; k < nkeys; k++)
                if (keys_down[k] > keys_down[hi])
                    hi = k;
            for (i = 0; i < n; i++)
                voice_start(&V[i], keys_down[hi], keys_down[hi], V[i].vel, 0);
            return;
        }
        if (note != sounding)
            return;
    }
    for (i = 0; i < JUNO_MAXV; i++)
        if (V[i].key == note && V[i].held)
            voice_release(&V[i]);
}

/* ----------------------------------------------------------- arpeggiator --- */
static int arp_on;                       /* 0 off, 1 on, 2 hold */
static int8_t arp_keys[16];              /* in the order played */
static uint8_t arp_nk, arp_down;         /* keys in the pattern; keys physically down */
static uint8_t arp_vel = 100;
static int arp_pos, arp_note = -1, arp_restart;
static float arp_acc, arp_gate_left;     /* internal clock: steps; the sounding step's time left, s */
static uint32_t arp_tick;                /* MIDI clock ticks since the last step */
static volatile int arp_lit;

static void arp_key(int note, int vel, int on)
{
    int i, j;
    if (on) {
        if (arp_down == 0) {             /* a chord after all keys were up: it starts the pattern again */
            if (arp_on == 2)
                arp_nk = 0;              /* and with hold, replaces the one held */
            arp_restart = 1;
        }
        arp_down++;
        arp_vel = (uint8_t)vel;
        for (i = 0; i < arp_nk; i++)
            if (arp_keys[i] == note)
                return;
        if (arp_nk < 16)
            arp_keys[arp_nk++] = (int8_t)note;
        return;
    }
    if (arp_down)
        arp_down--;
    if (arp_on == 2)
        return;                          /* held */
    for (i = 0; i < arp_nk; i++)
        if (arp_keys[i] == note) {
            for (j = i; j < arp_nk - 1; j++)
                arp_keys[j] = arp_keys[j + 1];
            arp_nk--;
            return;
        }
}

/* the pattern: the held keys in order, then an octave up..., up-and-down without repeating the ends */
static int arp_len(void)
{
    int n = arp_nk * (F.arp_range + 1);
    if (F.arp_order == 2 && n > 2)
        return 2 * n - 2;
    return n;
}
static int arp_note_at(int k)
{
    int8_t s[16];
    int i, j, n = arp_nk, total = n * (F.arp_range + 1), idx, note;
    for (i = 0; i < n; i++)
        s[i] = arp_keys[i];
    if (F.arp_order != 3)                /* by pitch */
        for (i = 0; i < n; i++)
            for (j = i + 1; j < n; j++)
                if (s[j] < s[i]) {
                    int8_t t = s[i];
                    s[i] = s[j];
                    s[j] = t;
                }
    if (F.arp_order == 1)
        idx = total - 1 - k;
    else if (F.arp_order == 2 && total > 2)
        idx = k < total ? k : 2 * total - 2 - k;
    else
        idx = k;
    note = s[idx % n] + 12 * (idx / n);
    while (note > 127)
        note -= 12;                      /* notes above the top drop an octave */
    return note;
}

static void arp_release_note(void)
{
    if (arp_note >= 0) {
        play_off(arp_note);
        arp_note = -1;
    }
}

static void arp_step(float step_s)
{
    int len = arp_len();
    arp_release_note();
    if (arp_nk == 0 || len == 0)
        return;
    if (arp_restart) {
        arp_pos = F.arp_first % len;
        arp_restart = 0;
    } else {
        arp_pos = (arp_pos + 1) % len;
    }
    arp_note = arp_note_at(arp_pos);
    play_on(arp_note, arp_vel);
    arp_gate_left = step_s * (0.01f + 0.99f * (float)F.arp_gate / 127.0f);
    arp_lit = 1;
}

static int use_midi_clock(void) { return F.clock_src == 1 || (F.clock_src == 2 && clock_seen); }

static void arp_control(void)
{
    float rate = 2.0f * fm_exp2f((float)F.arp_rate * (4.0f / 127.0f));   /* 2 .. 32 steps a second */
    if (!arp_on)
        return;
    if (arp_note >= 0) {
        arp_gate_left -= CR_DT;
        if (arp_gate_left <= 0.0f)
            arp_release_note();
    }
    if (use_midi_clock())
        return;                          /* steps on the ticks (midi_rt) */
    if (arp_nk == 0) {
        arp_acc = 0.999f;                /* the first step comes at once */
        return;
    }
    arp_acc += rate * CR_DT;
    if (arp_acc >= 1.0f) {
        arp_acc -= 1.0f;
        if (arp_acc > 1.0f)
            arp_acc = 0.0f;
        arp_step(1.0f / rate);
    }
}

/* ------------------------------------------------------------------ input --- */
static void key_on(int note, int vel)
{
    if (arp_on)
        arp_key(note, vel, 1);
    else
        play_on(note, vel);
}
static void key_off(int note)
{
    if (arp_on)
        arp_key(note, 0, 0);
    else
        play_off(note);
}

static void do_sustain(int on)
{
    int i;
    sustain = on;
    if (!on)
        for (i = 0; i < JUNO_MAXV; i++)
            if (!V[i].held && V[i].gate) {
                V[i].gate = 0;
                if (V[i].env.stage)
                    V[i].env.stage = 3;
                if (V[i].env2.stage)
                    V[i].env2.stage = 3;
            }
}

static void do_all_off(void)
{
    int i;
    nkeys = 0;
    arp_nk = arp_down = 0;
    arp_note = -1;
    learn_down = 0;
    for (i = 0; i < JUNO_MAXV; i++) {
        V[i].held = 0;
        V[i].gate = 0;
        if (V[i].env.stage)
            V[i].env.stage = 3;
        if (V[i].env2.stage)
            V[i].env2.stage = 3;
    }
}

static void do_panic(void)
{
    int i;
    do_all_off();
    for (i = 0; i < JUNO_MAXV; i++) {
        voice_t *v = &V[i];
        v->note = v->key = -1;
        v->env.stage = v->env2.stage = 0;
        v->env.level = v->env2.level = v->gate_amp = v->a0 = v->a1 = 0.0f;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;
    }
}

static void midi_rt(int rt)
{
    if (rt == 0xF8) {
        uint32_t d = sample_t - tick_last;
        if (clock_seen && d > 0u && d < (uint32_t)(JUNO_SR * 0.25f))   /* above 10 BPM: a tempo to follow */
            tick_len = tick_len > 0.0f ? tick_len + ((float)d - tick_len) * 0.15f : (float)d;
        tick_last = sample_t;
        clock_seen = 1;
        tick_count++;
        if (arp_on && use_midi_clock() && ++arp_tick >= 6u) {   /* sixteenths */
            arp_tick = 0;
            arp_step(tick_len > 0.0f ? 6.0f * tick_len * INV_SR : 0.125f);
        }
    } else if (rt == 0xFA) {             /* Start: the first tick after it is the downbeat */
        tick_count = 0;
        arp_tick = 5;
        arp_restart = 1;
    } else if (rt == 0xFC) {             /* Stop */
        if (arp_on && use_midi_clock())
            arp_release_note();
    }
}

static void events(void)
{
    while (ev_r != ev_w) {
        uint32_t e = evq[ev_r % EVQ], t = e & 0xFFu, a = (e >> 8) & 0xFFFFu, b = e >> 24;
        ev_r++;
        switch (t) {
        case EV_ON: key_on((int)a, (int)b); break;
        case EV_OFF: key_off((int)a); break;
        case EV_ALLOFF: do_all_off(); break;
        case EV_PANIC: do_panic(); break;
        case EV_BEND: bend = ((float)a - 8192.0f) * (1.0f / 8192.0f); break;
        case EV_MOD: modw = (float)b * (1.0f / 127.0f); break;
        case EV_SUS: do_sustain((int)b); break;
        case EV_ARP:
            if ((int)b != arp_on) {
                if (!b || !arp_on)       /* on or off: no note left hanging */
                    do_all_off();
                else if (b == 1 && arp_down == 0) {   /* hold let go with no key down: the held chord ends */
                    arp_release_note();
                    arp_nk = 0;
                }
                arp_on = (int)b;
                arp_restart = 1;
            }
            break;
        case EV_LEARN:
            learning = 1;
            learn_n = learn_down = 0;
            break;
        case EV_RT: midi_rt((int)b); break;
        default: break;
        }
    }
}

/* ----------------------------------------------------------- control rate --- */
static void env_run(env_t *e, float a_step, float decay, float sus, float release)
{
    if (e->stage == 1) {                 /* the attack: a straight rise, as the Juno's */
        e->level += a_step;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = 2;
        }
    } else if (e->stage == 2) {
        e->level += (sus - e->level) * decay;
    } else if (e->stage == 3) {
        e->level -= e->level * release;
        if (e->level < 0.0002f)
            e->level = 0.0f;
    }
}

static void lfo_control(void)
{
    if (F.lfo_sync && clock_seen && tick_len > 0.0f && sample_t - tick_last < (uint32_t)(JUNO_SR * 0.5f)) {
        static const uint8_t CLOCKS[6] = {0, 6, 12, 24, 48, 96};
        uint32_t n = CLOCKS[F.lfo_sync > 5 ? 5 : F.lfo_sync];
        float frac = (float)(sample_t - tick_last) / tick_len;
        lfo_phase = ((float)(tick_count % n) + (frac < 1.0f ? frac : 1.0f)) / (float)n;
    } else {
        lfo_phase += lfo_hz() * CR_DT;
        if (lfo_phase >= 1.0f)
            lfo_phase -= 1.0f;
    }
    lfo_val = lfo_phase < 0.5f ? 4.0f * lfo_phase - 1.0f : 3.0f - 4.0f * lfo_phase;
    {
        float d = lfo_delay_s(), t = (float)lfo_delay_t * CR_DT;
        lfo_delay_t++;
        lfo_amt = d <= 0.001f ? 1.0f : fm_clampf((t - 0.5f * d) / (0.5f * d), 0.0f, 1.0f);
    }
    if (P.det_lfo) {                     /* the detune LFO's own speed: 0.2 .. 13 Hz, no delay */
        dlfo_phase += 0.2f * fm_exp2f((float)P.det_lfo * (6.0f / 63.0f)) * CR_DT;
        if (dlfo_phase >= 1.0f)
            dlfo_phase -= 1.0f;
        dlfo_val = dlfo_phase < 0.5f ? 4.0f * dlfo_phase - 1.0f : 3.0f - 4.0f * dlfo_phase;
    } else {
        dlfo_val = lfo_val * lfo_amt;
    }
}

static void control(void)
{
    static const int8_t UNI[6] = {0, 1, -1, 2, -2, 3};   /* unison detune: steps for voices 1..6 */
    int i, n = nvoices();
    float a_step = CR_DT / env_time(P.p[JP_ENV_A], 3.0f), decay = env_coef(env_time(P.p[JP_ENV_D], 12.0f));
    float release = env_coef(env_time(P.p[JP_ENV_R], 12.0f)), sus = slider(JP_ENV_S);
    float a2 = CR_DT / env_time(P.env2[0], 3.0f), d2 = env_coef(env_time(P.env2[1], 12.0f));
    float r2 = env_coef(env_time(P.env2[3], 12.0f)), s2 = (float)P.env2[2] * (1.0f / 127.0f);
    float range = P.p[JP_DCO_RANGE] == 0 ? -12.0f : P.p[JP_DCO_RANGE] == 2 ? 12.0f : 0.0f;
    float lfo, dco_lfo, lfo_semi, bend_semi, porta, freq, envamt, lfo_vcf, kybd, level, pwm, step_uni;
    float vdepth = (float)F.vel_depth * (0.97f / 15.0f);
    lfo_control();
    arp_control();
    lfo = lfo_val * lfo_amt;
    dco_lfo = slider(JP_DCO_LFO) + modw;
    lfo_semi = lfo * (dco_lfo > 1.0f ? 1.0f : dco_lfo * dco_lfo) * 4.0f;   /* up to +-400 cents */
    bend_semi = bend * (float)F.bend_dco;
    porta = F.porta ? env_coef(0.002f + 2.0f * (float)F.porta * (float)F.porta / 16129.0f) : 1.0f;
    freq = slider(JP_VCF_FREQ);
    envamt = slider(JP_VCF_ENV) * (P.p[JP_VCF_POL] ? -1.0f : 1.0f);
    lfo_vcf = lfo * slider(JP_VCF_LFO) * 3.5f;   /* octaves */
    kybd = slider(JP_VCF_KYBD);
    level = slider(JP_VCA_LEVEL);
    pwm = slider(JP_DCO_PWM);
    step_uni = (float)P.uni_detune * 0.015f;     /* semitones a step: 15 is 22 cents */
    for (i = 0; i < JUNO_MAXV; i++) {
        voice_t *v = &V[i];
        int vp = i % JUNO_NVP;
        float e, e_f, cut_oct, fc, g, amp, semis, pw_amt, vf;
        if (i >= n && v->env.stage) {    /* fewer voices than before: let the extra ones go */
            v->held = v->gate = 0;
            v->env.stage = 3;
            if (v->env2.stage)
                v->env2.stage = 3;
        }
        v->g0 = v->g1;
        v->a0 = v->a1;
        env_run(&v->env, a_step, decay, sus, release);
        env_run(&v->env2, a2, d2, s2, r2);
        v->gate_amp += ((v->gate ? 1.0f : 0.0f) - v->gate_amp) * 0.3f;
        e = v->env.level;
        if (v->env.stage == 3 && e == 0.0f && (P.p[JP_VCA_MODE] == 0 || v->gate_amp < 0.0005f)) {
            v->env.stage = 0;            /* done: free */
            v->env2.stage = 0;
            v->env2.level = 0.0f;
            v->note = v->key = -1;
        }
        /* drift: a small step every few seconds, within a band (drift 4: about 3 cents; 15: about 11) */
        if (P.drift) {
            if (v->drift_t == 0) {
                float band = (float)P.drift * 0.0075f;
                v->drift_to = band * (rnd(&v->rng) - 0.1f);
                v->drift_t = (uint32_t)((2.0f + 3.0f * (0.5f + 0.5f * rnd(&v->rng))) / CR_DT);
            } else {
                v->drift_t--;
            }
            v->drift += (v->drift_to - v->drift) * 0.0007f;
        } else {
            v->drift = 0.0f;
        }
        v->uni = step_uni * (float)UNI[vp];
        /* pitch */
        v->pitch += (v->pitch_target - v->pitch) * porta;
        semis = v->pitch + range + lfo_semi + bend_semi + v->uni + v->drift
              + (float)P.vdet[vp] * (1.0f / 128.0f) + (float)P.vlfo[vp] * (1.0f / 64.0f) * dlfo_val
              + (v->note >= 0 ? tune[v->note % 12] : 0.0f);
        v->inc = 440.0f * fm_exp2f((semis - 69.0f) * (1.0f / 12.0f)) * INV_SR;
        if (v->inc > 0.45f)
            v->inc = 0.45f;
        /* PWM: 50% down to about 5% at the top; plus the voice's own swing from the detune LFO */
        pw_amt = P.p[JP_PWM_SRC] == 0 ? pwm * (0.5f + 0.5f * lfo_val)   /* the LFO's own swing, not delayed */
               : P.p[JP_PWM_SRC] == 1 ? pwm
               : pwm * e;
        v->pw = 0.5f - 0.45f * pw_amt + (float)P.vpwm[vp] * (0.11f / 7.0f) * dlfo_val;
        v->pw = fm_clampf(v->pw, 0.03f, 0.97f);
        /* velocity: it only takes away (as superOS-106): 127 gives the panel's setting */
        vf = 1.0f - vdepth * (1.0f - v->vel);
        /* the cutoff, in octaves above 5 Hz: the slider spans 5 Hz .. 50 kHz, the envelope up to 14 octaves either
         * way, the keyboard up to an octave per octave about C4 (+3 / -2 octaves over a 61-key keyboard) */
        e_f = P.env2_on ? v->env2.level : e;
        if (F.vel_route & 2u)
            e_f *= vf;
        cut_oct = freq * 13.3f + envamt * e_f * 14.0f + lfo_vcf + kybd * (v->pitch - 60.0f) * (1.0f / 12.0f)
                + bend * (float)F.bend_vcf * (3.5f / 127.0f);
        fc = 5.0f * fm_exp2f(fm_clampf(cut_oct, 0.0f, 13.3f));
        if (fc > 0.42f * JUNO_SR)
            fc = 0.42f * JUNO_SR;
        g = fm_tanf(FM_PI * fc * INV_SR);
        v->g1 = g / (1.0f + g);
        if (v->env.stage == 1 && v->a0 == 0.0f)
            v->g0 = v->g1;               /* a voice just started: no ramp from the last note's cutoff */
        /* the VCA */
        amp = P.p[JP_VCA_MODE] ? v->gate_amp : e * ((F.vel_route & 1u) ? vf : 1.0f);
        amp *= level * level * 1.6f;
        if (v->env.stage == 0)
            amp = 0.0f;
        v->a1 = amp;
    }
}

/* ------------------------------------------------------------- the voice --- */
static void voice_render(voice_t *v, float *mono, int ns)
{
    int i;
    float res = slider(JP_VCF_RES);
    float k = 4.3f * res * (0.85f + 0.15f * res);   /* feedback: self-oscillation in the top steps */
    float saw_on = P.p[JP_SAW] ? 1.0f : 0.0f, pulse_on = P.p[JP_PULSE] ? 1.0f : 0.0f;
    float sub = slider(JP_SUB), noise = slider(JP_NOISE) * 0.9f;
    float comp = 1.0f + 0.35f * k;       /* the Juno loses some bass with resonance; partly made up, not all */
    float dg = (v->g1 - v->g0) * (1.0f / (float)CR), da = (v->a1 - v->a0) * (1.0f / (float)CR);
    float G = v->g0, A = v->a0;
    float ph = v->phase, inc = v->inc, pw = v->pw, sq = v->sub;
    float s0 = v->s[0], s1 = v->s[1], s2 = v->s[2], s3 = v->s[3];
    for (i = 0; i < ns; i++) {
        float x, t, saw, pul, sb, u, G2, G3, G4, sig, y4, y;
        G += dg;
        A += da;
        ph += inc;
        /* the DCO: a ramp reset at the counter's end; the pulse, the ramp against the width; the sub, a flip-flop
         * toggled by the reset */
        if (ph >= 1.0f) {                /* the reset, t samples ago */
            ph -= 1.0f;
            t = ph / inc;
            sq = -sq;
            saw = 2.0f * ph - 1.0f + step_after(-2.0f, t);
            pul = (ph < pw ? 1.0f : -1.0f) + step_after(2.0f, t);
            sb = sq + step_after(2.0f * sq, t);
        } else {
            saw = 2.0f * ph - 1.0f;
            pul = ph < pw ? 1.0f : -1.0f;
            sb = sq;
            if (ph + inc >= 1.0f) {      /* a reset within the next sample */
                t = (ph + inc - 1.0f) / inc;
                saw += step_before(-2.0f, t);
                pul += step_before(2.0f, t);
                sb += step_before(-2.0f * sq, t);
            }
        }
        if (ph >= pw && ph - inc < pw)   /* the pulse's falling edge at pw, crossed in this sample */
            pul += step_after(-2.0f, (ph - pw) / inc);
        else if (ph < pw && ph + inc >= pw)
            pul += step_before(-2.0f, (ph + inc - pw) / inc);
        x = 0.5f * (saw_on * saw + pulse_on * pul) + 0.5f * sub * sb + noise * rnd(&v->rng);
        x = x * comp + 1e-4f * rnd(&v->rng);   /* a little hiss, as an analog filter has: lets it self-oscillate */
        /* the VCF: four one-pole stages, zero-delay feedback, the input stage saturating */
        G2 = G * G;
        G3 = G2 * G;
        G4 = G2 * G2;
        sig = (1.0f - G) * (G3 * s0 + G2 * s1 + G * s2 + s3);
        y4 = (G4 * x + sig) / (1.0f + k * G4);
        u = sat(x - k * y4);
        y = G * (u - s0) + s0; s0 = 2.0f * y - s0; u = y;
        y = G * (u - s1) + s1; s1 = 2.0f * y - s1; u = y;
        y = G * (u - s2) + s2; s2 = 2.0f * y - s2; u = y;
        y = G * (u - s3) + s3; s3 = 2.0f * y - s3;
        mono[i] += y * A;
    }
    v->phase = ph;
    v->sub = sq;
    v->s[0] = fm_flush(s0);
    v->s[1] = fm_flush(s1);
    v->s[2] = fm_flush(s2);
    v->s[3] = fm_flush(s3);
}

/* --------------------------------------------------------- HPF and chorus --- */
static void hpf_render(float *m, int ns)
{
    int i, pos = P.p[JP_HPF];
    if (pos == 0) {                      /* bass boost: a low shelf, about +6 dB below 120 Hz */
        float c = 1.0f - fm_expf(-FM_TWO_PI * 120.0f * INV_SR);
        for (i = 0; i < ns; i++) {
            hp_shelf += (m[i] - hp_shelf) * c;
            m[i] += hp_shelf;
        }
    } else if (pos >= 2) {               /* a one-pole high-pass at about 240 Hz or 720 Hz */
        float fc = pos == 2 ? 240.0f : 720.0f, a = fm_expf(-FM_TWO_PI * fc * INV_SR);
        for (i = 0; i < ns; i++) {
            float y = a * (hp_y1 + m[i] - hp_x1);
            hp_x1 = m[i];
            hp_y1 = y;
            m[i] = y;
        }
    }
}

/* the chorus: one bucket-brigade line read at two taps, swept in opposite directions by one triangle, each added
 * to the dry signal (left and right), the wet path band-limited as a BBD's filters do */
static void chorus_render(const float *m, float *out, int ns)
{
    int i, mode = P.p[JP_CHORUS];
    float rate, centre, depth, target = mode ? 1.0f : 0.0f;
    if (mode == 1) {
        rate = 0.513f; centre = 3.5f; depth = 1.85f;
    } else if (mode == 2) {
        rate = 0.863f; centre = 3.5f; depth = 1.85f;
    } else {                             /* I+II: a fast, shallow sweep; also the fade-out's settings */
        rate = 9.75f; centre = 3.3f; depth = 0.25f;
    }
    {
        float inc = rate * INV_SR, lpc = 1.0f - fm_expf(-FM_TWO_PI * 9000.0f * INV_SR);
        float c = centre * 0.001f * JUNO_SR, d = depth * 0.001f * JUNO_SR;
        for (i = 0; i < ns; i++) {
            float tri, dl, dr, wl, wr, fl, fr;
            uint32_t il, ir;
            cho_mix += (target - cho_mix) * 0.0005f;
            cho_buf[cho_w & (CHO_N - 1)] = m[i];
            cho_phase += inc;
            if (cho_phase >= 1.0f)
                cho_phase -= 1.0f;
            tri = cho_phase < 0.5f ? 4.0f * cho_phase - 1.0f : 3.0f - 4.0f * cho_phase;
            dl = c + d * tri;
            dr = c - d * tri;
            il = (uint32_t)dl;
            ir = (uint32_t)dr;
            fl = dl - (float)il;
            fr = dr - (float)ir;
            {
                float a0 = cho_buf[(cho_w - il) & (CHO_N - 1)], a1 = cho_buf[(cho_w - il - 1u) & (CHO_N - 1)];
                float b0 = cho_buf[(cho_w - ir) & (CHO_N - 1)], b1 = cho_buf[(cho_w - ir - 1u) & (CHO_N - 1)];
                wl = a0 + (a1 - a0) * fl;
                wr = b0 + (b1 - b0) * fr;
            }
            cho_w++;
            cho_lp[0] += (wl - cho_lp[0]) * lpc;
            cho_lp[1] += (cho_lp[0] - cho_lp[1]) * lpc;
            cho_lp[2] += (wr - cho_lp[2]) * lpc;
            cho_lp[3] += (cho_lp[2] - cho_lp[3]) * lpc;
            {
                float wet = cho_mix * 0.8f, dry = 1.0f - 0.2f * cho_mix;
                out[2 * i] = dry * m[i] + wet * cho_lp[1];
                out[2 * i + 1] = dry * m[i] + wet * cho_lp[3];
            }
        }
    }
}

/* ------------------------------------------------------------------- API --- */
void juno_init(void)
{
    int i, s, k;
    for (i = 0; i < JUNO_MAXV; i++) {
        V[i].note = V[i].key = -1;
        V[i].sub = 1.0f;
        V[i].rng = 0x9E3779B9u * (uint32_t)(i + 1);
        V[i].phase = (float)i * 0.137f;  /* the DCOs are not in phase with each other */
        V[i].drift_t = (uint32_t)i * 300u;
    }
    for (s = 0; s < 4; s++)
        for (k = 0; k < 12; k++)
            user_scale[s][k] = 64;
    scale_set();
}

static void patch_clamp(void)
{
    int i;
    for (i = 0; i < JP_N; i++)
        if (P.p[i] > JUNO_MAX[i])
            P.p[i] = JUNO_MAX[i];
    if (P.mode >= VM_N)
        P.mode = VM_POLY1;
    if (P.uni_detune > 15)
        P.uni_detune = 15;
    if (P.drift > 15)
        P.drift = 15;
    if (P.det_lfo > 63)
        P.det_lfo = 63;
    if (P.chord_n > 6)
        P.chord_n = 6;
    for (i = 0; i < 4; i++)
        if (P.env2[i] > 127)
            P.env2[i] = 127;
    for (i = 0; i < JUNO_NVP; i++)
        if (P.vpwm[i] > 7)
            P.vpwm[i] = 7;
}

void juno_set_patch(const juno_patch_t *p)
{
    int mode_was = P.mode;
    P = *p;
    patch_clamp();
    if (P.mode != mode_was)
        ev_push(EV_ALLOFF, 0, 0);        /* a voice mode change: nothing left hanging */
}
void juno_set_param(int i, int v)
{
    if (i >= 0 && i < JP_N)
        P.p[i] = (uint8_t)(v < 0 ? 0 : v > JUNO_MAX[i] ? JUNO_MAX[i] : v);
}
int juno_param(int i) { return i >= 0 && i < JP_N ? P.p[i] : 0; }
juno_patch_t *juno_patch(void) { return &P; }
void juno_patch_changed(void) { patch_clamp(); }

void juno_set_perf(const juno_perf_t *f)
{
    F = *f;
    scale_set();
}
void juno_set_user_scale(int i, const uint8_t v[12])
{
    int k;
    if (i < 0 || i > 3)
        return;
    for (k = 0; k < 12; k++)
        user_scale[i][k] = v[k] > 127 ? 127 : v[k];
    scale_set();
}

void juno_note_on(int note, int vel)
{
    if (vel <= 0)
        juno_note_off(note);
    else
        ev_push(EV_ON, (uint32_t)note, (uint32_t)vel);
}
void juno_note_off(int note) { ev_push(EV_OFF, (uint32_t)note, 0); }
void juno_all_off(void) { ev_push(EV_ALLOFF, 0, 0); }
void juno_panic(void) { ev_push(EV_PANIC, 0, 0); }
void juno_bend(int v) { ev_push(EV_BEND, (uint32_t)(v + 8192), 0); }
void juno_mod(int v) { ev_push(EV_MOD, 0, (uint32_t)v); }
void juno_sustain(int on) { ev_push(EV_SUS, 0, (uint32_t)(on != 0)); }
void juno_arp(int state) { ev_push(EV_ARP, 0, (uint32_t)state); }
void juno_chord_learn(void) { ev_push(EV_LEARN, 0, 0); }
void juno_clock(int rt) { ev_push(EV_RT, 0, (uint32_t)rt); }

void juno_render(float *out, uint32_t n)
{
    static float mono[CR];
    events();
    if (clock_seen && sample_t - tick_last > (uint32_t)JUNO_SR)
        clock_seen = 0;                  /* no tick for a second: the clock has gone */
    while (n) {
        int i, ns = n < CR ? (int)n : CR;
        control();
        for (i = 0; i < ns; i++)
            mono[i] = 0.0f;
        for (i = 0; i < JUNO_MAXV; i++)
            if (V[i].env.stage || V[i].a0 > 0.0f)
                voice_render(&V[i], mono, ns);
        for (i = 0; i < ns; i++)
            mono[i] *= 0.32f;            /* six voices at full level: about the output stage's knee */
        hpf_render(mono, ns);
        chorus_render(mono, out, ns);
        for (i = 0; i < 2 * ns; i++)     /* the output stage: clean below 0.7, rounding off above, never past 1 */
            if (fm_fabsf(out[i]) > 0.7f) {
                float a = fm_fabsf(out[i]) - 0.7f, y = 0.7f + 0.3f * fm_tanhf(a * (1.0f / 0.3f));
                out[i] = out[i] > 0.0f ? y : -y;
            }
        out += 2 * ns;
        n -= (uint32_t)ns;
        sample_t += (uint32_t)ns;
    }
}

float juno_voice_level(int v) { return v >= 0 && v < JUNO_MAXV && V[v].env.stage ? V[v].env.level : 0.0f; }
int juno_voice_note(int v) { return v >= 0 && v < JUNO_MAXV && V[v].env.stage ? V[v].note : -1; }
int juno_arp_state(void) { return arp_on; }
int juno_learning(void) { return learning; }
int juno_arp_step_lit(void)
{
    int l = arp_lit;
    arp_lit = 0;
    return l;
}
int juno_clock_running(void) { return clock_seen; }
