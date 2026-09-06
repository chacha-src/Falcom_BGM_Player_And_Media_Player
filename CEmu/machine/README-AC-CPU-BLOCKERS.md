# Arcade boards blocked on a missing CPU core

Two `CEMU_AC_BOARD_*` families instantiate their sound chips and open a session,
but render silence because CEmu has no core for their sound CPU. `CDriverAc::Open`
marks them with `hasCpu_ = 0` so the Z80 is never run over foreign code.

Everything below is what an implementation actually needs; none of it is guesswork.

---

## 1. `CEMU_AC_BOARD_IREM_M92` — bmaster, gunforce

Sound CPU is a **NEC V35 (uPD70136)** running **encrypted opcodes**.

### Hardware (MAME `src/mame/irem/m92.cpp`)

```
V35(config, m_soundcpu, 14.318181_MHz_XTAL);

sound_map:
  00000-1FFFF  ROM
  A0000-A3FFF  RAM
  A8000-A803F  IremGA20      (umask16 0x00ff — byte lane 0 only)
  A8040-A8043  YM2151        (umask16 0x00ff)
  A8044        soundlatch read / acknowledge_w   (separate_acknowledge)
  A8046        soundlatch2 write (back to main CPU via upd71059c ir3)
  FFFF0-FFFFF  ROM mirror of soundcpu+0x1FFF0   (reset vector)

soundlatch  data_pending -> NEC_INPUT_LINE_INTP1
YM2151 irq  -> NEC_INPUT_LINE_INTP0
YM2151 clock = 14.318181 MHz / 4
GA20   clock = 14.318181 MHz / 4
```

Per-set opcode decryption table (`irem_cpu.cpp`):

| set        | table                        |
|------------|------------------------------|
| gunforce   | `gunforce_decryption_table`  |
| bmaster    | `bomberman_decryption_table` |
| lethalth   | `lethalth_decryption_table`  |
| uccops     | `dynablaster_decryption_table` |
| psoldier   | `psoldier_decryption_table`  |

### Implementation path

1. **CPU core.** `CEmu/vendor/np2` already ships a real-mode x86 core
   (`i286c.c` + `v30patch.c`) driven through `np2mem`/`np2io`. Fork it into
   `CEmu/vendor/v35/` with its own bus callbacks instead of np2's globals, the
   same way `cemu_m68k_bus.cpp` wraps Musashi. V30-level instruction coverage is
   enough for the sound driver — it is ordinary 8086 code.
2. **V35 internals.** The sound driver uses the on-chip peripherals, so they
   cannot be skipped:
   - Special-function register block, relocatable via the `IDB` register,
     reset default `0xFFE00`–`0xFFEFF`.
   - Register banks: `BRKCS`/`RETRBI`/`TSKSW` bank-switch instructions, and
     interrupt entry switches banks rather than pushing when `INTM` says so.
   - Internal interrupt controller: `INTP0`/`INTP1` are external pins, and their
     priority/mask registers live in the SFR block.
3. **Opcode decryption.** V25/V35 decrypt *opcode fetches only*, not data reads.
   Model it as a 256-entry byte substitution applied on the fetch path, keyed by
   the per-set table. The tables in `irem_cpu.cpp` are plain data — port them
   verbatim into a new `CEmu/machine/cemu_irem_cpu_tables.cpp`.
4. **Board glue.** Extend `CHardAc` with `M92Read8/Write8` mirroring the memory
   map above, wire `SetSoundCommand` to `INTP1`, and route the existing
   `cemu_chip_ga20` + OPM instances at `A8000`/`A8040`. The GA20 chip and its
   sample ROM already load correctly today (`prom=524288` for bmaster,
   `prom=131072` for gunforce), so only the CPU is missing.

**Estimated effort:** the V35 core plus its internal peripherals is the bulk of
it; the board glue is a few hundred lines and closely mirrors the Mega System 1
68000 path already in `cemu_hard_ac.cpp`.

---

**Status (2026-09):** V35 core is wired for M92. Blade Master (`bmaster`) reaches
command drain, song-gate, and GA20 oneshots on some BGM codes (e.g. `0x2A`), but
FM voice programming still fails and audio fades — not multi-title sustained PASS.
Remaining: finish V35 opcode coverage / decrypt gaps that kill the play loop,
gunforce table path, then mark matrix OK only after family-batch PASS.

---

## 2. `CEMU_AC_BOARD_DECO` — cninja / thndzone

**Status (2026-09 Phase 3):** HuC6280 core is detached under `CEmu/vendor/h6280/`
(`h6280core.c` + bus callbacks) and wired for `CEMU_AC_BOARD_DECO`. `cninja`
(thndzone) boots to the idle `BRA` at `$E0E6`, programs YM2151 timers via IRQ2,
polls the deco_146 latch (`LDA $0000` after `TAM #$A0`), and keeps MPR banks
matching the ROM table — but FM voice registers stay untouched (timer-only
`$12`/`$14` traffic) so probe remains SILENT. Next: command→sequencer path.
Note: `baddudes`/`actfancr`/`brkthru` are **not** HuC6280 in modern MAME
(R65C02 / M6502 / M6809); only cninja-class subtypes use this core.

### Hardware (MAME `src/mame/dataeast/cninja.cpp`)

```
H6280(config, m_audiocpu, XTAL(32'220'000) / 8);

sound_map:
  000000-00FFFF  ROM
  100000-100001  YM2203
  110000-110001  YM2151
  120000-120001  OKIM6295 #1
  130000-130001  OKIM6295 #2
  140000         deco_146 soundlatch read
  1F0000-1F1FFF  RAM
```

Note the map is expressed in the HuC6280's 21-bit physical space; the CPU sees it
through its eight 8 KB MMU banking registers (`MPR0`-`MPR7`).

### Implementation path (done in Phase 3)

1. **CPU core.** Detached MAME 0.122 portable C core → `h6280core.c` with
   `H6280SetBus` callbacks (no `emu.h`).
2. **Bus.** `cemu_h6280_bus.cpp` mirrors the V35/Musashi active-board pattern.
3. **Board glue.** `CHardAc` DECO Init/LoadRoms/DecoRead8/Write8 + `CDriverAc`
   DecoRender; chips YM2151 + YM2203(OPN) + dual OKI6295.

### Cheaper partial alternative (still open)

`cninja.cpp` also emulates two Z80-based variants —

---

## 3. `CEMU_AC_BOARD_IREM_M62` — kungfum / mpatrol / ldrun

Sound CPU is an **M6803** (not Z80) with dual AY-3-8910 + MSM5205.

### Hardware (MAME `src/mame/irem/irem.cpp` m62_audio)

`
M6803 @ 3.579545 MHz
AY8910 x2 @ XTAL/4
MSM5205 x2 (ADPCM; VCK → NMI)

m62_sound_map:
  0800       sound_irq_ack_w
  0801-0802  m62_adpcm_w
  4000-FFFF  ROM

Ports: P1/P2 bit-bang AY address/data; AY#0 port A = soundlatch.
cmd_w: latch + IRQ when bit7 clear.
`

Board enum + dual AY chips are routed (`hasCpu_=0`). Stays **SILENT** until a
vendored M6803 core (and optional MSM5205) exists — no AY/square audition.

---

## 4. `CEMU_AC_BOARD_NAMCO_C352` — System 12 / ND-1 (H8/3002)

**Status (2026-09): PASS** — 35/35 probed H8 Sys12+ND-1 titles sustained PLAY
(aquarush…abcheck incl. ncv1/ncv2). Build: `.cursor/_h8_inc.cmd` /
`_h8_inc_no_core.cmd` / `_h8_core_relink.cmd`.

### Sys12 song mailbox

- Host strobe: BE word at **shared+0x0100** with **bit14 set** → `0x4000 | song`.
- H8 ACK: word becomes **`0x8000 | song`**. Busy: **+0x4050** (H8 sets 1, host clears).
- Driver hunts try-table (`0x20/0x08/0x30/0x01/…`) when prefer is silent/flat/blast,
  and **resumes** hunting if peak dies after a short SE.

### ND-1

- Same C76 mailbox at shared+0x0100; H8 map `@200000` / C352 `@A00000`.
- H8/3002 stub: `FFFFE8` bit7 ready (ncv1 boot poll); ND-1 clock 16.384 MHz.

### C352 fixes that unblocked audio

- Global key-on execute @ word **0x202**; 8-bit linear/muLaw (MAME `c352.cpp`).
- Byte-merge bus writes (`h8C352Shadow_`).
- Workaround: key-on with vol/freq still 0 → default `vol=0x8080`, `freq=0x2000`
  (envelope copy ~aquarush `0x6880` not yet driven). Real BGM songs update vols
  → varying peaks (PLAY); flat 4096 = stuck default → keep hunting.

### Still open (related families — cannot reuse H8)

- Sys11/22/NA/NB need **M37702**, not H8 (see §5). Soft mailbox only → SILENT.
- Model 2A/3 **SCSP** / Hornet **RF5C400**: see §6–§7 (chip stubs + no 68K host).
- **SEIBU_OPL** (raiden/heatbrl): encrypted Z80 (SEI80BU) → SILENT until decrypt wired.

### Hardware (MAME `namcos12.cpp` `sub_program_map`)

```
H8/3002 @ ~16.9 MHz, C352 @ 24.576 MHz
  000000-07FFFF  ROM (ROM_LOAD16_WORD_SWAP)
  080000-08FFFF  shared RAM (mailbox with main @ 1f080000)
  280000-287FFF  C352
  300000-300003  inputs / wait stubs
```

ND-1 (`namcond1`): ROM + shared `@200000` + C352 `@A00000` (no word-swap).

---

## 5. Namco Sys11 / Sys22 / NA-1 / NB-1 — M37702 (not H8)

**Inventory:** system22≈18, system11≈13, na1≈11, nb1≈11 (all SILENT until host PLAY).
**Status (2026-09):** Partial. Detached M37702 core is in-tree and wired:

| Piece | Path | Status |
|-------|------|--------|
| CPU | `CEmu/vendor/m37710/m37702core.*` + `o0..o3` | Compiles; bus-callback fork of Belmont engine |
| Glue | `CEmu/machine/cemu_m37702_bus.*` | Done |
| Host | `CHardAc::LoadRomsM37702` / `M37702Read8`/`Write8` | Sys11 + NA1 maps |
| Chip | C352 (Sys11/22) / C219 via `CEmuChipC140SetType(2)` (NA/NB) | Chip OK |

### Remaining PLAY blockers

1. **Sys11/22 need `c76.bin` / `c74.bin` (16KB internal mask ROM @0xC000).**
   Hoot zips (e.g. `danceyes`) ship `*sprog*` + wave only — `LoadRomsM37702` returns 0 → FAIL open.
   Files: missing `c76.bin` / `c74.bin` (MAME `namcomcu`).
2. **NA-1/NB-1 NSA-BIOS (`c69.bin`/`c70.bin`) is a bootstrap.**
   IRQ/timer handlers are `JMP ($01xx)` into RAM vectors installed by the **main 68000**
   sound upload. Hoot archives (e.g. `bkrtmaq`) have BIOS+PCM only — no main program.
   MCU runs and can poke C219 (writes observed) but song key-on never lands → peak 0.
   Files needed for full PLAY: main CPU program ROMs (or a dumped post-upload RAM image /
   HLE of the Namco sound driver).

Soft MixAdd / voice planting remains disabled. Sys12/ND-1 H8 path unchanged (e.g. aquarush PLAY).

### Why H8 cannot be reused

| Family | Sound MCU | Chip | Catalog clue |
|--------|-----------|------|--------------|
| Sys12 / ND-1 | **H8/3002** | C352 | `*.11s` / H8 dump → PLAY |
| Sys11 | **C76 = M37702** | C352 | `*sprog*` + **c76.bin** |
| Sys22 | **C74 = M37702** | C352 | `pr1data` + **c74.bin** |
| NA-1 / NB-1 | **C69/C70 = M37702** | C219≈C140 | `c69.bin` @0xC000 + main upload |

---

## 6. Sega Model 2A / Model 3 — SCSP + 68000 (not SH-2 audio)

**Inventory:** model2a≈24, model3≈9 (all SILENT). Early **model2** (daytona/vf/…)
uses MultiPCM+YM3438 via `segaM1Audio_` and already **PLAY** — that path does
**not** apply to model2a.

**Status (2026-09):** BLOCKED on **two** missing pieces:

1. **`cemu_chip_scsp.cpp` is a silence stub** (regs/ROM held; `MixAdd`/`Render`
   always zero). No soft-wave audition.
2. **No Model2A 68000 sound host** (`sega68_` only when `SegaM1Audio()`).
   Main CPUs are SH-2 / i960; **audio host is MC68EC000**, not SH-2.

### Hardware (MAME `model2.cpp` `model2_scsp` / `model2_snd`)

```
M68000 @ 45.1584 MHz / 4   (SCSP clock / 2)
SCSP    @ 45.1584 MHz / 2
MIDI via I8251 @ 31.25 kbaud (song commands from main)

model2_snd (68K view):
  000000-07FFFF  sound RAM (shared with SCSP; vectors copied from audiocpu)
  100000-100FFF  SCSP registers
  400000-400001  sample bank control
  600000-67FFFF  audiocpu ROM (catalog type=code, often @ offset 0x080000)
  800000-9FFFFF  samples[0..]
  A00000-DFFFFF  bank4
  E00000-FFFFFF  bank5

scsp_map: 000000-07FFFF = same sound RAM
```

Catalog (xml2) already lists `code` + interleaved `pcm` banks for every model2a
set (e.g. vf2 `epr-17574.30` + 4×2MB MPR). Musashi is available (MegaSys1 / GX /
Model1 MultiPCM). **Even with a 68K map wired, PLAY is impossible until SCSP
actually synthesizes PCM/FM** (MAME `scsp.cpp` / Mednafen-class port — large).

SH-2 is the **main** board CPU and is not an alternate audio host for model2a.

---

## 7. Konami Hornet / GTI Club — RF5C400 + 68000

**Inventory:** hornet≈8, gticlub≈4 (+ zr107/lethal/… same chip class).

**Status (2026-09):** BLOCKED on **two** missing pieces:

1. **`cemu_chip_rf5c400.cpp` is a silence stub** (same policy as SCSP).
2. **No 68000 sound host** for `CEMU_AC_BOARD_KONAMI_RF5C400`.

Catalog ships `code` (e.g. gradius4 `837a08.7s`) + dual `pcm` banks. Wave ROM
loads via `LoadRomsPcmChip`; song inject is latch-only (`SoftPcmInjectSong`).
Need a real RF5C400 core (MAME `rf5c400.cpp`) plus the Hornet/NWK-TR 68K sound
map and command latch before any title can PLAY.
