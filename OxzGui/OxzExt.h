#pragma once

/* Extensions the tool can extract. .oxz stays the only type we create. */
static const wchar_t* kOxzExtractExts[] = {
	L".oxz", L".tgz", L".tbz", L".txz", L".bz2", L".cab", L".iso", L".wim", L".tar",
	L".zip", L".rar", L".7z", L".xz", L".gz",
	NULL
};
