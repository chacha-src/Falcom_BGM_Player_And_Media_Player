// Br4.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Br4.h"


// CBr4 ダイアログ

IMPLEMENT_DYNAMIC(CBr4, CCustomBlurDialogBase)

CBr4::CBr4(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CBr4::IDD, pParent)
{

}

CBr4::~CBr4()
{
}

void CBr4::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CBr4, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
cmn(CBr4);

// CBr4 メッセージ ハンドラ
extern CString fnn;

TCHAR tibr4[][128]={
L"VT01DA Brandish4 -OP-", 
L"VT03DA 古の祈り-meditation-", 
L"VT05DA 遺跡-the prop-", 
L"VT13DA ショップ-relation-", 
L"VT32DA 迷宮-roar-", 
L"VT04DA 街区-division-", 
L"VT14DA カジノ-eighth wonder-", 
L"VT19DA 遊戯-hammer slime-", 
L"VT15DA 闘場-black field-", 
L"VT16DA 塔の謎-mysterious-", 
L"VT27DA 遺跡2-debris-", 
L"VT18DA 死闘-trigger-", 
L"VT28DA 遺跡3-deep haze-", 
L"VT06DA 水域-the abyss-", 
L"VT07DA 庭園-bramble-", 
L"VT33DA 召喚-hostile-", 
L"VT08DA 城塞-solid steel-", 
L"VT09DA 聖堂-judgement-", 
L"VT17DA 塔の復活-retribution-", 
L"VT10DA 胎内-forbidden power-", 
L"VT30DA 決闘-fatal riders-", 
L"VT11DA 神殿-heritage-", 
L"VT12DA 忘却の迷宮-another door-", 
L"VT34DA 祭壇-altar-", 
L"VT20DA ギリアス-victim-", 
L"VT39DA エピローグI-daybreak-", 
L"VT40DA エピローグII-twilght-", 
L"VT41DA エピローグIV-broken chain-", 
L"VT21DA VT21(エンディング用)", 
L"VT22DA VT22(エンディング用)", 
L"VT35DA ENDING", 
L"VT02DA Staff", 
L"VT36DA 休息-slumber-", 
L"VT31DA GAME OVER", 
L"VT38DA イントロダクション", 
L"VT43DA ジングル：詩人1", 
L"VT44DA ジングル：詩人2", 
L"VT45DA ジングル：詩人3", 
L"VT46DA ジングル：詩人4", 
L"VT47DA ジングル：詩人5", 
L"VT48DA ジングル：詩人6", 
L"VT49DA ジングル：詩人7"
};

TCHAR tibr4_en[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Ancient Prayer-meditation-",
L"VT05DA Ruins-the prop-",
L"VT13DA Shop-relation-",
L"VT32DA Labyrinth-roar-",
L"VT04DA District-division-",
L"VT14DA Casino-eighth wonder-",
L"VT19DA Game-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Tower Mystery-mysterious-",
L"VT27DA Ruins 2-debris-",
L"VT18DA Death Battle-trigger-",
L"VT28DA Ruins 3-deep haze-",
L"VT06DA Waters-the abyss-",
L"VT07DA Garden-bramble-",
L"VT33DA Summon-hostile-",
L"VT08DA Fortress-solid steel-",
L"VT09DA Cathedral-judgement-",
L"VT17DA Tower Revival-retribution-",
L"VT10DA Womb-forbidden power-",
L"VT30DA Duel-fatal riders-",
L"VT11DA Temple-heritage-",
L"VT12DA Labyrinth of Oblivion-another door-",
L"VT34DA Altar-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epilogue I-daybreak-",
L"VT40DA Epilogue II-twilight-",
L"VT41DA Epilogue IV-broken chain-",
L"VT21DA VT21(Ending)",
L"VT22DA VT22(Ending)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Rest-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introduction",
L"VT43DA Jingle: Poet 1",
L"VT44DA Jingle: Poet 2",
L"VT45DA Jingle: Poet 3",
L"VT46DA Jingle: Poet 4",
L"VT47DA Jingle: Poet 5",
L"VT48DA Jingle: Poet 6",
L"VT49DA Jingle: Poet 7"
};

TCHAR tibr4_fr[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Prière Ancestrale-meditation-",
L"VT05DA Ruines-the prop-",
L"VT13DA Magasin-relation-",
L"VT32DA Labyrinthe-roar-",
L"VT04DA Quartier-division-",
L"VT14DA Casino-eighth wonder-",
L"VT19DA Jeu-hammer slime-",
L"VT15DA Arène-black field-",
L"VT16DA Mystère de la Tour-mysterious-",
L"VT27DA Ruines 2-debris-",
L"VT18DA Combat à Mort-trigger-",
L"VT28DA Ruines 3-deep haze-",
L"VT06DA Eaux-the abyss-",
L"VT07DA Jardin-bramble-",
L"VT33DA Invocation-hostile-",
L"VT08DA Forteresse-solid steel-",
L"VT09DA Cathédrale-judgement-",
L"VT17DA Renaissance de la Tour-retribution-",
L"VT10DA Matrice-forbidden power-",
L"VT30DA Duel-fatal riders-",
L"VT11DA Temple-heritage-",
L"VT12DA Labyrinthe de l'Oubli-another door-",
L"VT34DA Autel-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Épilogue I-daybreak-",
L"VT40DA Épilogue II-twilight-",
L"VT41DA Épilogue IV-broken chain-",
L"VT21DA VT21(Fin)",
L"VT22DA VT22(Fin)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Repos-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introduction",
L"VT43DA Jingle: Poète 1",
L"VT44DA Jingle: Poète 2",
L"VT45DA Jingle: Poète 3",
L"VT46DA Jingle: Poète 4",
L"VT47DA Jingle: Poète 5",
L"VT48DA Jingle: Poète 6",
L"VT49DA Jingle: Poète 7"
};

TCHAR tibr4_it[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Preghiera Antica-meditation-",
L"VT05DA Rovine-the prop-",
L"VT13DA Negozio-relation-",
L"VT32DA Labirinto-roar-",
L"VT04DA Distretto-division-",
L"VT14DA Casinò-eighth wonder-",
L"VT19DA Gioco-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Mistero della Torre-mysterious-",
L"VT27DA Rovine 2-debris-",
L"VT18DA Battaglia Mortale-trigger-",
L"VT28DA Rovine 3-deep haze-",
L"VT06DA Acque-the abyss-",
L"VT07DA Giardino-bramble-",
L"VT33DA Evocazione-hostile-",
L"VT08DA Fortezza-solid steel-",
L"VT09DA Cattedrale-judgement-",
L"VT17DA Resurrezione della Torre-retribution-",
L"VT10DA Grembo-forbidden power-",
L"VT30DA Duello-fatal riders-",
L"VT11DA Tempio-heritage-",
L"VT12DA Labirinto dell'Oblio-another door-",
L"VT34DA Altare-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epilogo I-daybreak-",
L"VT40DA Epilogo II-twilight-",
L"VT41DA Epilogo IV-broken chain-",
L"VT21DA VT21(Finale)",
L"VT22DA VT22(Finale)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Riposo-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introduzione",
L"VT43DA Jingle: Poeta 1",
L"VT44DA Jingle: Poeta 2",
L"VT45DA Jingle: Poeta 3",
L"VT46DA Jingle: Poeta 4",
L"VT47DA Jingle: Poeta 5",
L"VT48DA Jingle: Poeta 6",
L"VT49DA Jingle: Poeta 7"
};

TCHAR tibr4_es[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Plegaria Ancestral-meditation-",
L"VT05DA Ruinas-the prop-",
L"VT13DA Tienda-relation-",
L"VT32DA Laberinto-roar-",
L"VT04DA Distrito-division-",
L"VT14DA Casino-eighth wonder-",
L"VT19DA Juego-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Misterio de la Torre-mysterious-",
L"VT27DA Ruinas 2-debris-",
L"VT18DA Batalla Mortal-trigger-",
L"VT28DA Ruinas 3-deep haze-",
L"VT06DA Aguas-the abyss-",
L"VT07DA Jardín-bramble-",
L"VT33DA Invocación-hostile-",
L"VT08DA Fortaleza-solid steel-",
L"VT09DA Catedral-judgement-",
L"VT17DA Resurrección de la Torre-retribution-",
L"VT10DA Matriz-forbidden power-",
L"VT30DA Duelo-fatal riders-",
L"VT11DA Templo-heritage-",
L"VT12DA Laberinto del Olvido-another door-",
L"VT34DA Altar-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epílogo I-daybreak-",
L"VT40DA Epílogo II-twilight-",
L"VT41DA Epílogo IV-broken chain-",
L"VT21DA VT21(Final)",
L"VT22DA VT22(Final)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Descanso-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introducción",
L"VT43DA Jingle: Poeta 1",
L"VT44DA Jingle: Poeta 2",
L"VT45DA Jingle: Poeta 3",
L"VT46DA Jingle: Poeta 4",
L"VT47DA Jingle: Poeta 5",
L"VT48DA Jingle: Poeta 6",
L"VT49DA Jingle: Poeta 7"
};

TCHAR tibr4_ko[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA 고의 기도-meditation-",
L"VT05DA 유적-the prop-",
L"VT13DA 상점-relation-",
L"VT32DA 미궁-roar-",
L"VT04DA 구역-division-",
L"VT14DA 카지노-eighth wonder-",
L"VT19DA 게임-hammer slime-",
L"VT15DA 투기장-black field-",
L"VT16DA 탑의 수수께끼-mysterious-",
L"VT27DA 유적 2-debris-",
L"VT18DA 사투-trigger-",
L"VT28DA 유적 3-deep haze-",
L"VT06DA 수역-the abyss-",
L"VT07DA 정원-bramble-",
L"VT33DA 소환-hostile-",
L"VT08DA 성채-solid steel-",
L"VT09DA 성당-judgement-",
L"VT17DA 탑의 부활-retribution-",
L"VT10DA 태내-forbidden power-",
L"VT30DA 결투-fatal riders-",
L"VT11DA 신전-heritage-",
L"VT12DA 망각의 미궁-another door-",
L"VT34DA 제단-altar-",
L"VT20DA 기리아스-victim-",
L"VT39DA 에필로그 I-daybreak-",
L"VT40DA 에필로그 II-twilight-",
L"VT41DA 에필로그 IV-broken chain-",
L"VT21DA VT21(엔딩용)",
L"VT22DA VT22(엔딩용)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA 휴식-slumber-",
L"VT31DA GAME OVER",
L"VT38DA 인트로덕션",
L"VT43DA 징글: 시인 1",
L"VT44DA 징글: 시인 2",
L"VT45DA 징글: 시인 3",
L"VT46DA 징글: 시인 4",
L"VT47DA 징글: 시인 5",
L"VT48DA 징글: 시인 6",
L"VT49DA 징글: 시인 7"
};

TCHAR tibr4_zh[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA 古之祈祷-meditation-",
L"VT05DA 遗迹-the prop-",
L"VT13DA 商店-relation-",
L"VT32DA 迷宫-roar-",
L"VT04DA 街区-division-",
L"VT14DA 赌场-eighth wonder-",
L"VT19DA 游戏-hammer slime-",
L"VT15DA 斗技场-black field-",
L"VT16DA 塔之谜-mysterious-",
L"VT27DA 遗迹2-debris-",
L"VT18DA 死斗-trigger-",
L"VT28DA 遗迹3-deep haze-",
L"VT06DA 水域-the abyss-",
L"VT07DA 庭园-bramble-",
L"VT33DA 召唤-hostile-",
L"VT08DA 城塞-solid steel-",
L"VT09DA 圣堂-judgement-",
L"VT17DA 塔之复活-retribution-",
L"VT10DA 胎内-forbidden power-",
L"VT30DA 决斗-fatal riders-",
L"VT11DA 神殿-heritage-",
L"VT12DA 忘却之迷宫-another door-",
L"VT34DA 祭坛-altar-",
L"VT20DA 基利亚斯-victim-",
L"VT39DA 尾声I-daybreak-",
L"VT40DA 尾声II-twilight-",
L"VT41DA 尾声IV-broken chain-",
L"VT21DA VT21(结局用)",
L"VT22DA VT22(结局用)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA 休息-slumber-",
L"VT31DA GAME OVER",
L"VT38DA 序章",
L"VT43DA 叮当：诗人1",
L"VT44DA 叮当：诗人2",
L"VT45DA 叮当：诗人3",
L"VT46DA 叮当：诗人4",
L"VT47DA 叮当：诗人5",
L"VT48DA 叮当：诗人6",
L"VT49DA 叮当：诗人7"
};

TCHAR tibr4_ar[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA صلاة الأسلاف-meditation-",
L"VT05DA الآثار-the prop-",
L"VT13DA المتجر-relation-",
L"VT32DA المتاهة-roar-",
L"VT04DA الحي-division-",
L"VT14DA الكازينو-eighth wonder-",
L"VT19DA اللعبة-hammer slime-",
L"VT15DA الساحة-black field-",
L"VT16DA لغز البرج-mysterious-",
L"VT27DA الآثار 2-debris-",
L"VT18DA معركة الموت-trigger-",
L"VT28DA الآثار 3-deep haze-",
L"VT06DA المياه-the abyss-",
L"VT07DA الحديقة-bramble-",
L"VT33DA الاستدعاء-hostile-",
L"VT08DA القلعة-solid steel-",
L"VT09DA الكاتدرائية-judgement-",
L"VT17DA نهضة البرج-retribution-",
L"VT10DA الرحم-forbidden power-",
L"VT30DA المبارزة-fatal riders-",
L"VT11DA المعبد-heritage-",
L"VT12DA متاهة النسيان-another door-",
L"VT34DA المذبح-altar-",
L"VT20DA غيلاس-victim-",
L"VT39DA الخاتمة I-daybreak-",
L"VT40DA الخاتمة II-twilight-",
L"VT41DA الخاتمة IV-broken chain-",
L"VT21DA VT21(النهاية)",
L"VT22DA VT22(النهاية)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA الراحة-slumber-",
L"VT31DA GAME OVER",
L"VT38DA المقدمة",
L"VT43DA جينغل: الشاعر 1",
L"VT44DA جينغل: الشاعر 2",
L"VT45DA جينغل: الشاعر 3",
L"VT46DA جينغل: الشاعر 4",
L"VT47DA جينغل: الشاعر 5",
L"VT48DA جينغل: الشاعر 6",
L"VT49DA جينغل: الشاعر 7"
};

TCHAR tibr4_ru[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Древняя Молитва-meditation-",
L"VT05DA Руины-the prop-",
L"VT13DA Магазин-relation-",
L"VT32DA Лабиринт-roar-",
L"VT04DA Район-division-",
L"VT14DA Казино-eighth wonder-",
L"VT19DA Игра-hammer slime-",
L"VT15DA Арена-black field-",
L"VT16DA Тайна Башни-mysterious-",
L"VT27DA Руины 2-debris-",
L"VT18DA Смертельная Битва-trigger-",
L"VT28DA Руины 3-deep haze-",
L"VT06DA Воды-the abyss-",
L"VT07DA Сад-bramble-",
L"VT33DA Призыв-hostile-",
L"VT08DA Крепость-solid steel-",
L"VT09DA Собор-judgement-",
L"VT17DA Воскрешение Башни-retribution-",
L"VT10DA Утроба-forbidden power-",
L"VT30DA Дуэль-fatal riders-",
L"VT11DA Храм-heritage-",
L"VT12DA Лабиринт Забвения-another door-",
L"VT34DA Алтарь-altar-",
L"VT20DA Гилас-victim-",
L"VT39DA Эпилог I-daybreak-",
L"VT40DA Эпилог II-twilight-",
L"VT41DA Эпилог IV-broken chain-",
L"VT21DA VT21(Финал)",
L"VT22DA VT22(Финал)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Отдых-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Вступление",
L"VT43DA Jingle: Поэт 1",
L"VT44DA Jingle: Поэт 2",
L"VT45DA Jingle: Поэт 3",
L"VT46DA Jingle: Поэт 4",
L"VT47DA Jingle: Поэт 5",
L"VT48DA Jingle: Поэт 6",
L"VT49DA Jingle: Поэт 7"
};

TCHAR tibr4_de[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Altes Gebet-meditation-",
L"VT05DA Ruinen-the prop-",
L"VT13DA Laden-relation-",
L"VT32DA Labyrinth-roar-",
L"VT04DA Bezirk-division-",
L"VT14DA Kasino-eighth wonder-",
L"VT19DA Spiel-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Turm-Mysterium-mysterious-",
L"VT27DA Ruinen 2-debris-",
L"VT18DA Todeskampf-trigger-",
L"VT28DA Ruinen 3-deep haze-",
L"VT06DA Gewässer-the abyss-",
L"VT07DA Garten-bramble-",
L"VT33DA Beschwörung-hostile-",
L"VT08DA Festung-solid steel-",
L"VT09DA Kathedrale-judgement-",
L"VT17DA Turm-Wiederbelebung-retribution-",
L"VT10DA Schoß-forbidden power-",
L"VT30DA Duell-fatal riders-",
L"VT11DA Tempel-heritage-",
L"VT12DA Labyrinth des Vergessens-another door-",
L"VT34DA Altar-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epilog I-daybreak-",
L"VT40DA Epilog II-twilight-",
L"VT41DA Epilog IV-broken chain-",
L"VT21DA VT21(Ende)",
L"VT22DA VT22(Ende)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Ruhe-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Einführung",
L"VT43DA Jingle: Poet 1",
L"VT44DA Jingle: Poet 2",
L"VT45DA Jingle: Poet 3",
L"VT46DA Jingle: Poet 4",
L"VT47DA Jingle: Poet 5",
L"VT48DA Jingle: Poet 6",
L"VT49DA Jingle: Poet 7"
};

TCHAR tibr4_pt[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Oração Ancestral-meditation-",
L"VT05DA Ruínas-the prop-",
L"VT13DA Loja-relation-",
L"VT32DA Labirinto-roar-",
L"VT04DA Distrito-division-",
L"VT14DA Cassino-eighth wonder-",
L"VT19DA Jogo-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Mistério da Torre-mysterious-",
L"VT27DA Ruínas 2-debris-",
L"VT18DA Batalha Mortal-trigger-",
L"VT28DA Ruínas 3-deep haze-",
L"VT06DA Águas-the abyss-",
L"VT07DA Jardim-bramble-",
L"VT33DA Invocação-hostile-",
L"VT08DA Fortaleza-solid steel-",
L"VT09DA Catedral-judgement-",
L"VT17DA Ressurreição da Torre-retribution-",
L"VT10DA Ventre-forbidden power-",
L"VT30DA Duelo-fatal riders-",
L"VT11DA Templo-heritage-",
L"VT12DA Labirinto do Esquecimento-another door-",
L"VT34DA Altar-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epílogo I-daybreak-",
L"VT40DA Epílogo II-twilight-",
L"VT41DA Epílogo IV-broken chain-",
L"VT21DA VT21(Final)",
L"VT22DA VT22(Final)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Descanso-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introdução",
L"VT43DA Jingle: Poeta 1",
L"VT44DA Jingle: Poeta 2",
L"VT45DA Jingle: Poeta 3",
L"VT46DA Jingle: Poeta 4",
L"VT47DA Jingle: Poeta 5",
L"VT48DA Jingle: Poeta 6",
L"VT49DA Jingle: Poeta 7"
};

TCHAR tibr4_nl[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Oude Gebed-meditation-",
L"VT05DA Ruïnes-the prop-",
L"VT13DA Winkel-relation-",
L"VT32DA Labyrint-roar-",
L"VT04DA Wijk-division-",
L"VT14DA Casino-eighth wonder-",
L"VT19DA Spel-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Torenmysterie-mysterious-",
L"VT27DA Ruïnes 2-debris-",
L"VT18DA Doodstrijd-trigger-",
L"VT28DA Ruïnes 3-deep haze-",
L"VT06DA Wateren-the abyss-",
L"VT07DA Tuin-bramble-",
L"VT33DA Oproeping-hostile-",
L"VT08DA Vesting-solid steel-",
L"VT09DA Kathedraal-judgement-",
L"VT17DA Torenwedergeboorte-retribution-",
L"VT10DA Schoot-forbidden power-",
L"VT30DA Duel-fatal riders-",
L"VT11DA Tempel-heritage-",
L"VT12DA Labyrint der Vergetelheid-another door-",
L"VT34DA Altaar-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epiloog I-daybreak-",
L"VT40DA Epiloog II-twilight-",
L"VT41DA Epiloog IV-broken chain-",
L"VT21DA VT21(Einde)",
L"VT22DA VT22(Einde)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Rust-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Introductie",
L"VT43DA Jingle: Dichter 1",
L"VT44DA Jingle: Dichter 2",
L"VT45DA Jingle: Dichter 3",
L"VT46DA Jingle: Dichter 4",
L"VT47DA Jingle: Dichter 5",
L"VT48DA Jingle: Dichter 6",
L"VT49DA Jingle: Dichter 7"
};

TCHAR tibr4_pl[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Starożytna Modlitwa-meditation-",
L"VT05DA Ruiny-the prop-",
L"VT13DA Sklep-relation-",
L"VT32DA Labirynt-roar-",
L"VT04DA Dzielnica-division-",
L"VT14DA Kasyno-eighth wonder-",
L"VT19DA Gra-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Tajemnica Wieży-mysterious-",
L"VT27DA Ruiny 2-debris-",
L"VT18DA Śmiertelna Walka-trigger-",
L"VT28DA Ruiny 3-deep haze-",
L"VT06DA Wody-the abyss-",
L"VT07DA Ogród-bramble-",
L"VT33DA Przywołanie-hostile-",
L"VT08DA Twierdza-solid steel-",
L"VT09DA Katedra-judgement-",
L"VT17DA Odrodzenie Wieży-retribution-",
L"VT10DA Łono-forbidden power-",
L"VT30DA Pojedynek-fatal riders-",
L"VT11DA Świątynia-heritage-",
L"VT12DA Labirynt Zapomnienia-another door-",
L"VT34DA Ołtarz-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Epilog I-daybreak-",
L"VT40DA Epilog II-twilight-",
L"VT41DA Epilog IV-broken chain-",
L"VT21DA VT21(Zakończenie)",
L"VT22DA VT22(Zakończenie)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Odpoczynek-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Wprowadzenie",
L"VT43DA Jingle: Poeta 1",
L"VT44DA Jingle: Poeta 2",
L"VT45DA Jingle: Poeta 3",
L"VT46DA Jingle: Poeta 4",
L"VT47DA Jingle: Poeta 5",
L"VT48DA Jingle: Poeta 6",
L"VT49DA Jingle: Poeta 7"
};

TCHAR tibr4_tr[][128]={
L"VT01DA Brandish4 -OP-",
L"VT03DA Kadim Dua-meditation-",
L"VT05DA Harabeler-the prop-",
L"VT13DA Dükkan-relation-",
L"VT32DA Labirent-roar-",
L"VT04DA Bölge-division-",
L"VT14DA Kumarhane-eighth wonder-",
L"VT19DA Oyun-hammer slime-",
L"VT15DA Arena-black field-",
L"VT16DA Kule Gizemi-mysterious-",
L"VT27DA Harabeler 2-debris-",
L"VT18DA Ölüm Savaşı-trigger-",
L"VT28DA Harabeler 3-deep haze-",
L"VT06DA Sular-the abyss-",
L"VT07DA Bahçe-bramble-",
L"VT33DA Çağrı-hostile-",
L"VT08DA Kale-solid steel-",
L"VT09DA Katedral-judgement-",
L"VT17DA Kulenin Dirilişi-retribution-",
L"VT10DA Rahim-forbidden power-",
L"VT30DA Düello-fatal riders-",
L"VT11DA Tapınak-heritage-",
L"VT12DA Unutulma Labirenti-another door-",
L"VT34DA Sunak-altar-",
L"VT20DA Gilas-victim-",
L"VT39DA Sonsöz I-daybreak-",
L"VT40DA Sonsöz II-twilight-",
L"VT41DA Sonsöz IV-broken chain-",
L"VT21DA VT21(Son)",
L"VT22DA VT22(Son)",
L"VT35DA ENDING",
L"VT02DA Staff",
L"VT36DA Dinlenme-slumber-",
L"VT31DA GAME OVER",
L"VT38DA Giriş",
L"VT43DA Jingle: Şair 1",
L"VT44DA Jingle: Şair 2",
L"VT45DA Jingle: Şair 3",
L"VT46DA Jingle: Şair 4",
L"VT47DA Jingle: Şair 5",
L"VT48DA Jingle: Şair 6",
L"VT49DA Jingle: Şair 7"
};

static inline CString Br4Track(int i) {
	switch (savedata.lang) {
		case 0: return CString(tibr4[i]);
		case 1: return CString(tibr4_en[i]);
		case 2: return CString(tibr4_fr[i]);
		case 3: return CString(tibr4_it[i]);
		case 4: return CString(tibr4_es[i]);
		case 5: return CString(tibr4_ko[i]);
		case 6: return CString(tibr4_zh[i]);
		case 7: return CString(tibr4_ar[i]);
		case 8: return CString(tibr4_ru[i]);
		case 9: return CString(tibr4_de[i]);
		case 10: return CString(tibr4_pt[i]);
		case 11: return CString(tibr4_nl[i]);
		case 12: return CString(tibr4_pl[i]);
		case 13: return CString(tibr4_tr[i]);
		default: return CString(tibr4_en[i]);
	}
}

CString CBr4::Gett(int a){
	CString s,ss;
	s = Br4Track(a);
	ss=s.Left(6);ss.TrimRight();
	fnn=s.Mid(7);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CBr4::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s;	s=Br4Track(idx);
	ret=s.Left(6); ret.TrimRight();
	ret2=m_list.GetCurSel();
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
	}else{
		fnn=s.Mid(7);
	}
	EndDialog(1567);
}

BOOL CBr4::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"ブランディッシュ４ 眠れる神の塔", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tour du Dieu Dormant", L"Brandish4 Torre del Dio Dormiente", L"Brandish4 Torre del Dios Durmiente", L"브랜디시4 잠든 신의 탑", L"撼天神塔4", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Башня Спящего Бога", L"Brandish4 Turm des Schlafenden Gottes", L"Brandish4 Torre do Deus Adormecido", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Wieża Śpiącego Boga", L"Brandish4 Tower of the Sleeping God"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(42);i++)
	{
		CString s = Br4Track(i);
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret2!=0) 
//		if(ret>65) m_list.SetCurSel(ret);
//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret2);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
