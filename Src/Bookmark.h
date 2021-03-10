#ifndef _BOOKMARK_H_
#define _BOOKMARK_H_

typedef struct DocPrefType {
	Char	name[dmDBNameLength];
	Char	category[dmCategoryLength];
	UInt32	offset;
} DocPrefType;

typedef struct DocBmkType {
	Char	name[16];
	UInt32	offset;
} DocBmkType;

typedef struct BookPrefType {
	UInt16	pathLen;
	UInt16	historyNum;
	UInt16	bookmarkNum;
	UInt16	chapter;
	UInt32	offset;
} BookPrefType;

typedef struct HistoryDataType {
	UInt16	chapter;
	UInt32	offset;
} HistoryDataType;

typedef struct BmkDataType {
	Char	name[16];
	UInt16	chapter;
	UInt32	offset;
} BmkDataType;

#define HISTORY_NUM		11

void	MyMemCopy(UInt8* desP, UInt8* srcP, UInt16 size);

void	PrefOpenDB();
void	PrefCloseDB();
void	PrefSync(UInt16 vol);

void	PrefGetLastPos(Char* path, UInt16* chapterP, UInt32* offsetP);
void	PrefSaveLastPos(Char* path, UInt16 chapter, UInt32 offset);

void	PrefGetBmkName(Char** namePP, Int16* numP);
void	PrefGetBmk(UInt16 idx, UInt16* chapterP, UInt32* offsetP);
void	PrefAddBmk(Char* nameP, UInt16 chapter, UInt32 offset);
void	PrefDelBmk(UInt16 idx);
void	PrefDelAllBmk();

void	PrefGetHistory(HistoryDataType* histP, Int16 histNum, Int16* numP, Int16* posP);
void	PrefSaveHistory(HistoryDataType* histP, Int16 start, Int16 end, Int16 pos);

void	PrefDelete(Char* path, Boolean isBook);

void	PrefMemDocGetCategory(Char* nameP, Char* categoryP);
void	PrefMemDocSetCategory(Char* nameP, Char* categoryP);
void	PrefMemDocChangeCategory(Char* newCategoryP, Char* oldCategoryP);
void	PrefMemDocDelCategory(Char* categoryP);

#endif // _BOOKMARK_H_
