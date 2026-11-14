// 召喚獣。モデルは Quaternius Ultimate Monsters（CC0、待機・移動・攻撃つき）を召喚専用で末尾に足したもの。
// 戦闘計算は見積もりの半分を 1.5 倍する。power はその中で少し差を付ける。代わりに MP と最大 HP の割合を払う。範囲は広め。

struct SumRow {
	const wchar_t* mesh;
	int el, mp, hpPct, power, rMin, rMax, area, hits, effect, fx, price, rank;
	const wchar_t* nm[14];
};

#define W_(x) L##x
#define W(x) W_(x)
#define S14(a,b,c,d,e,f,g,h,i,j,k,l,m,n) { W(a), W(b), W(c), W(d), W(e), W(f), W(g), W(h), W(i), W(j), W(k), W(l), W(m), W(n) }

static const SumRow kSum[SUMMON_N] = {
	{ L"SumDemon", EL_FIRE, 26, 16, 18, 2, 7, 4, 1, EF_NONE, SUM_FX_FIRE, 2200, 0, S14("炎霊カグツチ","Flame Kagutsuchi","Esprit de feu","Spirito di fuoco","Espíritu de fuego","화염령","炎灵","روح النار","Дух огня","Flammengeist","Espírito de fogo","Vuurgeest","Duch ognia","Ateş ruhu") },
	{ L"SumYeti", EL_ICE, 28, 16, 16, 2, 7, 4, 1, EF_NONE, SUM_FX_ICE, 2400, 0, S14("氷霊ヒョウガ","Frost Mammoth","Mammouth de givre","Mammut del gelo","Mamut de escarcha","빙령","冰灵","روح الصقيع","Дух льда","Frostgeist","Espírito do gelo","IJsgeest","Duch mrozu","Buz ruhu") },
	{ L"SumHywirl", EL_THUNDER, 24, 14, 17, 2, 8, 3, 2, EF_STUN, SUM_FX_BOLT, 2000, 0, S14("雷霊イカヅチ","Thunder Eagle","Aigle du tonnerre","Aquila del tuono","Águila del trueno","뇌령","雷灵","نسر الرعد","Орёл грома","Donnervogel","Águia do trovão","Donderarend","Orzeł gromu","Yıldırım kartalı") },
	{ L"SumGolelingEvolved", EL_EARTH, 30, 18, 20, 2, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 2600, 0, S14("土霊ダイチ","Earth Elephant","Éléphant de terre","Elefante di terra","Elefante de tierra","토령","土灵","فيل الأرض","Слон земли","Erdelefant","Elefante da terra","Aardeolifant","Słoń ziemi","Toprak fili") },
	{ L"SumNinja", EL_WIND, 22, 12, 16, 2, 8, 4, 1, EF_NONE, SUM_FX_WIND, 1800, 0, S14("風霊ハヤテ","Gale Wing","Aile de bourrasque","Ala di burrasca","Ala de ráfaga","풍령","风灵","جناح العاصفة","Крыло бури","Sturmschwinge","Asa de ventania","Stormvleugel","Skrzydło wichury","Fırtına kanadı") },
	{ L"SumSquidle", EL_ICE, 26, 15, 17, 2, 7, 4, 1, EF_SLOW, SUM_FX_WAVE, 2300, 0, S14("水霊ミズチ","Tide Shark","Requin de marée","Squalo di marea","Tiburón de marea","수령","水灵","قرش المد","Акула прилива","Gezeitenhai","Tubarão da maré","Getijdenhaai","Rekin przypływu","Gelgit köpekbalığı") },
	{ L"SumGoleling", EL_EARTH, 24, 15, 19, 2, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 2100, 0, S14("岩霊ゴウガン","Boulder Rhino","Rhino de roc","Rinoceronte di roccia","Rino de roca","암령","岩灵","وحيد صخر","Носорог скалы","Felsenrhino","Rinoceronte de rocha","Rotsneushoorn","Nosorożec skały","Kaya gergedanı") },
	{ L"SumWizard", EL_HOLY, 32, 18, 18, 2, 8, 4, 1, EF_NONE, SUM_FX_HOLY, 3200, 0, S14("光霊セイカ","Radiant Peacock","Paon radieux","Pavone radioso","Pavo radiante","광령","光灵","طاووس مشع","Павлин света","Strahlpfau","Pavão radiante","Stralendpauw","Paw światła","Işıltılı tavus") },
	{ L"SumGhost", EL_DARK, 28, 16, 17, 2, 7, 4, 1, EF_DEFDOWN, SUM_FX_DARK, 2500, 0, S14("闇霊クラヤミ","Gloom Bat","Chauve-souris d'ombre","Pipistrello d'ombra","Murciélago de sombra","암령","暗灵","خفاش الظلام","Летучая тьма","Düsternisflatterer","Morcego da treva","Duisternisvleermuis","Nietoperz mroku","Karanlık yarasa") },
	{ L"SumMushroomKing", EL_POISON, 24, 14, 15, 2, 7, 3, 1, EF_POISON, SUM_FX_MIASMA, 1900, 0, S14("毒霊ドクゼ","Venom Scorpion","Scorpion venimeux","Scorpione velenoso","Escorpión venenoso","독령","毒灵","عقرب السم","Скорпион яда","Giftskorpion","Escorpião venenoso","Gifschorpioen","Skorpion jadu","Zehir akrebi") },
	{ L"SumOrc", EL_FIRE, 20, 12, 16, 1, 6, 3, 1, EF_NONE, SUM_FX_FIRE, 1400, 0, S14("牙狼ガロウ","Fang Wolf","Loup à crocs","Lupo zannuto","Lobo de colmillos","아랑","牙狼","ذئب الأنياب","Клыкастый волк","Fangwolf","Lobo de presas","Tandwolf","Wilk kłów","Diş kurdu") },
	{ L"SumBirb", EL_WIND, 22, 12, 16, 2, 8, 4, 1, EF_NONE, SUM_FX_WIND, 1700, 0, S14("大鷲ワシア","Great Eagle","Grand aigle","Grande aquila","Gran águila","대수리","大鹫","نسر كبير","Большой орёл","Riesenaar","Grande águia","Grote arend","Wielki orzeł","Büyük kartal") },
	{ L"SumOrcEnemy", EL_EARTH, 34, 20, 22, 2, 6, 3, 1, EF_STUN, SUM_FX_ROAR, 3600, 0, S14("巨熊トラバ","Colossus Bear","Ours colosse","Orso colosso","Oso coloso","거웅","巨熊","دب عملاق","Медведь-исполин","Kolossbär","Urso colosso","Kolossale beer","Niedźwiedź kolos","Dev ayı") },
	{ L"SumArmabeeEvolved", EL_WIND, 20, 12, 15, 1, 6, 3, 1, EF_NONE, SUM_FX_WIND, 1300, 0, S14("鎌蟲カマキリ","Sickle Mantis","Mante faucille","Mantide falce","Mantis hoz","낫벌레","镰螳","سرعوف المنجل","Богомол-серп","Sichelmantis","Louva-a-deus foice","Sikkelbidsprinkhaan","Modliszka sierp","Orak peygamberdevesi") },
	{ L"SumGlubEvolved", EL_ICE, 32, 18, 18, 2, 7, 4, 1, EF_POISON, SUM_FX_WAVE, 3400, 0, S14("海蛇ナーガ","Trench Naga","Naga des fosses","Naga degli abissi","Naga de la fosa","해사","海蛇","حورية الخندق","Нага пучины","Graben-Naga","Naga da fossa","Sleufnaga","Naga rowu","Hendek naga") },
	{ L"SumTribal", EL_EARTH, 26, 16, 20, 2, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 2400, 0, S14("猪王イノシ","Iron Boar","Sanglier de fer","Cinghiale di ferro","Jabalí de hierro","철멧돼지","铁猪王","خنزير الحديد","Железный кабан","Eisenkeiler","Javali de ferro","IJzeren ever","Żelazny dzik","Demir yaban") },
	{ L"SumMonkroose", EL_FIRE, 30, 16, 19, 2, 7, 4, 1, EF_STUN, SUM_FX_ROAR, 3000, 0, S14("獅子レオ","Roaring Lion","Lion rugissant","Leone ruggente","León rugiente","사자","狮子","أسد زائر","Рыкающий лев","Brüllender Löwe","Leão rugidor","Brullende leeuw","Ryczący lew","Kükreyen aslan") },
	{ L"SumGreenSpikyBlob", EL_ICE, 22, 13, 16, 1, 6, 3, 1, EF_SLOW, SUM_FX_ICE, 1600, 0, S14("巨蟹クラブ","Scarlet Crab","Crabe écarlate","Granchio scarlatto","Cangrejo escarlata","거대게","巨蟹","سلطعون قرمزي","Багровый краб","Scharlachkrabbe","Caranguejo escarlate","Scharlakenkrab","Szkarłatny krab","Kızıl yengeç") },
	{ L"SumAlpaking", EL_EARTH, 20, 12, 15, 2, 7, 3, 1, EF_NONE, SUM_FX_SPIKE, 1200, 0, S14("角鹿エルク","Antler Stag","Cerf ramure","Cervo ramato","Ciervo astado","뿔사슴","角鹿","أيل القرون","Рогатый олень","Geweihhirsch","Cervo de galhada","Geweiherte","Jeleń poroża","Boynuzlu geyik") },
	{ L"SumGhostSkull", EL_DARK, 26, 14, 14, 2, 8, 4, 1, EF_SLEEP, SUM_FX_SLEEP, 2200, 0, S14("夜梟フクロウ","Night Owl","Hibou de nuit","Gufo notturno","Búho nocturno","밤부엉이","夜枭","بومة الليل","Ночная сова","Nachteule","Coruja da noite","Nachtuil","Nocna sowa","Gece baykuşu") },
	{ L"SumArmabee", EL_POISON, 18, 10, 14, 2, 7, 4, 1, EF_POISON, SUM_FX_MIASMA, 1100, 0, S14("蜂群ホウネ","Swarm of Bees","Essaim d'abeilles","Sciame d'api","Enjambre de abejas","벌떼","蜂群","سرب النحل","Рой пчёл","Bienenschwarm","Enxame de abelhas","Bijenzwerm","Rój pszczół","Arı sürüsü") },
	{ L"SumPinkSlime", EL_DARK, 16, 10, 13, 1, 6, 3, 1, EF_NONE, SUM_FX_DARK, 900, 0, S14("群鼠ネズミ","Rat Tide","Marée de rats","Marea di ratti","Marea de ratas","쥐떼","群鼠","مد الجرذان","Крысиный вал","Rattenflut","Maré de ratos","Rattenvloed","Fala szczurów","Sıçan seli") },
	{ L"SumAlien", EL_EARTH, 22, 12, 16, 2, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 1500, 0, S14("大猿エンコ","Great Ape","Grand singe","Grande scimmia","Gran simio","큰원숭이","大猿","قرد عظيم","Большая обезьяна","Großer Affe","Grande símio","Grote aap","Wielka małpa","Büyük maymun") },
	{ L"SumAlpakingEvolved", EL_HOLY, 36, 18, 20, 2, 8, 4, 1, EF_NONE, SUM_FX_HOLY, 5200, 0, S14("聖獣キリン","Qilin","Qilin","Qilin","Qilin","기린","麒麟","كيلين","Цилинь","Qilin","Qilin","Qilin","Qilin","Qilin") },
	{ L"SumDragonEvolved", EL_FIRE, 40, 22, 22, 2, 8, 5, 2, EF_NONE, SUM_FX_FIRE, 8400, 0, S14("不死鳥ホウオウ","Phoenix","Phénix","Fenice","Fénix","불사조","不死鸟","العنقاء","Феникс","Phönix","Fênix","Feniks","Feniks","Anka") },
	{ L"SumCactoro", EL_HOLY, 34, 16, 19, 2, 7, 3, 1, EF_NONE, SUM_FX_HOLY, 4800, 0, S14("一角獣ユニコーン","Unicorn","Licorne","Unicorno","Unicornio","유니콘","独角兽","وحيد القرن","Единорог","Einhorn","Unicórnio","Eenhoorn","Jednorożec","Tekboynuz") },
	{ L"SumMonkroose", EL_WIND, 32, 16, 18, 2, 8, 4, 1, EF_NONE, SUM_FX_WIND, 4200, 0, S14("獅鷲グリフォン","Griffin","Griffon","Grifone","Grifo","그리폰","狮鹫","عنقاء أسد","Грифон","Greif","Grifo","Griffioen","Gryf","Grifon") },
	{ L"SumDragon", EL_FIRE, 36, 18, 21, 2, 8, 4, 1, EF_NONE, SUM_FX_FIRE, 5600, 0, S14("飛竜ワイバーン","Wyvern","Vouivre","Viverna","Güiverno","와이번","飞龙","وايفرن","Виверна","Wyvern","Wyvern","Wyvern","Wiwerna","Wyvern") },
	{ L"SumMushnubEvolved", EL_EARTH, 30, 16, 17, 2, 7, 4, 1, EF_SLOW, SUM_FX_SPIKE, 3000, 0, S14("樹霊トレント","Treant","Tréant","Treant","Treant","트렌트","树灵","شجرة حية","Древень","Treant","Treant","Treant","Treant","Treant") },
	{ L"SumFish", EL_ICE, 42, 24, 22, 2, 8, 5, 1, EF_SLOW, SUM_FX_WAVE, 9200, 0, S14("海王クジラ","Leviathan","Léviathan","Leviatano","Leviatán","리바이어던","海王","لوياثان","Левиафан","Leviathan","Leviatã","Leviathan","Lewiatan","Leviatan") },
	{ L"SumDino", EL_EARTH, 38, 20, 22, 2, 7, 4, 1, EF_STUN, SUM_FX_ROAR, 7000, 0, S14("巨獣ベヘモ","Behemoth","Béhémoth","Behemoth","Behemot","베헤모스","巨兽","بهيموث","Бегемот","Behemoth","Behemoth","Behemoth","Behemot","Behemot") },
	{ L"SumGoleling", EL_EARTH, 28, 16, 18, 2, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 2800, 0, S14("鎧竜アンキロ","Armored Saurian","Saurien cuirassé","Sauro corazzato","Saurio acorazado","갑룡","铠龙","سحلية مدرعة","Панцирный ящер","Panzerosaurier","Sáurio couraçado","Gepantserde sauriër","Opancerzony gad","Zırhlı sürüngen") },
	{ L"SumGlub", EL_ICE, 24, 14, 15, 2, 7, 4, 1, EF_SLOW, SUM_FX_WAVE, 1800, 0, S14("水精ウンディーネ","Undine","Ondine","Ondina","Ondina","운디네","水精","أوندين","Ундина","Undine","Ondina","Undine","Undyna","Undine") },
	{ L"SumGreenBlob", EL_WIND, 18, 10, 14, 2, 8, 4, 1, EF_NONE, SUM_FX_WIND, 1000, 0, S14("風精シルフ","Sylph","Sylphe","Silfide","Sílfide","실프","风精","سيلف","Сильф","Sylphe","Silfo","Sylf","Sylf","Silf") },
	{ L"SumMushnub", EL_EARTH, 18, 12, 15, 1, 6, 3, 1, EF_NONE, SUM_FX_SPIKE, 1000, 0, S14("地精ノーム","Gnome","Gnome","Gnomo","Gnomo","놈","地精","جنوم","Гном","Gnom","Gnomo","Gnoom","Gnom","Gnom") },
	{ L"SumBlueDemon", EL_THUNDER, 34, 16, 20, 2, 7, 4, 2, EF_STUN, SUM_FX_BOLT, 4600, 0, S14("雷獣ライジュウ","Thunder Beast","Bête du tonnerre","Bestia del tuono","Bestia del trueno","뇌수","雷兽","وحش الرعد","Зверь грома","Donnerbestie","Fera do trovão","Donderbeest","Bestia gromu","Gök gürültüsü canavarı") },
#include "tactics_src/summon_high.inc"
#include "tactics_src/summon_vault.inc"
};

#undef S14
#undef W
#undef W_
static_assert(sizeof(kSum) / sizeof(kSum[0]) == SUMMON_N, "summon table");

static const wchar_t* SumPick(const wchar_t* const* s)
{
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}

const wchar_t* SummonWord()
{
	return LangPick14(L"召喚", L"Summon", L"Invocation", L"Evocazione", L"Invocación", L"소환", L"召唤", L"استدعاء", L"Призыв", L"Beschwörung", L"Invocação", L"Oproeping", L"Przyzwanie", L"Çağırma");
}

const wchar_t* SummonName(int id)
{
	if (id < 1 || id > SUMMON_N) return TT(TX_PT_NONE);
	return SumPick(kSum[id - 1].nm);
}

static const wchar_t* SumFxLine(int fx)
{
	switch (fx) {
	case SUM_FX_FIRE: return LangPick14(L"召喚獣が突っ込み、敵を炎が包む。", L"The beast charges and fire swallows the foes.", L"La bête charge et le feu engloutit les ennemis.", L"La bestia carica e il fuoco avvolge i nemici.", L"La bestia embiste y el fuego traga a los enemigos.", L"소환수가 돌진하고 불꽃이 적을 감싼다.", L"召唤兽冲锋，火焰吞没敌人。", L"يندفع الوحش وتبتلع النار الأعداء.", L"Зверь бьёт напролом, пламя глотает врагов.", L"Das Untier stürmt, Feuer schluckt die Feinde.", L"A fera investe e o fogo engole os inimigos.", L"Het beest stormt en vuur slokt de vijanden.", L"Bestia szarżuje, ogień połyka wrogów.", L"Yaratık dalar, ateş düşmanları yutar.");
	case SUM_FX_ICE: return LangPick14(L"敵が氷の標柱に閉じ込められる。", L"Foes are sealed inside pillars of ice.", L"Les ennemis sont scellés dans des piliers de glace.", L"I nemici restano chiusi in colonne di ghiaccio.", L"Los enemigos quedan en pilares de hielo.", L"적이 얼음 기둥에 갇힌다.", L"敌人被封进冰柱。", L"يُحبس الأعداء في أعمدة جليد.", L"Врагов замыкает в ледяные столбы.", L"Feinde werden in Eissäulen eingeschlossen.", L"Os inimigos ficam em pilares de gelo.", L"Vijanden zitten vast in ijspilaren.", L"Wrogowie zostają w lodowych słupach.", L"Düşmanlar buz sütunlarına kapanır.");
	case SUM_FX_BOLT: return LangPick14(L"空から雷が落ちる。", L"Lightning falls out of the sky.", L"La foudre tombe du ciel.", L"Il fulmine cade dal cielo.", L"El rayo cae del cielo.", L"하늘에서 번개가 떨어진다.", L"雷从空中落下。", L"يسقط البرق من السماء.", L"Молния бьёт с неба.", L"Blitze fallen vom Himmel.", L"O raio cai do céu.", L"Bliksem valt uit de lucht.", L"Piorun spada z nieba.", L"Yıldırım gökten düşer.");
	case SUM_FX_SPIKE: return LangPick14(L"岩の棘が足元から突き上がる。", L"Rock spikes drive up under their feet.", L"Des pointes de roc jaillissent sous leurs pieds.", L"Punte di roccia salgono sotto i piedi.", L"Púas de roca suben bajo sus pies.", L"바위 가시가 발밑에서 치솟는다.", L"岩刺从脚下刺出。", L"أشواك صخر تندفع من تحت الأقدام.", L"Каменные шипы бьют из-под ног.", L"Felsdornen stoßen unter den Füßen hoch.", L"Espinhos de rocha sobem sob os pés.", L"Rotspunten schieten onder de voeten omhoog.", L"Kamienne kolce wychodzą spod stóp.", L"Kaya dikenleri ayakların altından fırlar.");
	case SUM_FX_WIND: return LangPick14(L"風の刃が敵を横切る。", L"Blades of wind cut across the foes.", L"Des lames de vent traversent les ennemis.", L"Lame di vento tagliano i nemici.", L"Hojas de viento cruzan a los enemigos.", L"바람의 칼날이 적을 가른다.", L"风刃横切敌人。", L"شفرات ريح تقطع الأعداء.", L"Клинки ветра секут врагов.", L"Windklingen schneiden durch die Feinde.", L"Lâminas de vento cortam os inimigos.", L"Windklingen snijden door de vijanden.", L"Ostrza wiatru tną wrogów.", L"Rüzgar bıçakları düşmanları keser.");
	case SUM_FX_WAVE: return LangPick14(L"波が敵を呑む。", L"A wave swallows the foes.", L"Une vague engloutit les ennemis.", L"Un'onda inghiotte i nemici.", L"Una ola traga a los enemigos.", L"파도가 적을 삼킨다.", L"浪涛吞没敌人。", L"موجة تبتلع الأعداء.", L"Волна глотает врагов.", L"Eine Welle schluckt die Feinde.", L"Uma onda engole os inimigos.", L"Een golf slokt de vijanden.", L"Fala połyka wrogów.", L"Dalga düşmanları yutar.");
	case SUM_FX_HOLY: return LangPick14(L"光の柱が敵の上に立つ。", L"Pillars of light stand over the foes.", L"Des piliers de lumière se dressent sur les ennemis.", L"Colonne di luce stanno sui nemici.", L"Pilares de luz se alzan sobre los enemigos.", L"빛의 기둥이 적 위에 선다.", L"光柱立在敌人头顶。", L"أعمدة نور تقوم فوق الأعداء.", L"Столпы света встают над врагами.", L"Lichtsäulen stehen über den Feinden.", L"Pilares de luz erguem-se sobre os inimigos.", L"Zuilen van licht staan boven de vijanden.", L"Słupy światła stoją nad wrogami.", L"Işık sütunları düşmanların üstünde durur.");
	case SUM_FX_DARK: return LangPick14(L"闇が渦を巻いて敵を抉る。", L"A dark vortex tears at the foes.", L"Un vortex d'ombre déchire les ennemis.", L"Un vortice d'ombra lacera i nemici.", L"Un vórtice oscuro desgarra a los enemigos.", L"어둠의 소용돌이가 적을 가른다.", L"暗涡撕扯敌人。", L"دوامة ظلام تمزق الأعداء.", L"Тёмный вихрь рвёт врагов.", L"Ein dunkler Wirbel zerreißt die Feinde.", L"Um vórtice escuro rasga os inimigos.", L"Een donkere draaikolk scheurt de vijanden.", L"Mroczny wir szarpie wrogów.", L"Karanlık girdap düşmanları yırtar.");
	case SUM_FX_MIASMA: return LangPick14(L"毒霧が範囲に広がる。", L"Poison mist spreads over the area.", L"Un brouillard de poison couvre la zone.", L"Una nebbia di veleno copre l'area.", L"Niebla venenosa cubre el área.", L"독안개가 범위에 퍼진다.", L"毒雾铺开。", L"ضباب سم ينتشر.", L"Ядовитый туман стелется.", L"Giftnebel breitet sich aus.", L"Névoa venenosa cobre a área.", L"Gifmist spreidt zich uit.", L"Trująca mgła pokrywa obszar.", L"Zehir sisi alana yayılır.");
	case SUM_FX_ROAR: return LangPick14(L"咆哮が敵の足を止める。", L"A roar stops the foes in their tracks.", L"Un rugissement cloue les ennemis.", L"Un ruggito inchioda i nemici.", L"Un rugido clava a los enemigos.", L"포효가 적의 발을 멈춘다.", L"咆哮止住敌人的脚步。", L"زئير يوقف الأعداء.", L"Рёв останавливает врагов.", L"Ein Brüllen hält die Feinde fest.", L"Um rugido prende os inimigos.", L"Een brul houdt de vijanden tegen.", L"Ryk zatrzymuje wrogów.", L"Kükreme düşmanları durdurur.");
	case SUM_FX_SLEEP: return LangPick14(L"羽根が舞って敵を眠らせる。", L"Feathers drift down and put foes to sleep.", L"Des plumes endorment les ennemis.", L"Piume fanno dormire i nemici.", L"Plumas duermen a los enemigos.", L"깃털이 내려 적을 재운다.", L"羽毛飘落，使敌人入睡。", L"ريش يُنام الأعداء.", L"Перья усыпляют врагов.", L"Federn schläfern die Feinde ein.", L"Penas põem os inimigos a dormir.", L"Veren sussen de vijanden.", L"Pióra usypiają wrogów.", L"Tüyler düşmanları uyutur.");
	default: return LangPick14(L"糸が敵の足を絡める。", L"Threads bind the foes' feet.", L"Des fils lient les pieds ennemis.", L"Fili legano i piedi nemici.", L"Hilos atan los pies enemigos.", L"실이 적의 발을 묶는다.", L"丝线缠住敌人的脚。", L"خيوط تربط أقدام الأعداء.", L"Нити вяжут ноги врагов.", L"Fäden binden die Füße der Feinde.", L"Fios prendem os pés inimigos.", L"Draden binden de voeten.", L"Nici wiążą stopy wrogów.", L"İpler düşmanların ayaklarını bağlar.");
	}
}

static int SummonMpOf(const SumRow& r)
{
	int m = r.mp * 2;
	int lo = 40, hi = 90;
	if (r.rank >= 2) { lo = 100; hi = 130; }
	else if (r.rank == 1) { lo = 80; hi = 110; }
	if (m < lo) m = lo;
	if (m > hi) m = hi;
	return m;
}

static int SummonHpPctOf(const SumRow& r)
{
	int p = r.hpPct * 2;
	int lo = 30, hi = 48;
	if (r.rank >= 2) { lo = 46; hi = 58; }
	else if (r.rank == 1) { lo = 40; hi = 52; }
	if (p < lo) p = lo;
	if (p > hi) p = hi;
	return p;
}

static int SummonCpOf(const SumRow& r)
{
	if (r.rank >= 2) return 160;
	if (r.rank == 1) return 130;
	return 100;
}

static const wchar_t* SumStatusLine(int eff)
{
	switch (eff) {
	case EF_DEFDOWN: return LangPick14(L"当たると防御が下がる。", L"A hit lowers defense. ", L"Un coup baisse la défense. ", L"Un colpo cala la difesa. ", L"Un golpe baja la defensa. ", L"맞으면 방어가 떨어진다. ", L"命中会降低防御。", L"الضربة تخفض الدفاع. ", L"Попадание снижает защиту. ", L"Ein Treffer senkt die Abwehr. ", L"Um acerto baixa a defesa. ", L"Een treffer verlaagt de verdediging. ", L"Trafienie obniża obronę. ", L"İsabet savunmayı düşürür. ");
	case EF_STUN: return LangPick14(L"当たるとスタンする。", L"A hit stuns. ", L"Un coup étourdit. ", L"Un colpo stordisce. ", L"Un golpe aturde. ", L"맞으면 기절한다. ", L"命中会造成硬直。", L"الضربة تصعق. ", L"Попадание оглушает. ", L"Ein Treffer betäubt. ", L"Um acerto atordoa. ", L"Een treffer verdooft. ", L"Trafienie ogłusza. ", L"İsabet sersemletir. ");
	case EF_SLOW: return LangPick14(L"当たると行動が遅れる。", L"A hit delays the turn. ", L"Un coup retarde le tour. ", L"Un colpo ritarda il turno. ", L"Un golpe retrasa el turno. ", L"맞으면 행동이 늦어진다. ", L"命中会推迟行动。", L"الضربة تؤخر الدور. ", L"Попадание откладывает ход. ", L"Ein Treffer verzögert den Zug. ", L"Um acerto atrasa o turno. ", L"Een treffer vertraagt de beurt. ", L"Trafienie opóźnia turę. ", L"İsabet sırayı geciktirir. ");
	case EF_POISON: return LangPick14(L"当たると毒になる。", L"A hit poisons. ", L"Un coup empoisonne. ", L"Un colpo avvelena. ", L"Un golpe envenena. ", L"맞으면 독에 걸린다. ", L"命中会造成中毒。", L"الضربة تسمم. ", L"Попадание отравляет. ", L"Ein Treffer vergiftet. ", L"Um acerto envenena. ", L"Een treffer vergiftigt. ", L"Trafienie zatruwa. ", L"İsabet zehirler. ");
	case EF_BURN: return LangPick14(L"当たると炎上する。", L"A hit burns. ", L"Un coup brûle. ", L"Un colpo brucia. ", L"Un golpe quema. ", L"맞으면 불탄다. ", L"命中会造成炎上。", L"الضربة تحرق. ", L"Попадание поджигает. ", L"Ein Treffer entzündet. ", L"Um acerto queima. ", L"Een treffer doet branden. ", L"Trafienie podpala. ", L"İsabet yakar. ");
	case EF_SLEEP: return LangPick14(L"当たると眠る。", L"A hit puts foes to sleep. ", L"Un coup endort. ", L"Un colpo fa dormire. ", L"Un golpe duerme. ", L"맞으면 잠든다. ", L"命中会造成睡眠。", L"الضربة تُنام. ", L"Попадание усыпляет. ", L"Ein Treffer schläfert ein. ", L"Um acerto faz dormir. ", L"Een treffer doet slapen. ", L"Trafienie usypia. ", L"İsabet uyutur. ");
	default: return LangPick14(L"追加の状態異常はない。", L"It adds no ailment. ", L"Aucun état en plus. ", L"Nessun malus in più. ", L"No añade estado. ", L"추가 상태 이상은 없다. ", L"没有额外异常。", L"لا حالة إضافية. ", L"Без дополнительного состояния. ", L"Kein zusätzlicher Zustand. ", L"Sem estado extra. ", L"Geen extra toestand. ", L"Bez dodatkowego stanu. ", L"Ek durum yok. ");
	}
}
const wchar_t* SummonDesc(int id)
{
	static wchar_t buf[768];
	if (id < 1 || id > SUMMON_N) return L"";
	const SumRow& r = kSum[id - 1];
	const wchar_t* el = TacElemWord(r.el);
	if (!el || !el[0]) el = LangPick14(L"無", L"plain", L"neutre", L"neutro", L"neutro", L"무", L"无", L"بلا", L"без", L"neutral", L"neutro", L"neutraal", L"neutralny", L"yalın");
	const wchar_t* fmt = LangPick14(
		L"%sを呼ぶ。%s属性、威力%d、%d回命中、射程%d〜%d、範囲%d。%s支払うのは MP %d、最大HPの%d%%、CP %d。%s",
		L"Calls %s. %s, power %d, %d hits, range %d-%d, area %d. %sYou pay MP %d, %d%% of max HP, and CP %d. %s",
		L"Appelle %s. %s, puissance %d, %d coups, portée %d-%d, zone %d. %sCoût : PM %d, %d%% des PV max, PC %d. %s",
		L"Chiama %s. %s, potenza %d, %d colpi, gittata %d-%d, area %d. %sCosto: PM %d, %d%% dei PV max, CP %d. %s",
		L"Invoca %s. %s, potencia %d, %d golpes, alcance %d-%d, área %d. %sCuesta PM %d, %d%% de PV máx. y CP %d. %s",
		L"%s를 부른다. %s 속성, 위력 %d, %d회 명중, 사거리 %d~%d, 범위 %d. %s지불은 MP %d, 최대 HP %d%%, CP %d. %s",
		L"召唤%s。%s属性，威力%d，命中%d次，射程%d～%d，范围%d。%s消耗 MP %d、最大HP的%d%%、CP %d。%s",
		L"يستدعي %s. %s، القوة %d، %d ضربات، المدى %d-%d، المساحة %d. %sالتكلفة MP %d و%d%% من أقصى HP وCP %d. %s",
		L"Призывает %s. %s, сила %d, %d удара, дальность %d–%d, область %d. %sПлата: MP %d, %d%% макс. HP, CP %d. %s",
		L"Ruft %s. %s, Stärke %d, %d Treffer, Reichweite %d–%d, Fläche %d. %sKosten: MP %d, %d%% max. HP, CP %d. %s",
		L"Chama %s. %s, poder %d, %d golpes, alcance %d-%d, área %d. %sCusta PM %d, %d%% do HP máx. e CP %d. %s",
		L"Roept %s. %s, kracht %d, %d treffers, bereik %d-%d, gebied %d. %sKost MP %d, %d%% max HP en CP %d. %s",
		L"Przyzywa %s. %s, siła %d, %d trafienia, zasięg %d–%d, obszar %d. %sKoszt: MP %d, %d%% maks. HP i CP %d. %s",
		L"%s çağırır. %s, güç %d, %d vuruş, menzil %d-%d, alan %d. %sBedel MP %d, azami HP'nin %d%%'i ve CP %d. %s");
	swprintf_s(buf, fmt, SumPick(r.nm), el, r.power, r.hits, r.rMin, r.rMax, r.area, SumStatusLine(r.effect), SummonMpOf(r), SummonHpPctOf(r), SummonCpOf(r), SumFxLine(r.fx));
	return buf;
}

int SummonCount() { return SUMMON_N; }
int SummonPrice(int index) { return (index < 0 || index >= SUMMON_N) ? 0 : kSum[index].price; }
int SummonFx(int index) { return (index < 0 || index >= SUMMON_N) ? 0 : kSum[index].fx; }
int SummonRank(int index) { return (index < 0 || index >= SUMMON_N) ? 0 : kSum[index].rank; }

const wchar_t* SummonRankWord(int rank)
{
	if (rank >= 2) return LangPick14(L"最上位精霊", L"Supreme Spirit", L"Esprit suprême", L"Spirito supremo", L"Espíritu supremo", L"최상위 정령", L"最上位精灵", L"روح أسمى", L"Верховный дух", L"Höchster Geist", L"Espírito supremo", L"Oppergeest", L"Najwyższy duch", L"Yüce ruh");
	if (rank >= 1) return LangPick14(L"上位精霊", L"Higher Spirit", L"Esprit majeur", L"Spirito maggiore", L"Espíritu mayor", L"상위 정령", L"上位精灵", L"روح أعلى", L"Высший дух", L"Hoher Geist", L"Espírito maior", L"Hogere geest", L"Wyższy duch", L"Yüksek ruh");
	return SummonWord();
}

int SummonMesh(int index)
{
	if (index < 0 || index >= SUMMON_N) return -1;
	const wchar_t* w = kSum[index].mesh;
	for (int i = 0; i < kImportN; i++) if (wcscmp(kImportName[i], w) == 0) return i;
	return -1;
}

int SummonHpCost(const Unit& u, int skill)
{
	if (!IsSummonId(skill)) return 0;
	int pct = SummonHpPctOf(kSum[skill - SUMMON_BASE]);
	int mx = u.ch.stat[S_HP];
	if (mx < 1) mx = 1;
	int c = mx * pct / 100;
	if (c < 1) c = 1;
	if (c >= u.hp) c = u.hp - 1;
	if (c < 1) c = 1;
	return c;
}

int SummonCpCost(const Unit& u, int skill)
{
	(void)u;
	if (!IsSummonId(skill)) return 0;
	return SummonCpOf(kSum[skill - SUMMON_BASE]);
}

const SkillDef& SummonSkillRef(int index)
{
	static SkillDef tab[SUMMON_N];
	static int ready = 0;
	if (!ready) {
		for (int i = 0; i < SUMMON_N; i++) {
			const SumRow& r = kSum[i];
			SkillDef& d = tab[i];
			memset(&d, 0, sizeof(d));
			d.kind = SK_MAGIC;
			d.mp = SummonMpOf(r);
			d.power = r.power;
			d.rMin = r.rMin;
			d.rMax = r.rMax;
			d.area = r.area;
			d.element = r.el;
			d.hits = r.hits;
			d.effect = r.effect;
			d.shape = SH_RADIUS;
		}
		ready = 1;
	}
	if (index < 0 || index >= SUMMON_N) index = 0;
	return tab[index];
}
#undef S14
#undef W
#undef W_
