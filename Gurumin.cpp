// Gurumin.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Gurumin.h"


// CGurumin ダイアログ

IMPLEMENT_DYNAMIC(CGurumin, CCustomDialog)

CGurumin::CGurumin(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CGurumin::IDD, pParent)
{

}

CGurumin::~CGurumin()
{
}

void CGurumin::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CGurumin, CCustomDialog)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CGurumin);


// CGurumin メッセージ ハンドラ
extern CString fnn;

TCHAR tigu[][128]={
L"bgm01 ぐるぐるTonight",
L"bgm08 オバケな日々",
L"bgm03 あなたはだぁれ？",
L"bgm05 マイペースなオバケたち",
L"bgm06 ディース商店街のテーマ",
L"bgm04 宴の館",
L"bgm09 たたた大変でしゅ",
L"bgm11 EURO BEAT POCO",
L"bgm15 伝説のドリル",
L"bgm07 オバケワールド",
L"bgm36 ステージクリア",
L"bgm12 闇の霧",
L"bgm14 なんだか大変！？",
L"bgm10 Animal Minimal",
L"bgm17 グルグル魔人でポン",
L"bgm18 今来た道を戻れ",
L"bgm43 無敵の鎧は絶対破れマセ～ン！",
L"bgm21 マイナスイオンの静寂",
L"bgm27 秘密の密林サバイバル",
L"bgm22 dance in the forest",
L"bgm23 虹色オバケは泳げない",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL！",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 おもひで",
L"bgm29 不可思議オバケ卵",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 復習の賛歌",
L"bgm46 呪われし厄の牙",
L"bgm32 悲しき蒼穹を翔ける",
L"bgm38 やるしかないわね！",
L"bgm35 戦い終わって日が暮れて",
L"bgm40 明日はきっとみんな友達",
L"bgm42 いつもの場所へ",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 ディース商店街の歌",
L"★オープニング"
};

TCHAR tigu_en[][128]={
L"bgm01 Guruguru Tonight",
L"bgm08 Ghostly Days",
L"bgm03 Who Are You?",
L"bgm05 Laid-back Ghosts",
L"bgm06 Deuce Shopping District Theme",
L"bgm04 Banquet Hall",
L"bgm09 Uh Oh Trouble",
L"bgm11 EURO BEAT POCO",
L"bgm15 Legendary Drill",
L"bgm07 Ghost World",
L"bgm36 Stage Clear",
L"bgm12 Mist of Darkness",
L"bgm14 Something's Wrong!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru Demon Pan",
L"bgm18 Turn Back the Way You Came",
L"bgm43 Invincible Armor Never Breaks!",
L"bgm21 Negative Ion Silence",
L"bgm27 Secret Jungle Survival",
L"bgm22 dance in the forest",
L"bgm23 Rainbow Ghost Can't Swim",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Memories",
L"bgm29 Incredible Ghost Egg",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hymn of Revenge",
L"bgm46 Cursed Calamity Fang",
L"bgm32 Soaring the Sad Azure",
L"bgm38 No Choice But To Do It!",
L"bgm35 Battle Over, Sunset",
L"bgm40 Tomorrow We'll All Be Friends",
L"bgm42 To the Usual Place",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Deuce Shopping District Song",
L"★Opening"
};

TCHAR tigu_fr[][128]={
L"bgm01 Guruguru ce soir",
L"bgm08 Jours de fantômes",
L"bgm03 Qui es-tu?",
L"bgm05 Fantômes décontractés",
L"bgm06 Thème du quartier commercial Deuce",
L"bgm04 Salle de banquet",
L"bgm09 Oh là là des ennuis",
L"bgm11 EURO BEAT POCO",
L"bgm15 Perceuse légendaire",
L"bgm07 Monde des fantômes",
L"bgm36 Niveau terminé",
L"bgm12 Brume des ténèbres",
L"bgm14 Quelque chose ne va pas!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru démon Pan",
L"bgm18 Retourne par où tu es venu",
L"bgm43 L'armure invincible ne se brise jamais!",
L"bgm21 Silence des ions négatifs",
L"bgm27 Survival jungle secrète",
L"bgm22 dance in the forest",
L"bgm23 Le fantôme arc-en-ciel ne sait pas nager",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Souvenirs",
L"bgm29 Incroyable œuf fantôme",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hymne de la vengeance",
L"bgm46 Croc maudit du malheur",
L"bgm32 Planant dans l'azur triste",
L"bgm38 Pas le choix, il faut le faire!",
L"bgm35 Bataille terminée, coucher de soleil",
L"bgm40 Demain nous serons tous amis",
L"bgm42 Vers le lieu habituel",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Chanson du quartier commercial Deuce",
L"★Ouverture"
};

TCHAR tigu_de[][128]={
L"bgm01 Guruguru Heute Nacht",
L"bgm08 Geisterhafte Tage",
L"bgm03 Wer bist du?",
L"bgm05 Entspannte Geister",
L"bgm06 Deuce Einkaufsviertel Theme",
L"bgm04 Bankettsaal",
L"bgm09 Oh nein, Ärger",
L"bgm11 EURO BEAT POCO",
L"bgm15 Legendärer Bohrer",
L"bgm07 Geisterwelt",
L"bgm36 Level abgeschlossen",
L"bgm12 Nebel der Finsternis",
L"bgm14 Etwas stimmt nicht!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru Dämon Pan",
L"bgm18 Kehr den Weg zurück",
L"bgm43 Unbesiegbare Rüstung bricht nie!",
L"bgm21 Negativer Ionen-Stillstand",
L"bgm27 Geheimes Dschungel-Überleben",
L"bgm22 dance in the forest",
L"bgm23 Regenbogen-Geist kann nicht schwimmen",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Erinnerungen",
L"bgm29 Unglaubliches Geister-Ei",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hymne der Rache",
L"bgm46 Verfluchter Unglückszahn",
L"bgm32 Durch den traurigen Azur gleitend",
L"bgm38 Keine Wahl, mach es!",
L"bgm35 Kampf vorbei, Sonnenuntergang",
L"bgm40 Morgen sind wir alle Freunde",
L"bgm42 Zum gewohnten Ort",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Deuce Einkaufsviertel Lied",
L"★Eröffnung"
};

TCHAR tigu_es[][128]={
L"bgm01 Guruguru esta noche",
L"bgm08 Días fantasmal",
L"bgm03 ¿Quién eres?",
L"bgm05 Fantasmas relajados",
L"bgm06 Tema del barrio comercial Deuce",
L"bgm04 Sala de banquetes",
L"bgm09 ¡Ay, problemas!",
L"bgm11 EURO BEAT POCO",
L"bgm15 Taladro legendario",
L"bgm07 Mundo de fantasmas",
L"bgm36 Nivel completado",
L"bgm12 Niebla de oscuridad",
L"bgm14 ¡¿Algo anda mal!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru demonio Pan",
L"bgm18 Vuelve por donde viniste",
L"bgm43 ¡La armadura invencible nunca se rompe!",
L"bgm21 Silencio de iones negativos",
L"bgm27 Supervivencia secreta en la jungla",
L"bgm22 dance in the forest",
L"bgm23 El fantasma arcoíris no sabe nadar",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Recuerdos",
L"bgm29 Huevo fantasma increíble",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Himno de venganza",
L"bgm46 Colmillo maldito de calamidad",
L"bgm32 Volando en el azul triste",
L"bgm38 ¡No hay opción, hazlo!",
L"bgm35 Batalla terminada, atardecer",
L"bgm40 Mañana todos seremos amigos",
L"bgm42 Al lugar de siempre",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Canción del barrio comercial Deuce",
L"★Apertura"
};

TCHAR tigu_it[][128]={
L"bgm01 Guruguru stasera",
L"bgm08 Giorni spettrali",
L"bgm03 Chi sei?",
L"bgm05 Fantasmi rilassati",
L"bgm06 Tema quartiere commerciale Deuce",
L"bgm04 Sala dei banchetti",
L"bgm09 Oh no, guai",
L"bgm11 EURO BEAT POCO",
L"bgm15 Trapano leggendario",
L"bgm07 Mondo dei fantasmi",
L"bgm36 Livello completato",
L"bgm12 Nebbia delle tenebre",
L"bgm14 Qualcosa non va!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru demone Pan",
L"bgm18 Torna da dove sei venuto",
L"bgm43 L'armatura invincibile non si rompe mai!",
L"bgm21 Silenzio degli ioni negativi",
L"bgm27 Sopravvivenza nella giungla segreta",
L"bgm22 dance in the forest",
L"bgm23 Il fantasma arcobaleno non sa nuotare",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Ricordi",
L"bgm29 Uovo fantasma incredibile",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Inno alla vendetta",
L"bgm46 Zanna maledetta della calamità",
L"bgm32 Volando nell'azzurro triste",
L"bgm38 Nessuna scelta, fallo!",
L"bgm35 Battaglia finita, tramonto",
L"bgm40 Domani saremo tutti amici",
L"bgm42 Al posto consueto",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Canzone quartiere commerciale Deuce",
L"★Apertura"
};

TCHAR tigu_ko[][128]={
L"bgm01 구루구루 투나이트",
L"bgm08 유령의 나날",
L"bgm03 당신은 누구?",
L"bgm05 여유로운 유령들",
L"bgm06 듀스 상점가 테마",
L"bgm04 연회장",
L"bgm09 아이고 큰일났어",
L"bgm11 EURO BEAT POCO",
L"bgm15 전설의 드릴",
L"bgm07 유령의 세계",
L"bgm36 스테이지 클리어",
L"bgm12 어둠의 안개",
L"bgm14 뭔가 이상해!?",
L"bgm10 Animal Minimal",
L"bgm17 구루구루 마인 판",
L"bgm18 온 길을 되돌아가",
L"bgm43 무적의 갑옷은 절대 부서지지 않아!",
L"bgm21 마이너스 이온의 정적",
L"bgm27 비밀의 밀림 서바이벌",
L"bgm22 dance in the forest",
L"bgm23 무지개 유령은 수영을 못해",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 추억",
L"bgm29 불가사의한 유령 알",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 복수의 찬가",
L"bgm46 저주받은 재앙의 이빨",
L"bgm32 슬픈 푸른 하늘을 달리다",
L"bgm38 할 수밖에 없어!",
L"bgm35 싸움 끝나고 해 저물고",
L"bgm40 내일은 모두 친구",
L"bgm42 늘 있던 장소로",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 듀스 상점가의 노래",
L"★오프닝"
};

TCHAR tigu_zh[][128]={
L"bgm01 咕噜咕噜今夜",
L"bgm08 幽灵般的日子",
L"bgm03 你是谁？",
L"bgm05 悠闲的幽灵们",
L"bgm06 迪斯商业街主题",
L"bgm04 宴会厅",
L"bgm09 糟了糟了",
L"bgm11 EURO BEAT POCO",
L"bgm15 传说之钻",
L"bgm07 幽灵世界",
L"bgm36 关卡通关",
L"bgm12 暗雾",
L"bgm14 不对劲！？",
L"bgm10 Animal Minimal",
L"bgm17 咕噜咕噜魔人 Pan",
L"bgm18 原路返回",
L"bgm43 无敌铠甲永不破！",
L"bgm21 负离子寂静",
L"bgm27 秘密密林生存",
L"bgm22 dance in the forest",
L"bgm23 彩虹幽灵不会游泳",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 回忆",
L"bgm29 不可思议幽灵蛋",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 复仇赞歌",
L"bgm46 诅咒厄运之牙",
L"bgm32 翱翔于悲伤苍穹",
L"bgm38 只能做了！",
L"bgm35 战斗结束日暮时",
L"bgm40 明天大家是朋友",
L"bgm42 前往老地方",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 迪斯商业街之歌",
L"★片头"
};

TCHAR tigu_ar[][128]={
L"bgm01 غوروغورو الليلة",
L"bgm08 أيام أشباح",
L"bgm03 من أنت؟",
L"bgm05 أشباح مسترخية",
L"bgm06 ثيم حي ديوس التجاري",
L"bgm04 قاعة الولائم",
L"bgm09 أوه لا مشكلة",
L"bgm11 EURO BEAT POCO",
L"bgm15 المثقاب الأسطوري",
L"bgm07 عالم الأشباح",
L"bgm36 انتهى المستوى",
L"bgm12 ضباب الظلام",
L"bgm14 شيء خاطئ!؟",
L"bgm10 Animal Minimal",
L"bgm17 غوروغورو شيطان بان",
L"bgm18 ارجع من حيث أتيت",
L"bgm43 الدروع التي لا تقهر لا تنكسر أبداً!",
L"bgm21 صمت الأيونات السالبة",
L"bgm27 بقاء الغابة السرية",
L"bgm22 dance in the forest",
L"bgm23 شبح قوس القزح لا يعرف السباحة",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 ذكريات",
L"bgm29 بيضة شبح لا تصدق",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 ترنيمة الانتقام",
L"bgm46 ناب المصائب الملعون",
L"bgm32 التحليق في الأزرق الحزين",
L"bgm38 لا خيار، افعلها!",
L"bgm35 المعركة انتهت، غروب",
L"bgm40 غداً سنكون كلنا أصدقاء",
L"bgm42 إلى المكان المعتاد",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 أغنية حي ديوس التجاري",
L"★افتتاحية"
};

TCHAR tigu_ru[][128]={
L"bgm01 Гуругуру Сегодня",
L"bgm08 Призрачные дни",
L"bgm03 Кто ты?",
L"bgm05 Расслабленные призраки",
L"bgm06 Тема торгового района Диус",
L"bgm04 Банкетный зал",
L"bgm09 О нет, неприятности",
L"bgm11 EURO BEAT POCO",
L"bgm15 Легендарная дрель",
L"bgm07 Мир призраков",
L"bgm36 Уровень пройден",
L"bgm12 Тьма тумана",
L"bgm14 Что-то не так!?",
L"bgm10 Animal Minimal",
L"bgm17 Гуругуру демон Пан",
L"bgm18 Вернись тем же путём",
L"bgm43 Непобедимая броня никогда не сломается!",
L"bgm21 Тишина отрицательных ионов",
L"bgm27 Тайное джунглевое выживание",
L"bgm22 dance in the forest",
L"bgm23 Радужный призрак не умеет плавать",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Воспоминания",
L"bgm29 Невероятное призрачное яйцо",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Гимн мести",
L"bgm46 Проклятый клык бедствия",
L"bgm32 Паря в грустной лазури",
L"bgm38 Выбора нет, делай это!",
L"bgm35 Бой окончен, закат",
L"bgm40 Завтра мы все будем друзьями",
L"bgm42 В обычное место",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Песня торгового района Диус",
L"★Начало"
};

TCHAR tigu_pt[][128]={
L"bgm01 Guruguru Hoje à noite",
L"bgm08 Dias fantasmas",
L"bgm03 Quem é você?",
L"bgm05 Fantasmas relaxados",
L"bgm06 Tema do bairro comercial Deuce",
L"bgm04 Sala de banquetes",
L"bgm09 Oh não, problemas",
L"bgm11 EURO BEAT POCO",
L"bgm15 Broca lendária",
L"bgm07 Mundo dos fantasmas",
L"bgm36 Fase concluída",
L"bgm12 Neblina das trevas",
L"bgm14 Algo está errado!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru demônio Pan",
L"bgm18 Volte por onde veio",
L"bgm43 Armadura invencível nunca quebra!",
L"bgm21 Silêncio de íons negativos",
L"bgm27 Sobrevivência na selva secreta",
L"bgm22 dance in the forest",
L"bgm23 Fantasma arco-íris não sabe nadar",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Memórias",
L"bgm29 Ovo fantasma incrível",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hino da vingança",
L"bgm46 Presa maldita da calamidade",
L"bgm32 Voando no azul triste",
L"bgm38 Sem escolha, faça!",
L"bgm35 Batalha acabada, pôr do sol",
L"bgm40 Amanhã seremos todos amigos",
L"bgm42 Ao lugar de sempre",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Canção do bairro comercial Deuce",
L"★Abertura"
};

TCHAR tigu_nl[][128]={
L"bgm01 Guruguru Vanavond",
L"bgm08 Spookachtige dagen",
L"bgm03 Wie ben jij?",
L"bgm05 Ontspannen geesten",
L"bgm06 Deuce winkelwijk thema",
L"bgm04 Bankethal",
L"bgm09 Oh nee, problemen",
L"bgm11 EURO BEAT POCO",
L"bgm15 Legendarische boor",
L"bgm07 Geestenwereld",
L"bgm36 Level voltooid",
L"bgm12 Mist der duisternis",
L"bgm14 Er is iets mis!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru demon Pan",
L"bgm18 Ga terug waar je vandaan kwam",
L"bgm43 Onoverwinnelijk harnas breekt nooit!",
L"bgm21 Stilte van negatieve ionen",
L"bgm27 Geheim jungle-overleving",
L"bgm22 dance in the forest",
L"bgm23 Regenbooggeest kan niet zwemmen",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Herinneringen",
L"bgm29 Ongelooflijk spookei",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hymne van wraak",
L"bgm46 Vervloekte ramp tand",
L"bgm32 Zwevend in het trieste azuur",
L"bgm38 Geen keuze, doe het!",
L"bgm35 Gevecht voorbij, zonsondergang",
L"bgm40 Morgen zijn we allemaal vrienden",
L"bgm42 Naar de vaste plek",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Deuce winkelwijk lied",
L"★Opening"
};

TCHAR tigu_pl[][128]={
L"bgm01 Guruguru Dziś wieczorem",
L"bgm08 Dni duchów",
L"bgm03 Kim jesteś?",
L"bgm05 Zrelaksowani duchy",
L"bgm06 Motyw dzielnicy handlowej Deuce",
L"bgm04 Sala bankietowa",
L"bgm09 O nie, kłopoty",
L"bgm11 EURO BEAT POCO",
L"bgm15 Legendarny wiertło",
L"bgm07 Świat duchów",
L"bgm36 Poziom ukończony",
L"bgm12 Mgła ciemności",
L"bgm14 Coś jest nie tak!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru demon Pan",
L"bgm18 Wróć tą samą drogą",
L"bgm43 Niezwyciężona zbroja nigdy nie pęka!",
L"bgm21 Cisza jonów ujemnych",
L"bgm27 Sekretne przetrwanie w dżungli",
L"bgm22 dance in the forest",
L"bgm23 Duch tęczy nie umie pływać",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Wspomnienia",
L"bgm29 Niesamowite jajo ducha",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 Hymn zemsty",
L"bgm46 Przeklęty kieł klęski",
L"bgm32 Szybując w smutnym błękicie",
L"bgm38 Nie ma wyboru, rób to!",
L"bgm35 Bitwa skończona, zachód słońca",
L"bgm40 Jutro będziemy wszyscy przyjaciółmi",
L"bgm42 Do zwykłego miejsca",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Piosenka dzielnicy Deuce",
L"★Odsłony"
};

TCHAR tigu_tr[][128]={
L"bgm01 Guruguru Bu gece",
L"bgm08 Hayalet günler",
L"bgm03 Sen kimsin?",
L"bgm05 Rahat hayaletler",
L"bgm06 Deuce alışveriş bölgesi teması",
L"bgm04 Ziyafet salonu",
L"bgm09 Ah hayır, sıkıntı",
L"bgm11 EURO BEAT POCO",
L"bgm15 Efsanevi matkap",
L"bgm07 Hayalet dünyası",
L"bgm36 Seviye tamamlandı",
L"bgm12 Karanlık sisi",
L"bgm14 Bir şeyler yanlış!?",
L"bgm10 Animal Minimal",
L"bgm17 Guruguru şeytan Pan",
L"bgm18 Geldiğin yoldan geri dön",
L"bgm43 Yenilmez zırh asla kırılmaz!",
L"bgm21 Negatif iyon sessizliği",
L"bgm27 Gizli orman hayatta kalması",
L"bgm22 dance in the forest",
L"bgm23 Gökkuşağı hayaleti yüzemez",
L"bgm24 sight of silence",
L"bgm25 SAMURAI DRILL!",
L"bgm20 JunJun - Jungle gym",
L"bgm26 under the sky",
L"bgm28 bomber girl",
L"bgm39 Anılar",
L"bgm29 İnanılmaz hayalet yumurta",
L"bgm30 Blest of Wind",
L"bgm45 Rocky Nebula",
L"bgm31 İntikam ilahisi",
L"bgm46 Lanetli felaket dişi",
L"bgm32 Hüzünlü mavide süzülerek",
L"bgm38 Seçenek yok, yap!",
L"bgm35 Savaş bitti, gün batımı",
L"bgm40 Yarın hepimiz arkadaş olacağız",
L"bgm42 Her zamanki yere",
L"bgm02 Friends",
L"bgm33 TO MAKE THE END OF DIGING",
L"bgm44 Deuce alışveriş bölgesi şarkısı",
L"★Açılış"
};

static inline CString GuruminTrack(int i) {
	switch (savedata.lang) {
		case 0: return CString(tigu[i]);
		case 1: return CString(tigu_en[i]);
		case 2: return CString(tigu_fr[i]);
		case 3: return CString(tigu_it[i]);
		case 4: return CString(tigu_es[i]);
		case 5: return CString(tigu_ko[i]);
		case 6: return CString(tigu_zh[i]);
		case 7: return CString(tigu_ar[i]);
		case 8: return CString(tigu_ru[i]);
		case 9: return CString(tigu_de[i]);
		case 10: return CString(tigu_pt[i]);
		case 11: return CString(tigu_nl[i]);
		case 12: return CString(tigu_pl[i]);
		case 13: return CString(tigu_tr[i]);
		default: return CString(tigu_en[i]);
	}
}

CString CGurumin::Gett(int a){
	CString s,ss,sss;int aa;
	s = GuruminTrack(a);
	ss=s.Left(5);ss.TrimRight();
	sss=ss.Right(2); aa=_tstoi(sss);
	switch(aa){
		case 1: loop1=0; loop2=0; break;
		case 2: loop1=0; loop2=0; break;
		case 3: loop1=211049; loop2=2479133; break;
		case 4: loop1=875909; loop2=5506409; break;
		case 5: loop1=956443; loop2=4536327; break;
		case 6: loop1=559133; loop2=3320188; break;
		case 7: loop1=717837; loop2=6288363; break;
		case 8: loop1=350186; loop2=4075770; break;
		case 9: loop1=111612; loop2=3083831; break;
		case 10: loop1=454418; loop2=5283368; break;
		case 11: loop1=0; loop2=0; break;
		case 12: loop1=22562; loop2=2656272; break;
		case 14: loop1=138782; loop2=4122910; break;
		case 15: loop1=0; loop2=0; break;
		case 17: loop1=180224; loop2=9620008; break;
		case 18: loop1=532240; loop2=9841064; break;
		case 20: loop1=624308; loop2=6098791; break;
		case 21: loop1=1119619; loop2=6842973; break;
		case 22: loop1=664867; loop2=6712867; break;
		case 23: loop1=1291826; loop2=10041266; break;
		case 24: loop1=1264850; loop2=9167570; break;
		case 25: loop1=460165; loop2=10004014; break;
		case 26: loop1=1346511; loop2=6604369; break;
		case 27: loop1=400130; loop2=6750514; break;
		case 28: loop1=610146; loop2=8254146; break;
		case 29: loop1=1234844; loop2=10537980; break;
		case 30: loop1=203348; loop2=5693798; break;
		case 31: loop1=347847; loop2=6861079; break;
		case 32: loop1=1176658; loop2=11458258; break;
		case 33: loop1=962322; loop2=5580795; break;
		case 35: loop1=22050; loop2=5243486; break;
		case 36: loop1=1004300; loop2=2062700; break;
		case 38: loop1=333547; loop2=4899691; break;
		case 39: loop1=1091098; loop2=5324698; break;
		case 40: loop1=168548; loop2=5191462; break;
		case 42: loop1=239571; loop2=5567931; break;
		case 43: loop1=150544; loop2=4501022; break;
		case 44: loop1=712610; loop2=3473489; break;
		case 45: loop1=419768; loop2=4190318; break;
		case 46: loop1=496280; loop2=3883160; break;
		case 0: loop1=0; loop2=0; break;
	}
	fnn=s.Mid(6);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CGurumin::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s,ss,sss;int aa;	s = GuruminTrack(idx);
	ret=s.Left(5); ret.TrimRight(); ss=ret;
	ret2=m_list.GetCurSel();
	sss=ss.Right(2); aa=_tstoi(sss);
	switch(aa){
		case 1: loop1=0; loop2=0; break;
		case 2: loop1=0; loop2=0; break;
		case 3: loop1=211049; loop2=2479133; break;
		case 4: loop1=875909; loop2=5506409; break;
		case 5: loop1=956443; loop2=4536327; break;
		case 6: loop1=559133; loop2=3320188; break;
		case 7: loop1=717837; loop2=6288363; break;
		case 8: loop1=350186; loop2=4075770; break;
		case 9: loop1=111612; loop2=3083831; break;
		case 10: loop1=454418; loop2=5283368; break;
		case 11: loop1=0; loop2=0; break;
		case 12: loop1=22562; loop2=2656272; break;
		case 14: loop1=138782; loop2=4122910; break;
		case 15: loop1=0; loop2=0; break;
		case 17: loop1=180224; loop2=9620008; break;
		case 18: loop1=532240; loop2=9841064; break;
		case 20: loop1=624308; loop2=6098791; break;
		case 21: loop1=1119619; loop2=6842973; break;
		case 22: loop1=664867; loop2=6712867; break;
		case 23: loop1=1291826; loop2=10041266; break;
		case 24: loop1=1264850; loop2=9167570; break;
		case 25: loop1=460165; loop2=10004014; break;
		case 26: loop1=1346511; loop2=6604369; break;
		case 27: loop1=400130; loop2=6750514; break;
		case 28: loop1=610146; loop2=8254146; break;
		case 29: loop1=1234844; loop2=10537980; break;
		case 30: loop1=203348; loop2=5693798; break;
		case 31: loop1=347847; loop2=6861079; break;
		case 32: loop1=1176658; loop2=11458258; break;
		case 33: loop1=962322; loop2=5580795; break;
		case 35: loop1=22050; loop2=5243486; break;
		case 36: loop1=1004300; loop2=2062700; break;
		case 38: loop1=333547; loop2=4899691; break;
		case 39: loop1=1091098; loop2=5324698; break;
		case 40: loop1=168548; loop2=5191462; break;
		case 42: loop1=239571; loop2=5567931; break;
		case 43: loop1=150544; loop2=4501022; break;
		case 44: loop1=712610; loop2=3473489; break;
		case 45: loop1=419768; loop2=4190318; break;
		case 46: loop1=496280; loop2=3883160; break;
		case 0: loop1=0; loop2=0; break;
	}
#if UNICODE
	if(s.Left(1)==L"★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)==L"★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(6);
	}
	EndDialog(1567);
}

BOOL CGurumin::OnInitDialog() 
{
	CCustomDialog::OnInitDialog();
	SetWindowText(LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(41);i++)
	{
		CString s = GuruminTrack(i);
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
