#pragma once
/* Resolve program names from embedded SASAMI_GS / SASAMI_XG / SASAMI_EX.DAT (same as MIDI monitor). */

/* Full path used by MIDI monitor (map / XG flag known). */
void SasamiToneLookup(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum,
	wchar_t* out, int outN);

/* Exact DAT hit only — no GM / Capital fallback. Missing → out empty (caller shows ------). */
void SasamiToneLookupStrict(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum,
	wchar_t* out, int outN);

/* 1 if any PC exists for this bank in the map (for bank combo ------ slots). */
int SasamiToneBankUsed(int isXg, int mapId, int bankMsb, int bankLsb, int isDrum);

/* Score / composer: bank from @BANK or paired @n:m; prefers GS then XG then GM English. */
void SasamiToneLookupAuto(int bankMsb, int bankLsb, int pc, int isDrum, wchar_t* out, int outN);
