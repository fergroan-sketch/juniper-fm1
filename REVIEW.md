# JUNIPER FM-1 0.5.1: reviewer's report

22 November 2026. This covers releases 0.4, 0.5 and 0.5.1: the 128-patch factory bank, arpeggios stored per patch, the
opened-up envelopes and filters, the installer site and the online player. Version 0.3 is the baseline.
Up to 0.5 the firmware was called JUNO FM-1; 0.5.1 is published as JUNIPER FM-1.

## In brief

| | |
| --- | --- |
| Package | `site/firmware/juniperfm1-0.5.1.fwsc`: 616,231 bytes, identity `FM-1_61000501` |
| SHA-256 | `2e8e709fd93fa2425b4537a9c1d9410ad48af288419925c3e7df1981cc11bc2c` |
| App image | 241,764 bytes |
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
   - **Levels.** SWEEP and SOFT SUB had their cutoff raised and their level set to 110 and 100. `BANK_MAGIC`
     stays `"JNB4"`, so what 0.5 saved is kept.
7. **The name.** The firmware is now JUNIPER FM-1: on the splash, crash and safe-mode screens, over USB, in
   firmware.json, the site and the docs. The package is `juniperfm1-<version>.fwsc`. The identity prefix stays
   `FM-1_61`, so it installs over JUNO FM-1 0.5 and keeps its saves. Source files and symbols (`juno.c`,
   `juno_*`) keep their names.
8. **Site and player.** `tools/make_pages.sh` builds `site/` for GitHub Pages: the front page, the installer, the
   0.5.1 package, m-bootloader 2.4, and `play/`, which runs the package in fm1-emu from the site's own copy.
   `fm1.toml` points `repo` and `install` at fergroan-sketch/juniper-fm1 and its Pages site. `fmhub.json` and
   `card.png` (480 × 480) are there for the firmware hub. Offline, "Open JUNIPER installer.command" serves `site/`
   on http://localhost:8106.

## How it was checked

- **Factory table.** factory.h matched the SHA-256 checksums supplied with each bank (0.4: `f1a4928c…7bee`;
  0.5: `248669c0…edb0`; 0.5.1 with the new levels and name: `3df9a329…d39d`). A `_Static_assert` keeps the table at exactly 128 patches, and another checks that the
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
  same SHA-256 as the build. The installer page loads the 0.5.1 metadata and enables Install (see the screenshot).

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
4. **SWEEP and SOFT SUB levels (fixed in 0.5.1).** These were about 10 dB quiet in 0.5. On the host they now
   peak at −28.3 and −27.0 dBFS, inside the bank's range (most patches sit around −32, the loudest at about −24).
5. **Links not live yet.** `fm1.toml`, firmware.json, fmhub.json and the site point at
   github.com/fergroan-sketch/juniper-fm1 and fergroan-sketch.github.io/juniper-fm1. They work once the
   repository is pushed and Pages is on. The player (`play/`) can only be tried online: fm1-emu fetches the
   package from the Pages copy.
6. **Stale demo renders.** `demo/` (not in git) holds WAVs of the old 18-patch bank. `app/tests/demo.txt` can
   render the current bank.
7. **Other bank files.** `tools/bank-0.5-alt.txt` and `tools/open_up.py` are a second opened-up bank I made before
   the chosen one arrived. It isn't built; it's kept for comparison.
8. **Naming.** Renamed JUNIPER FM-1 for the public repository, so it no longer uses Roland's "JUNO" as a brand.
   The Juno-106 is named only as what inspired it and as the source of its SysEx format, with the line
   "Unofficial: not affiliated with or endorsed by Roland or M-VAVE".

## Files to read first

- `app/app.c`: `FACTORY` and `patch_init`, `banks_load`, `patch_arp` and `patch_select`, `save`, `arp_button`.
- `app/factory.h` and `tools/bank.txt`: the bank, one patch per line.
- `app/tests/arp_patch.txt` and `app/tests/sysex_slot.txt`: the new host tests.
- `PRESETS.md`: the patch list.
- `app/site/play.html` and `tools/make_pages.sh`: the online player and the Pages site.
