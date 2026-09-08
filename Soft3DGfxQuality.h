#pragma once

enum { S3_GFX_QUALITY_MAX = 9 }; /* UI shows 1..10 */

struct S3GfxQualityParams {
	int shadowSize;       /* 0 = off */
	int ssr;              /* 0 off, 1 half-ish/weak, 2 on */
	int dof;              /* 0 off, 1 weak, 2 on */
	int aniso;            /* 4 / 8 / 16 */
	float casStrength;    /* 0..1-ish, fed to FIN via Dof.w */
	int rearW, rearH;     /* 0 = off (Race) */
	int reflectSize;      /* 0 = off (Race) */
	int mirrorSlots;      /* Maze mirror RT count to use */
	int mirrorSize;       /* Maze mirror RT size */
};

const S3GfxQualityParams& S3GfxQualityGet(int level0to9);
int S3GfxQualityClamp(int level);
int S3GfxQualitySuggestFromGpu(); /* probes GPU; returns 0..9 */
void S3GfxQualityEnsureAutoDefault(); /* applies suggest while s3_gfx_auto */
void S3GfxQualityFillCombo(CComboBox& cb);
LPCTSTR S3GfxQualityLabel();
