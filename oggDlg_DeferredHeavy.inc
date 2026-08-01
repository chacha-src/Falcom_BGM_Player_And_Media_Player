		// Deferred: tooltips + fnWFilter (moved from OnInitDialog for faster first paint)
	COSVersion os;
	DWORD edition;
	OSVERSIONINFOEX in;
	BOOL dumy;
	os.GetVersionInfo(in, edition, dumy);
	CString cpus;
	char CPUBrandString[0x40];
	int CPUInfo[4] = { -1 };
	__cpuid(CPUInfo, 0x80000002);
	memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000003);
	memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000004);
	memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
	cpus = CPUBrandString;
	{
		// ツールヒント コントロールを作成します
		CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);

		auto addTip = [this](int id, LPCTSTR text) {
			if (!m_tooltip.GetSafeHwnd() || !text) return;
			CWnd* w = GetDlgItem(id);
			HWND hw = w ? w->GetSafeHwnd() : NULL;
			if (!hw || !::IsWindow(hw)) return;
			m_tooltip.AddTool(w, text);
		};


		// TODO: コントロールを追加するために以下のフォームの 1 つを使用してください:
		// addTip(IDC_<name>, <string-table-id>);
		addTip(IDC_BUTTON1, LL14(
			L"演奏中のogg/wav/mp3/avi/kpiファイルを停止します",
			L"Stop playing ogg/wav/mp3/avi/kpi file",
			L"Arreter la lecture du fichier ogg/wav/mp3/avi/kpi",
			L"Interrompi la riproduzione del file ogg/wav/mp3/avi/kpi",
			L"Detener la reproduccion del archivo ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi 파일 재생 중지",
			L"停止播放ogg/wav/mp3/avi/kpi文件",
			L"إيقاف تشغيل ملف ogg/wav/mp3/avi/kpi",
			L"Остановить воспроизведение файла ogg/wav/mp3/avi/kpi",
			L"Wiedergabe der ogg/wav/mp3/avi/kpi-Datei stoppen",
			L"Parar a reproducao do arquivo ogg/wav/mp3/avi/kpi",
			L"Afspelen van ogg/wav/mp3/avi/kpi-bestand stoppen",
			L"Zatrzymaj odtwarzanie pliku ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi dosyas?n?n oynat?lmas?n? durdur"));
		addTip(IDC_BUTTON2, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON6, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON7, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON8, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON15, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON17, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON19, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON25, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON23, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON24, LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"트랙 목록에서 ogg 선택 후 재생",
			L"从曲目列表中选中并播放ogg",
			L"اختر وشغّل ogg من قائمة المسارات",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		addTip(IDC_BUTTON27, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON28, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON31, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON35, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON33, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON37, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON39, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON44, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON45, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON46, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON47, LL14(
			L"曲一覧表からmp3を選択し再生します",
			L"Select and play mp3 from track list",
			L"Selectionner et lire un mp3 depuis la liste",
			L"Seleziona e riproduci mp3 dalla lista",
			L"Seleccionar y reproducir mp3 de la lista",
			L"트랙 목록에서 mp3 선택 후 재생",
			L"从曲目列表中选中并播放mp3",
			L"اختر وشغّل mp3 من قائمة المسارات",
			L"Выбрать и воспроизвести mp3 из списка",
			L"mp3 aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir mp3 da lista de faixas",
			L"mp3 selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz mp3 z listy utworow",
			L"Parca listesinden mp3 sec ve oynat"));
		addTip(IDC_BUTTON48, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON51, LL14(
			L"曲一覧表からwav(adp)を選択し再生します",
			L"Select and play wav(adp) from track list",
			L"Selectionner et lire un wav(adp) depuis la liste",
			L"Seleziona e riproduci wav(adp) dalla lista",
			L"Seleccionar y reproducir wav(adp) de la lista",
			L"트랙 목록에서 wav(adp) 선택 후 재생",
			L"从曲目列表中选中并播放wav(adp)",
			L"اختر وشغّل wav(adp) من قائمة المسارات",
			L"Выбрать и воспроизвести wav(adp) из списка",
			L"wav(adp) aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav(adp) da lista de faixas",
			L"wav(adp) selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav(adp) z listy utworow",
			L"Parca listesinden wav(adp) sec ve oynat"));
		addTip(IDC_BUTTON53, LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"트랙 목록에서 wav 선택 후 재생",
			L"从曲目列表中选中并播放wav",
			L"اختر وشغّل wav من قائمة المسارات",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		addTip(IDC_BUTTON54, LL14(
			L"曲一覧表からmp3を選択し再生します",
			L"Select and play mp3 from track list",
			L"Selectionner et lire un mp3 depuis la liste",
			L"Seleziona e riproduci mp3 dalla lista",
			L"Seleccionar y reproducir mp3 de la lista",
			L"트랙 목록에서 mp3 선택 후 재생",
			L"从曲目列表中选中并播放mp3",
			L"اختر وشغّل mp3 من قائمة المسارات",
			L"Выбрать и воспроизвести mp3 из списка",
			L"mp3 aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir mp3 da lista de faixas",
			L"mp3 selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz mp3 z listy utworow",
			L"Parca listesinden mp3 sec ve oynat"));
		addTip(IDC_BUTTON57, LL14(
			L"プレイリストを表示／非表示します。\n表示されている時に演奏を開始またはドロップで演奏するとリストに追加されます。\n非表示の時はリストには追加されません。\n本体へのドロップは1つだけでしたがプレイリストへのドロップは複数出来ます。",
			L"Show/hide playlist.\nWhen visible, playback or drop adds to list.\nWhen hidden, items are not added.\nMain window accepts single drop; playlist accepts multiple.",
			L"Afficher/masquer la liste de lecture.\nVisible : la lecture ou le depot ajoute a la liste.\nMasque : les elements ne sont pas ajoutes.\nFenetre principale : un seul depot ; liste : plusieurs.",
			L"Mostra/nascondi playlist.\nVisibile: riproduzione o trascinamento aggiunge alla lista.\nNascosto: gli elementi non vengono aggiunti.\nFinestra principale: un file; playlist: piu file.",
			L"Mostrar/ocultar lista de reproduccion.\nVisible: reproducir o soltar anade a la lista.\nOculto: los elementos no se anaden.\nVentana principal: un archivo; lista: varios.",
			L"재생 목록 표시/숨기기.\n표시 시 재생 또는 드롭 시 목록에 추가됨.\n숨김 시 목록에 추가 안 됨.\n본체는 1개만, 재생 목록은 여러 개 드롭 가능.",
			L"显示/隐藏播放列表。\n显示时，开始播放或拖放将添加到列表中。\n隐藏时，不会添加到列表。\n主窗口只接受一个拖放，播放列表可接受多个。",
			L"إظهار/إخفاء قائمة التشغيل.\nعند الظهور: التشغيل أو الإفلات يضيف إلى القائمة.\nعند الإخفاء: لا تُضاف العناصر.\nالنافذة الرئيسية: إفلات واحد؛ القائمة: عدة.",
			L"Показать/скрыть плейлист.\nПри отображении воспроизведение или перетаскивание добавляет в список.\nПри скрытии элементы не добавляются.\nГлавное окно: один файл; плейлист: несколько.",
			L"Wiedergabeliste anzeigen/ausblenden.\nBei Anzeige: Wiedergabe oder Drop fugt zur Liste hinzu.\nBei Ausblenden: Elemente nicht hinzugefugt.\nHauptfenster: ein Drop; Playlist: mehrere.",
			L"Mostrar/ocultar lista de reproducao.\nQuando visivel, reproduzir ou soltar adiciona a lista.\nQuando oculto, itens nao sao adicionados.\nJanela principal: um arquivo; lista: varios.",
			L"Afspeellijst tonen/verbergen.\nZichtbaar: afspelen of neerzetten voegt toe aan lijst.\nVerborgen: items worden niet toegevoegd.\nHoofdvenster: een bestand; afspeellijst: meerdere.",
			L"Poka?/ukryj list? odtwarzania.\nGdy widoczna: odtwarzanie lub upuszczenie dodaje do listy.\nGdy ukryta: elementy nie s? dodawane.\nG?owne okno: jeden plik; lista: wiele.",
			L"Calma listesini goster/gizle.\nGorunurken, oynatma veya b?rakma listeye ekler.\nGizliyken, o?eler eklenmez.\nAna pencere: tek dosya; calma listesi: birden fazla."));
		addTip(IDC_BUTTON59, LL14(
			L"イコライザーを設定します",
			L"Open equalizer settings",
			L"Ouvrir les parametres de l'egaliseur",
			L"Apri le impostazioni dell'equalizzatore",
			L"Abrir configuracion del ecualizador",
			L"이퀄라이저 설정 열기",
			L"打开均衡器设置",
			L"فتح إعدادات المعادل",
			L"Открыть настройки эквалайзера",
			L"Equalizer-Einstellungen offnen",
			L"Abrir configuracoes do equalizador",
			L"Equalizerinstellingen openen",
			L"Otworz ustawienia korektora",
			L"Ekolayzer ayarlar?n? ac"));
		addTip(IDC_BUTTON3, LL14(
			L"演奏中のogg/wav/mp3/avi/kpiファイルを一時停止/再開します",
			L"Pause/resume playing ogg/wav/mp3/avi/kpi file",
			L"Suspendre/reprendre la lecture du fichier ogg/wav/mp3/avi/kpi",
			L"Metti in pausa/riprendi la riproduzione del file ogg/wav/mp3/avi/kpi",
			L"Pausar/reanudar la reproduccion del archivo ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi 파일 재생 일시 정지/재개",
			L"暂停/恢复播放ogg/wav/mp3/avi/kpi文件",
			L"إيقاف/استئناف تشغيل ملف ogg/wav/mp3/avi/kpi",
			L"Приостановить/возобновить воспроизведение файла ogg/wav/mp3/avi/kpi",
			L"Wiedergabe der ogg/wav/mp3/avi/kpi-Datei pausieren/fortsetzen",
			L"Pausar/retomar a reproducao do arquivo ogg/wav/mp3/avi/kpi",
			L"Afspelen van ogg/wav/mp3/avi/kpi-bestand pauzeren/hervatten",
			L"Wstrzymaj/wznow odtwarzanie pliku ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi dosyas?n?n oynat?lmas?n? duraklat/devam ettir"));
		addTip(IDC_BUTTON4, LL14(
			L"演奏中だった曲を頭から再演奏します",
			L"Restart current track from beginning",
			L"Redemarrer la piste depuis le debut",
			L"Riavvia la traccia corrente dall'inizio",
			L"Reiniciar la pista actual desde el principio",
			L"현재 트랙 처음부터 다시 재생",
			L"从头重新播放当前曲目",
			L"إعادة تشغيل المسار الحالي من البداية",
			L"Перезапустить текущий трек с начала",
			L"Aktuellen Titel von Anfang an neu starten",
			L"Reiniciar a faixa atual do inicio",
			L"Huidig nummer vanaf het begin opnieuw starten",
			L"Uruchom ponownie bie??cy utwor od pocz?tku",
			L"Gecerli parcay? ba?tan yeniden ba?lat"));
		addTip(IDC_BUTTON5, LL14(
			L"フェードアウトして停止します。(内蔵デコーダのみ)",
			L"Fade out and stop (built-in decoder only)",
			L"Fondu et arret (decodeur integre uniquement)",
			L"Dissolvenza e stop (solo decodificatore integrato)",
			L"Fundido y detener (solo decodificador integrado)",
			L"페이드 아웃 후 중지 (내장 디코더만)",
			L"淡出并停止（仅内置解码器）",
			L"تلاشٍ وإيقاف (فقط وحدة فك التشفير المدمجة)",
			L"Плавное затухание и остановка (только встроенный декодер)",
			L"Ausblenden und stoppen (nur integrierter Decoder)",
			L"Fade out e parar (somente decodificador integrado)",
			L"Uitfaden en stoppen (alleen ingebouwde decoder)",
			L"Wycisz i zatrzymaj (tylko wbudowany dekoder)",
			L"Solukla?t?r ve durdur (yaln?zca dahili kod cozucu)"));
		addTip(IDC_BUTTON9, LL14(
			L"各ゲームのフォルダ位置を指定します",
			L"Specify folder location for each game",
			L"Specifier l'emplacement du dossier pour chaque jeu",
			L"Specificare la posizione della cartella per ogni gioco",
			L"Especificar la ubicacion de la carpeta para cada juego",
			L"각 게임 폴더 위치 지정",
			L"指定各个游戏的文件夹位置",
			L"تحديد موقع المجلد لكل لعبة",
			L"Указать расположение папки для каждой игры",
			L"Ordnerpfad fur jedes Spiel festlegen",
			L"Especificar a localizacao da pasta para cada jogo",
			L"Maplocatie voor elk spel opgeven",
			L"Okre?l lokalizacj? folderu dla ka?dej gry",
			L"Her oyun icin klasor konumunu belirt"));
		addTip(IDOK, LL14(
			L"簡易プレイヤを終了します",
			L"Exit simple player",
			L"Quitter le lecteur simple",
			L"Esci dal lettore semplice",
			L"Salir del reproductor simple",
			L"간이 플레이어 종료",
			L"退出简易播放器",
			L"الخروج من المشغل البسيط",
			L"Выйти из простого плеера",
			L"Einfachen Player beenden",
			L"Sair do player simples",
			L"Eenvoudige speler afsluiten",
			L"Zamknij prosty odtwarzacz",
			L"Basit oynat?c?dan c?k"));
		addTip(IDC_SLIDER1, LL14(
			L"音量を変更します\nWindows全体の音量が関係してきます。",
			L"Change volume\nAffects overall Windows volume.",
			L"Modifier le volume\nAffecte le volume global de Windows.",
			L"Cambia volume\nInfluisce sul volume generale di Windows.",
			L"Cambiar volumen\nAfecta el volumen general de Windows.",
			L"볼륨 변경\nWindows 전체 볼륨에 영향을 줍니다.",
			L"更改音量\n影响Windows整体音量。",
			L"تغيير مستوى الصوت\nيؤثر على مستوى صوت Windows الإجمالي.",
			L"Изменить громкость\nВлияет на общую громкость Windows.",
			L"Lautstarke andern\nBeeinflusst die globale Windows-Lautstarke.",
			L"Alterar volume\nAfeta o volume geral do Windows.",
			L"Volume aanpassen\nBeinvloedt het algehele Windows-volume.",
			L"Zmie? g?o?no??\nWp?ywa na ogoln? g?o?no?? systemu Windows.",
			L"Sesi de?i?tir\nWindows genel ses seviyesini etkiler."));
		addTip(IDC_SLIDER3, LL14(
			L"DirectSound音量を変更します\nこの簡易プレイヤのみの変更でとどまります。\nWindowsの音量は変化しません。",
			L"Change DirectSound volume\nOnly affects this player.\nWindows volume unchanged.",
			L"Modifier le volume DirectSound\nAffecte uniquement ce lecteur.\nLe volume Windows reste inchange.",
			L"Cambia volume DirectSound\nInfluisce solo su questo lettore.\nIl volume di Windows rimane invariato.",
			L"Cambiar volumen DirectSound\nSolo afecta este reproductor.\nEl volumen de Windows no cambia.",
			L"DirectSound 볼륨 변경\n이 플레이어에만 영향.\nWindows 볼륨 변경 없음.",
			L"更改DirectSound音量\n仅影响此播放器。\nWindows音量不变。",
			L"تغيير مستوى DirectSound\nيؤثر على هذا المشغل فقط.\nمستوى Windows يبقى دون تغيير.",
			L"Изменить громкость DirectSound\nВлияет только на этот плеер.\nГромкость Windows не изменяется.",
			L"DirectSound-Lautstarke andern\nBetrifft nur diesen Player.\nWindows-Lautstarke bleibt unverandert.",
			L"Alterar volume DirectSound\nAfeta apenas este player.\nVolume do Windows inalterado.",
			L"DirectSound-volume aanpassen\nBeinvloedt alleen deze speler.\nWindows-volume blijft ongewijzigd.",
			L"Zmie? g?o?no?? DirectSound\nWp?ywa tylko na ten odtwarzacz.\nG?o?no?? Windows pozostaje bez zmian.",
			L"DirectSound sesini de?i?tir\nYaln?zca bu oynat?c?y? etkiler.\nWindows ses seviyesi de?i?mez."));
		addTip(IDC_CHECK1, LL14(
			L"スペクトルアナライザー(波形)を表示/非表示します",
			L"Show/hide spectrum analyzer (waveform)",
			L"Afficher/masquer l'analyseur de spectre (forme d'onde)",
			L"Mostra/nascondi analizzatore di spettro (forma d'onda)",
			L"Mostrar/ocultar analizador de espectro (forma de onda)",
			L"스펙트럼 분석기(파형) 표시/숨기기",
			L"显示/隐藏频谱分析器（波形）",
			L"إظهار/إخفاء محلل الطيف (شكل الموجة)",
			L"Показать/скрыть анализатор спектра (осциллограмма)",
			L"Spektrumanalysator (Wellenform) anzeigen/ausblenden",
			L"Mostrar/ocultar analisador de espectro (forma de onda)",
			L"Spectrumanalyzer (golfvorm) tonen/verbergen",
			L"Poka?/ukryj analizator widma (kszta?t fali)",
			L"Spektrum analizorunu (dalga formu) goster/gizle"));
		addTip(IDC_CHECK2, LL14(
			L"演奏中の曲をwavで保存します",
			L"Save playing track as wav",
			L"Enregistrer la piste en cours de lecture en wav",
			L"Salva la traccia in riproduzione come wav",
			L"Guardar la pista en reproduccion como wav",
			L"재생 중인 트랙을 wav로 저장",
			L"将正在播放的曲目保存为wav",
			L"حفظ المسار قيد التشغيل كـ wav",
			L"Сохранить воспроизводимый трек как wav",
			L"Wiedergegebenen Titel als wav speichern",
			L"Salvar faixa em reproducao como wav",
			L"Afspelend nummer opslaan als wav",
			L"Zapisz odtwarzany utwor jako wav",
			L"Oynat?lan parcay? wav olarak kaydet"));
		addTip(IDC_CHECK_MICMIX, LL14(
			L"WAV保存ONのとき、マイクの音を再生PCMにミックスして書き出します。端末は設定の「マイク(録音)」で選択",
			L"When Save WAV is on, mix mic into saved PCM. Choose device in Settings > Microphone",
			L"Si Sauver WAV est ON, mixer le micro. Choisir le peripherique dans Parametres",
			L"Con Salva WAV, mixa il microfono. Scegli il dispositivo in Impostazioni",
			L"Con Guardar WAV, mezcla el micro. Elija el dispositivo en Ajustes",
			L"WAV 저장 ON일 때 마이크를 PCM에 믹스. 장치는 설정의 마이크(녹음)에서 선택",
			L"WAV保存开启时混入麦克风。设备在设置的「麦克风(录音)」中选择",
			L"عند حفظ WAV امزج الميكروفون. اختر الجهاز من الإعدادات",
			L"При сохранении WAV микшировать микрофон. Устройство — в настройках",
			L"Bei WAV-Speichern Mikrofon mischen. Geraet unter Einstellungen waehlen",
			L"Com Salvar WAV, misturar microfone. Escolha o dispositivo em Configuracoes",
			L"Bij WAV-opslaan microfoon mixen. Apparaat kiezen in Instellingen",
			L"Przy zapisie WAV zmiksuj mikrofon. Urzadzenie w Ustawieniach",
			L"WAV kaydında mikrofonu karıştır. Aygıtı Ayarlar'dan seçin"));
		addTip(IDC_SLIDER_MICLEV, LL14(
			L"マイクのミックスレベル(0〜200%)",
			L"Microphone mix level (0-200%)",
			L"Niveau de mix micro (0-200%)",
			L"Livello mix microfono (0-200%)",
			L"Nivel de mezcla del micro (0-200%)",
			L"마이크 믹스 레벨(0~200%)",
			L"麦克风混音电平(0–200%)",
			L"مستوى مزج الميكروفون (0-200%)",
			L"Уровень микса микрофона (0–200%)",
			L"Mikrofon-Mixpegel (0–200%)",
			L"Nível de mix do microfone (0-200%)",
			L"Microfoon-mixniveau (0-200%)",
			L"Poziom miksu mikrofonu (0–200%)",
			L"Mikrofon karışım seviyesi (0-200%)"));
		addTip(IDC_CHECK3, LL14(
			L"動画(OPやイベント)のoggの時に動画画面も表示します",
			L"Show video window for video ogg (OP, events)",
			L"Afficher la fenetre video pour les ogg video (OP, evenements)",
			L"Mostra la finestra video per ogg video (OP, eventi)",
			L"Mostrar ventana de video para ogg de video (OP, eventos)",
			L"비디오 ogg(OP, 이벤트) 재생 시 비디오 창 표시",
			L"播放视频ogg（OP、事件）时同时显示视频画面",
			L"عرض نافذة الفيديو لملف ogg بالفيديو (OP، الأحداث)",
			L"Показывать окно видео для видео-ogg (OP, события)",
			L"Videofenster fur Video-ogg anzeigen (OP, Ereignisse)",
			L"Mostrar janela de video para ogg de video (OP, eventos)",
			L"Videovenster tonen voor video-ogg (OP, events)",
			L"Poka? okno wideo dla ogg wideo (OP, zdarzenia)",
			L"Video ogg (OP, olaylar) icin video penceresini goster"));
		addTip(IDC_CHECK4, LL14(
			L"スペクトルアナライザー(波形)をモノラル表示、ステレオ表示切り替えを行います",
			L"Switch spectrum analyzer between mono and stereo",
			L"Basculer l'analyseur de spectre entre mono et stereo",
			L"Passa l'analizzatore di spettro tra mono e stereo",
			L"Cambiar el analizador de espectro entre mono y estereo",
			L"스펙트럼 분석기(파형) 모노/스테레오 전환",
			L"切换频谱分析器的单声道/立体声显示",
			L"تبديل محلل الطيف بين أحادي وستريو",
			L"Переключить анализатор спектра между моно и стерео",
			L"Spektrumanalysator zwischen Mono und Stereo umschalten",
			L"Alternar analisador de espectro entre mono e estereo",
			L"Spectrumanalyzer wisselen tussen mono en stereo",
			L"Prze??cz analizator widma mi?dzy mono a stereo",
			L"Spektrum analizorunu mono ve stereo aras?nda de?i?tir"));

		addTip(IDC_BUTTON14, LL14(
			L"「再生するゲーム」で選択されたゲームの演奏を開始します",
			L"Start playback of games selected in \"Games to play\"",
			L"Demarrer la lecture des jeux selectionnes dans « Jeux a lire »",
			L"Avvia la riproduzione dei giochi selezionati in \"Giochi da riprodurre\"",
			L"Iniciar la reproduccion de los juegos seleccionados en \"Juegos a reproducir\"",
			L"「재생할 게임」에서 선택한 게임 재생 시작",
			L"开始播放「播放游戏」中选中的游戏",
			L"بدء تشغيل الألعاب المحددة في \"ألعاب للتشغيل\"",
			L"Начать воспроизведение игр, выбранных в «Игры для воспроизведения»",
			L"Wiedergabe der in „Wiederzugebende Spiele“ ausgewahlten Spiele starten",
			L"Iniciar reproducao dos jogos selecionados em \"Jogos a reproduzir\"",
			L"Start afspelen van spellen geselecteerd in \"Af te spelen spellen\"",
			L"Rozpocznij odtwarzanie gier wybranych w „Grach do odtworzenia”",
			L"\"Oynatilacak Oyunlar\"da secilen oyunlari calmaya basla"));

		addTip(IDC_BUTTON12, LL14(
			L"拡張パネルを開く/閉じる",
			L"Open/close extended panel",
			L"Ouvrir/fermer le panneau etendu",
			L"Apri/chiudi pannello esteso",
			L"Abrir/cerrar panel extendido",
			L"확장 패널 열기/닫기",
			L"打开/关闭扩展面板",
			L"فتح/إغلاق اللوحة الممتدة",
			L"Открыть/закрыть расширенную панель",
			L"Erweitertes Panel offnen/schliesen",
			L"Abrir/fechar painel estendido",
			L"Uitgebreid paneel openen/sluiten",
			L"Otworz/zamknij rozszerzony panel",
			L"Geni?letilmi? paneli ac/kapat"));

		addTip(IDC_BUTTON21, LL14(
			L"各種設定を行います。",
			L"Open settings.",
			L"Ouvrir les parametres.",
			L"Apri le impostazioni.",
			L"Abrir configuracion.",
			L"설정 열기.",
			L"打开设置。",
			L"فتح الإعدادات.",
			L"Открыть настройки.",
			L"Einstellungen offnen.",
			L"Abrir configuracoes.",
			L"Instellingen openen.",
			L"Otworz ustawienia.",
			L"Ayarlar? ac."));

		addTip(IDC_CHECK5, LL14(
			L"「再生するゲーム」で選択されているゲームをランダムに演奏します",
			L"Random play from selected games",
			L"Lecture aleatoire depuis les jeux selectionnes",
			L"Riproduzione casuale dai giochi selezionati",
			L"Reproduccion aleatoria de los juegos seleccionados",
			L"「재생할 게임」에서 선택한 게임을 랜덤으로 재생",
			L"从所选游戏中随机播放",
			L"تشغيل عشوائي من الألعاب المحددة",
			L"Случайное воспроизведение из выбранных игр",
			L"Zufallige Wiedergabe aus ausgewahlten Spielen",
			L"Reproducao aleatoria dos jogos selecionados",
			L"Willekeurig afspelen van geselecteerde spellen",
			L"Losowe odtwarzanie z wybranych gier",
			L"Secilen oyunlardan rastgele oynat"));
		addTip(IDC_CHECK6, LL14(
			L"「再生するゲーム」で選択されているゲームを順番に演奏します", /* 日本語 */
			L"Plays the games selected in \"Games to Play\" in order.", /* 英語 */
			L"Joue les jeux sélectionnés dans « Jeux à lire » dans l'ordre.", /* フランス語 */
			L"Riproduce i giochi selezionati in \"Giochi da riprodurre\" in ordine.", /* イタリア語 */
			L"Reproduce los juegos seleccionados en \"Juegos a reproducir\" en orden.", /* スペイン語 */
			L"「재생할 게임」에서 선택된 게임을 순서대로 연주합니다.", /* 韓国語 */
			L"按照顺序播放“播放游戏”中选中的游戏。", /* 中国語 */
			L"تشغيل الألعاب المحددة في \"ألعاب للتشغيل\" بالترتيب.", /* アラビア語 */
			L"Последовательно воспроизводит игры, выбранные в «Игры для воспроизведения».", /* ロシア語 */
			L"Spielt die in „Wiederzugebende Spiele“ ausgewählten Spiele der Reihe nach ab.", /* ドイツ語 */
			L"Reproduz os jogos selecionados em \"Jogos a reproduzir\" em ordem.", /* ポルトガル語 */
			L"Speelt de spellen geselecteerd in \"Af te spelen spellen\" op volgorde af.", /* オランダ語 */
			L"Odtwarza gry wybrane w „Gry do odtworzenia” w odpowiedniej kolejności.", /* ポーランド語 */
			L"\"Oynatılacak Oyunlar\" kısmında seçilen oyunları sırayla oynatır.")); /* トルコ語 */

		addTip(IDC_CHECK7, LL14(L"「再生するゲーム」の対象：YS6 ナピシュテムの匣", L"Include in \"Games to play\": Ys VI Napishtim no Hako", L"Inclure dans « Jeux a lire » : Ys VI Napishtim no Hako", L"Includi in \"Giochi da riprodurre\": Ys VI Napishtim no Hako", L"Incluir en \"Juegos a reproducir\": Ys VI Napishtim no Hako", L"「재생할 게임」 대상: Ys VI Napishtim no Hako", L"纳入「播放游戏」：Ys VI Napishtim no Hako", L"ضمن \"ألعاب للتشغيل\": Ys VI Napishtim no Hako", L"В «Игры для воспроизведения»: Ys VI Napishtim no Hako", L"In „Wiederzugebende Spiele“: Ys VI Napishtim no Hako", L"Incluir em \"Jogos a reproduzir\": Ys VI Napishtim no Hako", L"Opnemen in \"Af te spelen spellen\": Ys VI Napishtim no Hako", L"W „Grach do odtworzenia”: Ys VI Napishtim no Hako", L"\"Oynatilacak Oyunlar\" hedefi: Ys VI Napishtim no Hako"));
		addTip(IDC_CHECK8, LL14(L"「再生するゲーム」の対象：YS フェルガナの誓い", L"Include in \"Games to play\": Ys Felghana no Chikai", L"Inclure dans « Jeux a lire » : Ys Felghana no Chikai", L"Includi in \"Giochi da riprodurre\": Ys Felghana no Chikai", L"Incluir en \"Juegos a reproducir\": Ys Felghana no Chikai", L"「재생할 게임」 대상: Ys Felghana no Chikai", L"纳入「播放游戏」：Ys Felghana no Chikai", L"ضمن \"ألعاب للتشغيل\": Ys Felghana no Chikai", L"В «Игры для воспроизведения»: Ys Felghana no Chikai", L"In „Wiederzugebende Spiele“: Ys Felghana no Chikai", L"Incluir em \"Jogos a reproduzir\": Ys Felghana no Chikai", L"Opnemen in \"Af te spelen spellen\": Ys Felghana no Chikai", L"W „Grach do odtworzenia”: Ys Felghana no Chikai", L"\"Oynatilacak Oyunlar\" hedefi: Ys Felghana no Chikai"));
		addTip(IDC_CHECK9, LL14(L"「再生するゲーム」の対象：英雄伝説6空の軌跡FC", L"Include in \"Games to play\": Legend of Heroes VI Trails in the Sky FC", L"Inclure dans « Jeux a lire » : Legende des Heros VI Les Sentiers du Ciel FC", L"Includi in \"Giochi da riprodurre\": Legend of Heroes VI Trails in the Sky FC", L"Incluir en \"Juegos a reproducir\": Legend of Heroes VI Trails in the Sky FC", L"「재생할 게임」 대상: 영웅전설6 하늘의 궤적 FC", L"纳入「播放游戏」：英雄传说6 空之轨迹FC", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes VI Trails in the Sky FC", L"В «Игры для воспроизведения»: Legend of Heroes VI Trails in the Sky FC", L"In „Wiederzugebende Spiele“: Legend of Heroes VI Trails in the Sky FC", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes VI Trails in the Sky FC", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes VI Trails in the Sky FC", L"W „Grach do odtworzenia”: Legend of Heroes VI Trails in the Sky FC", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes VI Trails in the Sky FC"));
		addTip(IDC_CHECK10, LL14(L"「再生するゲーム」の対象：英雄伝説6空の軌跡SC", L"Include in \"Games to play\": Legend of Heroes VI Trails in the Sky SC", L"Inclure dans « Jeux a lire » : Legende des Heros VI Les Sentiers du Ciel SC", L"Includi in \"Giochi da riprodurre\": Legend of Heroes VI Trails in the Sky SC", L"Incluir en \"Juegos a reproducir\": Legend of Heroes VI Trails in the Sky SC", L"「재생할 게임」 대상: 영웅전설6 하늘의 궤적 SC", L"纳入「播放游戏」：英雄传说6 空之轨迹SC", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes VI Trails in the Sky SC", L"В «Игры для воспроизведения»: Legend of Heroes VI Trails in the Sky SC", L"In „Wiederzugebende Spiele“: Legend of Heroes VI Trails in the Sky SC", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes VI Trails in the Sky SC", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes VI Trails in the Sky SC", L"W „Grach do odtworzenia”: Legend of Heroes VI Trails in the Sky SC", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes VI Trails in the Sky SC"));
		addTip(IDC_CHECK11, LL14(L"「再生するゲーム」の対象：YS オリジン", L"Include in \"Games to play\": Ys Origin", L"Inclure dans « Jeux a lire » : Ys Origin", L"Includi in \"Giochi da riprodurre\": Ys Origin", L"Incluir en \"Juegos a reproducir\": Ys Origin", L"「재생할 게임」 대상: Ys Origin", L"纳入「播放游戏」：Ys Origin", L"ضمن \"ألعاب للتشغيل\": Ys Origin", L"В «Игры для воспроизведения»: Ys Origin", L"In „Wiederzugebende Spiele“: Ys Origin", L"Incluir em \"Jogos a reproduzir\": Ys Origin", L"Opnemen in \"Af te spelen spellen\": Ys Origin", L"W „Grach do odtworzenia”: Ys Origin", L"\"Oynatilacak Oyunlar\" hedefi: Ys Origin"));
		addTip(IDC_CHECK12, LL14(L"「再生するゲーム」の対象：英雄伝説6空の軌跡TC", L"Include in \"Games to play\": Legend of Heroes VI Trails in the Sky The 3rd", L"Inclure dans « Jeux a lire » : Legende des Heros VI Les Sentiers du Ciel The 3rd", L"Includi in \"Giochi da riprodurre\": Legend of Heroes VI Trails in the Sky The 3rd", L"Incluir en \"Juegos a reproducir\": Legend of Heroes VI Trails in the Sky The 3rd", L"「재생할 게임」 대상: 영웅전설6 하늘의 궤적 The 3rd", L"纳入「播放游戏」：英雄传说6 空之轨迹The3rd", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes VI Trails in the Sky The 3rd", L"В «Игры для воспроизведения»: Legend of Heroes VI Trails in the Sky The 3rd", L"In „Wiederzugebende Spiele“: Legend of Heroes VI Trails in the Sky The 3rd", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes VI Trails in the Sky The 3rd", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes VI Trails in the Sky The 3rd", L"W „Grach do odtworzenia”: Legend of Heroes VI Trails in the Sky The 3rd", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes VI Trails in the Sky The 3rd"));
		addTip(IDC_CHECK13, LL14(L"「再生するゲーム」の対象：ZWEI II", L"Include in \"Games to play\": ZWEI II", L"Inclure dans « Jeux a lire » : ZWEI II", L"Includi in \"Giochi da riprodurre\": ZWEI II", L"Incluir en \"Juegos a reproducir\": ZWEI II", L"「재생할 게임」 대상: ZWEI II", L"纳入「播放游戏」：ZWEI II", L"ضمن \"ألعاب للتشغيل\": ZWEI II", L"В «Игры для воспроизведения»: ZWEI II", L"In „Wiederzugebende Spiele“: ZWEI II", L"Incluir em \"Jogos a reproduzir\": ZWEI II", L"Opnemen in \"Af te spelen spellen\": ZWEI II", L"W „Grach do odtworzenia”: ZWEI II", L"\"Oynatilacak Oyunlar\" hedefi: ZWEI II"));
		addTip(IDC_CHECK14, LL14(L"「再生するゲーム」の対象：Ys Chronicles Ys 1", L"Include in \"Games to play\": Ys Chronicles Ys 1", L"Inclure dans « Jeux a lire » : Ys Chronicles Ys 1", L"Includi in \"Giochi da riprodurre\": Ys Chronicles Ys 1", L"Incluir en \"Juegos a reproducir\": Ys Chronicles Ys 1", L"「재생할 게임」 대상: Ys Chronicles Ys 1", L"纳入「播放游戏」：Ys Chronicles Ys 1", L"ضمن \"ألعاب للتشغيل\": Ys Chronicles Ys 1", L"В «Игры для воспроизведения»: Ys Chronicles Ys 1", L"In „Wiederzugebende Spiele“: Ys Chronicles Ys 1", L"Incluir em \"Jogos a reproduzir\": Ys Chronicles Ys 1", L"Opnemen in \"Af te spelen spellen\": Ys Chronicles Ys 1", L"W „Grach do odtworzenia”: Ys Chronicles Ys 1", L"\"Oynatilacak Oyunlar\" hedefi: Ys Chronicles Ys 1"));
		addTip(IDC_CHECK15, LL14(L"「再生するゲーム」の対象：Ys Chronicles Ys 2", L"Include in \"Games to play\": Ys Chronicles Ys 2", L"Inclure dans « Jeux a lire » : Ys Chronicles Ys 2", L"Includi in \"Giochi da riprodurre\": Ys Chronicles Ys 2", L"Incluir en \"Juegos a reproducir\": Ys Chronicles Ys 2", L"「재생할 게임」 대상: Ys Chronicles Ys 2", L"纳入「播放游戏」：Ys Chronicles Ys 2", L"ضمن \"ألعاب للتشغيل\": Ys Chronicles Ys 2", L"В «Игры для воспроизведения»: Ys Chronicles Ys 2", L"In „Wiederzugebende Spiele“: Ys Chronicles Ys 2", L"Incluir em \"Jogos a reproduzir\": Ys Chronicles Ys 2", L"Opnemen in \"Af te spelen spellen\": Ys Chronicles Ys 2", L"W „Grach do odtworzenia”: Ys Chronicles Ys 2", L"\"Oynatilacak Oyunlar\" hedefi: Ys Chronicles Ys 2"));
		addTip(IDC_CHECK16, LL14(L"「再生するゲーム」の対象：XANADU NEXT", L"Include in \"Games to play\": XANADU NEXT", L"Inclure dans « Jeux a lire » : XANADU NEXT", L"Includi in \"Giochi da riprodurre\": XANADU NEXT", L"Incluir en \"Juegos a reproducir\": XANADU NEXT", L"「재생할 게임」 대상: XANADU NEXT", L"纳入「播放游戏」：XANADU NEXT", L"ضمن \"ألعاب للتشغيل\": XANADU NEXT", L"В «Игры для воспроизведения»: XANADU NEXT", L"In „Wiederzugebende Spiele“: XANADU NEXT", L"Incluir em \"Jogos a reproduzir\": XANADU NEXT", L"Opnemen in \"Af te spelen spellen\": XANADU NEXT", L"W „Grach do odtworzenia”: XANADU NEXT", L"\"Oynatilacak Oyunlar\" hedefi: XANADU NEXT"));
		addTip(IDC_CHECK17, LL14(L"「再生するゲーム」の対象：Ys 完全版 Ys 1", L"Include in \"Games to play\": Ys Complete Ys 1", L"Inclure dans « Jeux a lire » : Ys Complete Ys 1", L"Includi in \"Giochi da riprodurre\": Ys Complete Ys 1", L"Incluir en \"Juegos a reproducir\": Ys Complete Ys 1", L"「재생할 게임」 대상: Ys Complete Ys 1", L"纳入「播放游戏」：Ys Complete Ys 1", L"ضمن \"ألعاب للتشغيل\": Ys Complete Ys 1", L"В «Игры для воспроизведения»: Ys Complete Ys 1", L"In „Wiederzugebende Spiele“: Ys Complete Ys 1", L"Incluir em \"Jogos a reproduzir\": Ys Complete Ys 1", L"Opnemen in \"Af te spelen spellen\": Ys Complete Ys 1", L"W „Grach do odtworzenia”: Ys Complete Ys 1", L"\"Oynatilacak Oyunlar\" hedefi: Ys Complete Ys 1"));
		addTip(IDC_CHECK18, LL14(L"「再生するゲーム」の対象：Ys 完全版 Ys 2", L"Include in \"Games to play\": Ys Complete Ys 2", L"Inclure dans « Jeux a lire » : Ys Complete Ys 2", L"Includi in \"Giochi da riprodurre\": Ys Complete Ys 2", L"Incluir en \"Juegos a reproducir\": Ys Complete Ys 2", L"「재생할 게임」 대상: Ys Complete Ys 2", L"纳入「播放游戏」：Ys Complete Ys 2", L"ضمن \"ألعاب للتشغيل\": Ys Complete Ys 2", L"В «Игры для воспроизведения»: Ys Complete Ys 2", L"In „Wiederzugebende Spiele“: Ys Complete Ys 2", L"Incluir em \"Jogos a reproduzir\": Ys Complete Ys 2", L"Opnemen in \"Af te spelen spellen\": Ys Complete Ys 2", L"W „Grach do odtworzenia”: Ys Complete Ys 2", L"\"Oynatilacak Oyunlar\" hedefi: Ys Complete Ys 2"));
		addTip(IDC_CHECK19, LL14(L"「再生するゲーム」の対象：Sorcerian Original", L"Include in \"Games to play\": Sorcerian Original", L"Inclure dans « Jeux a lire » : Sorcerian Original", L"Includi in \"Giochi da riprodurre\": Sorcerian Original", L"Incluir en \"Juegos a reproducir\": Sorcerian Original", L"「재생할 게임」 대상: Sorcerian Original", L"纳入「播放游戏」：Sorcerian Original", L"ضمن \"ألعاب للتشغيل\": Sorcerian Original", L"В «Игры для воспроизведения»: Sorcerian Original", L"In „Wiederzugebende Spiele“: Sorcerian Original", L"Incluir em \"Jogos a reproduzir\": Sorcerian Original", L"Opnemen in \"Af te spelen spellen\": Sorcerian Original", L"W „Grach do odtworzenia”: Sorcerian Original", L"\"Oynatilacak Oyunlar\" hedefi: Sorcerian Original"));
		addTip(IDC_CHECK20, LL14(L"「再生するゲーム」の対象：Zwei!!", L"Include in \"Games to play\": Zwei!!", L"Inclure dans « Jeux a lire » : Zwei!!", L"Includi in \"Giochi da riprodurre\": Zwei!!", L"Incluir en \"Juegos a reproducir\": Zwei!!", L"「재생할 게임」 대상: Zwei!!", L"纳入「播放游戏」：Zwei!!", L"ضمن \"ألعاب للتشغيل\": Zwei!!", L"В «Игры для воспроизведения»: Zwei!!", L"In „Wiederzugebende Spiele“: Zwei!!", L"Incluir em \"Jogos a reproduzir\": Zwei!!", L"Opnemen in \"Af te spelen spellen\": Zwei!!", L"W „Grach do odtworzenia”: Zwei!!", L"\"Oynatilacak Oyunlar\" hedefi: Zwei!!"));
		addTip(IDC_CHECK21, LL14(L"「再生するゲーム」の対象：ぐるみん", L"Include in \"Games to play\": Gurumin", L"Inclure dans « Jeux a lire » : Gurumin", L"Includi in \"Giochi da riprodurre\": Gurumin", L"Incluir en \"Juegos a reproducir\": Gurumin", L"「재생할 게임」 대상: 그루민", L"纳入「播放游戏」：咕噜小天使", L"ضمن \"ألعاب للتشغيل\": Gurumin", L"В «Игры для воспроизведения»: Gurumin", L"In „Wiederzugebende Spiele“: Gurumin", L"Incluir em \"Jogos a reproduzir\": Gurumin", L"Opnemen in \"Af te spelen spellen\": Gurumin", L"W „Grach do odtworzenia”: Gurumin", L"\"Oynatilacak Oyunlar\" hedefi: Gurumin"));
		addTip(IDC_CHECK22, LL14(L"「再生するゲーム」の対象：ダイナソア リザレクション", L"Include in \"Games to play\": Dinosaur Resurrection", L"Inclure dans « Jeux a lire » : Resurrection Dinosaure", L"Includi in \"Giochi da riprodurre\": Resurrezione Dinosauro", L"Incluir en \"Juegos a reproducir\": Resurreccion Dinosaurio", L"「재생할 게임」 대상: 공룡 부활", L"纳入「播放游戏」：恐龙复活", L"ضمن \"ألعاب للتشغيل\": Dinosaur Resurrection", L"В «Игры для воспроизведения»: Dinosaur Resurrection", L"In „Wiederzugebende Spiele“: Dinosaurier Auferstehung", L"Incluir em \"Jogos a reproduzir\": Ressurreicao Dinossauro", L"Opnemen in \"Af te spelen spellen\": Dinosaurus Herrijzenis", L"W „Grach do odtworzenia”: Dinozaur Zmartwychwstanie", L"\"Oynatilacak Oyunlar\" hedefi: Dinozor Dirilisi"));
		addTip(IDC_CHECK23, LL14(L"「再生するゲーム」の対象：ブランディッシュ４", L"Include in \"Games to play\": Brandish4", L"Inclure dans « Jeux a lire » : Brandish4", L"Includi in \"Giochi da riprodurre\": Brandish4", L"Incluir en \"Juegos a reproducir\": Brandish4", L"「재생할 게임」 대상: Brandish4", L"纳入「播放游戏」：Brandish4", L"ضمن \"ألعاب للتشغيل\": Brandish4", L"В «Игры для воспроизведения»: Brandish4", L"In „Wiederzugebende Spiele“: Brandish4", L"Incluir em \"Jogos a reproduzir\": Brandish4", L"Opnemen in \"Af te spelen spellen\": Brandish4", L"W „Grach do odtworzenia”: Brandish4", L"\"Oynatilacak Oyunlar\" hedefi: Brandish4"));
		addTip(IDC_CHECK24, LL14(L"「再生するゲーム」の対象：英雄伝説III 白き魔女", L"Include in \"Games to play\": Legend of Heroes III White Witch", L"Inclure dans « Jeux a lire » : Legende des Heros III Sorciere Blanche", L"Includi in \"Giochi da riprodurre\": Legend of Heroes III Strega Bianca", L"Incluir en \"Juegos a reproducir\": Legend of Heroes III Bruja Blanca", L"「재생할 게임」 대상: 영웅전설III 흰 마녀", L"纳入「播放游戏」：英雄传说III 白之魔女", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes III White Witch", L"В «Игры для воспроизведения»: Legend of Heroes III White Witch", L"In „Wiederzugebende Spiele“: Legend of Heroes III Weise Hexe", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes III Bruxa Branca", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes III White Witch", L"W „Grach do odtworzenia”: Legend of Heroes III Biala Czarownica", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes III White Witch"));
		addTip(IDC_CHECK25, LL14(L"「再生するゲーム」の対象：英雄伝説IV 朱紅い雫", L"Include in \"Games to play\": Legend of Heroes IV Tear of Vermillion", L"Inclure dans « Jeux a lire » : Legende des Heros IV Larme de Vermillon", L"Includi in \"Giochi da riprodurre\": Legend of Heroes IV Lacrima di Vermiglio", L"Incluir en \"Juegos a reproducir\": Legend of Heroes IV Lagrima de Bermellon", L"「재생할 게임」 대상: 영웅전설IV 주홍의 눈물", L"纳入「播放游戏」：英雄传说IV 朱红之泪", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes IV Tear of Vermillion", L"В «Игры для воспроизведения»: Legend of Heroes IV Tear of Vermillion", L"In „Wiederzugebende Spiele“: Legend of Heroes IV Trane der Purpur", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes IV Tear of Vermillion", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes IV Tear of Vermillion", L"W „Grach do odtworzenia”: Legend of Heroes IV Tear of Vermillion", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes IV Tear of Vermillion"));
		addTip(IDC_CHECK26, LL14(L"「再生するゲーム」の対象：英雄伝説V 海の檻歌", L"Include in \"Games to play\": Legend of Heroes V Cagesong of the Ocean", L"Inclure dans « Jeux a lire » : Legende des Heros V Chant des Profondeurs", L"Includi in \"Giochi da riprodurre\": Legend of Heroes V Canto dell'Oceano", L"Incluir en \"Juegos a reproducir\": Legend of Heroes V Cantico del Oceano", L"「재생할 게임」 대상: 영웅전설V 바다의 감옥가", L"纳入「播放游戏」：英雄传说V 海之槛歌", L"ضمن \"ألعاب للتشغيل\": Legend of Heroes V Cagesong of the Ocean", L"В «Игры для воспроизведения»: Legend of Heroes V Cagesong of the Ocean", L"In „Wiederzugebende Spiele“: Legend of Heroes V Käfiglied des Ozeans", L"Incluir em \"Jogos a reproduzir\": Legend of Heroes V Cagesong of the Ocean", L"Opnemen in \"Af te spelen spellen\": Legend of Heroes V Cagesong of the Ocean", L"W „Grach do odtworzenia”: Legend of Heroes V Cagesong of the Ocean", L"\"Oynatilacak Oyunlar\" hedefi: Legend of Heroes V Cagesong of the Ocean"));
		addTip(IDC_BUTTON58, LL14(
			L"mp3/m4a/ogg/flacに埋め込まれているジャケットを表示します。", /* 日本語 */
			L"Shows album art embedded in mp3/m4a/ogg/flac files.", /* 英語 */
			L"Affiche la pochette intégrée dans les fichiers mp3/m4a/ogg/flac.", /* フランス語 */
			L"Mostra la copertina incorporata nei file mp3/m4a/ogg/flac.", /* イタリア語 */
			L"Muestra la portada incrustada en los archivos mp3/m4a/ogg/flac.", /* スペイン語 */
			L"mp3/m4a/ogg/flac에 포함된 앨범 아트를 표시합니다.", /* 韓国語 */
			L"显示 mp3/m4a/ogg/flac 中嵌入的专辑封面。", /* 中国語 */
			L"عرض غلاف الألبوم المضمن في ملفات mp3/m4a/ogg/flac.", /* アラビア語 */
			L"Показывает обложку альбома, встроенную в файлы mp3/m4a/ogg/flac.", /* ロシア語 */
			L"Zeigt das in mp3/m4a/ogg/flac-Dateien eingebettete Albumcover an.", /* ドイツ語 */
			L"Mostra a capa do álbum incorporada em arquivos mp3/m4a/ogg/flac.", /* ポルトガル語 */
			L"Toont de albumhoes die is ingebed in mp3/m4a/ogg/flac-bestanden.", /* オランダ語 */
			L"Pokazuje okładkę albumu osadzoną w plikach mp3/m4a/ogg/flac.", /* ポーランド語 */
			L"mp3/m4a/ogg/flac dosyalarına gömülü albüm kapağını gösterir.")); /* トルコ語 */

		addTip(IDC_EDIT1, LL14(
			L"次の曲へいくためのループ回数を設定します",
			L"Set loop count before next track",
			L"Definir le nombre de boucles avant la piste suivante",
			L"Imposta il numero di loop prima del brano successivo",
			L"Establecer el numero de bucles antes de la siguiente pista",
			L"다음 곡으로 가기 전 루프 횟수 설정",
			L"设置进入下一曲目前的循环次数",
			L"تعيين عدد الحلقات قبل الانتقال للمسار التالي",
			L"Установить количество повторов перед следующим треком",
			L"Anzahl der Wiederholungen vor dem nachsten Titel festlegen",
			L"Definir numero de loops antes da proxima faixa",
			L"Aantal herhalingen instellen voor volgend nummer",
			L"Ustaw liczb? p?tli przed nast?pnym utworem",
			L"Sonraki parcaya gecmeden once dongu say?s?n? ayarla"));

		addTip(IDC_STATICaaab, LL14(
			L"Windows全体の主音量（IDC_SLIDER1と連動）",
			L"Overall Windows master volume (linked to IDC_SLIDER1)",
			L"Volume principal Windows (lie a IDC_SLIDER1)",
			L"Volume master Windows (collegato a IDC_SLIDER1)",
			L"Volumen maestro de Windows (vinculado a IDC_SLIDER1)",
			L"Windows 전체 마스터 볼륨(IDC_SLIDER1 연동)",
			L"Windows 整体主音量（与 IDC_SLIDER1 联动）",
			L"مستوى الصوت الرئيسي لـ Windows (مرتبط بـ IDC_SLIDER1)",
			L"Общая громкость Windows (связана с IDC_SLIDER1)",
			L"Windows-Hauptlautstarke (verbunden mit IDC_SLIDER1)",
			L"Volume mestre do Windows (ligado ao IDC_SLIDER1)",
			L"Hoofdvolume Windows (gekoppeld aan IDC_SLIDER1)",
			L"Glowna glosnosc Windows (powiazana z IDC_SLIDER1)",
			L"Windows ana ses seviyesi (IDC_SLIDER1 ile bagli)"));
		addTip(IDC_STATICaaa, LL14(
			L"このプレイヤ専用のDirectSound音量（IDC_SLIDER3と連動）",
			L"DirectSound volume for this player only (linked to IDC_SLIDER3)",
			L"Volume DirectSound de ce lecteur uniquement (lie a IDC_SLIDER3)",
			L"Volume DirectSound solo per questo lettore (collegato a IDC_SLIDER3)",
			L"Volumen DirectSound solo de este reproductor (vinculado a IDC_SLIDER3)",
			L"이 플레이어 전용 DirectSound 볼륨(IDC_SLIDER3 연동)",
			L"此播放器专用 DirectSound 音量（与 IDC_SLIDER3 联动）",
			L"مستوى DirectSound لهذا المشغل فقط (مرتبط بـ IDC_SLIDER3)",
			L"Громкость DirectSound только для этого плеера (связана с IDC_SLIDER3)",
			L"DirectSound-Lautstarke nur fur diesen Player (verbunden mit IDC_SLIDER3)",
			L"Volume DirectSound apenas deste player (ligado ao IDC_SLIDER3)",
			L"DirectSound-volume alleen voor deze speler (gekoppeld aan IDC_SLIDER3)",
			L"Glosnosc DirectSound tylko tego odtwarzacza (powiazana z IDC_SLIDER3)",
			L"Yalnizca bu oynatici DirectSound sesi (IDC_SLIDER3 ile bagli)"));
		addTip(IDC_STATICaaac, LL14(
			L"拡張音量（IDC_SLIDER4と連動）\n100%超も可能。イコライザーで音割れを抑えます",
			L"Extended volume (linked to IDC_SLIDER4)\nCan exceed 100%. Clipping is handled by the equalizer",
			L"Volume etendu (lie a IDC_SLIDER4)\nPeut depasser 100 %. L'egaliseur evite la saturation",
			L"Volume esteso (collegato a IDC_SLIDER4)\nOltre 100%. L'equalizzatore evita il clipping",
			L"Volumen extendido (vinculado a IDC_SLIDER4)\nMas del 100%. El ecualizador evita el recorte",
			L"확장 볼륨(IDC_SLIDER4 연동)\n100% 초과 가능. 이퀄라이저가 클리핑 방지",
			L"扩展音量（与 IDC_SLIDER4 联动）\n可超过100%。均衡器防止削波",
			L"مستوى الصوت الممتد (مرتبط بـ IDC_SLIDER4)\nيمكن تجاوز 100%. المعادل يمنع القص",
			L"Доп. громкость (связана с IDC_SLIDER4)\nСвыше 100%. Эквалайзер предотвращает клиппинг",
			L"Erweiterte Lautstarke (IDC_SLIDER4)\nUber 100%. Equalizer verhindert Clipping",
			L"Volume estendido (IDC_SLIDER4)\nAcima de 100%. Equalizador evita clipping",
			L"Uitgebreid volume (IDC_SLIDER4)\nBoven 100%. Equalizer voorkomt clipping",
			L"Rozszerzona glosnosc (IDC_SLIDER4)\nPowyzej 100%. Korektor zapobiega przesterowaniu",
			L"Genisletilmis ses (IDC_SLIDER4)\n%%100 ustune cikabilir. Ekolayzer kirpilmayi onler"));
		addTip(IDC_STATICaaad, LL14(
			L"次の曲へ進むまでのループ回数（IDC_EDIT1と連動）",
			L"Loop count before next track (linked to IDC_EDIT1)",
			L"Nombre de boucles avant la piste suivante (lie a IDC_EDIT1)",
			L"Numero di loop prima del brano successivo (collegato a IDC_EDIT1)",
			L"Numero de bucles antes de la siguiente pista (vinculado a IDC_EDIT1)",
			L"다음 곡까지 루프 횟수(IDC_EDIT1 연동)",
			L"进入下一曲前的循环次数（与 IDC_EDIT1 联动）",
			L"عدد الحلقات قبل المسار التالي (مرتبط بـ IDC_EDIT1)",
			L"Количество повторов перед следующим треком (связано с IDC_EDIT1)",
			L"Schleifenzahl vor dem nachsten Titel (verbunden mit IDC_EDIT1)",
			L"Numero de loops antes da proxima faixa (ligado ao IDC_EDIT1)",
			L"Aantal herhalingen voor volgend nummer (gekoppeld aan IDC_EDIT1)",
			L"Liczba petli przed nastepnym utworem (powiazana z IDC_EDIT1)",
			L"Sonraki parcaya kadar dongu sayisi (IDC_EDIT1 ile bagli)"));
		addTip(IDC_SLIDER4, LL14(
			L"100%以上の拡張音量を設定できます\nイコライザー処理で音割れを抑えます",
			L"Set extended volume above 100%\nClipping is handled by the equalizer",
			L"Regler un volume etendu au-dela de 100 %\nL'egaliseur evite la saturation",
			L"Imposta volume esteso oltre il 100%\nL'equalizzatore evita il clipping",
			L"Ajustar volumen extendido por encima del 100%\nEl ecualizador evita el recorte",
			L"100% 초과 확장 볼륨 설정\n이퀄라이저가 클리핑 방지",
			L"可设置超过100%的扩展音量\n均衡器防止削波",
			L"تعيين مستوى ممتد فوق 100%\nالمعادل يمنع القص",
			L"Установить доп. громкость выше 100%\nЭквалайзер предотвращает клиппинг",
			L"Erweiterte Lautstarke uber 100% einstellen\nEqualizer verhindert Clipping",
			L"Definir volume estendido acima de 100%\nEqualizador evita clipping",
			L"Uitgebreid volume boven 100% instellen\nEqualizer voorkomt clipping",
			L"Ustaw rozszerzona glosnosc powyzej 100%\nKorektor zapobiega przesterowaniu",
			L"%%100 ustune genisletilmis ses\nEkolayzer kirpilmayi onler"));
		addTip(IDC_STATICds2, LL14(
			L"100%以上の拡張音量を設定できます\nイコライザー処理で音割れを抑えます",
			L"Set extended volume above 100%\nClipping is handled by the equalizer",
			L"Regler un volume etendu au-dela de 100 %\nL'egaliseur evite la saturation",
			L"Imposta volume esteso oltre il 100%\nL'equalizzatore evita il clipping",
			L"Ajustar volumen extendido por encima del 100%\nEl ecualizador evita el recorte",
			L"100% 초과 확장 볼륨 설정\n이퀄라이저가 클리핑 방지",
			L"可设置超过100%的扩展音量\n均衡器防止削波",
			L"تعيين مستوى ممتد فوق 100%\nالمعادل يمنع القص",
			L"Установить доп. громкость выше 100%\nЭквалайзер предотвращает клиппинг",
			L"Erweiterte Lautstarke uber 100% einstellen\nEqualizer verhindert Clipping",
			L"Definir volume estendido acima de 100%\nEqualizador evita clipping",
			L"Uitgebreid volume boven 100% instellen\nEqualizer voorkomt clipping",
			L"Ustaw rozszerzona glosnosc powyzej 100%\nKorektor zapobiega przesterowaniu",
			L"%%100 ustune genisletilmis ses\nEkolayzer kirpilmayi onler"));
		addTip(IDC_SLIDER7, LL14(
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのテンポを変えます。wav,動画は変わりません。", /* 日本語 */
			L"Changes tempo for mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav and video are not affected.)", /* 英語 */
			L"Modifie le tempo pour mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav et vidéo inchangés.)", /* フランス語 */
			L"Cambia il tempo per mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav e video invariati.)", /* イタリア語 */
			L"Cambia el tempo para mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav y video sin cambios.)", /* スペイン語 */
			L"mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi의 템포를 변경합니다. (wav, 동영상은 변경되지 않습니다.)", /* 韓国語 */
			L"更改 mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi 的速度。wav、视频不受影响。", /* 中国語 */
			L"تغيير الإيقاع لـ mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav والفيديو لن يتغيرا.)", /* アラビア語 */
			L"Изменяет темп для mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav и видео не изменятся.)", /* ロシア語 */
			L"Ändert das Tempo für mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav und Video unverändert.)", /* ドイツ語 */
			L"Altera o tempo para mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav e vídeo inalterados.)", /* ポルトガル語 */
			L"Wijzigt tempo voor mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav en video blijven ongewijzigd.)", /* オランダ語 */
			L"Zmienia tempo dla mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav i wideo bez zmian.)", /* ポーランド語 */
			L"mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi için tempoyu değiştirir. wav ve video değişmez.")); /* トルコ語 */

		addTip(IDC_SLIDER8, LL14(
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのピッチを変えます。wav,動画は変わりません。", /* 日本語 */
			L"Changes pitch for mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav and video are not affected.)", /* 英語 */
			L"Modifie la hauteur pour mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav et vidéo inchangés.)", /* フランス語 */
			L"Cambia l'intonazione per mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav e video invariati.)", /* イタリア語 */
			L"Cambia el tono para mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav y video sin cambios.)", /* スペイン語 */
			L"mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi의 피치를 변경합니다. (wav, 동영상은 변경되지 않습니다.)", /* 韓国語 */
			L"更改 mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi 的音高。wav、视频不受影响。", /* 中国語 */
			L"تغيير طبقة الصوت لـ mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav والفيديو لن يتغيرا.)", /* アラビア語 */
			L"Изменяет высоту тона для mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav и видео не изменятся.)", /* ロシア語 */
			L"Ändert die Tonhöhe für mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav und Video unverändert.)", /* ドイツ語 */
			L"Altera o tom para mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav e vídeo inalterados.)", /* ポルトガル語 */
			L"Wijzigt de toonhoogte voor mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav en video blijven ongewijzigd.)", /* オランダ語 */
			L"Zmienia wysokość tonu dla mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi. (wav i wideo bez zmian.)", /* ポーランド語 */
			L"mp3, ogg, flac, m4a, opus, adpcm, dsd, kpi için perdeyi değiştirir. wav ve video değişmez.")); /* トルコ語 */

		addTip(IDC_STATIC_t, LL14(
			L"テンポを100に戻します。",
			L"Resets tempo to 100.",
			L"Réinitialise le tempo à 100.",
			L"Reimposta il tempo a 100.",
			L"Restablece el tempo a 100.",
			L"템포를 100으로 되돌립니다.",
			L"将速度重置为100。",
			L"إعادة تعيين الإيقاع إلى 100.",
			L"Сбросить темп до 100.",
			L"Setzt das Tempo auf 100 zurück.",
			L"Redefine o tempo para 100.",
			L"Zet tempo terug naar 100.",
			L"Resetuje tempo do 100.",
			L"Tempoyu 100'e sıfırlar."));

		addTip(IDC_STATIC_p, LL14(
			L"ピッチを100に戻します。",
			L"Resets pitch to 100.",
			L"Réinitialise la hauteur à 100.",
			L"Reimposta l'intonazione a 100.",
			L"Restablece el tono a 100.",
			L"피치를 100으로 되돌립니다.",
			L"将音高重置为100。",
			L"إعادة تعيين طبقة الصوت إلى 100.",
			L"Сбросить высоту тона до 100.",
			L"Setzt die Tonhöhe auf 100 zurück.",
			L"Redefine o tom para 100.",
			L"Zet de toonhoogte terug naar 100.",
			L"Resetuje wysokość tonu do 100.",
			L"Perdeyi 100'e sıfırlar."));

		CString s;
		s.Format(LL14(
			L"%s\n↑の情報が間違っている時は↓の内容を作者へ\n詳細：Ver %d.%d(%d) Build %d\n\n%s", /* 日本語 */
			L"%s\nIf the info above is incorrect, please send the content below to the author.\nDetails: Ver %d.%d(%d) Build %d\n\n%s", /* 英語 */
			L"%s\nSi les informations ci-dessus sont incorrectes, veuillez envoyer le contenu ci-dessous à l'auteur.\nDétails : Ver %d.%d(%d) Build %d\n\n%s", /* フランス語 */
			L"%s\nSe le informazioni sopra sono errate, invia il contenuto sottostante all'autore.\nDettagli: Ver %d.%d(%d) Build %d\n\n%s", /* イタリア語 */
			L"%s\nSi la información anterior es incorrecta, envíe el contenido a continuación al autor.\nDetalles: Ver %d.%d(%d) Build %d\n\n%s", /* スペイン語 */
			L"%s\n위의 정보가 틀렸을 때는 아래의 내용을 제작자에게 보내주세요.\n상세: Ver %d.%d(%d) Build %d\n\n%s", /* 韓国語 */
			L"%s\n如果上述信息有误，请将以下内容发送给作者。\n详细：Ver %d.%d(%d) Build %d\n\n%s", /* 中国語 */
			L"%s\nإذا كانت المعلومات أعلاه غير صحيحة، يرجى إرسال المحتوى أدناه إلى المؤلف.\nالتفاصيل: الإصدار %d.%d(%d) البناء %d\n\n%s", /* アラビア語 */
			L"%s\nЕсли указанная выше информация неверна, отправьте следующее содержимое автору.\nПодробности: Ver %d.%d(%d) Build %d\n\n%s", /* ロシア語 */
			L"%s\nFalls die obigen Informationen falsch sind, senden Sie bitte den folgenden Inhalt an den Autor.\nDetails: Ver %d.%d(%d) Build %d\n\n%s", /* ドイツ語 */
			L"%s\nSe as informações acima estiverem incorretas, envie o conteúdo abaixo ao autor.\nDetalhes: Ver %d.%d(%d) Build %d\n\n%s", /* ポルトガル語 */
			L"%s\nAls de bovenstaande informatie onjuist is, stuur dan de onderstaande inhoud naar de auteur.\nDetails: Ver %d.%d(%d) Build %d\n\n%s", /* オランダ語 */
			L"%s\nJeśli powyższe informacje są błędne, wyślij poniższą treść do autora.\nSzczegóły: Ver %d.%d(%d) Build %d\n\n%s", /* ポーランド語 */
			L"%s\nYukarıdaki bilgiler yanlışsa, lütfen aşağıdaki içeriği yazara gönderin.\nAyrıntılar: Ver %d.%d(%d) Build %d\n\n%s"), /* トルコ語 */
			os.GetVersionString(), in.dwMajorVersion, in.dwMinorVersion, edition, in.dwBuildNumber, cpus);

		addTip(IDC_STATIC_OS, s);
		addTip(IDC_STATIC_OS2, s);
	}
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	// FFT三角関数テーブルの初期化
	ipTab2[0] = 0;
	int i;
	// 窓関数テーブルの初期化
	for (i = 0; i < 15360; i++) {
		//fnWFilter[i/2] = (1-cos(2*M_PI*(i/2)/BUFSZ))/2; // Hanning窓
		fnWFilter[i] = 0.54f - 0.46f * (float)cos((2.0f * M_PI * i) / (i - 1));//sin(M_PI*(i + 0.5) / 4096); // sin窓
		//fnWFilter[i] = sin(syuha[i/47]/1000.0);
	}

