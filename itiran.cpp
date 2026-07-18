// itiran.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "itiran.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// itiran ダイアログ

extern CString fnn;
itiran::itiran(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(itiran::IDD, pParent)
{
	//{{AFX_DATA_INIT(itiran)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void itiran::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(itiran)
	DDX_Control(pDX, IDC_LIST1, m_list);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(itiran, CCustomBlurDialogBase)
	//{{AFX_MSG_MAP(itiran)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDOK, &itiran::OnBnClickedOk)
	cmn(itiran);

/////////////////////////////////////////////////////////////////////////////
// itiran メッセージ ハンドラ

TCHAR ti[][50] = {
L"001 --/SC 風を共に舞う気持ち SC Ver",
L"010 --/SC Shine of Eidos ～空の軌跡～",
L"011 --/SC OP 銀の意志 金の翼/山脇宏子",
L"012 --/SC ED I swear.../小寺可南子",
L"015 --/SC 解き放たれた至宝",
L"016 --/SC 女神の御許へ",
L"017 --/SC 折られた翼",
L"018 --/SC 希望の行方",
L"100 FC/SC 地方都市ロレント",
L"101 FC/SC 商業都市ボース",
L"102 FC/SC 海港都市ルーアン",
L"103 FC/SC 工房都市ツァイス",
L"104 FC/SC 王都グランセル",
L"105 FC/SC 陽だまりにて和む猫",
L"106 FC/SC 国境警備も楽じゃない",
L"107 FC/SC 王城",
L"108 FC/SC グランアリーナ",
L"110 --/SC ル＝ロックルへようこそ",
L"111 --/SC 灯火が消えた街",
L"112 --/SC Heartless Surprise Attack",
L"113 --/SC 飛行戦艦グロリアス",
L"200 FC/-- リベールの歩き方",
L"201 FC/SC Secret Green Passage",
L"202 FC/SC Rock on the Road",
L"210 --/SC 空を見上げて",
L"300 FC/SC 闇を彷徨う",
L"301 FC/SC 行く手をはばむ鋼の床",
L"302 FC/SC 暗がりがくれた安らぎ",
L"303 FC/SC 四輪の塔",
L"304 FC/SC レイストン要塞",
L"305 FC/-- 虚ろなる光の封土",
L"310 --/SC 隠された真の姿",
L"311 --/SC 潜入",
L"312 --/SC 浮遊都市リベルアーク",
L"313 --/SC その先を目指して",
L"314 --/SC 中枢塔《アクシスピラー》",
L"315 --/SC ★効果音★",
L"316 --/SC 仲間の元へ",
L"400 FC/-- Sophisticated Fight",
L"402 FC/SC To be Suggestive",
L"403 FC/SC 銀の意志",
L"404 FC/SC Challenger Invited",
L"405 FC/-- Ancient Makes",
L"406 FC/-- 至宝を守護せしモノ",
L"407 FC/SC 撃破！！",
L"408 FC/SC 消え行く星",
L"410 FC/SC ピンチ！！",
L"420 --/SC Strepitoso Fight",
L"421 --/SC The Fate Of The Fairies",
L"422 --/SC Obstructive Existence",
L"423 --/SC Fight with Assailant",
L"424 --/SC 大いなる畏怖",
L"425 --/SC Fateful confrontation",
L"426 --/SC Outskirts of Evolution",
L"427 --/SC The Merciless Savior",
L"428 --/SC 雷の穿つ墓標",
L"429 --/SC Feeling Danger Nearby",
L"500 FC/SC 星の在り処 Harmonica short Ver.",
L"501 FC/-- 琥珀の愛 Hum Ver.",
L"502 FC/-- 琥珀の愛 Piano Ver.",
L"503 FC/-- 琥珀の愛 Lute Ver.",
L"504 FC/-- 星の在り処 Harmonica long Ver.",
L"505 FC/SC 賑やかに行こう",
L"510 FC/SC 去り行く決意",
L"511 FC/SC 暗躍する者たち",
L"512 FC/-- 奴を逃がすな！",
L"513 FC/SC 胸の中に",
L"514 FC/SC 月明りの下で",
L"516 FC/SC 忍び寄る危機",
L"517 FC/-- 俺達カプア一家！",
L"518 FC/-- 旅立ちの小径",
L"519 FC/SC 奪還",
L"520 FC/-- 呪縛からの解放、そして・・・",
L"521 FC/SC 告白",
L"522 FC/SC 黒のオーブメント",
L"523 FC/SC リベールの誇り",
L"530 FC/-- (劇)姫の悩み",
L"531 FC/-- (劇)騎士達の嘆き",
L"532 FC/-- (劇)それぞれの思惑",
L"533 FC/-- (劇)城",
L"534 FC/-- (劇)コロシアム",
L"535 FC/-- (劇)決闘",
L"536 FC/-- (劇)姫の死",
L"537 FC/-- (劇)大団円",
L"540 --/SC 陰謀",
L"541 --/SC 執行者",
L"542 --/SC 福音計画",
L"543 --/SC 迫りくる脅威",
L"544 --/SC ハーメル",
L"546 --/SC うちひしがれて",
L"547 --/SC 荒野に潜む影",
L"548 --/SC 夢の続き",
L"549 --/SC 絆の在り処",
L"550 --/SC 銀の意志 Super Arrange Ver",
L"551 --/SC 星の在り処 Instrumental ver",
L"552 --/SC Etude of the Ruin",
L"554 --/SC 惨劇の真相",
L"556 --/SC 夢幻",
L"★FALCOMロゴ動画",
L"★オープニング動画",
L"★エンディング動画",
L"★動画1",
L"★動画2",
L"★動画3",
L"★動画4",
L"★動画5",
L"★動画6",
L"★動画7"
};

TCHAR ti_en[][150] = {
L"001 --/SC Dancing with the Wind SC Ver",
L"010 --/SC Shine of Eidos ~Trails in the Sky~",
L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",
L"012 --/SC ED I swear.../Kodera Kanako",
L"015 --/SC Unleashed Treasure",
L"016 --/SC To the Goddess",
L"017 --/SC Broken Wings",
L"018 --/SC Where Hope Goes",
L"100 FC/SC Provincial City Rolent",
L"101 FC/SC Commercial City Bose",
L"102 FC/SC Port City Ruan",
L"103 FC/SC Workshop City Zeiss",
L"104 FC/SC Royal Capital Grancel",
L"105 FC/SC Cat Relaxing in the Sun",
L"106 FC/SC Border Patrol Isn't Easy",
L"107 FC/SC Royal Castle",
L"108 FC/SC Grand Arena",
L"110 --/SC Welcome to Le Locle",
L"111 --/SC Town Where the Lights Went Out",
L"112 --/SC Heartless Surprise Attack",
L"113 --/SC Flying Battleship Glorious",
L"200 FC/-- Walking in Liberl",
L"201 FC/SC Secret Green Passage",
L"202 FC/SC Rock on the Road",
L"210 --/SC Look Up at the Sky",
L"300 FC/SC Wandering in the Darkness",
L"301 FC/SC Steel Floor Blocking the Path",
L"302 FC/SC Peace in the Darkness",
L"303 FC/SC Tetracyclic Towers",
L"304 FC/SC Leiston Fortress",
L"305 FC/-- Hollow Land of Light",
L"310 --/SC Hidden True Form",
L"311 --/SC Infiltration",
L"312 --/SC Floating City Liber Ark",
L"313 --/SC Aiming Beyond",
L"314 --/SC Central Tower Axis Pillar",
L"315 --/SC ★Sound Effects★",
L"316 --/SC To Our Comrades",
L"400 FC/-- Sophisticated Fight",
L"402 FC/SC To be Suggestive",
L"403 FC/SC Silver Will",
L"404 FC/SC Challenger Invited",
L"405 FC/-- Ancient Makes",
L"406 FC/-- Guardian of the Treasure",
L"407 FC/SC Crush!!",
L"408 FC/SC Disappearing Star",
L"410 FC/SC Pinch!!",
L"420 --/SC Strepitoso Fight",
L"421 --/SC The Fate Of The Fairies",
L"422 --/SC Obstructive Existence",
L"423 --/SC Fight with Assailant",
L"424 --/SC Great Dread",
L"425 --/SC Fateful confrontation",
L"426 --/SC Outskirts of Evolution",
L"427 --/SC The Merciless Savior",
L"428 --/SC Grave Marker Pierced by Lightning",
L"429 --/SC Feeling Danger Nearby",
L"500 FC/SC Where the Stars Are Harmonica short Ver.",
L"501 FC/-- Amber Love Hum Ver.",
L"502 FC/-- Amber Love Piano Ver.",
L"503 FC/-- Amber Love Lute Ver.",
L"504 FC/-- Where the Stars Are Harmonica long Ver.",
L"505 FC/SC Let's Go Lively",
L"510 FC/SC Determination to Leave",
L"511 FC/SC Those Who Move in the Shadows",
L"512 FC/-- Don't Let Him Escape!",
L"513 FC/SC In My Heart",
L"514 FC/SC Under the Moonlight",
L"516 FC/SC Creeping Crisis",
L"517 FC/-- We're the Capua Family!",
L"518 FC/-- Path of Departure",
L"519 FC/SC Recapture",
L"520 FC/-- Liberation from the Curse, and...",
L"521 FC/SC Confession",
L"522 FC/SC Black Ouroboros",
L"523 FC/SC Pride of Liberl",
L"530 FC/-- (Drama) Princess's Worry",
L"531 FC/-- (Drama) Knights' Lament",
L"532 FC/-- (Drama) Each One's Scheme",
L"533 FC/-- (Drama) Castle",
L"534 FC/-- (Drama) Colosseum",
L"535 FC/-- (Drama) Duel",
L"536 FC/-- (Drama) Princess's Death",
L"537 FC/-- (Drama) Grand Finale",
L"540 --/SC Conspiracy",
L"541 --/SC Enforcer",
L"542 --/SC Gospel Plan",
L"543 --/SC Approaching Threat",
L"544 --/SC Hamel",
L"546 --/SC Crushed",
L"547 --/SC Shadow Lurking in the Wasteland",
L"548 --/SC Continuation of the Dream",
L"549 --/SC Where Bonds Are",
L"550 --/SC Silver Will Super Arrange Ver",
L"551 --/SC Where the Stars Are Instrumental ver",
L"552 --/SC Etude of the Ruin",
L"554 --/SC Truth of the Tragedy",
L"556 --/SC Phantasm",
L"★FALCOM logo video",
L"★Opening video",
L"★Ending video",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5",
L"★Video 6",
L"★Video 7"
};

static const TCHAR ti_fr[][150] = {
L"001 --/SC Sentiments dansant avec le vent SC Ver",
L"010 --/SC Shine of Eidos ~Trails in the Sky~",
L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",
L"012 --/SC ED I swear.../Kodera Kanako",
L"015 --/SC Trésor libéré",
L"016 --/SC Vers la déesse",
L"017 --/SC Ailes brisées",
L"018 --/SC Où va l'espoir",
L"100 FC/SC Rolent - Ville provinciale",
L"101 FC/SC Bose - Ville commerciale",
L"102 FC/SC Ruan - Ville portuaire",
L"103 FC/SC Zeiss - Ville atelier",
L"104 FC/SC Grancel - Capitale royale",
L"105 FC/SC Chat au soleil",
L"106 FC/SC Patrouille frontière pas facile",
L"107 FC/SC Château royal",
L"108 FC/SC Grand Arena",
L"110 --/SC Bienvenue à Le Locle",
L"111 --/SC Ville où les lumières se sont éteintes",
L"112 --/SC Heartless Surprise Attack",
L"113 --/SC Cuirassé volant Glorious",
L"200 FC/-- Marcher dans Liberl",
L"201 FC/SC Secret Green Passage",
L"202 FC/SC Rock on the Road",
L"210 --/SC Regarder le ciel",
L"300 FC/SC Errance dans les ténèbres",
L"301 FC/SC Plancher d'acier bloquant le chemin",
L"302 FC/SC Paix des ténèbres",
L"303 FC/SC Tours tétracycliques",
L"304 FC/SC Forteresse Leiston",
L"305 FC/-- Terre vacante de lumière",
L"310 --/SC Forme vraie cachée",
L"311 --/SC Infiltration",
L"312 --/SC Cité flottante Liber Ark",
L"313 --/SC Viser au-delà",
L"314 --/SC Tour centrale Axis Pillar",
L"315 --/SC ★Sound Effects★",
L"316 --/SC Vers nos compagnons",
L"400 FC/-- Sophisticated Fight",
L"402 FC/SC To be Suggestive",
L"403 FC/SC Volonté d'argent",
L"404 FC/SC Challenger Invited",
L"405 FC/-- Ancient Makes",
L"406 FC/-- Gardien du trésor",
L"407 FC/SC Écrasement!!",
L"408 FC/SC Étoile défaillante",
L"410 FC/SC Pinch!!",
L"420 --/SC Strepitoso Fight",
L"421 --/SC The Fate Of The Fairies",
L"422 --/SC Obstructive Existence",
L"423 --/SC Fight with Assailant",
L"424 --/SC Grande terreur",
L"425 --/SC Fateful confrontation",
L"426 --/SC Outskirts of Evolution",
L"427 --/SC The Merciless Savior",
L"428 --/SC Stèle transpercée par la foudre",
L"429 --/SC Feeling Danger Nearby",
L"500 FC/SC Où sont les étoiles Harmonica short Ver.",
L"501 FC/-- Amour d'ambre Hum Ver.",
L"502 FC/-- Amour d'ambre Piano Ver.",
L"503 FC/-- Amour d'ambre Lute Ver.",
L"504 FC/-- Où sont les étoiles Harmonica long Ver.",
L"505 FC/SC Allons gaiement",
L"510 FC/SC Décision de partir",
L"511 FC/SC Ceux qui agissent dans l'ombre",
L"512 FC/-- Ne le laissez pas s'échapper!",
L"513 FC/SC Dans mon cœur",
L"514 FC/SC Sous le clair de lune",
L"516 FC/SC Crise rampante",
L"517 FC/-- Nous sommes la famille Capua!",
L"518 FC/-- Sentier du départ",
L"519 FC/SC Reprise",
L"520 FC/-- Libération de la malédiction, et...",
L"521 FC/SC Aveu",
L"522 FC/SC Orbement noir",
L"523 FC/SC Fierté de Liberl",
L"530 FC/-- (Drame) Souci de la princesse",
L"531 FC/-- (Drame) Lamentation des chevaliers",
L"532 FC/-- (Drame) Intentions de chacun",
L"533 FC/-- (Drame) Château",
L"534 FC/-- (Drame) Colisée",
L"535 FC/-- (Drame) Duel",
L"536 FC/-- (Drame) Mort de la princesse",
L"537 FC/-- (Drame) Grand final",
L"540 --/SC Complot",
L"541 --/SC Exécuteur",
L"542 --/SC Plan Évangile",
L"543 --/SC Menace approchant",
L"544 --/SC Hamel",
L"546 --/SC Écrasé",
L"547 --/SC Ombre dans le désert",
L"548 --/SC Suite du rêve",
L"549 --/SC Où sont les liens",
L"550 --/SC Silver Will Super Arrange Ver",
L"551 --/SC Where the Stars Are Instrumental ver",
L"552 --/SC Etude of the Ruin",
L"554 --/SC Vérité de la tragédie",
L"556 --/SC Phantasm",
L"★FALCOM logo video",
L"★Opening video",
L"★Ending video",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5",
L"★Video 6",
L"★Video 7"
};

#define ITIRAN_LANG(SUF) TCHAR ti_##SUF[][250]
#define ITIRAN_LANG_END ;

ITIRAN_LANG(it) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(es) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(ko) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(zh) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(ar) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(ru) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(de) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(pt) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(nl) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(pl) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END
ITIRAN_LANG(tr) = { L"001 --/SC Dancing with the Wind SC Ver",L"010 --/SC Shine of Eidos ~Trails in the Sky~",L"011 --/SC OP Silver Will Golden Wings/Yamawaki Hiroko",L"012 --/SC ED I swear.../Kodera Kanako",L"015 --/SC Unleashed Treasure",L"016 --/SC To the Goddess",L"017 --/SC Broken Wings",L"018 --/SC Where Hope Goes",L"100 FC/SC Provincial City Rolent",L"101 FC/SC Commercial City Bose",L"102 FC/SC Port City Ruan",L"103 FC/SC Workshop City Zeiss",L"104 FC/SC Royal Capital Grancel",L"105 FC/SC Cat Relaxing in the Sun",L"106 FC/SC Border Patrol Isn't Easy",L"107 FC/SC Royal Castle",L"108 FC/SC Grand Arena",L"110 --/SC Welcome to Le Locle",L"111 --/SC Town Where the Lights Went Out",L"112 --/SC Heartless Surprise Attack",L"113 --/SC Flying Battleship Glorious",L"200 FC/-- Walking in Liberl",L"201 FC/SC Secret Green Passage",L"202 FC/SC Rock on the Road",L"210 --/SC Look Up at the Sky",L"300 FC/SC Wandering in the Darkness",L"301 FC/SC Steel Floor Blocking the Path",L"302 FC/SC Peace in the Darkness",L"303 FC/SC Tetracyclic Towers",L"304 FC/SC Leiston Fortress",L"305 FC/-- Hollow Land of Light",L"310 --/SC Hidden True Form",L"311 --/SC Infiltration",L"312 --/SC Floating City Liber Ark",L"313 --/SC Aiming Beyond",L"314 --/SC Central Tower Axis Pillar",L"315 --/SC ★Sound Effects★",L"316 --/SC To Our Comrades",L"400 FC/-- Sophisticated Fight",L"402 FC/SC To be Suggestive",L"403 FC/SC Silver Will",L"404 FC/SC Challenger Invited",L"405 FC/-- Ancient Makes",L"406 FC/-- Guardian of the Treasure",L"407 FC/SC Crush!!",L"408 FC/SC Disappearing Star",L"410 FC/SC Pinch!!",L"420 --/SC Strepitoso Fight",L"421 --/SC The Fate Of The Fairies",L"422 --/SC Obstructive Existence",L"423 --/SC Fight with Assailant",L"424 --/SC Great Dread",L"425 --/SC Fateful confrontation",L"426 --/SC Outskirts of Evolution",L"427 --/SC The Merciless Savior",L"428 --/SC Grave Marker Pierced by Lightning",L"429 --/SC Feeling Danger Nearby",L"500 FC/SC Where the Stars Are Harmonica short Ver.",L"501 FC/-- Amber Love Hum Ver.",L"502 FC/-- Amber Love Piano Ver.",L"503 FC/-- Amber Love Lute Ver.",L"504 FC/-- Where the Stars Are Harmonica long Ver.",L"505 FC/SC Let's Go Lively",L"510 FC/SC Determination to Leave",L"511 FC/SC Those Who Move in the Shadows",L"512 FC/-- Don't Let Him Escape!",L"513 FC/SC In My Heart",L"514 FC/SC Under the Moonlight",L"516 FC/SC Creeping Crisis",L"517 FC/-- We're the Capua Family!",L"518 FC/-- Path of Departure",L"519 FC/SC Recapture",L"520 FC/-- Liberation from the Curse, and...",L"521 FC/SC Confession",L"522 FC/SC Black Ouroboros",L"523 FC/SC Pride of Liberl",L"530 FC/-- (Drama) Princess's Worry",L"531 FC/-- (Drama) Knights' Lament",L"532 FC/-- (Drama) Each One's Scheme",L"533 FC/-- (Drama) Castle",L"534 FC/-- (Drama) Colosseum",L"535 FC/-- (Drama) Duel",L"536 FC/-- (Drama) Princess's Death",L"537 FC/-- (Drama) Grand Finale",L"540 --/SC Conspiracy",L"541 --/SC Enforcer",L"542 --/SC Gospel Plan",L"543 --/SC Approaching Threat",L"544 --/SC Hamel",L"546 --/SC Crushed",L"547 --/SC Shadow Lurking in the Wasteland",L"548 --/SC Continuation of the Dream",L"549 --/SC Where Bonds Are",L"550 --/SC Silver Will Super Arrange Ver",L"551 --/SC Where the Stars Are Instrumental ver",L"552 --/SC Etude of the Ruin",L"554 --/SC Truth of the Tragedy",L"556 --/SC Phantasm",L"★FALCOM logo video",L"★Opening video",L"★Ending video",L"★Video 1",L"★Video 2",L"★Video 3",L"★Video 4",L"★Video 5",L"★Video 6",L"★Video 7" } ITIRAN_LANG_END

#undef ITIRAN_LANG
#undef ITIRAN_LANG_END

static inline CString ITIRAN_TRACK(int i) { switch (savedata.lang) { case 0: return GameTrackTitle(ti[i]); case 1: return GameTrackTitle(ti_en[i]); case 2: return GameTrackTitle(ti_fr[i]); case 3: return GameTrackTitle(ti_it[i]); case 4: return GameTrackTitle(ti_es[i]); case 5: return GameTrackTitle(ti_ko[i]); case 6: return GameTrackTitle(ti_zh[i]); case 7: return GameTrackTitle(ti_ar[i]); case 8: return GameTrackTitle(ti_ru[i]); case 9: return GameTrackTitle(ti_de[i]); case 10: return GameTrackTitle(ti_pt[i]); case 11: return GameTrackTitle(ti_nl[i]); case 12: return GameTrackTitle(ti_pl[i]); case 13: return GameTrackTitle(ti_tr[i]); default: return GameTrackTitle(ti_en[i]); } }

CString itiran::Gett(int a) {
	CString s;
	s = ITIRAN_TRACK(a);
	fnn = s.Mid(10);
	return s;
}

void itiran::OnDblclkList1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx = m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = ITIRAN_TRACK(idx);
	ret = _tstoi(s.Left(3));
	ret2 = m_list.GetCurSel();
	if (ret2 > 97)
		ret = ret2;
#if UNICODE
	if (s.Left(1) == "★") {
		fnn = s.Mid(1);
#else
	if (s.Left(2) == "★") {
		fnn = s.Mid(2);
#endif
	}
	else {
		fnn = ""; if (s.GetLength() > 3)
			fnn = s.Mid(10);
	}
	EndDialog(1567);
	}

BOOL itiran::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"空の軌跡 Second Chapter", L"Trails in the Sky Second Chapter", L"Les Sentiers du Ciel Second Chapitre", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"하늘의 궤적 Second Chapter", L"空之轨迹 Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter", L"Trails in the Sky Second Chapter"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for (int i = 0; i < 98; i++)
	{
		CString s;
		s = "ED6";
		s += ITIRAN_TRACK(i);
		//		s+="";
		/*		switch(ti[i][0])
				{
				case '0':
					s+="▼(タイトル)";
					break;
				case '1':
					s+="▼(町↓)";
					break;
				case '2':
					s+="▼(フィールド↓)";
					break;
				case '3':
					s+="▼(ダンジョン↓)";
					break;
				case '4':
					s+="▼(戦闘↓)";
					break;
				case '5':
					s+="▼(イベント↓)";
					break;
				}
		*/		dx = m_list.AddString(s);
		m_list.SetItemData(dx, i);
	}
	dx = m_list.AddString(LL14(L"★FALCOMロゴ動画", L"★FALCOM logo video", L"★Vidéo logo FALCOM", L"★Video logo FALCOM", L"★Vídeo logo FALCOM", L"★FALCOM 로고 동영상", L"★FALCOM 标志视频", L"★فيديو شعار FALCOM", L"★Видео логотипа FALCOM", L"★FALCOM-Logo-Video", L"★Vídeo logótipo FALCOM", L"★FALCOM-logovideo", L"★Wideo logo FALCOM", L"★FALCOM logo videosu"));
	m_list.SetItemData(dx, 98);
	dx = m_list.AddString(LL14(L"★オープニング動画", L"★Opening video", L"★Vidéo d'ouverture", L"★Video di apertura", L"★Vídeo de apertura", L"★오프닝 동영상", L"★开场视频", L"★فيديو الافتتاح", L"★Видео заставки", L"★Opening-Video", L"★Vídeo de abertura", L"★Openingvideo", L"★Wideo openingu", L"★Açılış videosu"));
	m_list.SetItemData(dx, 99);
	dx = m_list.AddString(LL14(L"★エンディング動画", L"★Ending video", L"★Vidéo de fin", L"★Video finale", L"★Vídeo final", L"★엔딩 동영상", L"★结尾视频", L"★فيديو النهاية", L"★Видео концовки", L"★Endvideo", L"★Vídeo final", L"★Eindvideo", L"★Wideo końcowe", L"★Bitiş videosu"));
	m_list.SetItemData(dx, 100);
	dx = m_list.AddString(LL14(L"★動画1", L"★Video 1", L"★Vidéo 1", L"★Video 1", L"★Vídeo 1", L"★동영상 1", L"★视频1", L"★فيديو 1", L"★Видео 1", L"★Video 1", L"★Vídeo 1", L"★Video 1", L"★Wideo 1", L"★Video 1"));
	m_list.SetItemData(dx, 101);
	dx = m_list.AddString(LL14(L"★動画2", L"★Video 2", L"★Vidéo 2", L"★Video 2", L"★Vídeo 2", L"★동영상 2", L"★视频2", L"★فيديو 2", L"★Видео 2", L"★Video 2", L"★Vídeo 2", L"★Video 2", L"★Wideo 2", L"★Video 2"));
	m_list.SetItemData(dx, 102);
	dx = m_list.AddString(LL14(L"★動画3", L"★Video 3", L"★Vidéo 3", L"★Video 3", L"★Vídeo 3", L"★동영상 3", L"★视频3", L"★فيديو 3", L"★Видео 3", L"★Video 3", L"★Vídeo 3", L"★Video 3", L"★Wideo 3", L"★Video 3"));
	m_list.SetItemData(dx, 103);
	dx = m_list.AddString(LL14(L"★動画4", L"★Video 4", L"★Vidéo 4", L"★Video 4", L"★Vídeo 4", L"★동영상 4", L"★视频4", L"★فيديو 4", L"★Видео 4", L"★Video 4", L"★Vídeo 4", L"★Video 4", L"★Wideo 4", L"★Video 4"));
	m_list.SetItemData(dx, 104);
	dx = m_list.AddString(LL14(L"★動画5", L"★Video 5", L"★Vidéo 5", L"★Video 5", L"★Vídeo 5", L"★동영상 5", L"★视频5", L"★فيديو 5", L"★Видео 5", L"★Video 5", L"★Vídeo 5", L"★Video 5", L"★Wideo 5", L"★Video 5"));
	m_list.SetItemData(dx, 105);
	dx = m_list.AddString(LL14(L"★動画6", L"★Video 6", L"★Vidéo 6", L"★Video 6", L"★Vídeo 6", L"★동영상 6", L"★视频6", L"★فيديو 6", L"★Видео 6", L"★Video 6", L"★Vídeo 6", L"★Video 6", L"★Wideo 6", L"★Video 6"));
	m_list.SetItemData(dx, 106);
	dx = m_list.AddString(LL14(L"★動画7", L"★Video 7", L"★Vidéo 7", L"★Video 7", L"★Vídeo 7", L"★동영상 7", L"★视频7", L"★فيديو 7", L"★Видео 7", L"★Video 7", L"★Vídeo 7", L"★Video 7", L"★Wideo 7", L"★Video 7"));
	m_list.SetItemData(dx, 107);

	m_list.SetCurSel(0);
	if (ret2 != 0) m_list.SetCurSel(ret2);

	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	// 例外: OCX プロパティ ページの戻り値は FALSE となります
}

void itiran::OnBnClickedOk()
{
	CCustomBlurDialogBase::OnOK();
}

