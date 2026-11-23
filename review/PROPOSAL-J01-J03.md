# Corrective proposal for findings J01 to J03 (JUNIPER FM-1)

**Revision 2, 23 November 2026.** This answers the reviewer's decision #2 (corrections C01 to C04). It's the
canonical proposal. The earlier cloud draft is superseded.

**Status: proposal only.** Implementation is held. Nothing here has been coded, built or released. Firmware
v0.5.1, its identity `FM-1_61000501` and its repository code are unchanged.

## Scope

- **In scope:**
  - J01 (bank safety), J02 (SysEx and the arpeggiator), J03 (who owns the arpeggiator).
  - Their regression tests in the host simulator.
- **Not changed:**
  - The engine's DSP (`juno.c`), the factory patch bytes (`tools/bank.txt`, `app/factory.h`) and fm1-base.
  - The firmware identity prefix `FM-1_61`, and the patch and block layout. The saved-bank tag stays `JNB4`;
    see J01.
  - TRI-6 and TUNNEL, which stay separate.
- **Gates:**
  - The v0.5.1 hardware test (review/HARDWARE-CHECKLIST.md) is reported first.
  - No 0.5.2 code, build, commit or release until the reviewer approves this revision and has seen that evidence.

### Changes from revision 1

| | Revision 1 | Revision 2 |
| --- | --- | --- |
| Block format | New tag `JNB5` with a CRC per block | **No format change.** fm1-base's storage already checks every object with a CRC and keeps an A/B copy; a second CRC would add nothing. The tag stays `JNB4` |
| Missing and corrupt blocks | Not told apart | Told apart by a record of saved blocks kept in the settings' spare byte (C01) |
| Overwrite confirmation | SEL yes, HOME no | SAVE twice within 3 s; a full state machine (C02) |
| ARP ownership | Owner in one variable; the player's settings overwritten by a patch | OFF / PATCH-OWNED / PLAYER-OWNED, with the player's settings kept apart and restored (C03) |
| Storing an arpeggio | Plain SAVE stored whatever was playing | A dedicated gesture, SAVE + ARP, that never changes playback (C03) |
| SysEx | One rule for 0x30 and 0x31 | Separate rules (C04) |

## J01: bank safety (C01, C02)

### What a bank object can be on the FM-1

Each of the four bank objects (`OBJ_BANK0` to `OBJ_BANK3`, 32 patches each) is read with `plat_store_size` and
`plat_store_load`. fm1-base's storage (`device/storage.c`) keeps every object as an A/B copy with a CRC over its
header and its payload. A torn or damaged copy is ignored and the other one used. If neither is valid, the result is
`< 0`, the same answer as an object that was never written. So at the storage layer, **"missing" and "corrupt"
can't be told apart**; the app has to remember which blocks it has saved.

| Case | How it is recognised | 0.5.1 does | 0.5.2 would do |
| --- | --- | --- | --- |
| **Missing** (first install, never saved) | `plat_store_size < 0` **and** the block's bit in the saved-blocks record is 0 | Factory bank | Factory bank, **no message** (normal) |
| **Valid JNB4** | Size 2,564 and tag `JNB4` | Load | Load (and set its bit in the record) |
| **Valid JNB3** (0.4) | Size 2,564 and tag `JNB3` | Factory bank in RAM; the first SAVE in that block overwrites it | **Load as it is** (byte-compatible, below); rewritten as `JNB4` at its next SAVE |
| **Valid JNB2** (0.2, 0.3) | Size 2,564 and tag `JNB2` | As JNB3 | **Load as it is** (byte-compatible, below); the arp bytes are 0, so no arpeggio |
| **Unknown tag** | Size 2,564, any other tag | Factory bank in RAM; the first SAVE overwrites | **Held** (see below) |
| **Wrong size** | `plat_store_size` ≥ 0 but ≠ 2,564 | Factory bank in RAM; the first SAVE overwrites | **Held** |
| **Corrupt / lost** | `plat_store_size < 0` **but** the block's bit in the record is 1: the app saved it once and the storage can no longer give it back | Same as missing: silently replaced at the first SAVE | **Held**, with the message "BANK n UNREADABLE" |

- **Where the record lives.** The saved-blocks record is one byte, bits 0 to 3 for the four blocks. It goes in
  `settings_t.rsv[0]`, a spare byte that is 0 on every FM-1 today. The settings layout doesn't change.
  - A bit is set when that block saves successfully or loads as valid. It's never cleared.
  - On an FM-1 upgrading from 0.5.1, the record starts at 0. Every block that loads as valid sets its bit at once,
    so from then on a block that disappears counts as corrupt, not missing.
- **What "held" means:**
  - The block's RAM copy is the factory bank, so all its slots play.
  - The flash copy is never written without confirmation (C02).
  - The screen says "BANK n KEPT ASIDE" (or "UNREADABLE") once at start-up and on the first SAVE into it.
  - A backup (below) can still read the held block's original bytes.
- **Field check on every loaded patch, in any valid case:**
  - The name gets a terminating zero and only printable characters.
  - `mode` must be below `VM_N`, else POLY 1.
  - `chord_n` is at most 6.
  - The arp bytes are masked, as `patch_arp` does today.
  - The engine already clamps the panel values (`patch_clamp`). This is a repair, not a hold: the storage CRC was
    valid, so the bytes are what a firmware wrote.
- **Backup:** build with fm1-base's `backup = true` (`APP_BACKUP_MAGIC "JNPB"`, objects SETTINGS and BANK0 to
  BANK3). That way `fm1_backup.py` and the site's backup page can save and restore every patch. For a held block
  it saves the original flash bytes. This also meets the hardware test's "back up first" condition, which 0.5.1
  can't.

### JNB2 and JNB3 byte compatibility (verified from source)

- **The patch is 80 bytes in every version.**
  - `juno_patch_t` in `app/juno.h` is the same field for field in git `8e54dc3` (0.5) and `HEAD` (0.5.1). The only
    change is the header comment's product name.
  - 0.4 differed from 0.5 only in `app/app.c`'s tag and `app/factory.h`; `juno.h` wasn't touched.
  - 0.3's `app.c` (kept as `app/app.c.v0.3.bak`) asserts `sizeof(juno_patch_t) == 80` and has the same `block_t`:
    a `uint32_t` tag, then `juno_patch_t p[32]`, `BLK_N 32`. 2,564 bytes.
- **The arp bytes in older banks.**
  - `rsv[0..4]` (the arpeggio) was first written in 0.4.
  - In 0.2 and 0.3, `patch_init` zeroes the whole patch and nothing writes `rsv`, so a JNB2 patch has no
    arpeggio. That's the right reading.
- **0.2's tag.** 0.3 itself writes `JNB2` and reads 0.2's banks as `JNB2`. No 0.2 source survives, so 0.2's own
  layout is known only through 0.3's loader. A 0.2 bank that doesn't match in size is held, not loaded.
- **The test confirms it by bytes:** `bank_jnb3.txt` below plants a JNB3 block made by the 0.5.1 code itself and
  checks every edited field.

### Overwrite confirmation (C02)

A SAVE that would write a held block goes through this state machine. A SAVE that writes only non-held blocks is
unchanged.

```
          SAVE released, a held block dirty
  IDLE ─────────────────────────────────────▶ CONFIRM  (screen: "OVERWRITE BANK n? SAVE AGAIN")
   ▲                                            │
   │  SAVE released again within 3.0 s          │  any other button or encoder: cancel ("NOT SAVED")
   │  → write all dirty blocks                  │  3.0 s with no SAVE: expire ("NOT SAVED")
   │     ok:   the block stops being held       │  key presses and MIDI play as usual, no effect
   │     fail: "SAVE FAILED", still held,       │
   │           edits stay in RAM, SAVE lit      │
   └────────────────────────────────────────────┘
```

- **Nothing is written on the first SAVE,** not even the non-held dirty blocks: it's all or nothing.
- **Cancel or expiry:** goes back to IDLE. The edits stay in RAM, SAVE stays lit, and the block stays held.
- **A failed write** (`plat_store_save < 0`) never clears "held". A block clears "held" only when its write
  returns 0.
- **SAVE acts on release** (needed by C03's gesture). The confirmation counts releases.

### Regression tests (host)

These use `flash_dump`, a small test helper (`tools/test/flash_obj.py`) that rewrites one object in the image with
storage.c's header and CRC, then `flash_load` and `reboot`. Two new `expect` names: `bank_held.N` (0 or 1) and
`saved_blocks` (the record).

| Test | Steps | Expect |
| --- | --- | --- |
| `bank_missing.txt` | Fresh flash; boot | `bank_held.0..3` 0; no message; A02 is WARM PAD |
| `bank_jnb4.txt` | Edit A03 FREQ to 20, SAVE; reboot | `param11 20` on A03; `saved_blocks 1` |
| `bank_jnb3.txt` | As `bank_jnb4`, plus a chord, MONO and an arpeggio on A03; dump; helper sets block 0's tag to `JNB3`; load; reboot | All edited fields come back; `bank_held.0` 0. After SAVE on A04: block 0's tag is `JNB4` in the dump |
| `bank_jnb2.txt` | As `bank_jnb3` without the arpeggio, tag `JNB2` | Loaded; every edited field comes back; `arp 0` on A03 |
| `bank_unknown.txt` | Tag block 2 `XXXX`; reboot | `bank_held.2` 1; B05 plays the factory patch; other blocks load |
| `bank_size.txt` | Helper writes block 1 as 1,000 bytes; reboot | `bank_held.1` 1 |
| `bank_corrupt.txt` | Save in block 0 (bit set); helper damages both copies' payload CRC; reboot | `bank_held.0` 1, "UNREADABLE" said once |
| `bank_lost_vs_missing.txt` | Same damage but `saved_blocks` 0 (settings from 0.5.1) | Treated as missing: factory, no hold. This documents the one case the record can't catch |
| `confirm_first.txt` | Held block 2; edit B05; SAVE (tap) | `store_writes` unchanged; screen "OVERWRITE BANK 3? SAVE AGAIN" |
| `confirm_ok.txt` | Then SAVE again within 1 s | `store_writes` +1 or more; `bank_held.2` 0 |
| `confirm_cancel.txt` | SAVE, then tap HOME | `store_writes` unchanged; `bank_held.2` 1; SAVE lit; `reboot` → `bank_held.2` 1; dump shows the old bytes |
| `confirm_expire.txt` | SAVE, wait 3,100 ms, SAVE | The second SAVE starts a new confirmation; nothing written |
| `confirm_encoder.txt` | SAVE, turn K1 | Cancelled, as with a button |
| `confirm_fail.txt` | `flash_fail 1`; SAVE, SAVE | "SAVE FAILED"; `bank_held.2` 1; `store_tries` +1, `store_writes` unchanged; `flash_fail 0`, SAVE, SAVE → written |
| `confirm_keys.txt` | SAVE, play a key, SAVE | The key plays; the confirmation stays and the write happens |
| `fields.txt` | Helper plants a valid-tag patch with mode 9, chord_n 40 and a 12-byte name with no zero | `mode 0`, `chord_n 6` or less, the name 11 characters at most |
| `backup.txt` | Back up over the simulator's SysEx; change patches; restore | Byte-identical banks after restore |

## J02 and C04: SysEx and the arpeggiator

`app_sysex` runs with the MIDI input, not in the main loop, so it changes no arp state itself. It sets
`arp_recheck` with what happened, and the main loop, which already handles `pc_pending`, applies the rule.

| Message | What happens to the patch | Arpeggiator rule |
| --- | --- | --- |
| **0x30 into a slot that isn't playing** | The slot is reset (`sysex_slot_plain`, as in 0.5.1) | **No change**, whoever owns it |
| **0x30 into the playing slot** | The slot and the edit buffer are reset: no stored arpeggio | Treated as "choose a patch without an arpeggio": PATCH-OWNED → stop and restore the player's settings → OFF. PLAYER-OWNED (on or HOLD) → **no change**. OFF → no change |
| **0x31** (the manual: the edit buffer) | Only the Juno-106 fields change (the 16 sliders and 2 switch bytes). Mode, chord and the stored arpeggio are kept | **Never starts or stops anything.** Owner and state unchanged in every case: OFF stays off, PATCH-OWNED keeps running with the patch's settings, PLAYER-OWNED (on or HOLD) unchanged |

### Regression tests (`sysex_arp.txt`)

| # | Steps | Expect |
| --- | --- | --- |
| S1 | A15 (PATCH-OWNED, arp 1); 0x30 into slot 14 (playing); wait 30 | `arp 0`, `arp_owner 0`, the player's rate restored |
| S2 | A15; ARP (→ HOLD, PLAYER-OWNED); 0x30 into slot 14 | `arp 2`, `arp_owner 2` |
| S3 | A15 (PATCH-OWNED); 0x30 into slot 19 (A20) | `arp 1`, `arp_owner 1` |
| S4 | A15 (PATCH-OWNED); 0x31 | `arp 1`, `arp_owner 1`, `arp_rate` 70 (A15's) |
| S5 | A14; ARP twice (HOLD); 0x31 | `arp 2`, `arp_owner 2` |
| S6 | A14 (OFF); 0x31 | `arp 0` |
| S7 | Repeat S1 ten times, alternating 0x30 into the playing slot and turning to A15 | `arp` alternates 0 and 1 every time |
| S8 | `sysex_slot.txt` (existing) | Still passes |

## J03 and C03: who owns the arpeggiator

### States

| State | `juno_arp_state` | Settings in use | Meaning |
| --- | --- | --- | --- |
| **OFF** | 0 | The player's (`set.perf.arp_*`) | Not running |
| **PATCH-OWNED** | 1 | The patch's (`edit.rsv[1..4]`), loaded into the engine; the player's kept untouched in `set.perf` | Started by the patch |
| **PLAYER-OWNED** | 1 (on) or 2 (HOLD) | The player's | Started or taken over with the ARP button |

The player's global arp settings live in `set.perf` and are saved with the settings. A patch never writes them.
The engine gets the patch's settings through a copy of the performance block while PATCH-OWNED, and the player's
copy goes back to the engine when PATCH-OWNED ends. So the player's settings are restored exactly.

### Events

| Event | From OFF | From PATCH-OWNED | From PLAYER-OWNED |
| --- | --- | --- | --- |
| Choose a patch **with** a stored arpeggio (PRESET, SELECT, ALGO, program change, power-on) | Start with the patch's settings → PATCH | Switch to the new patch's settings → PATCH | No change → PLAYER |
| Choose a patch **without** one | – | Stop; restore the player's settings to the engine → OFF | No change → PLAYER |
| **ARP** button | On, with the player's settings → PLAYER (on) | Take over: keep running, **adopt the patch's settings as the player's** (written to `set.perf`) → PLAYER (on). The next press → HOLD | on → HOLD → OFF, as 0.5.1 |
| Turn a knob on the ARP page | Changes the player's settings | Changes the **patch's** settings (`edit.rsv`; SAVE lights). The player's are untouched | Changes the player's settings |
| SysEx 0x30 / 0x31 | See J02 above | See J02 above | See J02 above |
| Plain **SAVE** | Saves the patch with its stored arp flag **as it is** (no change from what's playing) | Same | Same |

- **The ARP-button takeover is the one place a patch's settings become the player's.** It's the player's own
  action, and the screen says "ARPEGGIO ON (YOURS)".
- **The ARP page** shows "PATCH" or "YOURS" beside the state, so it's always clear whose settings a knob changes.

### Storing or removing a patch's arpeggio: the gesture

> **Hold SAVE, press ARP, release both.** This toggles the stored-arpeggio flag of the patch being edited. It
> **does not change what is playing**, and it doesn't save. The screen says "PATCH ARP: STORED" or
> "PATCH ARP: NONE", and SAVE lights because the patch is edited. Then **SAVE** (a normal press and release)
> writes it.

- **When the flag is turned on,** the patch's arp settings are copied from the settings now in use: the player's
  if PLAYER-OWNED or OFF, the patch's if PATCH-OWNED.
- **When it's turned off,** the arp bytes are cleared.
- **It never toggles playback:**
  - ARP pressed while SAVE is held never reaches `arp_button`.
  - SAVE pressed while ARP is held is a plain SAVE once both are released.
  - SAVE acts on **release**, and a release that ended a SAVE + ARP chord does nothing more.
- **Plain SAVE never changes the stored-arpeggio flag.** In 0.5.1, SAVE copied whatever was playing into the patch,
  which is how a playing arpeggio could become stored by accident.
- **A new arp patch, start to finish:** choose a patch, ARP to start, set the ARP page, SAVE + ARP ("STORED"),
  SAVE.
- **Removing it:** on that patch, SAVE + ARP ("NONE"), then SAVE. The arpeggio playing now goes on until you stop
  it or change patch, because removing the flag doesn't touch playback.

### Regression tests (`arp_owner.txt`, `arp_gesture.txt`)

These need `expect arp_owner` (0 OFF, 1 PATCH, 2 PLAYER), `expect arp_rate` (the rate the engine uses),
`expect player_arp_rate` (`set.perf`) and `expect patch_arp` (`edit.rsv[0]`).

| # | Steps | Expect |
| --- | --- | --- |
| O1 | A14; ARP twice (HOLD); turn to A15 | `arp 2`, `arp_owner 2`, `arp_rate` = `player_arp_rate` (not A15's 70) |
| O2 | Then turn to A14 | `arp 2` (the hold survives) |
| O3 | A14 with player rate 30; turn to A15 | `arp 1`, `arp_owner 1`, `arp_rate 70`, `player_arp_rate 30` |
| O4 | Then turn to A14 | `arp 0`, `arp_owner 0`, `arp_rate 30` (restored) |
| O5 | A15 (PATCH); turn to A16 | `arp_owner 1`, `arp_rate 75` (A16's) |
| O6 | A15 (PATCH, 70); ARP (takeover) | `arp 1`, `arp_owner 2`, `player_arp_rate 70`; turn to A14 → `arp 1` |
| O7 | A15 (PATCH); ARP-page knob rate to 90 | `arp_rate 90`, `player_arp_rate` unchanged, SAVE lit |
| O8 | HOLD on A14; ARP (→ OFF) | `arp 0`, `arp_owner 0` |
| O9 | A15 (PATCH); program change 13 over MIDI | `arp 0` |
| O10 | Power-on with A15 selected | `arp 1`, `arp_owner 1` |
| O11 | `arp_patch.txt` (existing) | Still passes |
| G1 | A02 (no arp), ARP on (PLAYER, rate 40); hold SAVE, tap ARP, release SAVE | `arp 1` (unchanged), `patch_arp 1`, `store_writes` unchanged, SAVE lit |
| G2 | Then SAVE; turn to A03; ARP until OFF; back to A02 | `arp 1`, `arp_owner 1`, `arp_rate 40` |
| G3 | On A02 (PATCH); hold SAVE, tap ARP, release; SAVE | `patch_arp 0`; `arp 1` still playing (unchanged); turn away and back → `arp 0` |
| G4 | A02 with ARP on; plain SAVE | `patch_arp` unchanged (0): plain SAVE stores no arpeggio |
| G5 | Hold ARP, press SAVE, release both | Exactly a plain SAVE; `arp` unchanged |
| G6 | Hold SAVE, tap ARP twice, release | `patch_arp` back to what it was (toggled twice) |

## Unchanged and required

- **All 37 existing host checks** (basic 10, features 20, arp_patch 3, sysex_slot 4) must pass unchanged.
- **The factory bank:** `app/factory.h`'s SHA-256 stays `3df9a329fbdb77a57f7a458655486eb45b8cd7b14e4e3c0811a164b5953cd39d`.
- **No DSP change:** a render of all 128 patches (the length and level measurement used for 0.5) must be
  sample-identical before and after the change. `juno.c` isn't touched.
- **No change to the firmware identity** (`FM-1_61`), the block layout or the settings layout.

## Delivery, only after approval and the v0.5.1 hardware report

1. A branch `fix/j01-j03` on fergroan-sketch/juniper-fm1. First commit: the new tests and expect names only, which
   must fail on 0.5.1. Then the fix, which makes them pass.
2. The full host suite: 4 existing scripts plus about 40 new cases above.
3. A release build of 0.5.2 under `FM-1_61`, with RAM checked against the budget (expected: a few bytes more).
4. A hardware recheck of checklist sections 4 to 6, and a backup and restore using the new backup.
