#include "stdafx.h"
#include "CSasamiAutoArrange.h"
#include <string.h>
#include <stdlib.h>

/* スタイルは 16 分（または 12 分割）の格子。x が打点。ベース数字は度数。 */

enum {
	CH_MAJ = 0, CH_MIN, CH_7, CH_M7, CH_MAJ7, CH_SUS, CH_DIM, CH_POW
};
enum { SEC_BODY = 0, SEC_INTRO, SEC_BREAK, SEC_END };
enum {
	DV_KICK = 0, DV_SNARE, DV_HAT, DV_OHAT, DV_RIDE, DV_CRASH, DV_TOM, DV_CLAP, DV_RIM, DV_COUNT
};

struct StylePat {
	int div;
	uint16_t drum[DV_COUNT];
	uint8_t bass[16];
	uint16_t chord;
	int chordSlots;
	int arp;
	int swing;
	int power;
	int progBass;
	int progChord;
	int bend; /* 1=小節頭のベースにピッチベンド（しゃくり）。コードトーン以外は出さない */
	uint8_t progAll[8]; /* 0bass 1chord 2pad 3beat 4off 5gtr 6str 7lead */
	int progKit;
};

struct BarInfo {
	uint32_t t0;
	int len;
	int root;
	int type;
	int root2; /* 0 以上なら後半はこの和音。2分音符で小節内が変わる */
	int type2;
	int section;
	int voiced;
};

static int IsNoteKind(uint8_t k)
{
	return k == SC_EV_NOTE || k == SC_EV_FM_NOTE;
}

static int IsSetupKind(uint8_t k)
{
	switch (k) {
	case SC_EV_TEMPO: case SC_EV_FM_TEMPO: case SC_EV_METER: case SC_EV_KEY:
	case SC_EV_PROG: case SC_EV_BANK: case SC_EV_VOL: case SC_EV_PAN: case SC_EV_VELO:
	case SC_EV_PITCH: case SC_EV_FM_VOICE: case SC_EV_FM_VOL: case SC_EV_FM_PITCH:
	case SC_EV_DRUM: case SC_EV_CLEF: case SC_EV_PCM_SAMPLE: case SC_EV_FM_FLR:
	case SC_EV_FM_LFO: case SC_EV_FM_DETUNE: case SC_EV_FM_EX:
		return 1;
	default:
		return 0;
	}
}

int ScAutoArrangeCountNotes(const ScEvent* ev, int n)
{
	int c = 0;
	if (!ev || n <= 0) return 0;
	for (int i = 0; i < n; i++)
		if (IsNoteKind(ev[i].kind)) c++;
	return c;
}

static const wchar_t* StyleNm(int style)
{
	switch (style) {
	case SC_AS_16BEAT: return LL14(L"16ビート", L"16-beat", L"Binaire 16", L"Sedicesimi", L"Semicorcheas", L"16비트", L"16拍", L"إيقاع 16", L"16-дольный", L"16tel-Beat", L"Semicolcheias", L"16e noten", L"Szesnastki", L"16'lık");
	case SC_AS_8BEAT: return LL14(L"8ビート", L"8-beat", L"Binaire 8", L"Ottavi", L"Corcheas", L"8비트", L"8拍", L"إيقاع 8", L"8-дольный", L"8tel-Beat", L"Colcheias", L"8e noten", L"Ósemki", L"8'lik");
	case SC_AS_REGGAE: return LL14(L"レゲエ", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"레게", L"雷鬼", L"ريغي", L"Регги", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae");
	case SC_AS_TECHNO: return LL14(L"テクノ", L"Techno", L"Techno", L"Techno", L"Techno", L"테크노", L"科技舞曲", L"تكنو", L"Техно", L"Techno", L"Techno", L"Techno", L"Techno", L"Tekno");
	case SC_AS_HOUSE: return LL14(L"ハウス", L"House", L"House", L"House", L"House", L"하우스", L"浩室", L"هاوس", L"Хаус", L"House", L"House", L"House", L"House", L"House");
	case SC_AS_TRANCE: return LL14(L"トランス", L"Trance", L"Trance", L"Trance", L"Trance", L"트랜스", L"出神", L"ترانس", L"Транс", L"Trance", L"Trance", L"Trance", L"Trance", L"Trans");
	case SC_AS_DNB: return LL14(L"ドラムンベース", L"Drum and bass", L"Drum and bass", L"Drum and bass", L"Drum and bass", L"드럼앤베이스", L"鼓打贝斯", L"درام أند بيس", L"Драм-н-бейс", L"Drum and Bass", L"Drum and bass", L"Drum and bass", L"Drum and bass", L"Drum and bass");
	case SC_AS_HIPHOP: return LL14(L"ヒップホップ", L"Hip-hop", L"Hip-hop", L"Hip-hop", L"Hip-hop", L"힙합", L"嘻哈", L"هيب هوب", L"Хип-хоп", L"Hip-Hop", L"Hip-hop", L"Hiphop", L"Hip-hop", L"Hip-hop");
	case SC_AS_FUNK: return LL14(L"ファンク", L"Funk", L"Funk", L"Funk", L"Funk", L"펑크", L"放克", L"فانك", L"Фанк", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk");
	case SC_AS_DISCO: return LL14(L"ディスコ", L"Disco", L"Disco", L"Disco", L"Disco", L"디스코", L"迪斯科", L"ديسكو", L"Диско", L"Disco", L"Disco", L"Disco", L"Disco", L"Disko");
	case SC_AS_ROCK: return LL14(L"ロック", L"Rock", L"Rock", L"Rock", L"Rock", L"록", L"摇滚", L"روك", L"Рок", L"Rock", L"Rock", L"Rock", L"Rock", L"Rock");
	case SC_AS_METAL: return LL14(L"メタル", L"Metal", L"Metal", L"Metal", L"Metal", L"메탈", L"金属", L"ميتال", L"Метал", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal");
	case SC_AS_BALLAD: return LL14(L"バラード", L"Ballad", L"Ballade", L"Ballata", L"Balada", L"발라드", L"抒情曲", L"أغنية بطيئة", L"Баллада", L"Ballade", L"Balada", L"Ballade", L"Ballada", L"Balad");
	case SC_AS_WALTZ: return LL14(L"ワルツ", L"Waltz", L"Valse", L"Valzer", L"Vals", L"왈츠", L"华尔兹", L"فالس", L"Вальс", L"Walzer", L"Valsa", L"Wals", L"Walc", L"Vals");
	case SC_AS_BOSSA: return LL14(L"ボサノバ", L"Bossa nova", L"Bossa-nova", L"Bossa nova", L"Bossa nova", L"보사노바", L"波萨诺瓦", L"بوسا نوفا", L"Босанова", L"Bossa Nova", L"Bossa nova", L"Bossa nova", L"Bossa nova", L"Bossa nova");
	case SC_AS_SAMBA: return LL14(L"サンバ", L"Samba", L"Samba", L"Samba", L"Samba", L"삼바", L"桑巴", L"سامبا", L"Самба", L"Samba", L"Samba", L"Samba", L"Samba", L"Samba");
	case SC_AS_SHUFFLE: return LL14(L"シャッフル", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle", L"셔플", L"摇摆", L"شَفْل", L"Шаффл", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle");
	case SC_AS_MARCH: return LL14(L"マーチ", L"March", L"Marche", L"Marcia", L"Marcha", L"행진곡", L"进行曲", L"مارش", L"Марш", L"Marsch", L"Marcha", L"Mars", L"Marsz", L"Marş");
	case SC_AS_POLKA: return LL14(L"ポルカ", L"Polka", L"Polka", L"Polka", L"Polka", L"폴카", L"波尔卡", L"بولكا", L"Полька", L"Polka", L"Polca", L"Polka", L"Polka", L"Polka");
	case SC_AS_CITYPOP: return LL14(L"シティポップ", L"City pop", L"City pop", L"City pop", L"City pop", L"시티팝", L"都市流行", L"سيتي بوب", L"Сити-поп", L"City-Pop", L"City pop", L"Citypop", L"City pop", L"City pop");
	case SC_AS_GAME: return LL14(L"ゲーム", L"Game", L"Jeu vidéo", L"Gioco", L"Videojuego", L"게임", L"游戏", L"موسيقى ألعاب", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun");
	case SC_AS_ORCH: return LL14(L"オーケストラ", L"Orchestra", L"Orchestre", L"Orchestra", L"Orquesta", L"오케스트라", L"管弦乐", L"أوركسترا", L"Оркестр", L"Orchester", L"Orquestra", L"Orkest", L"Orkiestra", L"Orkestra");
	case SC_AS_AMBIENT: return LL14(L"アンビエント", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"앰비언트", L"氛围", L"أمبينت", L"Эмбиент", L"Ambient", L"Ambiente", L"Ambient", L"Ambient", L"Ambient");
	case SC_AS_BREAKS: return LL14(L"ブレイクビーツ", L"Breakbeat", L"Breakbeat", L"Breakbeat", L"Breakbeat", L"브레이크비트", L"碎拍", L"بريك بيت", L"Брейкбит", L"Breakbeat", L"Breakbeat", L"Breakbeat", L"Breakbeat", L"Breakbeat");
	case SC_AS_LATIN: return LL14(L"ラテン", L"Latin", L"Latin", L"Latino", L"Latino", L"라틴", L"拉丁", L"لاتيني", L"Латино", L"Latin", L"Latino", L"Latijn", L"Latynoski", L"Latin");
	case SC_AS_ENKA: return LL14(L"演歌", L"Enka", L"Enka", L"Enka", L"Enka", L"엔카", L"演歌", L"إنكا", L"Энка", L"Enka", L"Enka", L"Enka", L"Enka", L"Enka");
	case SC_AS_FUSION: return LL14(L"フュージョン", L"Fusion", L"Fusion", L"Fusion", L"Fusión", L"퓨전", L"融合", L"فيوجن", L"Фьюжн", L"Fusion", L"Fusão", L"Fusion", L"Fusion", L"Füzyon");
	case SC_AS_ACOUSTIC: return LL14(L"アコースティック", L"Acoustic", L"Acoustique", L"Acustico", L"Acústico", L"어쿠스틱", L"原声", L"أكوستيك", L"Акустика", L"Akustisch", L"Acústico", L"Akoestisch", L"Akustyczny", L"Akustik");
	case SC_AS_PIANO: return LL14(L"ピアノトリオ", L"Piano trio", L"Trio piano", L"Trio pianistico", L"Trío de piano", L"피아노 트리오", L"钢琴三重奏", L"ثلاثي بيانو", L"Фортепианное трио", L"Klaviertrio", L"Trio de piano", L"Pianotrio", L"Trio fortepianowe", L"Piyano üçlüsü");
	case SC_AS_FANFARE: return LL14(L"ファンファーレ", L"Fanfare", L"Fanfare", L"Fanfara", L"Fanfarria", L"팬파레", L"号角", L"فانفار", L"Фанфары", L"Fanfare", L"Fanfarra", L"Fanfare", L"Fanfara", L"Fanfar");
	case SC_AS_SLOWROCK: return LL14(L"スローロック", L"Slow rock", L"Rock lent", L"Rock lento", L"Rock lento", L"슬로우 록", L"慢摇滚", L"روك بطيء", L"Медленный рок", L"Slow-Rock", L"Rock lento", L"Langzame rock", L"Wolny rock", L"Yavaş rock");
	case SC_AS_JAZZ: return LL14(L"ジャズ", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"재즈", L"爵士", L"جاز", L"Джаз", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"Caz");
	case SC_AS_SWINGJAZZ: return LL14(L"スウィングジャズ", L"Swing jazz", L"Jazz swing", L"Swing jazz", L"Jazz swing", L"스윙 재즈", L"摇摆爵士", L"سوينغ جاز", L"Свинг-джаз", L"Swing-Jazz", L"Jazz swing", L"Swingjazz", L"Swing jazz", L"Swing caz");
	case SC_AS_BEBOP: return LL14(L"ビバップ", L"Bebop", L"Bebop", L"Bebop", L"Bebop", L"비밥", L"比波普", L"بيبوب", L"Бибоп", L"Bebop", L"Bebop", L"Bebop", L"Bebop", L"Bebop");
	case SC_AS_BLUES: return LL14(L"ブルース", L"Blues", L"Blues", L"Blues", L"Blues", L"블루스", L"布鲁斯", L"بلوز", L"Блюз", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues");
	case SC_AS_SLOWBLUES: return LL14(L"スローブルース", L"Slow blues", L"Blues lent", L"Blues lento", L"Blues lento", L"슬로우 블루스", L"慢布鲁斯", L"بلوز بطيء", L"Медленный блюз", L"Slow-Blues", L"Blues lento", L"Langzame blues", L"Wolny blues", L"Yavaş blues");
	case SC_AS_ROCKNROLL: return LL14(L"ロックンロール", L"Rock'n'roll", L"Rock'n'roll", L"Rock'n'roll", L"Rock'n'roll", L"로큰롤", L"摇滚乐", L"روك أند رول", L"Рок-н-ролл", L"Rock'n'Roll", L"Rock'n'roll", L"Rock-'n-roll", L"Rock'n'roll", L"Rock'n'roll");
	case SC_AS_GOSPEL: return LL14(L"ゴスペル", L"Gospel", L"Gospel", L"Gospel", L"Góspel", L"가스펠", L"福音", L"غوسبل", L"Госпел", L"Gospel", L"Gospel", L"Gospel", L"Gospel", L"Gospel");
	case SC_AS_SOUL: return LL14(L"ソウル", L"Soul", L"Soul", L"Soul", L"Soul", L"소울", L"灵魂乐", L"سول", L"Соул", L"Soul", L"Soul", L"Soul", L"Soul", L"Soul");
	case SC_AS_RNB: return LL14(L"R&B", L"R&B", L"R&B", L"R&B", L"R&B", L"R&B", L"节奏布鲁斯", L"آر أند بي", L"R&B", L"R&B", L"R&B", L"R&B", L"R&B", L"R&B");
	case SC_AS_NEOSOUL: return LL14(L"ネオソウル", L"Neo soul", L"Neo soul", L"Neo soul", L"Neo soul", L"네오 소울", L"新灵魂乐", L"نيو سول", L"Нео-соул", L"Neo-Soul", L"Neo soul", L"Neo soul", L"Neo soul", L"Neo soul");
	case SC_AS_COUNTRY: return LL14(L"カントリー", L"Country", L"Country", L"Country", L"Country", L"컨트리", L"乡村", L"كانتري", L"Кантри", L"Country", L"Country", L"Country", L"Country", L"Country");
	case SC_AS_PUNK: return LL14(L"パンク", L"Punk", L"Punk", L"Punk", L"Punk", L"펑크", L"朋克", L"بانك", L"Панк", L"Punk", L"Punk", L"Punk", L"Punk", L"Punk");
	case SC_AS_GRUNGE: return LL14(L"グランジ", L"Grunge", L"Grunge", L"Grunge", L"Grunge", L"그런지", L"垃圾摇滚", L"غرانج", L"Гранж", L"Grunge", L"Grunge", L"Grunge", L"Grunge", L"Grunge");
	case SC_AS_SKA: return LL14(L"スカ", L"Ska", L"Ska", L"Ska", L"Ska", L"스카", L"斯卡", L"سكا", L"Ска", L"Ska", L"Ska", L"Ska", L"Ska", L"Ska");
	case SC_AS_KAYOKYOKU: return LL14(L"歌謡曲", L"Kayokyoku", L"Kayokyoku", L"Kayokyoku", L"Kayokyoku", L"가요", L"歌谣曲", L"كايوكيوكو", L"Каёкёку", L"Kayokyoku", L"Kayokyoku", L"Kayokyoku", L"Kayokyoku", L"Kayokyoku");
	case SC_AS_ANISON: return LL14(L"アニソン", L"Anime song", L"Chanson d'anime", L"Canzone anime", L"Canción de anime", L"애니송", L"动漫歌曲", L"أغنية أنمي", L"Анисон", L"Anime-Song", L"Canção de anime", L"Animelied", L"Piosenka anime", L"Anime şarkısı");
	case SC_AS_CHIPTUNE: return LL14(L"チップチューン", L"Chiptune", L"Chiptune", L"Chiptune", L"Chiptune", L"칩튠", L"芯片音乐", L"تشيبتيون", L"Чиптюн", L"Chiptune", L"Chiptune", L"Chiptune", L"Chiptune", L"Chiptune");
	case SC_AS_TRAP: return LL14(L"トラップ", L"Trap", L"Trap", L"Trap", L"Trap", L"트랩", L"陷阱", L"تراب", L"Трэп", L"Trap", L"Trap", L"Trap", L"Trap", L"Trap");
	case SC_AS_DRILL: return LL14(L"ドリル", L"Drill", L"Drill", L"Drill", L"Drill", L"드릴", L"钻头", L"دريل", L"Дрилл", L"Drill", L"Drill", L"Drill", L"Drill", L"Drill");
	case SC_AS_AFROBEAT: return LL14(L"アフロビート", L"Afrobeat", L"Afrobeat", L"Afrobeat", L"Afrobeat", L"아프로비트", L"非洲节拍", L"أفروبيت", L"Афробит", L"Afrobeat", L"Afrobeat", L"Afrobeat", L"Afrobeat", L"Afrobeat");
	case SC_AS_SWING3: return LL14(L"ジャズワルツ", L"Jazz waltz", L"Valse jazz", L"Valzer jazz", L"Vals jazz", L"재즈 왈츠", L"爵士华尔兹", L"فالس جاز", L"Джаз-вальс", L"Jazz-Walzer", L"Valsa jazz", L"Jazzwals", L"Walc jazzowy", L"Caz valsi");
	case SC_AS_TANGO: return LL14(L"タンゴ", L"Tango", L"Tango", L"Tango", L"Tango", L"탱고", L"探戈", L"تانغو", L"Танго", L"Tango", L"Tango", L"Tango", L"Tango", L"Tango");
	case SC_AS_FLAMENCO: return LL14(L"フラメンコ", L"Flamenco", L"Flamenco", L"Flamenco", L"Flamenco", L"플라멩코", L"弗拉门戈", L"فلامنكو", L"Фламенко", L"Flamenco", L"Flamenco", L"Flamenco", L"Flamenco", L"Flamenko");
	case SC_AS_SURF: return LL14(L"サーフロック", L"Surf rock", L"Surf rock", L"Surf rock", L"Surf rock", L"서프 록", L"冲浪摇滚", L"سيرف روك", L"Сёрф-рок", L"Surf-Rock", L"Surf rock", L"Surfrock", L"Surf rock", L"Surf rock");
	case SC_AS_LOFI: return LL14(L"ローファイ", L"Lo-fi", L"Lo-fi", L"Lo-fi", L"Lo-fi", L"로파이", L"低保真", L"لو-فاي", L"Лоу-фай", L"Lo-fi", L"Lo-fi", L"Lo-fi", L"Lo-fi", L"Lo-fi");
	case SC_AS_DRUMSOLO: return LL14(L"ドラムソロ", L"Drum solo", L"Solo batterie", L"Assolo batteria", L"Solo batería", L"드럼 솔로", L"鼓独奏", L"منفرد طبول", L"Соло ударных", L"Drumsolo", L"Solo de bateria", L"Drumsolo", L"Solo perkusji", L"Davul solosu");
	case SC_AS_EDM: return LL14(L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"电子舞曲", L"إي دي إم", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM");
	case SC_AS_DUBSTEP: return LL14(L"ダブステップ", L"Dubstep", L"Dubstep", L"Dubstep", L"Dubstep", L"덥스텝", L"回响贝斯", L"دبستيب", L"Дабстеп", L"Dubstep", L"Dubstep", L"Dubstep", L"Dubstep", L"Dubstep");
	case SC_AS_SYNTHWAVE: return LL14(L"シンセウェイブ", L"Synthwave", L"Synthwave", L"Synthwave", L"Synthwave", L"신스웨이브", L"合成波", L"سينث ويف", L"Синтвейв", L"Synthwave", L"Synthwave", L"Synthwave", L"Synthwave", L"Synthwave");
	default: return L"?";
	}
}

/* テーマのジャンル見出し（ダイアログのグループ化用）。 */
const wchar_t* ScAutoStyleGroupName(int style)
{
	/* 分類名では括らない。見出しはそのスタイルの名前。 */
	return StyleNm(style);
}

const wchar_t* ScAutoStyleName(int style)
{
	return StyleNm(style);
}

static uint16_t Bits(const char* z)
{
	uint16_t m = 0;
	if (!z) return 0;
	for (int i = 0; z[i] && i < 16; i++)
		if (z[i] == 'x' || z[i] == 'X') m = (uint16_t)(m | (1u << i));
	return m;
}

static int SafeDeg(int d)
{
	/* 1根 2五 3根+8va 5三 7根-8va 8七/六。半音・増四は出さない。 */
	switch (d) {
	case 1: case 2: case 3: case 5: case 7: case 8: return d;
	default: return 1;
	}
}

static void Bass(uint8_t* b, const char* z)
{
	memset(b, 0, 16);
	if (!z) return;
	for (int i = 0; i < 16 && z[i]; i++)
		if (z[i] >= '1' && z[i] <= '9') b[i] = (uint8_t)SafeDeg(z[i] - '0');
}

/* テーマごとの定番 A/B/C。MML のドラム格子（x=打点）とベース度数。
   16ビートなら 16ビートのまま形だけ変える。ランダムなゴーストは足さない。 */
enum {
	FAM_16 = 0, FAM_8, FAM_METAL, FAM_REG, FAM_FLOOR, FAM_DNB, FAM_HOP,
	FAM_FUNK, FAM_DISCO, FAM_BALLAD, FAM_WALTZ, FAM_BOSSA, FAM_SAMBA,
	FAM_SHUF, FAM_JAZZ, FAM_BLUES, FAM_LATIN, FAM_SKA, FAM_TRAP,
	FAM_MARCH, FAM_PAD, FAM_AFRO, FAM_TANGO, FAM_COUNT
};

struct PatCell {
	const char *k, *sd, *hh, *oh, *rd, *cp, *rm;
	const char *bs, *ch;
	uint8_t div, slots, swing, power, bend, arp;
};

#define PC(k,sd,hh,oh,rd,cp,rm,bs,ch,div,slots,sw,pw,bd,arp) \
	{ k,sd,hh,oh,rd,cp,rm,bs,ch, (uint8_t)(div),(uint8_t)(slots),(uint8_t)(sw),(uint8_t)(pw),(uint8_t)(bd),(uint8_t)(arp) }

static const PatCell kCell[FAM_COUNT * 3] = {
	/* 16ビート A/B/C — 裏キックと16分ハイハット。コードは拍か裏だけ */
	PC("x.......x..x....","....x.......x...","xxxxxxxxxxxxxxxx","..............x.","","","", "1..2..5.1..2..8.","..x...x...x...x.",16,2,0,0,1,0),
	PC("x...x...x..x..x.","....x.......x...","x.x.x.x.x.x.x.x.","..x...........x.","","","", "1.5.1.2.1.5.8.2","x...x...x...x...",16,4,0,0,1,0),
	PC("x..x....x...x..x","....x..x....x...","xxxxxxxxxxxxxxxx","..............x.","","","", "1..1..2.1..5..2.","..x...x...x...x.",16,2,0,0,1,0),
	/* 8ビート */
	PC("x.......x.......","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1...2...1...5...","x...x...x...x...",16,4,0,0,0,0),
	PC("x...x...x...x...","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.1.2.2.1.1.5.5","x...x...x...x...",16,4,0,1,0,0),
	PC("x.......x..x....","....x.......x...","x...x...x...x...","","x.x.x.x.x.x.x.x.","","", "1.......2.......","x...x...x...x...",16,4,0,1,0,0),
	/* メタル */
	PC("x.x.x.x.x.x.x.x.","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.1.1.1.1.1.1.1","x.x.x.x.x.x.x.x.",16,2,0,1,0,0),
	PC("xxxxxxxxxxxxxxxx","....x.......x...","..x...x...x...x.","","","","", "1.1.1.1.2.2.1.1","x...x...x...x...",16,4,0,1,0,0),
	PC("x.x.x.x.x.x.x.x.","....x...x...x...","x.x.x.x.x.x.x.x.","","","","", "1.2.1.2.1.2.1.2","x.x.x.x.x.x.x.x.",16,2,0,1,0,0),
	/* レゲエ ワンドロップ */
	PC("........x.......","........x.......","..x...x...x...x.","","","","", "1.....2...1...5.","..x...x...x...x.",16,2,0,0,0,0),
	PC("x...............","....x.......x...","..x...x...x...x.","","","","", "1...1...2...5...","..x...x...x...x.",16,2,0,0,0,0),
	PC("........x...x...","........x.......","x.x.x.x.x.x.x.x.","","","","", "1.......2.......","..x...x...x...x.",16,2,0,0,0,0),
	/* 4つ打ち */
	PC("x...x...x...x...","","..x...x...x...x.","..............x.","","....x.......x...","", "1...1...1...1...","....x.......x...",16,4,0,0,0,0),
	PC("x...x...x...x...","","x.x.x.x.x.x.x.x.","..x...x...x...x.","","....x.......x...","", "1.5.1.5.1.5.1.8","x...............",16,0,0,0,0,0),
	PC("x...x...x...x...","","xxxxxxxxxxxxxxxx","","","....x.......x...","", "1.1.1.1.1.1.1.1","..x...x...x...x.",16,2,0,0,0,0),
	/* ドラムンベース */
	PC("x.........x.....","....x.......x...","xxxxxxxxxxxxxxxx","","","","", "1..1..5.1..2..8.","x.......x.......",16,4,0,0,0,0),
	PC("x.......x...x...","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.....5.1...2...","x.......x.......",16,4,0,0,0,0),
	PC("x...x.......x...","....x.......x...","xxxxxxxxxxxxxxxx","","","","", "1.1.1.5.1.2.1.8","x...............",16,0,0,0,0,0),
	/* ヒップホップ */
	PC("x.......x....x..","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.....5.1.....2.","x.......x.......",16,4,1,0,0,0),
	PC("x..x........x...","....x.......x...","..x...x...x...x.","","","","", "1...1.....5...2.","x.......x.......",16,4,1,0,1,0),
	PC("x.........x.x...","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.......5...2...","x...............",16,0,1,0,1,0),
	/* ファンク */
	PC("x..x....x.....x.","....x.......x...","xxxxxxxxxxxxxxxx","","","","", "1..5..1.2..1..5.","..x...x...x...x.",16,2,0,0,0,0),
	PC("x...x..x..x.....","....x..x....x...","x.x.x.x.x.x.x.x.","","","","", "1.1.5.1.8.1.2.5","..x...x...x...x.",16,2,0,0,0,0),
	PC("x.......x..x..x.","....x.......x.x.","xxxxxxxxxxxxxxxx","","","","", "1.....2.1...5...","x...x...x...x...",16,4,0,0,0,0),
	/* ディスコ */
	PC("x...x...x...x...","","x...x...x...x...","..x...x...x...x.","","","", "1.5.1.5.1.5.1.8","..x...x...x...x.",16,2,0,0,0,0),
	PC("x...x...x...x...","....x.......x...","..x...x...x...x.","","","","", "1...5...1...8...","....x.......x...",16,4,0,0,0,0),
	PC("x...x...x...x...","","x.x.x.x.x.x.x.x.","","","","", "1.1.5.5.1.1.8.8","x...x...x...x...",16,4,0,0,0,0),
	/* バラード */
	PC("x...............","....x.......x...","x.......x.......","","","","", "1.......2.......","x...............",16,0,0,0,1,0),
	PC("x.......x.......","....x.......x...","x...x...x...x...","","","","", "1...5...1...2...","x...x...x...x...",16,4,0,0,1,0),
	PC("x...............","","x...............","","","","", "1...............","x...............",16,0,0,0,1,0),
	/* ワルツ 3拍（12分割） */
	PC("x...........","....x...x...","x...x...x...","","","","", "1...2...5...","....x...x...",12,3,0,0,0,0),
	PC("x...........","....x...x...","....x...x...","","","","", "1...1...2...","x...x...x...",12,3,0,0,0,0),
	PC("x.....x.....","....x...x...","x...x...x...","","","","", "1...5...2...","....x...x...",12,3,0,0,0,0),
	/* ボサ */
	PC("","","x...x...x...x...","","","","x..x..x...x.x...", "1..2..1.5..2..7.","..x...x...x...x.",16,2,0,0,0,0),
	PC("x...........x...","","x...x...x...x...","","","","x.x...x.x...x...", "1.....5...1...2.","x...x...x...x...",16,4,0,0,0,0),
	PC("","","x.x.x.x.x.x.x.x.","","","","..x...x...x...x.", "1..1..2.1..5..2.","..x...x...x...x.",16,2,0,0,0,0),
	/* サンバ */
	PC("....x.......x...","","x...x...x...x...","","","","x.x.x.x.x.x.x.x.", "1.2.1.5.1.2.1.8","x...x...x...x...",16,4,0,0,0,0),
	PC("x.......x.......","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1..5..1..2..1...","..x...x...x...x.",16,2,0,0,0,0),
	PC("....x...x...x...","","x.x.x.x.x.x.x.x.","","","","x.x...x.x.x...x.", "1.1.2.5.1.1.2.8","x...x...x...x...",16,4,0,0,0,0),
	/* シャッフル */
	PC("x.....x.....","...x.....x..","x.xx.xx.xx.x","","","","", "1..1..2..5..","x.....x.....",12,3,1,0,0,0),
	PC("x.....x..x..","...x.....x..","x.x.x.x.x.x.","","","","", "1.....2.....","x.....x.....",12,3,1,0,0,0),
	PC("x...........","...x.....x..","x.xx.xx.xx.x","","","","", "1..5..1..2..","...x.....x..",12,3,1,0,0,0),
	/* ジャズ（ライド） */
	PC("x.......x.......","","","", "x.x.x.x.x.x.x.x.","","....x.......x...", "1..5..2..1..8...","x...x...x...x...",16,4,1,0,0,0),
	PC("x.....x...x.....","","","", "xxxxxxxxxxxxxxxx","","....x.......x...", "1.5.1.2.1.8.2.5","x.......x.......",16,4,1,0,0,0),
	PC("x...x...x...x...","","","", "x.x.x.x.x.x.x.x.","","....x.......x...", "1...2...1...5...","..x...x...x...x.",16,2,1,0,0,0),
	/* ブルース */
	PC("x.....x.....","...x.....x..","x.xx.xx.xx.x","","","","", "1..1..1..1..","x.....x.....",12,3,1,0,1,0),
	PC("x.....x.....","...x.....x..","x.x.x.x.x.x.","","","","", "1.1.1.5.1.1","x...x...x...",12,3,1,0,1,0),
	PC("x...........","...x.....x..","x.xx.xx.xx.x","","","","", "1.....5.....","x...........",12,0,1,0,1,0),
	/* ラテン */
	PC("x.....x.x.......","","x.x.x.x.x.x.x.x.","","","","..x..x...x...x..", "1..1..2.1..5..2.","..x...x...x...x.",16,2,0,0,0,0),
	PC("x.......x...x...","","x...x...x...x...","","","","..x...x...x..x..", "1...2...1...5...","x...x...x...x...",16,4,0,0,0,0),
	PC("x..x....x.....x.","","x.x.x.x.x.x.x.x.","","","","..x..x...x...x..", "1.2.1.5.1.2.1.8","..x...x...x...x.",16,2,0,0,0,0),
	/* スカ */
	PC("x.......x.......","....x.......x...","x...x...x...x...","","","","", "1...2...1...5...","..x...x...x...x.",16,2,0,0,0,0),
	PC("x...x...x...x...","....x.......x...","","..x...x...x...x.","","","", "1...1...2...5...","..x...x...x...x.",16,2,0,0,0,0),
	PC("x.......x.......","....x.......x...","..x...x...x...x.","","","","", "1.......2.......","..x...x...x...x.",16,2,0,0,0,0),
	/* トラップ（ハーフタイム＋808のしゃくり） */
	PC("x...........x...","........x.......","x.x.x.x.x.x.x.x.","..............x.","","","", "1...............","x...............",16,0,0,0,1,0),
	PC("x.......x.......","........x.......","xxxxxxxxxxxxxxxx","","","","", "1.....1.........","x...............",16,0,0,0,1,0),
	PC("x...x.......x...","........x.......","............xxxx","","","","", "1...........5...","x...............",16,0,0,0,1,0),
	/* マーチ */
	PC("x.......x.......","..x.x.x...x.x.x.","","","","","", "1...1...1...5...","x...x...x...x...",16,4,0,0,0,0),
	PC("x.......x.......","....x.......x...","x...x...x...x...","","","","", "1.......1.......","x...x...x...x...",16,4,0,0,0,0),
	PC("x...x...x...x...","....x.......x...","","","","","", "1...5...1...5...","x.......x.......",16,4,0,0,0,0),
	/* アンビエント */
	PC("","","x.......x.......","","","","", "1...............","x...............",16,0,0,0,1,0),
	PC("x...............","","x...............","","","","", "1.......5.......","x...............",16,0,0,0,1,0),
	PC("","","x...............","","","","", "1...............","x...............",16,0,0,0,0,0),
	/* アフロ */
	PC("x.....x.x.......","....x.......x...","x.x.x.x.x.x.x.x.","","","","..x..x...x...x..", "1..1..2.1..5..1.","..x...x...x...x.",16,2,0,0,0,0),
	PC("x.......x...x...","....x.......x...","x.x.x.x.x.x.x.x.","","","","..x...x...x..x..", "1...2...1...5...","..x...x...x...x.",16,2,0,0,0,0),
	PC("x..x..x...x..x..","....x.......x...","xxxxxxxxxxxxxxxx","","","","", "1.1.2.1.5.1.2.1","x...x...x...x...",16,4,0,0,0,0),
	/* タンゴ／フラメンコ */
	PC("x.......x.x.....","....x.......x...","x...x...x...x...","","","","", "1...2...1...5...","x...x...x...x...",16,4,0,0,0,0),
	PC("x...x...x...x...","....x.......x...","","..x...x...x...x.","","","", "1...1...2...7...","x...x...x...x...",16,4,0,0,0,0),
	PC("x.......x.......","....x.......x...","x.x.x.x.x.x.x.x.","","","","", "1.......2.......","..x...x...x...x.",16,2,0,0,0,0)
};
#undef PC

static int StyleFam(int style)
{
	switch (style) {
	case SC_AS_16BEAT: case SC_AS_CITYPOP: case SC_AS_ANISON: case SC_AS_KAYOKYOKU:
	case SC_AS_FUSION: case SC_AS_GAME: case SC_AS_CHIPTUNE:
		return FAM_16;
	case SC_AS_8BEAT: case SC_AS_ROCK: case SC_AS_PUNK: case SC_AS_GRUNGE:
	case SC_AS_SURF: case SC_AS_SLOWROCK: case SC_AS_COUNTRY: case SC_AS_ACOUSTIC:
		return FAM_8;
	case SC_AS_METAL: case SC_AS_DRUMSOLO:
		return FAM_METAL;
	case SC_AS_REGGAE: return FAM_REG;
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE: case SC_AS_EDM: case SC_AS_SYNTHWAVE:
		return FAM_FLOOR;
	case SC_AS_DNB: return FAM_DNB;
	case SC_AS_HIPHOP: case SC_AS_LOFI: case SC_AS_BREAKS: case SC_AS_RNB: case SC_AS_NEOSOUL:
		return FAM_HOP;
	case SC_AS_FUNK: case SC_AS_SOUL: case SC_AS_GOSPEL: return FAM_FUNK;
	case SC_AS_DISCO: return FAM_DISCO;
	case SC_AS_BALLAD: case SC_AS_ENKA: case SC_AS_ORCH: case SC_AS_PIANO: return FAM_BALLAD;
	case SC_AS_WALTZ: case SC_AS_SWING3: case SC_AS_POLKA: return FAM_WALTZ;
	case SC_AS_BOSSA: return FAM_BOSSA;
	case SC_AS_SAMBA: return FAM_SAMBA;
	case SC_AS_SHUFFLE: case SC_AS_ROCKNROLL: return FAM_SHUF;
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP: return FAM_JAZZ;
	case SC_AS_BLUES: case SC_AS_SLOWBLUES: return FAM_BLUES;
	case SC_AS_LATIN: case SC_AS_FLAMENCO: return FAM_LATIN;
	case SC_AS_SKA: return FAM_SKA;
	case SC_AS_TRAP: case SC_AS_DRILL: case SC_AS_DUBSTEP: return FAM_TRAP;
	case SC_AS_MARCH: case SC_AS_FANFARE: return FAM_MARCH;
	case SC_AS_AMBIENT: return FAM_PAD;
	case SC_AS_AFROBEAT: return FAM_AFRO;
	case SC_AS_TANGO: return FAM_TANGO;
	default: return FAM_16;
	}
}

static void PaintCell(const PatCell* c, StylePat* s)
{
	if (!c || !s) return;
	for (int i = 0; i < DV_COUNT; i++) s->drum[i] = 0;
	s->div = c->div ? c->div : 16;
	if (s->div > 16) s->div = 16;
	s->drum[DV_KICK] = Bits(c->k);
	s->drum[DV_SNARE] = Bits(c->sd);
	s->drum[DV_HAT] = Bits(c->hh);
	s->drum[DV_OHAT] = Bits(c->oh);
	s->drum[DV_RIDE] = Bits(c->rd);
	s->drum[DV_CLAP] = Bits(c->cp);
	s->drum[DV_RIM] = Bits(c->rm);
	Bass(s->bass, c->bs);
	s->chord = Bits(c->ch);
	s->chordSlots = c->slots;
	s->swing = c->swing;
	s->arp = c->arp;
	s->bend = c->bend;
	if (c->power) s->power = 1;
}

/* 各テーマ×パターンA〜Eの音色。GMは0始まり。ピアノ(0)はピアノ系だけ。
   列: ベース, コード, パッド, 拍, 裏拍, ギター, ストリングス, リード */
#define Z(b,c,p,a,h,g,s,l) { (uint8_t)(b),(uint8_t)(c),(uint8_t)(p),(uint8_t)(a),(uint8_t)(h),(uint8_t)(g),(uint8_t)(s),(uint8_t)(l) }
static const uint8_t kProg[][5][8] = {
	{ Z(33,4,89,27,90,25,48,81), Z(34,5,88,28,50,26,49,80), Z(36,7,91,29,62,27,52,87), Z(35,17,94,25,48,24,45,73), Z(38,90,95,81,88,30,61,62) },
	{ Z(33,27,48,4,25,29,49,56), Z(34,29,50,5,28,30,61,66), Z(32,25,89,0,24,27,40,73), Z(36,18,91,17,50,26,52,81), Z(35,4,49,26,90,25,48,65) },
	{ Z(34,16,89,25,50,24,48,66), Z(33,17,88,28,91,27,49,65), Z(35,18,94,26,48,25,52,73), Z(38,16,90,7,89,24,45,80), Z(36,19,91,4,50,28,61,56) },
	{ Z(38,90,88,81,80,62,50,87), Z(39,81,91,80,95,38,48,62), Z(38,88,94,87,90,81,52,80), Z(39,62,89,90,81,80,50,98), Z(38,95,91,81,88,87,49,62) },
	{ Z(38,90,88,4,81,62,50,87), Z(39,5,91,81,89,80,48,90), Z(38,88,94,7,95,62,52,81), Z(36,90,89,27,50,25,49,80), Z(39,81,95,4,88,87,61,62) },
	{ Z(38,90,91,81,88,62,50,87), Z(39,81,94,80,95,90,52,62), Z(38,88,89,87,91,81,48,80), Z(39,95,90,4,50,62,49,73), Z(38,62,91,81,88,80,61,87) },
	{ Z(38,89,88,81,90,27,48,87), Z(39,81,91,4,95,28,50,80), Z(36,90,94,7,88,62,49,81), Z(38,5,89,27,50,25,52,66), Z(39,88,95,81,91,30,48,62) },
	{ Z(36,5,89,27,90,28,48,81), Z(33,4,88,7,50,25,49,66), Z(38,90,91,81,95,62,52,80), Z(35,17,94,26,89,24,45,73), Z(37,7,50,28,91,27,48,65) },
	{ Z(36,7,16,27,90,28,48,66), Z(37,17,89,5,91,25,49,65), Z(33,18,88,4,50,26,52,56), Z(34,16,94,28,62,29,61,81), Z(35,19,91,7,48,24,45,73) },
	{ Z(34,90,88,4,81,27,50,62), Z(38,5,91,17,89,25,48,66), Z(33,16,94,7,95,28,49,56), Z(36,18,89,27,50,26,52,81), Z(39,62,90,81,88,30,61,87) },
	{ Z(34,29,48,27,25,30,49,56), Z(33,30,50,28,62,29,61,66), Z(32,25,89,4,24,27,40,73), Z(36,18,91,17,50,26,52,81), Z(35,27,49,5,90,24,48,65) },
	{ Z(34,30,61,29,62,48,52,81), Z(36,29,50,30,91,27,49,80), Z(33,62,88,18,95,25,61,87), Z(38,30,90,81,89,28,48,56), Z(37,29,94,7,50,26,45,66) },
	{ Z(32,0,49,4,89,24,48,73), Z(35,4,48,5,88,25,40,65), Z(33,1,52,6,94,26,45,56), Z(34,19,50,17,91,27,49,66), Z(38,90,89,81,95,28,61,80) },
	{ Z(32,0,48,4,89,24,49,73), Z(33,19,52,6,50,25,40,56), Z(35,4,49,5,88,26,45,65), Z(34,16,91,17,94,27,48,66), Z(42,1,89,11,90,46,52,71) },
	{ Z(33,4,89,26,88,24,48,65), Z(35,5,48,27,90,25,49,66), Z(32,11,94,7,50,28,45,73), Z(34,0,52,6,91,24,40,56), Z(36,17,89,4,95,26,61,81) },
	{ Z(34,90,88,27,81,25,50,66), Z(36,17,91,7,89,28,48,65), Z(33,16,94,4,50,26,49,56), Z(38,62,89,81,95,30,61,80), Z(32,11,48,12,90,24,45,73) },
	{ Z(33,26,48,4,89,27,49,66), Z(34,29,50,5,90,28,61,56), Z(32,25,89,0,24,27,40,65), Z(36,17,91,7,50,26,52,81), Z(35,4,94,11,88,24,45,73) },
	{ Z(32,61,48,56,62,19,52,73), Z(33,56,50,65,49,16,40,71), Z(34,62,91,66,89,18,61,56), Z(42,19,52,4,48,24,45,65), Z(35,16,94,17,50,27,49,81) },
	{ Z(34,21,48,4,89,24,49,65), Z(33,16,50,6,90,25,40,66), Z(32,19,91,11,88,27,52,73), Z(36,17,89,7,94,26,45,56), Z(35,22,49,5,50,28,61,81) },
	{ Z(33,5,89,27,90,25,48,81), Z(35,4,88,7,50,26,49,66), Z(34,17,94,28,91,24,52,80), Z(38,90,91,81,95,62,61,87), Z(36,16,89,4,88,27,45,73) },
	{ Z(38,80,88,81,87,62,50,90), Z(39,81,91,80,95,90,48,87), Z(38,87,94,4,89,7,52,80), Z(36,90,89,12,50,13,49,81), Z(33,6,48,11,91,24,45,73) },
	{ Z(42,48,52,40,49,46,61,73), Z(32,19,50,56,45,24,40,71), Z(43,52,94,65,48,46,61,72), Z(35,16,89,11,91,6,49,60), Z(33,0,49,4,88,25,45,68) },
	{ Z(35,88,89,4,94,24,49,80), Z(38,90,91,81,95,62,50,87), Z(32,6,48,11,52,46,45,73), Z(39,94,88,5,89,25,40,81), Z(33,19,50,17,91,27,61,66) },
	{ Z(36,5,89,27,90,28,48,81), Z(38,81,91,80,95,62,50,87), Z(34,7,88,4,50,25,49,66), Z(39,90,94,7,89,30,52,80), Z(33,17,48,26,91,24,45,65) },
	{ Z(34,16,89,27,90,25,48,66), Z(33,17,88,11,50,24,49,65), Z(36,90,91,7,95,28,52,81), Z(32,11,94,4,48,26,45,73), Z(35,19,49,6,91,46,40,56) },
	{ Z(33,19,48,65,89,24,49,66), Z(32,0,52,4,50,25,40,73), Z(35,16,89,6,94,27,45,56), Z(34,17,91,26,88,28,61,81), Z(38,90,49,5,95,7,48,80) },
	{ Z(35,5,89,26,90,27,48,66), Z(33,4,88,7,50,25,49,81), Z(36,17,94,11,91,28,52,65), Z(34,16,48,4,89,24,45,73), Z(38,90,91,81,95,62,61,87) },
	{ Z(32,0,48,24,89,25,49,73), Z(33,4,49,26,88,27,40,65), Z(35,6,52,11,50,46,45,66), Z(34,19,91,5,94,28,61,56), Z(36,17,89,7,90,24,48,81) },
	{ Z(33,0,48,4,89,1,49,73), Z(32,1,49,5,88,6,40,65), Z(35,4,52,2,50,7,45,66), Z(34,6,91,11,94,19,61,56), Z(38,5,89,17,95,16,48,81) },
	{ Z(32,61,62,56,48,19,52,73), Z(34,56,50,65,49,16,40,71), Z(33,62,91,66,89,18,61,60), Z(42,19,52,4,48,24,45,65), Z(36,16,94,17,50,27,49,81) },
	{ Z(34,29,48,4,89,27,49,56), Z(33,4,50,5,90,25,40,66), Z(35,0,49,26,88,24,45,73), Z(32,19,91,17,94,28,52,65), Z(36,18,89,7,50,30,61,81) },
	{ Z(32,0,48,26,89,4,49,66), Z(35,4,52,11,50,27,45,65), Z(33,5,89,7,88,25,40,56), Z(34,16,91,6,94,24,61,73), Z(36,17,49,2,90,28,48,81) },
	{ Z(34,61,48,56,62,26,52,66), Z(33,0,50,4,89,27,49,65), Z(32,16,91,11,94,25,40,73), Z(36,17,89,7,88,28,45,81), Z(35,5,52,6,90,24,61,56) },
	{ Z(33,0,48,26,89,4,49,66), Z(35,4,50,11,88,27,45,65), Z(32,5,91,7,94,25,40,73), Z(34,16,89,6,90,24,52,81), Z(36,17,49,2,50,28,61,56) },
	{ Z(34,29,48,26,89,27,49,66), Z(33,17,50,4,90,25,61,65), Z(32,0,89,7,88,24,40,56), Z(36,18,91,28,94,30,52,81), Z(35,5,49,11,50,26,45,73) },
	{ Z(35,4,89,26,88,27,49,66), Z(33,0,48,5,90,25,40,65), Z(32,17,52,7,94,24,45,73), Z(34,16,91,11,50,28,61,56), Z(36,19,49,6,89,46,48,81) },
	{ Z(34,29,48,26,89,27,49,66), Z(33,30,50,4,90,28,61,56), Z(32,25,89,0,24,27,40,65), Z(36,17,91,7,50,26,52,81), Z(35,18,94,5,88,24,45,73) },
	{ Z(33,16,48,4,89,27,49,66), Z(32,19,52,0,90,25,40,65), Z(34,17,91,5,94,26,61,56), Z(35,18,89,7,88,24,45,73), Z(36,21,50,6,95,28,48,81) },
	{ Z(36,5,89,27,90,16,48,66), Z(33,4,88,7,50,17,49,65), Z(34,18,91,28,94,25,52,56), Z(35,17,48,26,89,24,45,73), Z(38,90,95,81,91,62,61,80) },
	{ Z(35,5,89,4,90,27,48,81), Z(33,4,88,7,50,28,49,66), Z(38,90,91,81,95,62,52,80), Z(36,17,94,26,89,25,45,65), Z(39,91,48,5,88,24,61,87) },
	{ Z(35,5,89,4,90,26,48,66), Z(33,4,88,7,50,27,49,65), Z(36,17,94,11,91,25,52,81), Z(34,16,48,6,89,24,45,73), Z(38,90,95,81,88,62,61,80) },
	{ Z(32,25,48,26,89,24,49,66), Z(33,27,50,4,90,28,40,65), Z(34,105,91,6,88,29,45,73), Z(35,16,89,7,94,17,52,56), Z(36,18,49,5,50,30,61,81) },
	{ Z(34,30,61,29,62,27,48,81), Z(33,29,50,28,90,30,49,80), Z(36,62,91,18,95,25,52,87), Z(38,30,88,81,89,26,61,56), Z(37,17,94,7,50,24,45,66) },
	{ Z(34,30,48,29,89,27,49,81), Z(33,29,50,4,90,28,61,66), Z(35,0,91,5,94,25,40,73), Z(32,19,89,17,88,24,45,56), Z(38,90,95,81,50,62,52,80) },
	{ Z(34,16,89,27,90,25,48,66), Z(33,17,88,28,50,26,49,65), Z(36,18,91,7,94,24,52,56), Z(32,19,48,4,89,6,45,73), Z(38,90,95,81,62,30,61,80) },
	{ Z(33,4,89,27,90,25,48,65), Z(32,0,48,5,88,24,49,66), Z(35,19,52,6,94,26,40,73), Z(34,16,91,17,50,28,61,56), Z(38,90,89,81,95,7,45,80) },
	{ Z(33,4,89,27,90,25,48,81), Z(34,62,91,81,88,29,61,80), Z(38,90,94,5,95,30,50,87), Z(36,17,48,7,50,26,52,66), Z(35,16,89,4,91,24,45,73) },
	{ Z(38,80,88,81,87,12,50,90), Z(39,81,91,80,95,13,48,87), Z(38,87,94,4,89,7,52,62), Z(36,90,89,12,50,11,49,81), Z(33,6,48,11,91,24,45,73) },
	{ Z(38,89,88,81,90,62,50,87), Z(39,90,91,80,95,7,48,81), Z(36,5,94,4,89,27,49,66), Z(38,81,50,87,88,30,52,80), Z(35,17,48,26,91,25,45,65) },
	{ Z(38,81,90,80,95,62,50,87), Z(39,90,91,7,89,30,48,81), Z(36,5,88,4,94,27,49,66), Z(37,17,50,28,91,25,52,80), Z(35,16,89,26,48,24,45,73) },
	{ Z(34,16,89,27,90,25,48,66), Z(36,17,88,7,50,28,49,65), Z(33,18,91,11,94,24,52,56), Z(38,90,95,81,62,30,61,80), Z(32,19,48,4,89,6,45,73) },
	{ Z(32,0,48,26,89,4,49,66), Z(33,4,52,11,50,27,45,65), Z(35,5,91,7,88,25,40,73), Z(34,16,89,6,94,24,61,56), Z(36,19,49,2,90,46,48,81) },
	{ Z(33,16,48,26,89,24,49,65), Z(32,19,52,4,90,25,40,66), Z(34,21,91,6,94,27,45,73), Z(35,17,89,7,88,28,61,56), Z(42,0,50,11,49,46,48,71) },
	{ Z(32,24,48,27,89,25,49,65), Z(33,25,50,26,90,28,40,66), Z(34,16,91,6,94,29,45,73), Z(35,19,89,4,88,7,52,56), Z(36,18,49,11,50,30,61,81) },
	{ Z(34,27,48,29,89,25,49,66), Z(33,30,50,4,90,28,61,56), Z(36,17,91,7,94,26,52,81), Z(32,0,89,5,88,24,40,73), Z(38,90,95,81,62,62,50,80) },
	{ Z(35,5,89,4,90,26,48,81), Z(33,4,88,7,50,27,49,66), Z(32,0,94,11,91,24,45,73), Z(38,90,48,81,95,62,52,80), Z(36,17,49,6,89,25,40,65) },
	{ Z(34,30,61,29,62,48,52,81), Z(36,16,50,27,89,18,49,56), Z(33,17,91,7,94,25,45,66), Z(38,90,88,81,95,62,61,80), Z(32,0,48,4,90,24,40,73) },
	{ Z(38,90,88,81,80,62,50,87), Z(39,81,91,4,95,90,48,80), Z(38,62,94,87,89,7,52,81), Z(36,5,50,27,91,25,49,66), Z(39,95,89,80,88,30,61,62) },
	{ Z(38,81,90,80,95,62,50,87), Z(39,90,91,7,89,30,48,81), Z(36,62,88,4,94,27,49,80), Z(38,95,50,87,91,18,52,66), Z(35,17,48,26,89,25,45,73) },
	{ Z(38,90,88,4,81,62,50,87), Z(39,5,91,81,89,80,48,66), Z(33,16,94,27,95,25,49,80), Z(36,62,89,7,90,28,61,56), Z(35,17,50,26,88,24,45,73) }
};
#undef Z
static_assert(sizeof(kProg) / sizeof(kProg[0]) == SC_AS_COUNT, "kProg rows");

static int KitOf(int id, int pat)
{
	if (pat < 0) pat = 0;
	switch (id) {
	case SC_AS_METAL: case SC_AS_PUNK: case SC_AS_ROCK: case SC_AS_GRUNGE:
	case SC_AS_DRUMSOLO: case SC_AS_SURF:
		return pat < 2 ? 16 : 8;
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP: case SC_AS_SWING3:
	case SC_AS_BALLAD: case SC_AS_ENKA: case SC_AS_PIANO:
		return pat < 3 ? 32 : 40;
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE: case SC_AS_DNB:
	case SC_AS_TRAP: case SC_AS_DRILL: case SC_AS_EDM: case SC_AS_DUBSTEP:
	case SC_AS_SYNTHWAVE: case SC_AS_CHIPTUNE: case SC_AS_GAME:
		return pat < 2 ? 25 : 24;
	case SC_AS_ORCH: case SC_AS_FANFARE: case SC_AS_MARCH:
		return 48;
	default:
		return pat == 0 ? 0 : 8;
	}
}

/* スタイルは1件ずつ。ファミリーの使い回しはしない。
   ピアノ系 PC 0-7 は「ピアノ」だけ。他はギター、オルガン、ブラス、シンセ、弦。
   prog は ベース,コード,パッド,アルペジオ,ハモリ,対旋律,スタブ,カラー。
   progB は同じスタイルのもう一つの音色列（アレンジの偶奇）。 */
struct StyleDef {
	uint8_t div, swing, power, bend, arp, slots, kit;
	const char *kick, *snare, *clap, *hat, *ohat, *ride, *crash, *tom, *rim;
	const char *bass, *chord;
	uint8_t prog[8];
	uint8_t progB[8];
};
static const StyleDef kDef[SC_AS_COUNT] = {
	/* 16ビート */
	{ 16,0,0,1,0, 2,0,
	  "x.......x.x.....","....x.......x...","","xxxxxxxxxxxxxxxx","..............x.","","","","",
	  "1..3..5.1..8..2.","..x...x...x...x.",
	  {33,27,50,25,52,29,62,73},{34,28,49,26,53,30,63,75} },
	/* 8ビート */
	{ 16,0,0,0,0, 2,0,
	  "x.......x.......","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1...5...1...2...","x.x.x.x.x.x.x.x.",
	  {33,18,48,24,56,25,61,71},{32,17,49,26,57,28,62,68} },
	/* レゲエ */
	{ 16,0,0,0,0, 2,0,
	  "........x.......","........x.......","","..x...x...x...x.","","","","",".x...x...x...x..",
	  "8.....1...2...5.","..x..x....x..x..",
	  {34,25,49,12,66,28,62,73},{35,24,48,13,65,27,63,75} },
	/* テクノ */
	{ 16,0,0,0,1, 1,25,
	  "x...x...x...x...","","....x.......x...","..x...x...x...x.","..............x.","","","","",
	  "..1...8...1...5.","..x...x...x...x.",
	  {38,81,90,87,62,30,55,94},{39,82,91,86,63,29,54,95} },
	/* ハウス */
	{ 16,0,0,0,0, 2,25,
	  "x...x...x...x...","","....x.......x...","x.x.x.x.x.x.x.x.","..x...x...x...x.","","","","",
	  "1.5.1.2.1.5.8.2.","....x.......x...",
	  {38,18,50,12,52,27,63,87},{39,17,51,13,53,28,62,86} },
	/* トランス */
	{ 16,0,0,0,1, 1,25,
	  "x...x...x...x...","","","xxxxxxxxxxxxxxxx","","","............x...","","",
	  "1.1.1.1.5.5.5.5.","x...............",
	  {38,81,91,87,62,30,55,94},{39,82,90,86,63,29,54,95} },
	/* ドラムンベース */
	{ 16,0,0,0,0, 2,25,
	  "x.........x.....","....x.......x...","","xxxxxxxxxxxxxxxx","","","","..x.....x.......","",
	  "1..1..8.1..2..5.","x.......x.......",
	  {38,81,88,87,62,30,55,94},{39,82,89,86,63,29,54,93} },
	/* ヒップホップ */
	{ 16,1,0,1,0, 4,25,
	  "x.......x....x..","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1.....8.1.....2.","x.......x.......",
	  {36,28,89,25,52,27,62,11},{35,29,88,26,53,30,63,12} },
	/* ファンク */
	{ 16,0,0,0,0, 2,0,
	  "x..x....x.....x.","....x.x...x...x.","","xxxxxxxxxxxxxxxx","..x.......x.....","","","","",
	  "1..5.1.8.2.1..3.","..x...x...x...x.",
	  {34,28,50,26,66,27,62,75},{36,29,51,25,65,30,63,71} },
	/* ディスコ */
	{ 16,0,0,0,0, 2,0,
	  "x...x...x...x...","","","x...x...x...x...","......x.......x.","","","","",
	  "1.1.5.5.8.8.2.2.","..x...x...x...x.",
	  {33,49,50,27,62,25,61,87},{34,48,51,28,63,26,56,86} },
	/* ロック */
	{ 16,0,1,0,0, 2,0,
	  "x.......x.x.....","....x.......x...","","","","x.x.x.x.x.x.x.x.","","","",
	  "1.1.5.5.1.1.2.2.","x.x.x.x.x.x.x.x.",
	  {34,30,48,29,62,27,61,29},{33,29,49,30,63,28,56,25} },
	/* メタル */
	{ 16,0,1,0,0, 1,0,
	  "xxxxxxxxxxxxxxxx","....x.......x...","","","","xxxxxxxxxxxxxxxx","","x.......x.......","",
	  "1.1.1.1.1.1.1.1.","xxxxxxxxxxxxxxxx",
	  {34,30,48,29,55,31,61,87},{33,29,49,30,54,27,62,86} },
	/* バラード */
	{ 16,0,0,1,0, 4,0,
	  "x...............","","","","","x.......x.......","","","....x.......x...",
	  "1.......5.......","x...............",
	  {32,48,49,24,73,25,56,46},{33,50,48,26,75,27,57,45} },
	/* ワルツ */
	{ 12,0,0,0,0, 3,0,
	  "x...........","....x...x...","","x...x...x...","","","","","",
	  "1...5...5...","....x...x...",
	  {32,21,48,11,73,24,56,22},{33,23,49,12,75,25,57,21} },
	/* ボサノバ */
	{ 16,0,0,0,0, 2,0,
	  "","","","","x...x...x...x...","","","","x..x..x...x.x...",
	  "1..5..1.8..2..7.","..x...x...x...x.",
	  {32,24,49,11,73,25,62,12},{33,25,48,12,75,26,56,13} },
	/* サンバ */
	{ 16,0,0,0,1, 1,0,
	  "....x.......x...","xxxxxxxxxxxxxxxx","","x.x.x.x.x.x.x.x.","","","","....x.......x...","",
	  "1.1.5.2.1.3.5.8.","x..x..x..x..x..x",
	  {34,25,49,12,62,27,61,73},{33,24,48,13,63,28,56,75} },
	/* シャッフル */
	{ 12,1,0,0,0, 3,0,
	  "x.....x.....","...x.....x..","","x.xx.xx.xx.x","","","","","",
	  "1..1..5..5..","x.....x.....",
	  {33,16,48,26,66,25,62,11},{34,17,49,27,65,28,63,12} },
	/* マーチ */
	{ 16,0,0,0,0, 3,48,
	  "x.......x.......","..x.x.x...x.x.x.","","","","","","x...............","",
	  "1...1...5...1...","x...x...x...x...",
	  {32,56,48,73,71,25,61,60},{33,57,49,72,68,26,62,59} },
	/* ポルカ */
	{ 16,0,0,0,0, 2,0,
	  "x.......x.......","....x.......x...","","..x.x.....x.x...","","","","","",
	  "1.......5.......","..x.x.x...x.x.x.",
	  {33,21,48,12,73,24,56,22},{32,23,49,13,75,25,57,21} },
	/* シティポップ */
	{ 16,0,0,1,0, 2,0,
	  "x.......x..x....","....x.......x...","","xxxxxxxxxxxxxxxx","..............x.","","","","",
	  "1..5..8.1.2..5.3","..x...x...x...x.",
	  {35,27,88,25,52,29,62,87},{34,28,89,26,53,30,63,86} },
	/* ゲーム */
	{ 16,0,0,0,1, 2,0,
	  "x..x....x.......","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1.5.8.3.1.5.2.3.","x.x.....x.x.....",
	  {38,80,88,87,62,27,55,81},{39,81,89,86,63,30,54,82} },
	/* オーケストラ */
	{ 16,0,0,0,0, 8,48,
	  "x...............","","","","","","x...........x...","","",
	  "1...............","x...........x...",
	  {32,48,49,46,73,60,56,45},{33,49,50,45,72,57,61,46} },
	/* アンビエント */
	{ 16,0,0,0,0, 8,0,
	  "","","","x...............","","","","","",
	  "1...............","x...............",
	  {32,89,88,11,73,24,52,94},{33,90,91,12,75,25,53,95} },
	/* ブレイクビーツ */
	{ 16,0,0,0,0, 2,0,
	  "x.x.....x..x....","....x.....x.x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1.....8.1...5...","x.......x...x...",
	  {36,28,89,25,52,27,62,81},{35,29,88,26,53,30,63,87} },
	/* ラテン */
	{ 16,0,0,0,1, 2,0,
	  "x.....x.x.......","","","","x.x.x.x.x.x.x.x.","","","....x.......x...","..x..x...x...x..",
	  "1..1..5.8.1...2.","x..xx..xx..xx..x",
	  {34,24,49,12,62,25,61,73},{33,25,48,13,63,27,56,75} },
	/* 演歌 */
	{ 16,0,0,1,0, 4,0,
	  "x.......x.......","....x.......x...","","x...x...x...x...","","","","","",
	  "1.......2.......","x...............",
	  {32,107,48,73,72,24,56,75},{33,108,49,74,79,25,57,76} },
	/* フュージョン */
	{ 16,0,0,0,0, 2,0,
	  "x..x....x.x...x.","....x.x.....x.x.","","","","xxxxxxxxxxxxxxxx","","","",
	  "1.1.5.8.2.1.3.1.","x.....x...x.....",
	  {35,26,50,11,66,27,62,73},{34,27,51,12,65,28,63,75} },
	/* アコースティック */
	{ 16,0,0,0,0, 2,0,
	  "x.......x.......","","","","x...x...x...x...","","","","....x.......x...",
	  "1.......5.......","x...x...x...x...",
	  {32,24,48,25,73,26,56,11},{33,25,49,27,75,28,57,12} },
	/* ピアノ */
	{ 16,0,0,0,1, 4,0,
	  "x.......x.......","","","","","","","","....x.......x...",
	  "1...5...1...2...","x...x...x...x...",
	  {32,0,48,11,73,0,56,46},{33,0,49,12,74,0,57,45} },
	/* ファンファーレ */
	{ 16,0,0,0,0, 3,48,
	  "x.......x.......","x.x.x.x.x.x.x.x.","","","","","x...............","","",
	  "1.....5.1...8...","x...x...x...x...",
	  {32,56,48,73,71,60,61,72},{33,57,49,72,68,59,62,76} },
	/* スローロック */
	{ 16,0,1,1,0, 4,0,
	  "x.......x.x.....","....x.......x...","","","","x...x...x...x...","","","",
	  "1.......5.......","x.......x.......",
	  {34,29,48,27,62,25,61,30},{33,30,49,28,63,26,56,29} },
	/* ジャズ */
	{ 12,1,0,1,0, 2,0,
	  "x...........","","","...x.....x..","","x..x.xx..x.x","","","......x.....",
	  "1..5..2..8..","..x.....x...",
	  {32,26,49,11,66,27,56,73},{33,25,48,12,65,28,57,75} },
	/* スウィング */
	{ 12,1,0,0,0, 1,0,
	  "x.....x.....","...x.....x..","","","","x..x.xx..x.x","","","",
	  "1..8..5..2..","x..x..x..x..",
	  {32,61,48,11,66,26,56,73},{33,62,49,12,65,27,57,75} },
	/* ビバップ */
	{ 12,1,0,0,1, 1,0,
	  "x..x..x...x.","..x...x.x..x","","...x.....x..","","x..x.xx..x.x","","","",
	  "1.5.8.2.3.7.","..x.....x...",
	  {32,11,49,12,66,26,56,73},{33,12,48,13,65,27,57,75} },
	/* ブルース */
	{ 16,1,0,1,0, 2,0,
	  "x.......x....x..","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1.3.5.8.8.5.3.1.","..x...x...x...x.",
	  {33,16,48,26,66,25,62,11},{34,17,49,27,65,28,63,12} },
	/* スローブルース */
	{ 12,1,0,1,1, 3,0,
	  "x.....x.....","...x.....x..","","x.xx.xx.xx.x","","","","","",
	  "1..3..5..8..","x.....x.....",
	  {32,18,49,26,66,25,56,11},{33,16,48,27,65,28,57,12} },
	/* ロックンロール */
	{ 12,1,0,0,0, 1,0,
	  "x.....x.....","...x.....x..","","x.x.x.x.x.x.","","","","","",
	  "1..1..5..8..","x..x..x..x..",
	  {33,25,48,29,66,27,62,11},{34,24,49,30,65,28,63,12} },
	/* ゴスペル */
	{ 16,0,0,1,1, 4,0,
	  "x.......x.x.....","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1..5..3.8..2..5.","x...x...x...x...",
	  {32,19,48,24,52,25,61,73},{33,16,49,27,53,26,62,75} },
	/* ソウル */
	{ 16,0,0,0,0, 2,0,
	  "x.....x.x.......","....x.......x...","","..x...x...x...x.","","","","....x.......x...","",
	  "1..1.5..1..8.3.2","..x...x...x...x.",
	  {33,27,50,26,66,25,62,73},{34,28,51,25,65,29,63,75} },
	/* R&B */
	{ 16,1,0,1,0, 4,25,
	  "x......x..x.....","....x.......x...","....x.......x...","x.x.x.x.x.x.x.x.","","","","","",
	  "1.....5.1...8...","x.......x.......",
	  {38,89,88,25,52,27,62,11},{39,90,91,26,53,28,63,12} },
	/* ネオソウル */
	{ 16,1,0,1,0, 4,0,
	  "x....x.x..x.....","....x.......x...","","x.xxx.x.xx.xx.x.","","","","","",
	  "1....58.2....5.3","x.......x...x...",
	  {35,26,89,11,66,27,62,73},{34,27,88,12,65,28,63,75} },
	/* カントリー */
	{ 16,0,0,0,0, 2,0,
	  "x...x...x...x...","....x.......x...","","xxxxxxxxxxxxxxxx","","","","","",
	  "1...5...1...8...","..x...x...x...x.",
	  {32,25,48,27,73,26,62,11},{33,24,49,28,75,29,61,12} },
	/* パンク */
	{ 16,0,1,0,0, 1,0,
	  "x.x.x.x.x.x.x.x.","....x.......x...","","xxxxxxxxxxxxxxxx","","","","","",
	  "1.1.1.1.1.1.1.1.","x.x.x.x.x.x.x.x.",
	  {34,30,48,29,62,31,61,87},{33,29,49,30,63,27,56,86} },
	/* グランジ */
	{ 16,0,1,0,0, 4,0,
	  "x.......x..x....","....x.......x...","","","","x...x...x...x...","","","",
	  "1...1...1.5.8...","x.......x.......",
	  {34,29,48,30,62,27,61,31},{33,30,49,29,63,28,56,25} },
	/* スカ */
	{ 16,0,0,0,0, 2,0,
	  "........x.......","....x.......x...","","..x...x...x...x.","","","","","",
	  "1.5.8.2.3.2.5.1.","..x...x...x...x.",
	  {33,27,49,25,66,28,62,73},{34,26,48,24,65,29,63,75} },
	/* 歌謡曲 */
	{ 16,0,0,1,0, 4,0,
	  "x.......x.x.....","....x.......x...","","x...x...x...x...","","","","","",
	  "1..5..3.8..2..5.","x...x...x...x...",
	  {32,48,49,24,73,25,56,107},{33,50,48,26,75,27,57,108} },
	/* アニソン */
	{ 16,0,0,1,0, 2,0,
	  "x..x..x.x.x..x..","....x.......x...","","xxxxxxxxxxxxxxxx","..............x.","","","","",
	  "1.1.5.5.8.8.2.2.","x.x.x.x.x.x.x.x.",
	  {38,81,88,87,62,27,55,80},{39,82,89,86,63,30,54,81} },
	/* チップチューン */
	{ 16,0,0,0,1, 1,0,
	  "x.......x.......","....x.......x...","","x.x.x.x.x.x.x.x.","","","","","",
	  "1.1.5.5.1.1.5.5.","xxxxxxxxxxxxxxxx",
	  {80,81,88,87,82,38,62,86},{81,82,89,86,80,39,63,87} },
	/* トラップ */
	{ 16,0,0,1,0, 4,25,
	  "x.....x...x.....","........x.......","","x.x.x.xxx.x.xxxx","","","","","",
	  "1.......1.....1.","x...............",
	  {38,89,88,81,62,30,55,94},{39,90,91,87,63,29,54,95} },
	/* ドリル */
	{ 16,0,0,1,0, 2,25,
	  "x.....x..x..x...","........x.......","","x.xx.x.xx.xx.x.x","","","","","",
	  "1.......8.....7.","......x.......x.",
	  {38,81,90,87,62,30,55,94},{39,82,91,86,63,29,54,95} },
	/* アフロビート */
	{ 16,0,0,0,1, 2,0,
	  "x..x..x...x.x...","","","","x.xxx.xxx.xxx.xx","","","....x.......x...","..x..x...x...x..",
	  "1..1.5.1..5.2..8","x..x..x..x..x..x",
	  {34,27,49,12,66,25,62,73},{33,28,48,13,65,26,61,75} },
	/* ジャズワルツ */
	{ 12,1,0,0,0, 1,0,
	  "x...........","","","...x.....x..","","x..x..x..x..","","","",
	  "1...5...3...","...x..x..x..",
	  {32,26,48,11,73,25,56,71},{33,27,49,12,75,24,57,68} },
	/* タンゴ */
	{ 16,0,0,0,1, 1,0,
	  "x..x..x.x.......","","","","","","","","............x...",
	  "1..1..1.5.......","x..x..x.x...x...",
	  {32,23,48,11,73,24,56,65},{33,21,49,12,75,25,57,66} },
	/* フラメンコ */
	{ 16,0,0,0,1, 1,0,
	  "","","x..x..x.x.x.x.x.","","","","","","..x..x...x...x..",
	  "1..1..5.1..2..5.","x..x..x.x.x.x.x.",
	  {24,25,48,12,73,27,62,11},{25,24,49,13,75,26,61,12} },
	/* サーフ */
	{ 16,0,1,0,0, 2,0,
	  "x.......x.......","....x.......x...","","xxxx.xxxx.xxxx.x","","","","..........x.x...","",
	  "1.5.1.5.8.2.8.2.","x.x.....x.x.....",
	  {34,29,48,27,62,30,61,25},{33,30,49,28,63,31,56,24} },
	/* ローファイ */
	{ 16,1,0,0,1, 4,0,
	  "x.......x....x..","....x.......x...","","x...x...x...x...","","","","","",
	  "1.....5.1.....3.","x...............",
	  {33,11,89,25,52,27,62,12},{32,12,88,26,53,28,63,13} },
	/* ドラムソロ */
	{ 16,0,1,0,0, 0,0,
	  "x.x..x..x.x..x..","..x.x..xx..x.x.x","","xxxxxxxxxxxxxxxx","","","x...............",".x...x.x..x.x.x.","",
	  "1...............","",
	  {32,48,49,11,73,117,56,47},{33,50,48,12,75,116,57,117} },
	/* EDM */
	{ 16,0,0,0,0, 4,25,
	  "x...x...x...x...","","....x.......x...","xxxxxxxxxxxxxxxx","x...............","","","","",
	  "1...1...5...8...","x.......x.......",
	  {38,81,90,87,62,30,55,94},{39,82,91,86,63,29,54,95} },
	/* ダブステップ */
	{ 16,0,0,0,0, 4,25,
	  "x...............","........x.......","","x...x...x...x...","","","","","",
	  "1.......1.......","........x.......",
	  {38,81,90,87,62,30,55,94},{39,82,91,86,63,29,54,95} },
	/* シンセウェイブ */
	{ 16,0,0,0,1, 4,25,
	  "x.......x.......","....x.......x...","","x.x.x.x.x.x.x.x.","","","","..............xx","",
	  "1.1.1.1.8.8.8.8.","x.......x.......",
	  {38,88,91,81,62,30,55,87},{39,89,90,82,63,29,54,86} },
};
static_assert(sizeof(kDef) / sizeof(kDef[0]) == SC_AS_COUNT, "kDef");

static void ApplyStyleDef(int id, StylePat* s)
{
	if (!s) return;
	if (id < 0 || id >= SC_AS_COUNT) id = 0;
	const StyleDef& d = kDef[id];
	s->div = d.div ? d.div : 16;
	s->swing = d.swing;
	s->power = d.power;
	s->bend = d.bend;
	s->arp = d.arp;
	s->chordSlots = d.slots;
	s->progKit = d.kit;
	s->drum[DV_KICK] = Bits(d.kick);
	s->drum[DV_SNARE] = Bits(d.snare);
	s->drum[DV_CLAP] = Bits(d.clap);
	s->drum[DV_HAT] = Bits(d.hat);
	s->drum[DV_OHAT] = Bits(d.ohat);
	s->drum[DV_RIDE] = Bits(d.ride);
	s->drum[DV_CRASH] = Bits(d.crash);
	s->drum[DV_TOM] = Bits(d.tom);
	s->drum[DV_RIM] = Bits(d.rim);
	Bass(s->bass, d.bass);
	s->chord = Bits(d.chord);
	for (int i = 0; i < 8; i++) s->progAll[i] = d.prog[i];
	s->progBass = d.prog[0];
	s->progChord = d.prog[1];
}

/* 同じ楽器族の中でアレンジごとに隣の音色へずらす。ピアノ 0〜7 には落とさない。 */
/* 52〜55 は声。動くパートに置くと幽霊ボイスになる。 */
static int NoGhost(int p, int role)
{
	if (p >= 52 && p <= 55) {
		static const int alt[4][8] = {
			{ 33, 27, 89, 25, 11, 29, 62, 80 },
			{ 34, 28, 90, 26, 12, 30, 63, 81 },
			{ 35, 29, 48, 27, 13, 31, 61, 82 },
			{ 36, 30, 49, 28, 16, 25, 56, 87 }
		};
		return alt[p - 52][role & 7];
	}
	if ((p == 91 || p == 94 || p == 95) && role != 2)
		return (role == 7) ? 81 : 89;
	return p;
}

static int KinTone(int base, int pat, int role)
{
	if (base < 0) base = 0;
	if (base > 127) base = 127;
	if (base <= 7) {
		static const int kP[8] = { 0, 4, 5, 6, 2, 1, 3, 7 };
		return kP[(base + (pat < 0 ? 0 : pat)) & 7];
	}
	if (pat < 2) return NoGhost(base, role);
	if (pat >= 8) {
		static const int kCousin[8][8] = {
			{ 38, 39, 43, 34, 33, 35, 36, 32 },
			{ 18, 19, 16, 17, 27, 25, 30, 28 },
			{ 88, 89, 90, 50, 48, 49, 51, 89 },
			{ 80, 81, 82, 87, 62, 71, 11, 12 },
			{ 11, 12, 13, 26, 27, 28, 16, 17 },
			{ 24, 25, 26, 27, 28, 29, 30, 31 },
			{ 48, 49, 61, 62, 56, 57, 45, 46 },
			{ 80, 81, 87, 73, 75, 56, 68, 71 }
		};
		int p = kCousin[role & 7][(pat + role) & 7];
		if (p <= 7) p = base;
		return NoGhost(p, role);
	}
	static const int kAlt[8] = { 0, 1, -1, 2, 3, -2, 4, -3 };
	int d = kAlt[(pat + role * 3) & 7];
	int fam = base & ~7;
	int p = fam + ((base + d) & 7);
	if (p <= 7 && base > 7) p = base;
	if (p < 0 || p > 127) p = base;
	return NoGhost(p, role);
}

static void FillTone(int id, int pat, StylePat* s)
{
	if (!s) return;
	if (id < 0 || id >= SC_AS_COUNT) id = 0;
	if (pat < 0) pat = 0;
	const uint8_t* r = (pat & 1) ? kDef[id].progB : kDef[id].prog;
	for (int i = 0; i < 8; i++) s->progAll[i] = (uint8_t)KinTone(r[i], pat, i);
	s->progBass = s->progAll[0];
	s->progChord = s->progAll[1];
	s->progKit = kDef[id].kit;
	/* キットもアレンジで隣へ。0 のままにはしない。 */
	if (pat >= 2) {
		static const int kKit[16] = { 0, 8, 16, 24, 25, 32, 40, 48, 1, 9, 17, 26, 27, 33, 41, 49 };
		s->progKit = kKit[(kDef[id].kit + pat) & 15];
	}
}

/* スタイルごとの基準テンポ。120固定にしない。並びは SC_AS_* と同じ。 */
static const uint8_t kBpm[SC_AS_COUNT] = {
	126, 120, 80, 132, 124, 138, 174, 92, 110, 118,
	128, 148, 72, 96, 136, 104, 120, 120, 126, 108,
	144, 80, 70, 136, 112, 76, 118, 96, 72, 112,
	70,
	132, 156, 196, 84, 56, 168, 100, 102, 78, 84,
	114, 176, 96, 148, 108, 164, 148, 140, 140, 112,
	156, 128, 120, 160, 78, 120, 128, 140, 96
};
static_assert(sizeof(kBpm) / sizeof(kBpm[0]) == SC_AS_COUNT, "kBpm");

static int StyleBpm(int id)
{
	if (id < 0 || id >= SC_AS_COUNT) id = 0;
	int b = kBpm[id];
	if (b < 40) b = 40;
	return b;
}

static void ApplyThemePattern(int id, int var, StylePat* s)
{
	if (var < 0) var = 0;
	/* A〜E。格子はファミリーの定番3種を回し、16小節の形は本体側で分ける。 */
	var %= 5;
	int fam = StyleFam(id);
	if (fam < 0 || fam >= FAM_COUNT) fam = FAM_16;
	PaintCell(&kCell[fam * 3 + (var % 3)], s);
	if (id == SC_AS_METAL || id == SC_AS_PUNK || id == SC_AS_ROCK || id == SC_AS_GRUNGE
		|| id == SC_AS_SURF || id == SC_AS_SLOWROCK || id == SC_AS_DRUMSOLO)
		s->power = 1;
	if (id == SC_AS_ENKA || id == SC_AS_BALLAD || id == SC_AS_RNB || id == SC_AS_NEOSOUL
		|| id == SC_AS_CITYPOP || id == SC_AS_KAYOKYOKU || id == SC_AS_BLUES || id == SC_AS_SLOWBLUES
		|| id == SC_AS_GOSPEL || id == SC_AS_TRAP || id == SC_AS_DRILL || id == SC_AS_ANISON)
		s->bend = 1;
	if (id == SC_AS_GAME || id == SC_AS_CHIPTUNE) s->arp = 1;
	if (id == SC_AS_FANFARE) s->drum[DV_CRASH] = 1;
	FillTone(id, var, s);
}

static void LoadStyle(int id, int var, StylePat* s)
{
	memset(s, 0, sizeof(*s));
	s->div = 16;
	s->chordSlots = 1;
	s->progBass = 33;
	s->progChord = 0;
	ApplyStyleDef(id, s);
	FillTone(id, var, s);
}

static int TonicPc(int keySig); /* 下で定義。進行記述用に前方宣言。 */

/* --- コード進行プロファイル -------------------------------------------------
   off = 主音からの半音。type = 上の CH_*。小節 index をこの配列で巡回し、
   既存ノートが無い（＝コードを拾えない）小節や書き換え対象の土台に使う。
   「コード運び」をテーマに合わせることで、無音からでも破綻しない伴奏になる。 */
struct ProgStep { int8_t off; uint8_t type; };
struct ProgRef { const ProgStep* p; int len; };

static const uint8_t kHarm[SC_AS_COUNT][2][8] = {
	{ { 0x00,0x19,0x12,0x07,0x00,0x05,0x07,0x19 }, { 0x00,0x07,0x19,0x14,0x05,0x00,0x12,0x07 } },
	{ { 0x00,0x05,0x07,0x05,0x00,0x12,0x07,0x00 }, { 0x00,0x0A,0x05,0x00,0x07,0x05,0x07,0x00 } },
	{ { 0x10,0x0A,0x10,0x15,0x10,0x17,0x0A,0x10 }, { 0x10,0x15,0x17,0x15,0x10,0x0A,0x15,0x10 } },
	{ { 0x10,0x10,0x15,0x17,0x10,0x08,0x17,0x10 }, { 0x10,0x17,0x10,0x15,0x08,0x0A,0x10,0x17 } },
	{ { 0x10,0x08,0x03,0x0A,0x10,0x15,0x08,0x17 }, { 0x10,0x17,0x08,0x0A,0x15,0x10,0x17,0x10 } },
	{ { 0x10,0x08,0x03,0x0A,0x15,0x08,0x0A,0x10 }, { 0x10,0x17,0x08,0x03,0x0A,0x15,0x17,0x10 } },
	{ { 0x10,0x15,0x08,0x17,0x10,0x08,0x15,0x07 }, { 0x10,0x17,0x15,0x08,0x0A,0x10,0x17,0x10 } },
	{ { 0x10,0x15,0x10,0x07,0x10,0x08,0x15,0x17 }, { 0x10,0x17,0x10,0x15,0x08,0x17,0x10,0x10 } },
	{ { 0x40,0x40,0x45,0x27,0x32,0x27,0x40,0x39 }, { 0x40,0x45,0x40,0x27,0x32,0x27,0x40,0x40 } },
	{ { 0x00,0x05,0x00,0x07,0x19,0x12,0x07,0x00 }, { 0x00,0x19,0x05,0x07,0x12,0x07,0x00,0x05 } },
	{ { 0x00,0x0A,0x05,0x00,0x07,0x05,0x00,0x07 }, { 0x00,0x05,0x07,0x05,0x0A,0x05,0x00,0x07 } },
	{ { 0x70,0x70,0x78,0x7A,0x70,0x75,0x77,0x70 }, { 0x70,0x7A,0x78,0x77,0x70,0x75,0x78,0x7A } },
	{ { 0x40,0x39,0x45,0x27,0x32,0x27,0x34,0x39 }, { 0x40,0x45,0x32,0x27,0x39,0x45,0x27,0x40 } },
	{ { 0x00,0x05,0x07,0x00,0x00,0x12,0x07,0x00 }, { 0x00,0x07,0x19,0x05,0x00,0x05,0x07,0x00 } },
	{ { 0x40,0x22,0x32,0x27,0x40,0x39,0x32,0x27 }, { 0x40,0x25,0x34,0x29,0x32,0x27,0x40,0x39 } },
	{ { 0x00,0x05,0x07,0x0A,0x00,0x07,0x05,0x00 }, { 0x00,0x12,0x07,0x00,0x05,0x07,0x0A,0x00 } },
	{ { 0x20,0x25,0x20,0x27,0x25,0x20,0x27,0x20 }, { 0x20,0x20,0x25,0x20,0x27,0x20,0x25,0x27 } },
	{ { 0x00,0x07,0x00,0x05,0x00,0x07,0x00,0x00 }, { 0x00,0x05,0x00,0x07,0x00,0x19,0x05,0x00 } },
	{ { 0x00,0x07,0x00,0x07,0x00,0x05,0x07,0x00 }, { 0x00,0x05,0x07,0x07,0x00,0x07,0x00,0x00 } },
	{ { 0x45,0x27,0x34,0x39,0x32,0x27,0x40,0x39 }, { 0x40,0x39,0x32,0x27,0x45,0x34,0x32,0x27 } },
	{ { 0x10,0x08,0x03,0x0A,0x15,0x07,0x10,0x10 }, { 0x10,0x15,0x17,0x08,0x03,0x0A,0x10,0x07 } },
	{ { 0x00,0x07,0x19,0x14,0x05,0x00,0x05,0x07 }, { 0x00,0x05,0x07,0x19,0x12,0x07,0x00,0x00 } },
	{ { 0x40,0x40,0x45,0x50,0x40,0x39,0x45,0x40 }, { 0x40,0x32,0x45,0x40,0x50,0x39,0x45,0x40 } },
	{ { 0x10,0x17,0x08,0x0A,0x10,0x15,0x08,0x17 }, { 0x10,0x08,0x17,0x0A,0x15,0x17,0x10,0x10 } },
	{ { 0x00,0x05,0x07,0x05,0x12,0x07,0x00,0x05 }, { 0x00,0x07,0x05,0x00,0x05,0x12,0x07,0x00 } },
	{ { 0x00,0x19,0x12,0x07,0x00,0x05,0x07,0x00 }, { 0x05,0x07,0x19,0x12,0x07,0x00,0x19,0x07 } },
	{ { 0x40,0x4A,0x45,0x27,0x32,0x27,0x40,0x39 }, { 0x40,0x32,0x27,0x40,0x4A,0x45,0x32,0x27 } },
	{ { 0x00,0x07,0x19,0x05,0x00,0x05,0x00,0x07 }, { 0x00,0x05,0x19,0x07,0x00,0x07,0x05,0x00 } },
	{ { 0x32,0x27,0x40,0x39,0x32,0x27,0x34,0x29 }, { 0x40,0x39,0x32,0x27,0x34,0x29,0x32,0x27 } },
	{ { 0x00,0x07,0x00,0x07,0x05,0x00,0x07,0x00 }, { 0x00,0x05,0x07,0x00,0x00,0x07,0x05,0x00 } },
	{ { 0x00,0x19,0x05,0x07,0x00,0x05,0x19,0x07 }, { 0x19,0x05,0x00,0x07,0x00,0x19,0x07,0x05 } },
	{ { 0x32,0x27,0x40,0x40,0x39,0x32,0x27,0x40 }, { 0x40,0x29,0x32,0x27,0x34,0x29,0x32,0x27 } },
	{ { 0x00,0x29,0x32,0x27,0x00,0x20,0x05,0x15 }, { 0x00,0x39,0x32,0x27,0x05,0x15,0x00,0x29 } },
	{ { 0x34,0x29,0x32,0x27,0x40,0x39,0x32,0x27 }, { 0x34,0x29,0x32,0x27,0x34,0x29,0x32,0x40 } },
	{ { 0x20,0x20,0x20,0x20,0x25,0x25,0x20,0x27 }, { 0x20,0x25,0x20,0x20,0x25,0x27,0x20,0x27 } },
	{ { 0x20,0x20,0x25,0x20,0x27,0x25,0x20,0x20 }, { 0x20,0x25,0x20,0x27,0x25,0x20,0x20,0x27 } },
	{ { 0x20,0x20,0x25,0x20,0x27,0x25,0x20,0x27 }, { 0x20,0x25,0x20,0x20,0x25,0x27,0x20,0x20 } },
	{ { 0x00,0x20,0x05,0x15,0x00,0x19,0x12,0x07 }, { 0x00,0x05,0x00,0x07,0x19,0x05,0x00,0x00 } },
	{ { 0x20,0x39,0x32,0x27,0x20,0x25,0x32,0x27 }, { 0x20,0x25,0x20,0x39,0x32,0x27,0x20,0x25 } },
	{ { 0x30,0x35,0x37,0x30,0x30,0x08,0x35,0x37 }, { 0x30,0x37,0x35,0x30,0x08,0x0A,0x30,0x37 } },
	{ { 0x30,0x35,0x0A,0x03,0x30,0x35,0x37,0x30 }, { 0x30,0x0A,0x35,0x30,0x03,0x35,0x0A,0x30 } },
	{ { 0x00,0x05,0x00,0x07,0x00,0x07,0x05,0x00 }, { 0x00,0x00,0x05,0x07,0x00,0x05,0x07,0x00 } },
	{ { 0x00,0x05,0x07,0x05,0x00,0x07,0x05,0x00 }, { 0x00,0x07,0x05,0x07,0x00,0x05,0x00,0x07 } },
	{ { 0x10,0x03,0x0A,0x05,0x10,0x15,0x0A,0x10 }, { 0x10,0x0A,0x05,0x03,0x10,0x17,0x08,0x0A } },
	{ { 0x00,0x05,0x00,0x07,0x00,0x19,0x05,0x07 }, { 0x00,0x07,0x05,0x00,0x05,0x07,0x00,0x00 } },
	{ { 0x05,0x07,0x14,0x19,0x12,0x07,0x00,0x19 }, { 0x00,0x19,0x05,0x07,0x14,0x19,0x12,0x07 } },
	{ { 0x19,0x05,0x00,0x07,0x05,0x07,0x14,0x19 }, { 0x19,0x07,0x05,0x07,0x00,0x07,0x19,0x05 } },
	{ { 0x00,0x07,0x19,0x14,0x05,0x00,0x12,0x00 }, { 0x00,0x05,0x07,0x19,0x00,0x12,0x07,0x00 } },
	{ { 0x10,0x10,0x08,0x0A,0x10,0x17,0x08,0x0A }, { 0x10,0x0A,0x08,0x17,0x10,0x08,0x0A,0x10 } },
	{ { 0x10,0x01,0x10,0x17,0x10,0x08,0x01,0x17 }, { 0x10,0x17,0x01,0x10,0x08,0x17,0x10,0x10 } },
	{ { 0x00,0x00,0x05,0x00,0x00,0x07,0x05,0x00 }, { 0x00,0x05,0x00,0x00,0x07,0x05,0x00,0x07 } },
	{ { 0x32,0x27,0x40,0x39,0x32,0x27,0x40,0x40 }, { 0x40,0x32,0x27,0x40,0x34,0x29,0x32,0x27 } },
	{ { 0x10,0x07,0x10,0x15,0x10,0x08,0x07,0x10 }, { 0x10,0x15,0x07,0x10,0x08,0x15,0x07,0x10 } },
	{ { 0x10,0x0A,0x08,0x07,0x15,0x07,0x10,0x0A }, { 0x10,0x08,0x0A,0x07,0x15,0x0A,0x08,0x10 } },
	{ { 0x00,0x05,0x07,0x05,0x00,0x03,0x05,0x07 }, { 0x00,0x07,0x05,0x00,0x0A,0x05,0x00,0x07 } },
	{ { 0x30,0x35,0x0A,0x30,0x30,0x37,0x35,0x0A }, { 0x30,0x0A,0x35,0x30,0x35,0x37,0x30,0x30 } },
	{ { 0x10,0x15,0x10,0x17,0x10,0x10,0x15,0x17 }, { 0x10,0x17,0x10,0x15,0x10,0x15,0x17,0x10 } },
	{ { 0x19,0x05,0x00,0x07,0x19,0x05,0x00,0x07 }, { 0x19,0x05,0x00,0x07,0x05,0x07,0x19,0x07 } },
	{ { 0x10,0x08,0x0A,0x10,0x10,0x17,0x08,0x0A }, { 0x10,0x0A,0x08,0x17,0x10,0x15,0x0A,0x10 } },
	{ { 0x10,0x08,0x03,0x0A,0x15,0x17,0x08,0x0A }, { 0x10,0x17,0x08,0x0A,0x03,0x0A,0x15,0x10 } },
};


static int StyleProgression(int style, int variation, ProgStep* out, int cap)
{
	if (!out || cap < 1) return 0;
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	if (variation < 0) variation = 0;
	const int col = variation & 1;
	int n = cap < 8 ? cap : 8;
	for (int i = 0; i < n; i++) {
		uint8_t packed = kHarm[style][col][i];
		out[i].off = (int8_t)(packed & 15);
		out[i].type = (uint8_t)((packed >> 4) & 7);
	}
	return n;
}

int ScAutoStyleVariationCount(int style)
{
	(void)style;
	return 2;
}

/* --- 軽量 RNG（xorshift、std/alloc 不使用）：毎回違うアレンジを出すため --- */
struct Rng { uint32_t s; };
static void RngSeed(Rng* r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }
static uint32_t RngNext(Rng* r) { uint32_t x = r->s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; r->s = x ? x : 0x1234567u; return r->s; }
static int RngRange(Rng* r, int lo, int hi) { if (hi <= lo) return lo; return lo + (int)(RngNext(r) % (uint32_t)(hi - lo + 1)); }

static const wchar_t* PcName(int pc)
{
	static const wchar_t* k[12] = { L"C", L"C#", L"D", L"Eb", L"E", L"F", L"F#", L"G", L"Ab", L"A", L"Bb", L"B" };
	return k[((pc % 12) + 12) % 12];
}
static const wchar_t* ChTypeSuffix(int type)
{
	switch (type) {
	case CH_MIN: return L"m"; case CH_7: return L"7"; case CH_M7: return L"m7";
	case CH_MAJ7: return L"maj7"; case CH_SUS: return L"sus4"; case CH_DIM: return L"dim";
	case CH_POW: return L"5"; default: return L"";
	}
}

void ScAutoStyleProgDesc(int style, int variation, int keySig, wchar_t* buf, int cch)
{
	if (!buf || cch <= 0) return;
	buf[0] = 0;
	ProgStep steps[16];
	int n = StyleProgression(style, variation, steps, 16);
	int tonic = TonicPc(keySig);
	int pos = 0;
	for (int i = 0; i < n && pos < cch - 12; i++) {
		if (i > 0) { buf[pos++] = L' '; buf[pos++] = L'-'; buf[pos++] = L' '; buf[pos] = 0; }
		const wchar_t* nm = PcName(tonic + steps[i].off);
		const wchar_t* sf = ChTypeSuffix(steps[i].type);
		while (*nm && pos < cch - 2) buf[pos++] = *nm++;
		while (*sf && pos < cch - 2) buf[pos++] = *sf++;
		buf[pos] = 0;
	}
}

struct Ctx {
	ScEvent* ev;
	int* n;
	int max;
	int isFm;
	int added;
	int overflow;
	int jobs;
};

static int Push(Ctx* c, uint32_t tick, int ch, uint8_t kind, uint8_t a, uint8_t b, uint8_t c8, uint16_t dur)
{
	if (!c->ev || !c->n || *c->n >= c->max || *c->n >= SC_EV_MAX) {
		c->overflow = 1;
		return 0;
	}
	if (IsNoteKind(kind)) {
		for (int i = 0; i < *c->n; i++) {
			if ((int)c->ev[i].ch == ch && c->ev[i].kind == kind && c->ev[i].tick == tick && c->ev[i].a == a)
				return 1;
		}
	}
	ScEvent& e = c->ev[*c->n];
	memset(&e, 0, sizeof(e));
	e.tick = tick;
	e.seq = (uint32_t)(*c->n + 1);
	e.ch = (uint8_t)ch;
	e.kind = kind;
	e.a = a; e.b = b; e.c = c8;
	e.dur = dur;
	(*c->n)++;
	if (IsNoteKind(kind)) c->added++;
	return 1;
}

static int HasKind(const ScEvent* ev, int n, int ch, uint8_t kind)
{
	for (int i = 0; i < n; i++)
		if ((int)ev[i].ch == ch && ev[i].kind == kind) return 1;
	return 0;
}

static int DrumChannel(const ScEvent* ev, int n, int ch, int isFm)
{
	if (isFm) return ch == 6;
	return ScMidiPartIsDrum(ev, n, ch, 0xFFFFFFFFu);
}

static int PitchOf(const ScEvent& e, int isFm)
{
	if (e.kind == SC_EV_NOTE) return (int)e.a;
	if (e.kind != SC_EV_FM_NOTE) return -1;
	if (isFm && e.ch == 6) return -1;
	int sc = (int)(e.a & 0x0F);
	if (sc > 11) return -1;
	return ((int)(e.a >> 4) & 0x0F) * 12 + sc + 12;
}

static uint8_t PackFm(int midi)
{
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	int oct = midi / 12 - 1;
	int sc = midi % 12;
	if (oct < 0) { oct = 0; sc = 0; }
	if (oct > 9) oct = 9;
	return (uint8_t)((oct << 4) | sc);
}

static int NoteOverlaps(const ScEvent& e, uint32_t t0, uint32_t t1)
{
	if (!IsNoteKind(e.kind)) return 0;
	uint32_t end = e.tick + (e.dur ? e.dur : 1);
	return e.tick < t1 && end > t0;
}

static void ShiftFrom(ScEvent* ev, int n, uint32_t from, uint32_t dt)
{
	if (!dt) return;
	for (int i = 0; i < n; i++)
		if (ev[i].tick >= from) ev[i].tick += dt;
	ScStaffLayoutTouch();
}

static int ThirdIv(int type)
{
	if (type == CH_MIN || type == CH_M7 || type == CH_DIM) return 3;
	if (type == CH_SUS) return 5;
	if (type == CH_POW) return 7;
	return 4;
}

static int TonicPc(int keySig)
{
	static const int kT[15] = { 11, 6, 1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1 };
	int i = keySig + 7;
	if (i < 0) i = 0;
	if (i > 14) i = 14;
	return kT[i];
}

static int ChordTones(int rootPc, int type, int power, int* out, int cap)
{
	int iv[4];
	int n = 0;
	iv[n++] = 0;
	if (!power && type != CH_POW) {
		if (type == CH_SUS) iv[n++] = 5;
		else if (type != CH_POW) iv[n++] = ThirdIv(type);
	}
	iv[n++] = 7;
	if (!power && (type == CH_7 || type == CH_M7)) iv[n++] = 10;
	else if (!power && type == CH_MAJ7) iv[n++] = 11;
	int base = 48 + (rootPc % 12);
	while (base + 7 > 76) base -= 12;
	while (base < 48) base += 12;
	int w = 0;
	for (int i = 0; i < n && w < cap; i++) {
		int m = base + iv[i];
		if (m < 0) m = 0;
		if (m > 127) m = 127;
		out[w++] = m;
	}
	return w;
}

static int BassMidi(int rootPc, int deg, int type)
{
	int add = 0;
	switch (deg) {
	case 1: add = 0; break;
	case 2: add = 7; break;
	case 3: add = 12; break;
	case 4: add = -1; break;
	case 5: add = ThirdIv(type); break;
	case 6: add = 19; break;
	case 7: add = -12; break;
	case 8: add = (type == CH_7 || type == CH_M7) ? 10 : 9; break;
	case 9: add = 6; break;
	default: add = 0; break;
	}
	int m = 36 + (rootPc % 12) + add;
	while (m < 28) m += 12;
	while (m > (deg == 3 || deg == 6 ? 64 : 52)) m -= 12;
	if (m < 0) m = 0;
	if (m > 127) m = 127;
	return m;
}

static int ShiftOct(int midi, int oct, int lo, int hi)
{
	midi += 12 * oct;
	if (hi < lo) hi = lo;
	while (midi > hi) midi -= 12;
	while (midi < lo) midi += 12;
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	return midi;
}

static int ProgAt(const StylePat* s, int col, int dup)
{
	int i = col + (dup > 0 ? dup : 0);
	if (!s) return 0;
	return s->progAll[i & 7];
}

/* FM1-6 と SSG は同時に1音。RHY と Misao は対象外。 */
static int FmMelodic(int ch)
{
	return (ch >= 0 && ch < 10 && ch != 6) ? 1 : 0;
}

static void ClipFmMono(Ctx* c, int ch, uint32_t tick)
{
	if (!c || !c->ev || !c->n || !FmMelodic(ch)) return;
	for (int i = 0; i < *c->n; i++) {
		ScEvent& e = c->ev[i];
		if ((int)e.ch != ch || e.kind != SC_EV_FM_NOTE) continue;
		if (e.tick >= tick) continue;
		uint32_t end = e.tick + (e.dur ? e.dur : 1);
		if (end > tick) {
			uint32_t nd = tick - e.tick;
			if (nd < 1) nd = 1;
			e.dur = (uint16_t)nd;
		}
	}
}

/* 低い塊はベースとして最低音。和音は最高音を残して1本の線にする。 */
static int MonoKeep(const int* pit, int np)
{
	if (!pit || np < 1) return 0;
	int lo = pit[0], hi = pit[0];
	for (int i = 1; i < np; i++) {
		if (pit[i] < lo) lo = pit[i];
		if (pit[i] > hi) hi = pit[i];
	}
	if (hi - lo <= 12 && lo < 52) return lo;
	return hi;
}

static int PlaceNote(Ctx* c, int ch, uint32_t tick, int midi, int dur, int vel, int gate)
{
	if (ch < 0 || midi < 0) return 0;
	if (dur < 2) dur = 2;
	if (dur > 65535) dur = 65535;
	if (vel < 1) vel = 1;
	if (vel > 127) vel = 127;
	if (gate < 1) gate = 1;
	if (gate > 100) gate = 100;
	if (c->isFm) {
		if (ch == 6) return 0;
		ClipFmMono(c, ch, tick);
		return Push(c, tick, ch, SC_EV_FM_NOTE, PackFm(midi), 0, (uint8_t)gate, (uint16_t)dur);
	}
	return Push(c, tick, ch, SC_EV_NOTE, (uint8_t)midi, (uint8_t)vel, (uint8_t)gate, (uint16_t)dur);
}

static int GmDrum(int voice)
{
	switch (voice) {
	case DV_KICK: return 36;
	case DV_SNARE: return 38;
	case DV_CLAP: return 39;
	case DV_HAT: return 42;
	case DV_OHAT: return 46;
	case DV_RIDE: return 51;
	case DV_CRASH: return 49;
	case DV_TOM: return 47;
	case DV_RIM: return 37;
	default: return 42;
	}
}

static int FmPad(int voice)
{
	switch (voice) {
	case DV_KICK: return 0;
	case DV_SNARE: case DV_CLAP: return 1;
	case DV_CRASH: case DV_OHAT: case DV_RIDE: return 2;
	case DV_HAT: return 3;
	case DV_TOM: return 4;
	default: return 5;
	}
}

static void PlaceScoop(Ctx* c, int ch, uint32_t tick)
{
	if (!c || ch < 0) return;
	uint8_t kind = c->isFm ? SC_EV_FM_PITCH : SC_EV_PITCH;
	Push(c, tick, ch, kind, 52, 0, 0, 0);
	uint32_t back = tick + 8;
	Push(c, back, ch, kind, 64, 0, 0, 0);
}

static int DrumVel(int voice, int slot, int introSoft)
{
	int accent = ((slot % 4) == 0);
	int vel;
	if (voice == DV_HAT) vel = accent ? 84 : ((slot & 1) ? 46 : 66);
	else if (voice == DV_OHAT || voice == DV_CRASH) vel = 108;
	else if (voice == DV_SNARE || voice == DV_CLAP) vel = accent ? 118 : 98;
	else if (voice == DV_KICK) vel = (slot == 0) ? 122 : (accent ? 108 : 90);
	else if (voice == DV_RIM) vel = accent ? 80 : 64;
	else vel = accent ? 100 : 78;
	if (introSoft) vel -= 16;
	if (vel < 34) vel = 34;
	if (vel > 127) vel = 127;
	return vel;
}

static void PlaceDrum(Ctx* c, int ch, uint32_t tick, int voice, int vel, int dur)
{
	if (ch < 0) return;
	if (vel < 30) vel = 30;
	if (c->isFm) {
		if (dur < 2) dur = 2;
		Push(c, tick, ch, SC_EV_FM_NOTE, (uint8_t)FmPad(voice), 0, 0, (uint16_t)dur);
		return;
	}
	if (dur < 4) dur = 4;
	PlaceNote(c, ch, tick, GmDrum(voice), dur, vel, 55);
}

static void PlaceDrumNote(Ctx* c, int ch, uint32_t tick, int midi, int vel, int dur)
{
	if (!c || ch < 0 || midi < 0) return;
	if (vel < 28) vel = 28;
	if (vel > 127) vel = 127;
	if (c->isFm) {
		int voice = DV_CLAP;
		if (midi == 46 || midi == 49 || midi == 51 || midi == 52 || midi == 55 || midi == 57) voice = DV_OHAT;
		else if (midi == 42 || midi == 44) voice = DV_HAT;
		else if (midi == 36 || midi == 35) voice = DV_KICK;
		else if (midi == 38 || midi == 40) voice = DV_SNARE;
		else if (midi >= 41 && midi <= 50) voice = DV_TOM;
		PlaceDrum(c, ch, tick, voice, vel, dur);
		return;
	}
	if (dur < 2) dur = 2;
	PlaceNote(c, ch, tick, midi, dur, vel, 48);
}

/* クラッシュは 1 枚に固定しない。49 / 57 / スプラッシュ 55 / チャイナ 52。 */
static int CrashMidi(int style, int pat, int drummer, int bar, int slot)
{
	static const int k[8] = { 49, 57, 55, 52, 49, 57, 51, 55 };
	int i = (drummer * 3 + pat + (bar & 1) + (slot > 0 ? 1 : 0) + (style & 3)) & 7;
	return k[i];
}

/* ハイタム 50、ハイミッド 48、ローミッド 47、ロー 45。 */
static int TomMidi(int step)
{
	static const int k[4] = { 50, 48, 47, 45 };
	if (step < 0) step = 0;
	return k[step & 3];
}

static int PercMidi(int style, int pat, int which)
{
	switch (style) {
	case SC_AS_LATIN: case SC_AS_SAMBA: case SC_AS_BOSSA:
	case SC_AS_AFROBEAT: case SC_AS_TANGO: case SC_AS_FLAMENCO:
		return which ? 63 : 75;
	case SC_AS_REGGAE: case SC_AS_SKA:
		return which ? 56 : 54;
	case SC_AS_FUNK: case SC_AS_DISCO: case SC_AS_SOUL:
	case SC_AS_RNB: case SC_AS_NEOSOUL:
		return which ? 54 : 39;
	case SC_AS_ROCK: case SC_AS_METAL: case SC_AS_PUNK: case SC_AS_GRUNGE:
		return which ? 55 : 56;
	default:
		return which ? ((pat & 1) ? 54 : 70) : 39;
	}
}

/* 和音を同時に置かず、コードトーンを一本のラインで歩く。 */
static int CompLineMidi(int root, int type, int power, int step)
{
	int tones[4];
	int nt = ChordTones(root, type, power, tones, 4);
	if (nt < 1) return 64;
	const int i = ((step % nt) + nt) % nt;
	const int lap = (step / nt) & 1;
	int midi = tones[i] + (lap ? 12 : 0);
	while (midi < 52) midi += 12;
	while (midi > 84) midi -= 12;
	return midi;
}

static void PlaceChord(Ctx* c, int ch, uint32_t tick, int dur, int vel, int root, int type, int power, int arp)
{
	int tones[4];
	int nt = ChordTones(root, type, power, tones, 4);
	if (nt < 1 || ch < 0) return;
	if (dur < 2) dur = 2;
	if (c->isFm || arp) {
		int each = dur / nt;
		if (each < 2) each = 2;
		for (int i = 0; i < nt; i++)
			PlaceNote(c, ch, tick + (uint32_t)(i * each), tones[i], each, vel - i * 4, 80);
		return;
	}
	for (int i = 0; i < nt; i++)
		PlaceNote(c, ch, tick, tones[i], dur, vel - i * 6, 75);
}

static int PickEmpty(const int* notes, int lim, int isFm, int a, int b, int c, const int* prefer, int np)
{
	for (int pass = 0; pass < 2; pass++) {
		int n = (pass == 0) ? np : lim;
		for (int i = 0; i < n; i++) {
			int ch = (pass == 0) ? prefer[i] : i;
			if (ch < 0 || ch >= lim) continue;
			if (isFm && ch == 6) continue;
			if (ch == a || ch == b || ch == c) continue;
			if (notes[ch] != 0) continue;
			return ch;
		}
	}
	return -1;
}

static void MarkUi(ScStaffUi* ui, int ch, int clef, int anySolo)
{
	if (!ui || ch < 0 || ch >= ui->trackCount || ch >= 32) return;
	ui->visible[ch] = 1;
	ui->mute[ch] = 0;
	if (anySolo) ui->solo[ch] = 1;
	if (clef >= 0) ui->clef[ch] = clef;
}

static int s_arrGsMap; /* 0自動 1=55 2=88 3=88Pro 4=8820/8850 5=XG */

static void RouteBank(int drum, int* msb, int* lsb)
{
	const int map = s_arrGsMap;
	const int xg = (map == 5) || (map == 0 && ScStaffScorePreviewOut() == SC_OUT_XG);
	if (drum) {
		*msb = xg ? 127 : 0;
		*lsb = 0;
	} else if (xg) {
		*msb = 0;
		*lsb = 0;
	} else {
		/* 1=SC-55 … 4=8820/8850 のキャピタル。未指定は 8820（CC32=4）。
		   CC0=127 / CC32=0 は LA なので置かない。 */
		int bank = 4;
		if (map >= 1 && map <= 4) bank = map;
		*msb = 0;
		*lsb = bank;
	}
}

/* 手動の音色と同じ並び。tick 0 の既存イベントより前に、ドラム → バンク → PC。 */
static uint32_t ReserveHeadSeq(Ctx* c, int ch, int need)
{
	if (!c || !c->ev || !c->n || need < 1) return 0;
	const int n = *c->n;
	uint32_t minSeq = 0xFFFFFFFFu;
	int found = 0;
	for (int i = 0; i < n; i++) {
		if ((int)c->ev[i].ch != ch || c->ev[i].tick != 0) continue;
		found = 1;
		if (c->ev[i].seq < minSeq) minSeq = c->ev[i].seq;
	}
	if (!found) return (uint32_t)n;
	if (minSeq >= (uint32_t)need) return minSeq - (uint32_t)need;
	for (int i = 0; i < n; i++) {
		if ((int)c->ev[i].ch == ch && c->ev[i].tick == 0)
			c->ev[i].seq += (uint32_t)need;
	}
	return 0;
}

/* wantDrum なら a=1 を必ず書く（ch10 の無印は再生側で通常に落ちることがある）。
   writeTone が 0 のときは番号を変えず、並びだけドラム → バンク → PC にする。 */
static void PlaceHeadTone(Ctx* c, int ch, int wantDrum, int drumSlot, int writeTone, int prog, int msb, int lsb)
{
	if (!c || !c->ev || !c->n || ch < 0) return;
	if (prog < 0) prog = 0;
	if (prog > 127) prog = 127;
	if (msb < 0) msb = 0;
	if (msb > 127) msb = 127;
	if (lsb < 0) lsb = 0;
	if (lsb > 127) lsb = 127;
	int iDrum = -1, iBank = -1, iProg = -1;
	for (int i = 0; i < *c->n; i++) {
		if ((int)c->ev[i].ch != ch || c->ev[i].tick != 0) continue;
		if (c->ev[i].kind == SC_EV_DRUM) iDrum = i;
		else if (c->ev[i].kind == SC_EV_BANK) iBank = i;
		else if (c->ev[i].kind == SC_EV_PROG) iProg = i;
	}
	if (wantDrum) {
		if (iDrum < 0) {
			if (!Push(c, 0, ch, SC_EV_DRUM, 1, (uint8_t)(drumSlot ? 1 : 0), 0, 0)) return;
			iDrum = *c->n - 1;
		} else {
			c->ev[iDrum].a = 1;
			c->ev[iDrum].b = (uint8_t)(drumSlot ? 1 : 0);
			c->ev[iDrum].c = 0;
		}
	}
	/* バンクは番号を残すときも直す。8820 は CC32=4。127 は LA。 */
	if (iBank < 0) {
		if (!Push(c, 0, ch, SC_EV_BANK, (uint8_t)msb, (uint8_t)lsb, 0, 0)) return;
		iBank = *c->n - 1;
	} else {
		c->ev[iBank].a = (uint8_t)msb;
		c->ev[iBank].b = (uint8_t)lsb;
		c->ev[iBank].c = 0;
	}
	if (writeTone) {
		if (iProg < 0) {
			if (!Push(c, 0, ch, SC_EV_PROG, (uint8_t)prog, (uint8_t)prog, 0, 0)) return;
			iProg = *c->n - 1;
		} else {
			c->ev[iProg].a = (uint8_t)prog;
			c->ev[iProg].b = (uint8_t)prog;
			c->ev[iProg].c = 0;
		}
	}
	int need = 0;
	if (wantDrum && iDrum >= 0) need++;
	if (iBank >= 0) need++;
	if (iProg >= 0) need++;
	if (need < 1) return;
	uint32_t seq = ReserveHeadSeq(c, ch, need);
	if (wantDrum && iDrum >= 0) c->ev[iDrum].seq = seq++;
	if (iBank >= 0) c->ev[iBank].seq = seq++;
	if (iProg >= 0) c->ev[iProg].seq = seq++;
}

static int BpmToTempoT(int bpm)
{
	if (bpm < 40) bpm = 40;
	if (bpm > 240) bpm = 240;
	return (int)((13000.0 * 120.0) / (double)bpm + 0.5);
}

/* 初期値 13000（120）だけの譜面はスタイル BPM を書く。
   それ以外のテンポは残し、イベントが無ければ T をイベントにして試聴に乗せる。 */
static void SetHeadTempo(Ctx* c, int bpm, int rewrite, int* docTempoT)
{
	if (!c || !c->ev || !c->n) return;
	const int n = *c->n;
	int found = -1;
	for (int i = 0; i < n; i++) {
		const uint8_t k = c->ev[i].kind;
		if (k != SC_EV_TEMPO && k != SC_EV_FM_TEMPO) continue;
		if (found < 0 || c->ev[i].tick < c->ev[found].tick) found = i;
	}
	int have = 0;
	if (found >= 0)
		have = (int)c->ev[found].a | ((int)c->ev[found].b << 8);
	const int styleT = BpmToTempoT(bpm);
	const int header = (docTempoT && *docTempoT > 0) ? *docTempoT : 0;
	const int haveReal = (have > 0 && have != 13000);
	const int headReal = (header > 0 && header != 13000);
	/* 上書きはスタイルのテンポにする。重ねは、入っているテンポを残す。 */
	int want = styleT;
	if (!rewrite) {
		if (haveReal)
			want = have;
		else if (headReal)
			want = header;
	}
	if (want < 1) want = styleT;
	if (found >= 0) {
		c->ev[found].tick = 0;
		c->ev[found].a = (uint8_t)(want & 0xFF);
		c->ev[found].b = (uint8_t)((want >> 8) & 0xFF);
	} else {
		Push(c, 0, 0, c->isFm ? SC_EV_FM_TEMPO : SC_EV_TEMPO,
			(uint8_t)(want & 0xFF), (uint8_t)((want >> 8) & 0xFF), 0, 0);
	}
	if (docTempoT) *docTempoT = want;
}

/* tick 0 のプログラムが 1 以上なら、ユーザか前のスタイルが選んだ音色。0 は未反映。 */
static int HeadProgChosen(const ScEvent* ev, int n, int ch)
{
	for (int i = 0; i < n; i++) {
		if ((int)ev[i].ch != ch || ev[i].kind != SC_EV_PROG || ev[i].tick != 0) continue;
		return ev[i].a != 0;
	}
	return 0;
}

static void EnsureMelodic(Ctx* c, ScStaffUi* ui, int ch, int role, int prog, int anySolo, int keep)
{
	if (ch < 0) return;
	int n = *c->n;
	if (!c->isFm) {
		/* 手動の ScMidiApplyProgBankAt と同じく、バンクの直後に PC。
		   0 以外が既にあれば番号は変えず、並びだけ直す。 */
		int msb = 0, lsb = 4;
		RouteBank(0, &msb, &lsb);
		const int writeTone = !(keep && HeadProgChosen(c->ev, n, ch));
		PlaceHeadTone(c, ch, 0, 0, writeTone, prog, msb, lsb);
		if (!HasKind(c->ev, n, ch, SC_EV_PAN)) {
			int pan = (role == 0) ? 40 : (role == 1) ? 96 : 64;
			Push(c, 0, ch, SC_EV_PAN, (uint8_t)pan, 0, 0, 0);
		}
		if (!HasKind(c->ev, n, ch, SC_EV_VELO))
			Push(c, 0, ch, SC_EV_VELO, 100, 0, 0, 0);
		if (!HasKind(c->ev, n, ch, SC_EV_CLEF))
			Push(c, 0, ch, SC_EV_CLEF, (uint8_t)(role == 0 ? 1 : 0), 0, 0, 0);
	} else if (ch != 6) {
		int ssg = (ch >= 3 && ch <= 5);
		if (!ssg && ch < SC_FM_CH && !HasKind(c->ev, n, ch, SC_EV_FM_VOICE)) {
			int voice = (role == 0) ? 2 : (role == 2) ? 4 : 1;
			Push(c, 0, ch, SC_EV_FM_VOICE, (uint8_t)voice, 0, 0, 0);
		}
		if (!HasKind(c->ev, n, ch, SC_EV_FM_VOL)) {
			if (ssg) Push(c, 0, ch, SC_EV_FM_VOL, 13, 1, 0, 0);
			else Push(c, 0, ch, SC_EV_FM_VOL, 8, 0, 0, 0);
		}
		if (!HasKind(c->ev, n, ch, SC_EV_FM_FLR)) {
			uint8_t lr = (role == 0) ? 0x80 : (role == 1) ? 0x40 : 0xC0;
			Push(c, 0, ch, SC_EV_FM_FLR, lr, 0, 0, 0);
		}
	}
	MarkUi(ui, ch, role == 0 ? 1 : 0, anySolo);
}

static void EnsureDrum(Ctx* c, ScStaffUi* ui, int ch, int slot, int anySolo, int kit, int keep)
{
	if (ch < 0) return;
	/* a=1(ドラム) は native stream 用。b にドラム枠(0=1,1=2) を載せる(native では無視)。
	   既定ドラム ch でも slot>0 のときは枠を残すため DRUM イベントを書く。 */
	if (!c->isFm) {
		/* ch10 も DRUM を書く。無印のままだとキット PC 0 がピアノになる。
		   並びはリズム指定 → バンク → キット PC。 */
		int msb = 0, lsb = 0;
		RouteBank(1, &msb, &lsb);
		const int writeTone = !(keep && HeadProgChosen(c->ev, *c->n, ch));
		PlaceHeadTone(c, ch, 1, slot, writeTone, kit, msb, lsb);
	}
	if (!HasKind(c->ev, *c->n, ch, SC_EV_CLEF))
		Push(c, 0, ch, SC_EV_CLEF, 3, 0, 0, 0);
	MarkUi(ui, ch, 3, anySolo);
}

static int BarHas(const ScEvent* ev, int n, int ch, uint32_t t0, uint32_t t1)
{
	if (ch < 0) return 0;
	for (int i = 0; i < n; i++)
		if ((int)ev[i].ch == ch && NoteOverlaps(ev[i], t0, t1)) return 1;
	return 0;
}

/* その拍に既に音があるか。重ねのとき、鳴っている間だけ避けて空いた発音へ足す。 */
static int OnsetBusy(const ScEvent* ev, int n, int ch, uint32_t tick)
{
	if (ch < 0) return 1;
	for (int i = 0; i < n; i++) {
		if ((int)ev[i].ch != ch || !IsNoteKind(ev[i].kind)) continue;
		uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : 1u);
		if (ev[i].tick <= tick && end > tick) return 1;
	}
	return 0;
}

static int PitchBusy(const ScEvent* ev, int n, int ch, uint32_t tick, int midi)
{
	if (ch < 0) return 1;
	for (int i = 0; i < n; i++) {
		if ((int)ev[i].ch != ch || !IsNoteKind(ev[i].kind)) continue;
		if ((int)ev[i].a != midi) continue;
		uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : 1u);
		if (ev[i].tick <= tick && end > tick) return 1;
	}
	return 0;
}

/* 重ね。スロット [t0,t1) を新しいスタイルの発音か休符として扱う。
   休符: その音（midi<0 なら全部）でスロット内に始まる旧ノートを消す。
   発音: 同じ音のいちばん早い頭は残す。違う音と、同じ音の余分な頭（16分の裏など）は消す。
   前から被さる音は t0 で切る。戻り 1 は「もう置いてあるので足さない」。 */
/* 新しい音が既存の倍以上長いときは、短い方を消さない。
   既存が倍以上長いときは、短い方へ明け渡す。近い長さは半分残す。 */
static int OverlayKeep(int oldDur, int newDur, uint32_t tick)
{
	if (oldDur < 1) oldDur = 1;
	if (newDur < 1) newDur = oldDur;
	if (newDur >= oldDur * 2) return 1;
	if (newDur * 2 <= oldDur) return 0;
	return ((tick / 12u) & 1u) ? 0 : 1;
}

static int MergeSlot(ScEvent* ev, int* n, int* nOld, int ch, uint32_t t0, uint32_t t1, int midi, int rest, int* removed, int newDur)
{
	if (!ev || !n || !nOld || ch < 0) return 1;
	if (t1 <= t0) t1 = t0 + 1;
	if (newDur < 1) newDur = (int)(t1 - t0);
	int limit = *nOld;
	if (limit < 0) limit = 0;
	if (limit > *n) limit = *n;
	if (!rest) {
		for (int i = 0; i < limit; ) {
			int drop = 0;
			if ((int)ev[i].ch == ch && IsNoteKind(ev[i].kind) && ev[i].tick < t0) {
				int oldDur = ev[i].dur ? (int)ev[i].dur : 1;
				uint32_t end = ev[i].tick + (uint32_t)oldDur;
				/* 全音符の上に16分を置くときだけ、長い音をそこで切る。 */
				if (end > t0 && newDur * 2 <= oldDur) {
					uint32_t nd = t0 - ev[i].tick;
					if (nd < 2) drop = 1;
					else ev[i].dur = (uint16_t)nd;
				}
			}
			if (!drop) { i++; continue; }
			for (int j = i + 1; j < *n; j++) ev[j - 1] = ev[j];
			(*n)--;
			limit--;
			if (removed) (*removed)++;
		}
		*nOld = limit;
	}
	uint32_t keepTick = 0xFFFFFFFFu;
	if (!rest) {
		for (int i = 0; i < limit; i++) {
			if ((int)ev[i].ch != ch || !IsNoteKind(ev[i].kind)) continue;
			if (ev[i].tick < t0 || ev[i].tick >= t1) continue;
			if ((int)ev[i].a != midi) continue;
			if (ev[i].tick < keepTick) keepTick = ev[i].tick;
		}
	}
	int w = 0;
	int oldLeft = 0;
	int rm = 0;
	for (int i = 0; i < *n; i++) {
		int drop = 0;
		if (i < limit && (int)ev[i].ch == ch && IsNoteKind(ev[i].kind)
			&& ev[i].tick >= t0 && ev[i].tick < t1) {
			const int p = (int)ev[i].a;
			int oldDur = ev[i].dur ? (int)ev[i].dur : 1;
			int keep = OverlayKeep(oldDur, newDur, ev[i].tick);
			if (rest) {
				if (!keep && (midi < 0 || p == midi)) drop = 1;
			} else if (!keep && (p != midi || ev[i].tick != keepTick)) {
				drop = 1;
			}
		}
		if (drop) { rm++; continue; }
		if (w != i) ev[w] = ev[i];
		if (i < limit) oldLeft++;
		w++;
	}
	*n = w;
	*nOld = oldLeft;
	if (removed) *removed += rm;
	if (rest) return 1;
	return keepTick != 0xFFFFFFFFu;
}

/* 1ch に複数の構成音。欲しい音のいちばん早い頭は残し、違う音と余分な頭は消す。
   戻りは足りない音の数。0 なら足さない。 */
static int MergeKeep(ScEvent* ev, int* n, int* nOld, int ch, uint32_t t0, uint32_t t1, const int* midi, int nMidi, int* removed)
{
	if (!ev || !n || !nOld || ch < 0 || !midi || nMidi < 1) return 1;
	if (t1 <= t0) t1 = t0 + 1;
	int limit = *nOld;
	if (limit < 0) limit = 0;
	if (limit > *n) limit = *n;
	for (int i = 0; i < limit; ) {
		int drop = 0;
		if ((int)ev[i].ch == ch && IsNoteKind(ev[i].kind) && ev[i].tick < t0) {
			uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : 1u);
			if (end > t0) {
				uint32_t nd = t0 - ev[i].tick;
				if (nd < 2) drop = 1;
				else ev[i].dur = (uint16_t)nd;
			}
		}
		if (!drop) { i++; continue; }
		for (int j = i + 1; j < *n; j++) ev[j - 1] = ev[j];
		(*n)--;
		limit--;
		if (removed) (*removed)++;
	}
	int wantP[8];
	uint32_t keepTick[8];
	int keepN = 0;
	for (int t = 0; t < nMidi && keepN < 8; t++) {
		int seen = 0;
		for (int u = 0; u < keepN; u++) if (wantP[u] == midi[t]) seen = 1;
		if (seen) continue;
		wantP[keepN] = midi[t];
		keepTick[keepN] = 0xFFFFFFFFu;
		for (int i = 0; i < limit; i++) {
			if ((int)ev[i].ch != ch || !IsNoteKind(ev[i].kind)) continue;
			if (ev[i].tick < t0 || ev[i].tick >= t1) continue;
			if ((int)ev[i].a != midi[t]) continue;
			if (ev[i].tick < keepTick[keepN]) keepTick[keepN] = ev[i].tick;
		}
		keepN++;
	}
	int w = 0;
	int oldLeft = 0;
	int rm = 0;
	int kept = 0;
	for (int i = 0; i < *n; i++) {
		int drop = 0;
		if (i < limit && (int)ev[i].ch == ch && IsNoteKind(ev[i].kind)
			&& ev[i].tick >= t0 && ev[i].tick < t1) {
			int slot = -1;
			for (int t = 0; t < keepN; t++) if ((int)ev[i].a == wantP[t]) { slot = t; break; }
			if (slot < 0 || keepTick[slot] == 0xFFFFFFFFu || ev[i].tick != keepTick[slot])
				drop = 1;
			else {
				keepTick[slot] = 0xFFFFFFFEu;
				kept++;
			}
		}
		if (drop) { rm++; continue; }
		if (w != i) ev[w] = ev[i];
		if (i < limit) oldLeft++;
		w++;
	}
	*n = w;
	*nOld = oldLeft;
	if (removed) *removed += rm;
	if (keepN < kept) return 0;
	return keepN - kept;
}

static int SlotHasPitch(const ScEvent* ev, int n, int ch, uint32_t t0, uint32_t t1, int midi)
{
	if (!ev || ch < 0) return 0;
	for (int i = 0; i < n; i++) {
		if ((int)ev[i].ch != ch || !IsNoteKind(ev[i].kind)) continue;
		if (ev[i].tick < t0 || ev[i].tick >= t1) continue;
		if ((int)ev[i].a == midi) return 1;
	}
	return 0;
}

static void AnalyzeChord(const ScEvent* ev, int n, int isFm, int chordCh, int melodyCh, uint32_t t0, uint32_t t1, int keySig, int prevRoot, int prevType, int* outRoot, int* outType, int* outVoiced)
{
	int pc[12];
	memset(pc, 0, sizeof(pc));
	int low = 127;
	int lowPc = -1;
	for (int i = 0; i < n; i++) {
		if (!NoteOverlaps(ev[i], t0, t1)) continue;
		if (DrumChannel(ev, n, ev[i].ch, isFm)) continue;
		int use = 0;
		if (chordCh >= 0 && (int)ev[i].ch == chordCh) use = 1;
		else if (chordCh < 0 && (melodyCh < 0 || (int)ev[i].ch == melodyCh)) use = 1;
		if (!use) continue;
		int m = PitchOf(ev[i], isFm);
		if (m < 0) continue;
		int w = ev[i].dur ? (int)ev[i].dur : 12;
		pc[m % 12] += w;
		if (m < low) { low = m; lowPc = m % 12; }
	}
	int sum = 0;
	for (int i = 0; i < 12; i++) sum += pc[i];
	if (sum <= 0) {
		*outRoot = prevRoot >= 0 ? prevRoot : TonicPc(keySig);
		*outType = prevType >= 0 ? prevType : CH_MAJ;
		*outVoiced = 0;
		return;
	}
	/* 単音のメロディを毎小節コード扱いすると伴奏が暴れる。
	   三和音、または根音＋3度/4度/5度の二音だけをコードとして拾う。 */
	{
		int distinct = 0;
		for (int i = 0; i < 12; i++) if (pc[i] > 0) distinct++;
		int consonant = 0;
		if (distinct >= 3) consonant = 1;
		else if (distinct == 2) {
			int a = -1, b = -1;
			for (int i = 0; i < 12; i++) if (pc[i] > 0) { if (a < 0) a = i; else b = i; }
			int iv = (b - a + 12) % 12;
			if (iv > 6) iv = 12 - iv;
			if (iv == 3 || iv == 4 || iv == 5 || iv == 7) consonant = 1;
		}
		if (!consonant) {
			*outRoot = prevRoot >= 0 ? prevRoot : TonicPc(keySig);
			*outType = prevType >= 0 ? prevType : CH_MAJ;
			*outVoiced = 0;
			return;
		}
	}
	static const int kN[8] = { 3, 3, 4, 4, 4, 3, 3, 2 };
	static const int kIv[8][4] = {
		{ 0, 4, 7, 0 }, { 0, 3, 7, 0 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 },
		{ 0, 4, 7, 11 }, { 0, 5, 7, 0 }, { 0, 3, 6, 0 }, { 0, 7, 0, 0 }
	};
	int bestS = -100000, bestR = TonicPc(keySig), bestT = CH_MAJ;
	int tonic = TonicPc(keySig);
	for (int root = 0; root < 12; root++) {
		for (int t = 0; t < 8; t++) {
			int sc = 0;
			int matched = 0;
			for (int k = 0; k < kN[t]; k++) {
				int v = pc[(root + kIv[t][k]) % 12];
				if (v > 0) matched++;
				sc += v * (k == 0 ? 3 : 2);
			}
			if (matched < kN[t] - (kN[t] >= 4 ? 1 : 0)) sc -= 80;
			if (root == tonic) sc += 12;
			if (lowPc == root) sc += 18;
			if (prevRoot == root) sc += 6;
			if (sc > bestS) { bestS = sc; bestR = root; bestT = t; }
		}
	}
	*outRoot = bestR;
	*outType = bestT;
	*outVoiced = 1;
}

static int ToneBelow(int midi, int root, int type)
{
	int tones[4];
	int nt = ChordTones(root, type, 0, tones, 4);
	for (int d = 2; d <= 12; d++) {
		int m = midi - d;
		if (m < 36) break;
		int pc = (m % 12 + 12) % 12;
		for (int i = 0; i < nt; i++)
			if ((tones[i] % 12) == pc) return m;
	}
	int fb = midi - ThirdIv(type);
	if (fb < 36) fb += 12;
	return fb;
}

static void DecorateMelody(Ctx* c, int melody, int harm, int keySig, const BarInfo* bars, int nBars, int pitchMode)
{
	if (melody < 0 || !(c->jobs & (SC_AJ_MEL_VOL | SC_AJ_MEL_PITCH))) return;
	int n0 = *c->n;
	int lastVel = -1;
	int idx = 0;
	for (int i = 0; i < n0; i++) {
		ScEvent& e = c->ev[i];
		if ((int)e.ch != melody || !IsNoteKind(e.kind)) continue;
		int midi = PitchOf(e, c->isFm);
		if (midi < 0) continue;
		int bi = 0;
		while (bi + 1 < nBars && bars[bi + 1].t0 <= e.tick) bi++;
		const BarInfo& bar = bars[bi];
		uint32_t off = e.tick - bar.t0;
		int beat = bar.len / (bar.len >= 144 ? 3 : 4);
		if (beat < 12) beat = 12;
		int down = (off % (uint32_t)beat) < (uint32_t)(beat / 5);
		int accent = down ? 114 : ((off % (uint32_t)beat) < (uint32_t)(beat / 2) ? 92 : 74);
		if (c->jobs & SC_AJ_MEL_VOL) {
			if (!c->isFm) e.b = (uint8_t)accent;
			if (accent != lastVel) {
				if (c->isFm) {
					int ssg = (melody >= 3 && melody <= 5);
					int lv = ssg ? (8 + (accent - 70) / 8) : (26 - accent / 6);
					if (ssg && lv < 8) lv = 8;
					if (ssg && lv > 15) lv = 15;
					if (!ssg && lv < 4) lv = 4;
					if (!ssg && lv > 26) lv = 26;
					Push(c, e.tick, melody, SC_EV_FM_VOL, (uint8_t)lv, ssg ? 1 : 0, 0, 0);
				} else {
					Push(c, e.tick, melody, SC_EV_VELO, (uint8_t)accent, 0, 0, 0);
				}
				lastVel = accent;
			}
		}
		if (!(c->jobs & SC_AJ_MEL_PITCH) || pitchMode <= 0) { idx++; continue; }
		int harmMidi = ToneBelow(midi, bar.root, bar.type);
		int dur = e.dur ? (int)e.dur : beat;
		if (harm >= 0 && dur >= beat / 2)
			PlaceNote(c, harm, e.tick, harmMidi, dur * 9 / 10, accent - 18, 80);
		if (harm >= 0 && pitchMode >= 3 && down && midi + 12 <= 96)
			PlaceNote(c, harm, e.tick, midi + 12, dur / 2, accent - 28, 70);
		idx++;
		(void)keySig;
	}
}

/* 16小節の定番フォーム。短い1小節ループのコピーではない。
   0=フル 1=リフト 2=フィル 3=抜き */
enum { PH_FULL = 0, PH_LIFT = 1, PH_FILL = 2, PH_SPARSE = 3 };

static int BodyPhase(int pat, int bodyBar)
{
	static const uint8_t form[SC_ARRANGE_COUNT][16] = {
		{ 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 2 },
		{ 3, 3, 3, 3, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 1, 2 },
		{ 0, 0, 3, 1, 0, 0, 3, 2, 0, 0, 3, 1, 0, 0, 1, 2 },
		{ 0, 0, 0, 0, 0, 0, 3, 3, 3, 3, 0, 0, 1, 1, 1, 2 },
		{ 0, 0, 1, 1, 1, 1, 1, 2, 0, 0, 1, 1, 1, 1, 2, 2 },
		{ 0, 0, 0, 2, 0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2 },
		{ 0, 0, 0, 0, 1, 1, 1, 2, 0, 0, 3, 3, 1, 1, 1, 2 },
		{ 3, 3, 0, 0, 0, 0, 2, 2, 0, 0, 0, 1, 1, 1, 2, 2 },
		{ 0, 1, 0, 1, 0, 1, 0, 2, 0, 1, 0, 1, 0, 1, 0, 2 },
		{ 0, 0, 0, 1, 1, 1, 2, 2, 0, 0, 0, 1, 1, 1, 2, 2 },
		{ 3, 0, 3, 0, 0, 1, 1, 2, 3, 0, 3, 0, 1, 1, 1, 2 },
		{ 0, 0, 1, 0, 0, 1, 0, 2, 1, 1, 1, 1, 0, 0, 2, 2 },
		{ 0, 3, 0, 3, 1, 1, 1, 2, 0, 0, 0, 0, 1, 1, 2, 2 },
		{ 1, 1, 0, 0, 0, 0, 2, 2, 1, 1, 1, 0, 0, 0, 2, 2 },
		{ 0, 0, 2, 0, 0, 2, 0, 2, 1, 1, 2, 1, 1, 2, 0, 2 },
		{ 3, 3, 1, 1, 0, 0, 0, 2, 0, 0, 1, 1, 1, 1, 2, 2 }
	};
	if (pat < 0) pat = 0;
	if (pat >= SC_ARRANGE_COUNT) pat = 0;
	int i = bodyBar % 16;
	if (i < 0) i = 0;
	return (int)form[pat][i];
}

static void KeepDrum(uint16_t* m, uint16_t keep)
{
	*m = (uint16_t)(*m & keep);
}

static uint16_t QuarterMask(int div)
{
	if (div >= 16) return (uint16_t)0x1111;
	if (div >= 12) return (uint16_t)0x0111;
	return (uint16_t)0x1111;
}

static uint16_t BackbeatMask(int div)
{
	if (div >= 16) return (uint16_t)0x1010;
	if (div >= 12) return (uint16_t)0x0110;
	return (uint16_t)0x0010;
}

static uint16_t LastBeatMask(int div)
{
	if (div >= 16) return (uint16_t)0xF000;
	if (div >= 12) return (uint16_t)0x0F00;
	return (uint16_t)0xF000;
}

static void RootBass(uint8_t* bass)
{
	for (int i = 1; i < 16; i++) bass[i] = 0;
	if (!bass[0]) bass[0] = 1;
}

static void ClearDrums(uint16_t* drm)
{
	for (int v = 0; v < DV_COUNT; v++) drm[v] = 0;
}

/* イントロ=カウントインからフィル、間奏=ブレイクダウンからフィル、
   エンディング=グルーヴ、フィル、1拍目の締め。 */
static int IntroHot(int style)
{
	switch (style) {
	case SC_AS_GAME: case SC_AS_CHIPTUNE: case SC_AS_ANISON:
	case SC_AS_FANFARE: case SC_AS_MARCH: case SC_AS_METAL:
	case SC_AS_EDM: case SC_AS_SAMBA: case SC_AS_DISCO:
	case SC_AS_ORCH: case SC_AS_FUSION: case SC_AS_TRANCE:
	case SC_AS_DNB: case SC_AS_PUNK: case SC_AS_ROCK:
	case SC_AS_CITYPOP: case SC_AS_KAYOKYOKU: case SC_AS_SURF:
		return 1;
	default:
		return 0;
	}
}

/* 静かなスタイルはハットの埋めとベースの8分ゴーストを足さない。 */
static int StyleQuiet(int style)
{
	switch (style) {
	case SC_AS_BALLAD: case SC_AS_AMBIENT: case SC_AS_PIANO:
	case SC_AS_ENKA: case SC_AS_WALTZ: case SC_AS_ACOUSTIC:
	case SC_AS_LOFI:
		return 1;
	default:
		return 0;
	}
}

static int SlotN(int div)
{
	int n = div < 16 ? div : 16;
	if (n < 1) n = 1;
	return n;
}

static uint16_t SlotMask(int div)
{
	int n = SlotN(div);
	return (n >= 16) ? 0xFFFFu : (uint16_t)((1u << n) - 1);
}

static uint16_t RotBits(uint16_t m, int div, int sh)
{
	int n = SlotN(div);
	uint16_t low = SlotMask(div);
	m = (uint16_t)(m & low);
	if (n <= 1) return m;
	sh %= n;
	if (sh < 0) sh += n;
	if (sh == 0) return m;
	return (uint16_t)(((m << sh) | (m >> (n - sh))) & low);
}

static uint16_t DropNth(uint16_t m, int div, int mod, int eq)
{
	int n = SlotN(div);
	if (mod < 1) return m;
	uint16_t o = 0;
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (!(m & (uint16_t)(1u << i))) continue;
		if ((k % mod) != eq) o = (uint16_t)(o | (uint16_t)(1u << i));
		k++;
	}
	return o ? o : m;
}

static const uint8_t kWalk[8][8] = {
	{ 1, 5, 8, 3, 1, 2, 5, 1 },
	{ 1, 8, 5, 3, 2, 5, 8, 1 },
	{ 1, 1, 5, 1, 8, 5, 3, 1 },
	{ 1, 5, 1, 3, 5, 8, 2, 1 },
	{ 1, 3, 5, 8, 5, 3, 1, 5 },
	{ 5, 1, 8, 5, 3, 1, 2, 5 },
	{ 1, 2, 3, 5, 8, 5, 3, 1 },
	{ 8, 5, 3, 1, 5, 3, 8, 1 }
};

static void WalkBass(uint8_t* bass, int n, int pat, uint16_t kicks)
{
	if (!bass) return;
	int nth = 0;
	for (int i = 0; i < n && i < 16; i++) {
		if (!bass[i]) continue;
		bass[i] = kWalk[pat & 7][nth & 7];
		nth++;
	}
	if ((pat & 7) == 3 && kicks) {
		for (int i = 0; i < n && i < 16; i++)
			if ((kicks & (uint16_t)(1u << i)) && !bass[i])
				bass[i] = kWalk[3][i & 7];
	}
}

/* アレンジ A〜H は同じスタイルでも形が違う。ドラムの格子は残し、上ものを組み替える。 */
static void ApplyArrangeShape(int pat, int style, int div,
	uint16_t* drm, uint8_t* bass, uint16_t* comp, uint16_t* pad,
	uint16_t* arp, uint16_t* harm, uint16_t* cnt, uint16_t* stab, int keepFill)
{
	int n = SlotN(div);
	uint16_t low = SlotMask(div);
	int quiet = StyleQuiet(style);
	uint16_t hats = (drm && drm[DV_HAT]) ? drm[DV_HAT] : 0;
	uint16_t kicks = (drm && drm[DV_KICK]) ? drm[DV_KICK] : 0;
	uint16_t snares = (drm && drm[DV_SNARE]) ? drm[DV_SNARE] : 0;
	if (!hats) hats = kicks;
	if (pad && !quiet && (pat & 7) != 4) {
		if (kicks) *pad = (uint16_t)(kicks & low);
		else {
			uint16_t h = 1;
			if (n >= 2) h = (uint16_t)(h | (uint16_t)(1u << (n / 2)));
			*pad = (uint16_t)(h & low);
		}
	}
	switch (pat & 7) {
	case 0:
		if (!quiet && comp && *comp) {
			int bits = 0;
			for (int i = 0; i < n; i++) if (*comp & (uint16_t)(1u << i)) bits++;
			if (bits < 2 && kicks) *comp = (uint16_t)((*comp | kicks) & low);
		}
		break;
	case 1:
		if (comp && *comp) *comp = RotBits(*comp, div, n >= 8 ? 2 : 1);
		if (arp && *arp) *arp = RotBits(*arp, div, 1);
		if (!keepFill && drm && drm[DV_HAT]) drm[DV_HAT] = RotBits(drm[DV_HAT], div, 1);
		break;
	case 2:
		if (comp) {
			uint16_t back = 0;
			for (int i = 1; i < n; i += 2)
				back = (uint16_t)(back | (uint16_t)(1u << i));
			if (*comp) back = (uint16_t)(back & (uint16_t)(*comp | RotBits(*comp, div, 1) | kicks));
			if (!back) back = (uint16_t)(1u << 1);
			*comp = (uint16_t)(back & low);
		}
		if (stab && snares) *stab = (uint16_t)(snares & low);
		break;
	case 3:
		if (arp && hats) *arp = (uint16_t)(hats & low);
		if (comp) *comp = (uint16_t)((kicks | (snares ? snares : kicks)) & low);
		if (!keepFill && drm && !quiet && drm[DV_HAT]) {
			uint16_t g = drm[DV_HAT];
			for (int i = 1; i < n; i++)
				if (drm[DV_HAT] & (uint16_t)(1u << (i - 1)))
					g = (uint16_t)(g | (uint16_t)(1u << i));
			drm[DV_HAT] = (uint16_t)(g & low);
		}
		if (drm && !drm[DV_OHAT] && drm[DV_HAT]) {
			uint16_t o = 0;
			for (int i = 0; i < n; i++)
				if ((i % 8) == 4 || (i % 8) == 7)
					o = (uint16_t)(o | (uint16_t)(1u << i));
			drm[DV_OHAT] = (uint16_t)(o & drm[DV_HAT] & low);
		}
		break;
	case 4:
		if (comp) {
			uint16_t h = 1;
			if (n >= 2) h = (uint16_t)(h | (uint16_t)(1u << (n / 2)));
			*comp = (uint16_t)(h & low);
		}
		if (harm) {
			uint16_t e = 0;
			int step = (n >= 16) ? 2 : 1;
			for (int i = 0; i < n; i += step) e = (uint16_t)(e | (uint16_t)(1u << i));
			*harm = (uint16_t)(e & low);
		}
		if (pad) *pad = 1;
		break;
	case 5:
		if (comp && *comp) *comp = RotBits(*comp, div, n > 1 ? n - 1 : 0);
		if (arp && *arp) *arp = RotBits(*arp, div, n > 1 ? n - 1 : 0);
		if (!keepFill && drm && drm[DV_KICK])
			drm[DV_KICK] = RotBits(drm[DV_KICK], div, n >= 8 ? n / 4 : 1);
		break;
	case 6:
		if (comp && *comp) {
			uint16_t m = 0;
			for (int i = 0; i < n / 2; i++)
				if (*comp & (uint16_t)(1u << i)) m = (uint16_t)(m | (uint16_t)(1u << i));
			if (!m) m = 1;
			*comp = m;
		}
		if (arp) {
			uint16_t m = 0;
			for (int i = n / 2; i < n; i++)
				if (!hats || (hats & (uint16_t)(1u << i)) || i == n / 2)
					m = (uint16_t)(m | (uint16_t)(1u << i));
			*arp = (uint16_t)(m & low);
		}
		break;
	default:
		if (arp) *arp = (uint16_t)((kicks | hats) & low);
		if (comp && snares) *comp = (uint16_t)(snares & low);
		else if (comp && n > 1) *comp = (uint16_t)(1u << (n / 2));
		if (cnt && hats) *cnt = (uint16_t)(hats & low);
		break;
	}
	WalkBass(bass, n, pat, kicks);
	(void)stab;
}

/* 2人目以降は一人を分割しない。スタイルの譜面を回した別のドラマー。 */
static int DrummerOn(int di, int voice, int slot, int div, const uint16_t* drm, int pat)
{
	if (!drm || di <= 0) return 0;
	int n = SlotN(div);
	if (slot < 0 || slot >= n) return 0;
	uint16_t src = drm[voice];
	if ((di % 3) == 2 && voice == DV_KICK)
		src = drm[DV_SNARE] ? drm[DV_SNARE] : drm[DV_KICK];
	else if ((di % 3) == 2 && (voice == DV_SNARE || voice == DV_CLAP))
		src = drm[DV_KICK];
	else if ((di % 3) == 1 && voice == DV_HAT)
		src = drm[DV_HAT] ? drm[DV_HAT] : drm[DV_RIDE];
	if (!src) {
		if (voice == DV_HAT || voice == DV_OHAT || voice == DV_RIDE) src = drm[DV_HAT] ? drm[DV_HAT] : drm[DV_KICK];
		else if (voice == DV_SNARE || voice == DV_CLAP || voice == DV_RIM) src = drm[DV_SNARE] ? drm[DV_SNARE] : drm[DV_KICK];
		else src = drm[DV_KICK];
	}
	int sh = (di * (n / 2) + pat * (voice + 1) + voice) % n;
	if (sh < 0) sh += n;
	uint16_t alt = RotBits(src, div, sh);
	int on = (alt & (uint16_t)(1u << slot)) ? 1 : 0;
	int lead = (drm[voice] & (uint16_t)(1u << slot)) ? 1 : 0;
	if (on && lead && !(voice == DV_KICK && slot == 0)) on = 0;
	if (voice == DV_KICK && slot == 0) on = 1;
	return on;
}

static int MelodySlot(int pat, int slot, int div, uint16_t comp, uint16_t bass, uint16_t hat)
{
	int n = SlotN(div);
	if (slot < 0 || slot >= n) return 0;
	switch (pat & 7) {
	case 0: return (slot % 4) == 0;
	case 1: return (bass & (uint16_t)(1u << slot)) ? 1 : 0;
	case 2: return (comp & (uint16_t)(1u << slot)) ? 1 : 0;
	case 3: return (slot == 0) || ((hat & (uint16_t)(1u << slot)) && ((slot & 1) == 0));
	case 4: return slot == 0 || slot == n / 2;
	case 5: return (comp & (uint16_t)(1u << slot)) || (slot > 0 && (comp & (uint16_t)(1u << (slot - 1))));
	case 6: return slot < n / 2 && ((bass & (uint16_t)(1u << slot)) || (slot % 4) == 0);
	default: return slot >= n / 2 && ((slot & 1) == 0);
	}
}

#include "CSasamiPhraseBook.inl"
#include "CSasamiArrangeNote.inl"

static uint16_t Fit16(uint16_t m, int div)
{
	int n = SlotN(div);
	if (n >= 16) return (uint16_t)(m & SlotMask(div));
	uint16_t o = 0;
	for (int i = 0; i < n; i++) {
		int src = (i * 16) / n;
		if (m & (uint16_t)(1u << src))
			o = (uint16_t)(o | (uint16_t)(1u << i));
	}
	return o;
}

static void BassOn(uint8_t* b, int n, uint16_t hits, const uint8_t* deg, int nd)
{
	if (!b) return;
	memset(b, 0, 16);
	if (n > 16) n = 16;
	if (nd < 1) nd = 1;
	int k = 0;
	for (int i = 0; i < n; i++) {
		if (!(hits & (uint16_t)(1u << i))) continue;
		b[i] = (uint8_t)SafeDeg(deg[k % nd]);
		k++;
	}
}

#include "CSasamiArrangeLong.inl"


/* 譜面はループ長のチャートだけ。8小節回転とフレーズ帳では上書きしない。 */
static void DevelopBar(int style, int pat, int bar, int div, int section,
	uint16_t* drm, uint8_t* bass, uint16_t* comp, uint16_t* pad,
	uint16_t* arp, uint16_t* harm, uint16_t* cnt, uint16_t* stab, uint16_t* color)
{
	if (!drm || !bass) return;
	WriteLiveChart(style, pat, bar, div, section, drm, bass, comp, pad, arp, harm, cnt, stab, color);
	return;
}

/* 0=小節に一つ  1=ところどころ後半が変わる  2=奇数小節は2分で二つ */
static int HalfHarmRate(int style)
{
	switch (style) {
	case SC_AS_JAZZ: case SC_AS_BEBOP: case SC_AS_SWINGJAZZ:
	case SC_AS_FUNK: case SC_AS_GOSPEL: case SC_AS_FUSION:
	case SC_AS_BLUES: case SC_AS_SOUL: case SC_AS_CITYPOP:
	case SC_AS_KAYOKYOKU: case SC_AS_GAME: case SC_AS_ANISON:
	case SC_AS_DISCO: case SC_AS_RNB: case SC_AS_ROCKNROLL:
	case SC_AS_LATIN: case SC_AS_SAMBA:
		return 2;
	case SC_AS_16BEAT: case SC_AS_8BEAT: case SC_AS_BOSSA:
	case SC_AS_NEOSOUL: case SC_AS_SKA: case SC_AS_REGGAE:
	case SC_AS_HIPHOP: case SC_AS_ACOUSTIC: case SC_AS_PIANO:
	case SC_AS_BALLAD: case SC_AS_ENKA: case SC_AS_SLOWROCK:
	case SC_AS_COUNTRY: case SC_AS_FLAMENCO: case SC_AS_TANGO:
	case SC_AS_AFROBEAT: case SC_AS_SLOWBLUES: case SC_AS_SHUFFLE:
	case SC_AS_MARCH: case SC_AS_POLKA: case SC_AS_SURF: case SC_AS_ORCH:
		return 1;
	default:
		return 0;
	}
}

static void SecondChord(int root, int type, int bar, int style, int pat, int* r2, int* t2)
{
	int off = 7;
	int ty = CH_7;
	switch ((bar + style * 3 + pat) & 3) {
	case 0: off = 5; ty = (type == CH_MIN || type == CH_M7) ? CH_MIN : CH_MAJ; break;
	case 1: off = 7; ty = CH_7; break;
	case 2: off = 2; ty = CH_MIN; break;
	default: off = 9; ty = CH_MIN; break;
	}
	*r2 = (root + off) % 12;
	*t2 = ty;
	if (*r2 == root && *t2 == type) {
		*r2 = (root + 7) % 12;
		*t2 = CH_7;
	}
}

static void ApplyHalfBars(BarInfo* bars, int nBars, int style, int pat)
{
	if (!bars || nBars < 1) return;
	if (pat < 0) pat = 0;
	const int rate = HalfHarmRate(style);
	int body = 0;
	for (int i = 0; i < nBars; i++) {
		bars[i].root2 = -1;
		bars[i].type2 = 0;
		int barAt = i;
		if (bars[i].section == SEC_BODY) barAt = body++;
		int orn = ArrangeOrnament(ArrangeKind(style, pat));
		int forceHalf = (orn & ORN_HALF) ? 1 : 0;
		if (bars[i].len < 168) continue;
		if (!rate && !forceHalf) continue;
		if (bars[i].section == SEC_END) continue;
		if (bars[i].section == SEC_INTRO && StyleQuiet(style)) continue;
		if (bars[i].root < 0) continue;
		int want = forceHalf ? ((barAt + pat) & 1)
			: (orn & ORN_NOHALF) ? 0
			: (rate == 2) ? ((barAt + pat) & 1) : (((barAt + style + pat) & 3) == 2);
		if (!want) continue;
		SecondChord(bars[i].root, bars[i].type, barAt, style, pat, &bars[i].root2, &bars[i].type2);
	}
}

/* 伴奏の中に、小節単位の歌い回しを置く。毎小節ではない。 */
static int HookLine(int style, int bar, int pat)
{
	if (style == SC_AS_DRUMSOLO) return 0;
	switch (style) {
	case SC_AS_BALLAD: case SC_AS_CITYPOP: case SC_AS_KAYOKYOKU:
	case SC_AS_ENKA: case SC_AS_PIANO: case SC_AS_BOSSA:
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP:
	case SC_AS_SOUL: case SC_AS_GOSPEL: case SC_AS_GAME:
	case SC_AS_ANISON: case SC_AS_FUSION: case SC_AS_ACOUSTIC:
	case SC_AS_SLOWROCK: case SC_AS_RNB: case SC_AS_NEOSOUL:
		return ((bar + pat) & 3) == 0;
	default:
		return ((bar + pat + style) & 7) == 4;
	}
}

/* 後半2拍だけ 32 分のアルペジオが続く小節。 */
static int ArpRun32(int style, int bar, int pat)
{
	switch (style) {
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE:
	case SC_AS_DNB: case SC_AS_EDM: case SC_AS_CHIPTUNE:
	case SC_AS_GAME: case SC_AS_ANISON: case SC_AS_FUSION:
	case SC_AS_PIANO: case SC_AS_SYNTHWAVE: case SC_AS_CITYPOP:
	case SC_AS_DISCO: case SC_AS_RNB: case SC_AS_NEOSOUL:
	case SC_AS_BREAKS:
		break;
	default:
		return 0;
	}
	int m = (bar + pat + style) & 7;
	return (m == 1 || m == 5) ? 1 : 0;
}

/* 64 分が4つ続くじゃらん。返すのはその 16 分スロット。無ければ -1。 */
static int JaranAt(int style, int bar, int pat, int div, int section)
{
	if (section == SEC_END || div < 8) return -1;
	if (section == SEC_INTRO && StyleQuiet(style)) return -1;
	switch (style) {
	case SC_AS_DRUMSOLO: case SC_AS_TRAP: case SC_AS_DRILL:
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_DNB:
	case SC_AS_DUBSTEP:
		return -1;
	default:
		break;
	}
	int love = 0;
	switch (style) {
	case SC_AS_FLAMENCO: case SC_AS_ENKA: case SC_AS_KAYOKYOKU:
	case SC_AS_GAME: case SC_AS_ANISON: case SC_AS_JAZZ:
	case SC_AS_BEBOP: case SC_AS_PIANO: case SC_AS_ACOUSTIC:
	case SC_AS_LATIN: case SC_AS_BOSSA: case SC_AS_TANGO:
	case SC_AS_SURF: case SC_AS_CITYPOP: case SC_AS_FUSION:
		love = 1;
		break;
	default:
		break;
	}
	int period = love ? 4 : 8;
	if (((bar + pat + style) % period) != period - 1) return -1;
	int s = ((bar + style) & 1) ? (div / 2 - 2) : (div - 2);
	if (s < 0) s = 0;
	if (s & 1) s--;
	if (s >= div) s = div - 2;
	if (s < 0) return -1;
	return s;
}

static void ApplyForm(int section, int last, int phase, int div,
	int introI, int introN, int endFromEnd,
	uint16_t* drm, uint8_t* bass, int* chordMask, int* chordSlots, int* skipChord,
	int introHot)
{
	const uint16_t q = QuarterMask(div);
	const uint16_t back = BackbeatMask(div);
	const uint16_t beat4 = LastBeatMask(div);
	*skipChord = 0;
	if (section == SEC_INTRO) {
		/* 派手なスタイルはイントロも和音を残す。静かなスタイルだけカウントイン。 */
		const int drumOnly = !introHot && (introN > 1 && introI < introN / 2);
		if (last) {
			drm[DV_HAT] = 0;
			drm[DV_OHAT] = 0;
			drm[DV_RIDE] = 0;
			drm[DV_KICK] = 1;
			drm[DV_CRASH] = 1;
			drm[DV_SNARE] = beat4;
		} else if (drumOnly) {
			drm[DV_SNARE] = 0;
			drm[DV_TOM] = 0;
			drm[DV_CRASH] = 0;
			drm[DV_CLAP] = 0;
			drm[DV_OHAT] = 0;
			drm[DV_RIM] = 0;
			KeepDrum(&drm[DV_KICK], 1);
			if (!drm[DV_KICK]) drm[DV_KICK] = 1;
			KeepDrum(&drm[DV_HAT], q);
			if (!drm[DV_HAT] && !drm[DV_RIDE]) drm[DV_HAT] = q;
			KeepDrum(&drm[DV_RIDE], q);
			for (int i = 0; i < 16; i++) bass[i] = 0;
			*skipChord = 1;
		} else {
			drm[DV_CRASH] = 0;
			drm[DV_TOM] = 0;
			drm[DV_OHAT] = 0;
			if (introHot && introI == 0)
				drm[DV_CRASH] = 1;
		}
	} else if (section == SEC_BREAK) {
		if (last) {
			drm[DV_HAT] = 0;
			drm[DV_OHAT] = 0;
			drm[DV_RIDE] = 0;
			drm[DV_KICK] = 1;
			drm[DV_SNARE] = beat4;
			drm[DV_CRASH] = 1;
		} else {
			drm[DV_CRASH] = 0;
			drm[DV_TOM] = 0;
			drm[DV_OHAT] = 0;
			drm[DV_CLAP] = 0;
			KeepDrum(&drm[DV_KICK], 1);
			if (!drm[DV_KICK]) drm[DV_KICK] = 1;
			KeepDrum(&drm[DV_SNARE], back);
			if (!drm[DV_SNARE]) drm[DV_SNARE] = back;
			KeepDrum(&drm[DV_HAT], q);
			if (!drm[DV_HAT] && !drm[DV_RIDE]) drm[DV_HAT] = q;
			KeepDrum(&drm[DV_RIDE], q);
		}
	} else if (section == SEC_END) {
		if (endFromEnd <= 0) {
			ClearDrums(drm);
			drm[DV_KICK] = 1;
			drm[DV_CRASH] = 1;
			drm[DV_SNARE] = 1;
		} else if (endFromEnd == 1) {
			drm[DV_HAT] = 0;
			drm[DV_OHAT] = 0;
			drm[DV_RIDE] = 0;
			drm[DV_KICK] = 1;
			drm[DV_SNARE] = beat4;
			drm[DV_CRASH] = 1;
		} else {
			drm[DV_OHAT] = 0;
			drm[DV_TOM] = 0;
			drm[DV_CRASH] = 0;
		}
	} else if (phase == PH_SPARSE) {
		KeepDrum(&drm[DV_KICK], 1);
		if (!drm[DV_KICK]) drm[DV_KICK] = 1;
		KeepDrum(&drm[DV_SNARE], back);
		if (!drm[DV_SNARE]) drm[DV_SNARE] = back;
		KeepDrum(&drm[DV_HAT], q);
		if (!drm[DV_HAT] && !drm[DV_RIDE]) drm[DV_HAT] = q;
		KeepDrum(&drm[DV_RIDE], q);
		drm[DV_OHAT] = 0;
		drm[DV_TOM] = 0;
		drm[DV_CRASH] = 0;
		drm[DV_CLAP] = 0;
	} else if (phase == PH_LIFT) {
		if (div >= 16) drm[DV_OHAT] = (uint16_t)(drm[DV_OHAT] | 0x1010);
		int s = (div >= 16) ? 12 : ((div >= 12) ? 8 : 0);
		if (s > 0 && s < 16 && !bass[s]) bass[s] = 2;
	} else if (phase == PH_FILL) {
		drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & (uint16_t)~beat4);
		drm[DV_OHAT] = (uint16_t)(drm[DV_OHAT] & (uint16_t)~beat4);
		drm[DV_RIDE] = (uint16_t)(drm[DV_RIDE] & (uint16_t)~beat4);
		drm[DV_SNARE] = (uint16_t)(drm[DV_SNARE] | beat4);
		drm[DV_CRASH] = (uint16_t)(drm[DV_CRASH] | 1);
	}
}

static void FillMsg(ScAutoArrangeResult* out)
{
	if (!out) return;
	if (out->err == SC_AE_NONOTES) {
		wcsncpy_s(out->msg, LL14(
			L"音符が無いので、本体の長さで小節数を指定してください。",
			L"No notes — set body length to choose how many bars to write.",
			L"Aucune note : impossible d'arranger.",
			L"Nessuna nota: niente da arrangiare.",
			L"No hay notas: no se puede arreglar.",
			L"음표가 없어 편곡할 수 없습니다.",
			L"没有音符，无法编曲。",
			L"لا توجد نغمات، لا يمكن التوزيع.",
			L"Нет нот — аранжировать нечего.",
			L"Keine Noten, nichts zu arrangieren.",
			L"Sem notas, não há o que arranjar.",
			L"Geen noten, niets om te arrangeren.",
			L"Brak nut, nie ma czego aranżować.",
			L"Nota yok, düzenlenecek bir şey yok."), _TRUNCATE);
		return;
	}
	if (out->err == SC_AE_OVERFLOW) {
		wcsncpy_s(out->msg, LL14(
			L"譜面が一杯で、途中までしか置けませんでした。",
			L"The score is full; only part of the arrangement fit.",
			L"Partition pleine : arrangement partiel.",
			L"Partitura piena: arrangiamento parziale.",
			L"Partitura llena: arreglo parcial.",
			L"악보가 가득 차 일부만 넣었습니다.",
			L"乐谱已满，只写入了一部分。",
			L"المدرج ممتلئ، كُتب جزء فقط.",
			L"Партитура полна, вошла только часть.",
			L"Partitur voll, nur ein Teil passte.",
			L"Partitura cheia; só entrou uma parte.",
			L"Partituur vol, slechts een deel paste.",
			L"Partytura pełna, weszła tylko część.",
			L"Partisyon dolu, yalnızca bir kısmı sığdı."), _TRUNCATE);
		return;
	}
	if (out->err == SC_AE_EMPTY || out->added <= 0) {
		wcsncpy_s(out->msg, LL14(
			L"足せる空きがありません。既存のパートはそのままです。",
			L"No free space to add parts. Existing parts were left as they are.",
			L"Pas de place libre. Les parties existantes sont intactes.",
			L"Nessuno spazio libero. Le parti esistenti restano.",
			L"No hay sitio libre. Las partes existentes siguen igual.",
			L"넣을 빈 자리가 없습니다. 기존 파트는 그대로입니다.",
			L"没有可添加的空位。原有声部保持不变。",
			L"لا فراغ للإضافة. الأجزاء الحالية بقيت كما هي.",
			L"Нет свободного места. Существующие партии не тронуты.",
			L"Kein freier Platz. Vorhandene Stimmen bleiben.",
			L"Sem espaço livre. As partes existentes ficam.",
			L"Geen vrije plek. Bestaande partijen blijven.",
			L"Brak wolnego miejsca. Istniejące partie zostają.",
			L"Eklenecek boş yer yok. Var olan partiler duruyor."), _TRUNCATE);
		out->err = SC_AE_EMPTY;
		return;
	}
	_snwprintf_s(out->msg, _TRUNCATE, LL14(
		L"追加 %d 音。ドラム %d、ベース %d、コード %d、ハモリ %d。元の音符は残しています。",
		L"Added %d notes. Drums %d, bass %d, chords %d, harmony %d. Original notes kept.",
		L"Ajout de %d notes. Batterie %d, basse %d, accords %d, harmonie %d. Notes d'origine gardées.",
		L"Aggiunte %d note. Batteria %d, basso %d, accordi %d, armonia %d. Note originali tenute.",
		L"Añadidas %d notas. Batería %d, bajo %d, acordes %d, armonía %d. Notas originales conservadas.",
		L"%d음 추가. 드럼 %d, 베이스 %d, 코드 %d, 하모니 %d. 원래 음표는 남겼습니다.",
		L"追加 %d 个音。鼓 %d，贝斯 %d，和弦 %d，和声 %d。原有音符保留。",
		L"أُضيفت %d نغمة. طبل %d، باص %d، أكورد %d، هارموني %d. النغمات الأصلية باقية.",
		L"Добавлено %d нот. Ударные %d, бас %d, аккорды %d, гармония %d. Исходные ноты сохранены.",
		L"%d Noten ergänzt. Drums %d, Bass %d, Akkorde %d, Harmonie %d. Ursprungsnoten bleiben.",
		L"Adicionadas %d notas. Bateria %d, baixo %d, acordes %d, harmonia %d. Notas originais mantidas.",
		L"%d noten toegevoegd. Drums %d, bas %d, akkoorden %d, harmonie %d. Oorspronkelijke noten blijven.",
		L"Dodano %d nut. Perkusja %d, bas %d, akordy %d, harmonia %d. Oryginalne nuty zostały.",
		L"%d nota eklendi. Davul %d, bas %d, akor %d, armoni %d. Özgün notalar duruyor."),
		out->added,
		out->chDrum >= 0 ? out->chDrum + 1 : 0,
		out->chBass >= 0 ? out->chBass + 1 : 0,
		out->chChord >= 0 ? out->chChord + 1 : 0,
		out->chHarm >= 0 ? out->chHarm + 1 : 0);
}

struct NpNote { uint32_t tick; int midi; int ch; };

static int __cdecl CmpNpNote(const void* va, const void* vb)
{
	const NpNote* a = (const NpNote*)va;
	const NpNote* b = (const NpNote*)vb;
	if (a->ch != b->ch) return a->ch - b->ch;
	if (a->tick < b->tick) return -1;
	if (a->tick > b->tick) return 1;
	return a->midi - b->midi;
}

static int LineFam(int style)
{
	switch (style) {
	case SC_AS_GAME: case SC_AS_CHIPTUNE: return 1;
	case SC_AS_METAL: case SC_AS_PUNK: case SC_AS_ROCK: case SC_AS_GRUNGE:
	case SC_AS_SURF: case SC_AS_SLOWROCK: return 2;
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE: case SC_AS_EDM:
	case SC_AS_SYNTHWAVE: case SC_AS_DNB: return 3;
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP: case SC_AS_SWING3:
	case SC_AS_BLUES: case SC_AS_SHUFFLE: case SC_AS_ROCKNROLL: return 4;
	case SC_AS_BOSSA: case SC_AS_SAMBA: case SC_AS_LATIN: case SC_AS_AFROBEAT:
	case SC_AS_TANGO: case SC_AS_FLAMENCO: return 5;
	case SC_AS_WALTZ: case SC_AS_POLKA: return 6;
	case SC_AS_BALLAD: case SC_AS_ENKA: case SC_AS_PIANO: case SC_AS_AMBIENT:
	case SC_AS_LOFI: case SC_AS_SLOWBLUES: case SC_AS_NEOSOUL: return 7;
	case SC_AS_ORCH: case SC_AS_FANFARE: case SC_AS_MARCH: return 8;
	case SC_AS_TRAP: case SC_AS_DRILL: case SC_AS_DUBSTEP: case SC_AS_HIPHOP:
	case SC_AS_RNB: return 9;
	default: return 0;
	}
}

static int BassDeg(int fam, int pat, int bar, int slot)
{
	static const uint8_t phrase[8][8] = {
		{ 1, 5, 3, 2, 1, 8, 5, 2 },
		{ 1, 1, 5, 3, 2, 5, 8, 1 },
		{ 1, 2, 5, 1, 8, 5, 3, 2 },
		{ 1, 5, 1, 2, 3, 5, 1, 8 },
		{ 1, 3, 5, 2, 1, 5, 8, 2 },
		{ 1, 1, 8, 5, 1, 2, 5, 3 },
		{ 1, 5, 2, 1, 3, 8, 5, 1 },
		{ 1, 2, 1, 5, 8, 2, 5, 1 }
	};
	if (fam == 6) {
		static const uint8_t w[3] = { 1, 5, 3 };
		return w[(slot / 3) % 3];
	}
	if (fam == 9) return (slot < 8) ? 1 : 7;
	return phrase[(bar + pat) & 7][(slot / 2) & 7];
}

/* 3〜6音。音域の中でコードトーンを重ね、小節ごとに転回する。 */
static int FillVoicing(int root, int type, int power, int nWant, int invert, int lo, int hi, int* out)
{
	if (nWant < 1) nWant = 1;
	if (nWant > 6) nWant = 6;
	if (lo < 0) lo = 0;
	if (hi > 127) hi = 127;
	if (hi < lo) hi = lo;
	int tones[4];
	int nt = ChordTones(root, type, power, tones, 4);
	if (nt < 1) { out[0] = lo; return 1; }
	int w = 0;
	for (int step = 0; step < 24 && w < nWant; step++) {
		int idx = (step + invert) % nt;
		int oct = (step + invert) / nt;
		int m = tones[idx] + 12 * (oct - 1);
		while (m < lo) m += 12;
		while (m > hi) m -= 12;
		int dup = 0;
		for (int i = 0; i < w; i++) if (out[i] == m) dup = 1;
		if (!dup) out[w++] = m;
	}
	while (w < nWant) {
		int m = out[0] + 12 * w;
		while (m > hi) m -= 12;
		if (m < lo) m = lo;
		int dup = 0;
		for (int i = 0; i < w; i++) if (out[i] == m) dup = 1;
		if (dup) break;
		out[w++] = m;
	}
	return w;
}

static int ArpPitch(int root, int type, int power, int step, int shape, int dup)
{
	int tones[4];
	int nt = ChordTones(root, type, power, tones, 4);
	if (nt < 1) return 72;
	int idx = 0;
	int oct = 0;
	const int n = nt;
	switch (shape % 5) {
	case 0: idx = step % n; oct = (step / n) & 1; break;
	case 1: idx = n - 1 - (step % n); oct = 1 - ((step / n) & 1); break;
	case 2: {
		int span = n * 2;
		int p = step % span;
		if (p >= n) { idx = span - 1 - p; oct = 1; }
		else { idx = p; oct = 0; }
		break;
	}
	case 3:
		if ((step & 1) == 0) { idx = 0; oct = 0; }
		else { idx = 1 + ((step / 2) % (n > 1 ? n - 1 : 1)); oct = (step / 4) & 1; }
		break;
	default: idx = step % n; oct = step & 1; break;
	}
	if (idx < 0) idx = 0;
	if (idx >= n) idx = n - 1;
	int m = tones[idx] + 12 * oct + 12 * (dup & 1);
	while (m < 60) m += 12;
	while (m > 96) m -= 12;
	return m;
}

static int DrumVolOf(int section, int phase, int bar, int hot, int half)
{
	int v = 100;
	if (section == SEC_INTRO) v = hot ? (half ? 120 : 92) : (half ? 76 : 52);
	else if (section == SEC_END) v = half ? 68 : 112;
	else if (phase == PH_FILL) v = half ? 124 : 104;
	else if (phase == PH_SPARSE) v = half ? 86 : 60;
	else if (phase == PH_LIFT) v = half ? 118 : 90;
	else {
		static const int wave[8] = { 84, 108, 120, 94, 72, 112, 124, 88 };
		v = wave[bar & 7];
		if (half) v += 16;
	}
	if (v < 40) v = 40;
	if (v > 127) v = 127;
	return v;
}

static int ExprOf(int section, int phase, int bar, int hot)
{
	if (section == SEC_INTRO) return hot ? 112 : 74;
	if (section == SEC_END) return 100;
	if (phase == PH_SPARSE) return 76;
	if (phase == PH_FILL) return 122;
	if (phase == PH_LIFT) return 108;
	int v = 86 + (bar & 7) * 5;
	if (v > 120) v = 120;
	return v;
}

static void PlaceVol(Ctx* c, int ch, uint32_t tick, int val)
{
	if (!c || ch < 0) return;
	if (val < 1) val = 1;
	if (val > 127) val = 127;
	if (c->isFm) {
		int lv = 4 + val / 8;
		if (lv > 26) lv = 26;
		Push(c, tick, ch, SC_EV_FM_VOL, (uint8_t)lv, 0, 0, 0);
	} else {
		Push(c, tick, ch, SC_EV_VOL, (uint8_t)val, (uint8_t)val, 0, 0);
	}
}

static void PlaceExpr(Ctx* c, int ch, uint32_t tick, int val)
{
	if (!c || c->isFm || ch < 0) return;
	if (val < 1) val = 1;
	if (val > 127) val = 127;
	Push(c, tick, ch, SC_EV_CC, 11, (uint8_t)val, 0, 0);
}

static void EmitPitches(Ctx* c, ScEvent* ev, int* evCount, int* nEmit, int merge,
	int ch, uint32_t tick, uint32_t slot0, uint32_t slot1,
	const int* pit, int np, int dur, int vel, int gate, int* removed, int bend)
{
	if (ch < 0) return;
	if (np < 1) {
		if (merge) MergeSlot(ev, evCount, nEmit, ch, slot0, slot1, -1, 1, removed, dur);
		return;
	}
	if (!merge && OnsetBusy(ev, *nEmit, ch, tick)) return;
	int solo = 0;
	const int* use = pit;
	int useN = np;
	if (c->isFm && FmMelodic(ch) && np > 1) {
		solo = MonoKeep(pit, np);
		use = &solo;
		useN = 1;
	}
	for (int i = 0; i < useN; i++) {
		if (merge && MergeSlot(ev, evCount, nEmit, ch, slot0, slot1, use[i], 0, removed, dur))
			continue;
		int v = vel - i * 5;
		if (v < 40) v = 40;
		PlaceNote(c, ch, tick, use[i], dur, v, gate);
	}
	if (bend) PlaceScoop(c, ch, tick);
}


static int BitOn(uint16_t m, int slot)
{
	return (slot >= 0 && slot < 16 && (m & (uint16_t)(1u << slot))) ? 1 : 0;
}

static int FirstBitN(uint16_t m, int div)
{
	int n = div < 16 ? div : 16;
	for (int i = 0; i < n; i++) if (m & (uint16_t)(1u << i)) return i;
	return -1;
}

static int LastBitN(uint16_t m, int div)
{
	int n = div < 16 ? div : 16;
	for (int i = n - 1; i >= 0; i--) if (m & (uint16_t)(1u << i)) return i;
	return -1;
}

static int NextBitN(uint16_t m, int slot, int div)
{
	int n = div < 16 ? div : 16;
	for (int i = slot + 1; i < n; i++) if (m & (uint16_t)(1u << i)) return i;
	return div;
}

static int SpanTicks(int slot, int next, int div, uint32_t t0, uint32_t tick, int barLen, int slotDur)
{
	uint32_t tNext = (next >= div) ? (t0 + (uint32_t)barLen)
		: (t0 + (uint32_t)((uint64_t)next * (uint64_t)barLen / (uint64_t)div));
	int span = (int)(tNext > tick ? tNext - tick : (uint32_t)slotDur);
	if (span < 2) span = 2;
	return span;
}

/* 次の打点まで伸ばすと全音符になる。スタイルごとの上限で切る。 */
static int CapSpan(int span, int slotDur, int maxSlots)
{
	if (slotDur < 1) slotDur = 1;
	if (maxSlots < 1) maxSlots = 1;
	int cap = slotDur * maxSlots;
	if (cap < 2) cap = 2;
	if (span > cap) span = cap;
	if (span < 2) span = 2;
	return span;
}

static int NthBit(uint16_t m, int slot)
{
	int n = 0;
	if (slot > 16) slot = 16;
	for (int i = 0; i < slot; i++) if (m & (uint16_t)(1u << i)) n++;
	return n;
}

/* スタイルの打点そのものを毎小節使う。間引いて偶数小節を1音にしない。
   which 0 = 裏拍（カラー）。4 = パッドは頭を伸ばし、奇数小節は終わりでもう一度。
   それ以外はスタイルの打点をそのまま返す。アレンジ差は phrase の回転で決まる。 */
static uint16_t PhraseHits(uint16_t home, int phrase, int div, int which)
{
	int n = div < 16 ? div : 16;
	uint16_t low = (n >= 16) ? 0xFFFFu : (uint16_t)((1u << n) - 1);
	home = (uint16_t)(home & low);
	int first = FirstBitN(home, n);
	int last = LastBitN(home, n);
	if (first < 0) return 0;
	if (which == 4) {
		uint16_t m = (uint16_t)(1u << first);
		if ((phrase & 1) && last > first) m = (uint16_t)(m | (uint16_t)(1u << last));
		return m;
	}
	if (which == 0) {
		uint16_t m = 0;
		int k = 0;
		for (int i = 0; i < n; i++) {
			if (!(home & (uint16_t)(1u << i))) continue;
			if (k & 1) m = (uint16_t)(m | (uint16_t)(1u << i));
			k++;
		}
		if (phrase == 7 && last > first) m = (uint16_t)(m | (uint16_t)(1u << last));
		return m ? m : (uint16_t)(1u << first);
	}
	return home;
}

static int GrooveVol(int style, int slot, int div)
{
	int ix = slot;
	if (div < 16 && div > 0) ix = slot * 16 / div;
	if (ix < 0) ix = 0;
	if (ix > 15) ix = 15;
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	const StyleDef& d = kDef[style];
	const uint16_t bit = (uint16_t)(1u << ix);
	int v = d.swing ? 64 : 74;
	if (Bits(d.kick) & bit) v = d.power ? 124 : 114;
	else if ((Bits(d.snare) & bit) || (Bits(d.clap) & bit)) v = d.power ? 120 : 106;
	else if (Bits(d.hat) & bit) v = 86;
	else if ((Bits(d.ohat) & bit) || (Bits(d.ride) & bit)) v = 96;
	if (StyleQuiet(style)) v -= 14;
	if (v < 40) v = 40;
	if (v > 127) v = 127;
	return v;
}

static int DrumHitVol(int style, int voice, int slot, int div, int introSoft)
{
	int v = GrooveVol(style, slot, div);
	const int power = (style >= 0 && style < SC_AS_COUNT) ? kDef[style].power : 0;
	(void)div;
	if (voice == DV_SNARE || voice == DV_CLAP)
		v = power ? 124 : (StyleQuiet(style) ? 78 : 110);
	else if (voice == DV_KICK)
		v = (slot == 0 || power) ? 122 : 102;
	else if (voice == DV_CRASH || voice == DV_OHAT)
		v = 112;
	else if (voice == DV_RIM)
		v = 82;
	if (introSoft) v -= 16;
	if (v < 36) v = 36;
	if (v > 127) v = 127;
	return v;
}

int ScAutoArrangeApply(ScEvent* ev, int* evCount, int evMax, const ScAutoArrangeOpt* opt, ScAutoArrangeResult* out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
		out->chDrum = out->chBass = out->chChord = out->chHarm = -1;
	}
	if (!ev || !evCount || !opt || evMax <= 0) return SC_AE_NONOTES;
	const int explicitTargets = opt->targetMask != 0;
	const int totalNotes = ScAutoArrangeCountNotes(ev, *evCount);
	const int variation = opt->variation < 0 ? 0 : opt->variation;
	const int useProg = opt->useProg || explicitTargets || totalNotes <= 0;
	const int rewrite = (opt->mode == SC_AM_REWRITE);
	s_arrGsMap = opt->gsMap;
	{
		int loop = opt->loopBars;
		if (loop < 16 || loop > 64) loop = StyleLoopBars(opt->style);
		s_loopBars = loop;
	}

	const int isFm = opt->isFm ? 1 : 0;
	int chMax = isFm ? SC_FM_TOTAL : SC_MIDI_CH;
	int lim = chMax;
	if (opt->ui && opt->ui->trackCount > 0 && opt->ui->trackCount < lim)
		lim = opt->ui->trackCount;

	int notes[32];
	int polyTicks[32];
	int tickCount[32];
	int sumPitch[32];
	memset(notes, 0, sizeof(notes));
	memset(polyTicks, 0, sizeof(polyTicks));
	memset(tickCount, 0, sizeof(tickCount));
	memset(sumPitch, 0, sizeof(sumPitch));
	for (int ch = 0; ch < lim && ch < 32; ch++) {
		if (DrumChannel(ev, *evCount, ch, isFm)) notes[ch] = 1000000;
	}
	NpNote* np = (NpNote*)HeapAlloc(GetProcessHeap(), 0, sizeof(NpNote) * (size_t)(*evCount > 0 ? *evCount : 1));
	if (!np) {
		if (out) { out->err = SC_AE_OVERFLOW; FillMsg(out); }
		return SC_AE_OVERFLOW;
	}
	int nn = 0;
	for (int i = 0; i < *evCount; i++) {
		if (!IsNoteKind(ev[i].kind)) continue;
		if ((int)ev[i].ch < 0 || (int)ev[i].ch >= lim) continue;
		if (DrumChannel(ev, *evCount, ev[i].ch, isFm)) continue;
		int m = PitchOf(ev[i], isFm);
		if (m < 0) continue;
		np[nn].tick = ev[i].tick;
		np[nn].midi = m;
		np[nn].ch = (int)ev[i].ch;
		nn++;
	}
	if (nn > 1) qsort(np, (size_t)nn, sizeof(NpNote), CmpNpNote);
	{
		int ch = -1, ticks = 0, poly = 0, sum = 0, count = 0, run = 0;
		uint32_t lastTick = 0xFFFFFFFFu;
		for (int i = 0; i <= nn; i++) {
			int boundary = (i == nn) || (ch >= 0 && np[i].ch != ch);
			if (boundary && ch >= 0) {
				if (run > 0) { ticks++; if (run >= 2) poly++; }
				if (ch < 32) {
					notes[ch] = count;
					polyTicks[ch] = poly;
					tickCount[ch] = ticks;
					sumPitch[ch] = sum;
				}
				run = 0;
				lastTick = 0xFFFFFFFFu;
				ticks = poly = sum = count = 0;
			}
			if (i == nn) break;
			if (np[i].ch != ch) ch = np[i].ch;
			if (np[i].tick != lastTick) {
				if (run > 0) { ticks++; if (run >= 2) poly++; }
				run = 0;
				lastTick = np[i].tick;
			}
			run++;
			count++;
			sum += np[i].midi;
		}
	}
	HeapFree(GetProcessHeap(), 0, np);

	int chordSrc = -1, melodyCh = -1, bassHave = -1;
	int bestPoly = 3;
	for (int ch = 0; ch < lim && ch < 32; ch++) {
		int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
		if (polyTicks[ch] >= 4 && ratio >= 35 && polyTicks[ch] > bestPoly) {
			bestPoly = polyTicks[ch];
			chordSrc = ch;
		}
	}
	int cur = opt->curPart;
	if (cur >= 0 && cur < lim && cur < 32 && notes[cur] > 0 && notes[cur] < 1000000) {
		int ratio = tickCount[cur] ? (polyTicks[cur] * 100) / tickCount[cur] : 0;
		if (ratio < 50) melodyCh = cur;
	}
	if (melodyCh < 0) {
		int bestScore = -1;
		for (int ch = 0; ch < lim && ch < 32; ch++) {
			if (notes[ch] < 1 || notes[ch] >= 1000000) continue;
			int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
			if (ratio >= 50) continue;
			int avg = sumPitch[ch] / notes[ch];
			int score = notes[ch] * 128 + avg;
			if (score > bestScore) { bestScore = score; melodyCh = ch; }
		}
	}
	if (chordSrc >= 0 && chordSrc == melodyCh) {
		int ratio = tickCount[chordSrc] ? (polyTicks[chordSrc] * 100) / tickCount[chordSrc] : 0;
		if (ratio >= 50) melodyCh = -1;
		else chordSrc = -1;
	}
	for (int ch = 0; ch < lim && ch < 32; ch++) {
		if (notes[ch] < 8 || notes[ch] >= 1000000 || ch == melodyCh || ch == chordSrc) continue;
		int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
		if (ratio >= 40) continue;
		int avg = sumPitch[ch] / notes[ch];
		if (avg <= 48) { bassHave = ch; break; }
	}

	const int preferBassFm[] = { 5, 4, 3, 2, 1, 0, 7, 8, 9 };
	const int preferChFm[] = { 0, 1, 2, 7, 8, 9, 3, 4, 5 };
	const int preferHFm[] = { 7, 8, 9, 1, 0, 2, 4, 5, 3 };
	const int preferMid[] = { 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 0 };
	int chDrum = -1, chBass = -1, chChord = -1, chHarm = -1;
	int layers = opt->jobs & (SC_AJ_DRUM | SC_AJ_BASS | SC_AJ_CHORD);
	if ((opt->jobs & SC_AJ_DRUM) && layers) {
		if (isFm) chDrum = (6 < lim) ? 6 : -1;
		else if (9 < lim && (ScMidiPartIsDrum(ev, *evCount, 9, 0xFFFFFFFFu) || notes[9] == 0)) chDrum = 9;
		else if (25 < lim && (notes[25] == 0 || ScMidiPartIsDrum(ev, *evCount, 25, 0xFFFFFFFFu))) chDrum = 25;
		else chDrum = PickEmpty(notes, lim, 0, melodyCh, chordSrc, bassHave, preferMid, 13);
	}
	if (opt->jobs & SC_AJ_BASS) {
		if (bassHave >= 0) chBass = bassHave;
		else chBass = PickEmpty(notes, lim, isFm, melodyCh, chordSrc, chDrum, isFm ? preferBassFm : preferMid, isFm ? 9 : 13);
	}
	if ((opt->jobs & SC_AJ_CHORD) && chordSrc < 0)
		chChord = PickEmpty(notes, lim, isFm, melodyCh, chordSrc, chBass, isFm ? preferChFm : preferMid, isFm ? 9 : 13);
	if ((opt->jobs & SC_AJ_MEL_PITCH) && opt->pitchMode > 0 && melodyCh >= 0)
		chHarm = PickEmpty(notes, lim, isFm, melodyCh, chBass, chChord, isFm ? preferHFm : preferMid, isFm ? 9 : 13);

	/* --- v2: 明示ターゲット / 役割割り当て / 複数ドラム ch -------------------
	   ユーザーがチェックしたパートだけをアレンジ対象にし、drumMask の ch を
	   ドラム出力に使う。メロディ役は書き換えでも消さず、強弱/ハモリで作り込む。 */
	int genMel = 0;
	int drumChs[32]; uint8_t drumChSlot[32]; int nDrum = 0;
	int bassCh[32]; int nBass = 0;
	int chordCh[32]; int nChord = 0;
	int padCh[32]; int nPad = 0;
	int arpCh[32]; int nArp = 0;
	int harmCh[32]; int nHarm = 0;
	int cntCh[32]; int nCnt = 0;
	int stabCh[32]; int nStab = 0;
	int colorCh[32]; int nColor = 0;
	uint32_t deleteMask = 0;
	auto pushRole = [&](int ch, int role) {
		switch (role) {
		case SC_PR_BASS: if (nBass < 32) bassCh[nBass++] = ch; break;
		case SC_PR_CHORD: if (nChord < 32) chordCh[nChord++] = ch; break;
		case SC_PR_PAD: if (nPad < 32) padCh[nPad++] = ch; break;
		case SC_PR_ARPEGGIO: if (nArp < 32) arpCh[nArp++] = ch; break;
		case SC_PR_HARMONY: if (nHarm < 32) harmCh[nHarm++] = ch; break;
		case SC_PR_COUNTER: if (nCnt < 32) cntCh[nCnt++] = ch; break;
		case SC_PR_STAB: if (nStab < 32) stabCh[nStab++] = ch; break;
		case SC_PR_COLOR: if (nColor < 32) colorCh[nColor++] = ch; break;
		case SC_PR_MELODY: if (melodyCh < 0) melodyCh = ch; break;
		default: break;
		}
	};
	if (explicitTargets) {
		for (int ch = 0; ch < lim && ch < 32 && nDrum < 32; ch++) {
			if (!((opt->drumMask >> ch) & 1u)) continue;
			/* FM のドラムは RHY（ch6）。チェック10は GM の定番で、そのまま書くと FM7 になる。 */
			int dst = (isFm && 6 < lim) ? 6 : ch;
			int dup = 0;
			for (int k = 0; k < nDrum; k++) if (drumChs[k] == dst) dup = 1;
			if (dup) continue;
			drumChs[nDrum] = dst;
			drumChSlot[nDrum] = opt->drumSlot[ch] ? 1 : 0;
			nDrum++;
		}
		const int keptMelody = melodyCh;
		chBass = chChord = chHarm = melodyCh = -1;
		int pend[32]; int nPend = 0;
		for (int ch = 0; ch < lim && ch < 32; ch++) {
			if (!((opt->targetMask >> ch) & 1u)) continue;
			if ((opt->drumMask >> ch) & 1u) continue;
			int role = opt->partRole[ch];
			if (role == SC_PR_AUTO) {
				if (notes[ch] <= 0 || notes[ch] >= 1000000) {
					/* ch1 が空ならメロディ。ch2 以降の伴奏を見てから線を書く。 */
					if (ch == 0 && !((opt->drumMask >> 0) & 1u)) {
						pushRole(0, SC_PR_MELODY);
						genMel = 1;
						continue;
					}
					pend[nPend++] = ch;
					continue;
				}
				int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
				int avg = notes[ch] ? sumPitch[ch] / notes[ch] : 60;
				if (ratio >= 50) role = SC_PR_CHORD;
				else if (avg <= 48) role = SC_PR_BASS;
				else role = SC_PR_MELODY;
			}
			pushRole(ch, role);
		}
		static const int kCycle[] = {
			SC_PR_BASS, SC_PR_CHORD, SC_PR_PAD, SC_PR_ARPEGGIO, SC_PR_HARMONY,
			SC_PR_COUNTER, SC_PR_STAB, SC_PR_COLOR
		};
		for (int i = 0; i < nPend; i++)
			pushRole(pend[i], kCycle[i % 8]);
		if (melodyCh < 0 && keptMelody >= 0 && keptMelody < 32
			&& !((opt->targetMask >> keptMelody) & 1u))
			melodyCh = keptMelody;
		chBass = nBass > 0 ? bassCh[0] : -1;
		chChord = nChord > 0 ? chordCh[0] : -1;
		chHarm = nHarm > 0 ? harmCh[0] : -1;
		chDrum = nDrum > 0 ? drumChs[0] : -1;
		if (rewrite) {
			for (int i = 0; i < nDrum; i++) deleteMask |= (1u << drumChs[i]);
			for (int i = 0; i < nBass; i++) deleteMask |= (1u << bassCh[i]);
			for (int i = 0; i < nChord; i++) deleteMask |= (1u << chordCh[i]);
			for (int i = 0; i < nPad; i++) deleteMask |= (1u << padCh[i]);
			for (int i = 0; i < nArp; i++) deleteMask |= (1u << arpCh[i]);
			for (int i = 0; i < nHarm; i++) deleteMask |= (1u << harmCh[i]);
			for (int i = 0; i < nCnt; i++) deleteMask |= (1u << cntCh[i]);
			for (int i = 0; i < nStab; i++) deleteMask |= (1u << stabCh[i]);
			for (int i = 0; i < nColor; i++) deleteMask |= (1u << colorCh[i]);
		}
	} else if (chDrum >= 0) {
		drumChs[0] = chDrum; drumChSlot[0] = 0; nDrum = 1;
	}
	if (nBass == 0 && chBass >= 0) bassCh[nBass++] = chBass;
	if (nChord == 0 && chChord >= 0) chordCh[nChord++] = chChord;
	if (nHarm == 0 && chHarm >= 0) harmCh[nHarm++] = chHarm;

	/* 伴奏はメロディパートには書かない。メロディの強弱・ハモリは別ジョブ。 */
	if (melodyCh >= 0) {
		int w = 0;
		for (int i = 0; i < nDrum; i++) {
			if (drumChs[i] == melodyCh) continue;
			drumChs[w] = drumChs[i];
			drumChSlot[w] = drumChSlot[i];
			w++;
		}
		nDrum = w;
		chDrum = nDrum > 0 ? drumChs[0] : -1;
		w = 0;
		for (int i = 0; i < nBass; i++) if (bassCh[i] != melodyCh) bassCh[w++] = bassCh[i];
		nBass = w;
		w = 0;
		for (int i = 0; i < nChord; i++) if (chordCh[i] != melodyCh) chordCh[w++] = chordCh[i];
		nChord = w;
		w = 0;
		for (int i = 0; i < nPad; i++) if (padCh[i] != melodyCh) padCh[w++] = padCh[i];
		nPad = w;
		w = 0;
		for (int i = 0; i < nArp; i++) if (arpCh[i] != melodyCh) arpCh[w++] = arpCh[i];
		nArp = w;
		w = 0;
		for (int i = 0; i < nHarm; i++) if (harmCh[i] != melodyCh) harmCh[w++] = harmCh[i];
		nHarm = w;
		w = 0;
		for (int i = 0; i < nCnt; i++) if (cntCh[i] != melodyCh) cntCh[w++] = cntCh[i];
		nCnt = w;
		w = 0;
		for (int i = 0; i < nStab; i++) if (stabCh[i] != melodyCh) stabCh[w++] = stabCh[i];
		nStab = w;
		w = 0;
		for (int i = 0; i < nColor; i++) if (colorCh[i] != melodyCh) colorCh[w++] = colorCh[i];
		nColor = w;
		chBass = nBass > 0 ? bassCh[0] : -1;
		chChord = nChord > 0 ? chordCh[0] : -1;
		chHarm = nHarm > 0 ? harmCh[0] : -1;
		if (melodyCh >= 0 && melodyCh < 32)
			deleteMask &= ~(1u << melodyCh);
	}
	/* ch1 はドラム指定が無い限りドラムにしない。自動割当が空いていても ch10 に戻す。 */
	{
		int w = 0;
		for (int i = 0; i < nDrum; i++) {
			if (drumChs[i] == 0 && !(opt->drumMask & 1u)) continue;
			drumChs[w] = drumChs[i];
			drumChSlot[w] = drumChSlot[i];
			w++;
		}
		nDrum = w;
		if (nDrum == 0 && !explicitTargets && (opt->jobs & SC_AJ_DRUM) && (isFm ? 6 : 9) < lim) {
			drumChs[0] = isFm ? 6 : 9;
			drumChSlot[0] = 0;
			nDrum = 1;
		}
		chDrum = nDrum > 0 ? drumChs[0] : -1;
	}

	int canLayer = (nDrum > 0 || nBass > 0 || nChord > 0 || nPad > 0 || nArp > 0 || nHarm > 0
		|| nCnt > 0 || nStab > 0 || nColor > 0);
	int canMel = ((opt->jobs & SC_AJ_MEL_VOL) && melodyCh >= 0)
		|| ((opt->jobs & SC_AJ_MEL_PITCH) && opt->pitchMode > 0 && melodyCh >= 0);
	if (!canLayer && !canMel) {
		if (out) { out->err = SC_AE_EMPTY; FillMsg(out); }
		return SC_AE_EMPTY;
	}
	if (explicitTargets) layers = canLayer ? (SC_AJ_DRUM | SC_AJ_BASS | SC_AJ_CHORD) : 0;
	else if (!canLayer) layers = 0;

	int doIntro = (opt->jobs & SC_AJ_INTRO) && opt->introBars > 0 && layers;
	int doBreak = (opt->jobs & SC_AJ_BREAK) && opt->breakBars > 0 && layers;
	int doEnd = (opt->jobs & SC_AJ_ENDING) && opt->endBars > 0 && layers;
	/* 重ね: 対象 ch に既に伴奏があるなら、イントロや間奏でもう一度ずらさない。
	   空いている発音だけ後から足す。書き換えは従来どおりずらして作り直す。 */
	int haveAcc = 0;
	if (!rewrite) {
		for (int i = 0; i < *evCount && !haveAcc; i++) {
			if (!IsNoteKind(ev[i].kind)) continue;
			const int ch = (int)ev[i].ch;
			for (int k = 0; k < nDrum && !haveAcc; k++) if (drumChs[k] == ch) haveAcc = 1;
			for (int k = 0; k < nBass && !haveAcc; k++) if (bassCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nChord && !haveAcc; k++) if (chordCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nPad && !haveAcc; k++) if (padCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nArp && !haveAcc; k++) if (arpCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nHarm && !haveAcc; k++) if (harmCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nCnt && !haveAcc; k++) if (cntCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nStab && !haveAcc; k++) if (stabCh[k] == ch) haveAcc = 1;
			for (int k = 0; k < nColor && !haveAcc; k++) if (colorCh[k] == ch) haveAcc = 1;
		}
	}
	if (haveAcc) doIntro = 0;

	int defN = opt->defMeterN > 0 ? opt->defMeterN : 4;
	int defD = opt->defMeterD > 0 ? opt->defMeterD : 4;
	uint32_t introTicks = 0, introT0 = 0, introT1 = 0;
	if (doIntro) {
		/* イントロはメロディの頭の小節の直前。そこから後ろへずらす。 */
		uint32_t melAt = 0xFFFFFFFFu;
		for (int i = 0; i < *evCount; i++) {
			if (!IsNoteKind(ev[i].kind)) continue;
			if (DrumChannel(ev, *evCount, ev[i].ch, isFm)) continue;
			if (melodyCh >= 0 && (int)ev[i].ch != melodyCh) continue;
			if (ev[i].tick < melAt) melAt = ev[i].tick;
		}
		if (melAt == 0xFFFFFFFFu) {
			for (int i = 0; i < *evCount; i++) {
				if (!IsNoteKind(ev[i].kind)) continue;
				if (DrumChannel(ev, *evCount, ev[i].ch, isFm)) continue;
				if (ev[i].tick < melAt) melAt = ev[i].tick;
			}
		}
		uint32_t ins = 0;
		if (melAt != 0xFFFFFFFFu)
			ins = ScStaffSnapToBarTick(ev, *evCount, melAt, defN, defD);
		int nM = defN, dM = defD;
		ScStaffMeterAtTick(ev, *evCount, ins, defN, defD, &nM, &dM);
		int bl = ScStaffMeterTicksPerMeasure(nM, dM);
		introTicks = (uint32_t)(opt->introBars * bl);
		introT0 = ins;
		introT1 = ins + introTicks;
		ShiftFrom(ev, *evCount, ins, introTicks);
		/* 先頭にあった音色や拍子は複製せず、1拍目へ戻す。
		   複製したままだとイントロ明けで元のピアノに戻る。 */
		if (ins == 0) {
			for (int i = 0; i < *evCount; i++) {
				if (ev[i].tick == introTicks && IsSetupKind(ev[i].kind))
					ev[i].tick = 0;
			}
		}
	}

	int lead = melodyCh >= 0 ? melodyCh : chordSrc;
	uint32_t lead0 = 0xFFFFFFFFu, lead1 = 0;
	uint32_t noteEnd = 0;
	for (int i = 0; i < *evCount; i++) {
		if (!IsNoteKind(ev[i].kind)) continue;
		uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : 1);
		if (end > noteEnd) noteEnd = end;
		if (lead >= 0 && (int)ev[i].ch != lead) continue;
		if (DrumChannel(ev, *evCount, ev[i].ch, isFm)) continue;
		if (PitchOf(ev[i], isFm) < 0 && !(isFm && ev[i].ch == 6)) continue;
		if (ev[i].tick < lead0) lead0 = ev[i].tick;
		if (end > lead1) lead1 = end;
	}
	if (lead0 == 0xFFFFFFFFu) { lead0 = introT1; lead1 = noteEnd; }
	if (noteEnd == 0) {
		/* 音符が無いときは C4（主音）が置いてあるものとして、指定小節ぶん書く。 */
		int gb = opt->genBars > 0 ? opt->genBars : 16;
		noteEnd = introT1 + (uint32_t)(gb * ScStaffMeterTicksPerMeasure(defN, defD));
		lead0 = introT1; lead1 = noteEnd;
	} else if (opt->genBars > 0) {
		uint32_t want = introT1 + (uint32_t)(opt->genBars * ScStaffMeterTicksPerMeasure(defN, defD));
		if (noteEnd < want) noteEnd = want;
		if (lead1 < want) lead1 = want;
	}
	{
		/* 本体は最低16小節。短い1小節の繰り返しで終わらせない。 */
		int blMin = ScStaffMeterTicksPerMeasure(defN, defD);
		if (blMin < 1) blMin = 1;
		uint32_t minEnd = introT1 + (uint32_t)(16 * blMin);
		if (noteEnd < minEnd) noteEnd = minEnd;
		if (lead1 < minEnd) lead1 = minEnd;
	}

	uint32_t breakT0 = 0xFFFFFFFFu, breakT1 = 0;
	int naturalBreak = 0;
	if (doBreak) {
		int gap = 0;
		uint32_t t = 0;
		while (lead >= 0 && t < lead1 && gap == 0) {
			int nM = defN, dM = defD;
			ScStaffMeterAtTick(ev, *evCount, t, defN, defD, &nM, &dM);
			int bl = ScStaffMeterTicksPerMeasure(nM, dM);
			uint32_t t1 = t + (uint32_t)bl;
			if (t1 > lead0 && t < lead1 && !BarHas(ev, *evCount, lead, t, t1) && t >= lead0)
				gap = 1;
			t = t1;
			if (bl <= 0) break;
		}
		if (gap) naturalBreak = 1;
		else if (!haveAcc) {
			uint32_t mid = lead0 + (lead1 > lead0 ? (lead1 - lead0) / 2 : 0);
			uint32_t ins = ScStaffSnapToBarTick(ev, *evCount, mid, defN, defD);
			if (ins < lead0) ins = ScStaffSnapToBarTick(ev, *evCount, lead0, defN, defD);
			int nM = defN, dM = defD;
			ScStaffMeterAtTick(ev, *evCount, ins, defN, defD, &nM, &dM);
			int bl = ScStaffMeterTicksPerMeasure(nM, dM);
			uint32_t dt = (uint32_t)(opt->breakBars * bl);
			ShiftFrom(ev, *evCount, ins, dt);
			breakT0 = ins;
			breakT1 = ins + dt;
			noteEnd += dt;
			if (lead1 > ins) lead1 += dt;
		}
	}

	uint32_t endT0 = noteEnd;
	{
		uint32_t t = 0;
		int guard = 0;
		while (t < noteEnd && guard++ < 4096) {
			int nM = defN, dM = defD;
			ScStaffMeterAtTick(ev, *evCount, t, defN, defD, &nM, &dM);
			int bl = ScStaffMeterTicksPerMeasure(nM, dM);
			if (t + (uint32_t)bl > noteEnd && t < noteEnd) {
				if (noteEnd == t) break;
			}
			uint32_t next = t + (uint32_t)bl;
			if (next <= t) break;
			t = next;
			if (t >= noteEnd) { endT0 = t; break; }
		}
		if (endT0 < noteEnd) endT0 = noteEnd;
	}

	BarInfo* bars = (BarInfo*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(BarInfo) * 2048);
	if (!bars) {
		if (out) { out->err = SC_AE_OVERFLOW; FillMsg(out); }
		return SC_AE_OVERFLOW;
	}
	int nBars = 0;
	uint32_t limit = endT0;
	if (doEnd && !haveAcc) {
		int nM = defN, dM = defD;
		ScStaffMeterAtTick(ev, *evCount, endT0, defN, defD, &nM, &dM);
		limit = endT0 + (uint32_t)(opt->endBars * ScStaffMeterTicksPerMeasure(nM, dM));
	}
	{
		uint32_t t = 0;
		int prevR = -1, prevT = CH_MAJ;
		while (t < limit && nBars < 2048) {
			int nM = defN, dM = defD;
			ScStaffMeterAtTick(ev, *evCount, t, defN, defD, &nM, &dM);
			int bl = ScStaffMeterTicksPerMeasure(nM, dM);
			BarInfo& b = bars[nBars];
			b.t0 = t;
			b.len = bl;
			int voiced = 0;
			AnalyzeChord(ev, *evCount, isFm, chordSrc, melodyCh, t, t + (uint32_t)bl, opt->defKey, prevR, prevT, &b.root, &b.type, &voiced);
			b.voiced = voiced;
			prevR = b.root; prevT = b.type;
			if (introT1 > introT0 && t >= introT0 && t < introT1) b.section = SEC_INTRO;
			else if (breakT0 != 0xFFFFFFFFu && t >= breakT0 && t < breakT1) b.section = SEC_BREAK;
			else if (t >= endT0) b.section = SEC_END;
			else if (naturalBreak && doBreak && t >= lead0 && t + (uint32_t)bl <= lead1
				&& !BarHas(ev, *evCount, lead, t, t + (uint32_t)bl))
				b.section = SEC_BREAK;
			else b.section = SEC_BODY;
			t += (uint32_t)bl;
			nBars++;
			if (bl <= 0) break;
		}
	}
	if (nBars > 0) {
		int tonic = bars[0].root;
		int tonicType = bars[0].type;
		int src = -1;
		for (int i = 0; i < nBars; i++) {
			if (bars[i].section == SEC_BODY && bars[i].voiced) { src = i; break; }
		}
		if (src < 0) {
			for (int i = 0; i < nBars; i++) if (bars[i].voiced) { src = i; break; }
		}
		if (src >= 0) {
			tonic = bars[src].root;
			tonicType = bars[src].type;
			for (int i = 0; i < nBars; i++) {
				if (bars[i].section == SEC_INTRO || (i < src && !bars[i].voiced)) {
					bars[i].root = tonic;
					bars[i].type = tonicType;
				}
			}
		} else {
			/* 無音。C4（調号の主音）が入力されているものとしてコードを置く。 */
			tonic = TonicPc(opt->defKey);
			tonicType = CH_MAJ;
		}
		for (int i = nBars - 1; i >= 0; i--) {
			if (bars[i].root < 0 && i + 1 < nBars) {
				bars[i].root = bars[i + 1].root;
				bars[i].type = bars[i + 1].type;
			}
		}
		for (int i = 0; i < nBars; i++) {
			if (bars[i].section != SEC_END) continue;
			bars[i].root = tonic;
			bars[i].type = tonicType;
		}
	}

	/* スタイルごとの進行をイントロ・本体・間奏へ。エンディングはそのスタイルの着地。 */
	if (useProg && nBars > 0) {
		ProgStep prog[8];
		int pn = StyleProgression(opt->style, variation, prog, 8);
		if (pn > 0) {
			int tonic2 = TonicPc(opt->defKey);
			int bix = 0;
			for (int i = 0; i < nBars; i++) {
				if (bars[i].section == SEC_END) continue;
				const ProgStep& ps = prog[bix % pn];
				bars[i].root = ((tonic2 + (int)ps.off) % 12 + 12) % 12;
				bars[i].type = ps.type;
				bix++;
			}
			int ap = pn - 1;
			if (ap > 0 && prog[ap].off == prog[0].off) ap--;
			int endSeen = 0, endCount = 0;
		for (int i = 0; i < nBars; i++) if (bars[i].section == SEC_END) endCount++;
		for (int i = 0; i < nBars; i++) {
			if (bars[i].section != SEC_END) continue;
			endSeen++;
			if (endSeen < endCount) {
					bars[i].root = ((tonic2 + (int)prog[ap].off) % 12 + 12) % 12;
					bars[i].type = prog[ap].type;
			} else {
					bars[i].root = ((tonic2 + (int)prog[0].off) % 12 + 12) % 12;
					bars[i].type = prog[0].type;
				}
			}
		}
	}
	if (nBars > 0) {
		int pc = TonicPc(opt->defKey);
		for (int i = 0; i < nBars; i++) {
			if (bars[i].root < 0) { bars[i].root = pc; bars[i].type = CH_MAJ; }
		}
	}

	/* 書き換え: 対象 ch（メロディ役以外）の既存ノートを先に消す。 */
	int removedNotes = 0;
	if (deleteMask) {
		int w = 0;
		for (int i = 0; i < *evCount; i++) {
			int ch = (int)ev[i].ch;
			if (ch >= 0 && ch < 32 && ((deleteMask >> ch) & 1u) && IsNoteKind(ev[i].kind)) {
				removedNotes++;
				continue;
			}
			if (w != i) ev[w] = ev[i];
			w++;
		}
		*evCount = w;
	}

	int anySolo = 0;
	if (opt->ui) {
		for (int i = 0; i < opt->ui->trackCount && i < 32; i++)
			if (opt->ui->solo[i]) anySolo = 1;
	}
	int ctxJobs = opt->jobs;
	Ctx ctx = { ev, evCount, evMax, isFm, 0, 0, ctxJobs };
	StylePat st;
	int patVar = opt->patVar;
	if (patVar < 0 || patVar >= SC_ARRANGE_COUNT) patVar = 0;
	LoadStyle(opt->style, patVar, &st);
	ApplyHalfBars(bars, nBars, opt->style, patVar);
	SetHeadTempo(&ctx, StyleBpm(opt->style), rewrite, opt->docTempoT);
	for (int i = 0; i < nDrum; i++) EnsureDrum(&ctx, opt->ui, drumChs[i], drumChSlot[i], anySolo, st.progKit, haveAcc);
	for (int i = 0; i < nBass; i++) EnsureMelodic(&ctx, opt->ui, bassCh[i], 0, st.progAll[0], anySolo, haveAcc);
	for (int i = 0; i < nChord; i++) EnsureMelodic(&ctx, opt->ui, chordCh[i], 1, st.progAll[1], anySolo, haveAcc);
	for (int i = 0; i < nPad; i++) EnsureMelodic(&ctx, opt->ui, padCh[i], 2, st.progAll[2], anySolo, haveAcc);
	for (int i = 0; i < nArp; i++) EnsureMelodic(&ctx, opt->ui, arpCh[i], 1, st.progAll[3], anySolo, haveAcc);
	for (int i = 0; i < nHarm; i++) EnsureMelodic(&ctx, opt->ui, harmCh[i], 2, st.progAll[4], anySolo, haveAcc);
	for (int i = 0; i < nCnt; i++) EnsureMelodic(&ctx, opt->ui, cntCh[i], 2, st.progAll[5], anySolo, haveAcc);
	for (int i = 0; i < nStab; i++) EnsureMelodic(&ctx, opt->ui, stabCh[i], 1, st.progAll[6], anySolo, haveAcc);
	for (int i = 0; i < nColor; i++) EnsureMelodic(&ctx, opt->ui, colorCh[i], 2, st.progAll[7], anySolo, haveAcc);

	/* FM 同士は同じ拍で構成音を分ける。SSG は拍頭だけの別ライン。ベースは除く。 */
	int fmToneIx[32], ssgToneIx[32];
	for (int i = 0; i < 32; i++) { fmToneIx[i] = -1; ssgToneIx[i] = -1; }
	if (isFm) {
		int nf = 0, ns = 0;
		auto takeTone = [&](int ch) {
			if (ch < 0 || ch >= 32) return;
			for (int k = 0; k < nBass; k++) if (bassCh[k] == ch) return;
			if ((ch <= 2 || (ch >= 7 && ch <= 9)) && fmToneIx[ch] < 0)
				fmToneIx[ch] = nf++;
			else if (ch >= 3 && ch <= 5 && ssgToneIx[ch] < 0)
				ssgToneIx[ch] = ns++;
		};
		for (int i = 0; i < nChord; i++) takeTone(chordCh[i]);
		for (int i = 0; i < nPad; i++) takeTone(padCh[i]);
		for (int i = 0; i < nArp; i++) takeTone(arpCh[i]);
		for (int i = 0; i < nHarm; i++) takeTone(harmCh[i]);
		for (int i = 0; i < nCnt; i++) takeTone(cntCh[i]);
		for (int i = 0; i < nStab; i++) takeTone(stabCh[i]);
		for (int i = 0; i < nColor; i++) takeTone(colorCh[i]);
	}

	int introN = 0, endN = 0;
	for (int i = 0; i < nBars; i++) {
		if (bars[i].section == SEC_INTRO) introN++;
		else if (bars[i].section == SEC_END) endN++;
	}
	int introI = 0, endI = 0, bodyI = 0;
	int nEmit = *evCount;
	const int nPrior = nEmit;
	for (int bi = 0; bi < nBars && !ctx.overflow; bi++) {
		const BarInfo& b = bars[bi];
		int last = (bi + 1 >= nBars) || (bars[bi + 1].section != b.section);
		int div = st.div > 0 ? st.div : 16;
		if (div > 16) div = 16;
		uint16_t drm[DV_COUNT];
		memcpy(drm, st.drum, sizeof(drm));
		uint8_t bass[16];
		memcpy(bass, st.bass, 16);
		int chordMask = st.chord;
		int chordSlots = st.chordSlots;
		int skipChord = 0;
		int phase = PH_FULL;
		int introAt = 0;
		int endFromEnd = 0;
		if (b.section == SEC_BODY) phase = BodyPhase(patVar, bodyI);
		else if (b.section == SEC_INTRO) introAt = introI;
		else if (b.section == SEC_END) endFromEnd = endN - endI - 1;
		ApplyForm(b.section, last, phase, div, introAt, introN, endFromEnd,
			drm, bass, &chordMask, &chordSlots, &skipChord, IntroHot(opt->style));
		if (b.section == SEC_INTRO) introI++;
		else if (b.section == SEC_END) endI++;
		else if (b.section == SEC_BODY) bodyI++;
		const int hot = IntroHot(opt->style);
		const int barAt = (b.section == SEC_BODY && bodyI > 0) ? bodyI - 1 : bi;
		int anyBass = 0;
		for (int bs = 0; bs < div && bs < 16; bs++) if (bass[bs]) anyBass = 1;
		if (!anyBass) {
			bass[0] = 1;
			int mid = div / 2;
			if (mid > 0 && mid < 16) bass[mid] = 2;
			anyBass = 1;
		}
		/* ベースの空きは埋めない。スタイルに書いた度数だけが形になる。 */
		uint8_t bassSoft[16];
		memset(bassSoft, 0, sizeof(bassSoft));
		uint16_t bassSlots = 0;
		const int nSlot = div < 16 ? div : 16;
		const uint16_t slotLow = (nSlot >= 16) ? 0xFFFFu : (uint16_t)((1u << nSlot) - 1);
		for (int bs = 0; bs < nSlot; bs++) if (bass[bs]) bassSlots = (uint16_t)(bassSlots | (1u << bs));
		uint16_t home = (uint16_t)((uint16_t)chordMask & slotLow);
		if (!home) home = (uint16_t)(drm[DV_KICK] & slotLow);
		if (!home) home = bassSlots;
		if (!home) home = 1;
		int loopNow = s_loopBars;
		if (loopNow < 16) loopNow = 16;
		const int phrase = (loopNow > 0) ? (barAt % loopNow) : barAt;
		const int phraseEnd = (phrase + 1 == loopNow);
		const int allowBar = (b.root >= 0) && (b.section != SEC_INTRO || hot);
		/* コードはスタイルの打点を毎小節。パッドは伸ばす。
		   アルペジオはベースの動き、ハモリはハット（無ければコード打点）。役割が同じ格子に乗らない。 */
		uint16_t compM = home;
		uint16_t padM = PhraseHits(home, phrase, div, 4);
		uint16_t hatM = (uint16_t)(drm[DV_HAT] & slotLow);
		uint16_t arpM = bassSlots ? bassSlots : home;
		uint16_t innM = hatM ? hatM : home;
		uint16_t colM = PhraseHits(home, phrase, div, 0);
		uint16_t stabSrc = drm[DV_SNARE] ? drm[DV_SNARE] : drm[DV_CLAP];
		uint16_t stabM = phraseEnd ? 0 : (uint16_t)(stabSrc & slotLow);
		uint16_t cntM = bassSlots;
		if (phraseEnd) {
			int f = FirstBitN(bassSlots, div);
			cntM = (f >= 0) ? (uint16_t)(1u << f) : 0;
		}
		if (b.section == SEC_END && endFromEnd <= 0 && allowBar) {
			int f = FirstBitN(bassSlots ? bassSlots : home, div);
			bassSlots = (f >= 0) ? (uint16_t)(1u << f) : 1;
			for (int bs = 0; bs < 16; bs++) bass[bs] = (uint8_t)((bs == f) ? 1 : 0);
			f = FirstBitN(home, div);
			compM = padM = colM = (f >= 0) ? (uint16_t)(1u << f) : 1;
			arpM = innM = stabM = 0;
			cntM = bassSlots;
		}
		if (!allowBar) {
			bassSlots = 0;
			compM = padM = arpM = innM = colM = stabM = cntM = 0;
		} else if (skipChord) {
			compM = padM = arpM = innM = colM = stabM = cntM = 0;
		} else if (!(b.section == SEC_END && endFromEnd <= 0)) {
			DevelopBar(opt->style, patVar, barAt, div, b.section,
				drm, bass, &compM, &padM, &arpM, &innM, &cntM, &stabM, &colM);
			bassSlots = 0;
			for (int bs = 0; bs < nSlot; bs++)
				if (bass[bs]) bassSlots = (uint16_t)(bassSlots | (1u << bs));
		}
		const int midSlot = nSlot / 2;
		if (allowBar && !skipChord && b.root2 >= 0 && midSlot > 0 && midSlot < 16
			&& !(b.section == SEC_END && endFromEnd <= 0)) {
			if (!bass[midSlot]) {
				bass[midSlot] = 1;
				bassSlots = (uint16_t)(bassSlots | (uint16_t)(1u << midSlot));
			}
		}
		const int kind = ArrangeKind(opt->style, patVar);
		const int orn = ArrangeOrnament(kind);
		const int hook = (allowBar && !skipChord && b.section != SEC_END
			&& (orn & ORN_HOOK)) ? 1 : 0;
		uint16_t innBlock = innM;
		if (hook) {
			int step = (nSlot >= 16) ? 2 : 1;
			for (int s = 0; s < nSlot; s += step)
				innM = (uint16_t)(innM | (uint16_t)(1u << s));
		}
		const int run32 = (allowBar && !skipChord && b.section != SEC_END
			&& (orn & ORN_RUN) && !StyleQuiet(opt->style)) ? 1 : 0;
		const int jaranSlot = (allowBar && !skipChord && (orn & ORN_JARAN))
			? JaranAt(opt->style, barAt, patVar, div, b.section) : -1;
		for (int slot = 0; slot < div && !ctx.overflow; slot++) {
			const uint32_t slot0 = b.t0 + (uint32_t)((uint64_t)slot * (uint64_t)b.len / (uint64_t)div);
			const uint32_t slot1 = (slot + 1 >= div) ? (b.t0 + (uint32_t)b.len)
				: (b.t0 + (uint32_t)((uint64_t)(slot + 1) * (uint64_t)b.len / (uint64_t)div));
			uint32_t tick = slot0;
			int slotDur = b.len / div;
			if (slotDur < 2) slotDur = 2;
			if (st.swing && (slot & 1)) tick += (uint32_t)(slotDur / 3);
			if (tick >= b.t0 + (uint32_t)b.len) continue;
			int hr = b.root;
			int ht = b.type;
			const int half = (b.root2 >= 0 && midSlot > 0 && slot >= midSlot);
			if (half) { hr = b.root2; ht = b.type2; }
			auto cutNext = [&](uint16_t m) -> int {
				int n = NextBitN(m, slot, div);
				if (b.root2 >= 0 && midSlot > 0 && slot < midSlot && n > midSlot) return midSlot;
				return n;
			};
			const int famNow = StyleFam(opt->style);
			const int kindNow = ArrangeKind(opt->style, patVar);
			auto clip = [&](int span, int role) -> int {
				int h = HoldSlots(famNow, role, opt->style);
				int quietish = (b.section == SEC_INTRO || b.section == SEC_BREAK
					|| kindNow == AR_QUIET || kindNow == AR_DARK || kindNow == AR_PAD || kindNow == AR_HALF);
				if (quietish) {
					if (h < 4) h = 4;
					if (h > 8) h = 8;
				}
				return CapSpan(span, slotDur, h);
			};
			uint16_t bit = (uint16_t)(1u << slot);
			const int introSoft = (b.section == SEC_INTRO && !last);
			const int merge = haveAcc ? 1 : 0;
			/* 複数ドラム ch は一人を分割しない。それぞれフルキットの別譜面。 */
			int drumDur = slotDur > 12 ? 12 : slotDur;
			if (isFm) {
				if (drumDur < 12) drumDur = 12;
				if (drumDur > 24) drumDur = 24;
			}
			for (int di = 0; di < nDrum; di++) {
				for (int v = 0; v < DV_COUNT; v++) {
					const int drumMidi = GmDrum(v);
					const int hatMuted = (v == DV_HAT && (drm[DV_OHAT] & bit));
					int styleHit = (di == 0) ? (!hatMuted && (drm[v] & bit))
						: DrummerOn(di, v, slot, div, drm, patVar);
					/* ドラムが1人でも、2拍目と4拍目にハンドクラップを足す。 */
					if (!isFm && di == 0 && v == DV_CLAP && !styleHit && !drm[DV_CLAP]
						&& (drm[DV_SNARE] & bit) && ((slot % 8) == 4))
						styleHit = 1;
					int ghostHat = 0;
					const int prevHat = (slot > 0) && (drm[DV_HAT] & (uint16_t)(1u << (slot - 1)));
					if (di == 0 && !styleHit && v == DV_HAT && prevHat && !StyleQuiet(opt->style)
						&& div >= 16 && !(drm[DV_OHAT] & bit)
						&& !(b.section == SEC_END && endFromEnd <= 0))
						ghostHat = 1;
					int openInstead = 0;
					if (styleHit && v == DV_HAT && drm[DV_OHAT] && !(drm[DV_OHAT] & bit)
						&& ((slot % 8) == 4 || (slot % 8) == 7) && !StyleQuiet(opt->style))
						openInstead = 1;
					const int hit = styleHit || ghostHat;
					int nextHit = div;
					if (di == 0) nextHit = NextBitN(drm[v], slot, div);
					else {
						for (int s = slot + 1; s < div && s < 16; s++)
							if (DrummerOn(di, v, s, div, drm, patVar)) { nextHit = s; break; }
					}
					int drumSpan = SpanTicks(slot, nextHit, div, b.t0, tick, b.len, slotDur);
					if (!hit) {
						if (merge)
							MergeSlot(ev, evCount, &nEmit, drumChs[di], slot0, slot1, drumMidi, 1, &removedNotes, drumSpan);
						continue;
					}
					int skip = 0;
					if (merge)
						skip = MergeSlot(ev, evCount, &nEmit, drumChs[di], slot0, slot1, drumMidi, 0, &removedNotes, drumDur);
					else if (!isFm && PitchBusy(ev, nEmit, drumChs[di], tick, drumMidi))
						skip = 1;
					else if (isFm && OnsetBusy(ev, nEmit, drumChs[di], tick))
						skip = 1;
					if (!skip) {
						int dv = ghostHat ? (48 + (slot & 3) * 6)
							: DrumHitVol(opt->style, v, slot, div, introSoft);
						if (ghostHat && introSoft) dv = 42;
						if (di > 0) {
							dv -= 8;
							if (dv < 48) dv = 48;
						}
						if ((v == DV_KICK || v == DV_SNARE) && ((slot % 4) == 0 || (slot % 4) == 2)) {
							dv += 12;
							if (dv > 127) dv = 127;
						}
						if (openInstead)
							PlaceDrumNote(&ctx, drumChs[di], tick, 46, dv > 121 ? 127 : dv + 6, drumDur);
						else if (v == DV_CRASH)
							PlaceDrumNote(&ctx, drumChs[di], tick, CrashMidi(opt->style, patVar, di, barAt, slot), dv, drumDur);
						else if (v == DV_TOM)
							PlaceDrumNote(&ctx, drumChs[di], tick, TomMidi(nSlot > 1 ? (slot * 4 / nSlot) : 0), dv, drumDur);
						else
							PlaceDrum(&ctx, drumChs[di], tick, v, dv, drumDur);
					}
				}
			}
			const int gvol = GrooveVol(opt->style, slot, div);
			int block[6];
			int nBlock = 0;
			if (ctx.isFm) {
				nBlock = FillVoicing(hr, ht, st.power, 4, barAt + (half ? 3 : 0), 52, 79, block);
				for (int a = 0; a < nBlock; a++) {
					for (int b2 = a + 1; b2 < nBlock; b2++) {
						if (block[b2] < block[a]) {
							int t = block[a]; block[a] = block[b2]; block[b2] = t;
						}
					}
				}
			}
			auto lockPart = [&](int ch, int* hit, int* pit, int* np, int* span, int* gate) -> int {
				if (!ctx.isFm || !hit || !pit || !np || !span || !gate) return 0;
				if (ch < 0 || ch >= 32 || nBlock < 1) return 0;
				if (fmToneIx[ch] >= 0) {
					/* 16分で刻むと1音のFMが崩れる。拍頭で全員同時、音は構成音を分けて四分伸ばす。 */
					*hit = ((slot % 4) == 0) ? 1 : 0;
					*np = 0;
					if (*hit) pit[(*np)++] = block[fmToneIx[ch] % nBlock];
					*span = slotDur * 4;
					if (*span < 2) *span = 2;
					*gate = 88;
					return 1;
				}
				if (ssgToneIx[ch] >= 0) {
					*hit = ((slot % 4) == 0) ? 1 : 0;
					*np = 0;
					if (*hit) {
						int n = block[ssgToneIx[ch] % nBlock];
						while (n < 64) n += 12;
						while (n > 88) n -= 12;
						pit[(*np)++] = n;
					}
					*span = slotDur * 2;
					if (*span < 2) *span = 2;
					*gate = 55;
					return 1;
				}
				return 0;
			};
			auto lay = [&](int ch, int hit, const int* pit, int np, int span, int vol, int gate, int bend) {
				if (ch < 0) return;
				/* 短い裏拍は既にある拍へ足さない。拍以上の新しい音は、その拍の古い音を外してから置く。 */
				if (merge && hit && OnsetBusy(ev, nPrior, ch, tick)) {
					if (span < slotDur) return;
					MergeSlot(ev, evCount, &nEmit, ch, slot0, slot1, -1, 1, &removedNotes, 1);
				}
				int lv = vol;
				if (lv < 36) lv = 36;
				if (lv > 127) lv = 127;
				if (!hit || np < 1) {
					if (merge) MergeSlot(ev, evCount, &nEmit, ch, slot0, slot1, -1, 1, &removedNotes, span);
					return;
				}
				EmitPitches(&ctx, ev, evCount, &nEmit, merge, ch, tick, slot0, slot1,
					pit, np, span, lv, gate, &removedNotes, bend);
			};
			const int bHit0 = BitOn(bassSlots, slot);
			for (int i = 0; i < nBass; i++) {
				int hit = bHit0;
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(bassSlots), div, b.t0, tick, b.len, slotDur), 0);
				if (i == 0) {
					if (hit) {
						int deg = phraseEnd ? 1 : (int)bass[slot];
						if (half && slot == midSlot) deg = 1;
						if (deg < 1) deg = 1;
						int midi = BassMidi(hr, SafeDeg(deg), ht);
						pit[np++] = midi;
						/* 拍の頭だけルート＋5度。それ以外は一本の線。 */
						if (!ctx.isFm && !bassSoft[slot] && (slot % 4) == 0
							&& (famNow == FAM_8 || famNow == FAM_METAL || famNow == FAM_JAZZ)) {
							int fifth = BassMidi(hr, 2, ht);
							if (fifth == midi) fifth = midi + 12;
							while (fifth > 64) fifth -= 12;
							if (fifth != midi && fifth >= 28) pit[np++] = fifth;
						}
					}
				} else if (hit) {
					np = FillVoicing(hr, ht, st.power, 2, barAt + i, 40, 67, pit);
					span = clip(SpanTicks(slot, cutNext(bassSlots), div, b.t0, tick, b.len, slotDur), 0);
				} else hit = 0;
				int bend = (i == 0 && hit && slot == 0 && (phrase % 4) == 0
					&& (st.bend || StyleFam(opt->style) == FAM_FUNK || StyleFam(opt->style) == FAM_BLUES
						|| StyleFam(opt->style) == FAM_JAZZ));
				int bv = (i == 0 && bassSoft[slot]) ? 72 : (gvol - i * 4);
				if (i == 0 && (slot % 4) == 0) bv += 12;
				if (bv > 127) bv = 127;
				lay(bassCh[i], hit, pit, np, span, bv, 82, bend);
			}
			const int cFirst = FirstBitN(compM, div);
			for (int i = 0; i < nChord; i++) {
				int hit = BitOn(compM, slot);
				if (i > 0 && slot == cFirst) hit = 0;
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(compM), div, b.t0, tick, b.len, slotDur), 1);
				int gate = ((slot % 4) == 0) ? 96 : 68;
				if (!lockPart(chordCh[i], &hit, pit, &np, &span, &gate) && hit) {
					int thick = (famNow == FAM_JAZZ || famNow == FAM_BLUES || famNow == FAM_BALLAD || famNow == FAM_PAD
						|| famNow == FAM_DISCO || opt->style == SC_AS_HOUSE || opt->style == SC_AS_TRANCE
						|| opt->style == SC_AS_EDM || opt->style == SC_AS_SYNTHWAVE || opt->style == SC_AS_GOSPEL);
					int nVoice = thick ? 4 : 3;
					if (i > 0) nVoice = 3;
					int inv = barAt * 5 + opt->style + patVar * 7 + i * 3 + slot + (half ? 4 : 0);
					int lo = 48 + ((barAt & 3) == 2 ? 5 : 0) + (i % 2) * 7;
					int hi = 80 - ((barAt & 3) == 1 ? 4 : 0);
					np = FillVoicing(hr, ht, st.power, nVoice, inv, lo, hi, pit);
				}
				int cv = gvol - 4 - i * 3;
				if ((slot % 4) == 0) cv += 16;
				if (cv > 127) cv = 127;
				lay(chordCh[i], hit, pit, np, span, cv, gate, 0);
			}
			for (int i = 0; i < nPad; i++) {
				int hit = BitOn(padM, slot);
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(padM), div, b.t0, tick, b.len, slotDur), 2);
				int gate = 96;
				const int locked = lockPart(padCh[i], &hit, pit, &np, &span, &gate);
				if (!locked && hit)
					np = FillVoicing(hr, ht, 0, 3, phrase + 2 + i + (half ? 3 : 0), 55, 86, pit);
				lay(padCh[i], hit, pit, np, span, gvol - 16 - i * 2, gate, 0);
				if (!locked && i == 0 && hit && span >= 48)
					PlaceSing(&ctx, padCh[i], tick, span, opt->style, 0);
			}
			for (int i = 0; i < nArp; i++) {
				int hit = BitOn(arpM, slot);
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(arpM), div, b.t0, tick, b.len, slotDur), 3);
				int gate = 70;
				if (lockPart(arpCh[i], &hit, pit, &np, &span, &gate)) {
					lay(arpCh[i], hit, pit, np, span, gvol - 6, gate, 0);
						continue;
				}
				/* 拍の途中だけ 32分や64分。全部を同じ音価にしない。 */
				int sub = (div >= 16 && slot >= 0 && slot < 16) ? (int)s_figArp[slot] : 0;
				if (i == 0 && arpCh[i] >= 0 && hit && (sub > 0 || (run32 && slot >= midSlot))) {
					if (merge)
						MergeSlot(ev, evCount, &nEmit, arpCh[i], slot0, slot1, -1, 1, &removedNotes, slotDur);
					int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
					int nSub = (sub == 2) ? 4 : 2;
					int each = room / nSub;
					if (each < 1) each = 1;
					int shape = (opt->style + barAt + patVar) % 5;
					int step = barAt * 16 + slot * 2;
					int v0 = ((slot % 4) == 0) ? gvol : (gvol - 8);
					if (sub == 2) v0 -= 10;
					if (v0 < 48) v0 = 48;
					if (v0 > 118) v0 = 118;
					for (int k = 0; k < nSub && !ctx.overflow; k++) {
						int midi = ArpPitch(hr, ht, st.power, step + k, shape, 0);
						int v = v0 - ((k & 1) ? 8 : 0);
						if (v < 42) v = 42;
						PlaceNote(&ctx, arpCh[i], tick + (uint32_t)(k * each), midi, each, v, sub == 2 ? 36 : 48);
					}
					continue;
				}
				if (run32 && slot >= midSlot) hit = 0;
				if (hit) {
					int shape = (opt->style + barAt + patVar) % 5;
					int step = barAt * 4 + NthBit(arpM, slot);
					if (barAt & 1) step = 48 - (step & 15);
					if (i == 0)
						pit[np++] = ArpPitch(hr, ht, st.power, step, shape, 0);
					else
						np = FillVoicing(hr, ht, st.power, 3, phrase + i, 64, 96, pit);
				}
				lay(arpCh[i], hit, pit, np, span, gvol - 6, 70, 0);
			}
			for (int i = 0; i < nHarm; i++) {
				if (i == 0 && (opt->jobs & SC_AJ_MEL_PITCH)) continue;
				int hit = BitOn((i == 0) ? innM : innBlock, slot);
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext((i == 0) ? innM : innBlock), div, b.t0, tick, b.len, slotDur), 4);
				int gate = 74;
				if (lockPart(harmCh[i], &hit, pit, &np, &span, &gate)) {
					lay(harmCh[i], hit, pit, np, span, gvol - 6, gate, 0);
					continue;
				}
				/* じゃらんの拍はブロックを置かず、あとで 64 分を書く。 */
				if (i == 0 && slot == jaranSlot) {
					lay(harmCh[i], 0, pit, 0, span, 0, 0, 0);
					continue;
				}
				if (i == 0 && StyleFam(opt->style) != FAM_METAL && StyleFam(opt->style) != FAM_MARCH) {
					if (hit) {
						int nth = NthBit((i == 0) ? innM : innBlock, slot);
						int deg = LeadDeg(opt->style, patVar, barAt, nth + 4, (slot % 4) == 0, ht);
						pit[np++] = LeadMidi(hr, ht, deg, (patVar & 8) ? 1 : 0);
					}
					lay(harmCh[i], hit, pit, np, span, gvol - 6, 74, 0);
					continue;
				}
				if (hit)
					np = FillVoicing(hr, ht, st.power, 4, phrase + 5 + i, 60, 91, pit);
				lay(harmCh[i], hit, pit, np, span, gvol - 10, 88, 0);
			}
			for (int i = 0; i < nCnt; i++) {
				int hit = BitOn(cntM, slot);
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(cntM), div, b.t0, tick, b.len, slotDur), 5);
				int gate = 78;
				if (lockPart(cntCh[i], &hit, pit, &np, &span, &gate)) {
					lay(cntCh[i], hit, pit, np, span, gvol - 8, gate, 0);
					continue;
				}
				int cntSub = (div >= 16 && slot >= 0 && slot < 16) ? (int)s_figCnt[slot] : 0;
				if (i == 0 && hit && cntSub > 0 && cntCh[i] >= 0) {
					if (merge)
						MergeSlot(ev, evCount, &nEmit, cntCh[i], slot0, slot1, -1, 1, &removedNotes, slotDur);
					int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
					int nSub = (cntSub == 2) ? 4 : 2;
					int each = room / nSub;
					if (each < 1) each = 1;
					int v0 = gvol - (cntSub == 2 ? 16 : 6);
					if (v0 < 46) v0 = 46;
					for (int k = 0; k < nSub && !ctx.overflow; k++) {
						int deg = LeadDeg(opt->style, patVar, barAt, slot * 2 + k, k == 0 && (slot % 4) == 0, ht);
						int v = v0 - ((k & 1) ? 6 : 0);
						if (v < 40) v = 40;
						PlaceNote(&ctx, cntCh[i], tick + (uint32_t)(k * each), LeadMidi(hr, ht, deg, (patVar & 8) ? 1 : 0), each, v, cntSub == 2 ? 34 : 46);
					}
					continue;
				}
				if (hit && i == 0) {
					int nth = NthBit(cntM, slot);
					int deg = LeadDeg(opt->style, patVar, barAt, nth, (slot % 4) == 0 || (slot % 4) == 1, ht);
					pit[np++] = LeadMidi(hr, ht, deg, (patVar & 8) ? 1 : 0);
				} else if (hit) {
					int deg = LeadDeg(opt->style, patVar + 3, barAt, NthBit(cntM, slot), 1, ht);
					pit[np++] = LeadMidi(hr, ht, deg, (patVar & 8) ? 1 : 0);
				}
				lay(cntCh[i], hit, pit, np, span, gvol - 8, 78, 0);
			}
			for (int i = 0; i < nStab; i++) {
				int hit = BitOn(stabM, slot);
				int pit[6];
				int np = 0;
				int span = slotDur > 2 ? slotDur : 2;
				int gate = 36;
				if (!lockPart(stabCh[i], &hit, pit, &np, &span, &gate) && hit)
					np = FillVoicing(hr, ht, st.power, 4, phrase + i, 67, 96, pit);
				int sv = gvol + 18 - i * 3;
				if (sv > 127) sv = 127;
				lay(stabCh[i], hit, pit, np, span, sv, gate, 0);
			}
			for (int i = 0; i < nColor; i++) {
				int hit = BitOn(colM, slot);
				int pit[6];
				int np = 0;
				int span = clip(SpanTicks(slot, cutNext(colM), div, b.t0, tick, b.len, slotDur), 2);
				int gate = 84;
				if (!lockPart(colorCh[i], &hit, pit, &np, &span, &gate) && hit)
					np = FillVoicing(hr, ht, 0, 2, phrase, 72, 96, pit);
				lay(colorCh[i], hit, pit, np, span, gvol - 18, gate, 0);
			}
			/* その拍だけ、コードトーンを 64 分で4つ。じゃらん。 */
			if (!ctx.isFm && slot == jaranSlot && slotDur >= 8 && !(run32 && slot >= midSlot)) {
				int ch = (nHarm > 0) ? harmCh[0] : ((nArp > 0) ? arpCh[0] : -1);
				if (ch >= 0) {
					int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
					int each = room / 4;
					if (each >= 2) {
						int up = (barAt + opt->style) & 1;
						int step0 = (barAt * 3 + slot) & 7;
						for (int k = 0; k < 4 && !ctx.overflow; k++) {
							int step = up ? (step0 + k) : (step0 + 3 - k);
							int midi = ArpPitch(hr, ht, 0, step, 2, 0);
							int v = 64 + k * 12;
							if (v > 116) v = 116;
							PlaceNote(&ctx, ch, tick + (uint32_t)(k * each), midi, each, v, 40);
						}
					}
				}
			}
			if (genMel && melodyCh == 0 && allowBar && MelodySlot(patVar, slot, div, compM, bassSlots, drm[DV_HAT])
				&& !(ctx.isFm && (slot % 4) != 0)) {
				static const uint8_t md[8][8] = {
					{ 1, 5, 3, 8, 5, 3, 2, 1 },
					{ 5, 8, 5, 3, 1, 3, 5, 8 },
					{ 1, 3, 5, 3, 8, 5, 3, 1 },
					{ 8, 5, 3, 1, 2, 3, 5, 1 },
					{ 1, 1, 5, 8, 3, 5, 2, 1 },
					{ 3, 5, 8, 5, 3, 2, 5, 1 },
					{ 1, 8, 5, 3, 5, 8, 3, 1 },
					{ 5, 3, 1, 5, 8, 3, 5, 1 }
				};
				int deg = md[patVar & 7][(slot + barAt * 3 + opt->style) & 7];
				int midi = ShiftOct(BassMidi(hr, deg, ht), 1, 52, 74);
				int mv = ((slot % 4) == 0) ? 114 : 86;
				int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
				int mdor = ctx.isFm ? (slotDur * 4) : (((patVar & 7) == 3) ? slotDur : slotDur * 2);
				if (!ctx.isFm && mdor > room) mdor = room;
				if (mdor < 2) mdor = 2;
				PlaceNote(&ctx, melodyCh, tick, midi, mdor, mv, 84);
			}

		}
	}

	DecorateMelody(&ctx, melodyCh, chHarm, opt->defKey, bars, nBars, opt->pitchMode);
	HeapFree(GetProcessHeap(), 0, bars);

	if (out) {
		out->added = ctx.added;
		out->removed = removedNotes;
		out->chDrum = chDrum;
		out->chBass = chBass;
		out->chChord = chChord;
		out->chHarm = chHarm;
		uint32_t used = 0;
		for (int i = 0; i < nDrum; i++) used |= (1u << drumChs[i]);
		for (int i = 0; i < nBass; i++) used |= (1u << bassCh[i]);
		for (int i = 0; i < nChord; i++) used |= (1u << chordCh[i]);
		for (int i = 0; i < nPad; i++) used |= (1u << padCh[i]);
		for (int i = 0; i < nArp; i++) used |= (1u << arpCh[i]);
		for (int i = 0; i < nHarm; i++) used |= (1u << harmCh[i]);
		for (int i = 0; i < nCnt; i++) used |= (1u << cntCh[i]);
		for (int i = 0; i < nStab; i++) used |= (1u << stabCh[i]);
		for (int i = 0; i < nColor; i++) used |= (1u << colorCh[i]);
		if (melodyCh >= 0) used |= (1u << melodyCh);
		out->usedMask = used;
		out->err = ctx.overflow ? SC_AE_OVERFLOW : ((ctx.added > 0 || removedNotes > 0) ? SC_AE_OK : SC_AE_EMPTY);
		FillMsg(out);
		if ((opt->jobs & (SC_AJ_MEL_VOL | SC_AJ_MEL_PITCH)) && melodyCh < 0) {
			size_t used = wcslen(out->msg);
			const wchar_t* extra = LL14(
				L" メロディのパートは特定できないので、メロディは触っていません。",
				L" Melody part was not identified, so the melody was left alone.",
				L" Partie mélodie inconnue, mélodie inchangée.",
				L" Parte melodica non individuata, melodia invariata.",
				L" Parte de melodía no identificada, melodía intacta.",
				L" 멜로디 파트를 특정하지 못해 멜로디는 그대로입니다.",
				L" 未能判断旋律声部，未改旋律。",
				L" لم يُعرف جزء اللحن، تُرك اللحن.",
				L" Партия мелодии не найдена, мелодия не тронута.",
				L" Melodiestimme unbekannt, Melodie unverändert.",
				L" Parte da melodia não identificada, melodia intacta.",
				L" Melodiepartij niet gevonden, melodie ongemoeid.",
				L" Nie rozpoznano partii melodii, melodia bez zmian.",
				L" Melodi partisi bulunamadı, melodiye dokunulmadı.");
			if (extra && used + wcslen(extra) < 255)
				wcscat_s(out->msg, extra);
		}
	}
	return ctx.overflow ? SC_AE_OVERFLOW : ((ctx.added > 0 || removedNotes > 0) ? SC_AE_OK : SC_AE_EMPTY);
}

void ScAutoArrangeReleaseEmptySolo(ScStaffUi* ui, const ScEvent* ev, int n)
{
	if (!ui) return;
	for (int ch = 0; ch < ui->trackCount && ch < 32; ch++) {
		if (!ui->solo[ch]) continue;
		int has = 0;
		if (ev && n > 0) {
			for (int i = 0; i < n; i++) {
				if ((int)ev[i].ch == ch && (ev[i].kind == SC_EV_NOTE || ev[i].kind == SC_EV_FM_NOTE)) {
					has = 1;
					break;
				}
			}
		}
		if (!has) ui->solo[ch] = 0;
	}
}
