#pragma once
#include <string>

bool OxzIsAdmin();
bool OxzIsInstalled();
std::wstring OxzProgramDir();
bool OxzInstallHere(std::wstring& err);
bool OxzRemoveInstall(std::wstring& err);
/* 管理者の確認を出し、終わってから戻る。キャンセルは false。 */
bool OxzRelaunchElevated(const wchar_t* args);
/* 言語を変えたあと、メニュー文言を書き直せるときだけ書く。 */
void OxzRefreshMenuText();
