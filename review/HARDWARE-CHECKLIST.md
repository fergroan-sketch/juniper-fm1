# JUNIPER FM-1 0.5.1: hardware diagnostic checklist

This is the owner-operated diagnostic the reviewer's ruling of 23 November 2026 authorises: one controlled test of
the exact v0.5.1 release on Fergus's FM-1. Fergus runs every step. Nothing here was run by Claude, and Claude does
not connect to the FM-1.

Record each item as **PASS**, **FAIL** or **NOT TESTED**. For a FAIL, write what you did and what happened in the
"Notes" column, so it can be reproduced. A dash (–) in "Expected" means "observe and describe".

## 0. Before installing

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 0.1 | Download the release file, then in Terminal: `shasum -a 256 ~/Downloads/juniperfm1-0.5.1.fwsc` | `2e8e709fd93fa2425b4537a9c1d9410ad48af288419925c3e7df1981cc11bc2c` | | |
| 0.2 | Open https://fergroan-sketch.github.io/juniper-fm1/install/ in Chrome | The page ends with "JUNIPERFM1 0.5.1" | | |
| 0.3 | **Recovery: stock firmware.** Download M-VAVE's `FM-1.fwsc` from www.m-vave.com/download (Downloads › PC Software) and keep it. On the install page, under "Back to the stock firmware", choose that file, but do not press its Install. | The page accepts the file and enables "Install the stock firmware" | | |
| 0.4 | **Recovery: m-bootloader** (optional). Decide whether to tick "Also install m-bootloader" (PLAY at power-on for a recovery menu, REC for update mode). | – | | Ticked / not ticked |
| 0.5 | **Backup of saved patches.** JUNO FM-1 0.5 and JUNIPER 0.5.1 have **no backup function**: they weren't built with fm1-base's backup. Write down any patch you have saved and care about: its slot, name and the values on its pages. | List of saved slots | | J01's gap (see the proposal). Patches saved under 0.4 may still be in flash if you haven't saved into their 32-slot block since: the proposed 0.5.2 could read them back, so avoid saving into those blocks for now |
| 0.6 | Note one saved patch to check after the install: slot, name, two knob values. If you have none, save one now in JUNO 0.5, for example A02 with FREQ changed, then SAVE. | – | | |

## 1. Install and identity

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 1.1 | Connect the FM-1 by USB (directly, no hub). Press Install and allow MIDI. Don't unplug. | The install completes and the FM-1 restarts | | |
| 1.2 | Splash screen at power-on | "JUNIPER FM-1", whole and centred, not cut off at the edges | | Not seen on the host |
| 1.3 | Home page after boot | The patch page (slot, name, FREQ, RES, VCF ENV, CHORUS) | | |
| 1.4 | The patch from 0.6 | Still saved, with its values (0.5.1 keeps 0.5's saves) | | |
| 1.5 | The computer's MIDI device list (Audio MIDI Setup) | Shows "JUNIPER FM-1" | | |

## 2. Factory patches (J04)

Play each patch with single notes and a chord, hold for about 2 s, then release. Judge three things: whether it
sounds long enough (not a blip), whether its level matches its neighbours, and whether it clicks or distorts.

| # | Patches | What to listen for | Result | Notes |
| --- | --- | --- | --- | --- |
| 2.1 | Plucks: B28 PLUCK to B40 BELL PLUCK | These were blips in 0.4. Each should ring for half a second or more | | |
| 2.2 | Stabs: B41 BRASS STAB to B52 BIG STAB | Short but clearly a note (≈ 0.5 s), not a click | | |
| 2.3 | Arps: A15 ARP PLUCK to A27 ARP DREAM, holding a chord | Each starts the arpeggiator, and its notes are audible | | |
| 2.4 | B21 PIZZICATO, A57 SUB BOOM, B58 DEEP CHORD | These were among the shortest in 0.4 | | |
| 2.5 | A10 SWEEP, A60 SOFT SUB | These were ≈10 dB quiet in 0.5. They should now match their neighbours (A09, A11; A59, A61) | | |
| 2.6 | Chords: B53 MINOR 7 to B64 MAJOR 6; B42 HOUSE STAB, B44 RAVE STAB, B48 DUB STAB | One key plays the stored chord | | |
| 2.7 | A sample of the rest: A02 to A14 pads, A28 to A40 bass, A41 to A53 brass, B02 to B14 leads, B15 to B27 strings | – | | Note any patch that is too loud, too quiet, too short or harsh |
| 2.8 | Subs and basses at low notes: OCT− twice, then A54 to B01 | Audible on your speakers or headphones; no rumble-only patches | | |

## 3. Voice modes and controls

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 3.1 | GLO › VOICE MODE: try POLY 1, POLY 2, MONO, CHORD, UNISON and DUAL on A02 | Each behaves as named. MONO goes back to the highest key held. | | |
| 3.2 | PLAY, then play a chord, then single keys | The chord is learnt, and each key plays it | | |
| 3.3 | HOME knobs K1 to K4 on any patch | FREQ, RES, VCF ENV and CHORUS change the sound | | |
| 3.4 | Pages EDIT, SEL, ENV, LFO, FX, GLO (press again for the next page) | As listed in README "The controls" | | |

## 4. Arpeggiator ownership (J03): records current behaviour

These steps record what 0.5.1 does today. Expected values follow the 0.5.1 code. "(known)" marks behaviour the
reviewer flagged, which the proposal would change. Mark it as matching or not.

| # | Step | 0.5.1 should do | Result | Notes |
| --- | --- | --- | --- | --- |
| 4.1 | Go to A14 WIDE PAD (arp off), then PRESET +1 to A15 ARP PLUCK and hold a chord | Arp starts | | |
| 4.2 | PRESET −1 back to A14 | Arp stops | | |
| 4.3 | On A14, press ARP twice (HOLD) and hold a chord, then let go. Turn to A15, then back to A14. | **(known)** At A15 the patch takes over the hold, using its own rate and order. Back on A14 the arp **stops**, so your hold is lost. | | J03 |
| 4.4 | On A15 (arp started by the patch), press ARP once, then turn to A14 | ARP moves it to HOLD and makes it yours, so it keeps running on A14 | | |
| 4.5 | On A15, change ARP RATE on the ARP page, SAVE, change patch and come back | The saved rate comes back with A15 | | |

## 5. SysEx (J02), optional: needs a SysEx sender

This sends the same Juno-106 patch the host test uses. Use any SysEx tool, or this, run from the FM1 folder
(fm1-base's Python has mido):

    fm1-base/.venv/bin/python -c "import mido; o=mido.open_output([n for n in mido.get_output_names() if 'JUNIPER' in n][0]); o.send(mido.Message.from_hex('F0 41 30 00 0F 39 2D 00 37 00 55 00 00 19 34 3B 20 56 28 00 00 1A 18 F7'))"

`0F` is the slot, A16. Change it to `0E` for A15.

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 5.1 | While on A02, send it into A16 (`0F`), then turn to A16 | A16 is named "SYSEX A16", POLY 1, and doesn't arpeggiate | | |
| 5.2 | Turn to A17 | A17 still arpeggiates | | |
| 5.3 | On A15 with its arp running, send into A15 (`0E`) | **(known)** The sound changes, but the arp **keeps running** until the next patch change | | J02 |
| 5.4 | SAVE light after 5.1, before saving anything | **(known)** Lit: A16's block changed in memory but isn't saved. Note whether this is what you'd expect. | | |

## 6. SAVE and persistence (J05)

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 6.1 | Change patch with PRESET only | SAVE stays dark | | Changed in 0.5.1 |
| 6.2 | Turn a knob | SAVE lights | | |
| 6.3 | SAVE | The screen says "SAVED", and the light goes out | | |
| 6.4 | Power off for 10 s, then on | The saved values and the last patch come back | | |
| 6.5 | Save a patch in a different block (A33 to A64, B01 to B32 or B33 to B64), then power-cycle | It's kept, and 6.3's patch is too | | |

## 7. MIDI (J05)

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 7.1 | USB MIDI in: play notes from a DAW or keyboard | Notes play, with velocity if it's on (GLO 2nd) | | |
| 7.2 | USB MIDI in: program change 5, CC 74, pitch bend, CC 64 sustain | Patch A06; cutoff moves; pitch bends; notes sustain | | |
| 7.3 | USB MIDI out: record while playing and turning a knob | Notes and the knob's CC are recorded | | |
| 7.4 | MIDI clock from a DAW with the arp on, CLOCK set to auto (GLO 3rd) | The arp follows the tempo | | |
| 7.5 | Bluetooth MIDI (REC held 1 s toggles it), paired from a Mac or iPad | Notes in | | Optional |
| 7.6 | TRS MIDI in | Notes in | | Optional |

## 8. CPU and overload (J05)

0.5.1 has **no on-device CPU meter**. fm1-base's debug stats, which record audio blocks that ran late, aren't
switched on in this build. So this part is by ear, plus the crash log afterwards.

| # | Step | Expected | Result | Notes |
| --- | --- | --- | --- | --- |
| 8.1 | GLO: VOICES 6. FX: chorus I+II. Patch B13 SUPERSAW (unison). Hold 6-note chords with sustain, sweep FREQ and RES | No crackle, dropout or stutter; the screen keeps up | | |
| 8.2 | Same at VOICES 10 (the maximum) on A14 WIDE PAD and B23 OCTAVE STR (DUAL) | – | | Where does it start to crackle, if anywhere? |
| 8.3 | A16 ARP SEQ with HOLD, a 6-note chord, RATE at maximum, LFO to the VCF, chorus I+II | No crackle; the timing stays even | | |
| 8.4 | Bluetooth on during 8.1 | – | | |
| 8.5 | After the tests, from the FM1 folder: `fm1-base/.venv/bin/python fm1-base/tools/fm1_remote.py log` and `... crashes`. These read the FM-1's crash log only; they change nothing. | No new crash records | | Paste the output |

## 9. Afterwards

| # | Step | Result | Notes |
| --- | --- | --- | --- |
| 9.1 | Stay on 0.5.1, or go back to the stock firmware with 0.3's file | | Which one |
| 9.2 | Defects found: list them with their checklist numbers | | |
