// Nishi.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Nishi.h"


// CNishi ダイアログ

IMPLEMENT_DYNAMIC(CNishi, CCustomBlurDialogBase)

CNishi::CNishi(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CNishi::IDD, pParent)
{

}

CNishi::~CNishi()
{
}

void CNishi::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CNishi, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CNishi);


// CNishi メッセージ ハンドラ
extern CString fnn;

TCHAR tinishi[][128] = {
L"z001 Brieging(ゼフィールファルコン作戦会議)",
L"z002 COOL FIELD(フィールド山岳地帯)",
L"z003 ディアブロ(ディアブロ第二段階)",
L"z004 ゲイシルシティ(街)",
L"z005 破壊神の鼓動(ディアブロ第一段階)",
L"z006 インフェルノ(インフェルノ監獄(開放作戦))",
L"z007 イオリーン(アンタリア創世記ヒロインのテーマ)",
L"z008 Waltz de…(回想～バーンスタイン邸)",
L"z009 街 Part1(村のテーマ)",
L"z010 お別れイベントシーン(メルセデス)",
L"z011 街 Part2(町のテーマ",
L"z012 犠牲(悲しいイベント)",
L"z013 …ドレイク？(海賊の襲撃)",
L"z014 戦闘(通常戦闘)",
L"z015 Zephyr Falcon(解放軍ゼフィールファルコン)",
L"z016 The wind of Memory(シラノとメルセデスのピアノ曲)",
L"z017 砂漠(砂漠)",
L"z018 Aquamarine(最終ダンジョン～海底遺跡)",
L"z019 魔剣阿修羅(阿修羅との戦い)",
L"z020 Decisive Battle(ボスバトル)",
L"z021 陰謀(イベントシーン(悪役専用))",
L"z022 Force Field(解放戦争フィールド)",
L"z023 FUZZY(スタッフロール)",
L"z024 It's time to BATTLE!(ボスバトル2)",
L"z025 PEACE(村)",
L"z026 Precious Memories(シラノの想い出)",
L"z027 Sorrowful…(イベント(大体が、誰かが死んだあと))",
L"z028 Subway Crisis(ダンジョン)",
L"z029 軍都市(緊迫した町のテーマ)",
L"z030 目覚め(最終バトル直前)",
L"z031 b-e natural(ダンジョン)",
L"z032 チェザレ -part1-(チェザレのテーマ(オルガン))",
L"z033 チェザレ -part2-(ラストバトル)",
L"z034 Fireroad(契約)",
L"z035 Forth step towards plain(フィールド・森など)",
L"z036 THE GREAT REPEAT(ゲームタイトル画面)",
L"z037 闇(インフェルノ地下)",
L"z038 Jungle 2 Jungle(バース島)",
L"z039 Not natural but natural(無限ループダンジョン)",
L"z041 The wind of Memory（オーケストラ・ミックス）(エンディングテーマ)",
L"z042 迷い(公爵屋敷)",
L"z050 The wind of Memory（フレーズ）(ピアノ弾き)"
};

TCHAR tinishi_en[][128] = {
L"z001 Briefing(Zephyr Falcon War Council)",
L"z002 COOL FIELD(Field mountain area)",
L"z003 Diablo(Diablo second phase)",
L"z004 Gayl City(Town)",
L"z005 Pulse of the Destroyer(Diablo first phase)",
L"z006 Inferno(Inferno Prison(liberation))",
L"z007 Ioleen(Antaria Genesis heroine theme)",
L"z008 Waltz de...(Recollection~Bernstein residence)",
L"z009 Town Part1(Village theme)",
L"z010 Farewell event scene(Mercedes)",
L"z011 Town Part2(Town theme)",
L"z012 Sacrifice(Sad event)",
L"z013 ...Drake?(Pirate attack)",
L"z014 Battle(Normal battle)",
L"z015 Zephyr Falcon(Liberation Army Zephyr Falcon)",
L"z016 The wind of Memory(Silvano and Mercedes piano piece)",
L"z017 Desert(Desert)",
L"z018 Aquamarine(Final dungeon~underwater ruins)",
L"z019 Demon Sword Asura(Battle with Asura)",
L"z020 Decisive Battle(Boss battle)",
L"z021 Conspiracy(Event scene(villain))",
L"z022 Force Field(Liberation war field)",
L"z023 FUZZY(Staff roll)",
L"z024 It's time to BATTLE!(Boss battle 2)",
L"z025 PEACE(Village)",
L"z026 Precious Memories(Silvano's memory)",
L"z027 Sorrowful...(Event(usually after someone dies))",
L"z028 Subway Crisis(Dungeon)",
L"z029 Military city(Urgent town theme)",
L"z030 Awakening(Just before final battle)",
L"z031 b-e natural(Dungeon)",
L"z032 Cesare -part1-(Cesare theme(organ))",
L"z033 Cesare -part2-(Last battle)",
L"z034 Fireroad(Contract)",
L"z035 Forth step towards plain(Field, forest, etc)",
L"z036 THE GREAT REPEAT(Game title screen)",
L"z037 Darkness(Inferno underground)",
L"z038 Jungle 2 Jungle(Birth island)",
L"z039 Not natural but natural(Infinite loop dungeon)",
L"z041 The wind of Memory(Orchestra mix)(Ending theme)",
L"z042 Hesitation(Duke residence)",
L"z050 The wind of Memory(Phrase)(Piano)"
};

TCHAR tinishi_fr[][128] = {
L"z001 Briefing(Conseil de guerre Zephyr Falcon)",
L"z002 COOL FIELD(Région montagneuse)",
L"z003 Diablo(Deuxième phase de Diablo)",
L"z004 Gayl City(Ville)",
L"z005 Pulsation du Destructeur(Première phase de Diablo)",
L"z006 Inferno(Prison d'Inferno(libération))",
L"z007 Ioleen(Thème héroïne Antaria Genesis)",
L"z008 Waltz de...(Souvenir~Résidence Bernstein)",
L"z009 Ville Partie1(Thème village)",
L"z010 Scène d'adieu(Mercedes)",
L"z011 Ville Partie2(Thème ville)",
L"z012 Sacrifice(Événement triste)",
L"z013 ...Drake?(Attaque pirate)",
L"z014 Bataille(Bataille normale)",
L"z015 Zephyr Falcon(Armée de libération Zephyr Falcon)",
L"z016 The wind of Memory(Morceau piano Silvano et Mercedes)",
L"z017 Désert(Désert)",
L"z018 Aquamarine(Donjon final~ruines sous-marines)",
L"z019 Épée démoniaque Asura(Bataille contre Asura)",
L"z020 Bataille décisive(Bataille boss)",
L"z021 Conspiration(Scène événement(vilain))",
L"z022 Force Field(Champ guerre de libération)",
L"z023 FUZZY(Générique)",
L"z024 It's time to BATTLE!(Bataille boss 2)",
L"z025 PEACE(Village)",
L"z026 Precious Memories(Souvenir de Silvano)",
L"z027 Sorrowful...(Événement(souvent après une mort))",
L"z028 Subway Crisis(Donjon)",
L"z029 Ville militaire(Thème ville tendue)",
L"z030 Éveil(Juste avant la bataille finale)",
L"z031 b-e natural(Donjon)",
L"z032 Cesare -partie1-(Thème Cesare(orgue))",
L"z033 Cesare -partie2-(Dernière bataille)",
L"z034 Fireroad(Contrat)",
L"z035 Forth step towards plain(Champ, forêt, etc)",
L"z036 THE GREAT REPEAT(Écran titre)",
L"z037 Ténèbres(Sous-sol Inferno)",
L"z038 Jungle 2 Jungle(Île Birth)",
L"z039 Not natural but natural(Donjon boucle infinie)",
L"z041 The wind of Memory(Mix orchestre)(Thème ending)",
L"z042 Hésitation(Résidence ducale)",
L"z050 The wind of Memory(Phrase)(Piano)"
};

TCHAR tinishi_de[][128] = {
L"z001 Briefing(Kriegsrat Zephyr Falcon)",
L"z002 COOL FIELD(Bergregion)",
L"z003 Diablo(Phase zwei Diablo)",
L"z004 Gayl City(Stadt)",
L"z005 Puls des Zerstörers(Phase eins Diablo)",
L"z006 Inferno(Inferno-Gefängnis(Befreiung))",
L"z007 Ioleen(Antaria Genesis Heldin-Thema)",
L"z008 Waltz de...(Erinnerung~Bernstein Residenz)",
L"z009 Stadt Teil1(Dorf-Thema)",
L"z010 Abschiedsszene(Mercedes)",
L"z011 Stadt Teil2(Stadt-Thema)",
L"z012 Opfer(Trauriges Ereignis)",
L"z013 ...Drake?(Piratenangriff)",
L"z014 Schlacht(Normale Schlacht)",
L"z015 Zephyr Falcon(Befreiungsarmee Zephyr Falcon)",
L"z016 The wind of Memory(Klavierstück Silvano und Mercedes)",
L"z017 Wüste(Wüste)",
L"z018 Aquamarine(End-Dungeon~Unterwasserruinen)",
L"z019 Dämonenschwert Asura(Kampf gegen Asura)",
L"z020 Entscheidungsschlacht(Boss-Kampf)",
L"z021 Verschwörung(Ereignisszene(Bösewicht))",
L"z022 Force Field(Befreiungskrieg Feld)",
L"z023 FUZZY(Abspann)",
L"z024 It's time to BATTLE!(Boss-Kampf 2)",
L"z025 PEACE(Dorf)",
L"z026 Precious Memories(Silvanos Erinnerung)",
L"z027 Sorrowful...(Ereignis(meist nach einem Tod))",
L"z028 Subway Crisis(Dungeon)",
L"z029 Militärstadt(Dringendes Stadt-Thema)",
L"z030 Erwachen(Kurz vor Endkampf)",
L"z031 b-e natural(Dungeon)",
L"z032 Cesare -Teil1-(Cesare-Thema(Orgel))",
L"z033 Cesare -Teil2-(Letzter Kampf)",
L"z034 Fireroad(Vertrag)",
L"z035 Forth step towards plain(Feld, Wald, etc)",
L"z036 THE GREAT REPEAT(Titelscreen)",
L"z037 Dunkelheit(Inferno Untergrund)",
L"z038 Jungle 2 Jungle(Birth-Insel)",
L"z039 Not natural but natural(Endlos-Dungeon)",
L"z041 The wind of Memory(Orchester-Mix)(Ending-Thema)",
L"z042 Zögern(Herzogsresidenz)",
L"z050 The wind of Memory(Phrase)(Klavier)"
};

TCHAR tinishi_es[][128] = {
L"z001 Briefing(Consejo de guerra Zephyr Falcon)",
L"z002 COOL FIELD(Región montañosa)",
L"z003 Diablo(Segunda fase Diablo)",
L"z004 Gayl City(Ciudad)",
L"z005 Pulso del Destructor(Primera fase Diablo)",
L"z006 Inferno(Prisión Inferno(liberación))",
L"z007 Ioleen(Tema heroína Antaria Genesis)",
L"z008 Waltz de...(Recuerdo~Residencia Bernstein)",
L"z009 Pueblo Parte1(Tema aldea)",
L"z010 Escena de despedida(Mercedes)",
L"z011 Pueblo Parte2(Tema ciudad)",
L"z012 Sacrificio(Evento triste)",
L"z013 ...Drake?(Ataque pirata)",
L"z014 Batalla(Batalla normal)",
L"z015 Zephyr Falcon(Ejército liberación Zephyr Falcon)",
L"z016 The wind of Memory(Pieza piano Silvano y Mercedes)",
L"z017 Desierto(Desierto)",
L"z018 Aquamarine(Dungeon final~ruinas submarinas)",
L"z019 Espada demonio Asura(Batalla contra Asura)",
L"z020 Batalla decisiva(Batalla jefe)",
L"z021 Conspiración(Escena evento(villano))",
L"z022 Force Field(Campo guerra liberación)",
L"z023 FUZZY(Créditos)",
L"z024 It's time to BATTLE!(Batalla jefe 2)",
L"z025 PEACE(Aldea)",
L"z026 Precious Memories(Recuerdo de Silvano)",
L"z027 Sorrowful...(Evento(suele tras una muerte))",
L"z028 Subway Crisis(Mazmorra)",
L"z029 Ciudad militar(Tema ciudad tensa)",
L"z030 Despertar(Justo antes batalla final)",
L"z031 b-e natural(Mazmorra)",
L"z032 Cesare -parte1-(Tema Cesare(órgano))",
L"z033 Cesare -parte2-(Última batalla)",
L"z034 Fireroad(Contrato)",
L"z035 Forth step towards plain(Campo, bosque, etc)",
L"z036 THE GREAT REPEAT(Pantalla título)",
L"z037 Oscuridad(Inframundo Inferno)",
L"z038 Jungle 2 Jungle(Isla Birth)",
L"z039 Not natural but natural(Mazmorra bucle infinito)",
L"z041 The wind of Memory(Mix orquesta)(Tema ending)",
L"z042 Duda(Residencia ducal)",
L"z050 The wind of Memory(Phrase)(Piano)"
};

TCHAR tinishi_it[][128] = {
L"z001 Briefing(Consiglio di guerra Zephyr Falcon)",
L"z002 COOL FIELD(Regione montuosa)",
L"z003 Diablo(Seconda fase Diablo)",
L"z004 Gayl City(Città)",
L"z005 Polso del Distruttore(Prima fase Diablo)",
L"z006 Inferno(Prigione Inferno(liberazione))",
L"z007 Ioleen(Tema eroina Antaria Genesis)",
L"z008 Waltz de...(Ricordo~Residenza Bernstein)",
L"z009 Città Parte1(Tema villaggio)",
L"z010 Scena addio(Mercedes)",
L"z011 Città Parte2(Tema città)",
L"z012 Sacrificio(Evento triste)",
L"z013 ...Drake?(Attacco pirata)",
L"z014 Battaglia(Battaglia normale)",
L"z015 Zephyr Falcon(Esercito liberazione Zephyr Falcon)",
L"z016 The wind of Memory(Pezzo pianoforte Silvano e Mercedes)",
L"z017 Deserto(Deserto)",
L"z018 Aquamarine(Dungeon finale~rovine sottomarine)",
L"z019 Spada demone Asura(Battaglia contro Asura)",
L"z020 Battaglia decisiva(Battaglia boss)",
L"z021 Cospirazione(Scena evento(cattivo))",
L"z022 Force Field(Campo guerra liberazione)",
L"z023 FUZZY(Titoli di coda)",
L"z024 It's time to BATTLE!(Battaglia boss 2)",
L"z025 PEACE(Villaggio)",
L"z026 Precious Memories(Ricordo di Silvano)",
L"z027 Sorrowful...(Evento(solitamente dopo morte))",
L"z028 Subway Crisis(Dungeon)",
L"z029 Città militare(Tema città tesa)",
L"z030 Risveglio(Subito prima battaglia finale)",
L"z031 b-e natural(Dungeon)",
L"z032 Cesare -parte1-(Tema Cesare(organo))",
L"z033 Cesare -parte2-(Ultima battaglia)",
L"z034 Fireroad(Contratto)",
L"z035 Forth step towards plain(Campo, foresta, etc)",
L"z036 THE GREAT REPEAT(Schermata titolo)",
L"z037 Oscurità(Sotterranei Inferno)",
L"z038 Jungle 2 Jungle(Isola Birth)",
L"z039 Not natural but natural(Dungeon loop infinito)",
L"z041 The wind of Memory(Mix orchestra)(Tema ending)",
L"z042 Esitazione(Residenza ducale)",
L"z050 The wind of Memory(Phrase)(Pianoforte)"
};

TCHAR tinishi_ko[][128] = {
L"z001 Briefing(제피어 팔콘 작전회의)",
L"z002 COOL FIELD(필드 산악지대)",
L"z003 Diablo(디아블로 2단계)",
L"z004 Gayl City(거리)",
L"z005 파괴신의 고동(디아블로 1단계)",
L"z006 Inferno(인페르노 감옥(개방작전))",
L"z007 Ioleen(안타리아 창세기 히로인 테마)",
L"z008 Waltz de...(회상~번스타인 저택)",
L"z009 마을 Part1(마을 테마)",
L"z010 이별 이벤트 장면(메르세데스)",
L"z011 마을 Part2(마을 테마)",
L"z012 희생(슬픈 이벤트)",
L"z013 ...Drake?(해적 습격)",
L"z014 전투(일반 전투)",
L"z015 Zephyr Falcon(해방군 제피어 팔콘)",
L"z016 The wind of Memory(시라노와 메르세데스 피아노곡)",
L"z017 사막(사막)",
L"z018 Aquamarine(최종 던전~해저 유적)",
L"z019 마검 아수라(아수라와의 전투)",
L"z020 Decisive Battle(보스 배틀)",
L"z021 음모(이벤트 장면(악역 전용))",
L"z022 Force Field(해방전쟁 필드)",
L"z023 FUZZY(스태프롤)",
L"z024 It's time to BATTLE!(보스 배틀 2)",
L"z025 PEACE(마을)",
L"z026 Precious Memories(시라노의 추억)",
L"z027 Sorrowful...(이벤트(보통 누군가가 죽은 후))",
L"z028 Subway Crisis(던전)",
L"z029 군도시(긴박한 마을 테마)",
L"z030 각성(최종 전투 직전)",
L"z031 b-e natural(던전)",
L"z032 Cesare -part1-(체사레 테마(오르간))",
L"z033 Cesare -part2-(라스트 배틀)",
L"z034 Fireroad(계약)",
L"z035 Forth step towards plain(필드, 숲 등)",
L"z036 THE GREAT REPEAT(게임 타이틀 화면)",
L"z037 어둠(인페르노 지하)",
L"z038 Jungle 2 Jungle(버스 섬)",
L"z039 Not natural but natural(무한 루프 던전)",
L"z041 The wind of Memory(오케스트라 믹스)(엔딩 테마)",
L"z042 망설임(공작 저택)",
L"z050 The wind of Memory(Phrase)(피아노)"
};

TCHAR tinishi_zh[][128] = {
L"z001 Briefing(西风猎鹰作战会议)",
L"z002 COOL FIELD(野外山岳地带)",
L"z003 Diablo(迪亚布罗第二阶段)",
L"z004 Gayl City(城镇)",
L"z005 破坏神之鼓动(迪亚布罗第一阶段)",
L"z006 Inferno(地狱监狱(开放作战))",
L"z007 Ioleen(安塔利亚创世记女主角主题)",
L"z008 Waltz de...(回忆~伯恩斯坦邸)",
L"z009 城镇 Part1(村庄主题)",
L"z010 告别事件场景(梅尔赛德斯)",
L"z011 城镇 Part2(城镇主题)",
L"z012 牺牲(悲伤事件)",
L"z013 ...Drake?(海盗袭击)",
L"z014 战斗(普通战斗)",
L"z015 Zephyr Falcon(解放军西风猎鹰)",
L"z016 The wind of Memory(西拉诺与梅尔赛德斯钢琴曲)",
L"z017 沙漠(沙漠)",
L"z018 Aquamarine(最终迷宫~海底遗迹)",
L"z019 魔剑阿修罗(与阿修罗之战)",
L"z020 Decisive Battle(头目战)",
L"z021 阴谋(事件场景(反派专用))",
L"z022 Force Field(解放战争野外)",
L"z023 FUZZY(制作人员)",
L"z024 It's time to BATTLE!(头目战2)",
L"z025 PEACE(村庄)",
L"z026 Precious Memories(西拉诺的回忆)",
L"z027 Sorrowful...(事件(通常有人死后))",
L"z028 Subway Crisis(迷宫)",
L"z029 军都市(紧张城镇主题)",
L"z030 觉醒(最终战直前)",
L"z031 b-e natural(迷宫)",
L"z032 Cesare -part1-(切萨雷主题(管风琴))",
L"z033 Cesare -part2-(最终战)",
L"z034 Fireroad(契约)",
L"z035 Forth step towards plain(野外、森林等)",
L"z036 THE GREAT REPEAT(游戏标题画面)",
L"z037 黑暗(地狱地下)",
L"z038 Jungle 2 Jungle(出生岛)",
L"z039 Not natural but natural(无限循环迷宫)",
L"z041 The wind of Memory(管弦乐混音)(结尾主题)",
L"z042 迷惘(公爵宅邸)",
L"z050 The wind of Memory(Phrase)(钢琴)"
};

TCHAR tinishi_ar[][128] = {
L"z001 Briefing(مجلس حرب Zephyr Falcon)",
L"z002 COOL FIELD(منطقة جبلية)",
L"z003 Diablo(المرحلة الثانية Diablo)",
L"z004 Gayl City(المدينة)",
L"z005 نبض المدمر(المرحلة الأولى Diablo)",
L"z006 Inferno(سجن Inferno(تحرير))",
L"z007 Ioleen(ثيم البطلة Antaria Genesis)",
L"z008 Waltz de...(ذكرى~مقر Bernstein)",
L"z009 المدينة جزء1(ثيم القرية)",
L"z010 مشهد الوداع(Mercedes)",
L"z011 المدينة جزء2(ثيم المدينة)",
L"z012 تضحية(حدث حزين)",
L"z013 ...Drake?(هجوم قراصنة)",
L"z014 معركة(معركة عادية)",
L"z015 Zephyr Falcon(جيش التحرير Zephyr Falcon)",
L"z016 The wind of Memory(مقطوعة بيانو Silvano و Mercedes)",
L"z017 صحراء(صحراء)",
L"z018 Aquamarine(زنزانة نهائية~آثار تحت الماء)",
L"z019 سيف شيطان Asura(معركة ضد Asura)",
L"z020 معركة حاسمة(معركة رئيسية)",
L"z021 مؤامرة(مشهد حدث(شرير))",
L"z022 Force Field(حقل حرب التحرير)",
L"z023 FUZZY(شكر الممثلين)",
L"z024 It's time to BATTLE!(معركة رئيسية 2)",
L"z025 PEACE(قرية)",
L"z026 Precious Memories(ذكرى Silvano)",
L"z027 Sorrowful...(حدث(عادة بعد موت))",
L"z028 Subway Crisis(زنزانة)",
L"z029 مدينة عسكرية(ثيم مدينة عاجل)",
L"z030 صحوة(قبل المعركة النهائية)",
L"z031 b-e natural(زنزانة)",
L"z032 Cesare -جزء1-(ثيم Cesare(أرغن))",
L"z033 Cesare -جزء2-(معركة أخيرة)",
L"z034 Fireroad(عقد)",
L"z035 Forth step towards plain(حقل، غابة، إلخ)",
L"z036 THE GREAT REPEAT(شاشة العنوان)",
L"z037 ظلام(أرض Inferno السفلية)",
L"z038 Jungle 2 Jungle(جزيرة Birth)",
L"z039 Not natural but natural(زنزانة حلقة لا نهائية)",
L"z041 The wind of Memory(ميكس أوركسترا)(ثيم الخاتمة)",
L"z042 تردد(مقر الدوق)",
L"z050 The wind of Memory(Phrase)(بيانو)"
};

TCHAR tinishi_ru[][128] = {
L"z001 Briefing(Военный совет Zephyr Falcon)",
L"z002 COOL FIELD(Горная местность)",
L"z003 Diablo(Вторая фаза Диабло)",
L"z004 Gayl City(Город)",
L"z005 Пульс Разрушителя(Первая фаза Диабло)",
L"z006 Inferno(Тюрьма Инферно(освобождение))",
L"z007 Ioleen(Тема героини Antaria Genesis)",
L"z008 Waltz de...(Воспоминание~Резиденция Бернстайн)",
L"z009 Город Часть1(Тема деревни)",
L"z010 Сцена прощания(Mercedes)",
L"z011 Город Часть2(Тема города)",
L"z012 Жертва(Грустное событие)",
L"z013 ...Drake?(Пиратская атака)",
L"z014 Битва(Обычная битва)",
L"z015 Zephyr Falcon(Армия освобождения Zephyr Falcon)",
L"z016 The wind of Memory(Фортепианная пьеса Сильвано и Mercedes)",
L"z017 Пустыня(Пустыня)",
L"z018 Aquamarine(Финальное подземелье~подводные руины)",
L"z019 Меч демона Асура(Битва с Асурой)",
L"z020 Решающая битва(Бой с боссом)",
L"z021 Заговор(Сцена события(злодей))",
L"z022 Force Field(Поле освободительной войны)",
L"z023 FUZZY(Титры)",
L"z024 It's time to BATTLE!(Бой с боссом 2)",
L"z025 PEACE(Деревня)",
L"z026 Precious Memories(Воспоминание Сильвано)",
L"z027 Sorrowful...(Событие(обычно после смерти))",
L"z028 Subway Crisis(Подземелье)",
L"z029 Военный город(Тема напряжённого города)",
L"z030 Пробуждение(Прямо перед финальной битвой)",
L"z031 b-e natural(Подземелье)",
L"z032 Cesare -часть1-(Тема Чезаре(орган))",
L"z033 Cesare -часть2-(Последняя битва)",
L"z034 Fireroad(Контракт)",
L"z035 Forth step towards plain(Поле, лес и т.д.)",
L"z036 THE GREAT REPEAT(Экран названия)",
L"z037 Тьма(Подземелье Инферно)",
L"z038 Jungle 2 Jungle(Остров Берт)",
L"z039 Not natural but natural(Подземелье с бесконечным циклом)",
L"z041 The wind of Memory(Оркестровый микс)(Тема концовки)",
L"z042 Колебание(Резиденция герцога)",
L"z050 The wind of Memory(Phrase)(Фортепиано)"
};

TCHAR tinishi_pt[][128] = {
L"z001 Briefing(Conselho de guerra Zephyr Falcon)",
L"z002 COOL FIELD(Região montanhosa)",
L"z003 Diablo(Segunda fase Diablo)",
L"z004 Gayl City(Cidade)",
L"z005 Pulso do Destruidor(Primeira fase Diablo)",
L"z006 Inferno(Prisão Inferno(libertação))",
L"z007 Ioleen(Tema heroína Antaria Genesis)",
L"z008 Waltz de...(Memória~Residência Bernstein)",
L"z009 Cidade Parte1(Tema vila)",
L"z010 Cena de despedida(Mercedes)",
L"z011 Cidade Parte2(Tema cidade)",
L"z012 Sacrifício(Evento triste)",
L"z013 ...Drake?(Ataque pirata)",
L"z014 Batalha(Batalha normal)",
L"z015 Zephyr Falcon(Exército libertação Zephyr Falcon)",
L"z016 The wind of Memory(Peça piano Silvano e Mercedes)",
L"z017 Deserto(Deserto)",
L"z018 Aquamarine(Dungeon final~ruínas submarinas)",
L"z019 Espada demônio Asura(Batalha contra Asura)",
L"z020 Batalha decisiva(Batalha chefe)",
L"z021 Conspiração(Cena evento(vilão))",
L"z022 Force Field(Campo guerra libertação)",
L"z023 FUZZY(Créditos)",
L"z024 It's time to BATTLE!(Batalha chefe 2)",
L"z025 PEACE(Vila)",
L"z026 Precious Memories(Memória de Silvano)",
L"z027 Sorrowful...(Evento(geralmente após morte))",
L"z028 Subway Crisis(Dungeon)",
L"z029 Cidade militar(Tema cidade tensa)",
L"z030 Despertar(Logo antes batalha final)",
L"z031 b-e natural(Dungeon)",
L"z032 Cesare -parte1-(Tema Cesare(órgão))",
L"z033 Cesare -parte2-(Última batalha)",
L"z034 Fireroad(Contrato)",
L"z035 Forth step towards plain(Campo, floresta, etc)",
L"z036 THE GREAT REPEAT(Tela título)",
L"z037 Escuridão(Subsolo Inferno)",
L"z038 Jungle 2 Jungle(Ilha Birth)",
L"z039 Not natural but natural(Dungeon loop infinito)",
L"z041 The wind of Memory(Mix orquestra)(Tema ending)",
L"z042 Hesitação(Residência ducal)",
L"z050 The wind of Memory(Phrase)(Piano)"
};

TCHAR tinishi_nl[][128] = {
L"z001 Briefing(Krijgsraad Zephyr Falcon)",
L"z002 COOL FIELD(Bergachtig gebied)",
L"z003 Diablo(Fase twee Diablo)",
L"z004 Gayl City(Stad)",
L"z005 Pols van de Vernietiger(Fase één Diablo)",
L"z006 Inferno(Inferno-gevangenis(bevrijding))",
L"z007 Ioleen(Antaria Genesis heldin-thema)",
L"z008 Waltz de...(Herinnering~Bernstein residentie)",
L"z009 Stad Deel1(Dorp-thema)",
L"z010 Afscheidsscène(Mercedes)",
L"z011 Stad Deel2(Stad-thema)",
L"z012 Offer(Droevige gebeurtenis)",
L"z013 ...Drake?(Piratenaanval)",
L"z014 Gevecht(Normaal gevecht)",
L"z015 Zephyr Falcon(Bevrijdingsleger Zephyr Falcon)",
L"z016 The wind of Memory(Pianostuk Silvano en Mercedes)",
L"z017 Woestijn(Woestijn)",
L"z018 Aquamarine(Eind-dungeon~onderwater ruïnes)",
L"z019 Demoonzwaard Asura(Gevecht tegen Asura)",
L"z020 Beslissende slag(Boss-gevecht)",
L"z021 Samenzwering(Gebeurtenisscène(schurk))",
L"z022 Force Field(Bevrijdingsoorlog veld)",
L"z023 FUZZY(Aftiteling)",
L"z024 It's time to BATTLE!(Boss-gevecht 2)",
L"z025 PEACE(Dorp)",
L"z026 Precious Memories(Silvano's herinnering)",
L"z027 Sorrowful...(Gebeurtenis(meestal na dood))",
L"z028 Subway Crisis(Dungeon)",
L"z029 Militaire stad(Urgent stad-thema)",
L"z030 Ontwaken(Vlak voor eindgevecht)",
L"z031 b-e natural(Dungeon)",
L"z032 Cesare -deel1-(Cesare-thema(orgel))",
L"z033 Cesare -deel2-(Laatste gevecht)",
L"z034 Fireroad(Contract)",
L"z035 Forth step towards plain(Veld, bos, etc)",
L"z036 THE GREAT REPEAT(Titelscherm)",
L"z037 Duisternis(Inferno ondergronds)",
L"z038 Jungle 2 Jungle(Birth-eiland)",
L"z039 Not natural but natural(Oneindige loop dungeon)",
L"z041 The wind of Memory(Orkest-mix)(Ending-thema)",
L"z042 Twijfel(Hertogresidentie)",
L"z050 The wind of Memory(Phrase)(Piano)"
};

TCHAR tinishi_pl[][128] = {
L"z001 Briefing(Rada wojenna Zephyr Falcon)",
L"z002 COOL FIELD(Region górski)",
L"z003 Diablo(Faza druga Diablo)",
L"z004 Gayl City(Miasto)",
L"z005 Puls Niszczyciela(Faza pierwsza Diablo)",
L"z006 Inferno(Więzienie Inferno(uwolnienie))",
L"z007 Ioleen(Temat bohaterki Antaria Genesis)",
L"z008 Waltz de...(Wspomnienie~Rezydencja Bernstein)",
L"z009 Miasto Część1(Temat wioski)",
L"z010 Scena pożegnania(Mercedes)",
L"z011 Miasto Część2(Temat miasta)",
L"z012 Ofiara(Smutne wydarzenie)",
L"z013 ...Drake?(Atak piratów)",
L"z014 Bitwa(Normalna bitwa)",
L"z015 Zephyr Falcon(Armia wyzwoleńcza Zephyr Falcon)",
L"z016 The wind of Memory(Utwór fortepianowy Silvano i Mercedes)",
L"z017 Pustynia(Pustynia)",
L"z018 Aquamarine(Ostatnie lochy~podwodne ruiny)",
L"z019 Miecz demona Asura(Bitwa z Asurą)",
L"z020 Bitwa decyzyjna(Bitwa z bossem)",
L"z021 Spisek(Scena wydarzenia(czarny charakter))",
L"z022 Force Field(Pole wojny wyzwoleńczej)",
L"z023 FUZZY(Napisy końcowe)",
L"z024 It's time to BATTLE!(Bitwa z bossem 2)",
L"z025 PEACE(Wioska)",
L"z026 Precious Memories(Wspomnienie Silvano)",
L"z027 Sorrowful...(Wydarzenie(zwykle po śmierci))",
L"z028 Subway Crisis(Lochy)",
L"z029 Miasto wojskowe(Temat napiętego miasta)",
L"z030 Przebudzenie(Tuż przed bitwą finałową)",
L"z031 b-e natural(Lochy)",
L"z032 Cesare -część1-(Temat Cesare(organy))",
L"z033 Cesare -część2-(Ostatnia bitwa)",
L"z034 Fireroad(Umowa)",
L"z035 Forth step towards plain(Pole, las, itd.)",
L"z036 THE GREAT REPEAT(Ekran tytułowy)",
L"z037 Ciemność(Podziemia Inferno)",
L"z038 Jungle 2 Jungle(Wyspa Birth)",
L"z039 Not natural but natural(Lochy nieskończonej pętli)",
L"z041 The wind of Memory(Mix orkiestrowy)(Temat zakończenia)",
L"z042 Wahanie(Rezydencja książęca)",
L"z050 The wind of Memory(Phrase)(Fortepian)"
};

TCHAR tinishi_tr[][128] = {
L"z001 Briefing(Zephyr Falcon savaş konseyi)",
L"z002 COOL FIELD(Dağlık bölge)",
L"z003 Diablo(İkinci aşama Diablo)",
L"z004 Gayl City(Şehir)",
L"z005 Yok Edici Nabzı(Birinci aşama Diablo)",
L"z006 Inferno(Inferno hapishanesi(kurtuluş))",
L"z007 Ioleen(Antaria Genesis kahraman teması)",
L"z008 Waltz de...(Anı~Bernstein konutu)",
L"z009 Şehir Bölüm1(Köy teması)",
L"z010 Veda sahnesi(Mercedes)",
L"z011 Şehir Bölüm2(Şehir teması)",
L"z012 Kurban(Üzücü olay)",
L"z013 ...Drake?(Korsan saldırısı)",
L"z014 Savaş(Normal savaş)",
L"z015 Zephyr Falcon(Kurtuluş ordusu Zephyr Falcon)",
L"z016 The wind of Memory(Silvano ve Mercedes piyano parçası)",
L"z017 Çöl(Çöl)",
L"z018 Aquamarine(Son zindan~sualtı harabeleri)",
L"z019 Şeytan kılıcı Asura(Asura ile savaş)",
L"z020 Kesin savaş(Patron savaşı)",
L"z021 Komplo(Olay sahnesi(kötü adam))",
L"z022 Force Field(Kurtuluş savaşı alanı)",
L"z023 FUZZY(Jenerik)",
L"z024 It's time to BATTLE!(Patron savaşı 2)",
L"z025 PEACE(Köy)",
L"z026 Precious Memories(Silvano'nun anısı)",
L"z027 Sorrowful...(Olay(genellikle ölüm sonrası))",
L"z028 Subway Crisis(Zindan)",
L"z029 Askeri şehir(Acil şehir teması)",
L"z030 Uyanış(Son savaş hemen öncesi)",
L"z031 b-e natural(Zindan)",
L"z032 Cesare -bölüm1-(Cesare teması(org))",
L"z033 Cesare -bölüm2-(Son savaş)",
L"z034 Fireroad(Sözleşme)",
L"z035 Forth step towards plain(Alan, orman, vb.)",
L"z036 THE GREAT REPEAT(Başlık ekranı)",
L"z037 Karanlık(Inferno yeraltı)",
L"z038 Jungle 2 Jungle(Birth adası)",
L"z039 Not natural but natural(Sonsuz döngü zindanı)",
L"z041 The wind of Memory(Orkestra miks)(Ending teması)",
L"z042 Tereddüt(Dük konutu)",
L"z050 The wind of Memory(Phrase)(Piyano)"
};

static inline CString NishiTrack(int i) {
	switch (savedata.lang) {
	case 0: return CString(tinishi[i]);
	case 1: return CString(tinishi_en[i]);
	case 2: return CString(tinishi_fr[i]);
	case 3: return CString(tinishi_it[i]);
	case 4: return CString(tinishi_es[i]);
	case 5: return CString(tinishi_ko[i]);
	case 6: return CString(tinishi_zh[i]);
	case 7: return CString(tinishi_ar[i]);
	case 8: return CString(tinishi_ru[i]);
	case 9: return CString(tinishi_de[i]);
	case 10: return CString(tinishi_pt[i]);
	case 11: return CString(tinishi_nl[i]);
	case 12: return CString(tinishi_pl[i]);
	case 13: return CString(tinishi_tr[i]);
	default: return CString(tinishi_en[i]);
	}
}

CString CNishi::Gett(int a) {
	CString s, ss;
	s = NishiTrack(a);
	ss = s.Left(4); ss.TrimRight();
	fnn = s.Mid(5);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CNishi::OnDblclkList1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int i = m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = NishiTrack(i);
	ret = s.Left(4); ret.TrimRight();
	ret2 = m_list.GetCurSel();
	if (s.Left(2) == "★") {
		fnn = s.Mid(2);
	}
	else {
		fnn = s.Mid(5);
	}
	EndDialog(1567);
}

BOOL CNishi::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"西風の狂詩曲(ラプソディー)", L"Rhapsody of the West Wind", L"Rhapsodie du Vent d'Ouest", L"Rapsodia del Vento d'Occidente", L"Rapsodia del Viento del Oeste", L"서풍의 광시곡", L"西风狂想曲", L"Rhapsody of the West Wind", L"Рапсодия Западного Ветра", L"Rhapsodie des Westwinds", L"Rapsódia do Vento Oeste", L"Rapsodie van de Westenwind", L"Rapsodia Zachodniego Wiatru", L"Batı Rüzgarı Rapsodisi"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for (int i = 0; i < (42); i++)
	{
		CString s = NishiTrack(i);
		dx = m_list.AddString(s);
		m_list.SetItemData(dx, i);
	}

	m_list.SetCurSel(0);
	if (ret2 != 0)
		//		if(ret>65) m_list.SetCurSel(ret);
		//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret2);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	// 例外: OCX プロパティ ページの戻り値は FALSE となります
}
