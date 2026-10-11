#pragma once

/* mp* で最長トラックに J があるとき、保存ダイアログの下にループ回数を付ける。
   askLoops が 0 のときは普通の保存ダイアログ。
   戻り 1 で path に保存先。*loops は 1〜32（askLoops が 0 のときは 1）。 */
int SasamiMidiPickSave(HWND owner, wchar_t* path, int pathChars, int askLoops, int* loops);
