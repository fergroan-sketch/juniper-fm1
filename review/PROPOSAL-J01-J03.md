# Corrective proposal for findings J01 to J03 (JUNIPER FM-1)

**Status: proposal only.** The reviewer's ruling of 23 November 2026 authorises preparing this proposal, not
implementing it. Nothing here has been coded. Firmware v0.5.1 and its repository are unchanged.

## Scope

- **In scope:** the three findings J01 (bank safety), J02 (SysEx and the arpeggiator) and J03 (who owns the
  arpeggiator), each with its regression tests in the host simulator.
- **Not in scope:**
  - Any change to the factory bank's sounds (J04 waits for the hardware listening).
  - CPU work (J05 waits for the hardware measurements).
  - New features, the Firmware Hub, and TRI-6 and TUNNEL, which stay separate.
- **Version:** if this is approved, the release would be 0.5.2. It keeps identity prefix `FM-1_61` and saved-bank
  tag `JNB4`. It's a revision, so it needs the reviewer's go-ahead before any code is written.

## J01: bank safety

### What 0.5.1 does (`app/app.c` `banks_load`, `save`)

- **At start-up:** each of the four 32-patch blocks is read from flash. If it's short, or its tag isn't `JNB4`,
  the RAM copy is filled from the factory bank. The flash copy isn't touched at that point.
- **The first SAVE:** writes the whole RAM block of the slot being saved, plus any block loaded by SysEx. A block
  that was refilled because of its tag is then overwritten with factory patches plus the one being saved. The
  user's old patches in that block are gone, with no warning.
- **Known older banks are discarded too.** Blocks from 0.4 (`JNB3`) and 0.2/0.3 (`JNB2`) have exactly the same
  layout as `JNB4` (4 + 32 × 80 bytes). 0.5 chose to discard them, but nothing makes that necessary.
- **Corruption isn't detected.** A block whose tag is right but whose contents are damaged is loaded as it is.
  - The engine clamps every value (`patch_clamp`), so the sound can't misbehave.
  - But the app's copy (the name, mode and chord) isn't checked. A name with no terminating zero could run on.
- **There's no backup.** JUNIPER isn't built with fm1-base's `backup = true`, so patches can't be saved to a
  computer.

### Proposed

1. **A checksum per block.**
   - `block_t` becomes `{magic, crc32, p[32]}`: 2,568 bytes, under `PLAT_STORE_MAX` (3,840).
   - The new tag is `JNB5`, written only by 0.5.2.
   - On load, a `JNB5` block whose CRC doesn't match is **not** used.
2. **Known older blocks are kept.**
   - `JNB2`, `JNB3` and `JNB4` blocks are loaded as they are.
   - They're rewritten as `JNB5` on their next SAVE, which makes this a migration, not a reset.
   - The factory bank fills only empty blocks (never-saved flash).
3. **Unknown or damaged blocks are protected.**
   - Such a block is marked "held". Its RAM copy is the factory bank, so the slots still play.
   - Its flash is never written without a question first. SAVE into a held block shows "OVERWRITE OLD BANK?
     SEL yes, HOME no". HOME keeps the old data.
   - The screen says "BANK n KEPT ASIDE" once at start-up.
4. **Every loaded patch is checked.**
   - The name is made terminated and printable.
   - `mode` must be below `VM_N`, and `chord_n` 6 or less.
   - The arp bytes are masked, as `patch_arp` already does.
5. **Backup.**
   - Build with `backup = true` (`APP_BACKUP_MAGIC "JNPB"`, objects SETTINGS and BANK0 to BANK3).
   - Then `fm1-base/tools/fm1_backup.py backup FILE` and the site's backup page can save and restore all 128
     patches.
   - This is what the "backup first" step of a hardware test needs.

### Regression tests (host)

These use `flash_dump`, a small Python edit of the image, `flash_load` and `reboot`.

| Test | Steps | Expect |
| --- | --- | --- |
| `bank_migrate.txt` | Save A03 with FREQ changed; dump; rewrite block 0's tag to `JNB3`; load; reboot | A03 keeps the changed FREQ, so the patch survives |
| `bank_corrupt.txt` | Save A03; dump; flip one byte inside block 0; load; reboot | A03 plays the factory GLASS PAD; the screen says "BANK 1 KEPT ASIDE"; after SAVE on A05 → the prompt; HOME → dump again shows the old bytes unchanged |
| `bank_unknown.txt` | Tag block 2 `XXXX`; reboot; SAVE in B05; SEL | The block is written only after SEL |
| `bank_fields.txt` | Plant a patch with mode 9, chord_n 40 and a 12-byte name with no zero; reboot; select it | mode is 0 or clamped; chord_n is 6 or less; the name shows 11 characters at most |
| `backup.txt` | Back up and restore over the host's SysEx | The bytes are the same after restoring |

Two `expect` names are needed in `app_host_expect`: `bank_held.N` and `name_len`.

## J02: SysEx into the playing slot

### What 0.5.1 does

`app_sysex` (`F0 41 30` into the current slot, or `F0 41 31` into the manual patch) replaces `edit` and
`juno_set_patch`, but never calls `patch_arp`. If the old patch had started the arpeggiator, it keeps running even
though the new patch has no arpeggio. That runs until the next patch change or ARP. SysEx arrives with the MIDI
input, not in the main loop, so it shouldn't touch `set.perf` directly.

### Proposed

- Add `static volatile uint8_t arp_recheck;`. The two `edit = t` branches set it.
- The main loop, where it already handles `pc_pending`, calls `patch_arp()` when it is set.
- `patch_arp` then applies the ownership rules below. A patch-started arpeggio stops, because the new patch has
  none. A user-started one goes on.

### Regression tests

| Test | Steps | Expect |
| --- | --- | --- |
| `sysex_arp.txt` 1 | Turn to A15 (arp 1, patch-owned); send a patch into slot 14; wait 30 | arp 0 |
| `sysex_arp.txt` 2 | On A15, press ARP (hold, user-owned); send a patch into slot 14 | arp 2 |
| `sysex_arp.txt` 3 | On A15 (patch-owned); send a patch into A20 (`0x13`) | arp 1: another slot doesn't stop it |
| `sysex_arp.txt` 4 | On A15 (patch-owned); send `F0 41 31` (manual) | arp 0 |
| `sysex_arp.txt` 5 | Repeat 1 ten times, alternating A15 and A14 | arp alternates 1 and 0 every time (deterministic) |

## J03: who owns the arpeggiator

### What 0.5.1 does

The only state is `arp_by_patch`, a 0 or 1.

- **An arp patch while the arp is already running (on or hold):** the patch overwrites the user's order, range,
  rate and gate, and claims ownership. The next patch without an arpeggio then **stops** an arpeggio the player
  had started or held.
- **ARP:** pressing it hands ownership to the player.
- **SAVE:** stores the arpeggio, whoever started it.

### Proposed rules

The arpeggiator has one owner: NONE (off), PATCH or PLAYER.

| Event | Owner NONE | Owner PATCH | Owner PLAYER |
| --- | --- | --- | --- |
| Choose a patch **with** an arpeggio | Start it with the patch's settings → PATCH | Switch to the new patch's settings → PATCH | **No change:** the player's arp and settings go on → PLAYER |
| Choose a patch **without** one | – | Stop → NONE | **No change** → PLAYER |
| ARP button (off › on › hold) | on → PLAYER | Next state (on › hold) → PLAYER | Next state; off → NONE |
| SysEx into the playing slot | As "choose a patch" | As "choose a patch" | No change |
| Program change | As "choose a patch" | As "choose a patch" | No change |
| SAVE | The patch's arp byte = 0 | Stores the running arp → the patch keeps its arpeggio | Stores the running arp → the patch becomes an arp patch (how a player makes one) |
| Power-on with an arp patch selected | Start → PATCH | – | – |

- **In short:** the player always wins. A patch can start, retune and stop only what a patch started. Hold is a
  player state, so no patch change ever ends a hold.
- **Code:**
  - `arp_by_patch` becomes `arp_owner` (0 NONE, 1 PATCH, 2 PLAYER).
  - `patch_arp` follows the table.
  - `arp_button` sets PLAYER, or NONE when it switches the arp off.
- **Screen:** the ARP page shows "PATCH" or "YOURS" beside ON/HOLD.

### Regression tests

These need `expect arp_owner` and `expect arp_rate` in `app_host_expect`.

| Test | Steps | Expect |
| --- | --- | --- |
| `arp_owner.txt` 1 | A14; ARP twice (hold); turn to A15 | arp 2, arp_owner 2, arp_rate as before (not A15's 70) |
| `arp_owner.txt` 2 | Then turn to A14 | arp 2 (the hold survives) |
| `arp_owner.txt` 3 | A15 (owner 1); turn to A16 | arp 1, arp_owner 1, arp_rate 75 (A16's) |
| `arp_owner.txt` 4 | A15; ARP (→ hold, owner 2); turn to A14 | arp 2 |
| `arp_owner.txt` 5 | Hold on A14; ARP (→ off) | arp 0, arp_owner 0 |
| `arp_owner.txt` 6 | A15; program change 13 (A14) over MIDI | arp 0 |
| `arp_owner.txt` 7 | A02; ARP on (owner 2); SAVE; turn to A03; ARP until off (owner 0); turn back to A02 | arp 1, arp_owner 1: A02 is now an arp patch and starts its own |
| `arp_patch.txt` (existing) | Unchanged | Still passes |

## Delivery, if approved

1. One change set on a branch `fix/j01-j03` of fergroan-sketch/juniper-fm1, with the tests first, so they fail on
   0.5.1 and pass after.
2. Host checks: the four existing scripts plus the new ones above. That's about 12 scripts, and all must pass.
3. A release build of 0.5.2 under `FM-1_61`, with RAM checked against the budget. The block grows by 4 bytes,
   ×4 in pool RAM.
4. No change to `tools/bank.txt` or `app/factory.h`.
5. Then a hardware recheck of sections 4 to 6 of the checklist.
