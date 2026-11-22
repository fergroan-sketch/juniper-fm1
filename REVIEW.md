# JUNO FM-1 0.5.1: reviewer's report

22 November 2026. This covers releases 0.4, 0.5 and 0.5.1: the 128-patch factory bank, arpeggios stored per patch, the
opened-up envelopes and filters, and the installer site. Version 0.3 is the baseline.

## In brief

| | |
| --- | --- |
| Package | `site/firmware/junofm1-0.5.1.fwsc`: 616,231 bytes, identity `FM-1_61000501` |
| SHA-256 | `89cad11782e2e5193611d6cc1a9cab7c66badd21d1e7a7ecad9850a3e8de5f86` |
| App image | 241,732 bytes |
| RAM | .data+.bss 77,816 of 98,304 B (79%); .ram_text 7,604 of 32,768 B; pool 109,472 of 344,064 B |
| Build checks | build.py's own checks pass: the radio's RAM code, no inlined 64-bit multiplies, no soft-double, register access through felucca-bsp only |
| Host checks | all 30 `expect`s in basic.txt and features.txt pass, plus the 3 in arp_patch.txt and the 4 in sysex_slot.txt |
| Licence | MPL-2.0 (LICENSE). fm1-base and felucca-bsp parts keep their own licences (site/firmware/*.txt) |

## What changed since 0.3

1. **Factory bank.** There are 128 new patches, generated from `tools/bank.txt` into `app/factory.h` by
   `tools/mk_factory.py`. They're grouped by kind: init, pads, arps, bass, brass, sub, leads, strings, plucks,
   stabs and chords (see PRESETS.md). Every patch was written for this firmware; none is taken from an instrument
   or an emulation.
2. **Per-patch arpeggio.** Patches A15 to A27 store an arpeggio (on, order, range, rate, gate) in the patch's
   spare bytes (`rsv[0..4]`).
   - Choosing such a patch starts the arpeggiator with those settings.
   - The next patch without an arpeggio stops it, but only if a patch started it. Pressing ARP hands control back
     to the player.
   - SAVE stores the arpeggio as it is playing at that moment.
3. **Chord patches.** B53 to B64 and three stabs carry a stored chord and play it from one key in CHORD mode.
4. **Bank identity.** `BANK_MAGIC` is now `"JNB4"`, so on first start a saved bank from 0.2 to 0.4 is replaced by
   the factory bank. 0.3's "refill only the INIT slots" logic is gone: a block is either loaded whole or refilled
   from FACTORY.
5. **0.5 sound pass.** 0.4's decays, releases and cutoffs were far too short for the engine's exponential curves.
   For example, slider value 40 is 25 ms of decay, and 30 on the cutoff slider is 44 Hz. Many plucks and stabs
   were blips of 30 to 90 ms. 0.5 lengthens decays and releases, raises cutoffs and evens out levels across the
   bank. 0.4's bank is kept in `tools/bank-0.4.txt`.
6. **0.5.1 fixes.**
   - **SysEx into a slot.** A Juno-106 patch sent into a slot (`F0 41 30`) now starts clean: POLY 1, no unison
     detune, no chord, no arpeggio. It's named `SYSEX A15` and so on, after its slot (`sysex_slot_plain`). Before,
     it kept the old slot's mode, chord, arpeggio and name.
   - **SAVE light.** It now lights only for unsaved patch edits (the edit buffer or a block loaded by SysEx), not
     after a patch change alone.
   - **Levels.** SWEEP and SOFT SUB had their cutoff and level raised. `BANK_MAGIC` stays `"JNB4"`, so what 0.5
     saved is kept.
7. **Installer site.** `site/` holds the installer, the 0.5 package and m-bootloader 2.4. It's served by
   "Open JUNO installer.command" on http://localhost:8106. The front page and README now show the installer
   (`app/site/img/install.png`).

## How it was checked

- **Factory table.** factory.h matched the SHA-256 checksums supplied with each bank (0.4: `f1a4928c…7bee`;
  0.5: `248669c0…edb0`; 0.5.1: `4745dec4…67b6`). A `_Static_assert` keeps the table at exactly 128 patches, and another checks that the
  arpeggio fits in the spare bytes.
- **Host simulator.** I ran `sh build.sh host`. basic.txt and features.txt cover playing, pages, voice modes,
  chord memory, the arpeggiator, Juno-106 SysEx in and slider CCs. arp_patch.txt checks that turning to A15
  starts the arpeggiator and turning back to A14 stops it. sysex_slot.txt sends a Juno-106 patch into A16
  (an arp slot) and checks that it no longer arpeggiates and is POLY 1, and that the next arp patch, A17, still does.
- **Patch lengths.** I played every patch on the host (C4 held 0.5 s, then released) and timed how long it stays
  within 40 dB of its peak.
  - **0.4:** 26 patches lasted under 0.3 s, and several were silent while the key was held.
  - **0.5:** every patch lasts at least 0.49 s (median 0.76 s). The shortest are the gated ARP SQUARE, INIT and
    ORGAN, which stop with the key, as they should.
- **Site.** Served locally: /install/, the .fwsc and the front page returned 200, and the .fwsc on the site has the
  same SHA-256 as the build. The installer page loads the 0.5 metadata and enables Install (see the screenshot).

## Not checked

- **On the FM-1 itself.** No install, CPU load or audio check was run from this machine; all checks above are
  host-side. The build checks memory but not the device's CPU budget at 6 voices with chorus. Fergus's listening
  on the FM-1 is the only on-device evidence so far (it found 0.4's blips).
- **The emulator** (`emu/`) is not built or included, because it needs Emscripten.
- **Bluetooth MIDI, TRS MIDI and USB audio.** These are not exercised by the host scripts beyond what fm1-base's
  stand-ins cover.

## Known issues and points for review

1. **Saved patches are lost on update.** On first start, 0.5 replaces any bank saved by 0.2 to 0.4 (the
   `JNB4` change). That's intended, but it should be said where people install from. README "Installing" says it.
2. **Juno-106 SysEx into an arp slot (fixed in 0.5.1).** The slot is now reset: see "0.5.1 fixes" above. A
   patch sent into the slot that is playing replaces the edit buffer, but an arpeggio that slot had already
   started keeps running until the next patch change or ARP.
3. **Arpeggiator already running.** If the arpeggiator is already running (on or hold) when an arp patch is
   chosen, the patch takes it over, and the next patch without an arpeggio turns it off. That stops a hold the
   player had set by hand before.
4. **SWEEP and SOFT SUB levels (changed in 0.5.1, now a little loud).** These were about 10 dB quiet. Measured
   on the host after 0.5.1, they peak at −25.8 and −22.9 dBFS, while most of the bank sits around −32 and the
   loudest other patches reach about −24. SOFT SUB is now the loudest patch, and SWEEP is among the louder ones.
   Lowering their level slider (about 100 for SOFT SUB, 110 for SWEEP) would bring them into line.
5. **Placeholder links.** `fm1.toml` still has `example.invalid` for `repo` and `install`, so the page footer and
   firmware.json point nowhere. They should be set once the GitHub repository (and Pages, if used) exists.
6. **Stale demo renders.** `demo/` (not in git) holds WAVs of the old 18-patch bank. `app/tests/demo.txt` can
   render the current bank.
7. **Other bank files.** `tools/bank-0.5-alt.txt` and `tools/open_up.py` are a second opened-up bank I made before
   the chosen one arrived. It isn't built; it's kept for comparison.
8. **Naming.** The firmware uses the name "JUNO", a Roland trademark. The README and site say it is unofficial
   and not affiliated with Roland or M-VAVE, and the engine is written from the published design with no code or
   data from Roland or any emulation. Whether that's enough for a public repository is the owner's call.

## Files to read first

- `app/app.c`: `FACTORY` and `patch_init`, `banks_load`, `patch_arp` and `patch_select`, `save`, `arp_button`.
- `app/factory.h` and `tools/bank.txt`: the bank, one patch per line.
- `app/tests/arp_patch.txt` and `app/tests/sysex_slot.txt`: the new host tests.
- `PRESETS.md`: the patch list.
