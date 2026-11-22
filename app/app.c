/* SPDX-License-Identifier: MPL-2.0 */
/* JUNO FM-1: the app around the engine (juno.c). The FM-1's controls as a Juno-106's panel, in pages, with
 * superOS-106's additions (its manual is the guide for what they do):
 *
 *   HOME  the patch: PRESET / SELECT +-1, ALGO +-8; K1..K4 FREQ, RES, VCF ENV, CHORUS
 *   EDIT  DCO (LFO, PWM, SUB, NOISE) > DCO 2 (RANGE, PWM MODE, SAW, PULSE) > VOICE (voice, DETUNE, VIBRATO, PWM)
 *   SEL   VCF (FREQ, RES, ENV, LFO) > VCF 2 (KYBD, ENV POL, HPF)
 *   ENV   ENV (A D S R) > ENV 2, the filter's own (A D S R) > VCA (MODE, LEVEL, ENV 2 on)
 *   LFO   LFO (RATE, DELAY, DCO LFO, VCF LFO) > LFO 2 (SYNC, DETUNE LFO, DRIFT)
 *   FX    CHORUS (CHORUS, HPF, LEVEL)
 *   GLO   MODE (MODE, VOICES, UNISON DETUNE, PORTA) > VELOCITY & BEND > SCALE & CLOCK > MIDI
 *   ARP   the arpeggiator off > on > on with hold, and its page (ORDER, RANGE, RATE, GATE)
 *   PLAY  chord memory: learn the next chord played
 *   SAVE  the patch into its slot (lit when changed); OCT- / OCT+ the octave; REC held: Bluetooth off / on
 *
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is its own unit (fm1.toml o2). */
#include "fastmath.h"
#include "juno.h"

/* --------------------------------------------------------------- the patches --- */
#define NPATCH 128
#define BLK_N 32                         /* patches a store object */
#define NBLK (NPATCH / BLK_N)
#define BANK_MAGIC 0x4A4E4234u           /* "JNB4": 0.5's opened-up bank */
typedef struct {
    uint32_t magic;
    juno_patch_t p[BLK_N];
} block_t;
static PLAT_POOL block_t blocks[NBLK];   /* 10 KB: in the pool RAM */
static uint8_t blk_dirty[NBLK];          /* changed in RAM (a SysEx load), not yet saved */
_Static_assert(sizeof(juno_patch_t) == 80, "a patch is 80 bytes");
_Static_assert(sizeof(block_t) <= PLAT_STORE_MAX, "a block fits a store object");

/* the factory patches, one for every slot, from factory.h (tools/mk_factory.py writes it from tools/bank.txt) */
/* a patch's arpeggio, in its spare bytes: on, then order, range, rate, gate */
#define PATCH_ARP 0

typedef struct {
    uint8_t p[JP_N];
    const char *name;
    uint8_t mode, uni, drift;
    uint8_t chord_n;
    int8_t chord[6];
    uint8_t arp;                         /* 1: the patch starts the arpeggiator */
    uint8_t arp_set[4];                  /* its order, range, rate, gate */
} factory_t;
static const factory_t FACTORY[] = {
#include "factory.h"
};
_Static_assert(sizeof(((juno_patch_t *)0)->rsv) >= PATCH_ARP + 5, "room for a patch's arpeggio");
#define NFACTORY (int)(sizeof FACTORY / sizeof FACTORY[0])

static void patch_init(juno_patch_t *d, const factory_t *f)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)d;
    for (i = 0; i < sizeof *d; i++)
        b[i] = 0;
    for (i = 0; i < JP_N; i++)
        d->p[i] = f->p[i];
    for (i = 0; i < JUNO_NAME_LEN - 1u && f->name[i]; i++)
        d->name[i] = f->name[i];
    d->mode = f->mode;
    d->uni_detune = f->uni;
    d->drift = f->drift;
    d->chord_n = f->chord_n;
    for (i = 0; i < 6; i++)
        d->chord[i] = f->chord[i];
    d->env2[0] = 0;                      /* envelope 2: off, shaped like a snappy filter envelope */
    d->env2[1] = 50;
    d->env2[2] = 40;
    d->env2[3] = 40;
    if (f->arp) {                        /* the patch's arpeggio */
        d->rsv[PATCH_ARP] = 1;
        for (i = 0; i < 4; i++)
            d->rsv[PATCH_ARP + 1 + i] = f->arp_set[i];
    }
}

_Static_assert(sizeof FACTORY / sizeof FACTORY[0] == NPATCH, "a factory patch for every slot");

static void banks_load(void)
{
    int b;
    for (b = 0; b < NBLK; b++)           /* read in place; a block that is not whole and ours: the factory's again */
        if (plat_store_load(OBJ_BANK0 + (uint32_t)b, &blocks[b], sizeof blocks[b]) != (int)sizeof blocks[b]
            || blocks[b].magic != BANK_MAGIC) {
            int i;
            for (i = 0; i < BLK_N; i++)
                patch_init(&blocks[b].p[i], &FACTORY[b * BLK_N + i < NFACTORY ? b * BLK_N + i : 0]);
            blocks[b].magic = BANK_MAGIC;
        }
}

static juno_patch_t *slot(int i) { return &blocks[(i / BLK_N) % NBLK].p[i % BLK_N]; }

/* -------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x4A4E5332u            /* "JNS2" */
typedef struct {
    uint32_t magic;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t patch;                       /* 0 .. 127 */
    uint8_t ble_off;
    uint8_t midi_ch;                     /* 0 omni, 1..16 */
    uint8_t local_off;
    uint8_t rsv[3];
    juno_perf_t perf;
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static juno_patch_t edit;                /* the patch being played and edited */
static uint8_t edit_dirty;               /* changed from its slot: SAVE is lit */

static void settings_defaults(void)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)&set;
    for (i = 0; i < sizeof set; i++)
        b[i] = 0;
    set.magic = SET_MAGIC;
    set.patch = 1;
    set.perf.voices = 6;
    set.perf.bend_dco = 2;
    set.perf.vel_route = 0;              /* as superOS-106: velocity off until asked for */
    set.perf.vel_depth = 8;
    set.perf.arp_rate = 64;
    set.perf.arp_gate = 64;
    set.perf.clock_src = 2;              /* auto: internal until MIDI clock comes */
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.patch >= NPATCH || set.ble_off > 1u || set.midi_ch > 16u
        || set.perf.voices < 1 || set.perf.voices > JUNO_MAXV)
        settings_defaults();
}

/* MIDI out: what the panel does, for a computer recording it (superOS-106's controller numbers) */
static const uint8_t SLIDER_CC[JP_N] = {
    [JP_LFO_RATE] = 76, [JP_LFO_DELAY] = 78, [JP_DCO_LFO] = 77, [JP_DCO_PWM] = 12, [JP_SUB] = 14, [JP_NOISE] = 13,
    [JP_VCF_FREQ] = 74, [JP_VCF_RES] = 71, [JP_VCF_ENV] = 16, [JP_VCF_LFO] = 17, [JP_VCF_KYBD] = 18,
    [JP_VCA_LEVEL] = 7, [JP_ENV_A] = 73, [JP_ENV_D] = 75, [JP_ENV_S] = 79, [JP_ENV_R] = 72,
};
static uint32_t midi_out_ch(void) { return set.midi_ch ? set.midi_ch - 1u : 0u; }

/* the Juno-106's patch as its SysEx carries it: 16 sliders, then two bytes of switches (its owner's manual,
 * MIDI implementation 3.4) */
static const uint8_t JUNO_SLIDER_ORDER[16] = {JP_LFO_RATE, JP_LFO_DELAY, JP_DCO_LFO, JP_DCO_PWM, JP_NOISE,
                                              JP_VCF_FREQ, JP_VCF_RES, JP_VCF_ENV, JP_VCF_LFO, JP_VCF_KYBD,
                                              JP_VCA_LEVEL, JP_ENV_A, JP_ENV_D, JP_ENV_S, JP_ENV_R, JP_SUB};
static uint8_t juno_sw1(const juno_patch_t *p)
{
    uint8_t b = (uint8_t)(1u << (p->p[JP_DCO_RANGE] > 2 ? 2 : p->p[JP_DCO_RANGE]));   /* 1 16', 2 8', 4 4' */
    if (p->p[JP_PULSE])
        b |= 0x08u;
    if (p->p[JP_SAW])
        b |= 0x10u;
    if (p->p[JP_CHORUS] == 0)
        b |= 0x20u;                      /* chorus off */
    else if (p->p[JP_CHORUS] == 1)
        b |= 0x40u;                      /* level I (else II; I+II is ours: sent as II) */
    return b;
}
static uint8_t juno_sw2(const juno_patch_t *p)
{
    uint8_t b = 0;
    if (p->p[JP_PWM_SRC] == 1)
        b |= 0x01u;                      /* PWM: MAN (else LFO) */
    if (p->p[JP_VCF_POL])
        b |= 0x02u;
    if (p->p[JP_VCA_MODE])
        b |= 0x04u;
    b |= (uint8_t)((3u - p->p[JP_HPF]) << 3);
    return b;
}
static void juno_sw1_in(juno_patch_t *p, uint8_t b)
{
    p->p[JP_DCO_RANGE] = (b & 4u) ? 2 : (b & 2u) ? 1 : 0;
    p->p[JP_PULSE] = (b >> 3) & 1u;
    p->p[JP_SAW] = (b >> 4) & 1u;
    p->p[JP_CHORUS] = (b & 0x20u) ? 0 : (b & 0x40u) ? 1 : 2;
}
static void juno_sw2_in(juno_patch_t *p, uint8_t b)
{
    p->p[JP_PWM_SRC] = (b & 1u) ? 1 : 0;
    p->p[JP_VCF_POL] = (b >> 1) & 1u;
    p->p[JP_VCA_MODE] = (b >> 2) & 1u;
    p->p[JP_HPF] = (uint8_t)(3u - ((b >> 3) & 3u));
}

static void send_patch_sysex(void)       /* F0 41 30 0n pp [18] F7: as the Juno sends at a patch change */
{
    uint8_t f[24];
    int i;
    if (!plat_usb_session())
        return;
    f[0] = 0xF0;
    f[1] = 0x41;
    f[2] = 0x30;
    f[3] = (uint8_t)midi_out_ch();
    f[4] = set.patch;
    for (i = 0; i < 16; i++)
        f[5 + i] = edit.p[JUNO_SLIDER_ORDER[i]] & 0x7Fu;
    f[21] = juno_sw1(&edit);
    f[22] = juno_sw2(&edit);
    f[23] = 0xF7;
    plat_sysex_send(f, sizeof f);
}

static uint8_t arp_by_patch;             /* the arpeggiator was started by the patch, not by ARP */

static void patch_arp(void)              /* a patch with an arpeggio starts it; the next patch without one stops it */
{
    const uint8_t *a = &edit.rsv[PATCH_ARP];
    if (a[0] == 1) {
        set.perf.arp_order = (uint8_t)(a[1] & 3u);
        set.perf.arp_range = (uint8_t)(a[2] > 2u ? 2u : a[2]);
        set.perf.arp_rate = (uint8_t)(a[3] & 0x7Fu);
        set.perf.arp_gate = (uint8_t)(a[4] & 0x7Fu);
        juno_set_perf(&set.perf);
        if (juno_arp_state() == 0)
            juno_arp(1);
        arp_by_patch = 1;
    } else if (arp_by_patch) {
        juno_arp(0);
        arp_by_patch = 0;
    }
}

static void patch_select(int i)
{
    set.patch = (uint8_t)((i % NPATCH + NPATCH) % NPATCH);
    edit = *slot(set.patch);
    edit_dirty = 0;
    set_dirty = 1;
    juno_set_patch(&edit);
    patch_arp();
}

int juno_ble_off(void) { return set.ble_off; }

void app_init(void)
{
    juno_init();
    settings_load();
    banks_load();
    juno_set_perf(&set.perf);
    edit = *slot(set.patch);
    juno_set_patch(&edit);
    patch_arp();
}

/* ----------------------------------------------------------------- audio --- */
static volatile uint8_t ui_poke;         /* MIDI changed what the screen shows */
static volatile int pc_pending = -1;     /* a program change, for the main loop */

static void cc_in(uint32_t cc, uint32_t v)
{
    int i;
    juno_patch_t *ep = juno_patch();
    for (i = 0; i < JP_N; i++)           /* the sliders */
        if (SLIDER_CC[i] == cc) {
            edit.p[i] = (uint8_t)v;
            juno_set_param(i, (int)v);
            edit_dirty = 1;
            ui_poke = 1;
            return;
        }
    switch (cc) {
    case 1: juno_mod((int)v); return;
    case 64: juno_sustain(v >= 64u); return;
    case 120: case 123: juno_all_off(); return;
    case 22:                             /* voice mode */
        if (v < VM_N && v != edit.mode) {
            edit.mode = (uint8_t)v;
            edit_dirty = ui_poke = 1;
            juno_set_patch(&edit);
        }
        return;
    case 38: case 39: case 40: case 41:  /* envelope 2 */
        edit.env2[cc - 38u] = ep->env2[cc - 38u] = (uint8_t)v;
        edit_dirty = ui_poke = 1;
        return;
    case 42:
        edit.env2_on = ep->env2_on = v != 0;
        edit_dirty = ui_poke = 1;
        return;
    case 114:
        edit.det_lfo = ep->det_lfo = (uint8_t)(v / 2u);
        edit_dirty = ui_poke = 1;
        return;
    case 122:
        set.local_off = v < 64u;
        set_dirty = ui_poke = 1;
        return;
    default:
        if (cc >= 102u && cc <= 107u) {
            edit.vdet[cc - 102u] = ep->vdet[cc - 102u] = (int8_t)((int)v - 64);
            edit_dirty = ui_poke = 1;
        } else if (cc >= 108u && cc <= 113u) {
            edit.vlfo[cc - 108u] = ep->vlfo[cc - 108u] = (int8_t)((int)v - 64);
            edit_dirty = ui_poke = 1;
        }
        return;
    }
}

static void midi_in(void)                /* audio interrupt: the packets that came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ch = st & 15u;
        uint32_t type = st & 0xF0u;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            juno_clock((int)st);
            continue;
        }
        if (st >= 0xF0u)
            continue;
        if (set.midi_ch && ch + 1u != set.midi_ch)
            continue;
        if (type == 0x90u && d2)
            juno_note_on((int)d1, (int)d2);
        else if (type == 0x80u || type == 0x90u)
            juno_note_off((int)d1);
        else if (type == 0xE0u)
            juno_bend((int)(d1 | d2 << 7) - 8192);
        else if (type == 0xB0u)
            cc_in(d1, d2);
        else if (type == 0xC0u)
            pc_pending = (int)d1;
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float buf[2 * APP_BLOCK];
    static float gain_now;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t i;
    midi_in();
    if (n > APP_BLOCK)
        n = APP_BLOCK;
    juno_render(buf, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;
        lr[2 * i] = (int32_t)(buf[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)(buf[2 * i + 1] * gain_now);
    }
}

void app_panic(void) { juno_panic(); }

/* ------------------------------------------------------------------ SysEx --- */
/* from USB (main loop): the Juno-106's messages (F0 41 30/31/32 0n ...) and superOS-106's user scale
 * (F0 41 3D 0n 02 ss [24 nibbles]); p is what lies between F0 and F7 */
int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t ch;
    int i;
    if (n < 4 || p[0] != 0x41u)
        return 0;
    ch = p[2] & 15u;
    if (set.midi_ch && ch + 1u != set.midi_ch)
        return 1;
    if ((p[1] == 0x30u || p[1] == 0x31u) && n >= 4u + 18u) {   /* a patch (0x30: into slot pp; 0x31: the manual) */
        juno_patch_t t = p[1] == 0x30u ? *slot(p[3] & 0x7Fu) : edit;
        for (i = 0; i < 16; i++)
            t.p[JUNO_SLIDER_ORDER[i]] = p[4 + i] & 0x7Fu;
        juno_sw1_in(&t, p[20]);
        juno_sw2_in(&t, p[21]);
        if (p[1] == 0x30u) {
            int pp = p[3] & 0x7F;
            *slot(pp) = t;
            blk_dirty[pp / BLK_N] = 1;   /* saved by SAVE, with the patch */
            if (pp == set.patch) {
                edit = t;
                juno_set_patch(&edit);
            }
        } else {
            edit = t;
            edit_dirty = 1;
            juno_set_patch(&edit);
        }
        ui_poke = 1;
        return 1;
    }
    if (p[1] == 0x32u && n >= 5u) {      /* one parameter: 0..15 the sliders, 16 and 17 the switches */
        uint32_t pn = p[3], v = p[4] & 0x7Fu;
        if (pn < 16u) {
            edit.p[JUNO_SLIDER_ORDER[pn]] = (uint8_t)v;
            juno_set_param(JUNO_SLIDER_ORDER[pn], (int)v);
        } else if (pn == 16u || pn == 17u) {
            if (pn == 16u)
                juno_sw1_in(&edit, (uint8_t)v);
            else
                juno_sw2_in(&edit, (uint8_t)v);
            for (i = 0; i < JP_N; i++)
                juno_set_param(i, edit.p[i]);
        }
        edit_dirty = ui_poke = 1;
        return 1;
    }
    if (p[1] == 0x3Du && n >= 5u + 24u && p[3] == 0x02u) {   /* superOS-106: a user scale, in nibbles */
        uint8_t v[12];
        for (i = 0; i < 12; i++)
            v[i] = (uint8_t)((p[5 + 2 * i] & 15u) << 4 | (p[6 + 2 * i] & 15u));
        juno_set_user_scale(p[4] & 3, v);
        return 1;
    }
    return 0;
}

/* -------------------------------------------------------------------- pages --- */
/* a page's four knobs: a panel parameter (0..JP_N-1), a patch extra (PX_*), a performance setting (PF_*), none -1 */
enum { PX_MODE = 50, PX_UNI, PX_ENV2ON, PX_E2A, PX_E2D, PX_E2S, PX_E2R, PX_DETLFO, PX_DRIFT, PX_VSEL, PX_VDET,
       PX_VLFO, PX_VPWM };
enum { PF_VOICES = 100, PF_BEND, PF_BENDVCF, PF_VELROUTE, PF_VELDEPTH, PF_PORTA, PF_SCALE, PF_LFOSYNC, PF_ARPORD,
       PF_ARPRANGE, PF_ARPFIRST, PF_ARPRATE, PF_ARPGATE, PF_CLOCK, PF_MIDICH, PF_LOCAL };
enum { PG_HOME, PG_DCO, PG_DCO2, PG_VOICE, PG_VCF, PG_VCF2, PG_ENV, PG_ENV2, PG_VCA, PG_LFO, PG_LFO2, PG_FX,
       PG_MODE, PG_GLO2, PG_GLO3, PG_GLO4, PG_ARP, NPAGES };
typedef struct {
    const char *title;
    int16_t k[4];
} page_t;
static const page_t PAGES[NPAGES] = {
    [PG_HOME] = {"", {JP_VCF_FREQ, JP_VCF_RES, JP_VCF_ENV, JP_CHORUS}},
    [PG_DCO] = {"DCO", {JP_DCO_LFO, JP_DCO_PWM, JP_SUB, JP_NOISE}},
    [PG_DCO2] = {"DCO 2", {JP_DCO_RANGE, JP_PWM_SRC, JP_SAW, JP_PULSE}},
    [PG_VOICE] = {"VOICE", {PX_VSEL, PX_VDET, PX_VLFO, PX_VPWM}},
    [PG_VCF] = {"VCF", {JP_VCF_FREQ, JP_VCF_RES, JP_VCF_ENV, JP_VCF_LFO}},
    [PG_VCF2] = {"VCF 2", {JP_VCF_KYBD, JP_VCF_POL, JP_HPF, -1}},
    [PG_ENV] = {"ENV", {JP_ENV_A, JP_ENV_D, JP_ENV_S, JP_ENV_R}},
    [PG_ENV2] = {"ENV 2 (VCF)", {PX_E2A, PX_E2D, PX_E2S, PX_E2R}},
    [PG_VCA] = {"VCA", {JP_VCA_MODE, JP_VCA_LEVEL, PX_ENV2ON, -1}},
    [PG_LFO] = {"LFO", {JP_LFO_RATE, JP_LFO_DELAY, JP_DCO_LFO, JP_VCF_LFO}},
    [PG_LFO2] = {"LFO 2", {PF_LFOSYNC, PX_DETLFO, PX_DRIFT, -1}},
    [PG_FX] = {"CHORUS", {JP_CHORUS, JP_HPF, JP_VCA_LEVEL, -1}},
    [PG_MODE] = {"VOICE MODE", {PX_MODE, PF_VOICES, PX_UNI, PF_PORTA}},
    [PG_GLO2] = {"VELOCITY / BEND", {PF_VELROUTE, PF_VELDEPTH, PF_BEND, PF_BENDVCF}},
    [PG_GLO3] = {"SCALE / CLOCK", {PF_SCALE, PF_CLOCK, PF_ARPFIRST, -1}},
    [PG_GLO4] = {"MIDI", {PF_MIDICH, PF_LOCAL, -1, -1}},
    [PG_ARP] = {"ARPEGGIO", {PF_ARPORD, PF_ARPRANGE, PF_ARPRATE, PF_ARPGATE}},
};

static struct {
    int page;
    int vsel;                            /* the VOICE page's voice, 0..5 */
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t rec_t;
    uint32_t turned_at[4];
    uint32_t knob_t[4];
    uint8_t redraw;
} ui;

static const char *knob_name(int id)
{
    static const char *const PX[] = {"MODE", "UNI DET", "ENV 2", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "DET LFO",
                                     "DRIFT", "VOICE", "DETUNE", "VIBRATO", "PWM LVL"};
    static const char *const PF[] = {"VOICES", "BEND", "BEND VCF", "VEL TO", "VEL DEPTH", "PORTA", "SCALE",
                                     "LFO SYNC", "ORDER", "RANGE", "FIRST", "RATE", "GATE", "CLOCK", "MIDI CH",
                                     "LOCAL"};
    if (id < 0)
        return "";
    if (id >= PF_VOICES)
        return PF[id - PF_VOICES];
    if (id >= PX_MODE)
        return PX[id - PX_MODE];
    return JUNO_PNAME[id];
}

static uint8_t *pf_ptr(int id)
{
    switch (id) {
    case PF_VOICES: return &set.perf.voices;
    case PF_BEND: return &set.perf.bend_dco;
    case PF_BENDVCF: return &set.perf.bend_vcf;
    case PF_VELROUTE: return &set.perf.vel_route;
    case PF_VELDEPTH: return &set.perf.vel_depth;
    case PF_PORTA: return &set.perf.porta;
    case PF_SCALE: return &set.perf.scale;
    case PF_LFOSYNC: return &set.perf.lfo_sync;
    case PF_ARPORD: return &set.perf.arp_order;
    case PF_ARPRANGE: return &set.perf.arp_range;
    case PF_ARPFIRST: return &set.perf.arp_first;
    case PF_ARPRATE: return &set.perf.arp_rate;
    case PF_ARPGATE: return &set.perf.arp_gate;
    case PF_CLOCK: return &set.perf.clock_src;
    case PF_LOCAL: return &set.local_off;
    default: return &set.midi_ch;
    }
}

static int knob_value(int id)
{
    if (id < 0)
        return 0;
    if (id >= PF_VOICES)
        return *pf_ptr(id);
    switch (id) {
    case PX_MODE: return edit.mode;
    case PX_UNI: return edit.uni_detune;
    case PX_ENV2ON: return edit.env2_on;
    case PX_E2A: case PX_E2D: case PX_E2S: case PX_E2R: return edit.env2[id - PX_E2A];
    case PX_DETLFO: return edit.det_lfo;
    case PX_DRIFT: return edit.drift;
    case PX_VSEL: return ui.vsel;
    case PX_VDET: return edit.vdet[ui.vsel];
    case PX_VLFO: return edit.vlfo[ui.vsel];
    case PX_VPWM: return edit.vpwm[ui.vsel];
    default: return edit.p[id];
    }
}
static int knob_lo(int id)
{
    return id == PF_VOICES ? 1 : (id == PX_VDET || id == PX_VLFO) ? -64 : 0;
}
static int knob_hi(int id)
{
    if (id < 0)
        return 0;
    if (id < JP_N)
        return JUNO_MAX[id];
    switch (id) {
    case PX_MODE: return VM_N - 1;
    case PX_UNI: case PX_DRIFT: case PF_VELDEPTH: return 15;
    case PX_ENV2ON: case PF_LOCAL: return 1;
    case PX_DETLFO: return 63;
    case PX_VSEL: return JUNO_NVP - 1;
    case PX_VDET: case PX_VLFO: return 63;
    case PX_VPWM: return 7;
    case PF_VOICES: return JUNO_MAXV;
    case PF_BEND: return 12;
    case PF_VELROUTE: case PF_ARPFIRST: return 3;
    case PF_SCALE: case PF_LFOSYNC: return 5;
    case PF_ARPORD: return 3;
    case PF_ARPRANGE: case PF_CLOCK: return 2;
    case PF_MIDICH: return 16;
    default: return 127;
    }
}
/* shown as a list of positions, not a fader */
static int knob_is_switch(int id)
{
    return id < JP_N ? JUNO_MAX[id] <= 3
         : id == PX_MODE || id == PX_ENV2ON || id == PF_VELROUTE || id == PF_LOCAL || id == PF_ARPORD
           || id == PF_ARPRANGE || id == PF_CLOCK || id == PF_SCALE || id == PF_LFOSYNC;
}

/* a value as words: a switch's positions by name, a slider 0..127 as the Juno's 0..10 scale with one decimal */
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void value_text_of(int id, int v, char *t)
{
    static const char *const MODE[] = {"POLY 1", "POLY 2", "MONO", "CHORD", "UNISON", "DUAL"};
    static const char *const VEL[] = {"OFF", "AMP", "FILTER", "BOTH"};
    static const char *const SCL[] = {"EQUAL", "ARABIC", "USER 1", "USER 2", "USER 3", "USER 4"};
    static const char *const SYNC[] = {"OFF", "1/16", "1/8", "BEAT", "2 BEATS", "BAR"};
    static const char *const ORD[] = {"UP", "DOWN", "UP/DOWN", "PLAYED"};
    static const char *const CLK[] = {"INT", "MIDI", "AUTO"};
    t[0] = 0;
    if (id < 0)
        return;
    switch (id) {
    case JP_DCO_RANGE: text_cat(t, v == 0 ? "16'" : v == 1 ? "8'" : "4'"); return;
    case JP_PWM_SRC: text_cat(t, v == 0 ? "LFO" : v == 1 ? "MAN" : "ENV"); return;
    case JP_SAW: case JP_PULSE: case PX_ENV2ON: text_cat(t, v ? "ON" : "OFF"); return;
    case JP_HPF: int_text(v, t); return;
    case JP_VCF_POL: text_cat(t, v ? "-" : "+"); return;
    case JP_VCA_MODE: text_cat(t, v ? "GATE" : "ENV"); return;
    case JP_CHORUS: text_cat(t, v == 0 ? "OFF" : v == 1 ? "I" : v == 2 ? "II" : "I+II"); return;
    case PX_MODE: text_cat(t, MODE[v % VM_N]); return;
    case PX_VSEL: int_text(v + 1, t); return;
    case PF_VELROUTE: text_cat(t, VEL[v & 3]); return;
    case PF_SCALE: text_cat(t, SCL[v % 6]); return;
    case PF_LFOSYNC: text_cat(t, SYNC[v % 6]); return;
    case PF_ARPORD: text_cat(t, ORD[v & 3]); return;
    case PF_ARPRANGE: int_text(v + 1, t); text_cat(t, " OCT"); return;
    case PF_CLOCK: text_cat(t, CLK[v % 3]); return;
    case PF_LOCAL: text_cat(t, v ? "OFF" : "ON"); return;
    case PF_MIDICH: if (v) int_text(v, t); else text_cat(t, "OMNI"); return;
    case PF_ARPRATE: {                   /* steps a second */
        float r = 2.0f * fm_exp2f((float)v * (4.0f / 127.0f));
        int_text((int32_t)(r + 0.5f), t);
        text_cat(t, "/S");
        return;
    }
    case PX_UNI: case PX_DRIFT: case PX_DETLFO: case PX_VDET: case PX_VLFO: case PX_VPWM: case PF_VOICES:
    case PF_BEND: case PF_VELDEPTH: case PF_ARPFIRST:
        int_text(v, t);
        return;
    default: {                           /* 0..127 as 0.0..10.0 */
        int tenths = (v * 100 + 63) / 127;
        char d[2] = {(char)('0' + tenths % 10), 0};
        int_text(tenths / 10, t);
        text_cat(t, ".");
        text_cat(t, d);
    }
    }
}

static void send_cc(uint32_t cc, uint32_t v)
{
    plat_midi_send(0x0Bu | (0xB0u | midi_out_ch()) << 8 | cc << 16 | (v & 0x7Fu) << 24, TO_USB | TO_BLE);
}

static void knob_set(int id, int v)
{
    juno_patch_t *ep = juno_patch();
    v = clamp(v, knob_lo(id), knob_hi(id));
    if (v == knob_value(id))
        return;
    if (id >= PF_VOICES) {
        *pf_ptr(id) = (uint8_t)v;
        set_dirty = 1;
        if (id == PF_VOICES)
            juno_all_off();
        juno_set_perf(&set.perf);
        return;
    }
    if (id >= PX_MODE) {
        switch (id) {
        case PX_MODE:
            edit.mode = (uint8_t)v;
            edit.chord_n = ep->chord_n;  /* keep a chord learnt meanwhile */
            for (v = 0; v < 6; v++)
                edit.chord[v] = ep->chord[v];
            juno_set_patch(&edit);
            send_cc(22, edit.mode);
            break;
        case PX_UNI: edit.uni_detune = ep->uni_detune = (uint8_t)v; break;
        case PX_ENV2ON: edit.env2_on = ep->env2_on = (uint8_t)v; send_cc(42, (uint32_t)v); break;
        case PX_E2A: case PX_E2D: case PX_E2S: case PX_E2R:
            edit.env2[id - PX_E2A] = ep->env2[id - PX_E2A] = (uint8_t)v;
            send_cc(38u + (uint32_t)(id - PX_E2A), (uint32_t)v);
            break;
        case PX_DETLFO: edit.det_lfo = ep->det_lfo = (uint8_t)v; break;
        case PX_DRIFT: edit.drift = ep->drift = (uint8_t)v; break;
        case PX_VSEL: ui.vsel = v; return;   /* (not a change to the patch) */
        case PX_VDET: edit.vdet[ui.vsel] = ep->vdet[ui.vsel] = (int8_t)v; send_cc(102u + (uint32_t)ui.vsel, (uint32_t)(v + 64)); break;
        case PX_VLFO: edit.vlfo[ui.vsel] = ep->vlfo[ui.vsel] = (int8_t)v; send_cc(108u + (uint32_t)ui.vsel, (uint32_t)(v + 64)); break;
        case PX_VPWM: edit.vpwm[ui.vsel] = ep->vpwm[ui.vsel] = (uint8_t)v; break;
        default: break;
        }
        edit_dirty = 1;
        return;
    }
    edit.p[id] = (uint8_t)v;
    edit_dirty = 1;
    juno_set_param(id, v);
    if (SLIDER_CC[id])
        send_cc(SLIDER_CC[id], (uint32_t)v);
    else if (plat_usb_session()) {       /* a switch: the Juno's parameter message, 16 or 17 */
        int sw2 = id == JP_PWM_SRC || id == JP_VCF_POL || id == JP_VCA_MODE || id == JP_HPF;
        uint8_t f[7] = {0xF0, 0x41, 0x32, (uint8_t)midi_out_ch(), (uint8_t)(sw2 ? 17 : 16),
                        (uint8_t)(sw2 ? juno_sw2(&edit) : juno_sw1(&edit)), 0xF7};
        plat_sysex_send(f, sizeof f);
    }
}

/* ---------------------------------------------------------------------- UI --- */
void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void patch_label(int i, char *t)  /* "A12": group A or B, then 1..64 */
{
    char n[4];
    t[0] = (char)('A' + i / 64);
    t[1] = 0;
    int_text(i % 64 + 1, n);
    if (i % 64 + 1 < 10)
        text_cat(t, "0");
    text_cat(t, n);
}

static void speak(void)
{
    char t[64], l[8];
    if (ann_on() && ann_where()) {
        patch_label(set.patch, l);
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, l);
        text_cat(t, " ");
        text_cat(t, edit.name);
        if (ui.page != PG_HOME) {
            text_cat(t, ". ");
            text_cat(t, PAGES[ui.page].title);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

/* a page button: its pages in turn, from the first when coming from another button's */
static void page_cycle(const uint8_t *pages, int n)
{
    int i, next = pages[0];
    for (i = 0; i < n; i++)
        if (ui.page == pages[i])
            next = pages[(i + 1) % n];
    ui.page = next;
    ui.redraw = 1;
    if (ann_on())
        ann_say(PAGES[ui.page].title, 0, ANN_NOW);
}

static void octave_step(int d)
{
    int o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    juno_all_off();
    set.octave = (int8_t)o;
    set_dirty = 1;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void save(void)
{
    juno_patch_t *ep = juno_patch();
    int b = set.patch / BLK_N, rc = 0, i;
    edit.chord_n = ep->chord_n;          /* the chord memory, as learnt */
    for (i = 0; i < 6; i++)
        edit.chord[i] = ep->chord[i];
    edit.rsv[PATCH_ARP] = juno_arp_state() ? 1 : 0;   /* the arpeggio, as playing */
    edit.rsv[PATCH_ARP + 1] = set.perf.arp_order;
    edit.rsv[PATCH_ARP + 2] = set.perf.arp_range;
    edit.rsv[PATCH_ARP + 3] = set.perf.arp_rate;
    edit.rsv[PATCH_ARP + 4] = set.perf.arp_gate;
    *slot(set.patch) = edit;
    blk_dirty[b] = 1;
    for (i = 0; i < NBLK && rc == 0; i++)
        if (blk_dirty[i]) {
            rc = plat_store_save(OBJ_BANK0 + (uint32_t)i, &blocks[i], sizeof blocks[i]);
            if (rc == 0)
                blk_dirty[i] = 0;
        }
    if (rc == 0 && set_dirty)
        rc = plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    if (rc == 0) {
        edit_dirty = 0;
        set_dirty = 0;
        app_say("SAVED", edit.name);
    } else {
        app_say("SAVE", "FAILED");
    }
}

static void arp_button(void)
{
    static const char *const W[] = {"OFF", "ON", "HOLD"};
    int s = (juno_arp_state() + 1) % 3;
    juno_arp(s);
    arp_by_patch = 0;
    ui.page = PG_ARP;
    app_say("ARPEGGIO", W[s]);
}

static void play_button(void)            /* chord memory: learn the next chord */
{
    if (edit.mode != VM_CHORD) {
        edit.mode = VM_CHORD;
        edit_dirty = 1;
        juno_set_patch(&edit);
    }
    juno_chord_learn();
    app_say("CHORD", "PLAY IT");
}

static void input(void)
{
    static const uint8_t EDIT_P[] = {PG_DCO, PG_DCO2, PG_VOICE}, SEL_P[] = {PG_VCF, PG_VCF2},
                         ENV_P[] = {PG_ENV, PG_ENV2, PG_VCA}, LFO_P[] = {PG_LFO, PG_LFO2}, FX_P[] = {PG_FX},
                         GLO_P[] = {PG_MODE, PG_GLO2, PG_GLO3, PG_GLO4}, HOME_P[] = {PG_HOME};
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {        /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k, v, ch = midi_out_ch();
        int32_t note = 53 + (int32_t)k + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            v = touch_vel(k);
            if (!set.local_off)
                juno_note_on((int)note, (int)v);
            plat_midi_send(0x09u | (0x90u | ch) << 8 | (uint32_t)note << 16 | v << 24, TO_USB | TO_BLE);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (!set.local_off)
                juno_note_off((int)note);
            plat_midi_send(0x08u | (0x80u | ch) << 8 | (uint32_t)note << 16, TO_USB | TO_BLE);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN: octave_step(-1); break;
        case B_OCTUP: octave_step(1); break;
        case B_HOME: page_cycle(HOME_P, 1); break;
        case B_EDIT: page_cycle(EDIT_P, 3); break;
        case B_SEL: page_cycle(SEL_P, 2); break;
        case B_ENV: page_cycle(ENV_P, 3); break;
        case B_LFO: page_cycle(LFO_P, 2); break;
        case B_FX: page_cycle(FX_P, 1); break;
        case B_GLO: page_cycle(GLO_P, 4); break;
        case B_ARP: arp_button(); break;
        case B_PLAY: play_button(); break;
        case B_SAVE: save(); break;
        case B_REC: if (FM1B_BLE) ui.rec_t = now; break;
        default: break;
        }
    }
    if (FM1B_BLE && ui.rec_t) {          /* REC held a second: Bluetooth off / on */
        if (btn >> B_REC & 1u) {
            if (now - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                set.ble_off = (uint8_t)!set.ble_off;
                set_dirty = 1;
                plat_ble_enable(!set.ble_off);
                app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
            }
        } else {
            ui.rec_t = 0;
        }
    }
    /* the patch: PRESET and SELECT one at a time, ALGO a row of eight */
    if ((e = plat_enc(EN_PRESET) + plat_enc(EN_SELECT)) != 0) {
        patch_select(set.patch + e);
        send_patch_sysex();
        plat_midi_send(0x0Cu | (0xC0u | midi_out_ch()) << 8 | (uint32_t)set.patch << 16, TO_USB | TO_BLE);
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        patch_select(set.patch + 8 * e);
        send_patch_sysex();
        plat_midi_send(0x0Cu | (0xC0u | midi_out_ch()) << 8 | (uint32_t)set.patch << 16, TO_USB | TO_BLE);
    }
    for (k = 0; k < 4u; k++) {
        int id = PAGES[ui.page].k[k], step;
        if ((e = plat_enc(EN_K1 + (int)k)) == 0 || id < 0)
            continue;
        /* switches move a position a detent; sliders faster as the knob turns faster */
        step = knob_is_switch(id) || knob_hi(id) - knob_lo(id) <= 16 ? 1
             : now - ui.knob_t[k] < 40u ? 4 : now - ui.knob_t[k] < 100u ? 2 : 1;
        ui.knob_t[k] = now;
        knob_set(id, knob_value(id) + e * step);
        ui.turned_at[k] = now;
        ui.redraw = 1;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {
        static const uint8_t PAGE_BTN[NPAGES] = {
            [PG_HOME] = B_HOME, [PG_DCO] = B_EDIT, [PG_DCO2] = B_EDIT, [PG_VOICE] = B_EDIT, [PG_VCF] = B_SEL,
            [PG_VCF2] = B_SEL, [PG_ENV] = B_ENV, [PG_ENV2] = B_ENV, [PG_VCA] = B_ENV, [PG_LFO] = B_LFO,
            [PG_LFO2] = B_LFO, [PG_FX] = B_FX, [PG_MODE] = B_GLO, [PG_GLO2] = B_GLO, [PG_GLO3] = B_GLO,
            [PG_GLO4] = B_GLO, [PG_ARP] = B_ARP};
        static uint32_t arp_blink;
        uint32_t lit = btn | 1u << PAGE_BTN[ui.page];
        int arp = juno_arp_state();
        if (juno_arp_step_lit())
            arp_blink = now;
        if (arp == 1 || (arp == 2 && now - arp_blink < 60u))
            lit |= 1u << B_ARP;          /* on: lit; hold: a flash at each step */
        if (juno_learning() && (now / 250u) & 1u)
            lit |= 1u << B_PLAY;
        if (edit_dirty || set_dirty)
            lit |= 1u << B_SAVE;
        plat_glow(1);
        plat_leds(lit, keys);
    }
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- drawing --- */
/* the Juno's colours: a dark panel, each section's slider caps in its own colour */
#define J_PANEL RGB(26, 26, 30)
#define J_SLOT RGB(60, 60, 66)
#define J_RED RGB(214, 58, 46)
#define J_ORANGE RGB(236, 140, 40)
#define J_YELLOW RGB(232, 200, 60)
#define J_BLUE RGB(70, 150, 220)
#define J_WHITE RGB(230, 230, 230)

static uint16_t section_colour(int id)
{
    if (id >= PF_VOICES || id == PX_MODE || id == PX_UNI)
        return J_WHITE;
    if (id >= PX_E2A && id <= PX_E2R)
        return J_BLUE;
    if (id >= PX_MODE)
        return J_ORANGE;
    if (id <= JP_LFO_DELAY)
        return J_RED;
    if (id <= JP_NOISE)
        return J_ORANGE;
    if (id <= JP_VCF_KYBD)
        return J_YELLOW;
    if (id <= JP_ENV_R)
        return J_BLUE;
    return J_WHITE;
}

static void text_dotted(int32_t x, int32_t w, int32_t y, const char *s, uint16_t c)
{
    char t[16], *dot = t, *frac = 0;     /* the font's full stop is too faint at this size: drawn as a dot */
    int32_t tw, tx;
    uint32_t i;
    for (i = 0; i < 15u && s[i]; i++)
        t[i] = s[i];
    t[i] = 0;
    while (*dot && *dot != '.')
        dot++;
    if (*dot) {
        *dot = 0;
        frac = dot + 1;
    }
    tw = text_w(&FONT_XS, t) + (frac ? 4 + text_w(&FONT_XS, frac) : 0);
    tx = cv_text(x + (w - tw) / 2, y, &FONT_XS, t, c);
    if (frac) {
        cv_rect(tx + 1, y + 9, 2, 2, c);
        cv_text(tx + 4, y, &FONT_XS, frac, c);
    }
}

/* one knob as a Juno fader: the slot, the cap at its value; a switch as its positions, the one set lit */
static void draw_knob(int32_t x, int32_t w, int id, int lit)
{
    char t[16];
    int hi = knob_hi(id), lo = knob_lo(id), v = knob_value(id);
    uint16_t col = section_colour(id);
    if (id < 0)
        return;
    cv_text(x + (w - text_w(&FONT_XS, knob_name(id))) / 2, 0, &FONT_XS, knob_name(id), lit ? C_WHITE : C_GRAY);
    if (knob_is_switch(id)) {            /* its positions, top to bottom */
        int n = hi - lo + 1, i, hgt = n > 4 ? 18 : 22, gap = n > 4 ? 20 : 26;
        for (i = 0; i < n; i++) {
            int32_t y = 20 + i * gap;
            value_text_of(id, lo + i, t);
            cv_rect(x + 3, y, w - 6, hgt, v == lo + i ? col : J_SLOT);
            cv_text(x + (w - text_w(&FONT_XS, t)) / 2, y + (hgt - 13) / 2, &FONT_XS, t,
                    v == lo + i ? C_BLACK : C_GRAY);
        }
    } else {                             /* a fader */
        int32_t top = 22, h = 104, cx = x + w / 2, y;
        int i;
        cv_rect(cx - 2, top, 4, h, J_SLOT);
        for (i = 0; i <= 10; i++)        /* the scale's marks */
            cv_rect(cx - 10, top + h - i * h / 10, 5, 1, C_DIM);
        if (lo < 0)                      /* a centred value: the centre marked */
            cv_rect(cx - 14, top + h / 2, 28, 1, C_GRAY);
        y = top + h - (v - lo) * h / (hi - lo);
        cv_rect(cx - 12, y - 6, 24, 12, col);
        cv_rect(cx - 12, y - 1, 24, 2, C_BLACK);
        value_text_of(id, v, t);
        text_dotted(x, w, 134, t, lit ? C_WHITE : C_HI);
    }
}

static void draw(void)
{
    char t[24], l[8];
    uint32_t i, now = plat_ms();
    const page_t *pg = &PAGES[ui.page];
    /* the header: the patch, or a message */
    cv_begin(240, 40, J_PANEL);
    cv_rect(0, 38, 240, 2, J_RED);
    patch_label(set.patch, l);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 8, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 8, &FONT_B, ui.msg[1], J_ORANGE);
    } else {
        int32_t x = cv_text(8, 4, &FONT_B, l, J_ORANGE);
        x = cv_text(x + 8, 4, &FONT_B, edit.name, C_WHITE);
        if (edit_dirty)
            cv_text(x + 2, 4, &FONT_B, "*", J_ORANGE);
        cv_text(8, 24, &FONT_XS, ui.page == PG_HOME ? "HOME" : pg->title, C_GRAY);
        value_text_of(PX_MODE, edit.mode, t);
        cv_text(232 - text_w(&FONT_XS, t), 24, &FONT_XS, t, J_YELLOW);
        if (FM1B_BLE)
            cv_text(208, 6, &FONT_XS, "BLE", plat_ble_connected() ? J_BLUE : C_DIM);
    }
    cv_blit(0, 0);
    /* the four knobs */
    cv_begin(240, 150, J_PANEL);
    for (i = 0; i < 4u; i++)
        draw_knob(4 + (int32_t)i * 58, 56, pg->k[i], ui.turned_at[i] && now - ui.turned_at[i] < 600u);
    cv_blit(0, 44);
    /* the foot: the voices sounding, the octave, the arpeggiator and the chorus */
    cv_begin(240, 44, J_PANEL);
    cv_rect(0, 0, 240, 1, C_LINE);
    {
        int nv = edit.mode == VM_MONO ? 1 : set.perf.voices;
        int32_t w = nv > 6 ? 9 : 12;
        for (i = 0; i < (uint32_t)nv && i < JUNO_MAXV; i++) {
            float lv = juno_voice_level((int)i);
            int32_t h = (int32_t)(lv * 18.0f), x = 8 + (int32_t)i * (w + 3);
            cv_rect(x, 8, w, 20, J_SLOT);
            if (juno_voice_note((int)i) >= 0)
                cv_rect(x, 8 + 20 - h - 1, w, h + 1, J_RED);
        }
    }
    t[0] = 0;
    text_cat(t, "OCT ");
    {
        char o[4] = {set.octave < 0 ? '-' : '+', (char)('0' + (set.octave < 0 ? -set.octave : set.octave)), 0};
        text_cat(t, o);
    }
    cv_text(160, 4, &FONT_XS, t, C_GRAY);
    {
        int arp = juno_arp_state();
        if (arp)
            cv_text(120, 4, &FONT_XS, arp == 2 ? "HOLD" : "ARP", J_ORANGE);
        if (juno_clock_running())
            cv_text(120, 20, &FONT_XS, "CLK", J_BLUE);
    }
    value_text_of(JP_CHORUS, edit.p[JP_CHORUS], l);
    t[0] = 0;
    text_cat(t, "CHO ");
    text_cat(t, l);
    cv_text(160, 20, &FONT_XS, t, edit.p[JP_CHORUS] ? J_BLUE : C_DIM);
    cv_blit(0, 196);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, J_PANEL);
    ui.page = PG_HOME;
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_foot, last_msg;
    static int was_learning;
    uint32_t now = plat_ms();
    int pc = pc_pending;
    if (pc >= 0) {                       /* a program change from MIDI */
        pc_pending = -1;
        patch_select(pc);
        ui.redraw = 1;
    }
    if (ui_poke) {
        ui_poke = 0;
        ui.redraw = 1;
    }
    if (was_learning && !juno_learning()) {   /* a chord learnt */
        juno_patch_t *ep = juno_patch();
        int i;
        edit.chord_n = ep->chord_n;
        for (i = 0; i < 6; i++)
            edit.chord[i] = ep->chord[i];
        edit_dirty = 1;
        {
            char t[16];
            int_text(edit.chord_n, t);
            text_cat(t, edit.chord_n == 1 ? " NOTE" : " NOTES");
            app_say("CHORD KEPT", t);
        }
    }
    was_learning = juno_learning();
    input();
    speak();
    if (now - last_foot >= 50u) {        /* the voice meters move */
        last_foot = now;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until && last_msg != ui.msg_until) {
        last_msg = ui.msg_until;
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "patch"))
        *got = set.patch;
    else if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "dirty"))
        *got = edit_dirty;
    else if (!strcmp(what, "mode"))
        *got = edit.mode;
    else if (!strcmp(what, "arp"))
        *got = juno_arp_state();
    else if (!strcmp(what, "chord_n"))
        *got = juno_patch()->chord_n;
    else if (!strncmp(what, "param", 5))   /* paramN: the edit buffer's parameter N */
        *got = edit.p[atoi(what + 5) % JP_N];
    else if (!strncmp(what, "voice", 5))   /* voiceN: its note, -1 free */
        *got = juno_voice_note(atoi(what + 5));
    else
        return 0;
    return 1;
}
#endif
