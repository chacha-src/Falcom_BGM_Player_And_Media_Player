// 戦技・技珠・魔石・装飾品の処理。craft_cat.inc の後に取り込む。
// CharData に空きが無いので、CP と装備は uid 引きの別ファイルに積む。

static const SkillDef& CraftSkillRef(int index)
{
	static SkillDef s_def[CRAFT_N];
	static bool s_built = false;
	if (!s_built) {
		for (int i = 0; i < CRAFT_N; i++) {
			const CraftRow& r = kCraftRow[i];
			SkillDef& d = s_def[i];
			d.nameTx = TX_CRAFT; d.descTx = TX_CRAFT;
			d.kind = r.kind; d.mp = 0; d.power = r.power;
			d.rMin = r.rMin; d.rMax = r.rMax; d.area = r.area;
			d.element = r.el; d.hits = r.hits; d.critBonus = r.crit;
			d.effect = r.eff; d.enemyOnly = 0; d.shape = r.shape;
		}
		s_built = true;
	}
	return s_def[Clampi(index, 0, CRAFT_N - 1)];
}

bool IsCraftId(int skill) { return skill >= CRAFT_BASE && skill < CRAFT_BASE + CRAFT_N; }
bool IsCraftSuper(int skill) { return IsCraftId(skill) && (skill - CRAFT_BASE) % CRAFT_PER_LINE >= CRAFT_NORM; }
int CraftCpCost(int index)
{
	if (index < 0 || index >= CRAFT_N) return CP_MAX;
	int k = index % CRAFT_PER_LINE;
	if (k < CRAFT_NORM) return kCraftRow[index].cp;
	return 100;
}
void CraftCostText(int index, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	if (index >= 0 && index < CRAFT_N && index % CRAFT_PER_LINE >= CRAFT_NORM)
		swprintf_s(out, (size_t)cap, L"%s→0", TT(TX_CP));
	else
		swprintf_s(out, (size_t)cap, L"%s%d", TT(TX_CP), CraftCpCost(index));
}
static int CraftIndexOf(int v) { return v >= CRAFT_BASE ? v - CRAFT_BASE : v; }
const wchar_t* CraftName(int skill)
{
	int i = CraftIndexOf(skill);
	if (i < 0 || i >= CRAFT_N) return TT(TX_PT_NONE);
	const wchar_t* const* s = kCraftRow[i].nm;
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}
// 半径0=単体、1=範囲、2以上=広域。
const wchar_t* CraftShapeName(int area, int shape)
{
	if (shape == SH_LINE) return TT(TX_SHAPE_LINE);
	if (shape == SH_FAN) return TT(TX_SHAPE_FAN);
	if (area <= 0) return TT(TX_SHAPE_ONE);
	if (area >= 2) return TT(TX_SHAPE_WIDE);
	return TT(TX_MISC_AREA);
}
static const wchar_t* CraftElWord(int el)
{
	switch (el) {
	case EL_FIRE: return LangPick14(L"火", L"Fire", L"Feu", L"Fuoco", L"Fuego", L"화염", L"火", L"نار", L"Огонь", L"Feuer", L"Fogo", L"Vuur", L"Ogień", L"Ateş");
	case EL_ICE: return LangPick14(L"氷", L"Ice", L"Glace", L"Ghiaccio", L"Hielo", L"냉기", L"冰", L"جليد", L"Лёд", L"Eis", L"Gelo", L"IJs", L"Lód", L"Buz");
	case EL_THUNDER: return LangPick14(L"雷", L"Thunder", L"Tonnerre", L"Tuono", L"Trueno", L"번개", L"雷", L"رعد", L"Гром", L"Donner", L"Trovão", L"Donder", L"Grom", L"Yıldırım");
	case EL_HOLY: return LangPick14(L"聖", L"Holy", L"Sacré", L"Sacro", L"Sagrado", L"성", L"圣", L"مقدس", L"Святость", L"Heilig", L"Sagrado", L"Heilig", L"Świętość", L"Kutsal");
	case EL_DARK: return LangPick14(L"闇", L"Dark", L"Ténèbres", L"Oscurità", L"Oscuridad", L"암", L"暗", L"ظلام", L"Тьма", L"Dunkel", L"Trevas", L"Donker", L"Mrok", L"Karanlık");
	case EL_WIND: return LangPick14(L"風", L"Wind", L"Vent", L"Vento", L"Viento", L"바람", L"风", L"ريح", L"Ветер", L"Wind", L"Vento", L"Wind", L"Wiatr", L"Rüzgar");
	case EL_EARTH: return LangPick14(L"地", L"Earth", L"Terre", L"Terra", L"Tierra", L"대지", L"地", L"أرض", L"Земля", L"Erde", L"Terra", L"Aarde", L"Ziemia", L"Toprak");
	case EL_POISON: return LangPick14(L"毒", L"Poison", L"Poison", L"Veleno", L"Veneno", L"독", L"毒", L"سم", L"Яд", L"Gift", L"Veneno", L"Gif", L"Trucizna", L"Zehir");
	default: return NULL;
	}
}
const wchar_t* CraftDesc(int skill)
{
	static wchar_t buf[768];
	int i = CraftIndexOf(skill);
	if (i < 0 || i >= CRAFT_N) return TT(TX_CRAFT);
	const CraftRow& r = kCraftRow[i];
	const wchar_t* el = CraftElWord(r.el);
	wchar_t act[220];
	act[0] = 0;
	if (r.eff == EF_RAISE) {
		wcscpy_s(act, LangPick14(
			L"倒れた味方を起こし、HPを戻す。", L"Raises a fallen ally and restores HP.",
			L"Relève un allié tombé et rend des PV.", L"Rialza un alleato caduto e ripristina i PS.",
			L"Levanta a un aliado caído y restaura PV.", L"쓰러진 아군을 일으키고 HP를 되돌린다.",
			L"扶起倒下的同伴并恢复HP。", L"ينهض بحليف سقط ويعيد صحته.",
			L"Поднимает павшего союзника и восстанавливает ОЗ.", L"Richtet einen gefallenen Verbündeten auf und stellt LP wieder her.",
			L"Ergue um aliado caído e restaura PV.", L"Laat een gevallene opstaan en herstelt LP.",
			L"Podnosi poległego sojusznika i przywraca PŻ.", L"Düşen bir yoldaşı kaldırır ve CP yeniler."));
	} else if (r.kind == SK_HEAL) {
		swprintf_s(act, LangPick14(
			L"HPを回復する。威力+%d。", L"Restores HP. Power +%d.",
			L"Rend des PV. Puissance +%d.", L"Ripristina PS. Potenza +%d.",
			L"Restaura PV. Potencia +%d.", L"HP를 회복한다. 위력 +%d.",
			L"恢复HP。威力+%d。", L"يعيد الصحة. القوة +%d.",
			L"Восстанавливает ОЗ. Сила +%d.", L"Stellt LP wieder her. Stärke +%d.",
			L"Restaura PV. Poder +%d.", L"Herstelt LP. Kracht +%d.",
			L"Przywraca PŻ. Siła +%d.", L"CP yeniler. Güç +%d."), r.power);
	} else if (r.kind == SK_BUFF) {
		const wchar_t* st = LangPick14(L"能力", L"a stat", L"une stat", L"una stat", L"un atributo", L"능력", L"能力", L"قدرة", L"параметр", L"einen Wert", L"um atributo", L"een waarde", L"cechę", L"bir değer");
		if (r.eff == EF_BUFF_ATK) st = LangPick14(L"攻撃", L"attack", L"l'attaque", L"l'attacco", L"el ataque", L"공격", L"攻击", L"الهجوم", L"атаку", L"Angriff", L"o ataque", L"aanval", L"atak", L"saldırı");
		else if (r.eff == EF_BUFF_DEF) st = LangPick14(L"防御", L"defense", L"la défense", L"la difesa", L"la defensa", L"방어", L"防御", L"الدفاع", L"защиту", L"Abwehr", L"a defesa", L"verdediging", L"obronę", L"savunma");
		else if (r.eff == EF_BUFF_AGI) st = LangPick14(L"素早さ", L"agility", L"l'agilité", L"l'agilità", L"la agilidad", L"민첩", L"敏捷", L"الرشاقة", L"ловкость", L"Beweglichkeit", L"a agilidade", L"behendigheid", L"zręczność", L"çeviklik");
		if (r.area > 0)
			swprintf_s(act, LangPick14(
				L"周囲%dの%sを高める。", L"Radius %d, raises %s.",
				L"Rayon %d, augmente %s.", L"Raggio %d, aumenta %s.",
				L"Radio %d, sube %s.", L"반경 %d의 %s를 높인다.",
				L"周围%d的%s提高。", L"نصف قطر %d، يرفع %s.",
				L"Радиус %d, повышает %s.", L"Radius %d, erhöht %s.",
				L"Raio %d, aumenta %s.", L"Straal %d, verhoogt %s.",
				L"Promień %d, podnosi %s.", L"Yarıçap %d, %s yükseltir."), r.area, st);
		else
			swprintf_s(act, LangPick14(
				L"%sを高める。", L"Raises %s.",
				L"Augmente %s.", L"Aumenta %s.",
				L"Sube %s.", L"%s를 높인다.",
				L"提高%s。", L"يرفع %s.",
				L"Повышает %s.", L"Erhöht %s.",
				L"Aumenta %s.", L"Verhoogt %s.",
				L"Podnosi %s.", L"%s yükseltir."), st);
	} else if (r.kind == SK_MAGIC) {
		if (el)
			swprintf_s(act, LangPick14(
				L"%s属性の魔法攻撃。威力+%d。", L"%s magic attack. Power +%d.",
				L"Attaque magique %s. Puissance +%d.", L"Attacco magico di %s. Potenza +%d.",
				L"Ataque mágico de %s. Potencia +%d.", L"%s 속성 마법 공격. 위력 +%d.",
				L"%s属性魔法攻击。威力+%d。", L"هجوم سحري (%s). القوة +%d.",
				L"Магическая атака (%s). Сила +%d.", L"Magischer %s-Angriff. Stärke +%d.",
				L"Ataque mágico de %s. Poder +%d.", L"Magische %s-aanval. Kracht +%d.",
				L"Atak magiczny (%s). Siła +%d.", L"%s büyü saldırısı. Güç +%d."), el, r.power);
		else
			swprintf_s(act, LangPick14(
				L"魔法攻撃。威力+%d。", L"Magic attack. Power +%d.",
				L"Attaque magique. Puissance +%d.", L"Attacco magico. Potenza +%d.",
				L"Ataque mágico. Potencia +%d.", L"마법 공격. 위력 +%d.",
				L"魔法攻击。威力+%d。", L"هجوم سحري. القوة +%d.",
				L"Магическая атака. Сила +%d.", L"Magischer Angriff. Stärke +%d.",
				L"Ataque mágico. Poder +%d.", L"Magische aanval. Kracht +%d.",
				L"Atak magiczny. Siła +%d.", L"Büyü saldırısı. Güç +%d."), r.power);
	} else if (el) {
		swprintf_s(act, LangPick14(
			L"%s属性の物理攻撃。威力%d%%。", L"%s physical attack. Power %d%%.",
			L"Attaque physique %s. Puissance %d%%.", L"Attacco fisico di %s. Potenza %d%%.",
			L"Ataque físico de %s. Potencia %d%%.", L"%s 속성 물리 공격. 위력 %d%%.",
			L"%s属性物理攻击。威力%d%%。", L"هجوم جسدي (%s). القوة %d%%.",
			L"Физическая атака (%s). Сила %d%%.", L"Physischer %s-Angriff. Stärke %d%%.",
			L"Ataque físico de %s. Poder %d%%.", L"Fysieke %s-aanval. Kracht %d%%.",
			L"Atak fizyczny (%s). Siła %d%%.", L"%s fiziksel saldırı. Güç %d%%."), el, r.power);
	} else {
		swprintf_s(act, LangPick14(
			L"物理攻撃。威力%d%%。", L"Physical attack. Power %d%%.",
			L"Attaque physique. Puissance %d%%.", L"Attacco fisico. Potenza %d%%.",
			L"Ataque físico. Potencia %d%%.", L"물리 공격. 위력 %d%%.",
			L"物理攻击。威力%d%%。", L"هجوم جسدي. القوة %d%%.",
			L"Физическая атака. Сила %d%%.", L"Physischer Angriff. Stärke %d%%.",
			L"Ataque físico. Poder %d%%.", L"Fysieke aanval. Kracht %d%%.",
			L"Atak fizyczny. Siła %d%%.", L"Fiziksel saldırı. Güç %d%%."), r.power);
	}
	swprintf_s(buf, LangPick14(
		L"%s。%s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s",
		L"%s。%s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s", L"%s. %s"),
		CraftShapeName(r.area, r.shape), act);
	if ((r.shape == SH_LINE || r.shape == SH_FAN) && r.area > 0) {
		wchar_t bit[64];
		swprintf_s(bit, LangPick14(
			L"長さ%d。", L"Length %d.", L"Longueur %d.", L"Lunghezza %d.", L"Longitud %d.", L"길이 %d.",
			L"长度%d。", L"طول %d.", L"Длина %d.", L"Länge %d.", L"Comprimento %d.", L"Lengte %d.", L"Długość %d.", L"Uzunluk %d."), r.area);
		wcscat_s(buf, L" ");
		wcscat_s(buf, bit);
	} else if (r.area > 0 && r.kind != SK_BUFF) {
		wchar_t bit[64];
		swprintf_s(bit, LangPick14(
			L"半径%d。", L"Radius %d.", L"Rayon %d.", L"Raggio %d.", L"Radio %d.", L"반경 %d.",
			L"半径%d。", L"نصف قطر %d.", L"Радиус %d.", L"Radius %d.", L"Raio %d.", L"Straal %d.", L"Promień %d.", L"Yarıçap %d."), r.area);
		wcscat_s(buf, L" ");
		wcscat_s(buf, bit);
	}
	if (r.hits > 1) {
		wchar_t bit[64];
		swprintf_s(bit, LangPick14(
			L"%d回ヒット。", L"%d hits.", L"%d coups.", L"%d colpi.", L"%d golpes.", L"%d회 공격.",
			L"%d次攻击。", L"%d ضربات.", L"%d удара.", L"%d Treffer.", L"%d golpes.", L"%d treffers.", L"%d uderzenia.", L"%d vuruş."), r.hits);
		wcscat_s(buf, L" ");
		wcscat_s(buf, bit);
	}
	const wchar_t* ex = NULL;
	if (r.kind != SK_BUFF && r.eff != EF_RAISE) {
		switch (r.eff) {
		case EF_DEFDOWN: ex = LangPick14(L"1〜3ターン、防御を下げる。", L"Lowers defense for 1–3 turns.", L"Baisse la défense pour 1–3 tours.", L"Riduce la difesa per 1–3 turni.", L"Baja la defensa durante 1–3 turnos.", L"1〜3턴 동안 방어를 낮춘다.", L"1至3回合降低防御。", L"يخفض الدفاع لدور أو ثلاثة.", L"Снижает защиту на 1–3 хода.", L"Senkt die Abwehr für 1–3 Züge.", L"Reduz a defesa por 1–3 turnos.", L"Verlaagt de verdediging voor 1–3 beurten.", L"Obniża obronę na 1–3 tury.", L"1–3 tur savunmayı düşürür."); break;
		case EF_SLOW: ex = LangPick14(L"1〜3ターン、素早さを下げる。", L"Lowers agility for 1–3 turns.", L"Baisse l'agilité pour 1–3 tours.", L"Riduce l'agilità per 1–3 turni.", L"Baja la agilidad durante 1–3 turnos.", L"1〜3턴 동안 민첩을 낮춘다.", L"1至3回合降低敏捷。", L"يخفض الرشاقة لدور أو ثلاثة.", L"Снижает ловкость на 1–3 хода.", L"Senkt die Beweglichkeit für 1–3 Züge.", L"Reduz a agilidade por 1–3 turnos.", L"Verlaagt de behendigheid voor 1–3 beurten.", L"Obniża zwinność na 1–3 tury.", L"1–3 tur çevikliği düşürür."); break;
		case EF_POISON: ex = LangPick14(L"1〜3ターン毒を与える。毎ターン最大HPの5%。", L"Poisons for 1–3 turns. 5% max HP each turn.", L"Empoisonne pour 1–3 tours. 5 % des PV max par tour.", L"Avvelena per 1–3 turni. 5% dei PS max a turno.", L"Envenena durante 1–3 turnos. 5% de PV máx. por turno.", L"1〜3턴 독을 준다. 매 턴 최대 HP의 5%.", L"中毒1至3回合。每回合失去最大HP的5%。", L"يسمم لدور أو ثلاثة. 5٪ من الصحة القصوى كل دور.", L"Отравляет на 1–3 хода. 5% максимальных ОЗ за ход.", L"Vergiftet für 1–3 Züge. 5 % der max. LP je Zug.", L"Envenena por 1–3 turnos. 5% dos PV máx. por turno.", L"Vergiftigt voor 1–3 beurten. 5% van de max. LP per beurt.", L"Zatruwa na 1–3 tury. 5% maks. PŻ na turę.", L"1–3 tur zehirler. Her tur azami CP'nin %5'i."); break;
		case EF_IGNDEF: ex = LangPick14(L"防御を無視する。", L"Ignores defense.", L"Ignore la défense.", L"Ignora la difesa.", L"Ignora la defensa.", L"방어를 무시한다.", L"无视防御。", L"يتجاهل الدفاع.", L"Игнорирует защиту.", L"Ignoriert die Abwehr.", L"Ignora a defesa.", L"Negeert de verdediging.", L"Ignoruje obronę.", L"Savunmayı yok sayar."); break;
		case EF_DRAIN: ex = LangPick14(L"与えたダメージを吸収する。", L"Drains the damage dealt.", L"Draine les dégâts infligés.", L"Assorbe i danni inflitti.", L"Absorbe el daño infligido.", L"준 피해를 흡수한다.", L"吸收造成的伤害。", L"يمتص الضرر.", L"Поглощает нанесённый урон.", L"Zieht den Schaden ab.", L"Absorve o dano causado.", L"Zuigt de toegebrachte schade op.", L"Pochłania zadane obrażenia.", L"Verilen hasarı emer."); break;
		case EF_CLEANSE: ex = LangPick14(L"毒・混乱・睡眠・スタン・鈍足・防御低下を解く。", L"Clears poison, confuse, sleep, stun, slow, and defense down.", L"Dissipe poison, confusion, sommeil, étourdissement, lenteur et baisse de défense.", L"Scioglie veleno, confusione, sonno, stordimento, lentezza e calo di difesa.", L"Disipa veneno, confusión, sueño, aturdimiento, lentitud y bajada de defensa.", L"독·혼란·수면·기절·둔화·방어 저하를 푼다.", L"解除毒、混乱、睡眠、硬直、迟缓、防御下降。", L"يزيل السم والارتباك والنوم والصعقة والبطء وخفض الدفاع.", L"Снимает яд, смятение, сон, оглушение, замедление и снижение защиты.", L"Nimmt Gift, Wirren, Schlaf, Betäubung, Langsam und Abwehrsenkung.", L"Remove veneno, confusão, sono, atordoamento, lentidão e queda de defesa.", L"Heft gif, verwarring, slaap, verdoving, traagheid en verdedigingsdaling op.", L"Zdejmuje truciznę, chaos, sen, ogłuszenie, spowolnienie i spadek obrony.", L"Zehir, şaşkınlık, uyku, sersem, yavaş ve savunma düşüşünü siler."); break;
		default: break;
		}
	}
	if (ex) { wcscat_s(buf, L" "); wcscat_s(buf, ex); }
	if (r.kind != SK_HEAL && r.kind != SK_BUFF && r.eff != EF_RAISE) {
		int ranged = (r.kind == SK_MAGIC) || r.rMin >= 2 || r.rMax >= 2;
		const wchar_t* cat = ElemWord(r.kind == SK_MAGIC ? EL_MAGIC : EL_PHYS);
		const wchar_t* rw = ElemWord(ranged ? EL_RANGE : EL_MELEE);
		const wchar_t* ew = EffectWord(r.eff);
		wchar_t bit[96];
		if (ew) swprintf_s(bit, L"%s・%s・%s", cat ? cat : L"", rw ? rw : L"", ew);
		else swprintf_s(bit, L"%s・%s", cat ? cat : L"", rw ? rw : L"");
		wcscat_s(buf, L" ");
		wcscat_s(buf, bit);
	}
	if (r.crit >= 10 && r.kind != SK_HEAL && r.kind != SK_BUFF) {
		wchar_t bit[48];
		swprintf_s(bit, LangPick14(
			L"会心+%d。", L"Crit +%d.", L"Critique +%d.", L"Critico +%d.", L"Crítico +%d.", L"치명 +%d.",
			L"会心+%d。", L"حرج +%d.", L"Крит +%d.", L"Krit +%d.", L"Crítico +%d.", L"Kritiek +%d.", L"Kryt +%d.", L"Kritik +%d."), r.crit);
		wcscat_s(buf, L" ");
		wcscat_s(buf, bit);
	}
	if (i % CRAFT_PER_LINE >= CRAFT_NORM) {
		wcscat_s(buf, L" ");
		wcscat_s(buf, LangPick14(
			L"CP100以上のときだけ使え、使うとCPは0になる。",
			L"Usable at CP 100 or more. Using it sets CP to 0.",
			L"Utilisable dès 100 PC. L'usage remet les PC à 0.",
			L"Usabile da 100 PC. L'uso azzera i PC.",
			L"Usable con 100 CP. Al usarla, el CP queda en 0.",
			L"CP 100 이상일 때만 쓸 수 있고, 쓰면 CP는 0이 된다.",
			L"CP达到100才能使用，使用后CP变为0。",
			L"يُستخدم عند 100 CP فأكثر، ويعيد CP إلى 0.",
			L"Доступно при CP 100 и выше. Применение обнуляет CP.",
			L"Nutzbar ab 100 CP. Der Einsatz setzt CP auf 0.",
			L"Usável com 100 CP. O uso zera o CP.",
			L"Bruikbaar vanaf 100 CP. Gebruik zet CP op 0.",
			L"Działa od 100 CP. Użycie zeruje CP.",
			L"100 CP ve üstünde kullanılır, kullanım CP'yi 0 yapar."));
	}
	return buf;
}
int CraftLineOf(int skill)
{
	int i = CraftIndexOf(skill);
	if (i < 0 || i >= CRAFT_N) return -1;
	if (i >= CRAFT_LEGACY) i -= CRAFT_LEGACY;
	return i / CRAFT_PER_LINE;
}
// 奥義の解放数。下位職で 1、転職で段が上がるごとに +1（最大 4）。敵は全解放。
int CraftSuperUnlocked(int cls)
{
	if (cls < 0) return CRAFT_SUPER_N;
	return Clampi(ClassInfo(cls).tier, 1, CRAFT_SUPER_N);
}
int CraftSuperUnlockedBest(const SaveData& s)
{
	int best = 1;
	for (int i = 0; i < MAX_ROSTER; i++)
		if (s.roster[i].used) best = max(best, CraftSuperUnlocked(s.roster[i].cls));
	return best;
}
static bool CraftSuperOpen(int index, int cls)
{
	int k = index % CRAFT_PER_LINE;
	return k < CRAFT_NORM || k - CRAFT_NORM < CraftSuperUnlocked(cls);
}

// ---------------- 保存 ----------------
#pragma pack(push, 4)
struct CraftRecV4 { DWORD uid; short cp; unsigned char bead[2], acc[2], pas[2]; };
struct CraftRecV5 { DWORD uid; short cp; unsigned char bead[4], acc[2], pas[2], pad[2]; };
struct CraftRec { DWORD uid; short cp; unsigned short bead[4]; unsigned char acc[2], pas[2]; };  // bead 0..2=戦技 3=奥義。1 始まり。0 は空
struct CraftStore {
	int stones;
	unsigned char beadN[CRAFT_N];
	unsigned char accN[64];
	unsigned char pasN[64];
	unsigned char goodN[16];
	CraftRec rec[128];
};
#pragma pack(pop)
static_assert(sizeof(CraftRecV4) == 12, "旧戦技記録は 12 バイト");
static_assert(sizeof(CraftRecV5) == 16, "戦技記録 v5 は 16 バイト");
static_assert(sizeof(CraftRec) == 20, "戦技記録は 20 バイト");
static_assert(CRAFT_N == CRAFT_LEGACY * 2, "戦技は旧表の倍。先頭 147 は動かさない");
static_assert(sizeof(((CraftStore*)0)->rec) / sizeof(CraftRec) >= MAX_ROSTER, "戦技記録は名簿以上");

static CraftStore g_craft;
static const DWORD kCraftMagic = 0x54435246u;
void CraftReset()
{
	memset(&g_craft, 0, sizeof(g_craft));
}
static const int kCraftItemMax = 99;
static const int kStoneMax = 999;

static int RemapCraftV1(int old)
{
	if (old < 0 || old >= 84) return -1;
	int line = old / 14;
	int slot = old % 14;
	int ns = slot < 10 ? slot : CRAFT_NORM + (slot - 10);
	return line * CRAFT_PER_LINE + ns;
}
static void CopyRecV4(CraftRec& d, const CraftRecV4& s)
{
	memset(&d, 0, sizeof(d));
	d.uid = s.uid;
	d.cp = s.cp;
	d.bead[0] = s.bead[0];
	d.bead[1] = s.bead[1];
	d.acc[0] = s.acc[0];
	d.acc[1] = s.acc[1];
	d.pas[0] = s.pas[0];
	d.pas[1] = s.pas[1];
}
static void CopyRecV5(CraftRec& d, const CraftRecV5& s)
{
	memset(&d, 0, sizeof(d));
	d.uid = s.uid;
	d.cp = s.cp;
	for (int k = 0; k < 4; k++) d.bead[k] = s.bead[k];
	d.acc[0] = s.acc[0];
	d.acc[1] = s.acc[1];
	d.pas[0] = s.pas[0];
	d.pas[1] = s.pas[1];
}
static int BeadIsSuperSlot(int bead)
{
	return bead >= 1 && bead <= CRAFT_N && (bead - 1) % CRAFT_PER_LINE >= CRAFT_NORM;
}
static void CraftFixBeads()
{
	for (int i = 0; i < 128; i++) {
		CraftRec& r = g_craft.rec[i];
		for (int k = 0; k < 3; k++) {
			if (r.bead[k] > CRAFT_N) { r.bead[k] = 0; continue; }
			if (BeadIsSuperSlot(r.bead[k])) {
				if (!r.bead[3]) r.bead[3] = r.bead[k];
				r.bead[k] = 0;
			}
		}
		if (r.bead[3] > CRAFT_N || (r.bead[3] && !BeadIsSuperSlot(r.bead[3]))) r.bead[3] = 0;
		for (int k = 1; k < CRAFT_SLOT_N; k++) if (r.bead[k])
			for (int j = 0; j < k; j++) if (r.bead[j] == r.bead[k]) r.bead[k] = 0;
	}
}
void CraftLoad()
{
	memset(&g_craft, 0, sizeof(g_craft));
	CString path = DatArc_Path(TacLeafCraft());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("rb")) != 0 || !fp) return;
	DWORD magic = 0; int ver = 0;
	if (fread(&magic, 4, 1, fp) == 1 && fread(&ver, 4, 1, fp) == 1 && magic == kCraftMagic && ver == 6) {
		if (fread(&g_craft, sizeof(g_craft), 1, fp) != 1) memset(&g_craft, 0, sizeof(g_craft));
	} else if (magic == kCraftMagic && ver == 5) {
#pragma pack(push, 4)
		struct Store5 { int stones; unsigned char beadN[147]; unsigned char accN[32]; unsigned char pasN[32]; CraftRecV5 rec[128]; };
#pragma pack(pop)
		Store5 old5;
		memset(&old5, 0, sizeof(old5));
		if (fread(&old5, sizeof(old5), 1, fp) == 1) {
			g_craft.stones = old5.stones;
			memcpy(g_craft.beadN, old5.beadN, sizeof(old5.beadN));
			memcpy(g_craft.accN, old5.accN, sizeof(old5.accN));
			memcpy(g_craft.pasN, old5.pasN, sizeof(old5.pasN));
			for (int i = 0; i < 128; i++) CopyRecV5(g_craft.rec[i], old5.rec[i]);
		}
	} else if (magic == kCraftMagic && ver == 4) {
#pragma pack(push, 4)
		struct Store4 { int stones; unsigned char beadN[147]; unsigned char accN[32]; unsigned char pasN[32]; CraftRecV4 rec[128]; };
#pragma pack(pop)
		Store4 old4;
		memset(&old4, 0, sizeof(old4));
		if (fread(&old4, sizeof(old4), 1, fp) == 1) {
			g_craft.stones = old4.stones;
			memcpy(g_craft.beadN, old4.beadN, sizeof(old4.beadN));
			memcpy(g_craft.accN, old4.accN, sizeof(old4.accN));
			memcpy(g_craft.pasN, old4.pasN, sizeof(old4.pasN));
			for (int i = 0; i < 128; i++) CopyRecV4(g_craft.rec[i], old4.rec[i]);
		}
	} else if (magic == kCraftMagic && ver == 3) {
#pragma pack(push, 4)
		struct Store3 { int stones; unsigned char beadN[147]; unsigned char accN[32]; CraftRecV4 rec[128]; };
#pragma pack(pop)
		Store3 old;
		memset(&old, 0, sizeof(old));
		if (fread(&old, sizeof(old), 1, fp) == 1) {
			g_craft.stones = old.stones;
			memcpy(g_craft.beadN, old.beadN, sizeof(old.beadN));
			memcpy(g_craft.accN, old.accN, sizeof(old.accN));
			for (int i = 0; i < 128; i++) CopyRecV4(g_craft.rec[i], old.rec[i]);
		}
	} else if (magic == kCraftMagic && ver == 2) {
#pragma pack(push, 4)
		struct Store2 { int stones; unsigned char beadN[126]; unsigned char accN[32]; CraftRecV4 rec[128]; };
#pragma pack(pop)
		Store2 old;
		memset(&old, 0, sizeof(old));
		if (fread(&old, sizeof(old), 1, fp) == 1) {
			g_craft.stones = old.stones;
			memcpy(g_craft.beadN, old.beadN, 126);
			memcpy(g_craft.accN, old.accN, sizeof(old.accN));
			for (int i = 0; i < 128; i++) CopyRecV4(g_craft.rec[i], old.rec[i]);
		}
	} else if (magic == kCraftMagic && ver == 1) {
#pragma pack(push, 4)
		struct OldStore { int stones; unsigned char beadN[84]; unsigned char accN[32]; CraftRecV4 rec[128]; };
#pragma pack(pop)
		OldStore old;
		memset(&old, 0, sizeof(old));
		if (fread(&old, sizeof(old), 1, fp) == 1) {
			g_craft.stones = old.stones;
			memcpy(g_craft.accN, old.accN, sizeof(old.accN));
			for (int i = 0; i < 128; i++) CopyRecV4(g_craft.rec[i], old.rec[i]);
			for (int i = 0; i < 84; i++) {
				int n = RemapCraftV1(i);
				if (n >= 0) g_craft.beadN[n] = old.beadN[i];
			}
			for (int i = 0; i < 128; i++) for (int k = 0; k < 2; k++) {
				int b = g_craft.rec[i].bead[k];
				if (b < 1) continue;
				int n = RemapCraftV1(b - 1);
				g_craft.rec[i].bead[k] = (n >= 0) ? (unsigned short)(n + 1) : 0;
			}
		}
	}
	fclose(fp);
	g_craft.stones = Clampi(g_craft.stones, 0, kStoneMax);
	for (int i = 0; i < 128; i++) {
		CraftRec& r = g_craft.rec[i];
		r.cp = (short)Clampi(r.cp, 0, CP_MAX);
		for (int k = 0; k < CRAFT_SLOT_N; k++) if (r.bead[k] > CRAFT_N) r.bead[k] = 0;
		for (int k = 0; k < 2; k++) {
			if (r.acc[k] > kAccN) r.acc[k] = 0;
			if (r.pas[k] > kPasN) r.pas[k] = 0;
		}
	}
	CraftFixBeads();
}
void CraftSave()
{
	CString path = DatArc_Path(TacLeafCraft());
	FILE* fp = NULL;
	if (_tfopen_s(&fp, path, _T("wb")) != 0 || !fp) return;
	int ver = 6;
	fwrite(&kCraftMagic, 4, 1, fp);
	fwrite(&ver, 4, 1, fp);
	fwrite(&g_craft, sizeof(g_craft), 1, fp);
	fclose(fp);
	DatArc_Commit(TacLeafCraft());
}
// 名簿から消えた仲間の記録を外す。装備していた技珠・装飾は所持数に戻る。
void CraftPrune(const SaveData& s)
{
	for (int i = 0; i < 128; i++) {
		CraftRec& r = g_craft.rec[i];
		if (!r.uid) continue;
		bool live = false;
		for (int k = 0; k < MAX_ROSTER && !live; k++) live = s.roster[k].used && s.roster[k].uid == r.uid;
		if (!live) memset(&r, 0, sizeof(r));
	}
}

static CraftRec* FindRec(DWORD uid, bool create)
{
	if (!uid) return NULL;
	for (int i = 0; i < 128; i++) if (g_craft.rec[i].uid == uid) return &g_craft.rec[i];
	if (!create) return NULL;
	for (int i = 0; i < 128; i++) {
		if (g_craft.rec[i].uid) continue;
		memset(&g_craft.rec[i], 0, sizeof(CraftRec));
		g_craft.rec[i].uid = uid;
		return &g_craft.rec[i];
	}
	return NULL;
}

// ---------------- 戦闘への受け渡し ----------------
void CraftBindUnit(Unit& u)
{
	u.cp = 0;
	for (int k = 0; k < CRAFT_SLOT_N; k++) u.craftEq[k] = 0;
	u.accEq[0] = u.accEq[1] = 0;
	u.pasEq[0] = u.pasEq[1] = 0;
	const CraftRec* r = FindRec(u.ch.uid, false);
	if (!r) return;
	u.cp = Clampi(r->cp, 0, CP_MAX);
	for (int k = 0; k < CRAFT_SLOT_N; k++) {
		int b = r->bead[k];
		if (b >= 1 && b <= CRAFT_N) u.craftEq[k] = CRAFT_BASE + b - 1;
	}
	for (int k = 0; k < 2; k++) {
		int a = r->acc[k];
		if (a >= 1 && a <= kAccN) u.accEq[k] = a;
		int p = r->pas[k];
		if (p >= 1 && p <= kPasN) u.pasEq[k] = p;
	}
}
void CraftSaveUnit(const Unit& u)
{
	CraftRec* r = FindRec(u.ch.uid, true);
	if (r) r->cp = (short)Clampi(u.cp, 0, CP_MAX);
}

// ---------------- 装備 ----------------
// 他の枠（自分の別枠を含む）で使っている数。skipRec/skipSlot の枠は数えない。
static int BeadInUse(int index, const CraftRec* skipRec, int skipSlot)
{
	int n = 0;
	for (int i = 0; i < 128; i++) {
		const CraftRec& r = g_craft.rec[i];
		if (!r.uid) continue;
		for (int k = 0; k < CRAFT_SLOT_N; k++) {
			if (&r == skipRec && k == skipSlot) continue;
			if (r.bead[k] == index + 1) n++;
		}
	}
	return n;
}
static int AccInUse(int index, const CraftRec* skipRec, int skipSlot)
{
	int n = 0;
	for (int i = 0; i < 128; i++) {
		const CraftRec& r = g_craft.rec[i];
		if (!r.uid) continue;
		for (int k = 0; k < 2; k++) {
			if (&r == skipRec && k == skipSlot) continue;
			if (r.acc[k] == index + 1) n++;
		}
	}
	return n;
}

// 職の縛りは無い。所持数と奥義の解放段だけを見る。同じ戦技は一つだけ。奥義は4枠目だけ。
static int BeadFitsSlot(int slot, int bead)
{
	int sup = BeadIsSuperSlot(bead);
	return bead <= 0 || (slot == 3 ? sup : (slot >= 0 && slot < 3 && !sup));
}
void EquipNextBead(CharData& c, int slot)
{
	if (slot < 0 || slot >= CRAFT_SLOT_N) return;
	CraftRec* r = FindRec(c.uid, true);
	if (!r) return;
	int cur = r->bead[slot];
	int next = 0;
	for (int v = cur + 1; v <= CRAFT_N; v++) {
		int idx = v - 1;
		if (!BeadFitsSlot(slot, v)) continue;
		bool dup = false;
		for (int k = 0; k < CRAFT_SLOT_N; k++) if (k != slot && r->bead[k] == v) dup = true;
		if (dup) continue;
		if (g_craft.beadN[idx] <= BeadInUse(idx, r, slot)) continue;
		next = v;
		break;
	}
	r->bead[slot] = (unsigned short)next;
}
void EquipNextAcc(CharData& c, int slot)
{
	if (slot < 0 || slot > 1) return;
	CraftRec* r = FindRec(c.uid, true);
	if (!r) return;
	int cur = r->acc[slot];
	int next = 0;
	for (int v = cur + 1; v <= kAccN; v++) {
		if (g_craft.accN[v - 1] <= AccInUse(v - 1, r, slot)) continue;
		next = v;
		break;
	}
	r->acc[slot] = (unsigned char)next;
}
const wchar_t* EquippedBeadName(const CharData& c, int slot)
{
	const CraftRec* r = FindRec(c.uid, false);
	if (!r || slot < 0 || slot >= CRAFT_SLOT_N || !r->bead[slot]) return TT(TX_PT_NONE);
	return CraftName(r->bead[slot] - 1);
}
const wchar_t* EquippedAccName(const CharData& c, int slot)
{
	const CraftRec* r = FindRec(c.uid, false);
	if (!r || slot < 0 || slot > 1 || !r->acc[slot]) return TT(TX_PT_NONE);
	return AccName(r->acc[slot]);
}
int EquippedBeadId(const CharData& c, int slot)
{
	const CraftRec* r = FindRec(c.uid, false);
	if (!r || slot < 0 || slot >= CRAFT_SLOT_N) return 0;
	return r->bead[slot];
}
int EquippedAccId(const CharData& c, int slot)
{
	const CraftRec* r = FindRec(c.uid, false);
	if (!r || slot < 0 || slot > 1) return 0;
	return r->acc[slot];
}
// この枠に付けられる残り数。今この枠に付いている分は使用中に数えない。
int AccSpare(const CharData& c, int slot, int index)
{
	if (slot < 0 || slot > 1 || index < 0 || index >= kAccN) return 0;
	const CraftRec* r = FindRec(c.uid, false);
	return max(0, (int)g_craft.accN[index] - AccInUse(index, r, slot));
}
int BeadSpare(const CharData& c, int slot, int index)
{
	if (slot < 0 || slot >= CRAFT_SLOT_N || index < 0 || index >= CRAFT_N) return 0;
	if (!BeadFitsSlot(slot, index + 1)) return 0;
	const CraftRec* r = FindRec(c.uid, false);
	if (r) for (int k = 0; k < CRAFT_SLOT_N; k++) if (k != slot && r->bead[k] == (unsigned short)(index + 1)) return 0;
	return max(0, (int)g_craft.beadN[index] - BeadInUse(index, r, slot));
}
bool EquipAccId(CharData& c, int slot, int id)
{
	if (slot < 0 || slot > 1) return false;
	CraftRec* r = FindRec(c.uid, true);
	if (!r) return false;
	if (id <= 0) { r->acc[slot] = 0; return true; }
	if (id > kAccN || (r->acc[slot] != id && AccSpare(c, slot, id - 1) < 1)) return false;
	r->acc[slot] = (unsigned char)id;
	return true;
}
bool EquipBeadId(CharData& c, int slot, int bead)
{
	if (slot < 0 || slot >= CRAFT_SLOT_N) return false;
	CraftRec* r = FindRec(c.uid, true);
	if (!r) return false;
	if (bead <= 0) { r->bead[slot] = 0; return true; }
	if (bead > CRAFT_N || !BeadFitsSlot(slot, bead) || (r->bead[slot] != bead && BeadSpare(c, slot, bead - 1) < 1)) return false;
	r->bead[slot] = (unsigned short)bead;
	return true;
}

// ---------------- 魔石・技珠 ----------------
int CraftGoodGet(int i)
{
	if (i < 0 || i >= (int)sizeof(g_craft.goodN)) return 0;
	return g_craft.goodN[i];
}
void CraftGoodAdd(int i, int d)
{
	if (i < 0 || i >= (int)sizeof(g_craft.goodN)) return;
	int c = (int)g_craft.goodN[i] + d;
	if (c < 0) c = 0;
	if (c > 99) c = 99;
	g_craft.goodN[i] = (unsigned char)c;
}
int StoneCount() { return g_craft.stones; }
void StoneAdd(int d) { g_craft.stones = Clampi(g_craft.stones + d, 0, kStoneMax); }
bool BeadHave(int index) { return index >= 0 && index < CRAFT_N && g_craft.beadN[index] > 0; }
int BeadCount(int index) { return (index < 0 || index >= CRAFT_N) ? 0 : g_craft.beadN[index]; }
// 通常は CP で 1〜4。奥義は解放順に 4 から増える。0 は通常。
int CraftStoneCost(int index)
{
	if (index < 0 || index >= CRAFT_N) return 0;
	int k = index % CRAFT_PER_LINE;
	if (k >= CRAFT_NORM) return 4 + (k - CRAFT_NORM);
	int cp = kCraftRow[index].cp;
	if (cp <= 22) return 1;
	if (cp <= 28) return 2;
	if (cp <= 35) return 3;
	return 4;
}
// 奥義の必要段階。通常は 0。一次職は 1 なので最初の奥義だけ買える。
int CraftSuperNeed(int index)
{
	if (index < 0 || index >= CRAFT_N) return 99;
	int folded = index;
	if (folded >= CRAFT_LEGACY) folded -= CRAFT_LEGACY;
	if (folded / CRAFT_PER_LINE >= 6) return 0;
	int k = folded % CRAFT_PER_LINE;
	return k < CRAFT_NORM ? 0 : k - CRAFT_NORM + 1;
}
bool BeadBuy(const SaveData& s, int index)
{
	if (index < 0 || index >= CRAFT_N) return false;
	int need = CraftSuperNeed(index);
	if (need > 0 && CraftSuperUnlockedBest(s) < need) return false;
	int cost = CraftStoneCost(index);
	if (g_craft.stones < cost || g_craft.beadN[index] >= kCraftItemMax) return false;
	g_craft.stones -= cost;
	g_craft.beadN[index]++;
	return true;
}
bool BeadSell(int index)
{
	if (index < 0 || index >= CRAFT_N || g_craft.beadN[index] < 1) return false;
	if (g_craft.beadN[index] <= BeadInUse(index, NULL, -1)) {
		for (int i = 0; i < 128; i++) {
			CraftRec& r = g_craft.rec[i];
			if (!r.uid) continue;
			for (int k = 0; k < CRAFT_SLOT_N; k++) if (r.bead[k] == (unsigned short)(index + 1)) { r.bead[k] = 0; i = 128; break; }
		}
	}
	if (g_craft.beadN[index] <= BeadInUse(index, NULL, -1)) return false;
	g_craft.beadN[index]--;
	StoneAdd(max(1, CraftStoneCost(index) / 2));
	return true;
}

// ---------------- 装飾品 ----------------
int AccCount() { return kAccN; }
const wchar_t* AccName(int id)
{
	if (id < 1 || id > kAccN) return TT(TX_PT_NONE);
	const wchar_t* const* s = kAccRow[id - 1].nm;
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}
int AccPrice(int index) { return (index < 0 || index >= kAccN) ? 0 : kAccRow[index].price; }
int AccStat(int id, int stat)
{
	if (id < 1 || id > kAccN) return 0;
	int v = 0;
	for (int k = 0; k < 4; k++) if (kAccRow[id - 1].s[k][1] && kAccRow[id - 1].s[k][0] == stat) v += kAccRow[id - 1].s[k][1];
	return v;
}
bool AccBuy(SaveData& s, int index)
{
	if (index < 0 || index >= kAccN) return false;
	if (s.gold < kAccRow[index].price || g_craft.accN[index] >= kCraftItemMax) return false;
	s.gold -= kAccRow[index].price;
	g_craft.accN[index]++;
	return true;
}

// ---------------- パッシブ ----------------
int PasCount() { return kPasN; }
const wchar_t* PasName(int id)
{
	if (id < 1 || id > kPasN) return TT(TX_PT_NONE);
	const wchar_t* const* s = kPasRow[id - 1].nm;
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}
const wchar_t* PasDesc(int id)
{
	if (id < 1 || id > kPasN) return L"";
	const wchar_t* const* s = kPasRow[id - 1].ds;
	return LangPick14(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13]);
}
int PasPrice(int index) { return (index < 0 || index >= kPasN) ? 0 : kPasRow[index].price; }
int PasHave(int index) { return (index < 0 || index >= kPasN) ? 0 : g_craft.pasN[index]; }
static int PasInUse(int index, const CraftRec* skipRec, int skipSlot)
{
	int n = 0;
	for (int i = 0; i < 128; i++) {
		const CraftRec& r = g_craft.rec[i];
		if (!r.uid) continue;
		for (int k = 0; k < 2; k++) {
			if (&r == skipRec && k == skipSlot) continue;
			if (r.pas[k] == index + 1) n++;
		}
	}
	return n;
}
bool PasBuy(SaveData& s, int index)
{
	if (index < 0 || index >= kPasN) return false;
	if (s.gold < kPasRow[index].price || g_craft.pasN[index] >= kCraftItemMax) return false;
	s.gold -= kPasRow[index].price;
	g_craft.pasN[index]++;
	return true;
}
bool PasSell(int index)
{
	if (index < 0 || index >= kPasN || g_craft.pasN[index] < 1) return false;
	if (g_craft.pasN[index] <= PasInUse(index, NULL, -1)) return false;
	g_craft.pasN[index]--;
	return true;
}
int EquippedPasId(const CharData& c, int slot)
{
	const CraftRec* r = FindRec(c.uid, false);
	if (!r || slot < 0 || slot > 1) return 0;
	return r->pas[slot];
}
const wchar_t* EquippedPasName(const CharData& c, int slot)
{
	int id = EquippedPasId(c, slot);
	return id > 0 ? PasName(id) : TT(TX_PT_NONE);
}
int PasSpare(const CharData& c, int slot, int index)
{
	if (slot < 0 || slot > 1 || index < 0 || index >= kPasN) return 0;
	const CraftRec* r = FindRec(c.uid, false);
	if (r && r->pas[slot ^ 1] == (unsigned char)(index + 1)) return 0;
	return max(0, (int)g_craft.pasN[index] - PasInUse(index, r, slot));
}
bool EquipPasId(CharData& c, int slot, int id)
{
	if (slot < 0 || slot > 1) return false;
	CraftRec* r = FindRec(c.uid, true);
	if (!r) return false;
	if (id <= 0) { r->pas[slot] = 0; return true; }
	if (r->pas[slot ^ 1] == (unsigned char)id) return false;
	if (id > kPasN || (r->pas[slot] != id && PasSpare(c, slot, id - 1) < 1)) return false;
	r->pas[slot] = (unsigned char)id;
	return true;
}
bool UnitHasPas(const Unit& u, int fx)
{
	if (fx <= PAS_NONE) return false;
	bool ail = fx == PAS_NOPOISON || fx == PAS_NOCONFUSE || fx == PAS_NOSLEEP || fx == PAS_NOSTUN || fx == PAS_NOSLOW || fx == PAS_NODOWN;
	for (int k = 0; k < 2; k++) {
		int id = u.pasEq[k];
		if (id < 1 || id > kPasN) continue;
		int row = kPasRow[id - 1].fx;
		if (row == fx) return true;
		if (ail && row == PAS_ALLAIL) return true;
	}
	return false;
}
bool UnitResistsEffect(const Unit& u, int effect)
{
	switch (effect) {
	case EF_POISON: return UnitHasPas(u, PAS_NOPOISON);
	case EF_CONFUSE: return UnitHasPas(u, PAS_NOCONFUSE);
	case EF_SLEEP: return UnitHasPas(u, PAS_NOSLEEP);
	case EF_STUN: return UnitHasPas(u, PAS_NOSTUN);
	case EF_SLOW: return UnitHasPas(u, PAS_NOSLOW);
	case EF_DEFDOWN: return UnitHasPas(u, PAS_NODOWN);
	default: return false;
	}
}

// ---------------- 敵ドロップ ----------------
// 魔石: 雑魚 12%/1 個、ボス 65%/2 個。装飾: 雑魚 3%（安価 12 種）、ボス 30%（全種）。
bool DropCraftLoot(int monster, int boss, Rng& r, wchar_t* name, int nameN)
{
	(void)monster;
	if (name && nameN > 0) name[0] = 0;
	bool got = false;
	if (r.Chance(boss ? 65 : 12)) {
		int n = boss ? 2 : 1;
		StoneAdd(n);
		if (name && nameN > 0) swprintf_s(name, nameN, L"%s x%d", TT(TX_STONE), n);
		got = true;
	}
	if (r.Chance(boss ? 30 : 3)) {
		int ix = boss ? r.Range(0, kAccN - 1) : r.Range(0, min(12, kAccN) - 1);
		if (g_craft.accN[ix] < kCraftItemMax) {
			g_craft.accN[ix]++;
			if (name && nameN > 0) {
				size_t len = wcslen(name);
				swprintf_s(name + len, nameN - len, L"%s%s", len ? L"  " : L"", AccName(ix + 1));
			}
			got = true;
		}
	}
	return got;
}

// ---------------- 高低差 ----------------
int BattleHeightSteps(const Battle& b, int ax, int az, int dx, int dz)
{
	float dh = (b.map.StandY(ax, az) - b.map.StandY(dx, dz)) / kHStep;
	return Clampi((int)lroundf(dh), -4, 4);
}
