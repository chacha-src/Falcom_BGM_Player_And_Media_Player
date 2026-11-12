/* ループは 16〜64 小節。8 小節で同じ形に戻さない。 */
static int s_loopBars = 32;

static int StyleLoopBars(int style)
{
	switch (StyleFam(style)) {
	case FAM_PAD: return 64;
	case FAM_BALLAD: case FAM_JAZZ: case FAM_BOSSA: case FAM_FLOOR:
	case FAM_DNB: case FAM_DISCO: case FAM_LATIN: case FAM_SAMBA:
	case FAM_AFRO: case FAM_TANGO: case FAM_16: return 32;
	case FAM_WALTZ: case FAM_SHUF: case FAM_BLUES: return 24;
	default: return 16;
	}
}

/* 歌の線。出す音は 1根 3三度 5五度 8根のオクターブ上だけ。8の次は必ず戻す。 */
static const uint8_t kLead[32][16] = {
	{ 1,3,5,3, 1,5,3,1, 5,3,1,3, 5,3,1,1 },
	{ 1,5,3,5, 3,1,3,5, 1,3,5,3, 1,5,3,1 },
	{ 1,3,1,5, 3,5,8,5, 3,5,3,1, 5,3,1,1 },
	{ 1,5,1,3, 5,3,1,5, 3,1,5,3, 1,3,5,1 },
	{ 1,3,5,1, 3,5,3,5, 8,5,3,1, 3,5,3,1 },
	{ 1,1,3,5, 3,5,3,1, 5,3,5,3, 1,3,1,1 },
	{ 1,5,3,1, 5,8,5,3, 1,5,3,5, 3,1,5,1 },
	{ 1,3,5,5, 3,1,5,3, 5,1,3,5, 3,5,1,1 },
	{ 1,5,5,3, 1,3,5,1, 3,5,1,3, 5,8,5,1 },
	{ 1,3,1,3, 5,3,5,1, 3,5,3,5, 1,3,1,1 },
	{ 1,5,3,5, 1,3,1,5, 8,5,3,5, 1,3,5,1 },
	{ 1,1,5,3, 5,3,1,3, 1,5,3,1, 5,3,1,1 },
	{ 1,3,5,8, 5,3,5,1, 3,1,5,3, 5,3,1,1 },
	{ 1,5,1,5, 3,5,3,1, 5,3,1,5, 3,1,3,1 },
	{ 1,3,3,5, 1,5,5,3, 1,3,5,3, 1,5,1,1 },
	{ 1,5,3,1, 3,5,1,3, 5,3,5,8, 5,3,1,1 },
	{ 1,3,5,3, 5,1,3,5, 1,5,3,1, 3,5,1,1 },
	{ 1,1,3,1, 5,3,5,3, 1,3,5,8, 5,3,1,1 },
	{ 1,5,1,3, 1,5,3,5, 3,1,3,5, 1,3,1,1 },
	{ 1,3,5,1, 5,3,5,3, 1,3,1,5, 8,5,3,1 },
	{ 1,5,5,1, 3,5,3,5, 1,3,5,3, 5,1,3,1 },
	{ 1,3,1,5, 1,3,5,3, 5,8,5,3, 1,5,1,1 },
	{ 1,5,3,5, 8,5,3,1, 5,3,1,3, 5,3,1,1 },
	{ 1,1,5,1, 3,5,3,5, 1,5,3,1, 3,5,3,1 },
	{ 1,3,5,3, 1,3,1,5, 3,5,8,5, 3,1,5,1 },
	{ 1,5,3,1, 5,3,5,1, 3,5,3,1, 5,3,5,1 },
	{ 1,3,1,3, 5,1,5,3, 1,5,3,5, 3,1,1,1 },
	{ 1,5,8,5, 3,1,5,3, 5,3,1,5, 3,5,1,1 },
	{ 1,3,5,1, 3,1,5,3, 5,3,5,3, 1,5,3,1 },
	{ 1,1,3,5, 1,5,3,5, 8,5,1,3, 5,3,1,1 },
	{ 1,5,3,5, 3,5,1,3, 1,3,5,1, 5,3,1,1 },
	{ 1,3,5,3, 5,3,1,5, 3,1,3,5, 8,5,1,1 }
};

static int LeadId(int fam, int pat, int chunk)
{
	static const uint8_t home[FAM_COUNT] = {
		0, 4, 8, 2, 1, 9, 5, 6, 3, 10, 12, 11, 7, 13, 6, 14, 2, 5, 9, 8, 10, 13, 15
	};
	static_assert(sizeof(home) / sizeof(home[0]) == FAM_COUNT, "lead home");
	int f = fam;
	if (f < 0 || f >= FAM_COUNT) f = 0;
	int id = (int)home[f] + (pat & 15) + chunk * 3;
	if (id < 0) id = 0;
	return id % 32;
}

/* 2度・4度・導音はそのまま出すとぶつかる。近いコードトーンへ寄せる。 */
static int ChordDeg(int deg, int type)
{
	if (deg < 1) deg = 1;
	if (deg > 8) deg = 1;
	if (type == CH_POW) return (deg == 8) ? 8 : ((deg == 1) ? 1 : 5);
	if (type == CH_SUS) {
		if (deg == 8) return 8;
		if (deg == 5 || deg == 6 || deg == 7) return 5;
		if (deg == 1) return 1;
		return 4;
	}
	/* メジャーセブンスの長7度の隣に根音を置くと短2度になる。三度と五度だけ重ねる。 */
	if (type == CH_MAJ7) {
		if (deg == 8 || deg == 5 || deg == 6) return 5;
		return 3;
	}
	switch (deg) {
	case 1: return 1;
	case 2: case 3: return 3;
	case 4: case 5: case 6: return 5;
	case 7: case 8: return 8;
	default: return 1;
	}
}

static int LeadDeg(int style, int pat, int bar, int nth, int strong, int type)
{
	int fam = StyleFam(style);
	int chunk = bar / 4;
	if (chunk < 0) chunk = 0;
	const uint8_t* p = kLead[LeadId(fam, pat, chunk)];
	int i = nth & 15;
	if (pat & 4) i = (i + 5) & 15;
	int deg = ChordDeg((int)p[i & 15], type);
	if (strong && deg == 8) deg = 1;
	return deg;
}

/* 根音をド4に置く。64はミなので、そこを基準にすると和音より長3度高く鳴る。 */
static int LeadMidi(int root, int type, int deg, int dropOct)
{
	int iv = 0;
	if (deg == 8) iv = 12;
	else if (deg == 5) iv = 7;
	else if (deg == 4) iv = 5;
	else if (deg == 3) iv = ThirdIv(type);
	int rootN = 60 + (root % 12);
	if (rootN < 64) rootN += 12;
	int m = rootN + iv;
	/* A〜H は和音の上に乗せる。I〜P はそのオクターブ下で同じ構成音。 */
	if (!dropOct) m += 12;
	if (m > 93) m -= 12;
	if (m < 0) m = 0;
	if (m > 127) m = 127;
	return m;
}

static int VibDepth(int style)
{
	switch (StyleFam(style)) {
	case FAM_BALLAD: case FAM_JAZZ: case FAM_BOSSA: case FAM_PAD: return 26;
	case FAM_FUNK: case FAM_BLUES: case FAM_16: case FAM_LATIN: return 18;
	case FAM_METAL: case FAM_MARCH: case FAM_FLOOR: case FAM_TRAP: case FAM_DNB: return 0;
	default: return 12;
	}
}

/* 長いパッドだけ、ごく浅いビブラート。旋律のしゃくりは幽霊ボイスになるので掛けない。 */
static void PlaceSing(Ctx* c, int ch, uint32_t tick, int dur, int style, int scoop)
{
	(void)scoop;
	if (!c || c->isFm || ch < 0 || dur < 96) return;
	int depth = VibDepth(style);
	if (depth <= 0) return;
	if (depth > 12) depth = 12;
	Push(c, tick, ch, SC_EV_CC, 1, (uint8_t)(depth / 4), 0, 0);
	uint32_t t1 = tick + (uint32_t)(dur / 2);
	if (t1 <= tick) t1 = tick + 1;
	Push(c, t1, ch, SC_EV_CC, 1, (uint8_t)depth, 0, 0);
}

/* 0=その拍のまま  1=この16分を32分ふたつ  2=この16分を64分四つ */
static uint8_t s_figArp[16];
static uint8_t s_figCnt[16];

static void ClearFig(void)
{
	memset(s_figArp, 0, sizeof(s_figArp));
	memset(s_figCnt, 0, sizeof(s_figCnt));
}

/* 1拍。0=8+16+16  1=16+16+32が4つ  2=16が4つ  3=8+8
   4=32が8つ  5=8+16+64が4つ  6=16が3つ+64が4つ  7=4分 */
static void BeatCell(uint16_t* mask, uint8_t* fig, int base, int cell)
{
	if (!mask || !fig || base < 0 || base > 12) return;
	*mask = (uint16_t)(*mask & (uint16_t)~(0xFu << base));
	for (int i = 0; i < 4; i++) fig[base + i] = 0;
	switch (cell) {
	case 0: *mask = (uint16_t)(*mask | (uint16_t)(0xDu << base)); break;
	case 1:
		*mask = (uint16_t)(*mask | (uint16_t)(0xFu << base));
		fig[base + 2] = 1; fig[base + 3] = 1;
		break;
	case 2: *mask = (uint16_t)(*mask | (uint16_t)(0xFu << base)); break;
	case 3: *mask = (uint16_t)(*mask | (uint16_t)(0x5u << base)); break;
	case 4:
		*mask = (uint16_t)(*mask | (uint16_t)(0xFu << base));
		for (int i = 0; i < 4; i++) fig[base + i] = 1;
		break;
	case 5:
		*mask = (uint16_t)(*mask | (uint16_t)(0xDu << base));
		fig[base + 3] = 2;
		break;
	case 6:
		*mask = (uint16_t)(*mask | (uint16_t)(0xFu << base));
		fig[base + 3] = 2;
		break;
	default: *mask = (uint16_t)(*mask | (uint16_t)(1u << base)); break;
	}
}

static int Grid16Fam(int fam)
{
	switch (fam) {
	case FAM_16: case FAM_FUNK: case FAM_FLOOR: case FAM_DNB:
	case FAM_JAZZ: case FAM_BLUES: case FAM_SHUF: case FAM_DISCO:
	case FAM_SAMBA: case FAM_LATIN: case FAM_AFRO: case FAM_METAL:
	case FAM_HOP: case FAM_8:
		return 1;
	default: return 0;
	}
}

/* 拍ごとに違う割り。全部を同じ音価で埋めない。64分は文の端だけ。 */
static void PaintHuman(int fam, int pat, int bar, int q, int kind, uint16_t* arpM, uint16_t* cntM)
{
	if (!arpM || !cntM || !Grid16Fam(fam)) return;
	static const uint8_t pop[8][4] = {
		{ 0, 1, 2, 0 }, { 2, 0, 1, 3 }, { 0, 2, 4, 0 }, { 3, 1, 0, 2 },
		{ 2, 0, 0, 1 }, { 0, 3, 2, 0 }, { 1, 2, 0, 3 }, { 2, 1, 3, 0 }
	};
	static const uint8_t jazz[8][4] = {
		{ 3, 3, 0, 3 }, { 3, 1, 3, 5 }, { 0, 3, 3, 6 }, { 3, 0, 1, 3 },
		{ 3, 0, 3, 3 }, { 0, 3, 1, 3 }, { 3, 3, 3, 5 }, { 3, 2, 0, 3 }
	};
	static const uint8_t floor[8][4] = {
		{ 2, 2, 1, 2 }, { 2, 4, 2, 6 }, { 2, 1, 2, 1 }, { 0, 2, 4, 2 },
		{ 2, 0, 2, 1 }, { 1, 2, 2, 4 }, { 2, 2, 0, 2 }, { 4, 2, 1, 2 }
	};
	static const uint8_t metal[8][4] = {
		{ 2, 2, 4, 2 }, { 2, 4, 2, 2 }, { 4, 2, 2, 2 }, { 2, 1, 2, 2 },
		{ 2, 2, 2, 4 }, { 1, 2, 4, 2 }, { 2, 4, 1, 2 }, { 4, 2, 2, 1 }
	};
	static const uint8_t hop[8][4] = {
		{ 7, 0, 3, 7 }, { 3, 0, 7, 3 }, { 0, 3, 2, 7 }, { 7, 3, 0, 7 },
		{ 3, 7, 0, 3 }, { 0, 7, 3, 0 }, { 7, 0, 7, 2 }, { 3, 0, 7, 0 }
	};
	static const uint8_t rock[8][4] = {
		{ 3, 0, 3, 0 }, { 0, 3, 2, 3 }, { 3, 2, 0, 3 }, { 2, 0, 3, 0 },
		{ 0, 3, 0, 2 }, { 3, 0, 3, 3 }, { 2, 3, 0, 3 }, { 0, 2, 3, 0 }
	};
	const uint8_t* row;
	int which = (pat + (pat >> 3) + q * 3) & 7;
	if (fam == FAM_JAZZ || fam == FAM_BLUES || fam == FAM_SHUF) row = jazz[which];
	else if (fam == FAM_FLOOR || fam == FAM_DNB || fam == FAM_DISCO) row = floor[which];
	else if (fam == FAM_METAL) row = metal[which];
	else if (fam == FAM_HOP) row = hop[which];
	else if (fam == FAM_8) row = rock[which];
	else row = pop[which];
	int allow64 = (fam == FAM_JAZZ || fam == FAM_BLUES || fam == FAM_SHUF
		|| fam == FAM_FLOOR || fam == FAM_DNB)
		&& (((bar & 3) == 3) || (q == 2 && (bar & 1)));
	if (kind == AR_VOCAL || kind == AR_QUIET || kind == AR_DARK || kind == AR_PAD) {
		allow64 = 0;
		row = jazz[0];
	}
	uint16_t a = 0, c = 0;
	for (int beat = 0; beat < 4; beat++) {
		int cell = row[beat];
		int cellC = row[(beat + 1) & 3];
		if (!allow64 && (cell == 5 || cell == 6)) cell = (cell == 5) ? 0 : 2;
		if (!allow64 && (cellC == 5 || cellC == 6)) cellC = (cellC == 5) ? 0 : 2;
		if (cellC == 4) cellC = 2;
		if (fam == FAM_METAL && (cell == 5 || cell == 6)) cell = 2;
		if (fam == FAM_METAL && (cellC == 5 || cellC == 6)) cellC = 2;
		BeatCell(&a, s_figArp, beat * 4, cell);
		BeatCell(&c, s_figCnt, beat * 4, cellC);
	}
	*arpM = a;
	*cntM = c;
}

/* 16分格子にしないスタイル。4分と8分と 8+16+16 だけをアレンジでずらす。 */
static void PaintSlow(int fam, int pat, int q, uint16_t* arpM, uint16_t* cntM)
{
	if (!arpM || !cntM) return;
	if (fam == FAM_WALTZ || fam == FAM_MARCH) return;
	static const uint8_t slow[8][4] = {
		{ 7, 3, 7, 0 }, { 3, 7, 0, 7 }, { 7, 0, 3, 7 }, { 0, 7, 7, 3 },
		{ 3, 0, 7, 3 }, { 7, 3, 0, 7 }, { 0, 3, 7, 0 }, { 3, 7, 3, 0 }
	};
	int which = (pat + (pat >> 3) + q * 3) & 7;
	const uint8_t* row = slow[which];
	uint16_t a = 0, c = 0;
	for (int beat = 0; beat < 4; beat++) {
		BeatCell(&a, s_figArp, beat * 4, row[beat]);
		BeatCell(&c, s_figCnt, beat * 4, row[(beat + 1) & 3]);
	}
	if (*arpM) *arpM = a;
	*cntM = c;
}

/* スタイルの骨格を、ループ全長の文に乗せる。8小節で同じ打点に戻さない。 */
static void WriteLiveChart(int style, int pat, int bar, int div, int section,
	uint16_t* drm, uint8_t* bass, uint16_t* comp, uint16_t* pad,
	uint16_t* arp, uint16_t* harm, uint16_t* cnt, uint16_t* stab, uint16_t* color)
{
	if (!drm || !bass) return;
	ClearFig();
	drm[DV_RIM] = 0;
	drm[DV_TOM] = 0;
	drm[DV_CRASH] = 0;
	drm[DV_CLAP] = 0;
	const int kind = ArrangeKind(style, pat);
	const int fam = StyleFam(style);
	const int quiet = StyleQuiet(style);
	int loop = s_loopBars;
	if (loop < 16 || loop > 64) loop = StyleLoopBars(style);
	if (bar < 0) bar = 0;
	const int at = bar % loop;
	const int qn = (loop / 4) > 0 ? (loop / 4) : 1;
	int q = at / qn;
	if (q > 3) q = 3;
	const int within = at - q * qn;
	const int qLast = (within == qn - 1);
	const int loopLast = (at == loop - 1);
	const int answer = (within & 1);
	const int midLift = (qn >= 8 && within >= qn / 2 && !qLast);
	const int hatMode = pat & 3;
	const int kickMode = (pat >> 2) & 3;

	uint16_t kick = 0x1111, snare = 0x1010, hat = 0x5555, ohat = 0, ride = 0;
	uint16_t compM = 0x0101, padM = 0x0001, arpM = 0x1111, harmM = 0x0101;
	uint16_t cntM = 0x1111, stabM = 0, colM = 0;
	static const uint8_t degRoot[] = { 1, 1, 2, 1 };
	static const uint8_t degOct[] = { 1, 3, 1, 3, 1, 3, 2, 3 };
	static const uint8_t degWalk[] = { 1, 5, 2, 8, 1, 2, 5, 3 };
	static const uint8_t degSync[] = { 1, 2, 5, 1, 8, 2, 5, 3 };
	static const uint8_t degOff[] = { 1, 5, 1, 2 };
	const uint8_t* deg = degRoot;
	int nd = 4;
	uint16_t bassHit = 0x1111;

	switch (fam) {
	case FAM_FLOOR:
		kick = 0x1111; snare = 0x1010; hat = 0x5555; ohat = (q >= 2) ? 0x4444 : 0;
		compM = 0x0101; arpM = 0xFFFF; cntM = 0xCCCC; harmM = 0x8888;
		bassHit = 0x1111; deg = degOct; nd = 8;
		break;
	case FAM_DNB:
		kick = 0x0111; snare = 0x1010; hat = 0xFFFF;
		compM = 0x0001; arpM = 0xFFFF; cntM = 0xAAAA; harmM = 0x2222;
		bassHit = (q & 1) ? 0x1515 : 0x0111; deg = degSync; nd = 8;
		break;
	case FAM_HOP:
		kick = answer ? 0x1101 : 0x1111; snare = 0x1010; hat = 0x5555;
		compM = 0x0101; arpM = (q == 2) ? 0x1111 : 0x0010; bassHit = 0x0101; deg = degRoot; nd = 4;
		break;
	case FAM_TRAP:
		kick = (q >= 2) ? 0x0101 : 0x0001; snare = 0x0100; hat = 0x5555; ohat = (q == 2) ? 0x4444 : 0;
		compM = 0x0001; arpM = 0x1111; bassHit = 0x0001; deg = degOct; nd = 4;
		break;
	case FAM_FUNK:
		kick = answer ? 0x1515 : 0x1511; snare = 0x1110; hat = 0xFFFF; ohat = 0x0404;
		compM = 0x4444; arpM = 0xAAAA; cntM = 0x2222; harmM = 0x8888;
		bassHit = 0x5757; deg = degSync; nd = 8;
		break;
	case FAM_DISCO:
		kick = 0x1111; snare = 0x1010; hat = 0; ohat = (q >= 2) ? 0x1111 : 0x0100;
		compM = 0x4444; arpM = 0xAAAA; cntM = 0x2222; harmM = 0x8888;
		bassHit = 0x5555; deg = degOct; nd = 8;
		break;
	case FAM_REG: case FAM_SKA:
		kick = (q == 0) ? 0x0100 : 0x0000; snare = 0x0100; hat = 0x5555;
		compM = 0x4444; arpM = 0; bassHit = 0x0101; deg = degOff; nd = 4;
		stabM = 0x4444;
		break;
	case FAM_JAZZ:
		kick = answer ? 0x0101 : 0x1111; snare = 0; ride = (q >= 2) ? 0xFFFF : 0x5555; hat = 0;
		compM = 0x8888; arpM = (q == 2) ? 0xFFFF : 0xDDDD; cntM = 0x2222; harmM = 0x8888;
		bassHit = 0x1111; deg = degWalk; nd = 8;
		stabM = 0;
		drm[DV_RIM] = Fit16(answer ? 0x0404 : 0x1010, div);
		break;
	case FAM_BLUES: case FAM_SHUF:
		kick = 0x1111; snare = answer ? 0x1110 : 0x1010; ride = 0x5555; hat = 0;
		compM = (q >= 2) ? 0x5555 : 0x1111; arpM = 0xDDDD; cntM = 0x8888; harmM = 0x2222;
		bassHit = 0x1111; deg = degWalk; nd = 8;
		break;
	case FAM_WALTZ:
		kick = 0x0001; snare = (q >= 2) ? 0x0110 : 0x0010; hat = 0x0111;
		compM = 0x0110; padM = 0x0001; bassHit = 0x0111; deg = degRoot; nd = 3;
		arpM = (q == 2) ? 0x0111 : 0x0010;
		break;
	case FAM_BOSSA:
		kick = answer ? 0x0141 : 0x0411; snare = 0; hat = 0x5555;
		drm[DV_RIM] = Fit16(answer ? 0x4104 : 0x1041, div);
		compM = 0x4444; bassHit = 0x0101; deg = degRoot; nd = 4; arpM = 0x0101;
		break;
	case FAM_SAMBA: case FAM_LATIN: case FAM_AFRO:
		kick = answer ? 0x1511 : 0x1111; snare = 0x1010; hat = (q >= 2) ? 0xFFFF : 0x5555;
		compM = 0x4545; arpM = 0xAAAA; cntM = 0x2222; harmM = 0x8888;
		bassHit = 0x1515; deg = degSync; nd = 8;
		break;
	case FAM_TANGO:
		kick = answer ? 0x0151 : 0x0051; snare = 0x1000; hat = 0x1111;
		compM = 0x1111; bassHit = 0x1111; deg = degRoot; nd = 4; arpM = 0x0101;
		break;
	case FAM_METAL:
		kick = (q >= 2) ? 0xFFFF : 0x5555; snare = answer ? 0x1111 : 0x1010; hat = 0xFFFF;
		compM = 0x1111; arpM = 0x5555; bassHit = 0x5555; deg = degRoot; nd = 4;
		break;
	case FAM_8:
		kick = answer ? 0x1011 : 0x1111; snare = 0x1010; hat = (q >= 2) ? 0xFFFF : 0x5555;
		compM = (q == 0) ? 0x0101 : 0x1111; bassHit = 0x1111; deg = degRoot; nd = 4;
		break;
	case FAM_BALLAD: case FAM_PAD:
		kick = (q >= 2) ? 0x0101 : 0x0001; snare = (q == 0) ? 0 : 0x1010;
		hat = (q >= 2) ? 0x5555 : 0x1111; ride = (fam == FAM_PAD) ? 0 : 0x1111;
		compM = 0x0001; padM = 0x0001; arpM = (q == 2) ? 0x0101 : 0; bassHit = 0x0001; deg = degRoot; nd = 2;
		harmM = 0x0101; stabM = 0; cntM = (q >= 2) ? 0x1111 : 0x0101;
		break;
	case FAM_MARCH:
		kick = 0x1111; snare = (q & 1) ? 0x5555 : 0x1111; hat = 0;
		compM = 0x1111; bassHit = 0x1111; deg = degRoot; nd = 4; arpM = 0;
		break;
	case FAM_16:
	default:
		kick = answer ? 0x1551 : 0x1511; snare = 0x1010; hat = 0xFFFF;
		compM = 0x4444; arpM = 0xEEEE; cntM = 0xAAAA; harmM = 0x2222;
		bassHit = 0x7777; deg = degSync; nd = 8;
		break;
	}

	if (midLift && !quiet && fam != FAM_WALTZ && fam != FAM_JAZZ) {
		hat = (uint16_t)(hat | 0xAAAA);
		ohat = (uint16_t)(ohat | 0x0100);
	}
	if (!quiet && fam != FAM_JAZZ && fam != FAM_WALTZ && fam != FAM_BALLAD && fam != FAM_PAD) {
		if (hatMode == 1 && hat) hat = 0xFFFF;
		else if (hatMode == 2 && hat) { ride = hat; hat = 0; }
		else if (hatMode == 3) ohat = (uint16_t)(ohat | 0x4444);
		if (kickMode == 1) kick = (uint16_t)(kick | 0x0008);
		else if (kickMode == 2) kick = (uint16_t)(kick & (uint16_t)~0x0100);
		else if (kickMode == 3) kick = (uint16_t)(kick | 0x1000);
	}

	if (!Grid16Fam(fam)) {
		switch (q) {
		case 0:
			if (cntM == 0x1111 || cntM == 0) cntM = quiet ? 0x0101 : 0x1111;
			harmM = quiet ? 0x0001 : 0x0101;
			if (fam != FAM_REG && fam != FAM_SKA) stabM = 0;
			colM = 0;
			deg = degRoot; nd = (nd > 4) ? 4 : nd;
			break;
		case 1:
			cntM = quiet ? 0x1111 : 0x1511;
			harmM = 0x1111;
			deg = degWalk; nd = 8;
			bassHit = (uint16_t)(bassHit | 0x0101);
			colM = 0;
			break;
		case 2:
			cntM = quiet ? 0x1111 : 0x5555;
			harmM = 0x1511;
			deg = degOct; nd = 8;
			if (fam != FAM_REG && fam != FAM_SKA) stabM = 0x1010;
			colM = 0x0100;
			if (!quiet && compM == 0x0001) compM = 0x0101;
			break;
		default:
			cntM = qLast ? 0x0001 : 0x1111;
			harmM = 0x0101;
			deg = degSync; nd = 8;
			stabM = 0;
			colM = 0;
			if (loopLast) { arpM = 0; compM = 0x0001; }
			break;
		}
	} else if (loopLast) {
		arpM = 0;
		cntM = 0x0001;
		compM = 0x0001;
	}

	switch (kind) {
	case AR_DARK: case AR_QUIET: case AR_PAD:
		hat = 0x1111; ohat = 0; compM = 0x0001; arpM = 0; stabM = 0;
		cntM = 0x0101; bassHit = 0x0001; deg = degRoot; nd = 2;
		break;
	case AR_VOCAL:
		compM = 0x0001; arpM = 0; stabM = 0; cntM = (q >= 2) ? 0x5555 : 0x1111;
		break;
	case AR_HALF: case AR_WOB:
		snare = 0x0100; kick = 0x0001; bassHit = 0x0101; compM = 0x0100; cntM = 0x0101;
		break;
	case AR_OFF: case AR_SKANK: case AR_ONEDROP:
		compM = 0x4444; stabM = 0x4444;
		if (kind == AR_ONEDROP) { kick = 0x0100; snare = 0x0100; }
		break;
	case AR_CALL:
		if (q & 1) { compM = 0x1111; stabM = 0x1010; cntM = 0x0001; }
		else { compM = 0x0001; stabM = 0; cntM = 0x5555; arpM = 0x0101; }
		break;
	case AR_BREAK:
		if (q == 3) { snare = loopLast ? 0xF000 : 0x1000; compM = 0x0001; arpM = 0; }
		break;
	case AR_BUILD:
		if (q == 0) { hat = 0x1111; kick = 0x0001; cntM = 0x0101; }
		else if (q == 1) { hat = 0x5555; kick = 0x0101; cntM = 0x1111; }
		else if (q == 2) { hat = 0xFFFF; kick = 0x1111; cntM = 0x5555; }
		else { hat = 0xFFFF; kick = 0x5555; ohat = 0x4444; cntM = 0x5555; }
		break;
	case AR_DROP:
		if (q == 2) { kick = 0x0001; snare = 0; hat = 0x1111; compM = 0; arpM = 0; cntM = 0x0101; }
		break;
	case AR_ARP: arpM = 0xFFFF; compM = 0x0001; break;
	case AR_BLAST: kick = 0xFFFF; snare = 0xFFFF; hat = 0; break;
	case AR_BRUSH: ride = 0x1111; hat = 0; snare = 0; compM = 0x0001; break;
	case AR_FOUR: case AR_RIDE: case AR_COMP:
		ride = 0x5555; hat = 0; compM = 0x0410; deg = degWalk; nd = 8;
		break;
	case AR_FAN: kick = 0x1111; snare = 0x1111; stabM = 0x1111; drm[DV_CRASH] = 1; break;
	case AR_UNI: compM = kick; stabM = kick; arpM = 0; break;
	case AR_GHOST: drm[DV_RIM] = Fit16(0x4444, div); break;
	default: break;
	}

	if ((qLast && (q == 1 || q == 3)) || loopLast) {
		uint16_t tail = Fit16(0xF000, div);
		hat = (uint16_t)(hat & (uint16_t)~tail);
		ride = (uint16_t)(ride & (uint16_t)~tail);
		snare = (uint16_t)(snare | tail);
		drm[DV_TOM] = tail;
		cntM = 0x0001;
		stabM = 0;
		arpM = 0;
	} else if (within == 0 && (q == 2 || (q == 0 && at != 0)) && !quiet && kind != AR_QUIET && kind != AR_PAD) {
		drm[DV_CRASH] = 1;
	}
	if ((fam == FAM_FUNK || fam == FAM_16) && !qLast && !quiet)
		drm[DV_RIM] = Fit16(0x4444, div);
	if ((fam == FAM_HOP || fam == FAM_DISCO) && snare)
		drm[DV_CLAP] = Fit16(snare, div);

	if (section == SEC_INTRO && !IntroHot(style) && (bar & 3) < 2) {
		kick = 0x0001; snare = 0; hat = 0x1111; ohat = 0; ride = 0;
		compM = 0x0001; arpM = 0; stabM = 0; cntM = 0x0101; harmM = 0x0001;
		bassHit = 0x0001; deg = degRoot; nd = 1;
		drm[DV_TOM] = 0; drm[DV_CRASH] = 0; drm[DV_CLAP] = 0;
	} else if (section == SEC_BREAK && !loopLast) {
		kick = 0x0001; ohat = 0; arpM = (uint16_t)(arpM & 0x0101); stabM = 0;
		cntM = 0x0101; compM = 0x0001;
	} else if (section == SEC_BODY && !qLast && !loopLast
		&& kind != AR_QUIET && kind != AR_DARK && kind != AR_PAD && kind != AR_HALF) {
		if (Grid16Fam(fam))
			PaintHuman(fam, pat, bar, q, kind, &arpM, &cntM);
		else
			PaintSlow(fam, pat, q, &arpM, &cntM);
	}
	if ((fam == FAM_16 || fam == FAM_FUNK) && section == SEC_BODY && !qLast && !loopLast
		&& kind != AR_QUIET && kind != AR_DARK && kind != AR_PAD && kind != AR_HALF) {
		/* ベースは小節ごとに 16分3つ と 8+16+16 を行き来する。32分は上の線に任せる。 */
		bassHit = (bar & 1) ? 0xDDDD : 0x7777;
		deg = degSync;
		nd = 8;
	}

	drm[DV_KICK] = Fit16(kick, div);
	drm[DV_SNARE] = Fit16(snare, div);
	drm[DV_HAT] = Fit16(hat, div);
	drm[DV_OHAT] = Fit16(ohat, div);
	drm[DV_RIDE] = Fit16(ride, div);
	if (quiet) { drm[DV_OHAT] = 0; drm[DV_CLAP] = 0; }
	BassOn(bass, SlotN(div), Fit16(bassHit, div), deg, nd);
	if (comp) *comp = Fit16(compM, div);
	if (pad) *pad = Fit16(padM, div);
	if (arp) *arp = Fit16(arpM, div);
	if (harm) *harm = Fit16(harmM, div);
	if (cnt) *cnt = Fit16(cntM, div);
	if (stab) *stab = Fit16(stabM, div);
	if (color) *color = Fit16(colM, div);
	(void)kind;
}
