#pragma once
// おまけタクティクスゲーム（モードレス）

class CWnd;
struct tagMSG;

void OpenOmakeTacticsModeless(CWnd* parent);
void CloseOmakeTacticsIfOpen();
BOOL IsOmakeTacticsOpen();
BOOL IsOmakeTacticsActive();
BOOL OmakeTacticsPreTranslate(MSG* pMsg);
