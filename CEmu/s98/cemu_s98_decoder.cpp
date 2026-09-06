#include <windows.h>
#include <wchar.h>
#include "../cemu_kpi_min.h"
#include "device/s98device.h"
#include "../fmmon/fmmon_shadow.h"

#define USE_ZLIB 1

#if USE_ZLIB
#include "zlib.h"
#endif

#define SUPPORT_VGM 0
#define SUPPORT_MYM 0

#define MASTER_CLOCK (7987200)
#define LOOPNUM 2
#define SAMPLE_RATE 44100
#define SYNC_RATE 60 /* (Hz) */
#define UNIT_RENDER (SAMPLE_RATE/SYNC_RATE)
#define FADEOUT_TIME 0/*(10 * SYNC_RATE)*/

#define S98DEVICE_MAX 16

/* S98 file header */
#define S98_MAGIC_V0	(0x53393830)	/* 'S980' */
#define S98_MAGIC_V1	(0x53393831)	/* 'S981' */
#define S98_MAGIC_V2	(0x53393832)	/* 'S982' */
#define S98_MAGIC_V3	(0x53393833)	/* 'S983' */
#define S98_MAGIC_VZ	(0x5339385A)	/* 'S98Z' */
#define S98_OFS_MAGIC		(0x00)
#define S98_OFS_TIMER_INFO1	(0x04)
#define S98_OFS_TIMER_INFO2	(0x08)
#define S98_OFS_COMPRESSING	(0x0C)
#define S98_OFS_OFSTITLE	(0x10)
#define S98_OFS_OFSDATA		(0x14)
#define S98_OFS_OFSLOOP		(0x18)
#define S98_OFS_OFSCOMP		(0x1C)
#define S98_OFS_DEVICECOUNT	(0x1C)
#define S98_OFS_DEVICEINFO	(0x20)

#define VGM_MAGIC		(0x56676D20)	/* 'Vgm ' */

S98DEVICEIF *S98DeviceCreate(int type, int clock, int rate)
{
	S98DEVICEIF *ret = 0;
	switch (type)
	{
		case S98DEVICETYPE_PSG_YM:
			ret = CreateS98DevicePSG(true);
			break;
		case S98DEVICETYPE_PSG_AY:
			ret = CreateS98DevicePSG(false);
			break;
		case S98DEVICETYPE_OPN:
			ret = CreateS98DeviceOPN();
			break;
		case S98DEVICETYPE_OPN2:
			ret = CreateS98DeviceOPN2();
			break;
		case S98DEVICETYPE_OPNA:
			ret = CreateS98DeviceOPNA();
			break;
		case S98DEVICETYPE_OPM:
			ret = CreateS98DeviceOPM();
			break;
		case S98DEVICETYPE_OPLL:
			ret = CreateS98DeviceOPLL();
			break;
		case S98DEVICETYPE_SNG:
			ret = CreateS98DeviceSNG();
			break;
		case S98DEVICETYPE_OPL:
			ret = CreateS98DeviceOPL();
			break;
		case S98DEVICETYPE_OPL2:
			ret = CreateS98DeviceOPL2();
			break;
		case S98DEVICETYPE_OPL3:
			ret = CreateS98DeviceOPL3();
			break;
	}
	if (ret) ret->Init(clock, rate);
	return ret;
}

class s98File {
public:
	bool OpenFromBuffer(const BYTE *Buffer, DWORD dwSize, CEMU_S98_MEDIAINFO *pInfo);
	void Close(void);
	DWORD SetPosition(DWORD dwpos);
	DWORD Write(double *Buffer, DWORD numSample);
	s98File();
	~s98File();
protected:
	int number_of_devices;
	S98DEVICEIF *devices[S98DEVICE_MAX];
	BYTE devicemap[0x40];

	BYTE *s98data;
	BYTE *s98head;
	BYTE *s98top;
    BYTE *s98tail;//add by kobarin
	BYTE *s98loop;
	int length;
	DWORD playtime; /* syncs */
	DWORD looptime; /* syncs */

	BYTE *s98cur;
	DWORD curtime;

#define SPS_SHIFT 28
#define SPS_LIMIT (1 << SPS_SHIFT)
	enum { SAMPLE_PER_SYNC, SYNC_PER_SAMPLE } spsmode;
	DWORD sps;		/* sync/sample or sample/syjnc */
	DWORD timerinfo1;
	DWORD timerinfo2;

	double sync_per_sec;

	DWORD lefthi;
	DWORD leftlo;

	Sample bufdev[UNIT_RENDER * 2];

	void CalcTime(void);
	void Step(void);
	void Reset(void);

	void WriteSub(double *Buffer, DWORD numSample);
	DWORD SyncToMsec(DWORD sync);
	DWORD MsecToSync(DWORD ms);
};

static void SetDwordLE(Uint8 *p, Uint32 v)
{
	p[0] = (v >> (8 * 0)) & 0xFF;
	p[1] = (v >> (8 * 1)) & 0xFF;
	p[2] = (v >> (8 * 2)) & 0xFF;
	p[3] = (v >> (8 * 3)) & 0xFF;
}
static void SetDwordBE(Uint8 *p, Uint32 v)
{
	p[0] = (v >> (8 * 3)) & 0xFF;
	p[1] = (v >> (8 * 2)) & 0xFF;
	p[2] = (v >> (8 * 1)) & 0xFF;
	p[3] = (v >> (8 * 0)) & 0xFF;
}
static Uint32 GetWordLE(Uint8 *p)
{
	int ret;
	ret  = ((Uint32)(Uint8)p[0]) << 0x00;
	ret |= ((Uint32)(Uint8)p[1]) << 0x08;
	return ret;
}
static Uint32 GetDwordLE(Uint8 *p)
{
	int ret;
	ret  = ((Uint32)(Uint8)p[0]) << 0x00;
	ret |= ((Uint32)(Uint8)p[1]) << 0x08;
	ret |= ((Uint32)(Uint8)p[2]) << 0x10;
	ret |= ((Uint32)(Uint8)p[3]) << 0x18;
	return ret;
}
static Uint32 GetDwordBE(Uint8 *p)
{
	Uint32 ret;
	ret  = ((Uint32)(Uint8)p[0]) << 0x18;
	ret |= ((Uint32)(Uint8)p[1]) << 0x10;
	ret |= ((Uint32)(Uint8)p[2]) << 0x08;
	ret |= ((Uint32)(Uint8)p[3]) << 0x00;
	return ret;
}


s98File::s98File()
{
	s98data = 0;
	number_of_devices = 0;
	memset(devices, 0, sizeof(devices));
}
s98File::~s98File()
{
	Close();
}

void s98File::CalcTime(void)
{
	BYTE *p = s98top;
	looptime = 0;
	playtime = 0;
	if (!s98data) return;
	while (1)
	{
		if (p == s98loop) looptime = playtime;
		if(p >= s98tail){
            int t = 0;
            return;
        }
        if (*p < 0x80)
		{
			p += 3;
		}
		else if (*p == 0xff)
		{
			playtime += 1;
			p += 1;
		}
		else if (*p == 0xfe)
		{
			int s = 0, n = 0;
			do
			{
				n |= (*(++p) & 0x7f) << s;
				s += 7;
			}
			while (*p & 0x80);
			playtime += n + 2;
			p += 1;
		}
		else
		{
			return;
		}
	}
}

static Uint32 DivFix(Uint32 p1, Uint32 p2, Uint32 fix)
{
	Uint32 ret;
	ret = p1 / p2;
	p1  = p1 % p2;/* p1 = p1 - p2 * ret; */
	while (fix--)
	{
		p1 += p1;
		ret += ret;
		if (p1 >= p2)
		{
			p1 -= p2;
			ret++;
		}
	}
	return ret;
}

void s98File::Reset(void)
{
	for (int d = 0; d < number_of_devices; d++)
		if (devices[d]) devices[d]->Reset();

#if FADEOUT_TIME
	loopcur = 0;
	fader = 0;
#endif

	s98cur = s98top;
	curtime = 0;

	lefthi = 0;
	leftlo = 0;
	Step();
}

void s98File::Step(void)
{
#if FADEOUT_TIME
	if (fader && ++fader >= FADEOUT_TIME)
	{
		lefthi = 0;
		for (int d = 0; d < number_of_devices; d++) if (devices[d]) devices[d]->Disable();
		return;
	}
#endif
	//while (1)
    while(s98cur < s98tail)
	{
		if (*s98cur < 0x80)
		{
			int d = devicemap[*s98cur >> 1];
			if (d != S98DEVICE_MAX && devices[d])
			{
				if (*s98cur & 1)
					devices[d]->SetReg(0x100 | s98cur[1], s98cur[2]);
				else
					devices[d]->SetReg(s98cur[1], s98cur[2]);
			}
			s98cur += 3;
			continue;
		}
		if (*s98cur == 0xfe || *s98cur == 0xff) break;
		if (*s98cur == 0xfd && s98loop)
		{
			s98cur = s98loop;
#if FADEOUT_TIME
			if (loopnum && !fader &&  ++loopcur == loopnum) fader = 1;
#endif
			continue;
		}
		lefthi = 0;
		for (int d = 0; d < number_of_devices; d++) if (devices[d]) devices[d]->Disable();
		return;
	}
	//while (1)
    while(s98cur < s98tail)
	{
		if (*s98cur == 0xff)
		{
			lefthi += 1;
			s98cur++;
		}
		else if (*s98cur == 0xfe)
		{
			int s = 0, n = 0;
			do
			{
				n |= (*(++s98cur) & 0x7f) << s;
				s += 7;
			} while (*s98cur & 0x80);
			lefthi += n + 2;
			s98cur++;
		}
		else
		{
			break;
		}
	}
	return;
}

DWORD s98File::SyncToMsec(DWORD sync)
{
	return (DWORD)(((double)sync) * ((double)1000) / sync_per_sec);
}

DWORD s98File::MsecToSync(DWORD ms)
{
	return (DWORD)(((double)ms) * sync_per_sec / ((double)1000));
}

DWORD s98File::SetPosition(DWORD dwpos_)
{
	if (!s98data) return 0;
	dwpos_ = MsecToSync(dwpos_);
/*
	char buf[1024];
	wsprintf(buf, "s98debug:%d:%d\n", dwpos_, curtime);
	OutputDebugString(buf);
*/
	if (dwpos_ < curtime)
	{
		Reset();
	}
	while (dwpos_ > curtime)
	{
		curtime++;
		if (lefthi && --lefthi == 0) Step();
	}
	return SyncToMsec(curtime);
}

#if SUPPORT_VGM
#include "vgm.h"
#endif
#if SUPPORT_MYM
#include "x1f.h"
#include "mym.h"
#endif

static DWORD GetTarOcts(BYTE *p, DWORD l)
{
	DWORD i, r;
	for (i = 0; i < l && p[i] == 0x20; i++);
	for (r = 0; i < l && '0' <= p[i] && p[i] <= '7'; i++) {
		r *= 8;
		r += p[i] - '0';
	}
	return r;
}

static DWORD IsTarHeader(BYTE *p)
{
	DWORD i, sum1, sum2;
	if (GetDwordBE(p + 0x101) != 0x75737461u || p[0x105] != 0x72) return 0;
	sum1 = GetTarOcts(p + 0x94, 8);
	sum2 = 0;
	for (i = 0; i < 512; i++) sum2 += (0x94 <= i && i < 0x9C) ? 0x20 : p[i];
	if ((sum1 & 0xFFFF) != (sum2 & 0xFFFF)) return 0;
	if (p[i] != 0x9C) return 512;
	sum1 = 512 + ((GetTarOcts(p + 0x7c, 12) + 511) & (~511));
	sum2 = IsTarHeader(p + sum1);
	return sum1 ? (sum1 + sum2) : 0;
}

static const int default_sample_rate = SAMPLE_RATE;

bool s98File::OpenFromBuffer(const BYTE *Buffer, DWORD dwSize, CEMU_S98_MEDIAINFO *pInfo)
{
	int sample_rate = (pInfo->dwSampleRate == 0) ? default_sample_rate : pInfo->dwSampleRate;

	Close();

	DWORD dataofs, loopofs;
	BYTE *buf = 0;
	DWORD length = dwSize;
	DWORD magic = 0;

	do
	{
		if (length < 0x40) break;
		buf = (BYTE *)malloc(length);
		if (!buf) break;
		XMEMCPY(buf, Buffer, length);
#if USE_ZLIB
		/* Uncompress GZIP */
		if (buf[0] == 0x1f && buf[1] == 0x8b)
		{
			BYTE *des = 0;
			BYTE *gzp;
			unsigned deslen = 4096;
			int z_err;
			z_stream zs;

			des = (BYTE *)malloc(deslen);
			if (!des) break;

			XMEMSET(&zs, 0, sizeof(z_stream));

			gzp = buf + 10;
			if (buf[3] & 4)
			{
				DWORD extra = *gzp++;
				extra += *gzp++ << 8;
				gzp += extra;
			}
			if (buf[3] & 8) while (*gzp++);
			if (buf[3] & 16) while (*gzp++);
			if (buf[3] & 2) gzp += 2;

			zs.next_in = gzp;
			zs.avail_in = length - (gzp - buf);
			zs.next_out = des;
			zs.avail_out = deslen;
			zs.zalloc = (alloc_func)0;
			zs.zfree = (free_func)0;
			zs.opaque = (voidpf)0;

			z_err = inflateInit2(&zs, -MAX_WBITS);
			if (z_err != Z_OK)
			{
				inflateEnd(&zs);
				break;
			}
			inflateReset(&zs);
			while (1)
			{
				z_err = inflate(&zs, Z_SYNC_FLUSH);
				if (z_err == Z_STREAM_END) break;
				if (z_err != Z_OK || zs.avail_in == 0)
				{
					free(des);
					des = 0;
					break;
				}
				if (zs.avail_out == 0)
				{
					BYTE *p;
					p = (BYTE *)realloc(des, deslen + 4096);
					if (!p)
					{
						free(des);
						des = 0;
						break;
					}
					des = p;
					zs.next_out = des + deslen;
					zs.avail_out += 4096;
					deslen += 4096;
				}
			};
			if (des)
			{
				free(buf);
				buf = des;
				length = zs.total_out;
			}
			inflateEnd(&zs);
			if (!des) break;
		}
#endif
		/* skip TAR header */
		s98head = buf;
		if (length >= 512)
		{
			DWORD lentarheader = IsTarHeader(s98head);
			s98head += lentarheader;
			length -= lentarheader;
		}
		if (length < 0x40) break;
		magic = GetDwordBE(s98head + S98_OFS_MAGIC);
		if (S98_MAGIC_V0 <= magic && magic <= S98_MAGIC_VZ)
		{
			/* version check */
			if (S98_MAGIC_V3 < magic) break;
		}
#if SUPPORT_VGM
		else if (VGM_MAGIC == magic)
		{
			DWORD cnvs98length;
			BYTE *cnvs98;
			cnvs98 = vgm2s98(s98head, length, &cnvs98length);
			if (!cnvs98) break;
			free(buf);
			s98head = buf = cnvs98;
			length = cnvs98length;
		}
#endif
		else
		{
#if SUPPORT_MYM
			DWORD cnvs98length;
			BYTE *cnvs98;
			cnvs98 = mym2s98(s98head, length, &cnvs98length);
			if (!cnvs98) break;
			free(buf);
			s98head = buf = cnvs98;
			length = cnvs98length;
#else
			break;
#endif
		}
		loopofs = GetDwordLE(s98head + S98_OFS_OFSLOOP);
		dataofs = GetDwordLE(s98head + S98_OFS_OFSDATA);
		/* Uncompress internal deflate(old gimmick) */
		if (GetDwordLE(s98head + S98_OFS_COMPRESSING))
		{
#if !USE_ZLIB
			break;	/* NOT SUPPORT */
#else
			uLongf dessize;
			BYTE *des;
			DWORD compofs;
			if (S98_MAGIC_V2 == magic)
			{
				compofs = GetDwordLE(s98head + S98_OFS_OFSCOMP);
			}
			if (!compofs) compofs = dataofs;
			des = (BYTE *)malloc(compofs + GetDwordLE(s98head + S98_OFS_COMPRESSING));
			if (!des) break;
			XMEMCPY(des, s98head, compofs);
			if (Z_OK != uncompress(&des[compofs], &dessize, s98head + compofs, length - compofs))
			{
				free(des);
				break;
			}
			length = dessize;
			s98head = des;
			s98data = des;
			s98top  = s98head + dataofs;
			s98tail = des + compofs + dessize;
            s98loop = loopofs ? (s98head + loopofs) : 0;
			des = 0;
#endif
		}
		else
		{
			s98data = buf;
			s98top  = s98head + dataofs;
			s98tail = buf + length;
			s98loop = loopofs ? (s98head + loopofs) : 0;
			buf = 0;
		}
		/* if (length <= loopofs) s98loop = 0; */
	} while(0);
	if (buf) free(buf);

	if (!s98data) return false;

	timerinfo1 = GetDwordLE(s98head + S98_OFS_TIMER_INFO1);
	timerinfo2 = GetDwordLE(s98head + S98_OFS_TIMER_INFO2);
	if (timerinfo1 == 0) timerinfo1 = 10;
	if (timerinfo2 == 0) timerinfo2 = 1000;

	int d;
	for (d = 0; d < 0x40; d++) devicemap[d] = S98DEVICE_MAX;
	number_of_devices = 0;
	if ((S98_MAGIC_V3 != magic && !GetDwordLE(s98head + S98_OFS_DEVICEINFO)) ||
		(S98_MAGIC_V3 == magic && !GetDwordLE(s98head + S98_OFS_DEVICECOUNT)))
	{
		devices[0] = S98DeviceCreate(S98DEVICETYPE_OPNA, MASTER_CLOCK, sample_rate);
		if (devices[0])
		{
			devicemap[0] = 0;
			number_of_devices = 1;
		}
	}
	else
	{
		int devicemax = S98DEVICE_MAX;
		BYTE *devinfo = s98head + S98_OFS_DEVICEINFO;
		if (S98_MAGIC_V3 == magic && GetDwordLE(s98head + S98_OFS_DEVICECOUNT))
		{
			devicemax = GetDwordLE(s98head + S98_OFS_DEVICECOUNT);
		}
		for (d = 0; d < devicemax; d++)
//		for (d = 0; d < devicemax && GetDwordLE(devinfo); d++)
		{
			devices[number_of_devices] = S98DeviceCreate(GetDwordLE(devinfo), GetDwordLE(devinfo + 4), sample_rate);
			if (devices[number_of_devices])
			{
				devicemap[d] = number_of_devices++;
				devices[d]->SetPan(GetDwordLE(devinfo + 8));
			} else {
				number_of_devices++;
			}
			devinfo += 16;
		}
	}

	double dsps;
	double sample_per_sec = (double)sample_rate;
	sync_per_sec = ((double)timerinfo2) / ((double)timerinfo1);
	if (sync_per_sec > sample_per_sec)
	{
		dsps = sample_per_sec / sync_per_sec;
		spsmode = SAMPLE_PER_SYNC;
	}
	else
	{
		dsps = sync_per_sec / sample_per_sec;
		spsmode = SYNC_PER_SAMPLE;
	}
	sps = (DWORD)(dsps * (double)SPS_LIMIT);
	if (sps >= SPS_LIMIT)
	{
		sps = SPS_LIMIT;
		spsmode = SYNC_PER_SAMPLE;
	}
	/* »_ÅÍðxS98Í¶ÝµÈ¢ */
	if (spsmode == SAMPLE_PER_SYNC) return false;

	CalcTime();

	pInfo->dwSampleRate = sample_rate;
	pInfo->dwChannels = 2;
	pInfo->nBitsPerSample = -64;
	pInfo->dwSeekableFlags = CEMU_S98_MEDIAINFO::SEEK_FLAGS_SAMPLE;
	pInfo->dwUnitSample = 0;//UNIT_RENDER;// * 4;

	if (s98loop)
	{
		//pInfo->dwLength = playtime + (playtime - looptime) * (loopnum - 1) + FADEOUT_TIME;

        pInfo->qwLength = SyncToMsec(playtime) * 10000ui64;
    	pInfo->qwLoop = SyncToMsec(playtime-looptime) * 10000ui64;
        pInfo->qwFadeOut = -1;
	}
	else
	{
        pInfo->qwLength = SyncToMsec(playtime) * 10000ui64;
    	pInfo->qwLoop = 0;
	}
	/* syncs to msec */
	//pInfo->dwLength = SyncToMsec(pInfo->dwLength);
    pInfo->dwCount = 1;
	Reset();
	return true;
}

void s98File::Close(void)
{
	if (s98data) { free(s98data); s98data = 0; }
	for (int d = 0; d < number_of_devices; d++) if (devices[d]) delete devices[d];
	number_of_devices = 0;
}

void s98File::WriteSub(double *Buffer, DWORD numSample)
{
    const double k = 1.0 / 0x8000;
	DWORD i, len;
	while (numSample)
	{
		len = (numSample > UNIT_RENDER) ? UNIT_RENDER : numSample;
		XMEMSET(bufdev, 0, len * 2 * sizeof(Sample));
		for (int d = 0; d < number_of_devices; d++)
			if (devices[d]) devices[d]->Mix(bufdev, len);
		for (i = 0; i < len * 2; i++)
		{
			Sample s;
			s = bufdev[i];
            *Buffer++ = (double)s * k;
		}
		numSample -= len;
	}
}

DWORD s98File::Write(double *Buffer, DWORD numSample)
{
	DWORD pos, numWrite = 0;
	if (!s98data) return numWrite;
	if (numSample == 0) return numWrite;

	for (pos = 0; pos < numSample; pos++)
	{
		if (lefthi)
		{
			leftlo += sps;
			if (leftlo >= SPS_LIMIT)
			{
				leftlo -= SPS_LIMIT;
				lefthi -= 1;
				curtime += 1;
			}
			if (lefthi == 0)
			{
				if (Buffer) WriteSub(&Buffer[numWrite * 2], pos + 1 - numWrite);
				numWrite = pos + 1;
				Step();
				if (lefthi == 0) break;
			}
		}
	}
	if (/*lefthi && */numWrite != numSample)
	{
		if (Buffer) WriteSub(&Buffer[numWrite * 2], numSample - numWrite);
		numWrite = numSample;
	}
	return numWrite;
}

#include "../cemu_s98.h"

void CEmuS98Init(CEmuS98Player* p)
{
	if (!p) return;
	memset(p, 0, sizeof(*p));
}

void CEmuS98Close(CEmuS98Player* p)
{
	if (!p || !p->impl) return;
	s98File* f = (s98File*)p->impl;
	delete f;
	p->impl = NULL;
	p->open = 0;
}

int CEmuS98OpenBuffer(CEmuS98Player* p, const BYTE* buf, DWORD size, DWORD sampleRate, const wchar_t* srcPath)
{
	if (!p || !buf || !size) return 0;
	CEmuS98Close(p);
	s98File* f = new s98File();
	CEMU_S98_MEDIAINFO mi;
	CEmuS98InitMediaInfo(&mi);
	if (!sampleRate) sampleRate = 44100;
	mi.dwSampleRate = sampleRate;
	if (!f->OpenFromBuffer(buf, size, &mi)) {
		delete f;
		return 0;
	}
	p->impl = f;
	p->sampleRate = mi.dwSampleRate;
	p->curSample = 0;
	if (mi.qwLoop == 0) {
		UINT64 extra = 1000ull * 10000ull;
		p->endSample = CEmuS98_100nsToSample(mi.qwLength + extra, mi.dwSampleRate);
	} else {
		p->endSample = (UINT64)-1;
	}
	if (srcPath && srcPath[0])
		wcsncpy_s(p->sourcePath, srcPath, _TRUNCATE);
	FmMonShadowReset();
	FmMonShadowSetSource(p->sourcePath);
	FmMonShadowSetSampleRate(p->sampleRate);
	p->open = 1;
	return 1;
}

int CEmuS98Seek(CEmuS98Player* p, UINT64 sample, DWORD flags)
{
	(void)flags;
	if (!p || !p->impl) return 0;
	s98File* f = (s98File*)p->impl;
	UINT64 t100 = CEmuS98SampleTo100ns(sample, p->sampleRate);
	f->SetPosition((DWORD)(t100 / 10000ull));
	p->curSample = sample;
	return 1;
}

int CEmuS98Render(CEmuS98Player* p, short* outStereo, int sampleFrames)
{
	if (!p || !p->impl || !outStereo || sampleFrames <= 0) return 0;
	if (p->curSample >= p->endSample) return 0;
	s98File* f = (s98File*)p->impl;
	double* dbuf = (double*)_alloca((size_t)sampleFrames * 2 * sizeof(double));
	DWORD got = f->Write(dbuf, (DWORD)sampleFrames);
	if (p->endSample != (UINT64)-1 && p->curSample + got >= p->endSample)
		got = (DWORD)((p->endSample > p->curSample) ? (p->endSample - p->curSample) : 0);
	for (DWORD i = 0; i < got * 2; i++) {
		double v = dbuf[i];
		if (v > 1.0) v = 1.0;
		if (v < -1.0) v = -1.0;
		outStereo[i] = (short)(v * 32767.0);
	}
	p->curSample += got;
	/* FmMon は readcemu で一度だけ進める（ここでやると 2 倍速になりモニターずれ） */
	return (int)got;
}

UINT64 CEmuS98LengthSamples(const CEmuS98Player* p)
{
	if (!p) return 0;
	if (p->endSample == (UINT64)-1) return 0;
	return p->endSample;
}
