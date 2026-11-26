#pragma once

/* Extensions the tool can extract. New archives are oxz, zip, or a tar wrapper. */
static const wchar_t* kOxzExtractExts[] = {
	L".oxz", L".tar.lz4", L".tar.gz", L".tar.bz2", L".tar.xz", L".tar.zst",
	L".tgz", L".tbz2", L".tbz", L".txz", L".tzst", L".tlz4", L".lz4", L".zst",
	L".bz2", L".cab", L".iso", L".wim", L".tar",
	L".zip", L".rar", L".7z", L".xz", L".gz",
	NULL
};
