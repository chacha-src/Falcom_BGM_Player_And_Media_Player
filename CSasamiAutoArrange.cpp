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
};

struct BarInfo {
	uint32_t t0;
	int len;
	int root;
	int type;
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
	switch (style) {
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP: case SC_AS_SWING3:
	case SC_AS_BLUES: case SC_AS_SLOWBLUES: case SC_AS_ROCKNROLL:
		return LL14(L"ジャズ／ブルース", L"Jazz / Blues", L"Jazz / Blues", L"Jazz / Blues", L"Jazz / Blues", L"재즈／블루스", L"爵士／布鲁斯", L"جاز / بلوز", L"Джаз / Блюз", L"Jazz / Blues", L"Jazz / Blues", L"Jazz / Blues", L"Jazz / Blues", L"Caz / Blues");
	case SC_AS_GOSPEL: case SC_AS_SOUL: case SC_AS_RNB: case SC_AS_NEOSOUL: case SC_AS_FUNK: case SC_AS_DISCO:
		return LL14(L"ソウル／ファンク", L"Soul / Funk", L"Soul / Funk", L"Soul / Funk", L"Soul / Funk", L"소울／펑크", L"灵魂／放克", L"سول / فانك", L"Соул / Фанк", L"Soul / Funk", L"Soul / Funk", L"Soul / Funk", L"Soul / Funk", L"Soul / Funk");
	case SC_AS_ROCK: case SC_AS_METAL: case SC_AS_PUNK: case SC_AS_GRUNGE: case SC_AS_SURF: case SC_AS_SLOWROCK:
		return LL14(L"ロック", L"Rock", L"Rock", L"Rock", L"Rock", L"록", L"摇滚", L"روك", L"Рок", L"Rock", L"Rock", L"Rock", L"Rock", L"Rock");
	case SC_AS_TECHNO: case SC_AS_HOUSE: case SC_AS_TRANCE: case SC_AS_DNB: case SC_AS_BREAKS:
	case SC_AS_EDM: case SC_AS_DUBSTEP: case SC_AS_SYNTHWAVE:
		return LL14(L"エレクトロニカ", L"Electronica", L"Électronique", L"Elettronica", L"Electrónica", L"일렉트로니카", L"电子乐", L"إلكترونيكا", L"Электроника", L"Electronica", L"Eletrônica", L"Electronica", L"Elektronika", L"Elektronik");
	case SC_AS_HIPHOP: case SC_AS_TRAP: case SC_AS_DRILL: case SC_AS_LOFI:
		return LL14(L"ヒップホップ", L"Hip-hop", L"Hip-hop", L"Hip-hop", L"Hip-hop", L"힙합", L"嘻哈", L"هيب هوب", L"Хип-хоп", L"Hip-Hop", L"Hip-hop", L"Hiphop", L"Hip-hop", L"Hip-hop");
	case SC_AS_BOSSA: case SC_AS_SAMBA: case SC_AS_LATIN: case SC_AS_TANGO: case SC_AS_FLAMENCO: case SC_AS_AFROBEAT:
		return LL14(L"ラテン／ワールド", L"Latin / World", L"Latin / Monde", L"Latino / World", L"Latino / World", L"라틴／월드", L"拉丁／世界", L"لاتيني / عالمي", L"Латино / Этно", L"Latin / World", L"Latino / World", L"Latijn / Wereld", L"Latynoski / World", L"Latin / Dünya");
	case SC_AS_KAYOKYOKU: case SC_AS_ANISON: case SC_AS_CITYPOP: case SC_AS_ENKA:
		return LL14(L"J-POP／歌謡", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-POP／가요", L"J-POP／歌谣", L"جي-بوب / كايو", L"J-Pop / Каё", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-Pop / Kayo", L"J-Pop / Kayo");
	case SC_AS_ORCH: case SC_AS_FANFARE: case SC_AS_MARCH: case SC_AS_WALTZ: case SC_AS_BALLAD:
	case SC_AS_PIANO: case SC_AS_ACOUSTIC: case SC_AS_AMBIENT:
		return LL14(L"アコースティック／劇伴", L"Acoustic / Score", L"Acoustique / BO", L"Acustico / Colonna", L"Acústico / BSO", L"어쿠스틱／극반", L"原声／配乐", L"أكوستيك / موسيقى", L"Акустика / Score", L"Akustisch / Score", L"Acústico / Trilha", L"Akoestisch / Score", L"Akustyczny / Ścieżka", L"Akustik / Score");
	case SC_AS_GAME: case SC_AS_CHIPTUNE: case SC_AS_FUSION: case SC_AS_COUNTRY:
	case SC_AS_SHUFFLE: case SC_AS_POLKA: case SC_AS_REGGAE: case SC_AS_DRUMSOLO:
		return LL14(L"その他", L"Other", L"Autre", L"Altro", L"Otro", L"기타", L"其他", L"أخرى", L"Прочее", L"Sonstiges", L"Outro", L"Overig", L"Inne", L"Diğer");
	default:
		return LL14(L"ビート", L"Beats", L"Rythmes", L"Ritmi", L"Ritmos", L"비트", L"节拍", L"إيقاعات", L"Биты", L"Beats", L"Batidas", L"Beats", L"Bity", L"Ritimler");
	}
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

static void Bass(uint8_t* b, const char* z)
{
	memset(b, 0, 16);
	if (!z) return;
	for (int i = 0; i < 16 && z[i]; i++)
		if (z[i] >= '1' && z[i] <= '9') b[i] = (uint8_t)(z[i] - '0');
}

static void LoadStyle(int id, StylePat* s)
{
	memset(s, 0, sizeof(*s));
	s->div = 16;
	s->chordSlots = 1;
	s->progBass = 33;
	s->progChord = 0;
	switch (id) {
	case SC_AS_16BEAT:
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_OHAT] = Bits("..............x.");
		s->chord = Bits("xxxxxxxxxxxxxxxx");
		Bass(s->bass, "1..3..2.1..3..4.");
		s->progChord = 25;
		break;
	case SC_AS_8BEAT:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1...2...1...2...");
		break;
	case SC_AS_REGGAE:
		s->drum[DV_KICK] = Bits("........x.......");
		s->drum[DV_SNARE] = Bits("........x.......");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->chord = Bits("..x...x...x...x.");
		Bass(s->bass, "1.....7...2...4.");
		s->progChord = 25;
		break;
	case SC_AS_TECHNO:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_CLAP] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("..x...x...x...x.");
		s->drum[DV_OHAT] = Bits("..............x.");
		s->chordSlots = 0;
		Bass(s->bass, "..1...3...1...3.");
		s->progBass = 38; s->progChord = 81;
		break;
	case SC_AS_HOUSE:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_CLAP] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_OHAT] = Bits("..x...x...x...x.");
		s->chord = Bits("....x.......x...");
		s->chordSlots = 2;
		Bass(s->bass, "1.3.1.3.1.3.1.3.");
		s->progBass = 38; s->progChord = 4;
		break;
	case SC_AS_TRANCE:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_OHAT] = Bits("..x...x...x...x.");
		s->chordSlots = 0;
		Bass(s->bass, "1121112111211121");
		s->progBass = 38; s->progChord = 81;
		break;
	case SC_AS_DNB:
		s->drum[DV_KICK] = Bits("x.........x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		Bass(s->bass, "1..1..7.1..2..4.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 81;
		break;
	case SC_AS_HIPHOP:
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x.......x....x..");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.....7.1.....2.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progChord = 4;
		break;
	case SC_AS_FUNK:
		s->drum[DV_KICK] = Bits("x..x....x.....x.");
		s->drum[DV_SNARE] = Bits("....x.x...x...x.");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		Bass(s->bass, "1..4.1.52.1..3.4");
		s->chord = Bits("..x...x...x...x.");
		s->progChord = 25;
		break;
	case SC_AS_DISCO:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_HAT] = Bits("x...x...x...x...");
		s->drum[DV_OHAT] = Bits("..x...x...x...x.");
		Bass(s->bass, "1.3.1.3.1.3.1.3.");
		s->chord = Bits("..x...x...x...x.");
		s->progChord = 25;
		break;
	case SC_AS_ROCK:
		s->power = 1;
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.1.2.2.1.1.2.2.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 2;
		s->progChord = 29;
		break;
	case SC_AS_METAL:
		s->power = 1;
		s->drum[DV_KICK] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.1.1.1.1.1.1.1.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->progChord = 29;
		break;
	case SC_AS_BALLAD:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_RIM] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x.......x.......");
		Bass(s->bass, "1.......2.......");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 4;
		s->arp = 1;
		s->progBass = 32;
		break;
	case SC_AS_WALTZ:
		s->div = 12;
		s->drum[DV_KICK] = Bits("x...........");
		s->drum[DV_SNARE] = Bits("....x...x...");
		s->drum[DV_HAT] = Bits("x...x...x...");
		Bass(s->bass, "1...2...2...");
		s->chord = Bits("....x...x...");
		s->chordSlots = 3;
		s->progBass = 32;
		break;
	case SC_AS_BOSSA:
		s->drum[DV_RIM] = Bits("x..x..x...x.x...");
		s->drum[DV_HAT] = Bits("x...x...x...x...");
		Bass(s->bass, "1..2..1.5..2..7.");
		s->chord = Bits("..x...x...x...x.");
		s->chordSlots = 2;
		s->progBass = 32; s->progChord = 25;
		break;
	case SC_AS_SAMBA:
		s->drum[DV_KICK] = Bits("....x.......x...");
		s->drum[DV_SNARE] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.1.2.5.1.3.2.4.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->arp = 1;
		s->progChord = 25;
		break;
	case SC_AS_SHUFFLE:
		s->div = 12;
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x.....x.....");
		s->drum[DV_SNARE] = Bits("...x.....x..");
		s->drum[DV_HAT] = Bits("x.xx.xx.xx.x");
		Bass(s->bass, "1..1..2..2..");
		s->chord = Bits("x.....x.....");
		s->chordSlots = 3;
		s->progChord = 29;
		break;
	case SC_AS_MARCH:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("..x.x.x...x.x.x.");
		Bass(s->bass, "1...1...1...1...");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 3;
		s->progBass = 32; s->progChord = 61;
		break;
	case SC_AS_POLKA:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		Bass(s->bass, "1.......1.......");
		s->chord = Bits("..x.x.x...x.x.x.");
		s->progChord = 25;
		break;
	case SC_AS_CITYPOP:
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_OHAT] = Bits("..............x.");
		Bass(s->bass, "1..5..7.1.2..4.3");
		s->chord = Bits("..x...x...x...x.");
		s->chordSlots = 2;
		s->progChord = 4;
		break;
	case SC_AS_GAME:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.5.2.3.1.5.2.3.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 2;
		s->arp = 1;
		s->progBass = 38; s->progChord = 80;
		break;
	case SC_AS_ORCH:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_CRASH] = Bits("x...............");
		Bass(s->bass, "1...............");
		s->chordSlots = 0;
		s->progBass = 32; s->progChord = 48;
		break;
	case SC_AS_AMBIENT:
		s->drum[DV_HAT] = Bits("x.......x.......");
		Bass(s->bass, "1...............");
		s->chordSlots = 0;
		s->progBass = 32; s->progChord = 89;
		break;
	case SC_AS_BREAKS:
		s->drum[DV_KICK] = Bits("x.x.....x..x....");
		s->drum[DV_SNARE] = Bits("....x.....x.x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.....7.1...2...");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progChord = 4;
		break;
	case SC_AS_LATIN:
		s->drum[DV_KICK] = Bits("x.....x.x.......");
		s->drum[DV_RIM] = Bits("..x..x...x...x..");
		s->drum[DV_TOM] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1..1..2.7.1...5.");
		s->chord = Bits("x..xx..xx..xx..x");
		s->arp = 1;
		s->progChord = 25;
		break;
	case SC_AS_ENKA:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.......2.......");
		s->chordSlots = 0;
		s->progBass = 32; s->progChord = 48;
		break;
	case SC_AS_FUSION:
		s->drum[DV_RIDE] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_KICK] = Bits("x..x....x.x...x.");
		s->drum[DV_SNARE] = Bits("....x.x.....x.x.");
		Bass(s->bass, "1.1.4.5.2.1.3.1.");
		s->chord = Bits("x.....x...x.....");
		s->chordSlots = 2;
		s->progChord = 4;
		break;
	case SC_AS_ACOUSTIC:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_RIM] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.......2.......");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 2;
		s->progBass = 32; s->progChord = 25;
		break;
	case SC_AS_PIANO:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_RIM] = Bits("....x.......x...");
		Bass(s->bass, "1...5...1...2...");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 4;
		s->arp = 1;
		s->progBass = 0; s->progChord = 0;
		break;
	case SC_AS_FANFARE:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_CRASH] = Bits("x...............");
		Bass(s->bass, "1.....2.1...5...");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 3;
		s->progBass = 32; s->progChord = 61;
		break;
	case SC_AS_SLOWROCK:
		s->power = 1;
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.......2.......");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 8;
		s->progChord = 29;
		break;
	case SC_AS_JAZZ:
		s->div = 12; s->swing = 1;
		s->drum[DV_RIDE] = Bits("x..x.xx..x.x");
		s->drum[DV_HAT] = Bits("...x.....x..");
		s->drum[DV_KICK] = Bits("x...........");
		s->drum[DV_RIM] = Bits("......x.....");
		Bass(s->bass, "1..2..5..7..");
		s->chord = Bits("..x.....x...");
		s->chordSlots = 2;
		s->progBass = 32; s->progChord = 0;
		break;
	case SC_AS_SWINGJAZZ:
		s->div = 12; s->swing = 1;
		s->drum[DV_RIDE] = Bits("x..x.xx..x.x");
		s->drum[DV_HAT] = Bits("...x.....x..");
		s->drum[DV_KICK] = Bits("x.....x.....");
		s->drum[DV_SNARE] = Bits("...x.....x..");
		Bass(s->bass, "1..5..2..7..");
		s->chord = Bits("x..x..x..x..");
		s->chordSlots = 1;
		s->progBass = 32; s->progChord = 26;
		break;
	case SC_AS_BEBOP:
		s->div = 12; s->swing = 1;
		s->drum[DV_RIDE] = Bits("x..x.xx..x.x");
		s->drum[DV_HAT] = Bits("...x.....x..");
		s->drum[DV_KICK] = Bits("x..x..x...x.");
		s->drum[DV_SNARE] = Bits("..x...x.x..x");
		Bass(s->bass, "1.2.3.5.6.7.");
		s->chord = Bits("..x.....x...");
		s->chordSlots = 1;
		s->progBass = 32; s->progChord = 0;
		break;
	case SC_AS_BLUES:
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.3.5.6.6.5.3.1.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 1;
		s->progBass = 32; s->progChord = 26;
		break;
	case SC_AS_SLOWBLUES:
		s->div = 12; s->swing = 1;
		s->drum[DV_KICK] = Bits("x.....x.....");
		s->drum[DV_SNARE] = Bits("...x.....x..");
		s->drum[DV_HAT] = Bits("x.xx.xx.xx.x");
		Bass(s->bass, "1..3..5..6..");
		s->chord = Bits("x.....x.....");
		s->chordSlots = 3;
		s->arp = 1;
		s->progBass = 32; s->progChord = 0;
		break;
	case SC_AS_ROCKNROLL:
		s->div = 12; s->swing = 1;
		s->drum[DV_KICK] = Bits("x.....x.....");
		s->drum[DV_SNARE] = Bits("...x.....x..");
		s->drum[DV_HAT] = Bits("x.xx.xx.xx.x");
		Bass(s->bass, "1..1..5..5..");
		s->chord = Bits("x..x..x..x..");
		s->chordSlots = 1;
		s->progBass = 33; s->progChord = 25;
		break;
	case SC_AS_GOSPEL:
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1..5..3.6..2..5.");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 4;
		s->arp = 1;
		s->progBass = 32; s->progChord = 16;
		break;
	case SC_AS_SOUL:
		s->drum[DV_KICK] = Bits("x.....x.x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_TOM] = Bits("............x...");
		Bass(s->bass, "1..1.5..1..5.3.2");
		s->chord = Bits("..x...x...x...x.");
		s->chordSlots = 2;
		s->progBass = 33; s->progChord = 17;
		break;
	case SC_AS_RNB:
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x......x..x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_CLAP] = Bits("....x.......x...");
		Bass(s->bass, "1.....5.1...2...");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 4;
		break;
	case SC_AS_NEOSOUL:
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x....x.x..x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.xxx.x.xx.xx.x.");
		Bass(s->bass, "1....57.2....4.3");
		s->chord = Bits("x.......x...x...");
		s->chordSlots = 4;
		s->progBass = 33; s->progChord = 4;
		break;
	case SC_AS_COUNTRY:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		Bass(s->bass, "1...5...1...5...");
		s->chord = Bits("..x...x...x...x.");
		s->chordSlots = 2;
		s->progBass = 32; s->progChord = 25;
		break;
	case SC_AS_PUNK:
		s->power = 1;
		s->drum[DV_KICK] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1111111111111111");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->progChord = 29;
		break;
	case SC_AS_GRUNGE:
		s->power = 1;
		s->drum[DV_KICK] = Bits("x.......x..x....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x...x...x...x...");
		Bass(s->bass, "1...1...1.2.3...");
		s->chord = Bits("x.......x.......");
		s->progChord = 29;
		break;
	case SC_AS_SKA:
		s->drum[DV_KICK] = Bits("........x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("..x...x...x...x.");
		Bass(s->bass, "1.2.3.4.5.4.3.2.");
		s->chord = Bits("..x...x...x...x.");
		s->progBass = 33; s->progChord = 25;
		break;
	case SC_AS_KAYOKYOKU:
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1..5..3.6..2..5.");
		s->chord = Bits("x...x...x...x...");
		s->chordSlots = 4;
		s->progBass = 32; s->progChord = 48;
		break;
	case SC_AS_ANISON:
		s->drum[DV_KICK] = Bits("x..x..x.x.x..x..");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("xxxxxxxxxxxxxxxx");
		s->drum[DV_OHAT] = Bits("..............x.");
		Bass(s->bass, "1.1.5.5.6.6.4.4.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 2;
		s->progBass = 38; s->progChord = 4;
		break;
	case SC_AS_CHIPTUNE:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.1.5.5.1.1.5.5.");
		s->chord = Bits("xxxxxxxxxxxxxxxx");
		s->chordSlots = 1;
		s->arp = 1;
		s->progBass = 80; s->progChord = 80;
		break;
	case SC_AS_TRAP:
		s->drum[DV_KICK] = Bits("x.....x...x.....");
		s->drum[DV_SNARE] = Bits("........x.......");
		s->drum[DV_HAT] = Bits("x.x.x.xxx.x.xxxx");
		Bass(s->bass, "1.......1.....1.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 89;
		break;
	case SC_AS_DRILL:
		s->drum[DV_KICK] = Bits("x.....x..x..x...");
		s->drum[DV_SNARE] = Bits("........x.......");
		s->drum[DV_HAT] = Bits("x.xx.x.xx.xx.x.x");
		Bass(s->bass, "1.......7.....6.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 89;
		break;
	case SC_AS_AFROBEAT:
		s->drum[DV_KICK] = Bits("x..x..x...x.x...");
		s->drum[DV_RIM] = Bits("..x..x...x...x..");
		s->drum[DV_HAT] = Bits("x.xxx.xxx.xxx.xx");
		s->drum[DV_TOM] = Bits("....x.......x...");
		Bass(s->bass, "1..1.5.1..2.4..5");
		s->chord = Bits("x..x..x..x..x..x");
		s->arp = 1;
		s->progBass = 33; s->progChord = 17;
		break;
	case SC_AS_SWING3:
		s->div = 12; s->swing = 1;
		s->drum[DV_RIDE] = Bits("x..x..x..x..");
		s->drum[DV_HAT] = Bits("...x.....x..");
		s->drum[DV_KICK] = Bits("x...........");
		Bass(s->bass, "1...5...3...");
		s->chord = Bits("...x..x..x..");
		s->chordSlots = 1;
		s->progBass = 32; s->progChord = 0;
		break;
	case SC_AS_TANGO:
		s->drum[DV_KICK] = Bits("x..x..x.x.......");
		s->drum[DV_RIM] = Bits("............x...");
		Bass(s->bass, "1..1..1.5.......");
		s->chord = Bits("x..x..x.x...x...");
		s->chordSlots = 1;
		s->arp = 1;
		s->progBass = 32; s->progChord = 40;
		break;
	case SC_AS_FLAMENCO:
		s->drum[DV_CLAP] = Bits("x..x..x.x.x.x.x.");
		s->drum[DV_RIM] = Bits("..x..x...x...x..");
		Bass(s->bass, "1..1..5.1..4..5.");
		s->chord = Bits("x..x..x.x.x.x.x.");
		s->arp = 1;
		s->progBass = 24; s->progChord = 24;
		break;
	case SC_AS_SURF:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_TOM] = Bits("..........x.x...");
		Bass(s->bass, "1.1.1.1.5.5.5.5.");
		s->chord = Bits("x.x.x.x.x.x.x.x.");
		s->chordSlots = 2;
		s->progBass = 29; s->progChord = 29;
		break;
	case SC_AS_LOFI:
		s->swing = 1;
		s->drum[DV_KICK] = Bits("x.......x....x..");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.....5.1.....3.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->arp = 1;
		s->progBass = 33; s->progChord = 4;
		break;
	case SC_AS_DRUMSOLO:
		s->drum[DV_KICK] = Bits("x.x..x..x.x..x..");
		s->drum[DV_SNARE] = Bits("..x.x..xx..x.x.x");
		s->drum[DV_TOM] = Bits(".x...x.x..x.x.x.");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_CRASH] = Bits("x...............");
		Bass(s->bass, "1.......1.......");
		s->chordSlots = 0;
		s->progBass = 32;
		break;
	case SC_AS_EDM:
		s->drum[DV_KICK] = Bits("x...x...x...x...");
		s->drum[DV_CLAP] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("..x...x...x...x.");
		s->drum[DV_OHAT] = Bits("..............x.");
		Bass(s->bass, "1...1...1...1...");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 81;
		break;
	case SC_AS_DUBSTEP:
		s->drum[DV_KICK] = Bits("x...............");
		s->drum[DV_SNARE] = Bits("........x.......");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.......1.......");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->progBass = 38; s->progChord = 81;
		break;
	case SC_AS_SYNTHWAVE:
		s->drum[DV_KICK] = Bits("x.......x.......");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_HAT] = Bits("x.x.x.x.x.x.x.x.");
		s->drum[DV_TOM] = Bits("..............xx");
		Bass(s->bass, "1.1.1.1.6.6.6.6.");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 4;
		s->arp = 1;
		s->progBass = 38; s->progChord = 88;
		break;
	default:
		s->power = 1;
		s->drum[DV_KICK] = Bits("x.......x.x.....");
		s->drum[DV_SNARE] = Bits("....x.......x...");
		s->drum[DV_RIDE] = Bits("x.x.x.x.x.x.x.x.");
		Bass(s->bass, "1.......2.......");
		s->chord = Bits("x.......x.......");
		s->chordSlots = 8;
		s->progChord = 29;
		break;
	}
}

static int TonicPc(int keySig); /* 下で定義。進行記述用に前方宣言。 */

/* --- コード進行プロファイル -------------------------------------------------
   off = 主音からの半音。type = 上の CH_*。小節 index をこの配列で巡回し、
   既存ノートが無い（＝コードを拾えない）小節や書き換え対象の土台に使う。
   「コード運び」をテーマに合わせることで、無音からでも破綻しない伴奏になる。 */
struct ProgStep { int8_t off; uint8_t type; };
struct ProgRef { const ProgStep* p; int len; };

static const ProgStep PG_POP1465[]  = { {0,CH_MAJ},{7,CH_MAJ},{9,CH_MIN},{5,CH_MAJ} };          /* I  V  vi IV */
static const ProgStep PG_POP6415[]  = { {9,CH_MIN},{5,CH_MAJ},{0,CH_MAJ},{7,CH_MAJ} };          /* vi IV I  V  */
static const ProgStep PG_POP50s[]   = { {0,CH_MAJ},{9,CH_MIN},{5,CH_MAJ},{7,CH_MAJ} };          /* I  vi IV V  */
static const ProgStep PG_CANON[]    = { {0,CH_MAJ},{7,CH_MAJ},{9,CH_MIN},{4,CH_MIN},{5,CH_MAJ},{0,CH_MAJ},{5,CH_MAJ},{7,CH_MAJ} };
static const ProgStep PG_JAZ251[]   = { {2,CH_M7},{7,CH_7},{0,CH_MAJ7},{0,CH_MAJ7} };           /* ii V I   */
static const ProgStep PG_JAZ1625[]  = { {0,CH_MAJ7},{9,CH_M7},{2,CH_M7},{7,CH_7} };             /* I vi ii V */
static const ProgStep PG_RHYTHM[]   = { {0,CH_MAJ7},{9,CH_7},{2,CH_M7},{7,CH_7} };              /* rhythm changes */
static const ProgStep PG_BLUES12[]  = { {0,CH_7},{0,CH_7},{0,CH_7},{0,CH_7},{5,CH_7},{5,CH_7},{0,CH_7},{0,CH_7},{7,CH_7},{5,CH_7},{0,CH_7},{7,CH_7} };
static const ProgStep PG_BLUESQ[]   = { {0,CH_7},{5,CH_7},{0,CH_7},{7,CH_7} };
static const ProgStep PG_MINAEOL[]  = { {0,CH_MIN},{10,CH_MAJ},{8,CH_MAJ},{7,CH_MAJ} };         /* i VII VI V */
static const ProgStep PG_MIN1647[]  = { {0,CH_MIN},{8,CH_MAJ},{5,CH_MIN},{7,CH_7} };            /* i VI iv V */
static const ProgStep PG_ANDAL[]    = { {0,CH_MIN},{10,CH_MAJ},{8,CH_MAJ},{7,CH_MAJ} };         /* Andalusian */
static const ProgStep PG_GOSPEL[]   = { {0,CH_MAJ7},{4,CH_7},{9,CH_M7},{2,CH_M7},{7,CH_7},{0,CH_MAJ7} };
static const ProgStep PG_SOULVAMP[] = { {0,CH_7},{5,CH_7} };
static const ProgStep PG_JROYAL[]   = { {5,CH_MAJ7},{7,CH_7},{9,CH_MIN},{4,CH_MIN} };           /* 王道 IV V iii vi */
static const ProgStep PG_JKOMORI[]  = { {5,CH_MAJ},{7,CH_MAJ},{0,CH_MAJ},{9,CH_MIN} };          /* 小室 IV V I vi */
static const ProgStep PG_TANGO[]    = { {0,CH_MIN},{7,CH_7},{0,CH_MIN},{5,CH_MIN} };
static const ProgStep PG_POWER[]    = { {0,CH_POW},{5,CH_POW},{7,CH_POW},{5,CH_POW} };

#define PGR(a) { a, (int)(sizeof(a)/sizeof((a)[0])) }

static int StyleProgList(int style, ProgRef out[4])
{
	switch (style) {
	case SC_AS_JAZZ: case SC_AS_SWINGJAZZ: case SC_AS_BEBOP: case SC_AS_SWING3:
		out[0] = PGR(PG_JAZ251); out[1] = PGR(PG_JAZ1625); out[2] = PGR(PG_RHYTHM); out[3] = PGR(PG_CANON); return 4;
	case SC_AS_BLUES: case SC_AS_SLOWBLUES: case SC_AS_ROCKNROLL:
		out[0] = PGR(PG_BLUES12); out[1] = PGR(PG_BLUESQ); out[2] = PGR(PG_POP1465); return 3;
	case SC_AS_GOSPEL:
		out[0] = PGR(PG_GOSPEL); out[1] = PGR(PG_JAZ1625); out[2] = PGR(PG_POP1465); return 3;
	case SC_AS_SOUL: case SC_AS_RNB: case SC_AS_NEOSOUL: case SC_AS_FUNK: case SC_AS_DISCO: case SC_AS_AFROBEAT:
		out[0] = PGR(PG_SOULVAMP); out[1] = PGR(PG_POP6415); out[2] = PGR(PG_JROYAL); return 3;
	case SC_AS_ROCK: case SC_AS_METAL: case SC_AS_PUNK: case SC_AS_GRUNGE: case SC_AS_SURF: case SC_AS_SLOWROCK:
		out[0] = PGR(PG_POWER); out[1] = PGR(PG_POP1465); out[2] = PGR(PG_POP50s); return 3;
	case SC_AS_KAYOKYOKU: case SC_AS_ANISON: case SC_AS_CITYPOP: case SC_AS_ENKA:
		out[0] = PGR(PG_JROYAL); out[1] = PGR(PG_JKOMORI); out[2] = PGR(PG_CANON); out[3] = PGR(PG_POP1465); return 4;
	case SC_AS_TRAP: case SC_AS_DRILL: case SC_AS_DUBSTEP: case SC_AS_SYNTHWAVE: case SC_AS_AMBIENT: case SC_AS_LOFI:
		out[0] = PGR(PG_MINAEOL); out[1] = PGR(PG_MIN1647); out[2] = PGR(PG_POP6415); return 3;
	case SC_AS_LATIN: case SC_AS_BOSSA: case SC_AS_SAMBA:
		out[0] = PGR(PG_JAZ1625); out[1] = PGR(PG_SOULVAMP); out[2] = PGR(PG_JAZ251); return 3;
	case SC_AS_TANGO: case SC_AS_FLAMENCO:
		out[0] = PGR(PG_ANDAL); out[1] = PGR(PG_TANGO); return 2;
	case SC_AS_CHIPTUNE: case SC_AS_GAME: case SC_AS_FANFARE: case SC_AS_MARCH: case SC_AS_ORCH:
		out[0] = PGR(PG_CANON); out[1] = PGR(PG_POP1465); out[2] = PGR(PG_POP50s); return 3;
	default:
		out[0] = PGR(PG_POP1465); out[1] = PGR(PG_POP6415); out[2] = PGR(PG_POP50s); out[3] = PGR(PG_CANON); return 4;
	}
}

static int StyleProgression(int style, int variation, ProgStep* out, int cap)
{
	ProgRef list[4];
	int n = StyleProgList(style, list);
	if (n < 1) return 0;
	if (variation < 0) variation = 0;
	const ProgRef& r = list[variation % n];
	int len = r.len < cap ? r.len : cap;
	for (int i = 0; i < len; i++) out[i] = r.p[i];
	return len;
}

int ScAutoStyleVariationCount(int style)
{
	ProgRef list[4];
	return StyleProgList(style, list);
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

static void EnsureMelodic(Ctx* c, ScStaffUi* ui, int ch, int role, int prog, int anySolo)
{
	if (ch < 0) return;
	int n = *c->n;
	if (!c->isFm) {
		if (!HasKind(c->ev, n, ch, SC_EV_PROG))
			Push(c, 0, ch, SC_EV_PROG, (uint8_t)prog, (uint8_t)prog, 0, 0);
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

static void EnsureDrum(Ctx* c, ScStaffUi* ui, int ch, int slot, int anySolo)
{
	if (ch < 0) return;
	/* a=1(ドラム) は native stream 用。b にドラム枠(0=1,1=2) を載せる(native では無視)。
	   既定ドラム ch でも slot>0 のときは枠を残すため DRUM イベントを書く。 */
	if (!c->isFm) {
		int needEvt = (!ScMidiPartDefaultDrum(ch) || slot > 0);
		if (needEvt && !HasKind(c->ev, *c->n, ch, SC_EV_DRUM))
			Push(c, 0, ch, SC_EV_DRUM, 1, (uint8_t)(slot ? 1 : 0), 0, 0);
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
		if (pitchMode >= 2 && dur >= beat && (idx % 2) == 0) {
			uint8_t pk = c->isFm ? SC_EV_FM_PITCH : SC_EV_PITCH;
			Push(c, e.tick, melody, pk, 56, 0, 0, 0);
			Push(c, e.tick + 6, melody, pk, 64, 0, 0, 0);
		}
		idx++;
		(void)keySig;
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

int ScAutoArrangeApply(ScEvent* ev, int* evCount, int evMax, const ScAutoArrangeOpt* opt, ScAutoArrangeResult* out)
{
	if (out) {
		memset(out, 0, sizeof(*out));
		out->chDrum = out->chBass = out->chChord = out->chHarm = -1;
	}
	if (!ev || !evCount || !opt || evMax <= 0) return SC_AE_NONOTES;
	const int totalNotes = ScAutoArrangeCountNotes(ev, *evCount);
	const int explicitTargets = opt->targetMask != 0;
	/* 0 音でも、対象パート指定か本体小節数があればゼロから生成する。 */
	if (totalNotes <= 0 && !explicitTargets && opt->genBars <= 0) {
		if (out) { out->err = SC_AE_NONOTES; FillMsg(out); }
		return SC_AE_NONOTES;
	}
	Rng rng;
	{
		uint32_t seed = (uint32_t)opt->seed;
		if (!seed) seed = GetTickCount() ^ ((uint32_t)opt->style * 2654435761u) ^ ((uint32_t)(opt->variation + 1) * 40503u);
		RngSeed(&rng, seed);
	}
	const int variation = opt->variation < 0 ? 0 : opt->variation;
	const int useProg = opt->useProg || explicitTargets || totalNotes <= 0;
	const int rewrite = (opt->mode == SC_AM_REWRITE);

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
	int drumChs[8]; uint8_t drumChSlot[8]; int nDrum = 0;
	int extraCh[8]; int nExtra = 0;
	uint32_t deleteMask = 0;
	if (explicitTargets) {
		for (int ch = 0; ch < lim && ch < 32 && nDrum < 8; ch++) {
			if (!((opt->drumMask >> ch) & 1u)) continue;
			drumChs[nDrum] = ch;
			drumChSlot[nDrum] = opt->drumSlot[ch] ? 1 : 0;
			nDrum++;
		}
		chBass = chChord = chHarm = melodyCh = -1;
		int pend[32]; int nPend = 0;
		for (int ch = 0; ch < lim && ch < 32; ch++) {
			if (!((opt->targetMask >> ch) & 1u)) continue;
			if ((opt->drumMask >> ch) & 1u) continue;
			int role = opt->partRole[ch];
			if (role == SC_PR_AUTO) {
				if (notes[ch] <= 0 || notes[ch] >= 1000000) { pend[nPend++] = ch; continue; }
				int ratio = tickCount[ch] ? (polyTicks[ch] * 100) / tickCount[ch] : 0;
				int avg = notes[ch] ? sumPitch[ch] / notes[ch] : 60;
				if (ratio >= 50) role = SC_PR_CHORD;
				else if (avg <= 48) role = SC_PR_BASS;
				else role = SC_PR_MELODY;
			}
			switch (role) {
			case SC_PR_BASS: if (chBass < 0) chBass = ch; else if (nExtra < 8) extraCh[nExtra++] = ch; break;
			case SC_PR_CHORD: case SC_PR_PAD: if (chChord < 0) chChord = ch; else if (nExtra < 8) extraCh[nExtra++] = ch; break;
			case SC_PR_HARMONY: if (chHarm < 0) chHarm = ch; else if (nExtra < 8) extraCh[nExtra++] = ch; break;
			case SC_PR_ARPEGGIO: if (nExtra < 8) extraCh[nExtra++] = ch; break;
			case SC_PR_MELODY: default: if (melodyCh < 0) melodyCh = ch; else if (nExtra < 8) extraCh[nExtra++] = ch; break;
			}
		}
		for (int i = 0; i < nPend; i++) {
			int ch = pend[i];
			if (chBass < 0) chBass = ch;
			else if (chChord < 0) chChord = ch;
			else if (chHarm < 0) chHarm = ch;
			else if (nExtra < 8) extraCh[nExtra++] = ch;
		}
		chDrum = nDrum > 0 ? drumChs[0] : -1;
		if (rewrite) {
			for (int i = 0; i < nDrum; i++) deleteMask |= (1u << drumChs[i]);
			if (chBass >= 0) deleteMask |= (1u << chBass);
			if (chChord >= 0) deleteMask |= (1u << chChord);
			if (chHarm >= 0) deleteMask |= (1u << chHarm);
			for (int i = 0; i < nExtra; i++) deleteMask |= (1u << extraCh[i]);
		}
	} else if (chDrum >= 0) {
		drumChs[0] = chDrum; drumChSlot[0] = 0; nDrum = 1;
	}

	int canLayer = (nDrum > 0 || chBass >= 0 || chChord >= 0 || nExtra > 0);
	int canMel = ((opt->jobs & SC_AJ_MEL_VOL) && melodyCh >= 0)
		|| ((opt->jobs & SC_AJ_MEL_PITCH) && opt->pitchMode > 0 && melodyCh >= 0 && (chHarm >= 0 || opt->pitchMode >= 2))
		|| (explicitTargets && melodyCh >= 0);
	if (!canLayer && !canMel) {
		if (out) { out->err = SC_AE_EMPTY; FillMsg(out); }
		return SC_AE_EMPTY;
	}
	if (explicitTargets) layers = canLayer ? (SC_AJ_DRUM | SC_AJ_BASS | SC_AJ_CHORD) : 0;
	else if (!canLayer) layers = 0;

	int doIntro = (opt->jobs & SC_AJ_INTRO) && opt->introBars > 0 && layers;
	int doBreak = (opt->jobs & SC_AJ_BREAK) && opt->breakBars > 0 && layers;
	int doEnd = (opt->jobs & SC_AJ_ENDING) && opt->endBars > 0 && layers;

	int defN = opt->defMeterN > 0 ? opt->defMeterN : 4;
	int defD = opt->defMeterD > 0 ? opt->defMeterD : 4;
	uint32_t introTicks = 0;
	if (doIntro) {
		int nM = defN, dM = defD;
		ScStaffMeterAtTick(ev, *evCount, 0, defN, defD, &nM, &dM);
		int bl = ScStaffMeterTicksPerMeasure(nM, dM);
		introTicks = (uint32_t)(opt->introBars * bl);
		int n0 = *evCount;
		ShiftFrom(ev, n0, 0, introTicks);
		Ctx tmp = { ev, evCount, evMax, isFm, 0, 0, opt->jobs };
		for (int i = 0; i < n0; i++) {
			if (ev[i].tick == introTicks && IsSetupKind(ev[i].kind)) {
				ScEvent cpy = ev[i];
				cpy.tick = 0;
				if (!Push(&tmp, 0, cpy.ch, cpy.kind, cpy.a, cpy.b, cpy.c, cpy.dur)) {
					if (out) { out->err = SC_AE_OVERFLOW; FillMsg(out); }
					return SC_AE_OVERFLOW;
				}
				ev[*evCount - 1].seq = cpy.seq;
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
	if (lead0 == 0xFFFFFFFFu) { lead0 = introTicks; lead1 = noteEnd; }
	if (noteEnd == 0) {
		int gb = opt->genBars > 0 ? opt->genBars : 16;
		noteEnd = introTicks + (uint32_t)(gb * ScStaffMeterTicksPerMeasure(defN, defD));
		lead0 = introTicks; lead1 = noteEnd;
	} else if (opt->genBars > 0) {
		uint32_t want = introTicks + (uint32_t)(opt->genBars * ScStaffMeterTicksPerMeasure(defN, defD));
		if (noteEnd < want) noteEnd = want;
		if (lead1 < want) lead1 = want;
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
		else {
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
	if (doEnd) {
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
			if (introTicks && t < introTicks) b.section = SEC_INTRO;
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
		}
		for (int i = nBars - 1; i >= 0; i--) {
			if (bars[i].root < 0 && i + 1 < nBars) {
				bars[i].root = bars[i + 1].root;
				bars[i].type = bars[i + 1].type;
			}
		}
		int endSeen = 0;
		int endCount = 0;
		for (int i = 0; i < nBars; i++) if (bars[i].section == SEC_END) endCount++;
		for (int i = 0; i < nBars; i++) {
			if (bars[i].section != SEC_END) continue;
			endSeen++;
			if (endSeen < endCount) {
				bars[i].root = (tonic + 7) % 12;
				bars[i].type = CH_7;
			} else {
				bars[i].root = tonic;
				bars[i].type = tonicType;
			}
		}
	}

	/* テーマのコード進行を、既存ノートでコードを拾えない本体小節へ適用。
	   これで無音からの生成や空き小節でも「らしい」コード運びになる。 */
	if (useProg && nBars > 0) {
		ProgStep prog[16];
		int pn = StyleProgression(opt->style, variation, prog, 16);
		if (pn > 0) {
			int tonic2 = TonicPc(opt->defKey);
			int bix = 0;
			for (int i = 0; i < nBars; i++) {
				if (bars[i].section != SEC_BODY) { continue; }
				if (!bars[i].voiced) {
					const ProgStep& ps = prog[(bix + (variation % pn)) % pn];
					bars[i].root = ((tonic2 + ps.off) % 12 + 12) % 12;
					bars[i].type = ps.type;
				}
				bix++;
			}
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
	if (explicitTargets && melodyCh >= 0) ctxJobs |= (SC_AJ_MEL_VOL | SC_AJ_MEL_PITCH);
	Ctx ctx = { ev, evCount, evMax, isFm, 0, 0, ctxJobs };
	StylePat st;
	LoadStyle(opt->style, &st);
	for (int i = 0; i < nDrum; i++) EnsureDrum(&ctx, opt->ui, drumChs[i], drumChSlot[i], anySolo);
	if (chBass >= 0) EnsureMelodic(&ctx, opt->ui, chBass, 0, st.progBass, anySolo);
	if (chChord >= 0) EnsureMelodic(&ctx, opt->ui, chChord, 1, st.progChord, anySolo);
	if (chHarm >= 0) EnsureMelodic(&ctx, opt->ui, chHarm, 2, st.progChord, anySolo);
	for (int i = 0; i < nExtra; i++) EnsureMelodic(&ctx, opt->ui, extraCh[i], 1, st.progChord, anySolo);

	int bodyIx = 0;
	int nEmit = *evCount;
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
		if (b.section == SEC_INTRO && !last) {
			drm[DV_SNARE] = 0; drm[DV_TOM] = 0; drm[DV_CRASH] = 0; drm[DV_CLAP] = 0; drm[DV_OHAT] = 0;
			drm[DV_HAT] = (uint16_t)(drm[DV_HAT] & 0x5555);
			skipChord = 1;
		} else if (b.section == SEC_INTRO && last) {
			drm[DV_SNARE] = (uint16_t)(drm[DV_SNARE] | (div >= 16 ? 0xF000 : 0x0E00));
			drm[DV_CRASH] = (uint16_t)(drm[DV_CRASH] | 1);
		} else if (b.section == SEC_BREAK) {
			drm[DV_KICK] = (uint16_t)(div >= 16 ? 0x0101 : 0x0041);
			drm[DV_SNARE] = (uint16_t)(div >= 16 ? 0x1010 : 0x0208);
			drm[DV_HAT] = (uint16_t)(div >= 16 ? 0x5555 : 0x0AAA);
			drm[DV_OHAT] = drm[DV_RIDE] = drm[DV_CRASH] = drm[DV_TOM] = drm[DV_CLAP] = drm[DV_RIM] = 0;
			Bass(bass, div >= 16 ? "1.4.2.5.1.4.2.5." : "1.4.2.5.1.4.");
			chordSlots = 0;
			chordMask = 1;
		} else if (b.section == SEC_END && !last) {
			drm[DV_KICK] = 1;
			drm[DV_SNARE] = (uint16_t)(div >= 16 ? 0xFFFF : 0x0FFF);
			drm[DV_HAT] = drm[DV_OHAT] = drm[DV_RIDE] = drm[DV_CRASH] = 0;
			drm[DV_TOM] = (uint16_t)(div >= 16 ? 0x0F00 : 0x00F0);
			Bass(bass, div >= 16 ? "1..4..2..5....." : "1..4..2..5..");
			chordSlots = 0;
		} else if (b.section == SEC_END && last) {
			memset(drm, 0, sizeof(drm));
			drm[DV_KICK] = 1;
			drm[DV_CRASH] = 1;
			memset(bass, 0, sizeof(bass));
			bass[0] = 1;
			chordSlots = 0;
			chordMask = 1;
		} else if (b.section == SEC_BODY && (bodyIx % 4) == 3) {
			drm[DV_SNARE] = (uint16_t)(drm[DV_SNARE] | (div >= 16 ? 0xF000 : 0x0E00));
		}
		/* バリエーション: 本体小節にたまにゴーストや追いキックを足し、毎回同じにしない。 */
		if (b.section == SEC_BODY && RngRange(&rng, 0, 7) == 0)
			drm[DV_KICK] = (uint16_t)(drm[DV_KICK] | (1u << (div >= 16 ? 14 : 10)));
		int occDrum[8];
		for (int i = 0; i < nDrum; i++)
			occDrum[i] = (b.section == SEC_BODY) && BarHas(ev, nEmit, drumChs[i], b.t0, b.t0 + (uint32_t)b.len);
		int occExtra[8];
		for (int i = 0; i < nExtra; i++)
			occExtra[i] = (b.section == SEC_BODY) && BarHas(ev, nEmit, extraCh[i], b.t0, b.t0 + (uint32_t)b.len);
		int occupiedBass = (b.section == SEC_BODY) && BarHas(ev, nEmit, chBass, b.t0, b.t0 + (uint32_t)b.len);
		int occupiedCh = (b.section == SEC_BODY) && BarHas(ev, nEmit, chChord, b.t0, b.t0 + (uint32_t)b.len);
		for (int slot = 0; slot < div && !ctx.overflow; slot++) {
			uint32_t tick = b.t0 + (uint32_t)((uint64_t)slot * (uint64_t)b.len / (uint64_t)div);
			int slotDur = b.len / div;
			if (slotDur < 2) slotDur = 2;
			if (st.swing && (slot & 1)) tick += (uint32_t)(slotDur / 3);
			if (tick >= b.t0 + (uint32_t)b.len) continue;
			uint16_t bit = (uint16_t)(1u << slot);
			int vel = 78 + ((slot % 4) == 0 ? 28 : (slot & 1) ? -8 : 8);
			vel += RngRange(&rng, -5, 6); /* ヒューマナイズ */
			if (vel < 1) vel = 1;
			if (vel > 127) vel = 127;
			/* 複数ドラム ch: 枠0=本体(キック/スネア/タム/クラップ/リム)、枠1=シンバル系。
			   1 ch のときは全部そのパートに書く。 */
			int drumDur = isFm ? 2 : (slotDur > 12 ? 12 : slotDur);
			for (int di = 0; di < nDrum; di++) {
				if (occDrum[di]) continue;
				int kslot = drumChSlot[di];
				for (int v = 0; v < DV_COUNT; v++) {
					if (nDrum > 1) {
						int isCym = (v == DV_HAT || v == DV_OHAT || v == DV_RIDE || v == DV_CRASH);
						if (kslot == 1 && !isCym) continue;
						if (kslot == 0 && isCym) continue;
					}
					if (v == DV_HAT && (drm[DV_OHAT] & bit)) continue;
					if (drm[v] & bit)
						PlaceDrum(&ctx, drumChs[di], tick, v, v == DV_HAT ? vel - 18 : vel, drumDur);
				}
			}
			if (!occupiedBass && chBass >= 0 && slot < 16 && bass[slot]) {
				int next = slot + 1;
				while (next < div && next < 16 && !bass[next]) next++;
				int span = (next - slot) * slotDur;
				if (span < slotDur) span = slotDur;
				if (span > b.len) span = b.len;
				PlaceNote(&ctx, chBass, tick, BassMidi(b.root, bass[slot], b.type), span, vel, 85);
			}
			if (!skipChord && !occupiedCh && chChord >= 0 && (chordSlots == 0 ? slot == 0 : (chordMask & bit))) {
				int dur = (chordSlots <= 0) ? (b.len - 2) : chordSlots * slotDur;
				uint32_t ct = tick;
				if (b.section == SEC_INTRO && last) {
					uint32_t mid = b.t0 + (uint32_t)(b.len / 2);
					if (chordSlots <= 0) {
						ct = mid;
						dur = b.len / 2 - 2;
					} else if (tick < mid)
						continue;
				}
				if (dur < 2) dur = 2;
				PlaceChord(&ctx, chChord, ct, dur, vel - 8, b.root, b.type, st.power, st.arp);
			}
			/* 追加で選ばれた旋律パートはアルペジオのコード層として鳴らす。 */
			if (!skipChord && nExtra > 0 && (chordSlots == 0 ? slot == 0 : (chordMask & bit))) {
				int dur = (chordSlots <= 0) ? (b.len - 2) : chordSlots * slotDur;
				if (dur < 2) dur = 2;
				for (int ei = 0; ei < nExtra; ei++) {
					if (occExtra[ei]) continue;
					PlaceChord(&ctx, extraCh[ei], tick, dur, vel - 12, b.root, b.type, st.power, 1);
				}
			}
		}
		if (b.section == SEC_BODY) bodyIx++;
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
		for (int i = 0; i < nExtra; i++) used |= (1u << extraCh[i]);
		if (chBass >= 0) used |= (1u << chBass);
		if (chChord >= 0) used |= (1u << chChord);
		if (chHarm >= 0) used |= (1u << chHarm);
		if (melodyCh >= 0) used |= (1u << melodyCh);
		out->usedMask = used;
		out->err = ctx.overflow ? SC_AE_OVERFLOW : ((ctx.added > 0 || removedNotes > 0) ? SC_AE_OK : SC_AE_EMPTY);
		FillMsg(out);
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
