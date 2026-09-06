# Third-party notices

This repository is intended for public distribution. Redistribution must
comply with the licenses of embedded components listed in [CREDITS.md](CREDITS.md).

Summary:

| Component | Typical license | Location |
|-----------|-----------------|----------|
| MAME-derived device code | BSD-3-Clause | `CEmu/chip/*`, board maps |
| hoot ssC30 (Namco CUS30) | MAME-aligned BSD-3-Clause | `CEmu/chip/cemu_chip_c30.*` |
| ymfm | BSD-3-Clause | `kb_sasami/source/ymfm/` |
| Musashi | see vendor tree | `CEmu/vendor/musashi/` |
| Game Music Emu Z80 | LGPL-2.1 | `CEmu/z80/` |
| mc6809 (Sean Conner) | LGPL-3.0-or-later | `CEmu/vendor/mc6809/` |
| zlib / minizip | zlib | link / minizip |

New chip or CPU ports must:

1. Keep the upstream copyright and license text in the file header.
2. Add a one-line entry to CREDITS.md with author and MAME (or other) path.
3. Record the source URL in `.cursor/_ac_sound_matrix.csv` `notes` when applicable.
