#include "OxzLook.h"
#include "OxzMenu.h"
#include "OxzLang.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_NONE
#define DWMSBT_NONE 1
#define DWMSBT_TRANSIENTWINDOW 3
#endif

static const OxzPal kPal[OXZ_THEME_COUNT] = {
	{ RGB(255,232,244), RGB(232,214,255), RGB(158,140,228), RGB(208,198,255), RGB(130,70,120),
	  RGB(255,86,150), RGB(240,170,200), RGB(216,132,176),
	  RGB(255,248,252), RGB(220,160,190), RGB(230,200,215),
	  RGB(52,34,58), RGB(108,88,108), RGB(228,234,255), RGB(186,204,248),
	  RGB(255,250,253), RGB(176,118,152) },
	{ RGB(56,74,112), RGB(86,104,148), RGB(40,54,92), RGB(204,176,92), RGB(32,44,74),
	  RGB(168,48,66), RGB(210,180,96), RGB(118,136,172),
	  RGB(48,62,98), RGB(92,110,148), RGB(68,84,122),
	  RGB(240,232,218), RGB(158,164,184), RGB(78,102,156), RGB(52,70,122),
	  RGB(148,164,198), RGB(28,38,64) },
	{ RGB(28,18,36), RGB(48,22,40), RGB(140,24,48), RGB(212,170,64), RGB(80,10,20),
	  RGB(196,32,56), RGB(212,170,64), RGB(160,48,64),
	  RGB(42,28,48), RGB(120,40,56), RGB(60,36,52),
	  RGB(245,230,210), RGB(196,176,166), RGB(80,28,44), RGB(120,36,52),
	  RGB(90,50,60), RGB(20,8,12) },
	{ RGB(12,18,40), RGB(20,36,72), RGB(40,200,220), RGB(120,90,255), RGB(10,40,70),
	  RGB(80,230,255), RGB(160,120,255), RGB(70,160,200),
	  RGB(18,28,52), RGB(50,90,120), RGB(30,50,80),
	  RGB(186,236,214), RGB(150,196,186), RGB(20,60,90), RGB(30,40,100),
	  RGB(80,140,170), RGB(8,16,36) },
	{ RGB(245,240,228), RGB(232,224,206), RGB(160,36,36), RGB(40,36,32), RGB(80,20,20),
	  RGB(176,32,32), RGB(40,36,32), RGB(120,90,70),
	  RGB(250,246,236), RGB(160,130,100), RGB(210,196,170),
	  RGB(32,28,24), RGB(96,84,72), RGB(176,48,48), RGB(128,32,32),
	  RGB(255,250,242), RGB(90,60,48) },
	{ RGB(236,246,232), RGB(210,230,206), RGB(56,120,72), RGB(168,196,120), RGB(30,70,40),
	  RGB(46,130,70), RGB(140,180,90), RGB(90,140,80),
	  RGB(246,252,244), RGB(140,170,130), RGB(200,220,190),
	  RGB(28,48,32), RGB(64,96,68), RGB(220,240,210), RGB(160,200,150),
	  RGB(248,255,246), RGB(70,110,70) },
	{ RGB(255,248,230), RGB(255,230,246), RGB(255,140,180), RGB(255,210,80), RGB(180,80,110),
	  RGB(255,120,160), RGB(255,196,64), RGB(240,160,190),
	  RGB(255,252,246), RGB(230,170,190), RGB(255,220,200),
	  RGB(72,40,56), RGB(112,72,88), RGB(255,236,210), RGB(255,190,210),
	  RGB(255,252,248), RGB(200,120,140) },
	{ RGB(228,230,234), RGB(176,180,188), RGB(70,76,86), RGB(196,140,48), RGB(40,44,50),
	  RGB(186,118,36), RGB(120,128,138), RGB(96,102,112),
	  RGB(236,238,242), RGB(120,126,136), RGB(168,172,180),
	  RGB(28,30,34), RGB(70,74,82), RGB(64,110,168), RGB(48,72,120),
	  RGB(248,250,252), RGB(56,60,68) },
	{ RGB(255,214,232), RGB(255,236,168), RGB(255,80,150), RGB(255,210,70), RGB(120,20,60),
	  RGB(255,45,140), RGB(255,196,40), RGB(255,120,170),
	  RGB(255,248,252), RGB(230,80,140), RGB(255,200,220),
	  RGB(40,16,28), RGB(112,48,76), RGB(255,228,120), RGB(255,160,200),
	  RGB(255,252,248), RGB(160,40,90) },
	{ RGB(40,50,92), RGB(62,70,118), RGB(28,36,72), RGB(220,184,80), RGB(22,28,56),
	  RGB(220,176,72), RGB(152,42,60), RGB(104,96,72),
	  RGB(46,54,100), RGB(188,156,72), RGB(56,62,102),
	  RGB(240,230,206), RGB(176,164,140), RGB(104,82,36), RGB(64,50,24),
	  RGB(212,180,96), RGB(20,26,52) },
	{ RGB(255,236,242), RGB(220,242,214), RGB(255,170,190), RGB(150,200,140), RGB(180,80,110),
	  RGB(240,120,160), RGB(120,180,90), RGB(220,160,180),
	  RGB(255,250,248), RGB(210,160,180), RGB(230,210,200),
	  RGB(60,40,48), RGB(104,76,90), RGB(255,220,228), RGB(200,230,190),
	  RGB(255,252,250), RGB(170,110,130) },
	{ RGB(210,236,255), RGB(255,236,170), RGB(40,150,220), RGB(255,196,40), RGB(20,80,140),
	  RGB(255,176,20), RGB(40,160,230), RGB(120,190,230),
	  RGB(248,252,255), RGB(120,180,220), RGB(190,220,240),
	  RGB(20,50,80), RGB(36,84,112), RGB(255,230,140), RGB(160,210,245),
	  RGB(255,255,250), RGB(40,110,160) },
	{ RGB(248,228,200), RGB(230,200,150), RGB(180,70,30), RGB(200,140,40), RGB(100,40,16),
	  RGB(196,64,28), RGB(210,150,40), RGB(180,120,70),
	  RGB(255,248,236), RGB(190,140,90), RGB(220,180,130),
	  RGB(60,32,16), RGB(104,68,40), RGB(255,210,160), RGB(220,150,90),
	  RGB(255,246,232), RGB(120,70,36) },
	{ RGB(236,244,250), RGB(186,210,228), RGB(70,140,190), RGB(210,230,242), RGB(48,86,120),
	  RGB(40,120,180), RGB(150,190,220), RGB(120,160,190),
	  RGB(246,250,254), RGB(120,160,190), RGB(198,216,230),
	  RGB(20,40,60), RGB(48,80,104), RGB(150,196,230), RGB(90,150,200),
	  RGB(255,255,255), RGB(56,96,128) },
	{ RGB(255,214,170), RGB(214,150,170), RGB(220,90,50), RGB(140,60,100), RGB(80,30,40),
	  RGB(230,100,40), RGB(160,60,100), RGB(200,120,80),
	  RGB(255,236,214), RGB(180,100,80), RGB(230,170,130),
	  RGB(36,16,26), RGB(96,64,58), RGB(255,180,120), RGB(214,130,110),
	  RGB(255,240,220), RGB(100,50,50) },
	{ RGB(255,228,210), RGB(255,210,230), RGB(255,160,120), RGB(255,220,160), RGB(180,90,80),
	  RGB(255,140,90), RGB(255,200,120), RGB(230,160,150),
	  RGB(255,248,242), RGB(220,160,140), RGB(240,200,180),
	  RGB(70,40,36), RGB(112,76,68), RGB(220,110,80), RGB(180,80,60),
	  RGB(255,250,246), RGB(160,90,80) },
	{ RGB(210,216,224), RGB(180,190,204), RGB(70,90,120), RGB(140,160,180), RGB(40,50,70),
	  RGB(60,100,150), RGB(120,150,180), RGB(130,145,165),
	  RGB(240,242,246), RGB(140,155,175), RGB(190,198,210),
	  RGB(28,36,48), RGB(68,78,94), RGB(64,96,140), RGB(48,72,110),
	  RGB(248,249,252), RGB(70,84,104) },
	{ RGB(200,230,228), RGB(150,200,206), RGB(20,120,140), RGB(230,250,248), RGB(10,70,80),
	  RGB(16,140,160), RGB(240,250,248), RGB(80,160,170),
	  RGB(244,252,252), RGB(100,170,175), RGB(180,220,218),
	  RGB(16,48,52), RGB(36,96,100), RGB(12,108,118), RGB(10,80,90),
	  RGB(250,255,255), RGB(30,100,110) },
	{ RGB(16,20,42), RGB(28,32,62), RGB(72,84,128), RGB(210,216,232), RGB(8,10,22),
	  RGB(196,206,230), RGB(140,150,186), RGB(70,80,120),
	  RGB(24,28,54), RGB(90,100,140), RGB(40,46,76),
	  RGB(236,240,250), RGB(148,156,180), RGB(48,58,110), RGB(36,44,88),
	  RGB(120,132,176), RGB(8,10,22) },
	{ RGB(52,34,16), RGB(78,50,22), RGB(196,132,36), RGB(232,184,72), RGB(40,22,8),
	  RGB(220,156,48), RGB(180,110,36), RGB(140,90,40),
	  RGB(62,40,18), RGB(168,118,48), RGB(88,56,24),
	  RGB(255,236,206), RGB(176,140,96), RGB(120,70,24), RGB(88,50,16),
	  RGB(232,186,90), RGB(28,14,6) },
	{ RGB(46,34,64), RGB(70,50,96), RGB(168,132,214), RGB(214,186,240), RGB(40,24,64),
	  RGB(186,148,228), RGB(140,110,186), RGB(120,90,160),
	  RGB(54,40,76), RGB(150,118,196), RGB(78,58,110),
	  RGB(246,236,255), RGB(168,148,196), RGB(96,64,140), RGB(72,48,112),
	  RGB(210,186,240), RGB(28,18,44) },
	{ RGB(244,228,196), RGB(220,196,150), RGB(20,118,112), RGB(196,72,48), RGB(80,48,24),
	  RGB(196,72,48), RGB(24,128,120), RGB(176,140,90),
	  RGB(250,240,214), RGB(160,110,70), RGB(220,196,150),
	  RGB(36,24,16), RGB(96,72,48), RGB(24,128,120), RGB(180,70,44),
	  RGB(255,248,228), RGB(96,56,32) },
	{ RGB(248,246,242), RGB(230,236,242), RGB(186,198,214), RGB(255,255,255), RGB(150,160,176),
	  RGB(120,140,168), RGB(210,198,214), RGB(190,198,208),
	  RGB(255,255,255), RGB(176,184,196), RGB(220,224,230),
	  RGB(48,52,64), RGB(96,104,116), RGB(236,240,246), RGB(214,220,230),
	  RGB(255,255,255), RGB(140,150,166) },
	{ RGB(236,240,244), RGB(214,222,230), RGB(36,64,112), RGB(186,196,210), RGB(24,40,72),
	  RGB(28,56,104), RGB(168,176,188), RGB(120,132,150),
	  RGB(248,250,252), RGB(140,152,170), RGB(200,208,216),
	  RGB(24,32,48), RGB(64,76,96), RGB(214,224,236), RGB(168,184,204),
	  RGB(255,255,255), RGB(40,56,88) },
	{ RGB(28,8,24), RGB(64,12,48), RGB(255,40,160), RGB(40,255,220), RGB(255,0,128),
	  RGB(255,60,180), RGB(80,255,230), RGB(255,120,200),
	  RGB(40,12,36), RGB(255,80,190), RGB(20,8,24),
	  RGB(255,230,250), RGB(200,140,190), RGB(255,0,140), RGB(0,220,200),
	  RGB(255,80,180), RGB(12,0,16) },
	{ RGB(8,16,8), RGB(16,32,12), RGB(180,255,40), RGB(255,40,80), RGB(40,80,16),
	  RGB(160,255,32), RGB(255,48,72), RGB(80,140,40),
	  RGB(18,24,14), RGB(140,200,40), RGB(10,16,8),
	  RGB(220,255,180), RGB(120,160,80), RGB(120,220,20), RGB(40,80,10),
	  RGB(180,255,60), RGB(4,8,2) },
	{ RGB(28,16,24), RGB(48,24,36), RGB(120,24,48), RGB(80,16,32), RGB(40,8,16),
	  RGB(168,36,64), RGB(90,70,80), RGB(100,48,64),
	  RGB(36,22,30), RGB(90,48,60), RGB(48,28,36),
	  RGB(236,220,224), RGB(160,128,136), RGB(72,28,40), RGB(48,16,28),
	  RGB(120,70,84), RGB(16,8,12) },
	{ RGB(22,22,26), RGB(36,36,42), RGB(70,74,82), RGB(160,164,172), RGB(16,16,20),
	  RGB(176,180,188), RGB(70,74,84), RGB(90,94,104),
	  RGB(30,30,36), RGB(100,104,114), RGB(42,42,50),
	  RGB(230,230,234), RGB(140,142,150), RGB(48,50,58), RGB(32,34,40),
	  RGB(150,154,164), RGB(10,10,14) },
	{ RGB(232,240,250), RGB(255,236,240), RGB(24,48,120), RGB(196,32,56), RGB(16,32,80),
	  RGB(180,28,52), RGB(36,56,128), RGB(160,180,210),
	  RGB(248,250,255), RGB(140,160,200), RGB(210,220,236),
	  RGB(20,28,56), RGB(48,64,104), RGB(255,228,232), RGB(200,214,236),
	  RGB(255,255,255), RGB(24,40,96) },
	{ RGB(20,20,24), RGB(36,36,40), RGB(196,160,48), RGB(48,48,52), RGB(12,12,16),
	  RGB(210,170,56), RGB(80,80,88), RGB(120,100,48),
	  RGB(32,32,36), RGB(140,116,48), RGB(48,48,52),
	  RGB(236,228,200), RGB(150,142,120), RGB(72,60,24), RGB(40,36,20),
	  RGB(220,184,72), RGB(8,8,10) },
	{ RGB(255,248,236), RGB(230,236,255), RGB(255,220,140), RGB(255,255,255), RGB(200,170,90),
	  RGB(212,168,64), RGB(255,255,255), RGB(220,200,140),
	  RGB(255,252,244), RGB(230,210,150), RGB(240,236,220),
	  RGB(72,56,24), RGB(120,100,64), RGB(255,244,210), RGB(230,220,180),
	  RGB(255,255,255), RGB(160,130,60) },
	{ RGB(168,160,148), RGB(120,112,100), RGB(90,110,70), RGB(70,78,64), RGB(60,56,48),
	  RGB(110,130,72), RGB(180,170,150), RGB(100,96,88),
	  RGB(186,178,164), RGB(90,84,74), RGB(150,142,130),
	  RGB(36,32,28), RGB(80,74,66), RGB(140,150,110), RGB(100,96,84),
	  RGB(210,204,190), RGB(48,44,38) },
	{ RGB(250,246,238), RGB(255,236,220), RGB(196,32,36), RGB(140,16,20), RGB(120,24,24),
	  RGB(200,36,40), RGB(40,40,40), RGB(180,150,120),
	  RGB(255,252,246), RGB(160,48,40), RGB(236,220,200),
	  RGB(40,24,20), RGB(96,64,56), RGB(255,220,210), RGB(200,80,70),
	  RGB(255,255,255), RGB(120,20,24) },
	{ RGB(10,14,40), RGB(22,28,64), RGB(255,220,120), RGB(180,200,255), RGB(40,48,90),
	  RGB(255,230,140), RGB(160,190,255), RGB(60,70,120),
	  RGB(16,20,48), RGB(120,140,200), RGB(28,32,70),
	  RGB(236,240,255), RGB(140,150,190), RGB(40,48,100), RGB(24,28,64),
	  RGB(200,210,255), RGB(6,8,20) },
	{ RGB(4,24,36), RGB(8,48,64), RGB(40,220,180), RGB(20,80,120), RGB(8,40,56),
	  RGB(64,230,190), RGB(180,80,220), RGB(20,70,90),
	  RGB(8,32,44), RGB(30,120,130), RGB(12,40,52),
	  RGB(210,245,240), RGB(100,150,150), RGB(20,80,90), RGB(8,40,56),
	  RGB(80,220,200), RGB(2,10,16) },
	{ RGB(220,236,210), RGB(186,214,170), RGB(230,80,120), RGB(240,190,60), RGB(80,120,60),
	  RGB(220,70,110), RGB(80,150,60), RGB(140,170,110),
	  RGB(236,246,228), RGB(120,150,90), RGB(200,220,180),
	  RGB(32,48,28), RGB(70,90,60), RGB(255,220,230), RGB(200,230,180),
	  RGB(255,255,255), RGB(60,90,40) },
	{ RGB(64,16,28), RGB(96,24,40), RGB(200,40,70), RGB(255,180,190), RGB(80,20,36),
	  RGB(210,48,78), RGB(120,180,70), RGB(140,60,80),
	  RGB(48,18,28), RGB(160,50,70), RGB(80,28,40),
	  RGB(255,230,234), RGB(180,120,130), RGB(120,24,40), RGB(72,16,28),
	  RGB(255,200,210), RGB(24,8,12) },
	{ RGB(214,232,248), RGB(180,210,236), RGB(255,140,40), RGB(80,80,200), RGB(40,80,140),
	  RGB(255,130,36), RGB(90,70,200), RGB(140,170,200),
	  RGB(236,244,252), RGB(160,180,210), RGB(190,210,230),
	  RGB(24,36,64), RGB(70,90,120), RGB(255,220,180), RGB(200,220,245),
	  RGB(255,255,255), RGB(40,70,130) },
};

static const int kCorner[OXZ_THEME_COUNT] = {
	10, 3, 0, 2, 0, 12, 16, 6, 0, 0,
	0, 8, 4, 5, 2, 0, 6, 8, 4, 5,
	8, 14, 6, 2, 0, 0, 4, 2, 8, 3,
	8, 0, 2, 0, 4, 10, 8, 6
};

static const wchar_t* kTheme[OXZ_THEME_COUNT][14] = {
	{ L"可愛い", L"Cute", L"Mignon", L"Carino", L"Tierno", L"귀여움", L"可爱", L"لطيف", L"Милый", L"Niedlich", L"Fofo", L"Schattig", L"Słodki", L"Sevimli" },
	{ L"格好いい", L"Stylish", L"Chic", L"Elegante", L"Elegante", L"멋짐", L"帅气", L"أنيق", L"Стильный", L"Schick", L"Elegante", L"Stoer", L"Szykowny", L"Şık" },
	{ L"中二", L"Dramatic", L"Dramatique", L"Drammatico", L"Dramatico", L"중2", L"中二", L"درامي", L"Драма", L"Dramatisch", L"Dramatico", L"Dramatisch", L"Dramat", L"Dramatik" },
	{ L"夜光", L"Neon", L"Neon", L"Neon", L"Neon", L"네온", L"夜光", L"نيون", L"Неон", L"Neon", L"Neon", L"Neon", L"Neon", L"Neon" },
	{ L"墨", L"Ink", L"Encre", L"Inchiostro", L"Tinta", L"먹", L"墨", L"حبر", L"Тушь", L"Tusche", L"Tinta", L"Inkt", L"Tusz", L"Mürekkep" },
	{ L"森", L"Forest", L"Foret", L"Foresta", L"Bosque", L"숲", L"森", L"غابة", L"Лес", L"Wald", L"Floresta", L"Bos", L"Las", L"Orman" },
	{ L"キャンディ", L"Candy", L"Bonbon", L"Caramella", L"Dulce", L"캔디", L"糖果", L"حلوى", L"Конфета", L"Bonbon", L"Doce", L"Snoep", L"Cukierek", L"Şeker" },
	{ L"鋼", L"Steel", L"Acier", L"Acciaio", L"Acero", L"강철", L"钢", L"فولاذ", L"Сталь", L"Stahl", L"Aco", L"Staal", L"Stal", L"Celik" },
	{ L"超可愛い", L"Super Cute", L"Super mignon", L"Super carino", L"Super tierno", L"초귀여움", L"超可爱", L"فائق اللطف", L"Супермилый", L"Superniedlich", L"Superfofo", L"Superschattig", L"Supersłodki", L"Süper sevimli" },
	{ L"超格好いい", L"Super Stylish", L"Super chic", L"Super elegante", L"Super elegante", L"초멋짐", L"超帅", L"فائق الأناقة", L"Суперстиль", L"Superschick", L"Superelegante", L"Superstoer", L"Superszyk", L"Süper şık" },
	{ L"春", L"Spring", L"Printemps", L"Primavera", L"Primavera", L"봄", L"春", L"ربيع", L"Весна", L"Frühling", L"Primavera", L"Lente", L"Wiosna", L"İlkbahar" },
	{ L"夏", L"Summer", L"Ete", L"Estate", L"Verano", L"여름", L"夏", L"صيف", L"Лето", L"Sommer", L"Verao", L"Zomer", L"Lato", L"Yaz" },
	{ L"秋", L"Autumn", L"Automne", L"Autunno", L"Otono", L"가을", L"秋", L"خريف", L"Осень", L"Herbst", L"Outono", L"Herfst", L"Jesien", L"Sonbahar" },
	{ L"冬", L"Winter", L"Hiver", L"Inverno", L"Invierno", L"겨울", L"冬", L"شتاء", L"Зима", L"Winter", L"Inverno", L"Winter", L"Zima", L"Kış" },
	{ L"夕暮れ", L"Dusk", L"Crepuscule", L"Crepuscolo", L"Atardecer", L"해질녘", L"黄昏", L"غسق", L"Закат", L"Dämmerung", L"Crepusculo", L"Schemer", L"Zmierzch", L"Alacakaranlık" },
	{ L"朝", L"Dawn", L"Aube", L"Alba", L"Amanecer", L"새벽", L"黎明", L"فجر", L"Рассвет", L"Morgendämmerung", L"Amanhecer", L"Dageraad", L"Świt", L"Şafak" },
	{ L"雨", L"Rain", L"Pluie", L"Pioggia", L"Lluvia", L"비", L"雨", L"مطر", L"Дождь", L"Regen", L"Chuva", L"Regen", L"Deszcz", L"Yağmur" },
	{ L"海", L"Sea", L"Mer", L"Mare", L"Mar", L"바다", L"海", L"بحر", L"Море", L"Meer", L"Mar", L"Zee", L"Morze", L"Deniz" },
	{ L"月", L"Moon", L"Lune", L"Luna", L"Luna", L"달", L"月", L"قمر", L"Луна", L"Mond", L"Lua", L"Maan", L"Księżyc", L"Ay" },
	{ L"琥珀", L"Amber", L"Ambre", L"Ambra", L"Ambar", L"호박", L"琥珀", L"كهرمان", L"Янтарь", L"Bernstein", L"Ambar", L"Barnsteen", L"Bursztyn", L"Kehribar" },
	{ L"藤", L"Wisteria", L"Glycine", L"Glicine", L"Glicinia", L"등나무", L"紫藤", L"وستيريا", L"Глициния", L"Glyzinie", L"Glicinia", L"Blauweregen", L"Glicynia", L"Mor salkım" },
	{ L"レトロ", L"Retro", L"Retro", L"Retro", L"Retro", L"레트로", L"复古", L"ريترو", L"Ретро", L"Retro", L"Retrô", L"Retro", L"Retro", L"Retro" },
	{ L"清楚・女", L"Neat (F)", L"Epuré (F)", L"Pulito (F)", L"Puro (F)", L"청초·여", L"清纯·女", L"نقي (أ)", L"Чистый (ж)", L"Schlicht (w)", L"Puro (F)", L"Strak (v)", L"Czysty (k)", L"Sade (k)" },
	{ L"清楚・男", L"Neat (M)", L"Epuré (H)", L"Pulito (M)", L"Puro (M)", L"청초·남", L"清爽·男", L"نقي (ذ)", L"Чистый (м)", L"Schlicht (m)", L"Puro (M)", L"Strak (m)", L"Czysty (m)", L"Sade (e)" },
	{ L"ぶっ壊れ・女", L"Broken (F)", L"Cassé (F)", L"Rotto (F)", L"Roto (F)", L"고장·여", L"崩坏·女", L"مكسور (أ)", L"Сломан (ж)", L"Kaputt (w)", L"Quebrado (F)", L"Kapot (v)", L"Zepsuty (k)", L"Bozuk (k)" },
	{ L"ぶっ壊れ・男", L"Broken (M)", L"Cassé (H)", L"Rotto (M)", L"Roto (M)", L"고장·남", L"崩坏·男", L"مكسور (ذ)", L"Сломан (м)", L"Kaputt (m)", L"Quebrado (M)", L"Kapot (m)", L"Zepsuty (m)", L"Bozuk (e)" },
	{ L"ゴシック・女", L"Gothic (F)", L"Gothique (F)", L"Gotico (F)", L"Gotico (F)", L"고딕·여", L"哥特·女", L"قوطي (أ)", L"Готика (ж)", L"Gotisch (w)", L"Gotico (F)", L"Gothic (v)", L"Gotyk (k)", L"Gotik (k)" },
	{ L"ゴシック・男", L"Gothic (M)", L"Gothique (H)", L"Gotico (M)", L"Gotico (M)", L"고딕·남", L"哥特·男", L"قوطي (ذ)", L"Готика (м)", L"Gotisch (m)", L"Gotico (M)", L"Gothic (m)", L"Gotyk (m)", L"Gotik (e)" },
	{ L"学園・女", L"School (F)", L"Ecole (F)", L"Scuola (F)", L"Escuela (F)", L"학원·여", L"学园·女", L"مدرسة (أ)", L"Школа (ж)", L"Schule (w)", L"Escola (F)", L"School (v)", L"Szkoła (k)", L"Okul (k)" },
	{ L"学園・男", L"School (M)", L"Ecole (H)", L"Scuola (M)", L"Escuela (M)", L"학원·남", L"学园·男", L"مدرسة (ذ)", L"Школа (м)", L"Schule (m)", L"Escola (M)", L"School (m)", L"Szkoła (m)", L"Okul (e)" },
	{ L"天界", L"Heaven", L"Ciel", L"Cielo", L"Cielo", L"천계", L"天界", L"سماء", L"Небеса", L"Himmel", L"Céu", L"Hemel", L"Niebo", L"Gök" },
	{ L"廃墟", L"Ruins", L"Ruines", L"Rovine", L"Ruinas", L"폐허", L"废墟", L"أطلال", L"Руины", L"Ruinen", L"Ruínas", L"Ruïnes", L"Ruiny", L"Harabe" },
	{ L"神社", L"Shrine", L"Sanctuaire", L"Santuario", L"Santuario", L"신사", L"神社", L"معبد", L"Храм", L"Schrein", L"Santuário", L"Heiligdom", L"Świątynia", L"Tapınak" },
	{ L"星空", L"Starry", L"Etoilé", L"Stellato", L"Estrellado", L"별하늘", L"星空", L"نجوم", L"Звёзды", L"Sternhimmel", L"Estrelado", L"Sterren", L"Gwiazdy", L"Yıldız" },
	{ L"深海", L"Abyss", L"Abysse", L"Abisso", L"Abismo", L"심해", L"深海", L"عمق", L"Бездна", L"Tiefe", L"Abismo", L"Diepte", L"Głębia", L"Derin" },
	{ L"花園", L"Garden", L"Jardin", L"Giardino", L"Jardín", L"화원", L"花园", L"حديقة", L"Сад", L"Garten", L"Jardim", L"Tuin", L"Ogród", L"Bahçe" },
	{ L"薔薇", L"Rose", L"Rose", L"Rosa", L"Rosa", L"장미", L"玫瑰", L"ورد", L"Роза", L"Rose", L"Rosa", L"Roos", L"Róża", L"Gül" },
	{ L"蝶", L"Butterfly", L"Papillon", L"Farfalla", L"Mariposa", L"나비", L"蝴蝶", L"فراشة", L"Бабочка", L"Falter", L"Borboleta", L"Vlinder", L"Motyl", L"Kelebek" },
};

static int gTheme = OXZ_THEME_GARDEN;
static int gPreview = -1;
static void (*gRefresh)() = nullptr;
static int gDwmAcrylic = -1;
static int gPt = 11;
static int gBold = 0;
static int gItalic = 0;
static int gAcrylic = 0;
static int gSoft = 0;
static int gAnim = OXZ_ANIM_AURORA;
static wchar_t gFace[LF_FACESIZE] = L"Segoe UI";
static std::vector<std::wstring> gFaces;

static void PutDword(const wchar_t* name, DWORD v)
{
	RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", name, REG_DWORD, &v, sizeof(v));
}

static DWORD GetDword(const wchar_t* name, DWORD def)
{
	DWORD v = def, cb = sizeof(v);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", name, RRF_RT_REG_DWORD, NULL, &v, &cb) != ERROR_SUCCESS)
		return def;
	return v;
}

static int CALLBACK FaceCb(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM lp)
{
	std::vector<std::wstring>* v = (std::vector<std::wstring>*)lp;
	if (!lf || !lf->lfFaceName[0] || lf->lfFaceName[0] == L'@') return 1;
	for (size_t i = 0; i < v->size(); ++i)
		if (_wcsicmp((*v)[i].c_str(), lf->lfFaceName) == 0) return 1;
	if ((int)v->size() >= OXZ_FACE_MAX) return 0;
	v->push_back(lf->lfFaceName);
	return 1;
}

static void EnsureFaces()
{
	if (!gFaces.empty()) return;
	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfCharSet = DEFAULT_CHARSET;
	HDC dc = GetDC(NULL);
	EnumFontFamiliesExW(dc, &lf, FaceCb, (LPARAM)&gFaces, 0);
	ReleaseDC(NULL, dc);
	std::sort(gFaces.begin(), gFaces.end(), [](const std::wstring& a, const std::wstring& b) {
		return _wcsicmp(a.c_str(), b.c_str()) < 0;
	});
	int have = 0;
	for (size_t i = 0; i < gFaces.size(); ++i)
		if (_wcsicmp(gFaces[i].c_str(), gFace) == 0) have = 1;
	if (!have && gFace[0]) gFaces.insert(gFaces.begin(), gFace);
	if ((int)gFaces.size() > OXZ_FACE_MAX) gFaces.resize(OXZ_FACE_MAX);
}

void OxzLookLoad()
{
	DWORD t = GetDword(L"theme", OXZ_THEME_GARDEN);
	DWORD pt = GetDword(L"fontPt", 11);
	if (t >= OXZ_THEME_COUNT) t = OXZ_THEME_GARDEN;
	if (pt < 8 || pt > 24) pt = 11;
	gTheme = (int)t;
	gPt = (int)pt;
	gBold = GetDword(L"fontBold", 0) ? 1 : 0;
	gItalic = GetDword(L"fontItalic", 0) ? 1 : 0;
	gAcrylic = GetDword(L"acrylic", 0) ? 1 : 0;
	gSoft = GetDword(L"soft", 0) ? 1 : 0;
	{
		DWORD a = GetDword(L"anim", OXZ_ANIM_AURORA);
		if (a >= OXZ_ANIM_COUNT) a = OXZ_ANIM_AURORA;
		gAnim = (int)a;
	}
	wchar_t face[LF_FACESIZE];
	DWORD cb = sizeof(face);
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"fontFace", RRF_RT_REG_SZ, NULL, face, &cb) == ERROR_SUCCESS && face[0])
		wcsncpy_s(gFace, face, _TRUNCATE);
}

const OxzPal& OxzLook()
{
	int id = gPreview;
	if (id < 0 || id >= OXZ_THEME_COUNT) id = gTheme;
	if (id < 0 || id >= OXZ_THEME_COUNT) id = OXZ_THEME_GARDEN;
	return kPal[id];
}

void OxzSetLookRefresh(void (*fn)())
{
	gRefresh = fn;
}

void OxzHoverTheme(int id)
{
	int next = (id >= 0 && id < OXZ_THEME_COUNT) ? id : -1;
	if (next == gPreview) return;
	gPreview = next;
	if (gRefresh) gRefresh();
}

void OxzFinishThemePreview(int commit)
{
	int had = gPreview >= 0;
	gPreview = -1;
	if (had && !commit && gRefresh) gRefresh();
}

int OxzTheme() { return gTheme; }
int OxzThemeCount() { return OXZ_THEME_COUNT; }
const wchar_t* OxzThemeName(int id)
{
	if (id < 0 || id >= OXZ_THEME_COUNT) id = OXZ_THEME_GARDEN;
	int lang = OxzLang();
	if (lang < 0 || lang > 13) lang = 1;
	const wchar_t* s = kTheme[id][lang];
	if (!s || !s[0]) s = kTheme[id][1];
	return s ? s : L"";
}
int OxzFontPt() { return gPt; }
int OxzFontBold() { return gBold; }
int OxzFontItalic() { return gItalic; }
const wchar_t* OxzFontFace() { return gFace; }
int OxzAcrylic() { return gAcrylic; }
int OxzSoft() { return gSoft; }
int OxzCorner()
{
	int id = gTheme;
	if (id < 0 || id >= OXZ_THEME_COUNT) id = OXZ_THEME_GARDEN;
	return kCorner[id];
}

COLORREF OxzInk(COLORREF bg)
{
	int y = (GetRValue(bg) * 30 + GetGValue(bg) * 59 + GetBValue(bg) * 11) / 100;
	if (y >= 148) return RGB(32, 26, 36);
	return RGB(248, 244, 236);
}

static UINT DpiOf(HWND hwnd)
{
	typedef UINT(WINAPI* Fn)(HWND);
	Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	UINT dpi = fn && hwnd ? fn(hwnd) : 96;
	return dpi ? dpi : 96;
}

HFONT OxzCreateUiFont(HWND hwnd)
{
	int h = -MulDiv(gPt, (int)DpiOf(hwnd), 72);
	return CreateFontW(h, 0, 0, 0, gBold ? FW_BOLD : FW_NORMAL, gItalic ? TRUE : FALSE, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, gFace);
}

static DWORD WinBuild()
{
	typedef LONG(WINAPI* Fn)(OSVERSIONINFOW*);
	static DWORD build = 0;
	if (build) return build;
	OSVERSIONINFOW os;
	memset(&os, 0, sizeof(os));
	os.dwOSVersionInfoSize = sizeof(os);
	Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
	if (fn && fn(&os) == 0) build = os.dwBuildNumber;
	if (!build) build = 1;
	return build;
}

static int Win11()
{
	return WinBuild() >= 22000;
}

int OxzPerPixelAcrylic()
{
	return gAcrylic && WinBuild() >= 26100;
}

struct Plate {
	HDC mem;
	HBITMAP dib;
	HBITMAP old;
	void* bits;
	int w, h;
};

HDC OxzPlateBegin(void** hold, HDC ref, int w, int h, COLORREF fill)
{
	if (hold) *hold = NULL;
	if (!ref || w <= 0 || h <= 0) return ref;
	Plate* p = new Plate();
	memset(p, 0, sizeof(*p));
	p->w = w;
	p->h = h;
	p->mem = CreateCompatibleDC(ref);
	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	p->dib = CreateDIBSection(p->mem, &bi, DIB_RGB_COLORS, &p->bits, NULL, 0);
	if (!p->mem || !p->dib || !p->bits) {
		if (p->dib) DeleteObject(p->dib);
		if (p->mem) DeleteDC(p->mem);
		delete p;
		return ref;
	}
	p->old = (HBITMAP)SelectObject(p->mem, p->dib);
	RECT rc = { 0, 0, w, h };
	HBRUSH br = CreateSolidBrush(fill);
	FillRect(p->mem, &rc, br);
	DeleteObject(br);
	if (hold) *hold = p;
	return p->mem;
}

void OxzPlateEnd(void* hold, HDC dst, int x, int y, int chroma)
{
	Plate* p = (Plate*)hold;
	if (!p) return;
	UINT32* px = (UINT32*)p->bits;
	int n = p->w * p->h;
	if (chroma) {
		for (int i = 0; i < n; ++i) {
			if ((px[i] & 0x00FFFFFFu) == 0x00010101u) px[i] = 0;
			else px[i] |= 0xFF000000u;
		}
	} else {
		for (int i = 0; i < n; ++i) px[i] |= 0xFF000000u;
	}
	int wrote = 0;
	if (dst && chroma) {
		static int bp = 0;
		if (!bp) { BufferedPaintInit(); bp = 1; }
		RECT rc = { x, y, x + p->w, y + p->h };
		BP_PAINTPARAMS params;
		memset(&params, 0, sizeof(params));
		params.cbSize = sizeof(params);
		HDC buf = NULL;
		HPAINTBUFFER pb = BeginBufferedPaint(dst, &rc, BPBF_TOPDOWNDIB, &params, &buf);
		if (pb) {
			RGBQUAD* bits = NULL;
			int stride = 0;
			if (SUCCEEDED(GetBufferedPaintBits(pb, &bits, &stride)) && bits && stride >= p->w) {
				const UINT32* src = (const UINT32*)p->bits;
				for (int row = 0; row < p->h; ++row) {
					UINT32* drow = (UINT32*)((BYTE*)bits + (size_t)row * (size_t)stride * 4);
					memcpy(drow, src + (size_t)row * (size_t)p->w, (size_t)p->w * 4);
				}
				wrote = 1;
			}
			EndBufferedPaint(pb, wrote ? TRUE : FALSE);
		}
	}
	BLENDFUNCTION bf;
	bf.BlendOp = AC_SRC_OVER;
	bf.BlendFlags = 0;
	bf.SourceConstantAlpha = 255;
	bf.AlphaFormat = AC_SRC_ALPHA;
	if (dst && !wrote) {
		if (!AlphaBlend(dst, x, y, p->w, p->h, p->mem, 0, 0, p->w, p->h, bf)) {
			bf.AlphaFormat = 0;
			if (!AlphaBlend(dst, x, y, p->w, p->h, p->mem, 0, 0, p->w, p->h, bf))
				BitBlt(dst, x, y, p->w, p->h, p->mem, 0, 0, SRCCOPY);
		}
	}
	SelectObject(p->mem, p->old);
	DeleteObject(p->dib);
	DeleteDC(p->mem);
	delete p;
}

static const wchar_t* kBarInk = L"OxzBarInk";
static const wchar_t* kOldProc = L"OxzOldProc";
static const wchar_t* kInPrint = L"OxzInPrint";
static const wchar_t* kPostOp = L"OxzPostOp";
static const UINT OX_POST = WM_APP + 44;

static int ClassIs(HWND hwnd, const wchar_t* name)
{
	wchar_t cls[64];
	if (!GetClassNameW(hwnd, cls, 64)) return 0;
	return _wcsicmp(cls, name) == 0;
}

static COLORREF LeafFill(HWND hwnd)
{
	if (ClassIs(hwnd, L"Static")) return OxzLook().bg0;
	if (ClassIs(hwnd, L"Button")) return OxzLook().bg1;
	if (ClassIs(hwnd, L"SysHeader32")) return OxzLook().ribbon0;
	return OxzLook().face;
}

static int NeedsNc(HWND hwnd)
{
	LONG st = GetWindowLongW(hwnd, GWL_STYLE);
	if (st & (WS_VSCROLL | WS_HSCROLL)) return 1;
	return ClassIs(hwnd, L"SysListView32") || ClassIs(hwnd, L"SysHeader32")
		|| ClassIs(hwnd, L"Edit") || ClassIs(hwnd, L"ListBox");
}

void OxzSetBarInk(HWND hwnd, COLORREF ink)
{
	if (hwnd) SetPropW(hwnd, kBarInk, (HANDLE)(UINT_PTR)ink);
}

static void PaintProgress(HWND hwnd, HDC mem, int w, int h)
{
	int lo = (int)SendMessageW(hwnd, PBM_GETRANGE, TRUE, 0);
	int hi = (int)SendMessageW(hwnd, PBM_GETRANGE, FALSE, 0);
	int pos = (int)SendMessageW(hwnd, PBM_GETPOS, 0, 0);
	COLORREF ink = (COLORREF)(UINT_PTR)GetPropW(hwnd, kBarInk);
	RECT edge = { 0, 0, w, h };
	RECT bar;
	int inner, fillW;
	if (!ink) ink = OxzLook().accent;
	if (hi <= lo) { lo = 0; hi = 1000; }
	if (pos < lo) pos = lo;
	if (pos > hi) pos = hi;
	DrawEdge(mem, &edge, EDGE_SUNKEN, BF_RECT);
	inner = w > 4 ? w - 4 : w;
	fillW = (int)(((unsigned long long)(pos - lo) * (unsigned)inner) / (unsigned)(hi - lo));
	if (fillW > inner) fillW = inner;
	if (fillW > 0 && h > 4) {
		HBRUSH br = CreateSolidBrush(ink);
		bar.left = 2;
		bar.top = 2;
		bar.right = 2 + fillW;
		bar.bottom = h - 2;
		FillRect(mem, &bar, br);
		DeleteObject(br);
	}
}

static void PaintGlassClient(HWND hwnd, HDC dc, COLORREF fill)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w < 1 || h < 1 || !dc) return;
	if (ClassIs(hwnd, L"SysListView32")) {
		DWORD ex = (DWORD)ListView_GetExtendedListViewStyle(hwnd);
		if (ex & LVS_EX_DOUBLEBUFFER)
			ListView_SetExtendedListViewStyleEx(hwnd, LVS_EX_DOUBLEBUFFER, 0);
	}
	void* plate = NULL;
	HDC mem = OxzPlateBegin(&plate, dc, w, h, fill);
	if (plate && mem) {
		if (ClassIs(hwnd, L"msctls_progress32")) PaintProgress(hwnd, mem, w, h);
		else {
			SetPropW(hwnd, kInPrint, (HANDLE)1);
			SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT | PRF_ERASEBKGND);
			RemovePropW(hwnd, kInPrint);
		}
		int saved = 0;
		HWND hdr = NULL;
		HWND ed = NULL;
		if (ClassIs(hwnd, L"SysListView32")) {
			hdr = (HWND)SendMessageW(hwnd, LVM_GETHEADER, 0, 0);
			ed = (HWND)SendMessageW(hwnd, LVM_GETEDITCONTROL, 0, 0);
			if (!ed) ed = FindWindowExW(hwnd, NULL, L"Edit", NULL);
		}
		if (hdr || ed) saved = SaveDC(dc);
		if (hdr) {
			RECT hr;
			GetWindowRect(hdr, &hr);
			MapWindowPoints(NULL, hwnd, (POINT*)&hr, 2);
			ExcludeClipRect(dc, hr.left, hr.top, hr.right, hr.bottom);
		}
		if (ed && IsWindowVisible(ed)) {
			RECT er;
			GetWindowRect(ed, &er);
			MapWindowPoints(NULL, hwnd, (POINT*)&er, 2);
			ExcludeClipRect(dc, er.left, er.top, er.right, er.bottom);
		}
		OxzPlateEnd(plate, dc, 0, 0, 0);
		if (saved) RestoreDC(dc, saved);
		return;
	}
	static int bp = 0;
	if (!bp) { BufferedPaintInit(); bp = 1; }
	BP_PAINTPARAMS params;
	memset(&params, 0, sizeof(params));
	params.cbSize = sizeof(params);
	HDC buf = NULL;
	HPAINTBUFFER pb = BeginBufferedPaint(dc, &rc, BPBF_TOPDOWNDIB, &params, &buf);
	if (pb && buf) {
		HBRUSH br = CreateSolidBrush(fill);
		FillRect(buf, &rc, br);
		DeleteObject(br);
		if (ClassIs(hwnd, L"msctls_progress32")) PaintProgress(hwnd, buf, w, h);
		else {
			SetPropW(hwnd, kInPrint, (HANDLE)1);
			SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)buf, PRF_CLIENT | PRF_ERASEBKGND);
			RemovePropW(hwnd, kInPrint);
		}
		BufferedPaintMakeOpaque(pb, NULL);
		EndBufferedPaint(pb, TRUE);
	}
}

static void PresentLeaf(HWND hwnd)
{
	HDC dc = GetDC(hwnd);
	if (!dc) return;
	PaintGlassClient(hwnd, dc, LeafFill(hwnd));
	ReleaseDC(hwnd, dc);
}

static void HeaderArrow(HDC mem, const RECT& ir, int up, COLORREF ink)
{
	int cx = ir.right - 12;
	int cy = (ir.top + ir.bottom) / 2;
	POINT p[3];
	if (up) {
		p[0].x = cx; p[0].y = cy - 3;
		p[1].x = cx - 4; p[1].y = cy + 2;
		p[2].x = cx + 4; p[2].y = cy + 2;
	} else {
		p[0].x = cx; p[0].y = cy + 3;
		p[1].x = cx - 4; p[1].y = cy - 2;
		p[2].x = cx + 4; p[2].y = cy - 2;
	}
	HBRUSH br = CreateSolidBrush(ink);
	HPEN pen = CreatePen(PS_SOLID, 1, ink);
	HGDIOBJ ob = SelectObject(mem, br);
	HGDIOBJ op = SelectObject(mem, pen);
	Polygon(mem, p, 3);
	SelectObject(mem, ob);
	SelectObject(mem, op);
	DeleteObject(br);
	DeleteObject(pen);
}

static void HeaderLabel(HWND head, int i, wchar_t* text, int n, UINT* fmt)
{
	text[0] = 0;
	if (fmt) *fmt = 0;
	HWND list = GetParent(head);
	wchar_t cls[32];
	cls[0] = 0;
	if (list) GetClassNameW(list, cls, 32);
	if (list && _wcsicmp(cls, L"SysListView32") == 0) {
		LVCOLUMNW col;
		memset(&col, 0, sizeof(col));
		col.mask = LVCF_TEXT;
		col.pszText = text;
		col.cchTextMax = n;
		SendMessageW(list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
	}
	HDITEMW item;
	memset(&item, 0, sizeof(item));
	item.mask = HDI_FORMAT;
	if (!text[0]) {
		item.mask |= HDI_TEXT;
		item.pszText = text;
		item.cchTextMax = n;
	}
	if (SendMessageW(head, HDM_GETITEMW, (WPARAM)i, (LPARAM)&item) && fmt)
		*fmt |= (item.fmt & (HDF_SORTUP | HDF_SORTDOWN));
}

/* 32bpp DIB へ DrawText すると字形が残らない。24bpp に描いてから α=255 で載せる。
   地はボタン面。ウィンドウ色やリボン色でカラム行を潰さない。 */
static void PaintGlassHeader(HWND hwnd, HDC dc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w < 1 || h < 1 || !dc) return;
	BITMAPINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 24;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits24 = NULL;
	HDC mem = CreateCompatibleDC(dc);
	HBITMAP dib = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits24, NULL, 0) : NULL;
	if (!mem || !dib || !bits24) {
		if (dib) DeleteObject(dib);
		if (mem) DeleteDC(mem);
		return;
	}
	HGDIOBJ oldbm = SelectObject(mem, dib);
	RECT all = { 0, 0, w, h };
	FillRect(mem, &all, GetSysColorBrush(COLOR_BTNFACE));
	HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
	if (!font) font = (HFONT)SendMessageW(GetParent(hwnd), WM_GETFONT, 0, 0);
	HGDIOBJ oldf = font ? SelectObject(mem, font) : NULL;
	SetBkMode(mem, TRANSPARENT);
	SetTextColor(mem, GetSysColor(COLOR_BTNTEXT));
	int n = (int)SendMessageW(hwnd, HDM_GETITEMCOUNT, 0, 0);
	for (int i = 0; i < n; ++i) {
		RECT ir;
		if (!SendMessageW(hwnd, HDM_GETITEMRECT, (WPARAM)i, (LPARAM)&ir)) continue;
		if (ir.right <= ir.left || ir.bottom <= ir.top) continue;
		DrawEdge(mem, &ir, EDGE_RAISED, BF_RECT | BF_SOFT);
		wchar_t text[256];
		UINT fmt = 0;
		HeaderLabel(hwnd, i, text, 256, &fmt);
		RECT tr = ir;
		tr.left += 8;
		tr.right -= 8;
		tr.top += 1;
		tr.bottom -= 1;
		if (fmt & (HDF_SORTUP | HDF_SORTDOWN)) tr.right -= 16;
		if (text[0] && tr.right > tr.left)
			DrawTextW(mem, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
		if (fmt & HDF_SORTUP) HeaderArrow(mem, ir, 1, GetSysColor(COLOR_BTNTEXT));
		else if (fmt & HDF_SORTDOWN) HeaderArrow(mem, ir, 0, GetSysColor(COLOR_BTNTEXT));
	}
	if (oldf) SelectObject(mem, oldf);
	void* plate = NULL;
	HDC pdc = OxzPlateBegin(&plate, dc, w, h, GetSysColor(COLOR_BTNFACE));
	Plate* p = (Plate*)plate;
	if (p && p->bits && pdc) {
		int stride = (w * 3 + 3) & ~3;
		UINT32* dst = (UINT32*)p->bits;
		const BYTE* src = (const BYTE*)bits24;
		for (int y = 0; y < h; ++y) {
			const BYTE* row = src + (size_t)y * (size_t)stride;
			UINT32* out = dst + (size_t)y * (size_t)w;
			for (int x = 0; x < w; ++x) {
				const BYTE* s = row + x * 3;
				out[x] = 0xFF000000u | ((UINT32)s[2] << 16) | ((UINT32)s[1] << 8) | (UINT32)s[0];
			}
		}
		OxzPlateEnd(plate, dc, 0, 0, 0);
	} else if (plate) {
		OxzPlateEnd(plate, NULL, 0, 0, 0);
	}
	SelectObject(mem, oldbm);
	DeleteObject(dib);
	DeleteDC(mem);
}

static void PresentHeader(HWND hwnd)
{
	HDC dc = GetDC(hwnd);
	if (!dc) return;
	PaintGlassHeader(hwnd, dc);
	ReleaseDC(hwnd, dc);
}

/* リストの全面描画は子のヘッダーやインライン編集欄に被さる。リストのあとで描き戻す。 */
static void MakeWindowOpaque(HWND hwnd);
static void RestoreListHeader(HWND list)
{
	if (!list) return;
	wchar_t cls[32];
	if (!GetClassNameW(list, cls, 32) || _wcsicmp(cls, L"SysListView32") != 0) return;
	HWND hdr = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
	if (hdr) PresentHeader(hdr);
	HWND ed = (HWND)SendMessageW(list, LVM_GETEDITCONTROL, 0, 0);
	if (!ed) ed = FindWindowExW(list, NULL, L"Edit", NULL);
	if (ed && IsWindowVisible(ed)) {
		PresentLeaf(ed);
		MakeWindowOpaque(ed);
	}
}

static void PaintGlassLabel(HWND hwnd, HDC dc)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w < 1 || h < 1 || !dc) return;
	void* plate = NULL;
	HDC mem = OxzPlateBegin(&plate, dc, w, h, RGB(1, 1, 1));
	if (!plate || !mem) return;
	wchar_t text[1024];
	text[0] = 0;
	GetWindowTextW(hwnd, text, 1024);
	if (text[0]) {
		HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
		HGDIOBJ old = font ? SelectObject(mem, font) : NULL;
		SetBkMode(mem, TRANSPARENT);
		SetTextColor(mem, OxzLook().text);
		DWORD st = (DWORD)GetWindowLongW(hwnd, GWL_STYLE);
		UINT fmt = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
		if (st & SS_RIGHT) fmt |= DT_RIGHT;
		else if (st & SS_CENTER) fmt |= DT_CENTER;
		else fmt |= DT_LEFT;
		RECT tr = { 0, 0, w, h };
		DrawTextW(mem, text, -1, &tr, fmt);
		if (old) SelectObject(mem, old);
	}
	OxzPlateEnd(plate, dc, 0, 0, 1);
}

/* 枠とスクロールバーは Def が α=0 で描いた画素を取り込み、全面 α=255 で戻す。
   クライアントは同じバッファへ描き直す。空の WM_PRINT で上書きしない。 */
static void MakeWindowOpaque(HWND hwnd)
{
	RECT wr;
	GetWindowRect(hwnd, &wr);
	int w = wr.right - wr.left;
	int h = wr.bottom - wr.top;
	if (w < 1 || h < 1 || w > 7680 || h > 4320) return;
	POINT org = { 0, 0 };
	ClientToScreen(hwnd, &org);
	int cl = org.x - wr.left;
	int ct = org.y - wr.top;
	RECT cr;
	GetClientRect(hwnd, &cr);
	int cw = cr.right - cr.left;
	int ch = cr.bottom - cr.top;
	static int bp = 0;
	if (!bp) { BufferedPaintInit(); bp = 1; }
	HDC wdc = GetWindowDC(hwnd);
	if (!wdc) return;
	RECT full = { 0, 0, w, h };
	BP_PAINTPARAMS params;
	memset(&params, 0, sizeof(params));
	params.cbSize = sizeof(params);
	HDC buf = NULL;
	HPAINTBUFFER pb = BeginBufferedPaint(wdc, &full, BPBF_TOPDOWNDIB, &params, &buf);
	if (pb && buf) {
		BitBlt(buf, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
		if (cw > 0 && ch > 0) {
			void* plate = NULL;
			HDC mem = OxzPlateBegin(&plate, buf, cw, ch, LeafFill(hwnd));
			if (plate && mem) {
				SetPropW(hwnd, kInPrint, (HANDLE)1);
				SendMessageW(hwnd, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT | PRF_ERASEBKGND);
				RemovePropW(hwnd, kInPrint);
				OxzPlateEnd(plate, buf, cl, ct, 0);
			}
		}
		BufferedPaintMakeOpaque(pb, NULL);
		EndBufferedPaint(pb, TRUE);
	}
	ReleaseDC(hwnd, wdc);
}

static LRESULT CALLBACK GlassChild(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	WNDPROC old = (WNDPROC)GetPropW(hwnd, kOldProc);
	if (!old) return DefWindowProcW(hwnd, msg, wp, lp);
	if (msg == WM_NCDESTROY) {
		RemovePropW(hwnd, kOldProc);
		RemovePropW(hwnd, kInPrint);
		RemovePropW(hwnd, kPostOp);
		return CallWindowProcW(old, hwnd, msg, wp, lp);
	}
	if (!OxzPerPixelAcrylic() || GetPropW(hwnd, kInPrint))
		return CallWindowProcW(old, hwnd, msg, wp, lp);
	int stat = ClassIs(hwnd, L"Static");
	int btn = ClassIs(hwnd, L"Button");
	int list = ClassIs(hwnd, L"SysListView32") || ClassIs(hwnd, L"ListBox");
	int head = ClassIs(hwnd, L"SysHeader32");
	int edit = ClassIs(hwnd, L"Edit");
	int leaf = stat || btn || list || head || edit
		|| ClassIs(hwnd, L"msctls_progress32") || ClassIs(hwnd, L"msctls_trackbar32");
	if (!leaf) return CallWindowProcW(old, hwnd, msg, wp, lp);
	if (msg == WM_ERASEBKGND) return 1;
	if (msg == OX_POST) {
		RemovePropW(hwnd, kPostOp);
		if (head) PresentHeader(hwnd);
		else {
			PresentLeaf(hwnd);
			if (edit && NeedsNc(hwnd)) MakeWindowOpaque(hwnd);
			RestoreListHeader(hwnd);
		}
		ValidateRect(hwnd, NULL);
		return 0;
	}
	if (edit && (msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_KEYUP
		|| msg == WM_IME_COMPOSITION || msg == WM_IME_ENDCOMPOSITION
		|| msg == WM_PASTE || msg == WM_CUT || msg == WM_CLEAR || msg == WM_UNDO
		|| msg == EM_SETSEL || msg == WM_SETTEXT
		|| msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE
		|| msg == WM_SETFOCUS || msg == WM_KILLFOCUS)) {
		LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
		if (!GetPropW(hwnd, kPostOp)) {
			SetPropW(hwnd, kPostOp, (HANDLE)1);
			PostMessageW(hwnd, OX_POST, 0, 0);
		}
		return r;
	}
	if (head && (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE || msg == WM_MOUSEHOVER
		|| msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK
		|| msg == WM_CAPTURECHANGED || msg == WM_TIMER)) {
		SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
		LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
		SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
		PresentHeader(hwnd);
		ValidateRect(hwnd, NULL);
		if (!GetPropW(hwnd, kPostOp)) {
			SetPropW(hwnd, kPostOp, (HANDLE)1);
			PostMessageW(hwnd, OX_POST, 0, 0);
		}
		return r;
	}
	if (list && (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE || msg == WM_MOUSEHOVER
		|| msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK
		|| msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP
		|| msg == WM_KEYDOWN || msg == WM_SETFOCUS || msg == WM_KILLFOCUS)) {
		LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
		if (!GetPropW(hwnd, kPostOp)) {
			SetPropW(hwnd, kPostOp, (HANDLE)1);
			PostMessageW(hwnd, OX_POST, 0, 0);
		}
		return r;
	}
	if (msg == WM_PAINT) {
		if (list) {
			PresentLeaf(hwnd);
			RestoreListHeader(hwnd);
			ValidateRect(hwnd, NULL);
			return 0;
		}
		if (head) {
			PresentHeader(hwnd);
			ValidateRect(hwnd, NULL);
			return 0;
		}
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		if (stat) PaintGlassLabel(hwnd, dc);
		else PresentLeaf(hwnd);
		EndPaint(hwnd, &ps);
		return 0;
	}
	if (msg == WM_NCPAINT) {
		LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
		if (head) {
			PresentHeader(hwnd);
			return r;
		}
		if (NeedsNc(hwnd)) {
			PresentLeaf(hwnd);
			MakeWindowOpaque(hwnd);
			RestoreListHeader(hwnd);
		}
		return r;
	}
	if (list && (msg == WM_VSCROLL || msg == WM_HSCROLL || msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL)) {
		LRESULT r = CallWindowProcW(old, hwnd, msg, wp, lp);
		PresentLeaf(hwnd);
		MakeWindowOpaque(hwnd);
		RestoreListHeader(hwnd);
		ValidateRect(hwnd, NULL);
		return r;
	}
	return CallWindowProcW(old, hwnd, msg, wp, lp);
}

static void HookOne(HWND hwnd)
{
	if (!hwnd) return;
	wchar_t cls[64];
	if (!GetClassNameW(hwnd, cls, 64)) return;
	int list = _wcsicmp(cls, L"SysListView32") == 0;
	int head = _wcsicmp(cls, L"SysHeader32") == 0;
	if (list) {
		ListView_SetExtendedListViewStyleEx(hwnd, LVS_EX_DOUBLEBUFFER | LVS_EX_TRACKSELECT | LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT, 0);
		ListView_SetHoverTime(hwnd, (DWORD)-1);
		LONG st = GetWindowLongW(hwnd, GWL_STYLE);
		if ((st & WS_CLIPCHILDREN) == 0) {
			SetWindowLongW(hwnd, GWL_STYLE, st | WS_CLIPCHILDREN);
			SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
	}
	if (head) {
		SetWindowTheme(hwnd, L"", L"");
		LONG st = GetWindowLongW(hwnd, GWL_STYLE);
		if (st & HDS_HOTTRACK) SetWindowLongW(hwnd, GWL_STYLE, st & ~HDS_HOTTRACK);
	}
	if (GetPropW(hwnd, kOldProc)) return;
	if (!list && _wcsicmp(cls, L"Static") && _wcsicmp(cls, L"Button") && _wcsicmp(cls, L"Edit")
		&& _wcsicmp(cls, L"ListBox") && _wcsicmp(cls, L"SysHeader32")
		&& _wcsicmp(cls, L"msctls_progress32") && _wcsicmp(cls, L"msctls_trackbar32"))
		return;
	WNDPROC old = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)GlassChild);
	if (old) SetPropW(hwnd, kOldProc, (HANDLE)old);
}

static BOOL CALLBACK BindCb(HWND child, LPARAM)
{
	HookOne(child);
	EnumChildWindows(child, BindCb, 0);
	return TRUE;
}

void OxzBindAcrylic(HWND hwnd)
{
	if (!hwnd || !OxzPerPixelAcrylic()) return;
	EnumChildWindows(hwnd, BindCb, 0);
}

int OxzHostErase(HWND hwnd, HDC dc, HBRUSH br)
{
	if (gAcrylic && Win11()) return 1;
	if (dc && br && hwnd) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		FillRect(dc, &rc, br);
	}
	return 1;
}

static void ClearGlassRect(HDC dc, const RECT& rc)
{
	int w = rc.right - rc.left;
	int h = rc.bottom - rc.top;
	if (w < 1 || h < 1 || !dc) return;
	static int bp = 0;
	if (!bp) { BufferedPaintInit(); bp = 1; }
	BP_PAINTPARAMS params;
	memset(&params, 0, sizeof(params));
	params.cbSize = sizeof(params);
	HDC buf = NULL;
	HPAINTBUFFER pb = BeginBufferedPaint(dc, &rc, BPBF_TOPDOWNDIB, &params, &buf);
	if (pb) {
		RGBQUAD* bits = NULL;
		int stride = 0;
		if (SUCCEEDED(GetBufferedPaintBits(pb, &bits, &stride)) && bits && stride > 0) {
			int ww = w < stride ? w : stride;
			for (int y = 0; y < h; ++y) {
				RGBQUAD* row = bits + (size_t)y * (size_t)stride;
				for (int x = 0; x < ww; ++x) {
					row[x].rgbBlue = 0;
					row[x].rgbGreen = 0;
					row[x].rgbRed = 0;
					row[x].rgbReserved = 0;
				}
			}
		}
		EndBufferedPaint(pb, TRUE);
		return;
	}
	void* plate = NULL;
	HDC mem = OxzPlateBegin(&plate, dc, w, h, RGB(1, 1, 1));
	if (plate && mem) OxzPlateEnd(plate, dc, rc.left, rc.top, 1);
}

void OxzHostPaint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	if (OxzPerPixelAcrylic() && dc) {
		RECT rc;
		GetClientRect(hwnd, &rc);
		HRGN rgn = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
		for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
			if (!IsWindowVisible(c)) continue;
			RECT wr;
			GetWindowRect(c, &wr);
			MapWindowPoints(NULL, hwnd, (LPPOINT)&wr, 2);
			HRGN sub = CreateRectRgn(wr.left, wr.top, wr.right, wr.bottom);
			CombineRgn(rgn, rgn, sub, RGN_DIFF);
			DeleteObject(sub);
		}
		DWORD need = GetRegionData(rgn, 0, NULL);
		if (need) {
			std::vector<BYTE> raw(need);
			RGNDATA* data = (RGNDATA*)raw.data();
			if (GetRegionData(rgn, need, data)) {
				RECT* rs = (RECT*)data->Buffer;
				for (DWORD i = 0; i < data->rdh.nCount; ++i)
					ClearGlassRect(dc, rs[i]);
			}
		}
		DeleteObject(rgn);
	}
	EndPaint(hwnd, &ps);
}

HBRUSH OxzHostCtlColor(HDC dc, UINT msg, HBRUSH plate)
{
	const OxzPal& p = OxzLook();
	if (gAcrylic && Win11() && msg == WM_CTLCOLORSTATIC) {
		SetBkMode(dc, TRANSPARENT);
		SetTextColor(dc, p.text);
		return (HBRUSH)GetStockObject(NULL_BRUSH);
	}
	COLORREF bg = (msg == WM_CTLCOLORSTATIC) ? p.bg0 : p.face;
	SetBkColor(dc, bg);
	SetTextColor(dc, p.text);
	return plate ? plate : (HBRUSH)GetStockObject(WHITE_BRUSH);
}

static void ClearChildBackdrop(HWND hwnd)
{
	for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
		int none = DWMSBT_NONE;
		DwmSetWindowAttribute(c, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
		ClearChildBackdrop(c);
	}
}

static void SetAccent(HWND hwnd, int state)
{
	typedef BOOL(WINAPI* Fn)(HWND, void*);
	struct Pol { int state, flags, color, anim; };
	struct Dat { int attrib; void* data; SIZE_T size; };
	static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute");
	if (!fn) return;
	Pol pol = { state, 0, 0, 0 };
	Dat dat = { 19, &pol, sizeof(pol) };
	fn(hwnd, &dat);
}

void OxzTintCaption(HWND hwnd)
{
	if (!hwnd) return;
	const OxzPal& p = OxzLook();
	COLORREF cap = p.ribbon0;
	COLORREF text = OxzInk(cap);
	COLORREF border = p.ribbonEdge;
	DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
	DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
	DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

void OxzApplyChrome(HWND hwnd)
{
	if (!hwnd) return;
	OxzTintCaption(hwnd);
	DWORD build = WinBuild();
#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
	int backdropNone = DWMSBT_NONE;
	DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropNone, sizeof(backdropNone));
	MARGINS zero = { 0, 0, 0, 0 };
	DwmExtendFrameIntoClientArea(hwnd, &zero);
	SetAccent(hwnd, 0);
	DWM_BLURBEHIND bbOff = {};
	bbOff.dwFlags = DWM_BB_ENABLE;
	bbOff.fEnable = FALSE;
	DwmEnableBlurBehindWindow(hwnd, &bbOff);
	LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if (ex & WS_EX_LAYERED) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
	if (!GetPropW(hwnd, L"OxzBrFlag")) {
		SetPropW(hwnd, L"OxzBrFlag", (HANDLE)1);
		SetPropW(hwnd, L"OxzBr", (HANDLE)GetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND));
	}
	if (!gAcrylic) {
		if (build >= 26100) {
			BOOL useAlpha = FALSE;
			DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
		}
		int square = 1;
		DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &square, sizeof(square));
		HANDLE saved = GetPropW(hwnd, L"OxzBr");
		SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)saved);
		ClearChildBackdrop(hwnd);
		SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropNone, sizeof(backdropNone));
		DwmExtendFrameIntoClientArea(hwnd, &zero);
		if (build >= 26100) {
			BOOL useAlpha = FALSE;
			DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
		}
		DwmEnableBlurBehindWindow(hwnd, &bbOff);
		/* ガラスだった画素は RGB 0。アルファを切るとそれが黒になる。テーマ色を α=255 で書き戻す。 */
		{
			RECT rc;
			GetClientRect(hwnd, &rc);
			int w = rc.right - rc.left;
			int h = rc.bottom - rc.top;
			HDC dc = (w > 0 && h > 0) ? GetDC(hwnd) : NULL;
			if (dc) {
				static int bp = 0;
				if (!bp) { BufferedPaintInit(); bp = 1; }
				BP_PAINTPARAMS params;
				memset(&params, 0, sizeof(params));
				params.cbSize = sizeof(params);
				HDC buf = NULL;
				HPAINTBUFFER pb = BeginBufferedPaint(dc, &rc, BPBF_TOPDOWNDIB, &params, &buf);
				COLORREF color = OxzLook().bg0;
				if (pb && buf) {
					HBRUSH br = CreateSolidBrush(color);
					FillRect(buf, &rc, br);
					DeleteObject(br);
					RGBQUAD* bits = NULL;
					int stride = 0;
					if (SUCCEEDED(GetBufferedPaintBits(pb, &bits, &stride)) && bits && stride > 0) {
						UINT32 px = 0xFF000000u
							| ((UINT32)GetRValue(color) << 16)
							| ((UINT32)GetGValue(color) << 8)
							| (UINT32)GetBValue(color);
						int ww = w < stride ? w : stride;
						for (int y = 0; y < h; ++y) {
							UINT32* row = (UINT32*)((BYTE*)bits + (size_t)y * (size_t)stride * 4);
							for (int x = 0; x < ww; ++x) row[x] = px;
						}
					}
					EndBufferedPaint(pb, TRUE);
				} else {
					HBRUSH br = CreateSolidBrush(color);
					FillRect(dc, &rc, br);
					DeleteObject(br);
				}
				ReleaseDC(hwnd, dc);
			}
		}
		gDwmAcrylic = 0;
		SetPropW(hwnd, L"OxzDwm", (HANDLE)(INT_PTR)1);
		OxzBindAcrylic(hwnd);
		RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
		return;
	}
	BOOL composition = FALSE;
	DwmIsCompositionEnabled(&composition);
	if (build >= 22000 && composition) {
		int backdrop = DWMSBT_TRANSIENTWINDOW;
		if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop)))) {
			int round = 2;
			DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
			if (build >= 26100) {
				BOOL useAlpha = TRUE;
				DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
			}
			MARGINS margins = { -1, -1, -1, -1 };
			DwmExtendFrameIntoClientArea(hwnd, &margins);
		}
	} else if (build >= 10240) {
		ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
		SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);
		DWM_BLURBEHIND bb = {};
		bb.dwFlags = DWM_BB_ENABLE;
		bb.fEnable = TRUE;
		DwmEnableBlurBehindWindow(hwnd, &bb);
		HANDLE saved = GetPropW(hwnd, L"OxzBr");
		SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)saved);
	}
	ClearChildBackdrop(hwnd);
	SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	if (build >= 22000 && composition) {
		int backdrop = DWMSBT_TRANSIENTWINDOW;
		DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
		int round = 2;
		DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
		if (build >= 26100) {
			BOOL useAlpha = TRUE;
			DwmSetWindowAttribute(hwnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
		}
		MARGINS margins = { -1, -1, -1, -1 };
		DwmExtendFrameIntoClientArea(hwnd, &margins);
		SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, 0);
	}
	gDwmAcrylic = 1;
	SetPropW(hwnd, L"OxzDwm", (HANDLE)(INT_PTR)2);
	OxzBindAcrylic(hwnd);
}

void OxzRefreshChrome(HWND hwnd)
{
	if (!hwnd) return;
	int want = gAcrylic ? 2 : 1;
	int have = (int)(INT_PTR)GetPropW(hwnd, L"OxzDwm");
	if (have != want) OxzApplyChrome(hwnd);
	else OxzTintCaption(hwnd);
}

void OxzRowColors(int row, int sel, COLORREF* bg, COLORREF* fg)
{
	const OxzPal& p = OxzLook();
	COLORREF b = sel ? p.hotTop : (row % 2 ? p.bg1 : p.face);
	if (bg) *bg = b;
	if (fg) *fg = sel ? OxzInk(b) : p.text;
}

static COLORREF Mix(COLORREF a, COLORREF b, int wa)
{
	int wb = 100 - wa;
	if (wa < 0) wa = 0;
	if (wb < 0) wb = 0;
	return RGB(
		(GetRValue(a) * wa + GetRValue(b) * wb) / 100,
		(GetGValue(a) * wa + GetGValue(b) * wb) / 100,
		(GetBValue(a) * wa + GetBValue(b) * wb) / 100);
}

struct BtnMem { WNDPROC old; int hover; int down; };

void OxzPaintButton(const DRAWITEMSTRUCT* di, HFONT font)
{
	if (!di) return;
	const OxzPal& p = OxzLook();
	BtnMem* st = (BtnMem*)GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
	int dis = (di->itemState & ODS_DISABLED) != 0;
	int hot = st && st->hover && !dis;
	int down = ((di->itemState & ODS_SELECTED) != 0) || (st && st->down);
	COLORREF bg = down ? p.hotBot : hot ? p.hotTop : p.bg1;
	if (dis) bg = Mix(bg, RGB(214, 220, 214), 45);
	COLORREF fg = OxzInk(bg);
	int rad = OxzCorner();
	if (rad < 0) rad = 0;
	if (rad > 16) rad = 16;
	if (!OxzPerPixelAcrylic()) {
		HBRUSH face = CreateSolidBrush(p.bg0);
		FillRect(di->hDC, &di->rcItem, face);
		DeleteObject(face);
	}
	HRGN rgn = NULL;
	if (rad > 0) {
		rgn = CreateRoundRectRgn(di->rcItem.left, di->rcItem.top, di->rcItem.right + 1, di->rcItem.bottom + 1, rad * 2, rad * 2);
		SelectClipRgn(di->hDC, rgn);
	}
	HBRUSH br = CreateSolidBrush(bg);
	FillRect(di->hDC, &di->rcItem, br);
	DeleteObject(br);
	if (gSoft && !dis) {
		RECT gloss = di->rcItem;
		gloss.bottom = gloss.top + (gloss.bottom - gloss.top) / 3;
		if (gloss.bottom > gloss.top) {
			HBRUSH hi = CreateSolidBrush(Mix(RGB(255, 255, 255), bg, down ? 18 : 42));
			FillRect(di->hDC, &gloss, hi);
			DeleteObject(hi);
		}
	}
	if (rad > 0) {
		SelectClipRgn(di->hDC, NULL);
		DeleteObject(rgn);
	}
	HPEN pen = CreatePen(PS_SOLID, gSoft ? 2 : 1, dis ? RGB(160, 160, 160) : p.edge);
	HGDIOBJ old = SelectObject(di->hDC, pen);
	HGDIOBJ oldB = SelectObject(di->hDC, GetStockObject(NULL_BRUSH));
	if (rad > 0)
		RoundRect(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom, rad * 2, rad * 2);
	else
		Rectangle(di->hDC, di->rcItem.left, di->rcItem.top, di->rcItem.right, di->rcItem.bottom);
	SelectObject(di->hDC, oldB);
	SelectObject(di->hDC, old);
	DeleteObject(pen);
	wchar_t text[256];
	GetWindowTextW(di->hwndItem, text, 256);
	SetBkMode(di->hDC, TRANSPARENT);
	SetTextColor(di->hDC, fg);
	if (font) SelectObject(di->hDC, font);
	DrawTextW(di->hDC, text, -1, (RECT*)&di->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

int OxzLookCommand(UINT id)
{
	if (id == ID_LOOK_BOLD) { gBold = gBold ? 0 : 1; PutDword(L"fontBold", gBold); return 1; }
	if (id == ID_LOOK_ITALIC) { gItalic = gItalic ? 0 : 1; PutDword(L"fontItalic", gItalic); return 1; }
	if (id == ID_LOOK_ACRYLIC) { gAcrylic = gAcrylic ? 0 : 1; PutDword(L"acrylic", gAcrylic); return 1; }
	if (id == ID_LOOK_SOFT) { gSoft = gSoft ? 0 : 1; PutDword(L"soft", gSoft); return 1; }
	if (id >= ID_LOOK_ANIM0 && id < ID_LOOK_ANIM0 + OXZ_ANIM_COUNT) {
		gAnim = (int)(id - ID_LOOK_ANIM0);
		PutDword(L"anim", (DWORD)gAnim);
		return 1;
	}
	if (id >= ID_LOOK_SIZE0 && id < ID_LOOK_SIZE0 + 17) {
		gPt = 8 + (int)(id - ID_LOOK_SIZE0);
		PutDword(L"fontPt", (DWORD)gPt);
		return 1;
	}
	if (id >= ID_LOOK_THEME0 && id < ID_LOOK_THEME0 + OXZ_THEME_COUNT) {
		gPreview = -1;
		gTheme = (int)(id - ID_LOOK_THEME0);
		PutDword(L"theme", (DWORD)gTheme);
		return 1;
	}
	EnsureFaces();
	if (id >= ID_LOOK_FACE0 && id < ID_LOOK_FACE0 + (UINT)gFaces.size()) {
		wcsncpy_s(gFace, gFaces[id - ID_LOOK_FACE0].c_str(), _TRUNCATE);
		RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\OxzGui", L"fontFace", REG_SZ, gFace, (DWORD)((wcslen(gFace) + 1) * sizeof(wchar_t)));
		return 1;
	}
	return 0;
}

static const wchar_t* kAnim[OXZ_ANIM_COUNT][14] = {
	{ L"クラシック（フェード）", L"Classic (fade)", L"Classique (fondu)", L"Classica (dissolvenza)", L"Clasica (fundido)", L"클래식(페이드)", L"经典（淡入）", L"كلاسيكي (تلاشي)", L"Классика (затухание)", L"Klassisch (Fade)", L"Classica (fade)", L"Klassiek (fade)", L"Klasyczna (fade)", L"Klasik (fade)" },
	{ L"上下に伸びる", L"Expand up/down", L"Expansion haut/bas", L"Espansione su/giu", L"Expandir arriba/abajo", L"위아래로 펼침", L"上下展开", L"توسيع لأعلى/أسفل", L"Раскрытие вверх/вниз", L"Auf/Ab aufklappen", L"Expandir cima/baixo", L"Uitklappen omhoog/omlaag", L"Rozwin w gore/dol", L"Yukari/asagi acilim" },
	{ L"カーテン（上から）", L"Curtain (from top)", L"Rideau (haut)", L"Tendina (alto)", L"Cortina (arriba)", L"커튼(위에서)", L"窗帘（从上）", L"ستارة (من الأعلى)", L"Штора (сверху)", L"Vorhang (oben)", L"Cortina (cima)", L"Gordijn (boven)", L"Kurtyna (z gory)", L"Perde (yukaridan)" },
	{ L"ワイプ（横）", L"Wipe (horizontal)", L"Balayage (horizontal)", L"Wipe (orizzontale)", L"Barrido (horizontal)", L"와이프(가로)", L"擦除（横向）", L"مسح (أفقي)", L"Вытеснение (гориз.)", L"Wischen (horizontal)", L"Wipe (horizontal)", L"Wipe (horizontaal)", L"Wymazanie (poz.)", L"Silme (yatay)" },
	{ L"リップル（起点）", L"Ripple (from click)", L"Ondulation (clic)", L"Increspatura (clic)", L"Onda (clic)", L"리플(클릭)", L"涟漪（点击）", L"تموج (نقرة)", L"Рябь (от клика)", L"Wellig (Klick)", L"Ondulacao (clique)", L"Rimpel (klik)", L"Fala (klik)", L"Dalga (tik)" },
	{ L"ビッグバン／ブラックホール", L"Big Bang / Black Hole", L"Big Bang / Trou noir", L"Big Bang / Buco nero", L"Big Bang / Agujero negro", L"빅뱅/블랙홀", L"大爆炸／黑洞", L"الانفجار العظيم / ثقب أسود", L"Большой взрыв / Чёрная дыра", L"Urknall / Schwarzes Loch", L"Big Bang / Buraco negro", L"Big Bang / Zwart gat", L"Big Bang / Czarna dziura", L"Buyuk Patlama / Kara Delik" },
	{ L"螺旋（スパイラル）", L"Spiral", L"Spirale", L"Spirale", L"Espiral", L"스파이럴", L"螺旋", L"حلزوني", L"Спираль", L"Spirale", L"Espiral", L"Spiraal", L"Spirala", L"Spiral" },
	{ L"花びら", L"Petals", L"Petales", L"Petali", L"Petalos", L"꽃잎", L"花瓣", L"بتلات", L"Лепестки", L"Blütenblätter", L"Petalas", L"Bloemblaadjes", L"Platki", L"Yapraklar" },
	{ L"ジッパー（左右交互）", L"Zipper (L/R)", L"Fermeture (G/D)", L"Cerniera (S/D)", L"Cremallera (I/D)", L"지퍼(좌우)", L"拉链（左右）", L"سحاب (يسار/يمين)", L"Молния (Л/П)", L"Reißverschluss (L/R)", L"Ziper (E/D)", L"Rits (L/R)", L"Zamek (L/P)", L"Fermuar (S/S)" },
	{ L"オーロラ（波）", L"Aurora (wave)", L"Aurore (onde)", L"Aurora (onda)", L"Aurora (onda)", L"오로라(파도)", L"极光（波浪）", L"شفق (موجة)", L"Полярное сияние", L"Polarlicht (Welle)", L"Aurora (onda)", L"Noorderlicht (golf)", L"Zorza (fala)", L"Aurora (dalga)" },
};

static const wchar_t* AnimName(int id)
{
	if (id < 0 || id >= OXZ_ANIM_COUNT) id = OXZ_ANIM_AURORA;
	int lang = OxzLang();
	if (lang < 0 || lang > 13) lang = 1;
	const wchar_t* s = kAnim[id][lang];
	return (s && s[0]) ? s : kAnim[id][1];
}

static void AddItem(std::vector<OxzMi>& v, int kind, UINT id, const wchar_t* text, int checked)
{
	OxzMi m;
	m.kind = kind;
	m.id = id;
	m.text = text ? text : L"";
	m.checked = checked;
	v.push_back(std::move(m));
}

int OxzMenuAnim()
{
	return gAnim;
}

UINT OxzOpenLookMenu(HWND owner, int x, int y, const OxzPopAdd* add, int count)
{
	EnsureFaces();
	std::vector<OxzMi> items;
	OxzMi font;
	font.kind = OXMI_SUB;
	font.id = 0;
	font.text = OxzTxt(OS_LOOK_FONT);
	font.checked = 0;
	AddItem(font.sub, OXMI_CMD, ID_LOOK_BOLD, OxzTxt(OS_LOOK_BOLD), gBold);
	AddItem(font.sub, OXMI_CMD, ID_LOOK_ITALIC, OxzTxt(OS_LOOK_ITALIC), gItalic);
	OxzMi sizes;
	sizes.kind = OXMI_SUB;
	sizes.id = 0;
	sizes.text = OxzTxt(OS_LOOK_SIZE);
	sizes.checked = 0;
	for (int pt = 8; pt <= 24; ++pt) {
		wchar_t s[16];
		_snwprintf_s(s, _TRUNCATE, L"%d pt", pt);
		AddItem(sizes.sub, OXMI_CMD, ID_LOOK_SIZE0 + (pt - 8), s, pt == gPt);
	}
	font.sub.push_back(std::move(sizes));
	AddItem(font.sub, OXMI_SEP, 0, L"", 0);
	for (size_t i = 0; i < gFaces.size(); ++i)
		AddItem(font.sub, OXMI_CMD, ID_LOOK_FACE0 + (UINT)i, gFaces[i].c_str(), _wcsicmp(gFaces[i].c_str(), gFace) == 0);
	items.push_back(std::move(font));
	AddItem(items, OXMI_CMD, ID_LOOK_ACRYLIC, OxzTxt(OS_LOOK_ACRYLIC), gAcrylic);
	AddItem(items, OXMI_CMD, ID_LOOK_SOFT, OxzTxt(OS_LOOK_SOFT), gSoft);
	OxzMi theme;
	theme.kind = OXMI_SUB;
	theme.id = 0;
	theme.text = OxzTxt(OS_LOOK_THEME);
	theme.checked = 0;
	for (int i = 0; i < OXZ_THEME_COUNT; ++i)
		AddItem(theme.sub, OXMI_CMD, ID_LOOK_THEME0 + i, OxzThemeName(i), i == gTheme);
	items.push_back(std::move(theme));
	OxzMi anim;
	anim.kind = OXMI_SUB;
	anim.id = 0;
	anim.text = OxzTxt(OS_LOOK_ANIM);
	anim.checked = 0;
	for (int i = 0; i < OXZ_ANIM_COUNT; ++i)
		AddItem(anim.sub, OXMI_CMD, ID_LOOK_ANIM0 + i, AnimName(i), i == gAnim);
	items.push_back(std::move(anim));
	if (add && count > 0) {
		AddItem(items, OXMI_SEP, 0, L"", 0);
		for (int i = 0; i < count; ++i) {
			if (add[i].sep) AddItem(items, OXMI_SEP, 0, L"", 0);
			if (add[i].text && add[i].text[0]) AddItem(items, OXMI_CMD, add[i].id, add[i].text, 0);
		}
	}
	UINT id = OxzTrackPopup(owner, x, y, items);
	OxzFinishThemePreview(id >= ID_LOOK_THEME0 && id < ID_LOOK_THEME0 + OXZ_THEME_COUNT);
	return id;
}
