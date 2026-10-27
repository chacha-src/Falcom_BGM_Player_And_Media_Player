#pragma once
// おまけタクティクス: 表示用アクター（KayKit 人型＋手続きモンスター・髪・翼）

#include "OmakeTacticsRender.h"

namespace otac {

enum ClipId {
	CL_IDLE, CL_WALK, CL_RUN, CL_1H_CHOP, CL_1H_SLICE, CL_1H_STAB, CL_2H_CHOP, CL_2H_SPIN, CL_1H_SHOOT, CL_2H_SHOOT,
	CL_CAST_SHOOT, CL_CAST_RAISE, CL_CAST_LONG, CL_HIT_A, CL_HIT_B, CL_DEATH, CL_CHEER, CL_BLOCK, CL_DODGE, CL_2H_IDLE,
	CL_THROW, CL_PUNCH, CL_JUMP, CL_INTERACT, CL_TAUNT, CL_SPAWN, CL_USE,
	CL_IDLE_GLANCE_L, CL_IDLE_GLANCE_R, CL_IDLE_LOOK, CL_IDLE_STRETCH, CL_IDLE_SHIFT, CL_IDLE_CROSS,
	CL_COUNT
};

struct Library {
	int bodyModel[B_COUNT];
	int skelModel[4];
	int procModel[5];
	int clipMap[4][CL_COUNT];
	GpuMesh hair[B_COUNT][HAIR_STYLES];
	GpuMesh humHair[HUM_HAIR_N][HAIR_STYLES];
	Buf<VtxS> humHairV[HUM_HAIR_N][HAIR_STYLES]; // 頭の骨の空間。αが小さいほど根元から遠い
	V3 humHairPivot[HUM_HAIR_N][HAIR_STYLES];            // 垂れる髪の付け根（後頭部の生え際）
	int humHairModel[HUM_HAIR_N];
	int humHairN;
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
	int gearM[4], gearJ[4], nGear;
	int wepAtt, wepGear;  // att/gear のうち先頭から武器の数（アバター時はこれだけ描く）
	int hair;
	DWORD hairCol;
	int wings;
	int female;
	int face;           // 顔の型
	int avatar;         // アバター (0=なし)
	int bust;           // 胸の大きさ 0=A〜7=H
	float htScale;      // 身長。体は CS、武具と髪は同じ倍率
	DWORD avaTint;      // 0 は服の染め色のまま
	float scale, hue;
	DWORD tint;
	float height;       // 頭上（ワールド）
	float hover;        // 浮遊高さ
	float flap;         // 1 で移動中のはばたき。0 は待機の羽ばたき
	int gait;           // Gait。移動の足取り
	PoseMods mods;
	int clip, prev;
	float t, pt, blend, blendDur;
	int loop, ploop;
	float animSpeed;
	float idleOfs;      // 待機の開始位置（秒）。全員が同じ拍で動かないようにする
	float idleSeed;     // 次の待機の種類を決める
	float recoil;       // 被ダメの仰け反り 0..1
	float recoilX, recoilZ;
	int skillPose;      // 再生中のスキル。人体はスキル別の型を引く。0 未満は通常攻撃
	int prevSkill;      // 混ぜ元のクリップを始めたときのスキル
	int grip[2];        // [右/左] 持ち物の指の型 GRIP_*。GRIP_NONE の手はクリップの指のまま
	unsigned long long gripMask[2];   // その手の指の骨（ビット = 関節番号）
	int gripClip;       // 人体の Grip クリップ。無ければ -1
	Pose pose;
	int poseValid;
	const void* modelPtr;   // ActorPose 時に設定
};

void ActorFromChar(Actor& a, Library& lib, Renderer& r, const CharData& c);
void ActorFromMonster(Actor& a, const Library& lib, const Renderer& r, int monster, int boss);
void ActorPlay(Actor& a, int clip, int loop, float blendTime = .15f);
// kind: 0待機 1歩き 2走り 3構え 4攻撃 5別の攻撃。飛行の歩きと走りは翼を大きく動かす。
// giveWings は人型だけ。飛行メッシュに翼を足すと体が二重になる。restart は同じループを頭からやり直す。
void ActorPreview(Actor& a, const Library& lib, const Renderer& r, int kind, int style, int idleClip, BOOL fly, BOOL giveWings, BOOL restart = FALSE);
void ActorUpdate(Actor& a, float dt);
float ActorClipLen(const Actor& a, const Library& lib, const Renderer& r, int clip);
// 構えで止める割合。人体のスキル別の型はその型の構えの時刻。
float ActorWindup(const Actor& a, const Renderer& r, int clip);
BOOL ActorClipDone(const Actor& a, const Library& lib, const Renderer& r);
void ActorPose(Actor& a, const Library& lib, const Renderer& r);
void ActorDraw(Renderer& r, const Library& lib, Actor& a, const M4& world, const DrawParams& dp, float time);
M4 ActorWorld(const Actor& a, V3 pos, float face);
V3 ActorJointWorld(const Actor& a, const M4& world, int which);   // 0=頭 1=右手 2=胸

int AttackClipFor(int style, int variant);
int CastClipFor(int skillKind);

} // namespace otac
