#include "stdafx.h"
#include "CSasamiAutoArrange.h"
#include "SasamiToneNames.h"
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
	const int pIdx = (pat < 0) ? 0 : (pat & 15);
	if (pIdx < 2) return NoGhost(base, role);
	/* スタイルの楽器族（GM の8音色ブロック）を壊さずにアレンジ別の近い音色へ寄せる。
	   ジャズにシンセベースが混ざる等の破綻を防ぐ。 */
	if (base <= 7) {
		static const int kP[8] = { 0, 1, 2, 4, 5, 0, 4, 2 };
		return kP[(base + pIdx) & 7];
	}
	if (base >= 32 && base <= 39) {
		/* アコースティック系(32)は32/35、エレクトリック(33-37)はその中、シンセ(38-39)はその中 */
		if (base == 32) return (pIdx & 4) ? 35 : 32;
		if (base >= 38) return (pIdx & 2) ? 39 : 38;
		static const int kEB[4] = { 33, 34, 35, 36 };
		return kEB[(base + pIdx) & 3];
	}
	if (base >= 24 && base <= 31) {
		/* クリーン/アコギ(24-28)と歪み系(29-31)を分離して維持 */
		if (base <= 25) return (pIdx & 2) ? 25 : 24;
		if (base <= 28) { static const int kCG[3] = { 26, 27, 28 }; return kCG[(base + pIdx) % 3]; }
		static const int kDG[3] = { 29, 30, 29 };
		return kDG[(base + pIdx) % 3];
	}
	static const int kAlt[8] = { 0, 1, -1, 2, 0, -1, 1, 2 };
	int d = kAlt[(pIdx + role * 3) & 7];
	int fam = base & ~7;
	int p = fam + ((base + d) & 7);
	if (p <= 7 && base > 7) p = base;
	if (p < 0 || p > 127) p = base;
	return NoGhost(p, role);
}

/* 複数チャンネル（ch1〜ch32）で同じ役割が2本以上あるとき、役割とスタイルに合う補完音色を選ぶ。 */
static int RoleLayerProg(int style, int pat, int roleIdx, int layerIdx, const uint8_t* progAll)
{
	if (!progAll) return 0;
	int base = progAll[roleIdx & 7];
	if (layerIdx <= 0) return base;
	const int fam = StyleFam(style);
	const int elec = (fam == FAM_FLOOR || fam == FAM_DNB || fam == FAM_TRAP || fam == FAM_DISCO
		|| style == SC_AS_CHIPTUNE || style == SC_AS_GAME || style == SC_AS_SYNTHWAVE);
	const int jazz = (fam == FAM_JAZZ || fam == FAM_BLUES || fam == FAM_SHUF
		|| style == SC_AS_BOSSA || style == SC_AS_PIANO || style == SC_AS_LOFI);
	const int rock = (fam == FAM_8 || fam == FAM_METAL || style == SC_AS_PUNK || style == SC_AS_GRUNGE);
	switch (roleIdx & 7) {
	case 0: /* BASS layers */
		if (elec) return (layerIdx & 1) ? 39 : 38;
		if (jazz) return (layerIdx & 1) ? 35 : 32;
		return (layerIdx & 1) ? ((base == 34) ? 33 : 34) : 38;
	case 1: /* CHORD layers: EP / Organ / Guitar / Piano complement */
		if (elec) { static const int cE[3] = { 81, 4, 90 }; return cE[(layerIdx + pat) % 3]; }
		if (jazz) { static const int cJ[3] = { 4, 26, 0 }; return cJ[(layerIdx + pat) % 3]; }
		if (rock) { static const int cR[3] = { 29, 18, 25 }; return cR[(layerIdx + pat) % 3]; }
		{ static const int cP[3] = { 4, 27, 16 }; return cP[(layerIdx + pat) % 3]; }
	case 2: /* PAD layers */
		{ static const int cPd[4] = { 49, 89, 50, 88 }; return cPd[(layerIdx + pat) & 3]; }
	case 3: /* ARP layers */
		if (elec) { static const int cAe[3] = { 80, 87, 81 }; return cAe[(layerIdx + pat) % 3]; }
		{ static const int cAp[3] = { 25, 11, 46 }; return cAp[(layerIdx + pat) % 3]; }
	case 4: /* HARM layers */
		{ static const int cHm[4] = { 48, 61, 73, 50 }; return cHm[(layerIdx + pat) & 3]; }
	case 5: /* COUNTER layers */
		if (elec) { static const int cCe[3] = { 87, 82, 80 }; return cCe[(layerIdx + pat) % 3]; }
		{ static const int cCp[4] = { 73, 66, 40, 27 }; return cCp[(layerIdx + pat) & 3]; }
	case 6: /* STAB layers */
		if (elec) { static const int cSe[3] = { 62, 63, 81 }; return cSe[(layerIdx + pat) % 3]; }
		{ static const int cSp[3] = { 61, 56, 62 }; return cSp[(layerIdx + pat) % 3]; }
	default: /* COLOR layers */
		{ static const int cCo[4] = { 11, 12, 9, 46 }; return cCo[(layerIdx + pat) & 3]; }
	}
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
	/* スタイル系列を外さずにキットを振り分ける（ジャズに808、テクノに生ブラシを混ぜない）。 */
	if (pat >= 2) {
		int famK = StyleFam(id);
		static const int kKitRock[4]  = { 0, 8, 16, 1 };
		static const int kKitElec[4]  = { 24, 25, 26, 16 };
		static const int kKitJazz[4]  = { 32, 40, 8, 0 };
		static const int kKitOrch[4]  = { 48, 0, 8, 40 };
		static const int kKitLatin[4] = { 0, 8, 32, 24 };
		const int* pool = kKitRock;
		if (famK == FAM_FLOOR || famK == FAM_DNB || famK == FAM_TRAP || famK == FAM_HOP || id == SC_AS_CHIPTUNE) pool = kKitElec;
		else if (famK == FAM_JAZZ || famK == FAM_BLUES || famK == FAM_SHUF || id == SC_AS_LOFI || id == SC_AS_PIANO || id == SC_AS_NEOSOUL) pool = kKitJazz;
		else if (famK == FAM_BALLAD || famK == FAM_WALTZ || famK == FAM_MARCH || famK == FAM_PAD) pool = kKitOrch;
		else if (famK == FAM_BOSSA || famK == FAM_SAMBA || famK == FAM_LATIN || famK == FAM_AFRO || famK == FAM_TANGO) pool = kKitLatin;
		s->progKit = pool[(pat + (id & 3)) & 3];
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

/* スタイルが線の性格を決める。8個のコード表でもリズム型でもない。 */
struct LineCast {
	int scale;   /* 0長 1短 2ドリ 3ミクソ 4ペンタ 5ブルース 6フリギア 7和声短 */
	int hold;    /* 何小節で次の音へ移るか */
	int step;    /* その移動（スケール度数）。負は下降 */
	int leap;    /* たまに跳ぶ幅 */
	int notes;   /* 歌の音数 */
	int phrase;  /* 何小節で主音へ戻るか */
	int drum;    /* 0裏拍 1四つ 2トレシージョ 3三拍 4半分 5クラベ 6裏 7行進 8まばら 9速い */
	int bass;    /* 0保つ 1歩く 2オクターブ 3裏 */
	int answer;  /* もう一声の度数 */
	int lo, hi;
	int swing;
	int form12;
};

/* 歌の型。同じ番号は同じ輪郭で、スタイルごとに音階とリズムが別。 */
enum {
	MEL_POP = 0, MEL_ROCK, MEL_METAL, MEL_BALLAD, MEL_JAZZ, MEL_BLUES,
	MEL_WALTZ, MEL_OFF, MEL_FUNK, MEL_RAVE, MEL_HOP, MEL_LATIN,
	MEL_FAN, MEL_ENKA, MEL_CHIP, MEL_TANGO, MEL_SURF, MEL_PUNK,
	MEL_BOOGIE, MEL_WAVE
};

/* 列: 音階 ドラム ベース 和音の形 歌 低 高 音数 保ち 句 スウィング 12小節 進行 跳躍
   ロックとメタルはここが別。ファミリーの使い回しはしない。 */
struct Voice {
	int8_t scale, drum, bass, fig, mel, lo, hi, notes, hold, phrase, swing, form12, step, leap;
};
#define V(sc,dr,bs,fg,mel,lo,hi,nt,hd,ph,sw,f12,st,lp) \
	{ (int8_t)(sc),(int8_t)(dr),(int8_t)(bs),(int8_t)(fg),(int8_t)(mel),(int8_t)(lo),(int8_t)(hi), \
	  (int8_t)(nt),(int8_t)(hd),(int8_t)(ph),(int8_t)(sw),(int8_t)(f12),(int8_t)(st),(int8_t)(lp) }
static const Voice kVoice[SC_AS_COUNT] = {
	V(0,40,4,0, MEL_POP,   69,93, 4,2,4, 0,0, 1,2), /* 16ビート */
	V(0,17,0,0, MEL_POP,   67,88, 3,2,4, 0,0, 1,2), /* 8ビート */
	V(0,29,3,3, MEL_OFF,   64,84, 2,2,4, 0,0, 1,1), /* レゲエ */
	V(1,21,2,1, MEL_RAVE,  72,96, 2,8,8, 0,0, 0,3), /* テクノ */
	V(1,22,7,1, MEL_RAVE,  70,93, 3,4,4, 0,0, 1,2), /* ハウス */
	V(1,23,2,1, MEL_WAVE,  74,98, 4,8,8, 0,0, 1,2), /* トランス */
	V(1,25,2,1, MEL_HOP,   74,98, 4,2,4, 0,0, 1,3), /* ドラムンベース */
	V(1,19,0,0, MEL_HOP,   62,86, 3,4,4, 1,0, 0,2), /* ヒップホップ */
	V(3,18,3,3, MEL_FUNK,  64,88, 5,2,4, 0,0, 1,2), /* ファンク */
	V(0,24,7,1, MEL_POP,   69,93, 3,2,4, 0,0, 1,3), /* ディスコ */
	V(5,15,4,4, MEL_ROCK,  64,91, 3,2,4, 0,0, 1,2), /* ロック */
	V(1,10,5,6, MEL_METAL, 52,81, 6,2,4, 0,0, 1,2), /* メタル */
	V(0, 8,0,5, MEL_BALLAD,60,81, 2,4,8, 0,0, 1,1), /* バラード */
	V(0, 3,0,0, MEL_WALTZ, 64,86, 2,2,4, 0,0, 1,2), /* ワルツ */
	V(0, 5,8,0, MEL_LATIN, 64,86, 3,2,4, 0,0, 1,1), /* ボサ */
	V(0, 2,4,4, MEL_LATIN, 67,91, 4,2,4, 0,0, 1,2), /* サンバ */
	V(5,30,4,2, MEL_BLUES, 62,86, 3,2,4, 1,0, 1,2), /* シャッフル */
	V(0, 7,4,0, MEL_FAN,   67,91, 4,2,4, 0,0, 1,2), /* マーチ */
	V(0,32,4,4, MEL_FAN,   69,93, 4,2,2, 0,0, 1,3), /* ポルカ */
	V(0,41,1,0, MEL_POP,   65,91, 4,2,8, 0,0, 1,2), /* シティポップ */
	V(0,17,4,4, MEL_CHIP,  72,98, 6,2,4, 0,0, 1,3), /* ゲーム */
	V(0, 8,0,0, MEL_BALLAD,55,91, 4,4,8, 0,0, 1,2), /* オーケストラ */
	V(2, 8,0,5, MEL_BALLAD,53,76, 1,8,8, 0,0, 1,1), /* アンビエント */
	V(1,26,0,0, MEL_HOP,   64,88, 3,2,4, 0,0, 1,2), /* ブレイクビーツ */
	V(1,36,4,4, MEL_LATIN, 67,91, 4,2,4, 0,0, 1,2), /* ラテン */
	V(1, 8,0,5, MEL_ENKA,  55,79, 2,4,8, 0,0,-1,1), /* 演歌 */
	V(3,40,1,0, MEL_FUNK,  64,91, 5,2,4, 0,0, 1,2), /* フュージョン */
	V(0, 8,0,5, MEL_POP,   60,84, 2,4,4, 0,0, 1,1), /* アコースティック */
	V(0, 8,1,2, MEL_JAZZ,  60,88, 4,2,4, 1,0, 1,2), /* ピアノトリオ */
	V(0, 7,0,0, MEL_FAN,   67,93, 4,1,2, 0,0, 1,3), /* ファンファーレ */
	V(1,16,0,0, MEL_BALLAD,60,86, 2,4,8, 0,0, 1,2), /* スローロック */
	V(3, 0,1,2, MEL_JAZZ,  60,86, 4,1,4, 1,0, 1,2), /* ジャズ */
	V(3,48,1,2, MEL_JAZZ,  62,91, 5,1,8, 1,0, 1,2), /* スウィングジャズ */
	V(3,47,1,2, MEL_JAZZ,  58,93, 6,1,4, 1,0, 3,3), /* ビバップ */
	V(5,30,4,2, MEL_BLUES, 60,84, 4,4,12,1,1, 1,2), /* ブルース */
	V(5,16,0,5, MEL_BLUES, 55,79, 2,4,12,1,1, 1,1), /* スローブルース */
	V(5,30,9,2, MEL_BOOGIE,64,88, 4,2,4, 1,1, 1,2), /* ロックンロール */
	V(0,46,1,0, MEL_POP,   65,91, 4,2,8, 0,0, 1,2), /* ゴスペル */
	V(0,19,3,3, MEL_FUNK,  64,88, 4,2,4, 0,0, 1,2), /* ソウル */
	V(1,19,0,0, MEL_POP,   62,86, 3,4,4, 1,0, 1,1), /* R&B */
	V(2,19,1,2, MEL_JAZZ,  60,84, 3,4,8, 1,0, 1,1), /* ネオソウル */
	V(4,13,4,4, MEL_ROCK,  64,91, 3,2,4, 0,0, 1,2), /* カントリー */
	V(5,11,6,6, MEL_PUNK,  64,88, 6,1,2, 0,0, 1,1), /* パンク */
	V(5,12,0,0, MEL_ROCK,  52,79, 2,4,8, 0,0,-1,2), /* グランジ */
	V(0,28,4,3, MEL_POP,   67,91, 3,2,2, 0,0, 1,2), /* スカ */
	V(4,17,0,0, MEL_POP,   64,88, 3,2,4, 0,0, 1,1), /* 歌謡曲 */
	V(0,40,4,4, MEL_CHIP,  72,98, 5,2,8, 0,0, 1,3), /* アニソン */
	V(0,21,2,1, MEL_CHIP,  72,96, 6,2,4, 0,0, 1,3), /* チップチューン */
	V(1,20,0,1, MEL_HOP,   58,84, 2,4,4, 0,0, 0,2), /* トラップ */
	V(6,42,5,1, MEL_HOP,   52,81, 3,4,4, 0,0,-1,3), /* ドリル */
	V(0,27,3,4, MEL_LATIN, 65,90, 4,2,4, 0,0, 1,2), /* アフロビート */
	V(3, 3,1,2, MEL_JAZZ,  60,86, 4,2,4, 1,0, 1,2), /* ジャズワルツ */
	V(1,33,0,0, MEL_TANGO, 58,84, 3,2,4, 0,0, 1,2), /* タンゴ */
	V(6,34,0,4, MEL_TANGO, 62,88, 5,2,4, 0,0,-1,2), /* フラメンコ */
	V(4,14,4,4, MEL_SURF,  64,93, 6,2,4, 0,0,-1,2), /* サーフ */
	V(4, 8,0,5, MEL_JAZZ,  60,81, 2,4,4, 1,0, 1,1), /* ローファイ */
	V(1,43,0,4, MEL_HOP,   48,72, 1,2,2, 0,0, 1,1), /* ドラムソロ */
	V(1,44,7,1, MEL_RAVE,  72,98, 3,4,8, 0,0, 1,3), /* EDM */
	V(6,45,2,1, MEL_WAVE,  48,76, 3,8,4, 0,0, 0,4), /* ダブステップ */
	V(1,22,2,1, MEL_WAVE,  64,88, 4,4,8, 0,0, 1,2)  /* シンセウェイブ */
};
#undef V
static_assert(sizeof(kVoice) / sizeof(kVoice[0]) == SC_AS_COUNT, "kVoice rows");

static void CastOf(int style, int pat, int var, LineCast* o)
{
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	const Voice& v = kVoice[style];
	o->scale = v.scale; o->hold = v.hold; o->step = v.step; o->leap = v.leap; o->notes = v.notes;
	o->phrase = v.phrase; o->drum = v.drum; o->bass = v.bass; o->answer = 2;
	o->lo = v.lo; o->hi = v.hi; o->swing = v.swing; o->form12 = v.form12;
	if (pat & 1) o->phrase = (o->phrase >= 8) ? 4 : ((o->phrase <= 2) ? 4 : 2);
	if (pat & 2) o->leap += 1;
	if (pat & 4) o->step = -o->step;
	if (pat & 8) o->notes += 1;
	if (var) {
		o->step = -o->step;
		o->phrase = (o->phrase >= 8) ? 4 : (o->phrase <= 2 ? 4 : 8);
	}
	if (o->leap < 1) o->leap = 1;
	if (o->leap > 4) o->leap = 4;
	if (o->notes < 1) o->notes = 1;
	if (o->notes > 8) o->notes = 8;
	if (o->phrase < 2) o->phrase = 2;
	if (o->answer < 2) o->answer = 2;
}

void ScAutoStyleProgDesc(int style, int variation, int keySig, wchar_t* buf, int cch)
{
	(void)keySig;
	if (!buf || cch <= 0) return;
	buf[0] = 0;
	LineCast c;
	CastOf(style, 0, variation & 1, &c);
	const wchar_t* motion;
	if (c.form12)
		motion = LL14(L"12小節の歌", L"12-bar song", L"Chant de 12 mesures", L"Canto di 12 battute", L"Canto de 12 compases", L"12마디 노래", L"12小节的歌", L"أغنية من 12 مازورة", L"Песня на 12 тактов", L"12-taktiger Gesang", L"Canto de 12 compassos", L"Lied van 12 maten", L"Śpiew na 12 taktów", L"12 ölçü şarkı");
	else if (c.hold >= 8 && c.step == 0)
		motion = LL14(L"一つの中心に留まる", L"Stays on one center", L"Reste sur un centre", L"Resta su un centro", L"Se queda en un centro", L"한 중심에 머무름", L"停在一个中心", L"يبقى على مركز واحد", L"Держится одного центра", L"Bleibt auf einem Zentrum", L"Fica num centro", L"Blijft op één centrum", L"Zostaje na jednym środku", L"Bir merkezde kalır");
	else if (c.step < 0)
		motion = LL14(L"降りていく歌", L"A descending song", L"Chant descendant", L"Canto discendente", L"Canto que baja", L"내려가는 노래", L"下行的歌", L"أغنية هابطة", L"Нисходящая песня", L"Absteigender Gesang", L"Canto descendente", L"Dalend lied", L"Opadający śpiew", L"İnen şarkı");
	else if (c.step >= 3)
		motion = LL14(L"4度で進む歌", L"Moves by fourths", L"Avance par quartes", L"Avanza per quarte", L"Avanza por cuartas", L"4도로 나아감", L"按四度前进", L"يتقدم برابعات", L"Идёт по квартам", L"Geht in Quarten", L"Avança em quartas", L"Gaat in kwarten", L"Idzie kwartami", L"Dörtlülerle gider");
	else
		motion = LL14(L"隣の音へ進む歌", L"Moves to the next note", L"Passe à la note voisine", L"Passa alla nota vicina", L"Pasa a la nota vecina", L"이웃 음으로 나아감", L"走到相邻的音", L"ينتقل للنغمة المجاورة", L"Идёт на соседнюю ноту", L"Geht zur Nachbarnote", L"Segue para a nota ao lado", L"Gaat naar de buurtoon", L"Idzie na sąsiedni dźwięk", L"Komşu sese gider");
	const wchar_t* turn = (c.phrase <= 2)
		? LL14(L"2小節で戻る", L"returns in 2 bars", L"retour en 2 mesures", L"torna in 2 battute", L"vuelve en 2 compases", L"2마디에 돌아옴", L"2小节回到主音", L"يعود خلال مازورتين", L"возврат за 2 такта", L"kehrt in 2 Takten", L"volta em 2 compassos", L"terug in 2 maten", L"wraca po 2 taktach", L"2 ölçüde döner")
		: (c.phrase >= 8)
		? LL14(L"8小節で戻る", L"returns in 8 bars", L"retour en 8 mesures", L"torna in 8 battute", L"vuelve en 8 compases", L"8마디에 돌아옴", L"8小节回到主音", L"يعود خلال 8 موازير", L"возврат за 8 тактов", L"kehrt in 8 Takten", L"volta em 8 compassos", L"terug in 8 maten", L"wraca po 8 taktach", L"8 ölçüde döner")
		: LL14(L"4小節で戻る", L"returns in 4 bars", L"retour en 4 mesures", L"torna in 4 battute", L"vuelve en 4 compases", L"4마디에 돌아옴", L"4小节回到主音", L"يعود خلال 4 موازير", L"возврат за 4 такта", L"kehrt in 4 Takten", L"volta em 4 compassos", L"terug in 4 maten", L"wraca po 4 taktach", L"4 ölçüde döner");
	const wchar_t* stepw = (c.leap <= 1)
		? LL14(L"順次", L"stepwise", L"par degrés", L"per gradi", L"por grados", L"순차", L"级进", L"متدرج", L"поступенно", L"stufenweise", L"por graus", L"trapsgewijs", L"stopniowo", L"derece derece")
		: LL14(L"ときおり跳躍", L"with leaps", L"avec des sauts", L"con salti", L"con saltos", L"때때로 도약", L"间有跳进", L"مع قفزات", L"с прыжками", L"mit Sprüngen", L"com saltos", L"met sprongen", L"ze skokami", L"sıçramalı");
	int pos = 0;
	const wchar_t* parts[5] = { motion, L" / ", turn, L" / ", stepw };
	for (int p = 0; p < 5; p++) {
		const wchar_t* s = parts[p];
		while (s && *s && pos < cch - 1) buf[pos++] = *s++;
	}
	buf[pos] = 0;
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

static int PlaceNote(Ctx* c, int ch, uint32_t tick, int midi, int dur, int vel, int gate);
static int ShiftOct(int midi, int oct, int lo, int hi);

static int ScaleStep(int rootPc, int type, int deg)
{
	static const int8_t maj[7] = { 0, 2, 4, 5, 7, 9, 11 };
	static const int8_t minr[7] = { 0, 2, 3, 5, 7, 8, 10 };
	static const int8_t dom[7] = { 0, 2, 4, 5, 7, 9, 10 };
	const int8_t* sc = maj;
	if (type == CH_MIN || type == CH_M7 || type == CH_DIM) sc = minr;
	else if (type == CH_7) sc = dom;
	int d = deg;
	int oct = 0;
	while (d < 0) { d += 7; oct--; }
	while (d > 6) { d -= 7; oct++; }
	int pc = (rootPc + (int)sc[d]) % 12;
	if (pc < 0) pc += 12;
	return 60 + oct * 12 + pc;
}

static int LeadProg(int style)
{
	switch (style) {
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE:
	case SC_AS_DNB: case SC_AS_GAME: case SC_AS_BREAKS:
		return 81;
	case SC_AS_HIPHOP: case SC_AS_RNB: case SC_AS_CITYPOP:
		return 80;
	case SC_AS_ROCK: case SC_AS_METAL: case SC_AS_ROCKNROLL:
		return 29;
	case SC_AS_PIANO: case SC_AS_BALLAD: case SC_AS_SLOWROCK:
		return 0;
	case SC_AS_ORCH: case SC_AS_ENKA: case SC_AS_FANFARE:
		return 40;
	default:
		return 73;
	}
}

static int s_loopBars = 32;
static int LeadDeg(int style, int pat, int bar, int nth, int strong, int type);
static int LeadMidi(int root, int type, int deg, int dropOct);

/* 空のパートへ、コードトーンだけのメロディを1本。ループ内では小節ごとに別フレーズ。
   ループ最後の小節は根音で終わり、次の周の頭（根音）へつながる。 */
static void WriteBgmLead(Ctx* c, int ch, const BarInfo* b, int barAt, int style, int pat, int form, int echoCh)
{
	if (!c || !b || ch < 0 || form < 18 || b->len < 8) return;
	if (b->section == SEC_BREAK) return;
	if (b->section == SEC_INTRO && form < 35) return;
	int loop = s_loopBars;
	if (loop < 16) loop = 16;
	if (loop > 64) loop = 64;
	int at = barAt % loop;
	if (at < 0) at = 0;
	const int loopLast = (at == loop - 1);
	const int stepDur = b->len / 8;
	if (stepDur < 2) return;
	int phrasePos = at & 3;
	int cad = (phrasePos == 3) || (b->section == SEC_END) || loopLast;
	const int fam = StyleFam(style);
	int mask = 0xD5;
	int lo = 72, hi = 93;
	switch (fam) {
	case FAM_BALLAD: case FAM_PAD: case FAM_WALTZ:
		mask = (form >= 75) ? 0xD5 : 0x55; lo = 64; hi = 84; break;
	case FAM_JAZZ: case FAM_BLUES: case FAM_SHUF:
		mask = (pat & 1) ? 0xDB : 0xB6; lo = 64; hi = 88; break;
	case FAM_HOP: case FAM_TRAP:
		mask = (at & 1) ? 0x94 : 0x51; if (form >= 85) mask |= 0x22; lo = 70; hi = 91; break;
	case FAM_FLOOR: case FAM_DISCO:
		mask = (form >= 60) ? 0xBB : 0xAA; lo = 74; hi = 96; break;
	case FAM_REG: case FAM_SKA:
		mask = (form >= 70) ? 0x55 : 0x44; lo = 72; hi = 91; break;
	case FAM_METAL: case FAM_8:
		mask = (form >= 65) ? 0xFF : 0x55; lo = 76; hi = 96; break;
	case FAM_DNB:
		mask = (form >= 80) ? 0xFF : ((at & 1) ? 0xF0 : 0x0F); lo = 74; hi = 96; break;
	case FAM_FUNK: case FAM_16:
		mask = (form >= 70) ? 0xFF : 0xAD; lo = 72; hi = 93; break;
	case FAM_LATIN: case FAM_SAMBA: case FAM_BOSSA: case FAM_AFRO:
		mask = (form >= 70) ? 0xDB : 0xA9; lo = 72; hi = 93; break;
	default:
		mask = (form >= 75) ? 0xFF : 0xD5; break;
	}
	if (form < 30) mask &= 0x55;
	if (!mask) mask = 0x11;
	int8_t deg[8];
	for (int s = 0; s < 8; s++) {
		int strong = ((s % 4) == 0);
		int half = (b->root2 >= 0 && s >= 4);
		int type = half ? b->type2 : b->type;
		int d = LeadDeg(style, pat, at, s + phrasePos * 2, strong, type);
		if (loopLast) d = (s >= 4) ? 1 : (s == 0 ? 5 : 3);
		if (b->section == SEC_END) d = (s == 0) ? 1 : -1;
		if (!(mask & (1 << s))) d = -1;
		if (loopLast && (s == 0 || s == 4 || s == 6)) d = (s == 0) ? 5 : 1;
		if (b->section == SEC_INTRO && (s & 1)) d = -1;
		if (c->isFm && form < 75 && (s & 1)) d = -1;
		deg[s] = (int8_t)d;
	}
	int prevMidi = -1;
	for (int s = 0; s < 8 && !c->overflow; s++) {
		if (deg[s] < 0) continue;
		int half = (b->root2 >= 0 && s >= 4);
		int root = half ? b->root2 : b->root;
		int type = half ? b->type2 : b->type;
		int d = deg[s];
		int strong = ((s % 4) == 0);
		int midi = LeadMidi(root, type, d, pat >= 8);
		while (midi < lo) midi += 12;
		while (midi > hi) midi -= 12;
		if (prevMidi >= 0 && midi == prevMidi && !cad) {
			int alt = LeadMidi(root, type, (d == 1) ? 3 : 1, pat >= 8);
			while (alt < lo) alt += 12;
			while (alt > hi) alt -= 12;
			if (alt != midi) midi = alt;
		}
		uint32_t tk = b->t0 + (uint32_t)(s * stepDur);
		int dur = stepDur;
		if (cad && s == 0 && b->section == SEC_END) dur = b->len;
		else if (cad && s >= 6) dur = stepDur * 2;
		else if (form >= 80) {
			int nxt = -1;
			for (int n = s + 1; n < 8; n++) if (deg[n] >= 0) { nxt = n; break; }
			if (nxt == s + 1) dur = stepDur;
			else if (nxt < 0) dur = stepDur * 2;
		}
		if (dur > b->len - s * stepDur) dur = b->len - s * stepDur;
		if (dur < 2) dur = 2;
		int vel = 86 + (d > 0 ? d * 3 : 0);
		if (strong) vel += 16;
		if (s == 2 || s == 6) vel -= 8;
		if (phrasePos == 3 && s >= 4) vel -= 6;
		if (vel < 72) vel = 72;
		if (vel > 122) vel = 122;
		int gate = (form >= 60) ? 88 : 72;
		if (cad && (s >= 6 || b->section == SEC_END)) gate = 94;
		if (c->isFm) gate = 82;
		PlaceNote(c, ch, tk, midi, dur, vel, gate);
		if (echoCh >= 0 && echoCh != ch && strong && form >= 55 && !loopLast) {
			int em = midi - 12;
			if (em >= 52 && em != midi)
				PlaceNote(c, echoCh, tk, em, dur, vel - 18, gate);
		}
		prevMidi = midi;
		if (b->section == SEC_END) break;
	}
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
static int s_arrPat;
static uint8_t s_partPan[32];
static uint8_t s_styleRev = 48;
static uint8_t s_styleCho = 16;

static void ResetPartPans(void)
{
	for (int i = 0; i < 32; i++) s_partPan[i] = 64;
}

/* DAT にある 1,2,8,16 などのバリエーション。キャピタル（bank 0）には落とさない。 */
static int ToneVar(int xg, int mapId, int pc, int which)
{
	static const int kVar[8] = { 1, 2, 8, 16, 24, 3, 9, 32 };
	wchar_t name[4];
	int hit[8];
	int n = 0;
	if (pc < 0) pc = 0;
	if (pc > 127) pc = 127;
	for (int i = 0; i < 8; i++) {
		name[0] = 0;
		if (xg)
			SasamiToneLookupStrict(1, 0, 0, kVar[i], pc, 0, name, 4);
		else
			SasamiToneLookupStrict(0, mapId, kVar[i], 0, pc, 0, name, 4);
		if (name[0]) hit[n++] = kVar[i];
	}
	if (!n) return 0;
	if (which < 0) which = 0;
	return hit[which % n];
}

static void RouteBank(int drum, int prog, int which, int* msb, int* lsb)
{
	const int map = s_arrGsMap;
	const int xg = (map == 5) || (map == 0 && ScStaffScorePreviewOut() == SC_OUT_XG);
	if (drum) {
		*msb = xg ? 127 : 0;
		*lsb = 0;
	} else if (xg) {
		/* XG は CC0=0、CC32 がバリエーション。無い PC だけキャピタル。 */
		*msb = 0;
		*lsb = ToneVar(1, 0, prog, which);
	} else {
		/* CC32 は音源マップ（未指定は 8820）。CC0 は DAT のバリエーション。
		   CC0=127 / CC32=0 は LA なので置かない。 */
		int mapId = 4;
		if (map >= 1 && map <= 4) mapId = map;
		*msb = ToneVar(0, mapId, prog, which);
		*lsb = mapId;
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
		RouteBank(0, prog, role + s_arrPat, &msb, &lsb);
		const int writeTone = !(keep && HeadProgChosen(c->ev, n, ch));
		/* 既に音色があるパートは、選んであるバンクをバリエーションで上書きしない。 */
		if (!writeTone) {
			for (int i = 0; i < n; i++) {
				if ((int)c->ev[i].ch != ch || c->ev[i].kind != SC_EV_BANK || c->ev[i].tick != 0)
					continue;
				msb = c->ev[i].a;
				lsb = c->ev[i].b;
				break;
			}
		}
		PlaceHeadTone(c, ch, 0, 0, writeTone, prog, msb, lsb);
		if (!HasKind(c->ev, n, ch, SC_EV_PAN)) {
			int pan = (ch >= 0 && ch < 32) ? (int)s_partPan[ch] : 64;
			Push(c, 0, ch, SC_EV_PAN, (uint8_t)pan, 0, 0, 0);
		}
		if (!HasKind(c->ev, n, ch, SC_EV_CC)) {
			int rev = (role == 0) ? (int)s_styleRev / 3 : (int)s_styleRev;
			int cho = (role == 0) ? 0 : (int)s_styleCho;
			if (rev > 127) rev = 127;
			if (cho > 127) cho = 127;
			Push(c, 0, ch, SC_EV_CC, 91, (uint8_t)rev, 0, 0);
			if (cho > 0) Push(c, 0, ch, SC_EV_CC, 93, (uint8_t)cho, 0, 0);
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
			int pan = (ch >= 0 && ch < 32) ? (int)s_partPan[ch] : 64;
			uint8_t lr = (pan < 48) ? 0x80 : (pan > 80) ? 0x40 : 0xC0;
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
		RouteBank(1, kit, 0, &msb, &lsb);
		const int writeTone = !(keep && HeadProgChosen(c->ev, *c->n, ch));
		PlaceHeadTone(c, ch, 1, slot, writeTone, kit, msb, lsb);
		if (!HasKind(c->ev, *c->n, ch, SC_EV_PAN)) {
			int pan = (ch >= 0 && ch < 32) ? (int)s_partPan[ch] : 64;
			Push(c, 0, ch, SC_EV_PAN, (uint8_t)pan, 0, 0, 0);
		}
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
	int pcStrong[12];
	memset(pc, 0, sizeof(pc));
	memset(pcStrong, 0, sizeof(pcStrong));
	int low = 127;
	int lowPc = -1;
	uint32_t blen = (t1 > t0) ? (t1 - t0) : 192;
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
		uint32_t off = (ev[i].tick > t0) ? (ev[i].tick - t0) : 0;
		int beatBoost = 1;
		if (off < blen / 8) beatBoost = 3;
		else if (off >= (blen * 3) / 8 && off < (blen * 5) / 8) beatBoost = 2;
		pc[m % 12] += w * beatBoost;
		if (beatBoost >= 2 || w >= (int)(blen / 4)) pcStrong[m % 12] += w * beatBoost;
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
	static const int kN[8] = { 3, 3, 4, 4, 4, 3, 3, 2 };
	static const int kIv[8][4] = {
		{ 0, 4, 7, 0 }, { 0, 3, 7, 0 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 },
		{ 0, 4, 7, 11 }, { 0, 5, 7, 0 }, { 0, 3, 6, 0 }, { 0, 7, 0, 0 }
	};
	int tonic = TonicPc(keySig);

	/* 既存が単音メロディ（chordCh < 0）のときは、SSW風のダイアトニック・メロディ和声付けを行う。
	   強拍・ロングトーンの旋律音をコードトーンとして最大限活かし、半音衝突(アボイド)を避け、
	   自然なコード進行（五度圏・順次進行・過剰反復回避）で和音を選ぶ。 */
	if (chordCh < 0) {
		struct Cand { int off; int ty; int pri; };
		static const Cand kDia[10] = {
			{ 0, CH_MAJ, 14 }, /* I */
			{ 5, CH_MAJ, 12 }, /* IV */
			{ 7, CH_7,   11 }, /* V7 */
			{ 9, CH_MIN, 11 }, /* vi */
			{ 2, CH_MIN,  9 }, /* ii */
			{ 4, CH_MIN,  7 }, /* iii */
			{ 7, CH_MAJ, 10 }, /* V */
			{ 2, CH_7,    4 }, /* V7/V */
			{ 4, CH_7,    4 }, /* V7/vi */
			{ 9, CH_7,    3 }  /* V7/ii */
		};
		int bestS = -1000000, bestR = tonic, bestT = CH_MAJ;
		for (int ci = 0; ci < 10; ci++) {
			int root = (tonic + kDia[ci].off) % 12;
			int t = kDia[ci].ty;
			int sc = kDia[ci].pri * 8;
			for (int p = 0; p < 12; p++) {
				if (pc[p] <= 0) continue;
				int inChord = 0;
				for (int k = 0; k < kN[t]; k++) {
					if (((root + kIv[t][k]) % 12) == p) { inChord = 1; break; }
				}
				if (inChord) {
					sc += pc[p] * 3 + pcStrong[p] * 2;
				} else {
					/* コードの3度/ルートと短9度(1半音上)でぶつかるアボイドを強く減点 */
					int iv = (p - root + 12) % 12;
					int third = (t == CH_MIN || t == CH_M7 || t == CH_DIM) ? 3 : 4;
					if (iv == 1 || iv == ((third + 1) % 12))
						sc -= pc[p] * 4 + pcStrong[p] * 3;
					else
						sc -= pc[p];
				}
			}
			/* スムーズな和声進行の優先（五度下行 +18, 4度進行 +12, 同一和音継続は控えめに +4） */
			if (prevRoot >= 0) {
				int mv = (root - prevRoot + 12) % 12;
				if (mv == 5) sc += 18;      /* V->I, ii->V, vi->ii 等 */
				else if (mv == 7 || mv == 2 || mv == 9 || mv == 10) sc += 10;
				else if (mv == 0) sc += 4;
			}
			if (sc > bestS) { bestS = sc; bestR = root; bestT = t; }
		}
		*outRoot = bestR;
		*outType = bestT;
		*outVoiced = 1;
		return;
	}

	int distinct = 0;
	for (int i = 0; i < 12; i++) if (pc[i] > 0) distinct++;
	int consonant = (distinct >= 3) ? 1 : 0;
	if (!consonant && distinct == 2) {
		int a = -1, b = -1;
		for (int i = 0; i < 12; i++) if (pc[i] > 0) { if (a < 0) a = i; else b = i; }
		int iv = (b - a + 12) % 12;
		if (iv > 6) iv = 12 - iv;
		if (iv == 3 || iv == 4 || iv == 5 || iv == 7) consonant = 1;
	}
	if (!consonant) {
		*outRoot = prevRoot >= 0 ? prevRoot : tonic;
		*outType = prevType >= 0 ? prevType : CH_MAJ;
		*outVoiced = 0;
		return;
	}
	int bestS = -100000, bestR = tonic, bestT = CH_MAJ;
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

/* 16アレンジ（A〜P）の音楽的キャラクター詳細。
   単なる位相ずらしではなく、バッキング奏法・ベース奏法・アーティキュレーション・拡張和音・掛け合いを変える。 */
struct ArrProfile {
	uint8_t compMode;  /* 0:スタイル基準 1:16分刻み/ストローク 2:ボイスリード7th/9th 3:裏打ち強調
	                      4:ハーフタイム余白 5:8分ドライブ 6:分散カスケード 7:スタカートキメ
	                      8:レガート持続 9:コール&レスポンス 10:ポリリズム強調 */
	uint8_t bassMode;  /* 0:スタイル基準 1:ウォーキング/経過音多め 2:オクターブ跳躍 3:ハーフタイム重低音
	                      4:シンコペ溜め 5:8分推進 6:ファンク・フィル多め */
	uint8_t gatePct;   /* 42..98 基本ゲート長(%) */
	int8_t  octShift;  /* -12..+12 上声部音域バイアス */
	uint8_t extLevel;  /* 0:三和音/パワー 1:7th 2:9th/add9 */
	uint8_t callResp;  /* 0:なし 1:半小節交互 2:小節交互 */
	uint8_t progRot;   /* 0..3 進行の回し開始オフセット */
	uint8_t velShape;  /* 0:標準 1:頭拍強め 2:裏拍/2-4強め 3:うねりクレッシェンド 4:親密ソフト */
};

static const ArrProfile kArrProf[SC_ARRANGE_COUNT] = {
	/* A 標準スタジオ */     { 0, 0, 82,  0, 1, 0, 0, 0 },
	/* B タイト・シンコペ */ { 1, 4, 68,  0, 1, 0, 0, 2 },
	/* C 引き算・親密 */     { 2, 3, 86,  0, 1, 0, 0, 4 },
	/* D フルドライブ */     { 5, 5, 84,  0, 1, 0, 1, 1 },
	/* E コード歌い */       { 2, 1, 90,  0, 2, 0, 0, 3 },
	/* F 裏拍アクセント */   { 3, 4, 62,  5, 1, 0, 1, 2 },
	/* G ハーフタイム余白 */ { 4, 3, 92, -5, 2, 0, 0, 4 },
	/* H 推進力・刻み */     { 5, 5, 74,  0, 0, 0, 2, 1 },
	/* I 分散カスケード */   { 6, 1, 88,  7, 2, 0, 1, 3 },
	/* J ベース主導 */       { 3, 6, 70, -7, 1, 1, 2, 2 },
	/* K スタカートキメ */   { 7, 4, 48,  0, 1, 1, 0, 1 },
	/* L レガート膨らみ */   { 8, 0, 96,  0, 2, 0, 0, 3 },
	/* M ブライト高域 */     { 6, 2, 80, 12, 2, 0, 1, 0 },
	/* N ダーク重厚 */       { 4, 3, 84,-12, 0, 0, 2, 1 },
	/* O 掛け合い交互 */     { 9, 6, 76,  0, 1, 2, 1, 2 },
	/* P 祝祭ポリリズム */   {10, 2, 78,  5, 2, 1, 3, 3 }
};

/* 64小節以上でも同じパターンを繰り返さないための小節別マクロ設計図。 */
struct BarPlan {
	uint8_t sec;       /* 0:A1 1:A2 2:B1(前サビ) 3:C1(サビ1) 4:A3(2番A) 5:D(ブリッジ) 6:E(ソロ/展開) 7:C2(大サビ) */
	uint8_t phraseBar; /* セクション内の位置 0..7 */
	uint8_t energy;    /* 30..100 */
	uint8_t turn;      /* 0:通常 1:4小節折返し 2:8小節ターンアラウンド 3:最終終止 */
	int8_t  regShift;  /* -12, 0, +12 */
	uint8_t extLvl;    /* 0:三和音/パワー 1:7th 2:9th */
	uint8_t varBits;   /* 全小節で異なる決定論的変異ビット */
	uint8_t breakFeel; /* 0:通常 1:ハーフ/抜き 2:ビルドアップ */
};

static BarPlan s_plan[256];
static int s_planN = 0;

static uint32_t PlanHash(int style, int pat, int var, int bar)
{
	uint32_t h = (uint32_t)(style * 2654435761u) ^ (uint32_t)((pat + 1) * 2246822519u)
		^ (uint32_t)((var + 7) * 3266489917u) ^ (uint32_t)((bar + 13) * 668265263u);
	h ^= h >> 15;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h;
}

static const BarPlan& PlanOfBar(int bar)
{
	if (s_planN <= 0) return s_plan[0];
	if (bar < 0) bar = 0;
	if (bar < s_planN && bar < 256) return s_plan[bar];
	return s_plan[bar % (s_planN < 256 ? s_planN : 256)];
}

/* 64小節の楽曲構成（A1→A2→B1→C1→A3→D→E→C2）と自然なコード進行を組み立てる。
   機械的な+5/+7移調や2小節ループを避け、スタイルのkHarm[0]/[1]とダイアトニック展開で破綻なく変化させる。 */
static void BuildSongPlan(BarInfo* bars, int nBars, int style, int pat, int variation, int keySig, int useProg)
{
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	if (pat < 0 || pat >= SC_ARRANGE_COUNT) pat = 0;
	const ArrProfile& ap = kArrProf[pat & 15];
	const int tonic = TonicPc(keySig);
	const int fam = StyleFam(style);
	const int isPow = kDef[style].power ? 1 : 0;
	const int isBlues12 = kVoice[style].form12 ? 1 : 0;

	ProgStep stepA[8], stepB[8];
	int nA = StyleProgression(style, variation & 1, stepA, 8);
	int nB = StyleProgression(style, (variation ^ 1) & 1, stepB, 8);
	if (nA < 1) { stepA[0].off = 0; stepA[0].type = CH_MAJ; nA = 1; }
	if (nB < 1) { stepB[0] = stepA[0]; nB = 1; }

	const int homeType = (int)stepA[0].type;
	const int isMinor = (homeType == CH_MIN || homeType == CH_M7
		|| kVoice[style].scale == 1 || kVoice[style].scale == 2
		|| kVoice[style].scale == 6 || kVoice[style].scale == 7);
	const int isRich = (fam == FAM_JAZZ || fam == FAM_BLUES || fam == FAM_BOSSA || fam == FAM_FUNK
		|| style == SC_AS_CITYPOP || style == SC_AS_NEOSOUL || style == SC_AS_FUSION
		|| style == SC_AS_RNB || style == SC_AS_LOFI || style == SC_AS_PIANO || style == SC_AS_GOSPEL);
	const int halfStyle = (fam == FAM_JAZZ || fam == FAM_FUNK || fam == FAM_BOSSA || fam == FAM_BLUES
		|| style == SC_AS_CITYPOP || style == SC_AS_FUSION || style == SC_AS_GOSPEL
		|| style == SC_AS_SOUL || style == SC_AS_NEOSOUL || style == SC_AS_ANISON || style == SC_AS_16BEAT);

	int bodyN = 0;
	if (bars && nBars > 0) {
		for (int i = 0; i < nBars; i++)
			if (bars[i].section == SEC_BODY) bodyN++;
	}
	if (bodyN < 16) bodyN = 16;
	s_planN = (bodyN < 256) ? bodyN : 256;

	/* セクション別エネルギーと音域の設計（64小節を8セクション×8小節で展開） */
	static const uint8_t kSecEnergy[8] = { 54, 66, 76, 90, 58, 68, 84, 98 };
	static const int8_t  kSecReg[8]    = {  0,  0,  0, 12,  0, -12, 12, 12 };

	for (int b = 0; b < s_planN; b++) {
		int sec = 0;
		int pBar = b & 7;
		if (bodyN >= 48) {
			sec = (b / 8) & 7;
			pBar = b & 7;
		} else if (bodyN >= 24) {
			static const uint8_t s32[4] = { 0, 2, 3, 7 };
			sec = s32[(b / 8) & 3];
			pBar = b & 7;
		} else {
			static const uint8_t s16[4] = { 0, 2, 3, 7 };
			sec = s16[(b / 4) & 3];
			pBar = (b & 3) * 2;
		}
		BarPlan& pl = s_plan[b];
		pl.sec = (uint8_t)sec;
		pl.phraseBar = (uint8_t)pBar;
		int en = (int)kSecEnergy[sec] + (pBar * 2) - 4;
		if (sec == 2 && pBar >= 4) en += (pBar - 3) * 4; /* 前サビのビルドアップ */
		if (sec == 6 && pBar >= 4) en += (pBar - 3) * 3; /* ソロ後半の高揚 */
		if (ap.velShape == 4) en -= 14;
		else if (ap.velShape == 1) en += 6;
		if (en < 32) en = 32;
		if (en > 100) en = 100;
		pl.energy = (uint8_t)en;
		pl.turn = 0;
		if (b + 1 >= bodyN) pl.turn = 3;
		else if (pBar == 7) pl.turn = 2;
		else if (pBar == 3) pl.turn = 1;
		pl.regShift = kSecReg[sec];
		if (sec == 4 && (pat & 1)) pl.regShift = 0;
		int ext = (int)ap.extLevel;
		if (isPow) ext = 0;
		else if (isRich && ext < 1) ext = 1;
		if ((sec == 3 || sec == 5 || sec == 7) && !isPow && ext < 2) ext++;
		pl.extLvl = (uint8_t)ext;
		uint32_t h = PlanHash(style, pat, variation, b);
		pl.varBits = (uint8_t)(h & 0xFFu);
		pl.breakFeel = 0;
		if ((sec == 4 && pBar < 4) || (sec == 5 && pBar < 2)) pl.breakFeel = 1;
		else if ((sec == 2 && pBar >= 5) || (sec == 6 && pBar >= 5)) pl.breakFeel = 2;
		if (ap.compMode == 4 && (sec == 0 || sec == 4 || sec == 5)) pl.breakFeel = 1;
	}

	if (!bars || nBars < 1) return;

	/* セクション別の音楽的コード設計（メジャー／マイナー／12小節ブルース） */
	static const uint8_t kPreMaj[8]  = { 0x05, 0x07, 0x14, 0x19, 0x12, 0x07, 0x05, 0x27 };
	static const uint8_t kPreMin[8]  = { 0x08, 0x0A, 0x10, 0x15, 0x08, 0x0A, 0x15, 0x27 };
	static const uint8_t kBrgMaj[8]  = { 0x45, 0x15, 0x34, 0x39, 0x32, 0x27, 0x34, 0x29 };
	static const uint8_t kBrgMin[8]  = { 0x08, 0x03, 0x0A, 0x10, 0x15, 0x08, 0x12, 0x27 };
	static const uint8_t kBrgJazz[8] = { 0x45, 0x66, 0x34, 0x29, 0x32, 0x27, 0x34, 0x29 };
	static const uint8_t kPowPre[8]  = { 0x75, 0x77, 0x79, 0x75, 0x72, 0x75, 0x77, 0x77 };
	static const uint8_t kPowBrg[8]  = { 0x78, 0x7A, 0x70, 0x75, 0x78, 0x7A, 0x77, 0x77 };

	auto decodePacked = [&](uint8_t pk, int& outOff, int& outTy) {
		outOff = (int)(pk & 15);
		outTy = (int)((pk >> 4) & 7);
		if (isPow) outTy = CH_POW;
		else if (isRich) {
			if (outTy == CH_MAJ) outTy = CH_MAJ7;
			else if (outTy == CH_MIN) outTy = CH_M7;
		}
	};

	int introTot = 0, endTot = 0;
	for (int i = 0; i < nBars; i++) {
		if (bars[i].section == SEC_INTRO) introTot++;
		else if (bars[i].section == SEC_END) endTot++;
	}

	int body = 0, introCur = 0, endCur = 0;
	for (int i = 0; i < nBars; i++) {
		if (bars[i].section == SEC_INTRO) {
			/* イントロ: 1小節目はI、最後はV7で本体へ繋ぐ自然なターンアラウンド */
			int k = introCur++;
			int offI = (int)stepA[k % nA].off;
			int tyI = (int)stepA[k % nA].type;
			if (introTot >= 2 && k == introTot - 1) {
				offI = 7;
				tyI = isPow ? CH_POW : (isRich ? CH_7 : ((pat & 1) ? CH_SUS : CH_7));
			} else if (introTot >= 4 && k == introTot - 2) {
				offI = isMinor ? 5 : 2;
				tyI = isPow ? CH_POW : (isRich ? CH_M7 : CH_MIN);
			}
			bars[i].root = (tonic + offI) % 12;
			bars[i].type = tyI;
			bars[i].root2 = -1;
			bars[i].type2 = 0;
			continue;
		}
		if (bars[i].section == SEC_END) {
			/* エンディング: 2小節以上なら IV/ii -> V7 -> I の本格的な終止形を組む */
			int k = endCur++;
			int rem = endTot - 1 - k;
			int ty0 = homeType;
			if (ty0 == CH_7 || ty0 == CH_MAJ7 || ty0 == CH_SUS || ty0 == CH_POW) ty0 = isPow ? CH_POW : CH_MAJ;
			else if (ty0 == CH_M7 || ty0 == CH_DIM) ty0 = CH_MIN;
			int offE = 0, tyE = ty0;
			if (rem == 1) {
				offE = 7;
				tyE = isPow ? CH_POW : CH_7;
			} else if (rem == 2) {
				offE = isMinor ? 5 : ((isRich || (pat & 1)) ? 2 : 5);
				tyE = isPow ? CH_POW : ((offE == 2) ? (isRich ? CH_M7 : CH_MIN) : (isMinor ? CH_MIN : (isRich ? CH_MAJ7 : CH_MAJ)));
			} else if (rem >= 3) {
				offE = isMinor ? 8 : 9;
				tyE = isPow ? CH_POW : (isMinor ? CH_MAJ : (isRich ? CH_M7 : CH_MIN));
			}
			bars[i].root = (tonic + offE) % 12;
			bars[i].type = tyE;
			bars[i].root2 = -1;
			bars[i].type2 = 0;
			continue;
		}
		if (bars[i].section == SEC_BREAK) {
			/* ブレイク: 既存コードが無ければトニック/ドミナントで繋ぐ */
			if (!bars[i].voiced) {
				bars[i].root = (i > 0 && bars[i - 1].root >= 0) ? bars[i - 1].root : tonic;
				bars[i].type = (i > 0) ? bars[i - 1].type : homeType;
			}
			continue;
		}
		int at = (bars[i].section == SEC_BODY) ? body++ : 0;
		const BarPlan& pl = PlanOfBar(at);
		/* 既存メロディ/コードから和声が解析されている小節は、useProg 明示時以外そのまま尊重 */
		if (bars[i].voiced && !useProg) continue;
		int off = 0, ty = CH_MAJ;
		const int rot = (int)(ap.progRot & 3);
		const int cyc = at / 64; /* 64小節超えの周回でさらに変化 */

		if (isBlues12) {
			int p12 = at % 12;
			int chorus = at / 12;
			static const int bOff[12] = { 0, 0, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7 };
			off = bOff[p12];
			ty = CH_7;
			/* クイックチェンジやジャズブルース変化をコーラス毎に変える */
			if (p12 == 1 && ((chorus + pat) & 1)) off = 5;
			else if (p12 == 7 && chorus >= 1) { off = 9; ty = CH_7; }
			else if (p12 == 8 && (chorus & 1)) { off = 2; ty = CH_M7; }
			else if (p12 == 9 && (chorus & 1)) { off = 7; ty = CH_7; }
		} else {
			int p8 = pl.phraseBar & 7;
			int idxA = (p8 + rot + (cyc & 1 ? 2 : 0)) % nA;
			int idxB = (p8 + (rot >> 1) + (cyc & 1 ? 4 : 0)) % nB;
			switch (pl.sec) {
			case 0: /* A1: スタイル本来の進行 A */
				off = (int)stepA[idxA].off;
				ty = (int)stepA[idxA].type;
				break;
			case 1: /* A2: 進行 A + 後半ターンアラウンド */
				off = (int)stepA[idxA].off;
				ty = (int)stepA[idxA].type;
				if (p8 == 6) { off = isMinor ? 5 : 2; ty = isPow ? CH_POW : (isRich ? CH_M7 : CH_MIN); }
				else if (p8 == 7) { off = 7; ty = isPow ? CH_POW : CH_7; }
				break;
			case 2: /* B1: 前サビ（テンション上昇→V7） */
				decodePacked(isPow ? kPowPre[p8] : (isMinor ? kPreMin[p8] : kPreMaj[p8]), off, ty);
				break;
			case 3: /* C1: サビ1（スタイルの進行 B） */
				off = (int)stepB[idxB].off;
				ty = (int)stepB[idxB].type;
				break;
			case 4: /* A3: 2番A（リハーモナイズ版：A1の複製にしない） */
				off = (int)stepA[(p8 + 4 + rot) % nA].off;
				ty = (int)stepA[(p8 + 4 + rot) % nA].type;
				if (p8 == 0) { off = (int)stepA[0].off; ty = (int)stepA[0].type; }
				else if (p8 == 2) { off = isMinor ? 3 : 4; ty = isPow ? CH_POW : (isMinor ? CH_MAJ : (isRich ? CH_M7 : CH_MIN)); }
				else if (p8 == 7) { off = 7; ty = isPow ? CH_POW : CH_7; }
				break;
			case 5: /* D: ブリッジ（調性を保った展開） */
				if (isPow) decodePacked(kPowBrg[p8], off, ty);
				else if (fam == FAM_JAZZ || style == SC_AS_BOSSA || style == SC_AS_FUSION)
					decodePacked(kBrgJazz[p8], off, ty);
				else decodePacked(isMinor ? kBrgMin[p8] : kBrgMaj[p8], off, ty);
				break;
			case 6: /* E: ソロ／クライマックス準備 */
				if (p8 < 4) {
					off = (int)stepB[(p8 + 4) % nB].off;
					ty = (int)stepB[(p8 + 4) % nB].type;
				} else if (p8 < 7) {
					off = (int)stepA[(p8 + 2) % nA].off;
					ty = (int)stepA[(p8 + 2) % nA].type;
				} else {
					off = 7;
					ty = isPow ? CH_POW : ((pat & 1) ? CH_SUS : CH_7);
				}
				break;
			default: /* C2: 大サビ＋強い終止 */
				off = (int)stepB[idxB].off;
				ty = (int)stepB[idxB].type;
				if (p8 == 3) { off = isMinor ? 8 : 9; ty = isPow ? CH_POW : (isMinor ? CH_MAJ : (isRich ? CH_M7 : CH_MIN)); }
				else if (p8 == 6) { off = isMinor ? 5 : 2; ty = isPow ? CH_POW : (isRich ? CH_M7 : CH_MIN); }
				else if (p8 == 7) { off = (pl.turn == 3) ? 0 : 7; ty = (pl.turn == 3) ? homeType : (isPow ? CH_POW : CH_7); }
				break;
			}
		}

		if (off < 0) off = 0;
		if (ty < 0 || ty > CH_POW) ty = CH_MAJ;
		int root = (tonic + off) % 12;
		if (root < 0) root += 12;
		bars[i].root = root;
		bars[i].type = ty;
		bars[i].root2 = -1;
		bars[i].type2 = 0;
	}

	/* 2パス目: 次の小節の根音を見た上で、音楽的な半小節パッシング/ドミナント分割(root2)を置く */
	body = 0;
	for (int i = 0; i < nBars; i++) {
		if (bars[i].section != SEC_BODY || bars[i].len < 168) continue;
		int at = body++;
		if (!useProg && bars[i].voiced) continue;
		const BarPlan& pl = PlanOfBar(at);
		if (pl.turn == 3) continue;
		int nextRoot = (i + 1 < nBars && bars[i + 1].root >= 0) ? bars[i + 1].root : tonic;
		int wantHalf = 0;
		if (pl.turn == 2) wantHalf = 1; /* 8小節末のターンアラウンド */
		else if (halfStyle && (pl.sec >= 3 || (pl.varBits & 1))) wantHalf = ((at + pat) & 1);
		else if (pl.sec == 6 && pl.phraseBar >= 4) wantHalf = 1; /* Eセクション後半の加速 */
		else if (pl.sec == 7 && (pl.phraseBar == 5 || pl.phraseBar == 6)) wantHalf = 1;
		if (!wantHalf) continue;
		/* 次小節へのドミナント(V7 of next)またはダイアトニック経過和音 */
		int r2 = (nextRoot + 7) % 12;
		int t2 = isPow ? CH_POW : CH_7;
		if (r2 == bars[i].root) {
			r2 = (tonic + (isMinor ? 10 : 5)) % 12;
			t2 = isPow ? CH_POW : (isRich ? CH_MAJ7 : CH_MAJ);
		}
		bars[i].root2 = r2;
		bars[i].type2 = t2;
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

/* 4曲に共通の身振り。リズム型でもコードの積み上げでもない。
   分散は同じ指順（根・五・三・五・根の上）を次の和音へ運び、
   部分メロディは3度の二音がスケールを数音歩いて折り返す。 */
struct MotifWalk {
	int sweep;
	int prevSweep;
	int deg;
	int dir;
	int run;
	int prevTop;
	int slotSweep;
	int slotFrag;
	int fragTop;
	int fragThird;
};

static MotifWalk s_motif;

static void MotifBegin(void)
{
	s_motif.sweep = 0;
	s_motif.prevSweep = -1;
	s_motif.deg = 0;
	s_motif.dir = 1;
	s_motif.run = 0;
	s_motif.prevTop = -1;
	s_motif.slotSweep = -1;
	s_motif.slotFrag = -1;
	s_motif.fragTop = 64;
	s_motif.fragThird = 60;
}

static int NearMidi(int midi, int prev, int lo, int hi)
{
	if (hi < lo) hi = lo;
	if (prev >= 0) {
		while (midi < prev - 7) midi += 12;
		while (midi > prev + 6) midi -= 12;
	}
	while (midi < lo) midi += 12;
	while (midi > hi) midi -= 12;
	if (midi < lo) midi = lo;
	if (midi > hi) midi = hi;
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	return midi;
}

/* スチールギターの分散。一音ずつ。和音が変わっても指順は続け、近いオクターブへ寄せる。 */
static int BrokenSweep(int root, int type, int power, int slotKey)
{
	if (s_motif.slotSweep == slotKey && s_motif.prevSweep >= 0)
		return s_motif.prevSweep;
	s_motif.slotSweep = slotKey;
	int tones[4];
	int nt = ChordTones(root, type, power, tones, 4);
	if (nt < 1) return 64;
	/* -1 は五度の下、0 根、1 三度、2 五度、3 根の上。上がってから2音降りる。 */
	static const int finger[8] = { -1, 0, 1, 2, 3, 2, 1, 0 };
	int f = finger[s_motif.sweep & 7];
	s_motif.sweep++;
	int m;
	if (f < 0) m = tones[nt - 1] - 12;
	else if (f >= 3) m = tones[0] + 12;
	else if (f >= nt) m = tones[nt - 1];
	else m = tones[f];
	m = NearMidi(m, s_motif.prevSweep, 48, 84);
	s_motif.prevSweep = m;
	return m;
}

/* 弦の掛け合い。強拍はコードトーン、あいだは隣の音。もう一音は下の3度。 */
static void PartialDyad(int root, int type, int strong, int slotKey, int* top, int* third)
{
	if (s_motif.slotFrag == slotKey) {
		if (top) *top = s_motif.fragTop;
		if (third) *third = s_motif.fragThird;
		return;
	}
	s_motif.slotFrag = slotKey;
	int deg = s_motif.deg;
	if (strong) {
		static const int chordDeg[3] = { 0, 2, 4 };
		int best = 0;
		int bestD = 99;
		for (int i = 0; i < 3; i++) {
			int d = deg - chordDeg[i];
			if (d < 0) d = -d;
			if (d < bestD) { bestD = d; best = chordDeg[i]; }
		}
		deg = best;
		s_motif.run = 0;
	} else {
		deg += s_motif.dir;
		s_motif.run++;
		if (s_motif.run >= 4) {
			s_motif.dir = -s_motif.dir;
			s_motif.run = 0;
		}
	}
	while (deg < 0) deg += 7;
	while (deg > 6) deg -= 7;
	s_motif.deg = deg;
	int hi = ScaleStep(root, type, deg);
	int lo = ScaleStep(root, type, deg - 2);
	hi = NearMidi(hi, s_motif.prevTop, 64, 88);
	lo = NearMidi(lo, hi - 4, 55, 81);
	if (lo >= hi) lo = hi - 3;
	if (lo < 0) lo = 0;
	s_motif.prevTop = hi;
	s_motif.fragTop = hi;
	s_motif.fragThird = lo;
	if (top) *top = hi;
	if (third) *third = lo;
}

/* ループ長（最大64、メロディは16〜256）を一本の弧にする。
   全60スタイル×全16アレンジに個別のリズム・音色空間・表情・ベンド・ドラム性格を持たせる。 */
struct MelSlot {
	int8_t deg;
	uint8_t at, du, leg, bend;
};

enum {
	CCF_EXPR = 1,
	CCF_MOD = 2,
	CCF_PEDAL = 4,
	CCF_CLAP = 8
};

struct StyleExtra {
	uint8_t rev, cho, bendMode, ccFlags, arpMode, percSet, melDense, salt;
	uint16_t kickA, kickB, snareA, snareB, chordA, chordB, arpA, arpB;
};

/* 60スタイル固有の語法。A/Bパターン・パーカッション群・空間・ベンド・CC・アルペジオ型を全スタイル別に定義。 */
static const StyleExtra kStyleExtra[SC_AS_COUNT] = {
	/* 0 16BEAT */    { 46, 22, 1, CCF_EXPR|CCF_CLAP,         1, 0, 5, 11, 0x0409, 0x4841, 0x1010, 0x5210, 0x9249, 0x4A95, 0xDB6D, 0xEEEE },
	/* 1 8BEAT */     { 44, 14, 0, CCF_EXPR,                  0, 1, 4, 17, 0x0101, 0x0501, 0x1010, 0x1050, 0x5555, 0x1555, 0x5555, 0x5155 },
	/* 2 REGGAE */    { 54, 18, 0, CCF_EXPR,                  2, 5, 3, 23, 0x0100, 0x0101, 0x0100, 0x1100, 0x4444, 0x4440, 0x4444, 0x0444 },
	/* 3 TECHNO */    { 50, 36, 5, CCF_EXPR|CCF_CLAP,         3, 3, 4, 29, 0x1111, 0x1115, 0x1010, 0x1014, 0x4444, 0x5454, 0xFFFF, 0xAAAA },
	/* 4 HOUSE */     { 52, 30, 5, CCF_EXPR|CCF_CLAP,         3, 3, 4, 31, 0x1111, 0x1131, 0x1010, 0x1210, 0x4444, 0x4644, 0xAAAA, 0xDB6D },
	/* 5 TRANCE */    { 68, 44, 5, CCF_EXPR|CCF_MOD|CCF_CLAP, 3, 3, 4, 37, 0x1111, 0x5111, 0x1010, 0x5010, 0x0101, 0x4444, 0xFFFF, 0xF777 },
	/* 6 DNB */       { 42, 24, 5, CCF_EXPR,                  3, 3, 5, 41, 0x0401, 0x2409, 0x1010, 0x5012, 0x0909, 0x2449, 0xF6F6, 0xB6B6 },
	/* 7 HIPHOP */    { 48, 12, 4, CCF_EXPR|CCF_CLAP,         0, 3, 3, 43, 0x2101, 0x4481, 0x1010, 0x1010, 0x0101, 0x0901, 0x4445, 0x1505 },
	/* 8 FUNK */      { 34, 24, 1, CCF_EXPR|CCF_CLAP,         1, 0, 5, 47, 0x4209, 0x2449, 0x1450, 0x5210, 0x4924, 0x9249, 0xDB76, 0xB6DB },
	/* 9 DISCO */     { 48, 28, 0, CCF_EXPR|CCF_CLAP,         1, 0, 5, 53, 0x1111, 0x1511, 0x1010, 0x1010, 0x4444, 0x5454, 0xAAAA, 0x5555 },
	/* 10 ROCK */     { 40, 10, 1, CCF_EXPR,                  0, 1, 4, 59, 0x0501, 0x1505, 0x1010, 0x1410, 0x5555, 0x1555, 0x5555, 0x5551 },
	/* 11 METAL */    { 36,  8, 1, 0,                         5, 1, 6, 61, 0x5555, 0xFFFF, 0x1010, 0x5010, 0x5555, 0xDB6D, 0xFFFF, 0xEEEE },
	/* 12 BALLAD */   { 78, 28, 2, CCF_EXPR|CCF_MOD|CCF_PEDAL,4, 4, 3, 67, 0x0001, 0x0101, 0x1000, 0x1010, 0x0101, 0x1111, 0x1515, 0x5515 },
	/* 13 WALTZ */    { 66, 20, 0, CCF_EXPR|CCF_PEDAL,        4, 4, 3, 71, 0x0001, 0x0001, 0x0110, 0x0110, 0x0110, 0x0111, 0x0111, 0x0111 },
	/* 14 BOSSA */    { 52, 16, 2, CCF_EXPR,                  2, 5, 4, 73, 0x0909, 0x4909, 0x1428, 0x4228, 0x4925, 0x9249, 0x5555, 0x4A55 },
	/* 15 SAMBA */    { 50, 18, 0, CCF_EXPR,                  1, 5, 5, 79, 0x4919, 0x8929, 0x5212, 0x9252, 0x9249, 0x5A95, 0xDB6D, 0xB6B6 },
	/* 16 SHUFFLE */  { 44, 12, 6, CCF_EXPR,                  0, 2, 4, 83, 0x0101, 0x0901, 0x1010, 0x1810, 0x4949, 0x9249, 0x9292, 0x4949 },
	/* 17 MARCH */    { 54,  8, 0, CCF_EXPR,                  0, 4, 4, 89, 0x1111, 0x1111, 0x5555, 0x5454, 0x1111, 0x5555, 0x5555, 0x1515 },
	/* 18 POLKA */    { 44,  8, 0, 0,                         0, 4, 5, 97, 0x0101, 0x1111, 0x4444, 0x5454, 0x4444, 0x4444, 0x5555, 0x4444 },
	/* 19 CITYPOP */  { 56, 34, 2, CCF_EXPR|CCF_MOD|CCF_CLAP, 1, 0, 5,101, 0x2409, 0x4A41, 0x1010, 0x1290, 0x4A92, 0x924A, 0xDB6D, 0xEEB6 },
	/* 20 GAME */     { 48, 22, 0, CCF_EXPR,                  5, 0, 6,103, 0x0901, 0x4A41, 0x1010, 0x1410, 0x4A95, 0xDB6D, 0xFFFF, 0xDB6D },
	/* 21 ORCH */     { 84, 30, 2, CCF_EXPR|CCF_MOD|CCF_PEDAL,4, 4, 4,107, 0x0101, 0x1101, 0x1000, 0x5010, 0x0101, 0x1111, 0x5555, 0x1555 },
	/* 22 AMBIENT */  { 92, 48, 0, CCF_EXPR|CCF_MOD|CCF_PEDAL,4, 4, 2,109, 0x0001, 0x0100, 0x0000, 0x1000, 0x0001, 0x0101, 0x1111, 0x0505 },
	/* 23 BREAKS */   { 42, 20, 4, CCF_EXPR|CCF_CLAP,         3, 3, 4,113, 0x0841, 0x4849, 0x5010, 0x1290, 0x4909, 0x9249, 0xB6B6, 0xD6D6 },
	/* 24 LATIN */    { 54, 18, 2, CCF_EXPR,                  1, 5, 5,127, 0x0401, 0x2409, 0x5210, 0x9250, 0x9249, 0x4A92, 0xDB6D, 0x5555 },
	/* 25 ENKA */     { 74, 24, 3, CCF_EXPR|CCF_MOD|CCF_PEDAL,2, 4, 3,131, 0x0101, 0x0401, 0x1000, 0x1010, 0x0101, 0x1111, 0x1515, 0x0515 },
	/* 26 FUSION */   { 48, 28, 1, CCF_EXPR|CCF_MOD,          1, 0, 6,137, 0x2409, 0x4A29, 0x1490, 0x5212, 0x4A92, 0xA925, 0xF6F6, 0xDB6D },
	/* 27 ACOUSTIC */ { 58, 16, 1, CCF_EXPR|CCF_PEDAL,        2, 0, 4,139, 0x0101, 0x0501, 0x1010, 0x1010, 0x1111, 0x1515, 0x5555, 0x5515 },
	/* 28 PIANO */    { 64, 18, 0, CCF_EXPR|CCF_PEDAL,        4, 2, 4,149, 0x0101, 0x0401, 0x1010, 0x1090, 0x0101, 0x1515, 0x5555, 0x1555 },
	/* 29 FANFARE */  { 76, 20, 0, CCF_EXPR,                  0, 4, 4,151, 0x1111, 0x5511, 0x5555, 0x5454, 0x1515, 0x5555, 0x5555, 0x1515 },
	/* 30 SLOWROCK */ { 62, 24, 1, CCF_EXPR|CCF_PEDAL,        2, 1, 3,157, 0x0101, 0x0401, 0x1010, 0x1010, 0x1111, 0x5555, 0x5555, 0x1555 },
	/* 31 JAZZ */     { 46, 10, 2, CCF_EXPR,                  0, 2, 5,163, 0x0101, 0x0901, 0x1010, 0x9010, 0x4849, 0x9244, 0x5555, 0x4949 },
	/* 32 SWINGJAZZ */{ 52, 12, 2, CCF_EXPR,                  0, 2, 5,167, 0x1111, 0x1111, 0x1010, 0x1090, 0x4949, 0x5454, 0x5555, 0x9249 },
	/* 33 BEBOP */    { 38,  8, 2, CCF_EXPR,                  0, 2, 6,173, 0x0101, 0x4901, 0x1010, 0x5210, 0x9249, 0x4925, 0xAAAA, 0x5555 },
	/* 34 BLUES */    { 48, 12, 6, CCF_EXPR,                  0, 2, 4,179, 0x0101, 0x0901, 0x1010, 0x1090, 0x4949, 0x9249, 0x4949, 0x5555 },
	/* 35 SLOWBLUES */{ 60, 16, 6, CCF_EXPR|CCF_MOD,          2, 2, 3,181, 0x0001, 0x0401, 0x1010, 0x1010, 0x0101, 0x1111, 0x1515, 0x5515 },
	/* 36 ROCKNROLL */{ 40, 12, 1, CCF_EXPR,                  0, 1, 5,191, 0x1111, 0x1511, 0x1010, 0x1410, 0x5555, 0x4949, 0x5555, 0xAAAA },
	/* 37 GOSPEL */   { 62, 24, 2, CCF_EXPR|CCF_CLAP|CCF_PEDAL,1, 0, 5,193, 0x0401, 0x4501, 0x1010, 0x1210, 0x4A92, 0x5555, 0xDB6D, 0x5555 },
	/* 38 SOUL */     { 48, 20, 2, CCF_EXPR|CCF_CLAP,         1, 0, 4,197, 0x0501, 0x2409, 0x1010, 0x1090, 0x4444, 0x4A92, 0x5555, 0xB6B6 },
	/* 39 RNB */      { 58, 26, 2, CCF_EXPR|CCF_MOD|CCF_CLAP, 2, 3, 4,199, 0x0401, 0x4481, 0x1010, 0x1010, 0x0101, 0x4841, 0x1515, 0x5252 },
	/* 40 NEOSOUL */  { 56, 28, 2, CCF_EXPR|CCF_MOD|CCF_PEDAL,1, 2, 4,211, 0x0401, 0x2481, 0x1010, 0x1090, 0x4849, 0x9249, 0x4A4A, 0x5555 },
	/* 41 COUNTRY */  { 42, 10, 1, CCF_EXPR,                  2, 1, 4,223, 0x0101, 0x1111, 0x5555, 0x5454, 0x4444, 0x5555, 0x5555, 0x5155 },
	/* 42 PUNK */     { 28,  6, 0, 0,                         0, 1, 6,227, 0x5555, 0x5555, 0x1010, 0x1010, 0x5555, 0x5555, 0x5555, 0x5555 },
	/* 43 GRUNGE */   { 44, 14, 1, 0,                         0, 1, 3,229, 0x0401, 0x0501, 0x0100, 0x1010, 0x0501, 0x1511, 0x1515, 0x5555 },
	/* 44 SKA */      { 36, 12, 0, 0,                         0, 5, 5,233, 0x0101, 0x0501, 0x1010, 0x1010, 0x4444, 0x4444, 0x4444, 0x5555 },
	/* 45 KAYOKYOKU */{ 58, 22, 2, CCF_EXPR|CCF_MOD|CCF_CLAP, 1, 0, 4,239, 0x0401, 0x2409, 0x1010, 0x1010, 0x4A92, 0x5555, 0xDB6D, 0x5555 },
	/* 46 ANISON */   { 44, 24, 1, CCF_EXPR|CCF_CLAP,         1, 0, 6,241, 0x0409, 0x4A41, 0x1010, 0x5210, 0x9249, 0xDB6D, 0xFFFF, 0xDB6D },
	/* 47 CHIPTUNE */ { 32, 16, 0, 0,                         5, 3, 6,251, 0x0409, 0x4841, 0x1010, 0x1010, 0x5555, 0xDB6D, 0xFFFF, 0xEEEE },
	/* 48 TRAP */     { 58, 18, 4, CCF_EXPR|CCF_CLAP,         3, 3, 3,  3, 0x4001, 0x4401, 0x0100, 0x0100, 0x0101, 0x0401, 0x4445, 0x1111 },
	/* 49 DRILL */    { 54, 16, 4, CCF_EXPR|CCF_CLAP,         3, 3, 4,  7, 0x4001, 0x4441, 0x1040, 0x1040, 0x0101, 0x2001, 0x4041, 0x9191 },
	/* 50 AFROBEAT */ { 48, 14, 2, CCF_EXPR,                  1, 5, 5, 13, 0x0441, 0x2449, 0x4010, 0x4210, 0x9249, 0x4A92, 0xDB6D, 0x5555 },
	/* 51 SWING3 */   { 50, 12, 2, CCF_EXPR,                  4, 2, 4, 19, 0x0001, 0x0001, 0x0110, 0x0110, 0x0110, 0x0111, 0x0111, 0x0111 },
	/* 52 TANGO */    { 46, 12, 2, CCF_EXPR,                  0, 5, 4, 21, 0x4141, 0x4941, 0x4040, 0x4040, 0x4949, 0x4941, 0x4949, 0x5555 },
	/* 53 FLAMENCO */ { 52, 14, 1, CCF_EXPR,                  2, 5, 5, 25, 0x0101, 0x0901, 0x4448, 0x4448, 0x4949, 0x9249, 0xDB6D, 0x5555 },
	/* 54 SURF */     { 60, 26, 1, CCF_EXPR,                  0, 1, 5, 33, 0x0041, 0x0441, 0x1010, 0x1010, 0x5555, 0x5555, 0x5555, 0xAAAA },
	/* 55 LOFI */     { 64, 30, 2, CCF_EXPR|CCF_MOD|CCF_PEDAL,4, 2, 3, 39, 0x0401, 0x2401, 0x1010, 0x1010, 0x0401, 0x4401, 0x1515, 0x4444 },
	/* 56 DRUMSOLO */ { 40,  6, 0, 0,                         0, 1, 2, 45, 0x1555, 0x5555, 0x5555, 0xAAAA, 0x5555, 0x1555, 0x5555, 0x5555 },
	/* 57 EDM */      { 64, 38, 5, CCF_EXPR|CCF_MOD|CCF_CLAP, 3, 3, 5, 51, 0x1111, 0x1111, 0x1010, 0x5010, 0x4444, 0x5454, 0xFFFF, 0xDB6D },
	/* 58 DUBSTEP */  { 58, 32, 4, CCF_EXPR|CCF_CLAP,         3, 3, 4, 57, 0x4001, 0x4401, 0x0100, 0x0100, 0x0101, 0x4001, 0x4444, 0xFFFF },
	/* 59 SYNTHWAVE */{ 62, 46, 5, CCF_EXPR|CCF_MOD|CCF_CLAP, 3, 3, 4, 63, 0x1111, 0x1111, 0x1010, 0x1010, 0x5555, 0x4444, 0xFFFF, 0xAAAA }
};
static_assert(sizeof(kStyleExtra) / sizeof(kStyleExtra[0]) == SC_AS_COUNT, "kStyleExtra");

struct ArrGene {
	uint8_t dens;       /* 0:少なめ 1:標準 2:厚め 3:最大 */
	uint8_t sync;       /* シンコペ度 */
	uint8_t dockBoost;  /* コードとメロディのドッキング度 */
	uint8_t arpStyle;   /* アルペジオ変形 */
	uint8_t drumFlavor; /* ドラム性格 */
	uint8_t bendScale;  /* ベンド・表現の深さ */
	int8_t  octBias;    /* 音域バイアス (-12..+12) */
	uint8_t phase;      /* 小節位相差 */
};

/* アレンジA〜P（16通り）それぞれに明確な音楽的性格を与える。 */
static const ArrGene kArrGene[SC_ARRANGE_COUNT] = {
	/* A */ { 1, 1, 1, 0, 0, 1,  0, 0 },  /* 標準バランス */
	/* B */ { 1, 2, 1, 1, 1, 1,  0, 1 },  /* タイト・シンコペ */
	/* C */ { 0, 0, 1, 2, 2, 1,  0, 2 },  /* 引き算・親密 */
	/* D */ { 3, 2, 2, 3, 3, 2,  0, 3 },  /* 厚め・フルドライブ */
	/* E */ { 2, 1, 3, 1, 4, 2,  0, 4 },  /* コード主導・ギター/ピアノ歌い */
	/* F */ { 2, 3, 2, 2, 5, 1,  5, 5 },  /* 裏拍・アクセント強調 */
	/* G */ { 1, 1, 1, 0, 6, 2, -5, 6 },  /* ハーフタイム・余白重視 */
	/* H */ { 3, 2, 3, 3, 7, 2,  0, 7 },  /* 推進力・刻み重視 */
	/* I */ { 2, 2, 2, 4, 1, 1,  7, 1 },  /* 分散カスケード（SSW風） */
	/* J */ { 2, 2, 2, 2, 3, 2, -7, 3 },  /* ベース強調・うねり */
	/* K */ { 2, 3, 3, 1, 5, 2,  0, 5 },  /* スタカート・ブラス/キメ */
	/* L */ { 2, 0, 2, 4, 2, 3,  0, 7 },  /* レガート・表情膨らみ */
	/* M */ { 2, 2, 3, 4, 0, 2, 12, 2 },  /* ブライト・高域きらめき */
	/* N */ { 2, 1, 2, 1, 6, 2,-12, 4 },  /* ダーク・低域重厚 */
	/* O */ { 2, 3, 3, 2, 4, 2,  0, 6 },  /* 掛け合い・コール＆レスポンス */
	/* P */ { 3, 3, 3, 4, 7, 3,  5, 0 },  /* クライマックス・祝祭ポリリズム */
};

struct LineSong {
	LineCast cast;
	int span;
	int melSpan;
	int pat;
	int fig;     /* 0歌 1跳ね 2スウィング 3裏 4押し 5静 6刻み */
	int cap;
	int impact;
	int impact2;
	int rootDeg[64];
	int melDeg;
	int melKind;
	int styleId;
	int soloAcc; /* 通常パートが1chだけのときはベース＋和音を1パートへ統合 */
	int twoAcc;  /* 通常パートが2chのときはコード側へ分散・対旋律を混ぜる */
	MelSlot mel[256][12];
	uint8_t melCount[256];
	int prevMel, prevBass, prevArp, prevHarm, prevCnt, prevPad, prevCh, prevCol;
};

static LineSong s_song;
static uint32_t s_wroteMel;
static uint32_t s_wroteDrum;
static int s_barNotes;
static int s_putForce;
static int s_nextRootPc = -1;
static int s_secIdx = 0;  /* イントロ/ブレイク内の現在小節番号 (0..) */
static int s_secLen = 1;  /* イントロ/ブレイクの総小節数 */
static int s_endRem = 0;  /* エンディングの残り小節数 (0 = 最終小節) */

static int ScaleOff(int scale, int deg)
{
	static const int8_t sc[8][7] = {
		{ 0, 2, 4, 5, 7, 9, 11 },
		{ 0, 2, 3, 5, 7, 8, 10 },
		{ 0, 2, 3, 5, 7, 9, 10 },
		{ 0, 2, 4, 5, 7, 9, 10 },
		{ 0, 2, 4, 7, 9, 0, 0 },
		{ 0, 3, 5, 6, 7, 10, 0 },
		{ 0, 1, 3, 5, 7, 8, 10 },
		{ 0, 2, 3, 5, 7, 8, 11 }
	};
	int sca = scale & 7;
	int n = 7;
	if (sca == 4) n = 5;
	else if (sca == 5) n = 6;
	int d = deg;
	int oct = 0;
	while (d < 0) { d += n; oct--; }
	while (d >= n) { d -= n; oct++; }
	return (int)sc[sca][d] + oct * 12;
}

static int FamFig(int fam)
{
	switch (fam) {
	case FAM_PAD: return 5;
	case FAM_FLOOR: case FAM_DISCO: case FAM_DNB: case FAM_TRAP: case FAM_HOP: return 1;
	case FAM_JAZZ: case FAM_SHUF: case FAM_BLUES: return 2;
	case FAM_REG: case FAM_SKA: case FAM_FUNK: return 3;
	case FAM_METAL: case FAM_8: case FAM_SAMBA: case FAM_AFRO: case FAM_LATIN: return 4;
	default: return 0;
	}
}

static int StyleCap(int fam, int pat, int form)
{
	int base = 54;
	switch (fam) {
	case FAM_PAD: case FAM_BALLAD: base = 36; break;
	case FAM_WALTZ: case FAM_MARCH: base = 42; break;
	case FAM_HOP: case FAM_REG: case FAM_SKA: case FAM_BOSSA: base = 48; break;
	case FAM_JAZZ: case FAM_BLUES: case FAM_SHUF: case FAM_TANGO: base = 56; break;
	case FAM_16: case FAM_8: case FAM_FUNK: case FAM_LATIN: base = 68; break;
	default: base = 78; break;
	}
	int dens = 100;
	const int gd = kArrGene[pat & 15].dens;
	if (gd == 0) dens = 74;
	else if (gd == 2) dens = 112;
	else if (gd == 3) dens = 128;
	int f = 55 + form * 45 / 100;
	int cap = base * dens / 100 * f / 100;
	if (cap < 14) cap = 14;
	if (cap > 96) cap = 96;
	return cap;
}

static int BoomAt(int span, int pat, int which)
{
	if (span < 8) return (which == 0 && span > 2) ? span - 2 : -1;
	if (which == 1) {
		if (span < 32 || kArrGene[pat & 15].dens == 0) return -1;
		int b = span / 4;
		if (b < 8) b = 8;
		return b;
	}
	int b = span * (4 + (pat & 3)) / 8;
	if (b < span / 3) b = span / 3;
	if (b > span - 3) b = span - 3;
	return b;
}

static int CellDeg(int mel, int var, int i)
{
	static const int8_t pop[4][8] = {
		{ 0, 1, 2, 4, 2, 1, 0, 2 }, { 4, 2, 1, 0, 2, 4, 5, 4 },
		{ 0, 2, 4, 5, 4, 2, 1, 0 }, { 2, 0, 1, 2, 4, 2, 0, 4 }
	};
	static const int8_t rock[4][8] = {
		{ 0, 0, 2, 4, 2, 0, 4, 2 }, { 4, 4, 2, 0, 2, 4, 7, 4 },
		{ 0, 2, 4, 7, 4, 2, 0, 0 }, { 2, 4, 2, 0, -2, 0, 2, 4 }
	};
	static const int8_t metal[4][8] = {
		{ 0, 1, 2, 0, 4, 5, 4, 2 }, { 4, 2, 1, 0, 4, 2, 1, 0 },
		{ 0, 4, 5, 4, 2, 1, 0, -1 }, { 0, 0, 4, 5, 7, 5, 4, 2 }
	};
	static const int8_t jazz[4][8] = {
		{ 2, 3, 4, 6, 4, 3, 2, 1 }, { 6, 4, 2, 1, 3, 4, 6, 4 },
		{ 1, 2, 4, 6, 4, 2, 0, 2 }, { 4, 6, 5, 4, 2, 1, 3, 2 }
	};
	static const int8_t blues[4][8] = {
		{ 0, 2, 3, 4, 2, 0, 4, 2 }, { 4, 3, 2, 0, 2, 3, 4, 0 },
		{ 2, 3, 4, 5, 4, 2, 0, 2 }, { 0, 4, 3, 2, 0, -1, 0, 2 }
	};
	static const int8_t funk[4][8] = {
		{ 0, 0, 4, 6, 4, 2, 0, 4 }, { 2, 4, 6, 4, 2, 0, 2, 4 },
		{ 4, 2, 0, 4, 6, 4, 2, 0 }, { 0, 2, 4, 0, 6, 4, 2, 0 }
	};
	static const int8_t latin[4][8] = {
		{ 0, 2, 4, 2, 0, 4, 2, 0 }, { 4, 2, 0, 2, 4, 5, 4, 2 },
		{ 2, 4, 5, 4, 2, 0, 2, 4 }, { 0, 4, 2, 5, 4, 2, 0, 4 }
	};
	static const int8_t fan[4][8] = {
		{ 0, 2, 4, 7, 4, 2, 0, 4 }, { 4, 7, 4, 2, 0, 4, 7, 4 },
		{ 0, 4, 7, 4, 0, 2, 4, 0 }, { 7, 4, 2, 0, 4, 7, 4, 0 }
	};
	static const int8_t chip[4][8] = {
		{ 0, 2, 4, 7, 4, 2, 0, 4 }, { 0, 4, 7, 12, 7, 4, 0, 4 },
		{ 4, 2, 0, 2, 4, 7, 4, 0 }, { 0, 7, 4, 7, 12, 7, 4, 0 }
	};
	static const int8_t surf[4][8] = {
		{ 4, 3, 2, 1, 0, 2, 4, 3 }, { 7, 4, 2, 0, 2, 4, 7, 4 },
		{ 4, 4, 2, 0, 4, 2, 0, 0 }, { 2, 4, 7, 4, 2, 0, 2, 4 }
	};
	static const int8_t hop[4][8] = {
		{ 0, 0, 4, 0, 0, 5, 4, 0 }, { 4, 0, 0, 4, 7, 4, 0, 0 },
		{ 0, 5, 4, 0, 0, 4, 0, 5 }, { 7, 4, 0, 4, 0, 0, 4, 0 }
	};
	static const int8_t rave[4][8] = {
		{ 0, 0, 4, 0, 7, 4, 0, 0 }, { 4, 4, 0, 4, 7, 4, 0, 4 },
		{ 0, 7, 4, 0, 0, 7, 4, 0 }, { 7, 0, 4, 7, 4, 0, 7, 4 }
	};
	const int8_t* row = pop[var & 3];
	switch (mel) {
	case MEL_ROCK: case MEL_PUNK: case MEL_BOOGIE: row = rock[var & 3]; break;
	case MEL_METAL: row = metal[var & 3]; break;
	case MEL_JAZZ: row = jazz[var & 3]; break;
	case MEL_BLUES: row = blues[var & 3]; break;
	case MEL_FUNK: row = funk[var & 3]; break;
	case MEL_LATIN: row = latin[var & 3]; break;
	case MEL_FAN: row = fan[var & 3]; break;
	case MEL_CHIP: row = chip[var & 3]; break;
	case MEL_SURF: row = surf[var & 3]; break;
	case MEL_HOP: case MEL_OFF: row = hop[var & 3]; break;
	case MEL_RAVE: case MEL_WAVE: row = rave[var & 3]; break;
	default: break;
	}
	return row[i & 7];
}

static int ArchDeg(int bar, int span, int mel)
{
	if (span < 1) span = 1;
	int t = bar * 8 / span;
	if (t > 8) t = 8;
	if (mel == MEL_TANGO) return 5 - t * 5 / 8;
	if (mel == MEL_ENKA) {
		static const int a[9] = { 0, 2, 4, 2, 4, 2, 0, 2, 0 };
		return a[t];
	}
	if (mel == MEL_WALTZ) {
		static const int a[9] = { 0, 2, 4, 2, 5, 4, 2, 1, 0 };
		return a[t];
	}
	static const int a[9] = { 0, 1, 2, 4, 5, 4, 2, 1, 0 };
	return a[t];
}

static void FillRhythm(int mel, int cap, int* n, int* at, int* du, int* leg)
{
	auto set = [&](int i, int a, int d, int l) { at[i] = a; du[i] = d; leg[i] = l; };
	int c = cap;
	if (c < 1) c = 1;
	if (c > 6) c = 6;
	switch (mel) {
	case MEL_METAL:
		set(0, 0, 4, 0); set(1, 4, 3, 0); set(2, 8, 6, 1);
		set(3, 24, 4, 0); set(4, 28, 3, 0); set(5, 32, 10, 1);
		*n = 6; break;
	case MEL_PUNK:
		set(0, 0, 5, 1); set(1, 6, 5, 0); set(2, 12, 5, 1);
		set(3, 18, 5, 0); set(4, 24, 5, 1); set(5, 30, 5, 0);
		*n = 6; break;
	case MEL_SURF: case MEL_CHIP:
		for (int k = 0; k < 6; k++) set(k, k * 6, 5, k == 5);
		*n = 6; break;
	case MEL_BOOGIE:
		set(0, 0, 5, 1); set(1, 6, 5, 0); set(2, 12, 5, 1); set(3, 18, 5, 0);
		*n = 4; break;
	case MEL_WAVE:
		set(0, 0, 6, 1); set(1, 6, 6, 1); set(2, 12, 6, 1); set(3, 18, 6, 1);
		*n = 4; break;
	case MEL_ROCK:
		if (c <= 2) { set(0, 0, 28, 1); set(1, 32, 12, 1); *n = 2; }
		else { set(0, 0, 10, 1); set(1, 12, 8, 0); set(2, 24, 16, 1); *n = 3; }
		break;
	case MEL_BALLAD: case MEL_ENKA:
		if (c >= 4) {
			set(0, 0, 12, 1); set(1, 14, 10, 1); set(2, 26, 10, 1); set(3, 38, 8, 0);
			*n = 4;
		} else { set(0, 0, 34, 1); set(1, 36, 10, 1); *n = 2; }
		break;
	case MEL_WALTZ:
		set(0, 0, 28, 1); set(1, 32, 14, 1); *n = 2; break;
	case MEL_JAZZ:
		if (c >= 6) {
			for (int k = 0; k < 6; k++) set(k, k * 8, 7, (k & 1) == 0);
			*n = 6;
		} else {
			set(0, 0, 8, 1); set(1, 10, 6, 0); set(2, 18, 8, 1);
			set(3, 28, 6, 0); set(4, 36, 8, 1);
			*n = 5;
		}
		break;
	case MEL_BLUES:
		set(0, 0, 10, 1); set(1, 12, 8, 0); set(2, 24, 8, 1); set(3, 36, 8, 1);
		*n = 4; break;
	case MEL_OFF:
		set(0, 6, 10, 1); set(1, 30, 10, 1); *n = 2; break;
	case MEL_FUNK:
		set(0, 0, 4, 0); set(1, 6, 4, 1); set(2, 18, 4, 0); set(3, 24, 6, 1); set(4, 36, 4, 0);
		*n = 5; break;
	case MEL_RAVE:
		set(0, 0, 16, 1); set(1, 24, 12, 1); set(2, 38, 8, 0); *n = 3; break;
	case MEL_HOP:
		set(0, 6, 8, 1); set(1, 20, 6, 0); set(2, 36, 8, 1); *n = 3; break;
	case MEL_LATIN:
		set(0, 0, 8, 1); set(1, 10, 6, 0); set(2, 18, 10, 1); set(3, 36, 8, 1); *n = 4; break;
	case MEL_FAN:
		set(0, 0, 8, 1); set(1, 8, 8, 1); set(2, 16, 8, 1); set(3, 24, 18, 1); *n = 4; break;
	case MEL_TANGO:
		set(0, 0, 16, 1); set(1, 18, 8, 0); set(2, 28, 14, 1); *n = 3; break;
	default:
		set(0, 0, 14, 1); set(1, 16, 10, 1); set(2, 30, 8, 0); set(3, 40, 6, 0);
		*n = 4; break;
	}
	if (*n > c) *n = c;
}

/* 構成音だけ。0=根音 2=3度 4=5度 7=1オクターブ。 */
static int ChordDegAt(int pos, int k, int pat, int span)
{
	static const int t[8] = { 0, 4, 2, 7, 4, 2, 0, 4 };
	int i = pos * 3 + k + (pat & 7);
	if (span > 1 && pos >= span / 2) i += 3;
	if (i < 0) i = 0;
	return t[i & 7];
}

static int FamGroup(int fam)
{
	switch (fam) {
	case FAM_16: case FAM_FUNK: case FAM_DNB: return 0;
	case FAM_8: case FAM_METAL: return 1;
	case FAM_REG: case FAM_SKA: return 2;
	case FAM_FLOOR: case FAM_DISCO: return 3;
	case FAM_HOP: case FAM_TRAP: return 4;
	case FAM_BALLAD: case FAM_PAD: case FAM_MARCH: return 5;
	case FAM_JAZZ: case FAM_SHUF: case FAM_BLUES: return 6;
	case FAM_BOSSA: return 7;
	case FAM_SAMBA: case FAM_LATIN: case FAM_AFRO: return 8;
	case FAM_TANGO: return 9;
	default: return 1;
	}
}

/* スタイル固有の4/8/16/32/64小節進化マスク。kStyleExtra＋kArrGene＋kDefでスタイル×アレンジを分離。 */
static uint16_t FamMask(int fam, int pat, int pos, int role)
{
	const int style = (s_song.styleId >= 0 && s_song.styleId < SC_AS_COUNT) ? s_song.styleId : 0;
	const StyleExtra& sx = kStyleExtra[style];
	const ArrGene& ag = kArrGene[pat & 15];
	const int g = FamGroup(fam);
	const int bar = pos < 0 ? 0 : pos;
	const int sec = (bar >> 2) & 3;
	const int sub = bar & 3;
	const int ph = (bar + (int)ag.phase + (int)(sx.salt & 7)) & 7;

	if (fam == FAM_METAL && ((pat & 15) >= 8) && role == 1) {
		static const int gallop[8] = { 0, 2, 3, 4, 6, 7, 8, 10 };
		uint16_t mg = 0;
		int shift = (bar & 3) * 3 + (pat & 3);
		for (int i = 0; i < 8; i++) {
			int s = (gallop[i] + shift) & 15;
			mg = (uint16_t)(mg | (uint16_t)(1u << s));
		}
		return mg;
	}
	if (g == 2 && role == 0)
		return (bar & 1) ? (uint16_t)0x0101 : (uint16_t)0x0100;

	uint16_t base = 1;
	if (role == 1) {
		uint16_t cDef = Bits(kDef[style].chord);
		base = (sub == 0) ? sx.chordA : (sub == 1) ? sx.chordB
			: (sub == 2) ? (cDef ? cDef : sx.chordA)
			: (uint16_t)(sx.chordA ^ (sx.chordB & 0x4242u));
		if (sec >= 2) base = (uint16_t)(base | (sx.chordB & 0x0808u));
	} else if (role == 2) {
		base = (sub & 1) ? sx.arpB : sx.arpA;
		if (sec == 1 || sec == 3) base = (uint16_t)(base | ((base >> 1) & 0x5555u));
	} else if (role == 0) {
		base = (sub & 1) ? sx.kickB : sx.kickA;
		if (g != 2 && !(base & 1u)) base = (uint16_t)(base | 1u);
	} else if (role == 3) {
		uint16_t h = (sub & 1) ? sx.chordB : sx.snareA;
		base = (uint16_t)((h << 2) | (h >> 14));
	} else {
		uint16_t c = (sub & 1) ? sx.snareB : sx.chordA;
		base = (uint16_t)((c << 1) | (c >> 15));
	}

	/* 2・8・16小節ごとの同型ループを防ぐ小節別位相＋彫刻マスク */
	int rot = ((ph + sec * 3) & 3) * 2;
	if (g == 1 || g == 2 || g == 5) {
		/* 8ビート/レゲエ/バラードは8分格子を保ち、奇数16分へ崩さない */
		base = (uint16_t)(base & 0x5555u);
		if (g == 2) base = (uint16_t)(base & 0x4444u);
		if (rot & 1) rot++;
	} else if (ag.sync >= 2 && (sec == 1 || sec == 3) && role != 0) {
		base = (uint16_t)((base << (rot & 3)) | (base >> (16 - (rot & 3))));
	}
	if (g == 0 && (role == 0 || role == 1 || role == 2) && (base & 0xAAAAu) == 0)
		base = (uint16_t)(base | ((bar & 1) ? 0x0808u : 0x2080u));
	if (ag.dens == 0 && role != 0) {
		uint16_t thin = 0; int k = 0;
		for (int s = 0; s < 16; s++) if (base & (1u << s)) { if ((k++ & 1) == 0) thin |= (uint16_t)(1u << s); }
		if (thin) base = thin;
		if (role == 1 && g != 2) {
			static const uint16_t kThinVar[4] = { 0x0001u, 0x0101u, 0x0011u, 0x1001u };
			uint16_t tv = kThinVar[(bar + sec) & 3];
			if (g == 5) {
				static const uint16_t kBal[4] = { 0x0001u, 0x0101u, 0x0010u, 0x1001u };
				tv = kBal[(bar + sec) & 3];
			}
			base = (uint16_t)(base | tv);
		}
		if (g == 0 && role == 2 && (base & 0xAAAAu) == 0)
			base = (uint16_t)(base | 0x0808u);
	} else if (ag.dens >= 2 && role == 1 && g != 2 && g != 5) {
		static const uint16_t kBarShape[8] = { 0x5555u, 0x5155u, 0x5515u, 0x1555u, 0x4555u, 0x5545u, 0x5455u, 0x5551u };
		base = (uint16_t)(base | (sx.chordA & ((bar & 1) ? 0x4444u : 0x1111u)));
		if (g == 1) base = (uint16_t)(base & kBarShape[(bar + pat + sec * 3) & 7]);
	}
	if (role == 1 && g == 2) {
		static const uint16_t kSkank[4] = { 0x4444u, 0x4404u, 0x4044u, 0x0444u };
		static const uint16_t kSkankThin[4] = { 0x4040u, 0x0440u, 0x4004u, 0x0404u };
		base = (ag.dens == 0) ? kSkankThin[(bar + pat + sec) & 3] : kSkank[(bar + pat + sec) & 3];
	} else if (role == 1 && g != 5) {
		/* 全小節で2小節同型にならないよう、小節ごとに1〜2拍の抜け・置き換えを入れる */
		static const uint16_t kCarve[8] = { 0xFFFFu, 0xEFFFu, 0xFBFFu, 0xBFFFu, 0xFFEFu, 0xFFBFu, 0xFEFFu, 0xFFFEu };
		uint16_t carved = (uint16_t)(base & kCarve[(bar + sec * 2 + (pat & 3)) & 7]);
		if (carved) base = carved;
		if ((bar & 2) && g == 1) {
			uint16_t toggle = (uint16_t)(1u << (((bar * 2 + pat * 4) & 6) + 4));
			base = (uint16_t)(base ^ (toggle & 0x5554u));
		}
	}
	if (g == 1 || g == 2 || g == 5) {
		base = (uint16_t)(base & (g == 2 ? 0x4444u : 0x5555u));
	}
	if (role == 3) {
		uint16_t h = 0; int k = 0;
		for (int s = 0; s < 16; s++) if (base & (1u << s)) { if (k++ & 1) h |= (uint16_t)(1u << s); }
		base = h ? h : (uint16_t)(base & 0x1111u);
	} else if (role == 4) {
		int s = (g == 2) ? (2 + ((bar + pat) & 3) * 4) : (4 + ((bar + pat) & 3) * 2);
		if (g == 1 || g == 5) s &= ~1;
		if (s > 15) s = 15;
		base = (uint16_t)(1u << s);
		if (ag.dens >= 2 && g != 5) {
			int s2 = (s + 6) & 15;
			if (g == 1) s2 &= ~1;
			base = (uint16_t)(base | (1u << s2));
		}
	}
	if (!base) base = (g == 2) ? (uint16_t)0x0404u : (uint16_t)0x0101u;
	return base;
}

static int HitTimes(int pos, int span, int fig, int pat, int waltz, int* at, int max, int role = 1)
{
	(void)fig;
	if (!at || max < 1) return 0;
	if (span < 1) span = 1;
	if (pos < 0) pos = 0;
	if (waltz) {
		int skip = (pos + pat) % 3;
		int nout = 0;
		for (int b = 0; b < 3 && nout < max; b++) {
			if (pos < span - 1 && b == skip && (pos & 1)) continue;
			at[nout++] = b * 16;
		}
		if (!nout) { at[0] = 0; return 1; }
		return nout;
	}
	if (pos >= span - 1) {
		at[0] = 0;
		return 1;
	}
	uint16_t m = FamMask(StyleFam(s_song.styleId), pat, pos, role);
	int n = 0;
	for (int s = 0; s < 16 && n < max; s++) {
		if (m & (uint16_t)(1u << s)) at[n++] = s * 3;
	}
	if (!n) { at[0] = 0; return 1; }
	return n;
}

/* 16〜256小節対応・64小節展開＋form 0〜100連続制御＋全60スタイル/16アレンジ対応メロディ生成。
   deg は楽曲のキー基準のダイアトニック度数（sing 側で強拍のみ現在コードトーンへ自然に吸着）。 */
static void BuildMelody(int form, int melSpan)
{
	if (melSpan < 16) melSpan = 16;
	if (melSpan > 256) melSpan = 256;
	s_song.melSpan = melSpan;
	const int style = (s_song.styleId >= 0 && s_song.styleId < SC_AS_COUNT) ? s_song.styleId : 0;
	const StyleDef& sd = kDef[style];
	const StyleExtra& sx = kStyleExtra[style];
	const ArrGene& ag = kArrGene[s_song.pat & 15];
	const ArrProfile& ap = kArrProf[s_song.pat & 15];
	const int waltz = (s_song.cast.drum == 3) || (sd.div == 12 && (style == SC_AS_WALTZ || style == SC_AS_SWING3));
	const int pat = s_song.pat & 15;
	const int melFam = StyleFam(style);
	const int melKind = s_song.melKind;
	for (int bar = 0; bar < 256; bar++) s_song.melCount[bar] = 0;
	if (form <= 0) return;

	/* 16種の歌いやすい動機セル（順次進行＋跳躍＋解決のバランス） */
	static const int8_t kMotif[16][8] = {
		{ 0, 1, 2, 4, 3, 2, 1, 0 },  /* 王道J-POP上昇アーチ */
		{ 2, 4, 5, 4, 2, 3, 2, 0 },  /* 叙情サビ型 */
		{ 4, 4, 2, 4, 5, 4, 2, 1 },  /* フック反復型 */
		{ 0, 2, 4, 5, 4, 2, 0, 2 },  /* 爽やかペンタ */
		{ 4, 3, 2, 1, 0, 2, 3, 2 },  /* 下降しっとり */
		{ 0, 0, 2, 3, 4, 2, 1, 0 },  /* リズミック刻み */
		{ 2, 3, 4, 6, 5, 4, 2, 4 },  /* ジャズ/都会的テンション */
		{ 5, 4, 2, 4, 7, 5, 4, 2 },  /* 高揚サビクライマックス */
		{ 0, 4, 3, 2, 4, 2, 1, 0 },  /* 5度跳躍フック */
		{ 1, 2, 4, 2, 1, 0, -1, 0 }, /* 切ないマイナー寄り */
		{ 4, 5, 7, 5, 4, 2, 4, 2 },  /* 伸びやかハイトーン */
		{ 0, 2, 1, 2, 4, 5, 4, 2 },  /* スキップ軽快 */
		{ 3, 2, 0, 2, 4, 3, 2, 1 },  /* 穏やかAメロ */
		{ 2, 0, 2, 4, 5, 7, 5, 4 },  /* ビルドアップ上昇 */
		{ 0, 2, 4, 2, 0, -1, 0, 2 }, /* フォーク/バラード語り */
		{ 7, 6, 5, 4, 5, 4, 2, 0 }   /* 劇的エンディング下降 */
	};

	/* セクションごとの音域リフト（A1低→A2→B1上昇→C1サビ高→A3→Dブリッジ→Eソロ→C2最高潮） */
	static const int8_t kSecLift[8] = { 0, 1, 2, 4, 1, 2, 3, 5 };

	/* form 1..100 から連続的に密度・装飾・シンコペーションを決定 */
	int fClamped = form < 1 ? 1 : (form > 100 ? 100 : form);
	/* 0..6 の段階＋細かい端数判定で form=10,20,30,40,50,60,70,80,90,100 がすべて滑らかに変化 */
	int prevDeg = 0;

	for (int bar = 0; bar < melSpan; bar++) {
		const BarPlan& pl = PlanOfBar(bar);
		const int sec = pl.sec & 7;
		const int pBar = pl.phraseBar & 3;
		const int phraseEnd = (pBar == 3);
		const int songEnd = (bar >= melSpan - 2);
		const uint32_t h = PlanHash(style, pat, bar, 913);

		int at[12], du[12];
		uint8_t bend[12];
		uint8_t passN[12];
		int nd = 0;
		for (int z = 0; z < 12; z++) { bend[z] = 0; passN[z] = 0; }

		/* form 1..100 で滑らかに小節あたり音数を決定（最低1〜最大10） */
		int styleBase = (int)sx.melDense; /* 2..7 */
		if (styleBase < 2) styleBase = 2;
		if (styleBase > 7) styleBase = 7;
		/* form による連続スケール: form=15 -> ~1.5音, form=50 -> styleBase, form=100 -> styleBase+3 */
		int scaled100 = styleBase * (25 + fClamped * 115 / 100); /* 26%..140% of styleBase */
		int targetN = (scaled100 + ((int)(h % 100))) / 100;
		/* セクションとアレンジで変化をつける */
		if (sec == 3 || sec == 7) targetN += 1; /* サビは歌い込む */
		else if (sec == 6) targetN += 1;        /* ソロ/展開 */
		else if (sec == 0 && pBar == 0) targetN -= 1;
		else if (sec == 5 && pl.breakFeel) targetN -= 1; /* ブリッジ抜き */

		if (ap.compMode == 2 || ap.compMode == 6 || ag.dens == 0) targetN -= 1;
		else if (ap.compMode == 5 || ap.compMode == 7 || ap.compMode == 10 || ag.dens >= 3) targetN += 1;

		/* コール＆レスポンス系アレンジは偶数/奇数小節で呼吸を作る */
		if (ap.callResp >= 1 && (bar & 1) == 1 && !phraseEnd && sec != 3 && sec != 7 && targetN > 2)
			targetN -= (ap.callResp == 2 ? 2 : 1);

		/* 低form時の間引き（息継ぎ小節を作る） */
		if (fClamped <= 25 && (pBar == 3) && !songEnd && ((bar / 4 + pat) & 1)) {
			targetN = 1;
		}
		if (phraseEnd && targetN > 3 && fClamped < 85) targetN = (fClamped < 40) ? 1 : 2;
		if (waltz && targetN > 4) targetN = 4;
		if (targetN < 1) targetN = 1;
		int maxCap = (fClamped < 20) ? 2 : (fClamped < 35) ? 3 : (fClamped < 55) ? 5 : (fClamped < 75) ? 7 : (fClamped < 90) ? 9 : 11;
		if (targetN > maxCap) targetN = maxCap;
		if (targetN > 11) targetN = 11;

		if (bar >= melSpan - 1) {
			at[0] = 0; du[0] = 44; nd = 1;
		} else if (waltz) {
			static const int8_t rw[4][4] = {
				{ 0, 16, 32, -1 }, { 0, 24, 32, -1 }, { 0, 8, 16, 32 }, { 0, 16, 40, -1 }
			};
			const int8_t* rr = rw[(sec + pBar + pat + (int)(h & 3)) & 3];
			for (int k = 0; k < 4 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			if (nd < 1) { at[0] = 0; nd = 1; }
			for (int k = 0; k < nd; k++) {
				int nxt = (k + 1 < nd) ? (at[k + 1] - at[k] - 1) : (46 - at[k]);
				if (nxt < 4) nxt = 4;
				if (nxt > 36) nxt = 36;
				du[k] = nxt;
			}
		} else {
			/* スタイル族×セクション×アレンジごとの自然な拍割テーブル（48単位小節） */
			int row = (sec * 3 + (pBar & 1) + (int)ap.compMode + (int)(pl.varBits & 3) + (int)sx.salt) & 7;
			if (melFam == FAM_16 || melFam == FAM_FUNK || style == SC_AS_CITYPOP || style == SC_AS_ANISON || style == SC_AS_DISCO || style == SC_AS_FUSION) {
				static const int8_t r16[8][10] = {
					{ 0, 6, 9, 12, 18, 24, 30, 36, 42, -1 },
					{ 3, 6, 12, 18, 21, 24, 30, 36, 42, -1 },
					{ 0, 3, 6, 12, 15, 18, 24, 30, 33, 36 },
					{ 6, 12, 15, 18, 24, 27, 30, 36, 42, -1 },
					{ 0, 6, 12, 18, 24, 27, 33, 36, 42, 45 },
					{ 0, 9, 12, 18, 24, 30, 33, 39, 42, -1 },
					{ 6, 9, 12, 18, 24, 30, 36, 39, 42, -1 },
					{ 0, 6, 12, 15, 21, 24, 30, 36, 42, 45 }
				};
				const int8_t* rr = r16[row];
				for (int k = 0; k < 10 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			} else if (melFam == FAM_JAZZ || melFam == FAM_BLUES || melFam == FAM_SHUF || sd.div == 12) {
				static const int8_t rj[8][8] = {
					{ 0, 8, 12, 20, 24, 32, 36, 44 },
					{ 4, 12, 16, 24, 32, 40, -1, -1 },
					{ 0, 12, 20, 24, 28, 36, 40, -1 },
					{ 8, 12, 20, 24, 32, 36, 44, -1 },
					{ 0, 4, 12, 20, 24, 36, 44, -1 },
					{ 4, 12, 20, 28, 36, 40, -1, -1 },
					{ 0, 8, 16, 24, 32, 40, 44, -1 },
					{ 12, 20, 24, 32, 36, 44, -1, -1 }
				};
				const int8_t* rr = rj[row];
				for (int k = 0; k < 8 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			} else if (melFam == FAM_BALLAD || melFam == FAM_PAD || style == SC_AS_ENKA || style == SC_AS_SLOWROCK || style == SC_AS_AMBIENT) {
				static const int8_t rb[8][7] = {
					{ 0, 18, 24, 36, 42, -1, -1 },
					{ 0, 12, 24, 30, 36, 42, -1 },
					{ 6, 18, 24, 36, 42, -1, -1 },
					{ 0, 12, 18, 30, 36, -1, -1 },
					{ 0, 6, 18, 24, 36, 42, -1 },
					{ 12, 24, 30, 36, 42, -1, -1 },
					{ 0, 12, 24, 36, 42, 45, -1 },
					{ 0, 18, 30, 36, 42, -1, -1 }
				};
				const int8_t* rr = rb[row];
				for (int k = 0; k < 7 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			} else if (melFam == FAM_HOP || melFam == FAM_TRAP || melFam == FAM_REG || melFam == FAM_SKA || melFam == FAM_BOSSA || melFam == FAM_LATIN) {
				static const int8_t rh[8][8] = {
					{ 6, 12, 18, 24, 30, 36, 42, -1 },
					{ 0, 9, 18, 24, 33, 36, 42, -1 },
					{ 6, 18, 24, 30, 36, 42, -1, -1 },
					{ 0, 12, 18, 27, 30, 36, 42, -1 },
					{ 3, 12, 18, 24, 33, 42, -1, -1 },
					{ 0, 6, 15, 24, 30, 39, 42, -1 },
					{ 6, 12, 21, 24, 30, 36, 42, -1 },
					{ 0, 9, 12, 24, 30, 36, 45, -1 }
				};
				const int8_t* rr = rh[row];
				for (int k = 0; k < 8 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			} else {
				/* 8ビート/ロック/メタル/テクノ/チップチューン等 */
				static const int8_t r8[8][10] = {
					{ 0, 6, 12, 18, 24, 30, 36, 42, -1, -1 },
					{ 0, 12, 18, 24, 30, 36, 42, 45, -1, -1 },
					{ 6, 12, 18, 24, 30, 36, 42, -1, -1, -1 },
					{ 0, 6, 12, 24, 30, 36, 39, 42, -1, -1 },
					{ 0, 6, 9, 12, 18, 24, 30, 36, 42, -1 },
					{ 0, 12, 15, 18, 24, 30, 36, 42, -1, -1 },
					{ 6, 12, 24, 30, 36, 42, 45, -1, -1, -1 },
					{ 0, 6, 12, 18, 21, 24, 30, 36, 42, 45 }
				};
				const int8_t* rr = r8[row];
				for (int k = 0; k < 10 && rr[k] >= 0 && nd < targetN; k++) at[nd++] = rr[k];
			}
			if (nd < 1) { at[0] = 0; nd = 1; }
			/* フレーズ終端はロングトーンで歌い切る（後ろの音を残しつつ余韻を作る） */
			if (phraseEnd && nd > 2 && fClamped < 85) nd = 2;
			for (int k = 0; k < nd; k++) {
				int nxt = (k + 1 < nd) ? (at[k + 1] - at[k] - 1) : (46 - at[k]);
				if (phraseEnd && k == nd - 1) nxt = 46 - at[k];
				if (nxt < 3) nxt = 3;
				if (nxt > 38) nxt = 38;
				du[k] = nxt;
			}
		}

		/* アレンジ別のシンコペーション・前ノリ（8ビート/バラード/ワルツは崩さない） */
		const int straightStyle = (melFam == FAM_8 || melFam == FAM_BALLAD || melFam == FAM_PAD || melFam == FAM_MARCH || waltz);
		if ((ap.compMode == 4 || ap.compMode == 7 || ag.sync >= 2) && !straightStyle && nd >= 2) {
			for (int k = nd - 1; k >= 1; k--) {
				int sh = (ap.compMode == 7 || (pat & 1)) ? 3 : 6;
				int lim = (k + 1 < nd) ? (at[k + 1] - 2) : 44;
				if (at[k] > 0 && at[k] + sh <= lim && ((k + bar + pat) & 1))
					at[k] += sh;
			}
		}

		/* form>=65 から段階的に経過音（パッシングトーン）・装飾16分を追加 */
		if (fClamped >= 65 && !waltz && nd >= 2 && nd < 11 && !songEnd) {
			int maxAdd = (fClamped >= 90) ? 2 : 1;
			int added = 0;
			int minGap = (fClamped >= 85) ? 6 : 9;
			for (int k = 0; k + 1 < nd && added < maxAdd && nd < 11; k++) {
				int gap = at[k + 1] - at[k];
				if (gap < minGap) continue;
				if (((k + bar + pat + sec) & 1) != 0 && fClamped < 90) continue;
				int step = (gap >= 12 && straightStyle) ? 6 : 3;
				for (int j = nd; j > k + 1; j--) {
					at[j] = at[j - 1];
					du[j] = du[j - 1];
					passN[j] = passN[j - 1];
				}
				at[k + 1] = at[k] + step;
				du[k + 1] = step;
				passN[k + 1] = 1;
				if (du[k] > step) du[k] = step;
				nd++;
				added++;
				k++;
			}
		}

		/* 64小節のセクション別モチーフ選択（A系・B系・Cサビ・Dブリッジ・Eソロで明確に別動機） */
		int secLift = kSecLift[sec];
		if (ap.octShift > 0) secLift += 2;
		else if (ap.octShift < 0) secLift -= 1;

		/* A1(0)/A2(1)/A3(4)は親戚モチーフ、C1(3)/C2(7)はサビ専用モチーフ、D(5)/E(6)は対照モチーフ */
		int baseMotif = (style + pat * 3 + (int)sx.salt) & 15;
		int motifRow;
		if (sec == 0 || sec == 1 || sec == 4) motifRow = (baseMotif + (sec == 4 ? 1 : 0)) & 15;
		else if (sec == 2) motifRow = (baseMotif + 5) & 15;
		else if (sec == 3 || sec == 7) motifRow = (baseMotif + 7) & 15;
		else if (sec == 5) motifRow = (baseMotif + 11) & 15;
		else motifRow = (baseMotif + 13) & 15;

		/* 2小節対（問い→答え）と4小節フレーズ（起承転結）で歌う */
		int phraseLift = (pBar == 1) ? 1 : (pBar == 2) ? 2 : 0;
		if (sec == 3 || sec == 7) phraseLift = (pBar == 2) ? 3 : (pBar == 1 ? 1 : 0);
		else if (sec == 5) phraseLift = (pBar == 0) ? 2 : (pBar == 2 ? -1 : 0);

		for (int k = 0; k < nd; k++) {
			int idx = (pBar == 2) ? (7 - (k & 7)) : ((k + (pBar & 1) * 2) & 7);
			int mDeg = kMotif[motifRow][idx];
			int deg = mDeg + secLift + phraseLift;
			if (passN[k]) {
				/* 経過音は直前音から1度だけ滑らかに動く */
				deg = prevDeg + ((deg >= prevDeg) ? 1 : -1);
			} else if (k == 0 && pBar == 0 && (sec == 0 || sec == 3 || sec == 7)) {
				deg = secLift + ((sec == 3 || sec == 7) ? 2 : 0);
			} else if (k > 0) {
				/* 歌唱可能な跳躍幅（最大5度）に収める */
				if (deg - prevDeg > 5) deg = prevDeg + 3;
				else if (prevDeg - deg > 5) deg = prevDeg - 3;
			}
			/* フレーズ末尾・曲末の自然な終止感 */
			if (songEnd && k == nd - 1) deg = 0;
			else if (phraseEnd && k == nd - 1) {
				if (sec == 2) deg = secLift + 4;      /* プレコーラス末→ドミナント感でサビへ */
				else if (sec == 3 || sec == 7) deg = secLift; /* サビ末→トニック/高音解決 */
				else deg = (bar & 4) ? 0 : 2;
			}
			prevDeg = deg;

			if (sx.bendMode == 3 && (k == 1 || (phraseEnd && k == 0))) bend[k] = 1;
			else if ((sx.bendMode == 1 || sx.bendMode == 2 || sx.bendMode == 6) && du[k] >= 10 && (k == 0 || k == nd - 1)) bend[k] = 1;
			else if (fClamped >= 80 && sx.bendMode && du[k] >= 12 && k == 0 && (sec == 3 || sec == 6 || sec == 7)) bend[k] = 1;

			if (deg > 24) deg = 24;
			if (deg < -12) deg = -12;
			s_song.mel[bar][k].deg = (int8_t)deg;
			s_song.mel[bar][k].at = (uint8_t)at[k];
			s_song.mel[bar][k].du = (uint8_t)((du[k] < 1) ? 1 : du[k]);
			s_song.mel[bar][k].leg = (uint8_t)((k == 0 || du[k] >= 10) ? 1 : 0);
			s_song.mel[bar][k].bend = bend[k];
		}
		s_song.melCount[bar] = (uint8_t)nd;
	}
}

static void LineBegin(int style, int pat, int var, int form, int melBars, int soloAcc, int twoAcc)
{
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	CastOf(style, pat, var, &s_song.cast);
	s_song.pat = pat;
	s_song.styleId = style;
	s_song.melKind = kVoice[style].mel;
	s_song.fig = kVoice[style].fig;
	s_song.soloAcc = soloAcc;
	s_song.twoAcc = twoAcc;
	int span = s_loopBars;
	if (span < 8) span = 8;
	if (span > 64) span = 64;
	s_song.span = span;
	s_song.cap = StyleCap(StyleFam(style), pat, form);
	s_song.impact = BoomAt(span, pat, 0);
	s_song.impact2 = BoomAt(span, pat, 1);
	s_song.melDeg = 0;
	s_wroteMel = 0;
	int hold = s_song.cast.hold;
	if (hold < 1) hold = 2;
	if (hold > 8) hold = 8;
	int deg = 0;
	int i = 0;
	while (i < span) {
		int target = 0;
		if (s_song.cast.form12) {
			int p = i % 12;
			int bdeg = 0;
			if (p >= 4 && p < 6) bdeg = 3;
			else if (p == 8) bdeg = 4;
			else if (p == 9) bdeg = 3;
			s_song.rootDeg[i++] = bdeg;
			continue;
		}
		if (i >= span - 2) target = (i >= span - 1) ? 0 : 4;
		else if (i >= span * 3 / 4) target = 0;
		else if (i >= span / 2) target = 3;
		else if (i >= span / 4) target = (s_song.cast.step < 0) ? 0 : 1;
		else target = 0;
		int chunk = (i >= span - 2) ? 1 : hold;
		for (int h = 0; h < chunk && i < span; h++)
			s_song.rootDeg[i++] = deg;
		if (deg < target) deg++;
		else if (deg > target) deg--;
		if (deg > 5) deg = 4;
		if (deg < 0) deg = 0;
	}
	if (span > 0) s_song.rootDeg[span - 1] = 0;
	s_song.prevMel = s_song.prevBass = s_song.prevArp = -1;
	s_song.prevHarm = s_song.prevCnt = s_song.prevPad = s_song.prevCh = s_song.prevCol = -1;
	int ms = (melBars >= 16) ? melBars : span;
	if (ms < 16) ms = 16;
	if (ms > 256) ms = 256;
	BuildMelody(form, ms);
}


static int LinePitch(int tonic, int scale, int deg, int prev, int lo, int hi)
{
	int m = 60 + ((tonic % 12 + 12) % 12) + ScaleOff(scale, deg);
	return NearMidi(m, prev, lo, hi);
}

static void PutFrac(Ctx* c, int ch, const BarInfo* b, int st, int du, int midi, int vel, int gate)
{
	if (!c || !b || ch < 0 || midi < 0) return;
	/* 土台（ベース・和音・パッド）はスライダーが上がっても残す。上限は足し役だけ。 */
	if (!s_putForce && s_barNotes >= s_song.cap) return;
	if (st < 0) st = 0;
	if (st > 46) st = 46;
	if (du < 1) du = 1;
	if (st + du > 48) du = 48 - st;
	uint32_t tk = b->t0 + (uint32_t)((uint64_t)b->len * (uint32_t)st / 48u);
	int dur = (int)((uint64_t)b->len * (uint32_t)du / 48u);
	if (dur < 2) dur = 2;
	s_barNotes++;
	if (ch < 32) s_wroteMel |= (1u << ch);
	PlaceNote(c, ch, tk, midi, dur, vel, gate);
}

static int Tone(int tonic, int deg, int prev, int lo, int hi)
{
	return LinePitch(tonic, s_song.cast.scale, deg, prev, lo, hi);
}

static int NearTriad(int deg, int root)
{
	int best = root;
	int bd = 99;
	for (int oct = -1; oct <= 2; oct++) {
		static const int add[3] = { 0, 2, 4 };
		for (int k = 0; k < 3; k++) {
			int c = root + add[k] + oct * 7;
			int d = deg - c;
			if (d < 0) d = -d;
			if (d < bd) { bd = d; best = c; }
		}
	}
	return best;
}

static int PosOf(int bar)
{
	int span = s_song.span > 0 ? s_song.span : 16;
	int p = bar % span;
	if (p < 0) p = 0;
	return p;
}

static int RootAt(int bar, int section)
{
	if (section == SEC_END) return 0;
	return s_song.rootDeg[PosOf(bar)];
}

static int IsBoom(int pos)
{
	return (pos == s_song.impact || pos == s_song.impact2) ? 1 : 0;
}

static int ShapeVel(int baseVel, const BarPlan& pl, const ArrProfile& ap, int st);

static void PaintDrummerLayer(Ctx* c, int ch, int di, int style, int pat, int bar, const BarInfo* b,
	int div, const uint16_t* drm, int introSoft, int drumDur)
{
	if (!c || ch < 0 || di <= 0 || !b || !drm || div < 1) return;
	if (b->section == SEC_BREAK || b->section == SEC_END) return;
	if (c->isFm) return;
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	const StyleExtra& sx = kStyleExtra[style];
	const ArrProfile& ap = kArrProf[pat & 15];
	const BarPlan& pl = PlanOfBar((b->section == SEC_BODY) ? bar : 0);
	const int fam = StyleFam(style);
	const int quiet = StyleQuiet(style);

	/* 2本目以降のドラムch: セクション展開に連動するパーカッション層。
	   A1前半や抜きセクションは抑え、A2/B1/サビ/ソロで段階的に加わる。 */
	if (b->section == SEC_BODY) {
		if (di == 1 && pl.sec == 0 && (bar & 1) == 0 && quiet) return;
		if (di == 2 && pl.sec < 2 && pl.turn == 0) return;
		if (di >= 3 && pl.sec != 3 && pl.sec != 6 && pl.sec != 7 && pl.turn < 2) return;
		if (pl.breakFeel == 1 && pl.turn == 0 && di >= 2) return;
	}

	/* パーカッションの音色セット（スタイル固有）。キック/スネアを二重打ちして喧嘩させない。 */
	static const uint8_t kPerc[6][6] = {
		/* 0: Pop/Funk/CityPop */ { 54, 70, 63, 64, 69, 56 }, /* Tamb, Maracas, HiConga, LowConga, Cabasa, Cowbell */
		/* 1: Rock/Metal */       { 54, 56, 53, 52, 55, 65 }, /* Tamb, Cowbell, RideBell, China, Splash, HiTimbale */
		/* 2: Jazz/Blues/Lo-fi */ { 69, 53, 37, 44, 70, 81 }, /* Cabasa, RideBell, Rim, PedHH, Maracas, Triangle */
		/* 3: Electro/Trap */     { 70, 69, 37, 75, 56, 76 }, /* Maracas, Cabasa, Rim, Claves, Cowbell, HiWood */
		/* 4: Orch/Ballad/Waltz */{ 81, 54, 41, 49, 70, 80 }, /* Triangle, Tamb, LowFloor, Crash, Maracas, MuteTri */
		/* 5: Latin/Reggae/Afro */{ 75, 62, 63, 64, 60, 67 }  /* Claves, MuteConga, HiConga, LowConga, HiBongo, HiAgogo */
	};
	const uint8_t* pool = kPerc[sx.percSet % 6];
	const int waltz = (s_song.cast.drum == 3) || (div == 12 && (style == SC_AS_WALTZ || style == SC_AS_SWING3));
	const int nSlot = div < 16 ? div : 16;

	/* ターンアラウンドではパーカッションも駆け上がりフィルで呼応 */
	const int percFill = (b->section == SEC_BODY && pl.turn >= 1 && !quiet);

	for (int slot = 0; slot < nSlot && !c->overflow; slot++) {
		uint32_t tick = b->t0 + (uint32_t)((uint64_t)slot * (uint64_t)b->len / (uint64_t)div);
		if (s_song.cast.swing && (slot & 1)) {
			int sd = b->len / div;
			tick += (uint32_t)(sd / 3);
		}
		if (tick >= b->t0 + (uint32_t)b->len) continue;
		const uint16_t bit = (uint16_t)(1u << slot);
		const int mainKick = (drm[DV_KICK] & bit) != 0;
		const int mainSnare = ((drm[DV_SNARE] | drm[DV_CLAP]) & bit) != 0;
		int pitch = -1;
		int vel = 70;

		if (waltz) {
			if (di == 1 && (slot == 4 || slot == 8)) { pitch = pool[(pl.sec >= 3) ? 0 : 1]; vel = 68; }
			else if (di == 2 && (slot == 0 || slot == 8)) { pitch = pool[5]; vel = 60; }
			else if (di >= 3 && (slot == 2 || slot == 6 || slot == 10)) { pitch = pool[(di + slot) % 6]; vel = 54; }
		} else if (di == 1) {
			if (percFill && slot >= (nSlot * 3 / 4)) {
				/* 末尾1拍のコンガ/ティンバレス/タンバリン回し */
				int k = slot - (nSlot * 3 / 4);
				pitch = pool[2 + ((k + pl.sec) % 4)];
				vel = 72 + k * 6;
			} else if (fam == FAM_8 || fam == FAM_METAL) {
				if ((slot % 4) == 2 && !mainKick && !mainSnare) { pitch = pool[0]; vel = 74; }
				else if ((pl.sec == 3 || pl.sec == 7) && (slot % 4) == 0 && !mainKick) { pitch = pool[1]; vel = 76; }
				else if (pl.phraseBar == 3 && slot == (nSlot * 3 / 4) && !mainSnare) { pitch = pool[2]; vel = 80; }
			} else if (fam == FAM_LATIN || fam == FAM_SAMBA || fam == FAM_BOSSA || fam == FAM_AFRO || fam == FAM_REG || fam == FAM_SKA) {
				/* クラーベ/コンガ系は本格的なシンコペーション格子 */
				static const uint16_t kClave[4] = { 0x4949u, 0x2492u, 0x5249u, 0x14A4u };
				uint16_t cm = kClave[(pl.sec + (pl.varBits & 3) + pat) & 3];
				if (cm & bit) {
					pitch = pool[(slot & 2) ? 2 : 0];
					vel = (slot % 4 == 0) ? 80 : 68;
				} else if (!mainKick && !mainSnare && ((slot + pl.sec) & 3) == 3) {
					pitch = pool[3]; vel = 62;
				}
			} else {
				/* ポップス/ファンク/ジャズ/エレクトロ: セクションでシェイカー→コンガ→タンバリンへ展開 */
				if ((pl.sec == 3 || pl.sec == 7) && (slot % 4) == 2) {
					pitch = pool[0]; vel = 76;
				} else if (!mainKick && !mainSnare) {
					int sel = ((slot + bar + pl.sec * 2 + pat + (pl.varBits & 3)) % 6);
					if (sel == 0) { pitch = pool[2]; vel = 72; }
					else if (sel == 2 && (pl.sec >= 2 || ap.compMode >= 4)) { pitch = pool[3]; vel = 66; }
					else if (sel == 4 && (slot % 2 == 1) && pl.sec >= 1) { pitch = pool[1]; vel = 58; }
				} else if (sx.percSet == 4 && (slot % 8) == 4) {
					pitch = pool[0]; vel = 68;
				}
			}
		} else if (di == 2) {
			/* 第2パーカッション層: オフビート・ベル/シェイカー/トライアングルで立体感 */
			int step = (pl.sec >= 3) ? 2 : 4;
			int off = ((di + pat + pl.sec) & 1) ? 2 : 0;
			if (((slot + off) % step) == 0 && !mainKick) {
				pitch = pool[(slot % 4 == 0) ? 4 : 5];
				vel = 58 + ((pl.sec == 3 || pl.sec == 7) ? 10 : 0);
			}
		} else {
			/* 第3層以降: チャンネルごとに別位相・別楽器 */
			int mod = 3 + ((di + pat) & 3);
			int phase = (di * 5 + pat * 3 + bar + pl.sec) % mod;
			if (((slot + phase) % mod) == 0 && !mainKick && !mainSnare) {
				pitch = pool[(di * 2 + slot + pl.sec) % 6];
				vel = 52 + ((slot % 4) == 0 ? 10 : 0) - (di > 4 ? 6 : 0);
			}
		}
		if (pitch < 0) continue;
		vel = ShapeVel(vel, pl, ap, slot * 48 / div);
		if (introSoft) vel -= 14;
		if (vel < 34) vel = 34;
		if (vel > 106) vel = 106;
		PlaceDrumNote(c, ch, tick, pitch, vel, drumDur);
	}
}

static void PaintPulse(int style, int pat, int bar, int section, int div, int ending, uint16_t* drm)
{
	if (!drm || div < 1) return;
	for (int v = 0; v < DV_COUNT; v++) drm[v] = 0;
	if (div > 16) div = 16;
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	const StyleDef& sd = kDef[style];
	const StyleExtra& sx = kStyleExtra[style];
	const ArrGene& ag = kArrGene[pat & 15];
	const ArrProfile& ap = kArrProf[pat & 15];
	const BarPlan& pl = PlanOfBar((section == SEC_BODY) ? bar : 0);
	const uint16_t slotMask = (div >= 16) ? 0xFFFFu : (uint16_t)((1u << div) - 1u);
	const int q = (div >= 4) ? (div / 4) : 1;
	const int famNow = StyleFam(style);
	const int quiet = StyleQuiet(style);
	const int waltz = (s_song.cast.drum == 3) || (div == 12 && (style == SC_AS_WALTZ || style == SC_AS_SWING3));
	const int noOdd16 = (div >= 16) && (famNow == FAM_8 || famNow == FAM_BALLAD || famNow == FAM_PAD
		|| famNow == FAM_REG || famNow == FAM_SKA || famNow == FAM_MARCH || style == SC_AS_SLOWROCK || style == SC_AS_ENKA);

	auto bit = [&](int voice, int slot) {
		if (slot < 0 || voice < 0 || voice >= DV_COUNT) return;
		if (slot >= div) slot %= div;
		if (slot < 16)
			drm[voice] = (uint16_t)(drm[voice] | (uint16_t)(1u << slot));
	};
	auto clr = [&](int voice, int slot) {
		if (slot < 0 || slot >= 16 || voice < 0 || voice >= DV_COUNT) return;
		drm[voice] = (uint16_t)(drm[voice] & ~(uint16_t)(1u << slot));
	};
	auto g16 = [&](int voice, int s16) {
		int s = s16 * div / 16;
		if (s >= div) s = div - 1;
		bit(voice, s);
	};

	if (section == SEC_BREAK) {
		/* ブレイク: 1拍目の抜けアクセント＋刻みハット/リム＋最終小節末尾の駆け上がりフィル */
		if (s_secIdx == 0) {
			bit(DV_KICK, 0);
			if (!quiet) bit(DV_CRASH, 0);
		} else {
			bit(DV_KICK, 0);
			bit(DV_RIM, q * 2);
		}
		for (int s = 0; s < div; s += (q > 1 ? q : 1)) bit(DV_HAT, s);
		if (s_secIdx + 1 >= s_secLen) {
			int fStart = div - q;
			if (fStart < 1) fStart = div / 2;
			if (quiet) {
				bit(DV_RIM, div - 1);
				bit(DV_OHAT, fStart);
			} else {
				for (int s = fStart; s < div; s++) bit((s & 1) ? DV_TOM : DV_SNARE, s);
				bit(DV_OHAT, div - 1);
			}
		}
		return;
	}
	if (ending) {
		bit(DV_KICK, 0);
		bit(DV_CRASH, 0);
		return;
	}
	if (section == SEC_INTRO) {
		/* イントロ: 小節ごとに段階的に楽器を増やし、最終イントロ小節で本体へ繋ぐフィルを入れる */
		drm[DV_HAT] = (uint16_t)(Bits(sd.hat) & slotMask);
		drm[DV_RIDE] = (uint16_t)(Bits(sd.ride) & slotMask);
		if (!drm[DV_HAT] && !drm[DV_RIDE]) {
			for (int s = 0; s < div; s += q) bit(DV_HAT, s);
		}
		if (s_secIdx == 0 && s_secLen >= 3) {
			/* 1小節目: ハット/ライド＋リムで軽やかに導入 */
			if (div >= 16) drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x5555u);
			bit(DV_RIM, q);
			bit(DV_RIM, q * 3);
		} else if (s_secIdx == 1 && s_secLen >= 4) {
			/* 2小節目: キックが加わる */
			drm[DV_KICK] = (uint16_t)(Bits(sd.kick) & slotMask);
			if (!drm[DV_KICK]) bit(DV_KICK, 0);
			bit(DV_RIM, q * 3);
		} else {
			/* 後半: フルビートへ展開 */
			drm[DV_KICK] = (uint16_t)(Bits(sd.kick) & slotMask);
			drm[DV_SNARE] = (uint16_t)(Bits(sd.snare) & slotMask);
			if (!drm[DV_KICK]) bit(DV_KICK, 0);
			if (!drm[DV_SNARE] && !waltz) { bit(DV_SNARE, q); bit(DV_SNARE, q * 3); }
		}
		if (s_secIdx + 1 >= s_secLen) {
			/* 最終イントロ小節: 本体1小節目のクラッシュへ繋ぐフィルイン */
			int fStart = (s_secLen >= 2) ? (div - q) : (div / 2);
			if (quiet) {
				bit(DV_RIM, div - 1);
				bit(DV_OHAT, fStart);
			} else {
				for (int s = fStart; s < div; s++) {
					clr(DV_HAT, s); clr(DV_RIDE, s);
					bit((s & 1) ? DV_TOM : DV_SNARE, s);
				}
				bit(DV_KICK, div - 1);
			}
		}
		return;
	}
	if (section == SEC_END) {
		/* マルチ小節エンディングの終止前小節: 徐々に音数を抜きつつ最後の一打へ導く */
		drm[DV_KICK] = (uint16_t)(Bits(sd.kick) & slotMask);
		drm[DV_SNARE] = (uint16_t)(Bits(sd.snare) & slotMask);
		drm[DV_RIDE] = (uint16_t)((Bits(sd.ride) | (Bits(sd.hat) & (div >= 16 ? 0x5555u : slotMask))) & slotMask);
		if (!drm[DV_KICK]) bit(DV_KICK, 0);
		if (s_endRem == 1 && !quiet) {
			int fStart = div - q;
			for (int s = fStart; s < div; s++) bit((s == div - 1) ? DV_TOM : DV_SNARE, s);
			bit(DV_OHAT, fStart);
		}
		return;
	}

	/* 1) 全60スタイル固有の kDef[style] ドラム譜面をベースとして読み込む */
	drm[DV_KICK]  = (uint16_t)(Bits(sd.kick)  & slotMask);
	drm[DV_SNARE] = (uint16_t)(Bits(sd.snare) & slotMask);
	drm[DV_CLAP]  = (uint16_t)(Bits(sd.clap)  & slotMask);
	drm[DV_HAT]   = (uint16_t)(Bits(sd.hat)   & slotMask);
	drm[DV_OHAT]  = (uint16_t)(Bits(sd.ohat)  & slotMask);
	drm[DV_RIDE]  = (uint16_t)(Bits(sd.ride)  & slotMask);
	drm[DV_CRASH] = (uint16_t)(Bits(sd.crash) & slotMask);
	drm[DV_TOM]   = (uint16_t)(Bits(sd.tom)   & slotMask);
	drm[DV_RIM]   = (uint16_t)(Bits(sd.rim)   & slotMask);

	/* もし kDef にハット/ライドが両方無い場合だけスタイルに応じた基準ビートを補う */
	const int rideNative = (drm[DV_RIDE] != 0) || famNow == FAM_JAZZ || famNow == FAM_SHUF
		|| famNow == FAM_BLUES || style == SC_AS_SWING3 || style == SC_AS_PIANO;
	if (!drm[DV_HAT] && !drm[DV_RIDE] && !drm[DV_OHAT]) {
		int step = waltz ? (div / 3) : ((quiet || div < 16) ? q : (q > 1 ? q / 2 : 1));
		if (step < 1) step = 1;
		for (int s = 0; s < div; s += step) bit(rideNative ? DV_RIDE : DV_HAT, s);
	}

	const int sec = pl.sec & 7;
	const int pBar = pl.phraseBar & 3;
	const int vb = pl.varBits & 15;
	const int pat16 = pat & 15;
	const uint32_t dh = PlanHash(style, pat16, bar, 541);

	/* 2) 64小節の楽曲セクション（A1->A2->B1->C1->A3->D->E->C2）に沿ったダイナミック展開 */
	if (sec == 0) {
		/* A1: 抑えめにスタート。16分ハットは前半8分へ整理、クラッシュ控えめ */
		if (div >= 16 && drm[DV_HAT] == 0xFFFFu && !sd.power && ap.compMode != 5)
			drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x5555u);
		if (quiet && pBar < 2 && drm[DV_SNARE]) {
			/* バラード/ボサ/ジャズA1序盤はリムショット寄りにして歌を引き立てる */
			drm[DV_RIM] = (uint16_t)(drm[DV_RIM] | drm[DV_SNARE]);
			drm[DV_SNARE] = 0;
		}
		if (bar > 0) drm[DV_CRASH] = 0;
	} else if (sec == 1) {
		/* A2: 本来のスタイルグルーヴ全開＋奇数小節で kickB を少し足す */
		if (waltz) {
			if ((bar & 1) && !quiet) bit(DV_SNARE, (div * 2) / 3);
		} else if ((bar & 1) && div >= 16) {
			uint16_t ek = sx.kickB;
			if (noOdd16) ek = (uint16_t)(ek & 0x5555u);
			drm[DV_KICK] = (uint16_t)(drm[DV_KICK] | (ek & slotMask));
		}
		if (bar % 8 == 0 && !quiet) bit(DV_CRASH, 0);
	} else if (sec == 2) {
		/* B1 プレコーラス: 緊張感を高める（オープンハット先取り・後半推進） */
		if (waltz) {
			bit(DV_RIM, div / 3);
			if (pBar >= 2) bit(DV_OHAT, div - 1);
		} else if (!quiet) {
			if (div >= 16) g16(DV_OHAT, (vb & 1) ? 14 : 10);
			else bit(DV_OHAT, div - 2);
		}
		if (pBar >= 2 && !quiet && !waltz) {
			/* プレコーラス後半でキック/スネアの推進感 */
			if (div >= 16) g16(DV_KICK, noOdd16 ? 12 : 14);
			else bit(DV_KICK, div - 1);
		}
		if (bar % 8 == 0) bit(DV_CRASH, 0);
	} else if (sec == 3 || sec == 7) {
		/* C1 サビ / C2 最終サビ: 視界を開く（ライド/オープンHH開放・クラッシュ・クラップ重ね） */
		if (waltz) {
			bit(DV_RIDE, 0); bit(DV_RIDE, div / 3); bit(DV_RIDE, (div * 2) / 3);
			if (sec == 7) bit(DV_SNARE, (div * 2) / 3);
		} else if (!rideNative) {
			/* サビでハイハットをライドへ移行（またはオープンHHアクセント追加） */
			if (sec == 7 || (pat16 & 1) || ap.compMode == 9 || sd.power) {
				uint16_t srcH = drm[DV_HAT] ? drm[DV_HAT] : (uint16_t)(0x5555u & slotMask);
				if (div >= 16) srcH = (uint16_t)(srcH & 0x5555u); /* ライドは8分中心で濁らせない */
				drm[DV_RIDE] = (uint16_t)(drm[DV_RIDE] | srcH);
				if (sec == 7 && !quiet) drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & (div >= 16 ? 0x1111u : 0x0924u));
				else drm[DV_HAT] = 0;
			} else if (!quiet) {
				if (div >= 16) { g16(DV_OHAT, 6); g16(DV_OHAT, 14); }
				else { bit(DV_OHAT, q * 2 - 1); bit(DV_OHAT, div - 1); }
			}
		}
		if ((sx.ccFlags & CCF_CLAP) || (sec == 7 && !quiet && !rideNative && famNow != FAM_METAL)) {
			if (drm[DV_SNARE]) drm[DV_CLAP] = (uint16_t)(drm[DV_CLAP] | drm[DV_SNARE]);
		}
		if ((bar % 8) == 0 || (sec == 7 && (bar % 4) == 0)) bit(DV_CRASH, 0);
		/* サビのキック強化 */
		if (div >= 16 && !waltz && !quiet) {
			uint16_t ek = (bar & 1) ? sx.kickB : sx.kickA;
			if (noOdd16) ek = (uint16_t)(ek & 0x5555u);
			drm[DV_KICK] = (uint16_t)(drm[DV_KICK] | (ek & slotMask));
		}
	} else if (sec == 4) {
		/* A3 2番Aメロ: 1番とは違うタイトなポケット（ゴーストリム/シンコペ変化） */
		if (drm[DV_RIDE] && !rideNative) drm[DV_RIDE] = 0;
		if (waltz) {
			drm[DV_SNARE] = 0;
			bit(DV_RIM, div / 3); bit(DV_RIM, (div * 2) / 3);
		} else if (div >= 16) {
			if ((vb & 1) && !noOdd16) g16(DV_RIM, 7);
			else g16(DV_RIM, 10);
			if ((bar & 1) && !quiet) g16(DV_OHAT, 14);
		} else if (div == 12) {
			bit(DV_RIM, (vb & 1) ? 7 : 10);
		}
		if (bar % 8 == 0 && !quiet) bit(DV_CRASH, 0);
	} else if (sec == 5) {
		/* D ブリッジ: ハーフタイムまたは抜きビートで明確なコントラスト */
		if (waltz) {
			drm[DV_SNARE] = 0;
			bit(DV_KICK, 0);
			bit(DV_RIDE, 0);
			bit(DV_RIM, (div * 2) / 3);
		} else if (pl.breakFeel == 1) {
			/* スネアを3拍目へ落としたハーフタイム感＋空間的なライド/ハット */
			drm[DV_SNARE] = 0;
			drm[DV_CLAP] = 0;
			bit(quiet ? DV_RIM : DV_SNARE, q * 2);
			drm[DV_KICK] = (uint16_t)(drm[DV_KICK] & (div >= 16 ? 0x0501u : 0x0141u));
			bit(DV_KICK, 0);
			if (div >= 16) drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x1111u);
			else drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x0249u);
			if (!drm[DV_RIDE]) bit(DV_RIDE, 0);
		} else {
			/* トム/リム中心の展開グルーヴ */
			bit(DV_TOM, q * 3);
			if (bar % 8 == 0) bit(DV_CRASH, 0);
		}
	} else if (sec == 6) {
		/* E ソロ/ビルド: 躍動的なシンバルワークとドライブ感 */
		if (waltz) {
			bit(DV_RIDE, 0); bit(DV_RIDE, div / 3); bit(DV_RIDE, (div * 2) / 3);
			if ((bar & 1)) bit(DV_TOM, div - 1);
		} else if (!quiet) {
			if (div >= 16) {
				g16(DV_OHAT, (bar & 1) ? 10 : 14);
				if (pBar >= 2) {
					uint16_t ek = (bar & 1) ? sx.kickA : sx.kickB;
					if (noOdd16) ek = (uint16_t)(ek & 0x5555u);
					drm[DV_KICK] = (uint16_t)(drm[DV_KICK] | (ek & slotMask));
				}
			} else {
				bit(DV_RIDE, 0); bit(DV_RIDE, q); bit(DV_RIDE, q * 2); bit(DV_RIDE, q * 3);
			}
		}
		if (bar % 8 == 0) bit(DV_CRASH, 0);
	}

	/* 3) アレンジA〜P（ArrProfile + ArrGene）のドラム・ポケット差別化 */
	if (!waltz) {
		if (ag.drumFlavor == 6 || ap.compMode == 2) {
			/* ハーフタイム寄りアレンジ: 奇数小節は3拍目スネアで重厚に */
			if ((bar & 1) && drm[DV_SNARE]) {
				drm[DV_SNARE] = 0;
				bit(DV_SNARE, q * 2);
			}
		} else if (ag.drumFlavor == 5 || ap.compMode == 4) {
			/* リム/ゴーストポケット重視アレンジ */
			if (div >= 16 && !noOdd16) {
				g16(DV_RIM, ((bar + pat16) & 1) ? 7 : 15);
			} else if (div == 12) {
				bit(DV_RIM, ((bar + pat16) & 1) ? 5 : 11);
			}
		} else if (ag.drumFlavor == 4 || ap.compMode == 9) {
			/* ライド主体アレンジ */
			if (drm[DV_HAT] && !drm[DV_RIDE]) {
				drm[DV_RIDE] = (uint16_t)(drm[DV_HAT] & (div >= 16 ? 0x5555u : slotMask));
				drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & (div >= 16 ? 0x1111u : 0x0249u));
			}
		} else if (ag.drumFlavor == 7 || ap.compMode == 5 || ap.compMode == 10) {
			/* ドライビング/高密度アレンジ */
			if (!quiet && div >= 16 && !noOdd16 && !rideNative) {
				drm[DV_HAT] = (uint16_t)(drm[DV_HAT] | 0x5555u);
				if (sec >= 2) g16(DV_OHAT, 14);
			}
		}
		if (ag.dens == 0 && div >= 16 && !sd.power) {
			/* 省音アレンジはハットを4分/8分へ整理 */
			drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x5555u);
		}
	}

	/* 4) 小節固有の微細なグルーヴ揺らぎ（64小節全体で単調ループさせない） */
	if (pl.turn == 0) {
		int mut = (int)(dh % 7);
		if (waltz) {
			if (mut == 1 && !quiet) bit(DV_KICK, div / 3 * 2);
			else if (mut == 2) bit(DV_RIM, (div / 3) + 1);
			else if (mut == 3 && !quiet) bit(DV_OHAT, div - 1);
			else if (mut == 4 && sec >= 2) bit(DV_SNARE, (div * 2) / 3);
		} else if (div >= 16) {
			if (mut == 0 && !quiet) {
				int ks = noOdd16 ? ((vb & 1) ? 6 : 10) : ((vb & 1) ? 11 : 14);
				g16(DV_KICK, ks);
			} else if (mut == 1 && !quiet && !rideNative) {
				int os = ((bar + pat16) & 1) ? 6 : 14;
				g16(DV_OHAT, os);
			} else if (mut == 2) {
				int rs = noOdd16 ? 10 : ((vb & 1) ? 7 : 9);
				if (!(drm[DV_SNARE] & (1u << rs))) g16(DV_RIM, rs);
			} else if (mut == 3 && !quiet) {
				uint16_t sn = (vb & 1) ? sx.snareB : sx.snareA;
				if (noOdd16) sn = (uint16_t)(sn & 0x5555u);
				drm[DV_SNARE] = (uint16_t)(drm[DV_SNARE] | (sn & 0x8000u ? 0 : (sn & 0xC000u)));
			} else if (mut == 4 && sec >= 2 && !quiet) {
				g16(DV_TOM, noOdd16 ? 14 : 15);
			} else if (mut == 5 && !quiet && famNow != FAM_REG) {
				int ks = noOdd16 ? 14 : 15;
				g16(DV_KICK, ks);
			}
		} else if (div == 12) {
			/* 12連（シャッフル/ジャズ/ブルース/スローロック等）の小節変化 */
			if (mut == 0 && !quiet) bit(DV_KICK, (vb & 1) ? 8 : 11);
			else if (mut == 1) bit(DV_RIM, (vb & 1) ? 5 : 10);
			else if (mut == 2 && !quiet) bit(DV_OHAT, (vb & 1) ? 5 : 11);
			else if (mut == 3 && sec >= 2 && !quiet) bit(DV_SNARE, 11);
			else if (mut == 4 && sec >= 3) bit(DV_TOM, 10);
		}
	}

	/* 5) 4小節・8小節・16小節ごとの多彩なフィルイン（8パターンの語彙・静スタイル配慮） */
	if (pl.turn == 1) {
		/* 4小節ミニフィル: 最後の1拍（12..15 または 9..11）を小粋に返す */
		int fKind = ((bar / 4) + sec + pat16 + (int)sx.salt) & 7;
		int fStart = div - q;
		if (fStart < 1) fStart = div / 2;
		if (quiet) {
			/* 静かなスタイルはソフトなピックアップだけ */
			bit(DV_RIM, div - 1);
			if (fKind & 1) bit(DV_OHAT, fStart);
			else bit(DV_TOM, div - (q > 1 ? 2 : 1));
		} else {
			for (int s = fStart; s < div; s++) { clr(DV_HAT, s); clr(DV_RIDE, s); }
			switch (fKind) {
			case 0: bit(DV_SNARE, fStart); bit(DV_SNARE, div - 1); bit(DV_TOM, (fStart + div) / 2); break;
			case 1: bit(DV_TOM, fStart); bit(DV_TOM, div - 1); bit(DV_KICK, div - 1); break;
			case 2: bit(DV_SNARE, fStart); bit(DV_OHAT, div - 1); bit(DV_KICK, (fStart + div) / 2); break;
			case 3: for (int s = fStart; s < div; s++) bit((s == div - 1) ? DV_TOM : DV_SNARE, s); break;
			case 4: bit(DV_KICK, fStart); bit(DV_SNARE, (fStart + div) / 2); bit(DV_TOM, div - 1); break;
			case 5: bit(DV_RIM, fStart); bit(DV_SNARE, div - 1); bit(DV_OHAT, (fStart + div) / 2); break;
			case 6: bit(DV_SNARE, fStart); bit(DV_TOM, fStart + 1); bit(DV_TOM, div - 1); break;
			default: bit(DV_KICK, fStart); bit(DV_OHAT, fStart); bit(DV_SNARE, div - 1); break;
			}
		}
	} else if (pl.turn == 2) {
		/* 8小節/16小節のセクション境界フィル: 後半2拍をダイナミックに展開 */
		int fKind = ((bar / 8) * 3 + sec + pat16 + (int)sx.salt) & 7;
		int fStart = div / 2;
		if (quiet) {
			for (int s = fStart; s < div; s++) clr(DV_HAT, s);
			bit(DV_SNARE, fStart);
			bit(DV_TOM, fStart + q);
			bit(DV_OHAT, div - 1);
			if (sec == 2 || sec == 6) bit(DV_CRASH, div - 1);
		} else {
			for (int s = fStart; s < div; s++) {
				clr(DV_HAT, s); clr(DV_RIDE, s); clr(DV_OHAT, s); clr(DV_SNARE, s); clr(DV_KICK, s);
			}
			switch (fKind) {
			case 0: /* 王道スネア→タム回し */
				bit(DV_SNARE, fStart); bit(DV_SNARE, fStart + 1);
				for (int s = fStart + q; s < div; s++) bit((s & 1) ? DV_TOM : DV_SNARE, s);
				break;
			case 1: /* シンコペーション・キック＆スネア掛け合い */
				bit(DV_SNARE, fStart); bit(DV_KICK, fStart + 1);
				bit(DV_TOM, fStart + q); bit(DV_KICK, div - 2); bit(DV_SNARE, div - 1);
				break;
			case 2: /* クレッシェンド・スネアビルド */
				for (int s = fStart; s < div; s += (noOdd16 ? 2 : 1)) bit(DV_SNARE, s);
				bit(DV_KICK, fStart); bit(DV_KICK, fStart + q);
				break;
			case 3: /* タム連打＋シンバル先食い */
				for (int s = fStart; s < div - 1; s += (noOdd16 ? 2 : 1)) bit(DV_TOM, s);
				bit(DV_CRASH, div - 1); bit(DV_KICK, div - 1);
				break;
			case 4: /* ブレイク風キメ（3拍目ストップ→4拍目フィル） */
				bit(DV_KICK, fStart); bit(DV_CRASH, fStart);
				bit(DV_SNARE, div - q); bit(DV_TOM, div - 1);
				break;
			case 5: /* ジャズ/ファンク風コンプフィル */
				bit(DV_SNARE, fStart + 1); bit(DV_TOM, fStart + q);
				bit(DV_RIM, div - 2); bit(DV_OHAT, div - 1); bit(DV_KICK, div - 1);
				break;
			case 6: /* ロック/メタル向けダブルキック＆タム */
				bit(DV_KICK, fStart); bit(DV_SNARE, fStart);
				bit(DV_TOM, fStart + q); bit(DV_KICK, div - 2); bit(DV_TOM, div - 1); bit(DV_KICK, div - 1);
				break;
			default: /* クラップ＆スネア＋オープンハット駆け上がり */
				bit(DV_SNARE, fStart); bit(DV_CLAP, fStart);
				bit(DV_TOM, fStart + q); bit(DV_SNARE, div - 2); bit(DV_OHAT, div - 1);
				break;
			}
		}
	} else if (pl.turn == 3) {
		/* 曲末の終止小節 */
		for (int v = 0; v < DV_COUNT; v++) drm[v] = 0;
		bit(DV_KICK, 0);
		bit(DV_CRASH, 0);
		return;
	}

	/* 6) 安全装置（キック・バックビートの最低保証） */
	if (section == SEC_BODY && !drm[DV_KICK] && famNow != FAM_REG) bit(DV_KICK, 0);
	if (section == SEC_BODY && !waltz && famNow != FAM_BALLAD && famNow != FAM_PAD && famNow != FAM_REG
		&& pl.breakFeel == 0 && pl.turn == 0 && !drm[DV_SNARE] && !drm[DV_CLAP] && !drm[DV_RIM]) {
		bit(DV_SNARE, q);
		bit(DV_SNARE, q * 3);
	}
}

static void PlaceExpr(Ctx* c, int ch, uint32_t tick, int val);

static void ChordAt(const BarInfo* b, int st, int* pc, int* ty)
{
	int p = (b && b->root >= 0) ? (b->root % 12) : 0;
	int t = b ? b->type : CH_MAJ;
	if (b && b->root2 >= 0 && st >= 24) {
		p = b->root2 % 12;
		t = b->type2;
	}
	if (p < 0) p += 12;
	if (pc) *pc = p;
	if (ty) *ty = t;
}

static int FitRange(int midi, int prev, int lo, int hi)
{
	return NearMidi(midi, prev, lo, hi);
}

static int ChordPick(int pc, int type, int which, int prev, int lo, int hi)
{
	int tones[4];
	int n = ChordTones(pc, type, 0, tones, 4);
	if (n < 1) return FitRange(48 + (pc % 12), prev, lo, hi);
	if (which < 0) which = -which;
	return FitRange(tones[which % n], prev, lo, hi);
}

static int ScalePick(int pc, int type, int deg, int prev, int lo, int hi)
{
	return FitRange(ScaleStep(pc, type, deg), prev, lo, hi);
}

/* 強拍用。目標のスケール音に一番近い構成音。 */
static int SnapChord(int pc, int type, int deg, int prev, int lo, int hi)
{
	int target = ScaleStep(pc, type, deg);
	int tones[4];
	int n = ChordTones(pc, type, 0, tones, 4);
	if (n < 1) return FitRange(target, prev, lo, hi);
	int best = target;
	int bd = 99;
	for (int i = 0; i < n; i++) {
		int c = tones[i];
		while (c < target - 6) c += 12;
		while (c > target + 6) c -= 12;
		int d = c - target;
		if (d < 0) d = -d;
		if (d < bd) { bd = d; best = c; }
	}
	return FitRange(best, prev, lo, hi);
}

/* 次の根音の直前。すでに近ければ根音のまま。 */
static int ApproachMidi(int curRoot, int type, int nextPc)
{
	int cur = BassMidi(curRoot, 1, type);
	int nxt = BassMidi(nextPc, 1, type);
	while (nxt < cur - 6) nxt += 12;
	while (nxt > cur + 6) nxt -= 12;
	int gap = nxt - cur;
	if (gap < 0) gap = -gap;
	if (gap <= 2) return nxt;
	int ap = nxt - 1;
	int rel = (ap - (36 + (curRoot % 12)) + 120) % 12;
	if (rel == ThirdIv(type)) ap = nxt - 2;
	while (ap < 28) ap += 12;
	while (ap > 52) ap -= 12;
	return ap;
}

/* スムーズなボイスリーディング付きコードボイシング。
   前小節の中心音（prevCenter）に一番近い転回形を選び、機械的なルート跳躍を防ぐ。 */
static int BuildChordVoicing(int pc, int type, int power, int extLvl, int prevCenter, int lo, int hi, int* out, int cap)
{
	if (!out || cap < 1) return 0;
	int iv[5];
	int n = 0;
	iv[n++] = 0;
	if (power || type == CH_POW) {
		iv[n++] = 7;
		if (cap >= 3) iv[n++] = 12;
	} else {
		if (type == CH_SUS) iv[n++] = 5;
		else iv[n++] = ThirdIv(type);
		iv[n++] = (type == CH_DIM) ? 6 : 7;
		if (type == CH_7 || type == CH_M7 || (extLvl >= 1 && (type == CH_MIN)))
			iv[n++] = 10;
		else if (type == CH_MAJ7 || (extLvl >= 1 && type == CH_MAJ))
			iv[n++] = 11;
		else if (type == CH_DIM)
			iv[n++] = 9;
		if (extLvl >= 2 && n < 5 && type != CH_DIM && type != CH_SUS)
			iv[n++] = 14; /* 9th / add9 */
	}
	if (n > cap) n = cap;
	int target = (prevCenter >= lo && prevCenter <= hi) ? prevCenter : ((lo + hi) / 2);
	int best[5];
	int bestScore = 999999;
	for (int inv = 0; inv < n; inv++) {
		for (int baseOct = 3; baseOct <= 6; baseOct++) {
			int cand[5];
			int sum = 0;
			int ok = 1;
			for (int k = 0; k < n; k++) {
				int idx = (inv + k) % n;
				int m = baseOct * 12 + (pc % 12) + iv[idx];
				if (idx < inv) m += 12;
				while (k > 0 && m <= cand[k - 1]) m += 12;
				if (m < lo - 2 || m > hi + 2) { ok = 0; break; }
				cand[k] = m;
				sum += m;
			}
			if (!ok) continue;
			int avg = sum / n;
			int score = avg - target;
			if (score < 0) score = -score;
			if (cand[n - 1] - cand[0] > 17) score += 6;
			if (score < bestScore) {
				bestScore = score;
				for (int k = 0; k < n; k++) best[k] = cand[k];
			}
		}
	}
	if (bestScore >= 999999) {
		int base = 48 + (pc % 12);
		while (base < lo) base += 12;
		while (base > hi - 7) base -= 12;
		for (int k = 0; k < n; k++) {
			int m = base + iv[k];
			while (m > hi) m -= 12;
			while (m < lo) m += 12;
			best[k] = m;
		}
	}
	for (int k = 0; k < n; k++) {
		if (best[k] < 0) best[k] = 0;
		if (best[k] > 127) best[k] = 127;
		out[k] = best[k];
	}
	return n;
}

static int ShapeVel(int baseVel, const BarPlan& pl, const ArrProfile& ap, int st)
{
	int v = baseVel + ((int)pl.energy - 70) * 3 / 10;
	if (ap.velShape == 1) v += ((st % 12) == 0) ? 10 : -4;
	else if (ap.velShape == 2) v += ((st % 12) != 0 || st == 12 || st == 36) ? 10 : -4;
	else if (ap.velShape == 3) v += (st / 6) * 2 - 6;
	else if (ap.velShape == 4) v -= 10;
	if (v < 36) v = 36;
	if (v > 124) v = 124;
	return v;
}

/* キー基準の歌唱メロディ音高解決:
   曲全体のキー(tonic, scale)上で滑らかな旋律輪郭を保ちつつ、強拍・ロングトーンは現在コードの構成音へ
   ±2半音優先で自然に吸着し、弱拍の経過音でもコード3度/ルートとの半音ぶつかり(アボイド)を避ける。 */
static int MelPitch(int tonic, int scale, int pcC, int tyC, int rawDeg, int strong, int prev, int lo, int hi)
{
	int tPc = ((tonic % 12) + 12) % 12;
	int target = 60 + tPc + ScaleOff(scale, rawDeg);
	if (prev >= 0) {
		while (target - prev > 7 && target - 12 >= lo) target -= 12;
		while (prev - target > 7 && target + 12 <= hi) target += 12;
	}
	while (target < lo) target += 12;
	while (target > hi) target -= 12;

	int ct[4];
	int nc = ChordTones(pcC, tyC, 0, ct, 4);
	if (nc < 1) return target;

	if (strong) {
		int best = target;
		int bestScore = 9999;
		for (int oct = -2; oct <= 2; oct++) {
			for (int i = 0; i < nc; i++) {
				int cand = ct[i] + oct * 12;
				if (cand < lo || cand > hi) continue;
				int dt = cand - target; if (dt < 0) dt = -dt;
				int dp = (prev >= 0) ? (cand - prev) : 0; if (dp < 0) dp = -dp;
				int score = dt * 10 + ((dp > 7) ? (dp - 7) * 3 : 0);
				if (score < bestScore) { bestScore = score; best = cand; }
			}
		}
		return best;
	}

	int mPc = target % 12;
	int rPc = ct[0] % 12;
	int thirdPc = (nc > 1) ? (ct[1] % 12) : -1;
	int clash = (((mPc - rPc + 12) % 12) == 1) || (thirdPc >= 0 && ((mPc - thirdPc + 12) % 12) == 1);
	if (clash) {
		int up = target + 1;
		int dn = target - 1;
		if (up <= hi && ((up % 12) == rPc || (up % 12) == thirdPc || ((up - rPc + 12) % 12) == 2)) return up;
		if (dn >= lo) return dn;
	}
	return target;
}

static void ComposeBar(Ctx* c, const BarInfo* b, int bar, int form, int tonic,
	int nBass, const int* bassCh, int nChord, const int* chordCh,
	int nPad, const int* padCh, int nArp, const int* arpCh,
	int nHarm, const int* harmCh, int nCnt, const int* cntCh,
	int nStab, const int* stabCh, int nColor, const int* colorCh,
	int melodyCh, int genMel)
{
	if (!c || !b || b->len < 4) return;
	const int style = (s_song.styleId >= 0 && s_song.styleId < SC_AS_COUNT) ? s_song.styleId : 0;
	const StyleDef& sd = kDef[style];
	const StyleExtra& sx = kStyleExtra[style];
	const ArrGene& ag = kArrGene[s_song.pat & 15];
	const ArrProfile& ap = kArrProf[s_song.pat & 15];
	const BarPlan& pl = PlanOfBar((b->section == SEC_BODY) ? bar : 0);
	const int end = (b->section == SEC_END && s_endRem <= 0);
	const int endCadence = (b->section == SEC_END && s_endRem > 0);
	const int isBreak = (b->section == SEC_BREAK);
	const int pos = (b->section == SEC_BODY) ? PosOf(bar) : 0;
	const int span = s_song.span > 0 ? s_song.span : 16;
	const int melSpan = s_song.melSpan >= 16 ? s_song.melSpan : span;
	const int melBar = (b->section == SEC_BODY) ? (bar % melSpan) : 0;
	const int rd = RootAt(bar, b->section);
	const int nxt = (b->section == SEC_BODY) ? RootAt(bar + 1, SEC_BODY) : rd;
	const int fig = s_song.fig;
	const int waltz = (s_song.cast.drum == 3);
	const int thin = (ag.dens == 0);
	const int boom = (IsBoom(pos) || pl.sec == 3 || pl.sec == 7) && b->section == SEC_BODY;
	const int quiet = (fig == 5 || fig == 0) && (s_song.cast.drum == 8 || fig == 5);

	/* メロディを3〜4chへ丸コピーするユニゾン潰しをやめる。
	   form 0..100 に応じて対旋律(Counter)とサビの3度/6度ハモリ(Harm)が段階的に呼応する。
	   ch1が無いときだけ、form>=25 でハモリ/対旋律の1本に歌の動機を託す。 */
	const int melActive = (form >= 1 && b->section == SEC_BODY);
	const int boostNoCh1 = (!genMel && form >= 25) ? 1 : 0;
	int melChord = 0, melArp = 0, melBass = 0, melPad = 0, melHarm = 0, melCnt = 0;
	if (melActive) {
		if (boostNoCh1) {
			if (nHarm > 0) melHarm = 1;
			else if (nCnt > 0) melCnt = 1;
		} else if (genMel) {
			if (form >= 65 && (pl.sec >= 2 || ap.callResp > 0)) melCnt = 1;
			else if (form >= 35 && (pl.sec == 3 || pl.sec == 7 || pl.phraseBar == 3)) melCnt = 1;
			if (form >= 85 && nHarm > 0 && (pl.sec == 3 || pl.sec == 7)) melHarm = 2; /* サビの3度ハモリ */
		}
	}
	(void)melChord; (void)melArp; (void)melBass; (void)melPad;

	auto emitBend = [&](int ch, int at, int amt) {
		if (c->isFm || ch < 0) return;
		uint32_t tk = b->t0 + (uint32_t)((at * b->len) / 48);
		uint32_t rst = tk + (uint32_t)((4 * b->len) / 48);
		if (rst >= b->t0 + (uint32_t)b->len) rst = b->t0 + (uint32_t)b->len - 1;
		int val = 64 + amt;
		if (val < 16) val = 16;
		if (val > 112) val = 112;
		Push(c, tk, ch, SC_EV_PITCH, (uint8_t)val, 0, 0, 0);
		if (rst > tk) Push(c, rst, ch, SC_EV_PITCH, 64, 0, 0, 0);
	};

	auto sing = [&](int ch, int* prev, int lo, int hi, int mode, int vel0) -> int {
		if (ch < 0 || !prev) return 0;
		int src = (melBar >= 0 && melBar < 256) ? melBar : 0;
		int n = s_song.melCount[src];
		if (n < 1) return 0;
		if (n > 12) n = 12;
		int wrote = 0;
		for (int k = 0; k < n && !c->overflow; k++) {
			if (mode == 2 && (k & 1)) continue;
			if (mode == 1 && form < 65 && k + 1 < n) continue; /* 中formではフレーズ末尾だけ呼応 */
			int at = s_song.mel[src][k].at;
			int du = s_song.mel[src][k].du;
			if (mode == 1) { at += 6; if (at > 42) continue; }
			int raw = (int)s_song.mel[src][k].deg;
			if (mode == 2) {
				if (du < 10) du = 10;
				if (k + 2 < n) {
					int lim = (int)s_song.mel[src][k + 2].at - at - 1;
					if (lim < 2) lim = 2;
					if (du > lim) du = lim;
				}
			}
			int pcS, tyS;
			ChordAt(b, at, &pcS, &tyS);
			const int strong = ((at % 12) == 0) || k == 0 || k == n - 1 || du >= 12;
			int degAdj = (mode == 1) ? (raw - 2) : (mode == 2) ? (raw - 2) : raw;
			int m;
			if (k == n - 1 && pl.turn == 3)
				m = ChordPick(pcS, tyS, 0, *prev, lo, hi);
			else if (mode == 3)
				m = ChordPick(pcS, tyS, k, *prev, lo, hi);
			else
				m = MelPitch(tonic, s_song.cast.scale, pcS, tyS, degAdj, strong, *prev, lo, hi);
			*prev = m;
			int keep = s_putForce;
			s_putForce = 1;
			PutFrac(c, ch, b, at, du, m, ShapeVel(vel0 - ((k & 1) ? 8 : 0), pl, ap, at), mode == 2 ? 92 : 80);
			if (s_song.mel[src][k].bend && sx.bendMode && ag.bendScale >= 2)
				emitBend(ch, at, (sx.bendMode == 4) ? -14 : -10);
			s_putForce = keep;
			wrote = 1;
		}
		return wrote;
	};

	s_barNotes = 0;
	s_putForce = 0;
	if (isBreak) {
		/* ブレイク小節: 1小節目頭のキメ打ち＋柔らかなパッド余韻＋最終小節末尾のベースピックアップ */
		s_putForce = 1;
		int pcK, tyK;
		ChordAt(b, 0, &pcK, &tyK);
		int nextPcK = (s_nextRootPc >= 0) ? (s_nextRootPc % 12) : pcK;
		if (s_secIdx == 0) {
			int bm = ChordPick(pcK, tyK, 0, s_song.prevBass, 28, 48);
			s_song.prevBass = bm;
			if (nBass > 0) PutFrac(c, bassCh[0], b, 0, 12, bm, 98, 72);
			if (nChord > 0) {
				int tones[4];
				int nt = BuildChordVoicing(pcK, tyK, sd.power, pl.extLvl, s_song.prevCh, 52, 78, tones, 4);
				int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
				for (int v = 0; v < nv; v++)
					PutFrac(c, chordCh[0], b, 0, 12, tones[v], 96 - v * 5, 62);
			}
		}
		if (nPad > 0) {
			int tones[4];
			int nt = BuildChordVoicing(pcK, tyK, 0, pl.extLvl, s_song.prevPad, 58, 84, tones, 4);
			int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
			for (int v = 0; v < nv; v++)
				PutFrac(c, padCh[0], b, 0, 44, tones[v], 56 - v * 3, 98);
		}
		if (s_secIdx + 1 >= s_secLen && nBass > 0) {
			/* ブレイク明けへ勢いよく戻るピックアップ2音 */
			int m1 = ChordPick(pcK, tyK, 2, s_song.prevBass, 28, 48);
			int m2 = ApproachMidi(pcK, tyK, nextPcK);
			PutFrac(c, bassCh[0], b, 36, 5, m1, 88, 78);
			PutFrac(c, bassCh[0], b, 42, 5, m2, 98, 86);
			s_song.prevBass = m2;
		}
		return;
	}
	if (end) {
		s_putForce = 1;
		int pcE, tyE;
		ChordAt(b, 0, &pcE, &tyE);
		int bm = ChordPick(pcE, tyE, 0, s_song.prevBass, 28, 48);
		s_song.prevBass = bm;
		for (int i = 0; i < nBass; i++) {
			int m = bm + (i == 1 ? -12 : i >= 2 ? 12 : 0);
			while (m < 24) m += 12;
			while (m > 60) m -= 12;
			PutFrac(c, bassCh[i], b, 0, 48, m, 98 - i * 8, 96);
		}
		for (int i = 0; i < nChord; i++) {
			int tones[4];
			int nt = BuildChordVoicing(pcE, tyE, sd.power, pl.extLvl, s_song.prevCh, 52 + i * 4, 78 + i * 4, tones, 4);
			int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
			for (int v = 0; v < nv; v++)
				PutFrac(c, chordCh[i], b, 0, 48, tones[v], 80 - i * 6 - v * 4, 96);
		}
		for (int i = 0; i < nPad; i++) {
			int tones[4];
			int nt = BuildChordVoicing(pcE, tyE, 0, pl.extLvl, s_song.prevPad, 56 + i * 5, 84 + i * 4, tones, 4);
			int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
			for (int v = 0; v < nv; v++)
				PutFrac(c, padCh[i], b, 0, 48, tones[v], 66 - i * 5 - v * 3, 98);
		}
		if (genMel && melodyCh >= 0 && form >= 1) {
			int lm = MelPitch(tonic, s_song.cast.scale, pcE, tyE, 0, 1, s_song.prevMel, s_song.cast.lo, s_song.cast.hi);
			s_song.prevMel = lm;
			PutFrac(c, melodyCh, b, 0, 48, lm, 108, 96);
		}
		return;
	}

	if (!c->isFm && (sx.ccFlags & CCF_EXPR) && b->section == SEC_BODY) {
		/* 64小節の楽曲エネルギー(pl.energy)に連動したエクスプレッション・クレッシェンド */
		int baseEx = 62 + (int)(pl.energy * 50 / 100);
		int midEx = baseEx + ((pl.phraseBar == 3 || pl.phraseBar == 7) ? 14 : 6);
		if (baseEx > 116) baseEx = 116;
		if (midEx > 126) midEx = 126;
		if (nPad > 0 && padCh[0] >= 0) {
			PlaceExpr(c, padCh[0], b->t0, baseEx);
			PlaceExpr(c, padCh[0], b->t0 + (uint32_t)(b->len / 2), midEx);
		}
		if (genMel && melodyCh >= 0 && form >= 1) {
			int m0 = baseEx + 12; if (m0 > 120) m0 = 120;
			int m1 = midEx + 8; if (m1 > 127) m1 = 127;
			PlaceExpr(c, melodyCh, b->t0, m0);
			PlaceExpr(c, melodyCh, b->t0 + (uint32_t)(b->len / 2), m1);
		}
	}
	if (endCadence) {
		/* マルチ小節エンディングの終止前小節(IV/ii -> V7): ゆったり減速しながら最終Iへ導く */
		s_putForce = 1;
		int pcE, tyE;
		ChordAt(b, 0, &pcE, &tyE);
		int nextPcE = (s_nextRootPc >= 0) ? (s_nextRootPc % 12) : pcE;
		int bm = ChordPick(pcE, tyE, 0, s_song.prevBass, 28, 48);
		s_song.prevBass = bm;
		for (int i = 0; i < nBass; i++) {
			int m0 = bm + (i == 1 ? -12 : i >= 2 ? 12 : 0);
			while (m0 < 24) m0 += 12;
			while (m0 > 60) m0 -= 12;
			PutFrac(c, bassCh[i], b, 0, 22, m0, 92 - i * 8, 92);
			int m1 = (s_endRem == 1 && i == 0) ? ApproachMidi(pcE, tyE, nextPcE) : ChordPick(pcE, tyE, 2, m0, 28, 52);
			PutFrac(c, bassCh[i], b, 24, 22, m1, 86 - i * 8, 88);
		}
		for (int i = 0; i < nChord && i < 2; i++) {
			int tones[4];
			int nt = BuildChordVoicing(pcE, tyE, sd.power, pl.extLvl, s_song.prevCh, 52 + i * 4, 78 + i * 4, tones, 4);
			int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
			for (int v = 0; v < nv; v++) {
				PutFrac(c, chordCh[i], b, 0, 22, tones[v], 82 - i * 6 - v * 4, 92);
				PutFrac(c, chordCh[i], b, 24, 22, tones[v], 76 - i * 6 - v * 4, 90);
			}
		}
		for (int i = 0; i < nPad && i < 2; i++) {
			int tones[4];
			int nt = BuildChordVoicing(pcE, tyE, 0, pl.extLvl, s_song.prevPad, 56 + i * 5, 84 + i * 4, tones, 4);
			int nv = c->isFm ? 1 : (nt < 3 ? nt : 3);
			for (int v = 0; v < nv; v++)
				PutFrac(c, padCh[i], b, 0, 44, tones[v], 62 - i * 5 - v * 3, 98);
		}
		return;
	}
	if (!c->isFm && (sx.ccFlags & CCF_PEDAL) && nChord > 0 && chordCh[0] >= 0) {
		Push(c, b->t0, chordCh[0], SC_EV_PEDAL_ON, 127, 0, 0, 0);
		Push(c, b->t0 + (uint32_t)(b->len > 6 ? b->len - 4 : b->len - 1), chordCh[0], SC_EV_PEDAL_OFF, 0, 0, 0, 0);
	}
	if (!c->isFm && sx.bendMode == 5 && b->section == SEC_BODY && (pos + 1 == s_song.impact || pos + 1 == s_song.impact2) && nPad > 0 && padCh[0] >= 0) {
		for (int st = 0; st < 4; st++)
			Push(c, b->t0 + (uint32_t)(st * b->len / 4), padCh[0], SC_EV_PITCH, (uint8_t)(46 + st * 6), 0, 0, 0);
		Push(c, b->t0 + (uint32_t)(b->len - 2), padCh[0], SC_EV_PITCH, 64, 0, 0, 0);
	}

	s_putForce = 1;
	{
		const int famB = StyleFam(style);
		const int patB = s_song.pat & 15;
		const int divB = (sd.div == 12) ? 12 : 16;
		const int unitB = 48 / divB;
		const int allowOdd16 = (divB == 16) && (famB == FAM_16 || famB == FAM_FUNK || famB == FAM_DNB
			|| famB == FAM_LATIN || famB == FAM_SAMBA || famB == FAM_AFRO
			|| famB == FAM_HOP || famB == FAM_BOSSA || famB == FAM_TRAP);
		int nextPc = (s_nextRootPc >= 0) ? (s_nextRootPc % 12) : 0;
		if (nextPc < 0) nextPc += 12;

		for (int i = 0; i < nBass && !c->overflow; i++) {
			/* 3小節以上のイントロでは1小節目のベースを抜き、2小節目から入って立体感を出す */
			if (b->section == SEC_INTRO && s_secLen >= 3 && s_secIdx == 0) continue;
			int at[16], deg[16], appr[16];
			int nh = 0;
			auto addB = [&](int a, int d, int apFlag) {
				if (nh >= 16 || a > 45) return;
				if (a < 0) a = 0;
				for (int z = 0; z < nh; z++) {
					if (at[z] == a) { deg[z] = d; appr[z] = apFlag; return; }
				}
				at[nh] = a; deg[nh] = d; appr[nh] = apFlag; nh++;
			};

			if (i == 1) {
				/* ch2本目のベース: サブベース／土台アンカー（1本目の細かい動きと濁らせない） */
				addB(0, 1, 0);
				if (b->root2 >= 0) addB(24, 1, 0);
				else if (pl.sec >= 2 && !waltz) addB(24, (pl.varBits & 1) ? 2 : 1, 0);
				else if (waltz && pl.sec >= 3) addB(16, 2, 0);
			} else if (i == 2) {
				/* 3本目ベース: 高域オクターブの補完グルーヴ（前サビ・サビ・展開で厚みを足す） */
				if (pl.sec < 2 && pl.turn == 0 && ag.dens < 2) continue;
				if (waltz) { addB(16, 3, 0); addB(32, 2, 0); }
				else {
					addB(12, 3, 0);
					addB(36, (pl.turn >= 1) ? 1 : 2, pl.turn >= 1 ? 1 : 0);
					if (pl.sec >= 3 && allowOdd16) addB(21, 5, 0);
				}
			} else if (i >= 3) {
				/* 4本目以降のベース: サビ・ソロ限定のシンコペ/ペダル層（3本目と非重複） */
				if (pl.sec != 3 && pl.sec != 6 && pl.sec != 7 && pl.turn < 2) continue;
				if (waltz) { addB(0, 3, 0); addB(32, 5, 0); }
				else {
					int sh = (i & 1) ? 6 : 0;
					addB(sh, 1, 0);
					addB(18 + sh, 2, 0);
					addB(30 + (sh ? 6 : 0), 3, pl.turn >= 1 ? 1 : 0);
				}
			} else {
				/* メインベース: 全60スタイル固有の kDef[style].bass 格子を土台に読み出す */
				if (sd.bass && sd.bass[0]) {
					for (int s = 0; s < divB && sd.bass[s]; s++) {
						char ch = sd.bass[s];
						if (ch >= '1' && ch <= '9') {
							int d = SafeDeg(ch - '0');
							/* 64小節内で毎回同じ度数にならないようセクションと変異ビットで彩る */
							if (s > 0 && d == 1 && (pl.sec == 3 || pl.sec == 7) && (pl.varBits & (1u << (s & 3))))
								d = (famB == FAM_FLOOR || famB == FAM_DISCO || famB == FAM_16 || famB == FAM_FUNK) ? 3 : 2;
							else if (s > 0 && d == 5 && (pl.sec == 1 || pl.sec == 4) && ((pl.varBits >> 2) & 1))
								d = 8;
							else if (s > 0 && famB == FAM_BLUES && ((bar / 12) & 1) && (s == 6 || s == 9))
								d = (d == 5) ? 6 : 5;
							addB(s * unitB, d, 0);
						}
					}
				}
				if (nh == 0) {
					addB(0, 1, 0);
					if (waltz) { addB(16, 2, 0); addB(32, 5, 0); }
					else addB(24, 2, 0);
				}

				/* 半小節コード変化時は 24(3拍目頭) に必ず根音を置く */
				if (b->root2 >= 0 && !waltz) addB(24, 1, 0);

				/* アレンジ別ベース奏法（A〜P）＋64小節セクション展開 */
				if (ap.bassMode == 1 && !waltz) {
					/* ウォーキング・経過音主導 */
					static const int wd[4][4] = {
						{ 1, 5, 2, 8 }, { 1, 2, 5, 8 }, { 1, 8, 5, 2 }, { 1, 5, 8, 2 }
					};
					const int row = (bar + (patB & 3) + pl.sec) & 3;
					nh = 0;
					addB(0, wd[row][0], 0);
					addB(12, wd[row][1], 0);
					addB(24, (b->root2 >= 0) ? 1 : wd[row][2], 0);
					addB(36, wd[row][3], 1);
					if (pl.turn >= 1 && (pl.varBits & 1)) addB(42, 1, 1);
				} else if (ap.bassMode == 2 && !waltz && famB != FAM_BALLAD && famB != FAM_PAD) {
					/* オクターブ跳躍バウンス */
					int step = (famB == FAM_8 || famB == FAM_METAL || famB == FAM_16 || famB == FAM_DISCO || famB == FAM_FLOOR) ? 6 : 12;
					if (pl.breakFeel == 1) step = 12;
					nh = 0;
					for (int st = 0; st < 48; st += step) {
						int isOff = ((st / step) & 1);
						int d = isOff ? 3 : ((st == 24 && b->root2 < 0 && (pl.varBits & 1)) ? 2 : 1);
						addB(st, d, (st + step >= 48 && pl.turn >= 1) ? 1 : 0);
					}
				} else if ((ap.bassMode == 3 || pl.breakFeel == 1) && nh > 2) {
					/* ハーフタイム・抜きセクション：細かい打点を間引いて重心を下げる */
					int w = 0;
					for (int k = 0; k < nh; k++) {
						if (at[k] == 0 || at[k] == 24 || (waltz && at[k] == 16) || (pl.turn >= 1 && k == nh - 1)) {
							at[w] = at[k]; deg[w] = deg[k]; appr[w] = appr[k]; w++;
						}
					}
					if (w > 0) nh = w;
				} else if (ap.bassMode == 5 && !waltz && famB != FAM_BALLAD && famB != FAM_PAD && famB != FAM_JAZZ) {
					/* 推進力 8分ドライブ */
					static const int d8[4][8] = {
						{ 1, 1, 2, 1, 5, 2, 8, 1 },
						{ 1, 2, 1, 5, 1, 2, 3, 1 },
						{ 1, 1, 5, 2, 1, 8, 2, 1 },
						{ 1, 3, 2, 5, 1, 2, 5, 1 }
					};
					const int row = (bar + pl.sec + (pl.varBits & 3)) & 3;
					nh = 0;
					for (int k = 0; k < 8; k++)
						addB(k * 6, (k == 4 && b->root2 >= 0) ? 1 : d8[row][k], (k == 7) ? 1 : 0);
				} else {
					/* スタイル固有パターンの 64小節マイクロ変異 */
					if (pl.sec == 4 && nh > 3) {
						/* 2番A: 1か所抜いて新鮮な間を作る */
						int drop = 1 + (pl.varBits % (nh - 1));
						for (int k = drop; k + 1 < nh; k++) {
							at[k] = at[k + 1]; deg[k] = deg[k + 1]; appr[k] = appr[k + 1];
						}
						nh--;
					}
					if ((pl.sec == 2 || pl.sec == 3 || pl.sec == 6 || pl.sec == 7) && !waltz) {
						/* 前サビ・サビ・展開: 推進ピックアップ追加 */
						int pickSt = allowOdd16 ? ((pl.varBits & 1) ? 39 : 42) : ((divB == 12) ? 40 : 42);
						addB(pickSt, (pl.varBits & 2) ? 2 : 5, 1);
						if ((pl.sec == 3 || pl.sec == 7) && allowOdd16 && nh < 10)
							addB((pl.varBits & 4) ? 15 : 21, 3, 0);
					}
					if (ap.bassMode == 4 && allowOdd16 && nh > 1) {
						/* シンコペ溜め（16分系スタイル限定。8ビートやジャズは崩さない） */
						for (int k = nh - 1; k >= 1; k--) {
							if ((k + (pl.varBits & 1)) & 1) {
								int lim = (k + 1 < nh) ? (at[k + 1] - 3) : 45;
								if (at[k] + 3 <= lim) at[k] += 3;
							}
						}
					}
					if (ap.bassMode == 6 && !waltz && (pl.turn >= 1 || (pl.varBits & 3) == 0)) {
						/* ファンク/ベース主導フィル */
						if (allowOdd16) { addB(36, 2, 0); addB(39, 8, 0); addB(42, 3, 0); addB(45, 1, 1); }
						else { addB(36, 2, 0); addB(42, 1, 1); }
					}
					if (famB == FAM_BLUES && !waltz && b->section == SEC_BODY) {
						/* 12小節ブルースの5コーラス展開（64小節全体でコーラス毎に別ベースライン） */
						int chs = (bar / 12) % 5;
						static const int blWalk[5][4] = {
							{ 1, 5, 2, 8 }, { 1, 2, 5, 6 }, { 1, 3, 2, 8 }, { 1, 5, 6, 8 }, { 1, 8, 2, 5 }
						};
						if (chs > 0 && nh >= 2) {
							deg[nh - 1] = blWalk[chs][(bar + chs) & 3];
							if (chs >= 2 && (bar & 1)) addB(divB == 12 ? 32 : 30, blWalk[chs][2], 0);
							if (chs >= 3 && (pl.phraseBar & 1)) addB(divB == 12 ? 44 : 42, blWalk[chs][3], 1);
						}
					}
					if (pl.turn == 2 && !waltz && !quiet && b->section == SEC_BODY) {
						/* 8小節境界の推進ウォークアップ（次セクション頭へ自然に繋ぐ） */
						addB(36, (pl.varBits & 1) ? 2 : 5, 0);
						addB(divB == 12 ? 44 : 42, 1, 1);
					}
				}

				/* 4小節/8小節ターンアラウンドでは末尾音を次小節へのアプローチ音にする */
				if (nh > 1 && (pl.turn >= 1 || (pl.varBits & 3) == 3))
					appr[nh - 1] = 1;
				if (pl.turn == 3) {
					/* 曲の最終小節は解決の長い1〜2音で締める */
					nh = 1;
					at[0] = 0; deg[0] = 1; appr[0] = 0;
				}
			}

			/* 発音位置順に整列 */
			for (int a = 0; a < nh; a++) {
				for (int c2 = a + 1; c2 < nh; c2++) {
					if (at[c2] < at[a]) {
						int ta = at[a], td = deg[a], tp = appr[a];
						at[a] = at[c2]; deg[a] = deg[c2]; appr[a] = appr[c2];
						at[c2] = ta; deg[c2] = td; appr[c2] = tp;
					}
				}
			}

			for (int k = 0; k < nh && !c->overflow; k++) {
				if (k > 0 && at[k] == at[k - 1]) continue;
				int st = at[k];
				int pc, ty;
				ChordAt(b, st, &pc, &ty);
				int midi;
				if (appr[k] && k == nh - 1 && pl.turn != 3)
					midi = ApproachMidi(pc, ty, nextPc);
				else
					midi = BassMidi(pc, deg[k], ty);
				int loB = (i >= 2) ? 40 : 28;
				int hiB = (i >= 2) ? 58 : ((deg[k] == 3) ? 56 : 50);
				if (i == 1) { midi -= 12; hiB = 45; }
				if (ap.octShift <= -7 && i == 0) midi -= 12;
				while (midi < loB) midi += 12;
				while (midi > hiB) midi -= 12;

				int du = (k + 1 < nh) ? (at[k + 1] - st) : (48 - st);
				if (du < 2) du = 2;
				if (i == 0 && (famB == FAM_16 || famB == FAM_FUNK || famB == FAM_DNB) && du > 9) du = 9;
				else if (i == 0 && (famB == FAM_8 || famB == FAM_METAL) && du > 6 && pl.turn != 3) du = 6;
				if (st + du > 48) du = 48 - st;

				int baseVel = ((st % 12) == 0) ? 112 : ((st % 6) == 0) ? 98 : 84;
				if (appr[k]) baseVel = 94;
				if (i == 1) baseVel -= 10;
				else if (i >= 2) baseVel -= 16;
				int vel = ShapeVel(baseVel, pl, ap, st);
				int gate = (i == 1) ? 94 : (ap.gatePct > 86 ? 88 : (ap.gatePct < 55 ? 58 : 78));
				PutFrac(c, bassCh[i], b, st, du, midi, vel, gate);
				if (i == 0) s_song.prevBass = midi;
				if (i == 0 && sx.bendMode == 4 && k == nh - 1 && !c->isFm)
					emitBend(bassCh[i], st, -16);
			}
			if (s_song.soloAcc && i == 0 && !c->isFm && nChord < 1) {
				int pc0, ty0;
				ChordAt(b, waltz ? 16 : 24, &pc0, &ty0);
				int tones[4];
				int nt = BuildChordVoicing(pc0, ty0, sd.power, pl.extLvl, 64, 55, 76, tones, 3);
				int hst = waltz ? 16 : 24;
				int hdu = waltz ? 10 : 6;
				for (int t = 0; t < nt && t < 2; t++)
					PutFrac(c, bassCh[0], b, hst, hdu, tones[t], ShapeVel(74 - t * 4, pl, ap, hst), 80);
			}
		}
	}

	s_putForce = 1;
	for (int i = 0; i < nChord && !c->overflow; i++) {
		const int famC = StyleFam(style);
		const int patC = s_song.pat & 15;
		const int divC = (sd.div == 12) ? 12 : 16;
		const int unitC = 48 / divC;
		const int isPowC = sd.power ? 1 : 0;
		const int extC = isPowC ? 0 : (int)pl.extLvl;

		int loC = 52 + (int)ap.octShift / 2 + (pl.regShift > 0 ? 4 : pl.regShift < 0 ? -4 : 0);
		int hiC = 78 + (int)ap.octShift / 2 + (pl.regShift > 0 ? 5 : pl.regShift < 0 ? -4 : 0);
		if (i == 1) { loC -= 5; hiC -= 4; }
		else if (i >= 2) { loC += 7; hiC += 10; }
		if (loC < 48) loC = 48;
		if (hiC > 94) hiC = 94;
		if (hiC < loC + 10) hiC = loC + 10;

		const int baseGate = (int)ap.gatePct;
		auto emitVoice = [&](int st, int du, int velBase, int maxNotes, int gateOv) {
			if (st < 0 || st > 46 || du < 1) return;
			if (st + du > 48) du = 48 - st;
			int pc, ty;
			ChordAt(b, st, &pc, &ty);
			int tones[5];
			int nt = BuildChordVoicing(pc, ty, isPowC, extC, s_song.prevCh, loC, hiC, tones, 5);
			if (nt < 1) return;
			int nv = c->isFm ? 1 : (nt < maxNotes ? nt : maxNotes);
			int gate = (gateOv > 0) ? gateOv : baseGate;
			if (ap.compMode == 7 && du > 3) du = 3;
			else if (ap.compMode == 8 && du < 16) gate = 96;
			int sum = 0;
			for (int v = 0; v < nv; v++) {
				int idx = (c->isFm) ? ((bar + st + i) % nt) : v;
				int vv = ShapeVel(velBase - v * 5 - i * 4, pl, ap, st);
				PutFrac(c, chordCh[i], b, st, du, tones[idx], vv, gate);
				sum += tones[idx];
			}
			if (i == 0 && nv > 0) s_song.prevCh = sum / nv;
		};
		auto emitSingle = [&](int st, int du, int pick, int velBase, int gateOv) {
			if (st < 0 || st > 46 || du < 1) return;
			if (st + du > 48) du = 48 - st;
			int pc, ty;
			ChordAt(b, st, &pc, &ty);
			int tones[5];
			int nt = BuildChordVoicing(pc, ty, isPowC, extC, s_song.prevCh, loC, hiC, tones, 5);
			if (nt < 1) return;
			int m = tones[(pick < 0 ? -pick : pick) % nt];
			int vv = ShapeVel(velBase - i * 4, pl, ap, st);
			PutFrac(c, chordCh[i], b, st, du, m, vv, gateOv > 0 ? gateOv : baseGate);
			if (i == 0) s_song.prevCh = m;
		};

		/* 曲の最終小節は解決の長いボイシング */
		if (pl.turn == 3) {
			emitVoice(0, 44, 88, 4, 96);
			continue;
		}

		/* 複数コードチャンネル（ch1〜ch32対応）: 2本目以降は1本目と違う奏法で立体化 */
		if (i == 1) {
			/* 2本目: 持続ボイシング／半小節コード変更に追従する温かいコード床 */
			if (pl.breakFeel == 1 && (pl.phraseBar & 1) && ag.dens < 2) continue;
			int d0 = (b->root2 >= 0) ? 22 : 44;
			emitVoice(0, d0, 72, 3, 94);
			if (b->root2 >= 0) emitVoice(24, 22, 70, 3, 94);
			continue;
		} else if (i == 2) {
			/* 3本目コード: 上声部の分散アルペジオ／裏打ちスパークル（2本目と非重複） */
			if (pl.sec == 0 && (pl.phraseBar & 1) == 0 && ag.dens < 2) continue;
			int step = (divC == 12) ? 8 : 6;
			int start = (divC == 12) ? 4 : 6;
			for (int st = start, k = 0; st <= 44 && !c->overflow; st += step * 2, k++) {
				if (pl.breakFeel == 1 && (k & 1)) continue;
				emitSingle(st, 3, k + bar + (pl.varBits & 3), 78 - ((k & 1) ? 8 : 0), 62);
			}
			continue;
		} else if (i >= 3) {
			/* 4本目以降コード: サビ/展開/ターンアラウンド限定のアクセントボイシング */
			if (pl.sec != 3 && pl.sec != 6 && pl.sec != 7 && pl.turn == 0) continue;
			int st0 = (i & 1) ? 12 : 24;
			if (waltz) { emitVoice(16, 8, 76, 2, 72); }
			else {
				emitVoice(st0, 6, 80, 2, 68);
				if (pl.turn >= 1 || pl.sec == 7) emitVoice(36 + ((i & 1) ? 6 : 0), 4, 84, 2, 60);
			}
			continue;
		}

		/* メインコードチャンネル (i == 0):
		   アレンジ性格 (ap.compMode) × スタイル固有リズム (kDef.chord / kStyleExtra.chordA/B) × 64小節展開 */
		if (ap.compMode == 6 || ((famC == FAM_BALLAD || style == SC_AS_PIANO || style == SC_AS_ACOUSTIC || style == SC_AS_ENKA) && (pl.sec == 0 || pl.sec == 4 || pl.sec == 5) && ap.compMode == 0)) {
			/* 分散カスケード／アルペジオ伴奏（SSW風ピアノ/ギター） */
			int step = (divC == 12) ? 4 : ((famC == FAM_BALLAD || famC == FAM_PAD) ? 6 : 3);
			if (pl.breakFeel == 1 && step < 6) step = 6;
			static const int ord[4][8] = {
				{ 0, 1, 2, 3, 2, 1, 2, 3 },
				{ 0, 2, 1, 3, 0, 2, 3, 1 },
				{ 0, 1, 2, 1, 3, 2, 1, 2 },
				{ 3, 2, 1, 0, 1, 2, 3, 2 }
			};
			const int row = (pl.sec + (pl.varBits & 3) + (patC & 3)) & 3;
			for (int st = 0, k = 0; st <= 44 && !c->overflow; st += step, k++) {
				if (pl.breakFeel == 1 && k > 0 && ((k + pl.varBits) % 3 == 2)) continue;
				int vel = ((st % 12) == 0) ? 92 : 76 - ((k & 1) ? 6 : 0);
				emitSingle(st, step > 3 ? step - 1 : 3, ord[row][k & 7], vel, 88);
			}
		} else if (ap.compMode == 8 || famC == FAM_PAD || ((famC == FAM_BALLAD || style == SC_AS_ORCH) && ap.compMode != 7)) {
			/* レガート・持続コード（セクションで呼吸と動きをつける） */
			if (b->root2 >= 0) {
				emitVoice(0, 22, 82, 4, 96);
				emitVoice(24, 22, 80, 4, 96);
			} else if (pl.sec == 3 || pl.sec == 7 || pl.turn >= 1) {
				emitVoice(0, 28, 84, 4, 96);
				emitVoice(30, 16, 78, 3, 94);
			} else {
				emitVoice(0, 44, 78, 4, 96);
			}
		} else if (ap.compMode == 3 || famC == FAM_REG || famC == FAM_SKA) {
			/* 裏打ちスカンク／オフビート強調 */
			static const int skA[4] = { 6, 18, 30, 42 };
			for (int k = 0; k < 4 && !c->overflow; k++) {
				if (pl.breakFeel == 1 && (k == 0 || k == 2)) continue;
				emitVoice(skA[k], 3, (k == 1 || k == 3) ? 104 : 94, 3, 46);
				if ((pl.sec == 3 || pl.sec == 7 || (pl.varBits & 1)) && k == 3 && !c->isFm)
					emitVoice(45, 2, 82, 2, 40); /* ダブルスカンクの跳ね */
			}
		} else if (isPowC || ap.compMode == 5 || famC == FAM_8 || famC == FAM_METAL) {
			/* ロック／メタル／パンク／8ビート／サーフ：パワー dyad 刻み＋強拍/シンコペアクセント */
			int step = (style == SC_AS_METAL && (pl.sec == 3 || pl.sec == 6 || pl.sec == 7)) ? 3 : 6;
			if (pl.breakFeel == 1) step = 12;
			uint16_t mask = (pl.phraseBar & 1) ? sx.chordB : sx.chordA;
			if (!mask) mask = 0x5555u;
			for (int st = 0; st <= 44 && !c->overflow; st += step) {
				int s16 = st / 3;
				if (step == 3 && !(mask & (uint16_t)(1u << (s16 & 15))) && (st % 6) != 0) continue;
				if (ap.callResp == 2 && (bar & 1) && st >= 24 && st < 42) continue;
				int accent = (st == 0 || st == 24 || (pl.turn >= 1 && st == 36));
				if ((pl.sec == 2 || pl.sec == 6) && (st == 18 || st == 42)) accent = 1;
				int vel = accent ? 114 : (((st % 12) == 0) ? 88 : 74);
				int du = accent ? (step > 3 ? 5 : 3) : (step > 3 ? 4 : 2);
				emitVoice(st, du, vel, isPowC ? 2 : 3, accent ? 84 : 58);
			}
		} else if (famC == FAM_JAZZ || famC == FAM_BLUES || famC == FAM_SHUF || famC == FAM_BOSSA || style == SC_AS_LOFI || ap.compMode == 2) {
			/* ジャズ／ブルース／ボサ／ボイスリード：64小節で毎小節リズムが変わるコンピング */
			static const int8_t jPat[8][4] = {
				{ 0, 20, -1, -1 }, { 0, 12, 32, -1 }, { 8, 24, 40, -1 }, { 0, 20, 36, -1 },
				{ 4, 20, 32, -1 }, { 0, 16, 28, 40 }, { 12, 32, -1, -1 }, { 0, 24, 42, -1 }
			};
			static const int8_t bPat[4][4] = {
				{ 0, 9, 18, 30 }, { 6, 18, 27, 39 }, { 0, 12, 21, 36 }, { 0, 9, 24, 33 }
			};
			int row = (bar + pl.sec + (pl.varBits & 7) + (patC & 3)) & 7;
			const int8_t* rp = (famC == FAM_BOSSA) ? bPat[row & 3] : jPat[row];
			for (int k = 0; k < 4 && rp[k] >= 0 && !c->overflow; k++) {
				if (pl.breakFeel == 1 && k >= 2) break;
				int st = rp[k];
				int nextSt = (k + 1 < 4 && rp[k + 1] >= 0) ? rp[k + 1] : 46;
				int du = nextSt - st - 1;
				if (du > 12) du = (pl.sec >= 3 || ap.compMode == 8) ? 14 : 8;
				if (du < 3) du = 3;
				emitVoice(st, du, (k == 0) ? 98 : 88, 4, (du <= 4) ? 55 : 82);
			}
		} else if (waltz || famC == FAM_WALTZ) {
			/* 3/4 ワルツ・ジャズワルツ */
			emitVoice(0, 12, 84, 3, 86);
			emitVoice(16, (pl.varBits & 1) ? 10 : 6, 94, 3, 72);
			emitVoice(32, (pl.varBits & 2) ? 12 : 6, 96, 3, 72);
		} else if (famC == FAM_FLOOR || famC == FAM_DISCO || famC == FAM_TRAP || famC == FAM_HOP) {
			/* ダンス／エレクトロ／ヒップホップ：スタイル固有 chord 格子＋ビルドアップ */
			uint16_t mask = Bits(sd.chord);
			if (pl.sec == 1 || pl.sec == 4) mask = (uint16_t)(mask | (sx.chordA & 0x5555u));
			else if (pl.sec == 3 || pl.sec == 7) mask = (uint16_t)(mask | sx.chordB);
			else if (pl.breakFeel == 2) mask = (uint16_t)(mask | 0x5555u);
			if (!mask) mask = 0x4444u;
			for (int s = 0; s < divC && !c->overflow; s++) {
				if (!(mask & (uint16_t)(1u << s))) continue;
				if (pl.breakFeel == 1 && s > 0 && (s & 3) != 0) continue;
				int st = s * unitC;
				int du = (ap.compMode == 7 || (s & 3) == 2) ? 3 : 6;
				emitVoice(st, du, ((s & 3) == 2) ? 104 : 90, 3, (du <= 3) ? 54 : 80);
			}
		} else {
			/* 16ビート／ファンク／シティポップ／アニソン／フュージョン／ラテン／チップチューン等：
			   kDef[style].chord + kStyleExtra.chordA/B を土台に、A/A3はSSW風の粒立ち単音カッティング、
			   サビ(C1/C2)やキメ(K/P)ではボイスリード和音カッティングへ展開する */
			uint16_t mDef = Bits(sd.chord);
			uint16_t mBar = ((pl.phraseBar + patC) & 1) ? sx.chordB : sx.chordA;
			uint16_t mask = (pl.sec == 0 || pl.sec == 4) ? (uint16_t)(mDef | (mBar & 0xAAAAu))
				: (uint16_t)(mDef | mBar);
			if (ap.compMode == 1 || style == SC_AS_16BEAT || style == SC_AS_FUNK || style == SC_AS_CITYPOP || style == SC_AS_CHIPTUNE) {
				/* 16分粒立ちパターンの変異（64小節内で毎回少しずつ変える） */
				static const uint16_t m16[8] = {
					0xEEEEu, 0xDB6Du, 0x6DB6u, 0xBBBBu, 0xAAAAu, 0xDDDDu, 0x7777u, 0x5AD6u
				};
				uint16_t alt = m16[(pl.sec + (pl.varBits & 7) + (patC & 3)) & 7];
				if (pl.sec == 0 || pl.sec == 4) mask = (uint16_t)(alt & ~1u);
				else mask = alt;
			}
			if (ap.compMode == 4 || pl.breakFeel == 1) mask = (uint16_t)(mask & 0x1111u);
			else if (ap.compMode == 7) mask = (uint16_t)(mask & 0x4924u);
			if (ap.callResp == 1 && (bar & 1)) mask = (uint16_t)(mask & 0xFF00u);
			else if (ap.callResp == 1) mask = (uint16_t)(mask & 0x00FFu);
			if (!mask) mask = mDef ? mDef : 0x4444u;

			const int useBlock = (ap.compMode == 7 || ap.compMode == 10 || pl.sec == 3 || pl.sec == 7 || pl.turn >= 2);
			for (int s = 0; s < divC && !c->overflow; s++) {
				if (!(mask & (uint16_t)(1u << s))) continue;
				int st = s * unitC;
				int sub = s & 3;
				int vel = 106 - sub * 13;
				if (vel < 62) vel = 62;
				if (useBlock && ((s & 3) == 0 || (s & 3) == 2))
					emitVoice(st, (ap.compMode == 7) ? 2 : 3, vel, 3, (ap.compMode == 7) ? 44 : 66);
				else
					emitSingle(st, (ap.compMode == 8) ? 4 : 2, s + bar + (pl.varBits & 3), vel, 64);
			}
		}
	}

	for (int i = 0; i < nPad && !c->overflow; i++) {
		if (b->section == SEC_INTRO) continue;
		if (pl.sec == 0 && pl.phraseBar < 4 && ag.dens == 0 && i == 0) continue;
		if (i == 2 && pl.sec < 2) continue; /* 3本目のパッドは前サビ・サビから登場 */
		if (i >= 3 && pl.sec != 3 && pl.sec != 5 && pl.sec != 7) continue; /* 4本目以降はサビ/ブリッジ専用 */
		int loP = 55 + (i == 1 ? 7 : i == 2 ? 12 : i >= 3 ? 16 : 0) + (pl.regShift > 0 ? 5 : 0);
		int hiP = 81 + (i == 1 ? 7 : i == 2 ? 10 : i >= 3 ? 12 : 0) + (pl.regShift > 0 ? 5 : 0);
		if (hiP > 96) hiP = 96;
		auto holdPad = [&](int st, int du) {
			int pc, ty;
			ChordAt(b, st, &pc, &ty);
			int tones[4];
			int nt = BuildChordVoicing(pc, ty, 0, pl.extLvl, s_song.prevPad, loP, hiP, tones, 4);
			if (nt < 1) return;
			int nv = c->isFm ? 1 : ((i >= 2) ? (nt < 2 ? nt : 2) : (nt < 3 ? nt : 3));
			int sum = 0;
			for (int v = 0; v < nv; v++) {
				int idx = c->isFm ? ((v + i + bar) % nt) : ((v + (i >= 2 ? 1 : 0)) % nt);
				int vv = ShapeVel(62 - i * 4 - v * 3, pl, ap, st);
				PutFrac(c, padCh[i], b, st, du, tones[idx], vv, 100);
				sum += tones[idx];
			}
			if (i == 0 && nv > 0) s_song.prevPad = sum / nv;
		};
		if (i >= 3) {
			/* 4本目以降: 後半から膨らむクワイア/ストリングス層 */
			holdPad(12, 34);
		} else {
			int du = (b->root2 >= 0) ? 22 : 44;
			holdPad(0, du);
			if (b->root2 >= 0) holdPad(24, 22);
		}
	}

	if (nArp > 0 && arpCh && !(thin && form < 1 && pl.sec == 0 && (bar & 1))) {
		const int arpFam = StyleFam(style);
		const int divA = (sd.div == 12) ? 12 : 16;
		const int unitA = 48 / divA;
		for (int i = 0; i < nArp && !c->overflow; i++) {
			if (i == 1 && pl.sec == 0 && (bar & 1) == 0) continue; /* 2本目は1本目と密度差をつける */
			if (i == 2 && pl.sec < 2) continue;
			if (i >= 3 && pl.sec != 3 && pl.sec != 6 && pl.sec != 7) continue;
			if (ap.callResp == 2 && (bar & 1) == 0 && i == 0 && pl.sec < 6) continue;
			int shape = (pl.sec + (pl.varBits & 7) + (int)sx.arpMode + i * 2) % 6;
			if (waltz) {
				for (int k = 0; k < 3 && !c->overflow; k++) {
					int st = k * 16 + ((i & 1) ? 8 : 0) + ((i >= 2) ? 4 : 0);
					if (st > 44) continue;
					int pcA, tyA;
					ChordAt(b, st, &pcA, &tyA);
					int tones[4];
					int nt = BuildChordVoicing(pcA, tyA, 0, pl.extLvl, s_song.prevArp, 55 + i * 5, 88 + i * 2, tones, 4);
					int m = nt > 0 ? tones[(k + shape) % nt] : 64;
					s_song.prevArp = m;
					PutFrac(c, arpCh[i], b, st, 6, m, ShapeVel(84 - i * 6, pl, ap, st), 62);
				}
				continue;
			}
			uint16_t am = ((bar + i) & 1) ? sx.arpB : sx.arpA;
			if (!am) am = FamMask(arpFam, s_song.pat + i * 2, bar, 2);
			if (pl.breakFeel == 1) am = (uint16_t)(am & 0x5555u);
			else if (pl.sec == 3 || pl.sec == 7 || ap.compMode == 6) am = (uint16_t)(am | (sx.arpA & 0xAAAAu));
			if (i == 1) {
				/* 2本目: 裏位相＋間引きで対話するアルペジオ */
				am = (uint16_t)((((am << 2) | (am >> 14)) & 0xAAAAu));
			} else if (i == 2) {
				/* 3本目: 8分中心のハイ・スパークル */
				am = (uint16_t)((((am << 4) | (am >> 12)) & 0x5555u));
			} else if (i >= 3) {
				/* 4本目以降: 後半ビート集中シマー */
				am = (uint16_t)((((am << (i * 3)) | (am >> (16 - ((i * 3) & 15)))) & 0xFF00u));
			}
			if (ap.callResp == 1) am = (uint16_t)(am & ((bar & 1) ? 0x00FFu : 0xFF00u));
			if (!am) am = (i & 1) ? 0x2222u : 0x1111u;
			int k = 0;
			for (int s = 0; s < divA && !c->overflow; s++) {
				if (!(am & (uint16_t)(1u << s))) continue;
				int st = s * unitA;
				int pcA, tyA;
				ChordAt(b, st, &pcA, &tyA);
				int loA = 55 + i * 5 + (ap.octShift > 0 ? 5 : 0);
				int hiA = 86 + i * 3;
				if (loA > 78) loA = 78;
				if (hiA > 98) hiA = 98;
				int tones[5];
				int nt = BuildChordVoicing(pcA, tyA, 0, pl.extLvl, s_song.prevArp, loA, hiA, tones, 5);
				if (nt < 1) continue;
				static const int dirPat[6][4] = {
					{ 0, 1, 2, 3 }, { 3, 2, 1, 0 }, { 0, 2, 1, 3 },
					{ 0, 1, 3, 2 }, { 1, 2, 3, 0 }, { 2, 0, 3, 1 }
				};
				int m = tones[dirPat[shape][k & 3] % nt];
				s_song.prevArp = m;
				int du = (arpFam == FAM_BALLAD || arpFam == FAM_PAD || ap.compMode == 8) ? 8 : (unitA > 3 ? 3 : 2);
				int nextS = divA;
				for (int s2 = s + 1; s2 < divA; s2++) {
					if (am & (uint16_t)(1u << s2)) { nextS = s2; break; }
				}
				int maxDu = (nextS - s) * unitA - 1;
				if (maxDu < 2) maxDu = 2;
				if (du > maxDu) du = maxDu;
				int vv = ShapeVel(84 - i * 6 - ((k & 1) ? 8 : 0), pl, ap, st);
				PutFrac(c, arpCh[i], b, st, du, m, vv, (ap.compMode == 7) ? 46 : 64);
				k++;
			}
		}
	}

	for (int i = 0; i < nHarm && !c->overflow; i++) {
		if (melHarm == 1 && i == 0) {
			sing(harmCh[i], &s_song.prevHarm, 60, 88, 0, 82);
			continue;
		}
		if (melHarm == 2 && i == 0) {
			/* 高formのサビでは3度下ハモリでメロディを分厚く支える */
			sing(harmCh[i], &s_song.prevHarm, 57, 86, 2, 78);
			continue;
		}
		if (i == 2 && pl.sec < 3) continue;
		if (i >= 3 && pl.sec != 3 && pl.sec != 7) continue;
		if (pl.sec == 0 && pl.phraseBar < 4 && thin && i == 0) continue;
		int hat[6];
		int nh = HitTimes(bar + 3 + i * 2 + (pl.varBits & 3), 64, (fig == 3) ? 0 : fig, s_song.pat + 2 + i * 3, waltz, hat, (quiet || pl.breakFeel == 1 || i >= 2) ? 2 : 4, 3);
		for (int k = 0; k < nh && !c->overflow; k++) {
			int du = (k + 1 < nh) ? (hat[k + 1] - hat[k] - 1) : (46 - hat[k]);
			if (du > 18) du = 18;
			if (du < 4) du = 4;
			int pcH, tyH;
			ChordAt(b, hat[k], &pcH, &tyH);
			int tones[4];
			int nt = BuildChordVoicing(pcH, tyH, 0, pl.extLvl, s_song.prevHarm, 60 + i * 4, 90 + (i > 0 ? 4 : 0), tones, 4);
			if (nt < 1) continue;
			int m = tones[(k + i + (pl.varBits & 3)) % nt];
			s_song.prevHarm = m;
			PutFrac(c, harmCh[i], b, hat[k], du, m, ShapeVel(78 - i * 5, pl, ap, hat[k]), 84);
			if (!c->isFm && nt > 1 && i == 0 && (pl.sec >= 2 || form >= 30)) {
				int m2 = tones[(k + i + 1 + (pl.varBits & 3)) % nt];
				if (m2 != m) PutFrac(c, harmCh[i], b, hat[k], du, m2, ShapeVel(68 - i * 5, pl, ap, hat[k]), 80);
			}
		}
	}

	if (genMel && melodyCh >= 0 && form >= 1 && !(b->section == SEC_INTRO && form < 25)) {
		int src = (b->section == SEC_BODY) ? melBar : 0;
		if (src < 0 || src >= 256) src = 0;
		int melN = s_song.melCount[src];
		if (b->section == SEC_INTRO && melN > 2) melN = 2;
		if (melN > 12) melN = 12;
		int loM = s_song.cast.lo;
		int hiM = s_song.cast.hi;
		if (pl.sec == 3 || pl.sec == 7) { loM += 2; hiM += 3; }
		if (hiM > 96) hiM = 96;
		for (int k = 0; k < melN && !c->overflow; k++) {
			int mst = s_song.mel[src][k].at;
			int mdu = s_song.mel[src][k].du;
			int mlg = s_song.mel[src][k].leg;
			int raw = (int)s_song.mel[src][k].deg;
			int pcM, tyM;
			ChordAt(b, mst, &pcM, &tyM);
			int strong = ((mst % 12) == 0) || k == 0 || k == melN - 1 || mdu >= 12;
			int m;
			if (b->section == SEC_BODY && pl.turn == 3 && k == melN - 1)
				m = MelPitch(tonic, s_song.cast.scale, pcM, tyM, 0, 1, s_song.prevMel, loM, hiM);
			else
				m = MelPitch(tonic, s_song.cast.scale, pcM, tyM, raw, strong, s_song.prevMel, loM, hiM);
			s_song.prevMel = m;
			int vel = mlg ? 106 : 94;
			if (pl.sec == 3 || pl.sec == 7) vel += 8;
			else if (pl.sec == 0) vel -= 6;
			vel = ShapeVel(vel, pl, ap, mst);
			s_putForce = 1;
			PutFrac(c, melodyCh, b, mst, mdu, m, vel, mlg ? 92 : 76);
			if (!c->isFm && s_song.mel[src][k].bend && sx.bendMode)
				emitBend(melodyCh, mst, (sx.bendMode == 3) ? -14 : -10);
			if (!c->isFm && (sx.ccFlags & CCF_MOD) && mdu >= 18) {
				Push(c, b->t0 + (uint32_t)(mst * b->len / 48 + b->len / 8), melodyCh, SC_EV_CC, 1, 38, 0, 0);
				Push(c, b->t0 + (uint32_t)((mst + mdu) * b->len / 48), melodyCh, SC_EV_CC, 1, 0, 0, 0);
			}
			s_putForce = 0;
		}
	}

	s_putForce = 1;
	for (int i = 0; i < nCnt && !c->overflow; i++) {
		if (pl.turn == 3 || b->section == SEC_END) {
			int pc, ty;
			ChordAt(b, 0, &pc, &ty);
			int tones[4];
			int nt = BuildChordVoicing(pc, ty, 0, pl.extLvl, s_song.prevCnt, 55 + i * 4, 84 + i * 3, tones, 4);
			int m = nt > 0 ? tones[i % nt] : 64;
			s_song.prevCnt = m;
			PutFrac(c, cntCh[i], b, 0, 24, m, ShapeVel(76 - i * 4, pl, ap, 0), 84);
			continue;
		}
		if (melCnt && i == 0) {
			sing(cntCh[i], &s_song.prevCnt, 55, 84, 1, 80);
			continue;
		}
		if (i == 1 && pl.sec == 0 && (bar & 1)) continue;
		if (i == 2 && pl.sec < 2) continue;
		if (i >= 3 && pl.sec != 3 && pl.sec != 6 && pl.sec != 7) continue;
		int cat[6];
		int nc = HitTimes(bar + 5 + i * 3 + (pl.varBits & 3), 64, fig, s_song.pat + 4 + i * 2, waltz, cat, (pl.breakFeel == 1 || i >= 2) ? 2 : 4, 4);
		for (int k = 0; k < nc && !c->overflow; k++) {
			int du = (k + 1 < nc) ? (cat[k + 1] - cat[k] - 1) : 10;
			if (du > 12) du = 12;
			if (du < 3) du = 3;
			int pc, ty;
			ChordAt(b, cat[k], &pc, &ty);
			int tones[4];
			int nt = BuildChordVoicing(pc, ty, 0, pl.extLvl, s_song.prevCnt, 52 + i * 5, 86 + i * 3, tones, 4);
			if (nt < 1) continue;
			int m = tones[(k + i + (pl.varBits & 3)) % nt];
			s_song.prevCnt = m;
			PutFrac(c, cntCh[i], b, cat[k], du, m, ShapeVel(76 - i * 4, pl, ap, cat[k]), 78);
		}
	}
	for (int i = 0; i < nStab && !c->overflow; i++) {
		const int famS = StyleFam(style);
		const int patS = s_song.pat & 15;
		/* 抜きセクションやA1前半はスタブを控えて、サビ・ターンアラウンドで効かせる */
		if (pl.breakFeel == 1 && pl.turn == 0 && ap.compMode != 7) continue;
		if (i == 1 && pl.sec < 2 && pl.turn == 0) continue;
		if (i >= 2 && pl.sec != 3 && pl.sec != 6 && pl.sec != 7 && pl.turn < 2) continue;
		if (ap.callResp == 2 && (bar & 1) == 0 && pl.turn == 0) continue;

		int ats[4];
		int ns = 0;
		int sdu = 3;
		int vel = 108;
		if (pl.turn == 3 || b->section == SEC_END) {
			ats[0] = 0; ns = 1; sdu = 8; vel = 102;
		} else if (famS == FAM_BALLAD || famS == FAM_PAD || famS == FAM_MARCH) {
			if (pl.turn == 0 && pl.phraseBar != 0 && patS != 15) continue;
			ats[0] = (pl.turn >= 1) ? 36 : 0; ns = 1; sdu = 8; vel = 88;
		} else if (famS == FAM_JAZZ || famS == FAM_BLUES || famS == FAM_SHUF || famS == FAM_BOSSA) {
			static const int jSt[4][2] = { { 18, 42 }, { 8, 32 }, { 20, 44 }, { 12, 36 } };
			int row = (bar + pl.sec + (pl.varBits & 3)) & 3;
			ats[0] = jSt[row][0]; ats[1] = jSt[row][1];
			ns = (pl.sec >= 3 || ag.dens >= 2 || pl.turn >= 1) ? 2 : 1;
			sdu = 4; vel = 96;
		} else if (famS == FAM_8 || famS == FAM_METAL || famS == FAM_TANGO) {
			if (pl.sec == 0 && (bar & 1) && pl.turn == 0) continue;
			ats[0] = (pl.varBits & 1) ? 24 : 0;
			ns = 1; sdu = 5; vel = 114;
			if (pl.sec >= 3 || pl.turn >= 1 || ag.dens >= 3) { ats[1] = (pl.turn >= 1) ? 42 : 36; ns = 2; }
		} else if (famS == FAM_FUNK || famS == FAM_REG || famS == FAM_SKA) {
			static const int fSt[4][3] = { { 6, 18, 42 }, { 18, 30, 42 }, { 6, 30, 39 }, { 12, 30, 42 } };
			int row = (bar + pl.sec + (pl.varBits & 3)) & 3;
			ats[0] = fSt[row][0]; ats[1] = fSt[row][1]; ats[2] = fSt[row][2];
			ns = (pl.sec >= 3 || ap.compMode == 7) ? 3 : ((ag.dens == 0) ? 1 : 2);
			sdu = 3; vel = 110;
		} else if (famS == FAM_WALTZ) {
			ats[0] = 16; ats[1] = 32; ns = (pl.sec >= 2) ? 2 : 1; sdu = 4; vel = 98;
		} else {
			static const int pSt[4][3] = { { 15, 39, 45 }, { 18, 42, 30 }, { 9, 33, 42 }, { 21, 39, 45 } };
			int row = (bar + pl.sec + (pl.varBits & 3) + (patS & 3)) & 3;
			ats[0] = pSt[row][0]; ats[1] = pSt[row][1]; ats[2] = pSt[row][2];
			ns = (pl.sec >= 3 || pl.turn >= 1) ? 2 : 1;
			if (ap.compMode == 7 || ap.compMode == 10 || ag.dens >= 3) ns = 3;
			sdu = 3; vel = 108;
		}
		if (i > 0) {
			for (int s = 0; s < ns; s++) {
				ats[s] = (ats[s] + i * 6) % 42;
			}
		}
		int nv = c->isFm ? 1 : ((pl.sec >= 3 || ap.compMode == 7) ? 3 : 2);
		for (int s = 0; s < ns && !c->overflow; s++) {
			int st = ats[s];
			if (st < 0 || st > 44) continue;
			int pc, ty;
			ChordAt(b, st, &pc, &ty);
			int tones[4];
			int nn = BuildChordVoicing(pc, ty, sd.power, pl.extLvl, 78 + i * 4, 68 + i * 3, 94, tones, 4);
			if (nn < 1) continue;
			for (int v = 0; v < nv && v < nn; v++) {
				int idx = c->isFm ? ((bar + s + i) % nn) : v;
				PutFrac(c, stabCh[i], b, st, sdu, tones[idx], ShapeVel(vel - v * 7 - i * 5, pl, ap, st), 42);
			}
		}
	}
	for (int i = 0; i < nColor && !c->overflow; i++) {
		if (pl.turn == 3 || b->section == SEC_END) {
			int pc, ty;
			ChordAt(b, 0, &pc, &ty);
			int tones[4];
			int nn = BuildChordVoicing(pc, ty, 0, pl.extLvl, s_song.prevCol, 74, 96, tones, 4);
			int m = nn > 0 ? tones[nn - 1] : 79;
			s_song.prevCol = m;
			PutFrac(c, colorCh[i], b, 0, 24, m, ShapeVel(74, pl, ap, 0), 82);
			continue;
		}
		if (pl.sec == 0 && (bar & 1) && ag.dens < 2 && i == 0) continue;
		if (i == 1 && pl.sec < 3) continue;
		if (i >= 2 && pl.sec != 3 && pl.sec != 6 && pl.sec != 7) continue;
		int colAt[6];
		int ncol = HitTimes(bar + 2 + i * 3 + (pl.varBits & 3), 64, fig, s_song.pat + 5 + i * 2, waltz, colAt, (pl.sec >= 3 || ag.dens >= 2) ? (i >= 2 ? 2 : 3) : 2, 3);
		for (int k = 0; k < ncol && !c->overflow; k++) {
			int cdu = (form >= 60) ? 4 : 6;
			int lim = (k + 1 < ncol) ? (colAt[k + 1] - colAt[k] - 1) : (46 - colAt[k]);
			if (lim < 2) lim = 2;
			if (cdu > lim) cdu = lim;
			int pc, ty;
			ChordAt(b, colAt[k], &pc, &ty);
			int tones[4];
			int nn = BuildChordVoicing(pc, ty, 0, pl.extLvl, s_song.prevCol, 74 + i * 3, 98, tones, 4);
			if (nn < 1) continue;
			int m = tones[(k + i + (pl.varBits & 3)) % nn];
			s_song.prevCol = m;
			PutFrac(c, colorCh[i], b, colAt[k], cdu, m, ShapeVel(76 - i * 4, pl, ap, colAt[k]), 64);
		}
	}
	s_putForce = 0;
	(void)tonic;
	(void)rd;
	(void)nxt;
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
	const int useProg = opt->useProg || (totalNotes <= 0);
	const int rewrite = (opt->mode == SC_AM_REWRITE);
	s_arrGsMap = opt->gsMap;
	s_arrPat = opt->patVar < 0 ? 0 : opt->patVar;
	int bodyBars = 16;
	int melBars = (opt->melBars >= 16 && opt->melBars <= 256) ? opt->melBars : 0;
	{
		int loop = opt->loopBars;
		if (loop < 16 || loop > 64) loop = StyleLoopBars(opt->style);
		s_loopBars = loop;
		if (melBars <= 0) melBars = loop;
		int times = opt->loopTimes;
		if (times < 1) times = 1;
		if (times > 20) times = 20;
		bodyBars = loop * times;
		if (opt->genBars > bodyBars) bodyBars = opt->genBars;
		if (opt->form >= 1 && opt->melBars >= 16 && ((opt->targetMask & 1u) != 0) && bodyBars < melBars)
			bodyBars = melBars;
		if (bodyBars < 16) bodyBars = 16;
		if (bodyBars > 1280) bodyBars = 1280;
		/* 64小節以上でもメロディが16/32小節で機械ループしないよう本体小節数へ合わせる */
		if (opt->melBars < 16 && melBars < bodyBars)
			melBars = (bodyBars > 256) ? 256 : bodyBars;
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
	if (cur >= 0 && cur < lim && cur < 32 && notes[cur] >= 8 && notes[cur] < 1000000) {
		int ratio = tickCount[cur] ? (polyTicks[cur] * 100) / tickCount[cur] : 0;
		int avg = sumPitch[cur] / notes[cur];
		if (ratio < 50 && avg >= 58) melodyCh = cur;
	}
	if (melodyCh < 0) {
		int bestScore = -1;
		for (int ch = 0; ch < lim && ch < 32; ch++) {
			if (notes[ch] < 8 || notes[ch] >= 1000000) continue;
			int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
			if (ratio >= 50) continue;
			int avg = sumPitch[ch] / notes[ch];
			/* 低い単音はベース。メロディ扱いすると BGM の線が足されない。 */
			if (avg < 58) continue;
			int score = notes[ch] * 128 + avg;
			if (score > bestScore) { bestScore = score; melodyCh = ch; }
		}
	}
	if (chordSrc >= 0 && chordSrc == melodyCh) {
		int ratio = tickCount[chordSrc] ? (polyTicks[chordSrc] * 100) / tickCount[chordSrc] : 0;
		if (ratio >= 50) melodyCh = -1;
		else chordSrc = -1;
	}
	int form = opt->form;
	if (form < 0) form = 0;
	if (form > 100) form = 100;
	/* 既にメロディがある曲は上書きしない。無いときだけ、右へ倒すほどメロディ用の空きを取る。 */
	const int userHadMelody = (melodyCh >= 0) ? 1 : 0;
	int checkedLead = 0;
	for (int ch = 0; ch < lim && ch < 32; ch++) {
		if (!((opt->targetMask >> ch) & 1u)) continue;
		if ((opt->drumMask >> ch) & 1u) continue;
		checkedLead = 1;
		break;
	}
	/* ch1 にチェックが無いときは、スライダーを上げてもメロディチャンネルを作らない。 */
	if (0 && form >= 22 && !userHadMelody && !checkedLead) {
		const int preferLead[] = { 0, 11, 12, 13, 14, 4, 5, 2, 3 };
		const int preferLeadFm[] = { 0, 1, 7, 8, 2, 9 };
		int lead = PickEmpty(notes, lim, isFm, chordSrc, bassHave, -1,
			isFm ? preferLeadFm : preferLead, isFm ? 6 : 9);
		if (lead >= 0) melodyCh = lead;
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
	if (form < 1 || userHadMelody) {
		genMel = 0;
		if (!userHadMelody && melodyCh >= 0 && melodyCh < 32 && notes[melodyCh] == 0)
			melodyCh = -1;
	} else if (melodyCh == 0 && ((opt->targetMask & 1u) != 0)) {
		/* チェック済みの空 ch1 だけがメロディ。和音構成音で、他のパートには書かない。 */
		genMel = 1;
	} else {
		genMel = 0;
		if (!userHadMelody && melodyCh >= 0 && melodyCh < 32 && notes[melodyCh] == 0)
			melodyCh = -1;
	}

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
	int canLead = (genMel && melodyCh >= 0) ? 1 : 0;
	if (!canLayer && !canMel && !canLead) {
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
	/* スライダーで足す空のメロディ ch は、まだ音が無い。
	   それを「曲の穴」にすると全小節が間奏になり、ハイハットしか残らない。 */
	const int leadHeard = (lead >= 0 && lead0 != 0xFFFFFFFFu);
	if (lead0 == 0xFFFFFFFFu) { lead0 = introT1; lead1 = noteEnd; }
	if (noteEnd == 0) {
		/* 音符が無いときは C4（主音）が置いてあるものとして、指定小節ぶん書く。 */
		noteEnd = introT1 + (uint32_t)(bodyBars * ScStaffMeterTicksPerMeasure(defN, defD));
		lead0 = introT1; lead1 = noteEnd;
	} else {
		/* ループの長さ × 回数。24小節を3回なら本体は72小節。既存がそれより長ければ切らない。 */
		uint32_t want = introT1 + (uint32_t)(bodyBars * ScStaffMeterTicksPerMeasure(defN, defD));
		if (noteEnd < want) noteEnd = want;
		if (lead1 < want) lead1 = want;
	}
	{
		/* 本体はループを指定回数ぶん。短い1小節の繰り返しで終わらせない。 */
		int blMin = ScStaffMeterTicksPerMeasure(defN, defD);
		if (blMin < 1) blMin = 1;
		uint32_t minEnd = introT1 + (uint32_t)(bodyBars * blMin);
		if (noteEnd < minEnd) noteEnd = minEnd;
		if (lead1 < minEnd) lead1 = minEnd;
	}

	uint32_t breakT0 = 0xFFFFFFFFu, breakT1 = 0;
	int naturalBreak = 0;
	if (doBreak) {
		int gap = 0;
		if (leadHeard) {
			int heard = 0;
			uint32_t t = 0;
			while (t < lead1 && gap == 0) {
				int nM = defN, dM = defD;
				ScStaffMeterAtTick(ev, *evCount, t, defN, defD, &nM, &dM);
				int bl = ScStaffMeterTicksPerMeasure(nM, dM);
				uint32_t t1 = t + (uint32_t)bl;
				if (t >= lead0 && t < lead1) {
					if (BarHas(ev, *evCount, lead, t, t1)) heard = 1;
					else if (heard) gap = 1;
				}
				t = t1;
				if (bl <= 0) break;
			}
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
	BuildSongPlan(bars, nBars, opt->style, patVar, variation, opt->defKey, useProg);
	SetHeadTempo(&ctx, StyleBpm(opt->style), rewrite, opt->docTempoT);
	{
		const int stIdx = (opt->style >= 0 && opt->style < SC_AS_COUNT) ? opt->style : 0;
		s_styleRev = kStyleExtra[stIdx].rev;
		s_styleCho = kStyleExtra[stIdx].cho;
		ResetPartPans();
		int spread[32], nSp = 0;
		auto addSp = [&](const int* arr, int n, int skip0) {
			for (int k = skip0; k < n && nSp < 32; k++)
				if (arr[k] >= 0 && arr[k] < 32) spread[nSp++] = arr[k];
		};
		addSp(bassCh, nBass, 1);
		addSp(chordCh, nChord, 0);
		addSp(padCh, nPad, 0);
		addSp(arpCh, nArp, 0);
		addSp(harmCh, nHarm, 0);
		addSp(cntCh, nCnt, 0);
		addSp(stabCh, nStab, 0);
		addSp(colorCh, nColor, 0);
		static const int kPanOff[16] = { 18, 28, 12, 34, 22, 30, 14, 38, 26, 16, 36, 20, 32, 10, 40, 24 };
		for (int k = 0; k + 1 < nSp; k += 2) {
			int off = kPanOff[(k / 2) & 15];
			s_partPan[spread[k]] = (uint8_t)(64 - off);
			s_partPan[spread[k + 1]] = (uint8_t)(64 + off);
		}
		if (nSp & 1) s_partPan[spread[nSp - 1]] = 64;
		int dSp = 0;
		for (int i = 1; i < nDrum; i++) {
			if (drumChs[i] < 0 || drumChs[i] >= 32) continue;
			if (i + 1 < nDrum) {
				int off = kPanOff[(dSp / 2) & 15];
				s_partPan[drumChs[i]] = (uint8_t)((dSp & 1) ? (64 + off) : (64 - off));
			} else if (dSp & 1) {
				int prev = drumChs[i - 1];
				int off = 22;
				if (prev >= 0 && prev < 32) s_partPan[prev] = (uint8_t)(64 - off);
				s_partPan[drumChs[i]] = (uint8_t)(64 + off);
			} else {
				s_partPan[drumChs[i]] = 64;
			}
			dSp++;
		}
	}
	for (int i = 0; i < nDrum; i++) EnsureDrum(&ctx, opt->ui, drumChs[i], drumChSlot[i], anySolo, (i == 0) ? st.progKit : 0, haveAcc);
	for (int i = 0; i < nBass; i++) EnsureMelodic(&ctx, opt->ui, bassCh[i], 0, RoleLayerProg(opt->style, patVar, 0, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nChord; i++) EnsureMelodic(&ctx, opt->ui, chordCh[i], 1, RoleLayerProg(opt->style, patVar, 1, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nPad; i++) EnsureMelodic(&ctx, opt->ui, padCh[i], 2, RoleLayerProg(opt->style, patVar, 2, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nArp; i++) EnsureMelodic(&ctx, opt->ui, arpCh[i], 1, RoleLayerProg(opt->style, patVar, 3, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nHarm; i++) EnsureMelodic(&ctx, opt->ui, harmCh[i], 2, RoleLayerProg(opt->style, patVar, 4, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nCnt; i++) EnsureMelodic(&ctx, opt->ui, cntCh[i], 2, RoleLayerProg(opt->style, patVar, 5, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nStab; i++) EnsureMelodic(&ctx, opt->ui, stabCh[i], 1, RoleLayerProg(opt->style, patVar, 6, i, st.progAll), anySolo, haveAcc);
	for (int i = 0; i < nColor; i++) EnsureMelodic(&ctx, opt->ui, colorCh[i], 2, RoleLayerProg(opt->style, patVar, 7, i, st.progAll), anySolo, haveAcc);
	if (genMel && melodyCh >= 0)
		EnsureMelodic(&ctx, opt->ui, melodyCh, 2, LeadProg(opt->style), anySolo, 0);

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

	int introN = 0, breakN = 0, endN = 0;
	for (int i = 0; i < nBars; i++) {
		if (bars[i].section == SEC_INTRO) introN++;
		else if (bars[i].section == SEC_BREAK) breakN++;
		else if (bars[i].section == SEC_END) endN++;
	}
	int introI = 0, breakI = 0, endI = 0, bodyI = 0;
	MotifBegin();
	const int totNormal = nBass + nChord + nPad + nArp + nHarm + nCnt + nStab + nColor;
	LineBegin(opt->style, patVar, variation, form, melBars, totNormal == 1 ? 1 : 0, totNormal == 2 ? 1 : 0);
	st.swing = s_song.cast.swing ? 1 : 0;
	/* 外すのは、スライダーを最大にしても入り切らないパートだけ。
	   今の位置で鳴っていない役のチェックは残す。上げたときに戻る。 */
	{
		const int famNow = StyleFam(opt->style);
		const int cap = StyleCap(famNow, patVar, 100);
		const int quiet = StyleQuiet(opt->style) || famNow == FAM_PAD || famNow == FAM_BALLAD;
		int spent = 0;
		int dKeep = 0;
		s_wroteDrum = 0;
		for (int i = 0; i < nDrum; ) {
			int est = 0;
			if (dKeep == 0)
				est = quiet ? 6 : (famNow == FAM_FLOOR || famNow == FAM_DNB || famNow == FAM_METAL || famNow == FAM_SAMBA) ? 14 : 10;
			else
				est = quiet ? 0 : 4;
			if (!explicitTargets && (est < 1 || (spent + est > cap && dKeep > 0))) {
				for (int k = i + 1; k < nDrum; k++) {
					drumChs[k - 1] = drumChs[k];
					drumChSlot[k - 1] = drumChSlot[k];
				}
				nDrum--;
				continue;
			}
			s_wroteDrum |= (1u << drumChs[i]);
			spent += est;
			dKeep++;
			i++;
		}
		chDrum = nDrum > 0 ? drumChs[0] : -1;
		auto dropCh = [](int* arr, int* n, int ch) {
			int w = 0;
			for (int i = 0; i < *n; i++) if (arr[i] != ch) arr[w++] = arr[i];
			*n = w;
		};
		auto estOf = [&](int ch) -> int {
			if (genMel && ch == melodyCh) return 4;
			for (int i = 0; i < nBass; i++) if (bassCh[i] == ch) return (s_song.cast.bass == 1 || s_song.cast.bass == 2) ? 4 : 2;
			for (int i = 0; i < nChord; i++) if (chordCh[i] == ch) return 4;
			for (int i = 0; i < nPad; i++) if (padCh[i] == ch) return 2;
			for (int i = 0; i < nArp; i++) if (arpCh[i] == ch) return quiet ? 3 : 6;
			for (int i = 0; i < nHarm; i++) if (harmCh[i] == ch) return 4;
			for (int i = 0; i < nCnt; i++) if (cntCh[i] == ch) return 2;
			for (int i = 0; i < nStab; i++) if (stabCh[i] == ch) return quiet ? 0 : 1;
			for (int i = 0; i < nColor; i++) if (colorCh[i] == ch) return 1;
			return 0;
		};
		auto coreCh = [&](int ch) -> int {
			if (nBass > 0 && bassCh[0] == ch) return 1;
			if (nChord > 0 && chordCh[0] == ch) return 1;
			if (nPad > 0 && padCh[0] == ch) return 1;
			if (genMel && melodyCh == ch) return 1;
			return 0;
		};
		for (int ch = 0; ch < 32; ch++) {
			int est = estOf(ch);
			if (est < 1) continue;
			if (!explicitTargets && spent + est > cap && !coreCh(ch)) {
				dropCh(bassCh, &nBass, ch);
				dropCh(chordCh, &nChord, ch);
				dropCh(padCh, &nPad, ch);
				dropCh(arpCh, &nArp, ch);
				dropCh(harmCh, &nHarm, ch);
				dropCh(cntCh, &nCnt, ch);
				dropCh(stabCh, &nStab, ch);
				dropCh(colorCh, &nColor, ch);
				if (melodyCh == ch) { genMel = 0; melodyCh = -1; }
				continue;
			}
			if (spent < cap) spent += est;
		}
		auto keepMel = [&](int ch) { if (ch >= 0 && ch < 32) s_wroteMel |= (1u << ch); };
		for (int i = 0; i < nBass; i++) keepMel(bassCh[i]);
		for (int i = 0; i < nChord; i++) keepMel(chordCh[i]);
		for (int i = 0; i < nPad; i++) keepMel(padCh[i]);
		for (int i = 0; i < nArp; i++) keepMel(arpCh[i]);
		for (int i = 0; i < nHarm; i++) keepMel(harmCh[i]);
		for (int i = 0; i < nCnt; i++) keepMel(cntCh[i]);
		for (int i = 0; i < nStab; i++) keepMel(stabCh[i]);
		for (int i = 0; i < nColor; i++) keepMel(colorCh[i]);
		if (genMel) keepMel(melodyCh);
		chBass = nBass > 0 ? bassCh[0] : -1;
		chChord = nChord > 0 ? chordCh[0] : -1;
		chHarm = nHarm > 0 ? harmCh[0] : -1;
	}
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
		int breakAt = 0;
		int endFromEnd = 0;
		if (b.section == SEC_BODY) phase = BodyPhase(patVar, bodyI);
		else if (b.section == SEC_INTRO) introAt = introI;
		else if (b.section == SEC_BREAK) breakAt = breakI;
		else if (b.section == SEC_END) endFromEnd = endN - endI - 1;
		ApplyForm(b.section, last, phase, div, introAt, introN, endFromEnd,
			drm, bass, &chordMask, &chordSlots, &skipChord, IntroHot(opt->style));
		if (b.section == SEC_INTRO) introI++;
		else if (b.section == SEC_BREAK) breakI++;
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
		if (form >= 55 && allowBar && !skipChord && b.section != SEC_BREAK
			&& !(b.section == SEC_END && endFromEnd <= 0)) {
			/* アレンジごとの格子は残し、足りないときだけその文字の刻みを足す。 */
			static const uint16_t kAdd[16] = {
				0x4444, 0x2222, 0x8888, 0x4924, 0x5555, 0xAAAA, 0x1248, 0x8421,
				0x2492, 0x9249, 0x3333, 0x6666, 0x9999, 0x0F0F, 0xF0F0, 0x5A5A
			};
			int hits = 0;
			for (int s = 0; s < nSlot && s < 16; s++)
				if (compM & (uint16_t)(1u << s)) hits++;
			if (hits < 2) {
				uint16_t add = kAdd[patVar & 15];
				if (form < 75) add = (uint16_t)(add & 0x1111);
				else if (form < 90) add = (uint16_t)(add & 0x5555);
				compM = (uint16_t)((compM | add) & slotLow);
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
		/* 譜面帳が空の役割でも、チェックした ch には書く。3パートで止めない。 */
		if (allowBar && !skipChord && !(b.section == SEC_END && endFromEnd <= 0)) {
			auto fillRole = [&](uint16_t& m, int nCh, uint16_t pat) {
				if (nCh < 1 || m) return;
				m = (uint16_t)(pat & slotLow);
				if (!m) m = (uint16_t)((home ? home : (uint16_t)1) & slotLow);
				if (!m) m = 1;
			};
			uint16_t down = home ? (uint16_t)(home & slotLow) : (uint16_t)(0x1111 & slotLow);
			if (!down) down = 1;
			fillRole(padM, nPad, down);
			fillRole(arpM, nArp, bassSlots ? bassSlots : (uint16_t)(0xFFFF & slotLow));
			fillRole(innM, nHarm, hatM ? hatM : (uint16_t)(0x2222 & slotLow));
			fillRole(cntM, nCnt, (uint16_t)(0x4444 & slotLow));
			fillRole(stabM, nStab, stabSrc ? (uint16_t)(stabSrc & slotLow) : (uint16_t)(0x1010 & slotLow));
			fillRole(colM, nColor, (uint16_t)(0x0101 & slotLow));
			if (nHarm > 1 && !innBlock) innBlock = innM;
		}
		s_secIdx = (b.section == SEC_INTRO) ? introAt : (b.section == SEC_BREAK) ? breakAt : 0;
		s_secLen = (b.section == SEC_INTRO) ? (introN > 0 ? introN : 1) : (b.section == SEC_BREAK) ? (breakN > 0 ? breakN : 1) : 1;
		s_endRem = (b.section == SEC_END) ? endFromEnd : 0;
		PaintPulse(opt->style, patVar, barAt, b.section, div,
			(b.section == SEC_END && endFromEnd <= 0) ? 1 : 0, drm);
		s_nextRootPc = (b.root >= 0) ? b.root : TonicPc(opt->defKey);
		if (bi + 1 < nBars && bars[bi + 1].root >= 0)
			s_nextRootPc = bars[bi + 1].root;
		if (allowBar || form >= 1 || b.section == SEC_BREAK)
			ComposeBar(&ctx, &b, (b.section == SEC_BODY) ? barAt : s_secIdx, form, TonicPc(opt->defKey),
				nBass, bassCh, nChord, chordCh, nPad, padCh, nArp, arpCh,
				nHarm, harmCh, nCnt, cntCh, nStab, stabCh, nColor, colorCh,
				melodyCh, genMel);
		if (b.section != SEC_BREAK && !ctx.overflow) {
			int dDur = (b.len / div > 12) ? 12 : (b.len / div > 2 ? b.len / div : 4);
			for (int di = 1; di < nDrum; di++)
				PaintDrummerLayer(&ctx, drumChs[di], di, opt->style, patVar, barAt, &b, div, drm, (b.section == SEC_INTRO && !last), dDur);
		}
		const int spun = 1;
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
				if (quietish && form < 50) {
					if (h < 4) h = 4;
					if (h > 8) h = 8;
				}
				/* 白玉だけは切る。16分まで削るとアレンジが薄くなる。 */
				if (form >= 60 && role == 1 && h > 8) h = 8;
				if (form >= 85 && role == 1 && h > 4) h = 4;
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
			for (int di = 0; di < nDrum && di < 1; di++) {
				for (int v = 0; v < DV_COUNT; v++) {
					const int drumMidi = GmDrum(v);
					const int hatMuted = (v == DV_HAT && (drm[DV_OHAT] & bit));
					int styleHit = (!hatMuted && (drm[v] & bit)) ? 1 : 0;
					const int clapStyle = (opt->style >= 0 && opt->style < SC_AS_COUNT) && (kStyleExtra[opt->style].ccFlags & CCF_CLAP);
					if (!isFm && clapStyle && v == DV_CLAP && !styleHit && !drm[DV_CLAP]
						&& (drm[DV_SNARE] & bit) && ((slot % 8) == 4))
						styleHit = 1;
					int ghostHat = 0;
					const int prevHat = (slot > 0) && (drm[DV_HAT] & (uint16_t)(1u << (slot - 1)));
					/* 8分ハットの隙間を16分で埋めない。16ビートは譜面側が16分を書く。 */
					if (0 && di == 0 && !styleHit && v == DV_HAT && prevHat && !StyleQuiet(opt->style)
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
						/* 64小節セクション強弱・ゴーストノート・フィルクレッシェンド・ハット抑揚（生ドラマー表現） */
						const BarPlan& plD = PlanOfBar((b.section == SEC_BODY) ? barAt : 0);
						const ArrProfile& apD = kArrProf[patVar & 15];
						const int qD = (div >= 4) ? (div / 4) : 1;
						if (b.section == SEC_BODY) {
							/* セクションエネルギー補正（A1/ブリッジ控えめ → サビ/最終サビ強め） */
							dv += ((int)plD.energy - 74) / 3;
							if (apD.velShape == 4) dv -= 8;
							else if (apD.velShape == 1) dv += 5;
							/* フィルイン（4/8/16小節末）のクレッシェンド */
							int fFrom = (plD.turn == 2) ? (div / 2) : (plD.turn == 1 ? (div - qD) : div);
							if (slot >= fFrom && (v == DV_SNARE || v == DV_TOM || v == DV_KICK)) {
								int spanF = div - fFrom;
								int prog = (spanF > 1) ? ((slot - fFrom) * 38 / (spanF - 1)) : 24;
								dv = 76 + prog;
							} else if (v == DV_SNARE || v == DV_RIM) {
								/* バックビート(2/4拍)はしっかり鳴らし、裏拍ゴーストノートは繊細に落とす */
								int backbeat = (slot == qD || slot == qD * 3 || (plD.breakFeel == 1 && slot == qD * 2));
								if (backbeat && v == DV_SNARE) dv += 8;
								else if (!backbeat) dv = (v == DV_RIM) ? (dv - 18) : (dv - 26);
							} else if (v == DV_HAT || v == DV_RIDE) {
								/* ハット/ライドの拍子アクセント（頭強 → 8分裏中 → 16分裏弱） */
								int sub = slot % qD;
								if (sub == 0) dv += 6;
								else if (qD >= 4 && sub == 2) dv -= 6;
								else dv -= 16;
							} else if (v == DV_KICK && (slot % qD) == 0) {
								dv += 8;
							}
						} else if (b.section == SEC_INTRO) {
							/* イントロは徐々にクレッシェンドして本体へ繋ぐ */
							dv -= (s_secLen - 1 - s_secIdx) * 6;
						} else if (b.section == SEC_END && s_endRem > 0) {
							dv -= 8;
						}
						if (di > 0) dv -= 8;
						if (dv < 36) dv = 36;
						if (dv > 127) dv = 127;
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
			if (spun) continue;
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
					int v0 = ((slot % 4) == 0) ? gvol : (gvol - 8);
					if (sub == 2) v0 -= 10;
					if (v0 < 48) v0 = 48;
					if (v0 > 118) v0 = 118;
					for (int k = 0; k < nSub && !ctx.overflow; k++) {
						int midi = BrokenSweep(hr, ht, st.power, barAt * 64 + slot * 4 + k);
						int v = v0 - ((k & 1) ? 8 : 0);
						if (v < 42) v = 42;
						PlaceNote(&ctx, arpCh[i], tick + (uint32_t)(k * each), midi, each, v, sub == 2 ? 36 : 48);
					}
					continue;
				}
				if (run32 && slot >= midSlot) hit = 0;
				if (hit) {
					int midi = BrokenSweep(hr, ht, st.power, barAt * 64 + slot * 4);
					if (i > 0) {
						int tones[4];
						int nt = ChordTones(hr, ht, st.power, tones, 4);
						int want = midi - 8;
						int best = midi - 12;
						int bestD = 99;
						for (int t = 0; t < nt; t++) {
							int c = tones[t];
							while (c < want - 6) c += 12;
							while (c > want + 6) c -= 12;
							int d = c - want;
							if (d < 0) d = -d;
							if (d < bestD) { bestD = d; best = c; }
						}
						midi = NearMidi(best, want, 40, 76);
					}
					pit[np++] = midi;
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
				if (hit) {
					int top = 0, third = 0;
					PartialDyad(hr, ht, (slot % 4) == 0, barAt * 32 + slot, &top, &third);
					if (i == 0) {
						pit[np++] = top;
						if (StyleFam(opt->style) != FAM_METAL && StyleFam(opt->style) != FAM_MARCH)
							pit[np++] = third;
					} else {
						pit[np++] = NearMidi(top + 12, top + 12, 72, 96);
					}
				}
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
				if (i == 0 && hit && cntSub > 0 && cntCh[i] >= 0 && form < 75) {
					if (merge)
						MergeSlot(ev, evCount, &nEmit, cntCh[i], slot0, slot1, -1, 1, &removedNotes, slotDur);
					int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
					int nSub = (cntSub == 2) ? 4 : 2;
					int each = room / nSub;
					if (each < 1) each = 1;
					int v0 = gvol - (cntSub == 2 ? 16 : 6);
					if (v0 < 46) v0 = 46;
					for (int k = 0; k < nSub && !ctx.overflow; k++) {
						int top = 0, third = 0;
						PartialDyad(hr, ht, k == 0 && (slot % 4) == 0, barAt * 32 + slot * 4 + k + 1, &top, &third);
						int v = v0 - ((k & 1) ? 6 : 0);
						if (v < 40) v = 40;
						PlaceNote(&ctx, cntCh[i], tick + (uint32_t)(k * each), top, each, v, cntSub == 2 ? 34 : 46);
					}
					continue;
				}
				if (hit) {
					int top = 0, third = 0;
					PartialDyad(hr, ht, (slot % 4) == 0, barAt * 32 + slot + 17, &top, &third);
					pit[np++] = (i == 0) ? top : third;
				}
				lay(cntCh[i], hit, pit, np, span, gvol - 8, 78, 0);
			}
			for (int i = 0; i < nStab; i++) {
				int hit = BitOn(stabM, slot);
				int pit[6];
				int np = 0;
				int span = slotDur > 2 ? slotDur : 2;
				int gate = 36;
				if (!lockPart(stabCh[i], &hit, pit, &np, &span, &gate) && hit) {
					int top = 0, third = 0;
					PartialDyad(hr, ht, 1, barAt * 32 + slot, &top, &third);
					pit[np++] = top;
					if (i == 0) pit[np++] = third;
				}
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
				if (!lockPart(colorCh[i], &hit, pit, &np, &span, &gate) && hit) {
					int top = 0, third = 0;
					PartialDyad(hr, ht, (slot % 4) == 0, barAt * 32 + slot, &top, &third);
					pit[np++] = NearMidi(top + 12, top + 12, 76, 98);
					if (i == 0) pit[np++] = NearMidi(third + 12, third + 12, 72, 96);
				}
				lay(colorCh[i], hit, pit, np, span, gvol - 18, gate, 0);
			}
			/* その拍だけ、コードトーンを 64 分で4つ。じゃらん。 */
			if (!ctx.isFm && form < 70 && slot == jaranSlot && slotDur >= 8 && !(run32 && slot >= midSlot)) {
				int ch = (nHarm > 0) ? harmCh[0] : ((nArp > 0) ? arpCh[0] : -1);
				if (ch >= 0) {
					int room = (int)((slot1 > tick) ? (slot1 - tick) : (uint32_t)slotDur);
					int each = room / 4;
					if (each >= 2) {
						for (int k = 0; k < 4 && !ctx.overflow; k++) {
							int midi = BrokenSweep(hr, ht, 0, barAt * 64 + slot * 4 + k + 8);
							int v = 64 + k * 12;
							if (v > 116) v = 116;
							PlaceNote(&ctx, ch, tick + (uint32_t)(k * each), midi, each, v, 40);
						}
					}
				}
			}
			if (0 && genMel && melodyCh >= 0 && slot == 0 && b.root >= 0 && b.section != SEC_BREAK) {
				int echo = -1;
				if (form >= 48) {
					if (nHarm > 0 && harmCh[0] != melodyCh) echo = harmCh[0];
					else if (nCnt > 0 && cntCh[0] != melodyCh) echo = cntCh[0];
					else if (nPad > 0 && padCh[0] != melodyCh && form >= 75) echo = padCh[0];
				}
				WriteBgmLead(&ctx, melodyCh, &b, barAt, opt->style, patVar, form, echo);
			}

		}
	}

	/* チェックした通常パートにノートが無ければ、同じ指順の分散を8分で書く。
	   役割の打点が空のまま残った ch を埋め、ドラム指定は触らない。 */
	if (0 && explicitTargets && bars && nBars > 0 && !ctx.overflow) {
		for (int ch = 0; ch < lim && ch < 32; ch++) {
			if (!((opt->targetMask >> ch) & 1u)) continue;
			if ((opt->drumMask >> ch) & 1u) continue;
			if (isFm && ch == 6) continue;
			int isDrumRole = 0;
			for (int i = 0; i < nDrum; i++) if (drumChs[i] == ch) isDrumRole = 1;
			if (isDrumRole) continue;
			int has = 0;
			for (int i = 0; i < *evCount; i++) {
				if ((int)ev[i].ch == ch && IsNoteKind(ev[i].kind)) { has = 1; break; }
			}
			if (has) continue;
			EnsureMelodic(&ctx, opt->ui, ch, 1, 4, anySolo, 0);
			for (int bi = 0; bi < nBars && !ctx.overflow; bi++) {
				const BarInfo& b = bars[bi];
				if (b.root < 0 || b.section == SEC_BREAK || b.len < 4) continue;
				int step = b.len / 8;
				if (step < 2) step = b.len / 4;
				if (step < 2) step = 2;
				int nStep = b.len / step;
				if (nStep < 1) nStep = 1;
				if (nStep > 16) nStep = 16;
				for (int s = 0; s < nStep && !ctx.overflow; s++) {
					int hr = b.root, ht = b.type;
					if (b.root2 >= 0 && s >= nStep / 2) { hr = b.root2; ht = b.type2; }
					int midi = BrokenSweep(hr, ht, 0, (ch + 1) * 100000 + bi * 64 + s * 4);
					midi += ((ch % 3) - 1) * 12;
					while (midi < 48) midi += 12;
					while (midi > 88) midi -= 12;
					PlaceNote(&ctx, ch, b.t0 + (uint32_t)(s * step), midi, step, 90, 72);
				}
			}
			MarkUi(opt->ui, ch, 0, anySolo);
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
		out->usedMask = s_wroteMel;
		out->usedDrum = s_wroteDrum;
		out->err = ctx.overflow ? SC_AE_OVERFLOW : ((ctx.added > 0 || removedNotes > 0) ? SC_AE_OK : SC_AE_EMPTY);
		FillMsg(out);
		if (genMel && melodyCh >= 0) {
			const wchar_t* fmt = LL14(
				L"\nメロディ ch%d",
				L"\nMelody ch%d",
				L"\nMélodie ch%d",
				L"\nMelodia ch%d",
				L"\nMelodía ch%d",
				L"\n멜로디 ch%d",
				L"\n旋律 ch%d",
				L"\nلحن ch%d",
				L"\nМелодия ch%d",
				L"\nMelodie ch%d",
				L"\nMelodia ch%d",
				L"\nMelodie ch%d",
				L"\nMelodia ch%d",
				L"\nMelodi ch%d");
			wchar_t extra[128];
			swprintf_s(extra, fmt, melodyCh + 1);
			size_t used = wcslen(out->msg);
			if (used + wcslen(extra) < 255)
				wcscat_s(out->msg, extra);
		}
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
