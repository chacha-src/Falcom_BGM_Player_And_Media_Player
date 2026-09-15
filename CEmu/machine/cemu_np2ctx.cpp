#include "StdAfx.h"
#include "cemu_np2ctx.h"
#include "../vendor/np2/np2ffi.h"
#include <string.h>

static CRITICAL_SECTION s_np2Cs;
static volatile LONG s_np2CsReady;
static int s_np2Depth;
static void* s_owner;
static uint8_t* s_ram;
static void* s_cpu;

/* CRITICAL_SECTION を一度だけ初期化する */
static void Np2EnsureCs(void)
{
	const LONG was = InterlockedCompareExchange(&s_np2CsReady, 1, 0);
	if (was == 0) {
		InitializeCriticalSection(&s_np2Cs);
		InterlockedExchange(&s_np2CsReady, 2);
		return;
	}
	while (InterlockedCompareExchange(&s_np2CsReady, 2, 2) != 2)
		Sleep(0);
}

/* ライブ NP2 RAM/CPU へスナップショットを書き戻す */
static void Np2Install(uint8_t* ram, void* cpu, int haveCpu)
{
	uint8_t* live = np2_mem();
	if (live && ram && live != ram)
		memcpy(live, ram, CEMU_NP2_MEM_SIZE);
	if (haveCpu && cpu)
		np2_load_cpu(cpu, CEMU_NP2_CPU_SIZE);
}

/* ライブコアを現 owner のスナップショットへ退避する */
static void Np2SnapOwner(void)
{
	uint8_t* live = np2_mem();
	if (s_ram && live && s_ram != live)
		memcpy(s_ram, live, CEMU_NP2_MEM_SIZE);
	if (s_cpu)
		np2_save_cpu(s_cpu, CEMU_NP2_CPU_SIZE);
}

/* CPU スナップショットのバイト数 */
int CEmuNp2CpuBytes(void)
{
	return CEMU_NP2_CPU_SIZE;
}

/* owner がライブコアか */
int CEmuNp2IsOwner(const void* owner)
{
	return (owner && s_owner == owner) ? 1 : 0;
}

/* NP2 コアをロック（再入可） */
void CEmuNp2Lock(void)
{
	Np2EnsureCs();
	EnterCriticalSection(&s_np2Cs);
	s_np2Depth++;
}

/* NP2 コアをアンロック */
void CEmuNp2Unlock(void)
{
	if (s_np2Depth <= 0)
		return;
	s_np2Depth--;
	LeaveCriticalSection(&s_np2Cs);
}

/* ライブ NP2 コアをこの owner へ切替 */
void CEmuNp2Bind(void* owner, uint8_t* ram, void* cpu, int haveCpu)
{
	if (!owner || !ram)
		return;
	if (s_owner == owner) {
		s_ram = ram;
		s_cpu = cpu;
		return;
	}
	if (s_owner)
		Np2SnapOwner();
	/* 初回 bind（前 owner なし・haveCpu=0）は Init/BootDos 用にライブ RAM を残す。
	   既存セッションからの切替時だけスナップショットを入れる。 */
	if (s_owner || haveCpu)
		Np2Install(ram, cpu, haveCpu);
	s_owner = owner;
	s_ram = ram;
	s_cpu = cpu;
}

/* owner のライブコアを解放しスナップショットへ退避する */
void CEmuNp2Unbind(void* owner)
{
	if (!owner || s_owner != owner)
		return;
	Np2SnapOwner();
	s_owner = NULL;
	s_ram = NULL;
	s_cpu = NULL;
}
