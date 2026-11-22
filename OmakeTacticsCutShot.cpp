#include "stdafx.h"
#include "OmakeTacticsGame.h"
#include <math.h>

namespace otac {

static const float PI = 3.14159265f;

enum {
	VB_SLASH, VB_HEAVY, VB_SPIN, VB_THRUST, VB_SHOT,
	VB_METEOR, VB_APOC, VB_STORM, VB_GRAV, VB_BOLT,
	VB_ICE, VB_BEAM, VB_BREATH, VB_PILLAR, VB_NOVA,
	VB_QUAKE, VB_GALE, VB_DARK, VB_HEAL, VB_RAISE,
	VB_AURA, VB_WARP, VB_FAN, VB_BACK, VB_DRUM,
	VB_MARK, VB_CHAIN, VB_RAIN
};

struct Shot {
	int verb;
	float sky, rush, lag, side, span;
	int hits, crack, scatter;
};

struct Fall {
	V3 start, end;
	float y0, vy, grav, spawn, hit;
};

static Shot Mk(int verb, float sky, float rush, float lag, int hits, int crack, int scatter, float side, float span)
{
	Shot s;
	s.verb = verb; s.sky = sky; s.rush = rush; s.lag = lag;
	s.hits = hits; s.crack = crack; s.scatter = scatter;
	s.side = side; s.span = span;
	return s;
}

static void Finish(Shot& s)
{
	if (s.hits < 1) s.hits = 1;
	if (s.hits > 5) s.hits = 5;
	if (s.rush < 0.18f) s.rush = 0.18f;
	if (s.span < s.lag + 0.28f) s.span = s.lag + 0.28f;
	if (s.span > 3.40f) s.span = 3.40f;
}

static BOOL Hand(int skill, Shot& s)
{
	switch (skill) {
	case SKL_POWER: s = Mk(VB_HEAVY, 0.1f, 0.44f, 0.60f, 1, 1, 1, 1.f, 1.02f); break;
	case SKL_DOUBLE: s = Mk(VB_SLASH, 0.05f, 0.36f, 0.48f, 2, 0, 0, -0.8f, 0.92f); break;
	case SKL_WHIRL: s = Mk(VB_SPIN, 0.3f, 0.55f, 0.50f, 1, 0, 1, 1.f, 1.05f); break;
	case SKL_PIERCE: s = Mk(VB_THRUST, 0.0f, 0.34f, 0.52f, 2, 0, 0, 0.6f, 0.95f); break;
	case SKL_STITCH: s = Mk(VB_BACK, 0.1f, 0.40f, 0.58f, 2, 0, 0, -1.f, 1.00f); break;
	case SKL_VITAL: s = Mk(VB_HEAVY, 0.45f, 0.50f, 0.66f, 3, 0, 0, 0.4f, 1.12f); break;
	case SKL_BASH: s = Mk(VB_QUAKE, 0.05f, 0.42f, 0.58f, 2, 1, 1, -0.5f, 1.08f); break;
	case SKL_DRAGONFANG: s = Mk(VB_BREATH, 0.35f, 0.40f, 0.62f, 2, 0, 1, 1.f, 1.15f); break;
	case SKL_HOLYBLADE: s = Mk(VB_PILLAR, 1.2f, 0.48f, 0.70f, 2, 0, 0, 0.8f, 1.20f); break;
	case SKL_ARROWRAIN: s = Mk(VB_RAIN, 0.4f, 0.46f, 0.72f, 3, 0, 1, -0.7f, 1.25f); break;
	case SKL_ASSASSINATE: s = Mk(VB_BACK, 0.0f, 0.32f, 0.55f, 2, 0, 0, 1.f, 0.98f); break;
	case SKL_BRAVE: s = Mk(VB_HEAVY, 0.9f, 0.52f, 0.74f, 3, 1, 1, -1.f, 1.28f); break;
	case SKL_FIRE: s = Mk(VB_BEAM, 0.2f, 0.36f, 0.58f, 2, 0, 0, 0.5f, 1.00f); break;
	case SKL_BLIZZARD: s = Mk(VB_ICE, 0.8f, 0.50f, 0.78f, 3, 1, 1, -0.6f, 1.40f); break;
	case SKL_THUNDER: s = Mk(VB_BOLT, 1.4f, 0.42f, 0.70f, 4, 0, 0, 0.9f, 1.25f); break;
	case SKL_METEOR: s = Mk(VB_METEOR, 1.15f, 0.58f, 1.05f, 3, 1, 1, 1.f, 1.78f); break;
	case SKL_RUNEBLADE: s = Mk(VB_BEAM, 0.55f, 0.38f, 0.60f, 2, 0, 0, -0.4f, 1.05f); break;
	case SKL_HOLY: s = Mk(VB_PILLAR, 1.8f, 0.52f, 0.82f, 3, 0, 0, -0.9f, 1.35f); break;
	case SKL_DARKNESS: s = Mk(VB_DARK, 0.6f, 0.48f, 0.75f, 2, 0, 0, 0.7f, 1.30f); break;
	case SKL_HEAL: s = Mk(VB_HEAL, 0.2f, 0.55f, 0.80f, 1, 0, 0, -0.5f, 1.15f); break;
	case SKL_HIGHHEAL: s = Mk(VB_HEAL, 0.7f, 0.60f, 0.88f, 1, 0, 0, 0.6f, 1.25f); break;
	case SKL_HEALALL: s = Mk(VB_HEAL, 1.3f, 0.70f, 0.95f, 1, 0, 0, 1.f, 1.40f); break;
	case SKL_PROTECT: s = Mk(VB_AURA, 0.15f, 0.65f, 0.90f, 1, 0, 0, 0.8f, 1.20f); break;
	case SKL_HASTE: s = Mk(VB_GALE, 0.25f, 0.36f, 0.55f, 1, 0, 0, -1.f, 0.95f); break;
	case SKL_BLESS: s = Mk(VB_AURA, 0.55f, 0.58f, 0.85f, 1, 0, 0, -0.7f, 1.18f); break;
	case SKL_E_VENOM: s = Mk(VB_MARK, 0.1f, 0.40f, 0.62f, 1, 0, 0, 0.4f, 1.00f); break;
	case SKL_E_BREATH: s = Mk(VB_BREATH, 0.5f, 0.42f, 0.68f, 2, 0, 1, -0.8f, 1.15f); break;
	case SKL_E_CRUSH: s = Mk(VB_QUAKE, 0.0f, 0.38f, 0.55f, 1, 1, 1, 0.9f, 1.05f); break;
	case SKL_E_DRAIN: s = Mk(VB_DARK, 0.15f, 0.40f, 0.66f, 1, 0, 0, -0.3f, 1.05f); break;
	case SKL_E_SPIT: s = Mk(VB_SHOT, 0.2f, 0.34f, 0.55f, 1, 0, 0, 0.55f, 0.95f); break;
	case SKL_E_QUAKE: s = Mk(VB_QUAKE, 0.35f, 0.48f, 0.70f, 1, 1, 1, -1.f, 1.20f); break;
	case SKL_REFRESH: s = Mk(VB_HEAL, 0.35f, 0.50f, 0.72f, 1, 0, 0, 0.25f, 1.10f); break;
	case SKL_CLEAVE: s = Mk(VB_FAN, 0.2f, 0.40f, 0.56f, 1, 0, 1, 0.85f, 1.00f); break;
	case SKL_SNIPE: s = Mk(VB_SHOT, 0.15f, 0.30f, 0.62f, 1, 0, 0, -0.9f, 1.05f); break;
	case SKL_VENOMEDGE: s = Mk(VB_SLASH, 0.0f, 0.38f, 0.54f, 2, 0, 0, 0.35f, 0.95f); break;
	case SKL_SANCTUARY: s = Mk(VB_HEAL, 1.6f, 0.72f, 1.00f, 1, 0, 0, -0.4f, 1.45f); break;
	case SKL_SMITE: s = Mk(VB_PILLAR, 1.5f, 0.46f, 0.74f, 2, 0, 0, 0.2f, 1.22f); break;
	case SKL_GRAVITY: s = Mk(VB_GRAV, 2.2f, 0.40f, 1.15f, 3, 1, 1, -0.8f, 1.85f); break;
	case SKL_WARCRY: s = Mk(VB_DRUM, 0.1f, 0.42f, 0.55f, 3, 1, 1, 1.f, 1.15f); break;
	case SKL_RENEW: s = Mk(VB_RAISE, 0.8f, 0.55f, 0.85f, 1, 0, 0, 0.5f, 1.25f); break;
	case SKL_MIRROR: s = Mk(VB_AURA, 0.4f, 0.62f, 0.90f, 1, 0, 0, -0.2f, 1.20f); break;
	case SKL_GODSLASH: s = Mk(VB_HEAVY, 1.15f, 0.50f, 0.78f, 2, 1, 1, 0.75f, 1.35f); break;
	case SKL_RUIN: s = Mk(VB_DARK, 1.0f, 0.55f, 0.90f, 3, 1, 0, -0.85f, 1.50f); break;
	case SKL_APOC: s = Mk(VB_APOC, 1.7f, 0.92f, 1.20f, 4, 1, 1, -1.f, 2.05f); break;
	case SKL_TRANSMUTE: s = Mk(VB_AURA, 0.2f, 0.48f, 0.70f, 1, 0, 0, 0.15f, 1.05f); break;
	case SKL_RAISE: s = Mk(VB_RAISE, 1.4f, 0.60f, 0.95f, 1, 0, 0, 0.9f, 1.40f); break;
	case SKL_SHOWER: s = Mk(VB_RAIN, 0.55f, 0.44f, 0.68f, 3, 0, 1, 0.7f, 1.20f); break;
	case SKL_SHINE: s = Mk(VB_NOVA, 0.9f, 0.50f, 0.72f, 3, 0, 0, -0.6f, 1.22f); break;
	case SKL_CRESCENT: s = Mk(VB_FAN, 0.45f, 0.42f, 0.60f, 2, 0, 0, -0.9f, 1.05f); break;
	case SKL_GRANDSLASH: s = Mk(VB_HEAVY, 1.7f, 0.56f, 0.82f, 1, 1, 1, 0.95f, 1.35f); break;
	case SKL_STORM: s = Mk(VB_STORM, 0.35f, 0.70f, 0.36f, 4, 1, 1, 0.8f, 1.45f); break;
	case SKL_FROSTFIELD: s = Mk(VB_ICE, 0.3f, 0.58f, 0.66f, 4, 1, 1, -0.45f, 1.40f); break;
	case SKL_THUNDERFIELD: s = Mk(VB_BOLT, 0.6f, 0.48f, 0.62f, 4, 1, 0, 0.35f, 1.35f); break;
	case SKL_NOVA: s = Mk(VB_NOVA, 1.3f, 0.62f, 0.88f, 4, 0, 1, -0.25f, 1.45f); break;
	case SKL_LINESLASH: s = Mk(VB_THRUST, 0.1f, 0.36f, 0.52f, 1, 0, 1, 1.f, 0.95f); break;
	case SKL_FANSLASH: s = Mk(VB_FAN, 0.15f, 0.40f, 0.54f, 1, 0, 1, -0.55f, 0.98f); break;
	case SKL_ARROWLINE: s = Mk(VB_SHOT, 0.05f, 0.32f, 0.58f, 3, 0, 0, 0.8f, 1.05f); break;
	case SKL_ARROWFAN: s = Mk(VB_FAN, 0.35f, 0.40f, 0.64f, 3, 0, 0, -0.3f, 1.10f); break;
	case SKL_FIREBEAM: s = Mk(VB_BEAM, 0.15f, 0.34f, 0.56f, 1, 0, 0, 0.9f, 0.98f); break;
	case SKL_ICEBEAM: s = Mk(VB_BEAM, 0.25f, 0.40f, 0.66f, 1, 0, 0, -0.85f, 1.08f); break;
	case SKL_THUNDERBEAM: s = Mk(VB_CHAIN, 0.4f, 0.36f, 0.58f, 3, 0, 0, 0.6f, 1.05f); break;
	case SKL_HEALBEAM: s = Mk(VB_HEAL, 0.2f, 0.48f, 0.70f, 1, 0, 0, -0.7f, 1.12f); break;
	case SKL_HEALFAN: s = Mk(VB_HEAL, 0.45f, 0.52f, 0.78f, 1, 0, 0, 0.4f, 1.18f); break;
	case SKL_RAMPAGE: s = Mk(VB_HEAVY, 0.2f, 0.40f, 0.52f, 4, 1, 1, -0.8f, 1.25f); break;
	case SKL_EARTHREND: s = Mk(VB_QUAKE, 0.5f, 0.46f, 0.68f, 1, 1, 1, 0.7f, 1.22f); break;
	case SKL_BLOODAXE: s = Mk(VB_BACK, 0.25f, 0.42f, 0.60f, 2, 1, 1, -0.65f, 1.08f); break;
	case SKL_IRONHOWL: s = Mk(VB_DRUM, 0.05f, 0.38f, 0.50f, 3, 1, 1, 0.55f, 1.12f); break;
	case SKL_COLOSSMASH: s = Mk(VB_HEAVY, 2.3f, 0.62f, 0.88f, 1, 1, 1, -0.95f, 1.45f); break;
	case SKL_ARENAKING: s = Mk(VB_SPIN, 0.55f, 0.58f, 0.70f, 2, 1, 1, 0.4f, 1.20f); break;
	case SKL_WARDRUM: s = Mk(VB_DRUM, 0.2f, 0.44f, 0.58f, 3, 1, 1, -0.4f, 1.18f); break;
	case SKL_AXEWHIRL: s = Mk(VB_SPIN, 0.15f, 0.48f, 0.60f, 2, 0, 1, -0.75f, 1.10f); break;
	case SKL_FISTGOD: s = Mk(VB_HEAVY, 0.35f, 0.42f, 0.58f, 3, 1, 0, 0.5f, 1.15f); break;
	case SKL_WARLORD: s = Mk(VB_DRUM, 0.4f, 0.50f, 0.66f, 2, 1, 1, -0.55f, 1.20f); break;
	case SKL_BREAKKING: s = Mk(VB_QUAKE, 0.25f, 0.44f, 0.62f, 2, 1, 1, 0.85f, 1.15f); break;
	case SKL_STEELGOD: s = Mk(VB_PILLAR, 0.8f, 0.48f, 0.70f, 2, 1, 0, -0.5f, 1.20f); break;
	case SKL_HEGEFIST: s = Mk(VB_THRUST, 0.05f, 0.30f, 0.48f, 2, 0, 0, 0.95f, 0.95f); break;
	case SKL_PEERLESS: s = Mk(VB_SPIN, 0.7f, 0.52f, 0.68f, 3, 0, 1, 0.25f, 1.18f); break;
	case SKL_ENDFIST: s = Mk(VB_HEAVY, 1.85f, 0.58f, 0.86f, 3, 1, 1, -0.35f, 1.45f); break;
	case SKL_SHADOWSTEP: s = Mk(VB_WARP, 0.1f, 0.36f, 0.52f, 1, 0, 0, 1.f, 0.95f); break;
	case SKL_SMOKESCREEN: s = Mk(VB_DARK, 0.3f, 0.40f, 0.60f, 1, 0, 1, -0.8f, 1.05f); break;
	case SKL_VITALSTAB: s = Mk(VB_THRUST, 0.0f, 0.28f, 0.46f, 1, 0, 0, -0.45f, 0.90f); break;
	case SKL_BACKCUT: s = Mk(VB_BACK, 0.05f, 0.30f, 0.48f, 1, 0, 0, 0.7f, 0.92f); break;
	case SKL_NIGHTRUN: s = Mk(VB_GALE, 0.15f, 0.28f, 0.46f, 1, 0, 0, -0.6f, 0.88f); break;
	case SKL_SILENTKILL: s = Mk(VB_BACK, 0.0f, 0.26f, 0.50f, 1, 0, 0, -1.f, 0.92f); break;
	case SKL_DAGGERRAIN: s = Mk(VB_RAIN, 0.25f, 0.40f, 0.58f, 4, 0, 1, 0.45f, 1.15f); break;
	case SKL_DEATHMARK: s = Mk(VB_MARK, 0.2f, 0.46f, 0.72f, 1, 0, 0, -0.35f, 1.15f); break;
	case SKL_GHOSTHAND: s = Mk(VB_DARK, 0.45f, 0.42f, 0.64f, 1, 0, 0, 0.55f, 1.08f); break;
	case SKL_SHADEEMP: s = Mk(VB_DARK, 1.1f, 0.58f, 0.86f, 2, 0, 1, -0.4f, 1.35f); break;
	case SKL_TRACELESS: s = Mk(VB_WARP, 0.05f, 0.32f, 0.48f, 1, 0, 0, -0.7f, 0.90f); break;
	case SKL_NIGHTEMP: s = Mk(VB_DARK, 0.7f, 0.50f, 0.78f, 2, 0, 0, 0.85f, 1.25f); break;
	case SKL_VOIDTHIEF: s = Mk(VB_BACK, 0.35f, 0.38f, 0.60f, 1, 0, 0, -0.25f, 1.02f); break;
	case SKL_LASTNIGHT: s = Mk(VB_DARK, 0.9f, 0.52f, 0.80f, 3, 1, 0, 0.3f, 1.35f); break;
	case SKL_ETERNIGHT: s = Mk(VB_DARK, 1.5f, 0.66f, 1.00f, 4, 1, 1, -0.9f, 1.65f); break;
	case SKL_ESUNA: s = Mk(VB_HEAL, 0.15f, 0.48f, 0.70f, 1, 0, 0, 0.8f, 1.10f); break;
	case SKL_PURIFY: s = Mk(VB_HEAL, 0.55f, 0.52f, 0.76f, 1, 0, 0, -0.55f, 1.15f); break;
	case SKL_CLEANCUT: s = Mk(VB_SLASH, 0.1f, 0.34f, 0.50f, 1, 0, 0, 0.65f, 0.92f); break;
	case SKL_GALE: s = Mk(VB_GALE, 0.4f, 0.36f, 0.58f, 2, 0, 1, 0.9f, 1.05f); break;
	case SKL_MIASMA: s = Mk(VB_MARK, 0.35f, 0.50f, 0.78f, 2, 0, 0, -0.8f, 1.20f); break;
	case SKL_ELFIRE: s = Mk(VB_BREATH, 0.15f, 0.36f, 0.52f, 2, 0, 1, 0.7f, 0.98f); break;
	case SKL_ELICE: s = Mk(VB_SLASH, 0.2f, 0.40f, 0.56f, 2, 0, 0, -0.6f, 1.00f); break;
	case SKL_ELTHUNDER: s = Mk(VB_CHAIN, 0.3f, 0.34f, 0.50f, 2, 0, 0, 0.45f, 0.95f); break;
	case SKL_ELHOLY: s = Mk(VB_PILLAR, 0.6f, 0.42f, 0.62f, 2, 0, 0, -0.5f, 1.05f); break;
	case SKL_ELDARK: s = Mk(VB_BACK, 0.2f, 0.38f, 0.56f, 2, 0, 0, 0.3f, 1.00f); break;
	case SKL_ELWIND: s = Mk(VB_GALE, 0.1f, 0.32f, 0.48f, 2, 0, 0, -0.85f, 0.92f); break;
	case SKL_ELEATH: s = Mk(VB_QUAKE, 0.05f, 0.40f, 0.56f, 2, 1, 1, 0.6f, 1.02f); break;
	case SKL_ELPOISON: s = Mk(VB_MARK, 0.05f, 0.38f, 0.58f, 2, 0, 0, -0.4f, 1.00f); break;
	default: return FALSE;
	}
	Finish(s);
	return TRUE;
}

static int Twist(int verb, int skill)
{
	int k = skill < 0 ? 0 : skill;
	int m = (k / 3) % 3;
	switch (verb) {
	case VB_METEOR: { const int f[3] = { VB_METEOR, VB_APOC, VB_STORM }; return f[k % 3]; }
	case VB_BOLT: { const int f[3] = { VB_BOLT, VB_CHAIN, VB_BEAM }; return f[m]; }
	case VB_ICE: return (k & 1) ? VB_ICE : VB_NOVA;
	case VB_SLASH: { const int f[4] = { VB_SLASH, VB_HEAVY, VB_THRUST, VB_FAN }; return f[k % 4]; }
	case VB_HEAVY: { const int f[3] = { VB_HEAVY, VB_QUAKE, VB_DRUM }; return f[k % 3]; }
	case VB_SHOT: return (k & 1) ? VB_SHOT : VB_RAIN;
	default: break;
	}
	return verb;
}

static void Salt(Shot& s, int skill)
{
	int k = skill < 0 ? 1 : skill;
	s.side = (((k * 5 + 1) % 9) - 4) / 4.f;
	if (s.side > -0.18f && s.side < 0.18f) s.side = (k & 1) ? 0.72f : -0.72f;
	s.sky += ((k * 7) % 5) * 0.14f;
	s.rush = Clampf(s.rush + ((k * 3) % 5) * 0.02f, 0.22f, 1.15f);
	s.lag = Clampf(s.lag + ((k * 11) % 5) * 0.035f, 0.30f, 1.7f);
}

static Shot Compose(int skill)
{
	const SkillDef& sd = SkillInfo(skill);
	Shot s = Mk(VB_SLASH, 0.15f, 0.40f, 0.56f, 1, 0, 0, 1.f, 0.95f);
	if (sd.kind == SK_PASSIVE) { s.verb = -1; return s; }
	if (sd.effect == EF_WARP) s = Mk(VB_WARP, 0.1f, 0.40f, 0.58f, 1, 0, 0, 1.f, 1.00f);
	else if (sd.effect == EF_RAISE) s = Mk(VB_RAISE, 1.6f, 0.72f, 1.15f, 1, 0, 0, 0.5f, 2.85f);
	else if (sd.kind == SK_HEAL) s = Mk(VB_HEAL, 0.45f + sd.area * 0.35f, 0.70f, 1.05f, 1, 0, 0, 0.6f, 2.15f + sd.area * 0.18f);
	else if (sd.kind == SK_BUFF) {
		if (sd.effect == EF_BUFF_AGI) s = Mk(VB_GALE, 0.35f, 0.48f, 0.72f, 1, 0, 0, 1.f, 1.85f);
		else if (sd.effect == EF_BUFF_DEF) s = Mk(VB_AURA, 0.28f, 0.72f, 1.05f, 1, 0, 0, -0.7f, 2.05f);
		else s = Mk(VB_AURA, 0.55f, 0.68f, 1.00f, 1, 0, 0, 0.4f, 1.95f);
	} else if (sd.kind == SK_MAGIC) {
		switch (sd.element) {
		case EL_FIRE:
			if (sd.area >= 3 && sd.hits >= 3 && sd.power >= 16) s = Mk(VB_METEOR, 1.6f, 0.70f, 1.25f, sd.hits, 1, 1, 1.f, 2.85f);
			else if (sd.area >= 2 && sd.hits >= 3) s = Mk(VB_STORM, 0.7f, 0.74f, 0.55f, sd.hits, 1, 1, -0.6f, 2.15f);
			else if (sd.area >= 2) s = Mk(VB_NOVA, 1.05f, 0.62f, 0.95f, 2, 0, 1, 0.5f, 2.05f);
			else s = Mk(VB_BEAM, 0.2f, 0.36f, 0.58f, sd.hits > 0 ? sd.hits : 1, 0, 0, 0.7f, 1.00f);
			break;
		case EL_ICE:
			if (sd.area >= 2) s = Mk(VB_ICE, 0.85f, 0.66f, 0.98f, sd.hits, 1, 1, -0.5f, 2.25f);
			else s = Mk(VB_BEAM, 0.15f, 0.40f, 0.64f, 1, 0, 0, 0.4f, 1.05f);
			break;
		case EL_THUNDER:
			s = Mk(VB_BOLT, 1.05f + sd.area * 0.22f, 0.52f, 0.88f, sd.hits > 0 ? sd.hits : 1, sd.area >= 2 ? 1 : 0, 0, 0.8f, sd.area >= 2 ? 2.35f : 1.55f);
			break;
		case EL_EARTH:
			s = (sd.hits >= 3) ? Mk(VB_GRAV, 2.1f, 0.55f, 1.35f, sd.hits, 1, 1, -0.7f, 2.95f)
				: Mk(VB_QUAKE, 0.35f, 0.55f, 0.82f, 1, 1, 1, 0.6f, 1.65f);
			break;
		case EL_WIND:
			s = (sd.shape == SH_LINE) ? Mk(VB_GALE, 0.2f, 0.34f, 0.52f, sd.hits, 0, 1, 0.9f, 1.00f)
				: Mk(VB_GALE, 0.35f, 0.40f, 0.60f, sd.hits, 0, 1, -0.8f, 1.08f);
			break;
		case EL_DARK:
			s = (sd.area >= 1) ? Mk(VB_DARK, 1.05f, 0.62f, 1.00f, sd.hits, 1, 0, -0.6f, 2.15f)
				: Mk(VB_MARK, 0.2f, 0.44f, 0.68f, 1, 0, 0, 0.5f, 1.10f);
			break;
		case EL_HOLY:
			s = (sd.area >= 2) ? Mk(VB_NOVA, 1.45f, 0.68f, 1.05f, sd.hits, 0, 0, 0.4f, 2.35f)
				: Mk(VB_PILLAR, 1.7f, 0.62f, 1.00f, sd.hits, 0, 0, -0.5f, 2.15f);
			break;
		case EL_POISON:
			s = Mk(VB_MARK, 0.25f, 0.46f, 0.70f, sd.hits, 0, 0, -0.4f, 1.15f);
			break;
		default:
			s = (sd.area >= 2) ? Mk(VB_NOVA, 0.5f, 0.48f, 0.70f, 2, 0, 0, 0.3f, 1.15f)
				: Mk(VB_BEAM, 0.2f, 0.38f, 0.58f, 1, 0, 0, -0.6f, 1.00f);
			break;
		}
	} else if (sd.rMin >= 2 && sd.hits >= 3) s = Mk(VB_RAIN, 0.35f, 0.42f, 0.66f, sd.hits, 0, 1, 0.5f, 1.15f);
	else if (sd.rMin >= 2) s = Mk(VB_SHOT, 0.1f, 0.32f, 0.58f, 1, 0, 0, -0.7f, 1.00f);
	else if (sd.shape == SH_FAN) s = Mk(VB_FAN, 0.2f, 0.40f, 0.56f, 1, 0, 1, 0.8f, 1.00f);
	else if (sd.shape == SH_LINE) s = Mk(VB_THRUST, 0.05f, 0.34f, 0.50f, sd.hits > 0 ? sd.hits : 1, 0, 0, -0.5f, 0.95f);
	else if (sd.area >= 1 && sd.element == EL_EARTH) s = Mk(VB_QUAKE, 0.2f, 0.44f, 0.62f, 1, 1, 1, 0.7f, 1.12f);
	else if (sd.area >= 1 && sd.element == EL_WIND) s = Mk(VB_SPIN, 0.3f, 0.48f, 0.62f, 1, 0, 1, -0.6f, 1.08f);
	else if (sd.area >= 1) s = Mk(VB_FAN, 0.15f, 0.42f, 0.58f, 1, sd.element == EL_NONE ? 0 : 1, 1, 0.55f, 1.02f);
	else if (sd.effect == EF_POISON || sd.effect == EF_SLEEP || sd.effect == EF_BLIND) s = Mk(VB_BACK, 0.1f, 0.36f, 0.55f, 1, 0, 0, -0.8f, 0.98f);
	else if (sd.power >= 130 || sd.hits >= 3) s = Mk(VB_HEAVY, 0.4f + sd.power * 0.002f, 0.48f, 0.66f, sd.hits, 1, 1, 0.6f, 1.18f);
	else if (sd.hits >= 2) s = Mk(VB_SLASH, 0.1f, 0.38f, 0.52f, sd.hits, 0, 0, 0.5f, 0.98f);
	if (s.verb >= 0) {
		s.verb = Twist(s.verb, skill);
		if (sd.hits > s.hits) s.hits = sd.hits;
		Salt(s, skill);
		Finish(s);
	}
	return s;
}

static Shot ShotOf(int skill)
{
	Shot s;
	if (skill >= 0 && Hand(skill, s)) return s;
	if (skill < 0) { s.verb = -1; return s; }
	return Compose(skill);
}

static DWORD ElemCol(int el)
{
	switch (el) {
	case EL_FIRE: return RGBA8(255, 120, 40);
	case EL_ICE: return RGBA8(120, 200, 255);
	case EL_THUNDER: return RGBA8(255, 236, 90);
	case EL_HOLY: return RGBA8(255, 240, 180);
	case EL_DARK: return RGBA8(170, 80, 230);
	case EL_WIND: return RGBA8(120, 240, 160);
	case EL_EARTH: return RGBA8(200, 150, 90);
	case EL_POISON: return RGBA8(170, 230, 80);
	}
	return RGBA8(255, 244, 220);
}

static Fall MakeFall(V3 a, V3 d, float y0, float spawn, float hit, float side, int i, float spread)
{
	Fall f;
	float T = hit - spawn;
	if (T < 0.28f) T = 0.28f;
	f.spawn = spawn;
	f.hit = spawn + T;
	f.y0 = y0;
	f.vy = -0.75f;
	f.grav = 2.f * (y0 + f.vy * T - 0.32f) / (T * T);
	if (f.grav < 1.f) f.grav = 1.f;
	float ang = side * 0.7f + i * (PI * 2.f / 3.f);
	f.start = Lerp(a, d, 0.30f) + v3(cosf(ang) * spread, 0, sinf(ang) * spread);
	f.end = d + v3(cosf(ang) * spread * 0.28f, 0, sinf(ang) * spread * 0.28f);
	return f;
}

static V3 FallAt(const Fall& f, float t)
{
	float T = f.hit - f.spawn;
	if (T < 0.05f) T = 0.05f;
	float u = Clampf((t - f.spawn) / T, 0.f, 1.f);
	float tt = u * T;
	V3 p = Lerp(f.start, f.end, u);
	p.y = f.y0 + f.vy * tt - 0.5f * f.grav * tt * tt;
	return p;
}

static Fall RockOf(const Shot& s, V3 a, V3 d, int i)
{
	float y0 = 7.4f + s.sky;
	float spawn = 0.10f + i * 0.05f;
	float hit = s.lag + i * 0.20f;
	float spread = 0.72f;
	if (s.verb == VB_APOC) { y0 = 9.0f + s.sky; spawn = 0.08f + i * 0.07f; hit = s.lag + i * 0.16f; spread = 1.45f; }
	else if (s.verb == VB_STORM) { y0 = 4.6f + s.sky * 0.25f; spawn = 0.02f + i * 0.045f; hit = s.lag + i * 0.14f; spread = 1.15f; }
	else if (s.verb == VB_GRAV) { y0 = 10.8f + s.sky; spawn = 0.06f + i * 0.08f; hit = s.lag + i * 0.26f; spread = 0.4f; }
	else if (s.verb == VB_RAIN) { y0 = 5.4f + s.sky * 0.2f; spawn = 0.05f + i * 0.04f; hit = s.lag + i * 0.11f; spread = 0.95f; }
	else if (s.verb == VB_ICE) { y0 = 5.8f + s.sky; spawn = 0.08f + i * 0.06f; hit = s.lag + i * 0.16f; spread = 1.05f; }
	return MakeFall(a, d, y0, spawn, hit, s.side, i, spread);
}

static float Snap01(float x)
{
	x = Clampf(x, 0.f, 1.f);
	float k = 1.f - x;
	return 1.f - k * k * k;
}

// 二者を同じ画面に残す。視線は技の先。カメラは斜め前の横で、攻撃者の真後ろには置かない。
static void PairFrame(V3 a, V3 d, float h, float side, float t, float foeBias, float elev, V3& look, V3& eye)
{
	V3 caster = a + v3(0, h * 0.58f, 0);
	V3 foe = d + v3(0, 0.78f, 0);
	V3 axis = foe - caster;
	axis.y = 0;
	float span = Len(axis);
	if (span < 0.2f) axis = v3(1.f, 0, 0);
	else axis = axis * (1.f / span);
	V3 flank = v3(-axis.z, 0, axis.x);
	if (side < 0.f) flank = flank * -1.f;
	float slide = sinf(t * 0.42f) * 0.18f;
	V3 sideV = flank * cosf(slide) + axis * sinf(slide);
	if (foeBias < 0.42f) foeBias = 0.42f;
	if (foeBias > 0.68f) foeBias = 0.68f;
	look = Lerp(caster, foe, foeBias);
	float dist = span * 0.5f + 3.05f;
	if (dist < 3.4f) dist = 3.4f;
	if (dist > 5.8f) dist = 5.8f;
	eye = look + sideV * (dist * 0.9f) + axis * (dist * 0.12f);
	eye.y = look.y + elev;
}

static void BlendCam(float u, V3 l0, V3 e0, V3 l1, V3 e1, V3& look, V3& eye)
{
	u = Smooth01(Clampf(u, 0.f, 1.f));
	look = Lerp(l0, l1, u);
	eye = Lerp(e0, e1, u);
}

static void ShotCam(const Shot& s, float t, V3 a, V3 d, float h, V3& look, V3& eye)
{
	float side = s.side;
	V3 chestA = a + v3(0, h * 0.72f, 0);
	V3 axis = d - a;
	axis.y = 0;
	if (Len(axis) < 0.2f) axis = v3(0, 0, 1.f);
	else axis = Norm(axis);
	V3 flank = v3(-axis.z, 0, axis.x);
	if (side < 0.f) flank = flank * -1.f;
	V3 shoulder = chestA - axis * 0.85f + flank * 1.35f;
	V3 chestD = d + v3(0, 0.82f, 0);
	V3 footD = d + v3(0, 0.35f, 0);
	switch (s.verb) {
	case VB_METEOR: {
		Fall f = RockOf(s, a, d, 0);
		float rise = s.rush * 0.55f;
		if (rise < 0.24f) rise = 0.24f;
		V3 rock = FallAt(f, t < f.spawn ? f.spawn : (t < rise ? t : rise));
		V3 skyE = rock + v3(1.65f * side, 0.42f, -1.15f);
		if (t < rise) {
			float u = Smooth01(t / rise);
			look = Lerp(Lerp(chestA, chestD, 0.45f), FallAt(f, t < f.spawn ? f.spawn : t), u);
			eye = Lerp(a + v3(1.15f * side, h * 0.9f, -1.35f), skyE, u * u);
		} else {
			float snap = Snap01((t - rise) / 0.5f);
			look = Lerp(rock, Lerp(chestA, chestD, 0.62f), snap);
			eye = Lerp(skyE, shoulder + v3(0, 0.45f, 0), snap);
		}
		break;
	}
	case VB_APOC: {
		float rise = s.rush * 0.72f;
		if (rise < 0.35f) rise = 0.35f;
		float ang = t * (0.85f + fabsf(side) * 0.25f) * (side >= 0.f ? 1.f : -1.f);
		V3 hub = Lerp(a, d, 0.42f) + v3(0, 7.6f + s.sky, 0);
		V3 eyeS = hub + v3(sinf(ang) * 2.6f, 0.55f, cosf(ang) * 2.6f);
		if (t < rise) {
			float u = Smooth01(t / rise);
			look = Lerp(chestA, hub, u);
			eye = Lerp(a + v3(1.3f * side, h, -1.2f), eyeS, u);
		} else {
			float snap = Snap01((t - rise) / 0.55f);
			look = Lerp(hub, Lerp(chestA, d + v3(0, 0.7f, 0), 0.6f), snap);
			eye = Lerp(eyeS, shoulder + v3(0, 1.1f, 0), snap);
		}
		break;
	}
	case VB_STORM: {
		V3 high = Lerp(a, d, 0.55f) + v3(side * 0.8f, 4.4f + s.sky, -0.3f);
		float dive = Smooth01(t / max(0.28f, s.rush));
		look = Lerp(high + v3(0, 1.1f, 0), Lerp(chestA, d + v3(0, 0.75f, 0), 0.58f), dive);
		eye = Lerp(high + v3(1.5f * side, 0.15f, -0.7f), shoulder + v3(0, 0.35f, 0), dive);
		break;
	}
	case VB_GRAV: {
		float u = Clampf(t / max(0.3f, s.lag), 0.f, 1.f);
		look = Lerp(d + v3(side * 0.2f, 0.45f + (1.f - u) * (6.2f + s.sky), 0), chestA, 0.28f);
		eye = Lerp(d + v3(1.7f * side, 0.55f, 2.5f), shoulder, 0.45f);
		break;
	}
	case VB_BOLT:
	case VB_CHAIN: {
		V3 sky = d + v3(side * 0.35f, 6.2f + s.sky, 0.2f);
		float cut = s.rush * 0.62f;
		if (t < cut) {
			float u = Smooth01(t / max(0.12f, cut));
			look = Lerp(Lerp(chestA, chestD, 0.4f), sky, u);
			eye = Lerp(Lerp(chestA, chestD, 0.35f) + v3(side, h, -1.6f), d + v3(1.15f * side, 1.05f, 2.05f), u);
		} else {
			float u = Smooth01((t - cut) / 0.42f);
			look = Lerp(sky, Lerp(chestA, footD, 0.6f), u);
			eye = Lerp(d + v3(1.15f * side, 1.05f, 2.05f), shoulder + v3(0, 0.2f, 0), u);
		}
		break;
	}
	case VB_ICE: {
		float u = Smooth01(t / max(0.35f, s.rush));
		look = Lerp(Lerp(chestA, chestD, 0.5f), Lerp(chestA, d + v3(0, 0.9f, 0), 0.5f), u);
		eye = Lerp(shoulder, shoulder + v3(0, 0.55f, 0), u);
		break;
	}
	case VB_RAIN:
	case VB_SHOT: {
		Fall f = RockOf(s, a, d, 0);
		float rise = s.rush * 0.5f;
		if (rise < 0.18f) rise = 0.18f;
		V3 dart = FallAt(f, t < rise ? t : rise);
		if (t < rise) {
			look = dart;
			eye = dart + v3(1.1f * side, 0.35f, -1.4f);
		} else {
			float snap = Snap01((t - rise) / 0.45f);
			look = Lerp(dart, Lerp(chestA, chestD, 0.6f), snap);
			eye = Lerp(dart + v3(1.1f * side, 0.35f, -1.4f), shoulder, snap);
		}
		break;
	}
	case VB_BEAM:
	case VB_THRUST: {
		V3 dir = d - a; dir.y = 0;
		if (Len(dir) < 0.2f) dir = v3(0, 0, 1);
		dir = Norm(dir);
		V3 lateral = v3(-dir.z, 0, dir.x) * side;
		float u = Smooth01(t / max(0.16f, s.rush));
		float lead = u * 1.15f;
		if (lead > 1.f) lead = 1.f;
		if (lead > 0.62f) lead = 0.62f;
		look = Lerp(chestA, chestD, lead);
		eye = Lerp(a - dir * 1.1f + lateral * 1.15f + v3(0, h * 0.45f, 0), shoulder, u);
		break;
	}
	case VB_BREATH: {
		float u = Smooth01(t / max(0.2f, s.rush));
		look = Lerp(chestA, Lerp(chestA, d + v3(0, 0.8f, 0), 0.62f), u);
		eye = Lerp(a + v3(1.1f * side, h * 0.85f, -0.35f), shoulder, u);
		break;
	}
	case VB_PILLAR:
	case VB_RAISE: {
		float u = Smooth01(t / max(0.25f, s.rush));
		V3 top = d + v3(side * 0.4f, 5.5f + s.sky, 0.2f);
		V3 downL = Lerp(chestA, d + v3(0, s.verb == VB_RAISE ? 1.3f : 0.8f, 0), 0.55f);
		look = Lerp(Lerp(top, chestA, 0.22f), downL, u);
		eye = Lerp(shoulder + v3(0, 2.2f + s.sky * 0.35f, 0), shoulder + v3(0, 0.4f, 0), u);
		break;
	}
	case VB_NOVA: {
		float u = Smooth01(t / max(0.2f, s.rush));
		float pull = sinf(u * PI);
		look = Lerp(chestA, d + v3(0, 0.8f, 0), 0.55f);
		eye = shoulder + v3(0, pull * 0.45f, -pull * 0.35f);
		break;
	}
	case VB_QUAKE:
	case VB_DRUM: {
		float u = Smooth01(t / max(0.18f, s.rush));
		look = Lerp(chestA, Lerp(chestA, footD, 0.58f), u);
		eye = Lerp(shoulder + v3(0, -0.15f, 0), shoulder + v3(0, 0.2f, 0), u);
		if (t >= s.lag) eye.y += sinf((t - s.lag) * 6.f) * 0.04f;
		break;
	}
	case VB_GALE: {
		float u = Snap01(t / max(0.16f, s.rush));
		look = Lerp(chestA, d + v3(0, 0.8f, 0), 0.58f);
		eye = Lerp(shoulder - flank * 0.35f, shoulder + flank * 0.45f, u);
		break;
	}
	case VB_DARK: {
		float u = Smooth01(t / max(0.2f, s.rush));
		look = Lerp(chestA, Lerp(chestA, chestD, 0.6f), u);
		eye = Lerp(chestA + flank * 0.8f, shoulder, u);
		break;
	}
	case VB_HEAL: {
		float u = Smooth01(t / 1.1f);
		look = Lerp(chestA, d + v3(0, 0.95f, 0), 0.42f + u * 0.08f);
		eye = shoulder + v3(0, 0.15f + u * 0.35f, 0);
		break;
	}
	case VB_AURA: {
		float u = Smooth01(t / 1.2f);
		look = chestA + v3(0, 0.1f * u, 0);
		eye = shoulder + flank * (u * 0.25f) + v3(0, 0.2f, 0);
		break;
	}
	case VB_WARP: {
		float u = Smooth01((t - s.rush * 0.35f) / 0.4f);
		look = Lerp(chestA, Lerp(chestA, d + v3(0, 0.8f, 0), 0.55f), u);
		eye = Lerp(shoulder - axis * 0.2f, shoulder + flank * 0.2f, u);
		break;
	}
	case VB_FAN: {
		float u = Smooth01(t / max(0.18f, s.rush));
		float yaw = (u - 0.5f) * 1.4f * side;
		look = Lerp(chestA, d + v3(0, 0.8f, 0), 0.5f + u * 0.1f);
		eye = shoulder + flank * (yaw * 0.35f);
		break;
	}
	case VB_BACK: {
		float u = Smooth01(t / max(0.16f, s.rush));
		look = Lerp(chestD, chestA, 0.42f);
		eye = Lerp(d - axis * 1.3f + flank * 0.8f + v3(0, 1.1f, 0), shoulder, u * 0.35f);
		break;
	}
	case VB_MARK: {
		float u = Smooth01(t / max(0.2f, s.rush));
		look = Lerp(Lerp(chestA, d + v3(0, 0.2f, 0), 0.55f), Lerp(chestA, chestD, 0.58f), u);
		eye = Lerp(shoulder + v3(0, 1.6f, 0), shoulder, u);
		break;
	}
	case VB_SPIN: {
		float u = Smooth01(t / max(0.4f, s.rush));
		look = Lerp(chestA, d + v3(0, 0.85f, 0), 0.52f);
		eye = Lerp(shoulder - flank * 0.2f, shoulder + flank * 0.35f, u);
		break;
	}
	default: {
		float u = Smooth01(t / max(0.18f, s.rush));
		look = Lerp(chestA, Lerp(chestA, chestD, 0.6f), u);
		eye = Lerp(shoulder + flank * 0.15f, shoulder - axis * 0.15f, u);
		if (t >= s.lag) {
			float dip = expf(-(t - s.lag) * 4.f) * 0.08f;
			look.y -= dip;
		}
		break;
	}
	}
	V3 verbL = look;
	V3 verbE = eye;
	float bias = 0.56f;
	float elev = 0.62f;
	if (s.verb == VB_METEOR || s.verb == VB_APOC || s.verb == VB_STORM || s.verb == VB_RAIN) elev = 1.05f;
	else if (s.verb == VB_GRAV || s.verb == VB_QUAKE || s.verb == VB_DRUM) elev = 0.32f;
	else if (s.verb == VB_HEAL || s.verb == VB_AURA || s.verb == VB_RAISE) { bias = 0.4f; elev = 0.72f; }
	else if (s.verb == VB_BACK || s.verb == VB_WARP) bias = 0.46f;
	else if (s.verb == VB_PILLAR || s.verb == VB_BOLT || s.verb == VB_CHAIN) elev = 0.9f;
	V3 pairL, pairE;
	PairFrame(a, d, h, side, t, bias, elev, pairL, pairE);
	float skyW = Clampf((verbL.y - 2.1f) / 3.2f, 0.f, 1.f);
	V3 groundL = Lerp(verbL, pairL, 0.84f);
	V3 groundE = Lerp(verbE, pairE, 0.86f);
	V3 skyL = Lerp(verbL, Lerp(chestA, chestD, 0.48f), 0.24f);
	V3 skyE = Lerp(verbE, pairE + v3(0, 0.55f, 0), 0.42f);
	look = Lerp(groundL, skyL, skyW);
	eye = Lerp(groundE, skyE, skyW);
	if (Len(eye - look) < 0.85f) eye = pairE;
}

static float U01(int i, int salt)
{
	unsigned h = (unsigned)i * 2246822519u ^ (unsigned)salt * 3266489917u;
	h ^= h >> 16;
	h *= 668265263u;
	return (h & 65535u) / 65535.f;
}

static int FxKindFor(int verb)
{
	switch (verb) {
	case VB_METEOR: case VB_APOC: case VB_STORM: case VB_GRAV: return CFX_ROCK;
	case VB_BOLT: return CFX_BOLT;
	case VB_CHAIN: return CFX_BOLT;
	case VB_ICE: return CFX_ICE;
	case VB_BEAM: return CFX_BEAM;
	case VB_BREATH: return CFX_BREATH;
	case VB_PILLAR: case VB_RAISE: return CFX_PILLAR;
	case VB_NOVA: return CFX_RING;
	case VB_QUAKE: return CFX_CRACK;
	case VB_GALE: return CFX_GALE;
	case VB_DARK: return CFX_DARK;
	case VB_HEAL: return CFX_HEAL;
	case VB_AURA: return CFX_SIGIL;
	case VB_WARP: return CFX_WARP;
	case VB_FAN: case VB_SLASH: return CFX_SLASH;
	case VB_BACK: return CFX_DARK;
	case VB_DRUM: return CFX_RING;
	case VB_MARK: return CFX_SIGIL;
	case VB_RAIN: case VB_SHOT: case VB_THRUST: return CFX_ARROW;
	case VB_HEAVY: case VB_SPIN: return CFX_SLASH;
	default: return CFX_SLASH;
	}
}

BOOL Game::CutSkillCam(float t, V3& look, V3& eye)
{
	if (!m_seqCut || m_cutKind != 0 || m_seqSkill < 0) return FALSE;
	Shot s = ShotOf(m_seqSkill);
	if (s.verb < 0) return FALSE;
	V3 a = m_cutPosA;
	V3 d = (m_cutD >= 0) ? m_cutPosD : a;
	if (Len(d - a) < 0.25f) d = a + v3(sinf(m_cutYaw), 0, cosf(m_cutYaw)) * 2.4f;
	float h = 1.6f;
	if (m_cutA >= 0 && m_cutA < m_b.nUnits) h = m_act[m_cutA].height;
	if (h < 0.8f) h = 0.8f;
	ShotCam(s, t, a, d, h, look, eye);
	return TRUE;
}

float Game::CutShotHold() const
{
	if (m_cutKind != 0 || m_seqSkill < 0) return 0.f;
	Shot s = ShotOf(m_seqSkill);
	if (s.verb < 0) return 0.f;
	return s.span;
}

void Game::CutSkillWorld()
{
	if (!m_seqCut || m_cutStage != 2) return;
	if ((m_cutKind != 0 && m_cutKind != 1) || m_seqSkill < 0) { m_cutFxN = 0; return; }
	m_cutFxN = 0;
	Shot s = ShotOf(m_seqSkill);
	if (s.verb < 0) return;
	const SkillDef& sd = SkillInfo(m_seqSkill);
	DWORD col = ElemCol(sd.element);
	float cr = (col & 255) / 255.f;
	float cg = ((col >> 8) & 255) / 255.f;
	float cb = ((col >> 16) & 255) / 255.f;
	float t = m_cutStageT;
	V3 a = m_cutPosA;
	V3 dpos = (m_cutD >= 0) ? m_cutPosD : (a + v3(sinf(m_cutYaw), 0, cosf(m_cutYaw)) * 2.2f);
	auto fx = [&](V3 p, V3 dir, float size, int kind, float spin, float param, float stretch, float r, float g, float b, float alpha) {
		if (m_cutFxN >= CUT_FX_MAX || alpha < 0.03f || size < 0.02f) return;
		size *= (m_cutKind == 1 && IsCraftSuper(m_seqSkill)) ? 1.7f : 1.2f;
		if (alpha < 0.96f) alpha *= 1.16f;
		if (alpha > 1.f) alpha = 1.f;
		CutFxPt& o = m_cutFx[m_cutFxN++];
		o.p[0] = p.x; o.p[1] = p.y; o.p[2] = p.z;
		o.vel[0] = dir.x; o.vel[1] = dir.y; o.vel[2] = dir.z; o.vel[3] = stretch;
		o.sz[0] = size; o.sz[1] = (float)kind; o.sz[2] = spin; o.sz[3] = param;
		o.c[0] = r; o.c[1] = g; o.c[2] = b; o.c[3] = alpha;
	};
	auto once = [&](int bit, float when) -> BOOL {
		if (bit < 0 || bit > 30 || t < when) return FALSE;
		unsigned m = 1u << bit;
		if (m_cutShotMark & m) return FALSE;
		m_cutShotMark |= m;
		return TRUE;
	};
	auto punch = [&](float mag) {
		Shake(mag);
		float dur = 0.20f + mag * 0.26f;
		if (m_hitScreenT < dur) { m_hitScreenT = m_hitScreenMax = dur; }
		if (m_flashWhite < mag * 0.4f) m_flashWhite = mag * 0.4f;
	};
	int kind = FxKindFor(s.verb);
	// 攻撃側の段が尽きるまで出し続ける。段のあいだで消えない。
	int waves = 0;
	int lastStep = 0;
	for (int i = 0; i < m_seqN; i++) {
		const SeqStep& st = m_seq[i];
		if (st.counter) continue;
		if (st.attacker != m_seqCaster) continue;
		if (st.skill >= 0 && st.skill != m_seqSkill) continue;
		lastStep = i;
		waves++;
	}
	if (waves < 1) waves = s.hits > 0 ? s.hits : 1;
	BOOL live = TRUE;
	if (m_seqN > 0) {
		live = FALSE;
		if (m_seqCur < lastStep) live = TRUE;
		else if (m_seqCur == lastStep && (m_stepTi < 0.02f || m_seqT < m_stepTi + 0.05f)) live = TRUE;
	}
	float firstHit = s.span;
	if (firstHit < 0.62f) firstHit = 0.62f;
	float tail = (waves > 1) ? (0.42f + (waves - 1) * 0.40f) : 0.28f;
	float est = firstHit + tail + 0.16f;
	if (live && t + 0.20f > est) est = t + 0.20f;
	if (est > 8.f) est = 8.f;
	if (est > m_cutRainEnd) m_cutRainEnd = est;
	float landUntil = m_cutRainEnd;
	if (landUntil < firstHit) landUntil = firstHit;
	BOOL multi = (waves >= 2 || sd.hits >= 2 || sd.area >= 2);
	BOOL drain = (sd.effect == EF_DRAIN);
	if (m_seqCur >= 0 && m_seqCur < 24 && m_seqCur < m_seqN && m_seqT >= m_stepTi && m_stepTi > 0.02f) {
		const SeqStep& st = m_seq[m_seqCur];
		if (!st.counter && st.attacker == m_seqCaster && once(m_seqCur, 0.f)) {
			float mag = 0.28f;
			if (s.verb == VB_METEOR || s.verb == VB_APOC) mag = 0.62f;
			else if (s.verb == VB_HEAVY || s.verb == VB_QUAKE || s.verb == VB_DRUM || s.verb == VB_GRAV) mag = 0.46f;
			else if (s.verb == VB_BOLT || s.verb == VB_STORM || s.verb == VB_ICE) mag = 0.4f;
			punch(mag);
			if (s.scatter || s.crack || s.verb == VB_METEOR || s.verb == VB_RAIN || s.verb == VB_BOLT)
				SpawnDebris(dpos, v3(0, 2.2f, 0), (s.verb == VB_ICE) ? 2 : 0, s.verb == VB_APOC ? 10 : 6, 0.35f, col);
			Burst(dpos + v3(0, 0.2f, 0), 8, 2.2f, 0.12f, 0.36f, cr, cg, cb, 1, 1, 1.4f, 0, 0.45f);
		}
	}
	if (sd.element == EL_HOLY && sd.kind == SK_MAGIC) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (nf > 8) nf = 8;
		int angels = nf < 3 ? 3 : nf;
		for (int i = 0; i < angels; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i % m_cutFoeN] : dpos;
			float wing = (i - (angels - 1) * 0.5f) * 1.35f;
			V3 from = a + v3(-2.4f + wing, 4.8f + (i % 3) * 0.45f, -1.6f + wing * 0.25f);
			float launch = 0.18f + i * 0.16f;
			float flight = 0.72f;
			float hit = launch + flight;
			if (t < launch) {
				float g = Clampf((launch - t) / 0.18f, 0.f, 1.f);
				fx(from + v3(0, 0.4f, 0), v3(0, 1, 0), 0.55f, CFX_PILLAR, t * 2.f + i, 0.2f, 1.4f, 1.f, 0.95f, 0.72f, 0.35f * g);
				continue;
			}
			if (t <= hit) {
				float u = Clampf((t - launch) / flight, 0.f, 1.f);
				u = u * u;
				V3 p = Lerp(from, aim + v3(0, 0.85f, 0), u);
				fx(p, aim - from, 0.62f, CFX_PILLAR, t * 3.f + i, 0.15f, 1.6f, 1.f, 0.96f, 0.75f, 1.f);
				fx(p + v3(0.42f, 0.15f, 0), v3(1, 0.2f, 0), 0.38f, CFX_SLASH, t * 4.f, 0.4f, 1.2f, 1.f, 1.f, 0.9f, 0.7f);
				fx(p + v3(-0.42f, 0.15f, 0), v3(-1, 0.2f, 0), 0.38f, CFX_SLASH, -t * 4.f, 0.4f, 1.2f, 1.f, 1.f, 0.9f, 0.7f);
			} else if (t - hit < 0.45f) {
				float fade = 1.f - (t - hit) / 0.45f;
				fx(aim + v3(0, 0.7f, 0), v3(0, 1, 0), 0.4f + (t - hit) * 2.2f, CFX_RING, 0, fade, 1.f, 1.f, 0.95f, 0.7f, fade);
				fx(aim + v3(0, 0.35f, 0), v3(1, 0, 0), 0.7f, CFX_PILLAR, (float)i, 0.1f, 1.f, 1.f, 0.92f, 0.6f, fade);
			}
		}
		return;
	}
	if (s.verb == VB_ICE) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		float open = 0.22f;
		int spikes = nf * 4;
		if (spikes < 8) spikes = 8;
		if (spikes > 28) spikes = 28;
		for (int i = 0; i < spikes; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i % nf] : dpos;
			float ang = U01(i, m_seqSkill + 5) * PI * 2.f;
			float rad = (i % nf == 0) ? 0.12f : (0.25f + U01(i, 8) * 0.85f);
			V3 foot = aim + v3(cosf(ang) * rad, 0, sinf(ang) * rad);
			float riseAt = open + (i % nf) * 0.12f + (i / nf) * 0.05f;
			float grow = 0.42f;
			if (t < riseAt) continue;
			float u = Clampf((t - riseAt) / grow, 0.f, 1.f);
			float h = u * u * (1.15f + ((i % 3) == 0 ? 0.85f : 0.25f));
			V3 p = foot + v3(0, h * 0.5f - 0.15f, 0);
			fx(p, v3(0, 1, 0), 0.34f + h * 0.22f, CFX_ICE, ang, 0.05f, 1.8f + h, 0.75f, 0.9f, 1.f, 1.f);
			if (u > 0.82f && t - (riseAt + grow) < 0.28f && (i % nf) == 0) {
				float fade = 1.f - (t - riseAt - grow) / 0.28f;
				if (fade < 0.f) fade = 0.f;
				fx(aim + v3(0, 0.9f, 0), v3(0, 1, 0), 0.55f, CFX_RING, 0, fade, 1.f, 0.7f, 0.9f, 1.f, fade);
			}
		}
		return;
	}
	BOOL sky = (s.verb == VB_METEOR || s.verb == VB_APOC || s.verb == VB_STORM || s.verb == VB_GRAV || s.verb == VB_RAIN);
	if (sky || (s.verb == VB_SHOT && multi)) {
		float flight = 0.86f, gap = 0.09f, y0 = 7.6f + s.sky, spread = 1.15f, sz = 0.46f;
		int body = CFX_ROCK;
		if (s.verb == VB_APOC) { flight = 1.02f; gap = 0.11f; y0 = 9.4f + s.sky; spread = 1.85f; sz = 0.62f; }
		else if (s.verb == VB_STORM) { flight = 0.46f; gap = 0.07f; y0 = 4.8f + s.sky * 0.2f; spread = 1.45f; sz = 0.34f; }
		else if (s.verb == VB_GRAV) { flight = 1.15f; gap = 0.12f; y0 = 11.f + s.sky; spread = 0.42f; sz = 0.5f; }
		else if (s.verb == VB_ICE) { flight = 0.58f; gap = 0.075f; y0 = 6.2f + s.sky; spread = 1.25f; sz = 0.38f; body = CFX_ICE; }
		else if (s.verb == VB_RAIN) { flight = 0.38f; gap = 0.048f; y0 = 5.5f + s.sky * 0.15f; spread = 1.35f; sz = 0.30f; body = CFX_ARROW; }
		else if (s.verb == VB_SHOT) { flight = 0.32f; gap = 0.07f; y0 = 0.f; spread = 0.35f; sz = 0.28f; body = CFX_ARROW; }
		float open = 0.14f;
		int iMax = (int)((landUntil - open) / gap) + 1;
		if (iMax < 8) iMax = 8;
		if (iMax > 240) iMax = 240;
		int i0 = (int)((t - flight - 0.55f - open) / gap) - 1;
		if (i0 < 0) i0 = 0;
		for (int i = i0; i < iMax; i++) {
			float hit = open + i * gap + flight * 0.15f;
			if (s.verb == VB_METEOR || s.verb == VB_APOC || s.verb == VB_GRAV) hit = open + flight + i * gap;
			if (hit > landUntil + 0.08f) break;
			float spawn = hit - flight;
			if (spawn > t) break;
			float ang = U01(i, m_seqSkill + 3) * PI * 2.f;
			float rad = spread * (0.25f + U01(i, m_seqSkill + 9) * 0.75f);
			int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i % nf] : dpos;
			V3 end = aim + v3(cosf(ang) * rad * 0.35f, 0, sinf(ang) * rad * 0.35f);
			BOOL meteor = (s.verb == VB_METEOR || s.verb == VB_APOC || s.verb == VB_GRAV);
			V3 from = (y0 > 0.2f) ? (end + v3((U01(i, 1) - 0.5f) * 1.4f, y0, (U01(i, 2) - 0.5f) * 1.4f)) : (a + v3(0, 1.15f, 0));
			if (meteor) {
				float side = (U01(i, 6) - 0.35f) * 9.5f;
				float depth = (U01(i, 7) - 0.5f) * 4.5f;
				from = end + v3(side, y0 + U01(i, 11) * 2.4f, depth);
			}
			float since = t - hit;
			if (t >= spawn && t <= hit) {
				float u = Clampf((t - spawn) / flight, 0.f, 1.f);
				V3 p = Lerp(from, end + v3(0, 0.32f, 0), u);
				if (y0 > 0.2f && !meteor) p.y = y0 * (1.f - u) * (1.f - u) + 0.32f;
				float big = ((i % 6) == 0) ? 1.45f : 1.f;
				fx(p, end - from, sz * big, body, t * 2.4f + i, (body == CFX_ICE) ? 0.15f : u, 1.15f, cr, cg, cb, 1.f);
			}
			if (since >= 0.f && since < 0.50f && m_cutFxN < CUT_FX_MAX - 24) {
				float fade = 1.f - since / 0.50f;
				if (s.crack && (i % 2) == 0) {
					int nc = (s.verb == VB_GRAV) ? 4 : 3;
					for (int k = 0; k < nc; k++) {
						float ca = ang + k * (PI * 2.f / (float)nc);
						V3 dir = v3(cosf(ca), 0, sinf(ca));
						float heat = (s.verb == VB_ICE || s.verb == VB_GRAV) ? 0.05f : 0.85f;
						fx(end + dir * 0.12f, dir, 0.7f + ((i % 6) == 0 ? 0.35f : 0.f), CFX_CRACK, ca, heat, 1.5f, cr, cg, cb, fade);
					}
				}
				if (s.scatter && since < 0.28f && (i % 3) == 0) {
					float hop = sinf(Clampf(since / 0.28f, 0.f, 1.f) * PI);
					for (int k = 0; k < 2; k++) {
						float ca = ang + k * 2.2f;
						V3 dir = v3(cosf(ca), 0, sinf(ca));
						V3 p = end + dir * (0.25f + since * 2.6f) + v3(0, hop * 0.7f, 0);
						fx(p, dir, 0.18f, (body == CFX_ICE) ? CFX_ICE : CFX_ROCK, since * 6.f, 0.35f, 1.f, cr, cg, cb, fade);
					}
				}
				if ((i % 4) == 0 && since < 0.22f)
					fx(end, v3(1, 0, 0), 0.28f + since * 2.8f, (s.verb == VB_GRAV) ? CFX_DUST : CFX_RING, 0, fade, 1.f, cr, cg, cb, fade);
			}
		}
		return;
	}
	if (s.verb == VB_BOLT || s.verb == VB_CHAIN) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (t < 0.85f) {
			float g = Smooth01(t / 0.85f);
			V3 hub = dpos + v3(0, 5.4f + g * 1.2f, 0);
			for (int c = 0; c < 7; c++) {
				float ca = c * 0.9f + t * 0.4f;
				float rr = 1.2f + (c % 3) * 0.55f;
				V3 cloud = hub + v3(cosf(ca) * rr, 0.2f * sinf(t + c), sinf(ca) * rr * 0.65f);
				fx(cloud, v3(0, 1, 0), 1.1f + g * 0.8f, CFX_DUST, ca, 0.15f, 1.4f, 0.25f, 0.28f, 0.34f, 0.35f + g * 0.45f);
			}
		}
		float gap = 0.10f;
		float flight = 0.20f;
		float open = 0.18f;
		int iMax = (int)((landUntil - open) / gap) + 1;
		if (iMax < 6) iMax = 6;
		if (iMax > 80) iMax = 80;
		for (int i = 0; i < iMax; i++) {
			float hit = open + flight + i * gap;
			if (hit > landUntil + 0.05f) break;
			float spawn = hit - flight;
			if (t + 0.02f < spawn) break;
			float lat = (U01(i, m_seqSkill) - 0.5f) * 2.4f;
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i % nf] : dpos;
			V3 top = aim + v3(lat * s.side, 6.2f + s.sky, (U01(i, 4) - 0.5f) * 1.2f);
			V3 tip = aim + v3(lat * 0.15f, 0.7f, 0);
			if (t < hit && t >= spawn - 0.05f) {
				float gather = Clampf((t - spawn) / flight, 0.f, 1.f);
				fx(Lerp(top, tip, gather * 0.2f), v3(0, -1, 0), 0.28f + gather * 0.35f, CFX_BOLT, t * 9.f + i, 0.7f, 0.55f, cr, cg, cb, 0.3f + gather * 0.45f);
			}
			float since = t - hit;
			if (since >= 0.f && since < 0.22f) {
				float fade = 1.f - since / 0.22f;
				if (m_cutFoeN > 0 && since < 0.08f) {
					int id = m_cutFoe[i % m_cutFoeN];
					if (id >= 0 && id < m_b.nUnits) m_b.u[id].flash = 1.f;
				}
				fx(Lerp(top, tip, 0.55f), tip - top, 1.05f, CFX_BOLT, i * 1.3f, 1.f, 1.f, cr, cg, cb, fade);
				fx(tip, v3(1, 0, 0), 0.35f + since * 2.8f, CFX_RING, 0, fade, 1.f, cr, cg, cb, fade);
			}
		}
		return;
	}
	if (sd.kind == SK_MAGIC && sd.element == EL_WIND) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (nf > 8) nf = 8;
		for (int i = 0; i < nf; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i] : dpos;
			for (int k = 0; k < 6; k++) {
				float launch = 0.12f + k * 0.06f;
				float flight = 0.7f;
				float side = ((k & 1) ? 1.f : -1.f) * (2.4f + k * 0.35f);
				V3 from = a + v3(side, 1.6f + (k % 3) * 0.45f, -1.2f + k * 0.2f);
				if (t < launch) continue;
				float u = Clampf((t - launch) / flight, 0.f, 1.f);
				float spin = t * 6.f + k + i;
				V3 p = Lerp(from, aim + v3(0, 0.9f, 0), u * u);
				p = p + v3(cosf(spin) * (1.f - u) * 0.55f, sinf(spin) * 0.25f, sinf(spin) * (1.f - u) * 0.55f);
				fx(p, aim - from, 0.42f, CFX_HEAL, spin, u, 1.3f, 0.55f, 0.95f, 0.62f, 0.85f);
			}
			float cutAt = 0.55f + i * 0.08f;
			if (t >= cutAt && t - cutAt < 0.28f) {
				float u = (t - cutAt) / 0.28f;
				V3 p = Lerp(a + v3(0, 1.1f, 0), aim + v3(0, 0.85f, 0), u);
				fx(p, aim - a, 0.85f, CFX_SLASH, t * 8.f, u, 1.8f, 0.75f, 1.f, 0.8f, 1.f - u * 0.3f);
			}
		}
		return;
	}
	if (sd.kind == SK_MAGIC && sd.element == EL_EARTH && s.verb != VB_GRAV) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		int n = nf * 3;
		if (n < 6) n = 6;
		if (n > 24) n = 24;
		for (int i = 0; i < n; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i % nf] : dpos;
			float ang = U01(i, 4) * PI * 2.f;
			float rad = (i % 3 == 0) ? 0.1f : 0.35f + U01(i, 2) * 0.7f;
			V3 foot = aim + v3(cosf(ang) * rad, 0, sinf(ang) * rad);
			float riseAt = 0.16f + (i % nf) * 0.1f;
			if (t < riseAt) continue;
			float u = Clampf((t - riseAt) / 0.38f, 0.f, 1.f);
			float h = u * u * (0.7f + (i % 3) * 0.35f);
			fx(foot + v3(0, h * 0.45f, 0), v3(0, 1, 0), 0.42f + h * 0.2f, CFX_ROCK, ang, 0.2f, 1.2f, 0.72f, 0.5f, 0.28f, 1.f);
			if (u > 0.9f && (i % nf) == 0) {
				float fade = Clampf(1.f - (t - riseAt - 0.38f) / 0.35f, 0.f, 1.f);
				fx(aim, v3(1, 0, 0), 0.4f + (1.f - fade) * 1.6f, CFX_DUST, 0, fade, 1.f, 0.65f, 0.48f, 0.28f, fade);
			}
		}
		return;
	}
	if (sd.kind == SK_MAGIC && sd.element == EL_DARK) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (nf > 8) nf = 8;
		for (int i = 0; i < nf; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i] : dpos;
			float u = Clampf(t / 0.7f, 0.f, 1.f);
			fx(aim + v3(0, u * 1.6f, 0), v3(0, 1, 0), 0.5f, CFX_DARK, t, 0.4f, 2.4f, cr, cg, cb, 0.75f);
			for (int k = 0; k < 5; k++) {
				float ang = t * 2.2f + k * 1.256f + i;
				float rad = 1.3f * (1.f - u) + 0.15f;
				V3 p = aim + v3(cosf(ang) * rad, 0.4f + u * 0.8f, sinf(ang) * rad);
				fx(p, aim - p, 0.28f, CFX_DARK, ang, u, 1.f, cr, cg, cb, 0.8f);
			}
		}
		return;
	}
	if (sd.kind == SK_MAGIC && sd.element == EL_POISON) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (nf > 8) nf = 8;
		for (int i = 0; i < nf; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i] : dpos;
			for (int k = 0; k < 5; k++) {
				float birth = 0.08f + k * 0.11f;
				if (t < birth) continue;
				float u = t - birth;
				float pop = u > 0.85f ? Clampf(1.f - (u - 0.85f) / 0.25f, 0.f, 1.f) : 1.f;
				float ang = k * 1.3f + i;
				V3 p = aim + v3(cosf(ang) * 0.28f, 0.15f + u * 0.9f, sinf(ang) * 0.28f);
				fx(p, v3(0, 1, 0), 0.16f + u * 0.12f, CFX_RING, ang, pop, 1.f, 0.55f, 0.95f, 0.28f, pop);
			}
		}
		return;
	}
	if (sd.kind == SK_PHYS && (s.verb == VB_SLASH || s.verb == VB_HEAVY || s.verb == VB_FAN || s.verb == VB_THRUST)) {
		int nf = m_cutFoeN > 0 ? m_cutFoeN : 1;
		if (nf > 6) nf = 6;
		int arcs = (s.verb == VB_HEAVY) ? 2 : 3;
		for (int i = 0; i < nf; i++) {
			V3 aim = (m_cutFoeN > 0) ? m_cutFoePos[i] : dpos;
			for (int k = 0; k < arcs; k++) {
				float launch = 0.12f + k * 0.16f + i * 0.05f;
				float flight = (s.verb == VB_THRUST) ? 0.28f : 0.22f;
				if (t < launch || t > launch + flight + 0.12f) continue;
				float u = Clampf((t - launch) / flight, 0.f, 1.f);
				float fade = (t <= launch + flight) ? 1.f : Clampf(1.f - (t - launch - flight) / 0.12f, 0.f, 1.f);
				float lift = (k - 1) * 0.28f;
				V3 from = a + v3(0, 0.85f + lift, 0);
				V3 to = aim + v3(0, 0.7f + lift * 0.4f, 0);
				V3 p = Lerp(from, to, u);
				float yaw = (k - 1) * 0.55f * sinf(u * PI);
				p = p + v3(yaw, sinf(u * PI) * 0.35f, 0);
				fx(p, to - from, (s.verb == VB_HEAVY) ? 0.95f : 0.72f, CFX_SLASH, t * 5.f + k, u, 1.7f, cr, cg, cb, fade);
				if (u > 0.82f && k == 0)
					fx(to, v3(1, 0, 0), 0.35f + (u - 0.82f) * 2.2f, CFX_RING, 0, fade, 1.f, cr, cg, cb, fade);
			}
		}
		return;
	}
	if (drain || s.verb == VB_HEAL || s.verb == VB_RAISE || s.verb == VB_AURA || (s.verb == VB_DARK && !multi) || s.verb == VB_MARK || s.verb == VB_BEAM || s.verb == VB_BREATH || s.verb == VB_PILLAR) {
		float fade = 1.f;
		if (t > landUntil) fade = Clampf(1.f - (t - landUntil) / 0.35f, 0.f, 1.f);
		if (fade < 0.03f) return;
		if (drain) {
			fx(Lerp(dpos, a, 0.5f) + v3(0, 0.9f, 0), a - dpos, 0.55f, CFX_BEAM, t, 0.4f, 2.2f, cr, cg, cb, 0.55f * fade);
			int n = 16;
			for (int i = 0; i < n; i++) {
				float u = t * 0.72f + i / (float)n;
				u = u - (float)(int)u;
				float ang = i * 1.7f + t * 2.4f;
				V3 p = Lerp(dpos + v3(0, 0.85f, 0), a + v3(0, 1.05f, 0), u);
				p = p + v3(cosf(ang) * 0.16f, sinf(u * PI) * 0.12f, sinf(ang) * 0.16f);
				fx(p, a - dpos, 0.22f, CFX_DARK, ang, 0.35f, 1.f, cr, cg, cb, (0.35f + u * 0.6f) * fade);
			}
			return;
		}
		if (s.verb == VB_BEAM || s.verb == VB_BREATH) {
			V3 from = a + v3(0, 1.05f, 0);
			V3 to = dpos + v3(0, 0.8f, 0);
			fx(Lerp(from, to, 0.5f), to - from, s.verb == VB_BREATH ? 0.7f : 0.5f, kind, t, 0.6f, s.verb == VB_BREATH ? 1.8f : 2.6f, cr, cg, cb, 0.9f * fade);
			int n = (s.verb == VB_BREATH) ? 10 : 8;
			for (int i = 0; i < n; i++) {
				float u = t * (s.verb == VB_BREATH ? 1.1f : 1.6f) + i / (float)n;
				u = u - (float)(int)u;
				float w = sinf(u * PI) * 0.28f * s.side;
				V3 p = Lerp(from, to, u) + v3(w, sinf(t * 6.f + i) * 0.08f, w * 0.4f);
				fx(p, to - from, 0.24f + (1.f - u) * 0.2f, kind, t + i, u, 1.2f, cr, cg, cb, fade);
			}
			return;
		}
		if (s.verb == VB_PILLAR) {
			fx(dpos + v3(0, 2.2f + s.sky, 0), v3(0, 1, 0), 0.55f, CFX_PILLAR, t * 0.6f, 0.5f, 3.2f + s.sky, cr, cg, cb, 0.9f * fade);
			int n = 8;
			for (int i = 0; i < n; i++) {
				float u = t * 0.55f + i / (float)n;
				u = u - (float)(int)u;
				float ang = i * (PI * 2.f / (float)n) + t * 0.4f;
				V3 p = dpos + v3(cosf(ang) * 0.35f, u * (4.2f + s.sky), sinf(ang) * 0.35f);
				fx(p, v3(0, 1, 0), 0.28f, CFX_PILLAR, ang, 0.3f, 1.4f, cr, cg, cb, fade);
			}
			return;
		}
		if (s.verb == VB_MARK || (s.verb == VB_DARK && !multi)) {
			V3 c = dpos;
			fx(c + v3(0, 0.08f, 0), v3(1, 0, 0), 0.85f + s.sky * 0.2f, s.verb == VB_MARK ? CFX_SIGIL : CFX_DARK, t * 0.7f, 0.45f, 1.f, cr, cg, cb, 0.85f * fade);
			int n = 10;
			for (int i = 0; i < n; i++) {
				float u = t * 0.45f + i / (float)n;
				u = u - (float)(int)u;
				float ang = i * (PI * 2.f / (float)n) + t * (s.verb == VB_DARK ? -0.8f : 0.5f);
				float rad = 0.35f + u * 0.7f;
				V3 p = c + v3(cosf(ang) * rad, 0.15f + (s.verb == VB_DARK ? (1.f - u) * 1.4f : u * 1.1f), sinf(ang) * rad);
				fx(p, v3(0, s.verb == VB_DARK ? -1.f : 1.f, 0), 0.22f, kind, ang, 0.3f, 1.f, cr, cg, cb, fade * (0.45f + u));
			}
			return;
		}
		float u = Clampf(t / max(0.2f, landUntil), 0.f, 1.f);
		V3 c = (s.verb == VB_AURA) ? a : dpos;
		fx(c + v3(0, 0.06f, 0), v3(1, 0, 0), 0.85f + s.sky * 0.2f + u * 0.35f, CFX_SIGIL, t * 0.8f, 0.25f, 1.f, cr, cg, cb, 0.9f * fade);
		int n = (s.sky > 1.f || s.verb == VB_RAISE) ? 12 : 8;
		for (int i = 0; i < n; i++) {
			float ang = t * 1.1f + i * (PI * 2.f / (float)n);
			float rad = 0.4f + s.sky * 0.12f + (i & 1) * 0.22f;
			float rise = u * (s.verb == VB_RAISE ? 2.6f : 1.5f);
			V3 p = c + v3(cosf(ang) * rad, 0.25f + rise * ((i % 3) + 1) / 3.f, sinf(ang) * rad);
			fx(p, v3(0, 1, 0), 0.26f, s.verb == VB_RAISE ? CFX_PILLAR : CFX_HEAL, ang, 0.3f, s.verb == VB_RAISE ? 1.5f : 1.f, cr, cg, cb, 0.85f * fade);
		}
		if (s.verb == VB_RAISE) fx(dpos + v3(0, 1.4f, 0), v3(0, 1, 0), 0.4f, CFX_PILLAR, 0, 0.45f, 2.4f, cr, cg, cb, 0.85f * fade);
		return;
	}
	if (s.verb == VB_WARP) {
		float cut = s.rush * 0.42f;
		float fadeOut = Clampf(1.f - (t - cut) * 1.1f, 0.f, 1.f);
		if (t < cut + 0.2f) {
			float u = Clampf(t / max(0.05f, cut), 0.f, 1.f);
			for (int i = 0; i < 6; i++) {
				float ang = t * 4.f + i * (PI * 2.f / 6.f);
				V3 p = a + v3(cosf(ang) * (0.55f * (1.f - u)), 0.4f + u * 1.6f, sinf(ang) * (0.55f * (1.f - u)));
				fx(p, v3(0, 1, 0), 0.32f * (1.f - u * 0.4f), CFX_WARP, ang, 1.f, 1.f, cr, cg, cb, 0.85f);
			}
		}
		if (t >= cut) {
			for (int i = 0; i < 6; i++) {
				float ang = -t * 5.f + i * (PI * 2.f / 6.f);
				float grow = Clampf((t - cut) / 0.35f, 0.f, 1.f);
				V3 p = dpos + v3(cosf(ang) * grow * 0.6f, 0.35f + grow * 1.2f, sinf(ang) * grow * 0.6f);
				fx(p, v3(0, 1, 0), 0.28f + grow * 0.2f, CFX_WARP, ang, 1.f, 1.f, cr, cg, cb, fadeOut);
			}
		}
		if (once(30, cut)) punch(0.3f);
		return;
	}
	if (s.verb == VB_GALE || s.verb == VB_FAN || s.verb == VB_SPIN || s.verb == VB_NOVA || s.verb == VB_QUAKE || s.verb == VB_DRUM || s.verb == VB_DARK) {
		float gap = (s.verb == VB_NOVA || s.verb == VB_QUAKE || s.verb == VB_DRUM) ? 0.16f : 0.11f;
		float open = 0.20f;
		int iMax = (int)((landUntil - open) / gap) + 1;
		if (iMax < 4) iMax = 4;
		if (iMax > 64) iMax = 64;
		for (int i = 0; i < iMax; i++) {
			float hit = open + i * gap;
			if (hit > landUntil + 0.05f) break;
			float since = t - hit;
			if (since < -0.28f || since > 0.36f) continue;
			float u = Clampf((t - (hit - 0.28f)) / 0.28f, 0.f, 1.f);
			float fade = (since <= 0.f) ? 0.9f : Clampf(1.f - since / 0.36f, 0.f, 1.f);
			V3 from = a + v3(0, 1.0f, 0);
			V3 to = dpos + v3((U01(i, 5) - 0.5f) * 0.8f, 0.7f, (U01(i, 6) - 0.5f) * 0.8f);
			V3 p = Lerp(from, to, u);
			V3 dir = to - from;
			if (s.verb == VB_GALE) p = p + v3(sinf(u * PI + i) * s.side, 0, 0);
			if (s.verb == VB_FAN) {
				float yaw = (u - 0.5f) * 1.3f * s.side + (i - iMax * 0.5f) * 0.18f;
				p = a + v3(sinf(yaw) * (0.6f + u * 1.5f), 0.95f, cosf(yaw) * (0.6f + u * 1.5f));
			}
			if (s.verb == VB_SPIN) {
				float ang = u * PI * 2.f * (s.side >= 0.f ? 1.f : -1.f) + i * 0.7f;
				p = dpos + v3(sinf(ang) * (0.5f + u * 1.1f), 0.85f, cosf(ang) * (0.5f + u * 1.1f));
			}
			if (s.verb == VB_DARK) p.y += sinf(u * PI) * 0.4f;
			fx(p, dir, (s.verb == VB_NOVA || s.verb == VB_DRUM) ? 0.7f : 0.5f, kind, t * 3.f + i, u, 1.3f, cr, cg, cb, fade);
			if ((s.verb == VB_NOVA || s.verb == VB_DRUM || s.verb == VB_QUAKE) && since >= 0.f) {
				fx(dpos, v3(1, 0, 0), 0.25f + since * (s.verb == VB_NOVA ? 4.2f : 2.8f), s.verb == VB_QUAKE ? CFX_DUST : CFX_RING, 0, fade, 1.f, cr, cg, cb, fade);
				if (s.crack && (i % 2) == 0) {
					float ang = i * 0.8f;
					fx(dpos, v3(cosf(ang), 0, sinf(ang)), 0.85f, CFX_CRACK, ang, s.verb == VB_QUAKE ? 0.08f : 0.5f, 1.4f, cr, cg, cb, fade);
				}
			}
		}
		return;
	}
	{
		float open = 0.22f;
		float flight = (s.verb == VB_SHOT || s.verb == VB_THRUST) ? 0.34f : 0.20f;
		if (!multi) {
			float cover = firstHit - 0.16f;
			if (cover > flight) flight = cover;
		}
		float gap = flight + 0.02f;
		if (multi) {
			gap = (landUntil - open) / (float)(waves + 1);
			if (gap < 0.14f) gap = 0.14f;
			if (gap > 0.30f) gap = 0.30f;
		}
		int iMax = multi ? (int)((landUntil - open) / gap) + 1 : 1;
		if (iMax < 1) iMax = 1;
		if (iMax > 40) iMax = 40;
		for (int i = 0; i < iMax; i++) {
			float hit = multi ? (open + flight + i * gap) : firstHit;
			if (hit > landUntil + 0.12f) break;
			float spawn = hit - flight;
			V3 from = a + v3(0, 1.05f, 0);
			V3 to = dpos + v3((i - iMax * 0.5f) * 0.22f * s.side, 0.72f, 0);
			V3 dir = to - from;
			if (t >= spawn && t <= hit + 0.26f) {
				float u = Clampf((t - spawn) / flight, 0.f, 1.4f);
				float fade = (t <= hit) ? 0.95f : Clampf(1.f - (t - hit) / 0.26f, 0.f, 1.f);
				V3 p = Lerp(from, to, Clampf(u, 0.f, 1.f));
				if (s.verb == VB_BACK) p.y += sinf(Clampf(u, 0.f, 1.f) * PI) * 0.35f;
				fx(p, dir, (s.verb == VB_HEAVY) ? 0.78f : 0.55f, kind, t * 3.f + i, Clampf(u, 0.f, 1.f), (s.verb == VB_THRUST || s.verb == VB_SHOT) ? 1.8f : 1.25f, cr, cg, cb, fade);
				if (u > 0.18f && u < 1.05f) {
					V3 trail = Lerp(from, to, Clampf(u - 0.16f, 0.f, 1.f));
					fx(trail, dir, 0.32f, kind, t * 2.f, u, 1.f, cr, cg, cb, fade * 0.55f);
				}
			}
			float since = t - hit;
			if (since >= 0.f && since < 0.34f && (s.crack || s.verb == VB_HEAVY || s.verb == VB_QUAKE)) {
				float fade = 1.f - since / 0.34f;
				float ang = i * 0.9f + 0.4f;
				fx(dpos, v3(cosf(ang), 0, sinf(ang)), 0.8f, CFX_CRACK, ang, 0.45f, 1.4f, cr, cg, cb, fade);
			}
		}
	}
}

} // namespace otac
