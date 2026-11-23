# JUNIPER FM-1 0.5.1: reviewer's report

23 November 2026. This report covers JUNIPER FM-1's whole history so far, from the first draft to the public
0.5.1 release. Up to 0.5 it was called JUNO FM-1. In 0.5.1 it was renamed JUNIPER FM-1 and published at
https://github.com/fergroan-sketch/juniper-fm1.

## In brief

| | |
| --- | --- |
| Package | `site/firmware/juniperfm1-0.5.1.fwsc`: 616,231 bytes, identity `FM-1_61000501` |
| SHA-256 | `2e8e709fd93fa2425b4537a9c1d9410ad48af288419925c3e7df1981cc11bc2c` |
| App image | 241,764 bytes |
| RAM | .data+.bss 77,816 of 98,304 B (79%); .ram_text 7,604 of 32,768 B; pool 109,472 of 344,064 B |
| Build checks | build.py's own checks pass: the radio's RAM code, no inlined 64-bit multiplies, no soft-double, register access through felucca-bsp only |
| Host checks | 37 of 37 pass: basic.txt 10, features.txt 20, arp_patch.txt 3, sysex_slot.txt 4 |
| Online | Release [v0.5.1](https://github.com/fergroan-sketch/juniper-fm1/releases/tag/v0.5.1) (pre-release), [installer](https://fergroan-sketch.github.io/juniper-fm1/install/), [player](https://fergroan-sketch.github.io/juniper-fm1/play/) |
| Licence | MPL-2.0 (LICENSE). fm1-base and felucca-bsp parts keep their own licences (site/firmware/*.txt) |

## History

Every package from the drafts to 0.5.1 has the identity prefix `FM-1_61`. That's why each release installs
over the one before.

| Release | Built (22 Nov 2026) | Name | Saved-bank tag | What it brought |
| --- | --- | --- | --- | --- |
| drafts | 14:52 | JUNO FM-1 | not known | The engine and app; 18 patches (A02 to A18), the rest INIT |
| 0.2 | 15:36 | JUNO FM-1 | not known | The first numbered package, with installer site and offline installer |
| 0.3 | 15:59 | JUNO FM-1 | `JNB2` | 128 factory patches by kind; saved banks' INIT slots refilled |
| 0.4 | 16:10 | JUNO FM-1 | `JNB3` | A new 128-patch bank, arpeggios in patches, chord patches |
| 0.5 | 16:29 | JUNO FM-1 | `JNB4` | The bank opened up: 0.4's patches were blips |
| 0.5.1 | 23:44 | JUNIPER FM-1 | `JNB4` | SysEx and SAVE-light fixes, two levels, the rename, GitHub, Pages, player |

The drafts to 0.3 were built in earlier sessions. Their entries here come from what they left behind: the
packages in `app/build/`, 0.3's `app.c` (kept as `app/app.c.v0.3.bak`, not in git) and its PRESETS.md. This
report checked 0.4 to 0.5.1 directly.

### The drafts (JUNO FM-1)

- **Engine:** per voice, one DCO (saw, pulse with PWM from the LFO, by hand or from the envelope, sub-octave square,
  noise, 16'/8'/4'), then a resonant four-pole low-pass with envelope, LFO and key follow, then the VCA. After the
  voices: the high-pass, then the stereo chorus I, II, I+II. Ranges follow the Juno-106 service notes.
- **Features, following superOS-106's manual:** voice modes POLY 1, POLY 2, MONO, CHORD, UNISON and DUAL, from 1
  to 10 voices. Also the arpeggiator, velocity, a second envelope for the filter, per-voice detune, vibrato and
  PWM, LFO sync, scales, the Juno-106's SysEx and superOS-106's controller numbers.
- **Patches:** 18 factory patches (WARM PAD, BRASS, STRINGS, BASS and so on) in A02 to A18. The rest were INIT.
- **Tested on:** fm1-base's host simulator only. README called the last of these the "second draft".

### 0.2

The first numbered package (`junofm1-0.2.fwsc`). The offline installer dates from the same build: the site in
`site/`, served by "Open JUNO installer.command" on http://localhost:8106. Nothing left behind shows whether 0.2
already tagged its saved bank `JNB2`. 0.3 treats a `JNB2` bank as 0.2's.

### 0.3

- **Bank:** filled all 128 slots, laid out by kind. A01 is INIT, and A02 to A18 keep the first draft's set. Then
  come bass, pads, strings, brass, keys, organ, leads, plucks, "synth" and effects.
- **Saved banks:** the tag was `JNB2`. A bank saved by 0.2 was kept, and only its INIT slots were refilled with
  the new patches.

### 0.4

1. **New factory bank.** All 128 patches were replaced, now generated from `tools/bank.txt` into `app/factory.h`
   by `tools/mk_factory.py`. They're grouped by kind: init, pads, arps, bass, brass, sub, leads, strings, plucks,
   stabs and chords (see PRESETS.md). Every patch was written for this firmware; none is taken from an instrument
   or an emulation.
2. **Arpeggios stored in patches.** A15 to A27 store an arpeggio (on, order, range, rate, gate) in the patch's
   spare bytes (`rsv[0..4]`).
   - Choosing such a patch starts the arpeggiator with those settings.
   - The next patch without an arpeggio stops it, but only if a patch started it. Pressing ARP hands control back
     to the player.
   - SAVE stores the arpeggio as it is playing at that moment.
3. **Chord patches.** B53 to B64, plus HOUSE, RAVE and DUB STAB, carry a stored chord and play it from one key.
4. **Saved banks.** The tag became `JNB3`, so the new bank loads in place of what 0.2 and 0.3 saved. 0.3's
   "refill only the INIT slots" logic is gone: a block is either loaded whole or refilled from FACTORY.

### 0.5

- **Why:** after installing 0.4, Fergus found many patches too short, "just a blip type sound".
- **The cause:** the engine's envelope and cutoff curves are exponential, and 0.4's slider values were far too
  low for them. Slider 40 is 25 ms of decay, and 30 on the cutoff slider is 44 Hz.
- **The size of it:** 26 patches (plucks, stabs and others) lasted 30 to 300 ms. Several were silent while the
  key was still held.
- **The fix:** 0.5 lengthens decays and releases, raises cutoffs and evens out levels across the bank. 0.4's bank
  is kept in `tools/bank-0.4.txt`.
- **Saved banks:** the tag became `JNB4`.

### 0.5.1

- **SysEx into a slot.** A Juno-106 patch sent into a slot (`F0 41 30`) now starts clean: POLY 1, no unison
  detune, no chord and no arpeggio. It's named after its slot, `SYSEX A16` and so on (`sysex_slot_plain`).
  Before, it kept the old slot's mode, chord, arpeggio and name, so a Juno patch loaded into A15 to A27 still
  arpeggiated.
- **SAVE light.** It now lights only for unsaved patch edits (the edit buffer, or a block loaded by SysEx). A
  patch change alone no longer lights it.
- **Levels.** SWEEP and SOFT SUB were about 10 dB quiet in 0.5. Their cutoff went up and their level was set to
  110 and 100. On the host they now peak at −28.3 and −27.0 dBFS. Most of the bank sits around −32, and the
  loudest patches reach about −24.
- **Saved banks:** the tag stays `JNB4`, so what 0.5 saved is kept.
- **Published:** with the rename and GitHub (below).

## The rename: JUNO FM-1 to JUNIPER FM-1

- **Why:** "JUNO" is Roland's trademark. Before making the repository public, Fergus chose a name of its own,
  JUNIPER FM-1.
- **On the device:** APP_NAME is now "JUNIPER FM-1", which is what the splash, crash and safe-mode screens show.
  The USB product name changed too: `usb_product` and `usb_manufacturer` are now `juniperfm1`.
- **Firmware file and hub data:** the package is `juniperfm1-<version>.fwsc` (fm1.toml `name = "JUNIPERFM1"`,
  `title`). firmware.json and fmhub.json carry the new name.
- **Everything else visible:** the screen-reader words, the site and installer, the player, README, PRESETS.md,
  this report, and the offline installer, now "Open JUNIPER installer.command".
- **Kept the same:**
  - The identity prefix `FM-1_61`, so 0.5.1 installs over JUNO FM-1 0.5.
  - The saved-bank tag `JNB4`, so 0.5's saves survive.
  - Source files and symbols (`juno.c`, `juno_*`), which nobody sees on the device.
  - The local folder name `juno-fm1`. `build.sh` and `tools/make_pages.sh` now work whatever the folder is called.
- **What the text says now:** "JUNO" isn't used as a brand anywhere. The Juno-106 is named only as the
  inspiration and as the source of the SysEx format, with the line "Unofficial: not affiliated with or endorsed by
  Roland or M-VAVE". The old screen-reader words file (`bab0e64309.json`) stays on the site, because installed
  JUNO FM-1 firmware still looks it up by that name.
- **Not renamed:** fm1-base builds the installer's heading from the short build name, so it reads
  "Install JUNIPERFM1". TRI-6 FM-1's installer works the same way.

## Publishing

- **Repository:** [fergroan-sketch/juniper-fm1](https://github.com/fergroan-sketch/juniper-fm1), public, with
  four commits on `main`: 0.5, this report's first version, JUNO FM-1 0.5.1, and JUNIPER FM-1 0.5.1.
  - **In git:** the source, tests, the bank and its tools, the built site, and an MPL-2.0 LICENSE.
  - **Left out:** `app/build/`, the `.bak` files and `demo/`. `demo/` holds stale renders of the first draft's
    18 patches.
- **GitHub Pages:** served from the `gh-pages` branch, which is `site/` split out with `git subtree split`. It
  has the front page, the installer, the 0.5.1 package, m-bootloader 2.4, and `play/`.
  - **`play/`:** runs the package in [fm1-emu](https://joakimeriksson.github.io/fm1-emu/), loading it from the
    Pages copy, because release assets fail CORS.
  - **`tools/make_pages.sh`:** builds the site.
- **Release:** [v0.5.1](https://github.com/fergroan-sketch/juniper-fm1/releases/tag/v0.5.1), marked as a
  pre-release, with the .fwsc attached.
- **Firmware hub:** `fmhub.json` and `card.png` (480 × 480, 7.5 KB, from the home screen).
- **Who ran what:** Fergus ran the `gh` commands, because this session isn't allowed to use `gh`.

## How it was checked

- **Factory table.** factory.h matched the SHA-256 checksum supplied with each bank:
  - 0.4: `f1a4928c…7bee`
  - 0.5: `248669c0…edb0`
  - 0.5.1 before the level change: `4745dec4…67b6`
  - 0.5.1 as released, with the levels and the new name in its header: `3df9a329…d39d`

  A `_Static_assert` keeps the table at exactly 128 patches, and another checks that the arpeggio fits in the
  spare bytes.
- **Host simulator.** `sh build.sh host` now runs all four scripts.
  - basic.txt and features.txt cover playing, pages, voice modes, chord memory, the arpeggiator, Juno-106 SysEx
    in, and slider CCs.
  - arp_patch.txt checks that turning to A15 starts the arpeggiator and turning back to A14 stops it.
  - sysex_slot.txt sends a Juno-106 patch into A16, an arp slot. It checks that A16 then doesn't arpeggiate and
    is POLY 1, and that the next arp patch, A17, still does.
- **Patch lengths.** I played every patch on the host (C4 held 0.5 s, then released) and timed how long it stays
  within 40 dB of its peak.
  - **0.4:** 26 patches lasted under 0.3 s.
  - **0.5:** every patch lasts at least 0.49 s, and half of them 0.76 s or more. The shortest are ARP SQUARE (a
    gated arpeggio), INIT and ORGAN, which stop with the key, as they should.
- **Online, 23 November, after the first Pages deployment:**
  - The front page, `/install/` and `/play/` return 200.
  - The live .fwsc is 616,231 bytes, with the same SHA-256 as the build. It's served with
    `Access-Control-Allow-Origin: *`.
  - The installer shows "JUNIPERFM1 0.5.1", its Install button is enabled, and Web MIDI is available. Install was
    not pressed.
  - In `/play/`, fm1-emu booted the firmware to its home page (A02 WARM PAD, POLY 1).
  - The front page's installer picture, first published as `img/install.png`, stayed "not found" on Pages for
    over ten minutes after the deployment, though the file was in `gh-pages` (GitHub's raw copy is 259 KB). It's
    now `img/installer.png`, and `gh-pages` was pushed again so that Pages redeploys.

## Not checked

- **On the FM-1 itself.** Nothing was installed and no CPU load or audio was checked from this machine; every
  check above is on the host or in the browser. The build checks memory but not the device's CPU budget at six
  voices with chorus. Fergus's listening on the FM-1 is the only on-device evidence so far, and it found 0.4's
  blips.
- **The splash screen.** The host doesn't draw fm1-base's default splash, so "JUNIPER FM-1" (12 characters, three
  more than "JUNO FM-1") wasn't seen. By the font's metrics it should fit the 240-pixel screen.
- **The built-in emulator** (`emu/`). It needs Emscripten, so it's left out; `play/` with fm1-emu stands in for it.
- **Bluetooth MIDI, TRS MIDI and USB audio.** These are covered only as far as fm1-base's host stand-ins go.

## Known issues and points for review

1. **0.5 replaced older saves.** On first start, 0.5 replaced any bank saved by 0.2 to 0.4. That was intended,
   because of the `JNB4` change. 0.5.1 keeps 0.5's saves. Its two level changes reach a block only if that block
   was never saved. README's "Installing" section says so.
2. **A playing arpeggio after SysEx.** A patch sent into the slot that is playing replaces the edit buffer. But an
   arpeggio that slot had already started keeps running until the next patch change or ARP.
3. **Choosing an arp patch over a running arpeggiator.** If the arpeggiator is already running (on or hold), the
   patch takes it over, and the next patch without an arpeggio turns it off. That stops a hold the player had set
   by hand.
4. **Stale demo renders.** `demo/` (not in git) holds WAVs of the first draft's 18 patches. `app/tests/demo.txt`
   can render the current bank.
5. **An extra bank in the repository.** `tools/bank-0.5-alt.txt` and `tools/open_up.py` are a second opened-up
   bank I made before the chosen one arrived. It isn't built; it's kept for comparison.
6. **History in the repository.** The first three commits still carry the JUNO FM-1 name, including the 0.5 and
   0.5.1 packages under their old file names. Only the latest commit is renamed.

## Files to read first

- `app/app.c`: `FACTORY` and `patch_init`, `banks_load`, `patch_arp` and `patch_select`, `save`, `arp_button`,
  `sysex_slot_plain` and `app_sysex`.
- `app/factory.h` and `tools/bank.txt`: the bank, one patch per line.
- `app/tests/arp_patch.txt` and `app/tests/sysex_slot.txt`: the newest host tests.
- `PRESETS.md`: the patch list.
- `app/fm1.toml`, `app/app.h`, `fmhub.json`: the name, the identity and the links.
- `app/site/play.html` and `tools/make_pages.sh`: the online player and the Pages site.
