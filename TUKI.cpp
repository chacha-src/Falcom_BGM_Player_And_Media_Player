// TUKI.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "TUKI.h"


// CTUKI ダイアログ

IMPLEMENT_DYNAMIC(CTUKI, CCustomBlurDialogBase)

CTUKI::CTUKI(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CTUKI::IDD, pParent)
{

}

CTUKI::~CTUKI()
{
}

void CTUKI::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CTUKI, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CTUKI);


// CTUKI メッセージ ハンドラ
extern CString fnn;

TCHAR tituki[][128] = {
L"Mc000 愛的廃墟",
L"Mc001 幽謐",
L"Mc003 歡潮",
L"Mc011 暢游",
L"Mc002 詭影",
L"Mc013 月泉",
L"Mc014 山雨欲來",
L"Mc004 絲路（鼓鈴）",
L"Mc015 踏春",
L"Mc005 快板小調／滑稽小調",
L"Mc016 夢魘",
L"Mc006 追思",
L"Mc017 暮歸",
L"Mc007 離情",
L"Mc018 空山",
L"Mc008 秋夜",
L"Mc019 塵思",
L"Mc020 密殺",
L"Mc021 埋伏",
L"Mc022 決戰",
L"Mc012 嬉戲",
L"Mc023 忘憂",
L"Mc024 天國梵音",
L"Mc025 激殺",
L"Mc026 怒殺",
L"Mc027 血戰",
L"Mc028 刃敵",
L"Mc075 (不明)",
L"★ファルコムロゴ",
L"★オープニング",
L"★スタッフロール",
L"★エンディング１",
L"★エンディング２",
L"★エンディング３",
L"★動画１",
L"★動画２",
L"★動画３",
L"★動画４",
L"★動画５"
};

TCHAR tituki_en[][128] = {
L"Mc000 Ruins of Love",
L"Mc001 Serenity",
L"Mc003 Joyful Tide",
L"Mc011 Free Roaming",
L"Mc002 Phantom Shadow",
L"Mc013 Moon Spring",
L"Mc014 Storm Approaching",
L"Mc004 Silk Road(Drum Bell)",
L"Mc015 Spring Walk",
L"Mc005 Allegro/Comic Tune",
L"Mc016 Nightmare",
L"Mc006 Remembrance",
L"Mc017 Evening Return",
L"Mc007 Parting",
L"Mc018 Empty Mountain",
L"Mc008 Autumn Night",
L"Mc019 Dust of Thought",
L"Mc020 Secret Assassin",
L"Mc021 Ambush",
L"Mc022 Decisive Battle",
L"Mc012 Play",
L"Mc023 Forget Sorrow",
L"Mc024 Heavenly Chant",
L"Mc025 Fierce Kill",
L"Mc026 Rage Kill",
L"Mc027 Blood Battle",
L"Mc028 Blade Enemy",
L"Mc075 (Unknown)",
L"★FALCOM logo",
L"★Opening",
L"★Staff roll",
L"★Ending 1",
L"★Ending 2",
L"★Ending 3",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5"
};

TCHAR tituki_fr[][128] = {
L"Mc000 Ruines de l'Amour",
L"Mc001 Sérénité",
L"Mc003 Marée Joyeuse",
L"Mc011 Errance Libre",
L"Mc002 Ombre Fantôme",
L"Mc013 Source de Lune",
L"Mc014 Tempête Imminente",
L"Mc004 Route de la Soie(Tambour Cloche)",
L"Mc015 Promenade de Printemps",
L"Mc005 Allegro/Comique",
L"Mc016 Cauchemar",
L"Mc006 Souvenir",
L"Mc017 Retour du Soir",
L"Mc007 Séparation",
L"Mc018 Montagne Vide",
L"Mc008 Nuit d'Automne",
L"Mc019 Poussière de Pensée",
L"Mc020 Assassin Secret",
L"Mc021 Embuscade",
L"Mc022 Bataille Décisive",
L"Mc012 Jeu",
L"Mc023 Oublier le Chagrin",
L"Mc024 Chant Céleste",
L"Mc025 Meurtre Féroce",
L"Mc026 Meurtre de Rage",
L"Mc027 Bataille Sanglante",
L"Mc028 Ennemi Lame",
L"Mc075 (Inconnu)",
L"★Logo FALCOM",
L"★Ouverture",
L"★Générique",
L"★Final 1",
L"★Final 2",
L"★Final 3",
L"★Vidéo 1",
L"★Vidéo 2",
L"★Vidéo 3",
L"★Vidéo 4",
L"★Vidéo 5"
};

TCHAR tituki_it[][128] = {
L"Mc000 Rovine dell'Amore",
L"Mc001 Serenità",
L"Mc003 Marea Gioiosa",
L"Mc011 Vagabondaggio Libero",
L"Mc002 Ombra Fantasma",
L"Mc013 Sorgente di Luna",
L"Mc014 Tempesta In Arrivo",
L"Mc004 Via della Seta(Tamburo Campana)",
L"Mc015 Passeggiata di Primavera",
L"Mc005 Allegro/Comico",
L"Mc016 Incubo",
L"Mc006 Ricordo",
L"Mc017 Ritorno Sera",
L"Mc007 Separazione",
L"Mc018 Montagna Vuota",
L"Mc008 Notte d'Autunno",
L"Mc019 Polvere di Pensiero",
L"Mc020 Assassino Segreto",
L"Mc021 Agguato",
L"Mc022 Battaglia Decisiva",
L"Mc012 Gioco",
L"Mc023 Dimenticare il Dolore",
L"Mc024 Canto Celeste",
L"Mc025 Uccisione Feroce",
L"Mc026 Uccisione per Rabbia",
L"Mc027 Battaglia di Sangue",
L"Mc028 Nemico Lama",
L"Mc075 (Sconosciuto)",
L"★Logo FALCOM",
L"★Apertura",
L"★Titoli",
L"★Finale 1",
L"★Finale 2",
L"★Finale 3",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5"
};

TCHAR tituki_es[][128] = {
L"Mc000 Ruinas del Amor",
L"Mc001 Serenidad",
L"Mc003 Marea Alegre",
L"Mc011 Vagabundeo Libre",
L"Mc002 Sombra Fantasma",
L"Mc013 Fuente Lunar",
L"Mc014 Tormenta Inminente",
L"Mc004 Ruta de la Seda(Tambor Campana)",
L"Mc015 Paseo de Primavera",
L"Mc005 Allegro/Cómico",
L"Mc016 Pesadilla",
L"Mc006 Recuerdo",
L"Mc017 Regreso del Atardecer",
L"Mc007 Separación",
L"Mc018 Montaña Vacía",
L"Mc008 Noche de Otoño",
L"Mc019 Polvo de Pensamiento",
L"Mc020 Asesino Secreto",
L"Mc021 Emboscada",
L"Mc022 Batalla Decisiva",
L"Mc012 Juego",
L"Mc023 Olvidar el Dolor",
L"Mc024 Canto Celestial",
L"Mc025 Muerte Feroz",
L"Mc026 Muerte por Ira",
L"Mc027 Batalla Sangrienta",
L"Mc028 Enemigo Filo",
L"Mc075 (Desconocido)",
L"★Logo FALCOM",
L"★Apertura",
L"★Créditos",
L"★Final 1",
L"★Final 2",
L"★Final 3",
L"★Vídeo 1",
L"★Vídeo 2",
L"★Vídeo 3",
L"★Vídeo 4",
L"★Vídeo 5"
};

TCHAR tituki_ko[][128] = {
L"Mc000 애의 폐허",
L"Mc001 유묵",
L"Mc003 환조",
L"Mc011 창유",
L"Mc002 궤영",
L"Mc013 월천",
L"Mc014 산우욕래",
L"Mc004 실로(고령)",
L"Mc015 답춘",
L"Mc005 쾌판소조/골계소조",
L"Mc016 악몽",
L"Mc006 추사",
L"Mc017 모귀",
L"Mc007 리정",
L"Mc018 공산",
L"Mc008 추야",
L"Mc019 진사",
L"Mc020 밀살",
L"Mc021 매복",
L"Mc022 결전",
L"Mc012 희희",
L"Mc023 망우",
L"Mc024 천국범음",
L"Mc025 격살",
L"Mc026 노살",
L"Mc027 혈전",
L"Mc028 인적",
L"Mc075 (불명)",
L"★팔콤 로고",
L"★오프닝",
L"★스태프 롤",
L"★엔딩 1",
L"★엔딩 2",
L"★엔딩 3",
L"★동영상 1",
L"★동영상 2",
L"★동영상 3",
L"★동영상 4",
L"★동영상 5"
};

TCHAR tituki_zh[][128] = {
L"Mc000 爱的废墟",
L"Mc001 幽谧",
L"Mc003 欢潮",
L"Mc011 畅游",
L"Mc002 诡影",
L"Mc013 月泉",
L"Mc014 山雨欲来",
L"Mc004 丝路（鼓铃）",
L"Mc015 踏春",
L"Mc005 快板小调/滑稽小调",
L"Mc016 梦魇",
L"Mc006 追思",
L"Mc017 暮归",
L"Mc007 离情",
L"Mc018 空山",
L"Mc008 秋夜",
L"Mc019 尘思",
L"Mc020 密杀",
L"Mc021 埋伏",
L"Mc022 决战",
L"Mc012 嬉戏",
L"Mc023 忘忧",
L"Mc024 天国梵音",
L"Mc025 激杀",
L"Mc026 怒杀",
L"Mc027 血战",
L"Mc028 刃敌",
L"Mc075 (不明)",
L"★FALCOM标志",
L"★片头曲",
L"★职员表",
L"★结局1",
L"★结局2",
L"★结局3",
L"★动画1",
L"★动画2",
L"★动画3",
L"★动画4",
L"★动画5"
};

TCHAR tituki_ar[][128] = {
L"Mc000 أطلال الحب",
L"Mc001 السكينة",
L"Mc003 مد الفرح",
L"Mc011 الترحال الحر",
L"Mc002 ظل الشبح",
L"Mc013 نبع القمر",
L"Mc014 العاصفة قادمة",
L"Mc004 طريق الحرير(طبل جرس)",
L"Mc015 نزهة الربيع",
L"Mc005 أليغرو/هزلي",
L"Mc016 كابوس",
L"Mc006 الذكرى",
L"Mc017 العودة عند المغرب",
L"Mc007 الفراق",
L"Mc018 الجبل الفارغ",
L"Mc008 ليلة الخريف",
L"Mc019 غبار الفكر",
L"Mc020 قاتل سري",
L"Mc021 كمين",
L"Mc022 المعركة الحاسمة",
L"Mc012 اللعب",
L"Mc023 نسيان الحزن",
L"Mc024 الترنيمة السماوية",
L"Mc025 القتل الشرس",
L"Mc026 القتل بالغضب",
L"Mc027 المعركة الدموية",
L"Mc028 عدو النصل",
L"Mc075 (غير معروف)",
L"★شعار FALCOM",
L"★المقدمة",
L"★طاقم العمل",
L"★النهاية 1",
L"★النهاية 2",
L"★النهاية 3",
L"★فيديو 1",
L"★فيديو 2",
L"★فيديو 3",
L"★فيديو 4",
L"★فيديو 5"
};

TCHAR tituki_ru[][128] = {
L"Mc000 Руины Любви",
L"Mc001 Безмятежность",
L"Mc003 Радостный Прилив",
L"Mc011 Свободное Странствие",
L"Mc002 Призрачная Тень",
L"Mc013 Лунный Источник",
L"Mc014 Приближающаяся Буря",
L"Mc004 Шёлковый Путь(Барабан Колокол)",
L"Mc015 Весенняя Прогулка",
L"Mc005 Аллегро/Комическая",
L"Mc016 Кошмар",
L"Mc006 Воспоминание",
L"Mc017 Вечернее Возвращение",
L"Mc007 Разлука",
L"Mc018 Пустая Гора",
L"Mc008 Осенняя Ночь",
L"Mc019 Пыль Мыслей",
L"Mc020 Тайный Убийца",
L"Mc021 Засада",
L"Mc022 Решающая Битва",
L"Mc012 Игра",
L"Mc023 Забвение Печали",
L"Mc024 Небесное Пение",
L"Mc025 Яростное Убийство",
L"Mc026 Убийство от Ярости",
L"Mc027 Кровавая Битва",
L"Mc028 Враг Лезвия",
L"Mc075 (Неизвестно)",
L"★Лого FALCOM",
L"★Заставка",
L"★Титры",
L"★Финал 1",
L"★Финал 2",
L"★Финал 3",
L"★Видео 1",
L"★Видео 2",
L"★Видео 3",
L"★Видео 4",
L"★Видео 5"
};

TCHAR tituki_de[][128] = {
L"Mc000 Ruinen der Liebe",
L"Mc001 Heiterkeit",
L"Mc003 Freudige Flut",
L"Mc011 Freies Umherstreifen",
L"Mc002 Phantom-Schatten",
L"Mc013 Mondquelle",
L"Mc014 Sturm Naht",
L"Mc004 Seidenstraße(Trommel Glocke)",
L"Mc015 Frühlingsspaziergang",
L"Mc005 Allegro/Komisch",
L"Mc016 Albtraum",
L"Mc006 Erinnerung",
L"Mc017 Abendliche Rückkehr",
L"Mc007 Abschied",
L"Mc018 Leerer Berg",
L"Mc008 Herbstnacht",
L"Mc019 Gedankenstaub",
L"Mc020 Geheimer Attentäter",
L"Mc021 Hinterhalt",
L"Mc022 Entscheidungsschlacht",
L"Mc012 Spiel",
L"Mc023 Schmerz Vergessen",
L"Mc024 Himmlischer Gesang",
L"Mc025 Heftige Tötung",
L"Mc026 Tötung aus Wut",
L"Mc027 Blutige Schlacht",
L"Mc028 Klingen-Feind",
L"Mc075 (Unbekannt)",
L"★FALCOM Logo",
L"★Eröffnung",
L"★Abspann",
L"★Ende 1",
L"★Ende 2",
L"★Ende 3",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5"
};

TCHAR tituki_pt[][128] = {
L"Mc000 Ruínas do Amor",
L"Mc001 Serenidade",
L"Mc003 Maré Alegre",
L"Mc011 Vagabundagem Livre",
L"Mc002 Sombra Fantasma",
L"Mc013 Fonte Lunar",
L"Mc014 Tempestade Próxima",
L"Mc004 Rota da Seda(Tambor Sino)",
L"Mc015 Passeio de Primavera",
L"Mc005 Allegro/Cômico",
L"Mc016 Pesadelo",
L"Mc006 Lembrança",
L"Mc017 Retorno ao Entardecer",
L"Mc007 Separação",
L"Mc018 Montanha Vazia",
L"Mc008 Noite de Outono",
L"Mc019 Pó de Pensamento",
L"Mc020 Assassino Secreto",
L"Mc021 Emboscada",
L"Mc022 Batalha Decisiva",
L"Mc012 Brincadeira",
L"Mc023 Esquecer a Mágoa",
L"Mc024 Canto Celestial",
L"Mc025 Morte Feroz",
L"Mc026 Morte por Raiva",
L"Mc027 Batalha Sangrenta",
L"Mc028 Inimigo Lâmina",
L"Mc075 (Desconhecido)",
L"★Logo FALCOM",
L"★Abertura",
L"★Créditos",
L"★Final 1",
L"★Final 2",
L"★Final 3",
L"★Vídeo 1",
L"★Vídeo 2",
L"★Vídeo 3",
L"★Vídeo 4",
L"★Vídeo 5"
};

TCHAR tituki_nl[][128] = {
L"Mc000 Ruïnes van Liefde",
L"Mc001 Sereniteit",
L"Mc003 Vrolijke Vloed",
L"Mc011 Vrij Zwerven",
L"Mc002 Fantoom Schaduw",
L"Mc013 Maanbron",
L"Mc014 Storm Nabij",
L"Mc004 Zijderoute(Trommel Bel)",
L"Mc015 Lentewandeling",
L"Mc005 Allegro/Komisch",
L"Mc016 Nachtmerrie",
L"Mc006 Herinnering",
L"Mc017 Avondelijke Terugkeer",
L"Mc007 Scheiding",
L"Mc018 Lege Berg",
L"Mc008 Herfstnacht",
L"Mc019 Gedachtestof",
L"Mc020 Geheime Moordenaar",
L"Mc021 Hinderlaag",
L"Mc022 Beslissende Strijd",
L"Mc012 Spel",
L"Mc023 Verdriet Vergeten",
L"Mc024 Hemels gezang",
L"Mc025 Felle Dood",
L"Mc026 Dood door Woede",
L"Mc027 Bloedige Strijd",
L"Mc028 Vijand der Kling",
L"Mc075 (Onbekend)",
L"★FALCOM Logo",
L"★Opening",
L"★Aftiteling",
L"★Einde 1",
L"★Einde 2",
L"★Einde 3",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5"
};

TCHAR tituki_pl[][128] = {
L"Mc000 Ruiny Miłości",
L"Mc001 Spokój",
L"Mc003 Radosny Przypływ",
L"Mc011 Wolna Wędrówka",
L"Mc002 Cień Widma",
L"Mc013 Źródło Księżyca",
L"Mc014 Nadciągająca Burza",
L"Mc004 Jedwabny Szlak(Bęben Dzwon)",
L"Mc015 Wiosenny Spacer",
L"Mc005 Allegro/Komiczny",
L"Mc016 Koszmar",
L"Mc006 Wspomnienie",
L"Mc017 Wieczorny Powrót",
L"Mc007 Rozłąka",
L"Mc018 Pusta Góra",
L"Mc008 Jesienna Noc",
L"Mc019 Pył Myśli",
L"Mc020 Tajemniczy Zabójca",
L"Mc021 Zasadzka",
L"Mc022 Decydująca Bitwa",
L"Mc012 Zabawa",
L"Mc023 Zapomnienie Smutku",
L"Mc024 Niebiański Śpiew",
L"Mc025 Ferozne Zabójstwo",
L"Mc026 Zabójstwo z Gniewu",
L"Mc027 Krwawa Bitwa",
L"Mc028 Wróg Ostrza",
L"Mc075 (Nieznany)",
L"★Logo FALCOM",
L"★Otwarcie",
L"★Napisy",
L"★Zakończenie 1",
L"★Zakończenie 2",
L"★Zakończenie 3",
L"★Wideo 1",
L"★Wideo 2",
L"★Wideo 3",
L"★Wideo 4",
L"★Wideo 5"
};

TCHAR tituki_tr[][128] = {
L"Mc000 Aşkın Harabeleri",
L"Mc001 Huzur",
L"Mc003 Neşeli Gelgit",
L"Mc011 Serbest Gezinme",
L"Mc002 Hayalet Gölge",
L"Mc013 Ay Pınarı",
L"Mc014 Fırtına Yaklaşıyor",
L"Mc004 İpek Yolu(Davul Çan)",
L"Mc015 Bahar Yürüyüşü",
L"Mc005 Allegro/Komik",
L"Mc016 Kabus",
L"Mc006 Anı",
L"Mc017 Akşam Dönüşü",
L"Mc007 Ayrılık",
L"Mc018 Boş Dağ",
L"Mc008 Sonbahar Gecesi",
L"Mc019 Düşünce Tozu",
L"Mc020 Gizli Suikastçı",
L"Mc021 Pusu",
L"Mc022 Belirleyici Savaş",
L"Mc012 Oyun",
L"Mc023 Üzüntüyü Unutmak",
L"Mc024 Göksel İlahi",
L"Mc025 Şiddetli Öldürme",
L"Mc026 Öfkeden Öldürme",
L"Mc027 Kanlı Savaş",
L"Mc028 Kılıç Düşmanı",
L"Mc075 (Bilinmiyor)",
L"★FALCOM Logosu",
L"★Açılış",
L"★Jenerik",
L"★Son 1",
L"★Son 2",
L"★Son 3",
L"★Video 1",
L"★Video 2",
L"★Video 3",
L"★Video 4",
L"★Video 5"
};

static inline CString TUKITrack(int i) {
	switch (savedata.lang) {
	case 0: return CString(tituki[i]);
	case 1: return CString(tituki_en[i]);
	case 2: return CString(tituki_fr[i]);
	case 3: return CString(tituki_it[i]);
	case 4: return CString(tituki_es[i]);
	case 5: return CString(tituki_ko[i]);
	case 6: return CString(tituki_zh[i]);
	case 7: return CString(tituki_ar[i]);
	case 8: return CString(tituki_ru[i]);
	case 9: return CString(tituki_de[i]);
	case 10: return CString(tituki_pt[i]);
	case 11: return CString(tituki_nl[i]);
	case 12: return CString(tituki_pl[i]);
	case 13: return CString(tituki_tr[i]);
	default: return CString(tituki_en[i]);
	}
}

CString CTUKI::Gett(int a) {
	CString s, ss;
	s = TUKITrack(a);
	ss = s.Left(5); ss.TrimRight();
	fnn = s.Mid(6);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CTUKI::OnDblclkList1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx = m_list.GetItemData(m_list.GetCurSel());
	CString s;	s = TUKITrack(idx);
	ret = s.Left(5); ret.TrimRight();
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
		fnn = s.Mid(6);
	}
	EndDialog(1567);
	}

BOOL CTUKI::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"月影のデスティニー", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny", L"TSUKI no Destiny"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close", L"Close"));

	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for (int i = 0; i < (39); i++)
	{
		CString s = TUKITrack(i);
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
