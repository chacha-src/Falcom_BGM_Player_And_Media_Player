// San1.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "San1.h"


// CSan1 ダイアログ

IMPLEMENT_DYNAMIC(CSan1, CCustomBlurDialogBase)

CSan1::CSan1(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CSan1::IDD, pParent)
{

}

CSan1::~CSan1()
{
}

void CSan1::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CSan1, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CSan1);


// CSan1 メッセージ ハンドラ
extern CString fnn;

TCHAR tisan1[][128] = {
L"42 永遠的旋律",
L"01 古松居",
L"27 緊迫",
L"12 風動",
L"48 破軍",
L"49 戰闘49",
L"39 悲訣",
L"38 暗思量",
L"47 幽谷潤",
L"05 陌上仙郷",
L"51 戰闘51",
L"08 蜘蛛洞",
L"03 臨江雨",
L"02 市井榮華",
L"35 笑笑",
L"36 采飛揚",
L"29 征途",
L"50 戰闘50",
L"07 功名路",
L"22 鬼界戰闘",
L"45 激戰",
L"13 背水一戰",
L"53 祭旗",
L"14 飲幽恨",
L"15 秋瑟涙",
L"46 天涯魂夢",
L"11 雲中境",
L"19 戰場",
L"10 江湖行",
L"04 朱樓春宴",
L"31 衝陣",
L"30 戰火",
L"21 歩歩為營",
L"41 衷情",
L"37 回首前塵",
L"54 玄機",
L"09 幻霧星霜",
L"20 水寒淵",
L"06 烽火紅顔",
L"16 月蒼茫",
L"32 登天",
L"23 華夜曲",
L"34 決戰千里",
L"★盡頭",
L"40 遙相憶",
L"★オープニング",
L"★動画",
L"★ファルコムロゴ"
};

TCHAR tisan1_en[][128] = {
L"42 Eternal Melody",
L"01 Ancient Pine Residence",
L"27 Tension",
L"12 Wind Blown",
L"48 Broken Army",
L"49 Battle 49",
L"39 Sorrowful Parting",
L"38 Dark Thoughts",
L"47 Valley Mist",
L"05 Rural Paradise",
L"51 Battle 51",
L"08 Spider Cave",
L"03 Riverside Rain",
L"02 City Prosperity",
L"35 Smile",
L"36 High Spirits",
L"29 Journey",
L"50 Battle 50",
L"07 Path of Glory",
L"22 Ghost Realm Battle",
L"45 Fierce Battle",
L"13 Last Stand",
L"53 Sacrificial Banner",
L"14 Drink of Resentment",
L"15 Autumn Tears",
L"46 Dreams of the Ends of Earth",
L"11 Realm in the Clouds",
L"19 Battlefield",
L"10 Roaming the Rivers",
L"04 Spring Feast at Red Pavilion",
L"31 Charge the Formation",
L"30 Flames of War",
L"21 Every Step a Fortress",
L"41 True Feelings",
L"37 Looking Back",
L"54 Mystery",
L"09 Mist and Frost",
L"20 Cold Abyss",
L"06 Beauty in the Flames",
L"16 Pale Moon",
L"32 Ascend to Heaven",
L"23 Night Song",
L"34 Decisive Battle",
L"★End",
L"40 Distant Memories",
L"★Opening",
L"★Video",
L"★FALCOM Logo"
};

TCHAR tisan1_fr[][128] = {
L"42 Mélodie Éternelle",
L"01 Résidence du Pin Ancien",
L"27 Tension",
L"12 Soufflé par le Vent",
L"48 Armée Brisée",
L"49 Bataille 49",
L"39 Adieu Déchirant",
L"38 Pensées Sombres",
L"47 Brume de la Vallée",
L"05 Paradis Rural",
L"51 Bataille 51",
L"08 Grotte de l'Araignée",
L"03 Pluie sur la Rivière",
L"02 Prospérité Urbaine",
L"35 Sourire",
L"36 Esprit Élevé",
L"29 Voyage",
L"50 Bataille 50",
L"07 Chemin de la Gloire",
L"22 Bataille du Royaume des Esprits",
L"45 Bataille Acharnée",
L"13 Dernier Stand",
L"53 Bannière Sacrificielle",
L"14 Boire le Ressentiment",
L"15 Larmes d'Automne",
L"46 Rêves des Confins",
L"11 Royaume dans les Nuages",
L"19 Champ de Bataille",
L"10 Errance sur les Fleuves",
L"04 Festin de Printemps au Pavillon Rouge",
L"31 Charger la Formation",
L"30 Flammes de Guerre",
L"21 Chaque Pas une Forteresse",
L"41 Vrais Sentiments",
L"37 Regard en Arrière",
L"54 Mystère",
L"09 Brume et Givre",
L"20 Abîme Froid",
L"06 Beauté dans les Flammes",
L"16 Lune Pâle",
L"32 Monter au Ciel",
L"23 Chant de la Nuit",
L"34 Bataille Décisive",
L"★Fin",
L"40 Souvenirs Lointains",
L"★Ouverture",
L"★Vidéo",
L"★Logo FALCOM"
};

TCHAR tisan1_it[][128] = {
L"42 Melodia Eterna",
L"01 Residenza del Pino Antico",
L"27 Tensione",
L"12 Soffiato dal Vento",
L"48 Esercito Infranto",
L"49 Battaglia 49",
L"39 Addio Addolorato",
L"38 Pensieri Oscuri",
L"47 Nebbia della Valle",
L"05 Paradiso Rurale",
L"51 Battaglia 51",
L"08 Caverna del Ragno",
L"03 Pioggia sul Fiume",
L"02 Prosperità Urbana",
L"35 Sorriso",
L"36 Spirito Alto",
L"29 Viaggio",
L"50 Battaglia 50",
L"07 Cammino della Gloria",
L"22 Battaglia del Regno dei Fantasmi",
L"45 Battaglia Feroce",
L"13 Ultima Resistenza",
L"53 Stendardo Sacrificale",
L"14 Bere il Rancore",
L"15 Lacrime d'Autunno",
L"46 Sogni ai Confini",
L"11 Regno tra le Nuvole",
L"19 Campo di Battaglia",
L"10 Vagabondaggio sui Fiumi",
L"04 Festa di Primavera al Padiglione Rosso",
L"31 Caricare la Formazione",
L"30 Fiamme di Guerra",
L"21 Ogni Passo una Fortezza",
L"41 Veri Sentimenti",
L"37 Sguardo Indietro",
L"54 Mistero",
L"09 Nebbia e Gelo",
L"20 Abisso Freddo",
L"06 Bellezza tra le Fiamme",
L"16 Luna Pallida",
L"32 Salire al Cielo",
L"23 Canto della Notte",
L"34 Battaglia Decisiva",
L"★Fine",
L"40 Ricordi Lontani",
L"★Apertura",
L"★Video",
L"★Logo FALCOM"
};

TCHAR tisan1_es[][128] = {
L"42 Melodía Eterna",
L"01 Residencia del Pino Antiguo",
L"27 Tensión",
L"12 Soplado por el Viento",
L"48 Ejército Destrozado",
L"49 Batalla 49",
L"39 Despedida Dolorosa",
L"38 Pensamientos Oscuros",
L"47 Niebla del Valle",
L"05 Paraíso Rural",
L"51 Batalla 51",
L"08 Cueva de la Araña",
L"03 Lluvia en el Río",
L"02 Prosperidad Urbana",
L"35 Sonrisa",
L"36 Ánimo Alto",
L"29 Viaje",
L"50 Batalla 50",
L"07 Camino de la Gloria",
L"22 Batalla del Reino de los Espíritus",
L"45 Batalla Feroz",
L"13 Última Resistencia",
L"53 Estandarte Sacrificial",
L"14 Beber el Resentimiento",
L"15 Lágrimas de Otoño",
L"46 Sueños de los Confines",
L"11 Reino en las Nubes",
L"19 Campo de Batalla",
L"10 Vagabundeo por los Ríos",
L"04 Festín de Primavera en el Pabellón Rojo",
L"31 Cargar la Formación",
L"30 Llamas de Guerra",
L"21 Cada Paso una Fortaleza",
L"41 Verdaderos Sentimientos",
L"37 Mirada Atrás",
L"54 Misterio",
L"09 Niebla y Escarcha",
L"20 Abismo Frío",
L"06 Belleza en las Llamas",
L"16 Luna Pálida",
L"32 Ascender al Cielo",
L"23 Canto Nocturno",
L"34 Batalla Decisiva",
L"★Fin",
L"40 Recuerdos Lejanos",
L"★Apertura",
L"★Video",
L"★Logo FALCOM"
};

TCHAR tisan1_ko[][128] = {
L"42 영원한 멜로디",
L"01 고송거주",
L"27 긴박",
L"12 풍동",
L"48 파군",
L"49 전투49",
L"39 비결",
L"38 암사량",
L"47 유곡윤",
L"05 맥상선향",
L"51 전투51",
L"08 지주동",
L"03 림강우",
L"02 시정영화",
L"35 소소",
L"36 채비양",
L"29 정도",
L"50 전투50",
L"07 공명로",
L"22 귀계전투",
L"45 격전",
L"13 배수일전",
L"53 제기",
L"14 음유한",
L"15 추슬루",
L"46 천애혼몽",
L"11 운중경",
L"19 전장",
L"10 강호행",
L"04 주루춘연",
L"31 충진",
L"30 전화",
L"21 보보위영",
L"41 충정",
L"37 회수전진",
L"54 현기",
L"09 환무성상",
L"20 수한연",
L"06 봉화홍안",
L"16 월창망",
L"32 등천",
L"23 화야곡",
L"34 결전천리",
L"★끝",
L"40 요상억",
L"★오프닝",
L"★동영상",
L"★팔콤로고"
};

TCHAR tisan1_zh[][128] = {
L"42 永恒的旋律",
L"01 古松居",
L"27 紧迫",
L"12 风动",
L"48 破军",
L"49 战斗49",
L"39 悲诀",
L"38 暗思量",
L"47 幽谷润",
L"05 陌上仙乡",
L"51 战斗51",
L"08 蜘蛛洞",
L"03 临江雨",
L"02 市井荣华",
L"35 笑笑",
L"36 采飞扬",
L"29 征途",
L"50 战斗50",
L"07 功名路",
L"22 鬼界战斗",
L"45 激战",
L"13 背水一战",
L"53 祭旗",
L"14 饮幽恨",
L"15 秋瑟泪",
L"46 天涯魂梦",
L"11 云中境",
L"19 战场",
L"10 江湖行",
L"04 朱楼春宴",
L"31 冲阵",
L"30 战火",
L"21 步步为营",
L"41 衷情",
L"37 回首前尘",
L"54 玄机",
L"09 幻雾星霜",
L"20 水寒渊",
L"06 烽火红颜",
L"16 月苍茫",
L"32 登天",
L"23 华夜曲",
L"34 决战千里",
L"★尽头",
L"40 遥相忆",
L"★开场",
L"★动画",
L"★法尔康标志"
};

TCHAR tisan1_ar[][128] = {
L"42 اللحن الأبدي",
L"01 مقر الصنوبر القديم",
L"27 التوتر",
L"12 مهب الريح",
L"48 الجيش المكسور",
L"49 معركة 49",
L"39 الوداع المؤلم",
L"38 الأفكار المظلمة",
L"47 ضباب الوادي",
L"05 الجنة الريفية",
L"51 معركة 51",
L"08 كهف العنكبوت",
L"03 المطر على النهر",
L"02 الازدهار الحضري",
L"35 الابتسامة",
L"36 الروح العالية",
L"29 الرحلة",
L"50 معركة 50",
L"07 طريق المجد",
L"22 معركة عالم الأرواح",
L"45 معركة شرسة",
L"13 الموقف الأخير",
L"53 الراية التضحية",
L"14 شرب الاستياء",
L"15 دموع الخريف",
L"46 أحلام الأطراف",
L"11 المملكة في السحاب",
L"19 ساحة المعركة",
L"10 التجول في الأنهار",
L"04 وليمة الربيع في الجناح الأحمر",
L"31 شحن التشكيلة",
L"30 ألسنة الحرب",
L"21 كل خطوة حصن",
L"41 المشاعر الحقيقية",
L"37 النظر للوراء",
L"54 الغموض",
L"09 الضباب والصقيع",
L"20 الهاوية الباردة",
L"06 الجمال في اللهب",
L"16 القمر الشاحب",
L"32 الصعود إلى السماء",
L"23 أغنية الليل",
L"34 المعركة الحاسمة",
L"★النهاية",
L"40 الذكريات البعيدة",
L"★المقدمة",
L"★الفيديو",
L"★شعار فالكوم"
};

TCHAR tisan1_ru[][128] = {
L"42 Вечная Мелодия",
L"01 Резиденция Древней Сосны",
L"27 Напряжение",
L"12 Унесённый Ветром",
L"48 Разбитая Армия",
L"49 Битва 49",
L"39 Болезненное Прощание",
L"38 Тёмные Мысли",
L"47 Туман Долины",
L"05 Сельский Рай",
L"51 Битва 51",
L"08 Паучья Пещера",
L"03 Дождь на Реке",
L"02 Городское Процветание",
L"35 Улыбка",
L"36 Высокий Дух",
L"29 Путешествие",
L"50 Битва 50",
L"07 Путь Славы",
L"22 Битва Царства Духов",
L"45 Ожесточённая Битва",
L"13 Последний Рубеж",
L"53 Жертвенное Знамя",
L"14 Пить Обиду",
L"15 Осенние Слёзы",
L"46 Сны о Краях Света",
L"11 Царство в Облаках",
L"19 Поле Битвы",
L"10 Скитания по Рекам",
L"04 Весенний Пир в Красном Павильоне",
L"31 Атаковать Формацию",
L"30 Пламя Войны",
L"21 Каждый Шаг - Крепость",
L"41 Истинные Чувства",
L"37 Взгляд Назад",
L"54 Тайна",
L"09 Туман и Иней",
L"20 Холодная Бездна",
L"06 Красота в Пламени",
L"16 Бледная Луна",
L"32 Восхождение к Небу",
L"23 Ночная Песня",
L"34 Решающая Битва",
L"★Конец",
L"40 Давние Воспоминания",
L"★Заставка",
L"★Видео",
L"★Логотип FALCOM"
};

TCHAR tisan1_de[][128] = {
L"42 Ewige Melodie",
L"01 Alte Kiefernresidenz",
L"27 Spannung",
L"12 Vom Wind Getragen",
L"48 Gebrochene Armee",
L"49 Schlacht 49",
L"39 Schmerzlicher Abschied",
L"38 Dunkle Gedanken",
L"47 Taltreib",
L"05 Ländliches Paradies",
L"51 Schlacht 51",
L"08 Spinnenhöhle",
L"03 Regen am Fluss",
L"02 Städtischer Wohlstand",
L"35 Lächeln",
L"36 Hoher Geist",
L"29 Reise",
L"50 Schlacht 50",
L"07 Weg des Ruhmes",
L"22 Geisterreich-Schlacht",
L"45 Erbitterte Schlacht",
L"13 Letztes Gefecht",
L"53 Opferbanner",
L"14 Groll Trinken",
L"15 Herbsttränen",
L"46 Träume der Weltenden",
L"11 Reich in den Wolken",
L"19 Schlachtfeld",
L"10 Auf den Flüssen Wandern",
L"04 Frühlingsfest im Roten Pavillon",
L"31 Formation Angreifen",
L"30 Kriegsflammen",
L"21 Jeder Schritt eine Festung",
L"41 Wahre Gefühle",
L"37 Rückblick",
L"54 Geheimnis",
L"09 Nebel und Frost",
L"20 Kalter Abgrund",
L"06 Schönheit in den Flammen",
L"16 Blasser Mond",
L"32 Zum Himmel Aufsteigen",
L"23 Nachtlied",
L"34 Entscheidungsschlacht",
L"★Ende",
L"40 Ferne Erinnerungen",
L"★Eröffnung",
L"★Video",
L"★FALCOM Logo"
};

TCHAR tisan1_pt[][128] = {
L"42 Melodia Eterna",
L"01 Residência do Pinheiro Antigo",
L"27 Tensão",
L"12 Levado pelo Vento",
L"48 Exército Destruído",
L"49 Batalha 49",
L"39 Adeus Doloroso",
L"38 Pensamentos Sombrios",
L"47 Névoa do Vale",
L"05 Paraíso Rural",
L"51 Batalha 51",
L"08 Caverna da Aranha",
L"03 Chuva no Rio",
L"02 Prosperidade Urbana",
L"35 Sorriso",
L"36 Espírito Alto",
L"29 Jornada",
L"50 Batalha 50",
L"07 Caminho da Glória",
L"22 Batalha do Reino dos Espíritos",
L"45 Batalha Feroz",
L"13 Última Resistência",
L"53 Estandarte Sacrificial",
L"14 Beber o Ressentimento",
L"15 Lágrimas de Outono",
L"46 Sonhos dos Confins",
L"11 Reino nas Nuvens",
L"19 Campo de Batalha",
L"10 Vagabundagem nos Rios",
L"04 Festim de Primavera no Pavilhão Vermelho",
L"31 Atacar a Formação",
L"30 Chamas de Guerra",
L"21 Cada Passo uma Fortaleza",
L"41 Verdadeiros Sentimentos",
L"37 Olhar para Trás",
L"54 Mistério",
L"09 Névoa e Geada",
L"20 Abismo Frio",
L"06 Beleza nas Chamas",
L"16 Lua Pálida",
L"32 Subir ao Céu",
L"23 Canção da Noite",
L"34 Batalha Decisiva",
L"★Fim",
L"40 Memórias Distantes",
L"★Abertura",
L"★Vídeo",
L"★Logo FALCOM"
};

TCHAR tisan1_nl[][128] = {
L"42 Eeuwige Melodie",
L"01 Oude Den Residentie",
L"27 Spanning",
L"12 Gedragen door de Wind",
L"48 Gebroken Leger",
L"49 Slag 49",
L"39 Hartverscheurend Afscheid",
L"38 Donkere Gedachten",
L"47 Vallei Mist",
L"05 Landelijk Paradijs",
L"51 Slag 51",
L"08 Spinnengrot",
L"03 Regen aan de Rivier",
L"02 Stedelijke Welvaart",
L"35 Glimlach",
L"36 Hoge Geest",
L"29 Reis",
L"50 Slag 50",
L"07 Pad van Glorie",
L"22 Geestenrijk Slag",
L"45 Verbeten Slag",
L"13 Laatste Strijd",
L"53 Offerbanner",
L"14 Wrok Drinken",
L"15 Herfsttranen",
L"46 Dromen van de Einden",
L"11 Rijk in de Wolken",
L"19 Slagveld",
L"10 Zwerven op de Rivieren",
L"04 Lentefeest in de Rode Paviljoen",
L"31 Formatie Bestormen",
L"30 Oorlogsvlammen",
L"21 Elke Stap een Fort",
L"41 Ware Gevoelens",
L"37 Terugblik",
L"54 Mysterie",
L"09 Mist en Vorst",
L"20 Koude Afgrond",
L"06 Schoonheid in de Vlammen",
L"16 Bleke Maan",
L"32 Opstijgen naar de Hemel",
L"23 Nachtlied",
L"34 Beslissende Slag",
L"★Einde",
L"40 Verre Herinneringen",
L"★Opening",
L"★Video",
L"★FALCOM Logo"
};

TCHAR tisan1_pl[][128] = {
L"42 Wieczna Melodia",
L"01 Rezydencja Starej Sosny",
L"27 Napięcie",
L"12 Niesiony przez Wiatr",
L"48 Złamana Armia",
L"49 Bitwa 49",
L"39 Bolesne Pożegnanie",
L"38 Mroczne Myśli",
L"47 Mgła Doliny",
L"05 Wiejski Raj",
L"51 Bitwa 51",
L"08 Jaskinia Pająka",
L"03 Deszcz nad Rzeką",
L"02 Miejski Dobrobyt",
L"35 Uśmiech",
L"36 Wysoki Duch",
L"29 Podróż",
L"50 Bitwa 50",
L"07 Ścieżka Chwały",
L"22 Bitwa Królestwa Duchów",
L"45 Zacięta Bitwa",
L"13 Ostatni Bastion",
L"53 Sztandar Ofiarny",
L"14 Pić Urazę",
L"15 Jesienne Łzy",
L"46 Sny o Krańcach",
L"11 Królestwo w Chmurach",
L"19 Pole Bitwy",
L"10 Wędrówka po Rzekach",
L"04 Wiosenna Uczta w Czerwonym Pawilonie",
L"31 Atakować Formację",
L"30 Płomienie Wojny",
L"21 Każdy Krok Twierdzą",
L"41 Prawdziwe Uczucia",
L"37 Spojrzenie Wstecz",
L"54 Tajemnica",
L"09 Mgła i Mróz",
L"20 Zimna Otchłań",
L"06 Piękno w Płomieniach",
L"16 Blada Księżyc",
L"32 Wznoszenie się do Nieba",
L"23 Pieśń Nocy",
L"34 Decydująca Bitwa",
L"★Koniec",
L"40 Odległe Wspomnienia",
L"★Otwarcie",
L"★Wideo",
L"★Logo FALCOM"
};

TCHAR tisan1_tr[][128] = {
L"42 Sonsuz Melodi",
L"01 Eski Çam İkametgâhı",
L"27 Gerginlik",
L"12 Rüzgarla Taşınan",
L"48 Parçalanmış Ordu",
L"49 Savaş 49",
L"39 Acılı Veda",
L"38 Karanlık Düşünceler",
L"47 Vadi Sisleri",
L"05 Kırsal Cennet",
L"51 Savaş 51",
L"08 Örümcek Mağarası",
L"03 Nehre Yağmur",
L"02 Kentsel Refah",
L"35 Gülümseme",
L"36 Yüksek Ruh",
L"29 Yolculuk",
L"50 Savaş 50",
L"07 Zafer Yolu",
L"22 Hayalet Diyarı Savaşı",
L"45 Şiddetli Savaş",
L"13 Son Direniş",
L"53 Kurban Bayrağı",
L"14 Kırgınlık İçmek",
L"15 Sonbahar Gözyaşları",
L"46 Uçların Rüyaları",
L"11 Bulutlardaki Krallık",
L"19 Savaş Alanı",
L"10 Nehirlerde Gezinme",
L"04 Kırmızı Köşkte Bahar Şöleni",
L"31 Formasyonu Yüklemek",
L"30 Savaş Alevleri",
L"21 Her Adım Bir Kale",
L"41 Gerçek Duygular",
L"37 Geriye Bakış",
L"54 Gizem",
L"09 Sis ve Don",
L"20 Soğuk Uçurum",
L"06 Alevlerdeki Güzellik",
L"16 Soluk Ay",
L"32 Göğe Yükselmek",
L"23 Gece Şarkısı",
L"34 Karar Savaşı",
L"★Son",
L"40 Uzak Anılar",
L"★Açılış",
L"★Video",
L"★FALCOM Logosu"
};

static inline CString San1Track(int i) {
	switch (savedata.lang) {
	case 0: return CString(tisan1[i]);
	case 1: return CString(tisan1_en[i]);
	case 2: return CString(tisan1_fr[i]);
	case 3: return CString(tisan1_it[i]);
	case 4: return CString(tisan1_es[i]);
	case 5: return CString(tisan1_ko[i]);
	case 6: return CString(tisan1_zh[i]);
	case 7: return CString(tisan1_ar[i]);
	case 8: return CString(tisan1_ru[i]);
	case 9: return CString(tisan1_de[i]);
	case 10: return CString(tisan1_pt[i]);
	case 11: return CString(tisan1_nl[i]);
	case 12: return CString(tisan1_pl[i]);
	case 13: return CString(tisan1_tr[i]);
	default: return CString(tisan1_en[i]);
	}
}

CString CSan1::Gett(int a) {
	CString s, ss;
	s = San1Track(a);
	ss = s.Left(2); ss.TrimRight();
	fnn = s.Mid(3);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CSan1::OnDblclkList1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx = m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = San1Track(idx);
	ret = s.Left(2); ret.TrimRight();
	ret2 = m_list.GetCurSel();
#if UNICODE
	if (s.Left(1) == "★") {
		fnn = s.Mid(1);
#else
	if (s.Left(2) == "★") {
		fnn = s.Mid(2);
#endif
	}
	else {
		fnn = s.Mid(3);
	}
	EndDialog(1567);
	}

BOOL CSan1::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"幻想三国志１", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"환상삼국지 1", L"幻想三国志1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1", L"Fantasy Sanguo 1"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close"));
	SetDlgItemText(IDC_STATIC, LL14(L"動画(★印)を再生するにはBinkの環境が必要です\nreadme.txtを読んで導入してください。", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup.", L"Bink runtime required for video (★) playback.\nSee readme.txt for setup."));

	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for (int i = 0; i < (41 + 3 + 4); i++)
	{
		CString s = San1Track(i);
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
