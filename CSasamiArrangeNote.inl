enum {
	AR_STD = 0, AR_BRIGHT, AR_DARK, AR_BUSY, AR_SPARSE, AR_HALF, AR_DRIVE, AR_SYNC,
	AR_OFF, AR_FILL, AR_VOCAL, AR_CALL, AR_THICK, AR_LIGHT, AR_BREAK, AR_CHORUS,
	AR_FOUR, AR_RIDE, AR_BRUSH, AR_ONEDROP, AR_STEP, AR_FLOOR, AR_BOSSA, AR_SAMBA,
	AR_SHUF, AR_TRAIN, AR_SKANK, AR_COMP, AR_BLAST, AR_HROLL, AR_BUILD, AR_DROP,
	AR_WOB, AR_ARP, AR_PAD, AR_FAN, AR_WALTZ, AR_TANGO, AR_RUMBA, AR_CLAVE,
	AR_RIM, AR_OPEN, AR_GHOST, AR_OCT, AR_UNI, AR_QUIET, AR_CLIMAX, AR_LINEAR
};
enum { ORN_HOOK = 1, ORN_RUN = 2, ORN_JARAN = 4, ORN_HALF = 8, ORN_NOHALF = 16 };

static int ArrangeKind(int style, int pat)
{
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	if (pat < 0 || pat >= SC_ARRANGE_COUNT) pat = 0;
	return kArrangeKind[style][pat];
}

static int ArrangeOrnament(int kind)
{
	switch (kind) {
	case AR_VOCAL: case AR_QUIET: case AR_PAD: case AR_BRUSH: return ORN_HOOK | ORN_NOHALF;
	case AR_COMP: return ORN_HOOK | ORN_HALF;
	case AR_ARP: case AR_BUSY: case AR_HROLL: case AR_BLAST: case AR_LINEAR: return ORN_RUN;
	case AR_GHOST: case AR_FILL: case AR_BRIGHT: case AR_OPEN: return ORN_JARAN;
	case AR_SYNC: case AR_OFF: case AR_CALL: case AR_SKANK: return ORN_HALF;
	case AR_HALF: case AR_DROP: case AR_WOB: case AR_SPARSE: case AR_DARK: return ORN_NOHALF;
	default: return 0;
	}
}

static void ApplyPhraseBook(int style, int pat, int bar, int div,
	uint16_t* drm, uint8_t* bass, uint16_t* comp, uint16_t* pad,
	uint16_t* arp, uint16_t* harm, uint16_t* cnt, uint16_t* stab)
{
	if (!drm) return;
	if (style < 0 || style >= SC_AS_COUNT) style = 0;
	if (pat < 0 || pat >= SC_ARRANGE_COUNT) pat = 0;
	int b = bar & 7;
	if (b < 0) b = 0;
	const uint16_t* w = kPhrase[style][pat][b];
	uint16_t m = SlotMask(div);
	drm[DV_KICK] = (uint16_t)(w[0] & m);
	drm[DV_SNARE] = (uint16_t)(w[1] & m);
	drm[DV_HAT] = (uint16_t)(w[2] & m);
	drm[DV_OHAT] = (uint16_t)(w[3] & m);
	drm[DV_RIDE] = (uint16_t)(w[4] & m);
	drm[DV_CLAP] = (uint16_t)(w[5] & m);
	drm[DV_TOM] = (uint16_t)(w[6] & m);
	drm[DV_CRASH] = (uint16_t)(w[7] & m);
	drm[DV_RIM] = (uint16_t)(w[8] & m);
	if (comp) *comp = (uint16_t)(w[9] & m);
	if (arp) *arp = (uint16_t)(w[10] & m);
	if (pad) *pad = (uint16_t)(w[11] & m);
	if (harm) *harm = (uint16_t)(w[12] & m);
	if (stab) *stab = (uint16_t)(w[13] & m);
	if (cnt) *cnt = (uint16_t)((w[10] ? w[10] : w[12]) & m);
	if (bass) {
		memset(bass, 0, 16);
		const uint8_t* src = kPhraseBass[style][pat][b];
		for (int i = 0; i < 16; i++) bass[i] = src[i];
	}
}

static const wchar_t* ArrangeNote(int kind)
{
	switch (kind) {
	case AR_STD: return LL14(L"標準", L"Standard", L"Standard", L"Standard", L"Estándar", L"표준", L"标准", L"قياسي", L"Стандарт", L"Standard", L"Padrão", L"Standaard", L"Standard", L"Standart");
	case AR_BRIGHT: return LL14(L"明るめ", L"Brighter", L"Plus clair", L"Più chiaro", L"Más claro", L"밝게", L"明亮", L"أفتح", L"Светлее", L"Heller", L"Mais claro", L"Lichter", L"Jaśniej", L"Daha parlak");
	case AR_DARK: return LL14(L"暗め", L"Darker", L"Plus sombre", L"Più scuro", L"Más oscuro", L"어둡게", L"偏暗", L"أغمق", L"Темнее", L"Dunkler", L"Mais escuro", L"Donkerder", L"Ciemniej", L"Daha koyu");
	case AR_BUSY: return LL14(L"細かい", L"Busier", L"Plus serré", L"Più fitto", L"Más tupido", L"촘촘하게", L"更密", L"أكثف", L"Плотнее", L"Dichter", L"Mais denso", L"Dichter", L"Gęściej", L"Daha sık");
	case AR_SPARSE: return LL14(L"ゆったり", L"Sparse", L"Plus aéré", L"Più arioso", L"Más despejado", L"여유 있게", L"舒缓", L"أوسع", L"Просторнее", L"Weiter", L"Mais aberto", L"Ruimer", L"Luźniej", L"Daha ferah");
	case AR_HALF: return LL14(L"ハーフ", L"Half-time", L"Demi-temps", L"Half-time", L"Medio tiempo", L"하프타임", L"半拍", L"نصف الإيقاع", L"Халфтайм", L"Halbzeit", L"Meio-tempo", L"Halftime", L"Pół tempa", L"Yarım tempo");
	case AR_DRIVE: return LL14(L"ドライブ", L"Driving", L"En avant", L"Incalzante", L"Impulso", L"드라이브", L"推进", L"دافعة", L"Драйв", L"Treibend", L"Impulso", L"Vooruit", L"Napęd", L"Sürücü");
	case AR_SYNC: return LL14(L"シンコペ", L"Syncopated", L"Syncopé", L"Sincopato", L"Sincopado", L"싱코페이션", L"切分", L"سنكوب", L"Синкопа", L"Synkopiert", L"Sincopado", L"Gesyncopeerd", L"Synkopa", L"Senkop");
	case AR_OFF: return LL14(L"裏打ち", L"Off-beat", L"Contretemps", L"In levare", L"A contratiempo", L"뒷박", L"反拍", L"ضد الضربة", L"Офф-бит", L"Offbeat", L"Contratempo", L"Off-beat", L"Po takcie", L"Ters vuruş");
	case AR_FILL: return LL14(L"フィル多め", L"More fills", L"Plus de breaks", L"Più fill", L"Más rellenos", L"필 많음", L"加花多", L"فواصل أكثر", L"Больше филлов", L"Mehr Fills", L"Mais viradas", L"Meer fills", L"Więcej filli", L"Daha fazla fill");
	case AR_VOCAL: return LL14(L"歌もの", L"Vocal support", L"Accompagnement chant", L"Accompagnamento", L"Acompañamiento", L"노래 반주", L"托唱", L"مرافقة الغناء", L"Под вокал", L"Gesang", L"Acompanhamento", L"Zangbegeleiding", L"Akompaniament", L"Vokal eşlik");
	case AR_CALL: return LL14(L"かけあい", L"Call and response", L"Question-réponse", L"Botta e risposta", L"Pregunta y respuesta", L"주고받기", L"对答", L"سؤال وجواب", L"Вопрос и ответ", L"Ruf und Antwort", L"Pergunta e resposta", L"Vraag en antwoord", L"Pytanie i odpowiedź", L"Soru ve cevap");
	case AR_THICK: return LL14(L"厚め", L"Thicker", L"Plus épais", L"Più spesso", L"Más grueso", L"두껍게", L"加厚", L"أثخن", L"Плотнее", L"Dicker", L"Mais grosso", L"Dikker", L"Grubszy", L"Daha kalın");
	case AR_LIGHT: return LL14(L"軽め", L"Lighter", L"Plus léger", L"Più leggero", L"Más ligero", L"가볍게", L"轻一些", L"أخف", L"Легче", L"Leichter", L"Mais leve", L"Lichter", L"Lżej", L"Daha hafif");
	case AR_BREAK: return LL14(L"ブレイク", L"Break", L"Break", L"Break", L"Corte", L"브레이크", L"间断", L"فاصل", L"Брейк", L"Break", L"Quebra", L"Break", L"Break", L"Kırılma");
	case AR_CHORUS: return LL14(L"サビ", L"Chorus", L"Refrain", L"Ritornello", L"Estribillo", L"후렴", L"副歌", L"اللازمة", L"Припев", L"Refrain", L"Refrão", L"Refrein", L"Refren", L"Nakarat");
	case AR_FOUR: return LL14(L"4ビート", L"Four-beat", L"Quatre temps", L"Quattro battute", L"Cuatro tiempos", L"4비트", L"四拍", L"أربع ضربات", L"Четыре доли", L"Vier Viertel", L"4 tempos", L"Vier tellen", L"4 uderzenia", L"4 vuruş");
	case AR_RIDE: return LL14(L"ライド", L"Ride", L"Cymbale ride", L"Ride", L"Ride", L"라이드", L"叮叮镲", L"صنج رايد", L"Райд", L"Ride", L"Ride", L"Ride", L"Ride", L"Ride");
	case AR_BRUSH: return LL14(L"ブラシ", L"Brushes", L"Balais", L"Spazzole", L"Cepillos", L"브러시", L"刷子", L"فراشي", L"Щётки", L"Besen", L"Vassouras", L"Borstels", L"Szczotki", L"Fırça");
	case AR_ONEDROP: return LL14(L"ワンドロップ", L"One drop", L"One drop", L"One drop", L"One drop", L"원드롭", L"一滴", L"ون دروب", L"Уан-дроп", L"One Drop", L"One drop", L"One drop", L"One drop", L"One drop");
	case AR_STEP: return LL14(L"ステッパー", L"Steppers", L"Steppers", L"Steppers", L"Steppers", L"스테퍼", L"踏步", L"ستيبرز", L"Степпер", L"Steppers", L"Steppers", L"Steppers", L"Steppers", L"Steppers");
	case AR_FLOOR: return LL14(L"四つ打ち", L"Four on the floor", L"Quatre au sol", L"Quattro sul pavimento", L"Cuatro en el suelo", L"포 온 더 플로어", L"四踩", L"أربع على الأرض", L"Четыре в пол", L"Vier auf dem Boden", L"Quatro no chão", L"Vier op de vloer", L"Cztery na podłodze", L"Dört dörtlük");
	case AR_BOSSA: return LL14(L"ボサノバ", L"Bossa nova", L"Bossa-nova", L"Bossa nova", L"Bossa nova", L"보사노바", L"波萨诺瓦", L"بوسا نوفا", L"Босанова", L"Bossa Nova", L"Bossa nova", L"Bossa nova", L"Bossa nova", L"Bossa nova");
	case AR_SAMBA: return LL14(L"サンバ", L"Samba", L"Samba", L"Samba", L"Samba", L"삼바", L"桑巴", L"سامبا", L"Самба", L"Samba", L"Samba", L"Samba", L"Samba", L"Samba");
	case AR_SHUF: return LL14(L"シャッフル", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle", L"셔플", L"摇摆", L"شفْل", L"Шафл", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle", L"Shuffle");
	case AR_TRAIN: return LL14(L"トレイン", L"Train beat", L"Train", L"Train", L"Tren", L"트레인 비트", L"火车节奏", L"إيقاع القطار", L"Поезд", L"Zug", L"Comboio", L"Trein", L"Pociąg", L"Tren ritmi");
	case AR_SKANK: return LL14(L"スカ", L"Skank", L"Skank", L"Skank", L"Skank", L"스캥크", L"斯卡", L"سكانك", L"Скэнк", L"Skank", L"Skank", L"Skank", L"Skank", L"Skank");
	case AR_COMP: return LL14(L"コンピング", L"Comping", L"Comping", L"Comping", L"Comping", L"컴핑", L"伴奏切分", L"كومبنج", L"Компинг", L"Comping", L"Comping", L"Comping", L"Comping", L"Comping");
	case AR_BLAST: return LL14(L"ブラスト", L"Blast", L"Blast", L"Blast", L"Ráfaga", L"블래스트", L"连击", L"بلاست", L"Бласт", L"Blast", L"Blast", L"Blast", L"Blast", L"Blast");
	case AR_HROLL: return LL14(L"ハットロール", L"Hat rolls", L"Roulements charleston", L"Rulli di charleston", L"Redobles de charles", L"햇 롤", L"踩镲滚奏", L"لفات هاي هات", L"Дроби хэта", L"Hi-Hat-Wirbel", L"Rufos de chimbal", L"Hi-hatroffels", L"Wirble na hi-hacie", L"Hat rulo");
	case AR_BUILD: return LL14(L"ビルドアップ", L"Build-up", L"Montée", L"Build-up", L"Subida", L"빌드업", L"堆积", L"تصاعد", L"Нарастание", L"Aufbau", L"Subida", L"Opbouw", L"Narastanie", L"Yükseliş");
	case AR_DROP: return LL14(L"ドロップ", L"Drop", L"Drop", L"Drop", L"Caída", L"드롭", L"下落", L"سقوط", L"Дроп", L"Drop", L"Queda", L"Drop", L"Drop", L"Düşüş");
	case AR_WOB: return LL14(L"ワブル", L"Wobble", L"Wobble", L"Wobble", L"Wobble", L"와블", L"摇摆低音", L"وبل", L"Вобл", L"Wobble", L"Wobble", L"Wobble", L"Wobble", L"Wobble");
	case AR_ARP: return LL14(L"アルペジオ", L"Arpeggio", L"Arpège", L"Arpeggio", L"Arpegio", L"아르페지오", L"琶音", L"أربيجيو", L"Арпеджио", L"Arpeggio", L"Arpejo", L"Arpeggio", L"Arpeggio", L"Arpej");
	case AR_PAD: return LL14(L"パッド", L"Pad", L"Nappe", L"Tappeto", L"Colchón", L"패드", L"铺底", L"وسادة", L"Пэд", L"Fläche", L"Base", L"Tapijt", L"Pad", L"Ped");
	case AR_FAN: return LL14(L"ファンファーレ", L"Fanfare", L"Fanfare", L"Fanfara", L"Fanfarria", L"팬파르", L"号角", L"فانفار", L"Фанфара", L"Fanfare", L"Fanfarra", L"Fanfare", L"Fanfara", L"Fanfar");
	case AR_WALTZ: return LL14(L"ワルツ", L"Waltz", L"Valse", L"Valzer", L"Vals", L"왈츠", L"华尔兹", L"فالس", L"Вальс", L"Walzer", L"Valsa", L"Wals", L"Walc", L"Vals");
	case AR_TANGO: return LL14(L"タンゴ", L"Tango", L"Tango", L"Tango", L"Tango", L"탱고", L"探戈", L"تانغو", L"Танго", L"Tango", L"Tango", L"Tango", L"Tango", L"Tango");
	case AR_RUMBA: return LL14(L"ルンバ", L"Rumba", L"Rumba", L"Rumba", L"Rumba", L"룸바", L"伦巴", L"رومبا", L"Румба", L"Rumba", L"Rumba", L"Rumba", L"Rumba", L"Rumba");
	case AR_CLAVE: return LL14(L"クラベ", L"Clave", L"Clave", L"Clave", L"Clave", L"클라베", L"克拉维", L"كلاف", L"Клаве", L"Clave", L"Clave", L"Clave", L"Clave", L"Clave");
	case AR_RIM: return LL14(L"リム", L"Rim click", L"Cercle", L"Rim clic", L"Aro", L"림 클릭", L"边击", L"نقرة الحافة", L"Римклик", L"Rim-Klick", L"Aro", L"Randklik", L"Obręcz", L"Kenar vuruşu");
	case AR_OPEN: return LL14(L"オープンハット", L"Open hat", L"Charleston ouvert", L"Charleston aperto", L"Charles abierto", L"오픈 햇", L"开镲", L"هاي هات مفتوح", L"Открытый хэт", L"Offene Hi-Hat", L"Chimbal aberto", L"Open hi-hat", L"Otwarty hi-hat", L"Açık hi-hat");
	case AR_GHOST: return LL14(L"ゴースト", L"Ghost notes", L"Notes fantômes", L"Note fantasma", L"Notas fantasma", L"고스트 노트", L"虚音", L"نغمات شبح", L"Призрачные ноты", L"Ghost Notes", L"Notas fantasma", L"Ghost notes", L"Duchy", L"Hayalet notalar");
	case AR_OCT: return LL14(L"オクターブ", L"Octave", L"Octave", L"Ottava", L"Octava", L"옥타브", L"八度", L"أوكتاف", L"Октава", L"Oktave", L"Oitava", L"Octaaf", L"Oktawa", L"Oktav");
	case AR_UNI: return LL14(L"ユニゾン", L"Unison", L"Unisson", L"Unisono", L"Unísono", L"유니즌", L"齐奏", L"يونيسون", L"Унисон", L"Unison", L"Uníssono", L"Unison", L"Unison", L"Unison");
	case AR_QUIET: return LL14(L"静かに", L"Quiet", L"Calme", L"Calmo", L"Quieto", L"잔잔하게", L"安静", L"هادئ", L"Тихо", L"Ruhig", L"Calmo", L"Rustig", L"Cicho", L"Sakin");
	case AR_CLIMAX: return LL14(L"盛り", L"Climax", L"Apogée", L"Culmine", L"Clímax", L"클라이맥스", L"高潮", L"ذروة", L"Кульминация", L"Höhepunkt", L"Auge", L"Climax", L"Kulminacja", L"Doruk");
	default: return LL14(L"リニア", L"Linear", L"Linéaire", L"Lineare", L"Lineal", L"리니어", L"线性", L"خطي", L"Линейно", L"Linear", L"Linear", L"Lineair", L"Liniowy", L"Doğrusal");
	}
}

void ScAutoArrangeCaption(int style, int pat, wchar_t* buf, int cch)
{
	if (!buf || cch < 2) return;
	buf[0] = 0;
	if (pat < 0 || pat >= SC_ARRANGE_COUNT) pat = 0;
	const wchar_t* note = ArrangeNote(ArrangeKind(style, pat));
	wchar_t ch = (wchar_t)(L'A' + pat);
	const wchar_t* fmt = LL14(
		L"アレンジ%c（%s）", L"Arrange %c (%s)", L"Arrangement %c (%s)", L"Arrangiamento %c (%s)",
		L"Arreglo %c (%s)", L"어레인지 %c (%s)", L"编曲%c（%s）", L"توزيع %c (%s)",
		L"Аранжировка %c (%s)", L"Arrangement %c (%s)", L"Arranjo %c (%s)", L"Arrangement %c (%s)",
		L"Aranżacja %c (%s)", L"Düzenleme %c (%s)");
	_snwprintf_s(buf, cch, _TRUNCATE, fmt, ch, note);
}
