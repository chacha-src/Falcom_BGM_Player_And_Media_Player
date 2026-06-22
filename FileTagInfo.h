#pragma once

struct FileTagFields {
	CString title;
	CString artist;
	CString album;
	CString year;
	CString track;
	CString genre;
	CString comment;
	int loop1;
	int loop2;

	FileTagFields() : loop1(0), loop2(0) {}
	void Clear() {
		title.Empty();
		artist.Empty();
		album.Empty();
		year.Empty();
		track.Empty();
		genre.Empty();
		comment.Empty();
		loop1 = loop2 = 0;
	}
	bool HasAnyTagField() const {
		return year.GetLength() || track.GetLength() || genre.GetLength() || comment.GetLength();
	}
	bool HasTitleArtistAlbum() const {
		return title.GetLength() || artist.GetLength() || album.GetLength();
	}
};

void ReadFileTagFields(LPCTSTR path, FileTagFields& out);
