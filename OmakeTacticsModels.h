#pragma once
// おまけタクティクス: 表示用アクター（KayKit 人型＋手続きモンスター・髪・翼）

#include "OmakeTacticsRender.h"

namespace otac {

enum ClipId {
	CL_IDLE, CL_WALK, CL_RUN, CL_1H_CHOP, CL_1H_SLICE, CL_1H_STAB, CL_2H_CHOP, CL_2H_SPIN, CL_1H_SHOOT, CL_2H_SHOOT,
	CL_CAST_SHOOT, CL_CAST_RAISE, CL_CAST_LONG, CL_HIT_A, CL_HIT_B, CL_DEATH, CL_CHEER, CL_BLOCK, CL_DODGE, CL_2H_IDLE,
	CL_THROW, CL_PUNCH, CL_JUMP, CL_INTERACT, CL_TAUNT, CL_SPAWN, CL_USE, CL_COUNT
};

struct Library {
	int bodyModel[B_COUNT];
	int skelModel[4];
	int procModel[5];
	int clipMap[4][CL_COUNT];
	GpuMesh hair[B_COUNT][HAIR_STYLES];
	GpuMesh wing[2][2];     // [羽根/竜][左/右]
	GpuMesh crown;
	GpuMesh ring;
	GpuMesh orb;
	int ready;
};

BOOL BuildLibrary(Renderer& r, Library& lib);
void ReleaseLibrary(Library& lib);
DWORD HairColor(int i);
float OutfitHue(int i);

struct Actor {
	int proc;           // 0=KayKit 1=手続き
	int model;
	int rig;
	int look;
	int body;
	int att[6], attJ[6], nAtt;
	int hair;
	DWORD hairCol;
	int wings;
	int female;
	float scale, hue;
	DWORD tint;
	float height;       // 頭上（ワールド）
	float hover;        // 浮遊高さ
	PoseMods mods;
	int clip, prev;
	float t, pt, blend, blendDur;
	int loop, ploop;
	float animSpeed;
	Pose pose;
	int poseValid;
	const void* modelPtr;   // ActorPose 時に設定
};

void ActorFromChar(Actor& a, const Library& lib, const Renderer& r, const CharData& c);
void ActorFromMonster(Actor& a, const Library& lib, const Renderer& r, int monster, int boss);
void ActorPlay(Actor& a, int clip, int loop, float blendTime = .15f);
void ActorUpdate(Actor& a, float dt);
float ActorClipLen(const Actor& a, const Library& lib, const Renderer& r, int clip);
BOOL ActorClipDone(const Actor& a, const Library& lib, const Renderer& r);
void ActorPose(Actor& a, const Library& lib, const Renderer& r);
void ActorDraw(Renderer& r, const Library& lib, Actor& a, const M4& world, const DrawParams& dp, float time);
M4 ActorWorld(const Actor& a, V3 pos, float face);
V3 ActorJointWorld(const Actor& a, const M4& world, int which);   // 0=頭 1=右手 2=胸

int AttackClipFor(int style, int variant);
int CastClipFor(int skillKind);

} // namespace otac
