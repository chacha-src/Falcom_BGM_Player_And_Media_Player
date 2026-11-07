// 進化先の説明。やり直しが利かないので、物理か魔法か、伸び、射程を3行で書く。
static const wchar_t* EvoKeep()
{
	return LangPick14(
		L"今より低い能力だけこの職の値まで上がり、今の数値は下がらない。覚えた技は残る。",
		L"Only stats below this class rise to its base. Nothing you have falls. Skills you know stay.",
		L"Seules les valeurs plus basses montent à cette classe. Rien ne baisse. Les techniques apprises restent.",
		L"Salono le statistiche più basse fino a questa classe. Nulla scende. Le tecniche imparate restano.",
		L"Solo suben los valores que estén por debajo de esta clase. Nada baja. Las técnicas aprendidas se quedan.",
		L"이 직업보다 낮은 능력만 올라가고, 지금 수치는 내려가지 않는다. 배운 기술은 남는다.",
		L"只有低于此职的能力会升到此职的值，现有数值不会下降。已学技能保留。",
		L"ترتفع القدرات الأدنى من هذه الوظيفة فقط. لا ينقص ما لديك. المهارات التي تعلمتها تبقى.",
		L"Растут только параметры ниже этой профессии. Уже имеющееся не падает. Выученные умения остаются.",
		L"Nur Werte unter dieser Klasse steigen auf ihren Grund. Nichts sinkt. Gelernte Fertigkeiten bleiben.",
		L"Só sobem valores abaixo desta classe. Nada desce. Técnicas já aprendidas ficam.",
		L"Alleen waarden onder deze klasse stijgen naar haar basis. Niets daalt. Geleerde vaardigheden blijven.",
		L"Rosną tylko wartości niższe niż ta profesja. Nic nie spada. Znane umiejętności zostają.",
		L"Yalnızca bu sınıfın altındaki değerler onun tabanına çıkar. Hiçbiri düşmez. Öğrenilen beceriler kalır.");
}
static void EvoFact(wchar_t* dst, int cap, const ClassDef& d, int job, int lead)
{
	wchar_t skill[240];
	skill[0] = 0;
	int sk = -1;
	if (lead) for (int i = 0; i < LEARN_N; i++) if (d.learn[i].skill >= 0) { sk = d.learn[i].skill; break; }
	if (sk >= 0) {
		const wchar_t* fmt = job
			? LangPick14(
				L"この段で「%s」を覚える。",
				L"This rank learns %s. ",
				L"Ce rang apprend %s. ",
				L"Questo grado impara %s. ",
				L"Este rango aprende %s. ",
				L"이 단계에서 「%s」을 배운다. ",
				L"此阶学会「%s」。",
				L"تتعلم في هذه الرتبة %s. ",
				L"На этом ранге изучается %s. ",
				L"Dieser Rang erlernt %s. ",
				L"Este grau aprende %s. ",
				L"Deze rang leert %s. ",
				L"Na tym stopniu uczysz się: %s. ",
				L"Bu kademe %s öğrenir. ")
			: LangPick14(
				L"この職では「%s」などを覚える。",
				L"This class learns %s, among others. ",
				L"Cette classe apprend %s, entre autres. ",
				L"Questa classe impara %s, tra le altre. ",
				L"Esta clase aprende %s, entre otras. ",
				L"이 직업은 「%s」 등을 배운다. ",
				L"此职会学会「%s」等。",
				L"تتعلم هذه الوظيفة %s وغيرها. ",
				L"Эта профессия учит %s и другое. ",
				L"Diese Klasse erlernt unter anderem %s. ",
				L"Esta classe aprende %s, entre outras. ",
				L"Deze klasse leert onder meer %s. ",
				L"Ta profesja uczy między innymi: %s. ",
				L"Bu sınıf %s ve başkalarını öğrenir. ");
		_snwprintf_s(skill, _TRUNCATE, fmt, SkillLabel(sk));
	}
	const wchar_t* hit = d.magAttack
		? LangPick14(
			L"通常攻撃そのものも魔法だ。",
			L"The basic attack itself is magic. ",
			L"L'attaque de base elle-même est magique. ",
			L"L'attacco base è magia. ",
			L"El ataque básico también es magia. ",
			L"일반 공격 자체도 마법이다. ",
			L"普通攻击本身也是魔法。",
			L"الهجوم العادي نفسه سحر. ",
			L"Обычная атака тоже магия. ",
			L"Der normale Angriff selbst ist Magie. ",
			L"O ataque básico também é magia. ",
			L"De gewone aanval zelf is magie. ",
			L"Zwykły atak też jest magią. ",
			L"Normal saldırının kendisi de büyüdür. ")
		: LangPick14(
			L"通常攻撃は武器の物理だ。",
			L"The basic attack is a physical weapon strike. ",
			L"L'attaque de base est un coup d'arme physique. ",
			L"L'attacco base è un colpo fisico. ",
			L"El ataque básico es un golpe físico. ",
			L"일반 공격은 무기의 물리다. ",
			L"普通攻击是武器的物理。",
			L"الهجوم العادي ضربة سلاح جسدية. ",
			L"Обычная атака — физический удар оружием. ",
			L"Der normale Angriff ist ein physischer Waffenschlag. ",
			L"O ataque básico é um golpe físico. ",
			L"De gewone aanval is een fysieke wapenslag. ",
			L"Zwykły atak to fizyczne uderzenie bronią. ",
			L"Normal saldırı silahla fiziksel vuruştur. ");
	wchar_t rng[240];
	if (d.rMin >= 2) {
		const wchar_t* fmt = LangPick14(
			L"通常攻撃は%dマスから%dマスで、隣の敵には届かない。",
			L"A basic attack reaches %d to %d tiles and cannot hit an adjacent enemy. ",
			L"L'attaque de base porte de %d à %d cases et n'atteint pas l'ennemi adjacent. ",
			L"L'attacco base va da %d a %d caselle e non colpisce chi è adiacente. ",
			L"El ataque básico alcanza de %d a %d casillas y no llega al enemigo adyacente. ",
			L"일반 공격은 %d칸에서 %d칸이며, 인접한 적에게는 닿지 않는다. ",
			L"普通攻击为%d到%d格，打不到相邻的敌人。",
			L"الهجوم العادي من %d إلى %d مربعات ولا يصيب العدو المجاور. ",
			L"Обычная атака бьёт с %d по %d клеток и не достаёт соседнего врага. ",
			L"Der normale Angriff reicht von %d bis %d Felder und trifft keinen angrenzenden Feind. ",
			L"O ataque básico vai de %d a %d casas e não alcança o inimigo adjacente. ",
			L"De gewone aanval reikt van %d tot %d vakken en raakt geen aangrenzende vijand. ",
			L"Zwykły atak sięga od %d do %d pól i nie trafia sąsiedniego wroga. ",
			L"Normal saldırı %d ile %d kare arasındadır ve bitişik düşmana ulaşmaz. ");
		_snwprintf_s(rng, _TRUNCATE, fmt, d.rMin, d.rMax);
	} else if (d.rMax <= 1) {
		wcscpy_s(rng, LangPick14(
			L"通常攻撃は隣の敵だけ。",
			L"A basic attack hits only an adjacent enemy. ",
			L"L'attaque de base ne touche que l'ennemi adjacent. ",
			L"L'attacco base colpisce solo chi è adiacente. ",
			L"El ataque básico solo golpea al enemigo adyacente. ",
			L"일반 공격은 인접한 적만. ",
			L"普通攻击只能打相邻的敌人。",
			L"الهجوم العادي يصيب العدو المجاور فقط. ",
			L"Обычная атака бьёт только соседнего врага. ",
			L"Der normale Angriff trifft nur einen angrenzenden Feind. ",
			L"O ataque básico só acerta o inimigo adjacente. ",
			L"De gewone aanval raakt alleen een aangrenzende vijand. ",
			L"Zwykły atak trafia tylko sąsiedniego wroga. ",
			L"Normal saldırı yalnız bitişik düşmanı vurur. "));
	} else {
		const wchar_t* fmt = LangPick14(
			L"通常攻撃は%dマスから%dマスで、隣にも届く。",
			L"A basic attack reaches %d to %d tiles, including adjacent. ",
			L"L'attaque de base porte de %d à %d cases, y compris l'adjacent. ",
			L"L'attacco base va da %d a %d caselle, anche adiacenti. ",
			L"El ataque básico alcanza de %d a %d casillas, también al adyacente. ",
			L"일반 공격은 %d칸에서 %d칸이며, 인접에도 닿는다. ",
			L"普通攻击为%d到%d格，相邻也能打到。",
			L"الهجوم العادي من %d إلى %d مربعات، بما فيها المجاور. ",
			L"Обычная атака бьёт с %d по %d клеток, включая соседнюю. ",
			L"Der normale Angriff reicht von %d bis %d Felder, auch angrenzend. ",
			L"O ataque básico vai de %d a %d casas, inclusive o adjacente. ",
			L"De gewone aanval reikt van %d tot %d vakken, ook aangrenzend. ",
			L"Zwykły atak sięga od %d do %d pól, także sąsiada. ",
			L"Normal saldırı %d ile %d kare arasındadır, bitişik de dahil. ");
		_snwprintf_s(rng, _TRUNCATE, fmt, d.rMin, d.rMax);
	}
	const wchar_t* mv = d.moveType == MV_FLY
		? LangPick14(
			L"飛行し、山以外の地形を越える。",
			L"You fly and cross terrain other than mountains. ",
			L"Vous volez et franchissez le terrain sauf les montagnes. ",
			L"Voli e attraversi il terreno, tranne le montagne. ",
			L"Vuelas y cruzas el terreno, salvo las montañas. ",
			L"비행하며, 산 이외의 지형을 넘는다. ",
			L"飞行，可越过山地以外的地形。",
			L"تطير وتتجاوز التضاريس عدا الجبال. ",
			L"Вы летаете и пересекаете местность, кроме гор. ",
			L"Ihr fliegt und überquert Gelände außer Bergen. ",
			L"Você voa e atravessa o terreno, exceto montanhas. ",
			L"Je vliegt en kruist terrein behalve bergen. ",
			L"Latasz i pokonujesz teren poza górami. ",
			L"Uçarsın ve dağlar dışında araziyi geçersin. ")
		: LangPick14(
			L"飛行はしない。",
			L"You do not fly. ",
			L"Vous ne volez pas. ",
			L"Non voli. ",
			L"No vuelas. ",
			L"비행은 하지 않는다. ",
			L"不会飞行。",
			L"لا تطير. ",
			L"Вы не летаете. ",
			L"Ihr fliegt nicht. ",
			L"Você não voa. ",
			L"Je vliegt niet. ",
			L"Nie latasz. ",
			L"Uçmazsın. ");
	_snwprintf_s(dst, cap, _TRUNCATE, L"%s%s%s%s%s", skill, hit, rng, mv, EvoKeep());
}
static void EvoRole(int cls, const wchar_t** a, const wchar_t** b)
{
	*a = *b = NULL;
	switch (cls) {
	case C_PALADIN:
		*a = LangPick14(
			L"物理寄りの聖騎士。剣で隣の敵を叩き、守備とHPで前に立つ。魔法で戦う職ではない。",
			L"A holy knight who fights physically. You strike the foe beside you and hold the front with defense and HP. This is not a magic class.",
			L"Chevalier sacré au combat physique. Vous frappez l'ennemi adjacent et tenez l'avant par la défense et les PV. Ce n'est pas une classe de magie.",
			L"Cavaliere sacro dal combattimento fisico. Colpisci il nemico adiacente e reggi il fronte con difesa e PS. Non è una classe magica.",
			L"Caballero sagrado de combate físico. Golpeas al enemigo adyacente y sostienes el frente con defensa y PV. No es una clase mágica.",
			L"물리 쪽의 성기사. 검으로 인접한 적을 치고, 수비와 HP로 앞에 선다. 마법으로 싸우는 직업이 아니다.",
			L"偏物理的圣骑士。用剑打相邻的敌人，以防御和HP站在前面。不是靠魔法作战的职。",
			L"فارس مقدس يقاتل جسديًا. تضرب العدو المجاور وتثبت المقدمة بالدفاع والصحة. ليست وظيفة سحر.",
			L"Святой рыцарь физического боя. Бьёте соседнего врага и держите фронт защитой и ОЗ. Это не магическая профессия.",
			L"Ein heiliger Ritter im physischen Kampf. Ihr schlagt den angrenzenden Feind und haltet die Front mit Abwehr und LP. Keine Magieklasse.",
			L"Cavaleiro sagrado de luta física. Você golpeia o inimigo adjacente e segura a frente com defesa e PV. Não é classe de magia.",
			L"Een heilige ridder die fysiek vecht. Je slaat de aangrenzende vijand en houdt het front met verdediging en LP. Geen magieklasse.",
			L"Święty rycerz walki fizycznej. Bijesz sąsiedniego wroga i trzymasz front obroną i PŻ. To nie profesja magii.",
			L"Fiziksel savaşan kutsal şövalye. Bitişik düşmanı kılıçla vurur, savunma ve CP ile önde durursun. Büyü sınıfı değildir.");
		*b = LangPick14(
			L"聖なる刃と回復と防御の補助を少し覚える。魔力は主役にならない。以後のレベルアップは守備と力に寄る。",
			L"You pick up a holy blade, some healing, and a guard buff. Magic never becomes the main stat. Later levels still lean on defense and strength.",
			L"Vous apprenez une lame sacrée, un peu de soin et un renfort de garde. La magie ne devient pas la stat principale. Les niveaux suivants penchent vers la défense et la force.",
			L"Impari una lama sacra, un po' di cura e un rinforzo alla guardia. La magia non diventa la statistica principale. I livelli successivi pendono su difesa e forza.",
			L"Aprendes un filo sagrado, algo de curación y un refuerzo de guarda. La magia no pasa a ser el valor principal. Los niveles siguientes se inclinan a defensa y fuerza.",
			L"성스러운 검과 회복, 방어 보조를 조금 배운다. 마력은 주역이 되지 않는다. 이후 레벨업은 수비와 힘에 기운다.",
			L"会一点圣剑、恢复和防御辅助。魔力不会成为主力。之后的升级偏向防御和力量。",
			L"تتعلم نصلًا مقدسًا وقليلًا من الشفاء ودعم الدفاع. السحر لا يصبح القدرة الأساسية. المستويات اللاحقة تميل إلى الدفاع والقوة.",
			L"Немного святого клинка, лечения и защиты. Магия не становится главной. Дальнейшие уровни склоняются к защите и силе.",
			L"Ihr lernt eine heilige Klinge, etwas Heilung und einen Schutzbonus. Magie wird nicht der Hauptwert. Spätere Stufen neigen zu Abwehr und Stärke.",
			L"Você aprende uma lâmina sagrada, um pouco de cura e um reforço de guarda. A magia não vira o atributo principal. Os níveis seguintes pendem para defesa e força.",
			L"Je leert een heilig zwaard, wat heling en een verdedigingsbuff. Magie wordt niet de hoofdwaarde. Latere niveaus hellen naar verdediging en kracht.",
			L"Uczysz się świętego ostrza, trochę leczenia i wsparcia obrony. Magia nie staje się główną cechą. Dalsze poziomy idą w obronę i siłę.",
			L"Kutsal kılıç, biraz iyileştirme ve savunma desteği öğrenirsin. Büyü ana değer olmaz. Sonraki seviyeler savunma ve güce yaslanır.");
		break;
	case C_RUNEKNIGHT:
		*a = LangPick14(
			L"剣と魔法の両方を使う魔剣士。純粋な力職でも、純粋な魔法職でもない。",
			L"A spellblade who uses both sword and magic. Neither a pure strength class nor a pure magic class.",
			L"Une lame-sort qui use de l'épée et de la magie. Ni une classe de force pure, ni une classe de magie pure.",
			L"Uno spadaccino magico che usa spada e magia. Né una classe di sola forza, né una di sola magia.",
			L"Un espadachín mágico que usa espada y magia. Ni clase de fuerza pura ni clase de magia pura.",
			L"검과 마법을 함께 쓰는 마검사. 순수한 힘 직업도, 순수한 마법 직업도 아니다.",
			L"剑与魔法并用的魔剑士。既不是纯力量职，也不是纯魔法职。",
			L"مبارز يستخدم السيف والسحر معًا. ليست وظيفة قوة خالصة ولا وظيفة سحر خالص.",
			L"Мечник, который бьёт и мечом, и магией. Не чистая сила и не чистая магия.",
			L"Ein Zauberklinge-Kämpfer mit Schwert und Magie. Weder reine Stärke noch reine Magie.",
			L"Um espadachim mágico que usa espada e magia. Nem classe de força pura nem de magia pura.",
			L"Een magiezwaardvechter met zwaard en magie. Geen pure kracht en geen pure magie.",
			L"Magiczny szermierz, miecz i magia naraz. Ani czysta siła, ani czysta magia.",
			L"Kılıç ve büyüyü birlikte kullanan büyü kılıççısı. Ne saf güç ne saf büyü sınıfıdır.");
		*b = LangPick14(
			L"通常攻撃は隣の物理だが、炎・雷・氷の魔法は離れた敵にも届く。力と魔力が同じくらい伸び、MPも増える。守備は重騎士ほど厚くない。",
			L"The basic attack is physical and adjacent, but fire, thunder, and ice reach farther foes. Strength and magic grow about equally, and MP rises. Defense is thinner than a heavy knight.",
			L"L'attaque de base est physique et adjacente, mais le feu, la foudre et la glace portent plus loin. Force et magie montent pareil, ainsi que les PM. La défense est moins épaisse qu'un chevalier lourd.",
			L"L'attacco base è fisico e adiacente, ma fuoco, tuono e gelo arrivano più lontano. Forza e magia crescono in pari, e anche i PM. La difesa è meno spessa di un cavaliere pesante.",
			L"El ataque básico es físico y adyacente, pero fuego, trueno y hielo llegan más lejos. Fuerza y magia crecen parejo, y también los PM. La defensa es menos gruesa que la de un caballero pesado.",
			L"일반 공격은 인접한 물리지만, 불·번개·얼음 마법은 먼 적에게도 닿는다. 힘과 마력이 비슷하게 늘고 MP도 늘어난다. 수비는 중기사만큼 두껍지 않다.",
			L"普通攻击是相邻的物理，但火、雷、冰魔法能打到远处。力量和魔力成长相近，MP也会增加。防御不如重骑士厚。",
			L"الهجوم العادي جسدي ومجاور، لكن النار والرعد والجليد تصل أبعد. القوة والسحر ينموان بالتساوي، والطاقة أيضًا. الدفاع أرق من الفارس الثقيل.",
			L"Обычная атака физическая и в упор, но огонь, гром и лёд бьют дальше. Сила и магия растут примерно одинаково, растёт и ОМ. Защита тоньше, чем у тяжёлого рыцаря.",
			L"Der normale Angriff ist physisch und angrenzend, aber Feuer, Blitz und Eis reichen weiter. Stärke und Magie wachsen ähnlich, auch die MP. Die Abwehr ist dünner als bei einem schweren Ritter.",
			L"O ataque básico é físico e adjacente, mas fogo, trovão e gelo alcançam mais longe. Força e magia crescem parelho, e os PM também. A defesa é menos grossa que a de um cavaleiro pesado.",
			L"De gewone aanval is fysiek en aangrenzend, maar vuur, donder en ijs reiken verder. Kracht en magie groeien ongeveer gelijk, en MP stijgt. Verdediging is dunner dan bij een zware ridder.",
			L"Zwykły atak jest fizyczny i sąsiedni, ale ogień, grom i lód sięgają dalej. Siła i magia rosną podobnie, rośnie też PM. Obrona jest cieńsza niż u ciężkiego rycerza.",
			L"Normal saldırı bitişik ve fizikseldir, ama ateş, yıldırım ve buz uzağa da gider. Güç ile büyü benzer artar, MP de yükselir. Savunma ağır şövalye kadar kalın değildir.");
		break;
	case C_BERSERKER:
		*a = LangPick14(
			L"物理の狂戦士。斧で隣の敵を殴り、攻撃力は前衛でも高い。魔法は使わない。",
			L"A physical berserker. You smash the foe beside you with an axe, and your attack is among the highest in the front line. You do not use magic.",
			L"Berserker physique. Vous frappez l'ennemi adjacent à la hache, et l'attaque est parmi les plus hautes de l'avant. Pas de magie.",
			L"Berserker fisico. Colpisci il nemico adiacente con l'ascia, e l'attacco è tra i più alti del fronte. Niente magia.",
			L"Berserker físico. Golpeas al enemigo adyacente con el hacha, y el ataque está entre los más altos del frente. No usas magia.",
			L"물리 광전사. 도끼로 인접한 적을 치고, 공격력은 전열에서도 높다. 마법은 쓰지 않는다.",
			L"物理狂战士。用斧打相邻的敌人，攻击力在前排里也很高。不用魔法。",
			L"محارب جسدي هائج. تضرب العدو المجاور بالفأس، والهجوم من الأعلى في المقدمة. لا تستخدم السحر.",
			L"Физический берсерк. Бьёте соседнего врага топором, атака одна из высших на передовой. Магию не используете.",
			L"Physischer Berserker. Ihr schlagt den angrenzenden Feind mit der Axt, der Angriff ist vorn unter den höchsten. Keine Magie.",
			L"Berserker físico. Você golpeia o inimigo adjacente com o machado, e o ataque está entre os mais altos da frente. Não usa magia.",
			L"Fysieke berserker. Je slaat de aangrenzende vijand met de bijl, en de aanval hoort bij de hoogste vooraan. Geen magie.",
			L"Fizyczny berserker. Bijesz sąsiada toporem, atak jest wśród najwyższych na froncie. Magii nie używasz.",
			L"Fiziksel çılgın savaşçı. Baltayla bitişik düşmanı vurursun, saldırı ön safta da yüksektir. Büyü kullanmazsın.");
		*b = LangPick14(
			L"HPと力が大きく伸びる。守備と魔防は薄く、当てられると痛い。以後のレベルアップも力に寄る。一撃で倒す側。",
			L"HP and strength grow hard. Defense and resistance stay thin, so hits hurt. Later levels keep leaning on strength. You are the one who ends a fight in one blow.",
			L"PV et force montent fort. Défense et résistance restent minces, donc les coups font mal. Les niveaux suivants penchent vers la force. Vous êtes celui qui finit d'un coup.",
			L"PS e forza crescono molto. Difesa e resistenza restano sottili, quindi i colpi fanno male. I livelli successivi pendono sulla forza. Sei tu a chiudere in un colpo.",
			L"PV y fuerza crecen mucho. Defensa y resistencia siguen finas, así que los golpes duelen. Los niveles siguientes se inclinan a la fuerza. Tú eres quien cierra de un golpe.",
			L"HP와 힘이 크게 늘어난다. 수비와 마방은 얇아, 맞으면 아프다. 이후 레벨업도 힘에 기운다. 일격으로 끝내는 쪽.",
			L"HP和力量大幅成长。防御和魔防偏薄，被打会疼。之后的升级也偏向力量。负责一击打倒。",
			L"الصحة والقوة تنموان كثيرًا. الدفاع والمقاومة يبقيان رقيقين، فالضربات تؤلم. المستويات اللاحقة تميل إلى القوة. أنت من ينهي القتال بضربة.",
			L"ОЗ и сила сильно растут. Защита и стойкость тонкие, удары больно проходят. Дальше уровни тоже клонятся к силе. Вы тот, кто кончает одним ударом.",
			L"LP und Stärke wachsen stark. Abwehr und Resistenz bleiben dünn, Treffer tun weh. Spätere Stufen neigen weiter zur Stärke. Ihr beendet den Kampf mit einem Schlag.",
			L"PV e força crescem muito. Defesa e resistência ficam finas, então o golpe dói. Os níveis seguintes pendem para a força. Você é quem fecha num golpe.",
			L"LP en kracht groeien hard. Verdediging en weerstand blijven dun, dus treffers doen pijn. Latere niveaus hellen naar kracht. Jij bent degene die in één slag afmaakt.",
			L"PŻ i siła rosną mocno. Obrona i odporność są cienkie, więc ciosy bolą. Dalsze poziomy też idą w siłę. To ty kończysz jednym uderzeniem.",
			L"CP ve güç çok artar. Savunma ve büyü direnci ince kalır, isabet acıtır. Sonraki seviyeler de güce yaslanır. Dövüşü tek vuruşta bitiren tarafsın.");
		break;
	case C_SNIPER:
		*a = LangPick14(
			L"物理の狙撃手。クロスボウで遠くの敵を射抜く。魔法は使わない。",
			L"A physical sniper. You pierce distant foes with a crossbow. You do not use magic.",
			L"Tireur physique. Vous percez les ennemis lointains à l'arbalète. Pas de magie.",
			L"Cecchino fisico. Trafiggi i nemici lontani con la balestra. Niente magia.",
			L"Francotirador físico. Atraviesas a los enemigos lejanos con la ballesta. No usas magia.",
			L"물리 저격수. 석궁으로 먼 적을 꿰뚫는다. 마법은 쓰지 않는다.",
			L"物理狙击手。用弩射穿远处的敌人。不用魔法。",
			L"قناص جسدي. تخترق الأعداء البعيدين بالقوس. لا تستخدم السحر.",
			L"Физический снайпер. Пробиваете дальних врагов из арбалета. Магию не используете.",
			L"Physischer Scharfschütze. Ihr durchbohrt ferne Feinde mit der Armbrust. Keine Magie.",
			L"Atirador físico. Você perfura inimigos distantes com a besta. Não usa magia.",
			L"Fysieke sluipschutter. Je doorboort verre vijanden met de kruisboog. Geen magie.",
			L"Fizyczny snajper. Przebijasz dalekich wrogów kuszą. Magii nie używasz.",
			L"Fiziksel keskin nişancı. Uzaktaki düşmanı arbaletle delersin. Büyü kullanmazsın.");
		*b = LangPick14(
			L"技量と素早さが伸び、力は中くらい。HPと守備は低く、前に出ると脆い。以後のレベルアップも技と素早さに寄る。",
			L"Dexterity and agility grow; strength stays middling. HP and defense are low, so the front line is fragile. Later levels keep leaning on dexterity and agility.",
			L"Adresse et agilité montent, la force reste moyenne. PV et défense sont bas, donc l'avant est fragile. Les niveaux suivants penchent vers l'adresse et l'agilité.",
			L"Destrezza e agilità crescono, la forza resta media. PS e difesa sono bassi, quindi il fronte è fragile. I livelli successivi pendono su destrezza e agilità.",
			L"Destreza y agilidad crecen, la fuerza se queda a medias. PV y defensa son bajos, así que el frente es frágil. Los niveles siguientes se inclinan a destreza y agilidad.",
			L"기량과 민첩이 늘고, 힘은 중간이다. HP와 수비는 낮아, 앞에 서면 약하다. 이후 레벨업도 기량과 민첩에 기운다.",
			L"技巧和敏捷成长，力量中等。HP和防御偏低，站到前面很脆。之后的升级也偏向技巧和敏捷。",
			L"البراعة والرشاقة تنموان، والقوة تبقى متوسطة. الصحة والدفاع منخفضان، فالمقدمة هشة. المستويات اللاحقة تميل إلى البراعة والرشاقة.",
			L"Сноровка и ловкость растут, сила средняя. ОЗ и защита низкие, на передовой хрупко. Дальше уровни клонятся к сноровке и ловкости.",
			L"Geschick und Beweglichkeit wachsen, die Stärke bleibt mittel. LP und Abwehr sind niedrig, vorn ist es zerbrechlich. Spätere Stufen neigen zu Geschick und Beweglichkeit.",
			L"Destreza e agilidade crescem, a força fica média. PV e defesa são baixos, então a frente é frágil. Os níveis seguintes pendem para destreza e agilidade.",
			L"Vaardigheid en behendigheid groeien, kracht blijft middelmatig. LP en verdediging zijn laag, dus vooraan is het breekbaar. Latere niveaus hellen naar vaardigheid en behendigheid.",
			L"Zręczność i zwinność rosną, siła zostaje średnia. PŻ i obrona są niskie, więc front jest kruchy. Dalsze poziomy idą w zręczność i zwinność.",
			L"Maharet ve çeviklik artar, güç ortada kalır. CP ve savunma düşüktür, öne çıkınca kırılgansın. Sonraki seviyeler maharet ve çevikliğe yaslanır.");
		break;
	case C_ASSASSIN:
		*a = LangPick14(
			L"物理の近接暗殺。短剣で隣を狙い、必殺が出やすい。魔法は使わない。",
			L"A physical melee assassin. You aim at the foe beside you with daggers, and critical hits come easily. You do not use magic.",
			L"Assassin physique au contact. Vous visez l'ennemi adjacent aux dagues, et les coups critiques sortent souvent. Pas de magie.",
			L"Assassino fisico in mischia. Punti il nemico adiacente con i pugnali, e i critici escono spesso. Niente magia.",
			L"Asesino físico en cuerpo a cuerpo. Apuntas al enemigo adyacente con dagas, y los críticos salen fácil. No usas magia.",
			L"물리 근접 암살. 단검으로 인접을 노리고, 필살이 잘 나온다. 마법은 쓰지 않는다.",
			L"物理近战暗杀。用短剑打相邻的敌人，容易出必杀。不用魔法。",
			L"قاتل جسدي من قرب. تصيب العدو المجاور بالخناجر، والضربة القاضية تخرج بسهولة. لا سحر.",
			L"Физический убийца вблизи. Бьёте соседнего врага кинжалами, критические удары часты. Магии нет.",
			L"Physischer Nah-Assassine. Ihr zielt mit Dolchen auf den angrenzenden Feind, kritische Treffer kommen leicht. Keine Magie.",
			L"Assassino físico no corpo a corpo. Você mira o inimigo adjacente com adagas, e o crítico sai fácil. Não usa magia.",
			L"Fysieke sluipmoordenaar van dichtbij. Je mikt met dolken op de aangrenzende vijand, en kritieken komen makkelijk. Geen magie.",
			L"Fizyczny zabójca z bliska. Celujesz sztyletami w sąsiada, a ciosy krytyczne padają łatwo. Bez magii.",
			L"Fiziksel yakın suikastçı. Hançerle bitişik düşmanı hedef alırsın, kritik kolay çıkar. Büyü kullanmazsın.");
		*b = LangPick14(
			L"素早さ・技・運が伸び、移動力も高い。HPと守備は低い。殴り合いを続けると落ちるので、先に仕留めて下がる。以後のレベルアップも素早さに寄る。",
			L"Agility, dexterity, and luck grow, and movement is high. HP and defense are low. A long brawl will drop you, so you kill first and step back. Later levels keep leaning on agility.",
			L"Agilité, adresse et chance montent, et le déplacement est haut. PV et défense sont bas. Une mêlée longue vous fait tomber : tuez d'abord, puis reculez. Les niveaux suivants penchent vers l'agilité.",
			L"Agilità, destrezza e fortuna crescono, e il movimento è alto. PS e difesa sono bassi. Una rissa lunga ti fa cadere: uccidi prima e arretra. I livelli successivi pendono sull'agilità.",
			L"Agilidad, destreza y suerte crecen, y el movimiento es alto. PV y defensa son bajos. Una pelea larga te tumba: mata primero y retrocede. Los niveles siguientes se inclinan a la agilidad.",
			L"민첩·기량·운이 늘고, 이동력도 높다. HP와 수비는 낮다. 난타를 이어가면 쓰러지니, 먼저 죽이고 빠진다. 이후 레벨업도 민첩에 기운다.",
			L"敏捷、技巧、运气成长，移动力也高。HP和防御低。久战会倒下，所以先杀掉再撤。之后的升级也偏向敏捷。",
			L"الرشاقة والبراعة والحظ تنمو، والحركة عالية. الصحة والدفاع منخفضان. القتال الطويل يسقطك، فاقتل أولًا ثم تراجع. المستويات اللاحقة تميل إلى الرشاقة.",
			L"Растут ловкость, сноровка и удача, движение высокое. ОЗ и защита низкие. Долгая рубка вас уронит: убейте первым и отойдите. Дальше уровни клонятся к ловкости.",
			L"Beweglichkeit, Geschick und Glück wachsen, die Bewegung ist hoch. LP und Abwehr sind niedrig. Ein langer Schlagabtausch wirft euch um: zuerst töten, dann zurück. Spätere Stufen neigen zur Beweglichkeit.",
			L"Agilidade, destreza e sorte crescem, e o movimento é alto. PV e defesa são baixos. Uma briga longa derruba você: mate primeiro e recue. Os níveis seguintes pendem para a agilidade.",
			L"Behendigheid, vaardigheid en geluk groeien, en de beweging is hoog. LP en verdediging zijn laag. Een lang gevecht laat je vallen: dood eerst, stap dan terug. Latere niveaus hellen naar behendigheid.",
			L"Rosną zwinność, zręczność i szczęście, ruch też jest wysoki. PŻ i obrona są niskie. Długa wymiana cię powali: zabij pierwszy i odejdź. Dalsze poziomy idą w zwinność.",
			L"Çeviklik, maharet ve şans artar, hareket de yüksektir. CP ve savunma düşüktür. Uzun boğuşma düşürür: önce öldür, sonra geri çekil. Sonraki seviyeler çevikliğe yaslanır.");
		break;
	case C_WIZARD:
		*a = LangPick14(
			L"攻撃魔法の魔法職。杖で範囲魔法を撃つ。武器の打撃はほぼ無い。",
			L"A magic class of attack spells. You cast area magic with a staff. Weapon blows barely exist.",
			L"Classe de magie d'attaque. Vous lancez des sorts de zone au bâton. Les coups d'arme comptent à peine.",
			L"Classe di magia d'attacco. Lanci magie ad area col bastone. I colpi d'arma quasi non contano.",
			L"Clase de magia de ataque. Lanzas magia de área con el bastón. Los golpes de arma casi no existen.",
			L"공격 마법의 마법 직업. 지팡이로 범위 마법을 쏜다. 무기의 타격은 거의 없다.",
			L"攻击魔法的魔法职。用杖放范围魔法。武器打击几乎没有。",
			L"وظيفة سحر هجومي. ترمي سحر المساحة بالعصا. ضربات السلاح تكاد لا توجد.",
			L"Магическая профессия атакующих заклинаний. Бьёте магией по площади посохом. Удары оружием почти ничто.",
			L"Eine Magieklasse für Angriffszauber. Ihr wirkt Flächenmagie mit dem Stab. Waffenschläge zählen kaum.",
			L"Classe de magia de ataque. Você lança magia em área com o cajado. Golpes de arma quase não existem.",
			L"Een magieklasse voor aanvalsspreuken. Je werpt gebiedsmagie met de staf. Wapenslagen tellen nauwelijks.",
			L"Magiczna profesja zaklęć ataku. Rzucasz magię obszarową laską. Uderzenia bronią prawie nie istnieją.",
			L"Saldırı büyüsünün büyü sınıfı. Asayla alan büyüsü atarsın. Silah vuruşu neredeyse yoktur.");
		*b = LangPick14(
			L"魔力とMPが大きく伸びる。HPと力は低く、前に出ると落ちやすい。以後のレベルアップも魔力に寄る。炎・雷・氷・闇を広く覚える。",
			L"Magic and MP grow hard. HP and strength are low, so the front line drops you. Later levels keep leaning on magic. You learn fire, thunder, ice, and darkness widely.",
			L"Magie et PM montent fort. PV et force sont bas, donc l'avant vous fait tomber. Les niveaux suivants penchent vers la magie. Vous apprenez largement feu, foudre, glace et ténèbres.",
			L"Magia e PM crescono molto. PS e forza sono bassi, quindi il fronte ti fa cadere. I livelli successivi pendono sulla magia. Impari ampiamente fuoco, tuono, gelo e oscurità.",
			L"Magia y PM crecen mucho. PV y fuerza son bajos, así que el frente te tumba. Los niveles siguientes se inclinan a la magia. Aprendes con amplitud fuego, trueno, hielo y oscuridad.",
			L"마력과 MP가 크게 늘어난다. HP와 힘은 낮아, 앞에 서면 쓰러지기 쉽다. 이후 레벨업도 마력에 기운다. 불·번개·얼음·어둠을 넓게 배운다.",
			L"魔力和MP大幅成长。HP和力量低，站到前面容易倒下。之后的升级也偏向魔力。火、雷、冰、暗都会广泛学会。",
			L"السحر والطاقة ينموان كثيرًا. الصحة والقوة منخفضتان، فالمقدمة تسقطك. المستويات اللاحقة تميل إلى السحر. تتعلم النار والرعد والجليد والظلام على نطاق واسع.",
			L"Магия и ОМ сильно растут. ОЗ и сила низкие, на передовой вы падаете. Дальше уровни клонятся к магии. Широко учите огонь, гром, лёд и тьму.",
			L"Magie und MP wachsen stark. LP und Stärke sind niedrig, vorn fallt ihr leicht. Spätere Stufen neigen zur Magie. Ihr lernt Feuer, Blitz, Eis und Dunkelheit breit.",
			L"Magia e PM crescem muito. PV e força são baixos, então a frente derruba você. Os níveis seguintes pendem para a magia. Você aprende fogo, trovão, gelo e trevas com amplitude.",
			L"Magie en MP groeien hard. LP en kracht zijn laag, dus vooraan val je snel. Latere niveaus hellen naar magie. Je leert vuur, donder, ijs en duisternis ruim.",
			L"Magia i PM rosną mocno. PŻ i siła są niskie, więc front cię powala. Dalsze poziomy idą w magię. Uczysz się szeroko ognia, gromu, lodu i ciemności.",
			L"Büyü ve MP çok artar. CP ve güç düşüktür, öne çıkınca kolay düşersin. Sonraki seviyeler büyüye yaslanır. Ateş, yıldırım, buz ve karanlığı geniş öğrenirsin.");
		break;
	case C_BISHOP:
		*a = LangPick14(
			L"回復と聖属性攻撃の魔法職。仲間を直し、聖なる魔法で敵も焼く。前に出て殴る職ではない。",
			L"A magic class of healing and holy attack. You mend allies and burn foes with holy magic. You are not the class that steps up and swings.",
			L"Classe de magie de soin et d'attaque sacrée. Vous relevez les alliés et brûlez les ennemis par le sacré. Vous n'êtes pas la classe qui avance pour frapper.",
			L"Classe di magia di cura e attacco sacro. Rimetri in piedi gli alleati e bruci i nemici col sacro. Non sei la classe che avanza per colpire.",
			L"Clase de magia de curación y ataque sagrado. Levantas a los aliados y quemas enemigos con lo sagrado. No eres la clase que avanza a golpear.",
			L"회복과 성속성 공격의 마법 직업. 동료를 고치고, 성스러운 마법으로 적도 태운다. 앞에 나와 때리는 직업이 아니다.",
			L"恢复与圣属性攻击的魔法职。治疗同伴，也用神圣魔法烧敌人。不是冲上去打的职。",
			L"وظيفة سحر للشفاء والهجوم المقدس. تصلح الحلفاء وتحرق الأعداء بالسحر المقدس. لست من يتقدم ليضرب.",
			L"Магия лечения и святой атаки. Лечите союзников и жжёте врагов святым. Это не профессия, которая выходит бить врукопашную.",
			L"Magieklasse für Heilung und heiligen Angriff. Ihr heilt Verbündete und verbrennt Feinde mit Heiligem. Nicht die Klasse, die vorgeht und schlägt.",
			L"Classe de magia de cura e ataque sagrado. Você cura aliados e queima inimigos com o sagrado. Não é a classe que avança para bater.",
			L"Magieklasse voor heling en heilige aanval. Je heelt bondgenoten en brandt vijanden met heilig. Niet de klasse die naar voren stapt om te slaan.",
			L"Magia leczenia i świętego ataku. Lecisz sojuszników i palisz wrogów świętością. To nie profesja, która wychodzi bić.",
			L"İyileştirme ve kutsal saldırı büyü sınıfı. Yoldaşları onarır, düşmanı kutsal büyüyle yakarsın. Öne çıkıp vuran sınıf değilsin.");
		*b = LangPick14(
			L"魔力・魔防・MPが伸び、HPは魔法職としてはやや厚い。力は低い。以後のレベルアップも魔力と魔防に寄る。全体回復まで覚える。",
			L"Magic, resistance, and MP grow, and HP is a little thick for a caster. Strength is low. Later levels lean on magic and resistance. You learn healing for the whole party.",
			L"Magie, résistance et PM montent, et les PV sont un peu épais pour un lanceur. La force est basse. Les niveaux suivants penchent vers magie et résistance. Vous apprenez le soin de tout le groupe.",
			L"Magia, resistenza e PM crescono, e i PS sono un po' spessi per un incantatore. La forza è bassa. I livelli successivi pendono su magia e resistenza. Impari la cura di tutto il gruppo.",
			L"Magia, resistencia y PM crecen, y los PV son algo gruesos para un lanzador. La fuerza es baja. Los niveles siguientes se inclinan a magia y resistencia. Aprendes la curación de todo el grupo.",
			L"마력·마방·MP가 늘고, HP는 마법 직업치고는 조금 두껍다. 힘은 낮다. 이후 레벨업도 마력과 마방에 기운다. 전체 회복까지 배운다.",
			L"魔力、魔防、MP成长，HP在魔法职里算稍厚。力量低。之后的升级也偏向魔力和魔防。会学到全体恢复。",
			L"السحر والمقاومة والطاقة تنمو، والصحة أسمك قليلًا لساحر. القوة منخفضة. المستويات اللاحقة تميل إلى السحر والمقاومة. تتعلم شفاء المجموعة كلها.",
			L"Растут магия, стойкость и ОМ, ОЗ для мага чуть толще. Сила низкая. Дальше уровни клонятся к магии и стойкости. Учите лечение всей группы.",
			L"Magie, Resistenz und MP wachsen, die LP sind für einen Zauberer etwas dick. Die Stärke ist niedrig. Spätere Stufen neigen zu Magie und Resistenz. Ihr lernt Heilung für die ganze Gruppe.",
			L"Magia, resistência e PM crescem, e os PV são um pouco grossos para um conjurador. A força é baixa. Os níveis seguintes pendem para magia e resistência. Você aprende a cura de todo o grupo.",
			L"Magie, weerstand en MP groeien, en LP zijn voor een magiër wat dik. Kracht is laag. Latere niveaus hellen naar magie en weerstand. Je leert heling voor de hele groep.",
			L"Rosną magia, odporność i PM, a PŻ są jak na maga trochę grube. Siła jest niska. Dalsze poziomy idą w magię i odporność. Uczysz się leczenia całej grupy.",
			L"Büyü, büyü direnci ve MP artar, CP bir büyücü için biraz kalındır. Güç düşüktür. Sonraki seviyeler büyü ve dirence yaslanır. Tüm grubu iyileştirmeyi öğrenirsin.");
		break;
	case C_SKYKNIGHT:
		*a = LangPick14(
			L"物理の飛行騎士。翼で移動し、山以外の地形を越える。魔法で戦う職ではない。",
			L"A physical flying knight. You move on wings and cross terrain other than mountains. This is not a class that fights with magic.",
			L"Chevalier physique volant. Vous vous déplacez par les ailes et franchissez le terrain sauf les montagnes. Ce n'est pas une classe qui combat par la magie.",
			L"Cavaliere fisico volante. Ti muovi con le ali e attraversi il terreno, tranne le montagne. Non è una classe che combatte con la magia.",
			L"Caballero físico volador. Te mueves con alas y cruzas el terreno, salvo las montañas. No es una clase que pelee con magia.",
			L"물리 비행 기사. 날개로 이동하고, 산 이외의 지형을 넘는다. 마법으로 싸우는 직업이 아니다.",
			L"物理飞行骑士。靠翅膀移动，可越过山地以外的地形。不是靠魔法作战的职。",
			L"فارس جسدي طائر. تتحرك بالأجنحة وتتجاوز التضاريس عدا الجبال. ليست وظيفة تقاتل بالسحر.",
			L"Физический летающий рыцарь. Двигаетесь на крыльях и пересекаете местность, кроме гор. Это не профессия боя магией.",
			L"Physischer fliegender Ritter. Ihr bewegt euch mit Flügeln und überquert Gelände außer Bergen. Keine Klasse, die mit Magie kämpft.",
			L"Cavaleiro físico voador. Você se move com asas e atravessa o terreno, exceto montanhas. Não é classe que luta com magia.",
			L"Fysieke vliegende ridder. Je beweegt op vleugels en kruist terrein behalve bergen. Geen klasse die met magie vecht.",
			L"Fizyczny latający rycerz. Ruszasz się na skrzydłach i pokonujesz teren poza górami. To nie profesja walki magią.",
			L"Fiziksel uçan şövalye. Kanatla hareket eder, dağlar dışında araziyi geçersin. Büyüyle savaşan sınıf değildir.");
		*b = LangPick14(
			L"力と素早さが伸び、加速の補助も少し覚える。守備は重騎士ほど厚くない。壁の向こうへ先に届く前衛。以後のレベルアップも素早さに寄る。",
			L"Strength and agility grow, and you learn a little haste. Defense is thinner than a heavy knight. You are the front liner who arrives past the wall first. Later levels lean on agility.",
			L"Force et agilité montent, et vous apprenez un peu de hâte. La défense est moins épaisse qu'un chevalier lourd. Vous êtes l'avant qui arrive le premier au-delà du mur. Les niveaux suivants penchent vers l'agilité.",
			L"Forza e agilità crescono, e impari un po' di fretta. La difesa è meno spessa di un cavaliere pesante. Sei il fronte che arriva primo oltre il muro. I livelli successivi pendono sull'agilità.",
			L"Fuerza y agilidad crecen, y aprendes un poco de prisa. La defensa es menos gruesa que la de un caballero pesado. Eres el frente que llega primero al otro lado del muro. Los niveles siguientes se inclinan a la agilidad.",
			L"힘과 민첩이 늘고, 가속 보조도 조금 배운다. 수비는 중기사만큼 두껍지 않다. 벽 너머로 먼저 닿는 전열. 이후 레벨업도 민첩에 기운다.",
			L"力量和敏捷成长，也会一点加速辅助。防御不如重骑士厚。是先绕到墙那边的前排。之后的升级也偏向敏捷。",
			L"القوة والرشاقة تنموان، وتتعلم قليلًا من التسريع. الدفاع أرق من الفارس الثقيل. أنت مقدمة تصل أولًا خلف الجدار. المستويات اللاحقة تميل إلى الرشاقة.",
			L"Растут сила и ловкость, немного учите ускорение. Защита тоньше, чем у тяжёлого рыцаря. Вы передовой, который первым оказывается за стеной. Дальше уровни клонятся к ловкости.",
			L"Stärke und Beweglichkeit wachsen, und ihr lernt etwas Hast. Die Abwehr ist dünner als bei einem schweren Ritter. Ihr seid die Front, die zuerst jenseits der Mauer ankommt. Spätere Stufen neigen zur Beweglichkeit.",
			L"Força e agilidade crescem, e você aprende um pouco de pressa. A defesa é menos grossa que a de um cavaleiro pesado. Você é a frente que chega primeiro além do muro. Os níveis seguintes pendem para a agilidade.",
			L"Kracht en behendigheid groeien, en je leert wat haast. Verdediging is dunner dan bij een zware ridder. Jij bent de voorhoede die als eerste voorbij de muur komt. Latere niveaus hellen naar behendigheid.",
			L"Rosną siła i zwinność, uczysz się też trochę pośpiechu. Obrona jest cieńsza niż u ciężkiego rycerza. Jesteś frontem, który pierwszy jest za murem. Dalsze poziomy idą w zwinność.",
			L"Güç ve çeviklik artar, biraz hız desteği de öğrenirsin. Savunma ağır şövalye kadar kalın değildir. Duvarın ötesine ilk varan ön safsın. Sonraki seviyeler çevikliğe yaslanır.");
		break;
	case C_HERO:
		*a = LangPick14(
			L"物理の剣士の完成形。両手剣で隣の敵を叩き、攻守とも高い。魔法が本業ではない。",
			L"The finished physical swordsman. You strike the foe beside you with a greatsword, and both attack and defense are high. Magic is not the job.",
			L"L'épéiste physique abouti. Vous frappez l'ennemi adjacent à l'épée à deux mains, attaque et défense sont hautes. La magie n'est pas le métier.",
			L"Lo spadaccino fisico compiuto. Colpisci il nemico adiacente con la spada a due mani, attacco e difesa sono alti. La magia non è il mestiere.",
			L"El espadachín físico acabado. Golpeas al enemigo adyacente con la espada a dos manos, ataque y defensa son altos. La magia no es el oficio.",
			L"물리 검사의 완성. 양손검으로 인접한 적을 치고, 공수 모두 높다. 마법이 본업은 아니다.",
			L"物理剑士的完成形。用双手剑打相邻的敌人，攻守都高。魔法不是本职。",
			L"السيّاف الجسدي المكتمل. تضرب العدو المجاور بسيف اليدين، والهجوم والدفاع عاليان. السحر ليس المهنة.",
			L"Законченный физический мечник. Бьёте соседнего врага двуручным мечом, и атака, и защита высоки. Магия не основная работа.",
			L"Der fertige physische Schwertkämpfer. Ihr schlagt den angrenzenden Feind mit dem Zweihänder, Angriff und Abwehr sind hoch. Magie ist nicht die Arbeit.",
			L"O espadachim físico acabado. Você golpeia o inimigo adjacente com a espada de duas mãos, ataque e defesa são altos. Magia não é o ofício.",
			L"De voltooide fysieke zwaardvechter. Je slaat de aangrenzende vijand met het tweehandige zwaard, aanval en verdediging zijn hoog. Magie is niet het vak.",
			L"Skończony fizyczny szermierz. Bijesz sąsiada mieczem dwuręcznym, atak i obrona są wysokie. Magia nie jest fachem.",
			L"Tamamlanmış fiziksel kılıççı. İki elli kılıçla bitişik düşmanı vurursun, saldırı da savunma da yüksektir. Büyü asıl iş değildir.");
		*b = LangPick14(
			L"力・HP・守備・技がよく伸びる。聖剣も覚えるが、範囲魔法の代わりにはならない。以後のレベルアップも力と守備に寄る。単体を殴り倒す前衛。",
			L"Strength, HP, defense, and dexterity grow well. You also learn a holy blade, but you do not replace area magic. Later levels lean on strength and defense. A front liner who drops single targets.",
			L"Force, PV, défense et adresse montent bien. Vous apprenez aussi une lame sacrée, mais vous ne remplacez pas la magie de zone. Les niveaux suivants penchent vers force et défense. Un avant qui abat les cibles seules.",
			L"Forza, PS, difesa e destrezza crescono bene. Impari anche una lama sacra, ma non sostituisci la magia ad area. I livelli successivi pendono su forza e difesa. Un fronte che abbatte i bersagli singoli.",
			L"Fuerza, PV, defensa y destreza crecen bien. También aprendes un filo sagrado, pero no sustituyes la magia de área. Los niveles siguientes se inclinan a fuerza y defensa. Un frente que tumba objetivos sueltos.",
			L"힘·HP·수비·기량이 잘 늘어난다. 성검도 배우지만, 범위 마법의 대역은 되지 않는다. 이후 레벨업도 힘과 수비에 기운다. 단체를 때려 눕히는 전열.",
			L"力量、HP、防御、技巧都成长良好。也会圣剑，但代替不了范围魔法。之后的升级也偏向力量和防御。是打倒单体的前排。",
			L"القوة والصحة والدفاع والبراعة تنمو جيدًا. تتعلم أيضًا نصلًا مقدسًا، لكنك لا تغني عن سحر المساحة. المستويات اللاحقة تميل إلى القوة والدفاع. مقدمة تُسقط الأهداف المنفردة.",
			L"Хорошо растут сила, ОЗ, защита и сноровка. Учите и святой клинок, но площадную магию не заменяете. Дальше уровни клонятся к силе и защите. Передовой, который валит одиночные цели.",
			L"Stärke, LP, Abwehr und Geschick wachsen gut. Ihr lernt auch eine heilige Klinge, ersetzt aber keine Flächenmagie. Spätere Stufen neigen zu Stärke und Abwehr. Eine Front, die Einzelziele niederschlägt.",
			L"Força, PV, defesa e destreza crescem bem. Você também aprende uma lâmina sagrada, mas não substitui magia em área. Os níveis seguintes pendem para força e defesa. Uma frente que derruba alvos sozinhos.",
			L"Kracht, LP, verdediging en vaardigheid groeien goed. Je leert ook een heilig zwaard, maar je vervangt geen gebiedsmagie. Latere niveaus hellen naar kracht en verdediging. Een voorhoede die losse doelen neerslaat.",
			L"Dobrze rosną siła, PŻ, obrona i zręczność. Uczysz się też świętego ostrza, ale nie zastępujesz magii obszarowej. Dalsze poziomy idą w siłę i obronę. Front, który zwala pojedyncze cele.",
			L"Güç, CP, savunma ve maharet iyi artar. Kutsal kılıç da öğrenirsin ama alan büyüsünün yerini tutmazsın. Sonraki seviyeler güç ve savunmaya yaslanır. Tek hedefleri deviren ön safsın.");
		break;
	case C_ARCHMAGE:
		*a = LangPick14(
			L"攻撃魔法の極致。杖で広範囲の強力な魔法を撃つ。物理の打撃は期待しない。",
			L"The peak of attack magic. You cast strong, wide magic with a staff. Do not expect physical blows.",
			L"Le sommet de la magie d'attaque. Vous lancez une magie forte et large au bâton. N'attendez pas les coups physiques.",
			L"Il vertice della magia d'attacco. Lanci magia forte e ampia col bastone. Non aspettarti colpi fisici.",
			L"La cima de la magia de ataque. Lanzas magia fuerte y amplia con el bastón. No esperes golpes físicos.",
			L"공격 마법의 극치. 지팡이로 넓은 범위의 강한 마법을 쏜다. 물리 타격은 기대하지 않는다.",
			L"攻击魔法的极致。用杖放广范围的强力魔法。不要指望物理打击。",
			L"ذروة سحر الهجوم. ترمي سحرًا قويًا واسعًا بالعصا. لا تتوقع الضربات الجسدية.",
			L"Вершина атакующей магии. Бьёте сильной широкой магией посохом. Физических ударов не ждите.",
			L"Der Gipfel der Angriffsmagie. Ihr wirkt starke, weite Magie mit dem Stab. Physische Schläge erwartet nicht.",
			L"O auge da magia de ataque. Você lança magia forte e ampla com o cajado. Não espere golpes físicos.",
			L"De top van aanvalsmagie. Je werpt sterke, brede magie met de staf. Verwacht geen fysieke slagen.",
			L"Szczyt magii ataku. Rzucasz silną, szeroką magię laską. Nie licz na ciosy fizyczne.",
			L"Saldırı büyüsünün zirvesi. Asayla geniş ve güçlü büyü atarsın. Fiziksel vuruş bekleme.");
		*b = LangPick14(
			L"魔力とMPが非常に伸びる。HPと力は低い。隕石級まで覚える代わりに、打たれると落ちる。以後のレベルアップも魔力に寄る。後列から撃つ職。",
			L"Magic and MP grow very hard. HP and strength stay low. You learn magic up to meteor scale, and a hit drops you. Later levels lean on magic. You cast from the back line.",
			L"Magie et PM montent très fort. PV et force restent bas. Vous apprenez jusqu'à l'échelle des météores, et un coup vous fait tomber. Les niveaux suivants penchent vers la magie. Vous lancez depuis l'arrière.",
			L"Magia e PM crescono moltissimo. PS e forza restano bassi. Impari fino alla scala delle meteore, e un colpo ti fa cadere. I livelli successivi pendono sulla magia. Lanci dal retro.",
			L"Magia y PM crecen muchísimo. PV y fuerza se quedan bajos. Aprendes hasta la escala de los meteoros, y un golpe te tumba. Los niveles siguientes se inclinan a la magia. Lanzas desde la retaguardia.",
			L"마력과 MP가 매우 늘어난다. HP와 힘은 낮다. 운석급까지 배우는 대신, 맞으면 쓰러진다. 이후 레벨업도 마력에 기운다. 후열에서 쏘는 직업.",
			L"魔力和MP成长极高。HP和力量低。能学到陨石级，但被打就会倒下。之后的升级也偏向魔力。从后排施放的职。",
			L"السحر والطاقة ينموان جدًا. الصحة والقوة تبقيان منخفضتين. تتعلم حتى مستوى النيازك، والضربة تسقطك. المستويات اللاحقة تميل إلى السحر. ترمي من الصف الخلفي.",
			L"Магия и ОМ растут очень сильно. ОЗ и сила низкие. Учите магию вплоть до метеоров, но удар вас роняет. Дальше уровни клонятся к магии. Бьёте из задней линии.",
			L"Magie und MP wachsen sehr stark. LP und Stärke bleiben niedrig. Ihr lernt bis zur Meteorstufe, und ein Treffer wirft euch um. Spätere Stufen neigen zur Magie. Ihr wirkt von hinten.",
			L"Magia e PM crescem demais. PV e força ficam baixos. Você aprende até a escala de meteoros, e um golpe derruba você. Os níveis seguintes pendem para a magia. Você lança da retaguarda.",
			L"Magie en MP groeien zeer hard. LP en kracht blijven laag. Je leert tot meteoorschaal, en een treffer laat je vallen. Latere niveaus hellen naar magie. Je werpt vanuit de achterhoede.",
			L"Magia i PM rosną bardzo mocno. PŻ i siła zostają niskie. Uczysz się aż do skali meteorów, a cios cię powala. Dalsze poziomy idą w magię. Rzucasz z tylnej linii.",
			L"Büyü ve MP çok sert artar. CP ve güç düşük kalır. Meteor ölçeğine kadar öğrenirsin, bir isabet düşürür. Sonraki seviyeler büyüye yaslanır. Arka saftan atarsın.");
		break;
	case C_SAINT:
		*a = LangPick14(
			L"回復と聖属性魔法の頂点。全体回復と聖なる攻撃を担う。殴って前に出る職ではない。",
			L"The peak of healing and holy magic. You carry party healing and holy attack. You are not the class that steps forward to swing.",
			L"Le sommet du soin et de la magie sacrée. Vous portez le soin du groupe et l'attaque sacrée. Vous n'êtes pas la classe qui avance pour frapper.",
			L"Il vertice di cura e magia sacra. Porti la cura del gruppo e l'attacco sacro. Non sei la classe che avanza per colpire.",
			L"La cima de la curación y la magia sagrada. Llevas la curación del grupo y el ataque sagrado. No eres la clase que avanza a golpear.",
			L"회복과 성속성 마법의 정점. 전체 회복과 성스러운 공격을 맡는다. 때리며 앞에 서는 직업이 아니다.",
			L"恢复与圣属性魔法的顶点。负责全体恢复和神圣攻击。不是冲上去打的职。",
			L"ذروة الشفاء والسحر المقدس. تحمل شفاء المجموعة والهجوم المقدس. لست من يتقدم ليضرب.",
			L"Вершина лечения и святой магии. На вас общее лечение и святая атака. Это не профессия, которая выходит бить.",
			L"Der Gipfel von Heilung und heiliger Magie. Ihr tragt Gruppenheilung und heiligen Angriff. Nicht die Klasse, die vorgeht und schlägt.",
			L"O auge da cura e da magia sagrada. Você carrega a cura do grupo e o ataque sagrado. Não é a classe que avança para bater.",
			L"De top van heling en heilige magie. Jij draagt groepsheling en heilige aanval. Niet de klasse die naar voren stapt om te slaan.",
			L"Szczyt leczenia i świętej magii. Niesiesz leczenie grupy i święty atak. To nie profesja, która wychodzi bić.",
			L"İyileştirme ve kutsal büyünün zirvesi. Grup iyileştirmesi ve kutsal saldırıyı taşırsın. Öne çıkıp vuran sınıf değilsin.");
		*b = LangPick14(
			L"魔力・魔防・MP・運が伸びる。力は低い。味方を立て直すのが仕事で、聖属性に弱い敵は魔法で焼ける。以後のレベルアップも魔力と魔防に寄る。",
			L"Magic, resistance, MP, and luck grow. Strength is low. Your work is to put allies back on their feet, and foes weak to holy burn under your spells. Later levels lean on magic and resistance.",
			L"Magie, résistance, PM et chance montent. La force est basse. Votre travail est de relever les alliés, et les ennemis faibles au sacré brûlent sous vos sorts. Les niveaux suivants penchent vers magie et résistance.",
			L"Magia, resistenza, PM e fortuna crescono. La forza è bassa. Il tuo lavoro è rialzare gli alleati, e i nemici deboli al sacro bruciano sotto gli incantesimi. I livelli successivi pendono su magia e resistenza.",
			L"Magia, resistencia, PM y suerte crecen. La fuerza es baja. Tu trabajo es volver a poner en pie a los aliados, y los enemigos débiles a lo sagrado arden bajo tus conjuros. Los niveles siguientes se inclinan a magia y resistencia.",
			L"마력·마방·MP·운이 늘어난다. 힘은 낮다. 아군을 다시 세우는 것이 일이고, 성속성에 약한 적은 마법으로 태울 수 있다. 이후 레벨업도 마력과 마방에 기운다.",
			L"魔力、魔防、MP、运气成长。力量低。工作是把同伴重新扶起，怕圣属性的敌人能用魔法烧掉。之后的升级也偏向魔力和魔防。",
			L"السحر والمقاومة والطاقة والحظ تنمو. القوة منخفضة. عملك أن تعيد الحلفاء إلى أقدامهم، والأعداء الضعفاء أمام المقدس يحترقون بسحرك. المستويات اللاحقة تميل إلى السحر والمقاومة.",
			L"Растут магия, стойкость, ОМ и удача. Сила низкая. Ваша работа — поднимать союзников, а враги, слабые к святому, горят от заклинаний. Дальше уровни клонятся к магии и стойкости.",
			L"Magie, Resistenz, MP und Glück wachsen. Die Stärke ist niedrig. Eure Arbeit ist, Verbündete wieder aufzustellen, und Feinde mit Schwäche gegen Heilig verbrennen unter euren Zaubern. Spätere Stufen neigen zu Magie und Resistenz.",
			L"Magia, resistência, PM e sorte crescem. A força é baixa. Seu trabalho é pôr os aliados de pé de novo, e inimigos fracos ao sagrado queimam sob seus feitiços. Os níveis seguintes pendem para magia e resistência.",
			L"Magie, weerstand, MP en geluk groeien. Kracht is laag. Je werk is bondgenoten weer op de been te zetten, en vijanden zwak tegen heilig branden onder je spreuken. Latere niveaus hellen naar magie en weerstand.",
			L"Rosną magia, odporność, PM i szczęście. Siła jest niska. Twoja praca to stawiać sojuszników z powrotem, a wrogowie słabi na świętość płoną od zaklęć. Dalsze poziomy idą w magię i odporność.",
			L"Büyü, direnç, MP ve şans artar. Güç düşüktür. İşin yoldaşları yeniden ayağa kaldırmaktır, kutsalda zayıf düşman büyünle yanar. Sonraki seviyeler büyü ve dirence yaslanır.");
		break;
	case C_PHANTOM:
		*a = LangPick14(
			L"物理の高速職。短剣と射撃で近い距離から中距離まで届く。魔法は使わない。",
			L"A fast physical class. Daggers and shots reach from close to mid range. You do not use magic.",
			L"Classe physique rapide. Dagues et tirs portent du proche au moyen. Pas de magie.",
			L"Classe fisica veloce. Pugnali e tiri arrivano dal vicino al medio. Niente magia.",
			L"Clase física veloz. Dagas y disparos llegan de cerca a media distancia. No usas magia.",
			L"물리 고속 직업. 단검과 사격으로 가까운 거리에서 중거리까지 닿는다. 마법은 쓰지 않는다.",
			L"物理高速职。短剑和射击能从近距离打到中距离。不用魔法。",
			L"وظيفة جسدية سريعة. الخناجر والرمي تصل من القريب إلى المتوسط. لا سحر.",
			L"Быстрая физическая профессия. Кинжалы и выстрелы бьют от ближней до средней дистанции. Магии нет.",
			L"Schnelle physische Klasse. Dolche und Schüsse reichen von nah bis mittel. Keine Magie.",
			L"Classe física veloz. Adagas e tiros alcançam do perto ao médio. Não usa magia.",
			L"Snelle fysieke klasse. Dolken en schoten reiken van dichtbij tot middellang. Geen magie.",
			L"Szybka fizyczna profesja. Sztylety i strzały sięgają od bliska do średniego dystansu. Bez magii.",
			L"Hızlı fiziksel sınıf. Hançer ve atış yakından orta menzile ulaşır. Büyü kullanmazsın.");
		*b = LangPick14(
			L"素早さ・技・運・移動力が大きく伸び、必殺と回避が高い。HPは高くない。仕留めて下がる動き。以後のレベルアップも素早さと技に寄る。",
			L"Agility, dexterity, luck, and movement grow hard, and critical hits and evasion are high. HP is not high. You kill and fall back. Later levels lean on agility and dexterity.",
			L"Agilité, adresse, chance et déplacement montent fort, critiques et esquive sont hauts. Les PV ne sont pas hauts. Vous tuez et reculez. Les niveaux suivants penchent vers agilité et adresse.",
			L"Agilità, destrezza, fortuna e movimento crescono molto, critici e schivata sono alti. I PS non sono alti. Uccidi e arretri. I livelli successivi pendono su agilità e destrezza.",
			L"Agilidad, destreza, suerte y movimiento crecen mucho, críticos y evasión son altos. Los PV no son altos. Matas y retrocedes. Los niveles siguientes se inclinan a agilidad y destreza.",
			L"민첩·기량·운·이동력이 크게 늘고, 필살과 회피가 높다. HP는 높지 않다. 죽이고 빠지는 움직임. 이후 레벨업도 민첩과 기량에 기운다.",
			L"敏捷、技巧、运气、移动力大幅成长，必杀和回避高。HP不高。杀掉再撤。之后的升级也偏向敏捷和技巧。",
			L"الرشاقة والبراعة والحظ والحركة تنمو كثيرًا، والضربة القاضية والتفادي عاليان. الصحة ليست عالية. تقتل ثم تتراجع. المستويات اللاحقة تميل إلى الرشاقة والبراعة.",
			L"Сильно растут ловкость, сноровка, удача и движение, крит и уклонение высоки. ОЗ невысоки. Убиваете и отходите. Дальше уровни клонятся к ловкости и сноровке.",
			L"Beweglichkeit, Geschick, Glück und Bewegung wachsen stark, Krit und Ausweichen sind hoch. Die LP sind nicht hoch. Ihr tötet und geht zurück. Spätere Stufen neigen zu Beweglichkeit und Geschick.",
			L"Agilidade, destreza, sorte e movimento crescem muito, crítico e esquiva são altos. Os PV não são altos. Você mata e recua. Os níveis seguintes pendem para agilidade e destreza.",
			L"Behendigheid, vaardigheid, geluk en beweging groeien hard, kritiek en ontwijken zijn hoog. LP zijn niet hoog. Je doodt en stapt terug. Latere niveaus hellen naar behendigheid en vaardigheid.",
			L"Mocno rosną zwinność, zręczność, szczęście i ruch, kryt i unik są wysokie. PŻ nie są wysokie. Zabijasz i odchodzisz. Dalsze poziomy idą w zwinność i zręczność.",
			L"Çeviklik, maharet, şans ve hareket çok artar, kritik ve kaçınma yüksektir. CP yüksek değildir. Öldürüp geri çekilirsin. Sonraki seviyeler çeviklik ve maharete yaslanır.");
		break;
	case C_DRAGONLORD:
		*a = LangPick14(
			L"物理の飛行重職。山以外の地形を越え、隣の敵を両手剣で叩く。魔法は本業ではない。",
			L"A heavy physical flyer. You cross terrain other than mountains and strike the foe beside you with a greatsword. Magic is not the job.",
			L"Volant physique lourd. Vous franchissez le terrain sauf les montagnes et frappez l'ennemi adjacent à l'épée à deux mains. La magie n'est pas le métier.",
			L"Volante fisico pesante. Attraversi il terreno tranne le montagne e colpisci il nemico adiacente con la spada a due mani. La magia non è il mestiere.",
			L"Volador físico pesado. Cruzas el terreno salvo las montañas y golpeas al enemigo adyacente con la espada a dos manos. La magia no es el oficio.",
			L"물리 비행 중직. 산 이외의 지형을 넘고, 인접한 적을 양손검으로 친다. 마법이 본업은 아니다.",
			L"物理飞行重职。越过山地以外的地形，用双手剑打相邻的敌人。魔法不是本职。",
			L"طائر جسدي ثقيل. تتجاوز التضاريس عدا الجبال وتضرب العدو المجاور بسيف اليدين. السحر ليس المهنة.",
			L"Тяжёлый физический летун. Пересекаете местность, кроме гор, и бьёте соседнего врага двуручным мечом. Магия не основная работа.",
			L"Schwerer physischer Flieger. Ihr überquert Gelände außer Bergen und schlagt den angrenzenden Feind mit dem Zweihänder. Magie ist nicht die Arbeit.",
			L"Voador físico pesado. Você atravessa o terreno, exceto montanhas, e golpeia o inimigo adjacente com a espada de duas mãos. Magia não é o ofício.",
			L"Zware fysieke vlieger. Je kruist terrein behalve bergen en slaat de aangrenzende vijand met het tweehandige zwaard. Magie is niet het vak.",
			L"Ciężki fizyczny lotnik. Pokonujesz teren poza górami i bijesz sąsiada mieczem dwuręcznym. Magia nie jest fachem.",
			L"Ağır fiziksel uçucu. Dağlar dışında araziyi geçip bitişik düşmanı iki elli kılıçla vurursun. Büyü asıl iş değildir.");
		*b = LangPick14(
			L"力・HP・守備が高い。範囲魔法は持たない。壁と溶岩を越えて先に届く前衛。以後のレベルアップも力と守備に寄る。",
			L"Strength, HP, and defense are high. You have no area magic. You are the front liner who arrives first across walls and lava. Later levels lean on strength and defense.",
			L"Force, PV et défense sont hauts. Pas de magie de zone. Vous êtes l'avant qui arrive le premier au-delà des murs et de la lave. Les niveaux suivants penchent vers force et défense.",
			L"Forza, PS e difesa sono alti. Niente magia ad area. Sei il fronte che arriva primo oltre muri e lava. I livelli successivi pendono su forza e difesa.",
			L"Fuerza, PV y defensa son altos. No tienes magia de área. Eres el frente que llega primero al otro lado de muros y lava. Los niveles siguientes se inclinan a fuerza y defensa.",
			L"힘·HP·수비가 높다. 범위 마법은 없다. 벽과 용암을 넘어 먼저 닿는 전열. 이후 레벨업도 힘과 수비에 기운다.",
			L"力量、HP、防御高。没有范围魔法。是越过墙和熔岩先到的前排。之后的升级也偏向力量和防御。",
			L"القوة والصحة والدفاع عالية. لا سحر مساحة. أنت المقدمة التي تصل أولًا عبر الجدران والحمم. المستويات اللاحقة تميل إلى القوة والدفاع.",
			L"Сила, ОЗ и защита высоки. Площадной магии нет. Вы передовой, который первым проходит стены и лаву. Дальше уровни клонятся к силе и защите.",
			L"Stärke, LP und Abwehr sind hoch. Keine Flächenmagie. Ihr seid die Front, die zuerst über Mauern und Lava kommt. Spätere Stufen neigen zu Stärke und Abwehr.",
			L"Força, PV e defesa são altos. Não há magia em área. Você é a frente que chega primeiro além de muros e lava. Os níveis seguintes pendem para força e defesa.",
			L"Kracht, LP en verdediging zijn hoog. Geen gebiedsmagie. Jij bent de voorhoede die als eerste over muren en lava komt. Latere niveaus hellen naar kracht en verdediging.",
			L"Siła, PŻ i obrona są wysokie. Nie ma magii obszarowej. Jesteś frontem, który pierwszy przechodzi mury i lawę. Dalsze poziomy idą w siłę i obronę.",
			L"Güç, CP ve savunma yüksektir. Alan büyüsü yoktur. Duvar ve lavın ötesine ilk varan ön safsın. Sonraki seviyeler güç ve savunmaya yaslanır.");
		break;
	case C_ALCHEMIST:
		*a = LangPick14(
			L"魔法寄りの錬金術師。攻撃魔法と回復と変換を杖で使う。純粋な前衛ではない。",
			L"An alchemist leaning to magic. You use attack spells, healing, and transmutation with a staff. Not a pure front liner.",
			L"Alchimiste tourné vers la magie. Vous usez de sorts d'attaque, de soin et de transmutation au bâton. Pas un avant pur.",
			L"Alchimista rivolto alla magia. Usi magie d'attacco, cura e trasmutazione col bastone. Non un fronte puro.",
			L"Alquimista inclinado a la magia. Usas conjuros de ataque, curación y transmutación con el bastón. No un frente puro.",
			L"마법 쪽의 연금술사. 공격 마법과 회복과 변환을 지팡이로 쓴다. 순수한 전열이 아니다.",
			L"偏魔法的炼金术师。用杖施放攻击魔法、恢复和转化。不是纯前排。",
			L"خيميائي يميل إلى السحر. تستخدم سحر الهجوم والشفاء والتحويل بالعصا. لست مقدمة خالصة.",
			L"Алхимик ближе к магии. Атакующие заклинания, лечение и превращение посохом. Не чистый передовой.",
			L"Alchemist zur Magie hin. Ihr wirkt Angriffszauber, Heilung und Verwandlung mit dem Stab. Keine reine Front.",
			L"Alquimista inclinado à magia. Você usa magia de ataque, cura e transmutação com o cajado. Não uma frente pura.",
			L"Alchemist die naar magie helt. Je gebruikt aanvalsspreuken, heling en transmutatie met de staf. Geen pure voorhoede.",
			L"Alchemik bliżej magii. Używasz zaklęć ataku, leczenia i transmutacji laską. Nie czysty front.",
			L"Büyüye yaslanan simyacı. Asayla saldırı büyüsü, iyileştirme ve dönüşüm kullanırsın. Saf ön saf değilsin.");
		*b = LangPick14(
			L"魔力とMPが伸び、力は魔法職にしては少しある。HPは低め。薬と属性で味方を支え、敵を削る。以後のレベルアップも魔力に寄る。",
			L"Magic and MP grow, and strength is a little higher than a pure caster. HP stays low. You support allies with medicine and elements, and you wear foes down. Later levels lean on magic.",
			L"Magie et PM montent, et la force est un peu plus haute qu'un lanceur pur. Les PV restent bas. Vous soutenez les alliés par la médecine et les éléments, et vous usez les ennemis. Les niveaux suivants penchent vers la magie.",
			L"Magia e PM crescono, e la forza è un po' più alta di un incantatore puro. I PS restano bassi. Sostieni gli alleati con medicina ed elementi, e logori i nemici. I livelli successivi pendono sulla magia.",
			L"Magia y PM crecen, y la fuerza es un poco más alta que la de un lanzador puro. Los PV se quedan bajos. Apoyas a los aliados con medicina y elementos, y desgasta a los enemigos. Los niveles siguientes se inclinan a la magia.",
			L"마력과 MP가 늘고, 힘은 마법 직업치고는 조금 있다. HP는 낮은 편. 약과 속성으로 아군을 돕고, 적을 깎는다. 이후 레벨업도 마력에 기운다.",
			L"魔力和MP成长，力量对魔法职来说稍高一点。HP偏低。用药和属性支援同伴、削减敌人。之后的升级也偏向魔力。",
			L"السحر والطاقة ينموان، والقوة أعلى قليلًا من الساحر الخالص. الصحة تبقى منخفضة. تدعم الحلفاء بالدواء والعناصر وتنهك الأعداء. المستويات اللاحقة تميل إلى السحر.",
			L"Растут магия и ОМ, сила чуть выше, чем у чистого мага. ОЗ низкие. Поддерживаете союзников зельем и стихиями и стачиваете врагов. Дальше уровни клонятся к магии.",
			L"Magie und MP wachsen, die Stärke ist für einen Zauberer etwas höher. Die LP bleiben niedrig. Ihr stützt Verbündete mit Medizin und Elementen und reibt Feinde auf. Spätere Stufen neigen zur Magie.",
			L"Magia e PM crescem, e a força é um pouco mais alta que a de um conjurador puro. Os PV ficam baixos. Você apoia aliados com medicina e elementos e desgasta inimigos. Os níveis seguintes pendem para a magia.",
			L"Magie en MP groeien, kracht is voor een magiër wat hoger. LP blijven laag. Je steunt bondgenoten met medicijn en elementen en slijt vijanden. Latere niveaus hellen naar magie.",
			L"Rosną magia i PM, siła jest jak na maga trochę wyższa. PŻ zostają niskie. Wspierasz sojuszników lekiem i żywiołami i ścierasz wrogów. Dalsze poziomy idą w magię.",
			L"Büyü ve MP artar, güç saf bir büyücüye göre biraz yüksektir. CP düşük kalır. İlaç ve elementle yoldaşları destekler, düşmanı eritirsin. Sonraki seviyeler büyüye yaslanır.");
		break;
	case C_ARCHALCHEMIST:
		*a = LangPick14(
			L"錬金の上級魔法職。杖で広範囲の攻撃魔法を撃つ。物理で前に出る職ではない。",
			L"A high magic class of alchemy. You cast wide attack magic with a staff. You are not the class that steps forward to fight physically.",
			L"Haute classe de magie d'alchimie. Vous lancez une magie d'attaque large au bâton. Vous n'êtes pas la classe qui avance pour le combat physique.",
			L"Alta classe di magia alchemica. Lanci magia d'attacco ampia col bastone. Non sei la classe che avanza per il fisico.",
			L"Alta clase de magia de alquimia. Lanzas magia de ataque amplia con el bastón. No eres la clase que avanza al combate físico.",
			L"연금의 상급 마법 직업. 지팡이로 넓은 범위의 공격 마법을 쏜다. 물리로 앞에 서는 직업이 아니다.",
			L"炼金的上级魔法职。用杖放广范围攻击魔法。不是靠物理冲到前面的职。",
			L"وظيفة سحر خيميائي عالية. ترمي سحر هجوم واسعًا بالعصا. لست من يتقدم للقتال الجسدي.",
			L"Высшая алхимическая магия. Бьёте широкой атакующей магией посохом. Это не профессия, которая выходит биться физически.",
			L"Hohe alchemische Magieklasse. Ihr wirkt weite Angriffsmagie mit dem Stab. Nicht die Klasse, die physisch vorgeht.",
			L"Alta classe de magia de alquimia. Você lança magia de ataque ampla com o cajado. Não é a classe que avança para lutar fisicamente.",
			L"Hoge alchemische magieklasse. Je werpt brede aanvalsmagie met de staf. Niet de klasse die fysiek naar voren stapt.",
			L"Wysoka magia alchemii. Rzucasz szeroką magię ataku laską. To nie profesja, która wychodzi bić fizycznie.",
			L"Simyanın üst büyü sınıfı. Asayla geniş saldırı büyüsü atarsın. Fiziksel olarak öne çıkan sınıf değilsin.");
		*b = LangPick14(
			L"魔力・魔防・MPが伸びる。力は上がっても殴り役にはならない。隕石級の魔法まで覚える。以後のレベルアップも魔力に寄る。",
			L"Magic, resistance, and MP grow. Even if strength rises, you are not the one who brawls. You learn magic up to meteor scale. Later levels lean on magic.",
			L"Magie, résistance et PM montent. Même si la force monte, vous n'êtes pas celui qui se bat au corps. Vous apprenez jusqu'à l'échelle des météores. Les niveaux suivants penchent vers la magie.",
			L"Magia, resistenza e PM crescono. Anche se la forza sale, non sei tu a fare la rissa. Impari fino alla scala delle meteore. I livelli successivi pendono sulla magia.",
			L"Magia, resistencia y PM crecen. Aunque la fuerza suba, no eres quien pelea a golpes. Aprendes hasta la escala de los meteoros. Los niveles siguientes se inclinan a la magia.",
			L"마력·마방·MP가 늘어난다. 힘이 올라도 때리는 역할은 아니다. 운석급 마법까지 배운다. 이후 레벨업도 마력에 기운다.",
			L"魔力、魔防、MP成长。力量即使上去也不是殴打的角色。会学到陨石级魔法。之后的升级也偏向魔力。",
			L"السحر والمقاومة والطاقة تنمو. حتى لو ارتفعت القوة فأنت لست من يتشاجر. تتعلم حتى مستوى النيازك. المستويات اللاحقة تميل إلى السحر.",
			L"Растут магия, стойкость и ОМ. Даже если сила выше, вы не тот, кто дерётся врукопашную. Учите магию до метеоров. Дальше уровни клонятся к магии.",
			L"Magie, Resistenz und MP wachsen. Selbst wenn die Stärke steigt, seid ihr nicht der Raufbold. Ihr lernt Magie bis zur Meteorstufe. Spätere Stufen neigen zur Magie.",
			L"Magia, resistência e PM crescem. Mesmo se a força sobe, você não é quem briga. Aprende magia até a escala de meteoros. Os níveis seguintes pendem para a magia.",
			L"Magie, weerstand en MP groeien. Zelfs als kracht stijgt, ben jij niet degene die vecht. Je leert magie tot meteoorschaal. Latere niveaus hellen naar magie.",
			L"Rosną magia, odporność i PM. Nawet jeśli siła rośnie, nie ty się bijesz. Uczysz się magii aż do meteorów. Dalsze poziomy idą w magię.",
			L"Büyü, direnç ve MP artar. Güç yükselse de yumruk atan rol sen değilsin. Meteor ölçeğine kadar büyü öğrenirsin. Sonraki seviyeler büyüye yaslanır.");
		break;
	case C_GLADIATOR:
		*a = LangPick14(
			L"物理の闘技士。斧と拳で隣の敵を叩き、力とHPは最上位。魔法は使わない。",
			L"A physical gladiator. You strike the foe beside you with axe and fist, and strength and HP are at the top. You do not use magic.",
			L"Gladiateur physique. Vous frappez l'ennemi adjacent à la hache et au poing, force et PV sont au sommet. Pas de magie.",
			L"Gladiatore fisico. Colpisci il nemico adiacente con ascia e pugno, forza e PS sono al vertice. Niente magia.",
			L"Gladiador físico. Golpeas al enemigo adyacente con hacha y puño, fuerza y PV están en la cima. No usas magia.",
			L"물리 투사. 도끼와 주먹으로 인접한 적을 치고, 힘과 HP는 최상위. 마법은 쓰지 않는다.",
			L"物理斗士。用斧和拳打相邻的敌人，力量和HP是最高一档。不用魔法。",
			L"مصارع جسدي. تضرب العدو المجاور بالفأس والقبضة، والقوة والصحة في القمة. لا سحر.",
			L"Физический гладиатор. Бьёте соседнего врага топором и кулаком, сила и ОЗ на самом верху. Магии нет.",
			L"Physischer Gladiator. Ihr schlagt den angrenzenden Feind mit Axt und Faust, Stärke und LP sind ganz oben. Keine Magie.",
			L"Gladiador físico. Você golpeia o inimigo adjacente com machado e punho, força e PV estão no topo. Não usa magia.",
			L"Fysieke gladiator. Je slaat de aangrenzende vijand met bijl en vuist, kracht en LP staan bovenaan. Geen magie.",
			L"Fizyczny gladiator. Bijesz sąsiada toporem i pięścią, siła i PŻ są na szczycie. Bez magii.",
			L"Fiziksel gladyatör. Balta ve yumrukla bitişik düşmanı vurursun, güç ve CP en üsttedir. Büyü kullanmazsın.");
		*b = LangPick14(
			L"力が大きく伸び、守備も狂戦士より厚い。魔防は期待しない。前に出て殴り切る。以後のレベルアップも力とHPに寄る。",
			L"Strength grows hard, and defense is thicker than a berserker. Do not expect resistance. You step forward and fight it out. Later levels lean on strength and HP.",
			L"La force monte fort, et la défense est plus épaisse qu'un berserker. N'attendez pas la résistance. Vous avancez et finissez au choc. Les niveaux suivants penchent vers force et PV.",
			L"La forza cresce molto, e la difesa è più spessa di un berserker. Non aspettarti la resistenza. Avanzi e chiudi a colpi. I livelli successivi pendono su forza e PS.",
			L"La fuerza crece mucho, y la defensa es más gruesa que la de un berserker. No esperes resistencia. Avanzas y lo cierras a golpes. Los niveles siguientes se inclinan a fuerza y PV.",
			L"힘이 크게 늘고, 수비도 광전사보다 두껍다. 마방은 기대하지 않는다. 앞에 나와 끝까지 친다. 이후 레벨업도 힘과 HP에 기운다.",
			L"力量大幅成长，防御也比狂战士厚。不要指望魔防。冲到前面打到底。之后的升级也偏向力量和HP。",
			L"القوة تنمو كثيرًا، والدفاع أسمك من المحارب الهائج. لا تتوقع المقاومة. تتقدم وتنهي بالضرب. المستويات اللاحقة تميل إلى القوة والصحة.",
			L"Сила сильно растёт, защита толще, чем у берсерка. Стойкости не ждите. Выходите вперёд и добиваете ударами. Дальше уровни клонятся к силе и ОЗ.",
			L"Die Stärke wächst stark, die Abwehr ist dicker als beim Berserker. Resistenz erwartet nicht. Ihr geht vor und schlagt es aus. Spätere Stufen neigen zu Stärke und LP.",
			L"A força cresce muito, e a defesa é mais grossa que a de um berserker. Não espere resistência. Você avança e fecha no golpe. Os níveis seguintes pendem para força e PV.",
			L"Kracht groeit hard, en verdediging is dikker dan bij een berserker. Verwacht geen weerstand. Je stapt naar voren en slaat het uit. Latere niveaus hellen naar kracht en LP.",
			L"Siła rośnie mocno, obrona jest grubsza niż u berserkera. Nie licz na odporność. Wychodzisz i dobijasz ciosami. Dalsze poziomy idą w siłę i PŻ.",
			L"Güç çok artar, savunma çılgın savaşçıdan kalındır. Büyü direnci bekleme. Öne çıkıp sonuna kadar vurursun. Sonraki seviyeler güç ve CP'ye yaslanır.");
		break;
	case C_SHADOW:
		*a = LangPick14(
			L"物理の影。短剣で隣の敵を背後から仕留める。魔法は使わない。",
			L"A physical shadow. You finish the foe beside you from behind with daggers. You do not use magic.",
			L"Ombre physique. Vous achevez l'ennemi adjacent par-derrière aux dagues. Pas de magie.",
			L"Ombra fisica. Finisci il nemico adiacente da dietro con i pugnali. Niente magia.",
			L"Sombra física. Rematas al enemigo adyacente por detrás con dagas. No usas magia.",
			L"물리의 그림자. 단검으로 인접한 적을 등 뒤에서 끝낸다. 마법은 쓰지 않는다.",
			L"物理之影。用短剑从背后了结相邻的敌人。不用魔法。",
			L"ظل جسدي. تنهي العدو المجاور من الخلف بالخناجر. لا سحر.",
			L"Физическая тень. Добиваете соседнего врага сзади кинжалами. Магии нет.",
			L"Physischer Schatten. Ihr erledigt den angrenzenden Feind von hinten mit Dolchen. Keine Magie.",
			L"Sombra física. Você acaba o inimigo adjacente por trás com adagas. Não usa magia.",
			L"Fysieke schaduw. Je maakt de aangrenzende vijand van achteren af met dolken. Geen magie.",
			L"Fizyczny cień. Kończysz sąsiada od tyłu sztyletami. Bez magii.",
			L"Fiziksel gölge. Bitişik düşmanı hançerle arkadan bitirirsin. Büyü kullanmazsın.");
		*b = LangPick14(
			L"素早さと技が伸び、必殺が高い。HPと守備は低い。長い殴り合いより、先に落とす。移動力が高い。以後のレベルアップも素早さに寄る。",
			L"Agility and dexterity grow, and critical hits are high. HP and defense are low. You drop the target first instead of a long brawl. Movement is high. Later levels lean on agility.",
			L"Agilité et adresse montent, les critiques sont hauts. PV et défense sont bas. Vous abattez la cible d'abord plutôt qu'une longue mêlée. Le déplacement est haut. Les niveaux suivants penchent vers l'agilité.",
			L"Agilità e destrezza crescono, i critici sono alti. PS e difesa sono bassi. Abbatti il bersaglio prima, invece di una rissa lunga. Il movimento è alto. I livelli successivi pendono sull'agilità.",
			L"Agilidad y destreza crecen, los críticos son altos. PV y defensa son bajos. Tumbas el objetivo primero, en vez de una pelea larga. El movimiento es alto. Los niveles siguientes se inclinan a la agilidad.",
			L"민첩과 기량이 늘고, 필살이 높다. HP와 수비는 낮다. 긴 난타보다 먼저 쓰러뜨린다. 이동력이 높다. 이후 레벨업도 민첩에 기운다.",
			L"敏捷和技巧成长，必杀高。HP和防御低。与其久战，不如先打倒。移动力高。之后的升级也偏向敏捷。",
			L"الرشاقة والبراعة تنموان، والضربة القاضية عالية. الصحة والدفاع منخفضان. تُسقط الهدف أولًا بدل قتال طويل. الحركة عالية. المستويات اللاحقة تميل إلى الرشاقة.",
			L"Растут ловкость и сноровка, крит высокий. ОЗ и защита низкие. Сначала роняете цель, а не тянете долгую рубку. Движение высокое. Дальше уровни клонятся к ловкости.",
			L"Beweglichkeit und Geschick wachsen, Krit ist hoch. LP und Abwehr sind niedrig. Ihr werft das Ziel zuerst, statt lange zu schlagen. Die Bewegung ist hoch. Spätere Stufen neigen zur Beweglichkeit.",
			L"Agilidade e destreza crescem, o crítico é alto. PV e defesa são baixos. Você derruba o alvo primeiro, em vez de uma briga longa. O movimento é alto. Os níveis seguintes pendem para a agilidade.",
			L"Behendigheid en vaardigheid groeien, kritiek is hoog. LP en verdediging zijn laag. Je laat het doel eerst vallen, in plaats van lang te vechten. Beweging is hoog. Latere niveaus hellen naar behendigheid.",
			L"Rosną zwinność i zręczność, kryt jest wysoki. PŻ i obrona są niskie. Najpierw zwalasz cel, zamiast długiej wymiany. Ruch jest wysoki. Dalsze poziomy idą w zwinność.",
			L"Çeviklik ve maharet artar, kritik yüksektir. CP ve savunma düşüktür. Uzun boğuşma yerine önce düşürürsün. Hareket yüksektir. Sonraki seviyeler çevikliğe yaslanır.");
		break;
	default:
		break;
	}
	if (*a) return;
	int fam = -1;
	if (cls >= C_JOB0 && cls < C_JOB0 + TAC_JOB_N) fam = kJobFam[cls - C_JOB0];
	switch (fam) {
	case 0:
		*a = LangPick14(
			L"物理職。剣で隣の敵を叩く前衛で、魔法はほとんど使わない。範囲魔法の代わりにはならない。",
			L"A physical class. A front liner who strikes the foe beside you with a sword, and barely uses magic. You do not replace area magic.",
			L"Classe physique. Un avant qui frappe l'ennemi adjacent à l'épée, et qui use à peine de magie. Vous ne remplacez pas la magie de zone.",
			L"Classe fisica. Un fronte che colpisce il nemico adiacente con la spada, e usa appena la magia. Non sostituisci la magia ad area.",
			L"Clase física. Un frente que golpea al enemigo adyacente con la espada, y apenas usa magia. No sustituyes la magia de área.",
			L"물리 직업. 검으로 인접한 적을 치는 전열이며, 마법은 거의 쓰지 않는다. 범위 마법의 대역은 되지 않는다.",
			L"物理职。用剑打相邻敌人的前排，几乎不用魔法。代替不了范围魔法。",
			L"وظيفة جسدية. مقدمة تضرب العدو المجاور بالسيف، وبالكاد تستخدم السحر. لا تغني عن سحر المساحة.",
			L"Физическая профессия. Передовой, который бьёт соседнего врага мечом и почти не использует магию. Площадную магию не заменяет.",
			L"Physische Klasse. Eine Front, die den angrenzenden Feind mit dem Schwert schlägt und kaum Magie nutzt. Flächenmagie ersetzt ihr nicht.",
			L"Classe física. Uma frente que golpeia o inimigo adjacente com a espada e mal usa magia. Você não substitui magia em área.",
			L"Fysieke klasse. Een voorhoede die de aangrenzende vijand met het zwaard slaat en nauwelijks magie gebruikt. Je vervangt geen gebiedsmagie.",
			L"Profesja fizyczna. Front, który bije sąsiada mieczem i prawie nie używa magii. Nie zastępujesz magii obszarowej.",
			L"Fiziksel sınıf. Kılıçla bitişik düşmanı vuran ön saf, büyüyü neredeyse kullanmaz. Alan büyüsünün yerini tutmaz.");
		*b = LangPick14(
			L"力・HP・守備が伸びる。単体を殴り倒す向き。以後のレベルアップもこの職の成長、つまり力と守備に寄る。",
			L"Strength, HP, and defense grow. You are built to drop single targets. Later levels follow this class, so they lean on strength and defense.",
			L"Force, PV et défense montent. Vous êtes fait pour abattre des cibles seules. Les niveaux suivants suivent cette classe, donc ils penchent vers force et défense.",
			L"Forza, PS e difesa crescono. Sei fatto per abbattere bersagli singoli. I livelli successivi seguono questa classe, quindi pendono su forza e difesa.",
			L"Fuerza, PV y defensa crecen. Estás hecho para tumbar objetivos sueltos. Los niveles siguientes siguen esta clase, así que se inclinan a fuerza y defensa.",
			L"힘·HP·수비가 늘어난다. 단체를 때려 눕히는 방향. 이후 레벨업도 이 직업의 성장, 곧 힘과 수비에 기운다.",
			L"力量、HP、防御成长。适合打倒单体。之后的升级也走此职的成长，也就是偏向力量和防御。",
			L"القوة والصحة والدفاع تنمو. أنت مبني لإسقاط الأهداف المنفردة. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى القوة والدفاع.",
			L"Растут сила, ОЗ и защита. Вы созданы валить одиночные цели. Дальнейшие уровни идут ростом этой профессии, то есть к силе и защите.",
			L"Stärke, LP und Abwehr wachsen. Ihr seid dafür da, Einzelziele niederzuschlagen. Spätere Stufen folgen dieser Klasse, also neigen sie zu Stärke und Abwehr.",
			L"Força, PV e defesa crescem. Você é feito para derrubar alvos sozinhos. Os níveis seguintes seguem esta classe, então pendem para força e defesa.",
			L"Kracht, LP en verdediging groeien. Je bent gemaakt om losse doelen neer te slaan. Latere niveaus volgen deze klasse, dus hellen ze naar kracht en verdediging.",
			L"Rosną siła, PŻ i obrona. Jesteś do zwalania pojedynczych celów. Dalsze poziomy idą wzrostem tej profesji, czyli w siłę i obronę.",
			L"Güç, CP ve savunma artar. Tek hedefleri devirmek için varsın. Sonraki seviyeler bu sınıfın büyümesini izler, yani güç ve savunmaya yaslanır.");
		break;
	case 1:
		*a = LangPick14(
			L"魔法職。杖の攻撃魔法が本分で、武器の打撃は期待しない。後列から範囲を撃つ。",
			L"A magic class. Attack spells from the staff are the work, and weapon blows are not the point. You cast areas from the back line.",
			L"Classe de magie. Les sorts d'attaque du bâton sont le métier, les coups d'arme ne sont pas le but. Vous lancez des zones depuis l'arrière.",
			L"Classe di magia. Le magie d'attacco del bastone sono il mestiere, i colpi d'arma non sono lo scopo. Lanci aree dal retro.",
			L"Clase de magia. Los conjuros de ataque del bastón son el oficio, los golpes de arma no son el fin. Lanzas áreas desde la retaguardia.",
			L"마법 직업. 지팡이의 공격 마법이 본분이고, 무기의 타격은 기대하지 않는다. 후열에서 범위를 쏜다.",
			L"魔法职。本分是杖的攻击魔法，不要指望武器打击。从后排放范围。",
			L"وظيفة سحر. سحر هجوم العصا هو العمل، وضربات السلاح ليست المقصود. ترمي المساحات من الصف الخلفي.",
			L"Магическая профессия. Работа — атакующая магия посоха, удары оружием не главное. Бьёте по площади из задней линии.",
			L"Magieklasse. Angriffszauber des Stabs sind die Arbeit, Waffenschläge sind nicht der Sinn. Ihr wirkt Flächen von hinten.",
			L"Classe de magia. Magias de ataque do cajado são o ofício, golpes de arma não são o ponto. Você lança áreas da retaguarda.",
			L"Magieklasse. Aanvalsspreuken van de staf zijn het werk, wapenslagen zijn niet het punt. Je werpt gebieden vanuit de achterhoede.",
			L"Profesja magii. Praca to atakująca magia laski, uderzenia bronią nie są celem. Rzucasz obszary z tyłu.",
			L"Büyü sınıfı. İş asanın saldırı büyüsüdür, silah vuruşu amaç değildir. Alanı arka saftan atarsın.");
		*b = LangPick14(
			L"魔力とMPが大きく伸び、HPと力は低い。前に出ると落ちやすい。以後のレベルアップもこの職の成長、つまり魔力に寄る。",
			L"Magic and MP grow hard, HP and strength stay low. The front line drops you. Later levels follow this class, so they lean on magic.",
			L"Magie et PM montent fort, PV et force restent bas. L'avant vous fait tomber. Les niveaux suivants suivent cette classe, donc ils penchent vers la magie.",
			L"Magia e PM crescono molto, PS e forza restano bassi. Il fronte ti fa cadere. I livelli successivi seguono questa classe, quindi pendono sulla magia.",
			L"Magia y PM crecen mucho, PV y fuerza se quedan bajos. El frente te tumba. Los niveles siguientes siguen esta clase, así que se inclinan a la magia.",
			L"마력과 MP가 크게 늘고, HP와 힘은 낮다. 앞에 서면 쓰러지기 쉽다. 이후 레벨업도 이 직업의 성장, 곧 마력에 기운다.",
			L"魔力和MP大幅成长，HP和力量低。站到前面容易倒下。之后的升级也走此职的成长，也就是偏向魔力。",
			L"السحر والطاقة ينموان كثيرًا، والصحة والقوة تبقيان منخفضتين. المقدمة تسقطك. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى السحر.",
			L"Магия и ОМ сильно растут, ОЗ и сила низкие. На передовой вы падаете. Дальнейшие уровни идут ростом этой профессии, то есть к магии.",
			L"Magie und MP wachsen stark, LP und Stärke bleiben niedrig. Vorn fallt ihr. Spätere Stufen folgen dieser Klasse, also neigen sie zur Magie.",
			L"Magia e PM crescem muito, PV e força ficam baixos. A frente derruba você. Os níveis seguintes seguem esta classe, então pendem para a magia.",
			L"Magie en MP groeien hard, LP en kracht blijven laag. Vooraan val je. Latere niveaus volgen deze klasse, dus hellen ze naar magie.",
			L"Magia i PM rosną mocno, PŻ i siła zostają niskie. Front cię powala. Dalsze poziomy idą wzrostem tej profesji, czyli w magię.",
			L"Büyü ve MP çok artar, CP ve güç düşük kalır. Öne çıkınca düşersin. Sonraki seviyeler bu sınıfın büyümesini izler, yani büyüye yaslanır.");
		break;
	case 2:
		*a = LangPick14(
			L"魔法職。回復と聖属性の攻撃で仲間を支える。殴って前に出る職ではない。",
			L"A magic class. You support allies with healing and holy attack. You are not the class that steps forward to swing.",
			L"Classe de magie. Vous soutenez les alliés par le soin et l'attaque sacrée. Vous n'êtes pas la classe qui avance pour frapper.",
			L"Classe di magia. Sostieni gli alleati con cura e attacco sacro. Non sei la classe che avanza per colpire.",
			L"Clase de magia. Apoyas a los aliados con curación y ataque sagrado. No eres la clase que avanza a golpear.",
			L"마법 직업. 회복과 성속성 공격으로 동료를 받친다. 때리며 앞에 서는 직업이 아니다.",
			L"魔法职。用恢复和圣属性攻击支援同伴。不是冲上去打的职。",
			L"وظيفة سحر. تدعم الحلفاء بالشفاء والهجوم المقدس. لست من يتقدم ليضرب.",
			L"Магическая профессия. Поддерживаете союзников лечением и святой атакой. Это не профессия, которая выходит бить.",
			L"Magieklasse. Ihr stützt Verbündete mit Heilung und heiligem Angriff. Nicht die Klasse, die vorgeht und schlägt.",
			L"Classe de magia. Você apoia aliados com cura e ataque sagrado. Não é a classe que avança para bater.",
			L"Magieklasse. Je steunt bondgenoten met heling en heilige aanval. Niet de klasse die naar voren stapt om te slaan.",
			L"Profesja magii. Wspierasz sojuszników leczeniem i świętym atakiem. To nie profesja, która wychodzi bić.",
			L"Büyü sınıfı. İyileştirme ve kutsal saldırıyla yoldaşları desteklersin. Öne çıkıp vuran sınıf değilsin.");
		*b = LangPick14(
			L"魔力・魔防・MPが伸び、力は低い。味方を立て直す後列。以後のレベルアップもこの職の成長、つまり魔力と魔防に寄る。",
			L"Magic, resistance, and MP grow, strength stays low. You are the back line that puts allies back up. Later levels follow this class, so they lean on magic and resistance.",
			L"Magie, résistance et PM montent, la force reste basse. Vous êtes l'arrière qui relève les alliés. Les niveaux suivants suivent cette classe, donc ils penchent vers magie et résistance.",
			L"Magia, resistenza e PM crescono, la forza resta bassa. Sei il retro che rialza gli alleati. I livelli successivi seguono questa classe, quindi pendono su magia e resistenza.",
			L"Magia, resistencia y PM crecen, la fuerza se queda baja. Eres la retaguardia que vuelve a levantar a los aliados. Los niveles siguientes siguen esta clase, así que se inclinan a magia y resistencia.",
			L"마력·마방·MP가 늘고, 힘은 낮다. 아군을 다시 세우는 후열. 이후 레벨업도 이 직업의 성장, 곧 마력과 마방에 기운다.",
			L"魔力、魔防、MP成长，力量低。是把同伴重新扶起的后排。之后的升级也走此职的成长，也就是偏向魔力和魔防。",
			L"السحر والمقاومة والطاقة تنمو، والقوة تبقى منخفضة. أنت الصف الخلفي الذي يعيد الحلفاء. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى السحر والمقاومة.",
			L"Растут магия, стойкость и ОМ, сила низкая. Вы задняя линия, которая поднимает союзников. Дальнейшие уровни идут ростом этой профессии, то есть к магии и стойкости.",
			L"Magie, Resistenz und MP wachsen, die Stärke bleibt niedrig. Ihr seid die Hinterreihe, die Verbündete wieder aufstellt. Spätere Stufen folgen dieser Klasse, also neigen sie zu Magie und Resistenz.",
			L"Magia, resistência e PM crescem, a força fica baixa. Você é a retaguarda que põe os aliados de pé. Os níveis seguintes seguem esta classe, então pendem para magia e resistência.",
			L"Magie, weerstand en MP groeien, kracht blijft laag. Jij bent de achterhoede die bondgenoten weer opzet. Latere niveaus volgen deze klasse, dus hellen ze naar magie en weerstand.",
			L"Rosną magia, odporność i PM, siła zostaje niska. Jesteś tylną linią, która stawia sojuszników. Dalsze poziomy idą wzrostem tej profesji, czyli w magię i odporność.",
			L"Büyü, direnç ve MP artar, güç düşük kalır. Yoldaşları yeniden kaldıran arka safsın. Sonraki seviyeler bu sınıfın büyümesini izler, yani büyü ve dirence yaslanır.");
		break;
	case 3:
		*a = LangPick14(
			L"物理職。近距離から中距離まで届き、必殺と回避が高い。魔法は使わない。",
			L"A physical class. You reach from close to mid range, and critical hits and evasion are high. You do not use magic.",
			L"Classe physique. Vous portez du proche au moyen, critiques et esquive sont hauts. Pas de magie.",
			L"Classe fisica. Arrivi dal vicino al medio, critici e schivata sono alti. Niente magia.",
			L"Clase física. Llegas de cerca a media distancia, críticos y evasión son altos. No usas magia.",
			L"물리 직업. 가까운 거리에서 중거리까지 닿고, 필살과 회피가 높다. 마법은 쓰지 않는다.",
			L"物理职。从近距离打到中距离，必杀和回避高。不用魔法。",
			L"وظيفة جسدية. تصل من القريب إلى المتوسط، والضربة القاضية والتفادي عاليان. لا سحر.",
			L"Физическая профессия. Бьёте от ближней до средней дистанции, крит и уклонение высоки. Магии нет.",
			L"Physische Klasse. Ihr reicht von nah bis mittel, Krit und Ausweichen sind hoch. Keine Magie.",
			L"Classe física. Você alcança do perto ao médio, crítico e esquiva são altos. Não usa magia.",
			L"Fysieke klasse. Je reikt van dichtbij tot middellang, kritiek en ontwijken zijn hoog. Geen magie.",
			L"Profesja fizyczna. Sięgasz od bliska do średniego dystansu, kryt i unik są wysokie. Bez magii.",
			L"Fiziksel sınıf. Yakından orta menzile ulaşırsın, kritik ve kaçınma yüksektir. Büyü kullanmazsın.");
		*b = LangPick14(
			L"素早さ・技・運が伸びる。HPと守備は低く、仕留めて下がる動き。以後のレベルアップもこの職の成長、つまり素早さと技に寄る。",
			L"Agility, dexterity, and luck grow. HP and defense are low, so you kill and fall back. Later levels follow this class, so they lean on agility and dexterity.",
			L"Agilité, adresse et chance montent. PV et défense sont bas, donc vous tuez et reculez. Les niveaux suivants suivent cette classe, donc ils penchent vers agilité et adresse.",
			L"Agilità, destrezza e fortuna crescono. PS e difesa sono bassi, quindi uccidi e arretri. I livelli successivi seguono questa classe, quindi pendono su agilità e destrezza.",
			L"Agilidad, destreza y suerte crecen. PV y defensa son bajos, así que matas y retrocedes. Los niveles siguientes siguen esta clase, así que se inclinan a agilidad y destreza.",
			L"민첩·기량·운이 늘어난다. HP와 수비는 낮아, 죽이고 빠지는 움직임. 이후 레벨업도 이 직업의 성장, 곧 민첩과 기량에 기운다.",
			L"敏捷、技巧、运气成长。HP和防御低，所以杀掉再撤。之后的升级也走此职的成长，也就是偏向敏捷和技巧。",
			L"الرشاقة والبراعة والحظ تنمو. الصحة والدفاع منخفضان، فتقتل ثم تتراجع. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى الرشاقة والبراعة.",
			L"Растут ловкость, сноровка и удача. ОЗ и защита низкие, поэтому убиваете и отходите. Дальнейшие уровни идут ростом этой профессии, то есть к ловкости и сноровке.",
			L"Beweglichkeit, Geschick und Glück wachsen. LP und Abwehr sind niedrig, also tötet ihr und geht zurück. Spätere Stufen folgen dieser Klasse, also neigen sie zu Beweglichkeit und Geschick.",
			L"Agilidade, destreza e sorte crescem. PV e defesa são baixos, então você mata e recua. Os níveis seguintes seguem esta classe, então pendem para agilidade e destreza.",
			L"Behendigheid, vaardigheid en geluk groeien. LP en verdediging zijn laag, dus je doodt en stapt terug. Latere niveaus volgen deze klasse, dus hellen ze naar behendigheid en vaardigheid.",
			L"Rosną zwinność, zręczność i szczęście. PŻ i obrona są niskie, więc zabijasz i odchodzisz. Dalsze poziomy idą wzrostem tej profesji, czyli w zwinność i zręczność.",
			L"Çeviklik, maharet ve şans artar. CP ve savunma düşüktür, öldürüp geri çekilirsin. Sonraki seviyeler bu sınıfın büyümesini izler, yani çeviklik ve maharete yaslanır.");
		break;
	case 4:
		*a = LangPick14(
			L"物理職の飛行。山以外の地形を越え、隣の敵を剣で叩く。魔法は本業ではない。範囲魔法は持たない。",
			L"A physical flyer. You cross terrain other than mountains and strike the foe beside you with a sword. Magic is not the job. You have no area magic.",
			L"Volant physique. Vous franchissez le terrain sauf les montagnes et frappez l'ennemi adjacent à l'épée. La magie n'est pas le métier. Pas de magie de zone.",
			L"Volante fisico. Attraversi il terreno tranne le montagne e colpisci il nemico adiacente con la spada. La magia non è il mestiere. Niente magia ad area.",
			L"Volador físico. Cruzas el terreno salvo las montañas y golpeas al enemigo adyacente con la espada. La magia no es el oficio. No tienes magia de área.",
			L"물리 직업의 비행. 산 이외의 지형을 넘고, 인접한 적을 검으로 친다. 마법이 본업은 아니다. 범위 마법은 없다.",
			L"物理职的飞行。越过山地以外的地形，用剑打相邻的敌人。魔法不是本职。没有范围魔法。",
			L"طائر جسدي. تتجاوز التضاريس عدا الجبال وتضرب العدو المجاور بالسيف. السحر ليس المهنة. لا سحر مساحة.",
			L"Физический летун. Пересекаете местность, кроме гор, и бьёте соседнего врага мечом. Магия не основная работа. Площадной магии нет.",
			L"Physischer Flieger. Ihr überquert Gelände außer Bergen und schlagt den angrenzenden Feind mit dem Schwert. Magie ist nicht die Arbeit. Keine Flächenmagie.",
			L"Voador físico. Você atravessa o terreno, exceto montanhas, e golpeia o inimigo adjacente com a espada. Magia não é o ofício. Não há magia em área.",
			L"Fysieke vlieger. Je kruist terrein behalve bergen en slaat de aangrenzende vijand met het zwaard. Magie is niet het vak. Geen gebiedsmagie.",
			L"Fizyczny lotnik. Pokonujesz teren poza górami i bijesz sąsiada mieczem. Magia nie jest fachem. Nie ma magii obszarowej.",
			L"Fiziksel uçucu. Dağlar dışında araziyi geçip bitişik düşmanı kılıçla vurursun. Büyü asıl iş değildir. Alan büyüsü yoktur.");
		*b = LangPick14(
			L"力・HP・守備が高い。壁と溶岩を越えて先に届く前衛。以後のレベルアップもこの職の成長、つまり力と守備に寄る。",
			L"Strength, HP, and defense are high. You are the front liner who arrives first across walls and lava. Later levels follow this class, so they lean on strength and defense.",
			L"Force, PV et défense sont hauts. Vous êtes l'avant qui arrive le premier au-delà des murs et de la lave. Les niveaux suivants suivent cette classe, donc ils penchent vers force et défense.",
			L"Forza, PS e difesa sono alti. Sei il fronte che arriva primo oltre muri e lava. I livelli successivi seguono questa classe, quindi pendono su forza e difesa.",
			L"Fuerza, PV y defensa son altos. Eres el frente que llega primero al otro lado de muros y lava. Los niveles siguientes siguen esta clase, así que se inclinan a fuerza y defensa.",
			L"힘·HP·수비가 높다. 벽과 용암을 넘어 먼저 닿는 전열. 이후 레벨업도 이 직업의 성장, 곧 힘과 수비에 기운다.",
			L"力量、HP、防御高。是越过墙和熔岩先到的前排。之后的升级也走此职的成长，也就是偏向力量和防御。",
			L"القوة والصحة والدفاع عالية. أنت المقدمة التي تصل أولًا عبر الجدران والحمم. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى القوة والدفاع.",
			L"Сила, ОЗ и защита высоки. Вы передовой, который первым проходит стены и лаву. Дальнейшие уровни идут ростом этой профессии, то есть к силе и защите.",
			L"Stärke, LP und Abwehr sind hoch. Ihr seid die Front, die zuerst über Mauern und Lava kommt. Spätere Stufen folgen dieser Klasse, also neigen sie zu Stärke und Abwehr.",
			L"Força, PV e defesa são altos. Você é a frente que chega primeiro além de muros e lava. Os níveis seguintes seguem esta classe, então pendem para força e defesa.",
			L"Kracht, LP en verdediging zijn hoog. Jij bent de voorhoede die als eerste over muren en lava komt. Latere niveaus volgen deze klasse, dus hellen ze naar kracht en verdediging.",
			L"Siła, PŻ i obrona są wysokie. Jesteś frontem, który pierwszy przechodzi mury i lawę. Dalsze poziomy idą wzrostem tej profesji, czyli w siłę i obronę.",
			L"Güç, CP ve savunma yüksektir. Duvar ve lavın ötesine ilk varan ön safsın. Sonraki seviyeler bu sınıfın büyümesini izler, yani güç ve savunmaya yaslanır.");
		break;
	case 5:
		*a = LangPick14(
			L"魔法職。攻撃魔法と回復と変換を杖で使う。純粋な前衛ではなく、薬と属性で味方を支え敵を削る。",
			L"A magic class. You use attack spells, healing, and transmutation with a staff. Not a pure front liner: you support allies with medicine and elements, and you wear foes down.",
			L"Classe de magie. Vous usez de sorts d'attaque, de soin et de transmutation au bâton. Pas un avant pur : vous soutenez les alliés par la médecine et les éléments, et vous usez les ennemis.",
			L"Classe di magia. Usi magie d'attacco, cura e trasmutazione col bastone. Non un fronte puro: sostieni gli alleati con medicina ed elementi, e logori i nemici.",
			L"Clase de magia. Usas conjuros de ataque, curación y transmutación con el bastón. No un frente puro: apoyas a los aliados con medicina y elementos, y desgasta a los enemigos.",
			L"마법 직업. 공격 마법과 회복과 변환을 지팡이로 쓴다. 순수한 전열이 아니라, 약과 속성으로 아군을 돕고 적을 깎는다.",
			L"魔法职。用杖施放攻击魔法、恢复和转化。不是纯前排，而是用药和属性支援同伴、削减敌人。",
			L"وظيفة سحر. تستخدم سحر الهجوم والشفاء والتحويل بالعصا. لست مقدمة خالصة: تدعم الحلفاء بالدواء والعناصر وتنهك الأعداء.",
			L"Магическая профессия. Атака, лечение и превращение посохом. Не чистый передовой: поддерживаете союзников зельем и стихиями и стачиваете врагов.",
			L"Magieklasse. Ihr wirkt Angriff, Heilung und Verwandlung mit dem Stab. Keine reine Front: ihr stützt Verbündete mit Medizin und Elementen und reibt Feinde auf.",
			L"Classe de magia. Você usa ataque, cura e transmutação com o cajado. Não uma frente pura: apoia aliados com medicina e elementos e desgasta inimigos.",
			L"Magieklasse. Je gebruikt aanval, heling en transmutatie met de staf. Geen pure voorhoede: je steunt bondgenoten met medicijn en elementen en slijt vijanden.",
			L"Profesja magii. Używasz ataku, leczenia i transmutacji laską. Nie czysty front: wspierasz sojuszników lekiem i żywiołami i ścierasz wrogów.",
			L"Büyü sınıfı. Asayla saldırı, iyileştirme ve dönüşüm kullanırsın. Saf ön saf değilsin: ilaç ve elementle yoldaşları destekler, düşmanı eritirsin.");
		*b = LangPick14(
			L"魔力とMPが伸び、力は少しあるが殴り役ではない。HPは低め。以後のレベルアップもこの職の成長、つまり魔力に寄る。",
			L"Magic and MP grow. Strength is a little there, but you are not the brawler. HP stays low. Later levels follow this class, so they lean on magic.",
			L"Magie et PM montent. La force est un peu là, mais vous n'êtes pas le bagarreur. Les PV restent bas. Les niveaux suivants suivent cette classe, donc ils penchent vers la magie.",
			L"Magia e PM crescono. La forza c'è un po', ma non sei tu a fare la rissa. I PS restano bassi. I livelli successivi seguono questa classe, quindi pendono sulla magia.",
			L"Magia y PM crecen. Hay un poco de fuerza, pero no eres quien pelea a golpes. Los PV se quedan bajos. Los niveles siguientes siguen esta clase, así que se inclinan a la magia.",
			L"마력과 MP가 늘고, 힘은 조금 있지만 때리는 역할은 아니다. HP는 낮은 편. 이후 레벨업도 이 직업의 성장, 곧 마력에 기운다.",
			L"魔力和MP成长，力量有一点，但不是殴打的角色。HP偏低。之后的升级也走此职的成长，也就是偏向魔力。",
			L"السحر والطاقة ينموان. القوة موجودة قليلًا، لكنك لست من يتشاجر. الصحة تبقى منخفضة. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى السحر.",
			L"Растут магия и ОМ. Сила немного есть, но вы не тот, кто дерётся. ОЗ низкие. Дальнейшие уровни идут ростом этой профессии, то есть к магии.",
			L"Magie und MP wachsen. Etwas Stärke ist da, aber ihr seid nicht der Raufbold. Die LP bleiben niedrig. Spätere Stufen folgen dieser Klasse, also neigen sie zur Magie.",
			L"Magia e PM crescem. Há um pouco de força, mas você não é quem briga. Os PV ficam baixos. Os níveis seguintes seguem esta classe, então pendem para a magia.",
			L"Magie en MP groeien. Er is wat kracht, maar jij bent niet degene die vecht. LP blijven laag. Latere niveaus volgen deze klasse, dus hellen ze naar magie.",
			L"Rosną magia i PM. Siły jest trochę, ale nie ty się bijesz. PŻ zostają niskie. Dalsze poziomy idą wzrostem tej profesji, czyli w magię.",
			L"Büyü ve MP artar. Biraz güç vardır ama yumruk atan rol sen değilsin. CP düşük kalır. Sonraki seviyeler bu sınıfın büyümesini izler, yani büyüye yaslanır.");
		break;
	case 6:
		*a = LangPick14(
			L"物理職。斧や拳で隣の敵を殴る前衛で、魔法は使わない。力とHPは最上位まで伸びる。",
			L"A physical class. A front liner who strikes the foe beside you with axe or fist, and does not use magic. Strength and HP grow to the top.",
			L"Classe physique. Un avant qui frappe l'ennemi adjacent à la hache ou au poing, sans magie. Force et PV montent jusqu'au sommet.",
			L"Classe fisica. Un fronte che colpisce il nemico adiacente con ascia o pugno, senza magia. Forza e PS crescono fino al vertice.",
			L"Clase física. Un frente que golpea al enemigo adyacente con hacha o puño, sin magia. Fuerza y PV crecen hasta la cima.",
			L"물리 직업. 도끼나 주먹으로 인접한 적을 치는 전열이며, 마법은 쓰지 않는다. 힘과 HP는 최상위까지 늘어난다.",
			L"物理职。用斧或拳打相邻敌人的前排，不用魔法。力量和HP会成长到最高一档。",
			L"وظيفة جسدية. مقدمة تضرب العدو المجاور بالفأس أو القبضة، بلا سحر. القوة والصحة تنموان حتى القمة.",
			L"Физическая профессия. Передовой, который бьёт соседнего врага топором или кулаком, без магии. Сила и ОЗ растут до самого верха.",
			L"Physische Klasse. Eine Front, die den angrenzenden Feind mit Axt oder Faust schlägt, ohne Magie. Stärke und LP wachsen bis ganz nach oben.",
			L"Classe física. Uma frente que golpeia o inimigo adjacente com machado ou punho, sem magia. Força e PV crescem até o topo.",
			L"Fysieke klasse. Een voorhoede die de aangrenzende vijand met bijl of vuist slaat, zonder magie. Kracht en LP groeien tot bovenaan.",
			L"Profesja fizyczna. Front, który bije sąsiada toporem albo pięścią, bez magii. Siła i PŻ rosną aż na szczyt.",
			L"Fiziksel sınıf. Balta ya da yumrukla bitişik düşmanı vuran ön saf, büyü kullanmaz. Güç ve CP en üste kadar artar.");
		*b = LangPick14(
			L"守備も厚いが、魔防は期待できない。魔法を食らうと痛い。前に出て殴り切る。以後のレベルアップもこの職の成長、つまり力とHPに寄る。",
			L"Defense is thick too, but do not expect resistance. Magic hits hurt. You step forward and fight it out. Later levels follow this class, so they lean on strength and HP.",
			L"La défense est épaisse aussi, mais n'attendez pas la résistance. La magie fait mal. Vous avancez et finissez au choc. Les niveaux suivants suivent cette classe, donc ils penchent vers force et PV.",
			L"Anche la difesa è spessa, ma non aspettarti la resistenza. La magia fa male. Avanzi e chiudi a colpi. I livelli successivi seguono questa classe, quindi pendono su forza e PS.",
			L"La defensa también es gruesa, pero no esperes resistencia. La magia duele. Avanzas y lo cierras a golpes. Los niveles siguientes siguen esta clase, así que se inclinan a fuerza y PV.",
			L"수비도 두껍지만, 마방은 기대할 수 없다. 마법을 맞으면 아프다. 앞에 나와 끝까지 친다. 이후 레벨업도 이 직업의 성장, 곧 힘과 HP에 기운다.",
			L"防御也厚，但不要指望魔防。吃到魔法会疼。冲到前面打到底。之后的升级也走此职的成长，也就是偏向力量和HP。",
			L"الدفاع سميك أيضًا، لكن لا تتوقع المقاومة. السحر يؤلم. تتقدم وتنهي بالضرب. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى القوة والصحة.",
			L"Защита тоже толстая, но стойкости не ждите. Магия больно бьёт. Выходите вперёд и добиваете. Дальнейшие уровни идут ростом этой профессии, то есть к силе и ОЗ.",
			L"Die Abwehr ist auch dick, aber Resistenz erwartet nicht. Magie tut weh. Ihr geht vor und schlagt es aus. Spätere Stufen folgen dieser Klasse, also neigen sie zu Stärke und LP.",
			L"A defesa também é grossa, mas não espere resistência. Magia dói. Você avança e fecha no golpe. Os níveis seguintes seguem esta classe, então pendem para força e PV.",
			L"Verdediging is ook dik, maar verwacht geen weerstand. Magie doet pijn. Je stapt naar voren en slaat het uit. Latere niveaus volgen deze klasse, dus hellen ze naar kracht en LP.",
			L"Obrona też jest gruba, ale nie licz na odporność. Magia boli. Wychodzisz i dobijasz. Dalsze poziomy idą wzrostem tej profesji, czyli w siłę i PŻ.",
			L"Savunma da kalındır ama büyü direnci bekleme. Büyü acıtır. Öne çıkıp sonuna kadar vurursun. Sonraki seviyeler bu sınıfın büyümesini izler, yani güç ve CP'ye yaslanır.");
		break;
	case 7:
		*a = LangPick14(
			L"物理職。短剣で隣を狙い、必殺で先に落とす。魔法は使わない。長い殴り合いは不利。",
			L"A physical class. You aim at the foe beside you with daggers and drop them first with a critical hit. You do not use magic. A long brawl is against you.",
			L"Classe physique. Vous visez l'ennemi adjacent aux dagues et l'abattez d'abord par un critique. Pas de magie. Une mêlée longue vous est contraire.",
			L"Classe fisica. Punti il nemico adiacente con i pugnali e lo abbatti prima con un critico. Niente magia. Una rissa lunga ti è contraria.",
			L"Clase física. Apuntas al enemigo adyacente con dagas y lo tumbas primero con un crítico. No usas magia. Una pelea larga va en tu contra.",
			L"물리 직업. 단검으로 인접을 노리고, 필살로 먼저 쓰러뜨린다. 마법은 쓰지 않는다. 긴 난타는 불리하다.",
			L"物理职。用短剑打相邻的敌人，靠必杀先打倒。不用魔法。久战不利。",
			L"وظيفة جسدية. تصيب العدو المجاور بالخناجر وتُسقطه أولًا بضربة قاضية. لا سحر. القتال الطويل ضدك.",
			L"Физическая профессия. Бьёте соседнего врага кинжалами и роняете его первым критическим ударом. Магии нет. Долгая рубка вам невыгодна.",
			L"Physische Klasse. Ihr zielt mit Dolchen auf den angrenzenden Feind und werft ihn zuerst mit einem Krit. Keine Magie. Ein langer Schlagabtausch ist gegen euch.",
			L"Classe física. Você mira o inimigo adjacente com adagas e o derruba primeiro com um crítico. Não usa magia. Uma briga longa joga contra você.",
			L"Fysieke klasse. Je mikt met dolken op de aangrenzende vijand en laat hem eerst vallen met een kritiek. Geen magie. Een lang gevecht is tegen je.",
			L"Profesja fizyczna. Celujesz sztyletami w sąsiada i zwalasz go pierwszy ciosem krytycznym. Bez magii. Długa wymiana jest przeciw tobie.",
			L"Fiziksel sınıf. Hançerle bitişik düşmanı hedef alıp kritikle önce düşürürsün. Büyü kullanmazsın. Uzun boğuşma aleyhinedir.");
		*b = LangPick14(
			L"素早さと技が伸び、HPと守備は低い。移動力は高い。以後のレベルアップもこの職の成長、つまり素早さに寄る。",
			L"Agility and dexterity grow, HP and defense stay low. Movement is high. Later levels follow this class, so they lean on agility.",
			L"Agilité et adresse montent, PV et défense restent bas. Le déplacement est haut. Les niveaux suivants suivent cette classe, donc ils penchent vers l'agilité.",
			L"Agilità e destrezza crescono, PS e difesa restano bassi. Il movimento è alto. I livelli successivi seguono questa classe, quindi pendono sull'agilità.",
			L"Agilidad y destreza crecen, PV y defensa se quedan bajos. El movimiento es alto. Los niveles siguientes siguen esta clase, así que se inclinan a la agilidad.",
			L"민첩과 기량이 늘고, HP와 수비는 낮다. 이동력은 높다. 이후 레벨업도 이 직업의 성장, 곧 민첩에 기운다.",
			L"敏捷和技巧成长，HP和防御低。移动力高。之后的升级也走此职的成长，也就是偏向敏捷。",
			L"الرشاقة والبراعة تنموان، والصحة والدفاع يبقيان منخفضين. الحركة عالية. المستويات اللاحقة تتبع هذه الوظيفة، فتميل إلى الرشاقة.",
			L"Растут ловкость и сноровка, ОЗ и защита низкие. Движение высокое. Дальнейшие уровни идут ростом этой профессии, то есть к ловкости.",
			L"Beweglichkeit und Geschick wachsen, LP und Abwehr bleiben niedrig. Die Bewegung ist hoch. Spätere Stufen folgen dieser Klasse, also neigen sie zur Beweglichkeit.",
			L"Agilidade e destreza crescem, PV e defesa ficam baixos. O movimento é alto. Os níveis seguintes seguem esta classe, então pendem para a agilidade.",
			L"Behendigheid en vaardigheid groeien, LP en verdediging blijven laag. Beweging is hoog. Latere niveaus volgen deze klasse, dus hellen ze naar behendigheid.",
			L"Rosną zwinność i zręczność, PŻ i obrona zostają niskie. Ruch jest wysoki. Dalsze poziomy idą wzrostem tej profesji, czyli w zwinność.",
			L"Çeviklik ve maharet artar, CP ve savunma düşük kalır. Hareket yüksektir. Sonraki seviyeler bu sınıfın büyümesini izler, yani çevikliğe yaslanır.");
		break;
	default:
		break;
	}
}
static void EvoKind(const SkillDef& sd, wchar_t* dst, int cap)
{
	const wchar_t* el = NULL;
	if (sd.element >= EL_FIRE && sd.element <= EL_POISON) el = ElemWord(sd.element);
	const wchar_t* core = NULL;
	int plain = 0;
	if (sd.kind == SK_HEAL && sd.effect == EF_RAISE)
		core = LangPick14(L"蘇生", L"a raise", L"un rappel", L"un richiamo", L"un alzar", L"소생", L"复苏", L"إحياء", L"воскрешение", L"erwecken", L"reviver", L"oprichten", L"wskrzeszenie", L"diriltme");
	else if (sd.kind == SK_HEAL) {
		plain = 1;
		core = LangPick14(L"回復", L"healing", L"soin", L"cura", L"curación", L"회복", L"恢复", L"شفاء", L"лечение", L"Heilung", L"cura", L"heling", L"leczenie", L"iyileştirme");
	} else if (sd.kind == SK_BUFF) {
		if (sd.effect == EF_BUFF_DEF) core = LangPick14(L"防御を上げる補助", L"a defense buff", L"un renfort de défense", L"un rinforzo di difesa", L"un refuerzo de defensa", L"방어를 올리는 보조", L"提升防御的辅助", L"دعم يرفع الدفاع", L"усиление защиты", L"ein Abwehrbonus", L"um reforço de defesa", L"een verdedigingsbuff", L"wzmocnienie obrony", L"savunma desteği");
		else if (sd.effect == EF_BUFF_AGI) core = LangPick14(L"素早さを上げる補助", L"an agility buff", L"un renfort d'agilité", L"un rinforzo di agilità", L"un refuerzo de agilidad", L"민첩을 올리는 보조", L"提升敏捷的辅助", L"دعم يرفع الرشاقة", L"усиление ловкости", L"ein Beweglichkeitsbonus", L"um reforço de agilidade", L"een behendigheidsbuff", L"wzmocnienie zwinności", L"çeviklik desteği");
		else if (sd.effect == EF_BUFF_ATK) core = LangPick14(L"攻撃を上げる補助", L"an attack buff", L"un renfort d'attaque", L"un rinforzo d'attacco", L"un refuerzo de ataque", L"공격을 올리는 보조", L"提升攻击的辅助", L"دعم يرفع الهجوم", L"усиление атаки", L"ein Angriffsbonus", L"um reforço de ataque", L"een aanvalsbuff", L"wzmocnienie ataku", L"saldırı desteği");
		else core = LangPick14(L"補助", L"a buff", L"un soutien", L"un sostegno", L"un apoyo", L"보조", L"辅助", L"دعم", L"поддержка", L"eine Stärkung", L"um apoio", L"een steun", L"wsparcie", L"destek");
	} else if (sd.kind == SK_PHYS && sd.effect == EF_POISON)
		core = LangPick14(L"毒の物理", L"poisonous physical", L"physique empoisonné", L"fisico avvelenato", L"físico venenoso", L"독의 물리", L"带毒的物理", L"جسدي سام", L"ядовитая физика", L"giftig physisch", L"físico venenoso", L"giftig fysiek", L"zatrute fizyczne", L"zehirli fiziksel");
	else if (sd.kind == SK_PHYS && sd.effect == EF_IGNDEF)
		core = LangPick14(L"防御無視の物理", L"physical that ignores defense", L"physique qui ignore la défense", L"fisico che ignora la difesa", L"físico que ignora la defensa", L"방어를 무시하는 물리", L"无视防御的物理", L"جسدي يتجاهل الدفاع", L"физика сквозь защиту", L"physisch, ignoriert Abwehr", L"físico que ignora defesa", L"fysiek dat verdediging negeert", L"fizyczne ignorujące obronę", L"savunmayı yok sayan fiziksel");
	else if (sd.kind == SK_PHYS) {
		plain = 1;
		core = LangPick14(L"物理", L"physical", L"physique", L"fisico", L"físico", L"물리", L"物理", L"جسدي", L"физика", L"physisch", L"físico", L"fysiek", L"fizyczne", L"fiziksel");
	} else if (sd.kind == SK_MAGIC && sd.effect == EF_SLOW)
		core = LangPick14(L"鈍足の魔法", L"slowing magic", L"magie de lenteur", L"magia di lentezza", L"magia de lentitud", L"둔화의 마법", L"迟缓的魔法", L"سحر البطء", L"магия замедления", L"verlangsamende Magie", L"magia de lentidão", L"vertragende magie", L"magia spowolnienia", L"yavaşlatan büyü");
	else if (sd.kind == SK_MAGIC && sd.effect == EF_STUN)
		core = LangPick14(L"スタンの魔法", L"stunning magic", L"magie d'étourdissement", L"magia di stordimento", L"magia de aturdimiento", L"기절의 마법", L"硬直的魔法", L"سحر الصعقة", L"магия оглушения", L"betäubende Magie", L"magia de atordoar", L"verdovende magie", L"magia ogłuszenia", L"sersemleten büyü");
	else if (sd.kind == SK_MAGIC && sd.effect == EF_DEFDOWN)
		core = LangPick14(L"防御を下げる魔法", L"magic that lowers defense", L"magie qui baisse la défense", L"magia che abbassa la difesa", L"magia que baja la defensa", L"방어를 내리는 마법", L"降低防御的魔法", L"سحر يخفض الدفاع", L"магия, снижающая защиту", L"Magie, die Abwehr senkt", L"magia que baixa a defesa", L"magie die verdediging verlaagt", L"magia obniżająca obronę", L"savunmayı düşüren büyü");
	else if (sd.kind == SK_MAGIC) {
		plain = 1;
		core = LangPick14(L"魔法", L"magic", L"magie", L"magia", L"magia", L"마법", L"魔法", L"سحر", L"магия", L"Magie", L"magia", L"magie", L"magia", L"büyü");
	} else
		core = LangPick14(L"技", L"a skill", L"une technique", L"una tecnica", L"una técnica", L"기술", L"技能", L"مهارة", L"умение", L"eine Fertigkeit", L"uma técnica", L"een vaardigheid", L"umiejętność", L"beceri");
	if (!core) core = L"";
	if (plain && el) {
		const wchar_t* fmt = LangPick14(L"%sの%s", L"%s %s", L"%s, %s", L"%s, %s", L"%s, %s", L"%s %s", L"%s的%s", L"%s %s", L"%s, %s", L"%s-%s", L"%s, %s", L"%s %s", L"%s, %s", L"%s %s");
		_snwprintf_s(dst, cap, _TRUNCATE, fmt, el, core);
	} else wcscpy_s(dst, cap, core);
}
static void EvoWhere(const SkillDef& sd, wchar_t* dst, int cap)
{
	if (sd.rMax < 0) {
		if (sd.hits > 1) {
			const wchar_t* fmt = LangPick14(
				L"武器の間合いで%d回当たる", L"hits %d times at weapon range", L"touche %d fois à portée d'arme", L"colpisce %d volte a portata d'arma", L"golpea %d veces a alcance de arma",
				L"무기 사거리에서 %d회 맞는다", L"以武器距离打中%d次", L"يصيب %d مرات بمدى السلاح", L"бьёт %d раза на дистанции оружия", L"trifft %d-mal auf Waffenreichweite",
				L"acerta %d vezes no alcance da arma", L"raakt %d keer op wapenbereik", L"trafia %d razy na zasięgu broni", L"silah menzilinde %d kez vurur");
			_snwprintf_s(dst, cap, _TRUNCATE, fmt, sd.hits);
		} else wcscpy_s(dst, cap, LangPick14(
			L"武器の間合い", L"at weapon range", L"à portée d'arme", L"a portata d'arma", L"a alcance de arma",
			L"무기 사거리", L"以武器距离", L"بمدى السلاح", L"на дистанции оружия", L"auf Waffenreichweite",
			L"no alcance da arma", L"op wapenbereik", L"na zasięgu broni", L"silah menzilinde"));
		return;
	}
	int a0 = sd.rMin < 0 ? 0 : sd.rMin;
	int a1 = sd.rMax;
	if (sd.shape == SH_LINE && a1 > 1) {
		if (sd.hits > 1) {
			const wchar_t* fmt = LangPick14(
				L"直線で%d〜%dマス、%d回当たる", L"a line from %d to %d tiles, %d hits", L"une ligne de %d à %d cases, %d coups", L"una linea da %d a %d caselle, %d colpi", L"una línea de %d a %d casillas, %d golpes",
				L"직선으로 %d〜%d칸, %d회", L"直线%d到%d格，打中%d次", L"خط من %d إلى %d، %d ضربات", L"линия с %d по %d клеток, %d удара", L"eine Linie von %d bis %d Felder, %d Treffer",
				L"uma linha de %d a %d casas, %d golpes", L"een lijn van %d tot %d vakken, %d treffers", L"linia od %d do %d pól, %d uderzenia", L"%d ile %d kare düz çizgi, %d vuruş");
			_snwprintf_s(dst, cap, _TRUNCATE, fmt, a0, a1, sd.hits);
		} else {
			const wchar_t* fmt = LangPick14(
				L"直線で%d〜%dマス", L"a line from %d to %d tiles", L"une ligne de %d à %d cases", L"una linea da %d a %d caselle", L"una línea de %d a %d casillas",
				L"직선으로 %d〜%d칸", L"直线%d到%d格", L"خط من %d إلى %d", L"линия с %d по %d клеток", L"eine Linie von %d bis %d Felder",
				L"uma linha de %d a %d casas", L"een lijn van %d tot %d vakken", L"linia od %d do %d pól", L"%d ile %d kare düz çizgi");
			_snwprintf_s(dst, cap, _TRUNCATE, fmt, a0, a1);
		}
		return;
	}
	if (sd.shape == SH_FAN && sd.area > 0) {
		const wchar_t* fmt = LangPick14(
			L"扇で半径%d、%d〜%dマス", L"a fan of radius %d, tiles %d to %d", L"un éventail de rayon %d, cases %d à %d", L"un ventaglio di raggio %d, caselle %d-%d", L"un abanico de radio %d, casillas %d a %d",
			L"부채꼴 반경 %d, %d〜%d칸", L"扇形半径%d，%d到%d格", L"مروحة نصف قطر %d، من %d إلى %d", L"веер радиуса %d, клетки %d–%d", L"ein Fächer mit Radius %d, Felder %d bis %d",
			L"um leque de raio %d, casas %d a %d", L"een waaier met straal %d, vakken %d tot %d", L"wachlarz o promieniu %d, pola %d–%d", L"yarıçap %d yelpaze, %d-%d kare");
		_snwprintf_s(dst, cap, _TRUNCATE, fmt, sd.area, a0, a1);
		return;
	}
	if (sd.area > 0) {
		if (sd.hits > 1) {
			const wchar_t* fmt = LangPick14(
				L"半径%d、%d〜%dマス、%d回当たる", L"radius %d, tiles %d to %d, %d hits", L"rayon %d, cases %d à %d, %d coups", L"raggio %d, caselle %d-%d, %d colpi", L"radio %d, casillas %d a %d, %d golpes",
				L"반경 %d, %d〜%d칸, %d회", L"半径%d，%d到%d格，打中%d次", L"نصف قطر %d، من %d إلى %d، %d ضربات", L"радиус %d, клетки %d–%d, %d удара", L"Radius %d, Felder %d bis %d, %d Treffer",
				L"raio %d, casas %d a %d, %d golpes", L"straal %d, vakken %d tot %d, %d treffers", L"promień %d, pola %d–%d, %d uderzenia", L"yarıçap %d, %d-%d kare, %d vuruş");
			_snwprintf_s(dst, cap, _TRUNCATE, fmt, sd.area, a0, a1, sd.hits);
		} else {
			const wchar_t* fmt = LangPick14(
				L"半径%d、%d〜%dマス", L"radius %d, tiles %d to %d", L"rayon %d, cases %d à %d", L"raggio %d, caselle %d-%d", L"radio %d, casillas %d a %d",
				L"반경 %d, %d〜%d칸", L"半径%d，%d到%d格", L"نصف قطر %d، من %d إلى %d", L"радиус %d, клетки %d–%d", L"Radius %d, Felder %d bis %d",
				L"raio %d, casas %d a %d", L"straal %d, vakken %d tot %d", L"promień %d, pola %d–%d", L"yarıçap %d, %d-%d kare");
			_snwprintf_s(dst, cap, _TRUNCATE, fmt, sd.area, a0, a1);
		}
		return;
	}
	if (a0 == 0 && a1 == 0) {
		wcscpy_s(dst, cap, LangPick14(
			L"自分だけ", L"yourself only", L"vous seul", L"solo te", L"solo tú", L"자신만", L"只有自己", L"نفسك فقط", L"только себя", L"nur euch selbst", L"só você", L"alleen jezelf", L"tylko siebie", L"yalnız kendini"));
		return;
	}
	if (sd.hits > 1) {
		const wchar_t* fmt = LangPick14(
			L"%d〜%dマス、%d回当たる", L"tiles %d to %d, %d hits", L"cases %d à %d, %d coups", L"caselle %d-%d, %d colpi", L"casillas %d a %d, %d golpes",
			L"%d〜%d칸, %d회", L"%d到%d格，打中%d次", L"من %d إلى %d، %d ضربات", L"клетки %d–%d, %d удара", L"Felder %d bis %d, %d Treffer",
			L"casas %d a %d, %d golpes", L"vakken %d tot %d, %d treffers", L"pola %d–%d, %d uderzenia", L"%d-%d kare, %d vuruş");
		_snwprintf_s(dst, cap, _TRUNCATE, fmt, a0, a1, sd.hits);
	} else {
		const wchar_t* fmt = LangPick14(
			L"%d〜%dマス", L"tiles %d to %d", L"cases %d à %d", L"caselle %d-%d", L"casillas %d a %d",
			L"%d〜%d칸", L"%d到%d格", L"من %d إلى %d", L"клетки %d–%d", L"Felder %d bis %d",
			L"casas %d a %d", L"vakken %d tot %d", L"pola %d–%d", L"%d-%d kare");
		_snwprintf_s(dst, cap, _TRUNCATE, fmt, a0, a1);
	}
}
static void EvoLearn(int sk, int lv, int soon, wchar_t* dst, int cap)
{
	if (sk < 0) {
		wcscpy_s(dst, cap, LangPick14(
			L"この段で新しく覚える技はない。", L"This rank learns no new skill.", L"Ce rang n'apprend aucune technique nouvelle.", L"Questo grado non impara tecniche nuove.", L"Este rango no aprende ninguna técnica nueva.",
			L"이 단계에서 새로 배우는 기술은 없다.", L"此阶没有新学的技能。", L"هذه الرتبة لا تتعلم مهارة جديدة.", L"На этом ранге новых умений нет.", L"Dieser Rang erlernt keine neue Fertigkeit.",
			L"Este grau não aprende técnica nova.", L"Deze rang leert geen nieuwe vaardigheid.", L"Na tym stopniu nie ma nowej umiejętności.", L"Bu kademe yeni beceri öğrenmez."));
		return;
	}
	wchar_t kind[96], where[160];
	EvoKind(SkillInfo(sk), kind, 96);
	EvoWhere(SkillInfo(sk), where, 160);
	const wchar_t* fmt = soon
		? LangPick14(
			L"転職した直後、Lv%dで「%s」。%sで、%s。",
			L"Right after the change, at Lv%d, you learn %s. It is %s, %s.",
			L"Juste après le changement, au Nv%d, vous apprenez %s. C'est %s, %s.",
			L"Subito dopo il cambio, al Lv%d, impari %s. È %s, %s.",
			L"Justo tras el cambio, en Nv%d, aprendes %s. Es %s, %s.",
			L"전직 직후, Lv%d에 「%s」. %s이며, %s.",
			L"转职当下，Lv%d学会「%s」。是%s，%s。",
			L"فور التغيير، في المستوى %d، تتعلم %s. إنه %s، %s.",
			L"Сразу после смены, на ур.%d, изучаете %s. Это %s, %s.",
			L"Direkt nach dem Wechsel, auf St.%d, erlernt ihr %s. Es ist %s, %s.",
			L"Logo após a mudança, no Nv%d, você aprende %s. É %s, %s.",
			L"Meteen na de wissel, op Lv%d, leer je %s. Het is %s, %s.",
			L"Zaraz po zmianie, na poz.%d, uczysz się %s. To %s, %s.",
			L"Geçişin hemen ardından, %d. seviyede %s öğrenirsin. %s, %s.")
		: LangPick14(
			L"この職だけの途中技はLv%dの「%s」。%sで、%s。",
			L"The skill only this job learns later is %s at Lv%d. It is %s, %s.",
			L"La technique que seule cette classe apprend plus tard est %s au Nv%d. C'est %s, %s.",
			L"La tecnica che solo questa classe impara dopo è %s al Lv%d. È %s, %s.",
			L"La técnica que solo esta clase aprende después es %s en Nv%d. Es %s, %s.",
			L"이 직업만 도중에 배우는 기술은 Lv%d의 「%s」. %s이며, %s.",
			L"只有此职中途学会的是Lv%d的「%s」。是%s，%s。",
			L"المهارة التي تتعلمها هذه الوظيفة وحدها لاحقًا هي %s في المستوى %d. إنها %s، %s.",
			L"Умение, которое только эта профессия учит позже, — %s на ур.%d. Это %s, %s.",
			L"Die Fertigkeit, die nur diese Klasse später lernt, ist %s auf St.%d. Sie ist %s, %s.",
			L"A técnica que só esta classe aprende depois é %s no Nv%d. É %s, %s.",
			L"De vaardigheid die alleen deze klasse later leert is %s op Lv%d. Het is %s, %s.",
			L"Umiejętność, której tylko ta profesja uczy się później, to %s na poz.%d. To %s, %s.",
			L"Yalnız bu sınıfın sonra öğrendiği beceri, %d. seviyede %s. %s, %s.");
	if (soon) _snwprintf_s(dst, cap, _TRUNCATE, fmt, lv, SkillLabel(sk), kind, where);
	else {
		const wchar_t* late = LangPick14(
			L"この職だけの途中技はLv%dの「%s」。%sで、%s。",
			L"Only this job, later at Lv%d, learns %s. It is %s, %s.",
			L"Cette classe seule, plus tard au Nv%d, apprend %s. C'est %s, %s.",
			L"Solo questa classe, più tardi al Lv%d, impara %s. È %s, %s.",
			L"Solo esta clase, más tarde en Nv%d, aprende %s. Es %s, %s.",
			L"이 직업만, 나중에 Lv%d에 「%s」을 배운다. %s이며, %s.",
			L"只有此职，稍后在Lv%d学会「%s」。是%s，%s。",
			L"هذه الوظيفة وحدها، لاحقًا في المستوى %d، تتعلم %s. إنها %s، %s.",
			L"Только эта профессия, позже на ур.%d, учит %s. Это %s, %s.",
			L"Nur diese Klasse, später auf St.%d, erlernt %s. Es ist %s, %s.",
			L"Só esta classe, depois no Nv%d, aprende %s. É %s, %s.",
			L"Alleen deze klasse, later op Lv%d, leert %s. Het is %s, %s.",
			L"Tylko ta profesja, później na poz.%d, uczy się %s. To %s, %s.",
			L"Yalnız bu sınıf, sonra %d. seviyede %s öğrenir. %s, %s.");
		_snwprintf_s(dst, cap, _TRUNCATE, late, lv, SkillLabel(sk), kind, where);
	}
}
static void EvoNext(int cls, wchar_t* dst, int cap)
{
	int i = cls - C_JOB0;
	int n = 0;
	wcscpy_s(dst, cap, LangPick14(
		L"この先は", L"Next: ", L"Ensuite : ", L"Poi: ", L"Luego: ", L"이 다음은 ", L"此后是", L"بعد ذلك: ", L"Дальше: ", L"Danach: ",
		L"Depois: ", L"Hierna: ", L"Dalej: ", L"Sonra: "));
	if (i >= 0 && i < TAC_JOB_N) {
		for (int k = 0; k < 4; k++) {
			int t = kJobNext[i][k];
			if (t < 0 || t >= TAC_JOB_N) continue;
			if (n) wcscat_s(dst, cap, LangPick14(L"、", L", ", L", ", L", ", L", ", L", ", L"、", L"، ", L", ", L", ", L", ", L", ", L", ", L", "));
			wcscat_s(dst, cap, L"「");
			wcscat_s(dst, cap, ClassTitle(C_JOB0 + t));
			wcscat_s(dst, cap, L"」");
			n++;
		}
	}
	if (!n) {
		wcscpy_s(dst, cap, LangPick14(
			L"この先の転職はない。", L"There is no further change from here.", L"Aucun changement ensuite.", L"Nessun cambio oltre.", L"No hay otro cambio después.",
			L"이 앞의 전직은 없다.", L"此后没有再转职。", L"لا تغيير بعد ذلك.", L"Дальше смены нет.", L"Danach kein weiterer Wechsel.",
			L"Não há outra mudança depois.", L"Hierna geen verdere wissel.", L"Dalej nie ma zmiany.", L"Bundan sonra geçiş yok."));
		return;
	}
	wcscat_s(dst, cap, LangPick14(L"。", L".", L".", L".", L".", L".", L"。", L".", L".", L".", L".", L".", L".", L"."));
}
static void EvoGrow(const ClassDef& d, wchar_t* dst, int cap)
{
	const wchar_t* s;
	if (d.group == G_SWIFT)
		s = LangPick14(
			L"以後のレベルアップは素早さと技に寄る。", L"Later levels lean on agility and dexterity. ", L"Les niveaux suivants penchent vers l'agilité et l'adresse. ", L"I livelli successivi pendono su agilità e destrezza. ", L"Los niveles siguientes se inclinan a agilidad y destreza. ",
			L"이후 레벨업은 민첩과 기량에 기운다. ", L"之后的升级偏向敏捷和技巧。", L"المستويات اللاحقة تميل إلى الرشاقة والبراعة. ", L"Дальше уровни клонятся к ловкости и сноровке. ", L"Spätere Stufen neigen zu Beweglichkeit und Geschick. ",
			L"Os níveis seguintes pendem para agilidade e destreza. ", L"Latere niveaus hellen naar behendigheid en vaardigheid. ", L"Dalsze poziomy idą w zwinność i zręczność. ", L"Sonraki seviyeler çeviklik ve maharete yaslanır. ");
	else if (d.group == G_MAGIC)
		s = LangPick14(
			L"以後のレベルアップは魔力・魔防・MPに寄る。力は低い。", L"Later levels lean on magic, resistance, and MP. Strength stays low. ", L"Les niveaux suivants penchent vers magie, résistance et PM. La force reste basse. ", L"I livelli successivi pendono su magia, resistenza e PM. La forza resta bassa. ", L"Los niveles siguientes se inclinan a magia, resistencia y PM. La fuerza se queda baja. ",
			L"이후 레벨업은 마력·마방·MP에 기운다. 힘은 낮다. ", L"之后的升级偏向魔力、魔防、MP。力量低。", L"المستويات اللاحقة تميل إلى السحر والمقاومة والطاقة. القوة تبقى منخفضة. ", L"Дальше уровни клонятся к магии, стойкости и ОМ. Сила низкая. ", L"Spätere Stufen neigen zu Magie, Resistenz und MP. Die Stärke bleibt niedrig. ",
			L"Os níveis seguintes pendem para magia, resistência e PM. A força fica baixa. ", L"Latere niveaus hellen naar magie, weerstand en MP. Kracht blijft laag. ", L"Dalsze poziomy idą w magię, odporność i PM. Siła zostaje niska. ", L"Sonraki seviyeler büyü, direnç ve MP'ye yaslanır. Güç düşük kalır. ");
	else if (d.group == G_MELEE)
		s = LangPick14(
			L"以後のレベルアップは力・守備・HPに寄る。", L"Later levels lean on strength, defense, and HP. ", L"Les niveaux suivants penchent vers force, défense et PV. ", L"I livelli successivi pendono su forza, difesa e PS. ", L"Los niveles siguientes se inclinan a fuerza, defensa y PV. ",
			L"이후 레벨업은 힘·수비·HP에 기운다. ", L"之后的升级偏向力量、防御、HP。", L"المستويات اللاحقة تميل إلى القوة والدفاع والصحة. ", L"Дальше уровни клонятся к силе, защите и ОЗ. ", L"Spätere Stufen neigen zu Stärke, Abwehr und LP. ",
			L"Os níveis seguintes pendem para força, defesa e PV. ", L"Latere niveaus hellen naar kracht, verdediging en LP. ", L"Dalsze poziomy idą w siłę, obronę i PŻ. ", L"Sonraki seviyeler güç, savunma ve CP'ye yaslanır. ");
	else
		s = LangPick14(
			L"以後のレベルアップは魔防・魔力・MP・HPに寄る。力は低い。", L"Later levels lean on resistance, magic, MP, and HP. Strength stays low. ", L"Les niveaux suivants penchent vers résistance, magie, PM et PV. La force reste basse. ", L"I livelli successivi pendono su resistenza, magia, PM e PS. La forza resta bassa. ", L"Los niveles siguientes se inclinan a resistencia, magia, PM y PV. La fuerza se queda baja. ",
			L"이후 레벨업은 마방·마력·MP·HP에 기운다. 힘은 낮다. ", L"之后的升级偏向魔防、魔力、MP、HP。力量低。", L"المستويات اللاحقة تميل إلى المقاومة والسحر والطاقة والصحة. القوة تبقى منخفضة. ", L"Дальше уровни клонятся к стойкости, магии, ОМ и ОЗ. Сила низкая. ", L"Spätere Stufen neigen zu Resistenz, Magie, MP und LP. Die Stärke bleibt niedrig. ",
			L"Os níveis seguintes pendem para resistência, magia, PM e PV. A força fica baixa. ", L"Latere niveaus hellen naar weerstand, magie, MP en LP. Kracht blijft laag. ", L"Dalsze poziomy idą w odporność, magię, PM i PŻ. Siła zostaje niska. ", L"Sonraki seviyeler direnç, büyü, MP ve CP'ye yaslanır. Güç düşük kalır. ");
	wcscpy_s(dst, cap, s);
}
// 先頭の1文。伸びる方面と、主体の通常攻撃が物理属性か魔法属性かを先に言う。
static void EvoAxis(const ClassDef& d, wchar_t* dst, int cap)
{
	const wchar_t* grow;
	if (d.group == G_SWIFT)
		grow = LangPick14(
			L"物理方面に伸びる（素早さ・技・HP）。",
			L"Grows on the physical side (agility, dexterity, and HP). ",
			L"Ça monte du côté physique (agilité, adresse et PV). ",
			L"Cresce sul lato fisico (agilità, destrezza e PS). ",
			L"Crece hacia lo físico (agilidad, destreza y PV). ",
			L"물리 쪽으로 큰다(민첩·기량·HP). ",
			L"向物理方面成长（敏捷、技巧、HP）。",
			L"ينمو نحو الجانب الجسدي (الرشاقة والبراعة والصحة). ",
			L"Растёт в физическую сторону (ловкость, сноровка и ОЗ). ",
			L"Wächst zur physischen Seite (Beweglichkeit, Geschick und LP). ",
			L"Cresce para o lado físico (agilidade, destreza e PV). ",
			L"Groeit naar de fysieke kant (behendigheid, vaardigheid en LP). ",
			L"Rośnie w stronę fizyczną (zwinność, zręczność i PŻ). ",
			L"Fiziksel tarafa büyür (çeviklik, maharet ve CP). ");
	else if (d.group == G_MAGIC)
		grow = LangPick14(
			L"魔法方面に伸びる（魔力・魔防・MP。力は低い）。",
			L"Grows on the magic side (magic, resistance, and MP; strength stays low). ",
			L"Ça monte du côté magique (magie, résistance et PM ; la force reste basse). ",
			L"Cresce sul lato magico (magia, resistenza e PM; la forza resta bassa). ",
			L"Crece hacia lo mágico (magia, resistencia y PM; la fuerza se queda baja). ",
			L"마법 쪽으로 큰다(마력·마방·MP. 힘은 낮다). ",
			L"向魔法方面成长（魔力、魔防、MP。力量低）。",
			L"ينمو نحو الجانب السحري (السحر والمقاومة والطاقة، والقوة تبقى منخفضة). ",
			L"Растёт в магическую сторону (магия, стойкость и ОМ; сила низкая). ",
			L"Wächst zur magischen Seite (Magie, Resistenz und MP; die Stärke bleibt niedrig). ",
			L"Cresce para o lado mágico (magia, resistência e PM; a força fica baixa). ",
			L"Groeit naar de magische kant (magie, weerstand en MP; kracht blijft laag). ",
			L"Rośnie w stronę magii (magia, odporność i PM; siła zostaje niska). ",
			L"Büyü tarafına büyür (büyü, direnç ve MP; güç düşük kalır). ");
	else if (d.group == G_MELEE)
		grow = LangPick14(
			L"物理方面に伸びる（力・守備・HP）。",
			L"Grows on the physical side (strength, defense, and HP). ",
			L"Ça monte du côté physique (force, défense et PV). ",
			L"Cresce sul lato fisico (forza, difesa e PS). ",
			L"Crece hacia lo físico (fuerza, defensa y PV). ",
			L"물리 쪽으로 큰다(힘·수비·HP). ",
			L"向物理方面成长（力量、防御、HP）。",
			L"ينمو نحو الجانب الجسدي (القوة والدفاع والصحة). ",
			L"Растёт в физическую сторону (сила, защита и ОЗ). ",
			L"Wächst zur physischen Seite (Stärke, Abwehr und LP). ",
			L"Cresce para o lado físico (força, defesa e PV). ",
			L"Groeit naar de fysieke kant (kracht, verdediging en LP). ",
			L"Rośnie w stronę fizyczną (siła, obrona i PŻ). ",
			L"Fiziksel tarafa büyür (güç, savunma ve CP). ");
	else
		grow = LangPick14(
			L"魔法方面に伸びる（魔防・魔力・MP・HP。力は低い）。",
			L"Grows on the magic side (resistance, magic, MP, and HP; strength stays low). ",
			L"Ça monte du côté magique (résistance, magie, PM et PV ; la force reste basse). ",
			L"Cresce sul lato magico (resistenza, magia, PM e PS; la forza resta bassa). ",
			L"Crece hacia lo mágico (resistencia, magia, PM y PV; la fuerza se queda baja). ",
			L"마법 쪽으로 큰다(마방·마력·MP·HP. 힘은 낮다). ",
			L"向魔法方面成长（魔防、魔力、MP、HP。力量低）。",
			L"ينمو نحو الجانب السحري (المقاومة والسحر والطاقة والصحة، والقوة تبقى منخفضة). ",
			L"Растёт в магическую сторону (стойкость, магия, ОМ и ОЗ; сила низкая). ",
			L"Wächst zur magischen Seite (Resistenz, Magie, MP und LP; die Stärke bleibt niedrig). ",
			L"Cresce para o lado mágico (resistência, magia, PM e PV; a força fica baixa). ",
			L"Groeit naar de magische kant (weerstand, magie, MP en LP; kracht blijft laag). ",
			L"Rośnie w stronę magii (odporność, magia, PM i PŻ; siła zostaje niska). ",
			L"Büyü tarafına büyür (direnç, büyü, MP ve CP; güç düşük kalır). ");
	const wchar_t* atk = d.magAttack
		? LangPick14(
			L"主体は魔法属性の攻撃。", L"The main attack is magical. ", L"L'attaque principale est magique. ", L"L'attacco principale è magico. ", L"El ataque principal es mágico. ",
			L"주체는 마법 속성 공격이다. ", L"主体是魔法属性攻击。", L"الهجوم الأساسي سحري. ", L"Основная атака магическая. ", L"Der Hauptangriff ist magisch. ",
			L"O ataque principal é mágico. ", L"De hoofdaanval is magisch. ", L"Główny atak jest magiczny. ", L"Ana saldırı büyüdür. ")
		: LangPick14(
			L"主体は物理属性の攻撃。", L"The main attack is physical. ", L"L'attaque principale est physique. ", L"L'attacco principale è fisico. ", L"El ataque principal es físico. ",
			L"주체는 물리 속성 공격이다. ", L"主体是物理属性攻击。", L"الهجوم الأساسي جسدي. ", L"Основная атака физическая. ", L"Der Hauptangriff ist physisch. ",
			L"O ataque principal é físico. ", L"De hoofdaanval is fysiek. ", L"Główny atak jest fizyczny. ", L"Ana saldırı fizikseldir. ");
	wchar_t rng[240];
	if (d.rMax < 0) {
		wcscpy_s(rng, LangPick14(
			L"通常攻撃は武器の射程。", L"Basic attacks use the weapon's range. ", L"L'attaque normale a la portée de l'arme. ", L"L'attacco normale ha la gittata dell'arma. ", L"El ataque normal usa el alcance del arma. ",
			L"통상 공격은 무기의 사거리다. ", L"普通攻击是武器的射程。", L"الهجوم العادي بمدى السلاح. ", L"Обычная атака на дальности оружия. ", L"Der normale Angriff hat die Reichweite der Waffe. ",
			L"O ataque normal usa o alcance da arma. ", L"De gewone aanval heeft het bereik van het wapen. ", L"Zwykły atak ma zasięg broni. ", L"Normal saldırı silahın menzilindedir. "));
	} else if (d.rMin >= 2) {
		const wchar_t* fmt = LangPick14(
			L"通常攻撃は%dマス先からで、隣には届かない。", L"Basic attacks start at %d tiles and cannot hit the square beside you. ", L"L'attaque normale part de %d cases et ne touche pas l'adjacent. ", L"L'attacco normale parte da %d caselle e non colpisce l'adiacente. ", L"El ataque normal empieza a %d casillas y no alcanza al adyacente. ",
			L"통상 공격은 %d칸 앞부터이며, 인접에는 닿지 않는다. ", L"普通攻击从%d格外开始，打不到相邻。", L"الهجوم العادي يبدأ من %d ولا يبلغ المجاور. ", L"Обычная атака с %d клеток и не бьёт соседнюю. ", L"Der normale Angriff beginnt bei %d Feldern und trifft nicht das angrenzende. ",
			L"O ataque normal começa a %d casas e não alcança o adjacente. ", L"De gewone aanval begint op %d vakken en raakt de aangrenzende niet. ", L"Zwykły atak zaczyna się od %d pól i nie sięga sąsiada. ", L"Normal saldırı %d kareden başlar, bitişiğe ulaşmaz. ");
		_snwprintf_s(rng, _TRUNCATE, fmt, d.rMin);
	} else if (d.rMax <= 1) {
		wcscpy_s(rng, LangPick14(
			L"通常攻撃は隣だけ。", L"Basic attacks hit only the square beside you. ", L"L'attaque normale ne touche que l'adjacent. ", L"L'attacco normale colpisce solo l'adiacente. ", L"El ataque normal solo alcanza al adyacente. ",
			L"통상 공격은 인접만. ", L"普通攻击只打相邻。", L"الهجوم العادي للمجاور فقط. ", L"Обычная атака только по соседней клетке. ", L"Der normale Angriff trifft nur das angrenzende Feld. ",
			L"O ataque normal só alcança o adjacente. ", L"De gewone aanval raakt alleen het aangrenzende vak. ", L"Zwykły atak tylko w sąsiada. ", L"Normal saldırı yalnız bitişiğe. "));
	} else {
		const wchar_t* fmt = LangPick14(
			L"通常攻撃は%d〜%dマス。", L"Basic attacks reach %d to %d tiles. ", L"L'attaque normale porte de %d à %d cases. ", L"L'attacco normale va da %d a %d caselle. ", L"El ataque normal alcanza de %d a %d casillas. ",
			L"통상 공격은 %d〜%d칸. ", L"普通攻击是%d到%d格。", L"الهجوم العادي من %d إلى %d. ", L"Обычная атака с %d по %d клеток. ", L"Der normale Angriff reicht von %d bis %d Felder. ",
			L"O ataque normal alcança de %d a %d casas. ", L"De gewone aanval reikt van %d tot %d vakken. ", L"Zwykły atak od %d do %d pól. ", L"Normal saldırı %d ile %d kare. ");
		_snwprintf_s(rng, _TRUNCATE, fmt, d.rMin, d.rMax);
	}
	const wchar_t* fly = (d.moveType == MV_FLY)
		? LangPick14(
			L"飛行で山以外の地形を越える。", L"It flies over terrain other than mountains. ", L"Il vole au-dessus du terrain, sauf les montagnes. ", L"Vola sul terreno, tranne le montagne. ", L"Vuela sobre el terreno, salvo las montañas. ",
			L"비행으로 산 이외의 지형을 넘는다. ", L"飞行可越过山以外的地形。", L"يطير فوق التضاريس ما عدا الجبال. ", L"Летит над местностью, кроме гор. ", L"Es fliegt über Gelände außer Bergen. ",
			L"Voa sobre o terreno, exceto montanhas. ", L"Het vliegt over terrein behalve bergen. ", L"Lata nad terenem poza górami. ", L"Dağ dışında araziyi uçarak geçer. ")
		: LangPick14(
			L"飛行はしない。", L"It does not fly. ", L"Il ne vole pas. ", L"Non vola. ", L"No vuela. ",
			L"비행은 하지 않는다. ", L"不会飞行。", L"لا يطير. ", L"Не летает. ", L"Es fliegt nicht. ",
			L"Não voa. ", L"Het vliegt niet. ", L"Nie lata. ", L"Uçmaz. ");
	const wchar_t* keep = LangPick14(
		L"今より低い能力だけ上がり、覚えた技は残る。",
		L"Only stats below the new base rise, and skills you already know stay.",
		L"Seules les stats sous la nouvelle base montent, et les techniques apprises restent.",
		L"Solo le statistiche sotto la nuova base salgono, e le tecniche imparate restano.",
		L"Solo suben los valores bajo la nueva base, y las técnicas aprendidas se quedan.",
		L"지금보다 낮은 능력만 오르고, 배운 기술은 남는다.",
		L"只有低于新基础的能力会上升，已学技能保留。",
		L"ترتفع القدرات الأدنى من الأساس الجديد فقط، والمهارات المعروفة تبقى.",
		L"Растут только параметры ниже новой базы, изученные умения остаются.",
		L"Nur Werte unter der neuen Basis steigen, und gelernte Fertigkeiten bleiben.",
		L"Só sobem os valores abaixo da nova base, e as técnicas aprendidas ficam.",
		L"Alleen waarden onder de nieuwe basis stijgen, en geleerde vaardigheden blijven.",
		L"Rosną tylko cechy poniżej nowej bazy, a znane umiejętności zostają.",
		L"Yalnız yeni tabanın altındaki değerler artar, öğrenilen beceriler kalır.");
	_snwprintf_s(dst, cap, _TRUNCATE, L"%s%s%s%s%s", grow, atk, rng, fly, keep);
}
void EvoBrief(int cls, wchar_t* out, int cap)
{
	if (!out || cap < 8) return;
	out[0] = 0;
	const ClassDef& d = ClassInfo(cls);
	if (cls >= C_JOB0 && cls < C_JOB0 + TAC_JOB_N) {
		int sk0 = -1, lv0 = 0, sk1 = -1, lv1 = 0;
		for (int i = 0; i < LEARN_N; i++) {
			if (d.learn[i].skill < 0) continue;
			if (sk0 < 0) { sk0 = d.learn[i].skill; lv0 = d.learn[i].lv; }
			else if (sk1 < 0) { sk1 = d.learn[i].skill; lv1 = d.learn[i].lv; break; }
		}
		wchar_t axis[720], soon[520], late[520];
		EvoAxis(d, axis, 720);
		if (sk1 >= 0) {
			EvoLearn(sk0, lv0, 1, soon, 520);
			EvoLearn(sk1, lv1, 0, late, 520);
		} else if (sk0 >= 0) {
			EvoLearn(sk0, lv0, 1, soon, 520);
			EvoNext(cls, late, 520);
		} else {
			EvoNext(cls, soon, 520);
			wcscpy_s(late, LangPick14(
				L"覚える技の差はない。", L"There is no skill difference.", L"Pas de différence de technique.", L"Nessuna differenza di tecnica.", L"No hay diferencia de técnica.",
				L"배우는 기술의 차이는 없다.", L"所学技能没有差别。", L"لا فرق في المهارة.", L"Разницы в умениях нет.", L"Kein Unterschied bei der Fertigkeit.",
				L"Não há diferença de técnica.", L"Geen verschil in vaardigheid.", L"Nie ma różnicy umiejętności.", L"Beceri farkı yok."));
		}
		_snwprintf_s(out, cap, _TRUNCATE, L"%s\n%s\n%s", axis, soon, late);
		return;
	}
	const wchar_t* a = NULL;
	const wchar_t* b = NULL;
	EvoRole(cls, &a, &b);
	if (!a || !b) {
		if (d.magic && d.group == G_HOLY) {
			a = LangPick14(
				L"魔法職。回復と聖なる攻撃で仲間を支える。殴る前衛ではない。",
				L"A magic class. You support allies with healing and holy attack. Not a brawling front liner.",
				L"Classe de magie. Vous soutenez les alliés par le soin et l'attaque sacrée. Pas un avant de mêlée.",
				L"Classe di magia. Sostieni gli alleati con cura e attacco sacro. Non un fronte da rissa.",
				L"Clase de magia. Apoyas a los aliados con curación y ataque sagrado. No un frente de golpes.",
				L"마법 직업. 회복과 성스러운 공격으로 동료를 받친다. 때리는 전열이 아니다.",
				L"魔法职。用恢复和神圣攻击支援同伴。不是殴打的前排。",
				L"وظيفة سحر. تدعم الحلفاء بالشفاء والهجوم المقدس. لست مقدمة تضرب.",
				L"Магическая профессия. Поддержка лечением и святой атакой. Не рукопашный передовой.",
				L"Magieklasse. Ihr stützt Verbündete mit Heilung und heiligem Angriff. Keine prügelnde Front.",
				L"Classe de magia. Você apoia aliados com cura e ataque sagrado. Não uma frente de briga.",
				L"Magieklasse. Je steunt bondgenoten met heling en heilige aanval. Geen vechtende voorhoede.",
				L"Profesja magii. Wspierasz sojuszników leczeniem i świętym atakiem. Nie bijący front.",
				L"Büyü sınıfı. İyileştirme ve kutsal saldırıyla yoldaşları desteklersin. Yumruk atan ön saf değilsin.");
			b = LangPick14(
				L"魔力と魔防とMPが伸び、力は低い。以後のレベルアップも魔力と魔防に寄る。",
				L"Magic, resistance, and MP grow, strength stays low. Later levels lean on magic and resistance.",
				L"Magie, résistance et PM montent, la force reste basse. Les niveaux suivants penchent vers magie et résistance.",
				L"Magia, resistenza e PM crescono, la forza resta bassa. I livelli successivi pendono su magia e resistenza.",
				L"Magia, resistencia y PM crecen, la fuerza se queda baja. Los niveles siguientes se inclinan a magia y resistencia.",
				L"마력과 마방과 MP가 늘고, 힘은 낮다. 이후 레벨업도 마력과 마방에 기운다.",
				L"魔力、魔防和MP成长，力量低。之后的升级也偏向魔力和魔防。",
				L"السحر والمقاومة والطاقة تنمو، والقوة تبقى منخفضة. المستويات اللاحقة تميل إلى السحر والمقاومة.",
				L"Растут магия, стойкость и ОМ, сила низкая. Дальше уровни клонятся к магии и стойкости.",
				L"Magie, Resistenz und MP wachsen, die Stärke bleibt niedrig. Spätere Stufen neigen zu Magie und Resistenz.",
				L"Magia, resistência e PM crescem, a força fica baixa. Os níveis seguintes pendem para magia e resistência.",
				L"Magie, weerstand en MP groeien, kracht blijft laag. Latere niveaus hellen naar magie en weerstand.",
				L"Rosną magia, odporność i PM, siła zostaje niska. Dalsze poziomy idą w magię i odporność.",
				L"Büyü, direnç ve MP artar, güç düşük kalır. Sonraki seviyeler büyü ve dirence yaslanır.");
		} else if (d.magic) {
			a = LangPick14(
				L"魔法職。攻撃魔法が本分で、武器の打撃は期待しない。",
				L"A magic class. Attack spells are the work, and weapon blows are not the point.",
				L"Classe de magie. Les sorts d'attaque sont le métier, les coups d'arme ne sont pas le but.",
				L"Classe di magia. Le magie d'attacco sono il mestiere, i colpi d'arma non sono lo scopo.",
				L"Clase de magia. Los conjuros de ataque son el oficio, los golpes de arma no son el fin.",
				L"마법 직업. 공격 마법이 본분이고, 무기의 타격은 기대하지 않는다.",
				L"魔法职。本分是攻击魔法，不要指望武器打击。",
				L"وظيفة سحر. سحر الهجوم هو العمل، وضربات السلاح ليست المقصود.",
				L"Магическая профессия. Работа — атакующая магия, удары оружием не главное.",
				L"Magieklasse. Angriffszauber sind die Arbeit, Waffenschläge sind nicht der Sinn.",
				L"Classe de magia. Magias de ataque são o ofício, golpes de arma não são o ponto.",
				L"Magieklasse. Aanvalsspreuken zijn het werk, wapenslagen zijn niet het punt.",
				L"Profesja magii. Praca to zaklęcia ataku, uderzenia bronią nie są celem.",
				L"Büyü sınıfı. İş saldırı büyüsüdür, silah vuruşu amaç değildir.");
			b = LangPick14(
				L"魔力とMPが伸び、HPと力は低い。前に出ると落ちやすい。以後のレベルアップも魔力に寄る。",
				L"Magic and MP grow, HP and strength stay low. The front line drops you. Later levels lean on magic.",
				L"Magie et PM montent, PV et force restent bas. L'avant vous fait tomber. Les niveaux suivants penchent vers la magie.",
				L"Magia e PM crescono, PS e forza restano bassi. Il fronte ti fa cadere. I livelli successivi pendono sulla magia.",
				L"Magia y PM crecen, PV y fuerza se quedan bajos. El frente te tumba. Los niveles siguientes se inclinan a la magia.",
				L"마력과 MP가 늘고, HP와 힘은 낮다. 앞에 서면 쓰러지기 쉽다. 이후 레벨업도 마력에 기운다.",
				L"魔力和MP成长，HP和力量低。站到前面容易倒下。之后的升级也偏向魔力。",
				L"السحر والطاقة ينموان، والصحة والقوة تبقيان منخفضتين. المقدمة تسقطك. المستويات اللاحقة تميل إلى السحر.",
				L"Растут магия и ОМ, ОЗ и сила низкие. На передовой вы падаете. Дальше уровни клонятся к магии.",
				L"Magie und MP wachsen, LP und Stärke bleiben niedrig. Vorn fallt ihr. Spätere Stufen neigen zur Magie.",
				L"Magia e PM crescem, PV e força ficam baixos. A frente derruba você. Os níveis seguintes pendem para a magia.",
				L"Magie en MP groeien, LP en kracht blijven laag. Vooraan val je. Latere niveaus hellen naar magie.",
				L"Rosną magia i PM, PŻ i siła zostają niskie. Front cię powala. Dalsze poziomy idą w magię.",
				L"Büyü ve MP artar, CP ve güç düşük kalır. Öne çıkınca düşersin. Sonraki seviyeler büyüye yaslanır.");
		} else if (d.moveType == MV_FLY) {
			a = LangPick14(
				L"物理職の飛行。山以外の地形を越え、武器で戦う。魔法が本業ではない。",
				L"A physical flyer. You cross terrain other than mountains and fight with a weapon. Magic is not the job.",
				L"Volant physique. Vous franchissez le terrain sauf les montagnes et combattez à l'arme. La magie n'est pas le métier.",
				L"Volante fisico. Attraversi il terreno tranne le montagne e combatti con l'arma. La magia non è il mestiere.",
				L"Volador físico. Cruzas el terreno salvo las montañas y peleas con el arma. La magia no es el oficio.",
				L"물리 직업의 비행. 산 이외의 지형을 넘고, 무기로 싸운다. 마법이 본업은 아니다.",
				L"物理职的飞行。越过山地以外的地形，用武器战斗。魔法不是本职。",
				L"طائر جسدي. تتجاوز التضاريس عدا الجبال وتقاتل بالسلاح. السحر ليس المهنة.",
				L"Физический летун. Пересекаете местность, кроме гор, и бьётесь оружием. Магия не основная работа.",
				L"Physischer Flieger. Ihr überquert Gelände außer Bergen und kämpft mit der Waffe. Magie ist nicht die Arbeit.",
				L"Voador físico. Você atravessa o terreno, exceto montanhas, e luta com a arma. Magia não é o ofício.",
				L"Fysieke vlieger. Je kruist terrein behalve bergen en vecht met het wapen. Magie is niet het vak.",
				L"Fizyczny lotnik. Pokonujesz teren poza górami i walczysz bronią. Magia nie jest fachem.",
				L"Fiziksel uçucu. Dağlar dışında araziyi geçip silahla savaşırsın. Büyü asıl iş değildir.");
			b = LangPick14(
				L"力とHPが伸びる。先に位置を取る前衛。以後のレベルアップも力に寄る。",
				L"Strength and HP grow. A front liner who takes position first. Later levels lean on strength.",
				L"Force et PV montent. Un avant qui prend la place le premier. Les niveaux suivants penchent vers la force.",
				L"Forza e PS crescono. Un fronte che prende posto per primo. I livelli successivi pendono sulla forza.",
				L"Fuerza y PV crecen. Un frente que toma sitio primero. Los niveles siguientes se inclinan a la fuerza.",
				L"힘과 HP가 늘어난다. 먼저 자리를 잡는 전열. 이후 레벨업도 힘에 기운다.",
				L"力量和HP成长。是先占位置的前排。之后的升级也偏向力量。",
				L"القوة والصحة تنموان. مقدمة تأخذ المكان أولًا. المستويات اللاحقة تميل إلى القوة.",
				L"Растут сила и ОЗ. Передовой, который первым занимает место. Дальше уровни клонятся к силе.",
				L"Stärke und LP wachsen. Eine Front, die zuerst Stellung nimmt. Spätere Stufen neigen zur Stärke.",
				L"Força e PV crescem. Uma frente que toma posição primeiro. Os níveis seguintes pendem para a força.",
				L"Kracht en LP groeien. Een voorhoede die eerst positie pakt. Latere niveaus hellen naar kracht.",
				L"Rosną siła i PŻ. Front, który pierwszy zajmuje miejsce. Dalsze poziomy idą w siłę.",
				L"Güç ve CP artar. Yeri ilk alan ön safsın. Sonraki seviyeler güce yaslanır.");
		} else if (d.group == G_SWIFT) {
			a = LangPick14(
				L"物理職。素早さと必殺で戦い、魔法は使わない。前で殴られ続ける役ではない。",
				L"A physical class. You fight with agility and critical hits, and you do not use magic. You are not the one who stands and takes hits.",
				L"Classe physique. Vous combattez par l'agilité et les critiques, sans magie. Vous n'êtes pas celui qui reste à encaisser.",
				L"Classe fisica. Combatti con agilità e critici, senza magia. Non sei quello che resta a incassare.",
				L"Clase física. Peleas con agilidad y críticos, sin magia. No eres quien se queda a encajar golpes.",
				L"물리 직업. 민첩과 필살로 싸우고, 마법은 쓰지 않는다. 앞에서 계속 맞는 역할이 아니다.",
				L"物理职。靠敏捷和必杀战斗，不用魔法。不是站在前面一直挨打的角色。",
				L"وظيفة جسدية. تقاتل بالرشاقة والضربة القاضية، بلا سحر. لست من يقف ويتلقى الضرب.",
				L"Физическая профессия. Бьётесь ловкостью и критом, без магии. Вы не тот, кто стоит и принимает удары.",
				L"Physische Klasse. Ihr kämpft mit Beweglichkeit und Krit, ohne Magie. Ihr seid nicht der, der stehen bleibt und Treffer nimmt.",
				L"Classe física. Você luta com agilidade e crítico, sem magia. Não é quem fica levando golpe.",
				L"Fysieke klasse. Je vecht met behendigheid en kritiek, zonder magie. Jij bent niet degene die blijft staan en treffers pakt.",
				L"Profesja fizyczna. Walczysz zwinnością i krytem, bez magii. Nie jesteś tym, kto stoi i przyjmuje ciosy.",
				L"Fiziksel sınıf. Çeviklik ve kritikle savaşırsın, büyü kullanmazsın. Önde durup darbe yiyen rol değilsin.");
			b = LangPick14(
				L"素早さと技が伸び、HPと守備は低め。仕留めて下がる。以後のレベルアップも素早さに寄る。",
				L"Agility and dexterity grow, HP and defense stay rather low. You kill and fall back. Later levels lean on agility.",
				L"Agilité et adresse montent, PV et défense restent plutôt bas. Vous tuez et reculez. Les niveaux suivants penchent vers l'agilité.",
				L"Agilità e destrezza crescono, PS e difesa restano piuttosto bassi. Uccidi e arretri. I livelli successivi pendono sull'agilità.",
				L"Agilidad y destreza crecen, PV y defensa se quedan más bien bajos. Matas y retrocedes. Los niveles siguientes se inclinan a la agilidad.",
				L"민첩과 기량이 늘고, HP와 수비는 낮은 편. 죽이고 빠진다. 이후 레벨업도 민첩에 기운다.",
				L"敏捷和技巧成长，HP和防御偏低。杀掉再撤。之后的升级也偏向敏捷。",
				L"الرشاقة والبراعة تنموان، والصحة والدفاع يبقيان منخفضين نوعًا. تقتل ثم تتراجع. المستويات اللاحقة تميل إلى الرشاقة.",
				L"Растут ловкость и сноровка, ОЗ и защита скорее низкие. Убиваете и отходите. Дальше уровни клонятся к ловкости.",
				L"Beweglichkeit und Geschick wachsen, LP und Abwehr bleiben eher niedrig. Ihr tötet und geht zurück. Spätere Stufen neigen zur Beweglichkeit.",
				L"Agilidade e destreza crescem, PV e defesa ficam antes baixos. Você mata e recua. Os níveis seguintes pendem para a agilidade.",
				L"Behendigheid en vaardigheid groeien, LP en verdediging blijven eerder laag. Je doodt en stapt terug. Latere niveaus hellen naar behendigheid.",
				L"Rosną zwinność i zręczność, PŻ i obrona zostają raczej niskie. Zabijasz i odchodzisz. Dalsze poziomy idą w zwinność.",
				L"Çeviklik ve maharet artar, CP ve savunma oldukça düşük kalır. Öldürüp geri çekilirsin. Sonraki seviyeler çevikliğe yaslanır.");
		} else if (d.group == G_HOLY) {
			a = LangPick14(
				L"物理寄りの聖職。武器で隣を叩き、守備で前に立つ。魔法が本業ではないが、聖なる補助を少し持つ。",
				L"A holy class that still fights physically. You strike beside you with a weapon and hold the front with defense. Magic is not the job, but you carry a little holy support.",
				L"Classe sacrée qui combat encore physiquement. Vous frappez l'adjacent à l'arme et tenez l'avant par la défense. La magie n'est pas le métier, mais vous portez un peu de soutien sacré.",
				L"Classe sacra che combatte ancora fisicamente. Colpisci l'adiacente con l'arma e reggi il fronte con la difesa. La magia non è il mestiere, ma porti un po' di sostegno sacro.",
				L"Clase sagrada que aún pelea físicamente. Golpeas al adyacente con el arma y sostienes el frente con la defensa. La magia no es el oficio, pero llevas un poco de apoyo sagrado.",
				L"물리 쪽의 성직. 무기로 인접을 치고, 수비로 앞에 선다. 마법이 본업은 아니지만, 성스러운 보조를 조금 가진다.",
				L"偏物理的圣职。用武器打相邻，以防御站在前面。魔法不是本职，但有一点神圣辅助。",
				L"وظيفة مقدسة تقاتل جسديًا. تضرب المجاور بالسلاح وتثبت المقدمة بالدفاع. السحر ليس المهنة، لكنك تحمل قليلًا من الدعم المقدس.",
				L"Святая профессия, которая всё ещё бьётся физически. Бьёте соседнего оружием и держите фронт защитой. Магия не основная работа, но есть немного святой поддержки.",
				L"Heilige Klasse, die noch physisch kämpft. Ihr schlagt den Angrenzenden mit der Waffe und haltet die Front mit Abwehr. Magie ist nicht die Arbeit, aber ihr tragt etwas heilige Hilfe.",
				L"Classe sagrada que ainda luta fisicamente. Você golpeia o adjacente com a arma e segura a frente com a defesa. Magia não é o ofício, mas você carrega um pouco de apoio sagrado.",
				L"Heilige klasse die nog fysiek vecht. Je slaat de aangrenzende met het wapen en houdt het front met verdediging. Magie is niet het vak, maar je draagt wat heilige steun.",
				L"Święta profesja, która wciąż bije się fizycznie. Bijesz sąsiada bronią i trzymasz front obroną. Magia nie jest fachem, ale masz trochę świętego wsparcia.",
				L"Hâlâ fiziksel savaşan kutsal sınıf. Silahla bitişiği vurur, savunmayla önde durursun. Büyü asıl iş değildir ama biraz kutsal desteğin vardır.");
			b = LangPick14(
				L"守備とHPが伸び、魔力は主役にならない。以後のレベルアップも守備に寄る。",
				L"Defense and HP grow, magic never becomes the main stat. Later levels lean on defense.",
				L"Défense et PV montent, la magie ne devient pas la stat principale. Les niveaux suivants penchent vers la défense.",
				L"Difesa e PS crescono, la magia non diventa la statistica principale. I livelli successivi pendono sulla difesa.",
				L"Defensa y PV crecen, la magia no pasa a ser el valor principal. Los niveles siguientes se inclinan a la defensa.",
				L"수비와 HP가 늘고, 마력은 주역이 되지 않는다. 이후 레벨업도 수비에 기운다.",
				L"防御和HP成长，魔力不会成为主力。之后的升级也偏向防御。",
				L"الدفاع والصحة ينموان، والسحر لا يصبح القدرة الأساسية. المستويات اللاحقة تميل إلى الدفاع.",
				L"Растут защита и ОЗ, магия не становится главной. Дальше уровни клонятся к защите.",
				L"Abwehr und LP wachsen, Magie wird nicht der Hauptwert. Spätere Stufen neigen zur Abwehr.",
				L"Defesa e PV crescem, a magia não vira o atributo principal. Os níveis seguintes pendem para a defesa.",
				L"Verdediging en LP groeien, magie wordt niet de hoofdwaarde. Latere niveaus hellen naar verdediging.",
				L"Rosną obrona i PŻ, magia nie staje się główną cechą. Dalsze poziomy idą w obronę.",
				L"Savunma ve CP artar, büyü ana değer olmaz. Sonraki seviyeler savunmaya yaslanır.");
		} else {
			a = LangPick14(
				L"物理職。武器で隣の敵を叩く前衛で、魔法は使わない。",
				L"A physical class. A front liner who strikes the foe beside you with a weapon, and does not use magic.",
				L"Classe physique. Un avant qui frappe l'ennemi adjacent à l'arme, sans magie.",
				L"Classe fisica. Un fronte che colpisce il nemico adiacente con l'arma, senza magia.",
				L"Clase física. Un frente que golpea al enemigo adyacente con el arma, sin magia.",
				L"물리 직업. 무기로 인접한 적을 치는 전열이며, 마법은 쓰지 않는다.",
				L"物理职。用武器打相邻敌人的前排，不用魔法。",
				L"وظيفة جسدية. مقدمة تضرب العدو المجاور بالسلاح، بلا سحر.",
				L"Физическая профессия. Передовой, который бьёт соседнего врага оружием, без магии.",
				L"Physische Klasse. Eine Front, die den angrenzenden Feind mit der Waffe schlägt, ohne Magie.",
				L"Classe física. Uma frente que golpeia o inimigo adjacente com a arma, sem magia.",
				L"Fysieke klasse. Een voorhoede die de aangrenzende vijand met het wapen slaat, zonder magie.",
				L"Profesja fizyczna. Front, który bije sąsiada bronią, bez magii.",
				L"Fiziksel sınıf. Silahla bitişik düşmanı vuran ön saf, büyü kullanmaz.");
			b = LangPick14(
				L"力とHPが伸びる。単体を殴り倒す向き。以後のレベルアップも力に寄る。",
				L"Strength and HP grow. You are built to drop single targets. Later levels lean on strength.",
				L"Force et PV montent. Vous êtes fait pour abattre des cibles seules. Les niveaux suivants penchent vers la force.",
				L"Forza e PS crescono. Sei fatto per abbattere bersagli singoli. I livelli successivi pendono sulla forza.",
				L"Fuerza y PV crecen. Estás hecho para tumbar objetivos sueltos. Los niveles siguientes se inclinan a la fuerza.",
				L"힘과 HP가 늘어난다. 단체를 때려 눕히는 방향. 이후 레벨업도 힘에 기운다.",
				L"力量和HP成长。适合打倒单体。之后的升级也偏向力量。",
				L"القوة والصحة تنموان. أنت مبني لإسقاط الأهداف المنفردة. المستويات اللاحقة تميل إلى القوة.",
				L"Растут сила и ОЗ. Вы созданы валить одиночные цели. Дальше уровни клонятся к силе.",
				L"Stärke und LP wachsen. Ihr seid dafür da, Einzelziele niederzuschlagen. Spätere Stufen neigen zur Stärke.",
				L"Força e PV crescem. Você é feito para derrubar alvos sozinhos. Os níveis seguintes pendem para a força.",
				L"Kracht en LP groeien. Je bent gemaakt om losse doelen neer te slaan. Latere niveaus hellen naar kracht.",
				L"Rosną siła i PŻ. Jesteś do zwalania pojedynczych celów. Dalsze poziomy idą w siłę.",
				L"Güç ve CP artar. Tek hedefleri devirmek için varsın. Sonraki seviyeler güce yaslanır.");
		}
	}
	wchar_t axis[720];
	EvoAxis(d, axis, 720);
	_snwprintf_s(out, cap, _TRUNCATE, L"%s\n%s\n%s", axis, a, b);
}
