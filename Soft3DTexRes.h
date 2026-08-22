#pragma once

// Load an EXE-embedded PNG (RCDATA) via WIC into BGRA8. Scales to dstW x dstH.
BOOL Soft3DTexLoadPngRes(int id, DWORD* dst, int dstW, int dstH);
