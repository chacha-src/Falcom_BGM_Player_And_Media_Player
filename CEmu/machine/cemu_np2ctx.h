#pragma once
#include <stdint.h>

/* NP2 i286c + 2 MiB RAM はプロセス全体で 1 組。PC-98/PC-AT ハードは
   各自 RAM/CPU スナップショットを持ち、Bind でライブコアを入れ替える
   （クロスフェード中の 2 セッション用）。np2_step / np2_init / メモリ
   操作は CEmuNp2Lock 必須（再入可能）。 */

enum { CEMU_NP2_MEM_SIZE = 0x200000 };
enum { CEMU_NP2_CPU_SIZE = 1024 };

/* NP2 コアへの再入ロック */
void CEmuNp2Lock(void);
void CEmuNp2Unlock(void);
/* ライブ NP2 コアをこの owner へ切替。haveCpu=0 は RAM のみ（Init）。 */
void CEmuNp2Bind(void* owner, uint8_t* ram, void* cpu, int haveCpu);
void CEmuNp2Unbind(void* owner);
/* owner が現在のライブコアか */
int CEmuNp2IsOwner(const void* owner);
/* CPU スナップショットバイト数 */
int CEmuNp2CpuBytes(void);

/* スコープガード: 構築で Lock、破棄で Unlock */
struct CEmuNp2Guard {
	CEmuNp2Guard() { CEmuNp2Lock(); }
	~CEmuNp2Guard() { CEmuNp2Unlock(); }
	CEmuNp2Guard(const CEmuNp2Guard&) = delete;
	CEmuNp2Guard& operator=(const CEmuNp2Guard&) = delete;
};
