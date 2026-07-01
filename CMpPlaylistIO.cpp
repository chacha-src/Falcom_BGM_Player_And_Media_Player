#include "stdafx.h"
#include "CMpPlaylistIO.h"
#include "CMediaPlayerDlg.h"
#include "PlayList.h"
#include <vector>

extern CPlayList* pl;
extern CMediaPlayerDlg* mp;
extern save savedata;

static BOOL MpExtEq(const CString& path, LPCTSTR ext)
{
	CString s = path;
	s.MakeLower();
	return s.Right((int)_tcslen(ext)) == ext;
}

BOOL MpIsPlaylistExtension(const CString& path)
{
	return MpExtEq(path, _T(".m3u")) || MpExtEq(path, _T(".m3u8"))
		|| MpExtEq(path, _T(".pls")) || MpExtEq(path, _T(".xspf"));
}

static CString MpDirOfFile(const CString& path)
{
	int p = path.ReverseFind(_T('\\'));
	if (p < 0) p = path.ReverseFind(_T('/'));
	if (p < 0) return _T("");
	return path.Left(p + 1);
}

static CString MpResolvePath(const CString& entry, const CString& baseDir, BOOL resolveRelative)
{
	CString p = entry;
	p.Trim();
	if (p.IsEmpty()) return _T("");
	if (!resolveRelative) return p;
	if (p.GetLength() >= 2 && p[1] == _T(':')) return p;
	if (p.GetLength() >= 2 && (p[0] == _T('\\') && p[1] == _T('\\'))) return p;
	if (!baseDir.IsEmpty() && p[0] != _T('\\') && p[0] != _T('/'))
		return baseDir + p;
	return p;
}

static BOOL MpFileExists(const CString& path)
{
	return path.GetLength() > 0 && ::PathFileExists(path);
}

static CString MpTruncPlField(const CString& s)
{
	if (s.GetLength() < 1024) return s;
	return s.Left(1023);
}

static int MpCountPlaylistFiles()
{
	if (!pl) return 0;
	int lcnt = 0;
	for (; lcnt < 999; ++lcnt) {
		CString s;
		if (lcnt == 0)
			s = _T("playlistu.dat");
		else
			s.Format(_T("playlistu%d.dat"), lcnt);
		if (!PathFileExists(pl->GetModulePath() + s))
			break;
	}
	return lcnt;
}

static void MpEnsurePlaylistBuffer()
{
	if (!pl) return;
	if (!pl->pc) {
		pl->pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		if (!pl->pc) return;
	}
}

static BOOL MpHasDuplicateFol(LPCTSTR fol)
{
	if (!pl || !pl->pc) return FALSE;
	const CString want = NormalizePlaylistPath(fol);
	if (want.IsEmpty()) return FALSE;
	for (int i = 0; i < pl->playcnt; i++) {
		if (NormalizePlaylistPath(pl->pc[i].fol).CompareNoCase(want) == 0) return TRUE;
	}
	return FALSE;
}

static CString MpDecodeAcpBytes(const BYTE* data, int len)
{
#ifdef _UNICODE
	CStringA a((LPCSTR)data, len);
	int wlen = MultiByteToWideChar(CP_ACP, 0, a, (int)a.GetLength(), NULL, 0);
	CStringW w;
	if (wlen > 0) {
		LPWSTR pw = w.GetBuffer(wlen);
		MultiByteToWideChar(CP_ACP, 0, a, (int)a.GetLength(), pw, wlen);
		w.ReleaseBuffer(wlen);
	}
	return CString(w);
#else
	return CString((LPCSTR)data, len);
#endif
}

static CString MpDecodeUtf8Bytes(const BYTE* data, int len, BOOL strict, BOOL& ok)
{
	ok = FALSE;
	if (len <= 0) return _T("");
	CStringW w;
	DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
	int wlen = MultiByteToWideChar(CP_UTF8, flags, (LPCCH)data, len, NULL, 0);
	if (wlen <= 0) return _T("");
	LPWSTR pw = w.GetBuffer(wlen);
	int got = MultiByteToWideChar(CP_UTF8, flags, (LPCCH)data, len, pw, wlen);
	w.ReleaseBuffer((got > 0) ? got : 0);
	if (got <= 0) return _T("");
	ok = TRUE;
	return CString(w);
}

// forceUtf8=FALSE: BOM / .m3u8 / 厳密UTF-8として成立ならUTF-8、でなければACP(Shift-JIS等)
// forceUtf8=TRUE : UTF-8として読み込む(不正バイトは寛容デコード)
static CString MpReadAllText(const CString& path, BOOL forceUtf8, BOOL& outUtf8)
{
	outUtf8 = FALSE;
	CFile f;
	if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite)) return _T("");

	ULONGLONG sz = f.GetLength();
	if (sz == 0 || sz > 64 * 1024 * 1024) { f.Close(); return _T(""); }

	std::vector<BYTE> buf((size_t)sz + 4, 0);
	UINT rd = f.Read(buf.data(), (UINT)sz);
	f.Close();
	if (rd == 0) return _T("");

	int offset = 0;
	BOOL hasBom = FALSE;
	if (rd >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
		offset = 3;
		hasBom = TRUE;
	}

	const int payload = rd - offset;
	const BYTE* body = buf.data() + offset;
	const BOOL extUtf8 = MpExtEq(path, _T(".m3u8"));

	if (forceUtf8) {
		BOOL ok = FALSE;
		CString u = MpDecodeUtf8Bytes(body, payload, FALSE, ok);
		if (ok) {
			outUtf8 = TRUE;
			return u;
		}
	}

	// 自動判定: BOM / .m3u8 / 厳密UTF-8として成立 → UTF-8
	if (hasBom || extUtf8 || !forceUtf8) {
		BOOL ok = FALSE;
		CString u = MpDecodeUtf8Bytes(body, payload, TRUE, ok);
		if (ok && !u.IsEmpty()) {
			outUtf8 = TRUE;
			return u;
		}
	}

	// Shift-JIS 等システム ANSI(ACP) へフォールバック
	outUtf8 = FALSE;
	return MpDecodeAcpBytes(body, payload);
}

BOOL MpExportPlaylistM3U(const CString& path, BOOL utf8)
{
	if (!pl || pl->pc == NULL || pl->playcnt <= 0) return FALSE;

	CFile f;
	UINT mode = CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive;
	if (!f.Open(path, mode)) return FALSE;

	auto WriteLine = [&](LPCTSTR line) {
#ifdef _UNICODE
		if (utf8) {
			int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
			if (n > 1) {
				std::vector<char> buf((size_t)n);
				WideCharToMultiByte(CP_UTF8, 0, line, -1, buf.data(), n, NULL, NULL);
				f.Write(buf.data(), n - 1);
			}
		}
		else {
			CStringA a(line);
			f.Write(a, a.GetLength());
		}
#else
		CStringA a(line);
		f.Write(a, a.GetLength());
#endif
		f.Write("\r\n", 2);
	};

	if (utf8) {
		const BYTE bom[3] = { 0xEF, 0xBB, 0xBF };
		f.Write(bom, 3);
	}

	WriteLine(_T("#EXTM3U"));
	for (int i = 0; i < pl->playcnt; i++) {
		const playlistdata0& d = pl->pc[i];
		CString inf;
		int dur = d.time;
		if (dur < 0) dur = -1;
		inf.Format(_T("#EXTINF:%d,%s"), dur, (LPCTSTR)MpTruncPlField(d.name));
		WriteLine(inf);
		WriteLine(MpTruncPlField(d.fol));
	}
	f.Close();
	return TRUE;
}

static int MpImportM3uText(const CString& text, const CString& baseDir, const MpM3uImportOptions& opt)
{
	if (!pl) return -1;
	MpEnsurePlaylistBuffer();
	if (!pl->pc) return -1;

	int added = 0;
	int extDur = -1;
	CString extTitle;

	auto AddPath = [&](const CString& rawPath) -> BOOL {
		CString fol = MpResolvePath(rawPath, baseDir, opt.resolveRelative);
		if (fol.IsEmpty()) return FALSE;
		fol = MpTruncPlField(fol);
		if (opt.skipMissing && !MpFileExists(fol)) return FALSE;
		if (opt.skipDuplicates && MpHasDuplicateFol(fol)) return FALSE;

		CString name = extTitle;
		if (name.IsEmpty()) {
			int slash = fol.ReverseFind(_T('\\'));
			if (slash < 0) slash = fol.ReverseFind(_T('/'));
			name = (slash >= 0) ? fol.Mid(slash + 1) : fol;
		}
		name = MpTruncPlField(name);
		int tm = extDur;
		pl->Add(name, -2, 0, 0, _T(""), _T(""), fol, 0, tm, TRUE, TRUE);
		added++;
		extDur = -1;
		extTitle.Empty();
		return TRUE;
	};

	int pos = 0;
	while (pos < text.GetLength()) {
		int eol = text.Find(_T('\n'), pos);
		if (eol < 0) eol = text.GetLength();
		CString line = text.Mid(pos, eol - pos);
		line.Trim();
		if (!line.IsEmpty() && line[line.GetLength() - 1] == _T('\r'))
			line = line.Left(line.GetLength() - 1);
		pos = eol + 1;

		if (line.IsEmpty()) continue;
		if (line[0] == _T('#')) {
			if (line.Left(8).CompareNoCase(_T("#EXTINF:")) == 0) {
				CString rest = line.Mid(8);
				int comma = rest.Find(_T(','));
				if (comma >= 0) {
					extDur = _tstoi(rest.Left(comma));
					extTitle = rest.Mid(comma + 1);
					extTitle.Trim();
				}
			}
			continue;
		}
		AddPath(line);
	}
	return added;
}

static int MpImportPlsText(const CString& text, const CString& baseDir, const MpM3uImportOptions& opt)
{
	if (!pl) return -1;
	MpEnsurePlaylistBuffer();
	if (!pl->pc) return -1;

	int added = 0;
	int pos = 0;
	while (pos < text.GetLength()) {
		int eol = text.Find(_T('\n'), pos);
		if (eol < 0) eol = text.GetLength();
		CString line = text.Mid(pos, eol - pos);
		line.Trim();
		if (!line.IsEmpty() && line[line.GetLength() - 1] == _T('\r'))
			line = line.Left(line.GetLength() - 1);
		pos = eol + 1;
		if (line.IsEmpty() || line[0] == _T('[')) continue;
		int eq = line.Find(_T('='));
		if (eq < 0) continue;
		CString key = line.Left(eq);
		CString val = line.Mid(eq + 1);
		key.MakeLower();
		val.Trim();
		if (key.Find(_T("file")) >= 0 && !val.IsEmpty()) {
			CString fol = MpResolvePath(val, baseDir, opt.resolveRelative);
			if (fol.IsEmpty()) continue;
			fol = MpTruncPlField(fol);
			if (opt.skipMissing && !MpFileExists(fol)) continue;
			if (opt.skipDuplicates && MpHasDuplicateFol(fol)) continue;
			CString name = fol;
			int slash = name.ReverseFind(_T('\\'));
			if (slash < 0) slash = name.ReverseFind(_T('/'));
			if (slash >= 0) name = name.Mid(slash + 1);
			name = MpTruncPlField(name);
			pl->Add(name, -2, 0, 0, _T(""), _T(""), fol, 0, -1, TRUE, TRUE);
			added++;
		}
	}
	return added;
}

static int MpImportXspfText(const CString& text, const CString& baseDir, const MpM3uImportOptions& opt)
{
	if (!pl) return -1;
	MpEnsurePlaylistBuffer();
	if (!pl->pc) return -1;

	int added = 0;
	int pos = 0;
	while (pos < text.GetLength()) {
		int loc = text.Find(_T("<location>"), pos);
		if (loc < 0) break;
		loc += 10;
		int locEnd = text.Find(_T("</location>"), loc);
		if (locEnd < 0) break;
		CString val = text.Mid(loc, locEnd - loc);
		val.Trim();
		val.Replace(_T("file:///"), _T(""));
		val.Replace(_T("/"), _T("\\"));
		pos = locEnd + 11;

		CString fol = MpResolvePath(val, baseDir, opt.resolveRelative);
		if (fol.IsEmpty()) continue;
		fol = MpTruncPlField(fol);
		if (opt.skipMissing && !MpFileExists(fol)) continue;
		if (opt.skipDuplicates && MpHasDuplicateFol(fol)) continue;
		CString name = fol;
		int slash = name.ReverseFind(_T('\\'));
		if (slash >= 0) name = name.Mid(slash + 1);
		name = MpTruncPlField(name);
		pl->Add(name, -2, 0, 0, _T(""), _T(""), fol, 0, -1, TRUE, TRUE);
		added++;
	}
	return added;
}

static BOOL MpSwitchOrCreatePlaylist(const MpM3uImportOptions& opt)
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd())) return FALSE;

	pl->Save();

	const int nCombo = pl->m_listchange.GetCount();
	const int newSentinel = (nCombo > 0) ? (nCombo - 1) : 0;

	if (opt.createNew) {
		const int newIdx = MpCountPlaylistFiles();
		if (newIdx >= 999) return FALSE;
		savedata.playlistnum = newIdx;
		pl->playcnt = 0;
		pl->pnt = -1;
		pl->pnt1 = -1;
		if (pl->pc) {
			free(pl->pc);
			pl->pc = NULL;
		}
		pl->pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		if (!pl->pc) return FALSE;
		if (::IsWindow(pl->m_lc.GetSafeHwnd()))
			pl->m_lc.SetItemCount(0);
		pl->loadplaylistname();
		if (newIdx < pl->m_listchange.GetCount())
			pl->m_listchange.SetCurSel(newIdx);
		savedata.playlistnum = newIdx;
		return TRUE;
	}

	if (opt.targetPlaylist >= 0 && opt.targetPlaylist < newSentinel) {
		if (pl->m_listchange.GetCurSel() != opt.targetPlaylist) {
			pl->m_listchange.SetCurSel(opt.targetPlaylist);
			pl->OnCbnSelchangeCombo1();
		}
	}

	MpEnsurePlaylistBuffer();
	return pl->pc != NULL;
}

int MpImportPlaylistFile(const CString& path, const MpM3uImportOptions& opt)
{
	if (!pl || path.IsEmpty()) return -1;
	if (!MpIsPlaylistExtension(path)) return -1;

	BOOL wasUtf8 = FALSE;
	CString text = MpReadAllText(path, opt.utf8, wasUtf8);
	if (text.IsEmpty()) return -1;

	if (!MpSwitchOrCreatePlaylist(opt)) return -1;

	CString baseDir = MpDirOfFile(path);
	int added = 0;
	if (MpExtEq(path, _T(".pls")))
		added = MpImportPlsText(text, baseDir, opt);
	else if (MpExtEq(path, _T(".xspf")))
		added = MpImportXspfText(text, baseDir, opt);
	else
		added = MpImportM3uText(text, baseDir, opt);

	if (added >= 0) {
		if (::IsWindow(pl->m_lc.GetSafeHwnd()))
			pl->m_lc.SetItemCount(pl->playcnt);
		pl->Save();
		pl->loadplaylistname();
		if (mp && ::IsWindow(mp->GetSafeHwnd())) {
			mp->ReloadPlaylistCombo();
			mp->RefreshList(TRUE);
		}
	}
	return added;
}
