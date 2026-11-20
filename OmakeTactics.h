#pragma once
// おまけタクティクスゲーム（モードレス）

class CWnd;
struct tagMSG;

void OpenOmakeTacticsModeless(CWnd* parent);
void CloseOmakeTacticsIfOpen();
BOOL IsOmakeTacticsOpen();
BOOL IsOmakeTacticsActive();
BOOL OmakeTacticsPreTranslate(MSG* pMsg);
// 本体ダイアログの WM_KICKIDLE から 1 枚。続ければ TRUE。
BOOL OmakeTacticsIdleFrame();
