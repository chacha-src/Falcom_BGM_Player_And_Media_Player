// おまけタクティクス: 文字列（14 言語）
// 並び: 日本語, 英語, フランス語, イタリア語, スペイン語, 韓国語, 中国語, アラビア語, ロシア語, ドイツ語, ポルトガル語, オランダ語, ポーランド語, トルコ語
#include "stdafx.h"
#include "OmakeTacticsText.h"

namespace otac {

namespace {

struct TxRow { int id; const wchar_t* s[14]; };

#define R(id, ja, en, fr, it, es, ko, zh, ar, ru, de, pt, nl, pl, tr) { id, { ja, en, fr, it, es, ko, zh, ar, ru, de, pt, nl, pl, tr } },

const TxRow kRows[] = {
// ---------------------------------------------------------------- 地形
R(TX_TER_PLAIN, L"平地", L"Plain", L"Plaine", L"Pianura", L"Llanura", L"평지", L"平地", L"سهل", L"Равнина", L"Ebene", L"Planície", L"Vlakte", L"Równina", L"Ova")
R(TX_TER_FOREST, L"森", L"Forest", L"Forêt", L"Foresta", L"Bosque", L"숲", L"森林", L"غابة", L"Лес", L"Wald", L"Floresta", L"Bos", L"Las", L"Orman")
R(TX_TER_HILL, L"丘", L"Hill", L"Colline", L"Collina", L"Colina", L"언덕", L"丘陵", L"تل", L"Холм", L"Hügel", L"Colina", L"Heuvel", L"Wzgórze", L"Tepe")
R(TX_TER_MOUNTAIN, L"山", L"Mountain", L"Montagne", L"Montagna", L"Montaña", L"산", L"山地", L"جبل", L"Гора", L"Berg", L"Montanha", L"Berg", L"Góra", L"Dağ")
R(TX_TER_WATER, L"水辺", L"Water", L"Eau", L"Acqua", L"Agua", L"물가", L"水域", L"ماء", L"Вода", L"Wasser", L"Água", L"Water", L"Woda", L"Su")
R(TX_TER_CHASM, L"谷底", L"Chasm", L"Gouffre", L"Abisso", L"Abismo", L"골짜기", L"深谷", L"هاوية", L"Пропасть", L"Schlucht", L"Abismo", L"Kloof", L"Przepaść", L"Uçurum")
R(TX_TER_LAVA, L"溶岩", L"Lava", L"Lave", L"Lava", L"Lava", L"용암", L"熔岩", L"حمم", L"Лава", L"Lava", L"Lava", L"Lava", L"Lawa", L"Lav")
R(TX_TER_SAND, L"砂地", L"Sand", L"Sable", L"Sabbia", L"Arena", L"모래땅", L"沙地", L"رمال", L"Песок", L"Sand", L"Areia", L"Zand", L"Piasek", L"Kum")
R(TX_TER_SNOW, L"雪原", L"Snowfield", L"Neige", L"Neve", L"Nieve", L"설원", L"雪原", L"ثلوج", L"Снег", L"Schneefeld", L"Neve", L"Sneeuw", L"Śnieg", L"Kar")
R(TX_TER_SWAMP, L"沼地", L"Swamp", L"Marais", L"Palude", L"Pantano", L"늪", L"沼泽", L"مستنقع", L"Болото", L"Sumpf", L"Pântano", L"Moeras", L"Bagno", L"Bataklık")
R(TX_TER_ROAD, L"道", L"Road", L"Route", L"Strada", L"Camino", L"길", L"道路", L"طريق", L"Дорога", L"Weg", L"Estrada", L"Weg", L"Droga", L"Yol")
R(TX_TER_BRIDGE, L"橋", L"Bridge", L"Pont", L"Ponte", L"Puente", L"다리", L"桥", L"جسر", L"Мост", L"Brücke", L"Ponte", L"Brug", L"Most", L"Köprü")
R(TX_TER_RUINS, L"遺跡", L"Ruins", L"Ruines", L"Rovine", L"Ruinas", L"유적", L"遗迹", L"أطلال", L"Руины", L"Ruinen", L"Ruínas", L"Ruïnes", L"Ruiny", L"Harabeler")
R(TX_TER_ROCK, L"岩場", L"Rocks", L"Rochers", L"Rocce", L"Rocas", L"바위", L"岩地", L"صخور", L"Скалы", L"Felsen", L"Rochas", L"Rotsen", L"Skały", L"Kayalık")
// ---------------------------------------------------------------- テーマ
R(TX_TH_PLAINS, L"平原", L"Plains", L"Plaines", L"Pianure", L"Llanuras", L"평원", L"平原", L"السهول", L"Равнины", L"Ebenen", L"Planícies", L"Vlakten", L"Równiny", L"Ovalar")
R(TX_TH_CANYON, L"渓谷", L"Canyon", L"Canyon", L"Canyon", L"Cañón", L"협곡", L"峡谷", L"الوادي", L"Каньон", L"Canyon", L"Desfiladeiro", L"Kloof", L"Kanion", L"Kanyon")
R(TX_TH_FOREST, L"深い森", L"Deep Forest", L"Forêt profonde", L"Foresta profonda", L"Bosque profundo", L"깊은 숲", L"密林", L"الغابة العميقة", L"Глухой лес", L"Tiefer Wald", L"Floresta densa", L"Diep woud", L"Głęboki las", L"Derin orman")
R(TX_TH_SNOW, L"雪原", L"Snowfield", L"Champ de neige", L"Distesa innevata", L"Campo nevado", L"설원", L"雪原", L"السهول الثلجية", L"Снежная равнина", L"Schneefeld", L"Campo nevado", L"Sneeuwvlakte", L"Śnieżna równina", L"Karlı ova")
R(TX_TH_DESERT, L"砂漠", L"Desert", L"Désert", L"Deserto", L"Desierto", L"사막", L"沙漠", L"الصحراء", L"Пустыня", L"Wüste", L"Deserto", L"Woestijn", L"Pustynia", L"Çöl")
R(TX_TH_VOLCANO, L"火山", L"Volcano", L"Volcan", L"Vulcano", L"Volcán", L"화산", L"火山", L"البركان", L"Вулкан", L"Vulkan", L"Vulcão", L"Vulkaan", L"Wulkan", L"Yanardağ")
R(TX_TH_SWAMP, L"湿地帯", L"Marshland", L"Marécages", L"Paludi", L"Marismas", L"습지대", L"湿地", L"الأراضي الرطبة", L"Топи", L"Sumpfland", L"Pantanal", L"Moerasland", L"Mokradła", L"Sulak alan")
R(TX_TH_RUINS, L"古代遺跡", L"Ancient Ruins", L"Ruines antiques", L"Rovine antiche", L"Ruinas antiguas", L"고대 유적", L"古代遗迹", L"الأطلال القديمة", L"Древние руины", L"Alte Ruinen", L"Ruínas antigas", L"Oude ruïnes", L"Starożytne ruiny", L"Antik harabeler")
// ---------------------------------------------------------------- 能力値
R(TX_ST_HP, L"HP", L"HP", L"PV", L"PS", L"PV", L"HP", L"HP", L"الصحة", L"ОЗ", L"LP", L"PV", L"LP", L"PŻ", L"CP")
R(TX_ST_MP, L"MP", L"MP", L"PM", L"PM", L"PM", L"MP", L"MP", L"المانا", L"ОМ", L"MP", L"PM", L"MP", L"PM", L"MP")
R(TX_ST_STR, L"力", L"STR", L"FOR", L"FOR", L"FUE", L"힘", L"力量", L"القوة", L"СИЛ", L"STÄ", L"FOR", L"KRA", L"SIŁ", L"GÜÇ")
R(TX_ST_DEF, L"守備", L"DEF", L"DÉF", L"DIF", L"DEF", L"수비", L"防御", L"الدفاع", L"ЗАЩ", L"VER", L"DEF", L"VER", L"OBR", L"SAV")
R(TX_ST_MAG, L"魔力", L"MAG", L"MAG", L"MAG", L"MAG", L"마력", L"魔力", L"السحر", L"МАГ", L"MAG", L"MAG", L"MAG", L"MAG", L"BÜY")
R(TX_ST_RES, L"魔防", L"RES", L"RÉS", L"RES", L"RES", L"마방", L"魔防", L"المقاومة", L"СОП", L"WID", L"RES", L"WEE", L"ODP", L"DİR")
R(TX_ST_AGI, L"素早さ", L"AGI", L"AGI", L"AGI", L"AGI", L"민첩", L"敏捷", L"الرشاقة", L"ЛОВ", L"GEW", L"AGI", L"BEH", L"ZWI", L"ÇEV")
R(TX_ST_DEX, L"技", L"DEX", L"DEX", L"DES", L"DES", L"기술", L"技巧", L"المهارة", L"ТЕХ", L"GES", L"DES", L"VAA", L"ZRĘ", L"BEC")
R(TX_ST_LUK, L"運", L"LUK", L"CHA", L"FOR", L"SUE", L"운", L"幸运", L"الحظ", L"УДЧ", L"GLÜ", L"SOR", L"GEL", L"SZC", L"ŞAN")
R(TX_ST_MOV, L"移動", L"MOV", L"DÉP", L"MOV", L"MOV", L"이동", L"移动", L"الحركة", L"ХОД", L"BEW", L"MOV", L"BEW", L"RUCH", L"HRK")
// ---------------------------------------------------------------- クラス名
R(TX_CL_SOLDIER, L"ソルジャー", L"Soldier", L"Soldat", L"Soldato", L"Soldado", L"솔저", L"士兵", L"جندي", L"Солдат", L"Soldat", L"Soldado", L"Soldaat", L"Żołnierz", L"Asker")
R(TX_CL_FIGHTER, L"ファイター", L"Fighter", L"Combattant", L"Combattente", L"Luchador", L"파이터", L"斗士", L"مقاتل", L"Боец", L"Kämpfer", L"Lutador", L"Vechter", L"Wojownik", L"Savaşçı")
R(TX_CL_ARCHER, L"アーチャー", L"Archer", L"Archer", L"Arciere", L"Arquero", L"아처", L"弓手", L"رامٍ", L"Стрелок", L"Schütze", L"Arqueiro", L"Boogschutter", L"Łucznik", L"Okçu")
R(TX_CL_THIEF, L"シーフ", L"Thief", L"Voleur", L"Ladro", L"Ladrón", L"시프", L"盗贼", L"لص", L"Вор", L"Dieb", L"Ladrão", L"Dief", L"Złodziej", L"Hırsız")
R(TX_CL_MAGE, L"メイジ", L"Mage", L"Mage", L"Mago", L"Mago", L"메이지", L"法师", L"ساحر", L"Маг", L"Magier", L"Mago", L"Magiër", L"Mag", L"Büyücü")
R(TX_CL_CLERIC, L"クレリック", L"Cleric", L"Clerc", L"Chierico", L"Clérigo", L"클레릭", L"牧师", L"كاهن", L"Клирик", L"Kleriker", L"Clérigo", L"Geestelijke", L"Kapłan", L"Rahip")
R(TX_CL_PALADIN, L"パラディン", L"Paladin", L"Paladin", L"Paladino", L"Paladín", L"팔라딘", L"圣骑士", L"فارس مقدس", L"Паладин", L"Paladin", L"Paladino", L"Paladijn", L"Paladyn", L"Paladin")
R(TX_CL_RUNEKNIGHT, L"ルーンナイト", L"Rune Knight", L"Chevalier runique", L"Cavaliere runico", L"Caballero rúnico", L"룬 나이트", L"符文骑士", L"فارس الرونية", L"Рунический рыцарь", L"Runenritter", L"Cavaleiro rúnico", L"Runenridder", L"Rycerz run", L"Rün şövalyesi")
R(TX_CL_BERSERKER, L"バーサーカー", L"Berserker", L"Berserker", L"Berserker", L"Berserker", L"버서커", L"狂战士", L"المحارب الهائج", L"Берсерк", L"Berserker", L"Berserker", L"Berserker", L"Berserker", L"Berserker")
R(TX_CL_SNIPER, L"スナイパー", L"Sniper", L"Tireur d'élite", L"Cecchino", L"Francotirador", L"스나이퍼", L"狙击手", L"قناص", L"Снайпер", L"Scharfschütze", L"Atirador", L"Sluipschutter", L"Snajper", L"Keskin nişancı")
R(TX_CL_ASSASSIN, L"アサシン", L"Assassin", L"Assassin", L"Assassino", L"Asesino", L"어새신", L"刺客", L"قاتل مأجور", L"Ассасин", L"Assassine", L"Assassino", L"Sluipmoordenaar", L"Skrytobójca", L"Suikastçı")
R(TX_CL_WIZARD, L"ウィザード", L"Wizard", L"Sorcier", L"Stregone", L"Hechicero", L"위자드", L"巫师", L"مشعوذ", L"Волшебник", L"Zauberer", L"Feiticeiro", L"Tovenaar", L"Czarodziej", L"Sihirbaz")
R(TX_CL_BISHOP, L"ビショップ", L"Bishop", L"Évêque", L"Vescovo", L"Obispo", L"비숍", L"主教", L"أسقف", L"Епископ", L"Bischof", L"Bispo", L"Bisschop", L"Biskup", L"Piskopos")
R(TX_CL_SKYKNIGHT, L"スカイナイト", L"Sky Knight", L"Chevalier céleste", L"Cavaliere celeste", L"Caballero celeste", L"스카이 나이트", L"天空骑士", L"فارس السماء", L"Небесный рыцарь", L"Himmelsritter", L"Cavaleiro celeste", L"Hemelridder", L"Rycerz niebios", L"Gök şövalyesi")
R(TX_CL_HERO, L"ヒーロー", L"Hero", L"Héros", L"Eroe", L"Héroe", L"히어로", L"勇者", L"بطل", L"Герой", L"Held", L"Herói", L"Held", L"Bohater", L"Kahraman")
R(TX_CL_ARCHMAGE, L"アークメイジ", L"Archmage", L"Archimage", L"Arcimago", L"Archimago", L"아크메이지", L"大法师", L"كبير السحرة", L"Архимаг", L"Erzmagier", L"Arquimago", L"Aartsmagiër", L"Arcymag", L"Başbüyücü")
R(TX_CL_SAINT, L"セイント", L"Saint", L"Saint", L"Santo", L"Santo", L"세인트", L"圣者", L"قديس", L"Святой", L"Heiliger", L"Santo", L"Heilige", L"Święty", L"Aziz")
R(TX_CL_PHANTOM, L"ファントム", L"Phantom", L"Fantôme", L"Fantasma", L"Fantasma", L"팬텀", L"幻影", L"الشبح", L"Фантом", L"Phantom", L"Fantasma", L"Fantoom", L"Fantom", L"Hayalet")
R(TX_CL_DRAGONLORD, L"ドラゴンロード", L"Dragon Lord", L"Seigneur dragon", L"Signore dei draghi", L"Señor dragón", L"드래곤 로드", L"龙王", L"سيد التنين", L"Повелитель драконов", L"Drachenfürst", L"Senhor dragão", L"Drakenheer", L"Władca smoków", L"Ejder lordu")
// ---------------------------------------------------------------- クラス説明
R(TX_CLD_SOLDIER,
	L"剣と盾を持つ基本の戦士。攻守のバランスが良い。",
	L"A basic warrior with sword and shield. Well balanced in attack and defense.",
	L"Guerrier de base avec épée et bouclier. Équilibré en attaque et en défense.",
	L"Guerriero di base con spada e scudo. Equilibrato in attacco e difesa.",
	L"Guerrero básico con espada y escudo. Equilibrado en ataque y defensa.",
	L"검과 방패를 든 기본 전사. 공격과 수비의 균형이 좋다.",
	L"手持剑与盾的基础战士。攻守平衡。",
	L"محارب أساسي بسيف ودرع. متوازن في الهجوم والدفاع.",
	L"Базовый воин с мечом и щитом. Сбалансирован в атаке и защите.",
	L"Grundkrieger mit Schwert und Schild. Ausgewogen in Angriff und Verteidigung.",
	L"Guerreiro básico com espada e escudo. Equilibrado no ataque e na defesa.",
	L"Basiskrijger met zwaard en schild. Evenwichtig in aanval en verdediging.",
	L"Podstawowy wojownik z mieczem i tarczą. Zrównoważony w ataku i obronie.",
	L"Kılıç ve kalkanlı temel savaşçı. Saldırı ve savunmada dengelidir.")
R(TX_CLD_FIGHTER,
	L"両手斧を振るう力自慢。HPと力が高い。",
	L"A brute wielding a two-handed axe. High HP and strength.",
	L"Une brute maniant une hache à deux mains. PV et force élevés.",
	L"Un bruto con un'ascia a due mani. PS e forza elevati.",
	L"Un bruto con un hacha a dos manos. PV y fuerza altos.",
	L"양손 도끼를 휘두르는 힘센 전사. HP와 힘이 높다.",
	L"挥舞双手斧的大力士。HP和力量很高。",
	L"محارب قوي يحمل فأسًا بكلتا يديه. صحة وقوة عاليتان.",
	L"Силач с двуручным топором. Много ОЗ и силы.",
	L"Ein Kraftprotz mit Zweihandaxt. Hohe LP und Stärke.",
	L"Um brutamontes com machado de duas mãos. PV e força altos.",
	L"Een krachtpatser met een tweehandige bijl. Veel LP en kracht.",
	L"Siłacz z dwuręcznym toporem. Dużo PŻ i siły.",
	L"İki elli balta kullanan güçlü dövüşçü. Yüksek CP ve güç.")
R(TX_CLD_ARCHER,
	L"クロスボウで離れた敵を狙う。隣接した敵には攻撃できない。",
	L"Shoots distant foes with a crossbow. Cannot attack adjacent enemies.",
	L"Tire sur les ennemis éloignés à l'arbalète. Ne peut pas attaquer au contact.",
	L"Colpisce i nemici lontani con la balestra. Non può attaccare chi è adiacente.",
	L"Dispara a enemigos lejanos con ballesta. No puede atacar a enemigos adyacentes.",
	L"석궁으로 멀리 있는 적을 노린다. 인접한 적은 공격할 수 없다.",
	L"用弩射击远处的敌人。无法攻击相邻的敌人。",
	L"يرمي الأعداء البعيدين بالقوس. لا يستطيع مهاجمة الأعداء المجاورين.",
	L"Стреляет из арбалета по дальним целям. Не может атаковать соседних врагов.",
	L"Beschießt entfernte Feinde mit der Armbrust. Kann keine angrenzenden Gegner angreifen.",
	L"Atira em inimigos distantes com a besta. Não ataca inimigos adjacentes.",
	L"Beschiet verre vijanden met een kruisboog. Kan geen aangrenzende vijanden aanvallen.",
	L"Strzela z kuszy do odległych wrogów. Nie atakuje sąsiadujących przeciwników.",
	L"Arbaletle uzaktaki düşmanları vurur. Bitişik düşmana saldıramaz.")
R(TX_CLD_THIEF,
	L"身軽な短剣使い。素早さと移動力に優れる。",
	L"A nimble dagger user. Excels in agility and movement.",
	L"Un manieur de dagues agile. Excelle en agilité et en déplacement.",
	L"Un agile utilizzatore di pugnali. Eccelle in agilità e movimento.",
	L"Un ágil usuario de dagas. Destaca en agilidad y movimiento.",
	L"몸이 가벼운 단검 사용자. 민첩과 이동력이 뛰어나다.",
	L"身手敏捷的短剑使。敏捷和移动力出众。",
	L"مستخدم خناجر رشيق. يتفوق في الرشاقة والحركة.",
	L"Ловкий мастер кинжалов. Отличается ловкостью и подвижностью.",
	L"Ein flinker Dolchkämpfer. Glänzt mit Gewandtheit und Bewegung.",
	L"Um ágil usuário de adagas. Destaca-se em agilidade e movimento.",
	L"Een lenige dolkvechter. Blinkt uit in behendigheid en beweging.",
	L"Zwinny mistrz sztyletów. Wyróżnia się zwinnością i ruchem.",
	L"Çevik bir hançer ustası. Çeviklik ve harekette üstündür.")
R(TX_CLD_MAGE,
	L"攻撃魔法を操る。魔力は高いが打たれ弱い。",
	L"Wields attack magic. High magic, but fragile.",
	L"Maîtrise la magie offensive. Magie élevée mais fragile.",
	L"Usa la magia offensiva. Magia alta ma fragile.",
	L"Domina la magia ofensiva. Magia alta, pero frágil.",
	L"공격 마법을 다룬다. 마력은 높지만 맷집이 약하다.",
	L"操纵攻击魔法。魔力高但很脆弱。",
	L"يستخدم سحر الهجوم. سحره عالٍ لكنه هش.",
	L"Владеет атакующей магией. Сильная магия, но хрупкое тело.",
	L"Beherrscht Angriffsmagie. Hohe Magie, aber zerbrechlich.",
	L"Usa magia de ataque. Magia alta, mas frágil.",
	L"Beheerst aanvalsmagie. Hoge magie, maar kwetsbaar.",
	L"Włada magią ofensywną. Wysoka magia, ale kruche ciało.",
	L"Saldırı büyüsü kullanır. Büyüsü yüksek ama kırılgandır.")
R(TX_CLD_CLERIC,
	L"回復と補助の魔法で仲間を支える。",
	L"Supports allies with healing and support magic.",
	L"Soutient ses alliés par la magie de soin et de soutien.",
	L"Sostiene gli alleati con magie di cura e supporto.",
	L"Apoya a sus aliados con magia de curación y apoyo.",
	L"회복과 보조 마법으로 동료를 돕는다.",
	L"用恢复与辅助魔法支援同伴。",
	L"يدعم الحلفاء بسحر الشفاء والدعم.",
	L"Поддерживает союзников лечебной и вспомогательной магией.",
	L"Unterstützt Verbündete mit Heil- und Hilfsmagie.",
	L"Apoia os aliados com magia de cura e suporte.",
	L"Ondersteunt bondgenoten met genezings- en hulpmagie.",
	L"Wspiera sojuszników magią leczenia i wsparcia.",
	L"Müttefiklerini iyileştirme ve destek büyüleriyle korur.")
R(TX_CLD_PALADIN,
	L"聖なる力を宿す重騎士。守りの要となる。",
	L"A heavy knight blessed with holy power. The backbone of your defense.",
	L"Chevalier lourd béni d'une force sacrée. Pilier de la défense.",
	L"Cavaliere pesante dotato di potere sacro. Il pilastro della difesa.",
	L"Caballero pesado con poder sagrado. El pilar de la defensa.",
	L"성스러운 힘을 지닌 중기사. 수비의 핵심이다.",
	L"拥有神圣之力的重骑士。防守的核心。",
	L"فارس ثقيل مبارك بقوة مقدسة. عماد الدفاع.",
	L"Тяжёлый рыцарь со святой силой. Опора обороны.",
	L"Schwerer Ritter mit heiliger Kraft. Das Rückgrat der Verteidigung.",
	L"Cavaleiro pesado com poder sagrado. O pilar da defesa.",
	L"Zware ridder met heilige kracht. De ruggengraat van de verdediging.",
	L"Ciężki rycerz obdarzony świętą mocą. Filar obrony.",
	L"Kutsal güce sahip ağır şövalye. Savunmanın bel kemiği.")
R(TX_CLD_RUNEKNIGHT,
	L"剣と魔法を両方使いこなす魔法剣士。",
	L"A spellblade who masters both sword and magic.",
	L"Une lame-sort qui maîtrise l'épée et la magie.",
	L"Uno spadaccino magico che padroneggia spada e magia.",
	L"Un espadachín mágico que domina espada y magia.",
	L"검과 마법을 모두 다루는 마법 검사.",
	L"剑与魔法兼修的魔法剑士。",
	L"مبارز سحري يتقن السيف والسحر معًا.",
	L"Чародей-мечник, владеющий и клинком, и магией.",
	L"Ein Zauberklinge, die Schwert und Magie beherrscht.",
	L"Um espadachim mágico que domina espada e magia.",
	L"Een magiezwaardvechter die zwaard en magie beheerst.",
	L"Magiczny szermierz władający mieczem i magią.",
	L"Hem kılıca hem büyüye hâkim büyülü kılıç ustası.")
R(TX_CLD_BERSERKER,
	L"二丁の斧で暴れまわる狂戦士。攻撃力は随一。",
	L"A raging warrior with twin axes. Unmatched attack power.",
	L"Un guerrier furieux aux deux haches. Puissance d'attaque inégalée.",
	L"Un guerriero furioso con due asce. Potenza d'attacco senza pari.",
	L"Un guerrero furioso con dos hachas. Poder de ataque inigualable.",
	L"쌍도끼로 날뛰는 광전사. 공격력은 최고.",
	L"挥舞双斧的狂战士。攻击力首屈一指。",
	L"محارب هائج بفأسين. قوة هجوم لا مثيل لها.",
	L"Неистовый воин с двумя топорами. Непревзойдённая атака.",
	L"Ein rasender Krieger mit zwei Äxten. Unerreichte Angriffskraft.",
	L"Um guerreiro furioso com dois machados. Ataque incomparável.",
	L"Een razende krijger met twee bijlen. Ongeëvenaarde aanvalskracht.",
	L"Szalony wojownik z dwoma toporami. Niezrównana siła ataku.",
	L"İki baltayla saldıran çılgın savaşçı. Saldırı gücü eşsizdir.")
R(TX_CLD_SNIPER,
	L"長射程のクロスボウで遠くの敵を射抜く。",
	L"Picks off distant enemies with a long-range crossbow.",
	L"Abat les ennemis lointains avec une arbalète à longue portée.",
	L"Abbatte i nemici lontani con una balestra a lunga gittata.",
	L"Abate enemigos lejanos con una ballesta de largo alcance.",
	L"장거리 석궁으로 먼 적을 꿰뚫는다.",
	L"用远程弩贯穿远方的敌人。",
	L"يصيب الأعداء البعيدين بقوس طويل المدى.",
	L"Поражает дальних врагов дальнобойным арбалетом.",
	L"Erledigt ferne Feinde mit einer weitreichenden Armbrust.",
	L"Abate inimigos distantes com uma besta de longo alcance.",
	L"Schakelt verre vijanden uit met een kruisboog met groot bereik.",
	L"Eliminuje odległych wrogów kuszą dalekiego zasięgu.",
	L"Uzun menzilli arbaletle uzaktaki düşmanları avlar.")
R(TX_CLD_ASSASSIN,
	L"一撃で仕留める影の刺客。必殺の一撃が得意。",
	L"A shadow killer who strikes to kill in one blow.",
	L"Un tueur de l'ombre qui frappe pour tuer d'un seul coup.",
	L"Un sicario dell'ombra che uccide con un solo colpo.",
	L"Un asesino de las sombras que mata de un solo golpe.",
	L"일격에 끝내는 그림자 자객. 필살의 일격이 특기.",
	L"一击毙命的暗影刺客。擅长必杀一击。",
	L"قاتل من الظل يقضي على عدوه بضربة واحدة.",
	L"Убийца из тени, разящий одним ударом.",
	L"Ein Schattenmörder, der mit einem Schlag tötet.",
	L"Um assassino das sombras que mata com um só golpe.",
	L"Een schaduwmoordenaar die met één slag doodt.",
	L"Zabójca z cienia, który zabija jednym ciosem.",
	L"Tek darbede öldüren gölge suikastçısı.")
R(TX_CLD_WIZARD,
	L"強力な範囲魔法を操る上級魔術師。",
	L"An advanced caster who wields powerful area magic.",
	L"Un lanceur avancé qui maîtrise une puissante magie de zone.",
	L"Un incantatore avanzato con potenti magie ad area.",
	L"Un hechicero avanzado con poderosa magia de área.",
	L"강력한 범위 마법을 다루는 상급 마술사.",
	L"操纵强力范围魔法的高级魔术师。",
	L"ساحر متقدم يستخدم سحرًا قويًا واسع النطاق.",
	L"Опытный чародей с мощной магией по площади.",
	L"Ein fortgeschrittener Magier mit mächtiger Flächenmagie.",
	L"Um mago avançado com poderosa magia em área.",
	L"Een gevorderde magiër met krachtige gebiedsmagie.",
	L"Zaawansowany czarodziej władający potężną magią obszarową.",
	L"Güçlü alan büyüleri kullanan ileri düzey büyücü.")
R(TX_CLD_BISHOP,
	L"高位の回復魔法と聖なる攻撃魔法を使う。",
	L"Uses high-level healing and holy attack magic.",
	L"Utilise des soins avancés et une magie sacrée offensive.",
	L"Usa cure avanzate e magia sacra offensiva.",
	L"Usa curación avanzada y magia sagrada ofensiva.",
	L"고위 회복 마법과 성스러운 공격 마법을 쓴다.",
	L"使用高级恢复魔法与神圣攻击魔法。",
	L"يستخدم سحر شفاء متقدمًا وسحرًا مقدسًا هجوميًا.",
	L"Владеет высшим исцелением и святой атакующей магией.",
	L"Nutzt hohe Heilmagie und heilige Angriffsmagie.",
	L"Usa cura avançada e magia sagrada de ataque.",
	L"Gebruikt hoge genezingsmagie en heilige aanvalsmagie.",
	L"Używa wyższej magii leczenia i świętej magii ataku.",
	L"Üst düzey iyileştirme ve kutsal saldırı büyüsü kullanır.")
R(TX_CLD_SKYKNIGHT,
	L"翼で空を駆ける騎士。地形を無視して移動できる（山は越えられない）。",
	L"A winged knight. Ignores terrain when moving (except mountains).",
	L"Chevalier ailé. Ignore le terrain en se déplaçant (sauf les montagnes).",
	L"Cavaliere alato. Ignora il terreno nel movimento (tranne le montagne).",
	L"Caballero alado. Ignora el terreno al moverse (salvo montañas).",
	L"날개로 하늘을 나는 기사. 지형을 무시하고 이동한다 (산은 불가).",
	L"以羽翼翱翔的骑士。移动时无视地形（山地除外）。",
	L"فارس مجنح. يتجاهل التضاريس عند الحركة (عدا الجبال).",
	L"Крылатый рыцарь. Не зависит от местности (кроме гор).",
	L"Geflügelter Ritter. Ignoriert beim Bewegen das Gelände (außer Berge).",
	L"Cavaleiro alado. Ignora o terreno ao se mover (exceto montanhas).",
	L"Gevleugelde ridder. Negeert terrein bij bewegen (behalve bergen).",
	L"Skrzydlaty rycerz. Ignoruje teren podczas ruchu (oprócz gór).",
	L"Kanatlı şövalye. Hareket ederken araziyi yok sayar (dağlar hariç).")
R(TX_CLD_HERO,
	L"あらゆる面で優れた伝説の剣士。",
	L"A legendary swordsman who excels in every way.",
	L"Un épéiste légendaire qui excelle en tout.",
	L"Uno spadaccino leggendario che eccelle in tutto.",
	L"Un espadachín legendario que destaca en todo.",
	L"모든 면에서 뛰어난 전설의 검사.",
	L"各方面都出类拔萃的传说剑士。",
	L"مبارز أسطوري يتفوق في كل شيء.",
	L"Легендарный мечник, превосходный во всём.",
	L"Ein legendärer Schwertkämpfer, der in allem glänzt.",
	L"Um espadachim lendário que se destaca em tudo.",
	L"Een legendarische zwaardvechter die overal in uitblinkt.",
	L"Legendarny szermierz, doskonały pod każdym względem.",
	L"Her yönden üstün efsanevi kılıç ustası.")
R(TX_CLD_ARCHMAGE,
	L"魔術を極めた者。隕石すら呼び寄せる。",
	L"A master of the arcane who can even call down meteors.",
	L"Un maître des arcanes capable d'invoquer des météores.",
	L"Un maestro dell'arcano capace di evocare meteore.",
	L"Un maestro de lo arcano capaz de invocar meteoros.",
	L"마술을 극한 자. 운석마저 불러낸다.",
	L"魔术的极致者，甚至能召唤陨石。",
	L"سيد الفنون الغامضة، يستطيع استدعاء النيازك.",
	L"Мастер тайных искусств, способный призвать метеоры.",
	L"Ein Meister der Arkanen, der sogar Meteore herbeiruft.",
	L"Um mestre do arcano capaz de invocar meteoros.",
	L"Een meester van het arcane die zelfs meteoren oproept.",
	L"Mistrz tajemnej magii, zdolny przywołać meteory.",
	L"Gizemli sanatların ustası; meteor bile çağırır.")
R(TX_CLD_SAINT,
	L"奇跡の癒しをもたらす聖者。",
	L"A saint who brings miraculous healing.",
	L"Un saint qui apporte des guérisons miraculeuses.",
	L"Un santo che dona guarigioni miracolose.",
	L"Un santo que trae curaciones milagrosas.",
	L"기적의 치유를 내리는 성자.",
	L"带来奇迹治愈的圣者。",
	L"قديس يجلب شفاءً معجزًا.",
	L"Святой, дарующий чудесное исцеление.",
	L"Ein Heiliger, der wundersame Heilung bringt.",
	L"Um santo que traz curas milagrosas.",
	L"Een heilige die wonderbaarlijke genezing brengt.",
	L"Święty przynoszący cudowne uzdrowienie.",
	L"Mucizevi şifa getiren aziz.")
R(TX_CLD_PHANTOM,
	L"幻影のように動く投げナイフの達人。",
	L"A throwing-knife master who moves like a phantom.",
	L"Un maître du couteau de lancer qui se déplace comme un fantôme.",
	L"Un maestro dei coltelli da lancio che si muove come un fantasma.",
	L"Un maestro de cuchillos arrojadizos que se mueve como un fantasma.",
	L"환영처럼 움직이는 투척 나이프의 달인.",
	L"如幻影般行动的飞刀高手。",
	L"سيد السكاكين المرمية، يتحرك كالشبح.",
	L"Мастер метательных ножей, движущийся как призрак.",
	L"Ein Wurfmesser-Meister, der sich wie ein Phantom bewegt.",
	L"Um mestre das facas de arremesso que se move como um fantasma.",
	L"Een meester met werpmessen die beweegt als een fantoom.",
	L"Mistrz noży do rzucania, poruszający się jak widmo.",
	L"Hayalet gibi hareket eden fırlatma bıçağı ustası.")
R(TX_CLD_DRAGONLORD,
	L"竜の翼を得た最強の騎士。空を飛んで移動する。",
	L"The mightiest knight, granted dragon wings. Flies over terrain.",
	L"Le plus puissant des chevaliers, doté d'ailes de dragon. Vole au-dessus du terrain.",
	L"Il cavaliere più potente, con ali di drago. Vola sopra il terreno.",
	L"El caballero más poderoso, con alas de dragón. Vuela sobre el terreno.",
	L"용의 날개를 얻은 최강의 기사. 하늘을 날아 이동한다.",
	L"获得龙翼的最强骑士。可飞行移动。",
	L"أقوى الفرسان، نال أجنحة التنين. يطير فوق التضاريس.",
	L"Сильнейший рыцарь с крыльями дракона. Летает над местностью.",
	L"Der mächtigste Ritter mit Drachenflügeln. Fliegt über das Gelände.",
	L"O cavaleiro mais poderoso, com asas de dragão. Voa sobre o terreno.",
	L"De machtigste ridder, met drakenvleugels. Vliegt over het terrein.",
	L"Najpotężniejszy rycerz ze skrzydłami smoka. Lata nad terenem.",
	L"Ejder kanatları kazanmış en güçlü şövalye. Arazinin üstünden uçar.")
// ---------------------------------------------------------------- スキル名
R(TX_SK_POWER, L"強撃", L"Power Strike", L"Coup puissant", L"Colpo potente", L"Golpe poderoso", L"강격", L"强击", L"ضربة قوية", L"Мощный удар", L"Kraftschlag", L"Golpe poderoso", L"Krachtslag", L"Potężny cios", L"Güçlü darbe")
R(TX_SK_DOUBLE, L"二連撃", L"Double Strike", L"Double frappe", L"Doppio colpo", L"Doble golpe", L"이연격", L"二连击", L"ضربة مزدوجة", L"Двойной удар", L"Doppelschlag", L"Golpe duplo", L"Dubbele slag", L"Podwójny cios", L"Çift darbe")
R(TX_SK_WHIRL, L"旋風斬", L"Whirlwind", L"Tourbillon", L"Turbine", L"Torbellino", L"선풍참", L"旋风斩", L"الإعصار", L"Вихрь", L"Wirbelwind", L"Redemoinho", L"Wervelwind", L"Trąba powietrzna", L"Kasırga")
R(TX_SK_PIERCE, L"鎧通し", L"Armor Pierce", L"Perce-armure", L"Perforarmatura", L"Perforar armadura", L"갑옷 꿰뚫기", L"破甲击", L"خارق الدروع", L"Бронебой", L"Rüstungsbrecher", L"Perfura-armadura", L"Pantserbreker", L"Przebicie pancerza", L"Zırh delici")
R(TX_SK_STITCH, L"影縫い", L"Shadow Stitch", L"Couture d'ombre", L"Cucitura d'ombra", L"Costura sombría", L"그림자 꿰매기", L"影缝", L"خياطة الظل", L"Теневой шов", L"Schattennaht", L"Costura sombria", L"Schaduwsteek", L"Szew cienia", L"Gölge dikişi")
R(TX_SK_VITAL, L"急所突き", L"Vital Strike", L"Coup vital", L"Colpo vitale", L"Golpe vital", L"급소 찌르기", L"要害突刺", L"ضربة قاتلة", L"Удар в уязвимость", L"Vitalstoß", L"Golpe vital", L"Vitale steek", L"Cios w czuły punkt", L"Hayati darbe")
R(TX_SK_BASH, L"シールドバッシュ", L"Shield Bash", L"Coup de bouclier", L"Colpo di scudo", L"Golpe de escudo", L"실드 배시", L"盾击", L"ضربة الدرع", L"Удар щитом", L"Schildschlag", L"Golpe de escudo", L"Schildslag", L"Uderzenie tarczą", L"Kalkan darbesi")
R(TX_SK_DRAGONFANG, L"竜牙斬", L"Dragon Fang", L"Croc du dragon", L"Zanna del drago", L"Colmillo de dragón", L"용아참", L"龙牙斩", L"ناب التنين", L"Клык дракона", L"Drachenzahn", L"Presa do dragão", L"Drakentand", L"Kieł smoka", L"Ejder dişi")
R(TX_SK_HOLYBLADE, L"聖光剣", L"Holy Blade", L"Lame sacrée", L"Lama sacra", L"Hoja sagrada", L"성광검", L"圣光剑", L"النصل المقدس", L"Святой клинок", L"Heilige Klinge", L"Lâmina sagrada", L"Heilig zwaard", L"Święte ostrze", L"Kutsal kılıç")
R(TX_SK_ARROWRAIN, L"アローレイン", L"Arrow Rain", L"Pluie de flèches", L"Pioggia di frecce", L"Lluvia de flechas", L"애로우 레인", L"箭雨", L"مطر السهام", L"Град стрел", L"Pfeilregen", L"Chuva de flechas", L"Pijlenregen", L"Deszcz strzał", L"Ok yağmuru")
R(TX_SK_ASSASSINATE, L"暗殺", L"Assassinate", L"Assassinat", L"Assassinio", L"Asesinato", L"암살", L"暗杀", L"اغتيال", L"Убийство", L"Meucheln", L"Assassinar", L"Sluipmoord", L"Zamach", L"Suikast")
R(TX_SK_BRAVE, L"ブレイブスラッシュ", L"Brave Slash", L"Taille héroïque", L"Fendente eroico", L"Tajo valiente", L"브레이브 슬래시", L"勇者斩", L"ضربة الشجاعة", L"Удар храбреца", L"Mutiger Hieb", L"Corte bravo", L"Dappere slag", L"Cięcie odwagi", L"Cesur kesiş")
R(TX_SK_FIRE, L"ファイア", L"Fire", L"Feu", L"Fuoco", L"Fuego", L"파이어", L"火焰", L"نار", L"Огонь", L"Feuer", L"Fogo", L"Vuur", L"Ogień", L"Ateş")
R(TX_SK_BLIZZARD, L"ブリザード", L"Blizzard", L"Blizzard", L"Bufera", L"Ventisca", L"블리자드", L"暴风雪", L"عاصفة ثلجية", L"Буран", L"Schneesturm", L"Nevasca", L"Sneeuwstorm", L"Zamieć", L"Tipi")
R(TX_SK_THUNDER, L"サンダー", L"Thunder", L"Foudre", L"Tuono", L"Trueno", L"썬더", L"雷击", L"صاعقة", L"Гром", L"Donner", L"Trovão", L"Donder", L"Piorun", L"Yıldırım")
R(TX_SK_METEOR, L"メテオ", L"Meteor", L"Météore", L"Meteora", L"Meteoro", L"메테오", L"陨石", L"نيزك", L"Метеор", L"Meteor", L"Meteoro", L"Meteoor", L"Meteor", L"Meteor")
R(TX_SK_RUNEBLADE, L"ルーンブレード", L"Rune Blade", L"Lame runique", L"Lama runica", L"Hoja rúnica", L"룬 블레이드", L"符文剑", L"النصل الروني", L"Рунный клинок", L"Runenklinge", L"Lâmina rúnica", L"Runenzwaard", L"Runiczne ostrze", L"Rün kılıcı")
R(TX_SK_HOLY, L"ホーリー", L"Holy", L"Sacré", L"Sacro", L"Sagrado", L"홀리", L"神圣", L"النور المقدس", L"Святость", L"Heilig", L"Sagrado", L"Heilig licht", L"Święte światło", L"Kutsal ışık")
R(TX_SK_DARKNESS, L"ダークネス", L"Darkness", L"Ténèbres", L"Oscurità", L"Oscuridad", L"다크니스", L"黑暗", L"الظلام", L"Тьма", L"Finsternis", L"Escuridão", L"Duisternis", L"Ciemność", L"Karanlık")
R(TX_SK_HEAL, L"ヒール", L"Heal", L"Soin", L"Cura", L"Curar", L"힐", L"治疗", L"شفاء", L"Лечение", L"Heilung", L"Cura", L"Genezing", L"Leczenie", L"İyileştir")
R(TX_SK_HIGHHEAL, L"ハイヒール", L"High Heal", L"Soin majeur", L"Cura superiore", L"Curación mayor", L"하이 힐", L"高级治疗", L"شفاء عالٍ", L"Сильное лечение", L"Großheilung", L"Cura maior", L"Grote genezing", L"Silne leczenie", L"Büyük iyileştirme")
R(TX_SK_HEALALL, L"オールヒール", L"Heal All", L"Soin de groupe", L"Cura di gruppo", L"Curación grupal", L"올 힐", L"全体治疗", L"شفاء جماعي", L"Массовое лечение", L"Gruppenheilung", L"Cura em grupo", L"Groepsgenezing", L"Leczenie grupowe", L"Toplu iyileştirme")
R(TX_SK_PROTECT, L"プロテクト", L"Protect", L"Protection", L"Protezione", L"Protección", L"프로텍트", L"守护", L"حماية", L"Защита", L"Schutz", L"Proteção", L"Bescherming", L"Ochrona", L"Koruma")
R(TX_SK_HASTE, L"ヘイスト", L"Haste", L"Hâte", L"Rapidità", L"Prisa", L"헤이스트", L"加速", L"تسريع", L"Ускорение", L"Hast", L"Pressa", L"Haast", L"Przyspieszenie", L"Hız")
R(TX_SK_BLESS, L"ブレス", L"Bless", L"Bénédiction", L"Benedizione", L"Bendición", L"블레스", L"祝福", L"بركة", L"Благословение", L"Segen", L"Bênção", L"Zegen", L"Błogosławieństwo", L"Kutsama")
R(TX_SK_REFRESH, L"再行動", L"Refresh", L"Second souffle", L"Rinnovo", L"Revitalizar", L"재행동", L"再行动", L"إعادة الحركة", L"Повтор хода", L"Noch einmal", L"Reanimar", L"Opnieuw", L"Ponowna tura", L"Yeniden hamle")
R(TX_SK_P_EVADE, L"見切り", L"Evasion", L"Esquive", L"Schivata", L"Evasión", L"간파", L"看破", L"المراوغة", L"Уклонение", L"Ausweichen", L"Evasão", L"Ontwijken", L"Unik", L"Kaçınma")
R(TX_SK_P_MIGHT, L"剛力", L"Might", L"Puissance", L"Vigore", L"Poderío", L"괴력", L"刚力", L"البأس", L"Мощь", L"Stärke", L"Vigor", L"Kracht", L"Krzepa", L"Kuvvet")
R(TX_SK_P_SPELL, L"魔力増幅", L"Spell Boost", L"Amplification", L"Amplificazione", L"Amplificación", L"마력 증폭", L"魔力增幅", L"تعزيز السحر", L"Усиление магии", L"Zauberverstärkung", L"Amplificação", L"Spreukversterking", L"Wzmocnienie magii", L"Büyü artırma")
R(TX_SK_P_IRON, L"鉄壁", L"Iron Wall", L"Mur de fer", L"Muro di ferro", L"Muro de hierro", L"철벽", L"铁壁", L"الجدار الحديدي", L"Железная стена", L"Eisenwall", L"Muralha de ferro", L"IJzeren muur", L"Żelazny mur", L"Demir duvar")
R(TX_SK_P_SWIFT, L"疾風", L"Swiftness", L"Célérité", L"Celerità", L"Celeridad", L"질풍", L"疾风", L"السرعة", L"Стремительность", L"Schnelligkeit", L"Celeridade", L"Snelheid", L"Chyżość", L"Çabukluk")
R(TX_SK_P_REGEN, L"再生", L"Regeneration", L"Régénération", L"Rigenerazione", L"Regeneración", L"재생", L"再生", L"التجدد", L"Регенерация", L"Regeneration", L"Regeneração", L"Regeneratie", L"Regeneracja", L"Yenilenme")
R(TX_SK_P_CRIT, L"必殺の心得", L"Critical Eye", L"Œil critique", L"Occhio critico", L"Ojo crítico", L"필살의 마음가짐", L"必杀心得", L"عين الحسم", L"Критический глаз", L"Kritisches Auge", L"Olho crítico", L"Kritisch oog", L"Krytyczne oko", L"Kritik göz")
R(TX_SK_E_VENOM, L"毒牙", L"Venom Fang", L"Croc venimeux", L"Zanna velenosa", L"Colmillo venenoso", L"독니", L"毒牙", L"ناب سام", L"Ядовитый клык", L"Giftzahn", L"Presa venenosa", L"Gifttand", L"Jadowity kieł", L"Zehirli diş")
R(TX_SK_E_BREATH, L"火炎の息", L"Fire Breath", L"Souffle de feu", L"Soffio di fuoco", L"Aliento de fuego", L"화염의 숨결", L"火焰吐息", L"نفث النار", L"Огненное дыхание", L"Feueratem", L"Sopro de fogo", L"Vuuradem", L"Ognisty oddech", L"Ateş nefesi")
R(TX_SK_E_CRUSH, L"押しつぶし", L"Crush", L"Écrasement", L"Schiacciamento", L"Aplastar", L"짓누르기", L"压碎", L"سحق", L"Сокрушение", L"Zermalmen", L"Esmagar", L"Verpletteren", L"Zmiażdżenie", L"Ezme")
R(TX_SK_E_DRAIN, L"吸血", L"Drain", L"Drain", L"Risucchio", L"Drenaje", L"흡혈", L"吸血", L"امتصاص", L"Высасывание", L"Aussaugen", L"Dreno", L"Leegzuigen", L"Wysysanie", L"Emme")
R(TX_SK_E_SPIT, L"粘液飛ばし", L"Slime Spit", L"Crachat visqueux", L"Sputo viscido", L"Escupitajo viscoso", L"점액 뱉기", L"喷射黏液", L"بصق اللزوجة", L"Плевок слизью", L"Schleimspucke", L"Cuspe viscoso", L"Slijmspuug", L"Plucie śluzem", L"Balçık tükürüğü")
R(TX_SK_E_QUAKE, L"地震", L"Quake", L"Séisme", L"Terremoto", L"Terremoto", L"지진", L"地震", L"زلزال", L"Землетрясение", L"Erdbeben", L"Terremoto", L"Aardbeving", L"Trzęsienie ziemi", L"Deprem")
// ---------------------------------------------------------------- スキル説明
R(TX_SKD_POWER,
	L"力を込めた一撃。威力1.5倍。",
	L"A forceful blow. 1.5x power.",
	L"Un coup puissant. Puissance x1,5.",
	L"Un colpo poderoso. Potenza x1,5.",
	L"Un golpe contundente. Potencia x1,5.",
	L"힘을 실은 일격. 위력 1.5배.",
	L"全力一击。威力1.5倍。",
	L"ضربة قوية. قوة ×1.5.",
	L"Мощный удар. Сила ×1,5.",
	L"Ein kraftvoller Hieb. 1,5-fache Stärke.",
	L"Um golpe vigoroso. Poder x1,5.",
	L"Een krachtige slag. 1,5x kracht.",
	L"Potężny cios. Siła ×1,5.",
	L"Güçlü bir darbe. 1,5 kat güç.")
R(TX_SKD_DOUBLE,
	L"素早く2回斬りつける。",
	L"Strikes twice in quick succession.",
	L"Frappe deux fois en succession rapide.",
	L"Colpisce due volte in rapida successione.",
	L"Golpea dos veces en rápida sucesión.",
	L"재빠르게 두 번 벤다.",
	L"迅速连斩两次。",
	L"يضرب مرتين بسرعة متتالية.",
	L"Быстро наносит два удара подряд.",
	L"Schlägt zweimal schnell hintereinander zu.",
	L"Golpeia duas vezes em rápida sucessão.",
	L"Slaat twee keer snel achter elkaar toe.",
	L"Uderza dwa razy w szybkim tempie.",
	L"Art arda hızla iki kez vurur.")
R(TX_SKD_WHIRL,
	L"周囲の敵をまとめて薙ぎ払う。",
	L"Sweeps away all adjacent enemies at once.",
	L"Balaye tous les ennemis adjacents d'un coup.",
	L"Spazza via tutti i nemici adiacenti in un colpo.",
	L"Barre a todos los enemigos adyacentes de una vez.",
	L"주위의 적을 한꺼번에 베어 넘긴다.",
	L"横扫周围所有敌人。",
	L"يكتسح جميع الأعداء المجاورين دفعة واحدة.",
	L"Сметает всех соседних врагов одним ударом.",
	L"Fegt alle angrenzenden Feinde auf einmal hinweg.",
	L"Varre todos os inimigos adjacentes de uma vez.",
	L"Veegt alle aangrenzende vijanden in één keer weg.",
	L"Zmiata naraz wszystkich sąsiednich wrogów.",
	L"Etraftaki tüm düşmanları tek seferde biçer.")
R(TX_SKD_PIERCE,
	L"相手の守備を半分無視する一撃。",
	L"A strike that ignores half of the target's defense.",
	L"Un coup qui ignore la moitié de la défense de la cible.",
	L"Un colpo che ignora metà della difesa del bersaglio.",
	L"Un golpe que ignora la mitad de la defensa del objetivo.",
	L"상대의 수비를 절반 무시하는 일격.",
	L"无视对方一半防御的一击。",
	L"ضربة تتجاهل نصف دفاع الهدف.",
	L"Удар, игнорирующий половину защиты цели.",
	L"Ein Stoß, der die Hälfte der Verteidigung ignoriert.",
	L"Um golpe que ignora metade da defesa do alvo.",
	L"Een slag die de helft van de verdediging negeert.",
	L"Cios ignorujący połowę obrony celu.",
	L"Hedefin savunmasının yarısını yok sayan darbe.")
R(TX_SKD_STITCH,
	L"影を縫い止め、相手の行動を遅らせる。",
	L"Pins the target's shadow, delaying its next turn.",
	L"Cloue l'ombre de la cible et retarde son prochain tour.",
	L"Inchioda l'ombra del bersaglio e ritarda il suo turno.",
	L"Clava la sombra del objetivo y retrasa su turno.",
	L"그림자를 꿰매어 상대의 행동을 늦춘다.",
	L"缝住影子，延迟对方的行动。",
	L"يثبّت ظل الهدف ويؤخر دوره التالي.",
	L"Пришивает тень цели и задерживает её ход.",
	L"Heftet den Schatten fest und verzögert den nächsten Zug.",
	L"Prende a sombra do alvo e atrasa seu próximo turno.",
	L"Speldt de schaduw vast en vertraagt de volgende beurt.",
	L"Przyszywa cień celu i opóźnia jego turę.",
	L"Hedefin gölgesini çiviler ve sırasını geciktirir.")
R(TX_SKD_VITAL,
	L"急所を狙う。必殺率が大きく上がる。",
	L"Aims for a weak spot. Greatly raises critical rate.",
	L"Vise un point faible. Augmente fortement le taux critique.",
	L"Mira a un punto debole. Aumenta molto la probabilità di critico.",
	L"Apunta a un punto débil. Aumenta mucho la probabilidad de crítico.",
	L"급소를 노린다. 필살률이 크게 오른다.",
	L"瞄准要害。大幅提升必杀率。",
	L"يستهدف نقطة ضعف. يرفع نسبة الضربة الحاسمة كثيرًا.",
	L"Бьёт в уязвимое место. Сильно повышает шанс крита.",
	L"Zielt auf eine Schwachstelle. Erhöht die kritische Rate stark.",
	L"Mira um ponto fraco. Aumenta muito a chance de crítico.",
	L"Mikt op een zwakke plek. Verhoogt de kritieke kans sterk.",
	L"Celuje w słaby punkt. Znacznie zwiększa szansę krytyka.",
	L"Zayıf noktayı hedefler. Kritik oranını büyük ölçüde artırır.")
R(TX_SKD_BASH,
	L"盾で殴りつけ、相手の守備を3ターン下げる。",
	L"Slams with the shield, lowering defense for 3 turns.",
	L"Frappe avec le bouclier et baisse la défense pendant 3 tours.",
	L"Colpisce con lo scudo e riduce la difesa per 3 turni.",
	L"Golpea con el escudo y baja la defensa durante 3 turnos.",
	L"방패로 내리쳐 상대의 수비를 3턴 낮춘다.",
	L"用盾猛击，降低对方防御3回合。",
	L"يضرب بالدرع ويخفض الدفاع لمدة 3 أدوار.",
	L"Удар щитом, снижает защиту на 3 хода.",
	L"Schlägt mit dem Schild zu und senkt die Verteidigung für 3 Runden.",
	L"Golpeia com o escudo e reduz a defesa por 3 turnos.",
	L"Slaat met het schild en verlaagt de verdediging 3 beurten.",
	L"Uderza tarczą i obniża obronę na 3 tury.",
	L"Kalkanla vurur, savunmayı 3 tur düşürür.")
R(TX_SKD_DRAGONFANG,
	L"竜の炎をまとう強烈な斬撃。",
	L"A fierce slash wreathed in dragon fire.",
	L"Une taille féroce enveloppée de feu de dragon.",
	L"Un fendente feroce avvolto dal fuoco del drago.",
	L"Un tajo feroz envuelto en fuego de dragón.",
	L"용의 불꽃을 두른 강렬한 참격.",
	L"缠绕龙炎的猛烈斩击。",
	L"ضربة شرسة ملفوفة بنار التنين.",
	L"Свирепый удар в драконьем пламени.",
	L"Ein wilder Hieb, umhüllt von Drachenfeuer.",
	L"Um corte feroz envolto em fogo de dragão.",
	L"Een woeste slag gehuld in drakenvuur.",
	L"Zaciekłe cięcie spowite smoczym ogniem.",
	L"Ejder ateşine sarılı şiddetli bir kesiş.")
R(TX_SKD_HOLYBLADE,
	L"聖なる光を宿した剣技。",
	L"A sword technique imbued with holy light.",
	L"Une technique d'épée imprégnée de lumière sacrée.",
	L"Una tecnica di spada infusa di luce sacra.",
	L"Una técnica de espada imbuida de luz sagrada.",
	L"성스러운 빛을 깃들인 검기.",
	L"蕴含神圣之光的剑技。",
	L"تقنية سيف مشبعة بالنور المقدس.",
	L"Приём меча, наполненный святым светом.",
	L"Eine Schwerttechnik voll heiligen Lichts.",
	L"Uma técnica de espada imbuída de luz sagrada.",
	L"Een zwaardtechniek vol heilig licht.",
	L"Technika miecza nasycona świętym światłem.",
	L"Kutsal ışıkla dolu bir kılıç tekniği.")
R(TX_SKD_ARROWRAIN,
	L"指定した範囲に矢の雨を降らせる。",
	L"Rains arrows down on the chosen area.",
	L"Fait pleuvoir des flèches sur la zone choisie.",
	L"Fa piovere frecce sull'area scelta.",
	L"Hace llover flechas sobre la zona elegida.",
	L"지정한 범위에 화살비를 내린다.",
	L"向指定范围降下箭雨。",
	L"يمطر السهام على المنطقة المختارة.",
	L"Обрушивает град стрел на выбранную область.",
	L"Lässt Pfeile auf das gewählte Gebiet regnen.",
	L"Faz chover flechas na área escolhida.",
	L"Laat pijlen regenen op het gekozen gebied.",
	L"Zsyła deszcz strzał na wybrany obszar.",
	L"Seçilen alana ok yağdırır.")
R(TX_SKD_ASSASSINATE,
	L"一撃で仕留める暗殺術。威力と必殺率が高い。",
	L"An assassin's art. High power and critical rate.",
	L"Art de l'assassin. Puissance et taux critique élevés.",
	L"Arte dell'assassino. Potenza e critico elevati.",
	L"Arte del asesino. Gran potencia y probabilidad de crítico.",
	L"일격에 끝내는 암살술. 위력과 필살률이 높다.",
	L"一击毙命的暗杀术。威力和必杀率都很高。",
	L"فن الاغتيال. قوة عالية ونسبة حسم عالية.",
	L"Искусство убийцы. Высокая сила и шанс крита.",
	L"Die Kunst des Meuchelns. Hohe Stärke und kritische Rate.",
	L"A arte do assassino. Alto poder e chance de crítico.",
	L"Sluipmoordkunst. Hoge kracht en kritieke kans.",
	L"Sztuka zabójcy. Wysoka siła i szansa krytyka.",
	L"Suikast sanatı. Yüksek güç ve kritik oranı.")
R(TX_SKD_BRAVE,
	L"勇者だけが放てる渾身の一撃。",
	L"An all-out strike only a true hero can unleash.",
	L"Une frappe totale que seul un vrai héros peut déchaîner.",
	L"Un colpo totale che solo un vero eroe può scatenare.",
	L"Un golpe total que solo un verdadero héroe puede desatar.",
	L"용사만이 날릴 수 있는 혼신의 일격.",
	L"只有勇者才能发出的全力一击。",
	L"ضربة كاملة لا يطلقها إلا بطل حقيقي.",
	L"Удар изо всех сил, доступный лишь истинному герою.",
	L"Ein Schlag mit aller Kraft, den nur ein wahrer Held entfesselt.",
	L"Um golpe total que só um verdadeiro herói pode desferir.",
	L"Een slag met volle kracht die alleen een echte held kan geven.",
	L"Cios z całych sił, dostępny tylko prawdziwemu bohaterowi.",
	L"Yalnızca gerçek bir kahramanın yapabileceği tam güç darbesi.")
R(TX_SKD_FIRE,
	L"炎の魔法で敵1体を攻撃する。",
	L"Attacks one enemy with fire magic.",
	L"Attaque un ennemi avec la magie du feu.",
	L"Attacca un nemico con la magia del fuoco.",
	L"Ataca a un enemigo con magia de fuego.",
	L"불꽃 마법으로 적 하나를 공격한다.",
	L"用火焰魔法攻击一名敌人。",
	L"يهاجم عدوًا واحدًا بسحر النار.",
	L"Атакует одного врага огненной магией.",
	L"Greift einen Feind mit Feuermagie an.",
	L"Ataca um inimigo com magia de fogo.",
	L"Valt één vijand aan met vuurmagie.",
	L"Atakuje jednego wroga magią ognia.",
	L"Bir düşmana ateş büyüsüyle saldırır.")
R(TX_SKD_BLIZZARD,
	L"氷の嵐で範囲内の敵を攻撃する。",
	L"Hits enemies in an area with a storm of ice.",
	L"Frappe les ennemis d'une zone par une tempête de glace.",
	L"Colpisce i nemici in un'area con una tempesta di ghiaccio.",
	L"Golpea a los enemigos de un área con una tormenta de hielo.",
	L"얼음 폭풍으로 범위 내의 적을 공격한다.",
	L"以冰之风暴攻击范围内的敌人。",
	L"يضرب الأعداء في منطقة بعاصفة جليدية.",
	L"Ледяная буря поражает врагов в области.",
	L"Trifft Feinde in einem Bereich mit einem Eissturm.",
	L"Atinge inimigos em uma área com uma tempestade de gelo.",
	L"Raakt vijanden in een gebied met een ijsstorm.",
	L"Burza lodowa rani wrogów na obszarze.",
	L"Buz fırtınasıyla alandaki düşmanlara saldırır.")
R(TX_SKD_THUNDER,
	L"雷を落とす。射程が長い。",
	L"Calls down lightning. Long range.",
	L"Fait tomber la foudre. Longue portée.",
	L"Fa cadere un fulmine. Lunga gittata.",
	L"Invoca un rayo. Largo alcance.",
	L"벼락을 떨어뜨린다. 사정거리가 길다.",
	L"降下雷电。射程很远。",
	L"يستدعي البرق. مدى طويل.",
	L"Обрушивает молнию. Большая дальность.",
	L"Ruft einen Blitz herab. Große Reichweite.",
	L"Invoca um raio. Longo alcance.",
	L"Roept bliksem op. Groot bereik.",
	L"Przywołuje piorun. Duży zasięg.",
	L"Yıldırım düşürür. Uzun menzil.")
R(TX_SKD_METEOR,
	L"隕石を降らせ、広範囲を焼き払う。",
	L"Drops meteors to scorch a wide area.",
	L"Fait tomber des météores sur une large zone.",
	L"Fa cadere meteore che bruciano un'ampia area.",
	L"Hace caer meteoros que arrasan un área amplia.",
	L"운석을 떨어뜨려 넓은 범위를 불태운다.",
	L"降下陨石，焚烧大范围区域。",
	L"يُسقط نيازك تحرق منطقة واسعة.",
	L"Обрушивает метеоры на большую область.",
	L"Lässt Meteore fallen, die ein großes Gebiet verbrennen.",
	L"Faz cair meteoros que queimam uma grande área.",
	L"Laat meteoren vallen die een groot gebied verschroeien.",
	L"Zrzuca meteory, które palą duży obszar.",
	L"Geniş bir alanı yakan meteorlar düşürür.")
R(TX_SKD_RUNEBLADE,
	L"剣に雷のルーンを宿して斬る。隣接した敵専用。",
	L"Slashes with a thunder rune on the blade. Adjacent enemies only.",
	L"Taille avec une rune de foudre sur la lame. Ennemis adjacents uniquement.",
	L"Fendente con una runa del tuono sulla lama. Solo nemici adiacenti.",
	L"Tajo con una runa de trueno en la hoja. Solo enemigos adyacentes.",
	L"검에 번개 룬을 깃들여 벤다. 인접한 적 전용.",
	L"在剑上附加雷之符文斩击。仅限相邻敌人。",
	L"يضرب بنصل عليه رون البرق. للأعداء المجاورين فقط.",
	L"Удар клинком с руной грома. Только по соседним врагам.",
	L"Hieb mit einer Donnerrune auf der Klinge. Nur angrenzende Feinde.",
	L"Corte com uma runa de trovão na lâmina. Só inimigos adjacentes.",
	L"Slag met een donderrune op het zwaard. Alleen aangrenzende vijanden.",
	L"Cięcie z runą pioruna na ostrzu. Tylko sąsiedni wrogowie.",
	L"Kılıçta yıldırım rünüyle keser. Yalnızca bitişik düşmanlar.")
R(TX_SKD_HOLY,
	L"聖なる光で敵を浄化する。",
	L"Purifies an enemy with holy light.",
	L"Purifie un ennemi avec une lumière sacrée.",
	L"Purifica un nemico con la luce sacra.",
	L"Purifica a un enemigo con luz sagrada.",
	L"성스러운 빛으로 적을 정화한다.",
	L"以神圣之光净化敌人。",
	L"يطهّر عدوًا بالنور المقدس.",
	L"Очищает врага святым светом.",
	L"Läutert einen Feind mit heiligem Licht.",
	L"Purifica um inimigo com luz sagrada.",
	L"Zuivert een vijand met heilig licht.",
	L"Oczyszcza wroga świętym światłem.",
	L"Bir düşmanı kutsal ışıkla arındırır.")
R(TX_SKD_DARKNESS,
	L"闇の力で攻撃し、守備を3ターン下げる。",
	L"Attacks with dark power and lowers defense for 3 turns.",
	L"Attaque par les ténèbres et baisse la défense pendant 3 tours.",
	L"Attacca con l'oscurità e riduce la difesa per 3 turni.",
	L"Ataca con poder oscuro y baja la defensa durante 3 turnos.",
	L"어둠의 힘으로 공격하고 수비를 3턴 낮춘다.",
	L"以黑暗之力攻击，并降低防御3回合。",
	L"يهاجم بقوة الظلام ويخفض الدفاع لمدة 3 أدوار.",
	L"Атакует силой тьмы и снижает защиту на 3 хода.",
	L"Greift mit dunkler Macht an und senkt die Verteidigung für 3 Runden.",
	L"Ataca com poder sombrio e reduz a defesa por 3 turnos.",
	L"Valt aan met duistere kracht en verlaagt de verdediging 3 beurten.",
	L"Atakuje mocą ciemności i obniża obronę na 3 tury.",
	L"Karanlık güçle saldırır, savunmayı 3 tur düşürür.")
R(TX_SKD_HEAL,
	L"味方1人のHPを回復する。",
	L"Restores HP to one ally.",
	L"Rend des PV à un allié.",
	L"Ripristina i PS di un alleato.",
	L"Restaura los PV de un aliado.",
	L"아군 한 명의 HP를 회복한다.",
	L"恢复一名同伴的HP。",
	L"يستعيد صحة حليف واحد.",
	L"Восстанавливает ОЗ одному союзнику.",
	L"Stellt die LP eines Verbündeten wieder her.",
	L"Restaura os PV de um aliado.",
	L"Herstelt de LP van één bondgenoot.",
	L"Przywraca PŻ jednemu sojusznikowi.",
	L"Bir müttefikin CP'sini yeniler.")
R(TX_SKD_HIGHHEAL,
	L"味方1人のHPを大きく回復する。",
	L"Restores a large amount of HP to one ally.",
	L"Rend beaucoup de PV à un allié.",
	L"Ripristina molti PS di un alleato.",
	L"Restaura muchos PV de un aliado.",
	L"아군 한 명의 HP를 크게 회복한다.",
	L"大幅恢复一名同伴的HP。",
	L"يستعيد قدرًا كبيرًا من صحة حليف واحد.",
	L"Восстанавливает много ОЗ одному союзнику.",
	L"Stellt viele LP eines Verbündeten wieder her.",
	L"Restaura muitos PV de um aliado.",
	L"Herstelt veel LP van één bondgenoot.",
	L"Przywraca dużo PŻ jednemu sojusznikowi.",
	L"Bir müttefikin CP'sini büyük ölçüde yeniler.")
R(TX_SKD_HEALALL,
	L"範囲内の味方全員のHPを回復する。",
	L"Restores HP to all allies in the area.",
	L"Rend des PV à tous les alliés de la zone.",
	L"Ripristina i PS di tutti gli alleati nell'area.",
	L"Restaura los PV de todos los aliados del área.",
	L"범위 내 아군 전원의 HP를 회복한다.",
	L"恢复范围内所有同伴的HP。",
	L"يستعيد صحة جميع الحلفاء في المنطقة.",
	L"Восстанавливает ОЗ всем союзникам в области.",
	L"Stellt die LP aller Verbündeten im Bereich wieder her.",
	L"Restaura os PV de todos os aliados na área.",
	L"Herstelt de LP van alle bondgenoten in het gebied.",
	L"Przywraca PŻ wszystkim sojusznikom na obszarze.",
	L"Alandaki tüm müttefiklerin CP'sini yeniler.")
R(TX_SKD_PROTECT,
	L"味方の守備を3ターン上げる。",
	L"Raises an ally's defense for 3 turns.",
	L"Augmente la défense d'un allié pendant 3 tours.",
	L"Aumenta la difesa di un alleato per 3 turni.",
	L"Aumenta la defensa de un aliado durante 3 turnos.",
	L"아군의 수비를 3턴 올린다.",
	L"提升同伴防御3回合。",
	L"يرفع دفاع حليف لمدة 3 أدوار.",
	L"Повышает защиту союзника на 3 хода.",
	L"Erhöht die Verteidigung eines Verbündeten für 3 Runden.",
	L"Aumenta a defesa de um aliado por 3 turnos.",
	L"Verhoogt de verdediging van een bondgenoot 3 beurten.",
	L"Zwiększa obronę sojusznika na 3 tury.",
	L"Bir müttefikin savunmasını 3 tur artırır.")
R(TX_SKD_HASTE,
	L"味方の素早さを3ターン上げる。",
	L"Raises an ally's agility for 3 turns.",
	L"Augmente l'agilité d'un allié pendant 3 tours.",
	L"Aumenta l'agilità di un alleato per 3 turni.",
	L"Aumenta la agilidad de un aliado durante 3 turnos.",
	L"아군의 민첩을 3턴 올린다.",
	L"提升同伴敏捷3回合。",
	L"يرفع رشاقة حليف لمدة 3 أدوار.",
	L"Повышает ловкость союзника на 3 хода.",
	L"Erhöht die Gewandtheit eines Verbündeten für 3 Runden.",
	L"Aumenta a agilidade de um aliado por 3 turnos.",
	L"Verhoogt de behendigheid van een bondgenoot 3 beurten.",
	L"Zwiększa zwinność sojusznika na 3 tury.",
	L"Bir müttefikin çevikliğini 3 tur artırır.")
R(TX_SKD_BLESS,
	L"味方の攻撃力を3ターン上げる。",
	L"Raises an ally's attack power for 3 turns.",
	L"Augmente l'attaque d'un allié pendant 3 tours.",
	L"Aumenta l'attacco di un alleato per 3 turni.",
	L"Aumenta el ataque de un aliado durante 3 turnos.",
	L"아군의 공격력을 3턴 올린다.",
	L"提升同伴攻击力3回合。",
	L"يرفع قوة هجوم حليف لمدة 3 أدوار.",
	L"Повышает атаку союзника на 3 хода.",
	L"Erhöht die Angriffskraft eines Verbündeten für 3 Runden.",
	L"Aumenta o ataque de um aliado por 3 turnos.",
	L"Verhoogt de aanvalskracht van een bondgenoot 3 beurten.",
	L"Zwiększa siłę ataku sojusznika na 3 tury.",
	L"Bir müttefikin saldırı gücünü 3 tur artırır.")
R(TX_SKD_REFRESH,
	L"行動を終えた隣接の味方のCTを戻し、すぐに再行動させる。",
	L"Restores an adjacent ally who already acted so they can move again at once.",
	L"Rend son tour à un allié adjacent qui a déjà agi.",
	L"Restituisce il turno a un alleato adiacente che ha già agito.",
	L"Devuelve el turno a un aliado adyacente que ya actuó.",
	L"이미 행동한 인접 아군의 CT를 되돌려 바로 다시 움직이게 한다.",
	L"让已经行动过的相邻同伴立即再次行动。",
	L"يعيد الحركة لحليف مجاور أنهى دوره ليتحرك فورًا.",
	L"Возвращает ход соседнему союзнику, который уже действовал.",
	L"Gibt einem benachbarten Verbündeten, der schon handelte, sofort wieder einen Zug.",
	L"Devolve o turno a um aliado adjacente que já agiu.",
	L"Geeft een aangrenzende bondgenoot die al handelde meteen opnieuw een beurt.",
	L"Przywraca turę sąsiedniemu sojusznikowi, który już działał.",
	L"Hareketini bitirmiş bitişik müttefiğe hemen yeniden sıra verir.")
R(TX_SKD_P_EVADE,
	L"回避率+15。",
	L"Evasion +15.",
	L"Esquive +15.",
	L"Schivata +15.",
	L"Evasión +15.",
	L"회피율 +15.",
	L"回避率+15。",
	L"المراوغة +15.",
	L"Уклонение +15.",
	L"Ausweichen +15.",
	L"Evasão +15.",
	L"Ontwijken +15.",
	L"Unik +15.",
	L"Kaçınma +15.")
R(TX_SKD_P_MIGHT,
	L"物理攻撃力+15%。",
	L"Physical attack +15%.",
	L"Attaque physique +15 %.",
	L"Attacco fisico +15%.",
	L"Ataque físico +15 %.",
	L"물리 공격력 +15%.",
	L"物理攻击力+15%。",
	L"الهجوم الجسدي +15٪.",
	L"Физическая атака +15%.",
	L"Physischer Angriff +15 %.",
	L"Ataque físico +15%.",
	L"Fysieke aanval +15%.",
	L"Atak fizyczny +15%.",
	L"Fiziksel saldırı +%15.")
R(TX_SKD_P_SPELL,
	L"魔法攻撃力+15%。",
	L"Magic attack +15%.",
	L"Attaque magique +15 %.",
	L"Attacco magico +15%.",
	L"Ataque mágico +15 %.",
	L"마법 공격력 +15%.",
	L"魔法攻击力+15%。",
	L"الهجوم السحري +15٪.",
	L"Магическая атака +15%.",
	L"Magischer Angriff +15 %.",
	L"Ataque mágico +15%.",
	L"Magische aanval +15%.",
	L"Atak magiczny +15%.",
	L"Büyü saldırısı +%15.")
R(TX_SKD_P_IRON,
	L"守備+15%。",
	L"Defense +15%.",
	L"Défense +15 %.",
	L"Difesa +15%.",
	L"Defensa +15 %.",
	L"수비 +15%.",
	L"防御+15%。",
	L"الدفاع +15٪.",
	L"Защита +15%.",
	L"Verteidigung +15 %.",
	L"Defesa +15%.",
	L"Verdediging +15%.",
	L"Obrona +15%.",
	L"Savunma +%15.")
R(TX_SKD_P_SWIFT,
	L"手番が回ってくる速さ+20%。",
	L"Turns come around 20% faster.",
	L"Les tours arrivent 20 % plus vite.",
	L"I turni arrivano il 20% più velocemente.",
	L"Los turnos llegan un 20 % más rápido.",
	L"차례가 돌아오는 속도 +20%.",
	L"行动回合到来的速度+20%。",
	L"تأتي الأدوار أسرع بنسبة 20٪.",
	L"Ходы наступают на 20% быстрее.",
	L"Züge kommen 20 % schneller.",
	L"Os turnos chegam 20% mais rápido.",
	L"Beurten komen 20% sneller.",
	L"Tury nadchodzą o 20% szybciej.",
	L"Sıra %20 daha hızlı gelir.")
R(TX_SKD_P_REGEN,
	L"手番の開始時に最大HPの10%を回復する。",
	L"Restores 10% of max HP at the start of each turn.",
	L"Rend 10 % des PV max au début de chaque tour.",
	L"Ripristina il 10% dei PS max all'inizio di ogni turno.",
	L"Restaura el 10 % de los PV máx. al inicio de cada turno.",
	L"차례 시작 시 최대 HP의 10%를 회복한다.",
	L"每次行动开始时恢复最大HP的10%。",
	L"يستعيد 10٪ من الصحة القصوى في بداية كل دور.",
	L"В начале хода восстанавливает 10% макс. ОЗ.",
	L"Stellt zu Beginn jedes Zuges 10 % der max. LP wieder her.",
	L"Restaura 10% dos PV máx. no início de cada turno.",
	L"Herstelt 10% van de max. LP aan het begin van elke beurt.",
	L"Na początku tury przywraca 10% maks. PŻ.",
	L"Her sıranın başında azami CP'nin %10'unu yeniler.")
R(TX_SKD_P_CRIT,
	L"必殺率+10。",
	L"Critical rate +10.",
	L"Taux critique +10.",
	L"Critico +10.",
	L"Crítico +10.",
	L"필살률 +10.",
	L"必杀率+10。",
	L"نسبة الحسم +10.",
	L"Шанс крита +10.",
	L"Kritische Rate +10.",
	L"Crítico +10.",
	L"Kritieke kans +10.",
	L"Szansa krytyka +10.",
	L"Kritik oranı +10.")
R(TX_SKD_E_VENOM,
	L"毒の牙で噛みつき、毒状態にする。",
	L"Bites with venomous fangs and poisons the target.",
	L"Mord avec des crocs venimeux et empoisonne la cible.",
	L"Morde con zanne velenose e avvelena il bersaglio.",
	L"Muerde con colmillos venenosos y envenena al objetivo.",
	L"독니로 물어 중독시킨다.",
	L"用毒牙咬住，使目标中毒。",
	L"يعض بأنياب سامة ويسمم الهدف.",
	L"Кусает ядовитыми клыками и отравляет цель.",
	L"Beißt mit Giftzähnen und vergiftet das Ziel.",
	L"Morde com presas venenosas e envenena o alvo.",
	L"Bijt met giftanden en vergiftigt het doelwit.",
	L"Gryzie jadowitymi kłami i zatruwa cel.",
	L"Zehirli dişlerle ısırır ve hedefi zehirler.")
R(TX_SKD_E_BREATH,
	L"炎を吐き、範囲を焼き払う。",
	L"Breathes fire to scorch an area.",
	L"Crache du feu et brûle une zone.",
	L"Sputa fuoco e brucia un'area.",
	L"Exhala fuego y abrasa un área.",
	L"불을 뿜어 범위를 태운다.",
	L"喷吐火焰，焚烧范围区域。",
	L"ينفث النار ويحرق منطقة.",
	L"Выдыхает пламя и выжигает область.",
	L"Speit Feuer und versengt ein Gebiet.",
	L"Cospe fogo e queima uma área.",
	L"Spuwt vuur en verschroeit een gebied.",
	L"Zieje ogniem i pali obszar.",
	L"Ateş püskürterek bir alanı yakar.")
R(TX_SKD_E_CRUSH,
	L"巨体で押しつぶす強烈な一撃。",
	L"A crushing blow with its massive body.",
	L"Un coup écrasant de tout son corps massif.",
	L"Un colpo schiacciante con il suo corpo massiccio.",
	L"Un golpe aplastante con su enorme cuerpo.",
	L"거대한 몸으로 짓누르는 강렬한 일격.",
	L"以巨大身躯压碎对手的猛烈一击。",
	L"ضربة ساحقة بجسده الضخم.",
	L"Сокрушительный удар массивным телом.",
	L"Ein zermalmender Schlag mit dem massigen Körper.",
	L"Um golpe esmagador com seu corpo enorme.",
	L"Een verpletterende slag met zijn massieve lichaam.",
	L"Miażdżący cios masywnym cielskiem.",
	L"Devasa gövdesiyle ezici bir darbe.")
R(TX_SKD_E_DRAIN,
	L"血を吸い、与えたダメージの半分を回復する。",
	L"Drinks blood, recovering half of the damage dealt.",
	L"Boit le sang et récupère la moitié des dégâts infligés.",
	L"Beve il sangue e recupera metà dei danni inflitti.",
	L"Bebe sangre y recupera la mitad del daño infligido.",
	L"피를 빨아 입힌 피해의 절반을 회복한다.",
	L"吸取鲜血，恢复所造成伤害的一半。",
	L"يشرب الدم ويستعيد نصف الضرر الذي ألحقه.",
	L"Пьёт кровь, восстанавливая половину нанесённого урона.",
	L"Trinkt Blut und heilt die Hälfte des verursachten Schadens.",
	L"Bebe sangue e recupera metade do dano causado.",
	L"Drinkt bloed en herstelt de helft van de toegebrachte schade.",
	L"Pije krew, odzyskując połowę zadanych obrażeń.",
	L"Kan emer ve verdiği hasarın yarısını geri kazanır.")
R(TX_SKD_E_SPIT,
	L"粘液を飛ばし、相手の行動を遅らせる。",
	L"Spits slime that delays the target's next turn.",
	L"Crache une bave qui retarde le prochain tour de la cible.",
	L"Sputa melma che ritarda il turno del bersaglio.",
	L"Escupe baba que retrasa el turno del objetivo.",
	L"점액을 뱉어 상대의 행동을 늦춘다.",
	L"喷射黏液，延迟对方的行动。",
	L"يبصق لزوجة تؤخر دور الهدف التالي.",
	L"Плюётся слизью и задерживает ход цели.",
	L"Spuckt Schleim, der den nächsten Zug des Ziels verzögert.",
	L"Cospe gosma que atrasa o próximo turno do alvo.",
	L"Spuwt slijm dat de volgende beurt van het doelwit vertraagt.",
	L"Pluje śluzem, który opóźnia turę celu.",
	L"Hedefin sırasını geciktiren balçık tükürür.")
R(TX_SKD_E_QUAKE,
	L"大地を揺らし、周囲の敵を攻撃する。",
	L"Shakes the ground, striking all nearby foes.",
	L"Fait trembler le sol et frappe tous les ennemis proches.",
	L"Scuote il terreno e colpisce i nemici vicini.",
	L"Sacude el suelo y golpea a los enemigos cercanos.",
	L"대지를 흔들어 주위의 적을 공격한다.",
	L"撼动大地，攻击周围的敌人。",
	L"يهز الأرض ويضرب جميع الأعداء القريبين.",
	L"Сотрясает землю и бьёт всех ближних врагов.",
	L"Erschüttert den Boden und trifft alle nahen Feinde.",
	L"Faz o chão tremer e atinge os inimigos próximos.",
	L"Laat de grond schudden en raakt alle nabije vijanden.",
	L"Wstrząsa ziemią i uderza pobliskich wrogów.",
	L"Yeri sarsar ve yakındaki tüm düşmanlara vurur.")
// ---------------------------------------------------------------- モンスター
R(TX_MO_SLIME, L"スライム", L"Slime", L"Gluant", L"Melma", L"Limo", L"슬라임", L"史莱姆", L"هلام", L"Слизень", L"Schleim", L"Gosma", L"Slijm", L"Szlam", L"Balçık")
R(TX_MO_SKEL_MINION, L"スケルトン兵", L"Skeleton Minion", L"Squelette sbire", L"Scheletro gregario", L"Esbirro esqueleto", L"스켈레톤 병사", L"骷髅兵", L"هيكل عظمي تابع", L"Скелет-прислужник", L"Skelettscherge", L"Esqueleto lacaio", L"Skeletdienaar", L"Szkielet sługa", L"İskelet er")
R(TX_MO_SKEL_WARRIOR, L"スケルトン戦士", L"Skeleton Warrior", L"Squelette guerrier", L"Scheletro guerriero", L"Esqueleto guerrero", L"스켈레톤 전사", L"骷髅战士", L"هيكل عظمي محارب", L"Скелет-воин", L"Skelettkrieger", L"Esqueleto guerreiro", L"Skeletkrijger", L"Szkielet wojownik", L"İskelet savaşçı")
R(TX_MO_SKEL_ARCHER, L"スケルトン弓兵", L"Skeleton Archer", L"Squelette archer", L"Scheletro arciere", L"Esqueleto arquero", L"스켈레톤 궁수", L"骷髅弓手", L"هيكل عظمي رامٍ", L"Скелет-стрелок", L"Skelettschütze", L"Esqueleto arqueiro", L"Skeletschutter", L"Szkielet łucznik", L"İskelet okçu")
R(TX_MO_BAT, L"大コウモリ", L"Giant Bat", L"Chauve-souris géante", L"Pipistrello gigante", L"Murciélago gigante", L"큰박쥐", L"巨型蝙蝠", L"خفاش عملاق", L"Гигантская летучая мышь", L"Riesenfledermaus", L"Morcego gigante", L"Reuzenvleermuis", L"Wielki nietoperz", L"Dev yarasa")
R(TX_MO_LICH, L"リッチ", L"Lich", L"Liche", L"Lich", L"Liche", L"리치", L"巫妖", L"ليتش", L"Лич", L"Lich", L"Lich", L"Lich", L"Lisz", L"Liç")
R(TX_MO_POISON_SLIME, L"ポイズンスライム", L"Poison Slime", L"Gluant toxique", L"Melma velenosa", L"Limo venenoso", L"포이즌 슬라임", L"毒史莱姆", L"هلام سام", L"Ядовитый слизень", L"Giftschleim", L"Gosma venenosa", L"Gifslijm", L"Trujący szlam", L"Zehirli balçık")
R(TX_MO_GOLEM, L"ゴーレム", L"Golem", L"Golem", L"Golem", L"Gólem", L"골렘", L"魔像", L"غولم", L"Голем", L"Golem", L"Golem", L"Golem", L"Golem", L"Golem")
R(TX_MO_WYVERN, L"ワイバーン", L"Wyvern", L"Vouivre", L"Viverna", L"Guiverno", L"와이번", L"双足飞龙", L"وايفرن", L"Виверна", L"Lindwurm", L"Serpe", L"Wyvern", L"Wywerna", L"Wyvern")
R(TX_MO_DARK_KNIGHT, L"暗黒騎士", L"Dark Knight", L"Chevalier noir", L"Cavaliere oscuro", L"Caballero oscuro", L"암흑기사", L"暗黑骑士", L"الفارس المظلم", L"Тёмный рыцарь", L"Dunkler Ritter", L"Cavaleiro sombrio", L"Duistere ridder", L"Mroczny rycerz", L"Kara şövalye")
R(TX_MO_DRAGON, L"ドラゴン", L"Dragon", L"Dragon", L"Drago", L"Dragón", L"드래곤", L"巨龙", L"تنين", L"Дракон", L"Drache", L"Dragão", L"Draak", L"Smok", L"Ejderha")
// ---------------------------------------------------------------- 拠点
R(TX_TITLE, L"おまけタクティクス", L"Bonus Tactics", L"Tactique bonus", L"Tattica bonus", L"Tácticas extra", L"보너스 택틱스", L"附赠战棋", L"التكتيكات الإضافية", L"Бонусная тактика", L"Bonus-Taktik", L"Táticas bônus", L"Bonustactiek", L"Dodatkowa taktyka", L"Bonus taktik")
R(TX_BASE_SORTIE, L"出撃", L"Sortie", L"Sortie", L"Sortita", L"Salir al combate", L"출격", L"出击", L"انطلاق", L"В бой", L"Ausrücken", L"Partir", L"Uitrukken", L"Do boju", L"Sefere çık")
R(TX_BASE_CREATE, L"キャラクタ作成", L"Create Character", L"Créer un personnage", L"Crea personaggio", L"Crear personaje", L"캐릭터 생성", L"创建角色", L"إنشاء شخصية", L"Создать персонажа", L"Charakter erstellen", L"Criar personagem", L"Personage maken", L"Utwórz postać", L"Karakter oluştur")
R(TX_BASE_PARTY, L"編成", L"Party", L"Équipe", L"Squadra", L"Grupo", L"편성", L"编队", L"الفريق", L"Отряд", L"Trupp", L"Grupo", L"Groep", L"Drużyna", L"Takım")
R(TX_BASE_EVOLVE, L"進化", L"Evolve", L"Évolution", L"Evoluzione", L"Evolución", L"진화", L"进化", L"التطور", L"Эволюция", L"Entwicklung", L"Evolução", L"Evolutie", L"Ewolucja", L"Evrim")
R(TX_BASE_CLOSE, L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat")
R(TX_BASE_STAGE, L"ステージ", L"Stage", L"Niveau", L"Livello", L"Fase", L"스테이지", L"关卡", L"المرحلة", L"Этап", L"Stufe", L"Fase", L"Level", L"Etap", L"Bölüm")
R(TX_BASE_ROSTER, L"仲間", L"Roster", L"Compagnons", L"Compagni", L"Compañeros", L"동료", L"同伴", L"الرفاق", L"Соратники", L"Gefährten", L"Companheiros", L"Metgezellen", L"Towarzysze", L"Yoldaşlar")
R(TX_BASE_HINT_NOCHAR, L"まずはキャラクタを作成しましょう", L"Start by creating a character", L"Commencez par créer un personnage", L"Inizia creando un personaggio", L"Empieza creando un personaje", L"먼저 캐릭터를 만들어 보세요", L"先来创建一个角色吧", L"ابدأ بإنشاء شخصية", L"Сначала создайте персонажа", L"Erstelle zuerst einen Charakter", L"Comece criando um personagem", L"Maak eerst een personage", L"Najpierw utwórz postać", L"Önce bir karakter oluşturun")
R(TX_BASE_HINT_NOPARTY, L"編成で出撃メンバーを選んでください", L"Choose your sortie members in Party", L"Choisissez les membres de la sortie dans Équipe", L"Scegli i membri della sortita in Squadra", L"Elige a los miembros en Grupo", L"편성에서 출격 멤버를 선택하세요", L"请在编队中选择出击成员", L"اختر أعضاء الانطلاق من قائمة الفريق", L"Выберите бойцов в разделе «Отряд»", L"Wähle im Trupp die Mitglieder zum Ausrücken", L"Escolha os membros em Grupo", L"Kies je leden bij Groep", L"Wybierz członków w menu Drużyna", L"Takım menüsünden sefer üyelerini seçin")
R(TX_BASE_CLEARED, L"クリア済み", L"Cleared", L"Terminé", L"Completato", L"Superado", L"클리어", L"已通关", L"مكتملة", L"Пройдено", L"Geschafft", L"Concluída", L"Voltooid", L"Ukończono", L"Tamamlandı")
R(TX_BASE_FULL, L"仲間は20人までです", L"You can have up to 20 companions", L"Vous pouvez avoir jusqu'à 20 compagnons", L"Puoi avere fino a 20 compagni", L"Puedes tener hasta 20 compañeros", L"동료는 20명까지입니다", L"同伴最多20人", L"يمكنك امتلاك 20 رفيقًا كحد أقصى", L"Можно иметь не более 20 соратников", L"Du kannst bis zu 20 Gefährten haben", L"Você pode ter até 20 companheiros", L"Je kunt maximaal 20 metgezellen hebben", L"Możesz mieć maksymalnie 20 towarzyszy", L"En fazla 20 yoldaşınız olabilir")
// ---------------------------------------------------------------- キャラクタ作成
R(TX_CR_TITLE, L"キャラクタ作成", L"Character Creation", L"Création de personnage", L"Creazione personaggio", L"Creación de personaje", L"캐릭터 생성", L"角色创建", L"إنشاء الشخصية", L"Создание персонажа", L"Charaktererstellung", L"Criação de personagem", L"Personage maken", L"Tworzenie postaci", L"Karakter oluşturma")
R(TX_CR_NAME, L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名字", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Imię", L"İsim")
R(TX_CR_RANDOM, L"おまかせ", L"Random", L"Aléatoire", L"Casuale", L"Aleatorio", L"랜덤", L"随机", L"عشوائي", L"Случайно", L"Zufällig", L"Aleatório", L"Willekeurig", L"Losowo", L"Rastgele")
R(TX_CR_GENDER, L"性別", L"Gender", L"Sexe", L"Sesso", L"Sexo", L"성별", L"性别", L"الجنس", L"Пол", L"Geschlecht", L"Sexo", L"Geslacht", L"Płeć", L"Cinsiyet")
R(TX_CR_MALE, L"男性", L"Male", L"Homme", L"Maschio", L"Hombre", L"남성", L"男性", L"ذكر", L"Мужской", L"Männlich", L"Masculino", L"Man", L"Mężczyzna", L"Erkek")
R(TX_CR_FEMALE, L"女性", L"Female", L"Femme", L"Femmina", L"Mujer", L"여성", L"女性", L"أنثى", L"Женский", L"Weiblich", L"Feminino", L"Vrouw", L"Kobieta", L"Kadın")
R(TX_CR_LINEAGE, L"系統", L"Path", L"Voie", L"Via", L"Senda", L"계통", L"系统", L"المسار", L"Путь", L"Pfad", L"Caminho", L"Pad", L"Ścieżka", L"Yol")
R(TX_CR_WARRIOR, L"戦士系", L"Warrior", L"Guerrier", L"Guerriero", L"Guerrero", L"전사계", L"战士系", L"محارب", L"Воин", L"Krieger", L"Guerreiro", L"Krijger", L"Wojownik", L"Savaşçı")
R(TX_CR_MAGIC, L"魔法系", L"Magic", L"Magie", L"Magia", L"Magia", L"마법계", L"魔法系", L"سحر", L"Магия", L"Magie", L"Magia", L"Magie", L"Magia", L"Büyü")
R(TX_CR_CLASS, L"クラス", L"Class", L"Classe", L"Classe", L"Clase", L"클래스", L"职业", L"الفئة", L"Класс", L"Klasse", L"Classe", L"Klasse", L"Klasa", L"Sınıf")
R(TX_CR_BODY, L"体型・装い", L"Build & Attire", L"Silhouette et tenue", L"Fisico e abiti", L"Físico y atuendo", L"체형·복장", L"体型・装束", L"البنية والملبس", L"Телосложение и наряд", L"Statur & Kleidung", L"Físico e traje", L"Bouw & kleding", L"Sylwetka i strój", L"Yapı ve kıyafet")
R(TX_CR_HAIR, L"髪型", L"Hairstyle", L"Coiffure", L"Acconciatura", L"Peinado", L"헤어스타일", L"发型", L"تسريحة الشعر", L"Причёска", L"Frisur", L"Penteado", L"Kapsel", L"Fryzura", L"Saç modeli")
R(TX_CR_HAIRCOLOR, L"髪色", L"Hair Color", L"Couleur des cheveux", L"Colore capelli", L"Color de pelo", L"머리색", L"发色", L"لون الشعر", L"Цвет волос", L"Haarfarbe", L"Cor do cabelo", L"Haarkleur", L"Kolor włosów", L"Saç rengi")
R(TX_CR_OUTFIT, L"服の色", L"Outfit Color", L"Couleur de tenue", L"Colore abiti", L"Color del atuendo", L"옷 색상", L"服装颜色", L"لون الملابس", L"Цвет одежды", L"Kleiderfarbe", L"Cor da roupa", L"Kledingkleur", L"Kolor stroju", L"Kıyafet rengi")
R(TX_CR_HAT, L"帽子・兜", L"Hat / Helmet", L"Chapeau / casque", L"Cappello / elmo", L"Sombrero / yelmo", L"모자·투구", L"帽子・头盔", L"قبعة / خوذة", L"Шляпа / шлем", L"Hut / Helm", L"Chapéu / elmo", L"Hoed / helm", L"Kapelusz / hełm", L"Şapka / miğfer")
R(TX_CR_CAPE, L"マント", L"Cape", L"Cape", L"Mantello", L"Capa", L"망토", L"披风", L"عباءة", L"Плащ", L"Umhang", L"Capa", L"Cape", L"Peleryna", L"Pelerin")
R(TX_CR_ON, L"あり", L"On", L"Oui", L"Sì", L"Sí", L"있음", L"有", L"نعم", L"Есть", L"An", L"Sim", L"Aan", L"Tak", L"Var")
R(TX_CR_OFF, L"なし", L"Off", L"Non", L"No", L"No", L"없음", L"无", L"لا", L"Нет", L"Aus", L"Não", L"Uit", L"Nie", L"Yok")
R(TX_CR_CREATE, L"作成する", L"Create", L"Créer", L"Crea", L"Crear", L"생성", L"创建", L"إنشاء", L"Создать", L"Erstellen", L"Criar", L"Maken", L"Utwórz", L"Oluştur")
R(TX_CR_BACK, L"戻る", L"Back", L"Retour", L"Indietro", L"Volver", L"돌아가기", L"返回", L"رجوع", L"Назад", L"Zurück", L"Voltar", L"Terug", L"Wstecz", L"Geri")
R(TX_CR_SAVE, L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet")
R(TX_CR_NAMEHINT, L"クリックして名前を入力", L"Click to enter a name", L"Cliquez pour saisir un nom", L"Clicca per inserire un nome", L"Haz clic para escribir un nombre", L"클릭하여 이름 입력", L"点击输入名字", L"انقر لإدخال الاسم", L"Нажмите, чтобы ввести имя", L"Klicken, um einen Namen einzugeben", L"Clique para digitar um nome", L"Klik om een naam in te voeren", L"Kliknij, aby wpisać imię", L"İsim girmek için tıklayın")
R(TX_CR_EDIT, L"容姿を変更", L"Change Appearance", L"Changer l'apparence", L"Cambia aspetto", L"Cambiar apariencia", L"외모 변경", L"更改外观", L"تغيير المظهر", L"Изменить внешность", L"Aussehen ändern", L"Mudar aparência", L"Uiterlijk wijzigen", L"Zmień wygląd", L"Görünümü değiştir")
R(TX_BODY_KNIGHT, L"騎士風", L"Knight", L"Chevalier", L"Cavaliere", L"Caballero", L"기사풍", L"骑士风", L"فارس", L"Рыцарь", L"Ritter", L"Cavaleiro", L"Ridder", L"Rycerz", L"Şövalye")
R(TX_BODY_BARBARIAN, L"蛮族風", L"Barbarian", L"Barbare", L"Barbaro", L"Bárbaro", L"야만족풍", L"蛮族风", L"بربري", L"Варвар", L"Barbar", L"Bárbaro", L"Barbaar", L"Barbarzyńca", L"Barbar")
R(TX_BODY_MAGE, L"魔導士風", L"Mage", L"Mage", L"Mago", L"Mago", L"마도사풍", L"魔导士风", L"ساحر", L"Маг", L"Magier", L"Mago", L"Magiër", L"Mag", L"Büyücü")
R(TX_BODY_ROGUE, L"盗賊風", L"Rogue", L"Voleur", L"Furfante", L"Pícaro", L"도적풍", L"盗贼风", L"محتال", L"Плут", L"Schurke", L"Ladino", L"Schurk", L"Łotrzyk", L"Haydut")
R(TX_BODY_HOODED, L"フード", L"Hooded", L"Encapuchonné", L"Incappucciato", L"Encapuchado", L"후드", L"兜帽", L"مقنّع", L"В капюшоне", L"Kapuze", L"Encapuzado", L"Met kap", L"W kapturze", L"Kapüşonlu")
R(TX_HAIR_DEFAULT, L"標準", L"Default", L"Standard", L"Standard", L"Estándar", L"기본", L"标准", L"افتراضي", L"Обычная", L"Standard", L"Padrão", L"Standaard", L"Domyślna", L"Varsayılan")
R(TX_HAIR_SHORT, L"ショート", L"Short", L"Courts", L"Corti", L"Corto", L"쇼트", L"短发", L"قصير", L"Короткая", L"Kurz", L"Curto", L"Kort", L"Krótkie", L"Kısa")
R(TX_HAIR_LONG, L"ロング", L"Long", L"Longs", L"Lunghi", L"Largo", L"롱", L"长发", L"طويل", L"Длинная", L"Lang", L"Longo", L"Lang", L"Długie", L"Uzun")
R(TX_HAIR_PONY, L"ポニーテール", L"Ponytail", L"Queue-de-cheval", L"Coda di cavallo", L"Coleta", L"포니테일", L"马尾", L"ذيل حصان", L"Хвост", L"Pferdeschwanz", L"Rabo de cavalo", L"Paardenstaart", L"Kucyk", L"At kuyruğu")
R(TX_HAIR_TWIN, L"ツインテール", L"Twin Tails", L"Couettes", L"Codini", L"Dos coletas", L"트윈테일", L"双马尾", L"ضفيرتان", L"Два хвостика", L"Zöpfe", L"Maria-chiquinha", L"Twee staartjes", L"Dwa kucyki", L"İki kuyruk")
R(TX_HAIR_BOB, L"ボブ", L"Bob", L"Carré", L"Caschetto", L"Melena corta", L"단발", L"波波头", L"قصة بوب", L"Каре", L"Bob", L"Chanel", L"Bob", L"Bob", L"Küt")
// ---------------------------------------------------------------- 編成
R(TX_PT_TITLE, L"編成", L"Party", L"Équipe", L"Squadra", L"Grupo", L"편성", L"编队", L"الفريق", L"Отряд", L"Trupp", L"Grupo", L"Groep", L"Drużyna", L"Takım")
R(TX_PT_HINT,
	L"カードをクリックで出撃メンバーに追加・解除（最大5人）",
	L"Click a card to add or remove it from the sortie (up to 5)",
	L"Cliquez sur une carte pour l'ajouter ou la retirer de la sortie (5 max)",
	L"Clicca una carta per aggiungerla o toglierla dalla sortita (max 5)",
	L"Haz clic en una carta para añadirla o quitarla (máx. 5)",
	L"카드를 클릭해 출격 멤버에 추가·해제 (최대 5명)",
	L"点击卡片加入或移出出击成员（最多5人）",
	L"انقر على بطاقة لإضافتها أو إزالتها من الانطلاق (5 كحد أقصى)",
	L"Нажмите на карточку, чтобы добавить или убрать бойца (до 5)",
	L"Karte anklicken, um sie hinzuzufügen oder zu entfernen (max. 5)",
	L"Clique em um cartão para adicioná-lo ou removê-lo (máx. 5)",
	L"Klik op een kaart om toe te voegen of te verwijderen (max. 5)",
	L"Kliknij kartę, aby dodać lub usunąć członka (maks. 5)",
	L"Sefere eklemek veya çıkarmak için karta tıklayın (en fazla 5)")
R(TX_PT_DELETE, L"削除", L"Delete", L"Supprimer", L"Elimina", L"Eliminar", L"삭제", L"删除", L"حذف", L"Удалить", L"Löschen", L"Excluir", L"Verwijderen", L"Usuń", L"Sil")
R(TX_PT_DELETE_Q, L"本当に削除しますか？", L"Really delete?", L"Vraiment supprimer ?", L"Eliminare davvero?", L"¿Eliminar de verdad?", L"정말 삭제하시겠습니까?", L"确定要删除吗？", L"هل تريد الحذف حقًا؟", L"Удалить на самом деле?", L"Wirklich löschen?", L"Excluir mesmo?", L"Echt verwijderen?", L"Na pewno usunąć?", L"Gerçekten silinsin mi?")
R(TX_PT_YES, L"はい", L"Yes", L"Oui", L"Sì", L"Sí", L"예", L"是", L"نعم", L"Да", L"Ja", L"Sim", L"Ja", L"Tak", L"Evet")
R(TX_PT_NO, L"いいえ", L"No", L"Non", L"No", L"No", L"아니요", L"否", L"لا", L"Нет", L"Nein", L"Não", L"Nee", L"Nie", L"Hayır")
R(TX_PT_DEPLOY, L"出撃メンバー", L"Sortie Members", L"Membres de la sortie", L"Membri della sortita", L"Miembros de la salida", L"출격 멤버", L"出击成员", L"أعضاء الانطلاق", L"Бойцы в походе", L"Ausrückende", L"Membros da partida", L"Uitrukkende leden", L"Członkowie wyprawy", L"Sefer üyeleri")
R(TX_PT_SKILLS, L"スキル", L"Skills", L"Compétences", L"Abilità", L"Habilidades", L"스킬", L"技能", L"المهارات", L"Навыки", L"Fertigkeiten", L"Habilidades", L"Vaardigheden", L"Umiejętności", L"Yetenekler")
R(TX_PT_NONE, L"なし", L"None", L"Aucune", L"Nessuna", L"Ninguna", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhuma", L"Geen", L"Brak", L"Yok")
// ---------------------------------------------------------------- 進化
R(TX_EV_TITLE, L"進化", L"Evolution", L"Évolution", L"Evoluzione", L"Evolución", L"진화", L"进化", L"التطور", L"Эволюция", L"Entwicklung", L"Evolução", L"Evolutie", L"Ewolucja", L"Evrim")
R(TX_EV_MAIN, L"進化するキャラ", L"Character to Evolve", L"Personnage à faire évoluer", L"Personaggio da evolvere", L"Personaje a evolucionar", L"진화할 캐릭터", L"要进化的角色", L"الشخصية المراد تطويرها", L"Кого развить", L"Zu entwickelnder Charakter", L"Personagem a evoluir", L"Te evolueren personage", L"Postać do ewolucji", L"Evrilecek karakter")
R(TX_EV_TARGET, L"進化先", L"Evolve Into", L"Évoluer en", L"Evolve in", L"Evoluciona a", L"진화 대상", L"进化目标", L"يتطور إلى", L"Во что развить", L"Entwickeln zu", L"Evoluir para", L"Evolueert naar", L"Ewoluuje w", L"Şuna evrilir")
R(TX_EV_PARTNER, L"素材キャラ", L"Partner Characters", L"Personnages partenaires", L"Personaggi partner", L"Personajes compañeros", L"재료 캐릭터", L"素材角色", L"الشخصيات المساندة", L"Персонажи-материал", L"Partner-Charaktere", L"Personagens parceiros", L"Partnerpersonages", L"Postacie-partnerzy", L"Ortak karakterler")
R(TX_EV_DO, L"進化する", L"Evolve", L"Faire évoluer", L"Evolvi", L"Evolucionar", L"진화하기", L"进化", L"طوّر", L"Развить", L"Entwickeln", L"Evoluir", L"Evolueren", L"Ewoluuj", L"Evrilt")
R(TX_EV_HINT_MAIN,
	L"Lv15で1段階目、Lv35で2段階目の進化が可能",
	L"First evolution at Lv15, second at Lv35",
	L"Première évolution au niv. 15, seconde au niv. 35",
	L"Prima evoluzione al Lv15, seconda al Lv35",
	L"Primera evolución en Nv15, segunda en Nv35",
	L"Lv15에서 1단계, Lv35에서 2단계 진화 가능",
	L"Lv15可进行第1阶段进化，Lv35可进行第2阶段进化",
	L"التطور الأول عند المستوى 15، والثاني عند المستوى 35",
	L"Первая эволюция на ур. 15, вторая на ур. 35",
	L"Erste Entwicklung ab Lv15, zweite ab Lv35",
	L"Primeira evolução no Nv15, segunda no Nv35",
	L"Eerste evolutie op Lv15, tweede op Lv35",
	L"Pierwsza ewolucja na poz. 15, druga na poz. 35",
	L"İlk evrim Sv15'te, ikincisi Sv35'te")
R(TX_EV_HINT_PARTNER,
	L"素材キャラは消滅します。能力の一部とスキルを引き継ぎます",
	L"Partners will be lost. Some of their stats and skills are inherited",
	L"Les partenaires disparaîtront. Une partie de leurs stats et compétences est héritée",
	L"I partner andranno persi. Parte delle loro statistiche e abilità viene ereditata",
	L"Los compañeros desaparecerán. Se heredan parte de sus atributos y habilidades",
	L"재료 캐릭터는 사라집니다. 능력 일부와 스킬을 계승합니다",
	L"素材角色将会消失。继承其部分能力与技能",
	L"ستختفي الشخصيات المساندة. تُورث بعض قدراتها ومهاراتها",
	L"Персонажи-материал исчезнут. Часть их характеристик и навыков наследуется",
	L"Partner gehen verloren. Ein Teil ihrer Werte und Fertigkeiten wird vererbt",
	L"Os parceiros serão perdidos. Parte dos atributos e habilidades é herdada",
	L"Partners verdwijnen. Een deel van hun waarden en vaardigheden wordt geërfd",
	L"Partnerzy znikną. Część ich statystyk i umiejętności zostanie odziedziczona",
	L"Ortaklar kaybolur. Özelliklerinin ve yeteneklerinin bir kısmı aktarılır")
R(TX_EV_NEED_GROUP, L"必要な素材の系統", L"Required Partner Type", L"Type de partenaire requis", L"Tipo di partner richiesto", L"Tipo de compañero requerido", L"필요한 재료 계통", L"所需素材系统", L"نوع الشريك المطلوب", L"Нужный тип материала", L"Benötigter Partnertyp", L"Tipo de parceiro exigido", L"Vereist partnertype", L"Wymagany typ partnera", L"Gereken ortak türü")
R(TX_EV_DONE, L"進化しました！", L"Evolved!", L"Évolution réussie !", L"Evoluzione riuscita!", L"¡Ha evolucionado!", L"진화했습니다!", L"进化成功！", L"تم التطور!", L"Эволюция завершена!", L"Entwickelt!", L"Evoluiu!", L"Geëvolueerd!", L"Ewolucja udana!", L"Evrildi!")
R(TX_EV_ANY, L"不問", L"Any", L"Indifférent", L"Qualsiasi", L"Cualquiera", L"무관", L"不限", L"أي نوع", L"Любой", L"Beliebig", L"Qualquer", L"Willekeurig", L"Dowolny", L"Fark etmez")
R(TX_EV_CONSUME, L"消費", L"Consumed", L"Consommé", L"Consumato", L"Consumido", L"소비", L"消耗", L"مستهلك", L"Расходуется", L"Verbraucht", L"Consumido", L"Verbruikt", L"Zużyty", L"Harcanır")
R(TX_EV_PREVIEW, L"進化後の能力", L"Stats After Evolution", L"Stats après évolution", L"Statistiche dopo l'evoluzione", L"Atributos tras evolucionar", L"진화 후 능력", L"进化后的能力", L"القدرات بعد التطور", L"Характеристики после эволюции", L"Werte nach Entwicklung", L"Atributos após evolução", L"Waarden na evolutie", L"Statystyki po ewolucji", L"Evrim sonrası özellikler")
R(TX_EV_NONE_ELIGIBLE, L"進化できるキャラがいません", L"No characters can evolve", L"Aucun personnage ne peut évoluer", L"Nessun personaggio può evolvere", L"Ningún personaje puede evolucionar", L"진화할 수 있는 캐릭터가 없습니다", L"没有可以进化的角色", L"لا توجد شخصيات يمكنها التطور", L"Никто не может эволюционировать", L"Kein Charakter kann sich entwickeln", L"Nenhum personagem pode evoluir", L"Geen personage kan evolueren", L"Żadna postać nie może ewoluować", L"Evrilebilecek karakter yok")
R(TX_G_MELEE, L"剣士系", L"Blade", L"Lame", L"Lama", L"Espada", L"검사계", L"剑士系", L"السيف", L"Клинок", L"Klinge", L"Lâmina", L"Zwaard", L"Ostrze", L"Kılıç")
R(TX_G_SWIFT, L"俊敏系", L"Swift", L"Agile", L"Agile", L"Ágil", L"민첩계", L"敏捷系", L"الرشاقة", L"Ловкость", L"Flink", L"Ágil", L"Snel", L"Zwinność", L"Çevik")
R(TX_G_MAGIC, L"魔術系", L"Arcane", L"Arcane", L"Arcano", L"Arcano", L"마술계", L"魔术系", L"السحر", L"Тайная магия", L"Arkan", L"Arcano", L"Arcaan", L"Tajemna magia", L"Gizem")
R(TX_G_HOLY, L"神聖系", L"Holy", L"Sacré", L"Sacro", L"Sagrado", L"신성계", L"神圣系", L"القداسة", L"Святость", L"Heilig", L"Sagrado", L"Heilig", L"Świętość", L"Kutsal")
// ---------------------------------------------------------------- 戦闘
R(TX_BT_TURN_PLAYER, L"味方の手番", L"Ally Turn", L"Tour allié", L"Turno alleato", L"Turno aliado", L"아군 차례", L"我方回合", L"دور الحلفاء", L"Ход союзника", L"Verbündeter am Zug", L"Turno aliado", L"Beurt bondgenoot", L"Tura sojusznika", L"Müttefik sırası")
R(TX_BT_TURN_ENEMY, L"敵の手番", L"Enemy Turn", L"Tour ennemi", L"Turno nemico", L"Turno enemigo", L"적 차례", L"敌方回合", L"دور العدو", L"Ход врага", L"Feind am Zug", L"Turno inimigo", L"Beurt vijand", L"Tura wroga", L"Düşman sırası")
R(TX_BT_MOVE, L"移動", L"Move", L"Déplacer", L"Muovi", L"Mover", L"이동", L"移动", L"تحرك", L"Ход", L"Bewegen", L"Mover", L"Bewegen", L"Ruch", L"Hareket")
R(TX_BT_ATTACK, L"攻撃", L"Attack", L"Attaquer", L"Attacca", L"Atacar", L"공격", L"攻击", L"هجوم", L"Атака", L"Angriff", L"Atacar", L"Aanval", L"Atak", L"Saldırı")
R(TX_BT_SKILL, L"スキル", L"Skill", L"Compétence", L"Abilità", L"Habilidad", L"스킬", L"技能", L"مهارة", L"Навык", L"Fertigkeit", L"Habilidade", L"Vaardigheid", L"Umiejętność", L"Yetenek")
R(TX_BT_WAIT, L"待機", L"Wait", L"Attendre", L"Attendi", L"Esperar", L"대기", L"待机", L"انتظار", L"Ждать", L"Warten", L"Esperar", L"Wachten", L"Czekaj", L"Bekle")
R(TX_BT_UNDO, L"移動を戻す", L"Undo Move", L"Annuler le déplacement", L"Annulla movimento", L"Deshacer movimiento", L"이동 취소", L"撤销移动", L"تراجع عن الحركة", L"Отменить ход", L"Bewegung zurück", L"Desfazer movimento", L"Beweging ongedaan", L"Cofnij ruch", L"Hareketi geri al")
R(TX_BT_STATUS, L"ステータス", L"Status", L"Statut", L"Stato", L"Estado", L"스테이터스", L"状态", L"الحالة", L"Состояние", L"Status", L"Estado", L"Status", L"Status", L"Durum")
R(TX_BT_ORDER, L"行動順", L"Turn Order", L"Ordre des tours", L"Ordine dei turni", L"Orden de turnos", L"행동 순서", L"行动顺序", L"ترتيب الأدوار", L"Порядок ходов", L"Zugreihenfolge", L"Ordem dos turnos", L"Beurtvolgorde", L"Kolejność tur", L"Sıra düzeni")
R(TX_BT_STAGE, L"ステージ", L"Stage", L"Niveau", L"Livello", L"Fase", L"스테이지", L"关卡", L"المرحلة", L"Этап", L"Stufe", L"Fase", L"Level", L"Etap", L"Bölüm")
R(TX_BT_HINT_MOVE,
	L"青いマスをクリックで移動／敵をクリックで接近して攻撃。左ドラッグで地図を見渡す",
	L"Click a blue cell to move / an enemy to attack. Left-drag to look around the map",
	L"Cliquez une case bleue pour avancer / un ennemi pour attaquer. Glisser gauche : regarder la carte",
	L"Clicca una casella blu per muoverti / un nemico per attaccare. Trascina a sinistra per guardare la mappa",
	L"Clic en una casilla azul para moverte / en un enemigo para atacar. Arrastra con el izquierdo para mirar el mapa",
	L"파란 칸을 클릭해 이동 / 적을 클릭하면 공격. 왼쪽 드래그로 지도를 둘러봅니다",
	L"点击蓝色格子移动／点击敌人攻击。按住左键拖动来查看地图",
	L"انقر على خانة زرقاء للتحرك / على عدو للهجوم. اسحب بالزر الأيسر لرؤية الخريطة",
	L"Синяя клетка — идти, враг — атаковать. Левая кнопка: осмотреть карту",
	L"Blaues Feld: bewegen / Feind: angreifen. Links ziehen, um die Karte zu überblicken",
	L"Clique numa casa azul para mover / num inimigo para atacar. Arraste com o esquerdo para ver o mapa",
	L"Klik op een blauw vak om te bewegen / op een vijand om aan te vallen. Sleep met links om de kaart te zien",
	L"Niebieskie pole: ruch / wróg: atak. Przeciągnij lewym przyciskiem, aby rozejrzeć się po mapie",
	L"Mavi kareye tıklayınca hareket, düşmana tıklayınca saldırı. Sol tuşla sürükleyerek haritaya bakın")
R(TX_BT_HINT_TARGET, L"攻撃する対象を選択", L"Choose a target to attack", L"Choisissez une cible à attaquer", L"Scegli un bersaglio da attaccare", L"Elige un objetivo para atacar", L"공격할 대상을 선택", L"选择攻击目标", L"اختر هدفًا للهجوم", L"Выберите цель атаки", L"Wähle ein Angriffsziel", L"Escolha um alvo para atacar", L"Kies een doelwit om aan te vallen", L"Wybierz cel ataku", L"Saldırılacak hedefi seçin")
R(TX_BT_HINT_ENEMY, L"敵の行動中…", L"Enemy is acting…", L"L'ennemi agit…", L"Il nemico agisce…", L"El enemigo actúa…", L"적이 행동 중…", L"敌人行动中…", L"العدو يتحرك…", L"Враг действует…", L"Der Feind handelt…", L"O inimigo está agindo…", L"De vijand handelt…", L"Wróg działa…", L"Düşman hareket ediyor…")
R(TX_BT_FORECAST, L"戦闘予測", L"Battle Forecast", L"Prévision de combat", L"Previsione scontro", L"Previsión de combate", L"전투 예측", L"战斗预测", L"توقع المعركة", L"Прогноз боя", L"Kampfprognose", L"Previsão de combate", L"Gevechtsvoorspelling", L"Prognoza walki", L"Savaş tahmini")
R(TX_BT_DMG, L"威力", L"Damage", L"Dégâts", L"Danni", L"Daño", L"위력", L"威力", L"الضرر", L"Урон", L"Schaden", L"Dano", L"Schade", L"Obrażenia", L"Hasar")
R(TX_BT_HIT, L"命中", L"Hit", L"Précision", L"Precisione", L"Precisión", L"명중", L"命中", L"الإصابة", L"Точность", L"Treffer", L"Acerto", L"Trefkans", L"Celność", L"İsabet")
R(TX_BT_CRIT, L"必殺", L"Crit", L"Critique", L"Critico", L"Crítico", L"필살", L"必杀", L"حسم", L"Крит", L"Krit.", L"Crítico", L"Kritiek", L"Kryt.", L"Kritik")
R(TX_BT_COUNTER, L"反撃", L"Counter", L"Riposte", L"Contrattacco", L"Contraataque", L"반격", L"反击", L"هجوم مضاد", L"Ответ", L"Konter", L"Contra-ataque", L"Tegenaanval", L"Kontra", L"Karşı saldırı")
R(TX_BT_NOCOUNTER, L"反撃不可", L"No Counter", L"Pas de riposte", L"Nessun contrattacco", L"Sin contraataque", L"반격 불가", L"无法反击", L"لا هجوم مضاد", L"Без ответа", L"Kein Konter", L"Sem contra-ataque", L"Geen tegenaanval", L"Brak kontry", L"Karşı saldırı yok")
R(TX_BT_MISS, L"MISS", L"MISS", L"RATÉ", L"MANCATO", L"FALLO", L"MISS", L"MISS", L"إخفاق", L"ПРОМАХ", L"VERFEHLT", L"ERROU", L"MIS", L"PUDŁO", L"ISKA")
R(TX_BT_CRITICAL, L"CRITICAL!", L"CRITICAL!", L"CRITIQUE !", L"CRITICO!", L"¡CRÍTICO!", L"CRITICAL!", L"暴击！", L"ضربة حاسمة!", L"КРИТ!", L"KRITISCH!", L"CRÍTICO!", L"KRITIEK!", L"KRYTYK!", L"KRİTİK!")
R(TX_BT_LEVELUP, L"LEVEL UP!", L"LEVEL UP!", L"NIVEAU SUPÉRIEUR !", L"LIVELLO SU!", L"¡SUBE DE NIVEL!", L"LEVEL UP!", L"升级！", L"ارتقاء المستوى!", L"НОВЫЙ УРОВЕНЬ!", L"STUFENAUFSTIEG!", L"SUBIU DE NÍVEL!", L"LEVEL OMHOOG!", L"NOWY POZIOM!", L"SEVİYE ATLADI!")
R(TX_BT_LEARNED, L"習得", L"Learned", L"Appris", L"Appresa", L"Aprendida", L"습득", L"习得", L"تعلّم", L"Изучено", L"Erlernt", L"Aprendida", L"Geleerd", L"Nauczono", L"Öğrenildi")
R(TX_BT_DEFEATED, L"撃破", L"Defeated", L"Vaincu", L"Sconfitto", L"Derrotado", L"격파", L"击破", L"هُزم", L"Повержен", L"Besiegt", L"Derrotado", L"Verslagen", L"Pokonany", L"Yenildi")
R(TX_BT_VICTORY, L"勝利！", L"Victory!", L"Victoire !", L"Vittoria!", L"¡Victoria!", L"승리!", L"胜利！", L"النصر!", L"Победа!", L"Sieg!", L"Vitória!", L"Overwinning!", L"Zwycięstwo!", L"Zafer!")
R(TX_BT_DEFEAT, L"敗北…", L"Defeat…", L"Défaite…", L"Sconfitta…", L"Derrota…", L"패배…", L"败北…", L"الهزيمة…", L"Поражение…", L"Niederlage…", L"Derrota…", L"Nederlaag…", L"Porażka…", L"Yenilgi…")
R(TX_BT_EXP, L"経験値", L"EXP", L"EXP", L"ESP", L"EXP", L"경험치", L"经验值", L"الخبرة", L"Опыт", L"EP", L"EXP", L"EP", L"PD", L"TP")
R(TX_BT_TOBASE, L"拠点へ戻る", L"Return to Base", L"Retour à la base", L"Torna alla base", L"Volver a la base", L"거점으로 돌아가기", L"返回据点", L"العودة إلى القاعدة", L"Вернуться на базу", L"Zur Basis", L"Voltar à base", L"Terug naar basis", L"Wróć do bazy", L"Üsse dön")
R(TX_BT_NEXT, L"次のステージへ", L"Next Stage", L"Niveau suivant", L"Livello successivo", L"Siguiente fase", L"다음 스테이지로", L"前往下一关", L"المرحلة التالية", L"Следующий этап", L"Nächste Stufe", L"Próxima fase", L"Volgend level", L"Następny etap", L"Sonraki bölüm")
R(TX_BT_RETRY, L"再挑戦", L"Retry", L"Réessayer", L"Riprova", L"Reintentar", L"재도전", L"重新挑战", L"إعادة المحاولة", L"Повторить", L"Erneut versuchen", L"Tentar de novo", L"Opnieuw", L"Spróbuj ponownie", L"Tekrar dene")
R(TX_BT_RETREAT, L"撤退", L"Retreat", L"Retraite", L"Ritirata", L"Retirada", L"철수", L"撤退", L"انسحاب", L"Отступить", L"Rückzug", L"Recuar", L"Terugtrekken", L"Odwrót", L"Geri çekil")
R(TX_BT_RETREAT_Q,
	L"撤退しますか？ 獲得した経験値は保持されます",
	L"Retreat? Experience gained so far is kept",
	L"Battre en retraite ? L'expérience acquise est conservée",
	L"Ritirarsi? L'esperienza ottenuta viene conservata",
	L"¿Retirarse? Se conserva la experiencia obtenida",
	L"철수하시겠습니까? 획득한 경험치는 유지됩니다",
	L"要撤退吗？已获得的经验值会保留",
	L"هل تريد الانسحاب؟ ستُحفظ الخبرة المكتسبة",
	L"Отступить? Полученный опыт сохранится",
	L"Rückzug? Bisher gewonnene Erfahrung bleibt erhalten",
	L"Recuar? A experiência obtida é mantida",
	L"Terugtrekken? Opgedane ervaring blijft behouden",
	L"Wycofać się? Zdobyte doświadczenie zostanie zachowane",
	L"Geri çekilinsin mi? Kazanılan tecrübe korunur")
R(TX_BT_BOSS, L"ボス", L"Boss", L"Boss", L"Boss", L"Jefe", L"보스", L"首领", L"الزعيم", L"Босс", L"Boss", L"Chefe", L"Baas", L"Boss", L"Patron")
R(TX_BT_TERRAIN, L"地形", L"Terrain", L"Terrain", L"Terreno", L"Terreno", L"지형", L"地形", L"التضاريس", L"Местность", L"Gelände", L"Terreno", L"Terrein", L"Teren", L"Arazi")
R(TX_BT_NOMP, L"MPが足りない", L"Not enough MP", L"PM insuffisants", L"PM insufficienti", L"PM insuficientes", L"MP 부족", L"MP不足", L"السحر غير كافٍ", L"Не хватает ОМ", L"Nicht genug MP", L"PM insuficientes", L"Te weinig MP", L"Za mało PM", L"MP yetersiz")
R(TX_BT_NOTARGET, L"対象がいない", L"No target in range", L"Aucune cible à portée", L"Nessun bersaglio a portata", L"Ningún objetivo al alcance", L"대상이 없음", L"没有可选目标", L"لا هدف في المدى", L"Нет цели в радиусе", L"Kein Ziel in Reichweite", L"Nenhum alvo ao alcance", L"Geen doelwit binnen bereik", L"Brak celu w zasięgu", L"Menzilde hedef yok")
R(TX_BT_HEALED, L"回復", L"Healed", L"Soigné", L"Curato", L"Curado", L"회복", L"恢复", L"شُفي", L"Вылечен", L"Geheilt", L"Curado", L"Genezen", L"Uleczono", L"İyileşti")
R(TX_BT_POISON, L"毒", L"Poison", L"Poison", L"Veleno", L"Veneno", L"독", L"中毒", L"سم", L"Яд", L"Gift", L"Veneno", L"Gif", L"Trucizna", L"Zehir")
R(TX_BT_BUFF, L"強化", L"Buff", L"Renforcé", L"Potenziato", L"Mejora", L"강화", L"强化", L"تعزيز", L"Усиление", L"Verstärkt", L"Reforço", L"Versterkt", L"Wzmocnienie", L"Güçlendirme")
R(TX_BT_SLOWED, L"鈍足", L"Slowed", L"Ralenti", L"Rallentato", L"Ralentizado", L"둔화", L"迟缓", L"مُبطأ", L"Замедлен", L"Verlangsamt", L"Lento", L"Vertraagd", L"Spowolniony", L"Yavaşladı")
R(TX_BT_ENDTURN, L"ターン", L"Turn", L"Tour", L"Turno", L"Turno", L"턴", L"回合", L"الدور", L"Ход", L"Runde", L"Turno", L"Beurt", L"Tura", L"Tur")
R(TX_BT_CAM, L"カメラ", L"Camera", L"Caméra", L"Telecamera", L"Cámara", L"카메라", L"镜头", L"الكاميرا", L"Камера", L"Kamera", L"Câmera", L"Camera", L"Kamera", L"Kamera")
R(TX_BT_HELP, L"操作説明", L"Controls", L"Commandes", L"Comandi", L"Controles", L"조작 설명", L"操作说明", L"التحكم", L"Управление", L"Steuerung", L"Controles", L"Besturing", L"Sterowanie", L"Kontroller")
R(TX_BT_MENU, L"メニュー", L"Menu", L"Menu", L"Menu", L"Menú", L"메뉴", L"菜单", L"القائمة", L"Меню", L"Menü", L"Menu", L"Menu", L"Menu", L"Menü")
R(TX_BT_LEADER, L"リーダー", L"Leader", L"Chef", L"Capo", L"Líder", L"리더", L"队长", L"القائد", L"Лидер", L"Anführer", L"Líder", L"Leider", L"Przywódca", L"Lider")
R(TX_BT_GUARD, L"防御", L"Guard", L"Garde", L"Guardia", L"Guardia", L"방어", L"防御", L"حراسة", L"Защита", L"Abwehr", L"Guarda", L"Verdedigen", L"Garda", L"Savunma")
R(TX_BT_SPEED, L"速さ", L"Speed", L"Vitesse", L"Velocità", L"Velocidad", L"속도", L"速度", L"السرعة", L"Скорость", L"Tempo", L"Velocidade", L"Snelheid", L"Szybkość", L"Hız")
// ---------------------------------------------------------------- 結果
R(TX_RS_TITLE, L"戦闘結果", L"Battle Results", L"Résultats du combat", L"Risultati dello scontro", L"Resultados del combate", L"전투 결과", L"战斗结果", L"نتائج المعركة", L"Итоги боя", L"Kampfergebnis", L"Resultado da batalha", L"Gevechtsresultaat", L"Wyniki bitwy", L"Savaş sonuçları")
R(TX_RS_STAGECLEAR, L"ステージクリア！", L"Stage Clear!", L"Niveau terminé !", L"Livello completato!", L"¡Fase superada!", L"스테이지 클리어!", L"关卡通关！", L"اكتملت المرحلة!", L"Этап пройден!", L"Stufe geschafft!", L"Fase concluída!", L"Level voltooid!", L"Etap ukończony!", L"Bölüm tamamlandı!")
R(TX_RS_KO, L"戦闘不能", L"Knocked Out", L"K.O.", L"K.O.", L"Fuera de combate", L"전투 불능", L"无法战斗", L"خارج القتال", L"Выбыл", L"Kampfunfähig", L"Nocauteado", L"Uitgeschakeld", L"Niezdolny do walki", L"Savaş dışı")
// ---------------------------------------------------------------- 操作説明
R(TX_HELP_TEXT,
	L"【基本】\n"
	L"・行動順は素早さで決まり、右上の「行動順」に表示されます。素早いキャラは割り込んで何度も動きます。\n"
	L"・青いマスが移動範囲、赤いマスが攻撃範囲です。敵の手番では敵の移動範囲も表示されます。\n"
	L"・移動後に「攻撃」「スキル」「待機」を選びます。行動前なら移動を戻せます。\n"
	L"・必殺の一撃やスキル・魔法ではカットイン演出が入ります（設定で短縮できます）。\n"
	L"・ボスを倒すか敵を全滅させると勝利。味方が全滅すると敗北です。\n"
	L"【マウス】 左クリック: 選択・決定　右クリック: 戻る　右ドラッグ: 視点回転　中ドラッグ: 視点移動　ホイール: ズーム\n"
	L"【キーボード】 矢印/WASD: カーソル　Enter/Space/Z: 決定　Esc/X: 戻る　Q/E: 回転　PgUp/PgDn: ズーム　Tab: ユニット切替　Shift: 早送り　F1/H: この画面\n"
	L"【パッド】 左スティック/十字キー: カーソル　A: 決定　B: 戻る　LB/RB: 回転　右スティック: 視点　トリガー: ズーム　X: 早送り　Y: 中央へ　Start: メニュー　Back: 操作説明\n"
	L"【育成】\n"
	L"・敵を倒すと経験値を得てレベルアップ。一定のレベルでスキルを覚えます。\n"
	L"・Lv15で上位クラスへ（素材1体）、Lv35でさらに最上位クラスへ（素材2体）進化できます。\n"
	L"・ステージは1～100。先へ進むほど敵が強くなり、得られる経験値も増えます。",
	L"[Basics]\n"
	L"- Turn order depends on agility and is shown in \"Turn Order\" at the top right. Fast units can cut in and act more often.\n"
	L"- Blue cells show the movement range, red cells the attack range. During enemy turns their movement range is shown too.\n"
	L"- After moving, choose Attack, Skill or Wait. You can undo the move before acting.\n"
	L"- Critical hits, skills and spells play a cut-in scene (can be shortened in settings).\n"
	L"- Win by defeating the boss or all enemies. You lose if all allies fall.\n"
	L"[Mouse] Left click: select/confirm  Right click: back  Right drag: rotate view  Middle drag: pan  Wheel: zoom\n"
	L"[Keyboard] Arrows/WASD: cursor  Enter/Space/Z: confirm  Esc/X: back  Q/E: rotate  PgUp/PgDn: zoom  Tab: next unit  Shift: fast forward  F1/H: this screen\n"
	L"[Gamepad] Left stick/D-pad: cursor  A: confirm  B: back  LB/RB: rotate  Right stick: view  Triggers: zoom  X: fast forward  Y: recenter  Start: menu  Back: controls\n"
	L"[Growth]\n"
	L"- Defeat enemies to gain EXP and level up. Skills are learned at certain levels.\n"
	L"- At Lv15 evolve to an advanced class (1 partner), at Lv35 to a master class (2 partners).\n"
	L"- Stages run from 1 to 100. Enemies grow stronger and give more EXP as you advance.",
	L"[Bases]\n"
	L"- L'ordre des tours dépend de l'agilité et s'affiche dans « Ordre des tours » en haut à droite. Les unités rapides s'intercalent et agissent plus souvent.\n"
	L"- Les cases bleues indiquent le déplacement, les rouges la portée d'attaque. Pendant le tour ennemi, sa portée de déplacement est aussi affichée.\n"
	L"- Après le déplacement, choisissez Attaquer, Compétence ou Attendre. Vous pouvez annuler le déplacement avant d'agir.\n"
	L"- Les coups critiques, compétences et sorts déclenchent une cinématique (raccourcissable dans les réglages).\n"
	L"- Vainquez le boss ou tous les ennemis pour gagner. Vous perdez si tous vos alliés tombent.\n"
	L"[Souris] Clic gauche : choisir/valider  Clic droit : retour  Glisser droit : tourner la vue  Glisser milieu : déplacer  Molette : zoom\n"
	L"[Clavier] Flèches/WASD : curseur  Entrée/Espace/Z : valider  Échap/X : retour  Q/E : tourner  PgPréc/PgSuiv : zoom  Tab : unité suivante  Maj : avance rapide  F1/H : cet écran\n"
	L"[Manette] Stick gauche/croix : curseur  A : valider  B : retour  LB/RB : tourner  Stick droit : vue  Gâchettes : zoom  X : avance rapide  Y : recentrer  Start : menu  Back : commandes\n"
	L"[Progression]\n"
	L"- Vainquez des ennemis pour gagner de l'EXP et monter de niveau. Des compétences s'apprennent à certains niveaux.\n"
	L"- Au niv. 15, évoluez vers une classe avancée (1 partenaire), au niv. 35 vers une classe ultime (2 partenaires).\n"
	L"- Les niveaux vont de 1 à 100. Les ennemis deviennent plus forts et donnent plus d'EXP au fil de la progression.",
	L"[Basi]\n"
	L"- L'ordine dei turni dipende dall'agilità ed è mostrato in \"Ordine dei turni\" in alto a destra. Le unità veloci si inseriscono e agiscono più spesso.\n"
	L"- Le caselle blu indicano il movimento, quelle rosse la portata d'attacco. Durante il turno nemico viene mostrato anche il suo movimento.\n"
	L"- Dopo il movimento scegli Attacca, Abilità o Attendi. Puoi annullare il movimento prima di agire.\n"
	L"- Colpi critici, abilità e magie mostrano una scena speciale (accorciabile nelle impostazioni).\n"
	L"- Vinci sconfiggendo il boss o tutti i nemici. Perdi se cadono tutti gli alleati.\n"
	L"[Mouse] Clic sinistro: scegli/conferma  Clic destro: indietro  Trascina destro: ruota vista  Trascina centrale: sposta  Rotella: zoom\n"
	L"[Tastiera] Frecce/WASD: cursore  Invio/Spazio/Z: conferma  Esc/X: indietro  Q/E: ruota  PagSu/PagGiù: zoom  Tab: unità successiva  Maiusc: avanti veloce  F1/H: questa schermata\n"
	L"[Gamepad] Stick sinistro/croce: cursore  A: conferma  B: indietro  LB/RB: ruota  Stick destro: vista  Grilletti: zoom  X: avanti veloce  Y: ricentra  Start: menu  Back: comandi\n"
	L"[Crescita]\n"
	L"- Sconfiggi i nemici per ottenere ESP e salire di livello. Le abilità si imparano a certi livelli.\n"
	L"- Al Lv15 evolvi in una classe avanzata (1 partner), al Lv35 in una classe suprema (2 partner).\n"
	L"- I livelli vanno da 1 a 100. Avanzando i nemici diventano più forti e danno più ESP.",
	L"[Básico]\n"
	L"- El orden de turnos depende de la agilidad y se muestra en «Orden de turnos» arriba a la derecha. Las unidades rápidas se adelantan y actúan más a menudo.\n"
	L"- Las casillas azules indican el movimiento y las rojas el alcance de ataque. En el turno enemigo también se muestra su movimiento.\n"
	L"- Tras moverte, elige Atacar, Habilidad o Esperar. Puedes deshacer el movimiento antes de actuar.\n"
	L"- Los críticos, habilidades y hechizos muestran una escena especial (se puede acortar en los ajustes).\n"
	L"- Ganas al derrotar al jefe o a todos los enemigos. Pierdes si caen todos tus aliados.\n"
	L"[Ratón] Clic izquierdo: elegir/confirmar  Clic derecho: volver  Arrastrar derecho: girar vista  Arrastrar central: desplazar  Rueda: zoom\n"
	L"[Teclado] Flechas/WASD: cursor  Intro/Espacio/Z: confirmar  Esc/X: volver  Q/E: girar  RePág/AvPág: zoom  Tab: siguiente unidad  Mayús: avance rápido  F1/H: esta pantalla\n"
	L"[Mando] Stick izquierdo/cruceta: cursor  A: confirmar  B: volver  LB/RB: girar  Stick derecho: vista  Gatillos: zoom  X: avance rápido  Y: centrar  Start: menú  Back: controles\n"
	L"[Progreso]\n"
	L"- Derrota enemigos para ganar EXP y subir de nivel. Las habilidades se aprenden en ciertos niveles.\n"
	L"- En Nv15 evoluciona a una clase avanzada (1 compañero) y en Nv35 a una clase maestra (2 compañeros).\n"
	L"- Las fases van de 1 a 100. Al avanzar, los enemigos son más fuertes y dan más EXP.",
	L"[기본]\n"
	L"- 행동 순서는 민첩으로 정해지며 오른쪽 위의 '행동 순서'에 표시됩니다. 빠른 캐릭터는 끼어들어 더 자주 행동합니다.\n"
	L"- 파란 칸은 이동 범위, 빨간 칸은 공격 범위입니다. 적의 차례에는 적의 이동 범위도 표시됩니다.\n"
	L"- 이동 후 '공격' '스킬' '대기'를 고릅니다. 행동 전이라면 이동을 되돌릴 수 있습니다.\n"
	L"- 필살의 일격이나 스킬·마법에서는 컷인 연출이 들어갑니다 (설정에서 단축 가능).\n"
	L"- 보스를 쓰러뜨리거나 적을 전멸시키면 승리. 아군이 전멸하면 패배입니다.\n"
	L"[마우스] 왼쪽 클릭: 선택·결정  오른쪽 클릭: 돌아가기  오른쪽 드래그: 시점 회전  가운데 드래그: 시점 이동  휠: 줌\n"
	L"[키보드] 방향키/WASD: 커서  Enter/Space/Z: 결정  Esc/X: 돌아가기  Q/E: 회전  PgUp/PgDn: 줌  Tab: 유닛 전환  Shift: 빨리 감기  F1/H: 이 화면\n"
	L"[패드] 왼쪽 스틱/십자키: 커서  A: 결정  B: 돌아가기  LB/RB: 회전  오른쪽 스틱: 시점  트리거: 줌  X: 빨리 감기  Y: 중앙으로  Start: 메뉴  Back: 조작 설명\n"
	L"[육성]\n"
	L"- 적을 쓰러뜨리면 경험치를 얻어 레벨 업. 일정 레벨에서 스킬을 익힙니다.\n"
	L"- Lv15에서 상위 클래스로 (재료 1명), Lv35에서 최상위 클래스로 (재료 2명) 진화할 수 있습니다.\n"
	L"- 스테이지는 1~100. 앞으로 갈수록 적이 강해지고 얻는 경험치도 늘어납니다.",
	L"【基本】\n"
	L"・行动顺序由敏捷决定，显示在右上角的“行动顺序”中。敏捷高的角色会插队，更频繁地行动。\n"
	L"・蓝色格子为移动范围，红色格子为攻击范围。敌方回合时也会显示敌人的移动范围。\n"
	L"・移动后选择“攻击”“技能”或“待机”。行动前可以撤销移动。\n"
	L"・必杀一击、技能和魔法会播放特写演出（可在设置中缩短）。\n"
	L"・击败首领或全灭敌人即为胜利。我方全灭则失败。\n"
	L"【鼠标】左键：选择・确定　右键：返回　右键拖动：旋转视角　中键拖动：移动视角　滚轮：缩放\n"
	L"【键盘】方向键/WASD：光标　Enter/Space/Z：确定　Esc/X：返回　Q/E：旋转　PgUp/PgDn：缩放　Tab：切换单位　Shift：快进　F1/H：本画面\n"
	L"【手柄】左摇杆/十字键：光标　A：确定　B：返回　LB/RB：旋转　右摇杆：视角　扳机：缩放　X：快进　Y：回到中心　Start：菜单　Back：操作说明\n"
	L"【培养】\n"
	L"・击败敌人可获得经验值并升级。达到一定等级会习得技能。\n"
	L"・Lv15可进化为高级职业（素材1名），Lv35可进化为最高级职业（素材2名）。\n"
	L"・关卡为1～100。越往后敌人越强，获得的经验值也越多。",
	L"[الأساسيات]\n"
	L"- يتحدد ترتيب الأدوار بالرشاقة ويظهر في «ترتيب الأدوار» أعلى اليمين. الوحدات السريعة تتقدم وتتحرك أكثر.\n"
	L"- الخانات الزرقاء مدى الحركة والحمراء مدى الهجوم. في دور العدو يظهر مدى حركته أيضًا.\n"
	L"- بعد الحركة اختر هجوم أو مهارة أو انتظار. يمكنك التراجع عن الحركة قبل الفعل.\n"
	L"- الضربات الحاسمة والمهارات والتعاويذ تعرض مشهدًا خاصًا (يمكن تقصيره من الإعدادات).\n"
	L"- تفوز بهزيمة الزعيم أو جميع الأعداء، وتخسر إذا سقط جميع حلفائك.\n"
	L"[الفأرة] نقر أيسر: اختيار/تأكيد  نقر أيمن: رجوع  سحب أيمن: تدوير العرض  سحب أوسط: تحريك  العجلة: تكبير\n"
	L"[لوحة المفاتيح] الأسهم/WASD: المؤشر  Enter/Space/Z: تأكيد  Esc/X: رجوع  Q/E: تدوير  PgUp/PgDn: تكبير  Tab: الوحدة التالية  Shift: تسريع  F1/H: هذه الشاشة\n"
	L"[ذراع التحكم] العصا اليسرى/الأسهم: المؤشر  A: تأكيد  B: رجوع  LB/RB: تدوير  العصا اليمنى: العرض  الزنادان: تكبير  X: تسريع  Y: توسيط  Start: القائمة  Back: التحكم\n"
	L"[التطوير]\n"
	L"- اهزم الأعداء لتكسب الخبرة وترتقي بالمستوى. تُتعلم المهارات عند مستويات معينة.\n"
	L"- عند المستوى 15 تتطور إلى فئة متقدمة (شريك واحد)، وعند 35 إلى فئة عليا (شريكان).\n"
	L"- المراحل من 1 إلى 100. كلما تقدمت ازداد الأعداء قوة وازدادت الخبرة.",
	L"[Основы]\n"
	L"- Порядок ходов зависит от ловкости и показан в «Порядке ходов» справа вверху. Быстрые бойцы вклиниваются и ходят чаще.\n"
	L"- Синие клетки — дальность хода, красные — дальность атаки. В ход врага показывается и его дальность хода.\n"
	L"- После перемещения выберите «Атака», «Навык» или «Ждать». До действия ход можно отменить.\n"
	L"- Критические удары, навыки и заклинания показывают вставку (можно сократить в настройках).\n"
	L"- Победа — убить босса или всех врагов. Поражение — если пали все союзники.\n"
	L"[Мышь] ЛКМ: выбор/подтверждение  ПКМ: назад  Перетаскивание ПКМ: поворот  Перетаскивание СКМ: сдвиг  Колесо: масштаб\n"
	L"[Клавиатура] Стрелки/WASD: курсор  Enter/Space/Z: подтвердить  Esc/X: назад  Q/E: поворот  PgUp/PgDn: масштаб  Tab: следующий боец  Shift: ускорение  F1/H: этот экран\n"
	L"[Геймпад] Левый стик/крестовина: курсор  A: подтвердить  B: назад  LB/RB: поворот  Правый стик: обзор  Курки: масштаб  X: ускорение  Y: центрировать  Start: меню  Back: управление\n"
	L"[Развитие]\n"
	L"- Побеждайте врагов, чтобы получать опыт и уровни. Навыки изучаются на определённых уровнях.\n"
	L"- На ур. 15 — эволюция в продвинутый класс (1 материал), на ур. 35 — в высший класс (2 материала).\n"
	L"- Этапы с 1 по 100. Чем дальше, тем сильнее враги и больше опыта.",
	L"[Grundlagen]\n"
	L"- Die Zugreihenfolge hängt von der Gewandtheit ab und wird oben rechts unter „Zugreihenfolge“ angezeigt. Schnelle Einheiten schieben sich dazwischen und handeln öfter.\n"
	L"- Blaue Felder zeigen die Bewegungs-, rote die Angriffsreichweite. Im Zug des Feindes wird auch dessen Reichweite angezeigt.\n"
	L"- Nach dem Bewegen Angriff, Fertigkeit oder Warten wählen. Vor der Aktion kann die Bewegung zurückgenommen werden.\n"
	L"- Kritische Treffer, Fertigkeiten und Zauber zeigen eine Zwischensequenz (in den Einstellungen kürzbar).\n"
	L"- Sieg durch Besiegen des Bosses oder aller Feinde. Niederlage, wenn alle Verbündeten fallen.\n"
	L"[Maus] Linksklick: wählen/bestätigen  Rechtsklick: zurück  Rechts ziehen: Ansicht drehen  Mitte ziehen: verschieben  Rad: Zoom\n"
	L"[Tastatur] Pfeile/WASD: Cursor  Enter/Leertaste/Z: bestätigen  Esc/X: zurück  Q/E: drehen  Bild↑/Bild↓: Zoom  Tab: nächste Einheit  Umschalt: Vorspulen  F1/H: diese Hilfe\n"
	L"[Gamepad] Linker Stick/Steuerkreuz: Cursor  A: bestätigen  B: zurück  LB/RB: drehen  Rechter Stick: Ansicht  Trigger: Zoom  X: Vorspulen  Y: zentrieren  Start: Menü  Back: Steuerung\n"
	L"[Entwicklung]\n"
	L"- Besiege Feinde, um EP zu sammeln und aufzusteigen. Fertigkeiten werden auf bestimmten Stufen erlernt.\n"
	L"- Ab Lv15 Entwicklung zur fortgeschrittenen Klasse (1 Partner), ab Lv35 zur Meisterklasse (2 Partner).\n"
	L"- Es gibt die Stufen 1 bis 100. Je weiter du kommst, desto stärker werden die Feinde und desto mehr EP gibt es.",
	L"[Básico]\n"
	L"- A ordem dos turnos depende da agilidade e aparece em \"Ordem dos turnos\" no canto superior direito. Unidades rápidas passam à frente e agem mais vezes.\n"
	L"- Casas azuis mostram o alcance de movimento, vermelhas o de ataque. No turno inimigo, o movimento dele também aparece.\n"
	L"- Depois de mover, escolha Atacar, Habilidade ou Esperar. É possível desfazer o movimento antes de agir.\n"
	L"- Críticos, habilidades e magias exibem uma cena especial (pode ser encurtada nas configurações).\n"
	L"- Vença derrotando o chefe ou todos os inimigos. Você perde se todos os aliados caírem.\n"
	L"[Mouse] Clique esquerdo: escolher/confirmar  Clique direito: voltar  Arrastar direito: girar visão  Arrastar meio: mover  Roda: zoom\n"
	L"[Teclado] Setas/WASD: cursor  Enter/Espaço/Z: confirmar  Esc/X: voltar  Q/E: girar  PgUp/PgDn: zoom  Tab: próxima unidade  Shift: acelerar  F1/H: esta tela\n"
	L"[Controle] Analógico esquerdo/direcional: cursor  A: confirmar  B: voltar  LB/RB: girar  Analógico direito: visão  Gatilhos: zoom  X: acelerar  Y: centralizar  Start: menu  Back: controles\n"
	L"[Evolução]\n"
	L"- Derrote inimigos para ganhar EXP e subir de nível. Habilidades são aprendidas em certos níveis.\n"
	L"- No Nv15 evolua para uma classe avançada (1 parceiro), no Nv35 para uma classe suprema (2 parceiros).\n"
	L"- As fases vão de 1 a 100. Quanto mais avança, mais fortes ficam os inimigos e mais EXP eles dão.",
	L"[Basis]\n"
	L"- De beurtvolgorde hangt af van behendigheid en staat rechtsboven bij 'Beurtvolgorde'. Snelle eenheden dringen voor en handelen vaker.\n"
	L"- Blauwe vakken tonen het bewegingsbereik, rode het aanvalsbereik. Tijdens de vijandelijke beurt wordt ook hun bereik getoond.\n"
	L"- Kies na het bewegen Aanval, Vaardigheid of Wachten. Vóór de actie kun je de beweging ongedaan maken.\n"
	L"- Kritieke treffers, vaardigheden en spreuken tonen een tussenscène (in te korten via instellingen).\n"
	L"- Je wint door de baas of alle vijanden te verslaan. Je verliest als alle bondgenoten vallen.\n"
	L"[Muis] Linksklik: kiezen/bevestigen  Rechtsklik: terug  Rechts slepen: beeld draaien  Middel slepen: verschuiven  Wiel: zoom\n"
	L"[Toetsenbord] Pijlen/WASD: cursor  Enter/Spatie/Z: bevestigen  Esc/X: terug  Q/E: draaien  PgUp/PgDn: zoom  Tab: volgende eenheid  Shift: versnellen  F1/H: dit scherm\n"
	L"[Gamepad] Linkerstick/D-pad: cursor  A: bevestigen  B: terug  LB/RB: draaien  Rechterstick: beeld  Triggers: zoom  X: versnellen  Y: centreren  Start: menu  Back: besturing\n"
	L"[Groei]\n"
	L"- Versla vijanden om EP te verdienen en een level te stijgen. Vaardigheden leer je op bepaalde levels.\n"
	L"- Op Lv15 evolueer je naar een gevorderde klasse (1 partner), op Lv35 naar een meesterklasse (2 partners).\n"
	L"- Er zijn levels 1 tot 100. Hoe verder je komt, hoe sterker de vijanden en hoe meer EP.",
	L"[Podstawy]\n"
	L"- Kolejność tur zależy od zwinności i widać ją w „Kolejności tur” w prawym górnym rogu. Szybkie jednostki wskakują przed innych i działają częściej.\n"
	L"- Niebieskie pola to zasięg ruchu, czerwone — zasięg ataku. W turze wroga widać także jego zasięg ruchu.\n"
	L"- Po ruchu wybierz Atak, Umiejętność lub Czekaj. Przed akcją możesz cofnąć ruch.\n"
	L"- Trafienia krytyczne, umiejętności i czary pokazują przerywnik (można skrócić w ustawieniach).\n"
	L"- Wygrywasz, pokonując bossa lub wszystkich wrogów. Przegrywasz, gdy padną wszyscy sojusznicy.\n"
	L"[Mysz] LPM: wybór/zatwierdź  PPM: wstecz  Przeciąganie PPM: obrót widoku  Przeciąganie ŚPM: przesuwanie  Kółko: zoom\n"
	L"[Klawiatura] Strzałki/WASD: kursor  Enter/Spacja/Z: zatwierdź  Esc/X: wstecz  Q/E: obrót  PgUp/PgDn: zoom  Tab: następna jednostka  Shift: przyspieszenie  F1/H: ten ekran\n"
	L"[Pad] Lewa gałka/krzyżak: kursor  A: zatwierdź  B: wstecz  LB/RB: obrót  Prawa gałka: widok  Spusty: zoom  X: przyspieszenie  Y: wyśrodkuj  Start: menu  Back: sterowanie\n"
	L"[Rozwój]\n"
	L"- Pokonuj wrogów, aby zdobywać PD i awansować. Umiejętności poznaje się na określonych poziomach.\n"
	L"- Na poz. 15 ewolucja do klasy zaawansowanej (1 partner), na poz. 35 do klasy mistrzowskiej (2 partnerów).\n"
	L"- Etapy od 1 do 100. Im dalej, tym silniejsi wrogowie i więcej PD.",
	L"[Temel]\n"
	L"- Sıra düzeni çevikliğe bağlıdır ve sağ üstteki \"Sıra düzeni\"nde gösterilir. Hızlı birimler araya girip daha sık hareket eder.\n"
	L"- Mavi kareler hareket menzilini, kırmızı kareler saldırı menzilini gösterir. Düşman sırasında onun hareket menzili de gösterilir.\n"
	L"- Hareketten sonra Saldırı, Yetenek veya Bekle'yi seçin. Eylemden önce hareketi geri alabilirsiniz.\n"
	L"- Kritik vuruşlar, yetenekler ve büyüler ara sahne gösterir (ayarlardan kısaltılabilir).\n"
	L"- Patronu veya tüm düşmanları yenerek kazanırsınız. Tüm müttefikler düşerse kaybedersiniz.\n"
	L"[Fare] Sol tık: seç/onayla  Sağ tık: geri  Sağ sürükle: görüşü döndür  Orta sürükle: kaydır  Tekerlek: yakınlaştır\n"
	L"[Klavye] Oklar/WASD: imleç  Enter/Boşluk/Z: onayla  Esc/X: geri  Q/E: döndür  PgUp/PgDn: yakınlaştır  Tab: sonraki birim  Shift: hızlandır  F1/H: bu ekran\n"
	L"[Oyun kolu] Sol çubuk/yön tuşları: imleç  A: onayla  B: geri  LB/RB: döndür  Sağ çubuk: görüş  Tetikler: yakınlaştır  X: hızlandır  Y: ortala  Start: menü  Back: kontroller\n"
	L"[Gelişim]\n"
	L"- Düşmanları yenerek TP kazanın ve seviye atlayın. Yetenekler belirli seviyelerde öğrenilir.\n"
	L"- Sv15'te ileri sınıfa (1 ortak), Sv35'te usta sınıfa (2 ortak) evrilebilirsiniz.\n"
	L"- Bölümler 1'den 100'e kadardır. İlerledikçe düşmanlar güçlenir ve daha fazla TP verir.")
// ---------------------------------------------------------------- その他
R(TX_MISC_LV, L"Lv", L"Lv", L"Niv.", L"Lv", L"Nv", L"Lv", L"Lv", L"مستوى", L"Ур.", L"Lv", L"Nv", L"Lv", L"Poz.", L"Sv")
R(TX_MISC_EXP, L"EXP", L"EXP", L"EXP", L"ESP", L"EXP", L"EXP", L"EXP", L"خبرة", L"Опыт", L"EP", L"EXP", L"EP", L"PD", L"TP")
R(TX_MISC_CLASS, L"クラス", L"Class", L"Classe", L"Classe", L"Clase", L"클래스", L"职业", L"الفئة", L"Класс", L"Klasse", L"Classe", L"Klasse", L"Klasa", L"Sınıf")
R(TX_MISC_TIER, L"段階", L"Tier", L"Rang", L"Grado", L"Rango", L"단계", L"阶段", L"المرتبة", L"Ступень", L"Stufe", L"Grau", L"Rang", L"Stopień", L"Kademe")
R(TX_MISC_FLY, L"飛行", L"Flying", L"Vol", L"Volo", L"Vuelo", L"비행", L"飞行", L"طيران", L"Полёт", L"Flug", L"Voo", L"Vliegend", L"Lot", L"Uçan")
R(TX_MISC_RANGE, L"射程", L"Range", L"Portée", L"Gittata", L"Alcance", L"사정거리", L"射程", L"المدى", L"Дальность", L"Reichweite", L"Alcance", L"Bereik", L"Zasięg", L"Menzil")
R(TX_MISC_MPCOST, L"消費MP", L"MP Cost", L"Coût PM", L"Costo PM", L"Coste PM", L"소비 MP", L"消耗MP", L"تكلفة السحر", L"Расход ОМ", L"MP-Kosten", L"Custo PM", L"MP-kosten", L"Koszt PM", L"MP bedeli")
R(TX_MISC_PASSIVE, L"常時", L"Passive", L"Passive", L"Passiva", L"Pasiva", L"상시", L"被动", L"دائمة", L"Пассивный", L"Passiv", L"Passiva", L"Passief", L"Pasywna", L"Pasif")
R(TX_MISC_LOADING, L"読み込み中…", L"Loading…", L"Chargement…", L"Caricamento…", L"Cargando…", L"불러오는 중…", L"加载中…", L"جارٍ التحميل…", L"Загрузка…", L"Wird geladen…", L"Carregando…", L"Laden…", L"Wczytywanie…", L"Yükleniyor…")
R(TX_MISC_DXFAIL, L"DirectX 11 を初期化できませんでした", L"Could not initialize DirectX 11", L"Impossible d'initialiser DirectX 11", L"Impossibile inizializzare DirectX 11", L"No se pudo inicializar DirectX 11", L"DirectX 11을 초기화할 수 없습니다", L"无法初始化 DirectX 11", L"تعذّر تهيئة DirectX 11", L"Не удалось инициализировать DirectX 11", L"DirectX 11 konnte nicht initialisiert werden", L"Não foi possível iniciar o DirectX 11", L"Kan DirectX 11 niet initialiseren", L"Nie można zainicjować DirectX 11", L"DirectX 11 başlatılamadı")
R(TX_MISC_ASSETFAIL, L"ゲームデータの読み込みに失敗しました", L"Failed to load game data", L"Échec du chargement des données du jeu", L"Caricamento dei dati di gioco non riuscito", L"Error al cargar los datos del juego", L"게임 데이터를 불러오지 못했습니다", L"游戏数据加载失败", L"فشل تحميل بيانات اللعبة", L"Не удалось загрузить данные игры", L"Spieldaten konnten nicht geladen werden", L"Falha ao carregar os dados do jogo", L"Laden van spelgegevens mislukt", L"Nie udało się wczytać danych gry", L"Oyun verileri yüklenemedi")
// ---------------------------------------------------------------- 設定
R(TX_SET_QUALITY, L"画質", L"Quality", L"Qualité", L"Qualità", L"Calidad", L"화질", L"画质", L"الجودة", L"Качество", L"Qualität", L"Qualidade", L"Kwaliteit", L"Jakość", L"Kalite")
R(TX_SET_Q_LOW, L"軽量", L"Light", L"Léger", L"Leggera", L"Ligera", L"가벼움", L"流畅", L"خفيفة", L"Лёгкое", L"Leicht", L"Leve", L"Licht", L"Lekka", L"Hafif")
R(TX_SET_Q_STD, L"標準", L"Standard", L"Standard", L"Standard", L"Estándar", L"표준", L"标准", L"قياسية", L"Обычное", L"Standard", L"Padrão", L"Standaard", L"Standard", L"Standart")
R(TX_SET_Q_HIGH, L"高画質", L"High", L"Élevée", L"Alta", L"Alta", L"고화질", L"高画质", L"عالية", L"Высокое", L"Hoch", L"Alta", L"Hoog", L"Wysoka", L"Yüksek")
R(TX_SET_ANIM, L"戦闘演出", L"Battle Scenes", L"Scènes de combat", L"Scene di battaglia", L"Escenas de combate", L"전투 연출", L"战斗演出", L"مشاهد القتال", L"Сцены боя", L"Kampfszenen", L"Cenas de batalha", L"Gevechtsscènes", L"Sceny walki", L"Savaş sahneleri")
R(TX_SET_ANIM_FULL, L"フル", L"Full", L"Complètes", L"Complete", L"Completas", L"전체", L"完整", L"كاملة", L"Полные", L"Voll", L"Completas", L"Volledig", L"Pełne", L"Tam")
R(TX_SET_ANIM_SHORT, L"短縮", L"Short", L"Courtes", L"Brevi", L"Cortas", L"단축", L"简短", L"مختصرة", L"Краткие", L"Kurz", L"Curtas", L"Kort", L"Skrócone", L"Kısa")
R(TX_SET_GRID, L"グリッド", L"Grid", L"Grille", L"Griglia", L"Cuadrícula", L"격자", L"网格", L"الشبكة", L"Сетка", L"Raster", L"Grade", L"Raster", L"Siatka", L"Izgara")
R(TX_CR_PREVIEW_HINT, L"ドラッグで回転", L"Drag to rotate", L"Glisser pour tourner", L"Trascina per ruotare", L"Arrastra para girar", L"드래그하여 회전", L"拖动旋转", L"اسحب للتدوير", L"Перетащите для поворота", L"Ziehen zum Drehen", L"Arraste para girar", L"Sleep om te draaien", L"Przeciągnij, aby obrócić", L"Döndürmek için sürükleyin")
R(TX_BT_HINT_CMD, L"コマンドを選択", L"Choose a command", L"Choisissez une commande", L"Scegli un comando", L"Elige un comando", L"명령을 선택", L"选择指令", L"اختر أمرًا", L"Выберите команду", L"Wähle einen Befehl", L"Escolha um comando", L"Kies een opdracht", L"Wybierz polecenie", L"Bir komut seçin")
R(TX_BT_HINT_SKILL, L"スキルの対象を選択", L"Choose a skill target", L"Choisissez la cible de la compétence", L"Scegli il bersaglio dell'abilità", L"Elige el objetivo de la habilidad", L"스킬 대상을 선택", L"选择技能目标", L"اختر هدف المهارة", L"Выберите цель навыка", L"Wähle ein Ziel für die Fertigkeit", L"Escolha o alvo da habilidade", L"Kies een doelwit voor de vaardigheid", L"Wybierz cel umiejętności", L"Yetenek hedefini seçin")
R(TX_BT_RESUME, L"戦闘に戻る", L"Resume Battle", L"Reprendre le combat", L"Riprendi la battaglia", L"Volver al combate", L"전투로 돌아가기", L"返回战斗", L"العودة إلى المعركة", L"Вернуться в бой", L"Kampf fortsetzen", L"Voltar à batalha", L"Gevecht hervatten", L"Wróć do walki", L"Savaşa dön")
R(TX_BT_GOAL, L"勝利条件: ボス撃破または敵全滅", L"Victory: defeat the boss or all enemies", L"Victoire : vaincre le boss ou tous les ennemis", L"Vittoria: sconfiggi il boss o tutti i nemici", L"Victoria: derrota al jefe o a todos los enemigos", L"승리 조건: 보스 격파 또는 적 전멸", L"胜利条件：击败首领或全灭敌人", L"شرط النصر: هزيمة الزعيم أو جميع الأعداء", L"Победа: убить босса или всех врагов", L"Sieg: Boss oder alle Feinde besiegen", L"Vitória: derrote o chefe ou todos os inimigos", L"Overwinning: versla de baas of alle vijanden", L"Zwycięstwo: pokonaj bossa lub wszystkich wrogów", L"Zafer: patronu veya tüm düşmanları yen")
R(TX_BT_ALLY, L"味方", L"Allies", L"Alliés", L"Alleati", L"Aliados", L"아군", L"我方", L"الحلفاء", L"Союзники", L"Verbündete", L"Aliados", L"Bondgenoten", L"Sojusznicy", L"Müttefikler")
R(TX_BT_ENEMY, L"敵", L"Enemies", L"Ennemis", L"Nemici", L"Enemigos", L"적", L"敌方", L"الأعداء", L"Враги", L"Feinde", L"Inimigos", L"Vijanden", L"Wrogowie", L"Düşmanlar")
R(TX_BT_CANCEL, L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal")
R(TX_BT_HINT_KEYS,
	L"カーソル: マウス/矢印/左スティック　決定: クリック/Enter/A　戻る: 右クリック/Esc/B　回転: Q E/LB RB　ズーム: ホイール　早送り: Shift/X",
	L"Cursor: mouse/arrows/left stick  Confirm: click/Enter/A  Back: right click/Esc/B  Rotate: Q E/LB RB  Zoom: wheel  Fast: Shift/X",
	L"Curseur : souris/flèches/stick G  Valider : clic/Entrée/A  Retour : clic droit/Échap/B  Tourner : Q E/LB RB  Zoom : molette  Rapide : Maj/X",
	L"Cursore: mouse/frecce/stick sx  Conferma: clic/Invio/A  Indietro: clic destro/Esc/B  Ruota: Q E/LB RB  Zoom: rotella  Veloce: Maiusc/X",
	L"Cursor: ratón/flechas/stick izq.  Confirmar: clic/Intro/A  Volver: clic derecho/Esc/B  Girar: Q E/LB RB  Zoom: rueda  Rápido: Mayús/X",
	L"커서: 마우스/방향키/왼쪽 스틱  결정: 클릭/Enter/A  돌아가기: 오른쪽 클릭/Esc/B  회전: Q E/LB RB  줌: 휠  빨리 감기: Shift/X",
	L"光标：鼠标/方向键/左摇杆　确定：点击/Enter/A　返回：右键/Esc/B　旋转：Q E/LB RB　缩放：滚轮　快进：Shift/X",
	L"المؤشر: الفأرة/الأسهم/العصا اليسرى  تأكيد: نقر/Enter/A  رجوع: نقر أيمن/Esc/B  تدوير: Q E/LB RB  تكبير: العجلة  تسريع: Shift/X",
	L"Курсор: мышь/стрелки/левый стик  Подтвердить: щелчок/Enter/A  Назад: ПКМ/Esc/B  Поворот: Q E/LB RB  Масштаб: колесо  Ускорение: Shift/X",
	L"Cursor: Maus/Pfeile/linker Stick  Bestätigen: Klick/Enter/A  Zurück: Rechtsklick/Esc/B  Drehen: Q E/LB RB  Zoom: Rad  Schnell: Umschalt/X",
	L"Cursor: mouse/setas/analógico esq.  Confirmar: clique/Enter/A  Voltar: clique direito/Esc/B  Girar: Q E/LB RB  Zoom: roda  Acelerar: Shift/X",
	L"Cursor: muis/pijlen/linkerstick  Bevestigen: klik/Enter/A  Terug: rechtsklik/Esc/B  Draaien: Q E/LB RB  Zoom: wiel  Snel: Shift/X",
	L"Kursor: mysz/strzałki/lewa gałka  Zatwierdź: klik/Enter/A  Wstecz: PPM/Esc/B  Obrót: Q E/LB RB  Zoom: kółko  Szybko: Shift/X",
	L"İmleç: fare/oklar/sol çubuk  Onayla: tık/Enter/A  Geri: sağ tık/Esc/B  Döndür: Q E/LB RB  Yakınlaştır: tekerlek  Hızlı: Shift/X")
R(TX_BT_REJOIN, L"次", L"Next", L"Suivant", L"Dopo", L"Siguiente", L"다음", L"下次", L"التالي", L"Дальше", L"Danach", L"Depois", L"Hierna", L"Potem", L"Sonra")
R(TX_GM_HEAL, L"回復ポイント", L"Heal Point", L"Point de soin", L"Punto cura", L"Punto de cura", L"회복 지점", L"回复点", L"نقطة العلاج", L"Точка лечения", L"Heilpunkt", L"Ponto de cura", L"Genezingspunt", L"Punkt leczenia", L"İyileşme noktası")
R(TX_GM_MIGHT, L"3ターン能力2倍", L"Stats ×2 for 3 turns", L"Stats ×2 pendant 3 tours", L"Stat ×2 per 3 turni", L"Atributos ×2 por 3 turnos", L"3턴 동안 능력 2배", L"3回合能力×2", L"الصفات ×2 لثلاث أدوار", L"Параметры ×2 на 3 хода", L"Werte ×2 für 3 Züge", L"Atributos ×2 por 3 turnos", L"Stats ×2 gedurende 3 beurten", L"Statystyki ×2 przez 3 tury", L"3 tur boyunca statü ×2")
R(TX_GM_WARP, L"ワープ", L"Warp", L"Téléportation", L"Teletrasporto", L"Teletransporte", L"워프", L"传送", L"انتقال", L"Телепорт", L"Warp", L"Teleporte", L"Warp", L"Teleport", L"Işınlanma")
R(TX_BT_ASLEEP, L"未発見", L"Unaware", L"Pas alerté", L"Non allertato", L"Sin alertar", L"미발견", L"未发现", L"غير منتبه", L"Не заметил", L"Unaufmerksam", L"Desatento", L"Onopgemerkt", L"Nieświadomy", L"Fark etmedi")
R(TX_GM_CHEST, L"宝箱", L"Chest", L"Coffre", L"Forziere", L"Cofre", L"보물상자", L"宝箱", L"صندوق", L"Сундук", L"Truhe", L"Baú", L"Kist", L"Skrzynia", L"Sandık")
R(TX_BT_FACE_FRONT, L"正面", L"Front", L"Face", L"Fronte", L"Frente", L"정면", L"正面", L"مواجهة", L"Спереди", L"Vorne", L"Frente", L"Voor", L"Przód", L"Önden")
R(TX_BT_FACE_SIDE, L"側面", L"Side", L"Flanc", L"Fianco", L"Flanco", L"측면", L"侧面", L"جانب", L"Сбоку", L"Seite", L"Flanco", L"Zijkant", L"Bok", L"Yandan")
R(TX_BT_FACE_BACK, L"背面", L"Back", L"Dos", L"Spalle", L"Espalda", L"배후", L"背后", L"خلف", L"Сзади", L"Rücken", L"Costas", L"Rug", L"Tył", L"Arkadan")
R(TX_BT_TRI_ADV, L"有利", L"Advantage", L"Avantage", L"Vantaggio", L"Ventaja", L"유리", L"有利", L"أفضلية", L"Преимущество", L"Vorteil", L"Vantagem", L"Voordeel", L"Przewaga", L"Üstün")
R(TX_BT_TRI_DIS, L"不利", L"Disadvantage", L"Désavantage", L"Svantaggio", L"Desventaja", L"불리", L"不利", L"ضعف", L"Невыгода", L"Nachteil", L"Desvantagem", L"Nadeel", L"Wada", L"Zayıf")
R(TX_BT_BOWFLY, L"対飛行", L"Anti-air", L"Antiaérien", L"Antiaereo", L"Antiaéreo", L"대비행", L"对飞行", L"ضد الطيران", L"Против полёта", L"Gegen Flug", L"Antiaéreo", L"Tegen vliegen", L"Przeciw lotowi", L"Uçuşa karşı")
R(TX_BT_DELAY, L"ディレイ", L"Delay", L"Retard", L"Ritardo", L"Retraso", L"지연", L"延迟", L"تأخير", L"Задержка", L"Verzögerung", L"Atraso", L"Vertraging", L"Opóźnienie", L"Gecikme")
R(TX_BT_FOLLOW, L"隣接追撃", L"Follow-up", L"Poursuite", L"Incalzare", L"Seguimiento", L"인접 추격", L"相邻追击", L"مطاردة مجاورة", L"Добивание", L"Nachsetzen", L"Perseguição", L"Vervolg", L"Dogrywka", L"Takip")
R(TX_BT_SHOVE, L"体当たり", L"Shove", L"Poussée", L"Spinta", L"Empujón", L"몸통박치기", L"撞击", L"دفعة", L"Толчок", L"Stoß", L"Empurrão", L"Duw", L"Pchnięcie", L"İtme")
R(TX_BT_DOOR, L"扉を開ける", L"Open Door", L"Ouvrir la porte", L"Apri la porta", L"Abrir la puerta", L"문을 연다", L"开门", L"افتح الباب", L"Открыть дверь", L"Tür öffnen", L"Abrir a porta", L"Deur openen", L"Otwórz drzwi", L"Kapıyı aç")
R(TX_TER_DOOR, L"扉", L"Door", L"Porte", L"Porta", L"Puerta", L"문", L"门", L"باب", L"Дверь", L"Tür", L"Porta", L"Deur", L"Drzwi", L"Kapı")
R(TX_CL_ALCHEMIST, L"錬金師", L"Alchemist", L"Alchimiste", L"Alchimista", L"Alquimista", L"연금술사", L"炼金师", L"خيميائي", L"Алхимик", L"Alchemist", L"Alquimista", L"Alchemist", L"Alchemik", L"Simyacı")
R(TX_CL_ARCHALCHEMIST, L"大錬金師", L"Archalchemist", L"Archalchimiste", L"Arcialchimista", L"Arquialquimista", L"대연금술사", L"大炼金师", L"خيميائي أعظم", L"Архиалхимик", L"Erzalchemist", L"Arquialquimista", L"Aartsalchemist", L"Arcyalchemik", L"Baş simyacı")
R(TX_CLD_ALCHEMIST, L"石と薬草を薬と武具の力に変える。", L"Turns ore and herbs into medicine and might.", L"Change minerai et herbes en remèdes et en force.", L"Trasforma minerali ed erbe in cure e potenza.", L"Convierte mineral y hierbas en medicina y poder.", L"광석과 약초를 약과 힘으로 바꾼다.", L"把矿石和药草炼成药与力量。", L"يحوّل الخام والأعشاب إلى دواء وقوة.", L"Превращает руду и травы в зелья и силу.", L"Wandelt Erz und Kräuter in Tränke und Kraft.", L"Transforma minério e ervas em cura e força.", L"Maakt van erts en kruiden drank en kracht.", L"Zmienia rudę i zioła w lek i siłę.", L"Cevher ve otu ilaç ile güce çevirir.")
R(TX_CLD_ARCHALCHEMIST, L"元素そのものを組み替える大錬金師。", L"An archalchemist who rewrites the elements.", L"Un archalchimiste qui réécrit les éléments.", L"Un arcialchimista che riscrive gli elementi.", L"Un arquialquimista que reescribe los elementos.", L"원소 자체를 다시 짜는 대연금술사.", L"重组元素本身的大炼金师。", L"خيميائي أعظم يعيد صياغة العناصر.", L"Архиалхимик, переписывающий стихии.", L"Ein Erzalchemist, der die Elemente neu schreibt.", L"Um arquialquimista que reescreve os elementos.", L"Een aartsalchemist die de elementen herschrijft.", L"Arcyalchemik, który przepisuje żywioły.", L"Elementleri yeniden yazan baş simyacı.")
R(TX_SK_CLEAVE, L"薙ぎ払い", L"Cleave", L"Balayage", L"Falciata", L"Tajo", L"휩쓸기", L"横扫", L"اكتساح", L"Размах", L"Schwung", L"Golpe amplo", L"Zwaai", L"Zamach", L"Biçme")
R(TX_SK_SNIPE, L"狙撃", L"Snipe", L"Tir précis", L"Tiro preciso", L"Disparo", L"저격", L"狙击", L"قنص", L"Выстрел", L"Scharfschuss", L"Tiro certeiro", L"Scherpschot", L"Strzał", L"Nişan")
R(TX_SK_VENOMEDGE, L"毒刃", L"Venom Edge", L"Lame venimeuse", L"Lama velenosa", L"Filo venenoso", L"독날", L"毒刃", L"نصل سام", L"Ядовитый клинок", L"Giftklinge", L"Lâmina venenosa", L"Gifkling", L"Jadowite ostrze", L"Zehirli ağız")
R(TX_SK_SANCTUARY, L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"성역", L"圣域", L"قدس", L"Святилище", L"Heiligtum", L"Santuário", L"Heiligdom", L"Sanktuarium", L"Sığınak")
R(TX_SK_SMITE, L"断罪", L"Smite", L"Châtiment", L"Punizione", L"Castigo", L"단죄", L"断罪", L"قصاص", L"Кара", L"Sühne", L"Punição", L"Vergelding", L"Kara", L"Azap")
R(TX_SK_GRAVITY, L"重力", L"Gravity", L"Gravité", L"Gravità", L"Gravedad", L"중력", L"重力", L"جاذبية", L"Тяжесть", L"Schwerkraft", L"Gravidade", L"Zwaartekracht", L"Grawitacja", L"Yerçekimi")
R(TX_SK_WARCRY, L"雄叫び", L"Warcry", L"Cri de guerre", L"Urlo di guerra", L"Grito de guerra", L"함성", L"战吼", L"صرخة حرب", L"Боевой клич", L"Kriegsschrei", L"Grito de guerra", L"Strijdkreet", L"Okrzyk", L"Savaş çığlığı")
R(TX_SK_RENEW, L"再生", L"Renew", L"Renouveau", L"Rinnovamento", L"Renovar", L"재생", L"再生", L"تجديد", L"Обновление", L"Erneuerung", L"Renovar", L"Vernieuwen", L"Odnowa", L"Yenileme")
R(TX_SK_MIRROR, L"鏡盾", L"Mirror", L"Miroir", L"Specchio", L"Espejo", L"거울 방패", L"镜盾", L"مرآة", L"Зеркало", L"Spiegel", L"Espelho", L"Spiegel", L"Zwierciadło", L"Ayna")
R(TX_SK_GODSLASH, L"神斬", L"Godslash", L"Entaille divine", L"Fendente divino", L"Tajo divino", L"신참", L"神斩", L"قطع إلهي", L"Божественный удар", L"Götterhieb", L"Corte divino", L"Godenslag", L"Boski cios", L"Tanrı kesisi")
R(TX_SK_RUIN, L"破滅", L"Ruin", L"Ruine", L"Rovina", L"Ruina", L"파멸", L"破灭", L"خراب", L"Погибель", L"Ruin", L"Ruína", L"Ruïne", L"Ruina", L"Yıkım")
R(TX_SK_APOC, L"終焉", L"Apocalypse", L"Apocalypse", L"Apocalisse", L"Apocalipsis", L"종언", L"终焉", L"نهاية", L"Апокалипсис", L"Apokalypse", L"Apocalipse", L"Apocalyps", L"Apokalipsa", L"Kıyamet")
R(TX_SK_TRANSMUTE, L"錬成", L"Transmute", L"Transmutation", L"Trasmutazione", L"Transmutar", L"연성", L"炼成", L"تحويل", L"Трансмутация", L"Transmutation", L"Transmutar", L"Transmutatie", L"Transmutacja", L"Dönüşüm")
R(TX_SKD_CLEAVE, L"周囲の敵をまとめて払う。", L"Sweeps nearby enemies.", L"Balaye les ennemis proches.", L"Falcia i nemici vicini.", L"Barre a los enemigos cercanos.", L"주변의 적을 함께 휩쓴다.", L"横扫周围的敌人。", L"يكتسح الأعداء القريبين.", L"Сметает ближних врагов.", L"Fegt nahe Feinde hinweg.", L"Varre inimigos próximos.", L"Veegt nabije vijanden weg.", L"Zmiata pobliskich wrogów.", L"Yakındaki düşmanları biçer.")
R(TX_SKD_SNIPE, L"遠くの一点を高い必殺率で射る。", L"A long shot with a high critical rate.", L"Un tir lointain au fort taux critique.", L"Un tiro lontano con alto critico.", L"Un disparo lejano con mucho crítico.", L"먼 곳을 높은 치명타로 쏜다.", L"以高暴击射击远处一点。", L"طلقة بعيدة بنسبة حرجة عالية.", L"Дальний выстрел с высоким критом.", L"Ein Fernschuss mit hoher Kritchance.", L"Um tiro distante com alto crítico.", L"Een schot van ver met hoge krit.", L"Daleki strzał z wysokim krytem.", L"Uzak bir noktaya yüksek kritik atış.")
R(TX_SKD_VENOMEDGE, L"刃に毒をまとい、敵を毒にする。", L"A poisoned blade that toxifies the foe.", L"Une lame empoisonnée qui intoxique.", L"Una lama avvelenata che intossica.", L"Una hoja envenenada que intoxica.", L"독이 깃든 칼날로 적을 중독시킨다.", L"刀刃附毒，使敌人中毒。", L"نصل مسموم يسمّم الخصم.", L"Отравленный клинок отравляет врага.", L"Eine Giftklinge, die den Feind vergiftet.", L"Uma lâmina venenosa que envenena.", L"Een gifkling die de vijand vergiftigt.", L"Zatrute ostrze, które zatruwa wroga.", L"Zehirli ağız düşmanı zehirler.")
R(TX_SKD_SANCTUARY, L"周囲の味方をまとめて癒す。", L"Heals nearby allies together.", L"Soigne ensemble les alliés proches.", L"Cura insieme gli alleati vicini.", L"Cura a los aliados cercanos.", L"주변 아군을 함께 치유한다.", L"一起治疗周围的同伴。", L"يشفي الحلفاء القريبين معًا.", L"Лечит ближних союзников разом.", L"Heilt nahe Verbündete gemeinsam.", L"Cura aliados próximos juntos.", L"Genezing voor nabije bondgenoten.", L"Leczy pobliskich sojuszników razem.", L"Yakındaki müttefikleri birlikte iyileştirir.")
R(TX_SKD_SMITE, L"聖なる一撃で敵を断つ。", L"A holy blow that judges the foe.", L"Un coup sacré qui juge l'ennemi.", L"Un colpo sacro che giudica il nemico.", L"Un golpe sagrado que juzga al enemigo.", L"성스러운 일격으로 적을 가른다.", L"以圣洁的一击斩断敌人。", L"ضربة مقدسة تدين العدو.", L"Святой удар, судящий врага.", L"Ein heiliger Schlag, der den Feind richtet.", L"Um golpe sagrado que julga o inimigo.", L"Een heilige slag die de vijand oordeelt.", L"Święty cios, który sądzi wroga.", L"Düşmanı yargılayan kutsal bir darbe.")
R(TX_SKD_GRAVITY, L"重力で範囲を縛り、次の手番を遅らせる。", L"Binds an area and delays the next turn.", L"Lie une zone et retarde le prochain tour.", L"Lega un'area e ritarda il turno.", L"Ata un área y retrasa el turno.", L"범위를 묶고 다음 행동을 늦춘다.", L"以重力束缚范围并推迟下次行动。", L"يقيّد مساحة ويؤخر الدور التالي.", L"Связывает область и откладывает ход.", L"Bindet ein Gebiet und verzögert den Zug.", L"Prende uma área e atrasa o turno.", L"Bindt een gebied en vertraagt de beurt.", L"Wiąże obszar i opóźnia turę.", L"Bir alanı bağlar ve sırayı geciktirir.")
R(TX_SKD_WARCRY, L"雄叫んで周囲の味方の攻撃を上げる。", L"A shout that raises nearby allies' attack.", L"Un cri qui augmente l'attaque des alliés proches.", L"Un urlo che alza l'attacco degli alleati vicini.", L"Un grito que sube el ataque de los aliados cercanos.", L"함성으로 주변 아군의 공격을 올린다.", L"战吼提高周围同伴的攻击。", L"صرخة ترفع هجوم الحلفاء القريبين.", L"Клич повышает атаку ближних союзников.", L"Ein Schrei hebt die Attacke naher Verbündeter.", L"Um grito que sobe o ataque dos aliados próximos.", L"Een kreet die de aanval van nabije bondgenoten verhoogt.", L"Okrzyk podnosi atak pobliskich sojuszników.", L"Yakındaki müttefiklerin saldırısını yükselten çığlık.")
R(TX_SKD_RENEW, L"大きく失った体力を戻す。", L"Restores a large amount of health.", L"Rend une grande part de vitalité.", L"Restituisce molta salute.", L"Recupera mucha salud.", L"잃은 체력을 크게 되돌린다.", L"大幅恢复失去的体力。", L"يعيد قدرًا كبيرًا من الصحة.", L"Возвращает много здоровья.", L"Stellt viel Gesundheit wieder her.", L"Recupera muita vida.", L"Herstelt veel gezondheid.", L"Przywraca dużo zdrowia.", L"Çok miktarda can geri getirir.")
R(TX_SKD_MIRROR, L"鏡の盾で防御を大きく上げる。", L"A mirror shield that greatly raises defense.", L"Un bouclier miroir qui augmente beaucoup la défense.", L"Uno scudo specchio che alza molto la difesa.", L"Un escudo espejo que sube mucho la defensa.", L"거울 방패로 방어를 크게 올린다.", L"镜盾大幅提高防御。", L"درع مرآة يرفع الدفاع كثيرًا.", L"Зеркальный щит сильно повышает защиту.", L"Ein Spiegel schild hebt die Abwehr stark.", L"Um escudo espelho que sobe muito a defesa.", L"Een spiegel schild dat de verdediging sterk verhoogt.", L"Tarcza zwierciadło mocno podnosi obronę.", L"Ayna kalkan savunmayı çok yükseltir.")
R(TX_SKD_GODSLASH, L"必殺。神域の一刀。", L"Ultimate. One slash from the divine realm.", L"Ultime. Une taille venue du divin.", L"Finale. Un fendente dal regno divino.", L"Definitiva. Un tajo del reino divino.", L"필살. 신역의 한 칼.", L"必杀。神域的一刀。", L"قاضية. قطعة من العالم الإلهي.", L"Ультимейт. Удар из божественного края.", L"Ultimativ. Ein Hieb aus dem göttlichen Reich.", L"Suprema. Um corte do reino divino.", L"Ultiem. Een slag uit het goddelijke rijk.", L"Ultimatum. Jedno cięcie z boskiej dziedziny.", L"Ultimate. Tanrı diyarından bir kesik.")
R(TX_SKD_RUIN, L"必殺。広範囲を破滅させる闇。", L"Ultimate. Darkness that ruins a wide area.", L"Ultime. Des ténèbres qui ruinent une large zone.", L"Finale. Un'oscurità che rovina una vasta area.", L"Definitiva. Oscuridad que arruina un área amplia.", L"필살. 넓은 범위를 파멸시키는 어둠.", L"必杀。毁灭大范围的黑暗。", L"قاضية. ظلام يخرب مساحة واسعة.", L"Ультимейт. Тьма, губящая большую область.", L"Ultimativ. Finsternis, die ein weites Gebiet ruiniert.", L"Suprema. Escuridão que arruína uma área ampla.", L"Ultim. Duisternis die een breed gebied ruïneert.", L"Ultimatum. Ciemność rujnująca szeroki obszar.", L"Ultimate. Geniş alanı yıkan karanlık.")
R(TX_SKD_APOC, L"必殺。周囲を終焉の炎で包む。", L"Ultimate. Wraps the area in ending fire.", L"Ultime. Enveloppe la zone d'un feu final.", L"Finale. Avvolge l'area in un fuoco finale.", L"Definitiva. Envuelve el área en fuego final.", L"필살. 주변을 종언의 불꽃으로 감싼다.", L"必杀。以终焉之炎包裹周围。", L"قاضية. يلف المكان بنار النهاية.", L"Ультимейт. Окутывает область огнём конца.", L"Ultimativ. Hüllt die Umgebung in Endfeuer.", L"Suprema. Envolve a área em fogo final.", L"Ultim. Hult het gebied in eindvuur.", L"Ultimatum. Otula obszar końcowym ogniem.", L"Ultimate. Çevreyi son ateşle sarar.")
R(TX_SKD_TRANSMUTE, L"鉱石を1つ消費し、攻撃を上げる。", L"Consumes one ore and raises attack.", L"Consomme un minerai et augmente l'attaque.", L"Consuma un minerale e alza l'attacco.", L"Consume un mineral y sube el ataque.", L"광석 1개를 소비해 공격을 올린다.", L"消耗一块矿石并提高攻击。", L"يستهلك خامًا ويرفع الهجوم.", L"Тратит одну руду и повышает атаку.", L"Verbraucht ein Erz und hebt die Attacke.", L"Consome um minério e sobe o ataque.", L"Verbruikt één erts en verhoogt de aanval.", L"Zużywa jedną rudę i podnosi atak.", L"Bir cevher harcar ve saldırıyı yükseltir.")
R(TX_BT_ITEM, L"道具", L"Item", L"Objet", L"Oggetto", L"Objeto", L"도구", L"道具", L"أداة", L"Предмет", L"Gegenstand", L"Item", L"Voorwerp", L"Przedmiot", L"Eşya")
R(TX_BT_ULT, L"必殺", L"ULTIMATE", L"ULTIME", L"FINALE", L"DEFINITIVA", L"필살", L"必杀", L"قاضية", L"УЛЬТИМЕЙТ", L"ULTIMATIV", L"SUPREMA", L"ULTIEM", L"ULTIMATUM", L"ULTIMATE")
R(TX_BASE_CODEX, L"技能と成長", L"Skills and Growth", L"Compétences et croissance", L"Abilità e crescita", L"Habilidades y crecimiento", L"기술과 성장", L"技能与成长", L"المهارات والنمو", L"Навыки и рост", L"Fertigkeiten und Wachstum", L"Habilidades e crescimento", L"Vaardigheden en groei", L"Umiejętności i rozwój", L"Yetenek ve gelişim")
R(TX_CDX_SKILL, L"スキル一覧", L"Skill List", L"Liste des compétences", L"Elenco abilità", L"Lista de habilidades", L"스킬 목록", L"技能一览", L"قائمة المهارات", L"Список навыков", L"Fertigkeitsliste", L"Lista de habilidades", L"Vaardigheidslijst", L"Lista umiejętności", L"Yetenek listesi")
R(TX_CDX_GROW, L"成長表", L"Growth", L"Croissance", L"Crescita", L"Crecimiento", L"성장표", L"成长表", L"النمو", L"Рост", L"Wachstum", L"Crescimento", L"Groei", L"Rozwój", L"Gelişim")
R(TX_CDX_ALCH, L"錬金", L"Alchemy", L"Alchimie", L"Alchimia", L"Alquimia", L"연금", L"炼金", L"الخيمياء", L"Алхимия", L"Alchemie", L"Alquimia", L"Alchemie", L"Alchemia", L"Simya")
R(TX_CDX_EXP, L"次のレベルまでの経験値", L"Experience to next level", L"Expérience pour le niveau suivant", L"Esperienza per il livello successivo", L"Experiencia para el siguiente nivel", L"다음 레벨까지 경험치", L"到下一级的经验", L"الخبرة للمستوى التالي", L"Опыт до следующего уровня", L"Erfahrung bis zum nächsten Level", L"Experiência para o próximo nível", L"Ervaring tot het volgende level", L"Doświadczenie do następnego poziomu", L"Sonraki seviye için deneyim")
R(TX_CDX_NEXT, L"必要", L"Need", L"Requis", L"Serve", L"Hace falta", L"필요", L"需要", L"يلزم", L"Нужно", L"Benötigt", L"Precisa", L"Nodig", L"Trzeba", L"Gerek")
R(TX_CDX_JOB, L"転職に必要なレベル（1次から順に）", L"Level to promote (from 1st job upward)", L"Niveau pour changer de classe (depuis la 1re)", L"Livello per la promozione (dalla 1ª classe)", L"Nivel para promover (desde la 1.ª clase)", L"전직에 필요한 레벨(1차부터)", L"转职所需等级（从1次起）", L"المستوى للترقية من الوظيفة الأولى", L"Уровень для смены класса, с 1-го", L"Stufe für den Klassenwechsel, ab der 1.", L"Nível para promover, a partir da 1.ª", L"Level om te promoveren, vanaf de 1e", L"Poziom zmiany klasy, od 1.", L"Sınıf atlamak için seviye, 1.'den")
R(TX_IT_ORE, L"鉱石", L"Ore", L"Minerai", L"Minerale", L"Mineral", L"광석", L"矿石", L"خام", L"Руда", L"Erz", L"Minério", L"Erts", L"Ruda", L"Cevher")
R(TX_IT_HERB, L"薬草", L"Herb", L"Herbe", L"Erba", L"Hierba", L"약초", L"药草", L"عشب", L"Трава", L"Kraut", L"Erva", L"Kruid", L"Zioło", L"Ot")
R(TX_IT_CRYSTAL, L"結晶", L"Crystal", L"Cristal", L"Cristallo", L"Cristal", L"결정", L"结晶", L"بلورة", L"Кристалл", L"Kristall", L"Cristal", L"Kristal", L"Kryształ", L"Kristal")
R(TX_IT_SHARD, L"欠片", L"Shard", L"Éclat", L"Scheggia", L"Fragmento", L"파편", L"碎片", L"شظية", L"Осколок", L"Splitter", L"Fragmento", L"Scherf", L"Odłamek", L"Parça")
R(TX_IT_ELIXIR, L"回復薬", L"Elixir", L"Élixir", L"Elisir", L"Elixir", L"회복약", L"回复药", L"إكسير", L"Эликсир", L"Elixier", L"Elixir", L"Elixer", L"Eliksir", L"İksir")
R(TX_IT_MIGHT, L"剛力薬", L"Might Tonic", L"Tonique de force", L"Tonico di forza", L"Tónico de fuerza", L"강력약", L"刚力药", L"مقوّي", L"Зелье силы", L"Krafttrunk", L"Tônico de força", L"Krachtdrank", L"Eliksir mocy", L"Güç iksiri")
R(TX_IT_DELAY, L"遅延弾", L"Delay Shot", L"Balle de retard", L"Colpo di ritardo", L"Disparo de retraso", L"지연탄", L"延迟弹", L"طلقة تأخير", L"Замедляющий выстрел", L"Verzögerungsschuss", L"Tiro de atraso", L"Vertragend schot", L"Pocisk opóźnienia", L"Gecikme atışı")
R(TX_RK_LINE0, L"剣の系譜。段が上がるほど一刀が重い。", L"The sword line. Each rank makes the blade heavier.", L"La lignée de l'épée. Chaque rang alourdit la lame.", L"La stirpe della spada. Ogni rango appesantisce la lama.", L"El linaje de la espada. Cada rango hace el filo más pesado.", L"검의 계보. 단이 오를수록 한 칼이 무겁다.", L"剑之系谱。阶位越高，一刀越重。", L"سلالة السيف. كل رتبة تجعل النصل أثقل.", L"Род меча. С каждым рангом удар тяжелее.", L"Die Schwertlinie. Jeder Rang macht die Klinge schwerer.", L"A linhagem da espada. Cada grau deixa o corte mais pesado.", L"De zwaardlijn. Elke rang maakt het lemmet zwaarder.", L"Ród miecza. Każdy stopień czyni cięcie cięższym.", L"Kılıç soyu. Her rütbe kesişi ağırlaştırır.")
R(TX_RK_LINE1, L"魔の系譜。禁呪へ近づく。", L"The mage line, drawing toward forbidden spells.", L"La lignée magique, vers les sorts interdits.", L"La stirpe magica, verso le magie proibite.", L"El linaje mágico, hacia los conjuros prohibidos.", L"마의 계보. 금주에 가까워진다.", L"魔之系谱，走向禁咒。", L"سلالة السحر نحو التعاويذ المحرمة.", L"Род магии, ближе к запретным чарам.", L"Die Magierlinie, hin zu verbotenen Sprüchen.", L"A linhagem mágica, rumo às magias proibidas.", L"De magielijn, naar verboden spreuken.", L"Ród magii, ku zaklętym formułom.", L"Büyü soyu, yasak büyülere yaklaşır.")
R(TX_RK_LINE2, L"聖の系譜。癒しが奇跡になる。", L"The holy line. Healing becomes miracle.", L"La lignée sainte. Le soin devient miracle.", L"La stirpe santa. La cura diventa miracolo.", L"El linaje santo. La cura se vuelve milagro.", L"성의 계보. 치유가 기적이 된다.", L"圣之系谱。治愈成为奇迹。", L"سلالة القداسة. الشفاء يصير معجزة.", L"Святой род. Лечение становится чудом.", L"Die heilige Linie. Heilung wird zum Wunder.", L"A linhagem santa. A cura vira milagre.", L"De heilige lijn. Genezing wordt een wonder.", L"Ród świętości. Leczenie staje się cudem.", L"Kutsal soy. İyileşme mucizeye döner.")
R(TX_RK_LINE3, L"影の系譜。音もなく急所を突く。", L"The shadow line. Strikes the vitals without a sound.", L"La lignée de l'ombre. Frappe les points vitaux sans bruit.", L"La stirpe dell'ombra. Colpisce i punti vitali senza rumore.", L"El linaje de la sombra. Hiere los puntos vitales sin ruido.", L"그림자의 계보. 소리 없이 급소를 찌른다.", L"影之系谱。无声刺中要害。", L"سلالة الظل. تضرب المقتل بلا صوت.", L"Род тени. Бьёт в уязвимое место без звука.", L"Die Schattenlinie. Trifft lautlos die Blöße.", L"A linhagem da sombra. Fere o ponto vital sem som.", L"De schaduwlijn. Treft geruisloos de zwakke plek.", L"Ród cienia. Bez dźwięku trafia w czuły punkt.", L"Gölge soyu. Sessizce can alacak yeri vurur.")
R(TX_RK_LINE4, L"竜の系譜。翼と炎が位とともに増す。", L"The dragon line. Wings and fire grow with each rank.", L"La lignée du dragon. Ailes et feu croissent avec le rang.", L"La stirpe del drago. Ali e fuoco crescono col rango.", L"El linaje del dragón. Alas y fuego crecen con el rango.", L"용의 계보. 날개와 불꽃이 위와 함께 자란다.", L"龙之系谱。翼与火焰随位而增。", L"سلالة التنين. الجناح والنار ينموان مع الرتبة.", L"Род дракона. Крылья и пламя растут с рангом.", L"Die Drachenlinie. Flügel und Feuer wachsen mit dem Rang.", L"A linhagem do dragão. Asas e fogo crescem com o grau.", L"De drakenlijn. Vleugels en vuur groeien met de rang.", L"Ród smoka. Skrzydła i ogień rosną ze stopniem.", L"Ejder soyu. Kanat ve ateş rütbeyle büyür.")
R(TX_RK_LINE5, L"錬金の系譜。石を世界の法則に変える。", L"The alchemy line. Turns stone into the laws of the world.", L"La lignée alchimique. Change la pierre en lois du monde.", L"La stirpe alchemica. Cambia la pietra in leggi del mondo.", L"El linaje alquímico. Vuelve la piedra en leyes del mundo.", L"연금의 계보. 돌을 세계의 법칙으로 바꾼다.", L"炼金系谱。把石头变成世界的法则。", L"سلالة الخيمياء. تحوّل الحجر إلى قوانين العالم.", L"Род алхимии. Превращает камень в законы мира.", L"Die Alchemiielinie. Macht aus Stein die Gesetze der Welt.", L"A linhagem da alquimia. Transforma pedra em leis do mundo.", L"De alchemielijn. Maakt van steen de wetten van de wereld.", L"Ród alchemii. Zamienia kamień w prawa świata.", L"Simya soyu. Taşı dünyanın yasasına çevirir.")

R(TX_RK_FIRST + 0, L"覇王", L"Overlord", L"Suzerain", L"Sovrano", L"Soberano", L"패왕", L"霸王", L"سيد", L"Владыка", L"Herrscher", L"Suserano", L"Heerser", L"Hegemon", L"Hükümdar")
R(TX_RK_FIRST + 1, L"剣聖", L"Sword Saint", L"Saint de l'épée", L"Santo della spada", L"Santo de la espada", L"검성", L"剑圣", L"قديس السيف", L"Святой меча", L"Schwertheiliger", L"Santo da espada", L"Zwaardheilige", L"Święty miecza", L"Kılıç ermişi")
R(TX_RK_FIRST + 2, L"軍神", L"War God", L"Dieu de la guerre", L"Dio della guerra", L"Dios de la guerra", L"군신", L"军神", L"إله الحرب", L"Бог войны", L"Kriegsgott", L"Deus da guerra", L"Oorlogsgod", L"Bóg wojny", L"Savaş tanrısı")
R(TX_RK_FIRST + 3, L"覇皇", L"High King", L"Haut roi", L"Alto re", L"Alto rey", L"패황", L"霸皇", L"ملك أعلى", L"Верховный царь", L"Hochkönig", L"Alto rei", L"Hoge koning", L"Wysoki król", L"Yüce kral")
R(TX_RK_FIRST + 4, L"天剣", L"Sky Blade", L"Lame céleste", L"Lama celeste", L"Hoja celeste", L"천검", L"天剑", L"نصل السماء", L"Небесный клинок", L"Himmelsklinge", L"Lâmina celeste", L"Hemelzwaard", L"Niebiańskie ostrze", L"Gök kılıcı")
R(TX_RK_FIRST + 5, L"神剣", L"God Blade", L"Lame divine", L"Lama divina", L"Hoja divina", L"신검", L"神剑", L"نصل إلهي", L"Божественный клинок", L"Götterklinge", L"Lâmina divina", L"Godenzwaard", L"Boskie ostrze", L"Tanrı kılıcı")
R(TX_RK_FIRST + 6, L"終焉の剣", L"End Blade", L"Lame finale", L"Lama finale", L"Hoja final", L"종언의 검", L"终焉之剑", L"سيف النهاية", L"Клинок конца", L"Endklinge", L"Lâmina final", L"Eindzwaard", L"Ostrze końca", L"Son kılıç")
R(TX_RK_FIRST + 7, L"大魔導師", L"Archmage", L"Archimage", L"Arcimago", L"Archimago", L"대마도사", L"大魔导师", L"ساحر أعظم", L"Архимаг", L"Erzmagier", L"Arquimago", L"Aartsmagiër", L"Arcykapłan magii", L"Baş büyücü")
R(TX_RK_FIRST + 8, L"禁呪師", L"Forbidden Caster", L"Lanceur interdit", L"Lanciatore proibito", L"Lanzador prohibido", L"금주술사", L"禁咒师", L"ملقي المحرّم", L"Заклинатель запретного", L"Verbotszauberer", L"Lançador proibido", L"Verboden werper", L"Zaklinacz zakazu", L"Yasak büyücü")
R(TX_RK_FIRST + 9, L"星詠み", L"Star Reader", L"Lecteur d'étoiles", L"Lettore di stelle", L"Lector de estrellas", L"별점쟁이", L"咏星者", L"قارئ النجوم", L"Чтец звёзд", L"Sternleser", L"Leitor de estrelas", L"Sterrenlezer", L"Czytacz gwiazd", L"Yıldız okuru")
R(TX_RK_FIRST + 10, L"虚ろの王", L"King of Void", L"Roi du vide", L"Re del vuoto", L"Rey del vacío", L"허무의 왕", L"虚空之王", L"ملك الفراغ", L"Король пустоты", L"König der Leere", L"Rei do vazio", L"Koning van de leegte", L"Król pustki", L"Boşluğun kralı")
R(TX_RK_FIRST + 11, L"災厄", L"Calamity", L"Calamité", L"Calamità", L"Calamidad", L"재액", L"灾厄", L"كارثة", L"Бедствие", L"Katastrophe", L"Calamidade", L"Rampspoed", L"Klęska", L"Felaket")
R(TX_RK_FIRST + 12, L"魔神", L"Demon God", L"Dieu démon", L"Dio demone", L"Dios demonio", L"마신", L"魔神", L"إله شيطان", L"Бог-демон", L"Dämonengott", L"Deus demônio", L"Demonengod", L"Bóg demon", L"İblis tanrı")
R(TX_RK_FIRST + 13, L"創世", L"Genesis", L"Genèse", L"Genesi", L"Génesis", L"창세", L"创世", L"التكوين", L"Творение", L"Schöpfung", L"Gênese", L"Schepping", L"Stworzenie", L"Yaratılış")
R(TX_RK_FIRST + 14, L"大司祭", L"High Priest", L"Grand prêtre", L"Grande sacerdote", L"Sumo sacerdote", L"대사제", L"大司祭", L"كاهن أعظم", L"Первосвященник", L"Hohepriester", L"Sumo sacerdote", L"Hogepriester", L"Arcykapłan", L"Baş rahip")
R(TX_RK_FIRST + 15, L"聖者", L"Saint", L"Saint", L"Santo", L"Santo", L"성자", L"圣者", L"قديس", L"Святой", L"Heiliger", L"Santo", L"Heilige", L"Święty", L"Aziz")
R(TX_RK_FIRST + 16, L"福音", L"Gospel", L"Évangile", L"Vangelo", L"Evangelio", L"복음", L"福音", L"إنجيل", L"Благая весть", L"Evangelium", L"Evangelho", L"Evangelie", L"Ewangelia", L"İncil")
R(TX_RK_FIRST + 17, L"奇跡", L"Miracle", L"Miracle", L"Miracolo", L"Milagro", L"기적", L"奇迹", L"معجزة", L"Чудо", L"Wunder", L"Milagre", L"Wonder", L"Cud", L"Mucize")
R(TX_RK_FIRST + 18, L"聖域", L"Hallow", L"Sanctuaire", L"Santuario", L"Santuario", L"성역", L"圣域", L"حرم", L"Святыня", L"Heiligtum", L"Santuário", L"Heiligdom", L"Święte miejsce", L"Kutsal alan")
R(TX_RK_FIRST + 19, L"神託", L"Oracle", L"Oracle", L"Oracolo", L"Oráculo", L"신탁", L"神谕", L"وحي", L"Оракул", L"Orakel", L"Oráculo", L"Orakel", L"Wyrocznia", L"Kahin")
R(TX_RK_FIRST + 20, L"救済", L"Salvation", L"Salut", L"Salvezza", L"Salvación", L"구제", L"救济", L"خلاص", L"Спасение", L"Erlösung", L"Salvação", L"Verlossing", L"Zbawienie", L"Kurtuluş")
R(TX_RK_FIRST + 21, L"影将", L"Shadow Captain", L"Capitaine de l'ombre", L"Capitano dell'ombra", L"Capitán de la sombra", L"영상", L"影将", L"قائد الظل", L"Капитан тени", L"Schattenhauptmann", L"Capitão da sombra", L"Schaduwkapitein", L"Kapitan cienia", L"Gölge yüzbaşı")
R(TX_RK_FIRST + 22, L"夜王", L"Night King", L"Roi de la nuit", L"Re della notte", L"Rey de la noche", L"야왕", L"夜王", L"ملك الليل", L"Король ночи", L"Nachtkönig", L"Rei da noite", L"Nachtkoning", L"Król nocy", L"Gece kralı")
R(TX_RK_FIRST + 23, L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"无音", L"صمت", L"Безмолвие", L"Stille", L"Silêncio", L"Stilte", L"Cisza", L"Sessizlik")
R(TX_RK_FIRST + 24, L"死神", L"Reaper", L"Faucheur", L"Mietitore", L"Segador", L"사신", L"死神", L"الحاصد", L"Жнец", L"Schnitter", L"Ceifador", L"Maaier", L"Żniwiarz", L"Azrail")
R(TX_RK_FIRST + 25, L"虚", L"Void", L"Vide", L"Vuoto", L"Vacío", L"허", L"虚", L"فراغ", L"Пустота", L"Leere", L"Vazio", L"Leegte", L"Pustka", L"Boşluk")
R(TX_RK_FIRST + 26, L"絶影", L"Last Shadow", L"Dernière ombre", L"Ultima ombra", L"Última sombra", L"절영", L"绝影", L"آخر ظل", L"Последняя тень", L"Letzter Schatten", L"Última sombra", L"Laatste schaduw", L"Ostatni cień", L"Son gölge")
R(TX_RK_FIRST + 27, L"終影", L"End Shadow", L"Ombre finale", L"Ombra finale", L"Sombra final", L"종영", L"终影", L"ظل النهاية", L"Тень конца", L"Endschatten", L"Sombra final", L"Eindschaduw", L"Cień końca", L"Son gölge")
R(TX_RK_FIRST + 28, L"竜将", L"Dragon Captain", L"Capitaine dragon", L"Capitano drago", L"Capitán dragón", L"용장", L"龙将", L"قائد التنين", L"Капитан дракона", L"Drachenhauptmann", L"Capitão dragão", L"Drakenkapitein", L"Kapitan smoka", L"Ejder yüzbaşı")
R(TX_RK_FIRST + 29, L"竜王", L"Dragon King", L"Roi dragon", L"Re drago", L"Rey dragón", L"용왕", L"龙王", L"ملك التنين", L"Король драконов", L"Drachenkönig", L"Rei dragão", L"Drakenkoning", L"Król smoków", L"Ejder kral")
R(TX_RK_FIRST + 30, L"古竜", L"Elder Dragon", L"Dragon ancien", L"Drago antico", L"Dragón antiguo", L"고룡", L"古龙", L"تنين قديم", L"Древний дракон", L"Altdrache", L"Dragão antigo", L"Oerdraak", L"Stary smok", L"Kadim ejder")
R(TX_RK_FIRST + 31, L"災竜", L"Calamity Dragon", L"Dragon calamité", L"Drago calamità", L"Dragón calamidad", L"재룡", L"灾龙", L"تنين الكارثة", L"Дракон бедствия", L"Unheilsdrache", L"Dragão calamidade", L"Rampspoeddraak", L"Smok klęski", L"Felaket ejderi")
R(TX_RK_FIRST + 32, L"天竜", L"Sky Dragon", L"Dragon céleste", L"Drago celeste", L"Dragón celeste", L"천룡", L"天龙", L"تنين السماء", L"Небесный дракон", L"Himmelsdrache", L"Dragão celeste", L"Hemeldraak", L"Niebiański smok", L"Gök ejderi")
R(TX_RK_FIRST + 33, L"神竜", L"God Dragon", L"Dragon divin", L"Drago divino", L"Dragón divino", L"신룡", L"神龙", L"تنين إلهي", L"Божественный дракон", L"Götterdrache", L"Dragão divino", L"Godendraak", L"Boski smok", L"Tanrı ejderi")
R(TX_RK_FIRST + 34, L"始竜", L"First Dragon", L"Dragon premier", L"Drago primo", L"Dragón primero", L"시룡", L"始龙", L"التنين الأول", L"Первый дракон", L"Erster Drache", L"Dragão primeiro", L"Eerste draak", L"Pierwszy smok", L"İlk ejder")
R(TX_RK_FIRST + 35, L"錬金卿", L"Alchemy Lord", L"Seigneur alchimiste", L"Signore alchimista", L"Señor alquimista", L"연금경", L"炼金卿", L"سيد الخيمياء", L"Лорд алхимии", L"Alchemielord", L"Senhor alquimista", L"Alchemielord", L"Lord alchemii", L"Simya lordu")
R(TX_RK_FIRST + 36, L"賢石", L"Wise Stone", L"Pierre sage", L"Pietra saggia", L"Piedra sabia", L"현석", L"贤石", L"حجر حكيم", L"Мудрый камень", L"Weiser Stein", L"Pedra sábia", L"Wijze steen", L"Mądry kamień", L"Bilge taş")
R(TX_RK_FIRST + 37, L"変換", L"Conversion", L"Conversion", L"Conversione", L"Conversión", L"변환", L"转换", L"تحويل", L"Превращение", L"Wandlung", L"Conversão", L"Omzetting", L"Przemiana", L"Dönüşüm")
R(TX_RK_FIRST + 38, L"賢者の石", L"Philosopher Stone", L"Pierre philosophale", L"Pietra filosofale", L"Piedra filosofal", L"현자의 돌", L"贤者之石", L"حجر الفلاسفة", L"Философский камень", L"Stein der Weisen", L"Pedra filosofal", L"Steen der wijzen", L"Kamień filozoficzny", L"Felsefe taşı")
R(TX_RK_FIRST + 39, L"元素", L"Element", L"Élément", L"Elemento", L"Elemento", L"원소", L"元素", L"عنصر", L"Стихия", L"Element", L"Elemento", L"Element", L"Żywioł", L"Element")
R(TX_RK_FIRST + 40, L"創造", L"Creation", L"Création", L"Creazione", L"Creación", L"창조", L"创造", L"خلق", L"Творение", L"Schöpfung", L"Criação", L"Schepping", L"Stworzenie", L"Yaratılış")
R(TX_RK_FIRST + 41, L"原初", L"Origin", L"Origine", L"Origine", L"Origen", L"원초", L"原初", L"الأصل", L"Исток", L"Ursprung", L"Origem", L"Oorsprong", L"Początek", L"Köken")
R(TX_ART_FIRST + 0, L"炎弾", L"Fire Bolt", L"Feu Trait", L"Fuoco Dardo", L"Fuego Rayo", L"화탄", L"炎弹", L"نارسهم", L"Огонь Стрела", L"Feuer Geschoss", L"Fogo Seta", L"Vuur Schicht", L"Ogień Pocisk", L"Ateş Ok")
R(TX_ARTD_FIRST + 0, L"炎属性の魔法。単体にダメージ。", L"Fire magic against one foe.", L"Magie de Feu sur une cible.", L"Magia di Fuoco su un bersaglio.", L"Magia de Fuego contra un objetivo.", L"화 속성의 마법. 단일 대상.", L"炎属性魔法，单体伤害。", L"سحر نار على هدف واحد.", L"Магия «Огонь» по одной цели.", L"Feuer-Magie gegen ein Ziel.", L"Magia de Fogo num alvo.", L"Vuur-magie op één doel.", L"Magia Ogień w jeden cel.", L"Tek hedefe Ateş büyüsü.")
R(TX_ART_FIRST + 1, L"氷輪", L"Ice Ring", L"Glace Anneau", L"Ghiaccio Anello", L"Hielo Anillo", L"빙륜", L"冰轮", L"جليدحلقة", L"Лёд Кольцо", L"Eis Ring", L"Gelo Anel", L"IJs Ring", L"Lód Pierścień", L"Buz Halka")
R(TX_ARTD_FIRST + 1, L"氷属性の魔法。着弾点の周囲にダメージ。", L"Ice magic around the impact.", L"Magie de Glace autour de l'impact.", L"Magia di Ghiaccio attorno all'impatto.", L"Magia de Hielo alrededor del impacto.", L"빙 속성의 마법. 착탄점 주위.", L"冰属性魔法，命中点周围伤害。", L"سحر جليد حول موضع الإصابة.", L"Магия «Лёд» вокруг попадания.", L"Eis-Magie um den Treffer.", L"Magia de Gelo à volta do impacto.", L"IJs-magie rond de inslag.", L"Magia Lód wokół trafienia.", L"İsabet noktasının çevresine Buz büyüsü.")
R(TX_ART_FIRST + 2, L"雷刃", L"Thunder Edge", L"Foudre Lame", L"Tuono Lama", L"Trueno Filo", L"뢰인", L"雷刃", L"رعدحد", L"Гром Клинок", L"Donner Klinge", L"Trovão Lâmina", L"Donder Kling", L"Grom Ostrze", L"Yıldırım Ağız")
R(TX_ARTD_FIRST + 2, L"雷属性の武器攻撃。", L"A weapon strike of Thunder.", L"Une attaque d'arme de Foudre.", L"Un colpo d'arma di Tuono.", L"Un ataque de arma de Trueno.", L"뢰 속성의 무기 공격.", L"雷属性的武器攻击。", L"ضربة سلاح من رعد.", L"Удар оружием стихии «Гром».", L"Ein Waffenschlag mit Donner.", L"Um golpe de arma de Trovão.", L"Een wapenslag van Donder.", L"Cios bronią żywiołu Grom.", L"Yıldırım nitelikli silah vuruşu.")
R(TX_ART_FIRST + 3, L"聖連撃", L"Holy Flurry", L"Sacré Rafale", L"Sacro Raffica", L"Sagrado Ráfaga", L"성연격", L"圣连击", L"مقدسهبة", L"Свет Шквал", L"Heilig Hagel", L"Sagrado Rajada", L"Heilig Vlaag", L"Świętość Seria", L"Kutsal Sağanak")
R(TX_ARTD_FIRST + 3, L"聖属性で二回攻撃する。", L"Two strikes of Holy.", L"Deux coups de Sacré.", L"Due colpi di Sacro.", L"Dos golpes de Sagrado.", L"성 속성으로 두 번 공격.", L"以圣属性攻击两次。", L"ضربتان من مقدس.", L"Два удара стихии «Свет».", L"Zwei Schläge mit Heilig.", L"Dois golpes de Sagrado.", L"Twee slagen van Heilig.", L"Dwa ciosy żywiołu Świętość.", L"Kutsal ile iki vuruş.")
R(TX_ART_FIRST + 4, L"闇癒", L"Dark Mend", L"Ténèbres Soin", L"Oscurità Cura", L"Oscuridad Cura", L"암유", L"暗愈", L"ظلامشفاء", L"Тьма Лечение", L"Dunkel Heilung", L"Trevas Cura", L"Duisternis Heel", L"Mrok Leczenie", L"Karanlık Şifa")
R(TX_ARTD_FIRST + 4, L"闇の力で味方を回復する。", L"Heals an ally with Dark.", L"Soigne un allié par Ténèbres.", L"Cura un alleato con Oscurità.", L"Cura a un aliado con Oscuridad.", L"암의 힘으로 아군을 회복.", L"以暗之力治疗友军。", L"يشفي حليفًا بـظلام.", L"Лечит союзника силой «Тьма».", L"Heilt einen Verbündeten mit Dunkel.", L"Cura um aliado com Trevas.", L"Geneest een bondgenoot met Duisternis.", L"Leczy sojusznika mocą Mrok.", L"Karanlık gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 5, L"風盾", L"Wind Ward", L"Vent Égide", L"Vento Egida", L"Viento Égida", L"풍순", L"风盾", L"ريحدرع", L"Ветер Щит", L"Wind Schild", L"Vento Égide", L"Wind Schild", L"Wiatr Tarcza", L"Rüzgar Kalkan")
R(TX_ARTD_FIRST + 5, L"風の加護で防御を上げる。", L"Raises defense with Wind.", L"Augmente la défense par Vent.", L"Aumenta la difesa con Vento.", L"Sube la defensa con Viento.", L"풍의 가호로 방어를 올린다.", L"以风的加护提高防御。", L"يرفع الدفاع بـريح.", L"Повышает защиту силой «Ветер».", L"Hebt die Abwehr mit Wind.", L"Sobe a defesa com Vento.", L"Verhoogt de verdediging met Wind.", L"Podnosi obronę mocą Wiatr.", L"Rüzgar korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 6, L"土縛", L"Earth Bind", L"Terre Lien", L"Terra Laccio", L"Tierra Lazo", L"토박", L"土缚", L"أرضقيد", L"Земля Узы", L"Erde Band", L"Terra Laço", L"Aarde Band", L"Ziemia Więź", L"Toprak Bağ")
R(TX_ARTD_FIRST + 6, L"土属性の魔法。当たった相手を遅らせる。", L"Earth magic that slows those hit.", L"Magie de Terre qui ralentit les touchés.", L"Magia di Terra che rallenta i colpiti.", L"Magia de Tierra que retrasa a los alcanzados.", L"토 속성의 마법. 맞은 상대를 늦춘다.", L"土属性魔法，使被命中者延迟。", L"سحر أرض يبطئ من يُصاب.", L"Магия «Земля», замедляет поражённых.", L"Erde-Magie, die Getroffene verzögert.", L"Magia de Terra que atrasa os atingidos.", L"Aarde-magie die geraakten vertraagt.", L"Magia Ziemia, która spowalnia trafionych.", L"İsabet edeni geciktiren Toprak büyüsü.")
R(TX_ART_FIRST + 7, L"毒牙", L"Poison Fang", L"Poison Croc", L"Veleno Zanna", L"Veneno Colmillo", L"독아", L"毒牙", L"سمناب", L"Яд Клык", L"Gift Fang", L"Veneno Presa", L"Gif Tand", L"Trucizna Kieł", L"Zehir Diş")
R(TX_ARTD_FIRST + 7, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 8, L"炎槍", L"Fire Lance", L"Feu Lance", L"Fuoco Lancia", L"Fuego Lanza", L"화창", L"炎枪", L"ناررمح", L"Огонь Копьё", L"Feuer Lanze", L"Fogo Lança", L"Vuur Lans", L"Ogień Włócznia", L"Ateş Mızrak")
R(TX_ARTD_FIRST + 8, L"炎属性の魔法。単体にダメージ。", L"Fire magic against one foe.", L"Magie de Feu sur une cible.", L"Magia di Fuoco su un bersaglio.", L"Magia de Fuego contra un objetivo.", L"화 속성의 마법. 단일 대상.", L"炎属性魔法，单体伤害。", L"سحر نار على هدف واحد.", L"Магия «Огонь» по одной цели.", L"Feuer-Magie gegen ein Ziel.", L"Magia de Fogo num alvo.", L"Vuur-magie op één doel.", L"Magia Ogień w jeden cel.", L"Tek hedefe Ateş büyüsü.")
R(TX_ART_FIRST + 9, L"氷嵐", L"Ice Gale", L"Glace Bourrasque", L"Ghiaccio Burrasca", L"Hielo Vendaval", L"빙람", L"冰岚", L"جليدعاصفة", L"Лёд Шквал", L"Eis Sturm", L"Gelo Vendaval", L"IJs Storm", L"Lód Sztorm", L"Buz Fırtına")
R(TX_ARTD_FIRST + 9, L"氷属性の魔法。着弾点の周囲にダメージ。", L"Ice magic around the impact.", L"Magie de Glace autour de l'impact.", L"Magia di Ghiaccio attorno all'impatto.", L"Magia de Hielo alrededor del impacto.", L"빙 속성의 마법. 착탄점 주위.", L"冰属性魔法，命中点周围伤害。", L"سحر جليد حول موضع الإصابة.", L"Магия «Лёд» вокруг попадания.", L"Eis-Magie um den Treffer.", L"Magia de Gelo à volta do impacto.", L"IJs-magie rond de inslag.", L"Magia Lód wokół trafienia.", L"İsabet noktasının çevresine Buz büyüsü.")
R(TX_ART_FIRST + 10, L"雷斬", L"Thunder Slash", L"Foudre Taille", L"Tuono Taglio", L"Trueno Tajo", L"뢰참", L"雷斩", L"رعدقطع", L"Гром Рубить", L"Donner Hieb", L"Trovão Corte", L"Donder Houw", L"Grom Cięcie", L"Yıldırım Kesik")
R(TX_ARTD_FIRST + 10, L"雷属性の武器攻撃。", L"A weapon strike of Thunder.", L"Une attaque d'arme de Foudre.", L"Un colpo d'arma di Tuono.", L"Un ataque de arma de Trueno.", L"뢰 속성의 무기 공격.", L"雷属性的武器攻击。", L"ضربة سلاح من رعد.", L"Удар оружием стихии «Гром».", L"Ein Waffenschlag mit Donner.", L"Um golpe de arma de Trovão.", L"Een wapenslag van Donder.", L"Cios bronią żywiołu Grom.", L"Yıldırım nitelikli silah vuruşu.")
R(TX_ART_FIRST + 11, L"聖双撃", L"Holy Twin", L"Sacré Paire", L"Sacro Coppia", L"Sagrado Par", L"성쌍격", L"圣双击", L"مقدسزوج", L"Свет Пара", L"Heilig Paar", L"Sagrado Par", L"Heilig Paar", L"Świętość Para", L"Kutsal Çift")
R(TX_ARTD_FIRST + 11, L"聖属性で二回攻撃する。", L"Two strikes of Holy.", L"Deux coups de Sacré.", L"Due colpi di Sacro.", L"Dos golpes de Sagrado.", L"성 속성으로 두 번 공격.", L"以圣属性攻击两次。", L"ضربتان من مقدس.", L"Два удара стихии «Свет».", L"Zwei Schläge mit Heilig.", L"Dois golpes de Sagrado.", L"Twee slagen van Heilig.", L"Dwa ciosy żywiołu Świętość.", L"Kutsal ile iki vuruş.")
R(TX_ART_FIRST + 12, L"闇雫", L"Dark Drop", L"Ténèbres Goutte", L"Oscurità Goccia", L"Oscuridad Gota", L"암적", L"暗滴", L"ظلامقطرة", L"Тьма Капля", L"Dunkel Tropfen", L"Trevas Gota", L"Duisternis Druppel", L"Mrok Kropla", L"Karanlık Damla")
R(TX_ARTD_FIRST + 12, L"闇の力で味方を回復する。", L"Heals an ally with Dark.", L"Soigne un allié par Ténèbres.", L"Cura un alleato con Oscurità.", L"Cura a un aliado con Oscuridad.", L"암의 힘으로 아군을 회복.", L"以暗之力治疗友军。", L"يشفي حليفًا بـظلام.", L"Лечит союзника силой «Тьма».", L"Heilt einen Verbündeten mit Dunkel.", L"Cura um aliado com Trevas.", L"Geneest een bondgenoot met Duisternis.", L"Leczy sojusznika mocą Mrok.", L"Karanlık gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 13, L"風壁", L"Wind Wall", L"Vent Mur", L"Vento Muro", L"Viento Muro", L"풍벽", L"风壁", L"ريحجدار", L"Ветер Стена", L"Wind Wall", L"Vento Muro", L"Wind Wal", L"Wiatr Mur", L"Rüzgar Duvar")
R(TX_ARTD_FIRST + 13, L"風の加護で防御を上げる。", L"Raises defense with Wind.", L"Augmente la défense par Vent.", L"Aumenta la difesa con Vento.", L"Sube la defensa con Viento.", L"풍의 가호로 방어를 올린다.", L"以风的加护提高防御。", L"يرفع الدفاع بـريح.", L"Повышает защиту силой «Ветер».", L"Hebt die Abwehr mit Wind.", L"Sobe a defesa com Vento.", L"Verhoogt de verdediging met Wind.", L"Podnosi obronę mocą Wiatr.", L"Rüzgar korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 14, L"土鎖", L"Earth Chain", L"Terre Chaîne", L"Terra Catena", L"Tierra Cadena", L"토쇄", L"土锁", L"أرضسلسلة", L"Земля Цепь", L"Erde Kette", L"Terra Corrente", L"Aarde Keten", L"Ziemia Łańcuch", L"Toprak Zincir")
R(TX_ARTD_FIRST + 14, L"土属性の魔法。当たった相手を遅らせる。", L"Earth magic that slows those hit.", L"Magie de Terre qui ralentit les touchés.", L"Magia di Terra che rallenta i colpiti.", L"Magia de Tierra que retrasa a los alcanzados.", L"토 속성의 마법. 맞은 상대를 늦춘다.", L"土属性魔法，使被命中者延迟。", L"سحر أرض يبطئ من يُصاب.", L"Магия «Земля», замедляет поражённых.", L"Erde-Magie, die Getroffene verzögert.", L"Magia de Terra que atrasa os atingidos.", L"Aarde-magie die geraakten vertraagt.", L"Magia Ziemia, która spowalnia trafionych.", L"İsabet edeni geciktiren Toprak büyüsü.")
R(TX_ART_FIRST + 15, L"毒針", L"Poison Needle", L"Poison Aiguille", L"Veleno Ago", L"Veneno Aguja", L"독침", L"毒针", L"سمإبرة", L"Яд Игла", L"Gift Nadel", L"Veneno Agulha", L"Gif Naald", L"Trucizna Igła", L"Zehir İğne")
R(TX_ARTD_FIRST + 15, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 16, L"炎滅", L"Fire Ruin", L"Feu Fin", L"Fuoco Fine", L"Fuego Fin", L"화멸", L"炎灭", L"نارفناء", L"Огонь Конец", L"Feuer Ende", L"Fogo Fim", L"Vuur Einde", L"Ogień Koniec", L"Ateş Son")
R(TX_ARTD_FIRST + 16, L"炎属性の魔法。単体にダメージ。", L"Fire magic against one foe.", L"Magie de Feu sur une cible.", L"Magia di Fuoco su un bersaglio.", L"Magia de Fuego contra un objetivo.", L"화 속성의 마법. 단일 대상.", L"炎属性魔法，单体伤害。", L"سحر نار على هدف واحد.", L"Магия «Огонь» по одной цели.", L"Feuer-Magie gegen ein Ziel.", L"Magia de Fogo num alvo.", L"Vuur-magie op één doel.", L"Magia Ogień w jeden cel.", L"Tek hedefe Ateş büyüsü.")
R(TX_ART_FIRST + 17, L"氷哮", L"Ice Roar", L"Glace Rugissement", L"Ghiaccio Ruggito", L"Hielo Rugido", L"빙효", L"冰哮", L"جليدزئير", L"Лёд Рёв", L"Eis Brüllen", L"Gelo Rugido", L"IJs Brul", L"Lód Ryk", L"Buz Kükreme")
R(TX_ARTD_FIRST + 17, L"氷属性の魔法。着弾点の周囲にダメージ。", L"Ice magic around the impact.", L"Magie de Glace autour de l'impact.", L"Magia di Ghiaccio attorno all'impatto.", L"Magia de Hielo alrededor del impacto.", L"빙 속성의 마법. 착탄점 주위.", L"冰属性魔法，命中点周围伤害。", L"سحر جليد حول موضع الإصابة.", L"Магия «Лёд» вокруг попадания.", L"Eis-Magie um den Treffer.", L"Magia de Gelo à volta do impacto.", L"IJs-magie rond de inslag.", L"Magia Lód wokół trafienia.", L"İsabet noktasının çevresine Buz büyüsü.")
R(TX_ART_FIRST + 18, L"雷衝", L"Thunder Rush", L"Foudre Ruée", L"Tuono Slancio", L"Trueno Embestida", L"뢰충", L"雷冲", L"رعداندفاع", L"Гром Рывок", L"Donner Ansturm", L"Trovão Investida", L"Donder Stormloop", L"Grom Szarża", L"Yıldırım Hamle")
R(TX_ARTD_FIRST + 18, L"雷属性の武器攻撃。", L"A weapon strike of Thunder.", L"Une attaque d'arme de Foudre.", L"Un colpo d'arma di Tuono.", L"Un ataque de arma de Trueno.", L"뢰 속성의 무기 공격.", L"雷属性的武器攻击。", L"ضربة سلاح من رعد.", L"Удар оружием стихии «Гром».", L"Ein Waffenschlag mit Donner.", L"Um golpe de arma de Trovão.", L"Een wapenslag van Donder.", L"Cios bronią żywiołu Grom.", L"Yıldırım nitelikli silah vuruşu.")
R(TX_ART_FIRST + 19, L"聖舞", L"Holy Dance", L"Sacré Danse", L"Sacro Danza", L"Sagrado Danza", L"성무", L"圣舞", L"مقدسرقص", L"Свет Танец", L"Heilig Tanz", L"Sagrado Dança", L"Heilig Dans", L"Świętość Taniec", L"Kutsal Dans")
R(TX_ARTD_FIRST + 19, L"聖属性で二回攻撃する。", L"Two strikes of Holy.", L"Deux coups de Sacré.", L"Due colpi di Sacro.", L"Dos golpes de Sagrado.", L"성 속성으로 두 번 공격.", L"以圣属性攻击两次。", L"ضربتان من مقدس.", L"Два удара стихии «Свет».", L"Zwei Schläge mit Heilig.", L"Dois golpes de Sagrado.", L"Twee slagen van Heilig.", L"Dwa ciosy żywiołu Świętość.", L"Kutsal ile iki vuruş.")
R(TX_ART_FIRST + 20, L"闇光", L"Dark Gleam", L"Ténèbres Lueur", L"Oscurità Bagliore", L"Oscuridad Brillo", L"암광", L"暗光", L"ظلامبريق", L"Тьма Блеск", L"Dunkel Glanz", L"Trevas Brilho", L"Duisternis Glans", L"Mrok Blask", L"Karanlık Parıltı")
R(TX_ARTD_FIRST + 20, L"闇の力で味方を回復する。", L"Heals an ally with Dark.", L"Soigne un allié par Ténèbres.", L"Cura un alleato con Oscurità.", L"Cura a un aliado con Oscuridad.", L"암의 힘으로 아군을 회복.", L"以暗之力治疗友军。", L"يشفي حليفًا بـظلام.", L"Лечит союзника силой «Тьма».", L"Heilt einen Verbündeten mit Dunkel.", L"Cura um aliado com Trevas.", L"Geneest een bondgenoot met Duisternis.", L"Leczy sojusznika mocą Mrok.", L"Karanlık gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 21, L"風陣", L"Wind Field", L"Vent Champ", L"Vento Campo", L"Viento Campo", L"풍진", L"风阵", L"ريحميدان", L"Ветер Поле", L"Wind Feld", L"Vento Campo", L"Wind Veld", L"Wiatr Pole", L"Rüzgar Alan")
R(TX_ARTD_FIRST + 21, L"風の加護で防御を上げる。", L"Raises defense with Wind.", L"Augmente la défense par Vent.", L"Aumenta la difesa con Vento.", L"Sube la defensa con Viento.", L"풍의 가호로 방어를 올린다.", L"以风的加护提高防御。", L"يرفع الدفاع بـريح.", L"Повышает защиту силой «Ветер».", L"Hebt die Abwehr mit Wind.", L"Sobe a defesa com Vento.", L"Verhoogt de verdediging met Wind.", L"Podnosi obronę mocą Wiatr.", L"Rüzgar korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 22, L"土崩", L"Earth Break", L"Terre Bris", L"Terra Rottura", L"Tierra Quiebre", L"토붕", L"土崩", L"أرضكسر", L"Земля Слом", L"Erde Bruch", L"Terra Quebra", L"Aarde Breuk", L"Ziemia Złamanie", L"Toprak Kırılma")
R(TX_ARTD_FIRST + 22, L"土属性の魔法。当たった相手を遅らせる。", L"Earth magic that slows those hit.", L"Magie de Terre qui ralentit les touchés.", L"Magia di Terra che rallenta i colpiti.", L"Magia de Tierra que retrasa a los alcanzados.", L"토 속성의 마법. 맞은 상대를 늦춘다.", L"土属性魔法，使被命中者延迟。", L"سحر أرض يبطئ من يُصاب.", L"Магия «Земля», замедляет поражённых.", L"Erde-Magie, die Getroffene verzögert.", L"Magia de Terra que atrasa os atingidos.", L"Aarde-magie die geraakten vertraagt.", L"Magia Ziemia, która spowalnia trafionych.", L"İsabet edeni geciktiren Toprak büyüsü.")
R(TX_ART_FIRST + 23, L"毒棘", L"Poison Thorn", L"Poison Épine", L"Veleno Spina", L"Veneno Espina", L"독극", L"毒棘", L"سمشوك", L"Яд Шип", L"Gift Dorn", L"Veneno Espinho", L"Gif Doorn", L"Trucizna Cierń", L"Zehir Diken")
R(TX_ARTD_FIRST + 23, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 24, L"炎極", L"Fire Apex", L"Feu Sommet", L"Fuoco Apice", L"Fuego Cumbre", L"화극의", L"炎极", L"نارذروة", L"Огонь Вершина", L"Feuer Gipfel", L"Fogo Ápice", L"Vuur Top", L"Ogień Szczyt", L"Ateş Zirve")
R(TX_ARTD_FIRST + 24, L"炎属性の魔法。単体にダメージ。", L"Fire magic against one foe.", L"Magie de Feu sur une cible.", L"Magia di Fuoco su un bersaglio.", L"Magia de Fuego contra un objetivo.", L"화 속성의 마법. 단일 대상.", L"炎属性魔法，单体伤害。", L"سحر نار على هدف واحد.", L"Магия «Огонь» по одной цели.", L"Feuer-Magie gegen ein Ziel.", L"Magia de Fogo num alvo.", L"Vuur-magie op één doel.", L"Magia Ogień w jeden cel.", L"Tek hedefe Ateş büyüsü.")
R(TX_ART_FIRST + 25, L"氷覇", L"Ice Reign", L"Glace Règne", L"Ghiaccio Regno", L"Hielo Reino", L"빙패", L"冰霸", L"جليدسلطان", L"Лёд Власть", L"Eis Herrschaft", L"Gelo Reino", L"IJs Heerschappij", L"Lód Panowanie", L"Buz Hüküm")
R(TX_ARTD_FIRST + 25, L"氷属性の魔法。着弾点の周囲にダメージ。", L"Ice magic around the impact.", L"Magie de Glace autour de l'impact.", L"Magia di Ghiaccio attorno all'impatto.", L"Magia de Hielo alrededor del impacto.", L"빙 속성의 마법. 착탄점 주위.", L"冰属性魔法，命中点周围伤害。", L"سحر جليد حول موضع الإصابة.", L"Магия «Лёд» вокруг попадания.", L"Eis-Magie um den Treffer.", L"Magia de Gelo à volta do impacto.", L"IJs-magie rond de inslag.", L"Magia Lód wokół trafienia.", L"İsabet noktasının çevresine Buz büyüsü.")
R(TX_ART_FIRST + 26, L"聖弾", L"Holy Bolt", L"Sacré Trait", L"Sacro Dardo", L"Sagrado Rayo", L"성탄", L"圣弹", L"مقدسسهم", L"Свет Стрела", L"Heilig Geschoss", L"Sagrado Seta", L"Heilig Schicht", L"Świętość Pocisk", L"Kutsal Ok")
R(TX_ARTD_FIRST + 26, L"聖属性の魔法。単体にダメージ。", L"Holy magic against one foe.", L"Magie de Sacré sur une cible.", L"Magia di Sacro su un bersaglio.", L"Magia de Sagrado contra un objetivo.", L"성 속성의 마법. 단일 대상.", L"圣属性魔法，单体伤害。", L"سحر مقدس على هدف واحد.", L"Магия «Свет» по одной цели.", L"Heilig-Magie gegen ein Ziel.", L"Magia de Sagrado num alvo.", L"Heilig-magie op één doel.", L"Magia Świętość w jeden cel.", L"Tek hedefe Kutsal büyüsü.")
R(TX_ART_FIRST + 27, L"闇輪", L"Dark Ring", L"Ténèbres Anneau", L"Oscurità Anello", L"Oscuridad Anillo", L"암륜", L"暗轮", L"ظلامحلقة", L"Тьма Кольцо", L"Dunkel Ring", L"Trevas Anel", L"Duisternis Ring", L"Mrok Pierścień", L"Karanlık Halka")
R(TX_ARTD_FIRST + 27, L"闇属性の魔法。着弾点の周囲にダメージ。", L"Dark magic around the impact.", L"Magie de Ténèbres autour de l'impact.", L"Magia di Oscurità attorno all'impatto.", L"Magia de Oscuridad alrededor del impacto.", L"암 속성의 마법. 착탄점 주위.", L"暗属性魔法，命中点周围伤害。", L"سحر ظلام حول موضع الإصابة.", L"Магия «Тьма» вокруг попадания.", L"Dunkel-Magie um den Treffer.", L"Magia de Trevas à volta do impacto.", L"Duisternis-magie rond de inslag.", L"Magia Mrok wokół trafienia.", L"İsabet noktasının çevresine Karanlık büyüsü.")
R(TX_ART_FIRST + 28, L"風刃", L"Wind Edge", L"Vent Lame", L"Vento Lama", L"Viento Filo", L"풍인", L"风刃", L"ريححد", L"Ветер Клинок", L"Wind Klinge", L"Vento Lâmina", L"Wind Kling", L"Wiatr Ostrze", L"Rüzgar Ağız")
R(TX_ARTD_FIRST + 28, L"風属性の武器攻撃。", L"A weapon strike of Wind.", L"Une attaque d'arme de Vent.", L"Un colpo d'arma di Vento.", L"Un ataque de arma de Viento.", L"풍 속성의 무기 공격.", L"风属性的武器攻击。", L"ضربة سلاح من ريح.", L"Удар оружием стихии «Ветер».", L"Ein Waffenschlag mit Wind.", L"Um golpe de arma de Vento.", L"Een wapenslag van Wind.", L"Cios bronią żywiołu Wiatr.", L"Rüzgar nitelikli silah vuruşu.")
R(TX_ART_FIRST + 29, L"土連撃", L"Earth Flurry", L"Terre Rafale", L"Terra Raffica", L"Tierra Ráfaga", L"토연격", L"土连击", L"أرضهبة", L"Земля Шквал", L"Erde Hagel", L"Terra Rajada", L"Aarde Vlaag", L"Ziemia Seria", L"Toprak Sağanak")
R(TX_ARTD_FIRST + 29, L"土属性で二回攻撃する。", L"Two strikes of Earth.", L"Deux coups de Terre.", L"Due colpi di Terra.", L"Dos golpes de Tierra.", L"토 속성으로 두 번 공격.", L"以土属性攻击两次。", L"ضربتان من أرض.", L"Два удара стихии «Земля».", L"Zwei Schläge mit Erde.", L"Dois golpes de Terra.", L"Twee slagen van Aarde.", L"Dwa ciosy żywiołu Ziemia.", L"Toprak ile iki vuruş.")
R(TX_ART_FIRST + 30, L"毒癒", L"Poison Mend", L"Poison Soin", L"Veleno Cura", L"Veneno Cura", L"독유", L"毒愈", L"سمشفاء", L"Яд Лечение", L"Gift Heilung", L"Veneno Cura", L"Gif Heel", L"Trucizna Leczenie", L"Zehir Şifa")
R(TX_ARTD_FIRST + 30, L"毒の力で味方を回復する。", L"Heals an ally with Poison.", L"Soigne un allié par Poison.", L"Cura un alleato con Veleno.", L"Cura a un aliado con Veneno.", L"독의 힘으로 아군을 회복.", L"以毒之力治疗友军。", L"يشفي حليفًا بـسم.", L"Лечит союзника силой «Яд».", L"Heilt einen Verbündeten mit Gift.", L"Cura um aliado com Veneno.", L"Geneest een bondgenoot met Gif.", L"Leczy sojusznika mocą Trucizna.", L"Zehir gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 31, L"炎盾", L"Fire Ward", L"Feu Égide", L"Fuoco Egida", L"Fuego Égida", L"화순", L"炎盾", L"ناردرع", L"Огонь Щит", L"Feuer Schild", L"Fogo Égide", L"Vuur Schild", L"Ogień Tarcza", L"Ateş Kalkan")
R(TX_ARTD_FIRST + 31, L"炎の加護で防御を上げる。", L"Raises defense with Fire.", L"Augmente la défense par Feu.", L"Aumenta la difesa con Fuoco.", L"Sube la defensa con Fuego.", L"화의 가호로 방어를 올린다.", L"以炎的加护提高防御。", L"يرفع الدفاع بـنار.", L"Повышает защиту силой «Огонь».", L"Hebt die Abwehr mit Feuer.", L"Sobe a defesa com Fogo.", L"Verhoogt de verdediging met Vuur.", L"Podnosi obronę mocą Ogień.", L"Ateş korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 32, L"氷縛", L"Ice Bind", L"Glace Lien", L"Ghiaccio Laccio", L"Hielo Lazo", L"빙박", L"冰缚", L"جليدقيد", L"Лёд Узы", L"Eis Band", L"Gelo Laço", L"IJs Band", L"Lód Więź", L"Buz Bağ")
R(TX_ARTD_FIRST + 32, L"氷属性の魔法。当たった相手を遅らせる。", L"Ice magic that slows those hit.", L"Magie de Glace qui ralentit les touchés.", L"Magia di Ghiaccio che rallenta i colpiti.", L"Magia de Hielo que retrasa a los alcanzados.", L"빙 속성의 마법. 맞은 상대를 늦춘다.", L"冰属性魔法，使被命中者延迟。", L"سحر جليد يبطئ من يُصاب.", L"Магия «Лёд», замедляет поражённых.", L"Eis-Magie, die Getroffene verzögert.", L"Magia de Gelo que atrasa os atingidos.", L"IJs-magie die geraakten vertraagt.", L"Magia Lód, która spowalnia trafionych.", L"İsabet edeni geciktiren Buz büyüsü.")
R(TX_ART_FIRST + 33, L"毒牙", L"Poison Fang", L"Poison Croc", L"Veleno Zanna", L"Veneno Colmillo", L"독아", L"毒牙", L"سمناب", L"Яд Клык", L"Gift Fang", L"Veneno Presa", L"Gif Tand", L"Trucizna Kieł", L"Zehir Diş")
R(TX_ARTD_FIRST + 33, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 34, L"聖槍", L"Holy Lance", L"Sacré Lance", L"Sacro Lancia", L"Sagrado Lanza", L"성창", L"圣枪", L"مقدسرمح", L"Свет Копьё", L"Heilig Lanze", L"Sagrado Lança", L"Heilig Lans", L"Świętość Włócznia", L"Kutsal Mızrak")
R(TX_ARTD_FIRST + 34, L"聖属性の魔法。単体にダメージ。", L"Holy magic against one foe.", L"Magie de Sacré sur une cible.", L"Magia di Sacro su un bersaglio.", L"Magia de Sagrado contra un objetivo.", L"성 속성의 마법. 단일 대상.", L"圣属性魔法，单体伤害。", L"سحر مقدس على هدف واحد.", L"Магия «Свет» по одной цели.", L"Heilig-Magie gegen ein Ziel.", L"Magia de Sagrado num alvo.", L"Heilig-magie op één doel.", L"Magia Świętość w jeden cel.", L"Tek hedefe Kutsal büyüsü.")
R(TX_ART_FIRST + 35, L"闇嵐", L"Dark Gale", L"Ténèbres Bourrasque", L"Oscurità Burrasca", L"Oscuridad Vendaval", L"암람", L"暗岚", L"ظلامعاصفة", L"Тьма Шквал", L"Dunkel Sturm", L"Trevas Vendaval", L"Duisternis Storm", L"Mrok Sztorm", L"Karanlık Fırtına")
R(TX_ARTD_FIRST + 35, L"闇属性の魔法。着弾点の周囲にダメージ。", L"Dark magic around the impact.", L"Magie de Ténèbres autour de l'impact.", L"Magia di Oscurità attorno all'impatto.", L"Magia de Oscuridad alrededor del impacto.", L"암 속성의 마법. 착탄점 주위.", L"暗属性魔法，命中点周围伤害。", L"سحر ظلام حول موضع الإصابة.", L"Магия «Тьма» вокруг попадания.", L"Dunkel-Magie um den Treffer.", L"Magia de Trevas à volta do impacto.", L"Duisternis-magie rond de inslag.", L"Magia Mrok wokół trafienia.", L"İsabet noktasının çevresine Karanlık büyüsü.")
R(TX_ART_FIRST + 36, L"風斬", L"Wind Slash", L"Vent Taille", L"Vento Taglio", L"Viento Tajo", L"풍참", L"风斩", L"ريحقطع", L"Ветер Рубить", L"Wind Hieb", L"Vento Corte", L"Wind Houw", L"Wiatr Cięcie", L"Rüzgar Kesik")
R(TX_ARTD_FIRST + 36, L"風属性の武器攻撃。", L"A weapon strike of Wind.", L"Une attaque d'arme de Vent.", L"Un colpo d'arma di Vento.", L"Un ataque de arma de Viento.", L"풍 속성의 무기 공격.", L"风属性的武器攻击。", L"ضربة سلاح من ريح.", L"Удар оружием стихии «Ветер».", L"Ein Waffenschlag mit Wind.", L"Um golpe de arma de Vento.", L"Een wapenslag van Wind.", L"Cios bronią żywiołu Wiatr.", L"Rüzgar nitelikli silah vuruşu.")
R(TX_ART_FIRST + 37, L"土双撃", L"Earth Twin", L"Terre Paire", L"Terra Coppia", L"Tierra Par", L"토쌍격", L"土双击", L"أرضزوج", L"Земля Пара", L"Erde Paar", L"Terra Par", L"Aarde Paar", L"Ziemia Para", L"Toprak Çift")
R(TX_ARTD_FIRST + 37, L"土属性で二回攻撃する。", L"Two strikes of Earth.", L"Deux coups de Terre.", L"Due colpi di Terra.", L"Dos golpes de Tierra.", L"토 속성으로 두 번 공격.", L"以土属性攻击两次。", L"ضربتان من أرض.", L"Два удара стихии «Земля».", L"Zwei Schläge mit Erde.", L"Dois golpes de Terra.", L"Twee slagen van Aarde.", L"Dwa ciosy żywiołu Ziemia.", L"Toprak ile iki vuruş.")
R(TX_ART_FIRST + 38, L"毒雫", L"Poison Drop", L"Poison Goutte", L"Veleno Goccia", L"Veneno Gota", L"독적", L"毒滴", L"سمقطرة", L"Яд Капля", L"Gift Tropfen", L"Veneno Gota", L"Gif Druppel", L"Trucizna Kropla", L"Zehir Damla")
R(TX_ARTD_FIRST + 38, L"毒の力で味方を回復する。", L"Heals an ally with Poison.", L"Soigne un allié par Poison.", L"Cura un alleato con Veleno.", L"Cura a un aliado con Veneno.", L"독의 힘으로 아군을 회복.", L"以毒之力治疗友军。", L"يشفي حليفًا بـسم.", L"Лечит союзника силой «Яд».", L"Heilt einen Verbündeten mit Gift.", L"Cura um aliado com Veneno.", L"Geneest een bondgenoot met Gif.", L"Leczy sojusznika mocą Trucizna.", L"Zehir gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 39, L"炎壁", L"Fire Wall", L"Feu Mur", L"Fuoco Muro", L"Fuego Muro", L"화벽", L"炎壁", L"نارجدار", L"Огонь Стена", L"Feuer Wall", L"Fogo Muro", L"Vuur Wal", L"Ogień Mur", L"Ateş Duvar")
R(TX_ARTD_FIRST + 39, L"炎の加護で防御を上げる。", L"Raises defense with Fire.", L"Augmente la défense par Feu.", L"Aumenta la difesa con Fuoco.", L"Sube la defensa con Fuego.", L"화의 가호로 방어를 올린다.", L"以炎的加护提高防御。", L"يرفع الدفاع بـنار.", L"Повышает защиту силой «Огонь».", L"Hebt die Abwehr mit Feuer.", L"Sobe a defesa com Fogo.", L"Verhoogt de verdediging met Vuur.", L"Podnosi obronę mocą Ogień.", L"Ateş korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 40, L"氷鎖", L"Ice Chain", L"Glace Chaîne", L"Ghiaccio Catena", L"Hielo Cadena", L"빙쇄", L"冰锁", L"جليدسلسلة", L"Лёд Цепь", L"Eis Kette", L"Gelo Corrente", L"IJs Keten", L"Lód Łańcuch", L"Buz Zincir")
R(TX_ARTD_FIRST + 40, L"氷属性の魔法。当たった相手を遅らせる。", L"Ice magic that slows those hit.", L"Magie de Glace qui ralentit les touchés.", L"Magia di Ghiaccio che rallenta i colpiti.", L"Magia de Hielo que retrasa a los alcanzados.", L"빙 속성의 마법. 맞은 상대를 늦춘다.", L"冰属性魔法，使被命中者延迟。", L"سحر جليد يبطئ من يُصاب.", L"Магия «Лёд», замедляет поражённых.", L"Eis-Magie, die Getroffene verzögert.", L"Magia de Gelo que atrasa os atingidos.", L"IJs-magie die geraakten vertraagt.", L"Magia Lód, która spowalnia trafionych.", L"İsabet edeni geciktiren Buz büyüsü.")
R(TX_ART_FIRST + 41, L"毒針", L"Poison Needle", L"Poison Aiguille", L"Veleno Ago", L"Veneno Aguja", L"독침", L"毒针", L"سمإبرة", L"Яд Игла", L"Gift Nadel", L"Veneno Agulha", L"Gif Naald", L"Trucizna Igła", L"Zehir İğne")
R(TX_ARTD_FIRST + 41, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 42, L"聖滅", L"Holy Ruin", L"Sacré Fin", L"Sacro Fine", L"Sagrado Fin", L"성멸", L"圣灭", L"مقدسفناء", L"Свет Конец", L"Heilig Ende", L"Sagrado Fim", L"Heilig Einde", L"Świętość Koniec", L"Kutsal Son")
R(TX_ARTD_FIRST + 42, L"聖属性の魔法。単体にダメージ。", L"Holy magic against one foe.", L"Magie de Sacré sur une cible.", L"Magia di Sacro su un bersaglio.", L"Magia de Sagrado contra un objetivo.", L"성 속성의 마법. 단일 대상.", L"圣属性魔法，单体伤害。", L"سحر مقدس على هدف واحد.", L"Магия «Свет» по одной цели.", L"Heilig-Magie gegen ein Ziel.", L"Magia de Sagrado num alvo.", L"Heilig-magie op één doel.", L"Magia Świętość w jeden cel.", L"Tek hedefe Kutsal büyüsü.")
R(TX_ART_FIRST + 43, L"闇哮", L"Dark Roar", L"Ténèbres Rugissement", L"Oscurità Ruggito", L"Oscuridad Rugido", L"암효", L"暗哮", L"ظلامزئير", L"Тьма Рёв", L"Dunkel Brüllen", L"Trevas Rugido", L"Duisternis Brul", L"Mrok Ryk", L"Karanlık Kükreme")
R(TX_ARTD_FIRST + 43, L"闇属性の魔法。着弾点の周囲にダメージ。", L"Dark magic around the impact.", L"Magie de Ténèbres autour de l'impact.", L"Magia di Oscurità attorno all'impatto.", L"Magia de Oscuridad alrededor del impacto.", L"암 속성의 마법. 착탄점 주위.", L"暗属性魔法，命中点周围伤害。", L"سحر ظلام حول موضع الإصابة.", L"Магия «Тьма» вокруг попадания.", L"Dunkel-Magie um den Treffer.", L"Magia de Trevas à volta do impacto.", L"Duisternis-magie rond de inslag.", L"Magia Mrok wokół trafienia.", L"İsabet noktasının çevresine Karanlık büyüsü.")
R(TX_ART_FIRST + 44, L"風衝", L"Wind Rush", L"Vent Ruée", L"Vento Slancio", L"Viento Embestida", L"풍충", L"风冲", L"ريحاندفاع", L"Ветер Рывок", L"Wind Ansturm", L"Vento Investida", L"Wind Stormloop", L"Wiatr Szarża", L"Rüzgar Hamle")
R(TX_ARTD_FIRST + 44, L"風属性の武器攻撃。", L"A weapon strike of Wind.", L"Une attaque d'arme de Vent.", L"Un colpo d'arma di Vento.", L"Un ataque de arma de Viento.", L"풍 속성의 무기 공격.", L"风属性的武器攻击。", L"ضربة سلاح من ريح.", L"Удар оружием стихии «Ветер».", L"Ein Waffenschlag mit Wind.", L"Um golpe de arma de Vento.", L"Een wapenslag van Wind.", L"Cios bronią żywiołu Wiatr.", L"Rüzgar nitelikli silah vuruşu.")
R(TX_ART_FIRST + 45, L"土舞", L"Earth Dance", L"Terre Danse", L"Terra Danza", L"Tierra Danza", L"토무", L"土舞", L"أرضرقص", L"Земля Танец", L"Erde Tanz", L"Terra Dança", L"Aarde Dans", L"Ziemia Taniec", L"Toprak Dans")
R(TX_ARTD_FIRST + 45, L"土属性で二回攻撃する。", L"Two strikes of Earth.", L"Deux coups de Terre.", L"Due colpi di Terra.", L"Dos golpes de Tierra.", L"토 속성으로 두 번 공격.", L"以土属性攻击两次。", L"ضربتان من أرض.", L"Два удара стихии «Земля».", L"Zwei Schläge mit Erde.", L"Dois golpes de Terra.", L"Twee slagen van Aarde.", L"Dwa ciosy żywiołu Ziemia.", L"Toprak ile iki vuruş.")
R(TX_ART_FIRST + 46, L"毒光", L"Poison Gleam", L"Poison Lueur", L"Veleno Bagliore", L"Veneno Brillo", L"독광", L"毒光", L"سمبريق", L"Яд Блеск", L"Gift Glanz", L"Veneno Brilho", L"Gif Glans", L"Trucizna Blask", L"Zehir Parıltı")
R(TX_ARTD_FIRST + 46, L"毒の力で味方を回復する。", L"Heals an ally with Poison.", L"Soigne un allié par Poison.", L"Cura un alleato con Veleno.", L"Cura a un aliado con Veneno.", L"독의 힘으로 아군을 회복.", L"以毒之力治疗友军。", L"يشفي حليفًا بـسم.", L"Лечит союзника силой «Яд».", L"Heilt einen Verbündeten mit Gift.", L"Cura um aliado com Veneno.", L"Geneest een bondgenoot met Gif.", L"Leczy sojusznika mocą Trucizna.", L"Zehir gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 47, L"炎陣", L"Fire Field", L"Feu Champ", L"Fuoco Campo", L"Fuego Campo", L"화진", L"炎阵", L"نارميدان", L"Огонь Поле", L"Feuer Feld", L"Fogo Campo", L"Vuur Veld", L"Ogień Pole", L"Ateş Alan")
R(TX_ARTD_FIRST + 47, L"炎の加護で防御を上げる。", L"Raises defense with Fire.", L"Augmente la défense par Feu.", L"Aumenta la difesa con Fuoco.", L"Sube la defensa con Fuego.", L"화의 가호로 방어를 올린다.", L"以炎的加护提高防御。", L"يرفع الدفاع بـنار.", L"Повышает защиту силой «Огонь».", L"Hebt die Abwehr mit Feuer.", L"Sobe a defesa com Fogo.", L"Verhoogt de verdediging met Vuur.", L"Podnosi obronę mocą Ogień.", L"Ateş korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 48, L"氷崩", L"Ice Break", L"Glace Bris", L"Ghiaccio Rottura", L"Hielo Quiebre", L"빙붕", L"冰崩", L"جليدكسر", L"Лёд Слом", L"Eis Bruch", L"Gelo Quebra", L"IJs Breuk", L"Lód Złamanie", L"Buz Kırılma")
R(TX_ARTD_FIRST + 48, L"氷属性の魔法。当たった相手を遅らせる。", L"Ice magic that slows those hit.", L"Magie de Glace qui ralentit les touchés.", L"Magia di Ghiaccio che rallenta i colpiti.", L"Magia de Hielo que retrasa a los alcanzados.", L"빙 속성의 마법. 맞은 상대를 늦춘다.", L"冰属性魔法，使被命中者延迟。", L"سحر جليد يبطئ من يُصاب.", L"Магия «Лёд», замедляет поражённых.", L"Eis-Magie, die Getroffene verzögert.", L"Magia de Gelo que atrasa os atingidos.", L"IJs-magie die geraakten vertraagt.", L"Magia Lód, która spowalnia trafionych.", L"İsabet edeni geciktiren Buz büyüsü.")
R(TX_ART_FIRST + 49, L"毒棘", L"Poison Thorn", L"Poison Épine", L"Veleno Spina", L"Veneno Espina", L"독극", L"毒棘", L"سمشوك", L"Яд Шип", L"Gift Dorn", L"Veneno Espinho", L"Gif Doorn", L"Trucizna Cierń", L"Zehir Diken")
R(TX_ARTD_FIRST + 49, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 50, L"聖極", L"Holy Apex", L"Sacré Sommet", L"Sacro Apice", L"Sagrado Cumbre", L"성극의", L"圣极", L"مقدسذروة", L"Свет Вершина", L"Heilig Gipfel", L"Sagrado Ápice", L"Heilig Top", L"Świętość Szczyt", L"Kutsal Zirve")
R(TX_ARTD_FIRST + 50, L"聖属性の魔法。単体にダメージ。", L"Holy magic against one foe.", L"Magie de Sacré sur une cible.", L"Magia di Sacro su un bersaglio.", L"Magia de Sagrado contra un objetivo.", L"성 속성의 마법. 단일 대상.", L"圣属性魔法，单体伤害。", L"سحر مقدس على هدف واحد.", L"Магия «Свет» по одной цели.", L"Heilig-Magie gegen ein Ziel.", L"Magia de Sagrado num alvo.", L"Heilig-magie op één doel.", L"Magia Świętość w jeden cel.", L"Tek hedefe Kutsal büyüsü.")
R(TX_ART_FIRST + 51, L"闇覇", L"Dark Reign", L"Ténèbres Règne", L"Oscurità Regno", L"Oscuridad Reino", L"암패", L"暗霸", L"ظلامسلطان", L"Тьма Власть", L"Dunkel Herrschaft", L"Trevas Reino", L"Duisternis Heerschappij", L"Mrok Panowanie", L"Karanlık Hüküm")
R(TX_ARTD_FIRST + 51, L"闇属性の魔法。着弾点の周囲にダメージ。", L"Dark magic around the impact.", L"Magie de Ténèbres autour de l'impact.", L"Magia di Oscurità attorno all'impatto.", L"Magia de Oscuridad alrededor del impacto.", L"암 속성의 마법. 착탄점 주위.", L"暗属性魔法，命中点周围伤害。", L"سحر ظلام حول موضع الإصابة.", L"Магия «Тьма» вокруг попадания.", L"Dunkel-Magie um den Treffer.", L"Magia de Trevas à volta do impacto.", L"Duisternis-magie rond de inslag.", L"Magia Mrok wokół trafienia.", L"İsabet noktasının çevresine Karanlık büyüsü.")
R(TX_ART_FIRST + 52, L"土弾", L"Earth Bolt", L"Terre Trait", L"Terra Dardo", L"Tierra Rayo", L"토탄", L"土弹", L"أرضسهم", L"Земля Стрела", L"Erde Geschoss", L"Terra Seta", L"Aarde Schicht", L"Ziemia Pocisk", L"Toprak Ok")
R(TX_ARTD_FIRST + 52, L"土属性の魔法。単体にダメージ。", L"Earth magic against one foe.", L"Magie de Terre sur une cible.", L"Magia di Terra su un bersaglio.", L"Magia de Tierra contra un objetivo.", L"토 속성의 마법. 단일 대상.", L"土属性魔法，单体伤害。", L"سحر أرض على هدف واحد.", L"Магия «Земля» по одной цели.", L"Erde-Magie gegen ein Ziel.", L"Magia de Terra num alvo.", L"Aarde-magie op één doel.", L"Magia Ziemia w jeden cel.", L"Tek hedefe Toprak büyüsü.")
R(TX_ART_FIRST + 53, L"毒輪", L"Poison Ring", L"Poison Anneau", L"Veleno Anello", L"Veneno Anillo", L"독륜", L"毒轮", L"سمحلقة", L"Яд Кольцо", L"Gift Ring", L"Veneno Anel", L"Gif Ring", L"Trucizna Pierścień", L"Zehir Halka")
R(TX_ARTD_FIRST + 53, L"毒属性の魔法。着弾点の周囲にダメージ。", L"Poison magic around the impact.", L"Magie de Poison autour de l'impact.", L"Magia di Veleno attorno all'impatto.", L"Magia de Veneno alrededor del impacto.", L"독 속성의 마법. 착탄점 주위.", L"毒属性魔法，命中点周围伤害。", L"سحر سم حول موضع الإصابة.", L"Магия «Яд» вокруг попадания.", L"Gift-Magie um den Treffer.", L"Magia de Veneno à volta do impacto.", L"Gif-magie rond de inslag.", L"Magia Trucizna wokół trafienia.", L"İsabet noktasının çevresine Zehir büyüsü.")
R(TX_ART_FIRST + 54, L"炎刃", L"Fire Edge", L"Feu Lame", L"Fuoco Lama", L"Fuego Filo", L"화인", L"炎刃", L"نارحد", L"Огонь Клинок", L"Feuer Klinge", L"Fogo Lâmina", L"Vuur Kling", L"Ogień Ostrze", L"Ateş Ağız")
R(TX_ARTD_FIRST + 54, L"炎属性の武器攻撃。", L"A weapon strike of Fire.", L"Une attaque d'arme de Feu.", L"Un colpo d'arma di Fuoco.", L"Un ataque de arma de Fuego.", L"화 속성의 무기 공격.", L"炎属性的武器攻击。", L"ضربة سلاح من نار.", L"Удар оружием стихии «Огонь».", L"Ein Waffenschlag mit Feuer.", L"Um golpe de arma de Fogo.", L"Een wapenslag van Vuur.", L"Cios bronią żywiołu Ogień.", L"Ateş nitelikli silah vuruşu.")
R(TX_ART_FIRST + 55, L"氷連撃", L"Ice Flurry", L"Glace Rafale", L"Ghiaccio Raffica", L"Hielo Ráfaga", L"빙연격", L"冰连击", L"جليدهبة", L"Лёд Шквал", L"Eis Hagel", L"Gelo Rajada", L"IJs Vlaag", L"Lód Seria", L"Buz Sağanak")
R(TX_ARTD_FIRST + 55, L"氷属性で二回攻撃する。", L"Two strikes of Ice.", L"Deux coups de Glace.", L"Due colpi di Ghiaccio.", L"Dos golpes de Hielo.", L"빙 속성으로 두 번 공격.", L"以冰属性攻击两次。", L"ضربتان من جليد.", L"Два удара стихии «Лёд».", L"Zwei Schläge mit Eis.", L"Dois golpes de Gelo.", L"Twee slagen van IJs.", L"Dwa ciosy żywiołu Lód.", L"Buz ile iki vuruş.")
R(TX_ART_FIRST + 56, L"雷癒", L"Thunder Mend", L"Foudre Soin", L"Tuono Cura", L"Trueno Cura", L"뢰유", L"雷愈", L"رعدشفاء", L"Гром Лечение", L"Donner Heilung", L"Trovão Cura", L"Donder Heel", L"Grom Leczenie", L"Yıldırım Şifa")
R(TX_ARTD_FIRST + 56, L"雷の力で味方を回復する。", L"Heals an ally with Thunder.", L"Soigne un allié par Foudre.", L"Cura un alleato con Tuono.", L"Cura a un aliado con Trueno.", L"뢰의 힘으로 아군을 회복.", L"以雷之力治疗友军。", L"يشفي حليفًا بـرعد.", L"Лечит союзника силой «Гром».", L"Heilt einen Verbündeten mit Donner.", L"Cura um aliado com Trovão.", L"Geneest een bondgenoot met Donder.", L"Leczy sojusznika mocą Grom.", L"Yıldırım gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 57, L"聖盾", L"Holy Ward", L"Sacré Égide", L"Sacro Egida", L"Sagrado Égida", L"성순", L"圣盾", L"مقدسدرع", L"Свет Щит", L"Heilig Schild", L"Sagrado Égide", L"Heilig Schild", L"Świętość Tarcza", L"Kutsal Kalkan")
R(TX_ARTD_FIRST + 57, L"聖の加護で防御を上げる。", L"Raises defense with Holy.", L"Augmente la défense par Sacré.", L"Aumenta la difesa con Sacro.", L"Sube la defensa con Sagrado.", L"성의 가호로 방어를 올린다.", L"以圣的加护提高防御。", L"يرفع الدفاع بـمقدس.", L"Повышает защиту силой «Свет».", L"Hebt die Abwehr mit Heilig.", L"Sobe a defesa com Sagrado.", L"Verhoogt de verdediging met Heilig.", L"Podnosi obronę mocą Świętość.", L"Kutsal korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 58, L"闇縛", L"Dark Bind", L"Ténèbres Lien", L"Oscurità Laccio", L"Oscuridad Lazo", L"암박", L"暗缚", L"ظلامقيد", L"Тьма Узы", L"Dunkel Band", L"Trevas Laço", L"Duisternis Band", L"Mrok Więź", L"Karanlık Bağ")
R(TX_ARTD_FIRST + 58, L"闇属性の魔法。当たった相手を遅らせる。", L"Dark magic that slows those hit.", L"Magie de Ténèbres qui ralentit les touchés.", L"Magia di Oscurità che rallenta i colpiti.", L"Magia de Oscuridad que retrasa a los alcanzados.", L"암 속성의 마법. 맞은 상대를 늦춘다.", L"暗属性魔法，使被命中者延迟。", L"سحر ظلام يبطئ من يُصاب.", L"Магия «Тьма», замедляет поражённых.", L"Dunkel-Magie, die Getroffene verzögert.", L"Magia de Trevas que atrasa os atingidos.", L"Duisternis-magie die geraakten vertraagt.", L"Magia Mrok, która spowalnia trafionych.", L"İsabet edeni geciktiren Karanlık büyüsü.")
R(TX_ART_FIRST + 59, L"毒牙", L"Poison Fang", L"Poison Croc", L"Veleno Zanna", L"Veneno Colmillo", L"독아", L"毒牙", L"سمناب", L"Яд Клык", L"Gift Fang", L"Veneno Presa", L"Gif Tand", L"Trucizna Kieł", L"Zehir Diş")
R(TX_ARTD_FIRST + 59, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 60, L"土槍", L"Earth Lance", L"Terre Lance", L"Terra Lancia", L"Tierra Lanza", L"토창", L"土枪", L"أرضرمح", L"Земля Копьё", L"Erde Lanze", L"Terra Lança", L"Aarde Lans", L"Ziemia Włócznia", L"Toprak Mızrak")
R(TX_ARTD_FIRST + 60, L"土属性の魔法。単体にダメージ。", L"Earth magic against one foe.", L"Magie de Terre sur une cible.", L"Magia di Terra su un bersaglio.", L"Magia de Tierra contra un objetivo.", L"토 속성의 마법. 단일 대상.", L"土属性魔法，单体伤害。", L"سحر أرض على هدف واحد.", L"Магия «Земля» по одной цели.", L"Erde-Magie gegen ein Ziel.", L"Magia de Terra num alvo.", L"Aarde-magie op één doel.", L"Magia Ziemia w jeden cel.", L"Tek hedefe Toprak büyüsü.")
R(TX_ART_FIRST + 61, L"毒嵐", L"Poison Gale", L"Poison Bourrasque", L"Veleno Burrasca", L"Veneno Vendaval", L"독람", L"毒岚", L"سمعاصفة", L"Яд Шквал", L"Gift Sturm", L"Veneno Vendaval", L"Gif Storm", L"Trucizna Sztorm", L"Zehir Fırtına")
R(TX_ARTD_FIRST + 61, L"毒属性の魔法。着弾点の周囲にダメージ。", L"Poison magic around the impact.", L"Magie de Poison autour de l'impact.", L"Magia di Veleno attorno all'impatto.", L"Magia de Veneno alrededor del impacto.", L"독 속성의 마법. 착탄점 주위.", L"毒属性魔法，命中点周围伤害。", L"سحر سم حول موضع الإصابة.", L"Магия «Яд» вокруг попадания.", L"Gift-Magie um den Treffer.", L"Magia de Veneno à volta do impacto.", L"Gif-magie rond de inslag.", L"Magia Trucizna wokół trafienia.", L"İsabet noktasının çevresine Zehir büyüsü.")
R(TX_ART_FIRST + 62, L"炎斬", L"Fire Slash", L"Feu Taille", L"Fuoco Taglio", L"Fuego Tajo", L"화참", L"炎斩", L"نارقطع", L"Огонь Рубить", L"Feuer Hieb", L"Fogo Corte", L"Vuur Houw", L"Ogień Cięcie", L"Ateş Kesik")
R(TX_ARTD_FIRST + 62, L"炎属性の武器攻撃。", L"A weapon strike of Fire.", L"Une attaque d'arme de Feu.", L"Un colpo d'arma di Fuoco.", L"Un ataque de arma de Fuego.", L"화 속성의 무기 공격.", L"炎属性的武器攻击。", L"ضربة سلاح من نار.", L"Удар оружием стихии «Огонь».", L"Ein Waffenschlag mit Feuer.", L"Um golpe de arma de Fogo.", L"Een wapenslag van Vuur.", L"Cios bronią żywiołu Ogień.", L"Ateş nitelikli silah vuruşu.")
R(TX_ART_FIRST + 63, L"氷双撃", L"Ice Twin", L"Glace Paire", L"Ghiaccio Coppia", L"Hielo Par", L"빙쌍격", L"冰双击", L"جليدزوج", L"Лёд Пара", L"Eis Paar", L"Gelo Par", L"IJs Paar", L"Lód Para", L"Buz Çift")
R(TX_ARTD_FIRST + 63, L"氷属性で二回攻撃する。", L"Two strikes of Ice.", L"Deux coups de Glace.", L"Due colpi di Ghiaccio.", L"Dos golpes de Hielo.", L"빙 속성으로 두 번 공격.", L"以冰属性攻击两次。", L"ضربتان من جليد.", L"Два удара стихии «Лёд».", L"Zwei Schläge mit Eis.", L"Dois golpes de Gelo.", L"Twee slagen van IJs.", L"Dwa ciosy żywiołu Lód.", L"Buz ile iki vuruş.")
R(TX_ART_FIRST + 64, L"雷雫", L"Thunder Drop", L"Foudre Goutte", L"Tuono Goccia", L"Trueno Gota", L"뢰적", L"雷滴", L"رعدقطرة", L"Гром Капля", L"Donner Tropfen", L"Trovão Gota", L"Donder Druppel", L"Grom Kropla", L"Yıldırım Damla")
R(TX_ARTD_FIRST + 64, L"雷の力で味方を回復する。", L"Heals an ally with Thunder.", L"Soigne un allié par Foudre.", L"Cura un alleato con Tuono.", L"Cura a un aliado con Trueno.", L"뢰의 힘으로 아군을 회복.", L"以雷之力治疗友军。", L"يشفي حليفًا بـرعد.", L"Лечит союзника силой «Гром».", L"Heilt einen Verbündeten mit Donner.", L"Cura um aliado com Trovão.", L"Geneest een bondgenoot met Donder.", L"Leczy sojusznika mocą Grom.", L"Yıldırım gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 65, L"聖壁", L"Holy Wall", L"Sacré Mur", L"Sacro Muro", L"Sagrado Muro", L"성벽", L"圣壁", L"مقدسجدار", L"Свет Стена", L"Heilig Wall", L"Sagrado Muro", L"Heilig Wal", L"Świętość Mur", L"Kutsal Duvar")
R(TX_ARTD_FIRST + 65, L"聖の加護で防御を上げる。", L"Raises defense with Holy.", L"Augmente la défense par Sacré.", L"Aumenta la difesa con Sacro.", L"Sube la defensa con Sagrado.", L"성의 가호로 방어를 올린다.", L"以圣的加护提高防御。", L"يرفع الدفاع بـمقدس.", L"Повышает защиту силой «Свет».", L"Hebt die Abwehr mit Heilig.", L"Sobe a defesa com Sagrado.", L"Verhoogt de verdediging met Heilig.", L"Podnosi obronę mocą Świętość.", L"Kutsal korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 66, L"闇鎖", L"Dark Chain", L"Ténèbres Chaîne", L"Oscurità Catena", L"Oscuridad Cadena", L"암쇄", L"暗锁", L"ظلامسلسلة", L"Тьма Цепь", L"Dunkel Kette", L"Trevas Corrente", L"Duisternis Keten", L"Mrok Łańcuch", L"Karanlık Zincir")
R(TX_ARTD_FIRST + 66, L"闇属性の魔法。当たった相手を遅らせる。", L"Dark magic that slows those hit.", L"Magie de Ténèbres qui ralentit les touchés.", L"Magia di Oscurità che rallenta i colpiti.", L"Magia de Oscuridad que retrasa a los alcanzados.", L"암 속성의 마법. 맞은 상대를 늦춘다.", L"暗属性魔法，使被命中者延迟。", L"سحر ظلام يبطئ من يُصاب.", L"Магия «Тьма», замедляет поражённых.", L"Dunkel-Magie, die Getroffene verzögert.", L"Magia de Trevas que atrasa os atingidos.", L"Duisternis-magie die geraakten vertraagt.", L"Magia Mrok, która spowalnia trafionych.", L"İsabet edeni geciktiren Karanlık büyüsü.")
R(TX_ART_FIRST + 67, L"毒針", L"Poison Needle", L"Poison Aiguille", L"Veleno Ago", L"Veneno Aguja", L"독침", L"毒针", L"سمإبرة", L"Яд Игла", L"Gift Nadel", L"Veneno Agulha", L"Gif Naald", L"Trucizna Igła", L"Zehir İğne")
R(TX_ARTD_FIRST + 67, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 68, L"土滅", L"Earth Ruin", L"Terre Fin", L"Terra Fine", L"Tierra Fin", L"토멸", L"土灭", L"أرضفناء", L"Земля Конец", L"Erde Ende", L"Terra Fim", L"Aarde Einde", L"Ziemia Koniec", L"Toprak Son")
R(TX_ARTD_FIRST + 68, L"土属性の魔法。単体にダメージ。", L"Earth magic against one foe.", L"Magie de Terre sur une cible.", L"Magia di Terra su un bersaglio.", L"Magia de Tierra contra un objetivo.", L"토 속성의 마법. 단일 대상.", L"土属性魔法，单体伤害。", L"سحر أرض على هدف واحد.", L"Магия «Земля» по одной цели.", L"Erde-Magie gegen ein Ziel.", L"Magia de Terra num alvo.", L"Aarde-magie op één doel.", L"Magia Ziemia w jeden cel.", L"Tek hedefe Toprak büyüsü.")
R(TX_ART_FIRST + 69, L"毒哮", L"Poison Roar", L"Poison Rugissement", L"Veleno Ruggito", L"Veneno Rugido", L"독효", L"毒哮", L"سمزئير", L"Яд Рёв", L"Gift Brüllen", L"Veneno Rugido", L"Gif Brul", L"Trucizna Ryk", L"Zehir Kükreme")
R(TX_ARTD_FIRST + 69, L"毒属性の魔法。着弾点の周囲にダメージ。", L"Poison magic around the impact.", L"Magie de Poison autour de l'impact.", L"Magia di Veleno attorno all'impatto.", L"Magia de Veneno alrededor del impacto.", L"독 속성의 마법. 착탄점 주위.", L"毒属性魔法，命中点周围伤害。", L"سحر سم حول موضع الإصابة.", L"Магия «Яд» вокруг попадания.", L"Gift-Magie um den Treffer.", L"Magia de Veneno à volta do impacto.", L"Gif-magie rond de inslag.", L"Magia Trucizna wokół trafienia.", L"İsabet noktasının çevresine Zehir büyüsü.")
R(TX_ART_FIRST + 70, L"炎衝", L"Fire Rush", L"Feu Ruée", L"Fuoco Slancio", L"Fuego Embestida", L"화충", L"炎冲", L"ناراندفاع", L"Огонь Рывок", L"Feuer Ansturm", L"Fogo Investida", L"Vuur Stormloop", L"Ogień Szarża", L"Ateş Hamle")
R(TX_ARTD_FIRST + 70, L"炎属性の武器攻撃。", L"A weapon strike of Fire.", L"Une attaque d'arme de Feu.", L"Un colpo d'arma di Fuoco.", L"Un ataque de arma de Fuego.", L"화 속성의 무기 공격.", L"炎属性的武器攻击。", L"ضربة سلاح من نار.", L"Удар оружием стихии «Огонь».", L"Ein Waffenschlag mit Feuer.", L"Um golpe de arma de Fogo.", L"Een wapenslag van Vuur.", L"Cios bronią żywiołu Ogień.", L"Ateş nitelikli silah vuruşu.")
R(TX_ART_FIRST + 71, L"氷舞", L"Ice Dance", L"Glace Danse", L"Ghiaccio Danza", L"Hielo Danza", L"빙무", L"冰舞", L"جليدرقص", L"Лёд Танец", L"Eis Tanz", L"Gelo Dança", L"IJs Dans", L"Lód Taniec", L"Buz Dans")
R(TX_ARTD_FIRST + 71, L"氷属性で二回攻撃する。", L"Two strikes of Ice.", L"Deux coups de Glace.", L"Due colpi di Ghiaccio.", L"Dos golpes de Hielo.", L"빙 속성으로 두 번 공격.", L"以冰属性攻击两次。", L"ضربتان من جليد.", L"Два удара стихии «Лёд».", L"Zwei Schläge mit Eis.", L"Dois golpes de Gelo.", L"Twee slagen van IJs.", L"Dwa ciosy żywiołu Lód.", L"Buz ile iki vuruş.")
R(TX_ART_FIRST + 72, L"雷光", L"Thunder Gleam", L"Foudre Lueur", L"Tuono Bagliore", L"Trueno Brillo", L"뢰광", L"雷光", L"رعدبريق", L"Гром Блеск", L"Donner Glanz", L"Trovão Brilho", L"Donder Glans", L"Grom Blask", L"Yıldırım Parıltı")
R(TX_ARTD_FIRST + 72, L"雷の力で味方を回復する。", L"Heals an ally with Thunder.", L"Soigne un allié par Foudre.", L"Cura un alleato con Tuono.", L"Cura a un aliado con Trueno.", L"뢰의 힘으로 아군을 회복.", L"以雷之力治疗友军。", L"يشفي حليفًا بـرعد.", L"Лечит союзника силой «Гром».", L"Heilt einen Verbündeten mit Donner.", L"Cura um aliado com Trovão.", L"Geneest een bondgenoot met Donder.", L"Leczy sojusznika mocą Grom.", L"Yıldırım gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 73, L"聖陣", L"Holy Field", L"Sacré Champ", L"Sacro Campo", L"Sagrado Campo", L"성진", L"圣阵", L"مقدسميدان", L"Свет Поле", L"Heilig Feld", L"Sagrado Campo", L"Heilig Veld", L"Świętość Pole", L"Kutsal Alan")
R(TX_ARTD_FIRST + 73, L"聖の加護で防御を上げる。", L"Raises defense with Holy.", L"Augmente la défense par Sacré.", L"Aumenta la difesa con Sacro.", L"Sube la defensa con Sagrado.", L"성의 가호로 방어를 올린다.", L"以圣的加护提高防御。", L"يرفع الدفاع بـمقدس.", L"Повышает защиту силой «Свет».", L"Hebt die Abwehr mit Heilig.", L"Sobe a defesa com Sagrado.", L"Verhoogt de verdediging met Heilig.", L"Podnosi obronę mocą Świętość.", L"Kutsal korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 74, L"闇崩", L"Dark Break", L"Ténèbres Bris", L"Oscurità Rottura", L"Oscuridad Quiebre", L"암붕", L"暗崩", L"ظلامكسر", L"Тьма Слом", L"Dunkel Bruch", L"Trevas Quebra", L"Duisternis Breuk", L"Mrok Złamanie", L"Karanlık Kırılma")
R(TX_ARTD_FIRST + 74, L"闇属性の魔法。当たった相手を遅らせる。", L"Dark magic that slows those hit.", L"Magie de Ténèbres qui ralentit les touchés.", L"Magia di Oscurità che rallenta i colpiti.", L"Magia de Oscuridad que retrasa a los alcanzados.", L"암 속성의 마법. 맞은 상대를 늦춘다.", L"暗属性魔法，使被命中者延迟。", L"سحر ظلام يبطئ من يُصاب.", L"Магия «Тьма», замедляет поражённых.", L"Dunkel-Magie, die Getroffene verzögert.", L"Magia de Trevas que atrasa os atingidos.", L"Duisternis-magie die geraakten vertraagt.", L"Magia Mrok, która spowalnia trafionych.", L"İsabet edeni geciktiren Karanlık büyüsü.")
R(TX_ART_FIRST + 75, L"毒棘", L"Poison Thorn", L"Poison Épine", L"Veleno Spina", L"Veneno Espina", L"독극", L"毒棘", L"سمشوك", L"Яд Шип", L"Gift Dorn", L"Veneno Espinho", L"Gif Doorn", L"Trucizna Cierń", L"Zehir Diken")
R(TX_ARTD_FIRST + 75, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 76, L"土極", L"Earth Apex", L"Terre Sommet", L"Terra Apice", L"Tierra Cumbre", L"토극의", L"土极", L"أرضذروة", L"Земля Вершина", L"Erde Gipfel", L"Terra Ápice", L"Aarde Top", L"Ziemia Szczyt", L"Toprak Zirve")
R(TX_ARTD_FIRST + 76, L"土属性の魔法。単体にダメージ。", L"Earth magic against one foe.", L"Magie de Terre sur une cible.", L"Magia di Terra su un bersaglio.", L"Magia de Tierra contra un objetivo.", L"토 속성의 마법. 단일 대상.", L"土属性魔法，单体伤害。", L"سحر أرض على هدف واحد.", L"Магия «Земля» по одной цели.", L"Erde-Magie gegen ein Ziel.", L"Magia de Terra num alvo.", L"Aarde-magie op één doel.", L"Magia Ziemia w jeden cel.", L"Tek hedefe Toprak büyüsü.")
R(TX_ART_FIRST + 77, L"毒覇", L"Poison Reign", L"Poison Règne", L"Veleno Regno", L"Veneno Reino", L"독패", L"毒霸", L"سمسلطان", L"Яд Власть", L"Gift Herrschaft", L"Veneno Reino", L"Gif Heerschappij", L"Trucizna Panowanie", L"Zehir Hüküm")
R(TX_ARTD_FIRST + 77, L"毒属性の魔法。着弾点の周囲にダメージ。", L"Poison magic around the impact.", L"Magie de Poison autour de l'impact.", L"Magia di Veleno attorno all'impatto.", L"Magia de Veneno alrededor del impacto.", L"독 속성의 마법. 착탄점 주위.", L"毒属性魔法，命中点周围伤害。", L"سحر سم حول موضع الإصابة.", L"Магия «Яд» вокруг попадания.", L"Gift-Magie um den Treffer.", L"Magia de Veneno à volta do impacto.", L"Gif-magie rond de inslag.", L"Magia Trucizna wokół trafienia.", L"İsabet noktasının çevresine Zehir büyüsü.")
R(TX_ART_FIRST + 78, L"氷弾", L"Ice Bolt", L"Glace Trait", L"Ghiaccio Dardo", L"Hielo Rayo", L"빙탄", L"冰弹", L"جليدسهم", L"Лёд Стрела", L"Eis Geschoss", L"Gelo Seta", L"IJs Schicht", L"Lód Pocisk", L"Buz Ok")
R(TX_ARTD_FIRST + 78, L"氷属性の魔法。単体にダメージ。", L"Ice magic against one foe.", L"Magie de Glace sur une cible.", L"Magia di Ghiaccio su un bersaglio.", L"Magia de Hielo contra un objetivo.", L"빙 속성의 마법. 단일 대상.", L"冰属性魔法，单体伤害。", L"سحر جليد على هدف واحد.", L"Магия «Лёд» по одной цели.", L"Eis-Magie gegen ein Ziel.", L"Magia de Gelo num alvo.", L"IJs-magie op één doel.", L"Magia Lód w jeden cel.", L"Tek hedefe Buz büyüsü.")
R(TX_ART_FIRST + 79, L"雷輪", L"Thunder Ring", L"Foudre Anneau", L"Tuono Anello", L"Trueno Anillo", L"뢰륜", L"雷轮", L"رعدحلقة", L"Гром Кольцо", L"Donner Ring", L"Trovão Anel", L"Donder Ring", L"Grom Pierścień", L"Yıldırım Halka")
R(TX_ARTD_FIRST + 79, L"雷属性の魔法。着弾点の周囲にダメージ。", L"Thunder magic around the impact.", L"Magie de Foudre autour de l'impact.", L"Magia di Tuono attorno all'impatto.", L"Magia de Trueno alrededor del impacto.", L"뢰 속성의 마법. 착탄점 주위.", L"雷属性魔法，命中点周围伤害。", L"سحر رعد حول موضع الإصابة.", L"Магия «Гром» вокруг попадания.", L"Donner-Magie um den Treffer.", L"Magia de Trovão à volta do impacto.", L"Donder-magie rond de inslag.", L"Magia Grom wokół trafienia.", L"İsabet noktasının çevresine Yıldırım büyüsü.")
R(TX_ART_FIRST + 80, L"聖刃", L"Holy Edge", L"Sacré Lame", L"Sacro Lama", L"Sagrado Filo", L"성인", L"圣刃", L"مقدسحد", L"Свет Клинок", L"Heilig Klinge", L"Sagrado Lâmina", L"Heilig Kling", L"Świętość Ostrze", L"Kutsal Ağız")
R(TX_ARTD_FIRST + 80, L"聖属性の武器攻撃。", L"A weapon strike of Holy.", L"Une attaque d'arme de Sacré.", L"Un colpo d'arma di Sacro.", L"Un ataque de arma de Sagrado.", L"성 속성의 무기 공격.", L"圣属性的武器攻击。", L"ضربة سلاح من مقدس.", L"Удар оружием стихии «Свет».", L"Ein Waffenschlag mit Heilig.", L"Um golpe de arma de Sagrado.", L"Een wapenslag van Heilig.", L"Cios bronią żywiołu Świętość.", L"Kutsal nitelikli silah vuruşu.")
R(TX_ART_FIRST + 81, L"闇連撃", L"Dark Flurry", L"Ténèbres Rafale", L"Oscurità Raffica", L"Oscuridad Ráfaga", L"암연격", L"暗连击", L"ظلامهبة", L"Тьма Шквал", L"Dunkel Hagel", L"Trevas Rajada", L"Duisternis Vlaag", L"Mrok Seria", L"Karanlık Sağanak")
R(TX_ARTD_FIRST + 81, L"闇属性で二回攻撃する。", L"Two strikes of Dark.", L"Deux coups de Ténèbres.", L"Due colpi di Oscurità.", L"Dos golpes de Oscuridad.", L"암 속성으로 두 번 공격.", L"以暗属性攻击两次。", L"ضربتان من ظلام.", L"Два удара стихии «Тьма».", L"Zwei Schläge mit Dunkel.", L"Dois golpes de Trevas.", L"Twee slagen van Duisternis.", L"Dwa ciosy żywiołu Mrok.", L"Karanlık ile iki vuruş.")
R(TX_ART_FIRST + 82, L"風癒", L"Wind Mend", L"Vent Soin", L"Vento Cura", L"Viento Cura", L"풍유", L"风愈", L"ريحشفاء", L"Ветер Лечение", L"Wind Heilung", L"Vento Cura", L"Wind Heel", L"Wiatr Leczenie", L"Rüzgar Şifa")
R(TX_ARTD_FIRST + 82, L"風の力で味方を回復する。", L"Heals an ally with Wind.", L"Soigne un allié par Vent.", L"Cura un alleato con Vento.", L"Cura a un aliado con Viento.", L"풍의 힘으로 아군을 회복.", L"以风之力治疗友军。", L"يشفي حليفًا بـريح.", L"Лечит союзника силой «Ветер».", L"Heilt einen Verbündeten mit Wind.", L"Cura um aliado com Vento.", L"Geneest een bondgenoot met Wind.", L"Leczy sojusznika mocą Wiatr.", L"Rüzgar gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 83, L"土盾", L"Earth Ward", L"Terre Égide", L"Terra Egida", L"Tierra Égida", L"토순", L"土盾", L"أرضدرع", L"Земля Щит", L"Erde Schild", L"Terra Égide", L"Aarde Schild", L"Ziemia Tarcza", L"Toprak Kalkan")
R(TX_ARTD_FIRST + 83, L"土の加護で防御を上げる。", L"Raises defense with Earth.", L"Augmente la défense par Terre.", L"Aumenta la difesa con Terra.", L"Sube la defensa con Tierra.", L"토의 가호로 방어를 올린다.", L"以土的加护提高防御。", L"يرفع الدفاع بـأرض.", L"Повышает защиту силой «Земля».", L"Hebt die Abwehr mit Erde.", L"Sobe a defesa com Terra.", L"Verhoogt de verdediging met Aarde.", L"Podnosi obronę mocą Ziemia.", L"Toprak korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 84, L"毒縛", L"Poison Bind", L"Poison Lien", L"Veleno Laccio", L"Veneno Lazo", L"독박", L"毒缚", L"سمقيد", L"Яд Узы", L"Gift Band", L"Veneno Laço", L"Gif Band", L"Trucizna Więź", L"Zehir Bağ")
R(TX_ARTD_FIRST + 84, L"毒属性の魔法。当たった相手を遅らせる。", L"Poison magic that slows those hit.", L"Magie de Poison qui ralentit les touchés.", L"Magia di Veleno che rallenta i colpiti.", L"Magia de Veneno que retrasa a los alcanzados.", L"독 속성의 마법. 맞은 상대를 늦춘다.", L"毒属性魔法，使被命中者延迟。", L"سحر سم يبطئ من يُصاب.", L"Магия «Яд», замедляет поражённых.", L"Gift-Magie, die Getroffene verzögert.", L"Magia de Veneno que atrasa os atingidos.", L"Gif-magie die geraakten vertraagt.", L"Magia Trucizna, która spowalnia trafionych.", L"İsabet edeni geciktiren Zehir büyüsü.")
R(TX_ART_FIRST + 85, L"毒牙", L"Poison Fang", L"Poison Croc", L"Veleno Zanna", L"Veneno Colmillo", L"독아", L"毒牙", L"سمناب", L"Яд Клык", L"Gift Fang", L"Veneno Presa", L"Gif Tand", L"Trucizna Kieł", L"Zehir Diş")
R(TX_ARTD_FIRST + 85, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 86, L"氷槍", L"Ice Lance", L"Glace Lance", L"Ghiaccio Lancia", L"Hielo Lanza", L"빙창", L"冰枪", L"جليدرمح", L"Лёд Копьё", L"Eis Lanze", L"Gelo Lança", L"IJs Lans", L"Lód Włócznia", L"Buz Mızrak")
R(TX_ARTD_FIRST + 86, L"氷属性の魔法。単体にダメージ。", L"Ice magic against one foe.", L"Magie de Glace sur une cible.", L"Magia di Ghiaccio su un bersaglio.", L"Magia de Hielo contra un objetivo.", L"빙 속성의 마법. 단일 대상.", L"冰属性魔法，单体伤害。", L"سحر جليد على هدف واحد.", L"Магия «Лёд» по одной цели.", L"Eis-Magie gegen ein Ziel.", L"Magia de Gelo num alvo.", L"IJs-magie op één doel.", L"Magia Lód w jeden cel.", L"Tek hedefe Buz büyüsü.")
R(TX_ART_FIRST + 87, L"雷嵐", L"Thunder Gale", L"Foudre Bourrasque", L"Tuono Burrasca", L"Trueno Vendaval", L"뢰람", L"雷岚", L"رعدعاصفة", L"Гром Шквал", L"Donner Sturm", L"Trovão Vendaval", L"Donder Storm", L"Grom Sztorm", L"Yıldırım Fırtına")
R(TX_ARTD_FIRST + 87, L"雷属性の魔法。着弾点の周囲にダメージ。", L"Thunder magic around the impact.", L"Magie de Foudre autour de l'impact.", L"Magia di Tuono attorno all'impatto.", L"Magia de Trueno alrededor del impacto.", L"뢰 속성의 마법. 착탄점 주위.", L"雷属性魔法，命中点周围伤害。", L"سحر رعد حول موضع الإصابة.", L"Магия «Гром» вокруг попадания.", L"Donner-Magie um den Treffer.", L"Magia de Trovão à volta do impacto.", L"Donder-magie rond de inslag.", L"Magia Grom wokół trafienia.", L"İsabet noktasının çevresine Yıldırım büyüsü.")
R(TX_ART_FIRST + 88, L"聖斬", L"Holy Slash", L"Sacré Taille", L"Sacro Taglio", L"Sagrado Tajo", L"성참", L"圣斩", L"مقدسقطع", L"Свет Рубить", L"Heilig Hieb", L"Sagrado Corte", L"Heilig Houw", L"Świętość Cięcie", L"Kutsal Kesik")
R(TX_ARTD_FIRST + 88, L"聖属性の武器攻撃。", L"A weapon strike of Holy.", L"Une attaque d'arme de Sacré.", L"Un colpo d'arma di Sacro.", L"Un ataque de arma de Sagrado.", L"성 속성의 무기 공격.", L"圣属性的武器攻击。", L"ضربة سلاح من مقدس.", L"Удар оружием стихии «Свет».", L"Ein Waffenschlag mit Heilig.", L"Um golpe de arma de Sagrado.", L"Een wapenslag van Heilig.", L"Cios bronią żywiołu Świętość.", L"Kutsal nitelikli silah vuruşu.")
R(TX_ART_FIRST + 89, L"闇双撃", L"Dark Twin", L"Ténèbres Paire", L"Oscurità Coppia", L"Oscuridad Par", L"암쌍격", L"暗双击", L"ظلامزوج", L"Тьма Пара", L"Dunkel Paar", L"Trevas Par", L"Duisternis Paar", L"Mrok Para", L"Karanlık Çift")
R(TX_ARTD_FIRST + 89, L"闇属性で二回攻撃する。", L"Two strikes of Dark.", L"Deux coups de Ténèbres.", L"Due colpi di Oscurità.", L"Dos golpes de Oscuridad.", L"암 속성으로 두 번 공격.", L"以暗属性攻击两次。", L"ضربتان من ظلام.", L"Два удара стихии «Тьма».", L"Zwei Schläge mit Dunkel.", L"Dois golpes de Trevas.", L"Twee slagen van Duisternis.", L"Dwa ciosy żywiołu Mrok.", L"Karanlık ile iki vuruş.")
R(TX_ART_FIRST + 90, L"風雫", L"Wind Drop", L"Vent Goutte", L"Vento Goccia", L"Viento Gota", L"풍적", L"风滴", L"ريحقطرة", L"Ветер Капля", L"Wind Tropfen", L"Vento Gota", L"Wind Druppel", L"Wiatr Kropla", L"Rüzgar Damla")
R(TX_ARTD_FIRST + 90, L"風の力で味方を回復する。", L"Heals an ally with Wind.", L"Soigne un allié par Vent.", L"Cura un alleato con Vento.", L"Cura a un aliado con Viento.", L"풍의 힘으로 아군을 회복.", L"以风之力治疗友军。", L"يشفي حليفًا بـريح.", L"Лечит союзника силой «Ветер».", L"Heilt einen Verbündeten mit Wind.", L"Cura um aliado com Vento.", L"Geneest een bondgenoot met Wind.", L"Leczy sojusznika mocą Wiatr.", L"Rüzgar gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 91, L"土壁", L"Earth Wall", L"Terre Mur", L"Terra Muro", L"Tierra Muro", L"토벽", L"土壁", L"أرضجدار", L"Земля Стена", L"Erde Wall", L"Terra Muro", L"Aarde Wal", L"Ziemia Mur", L"Toprak Duvar")
R(TX_ARTD_FIRST + 91, L"土の加護で防御を上げる。", L"Raises defense with Earth.", L"Augmente la défense par Terre.", L"Aumenta la difesa con Terra.", L"Sube la defensa con Tierra.", L"토의 가호로 방어를 올린다.", L"以土的加护提高防御。", L"يرفع الدفاع بـأرض.", L"Повышает защиту силой «Земля».", L"Hebt die Abwehr mit Erde.", L"Sobe a defesa com Terra.", L"Verhoogt de verdediging met Aarde.", L"Podnosi obronę mocą Ziemia.", L"Toprak korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 92, L"毒鎖", L"Poison Chain", L"Poison Chaîne", L"Veleno Catena", L"Veneno Cadena", L"독쇄", L"毒锁", L"سمسلسلة", L"Яд Цепь", L"Gift Kette", L"Veneno Corrente", L"Gif Keten", L"Trucizna Łańcuch", L"Zehir Zincir")
R(TX_ARTD_FIRST + 92, L"毒属性の魔法。当たった相手を遅らせる。", L"Poison magic that slows those hit.", L"Magie de Poison qui ralentit les touchés.", L"Magia di Veleno che rallenta i colpiti.", L"Magia de Veneno que retrasa a los alcanzados.", L"독 속성의 마법. 맞은 상대를 늦춘다.", L"毒属性魔法，使被命中者延迟。", L"سحر سم يبطئ من يُصاب.", L"Магия «Яд», замедляет поражённых.", L"Gift-Magie, die Getroffene verzögert.", L"Magia de Veneno que atrasa os atingidos.", L"Gif-magie die geraakten vertraagt.", L"Magia Trucizna, która spowalnia trafionych.", L"İsabet edeni geciktiren Zehir büyüsü.")
R(TX_ART_FIRST + 93, L"毒針", L"Poison Needle", L"Poison Aiguille", L"Veleno Ago", L"Veneno Aguja", L"독침", L"毒针", L"سمإبرة", L"Яд Игла", L"Gift Nadel", L"Veneno Agulha", L"Gif Naald", L"Trucizna Igła", L"Zehir İğne")
R(TX_ARTD_FIRST + 93, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 94, L"氷滅", L"Ice Ruin", L"Glace Fin", L"Ghiaccio Fine", L"Hielo Fin", L"빙멸", L"冰灭", L"جليدفناء", L"Лёд Конец", L"Eis Ende", L"Gelo Fim", L"IJs Einde", L"Lód Koniec", L"Buz Son")
R(TX_ARTD_FIRST + 94, L"氷属性の魔法。単体にダメージ。", L"Ice magic against one foe.", L"Magie de Glace sur une cible.", L"Magia di Ghiaccio su un bersaglio.", L"Magia de Hielo contra un objetivo.", L"빙 속성의 마법. 단일 대상.", L"冰属性魔法，单体伤害。", L"سحر جليد على هدف واحد.", L"Магия «Лёд» по одной цели.", L"Eis-Magie gegen ein Ziel.", L"Magia de Gelo num alvo.", L"IJs-magie op één doel.", L"Magia Lód w jeden cel.", L"Tek hedefe Buz büyüsü.")
R(TX_ART_FIRST + 95, L"雷哮", L"Thunder Roar", L"Foudre Rugissement", L"Tuono Ruggito", L"Trueno Rugido", L"뢰효", L"雷哮", L"رعدزئير", L"Гром Рёв", L"Donner Brüllen", L"Trovão Rugido", L"Donder Brul", L"Grom Ryk", L"Yıldırım Kükreme")
R(TX_ARTD_FIRST + 95, L"雷属性の魔法。着弾点の周囲にダメージ。", L"Thunder magic around the impact.", L"Magie de Foudre autour de l'impact.", L"Magia di Tuono attorno all'impatto.", L"Magia de Trueno alrededor del impacto.", L"뢰 속성의 마법. 착탄점 주위.", L"雷属性魔法，命中点周围伤害。", L"سحر رعد حول موضع الإصابة.", L"Магия «Гром» вокруг попадания.", L"Donner-Magie um den Treffer.", L"Magia de Trovão à volta do impacto.", L"Donder-magie rond de inslag.", L"Magia Grom wokół trafienia.", L"İsabet noktasının çevresine Yıldırım büyüsü.")
R(TX_ART_FIRST + 96, L"聖衝", L"Holy Rush", L"Sacré Ruée", L"Sacro Slancio", L"Sagrado Embestida", L"성충", L"圣冲", L"مقدساندفاع", L"Свет Рывок", L"Heilig Ansturm", L"Sagrado Investida", L"Heilig Stormloop", L"Świętość Szarża", L"Kutsal Hamle")
R(TX_ARTD_FIRST + 96, L"聖属性の武器攻撃。", L"A weapon strike of Holy.", L"Une attaque d'arme de Sacré.", L"Un colpo d'arma di Sacro.", L"Un ataque de arma de Sagrado.", L"성 속성의 무기 공격.", L"圣属性的武器攻击。", L"ضربة سلاح من مقدس.", L"Удар оружием стихии «Свет».", L"Ein Waffenschlag mit Heilig.", L"Um golpe de arma de Sagrado.", L"Een wapenslag van Heilig.", L"Cios bronią żywiołu Świętość.", L"Kutsal nitelikli silah vuruşu.")
R(TX_ART_FIRST + 97, L"闇舞", L"Dark Dance", L"Ténèbres Danse", L"Oscurità Danza", L"Oscuridad Danza", L"암무", L"暗舞", L"ظلامرقص", L"Тьма Танец", L"Dunkel Tanz", L"Trevas Dança", L"Duisternis Dans", L"Mrok Taniec", L"Karanlık Dans")
R(TX_ARTD_FIRST + 97, L"闇属性で二回攻撃する。", L"Two strikes of Dark.", L"Deux coups de Ténèbres.", L"Due colpi di Oscurità.", L"Dos golpes de Oscuridad.", L"암 속성으로 두 번 공격.", L"以暗属性攻击两次。", L"ضربتان من ظلام.", L"Два удара стихии «Тьма».", L"Zwei Schläge mit Dunkel.", L"Dois golpes de Trevas.", L"Twee slagen van Duisternis.", L"Dwa ciosy żywiołu Mrok.", L"Karanlık ile iki vuruş.")
R(TX_ART_FIRST + 98, L"風光", L"Wind Gleam", L"Vent Lueur", L"Vento Bagliore", L"Viento Brillo", L"풍광", L"风光", L"ريحبريق", L"Ветер Блеск", L"Wind Glanz", L"Vento Brilho", L"Wind Glans", L"Wiatr Blask", L"Rüzgar Parıltı")
R(TX_ARTD_FIRST + 98, L"風の力で味方を回復する。", L"Heals an ally with Wind.", L"Soigne un allié par Vent.", L"Cura un alleato con Vento.", L"Cura a un aliado con Viento.", L"풍의 힘으로 아군을 회복.", L"以风之力治疗友军。", L"يشفي حليفًا بـريح.", L"Лечит союзника силой «Ветер».", L"Heilt einen Verbündeten mit Wind.", L"Cura um aliado com Vento.", L"Geneest een bondgenoot met Wind.", L"Leczy sojusznika mocą Wiatr.", L"Rüzgar gücüyle bir müttefiki iyileştirir.")
R(TX_ART_FIRST + 99, L"土陣", L"Earth Field", L"Terre Champ", L"Terra Campo", L"Tierra Campo", L"토진", L"土阵", L"أرضميدان", L"Земля Поле", L"Erde Feld", L"Terra Campo", L"Aarde Veld", L"Ziemia Pole", L"Toprak Alan")
R(TX_ARTD_FIRST + 99, L"土の加護で防御を上げる。", L"Raises defense with Earth.", L"Augmente la défense par Terre.", L"Aumenta la difesa con Terra.", L"Sube la defensa con Tierra.", L"토의 가호로 방어를 올린다.", L"以土的加护提高防御。", L"يرفع الدفاع بـأرض.", L"Повышает защиту силой «Земля».", L"Hebt die Abwehr mit Erde.", L"Sobe a defesa com Terra.", L"Verhoogt de verdediging met Aarde.", L"Podnosi obronę mocą Ziemia.", L"Toprak korumasıyla savunmayı yükseltir.")
R(TX_ART_FIRST + 100, L"毒崩", L"Poison Break", L"Poison Bris", L"Veleno Rottura", L"Veneno Quiebre", L"독붕", L"毒崩", L"سمكسر", L"Яд Слом", L"Gift Bruch", L"Veneno Quebra", L"Gif Breuk", L"Trucizna Złamanie", L"Zehir Kırılma")
R(TX_ARTD_FIRST + 100, L"毒属性の魔法。当たった相手を遅らせる。", L"Poison magic that slows those hit.", L"Magie de Poison qui ralentit les touchés.", L"Magia di Veleno che rallenta i colpiti.", L"Magia de Veneno que retrasa a los alcanzados.", L"독 속성의 마법. 맞은 상대를 늦춘다.", L"毒属性魔法，使被命中者延迟。", L"سحر سم يبطئ من يُصاب.", L"Магия «Яд», замедляет поражённых.", L"Gift-Magie, die Getroffene verzögert.", L"Magia de Veneno que atrasa os atingidos.", L"Gif-magie die geraakten vertraagt.", L"Magia Trucizna, która spowalnia trafionych.", L"İsabet edeni geciktiren Zehir büyüsü.")
R(TX_ART_FIRST + 101, L"毒棘", L"Poison Thorn", L"Poison Épine", L"Veleno Spina", L"Veneno Espina", L"독극", L"毒棘", L"سمشوك", L"Яд Шип", L"Gift Dorn", L"Veneno Espinho", L"Gif Doorn", L"Trucizna Cierń", L"Zehir Diken")
R(TX_ARTD_FIRST + 101, L"毒を与える武器攻撃。", L"A weapon strike that poisons.", L"Une attaque d'arme qui empoisonne.", L"Un colpo d'arma che avvelena.", L"Un ataque de arma que envenena.", L"독을 주는 무기 공격.", L"附带中毒的武器攻击。", L"ضربة سلاح تسمّم.", L"Удар оружием, который отравляет.", L"Ein Waffenschlag, der vergiftet.", L"Um golpe de arma que envenena.", L"Een wapenslag die vergiftigt.", L"Cios bronią, który zatruwa.", L"Zehir veren silah vuruşu.")
R(TX_ART_FIRST + 102, L"氷極", L"Ice Apex", L"Glace Sommet", L"Ghiaccio Apice", L"Hielo Cumbre", L"빙극의", L"冰极", L"جليدذروة", L"Лёд Вершина", L"Eis Gipfel", L"Gelo Ápice", L"IJs Top", L"Lód Szczyt", L"Buz Zirve")
R(TX_ARTD_FIRST + 102, L"氷属性の魔法。単体にダメージ。", L"Ice magic against one foe.", L"Magie de Glace sur une cible.", L"Magia di Ghiaccio su un bersaglio.", L"Magia de Hielo contra un objetivo.", L"빙 속성의 마법. 단일 대상.", L"冰属性魔法，单体伤害。", L"سحر جليد على هدف واحد.", L"Магия «Лёд» по одной цели.", L"Eis-Magie gegen ein Ziel.", L"Magia de Gelo num alvo.", L"IJs-magie op één doel.", L"Magia Lód w jeden cel.", L"Tek hedefe Buz büyüsü.")
R(TX_ART_FIRST + 103, L"雷覇", L"Thunder Reign", L"Foudre Règne", L"Tuono Regno", L"Trueno Reino", L"뢰패", L"雷霸", L"رعدسلطان", L"Гром Власть", L"Donner Herrschaft", L"Trovão Reino", L"Donder Heerschappij", L"Grom Panowanie", L"Yıldırım Hüküm")
R(TX_ARTD_FIRST + 103, L"雷属性の魔法。着弾点の周囲にダメージ。", L"Thunder magic around the impact.", L"Magie de Foudre autour de l'impact.", L"Magia di Tuono attorno all'impatto.", L"Magia de Trueno alrededor del impacto.", L"뢰 속성의 마법. 착탄점 주위.", L"雷属性魔法，命中点周围伤害。", L"سحر رعد حول موضع الإصابة.", L"Магия «Гром» вокруг попадания.", L"Donner-Magie um den Treffer.", L"Magia de Trovão à volta do impacto.", L"Donder-magie rond de inslag.", L"Magia Grom wokół trafienia.", L"İsabet noktasının çevresine Yıldırım büyüsü.")

};

#undef R

const wchar_t* const* g_table[TX_COUNT];
bool g_built = false;

void BuildTable()
{
	for (int i = 0; i < TX_COUNT; i++) g_table[i] = nullptr;
	for (const TxRow& r : kRows)
		if (r.id >= 0 && r.id < TX_COUNT) g_table[r.id] = r.s;
	g_built = true;
}

} // namespace

const wchar_t* TT(int id)
{
	if (!g_built) BuildTable();
	if (id < 0 || id >= TX_COUNT || !g_table[id]) return L"?";
	const wchar_t* const* s = g_table[id];
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}

} // namespace otac
