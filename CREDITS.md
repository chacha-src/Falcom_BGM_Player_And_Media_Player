# Third-party credits

This project incorporates or is derived from the following open-source components.
When redistributing, retain the licenses of the respective authors.

## MAME (Multiple Arcade Machine Emulator)

- Project: https://github.com/mamedev/mame
- License: BSD-3-Clause (device cores and many drivers)
- Use: Arcade board maps, sound-chip behavior references, and ports of
  device cores (e.g. K054539, ES5505/OTIS, OKI MSM6295 patterns).
- Namco C140 PCM (`CEmu/chip/cemu_chip_c140.cpp`): ported from MAME
  `src/devices/sound/c140.cpp` (copyright-holders: R. Belmont), BSD-3-Clause,
  via the FBNeo `src/burn/snd/c140.cpp` adaptation of the same core.
- Namco CUS30 / 15XX wavetable (`CEmu/chip/cemu_chip_c30.cpp`): ported from
  hoot `ssC30.cpp` / `ssC30.h`, algorithm aligned with MAME
  `src/devices/sound/namco.cpp` (copyright-holders: Nicola Salmoria,
  Aaron Giles), BSD-3-Clause.
- Individual file headers in `CEmu/chip/` and `CEmu/machine/` name the
  corresponding MAME source paths where applicable.

## ymfm (Yamaha FM synthesis)

- Author: Aaron Giles
- License: BSD-3-Clause
- Location: `kb_sasami/source/ymfm/`
- Use: YM2610 / OPN family cores via CEmu wrappers.

## Musashi (Motorola 68000 emulator)

- Author: Karl Stenerud (and contributors)
- License: see `CEmu/vendor/musashi/`
- Use: Mega System 1 / Taito F3 / Konami System GX sound 68000.

## Game Music Emu / Ay_Cpu (Z80)

- Project: Game_Music_Emu (Shay Green / blargg and contributors)
- License: LGPL-2.1 (see file headers in `CEmu/z80/`)
- Use: Z80 sound CPU emulation for many arcade boards.

## mc6809 (Motorola 6809 emulator)

- Author: Sean Conner
- License: LGPL-3.0-or-later
- Location: `CEmu/vendor/mc6809/`
- Use: FM-7 / FM77AV sound CPU.

## fmgen / related FM cores

- See headers under `CEmu/s98/` and linked kpi sources for YM2151 and related.

## Other

- minizip / zlib: ZIP ROM loading
- NP2 / related: PC-98 CPU paths where linked
- hoot-derived: catalog XML layouts; Namco CUS30 core via `ssC30` (see above)

For questions about a specific chip file, check the copyright block at the top of
that `.cpp` / `.h` first; this list is the index.
