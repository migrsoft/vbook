#include "VBook.h"
#include "Bookmark.h"
#include "BookList.h"
#include "View.h"

#define bmName	"VBookPrefs"
#define bmType	'Pref'

typedef struct {
	Char	label[vbookMaxLabelLen];
	UInt16	num;
} CardIDType, *CardIDPtr;

static DmOpenRef	l_dbRef = NULL;

// Use only for book save in expansion card.
static UInt16		l_currentBmk;
static MemHandle	l_tempBmk;

#pragma mark === For PalmDoc in MEM ===

static Int16 MemDocFindPrefs(Char* nameP, Int16* posP)
{
	DocPrefType	*prefP;
	Int16		num;
	Int16		first, mid, last, cmp;
	MemHandle	record;
	
	record = DmQueryRecord(l_dbRef, 0);
	num = MemHandleSize(record) / sizeof(DocPrefType);
	if (num == 0) // No found.
	{
		if (posP)
			*posP = 0;
		return -1;
	}
	
	prefP = MemHandleLock(record);
	first = 0;
	last = num - 1;
	while (first <= last)
	{
		mid = (first + last) / 2;
		if (mid < first)
			break;
		
		cmp = StrCompare(nameP, prefP[mid].name);
		
		if (cmp < 0)
			last = mid - 1;
		else if (cmp > 0)
			first = mid + 1;
		else
		{
			MemHandleUnlock(record);
			return mid;
		}
	}
	
	MemHandleUnlock(record);

	if (posP)
		*posP = (cmp < 0) ? first : last + 1;

	return -1;
}

// If prefs is exist, return its pos, else, add a new prefs.
static Int16 MemDocGetPrefs(Char* nameP)
{
	MemHandle	recordH;
	UInt8*		recordP;
	DocPrefType	*prefP, pref;
	Int16		i, at, num;

	i = MemDocFindPrefs(nameP, &at);
	
	if (i >= 0)
		return i;
	
	MemSet(&pref, sizeof(DocPrefType), 0);
	
	recordH = DmGetRecord(l_dbRef, 0);

	num = MemHandleSize(recordH) / sizeof(DocPrefType);
	if (num == 0)
	{
		MemHandleResize(recordH, sizeof(DocPrefType));
		recordP = MemHandleLock(recordH);
		DmWrite(recordP, 0, &pref, sizeof(DocPrefType));
		MemHandleLock(recordH);
		
		i = 0;
	}
	else // Insert it, shift down if need.
	{
		MemHandleResize(recordH, sizeof(DocPrefType) * (num + 1));
		recordP = MemHandleLock(recordH);
		prefP = (DocPrefType*)recordP;
		for (i = num - 1; i >= at; i--)
		{
			DmWrite(
				recordP, sizeof(DocPrefType) * (i+1),
				&prefP[i], sizeof(DocPrefType));
		}
		DmWrite(recordP, sizeof(DocPrefType) * at, &pref, sizeof(DocPrefType));
		MemHandleUnlock(recordH);

		i = at;
	}
	
	DmReleaseRecord(l_dbRef, 0, true);
	return i;
}

static void MemDocGetLastPos(Char* nameP, UInt32* offsetP)
{
	MemHandle		record;
	DocPrefType*	prefP;
	Int16			i;
	
	i = MemDocFindPrefs(nameP, NULL);
	if (i == -1)
		return;
	
	record = DmQueryRecord(l_dbRef, 0);
	prefP = MemHandleLock(record);
	prefP += i;
	*offsetP = prefP->offset;
	MemHandleUnlock(record);
}

static void MemDocSaveLastPos(Char* nameP, UInt32 offset)
{
	MemHandle	recordH;
	UInt8*		recordP;
	DocPrefType	*prefP, pref;
	Int16		i;
	
	StrCopy(pref.name, nameP);
	pref.category[0] = 0;
	pref.offset = offset;
	
	i = MemDocGetPrefs(nameP);
	recordH = DmGetRecord(l_dbRef, 0);
	recordP = MemHandleLock(recordH);

	prefP = (DocPrefType*)recordP;
	StrNCopy(pref.category, prefP[i].category, dmCategoryLength);
	DmWrite(recordP, sizeof(DocPrefType) * i, &pref, sizeof(DocPrefType));

	MemHandleUnlock(recordH);
	DmReleaseRecord(l_dbRef, 0, true);
}

static void MemDocGetBmkName(DmOpenRef memRef, Char** namePP, Int16* numP)
{
	PALMDOCINFO*	infoP;
	MemHandle		record;
	DocBmkType*		bmkP;
	UInt16			i, j, k, num;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	i = infoP->nBlocksNum + 1;
	num = DmNumRecords(memRef) - i;
	MemHandleUnlock(record);

	*numP = num;
	if (num == 0) // This doc doesn't has any bookmark.
		return;
	
	if (namePP == NULL)
		return;
	
	num = DmNumRecords(memRef);
	for (j = 0; i < num; i++, j++)
	{
		record = DmQueryRecord(memRef, i);
		bmkP = MemHandleLock(record);
		StrNCopy(namePP[j], bmkP->name, 16);
		MemHandleUnlock(record);
		
		// Verify bookmarker string.
		k = 0;
		while (namePP[j][k])
		{
			if (namePP[j][k] < 0x20)
				namePP[j][k] = 0x20;
			k++;
		}
	}
}

static void MemDocGetBmk(DmOpenRef memRef, UInt16 idx, UInt32* offsetP)
{
	PALMDOCINFO*	infoP;
	MemHandle		record;
	DocBmkType*		bmkP;
	UInt16			i;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	i = infoP->nBlocksNum + 1;
	MemHandleUnlock(record);

	record = DmQueryRecord(memRef, i + idx);
	bmkP = MemHandleLock(record);
	*offsetP = bmkP->offset;
	MemHandleUnlock(record);
}

static void MemDocAddBmk(DmOpenRef memRef, Char* nameP, UInt32 offset)
{
	MemHandle		record;
	PALMDOCINFO*	infoP;
	UInt16			at;
	DocBmkType		bmk;
	DocBmkType*		bmkP;
	UInt16			i, num;
	
	StrCopy(bmk.name, nameP);
	bmk.offset = offset;

	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	i = infoP->nBlocksNum + 1;
	MemHandleUnlock(record);

	num = DmNumRecords(memRef);
	for (; i < num; i++)
	{
		record = DmGetRecord(memRef, i);
		bmkP = MemHandleLock(record);
		if (!StrNCompare(nameP, bmkP->name, 16))
		{
			// Already has this bookmark, overwrite it.
			DmWrite(bmkP, 0, &bmk, sizeof(DocBmkType));
			MemHandleUnlock(record);
			DmReleaseRecord(memRef, i, true);
			break;
		}
		MemHandleUnlock(record);
		DmReleaseRecord(memRef, i, false);
	}
	if (i == num)
	{
		at = dmMaxRecordIndex;
		record = DmNewRecord(memRef, &at, sizeof(DocBmkType));
		bmkP = MemHandleLock(record);
		DmWrite(bmkP, 0, &bmk, sizeof(DocBmkType));
		MemHandleUnlock(record);
		DmReleaseRecord(memRef, at, true);
	}
}

static void MemDocDelBmk(DmOpenRef memRef, UInt16 idx)
{
	PALMDOCINFO*	infoP;
	MemHandle		record;
	UInt16			i;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	i = infoP->nBlocksNum + 1;
	MemHandleUnlock(record);
	
	DmRemoveRecord(memRef, i + idx);
}

static void MemDocDelAllBmk(DmOpenRef memRef)
{
	PALMDOCINFO*	infoP;
	MemHandle		record;
	UInt16			i, at, num;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	i = infoP->nBlocksNum + 1;
	MemHandleUnlock(record);

	num = DmNumRecords(memRef);
	for (; i < num; i++)
	{
		at = DmNumRecords(memRef) - 1;
		DmRemoveRecord(memRef, at);
	}
}

static void MemDocPrefDelete(Char* nameP)
{
	MemHandle		record;
	DocPrefType*	prefP;
	Int16			i, num;
	
	i = MemDocFindPrefs(nameP, NULL);
	if (i == -1)
		return;
	
	record = DmGetRecord(l_dbRef, 0);
	prefP = MemHandleLock(record);
	num = MemHandleSize(record) / sizeof(DocPrefType);
	if (num == 1)
	{
		MemHandleUnlock(record);
		MemHandleResize(record, 2);
		DmReleaseRecord(l_dbRef, 0, true);
	}
	else
	{
		// Shift up if need.
		for (i++; i < num; i++)
		{
			DmWrite(
				prefP, sizeof(DocPrefType) * (i-1),
				&prefP[i], sizeof(DocPrefType));
		}
		MemHandleUnlock(record);
		MemHandleResize(record, sizeof(DocPrefType) * (num - 1));
		DmReleaseRecord(l_dbRef, 0, true);
	}
}

#pragma mark === For Book in MEM ===

static void MemBookGetLastPos(DmOpenRef memRef, UInt16* chapterP, UInt32* offsetP)
{
	VBOOKINFO*	infoP;
	MemHandle	record;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	
	*chapterP = infoP->nChapter;
	*offsetP = infoP->nOffset;
	
	MemHandleUnlock(record);
}

static void MemBookSaveLastPos(DmOpenRef memRef, UInt16 chapter, UInt32 offset)
{
	VBOOKINFO	*infoP, info;
	MemHandle	record;
	
	record = DmGetRecord(memRef, 0);
	infoP = MemHandleLock(record);
	MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
	info.nChapter = chapter;
	info.nOffset = offset;
	DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
	MemHandleUnlock(record);
	DmReleaseRecord(memRef, 0, true);
}

static void MemBookGetBmkName(DmOpenRef memRef, Char** namePP, Int16* numP)
{
	VBOOKINFO*		infoP;
	BmkDataType*	bmkP;
	MemHandle		record;
	Boolean			hasBmk;
	UInt16			i;
	
	*numP = 0;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	hasBmk = (infoP->nType & BOOKMARK) ? true : false;
	MemHandleUnlock(record);
	
	if (!hasBmk)
		return;
	
	record = DmQueryRecord(memRef, DmNumRecords(memRef) - 1);
	*numP = MemHandleSize(record) / sizeof(BmkDataType);
	
	if (*numP == 0 || namePP == NULL)
		return;
	
	bmkP = MemHandleLock(record);
	
	for (i=0; i < *numP; i++)
		StrNCopy(namePP[i], bmkP[i].name, 16);
	
	MemHandleUnlock(record);
}

static void MemBookGetBmk(
	DmOpenRef memRef, UInt16 idx, UInt16* chapterP, UInt32* offsetP)
{
	MemHandle		record;
	BmkDataType*	bmkP;
	
	record = DmQueryRecord(memRef, DmNumRecords(memRef) - 1);
	bmkP = MemHandleLock(record);

	*chapterP	= bmkP[idx].chapter;
	*offsetP	= bmkP[idx].offset;
	
	MemHandleUnlock(record);
}

static void MemBookAddBmk(
	DmOpenRef memRef, Char* nameP, UInt16 chapter, UInt32 offset)
{
	VBOOKINFO		*infoP, info;
	BmkDataType		*bmkP, bmk;
	MemHandle		record;
	UInt16			i, num;
	UInt16			at;
	
	StrNCopy(bmk.name, nameP, 16);
	bmk.chapter = chapter;
	bmk.offset = offset;
	
	record = DmGetRecord(memRef, 0);
	infoP = MemHandleLock(record);
	if (!(infoP->nType & BOOKMARK))
	{
		// No bookmark record, add a new one.
		MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
		info.nType |= BOOKMARK;
		DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
		MemHandleUnlock(record);
		DmReleaseRecord(memRef, 0, true);
		
		at = dmMaxRecordIndex;
		record = DmNewRecord(memRef, &at, sizeof(BmkDataType));
		bmkP = MemHandleLock(record);
		DmWrite(bmkP, 0, &bmk, sizeof(BmkDataType));
		MemHandleUnlock(record);
		DmReleaseRecord(memRef, at, true);
		
		return;
	}
	MemHandleUnlock(record);
	DmReleaseRecord(memRef, 0, false);
	
	at = DmNumRecords(memRef) - 1;
	record = DmGetRecord(memRef, at);
	num = MemHandleSize(record) / sizeof(BmkDataType);
	bmkP = MemHandleLock(record);
	
	for (i=0; i < num; i++)
	{
		if (!StrNCompare(nameP, bmkP[i].name, 16))
		{
			DmWrite(bmkP, sizeof(BmkDataType) * i, &bmk, sizeof(BmkDataType));
			break;
		}
	}
	if (i == num)
	{
		num++;
		MemHandleUnlock(record);
		MemHandleResize(record, sizeof(BmkDataType) * num);
		bmkP = MemHandleLock(record);
		DmWrite(bmkP, sizeof(BmkDataType) * i, &bmk, sizeof(BmkDataType));
	}
	MemHandleUnlock(record);
	DmReleaseRecord(memRef, at, true);
}

static void MemBookDelBmk(DmOpenRef memRef, UInt16 idx)
{
	VBOOKINFO		*infoP, info;
	MemHandle		record;
	BmkDataType*	bmkP;
	UInt16			i, at, num;
	
	at = DmNumRecords(memRef) - 1;
	record = DmGetRecord(memRef, at);
	num = MemHandleSize(record) / sizeof(BmkDataType);
	if (num == 1)
	{
		DmReleaseRecord(memRef, at, false);
		DmRemoveRecord(memRef, at);
		
		record = DmGetRecord(memRef, 0);
		infoP = MemHandleLock(record);
		MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
		info.nType ^= BOOKMARK;
		DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
		MemHandleUnlock(record);
		DmReleaseRecord(memRef, 0, true);

		return;
	}

	bmkP = MemHandleLock(record);
	
	for (i = idx+1; i < num; i++)
		DmWrite(bmkP, sizeof(BmkDataType) * (i-1), &bmkP[i], sizeof(BmkDataType));

	MemHandleUnlock(record);
	num--;
	MemHandleResize(record, sizeof(BmkDataType) * num);
	DmReleaseRecord(memRef, at, true);
}

static void MemBookDelAllBmk(DmOpenRef memRef)
{
	VBOOKINFO	*infoP, info;
	MemHandle	record;
	
	record = DmGetRecord(memRef, 0);
	infoP = MemHandleLock(record);
	MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
	info.nType ^= BOOKMARK;
	DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
	MemHandleUnlock(record);
	DmReleaseRecord(memRef, 0, true);
	
	DmRemoveRecord(memRef, DmNumRecords(memRef) - 1);
}

static void MemBookGetHistory(
	DmOpenRef memRef, HistoryDataType* histP, Int16 histNum, Int16* numP, Int16* posP)
{
	VBOOKINFO		*infoP;
	HistoryDataType	*hdrP;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			i, at;
	
	record = DmQueryRecord(memRef, 0);
	infoP = MemHandleLock(record);
	if (!(infoP->nType & HISTORY))
	{
		*numP = 0;
		*posP = -1;
		MemHandleUnlock(record);
		return;
	}
	
	at = DmNumRecords(memRef) - 1;
	if (infoP->nType & BOOKMARK)
		at--;
	MemHandleUnlock(record);
	
	record = DmQueryRecord(memRef, at);

	*numP = MemHandleSize(record) / sizeof(HistoryDataType);
	hdrP = MemHandleLock(record);
	for (i=0; i < *numP && i < histNum; i++)
	{
		histP[i].chapter	= hdrP[i].chapter;
		histP[i].offset		= hdrP[i].offset;
	}
	MemHandleUnlock(record);
	
	recordP = MemHandleLock(record);
	recordP += MemHandleSize(record) - 2;
	*posP = *((Int16*)recordP);
	MemHandleUnlock(record);
}

static void MemBookSaveHistory(
	DmOpenRef memRef, HistoryDataType* histP, Int16 start, Int16 end, Int16 pos)
{
	Err				err;
	VBOOKINFO		*infoP, info;
	HistoryDataType	hdr;
	MemHandle		record, hisRecord, oldH;
	MemPtr			recordP;
	Int16			i, j, num;
	UInt16			at;
	
	for (i=start, num=0; i != end; i = CYCLIC_INCREASE(i, HISTORY_NUM))
		num++;
	
	if (num)
	{
		// Create new history record.
		hisRecord = DmNewHandle(memRef, sizeof(HistoryDataType) * num + 2);
		if (hisRecord == 0)
			return;
		
		recordP = MemHandleLock(hisRecord);
		for (i=start, j=0; i != end; i = CYCLIC_INCREASE(i, HISTORY_NUM), j++)
		{
			hdr.chapter = histP[i].chapter;
			hdr.offset = histP[i].offset;
			DmWrite(recordP, sizeof(HistoryDataType) * j, &hdr, sizeof(HistoryDataType));
		}
		DmWrite(recordP, sizeof(HistoryDataType) * j, &pos, 2);

		MemHandleUnlock(hisRecord);
	}
	
	record = DmGetRecord(memRef, 0);
	infoP = MemHandleLock(record);

	if (infoP->nType & HISTORY)
	{
		// Already has a history record, replace it with new one.
		at = DmNumRecords(memRef) - 1;
		if (infoP->nType & BOOKMARK)
			at--;
		
		if (num)
		{
			MemHandleUnlock(record);
			DmReleaseRecord(memRef, 0, false);
			// Replace with new record.
			DmAttachRecord(memRef, &at, hisRecord, &oldH);
			MemHandleFree(oldH);
		}
		else // No history.
		{
			MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
			info.nType ^= HISTORY;
			DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
			MemHandleUnlock(record);
			DmReleaseRecord(memRef, 0, true);
			// Delete old history record.
			DmRemoveRecord(memRef, at);
		}
	}
	else
	{
		if (num)
		{
			at = dmMaxRecordIndex;
			// Insert before bookmark record.
			if (infoP->nType & BOOKMARK)
				at = DmNumRecords(memRef) - 1;
			err = DmAttachRecord(memRef, &at, hisRecord, NULL);

			MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
			info.nType |= HISTORY;
			DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
			
			MemHandleUnlock(record);
			DmReleaseRecord(memRef, 0, true);
		}
		else
		{
			MemHandleUnlock(record);
			DmReleaseRecord(memRef, 0, false);
			return;
		}
	}
}

#pragma mark === For Book/Doc in VFS ===

static void VfsGetSavedCardInfo(Char *labelP, Int16 *startP, Int16 *endP)
{
	LocalID		dbID, appInfoID;
	MemHandle	appInfoH;
	CardIDType	cardID, *cardIDP, *cardIDNewP;
	Int16		i, j, IDNum;
	
	appInfoID = DmGetAppInfoID(l_dbRef);
	
	if (appInfoID == 0)
	{
		// This will only be called in PrefOpenDB().

		appInfoH = DmNewHandle(l_dbRef, sizeof(CardIDType));
		cardIDP = MemHandleLock(appInfoH);
		
		StrCopy(cardID.label, labelP);
		cardID.num = DmNumRecords(l_dbRef) - 1;
		
		DmWrite(cardIDP, 0, &cardID, sizeof(CardIDType));
		MemHandleUnlock(appInfoH);
		
		appInfoID = MemHandleToLocalID(appInfoH);
		
		dbID = DmFindDatabase(0, bmName);
		
		DmSetDatabaseInfo(
			0, dbID, NULL,
			NULL, NULL,
			NULL, NULL, NULL, NULL,
			&appInfoID, NULL,
			NULL, NULL);
		
		return;
	}
	
	cardIDP = MemLocalIDToLockedPtr(appInfoID, 0);
	IDNum = MemPtrSize(cardIDP) / sizeof(CardIDType);
	
	for (i = 0; i < IDNum; i++)
		if (!StrCompare(cardIDP[i].label, labelP)) break;
	
	if (i == IDNum) // No found, add a new data.
	{
		appInfoH = DmNewHandle(l_dbRef, sizeof(CardIDType) * (IDNum + 1));
		cardIDNewP = MemHandleLock(appInfoH);
		
		// Copy original data.
		DmWrite(cardIDNewP, 0, cardIDP, sizeof(CardIDType) * IDNum);
		
		// Add new data at tail.
		StrCopy(cardID.label, labelP);
		cardID.num = 0;

		DmWrite(
			cardIDNewP, sizeof(CardIDType) * IDNum,
			&cardID, sizeof(CardIDType));
		
		MemHandleUnlock(appInfoH);
		
		// Replace old info data with new.
		appInfoID = MemHandleToLocalID(appInfoH);
		
		dbID = DmFindDatabase(0, bmName);
		
		DmSetDatabaseInfo(
			0, dbID, NULL,
			NULL, NULL,
			NULL, NULL, NULL, NULL,
			&appInfoID, NULL,
			NULL, NULL);
		
		// Release old info data.
		MemPtrFree(cardIDP);
		
		// Get pointer to new info data.
		cardIDP = MemLocalIDToLockedPtr(appInfoID, 0);
	}
	
	*startP = 1;
	for (j = 0; j < i; j++)
		*startP += cardIDP[j].num;
	
	*endP = *startP + cardIDP[i].num - 1;
	
	MemPtrUnlock(cardIDP);
}


// Update number of books saved in per expansion card.
static void VfsCardInfoCounter(Char* labelP, Boolean add)
{
	LocalID		appInfoID;
	CardIDType	cardID, *cardIDP;
	Int16		i, num;
	
	appInfoID = DmGetAppInfoID(l_dbRef);
	cardIDP = MemLocalIDToLockedPtr(appInfoID, 0);
	
	num = MemPtrSize(cardIDP);
	for (i = 0; i < num; i++)
		if (!StrCompare(cardIDP[i].label, labelP))
		{
			MemMove(&cardID, &cardIDP[i], sizeof(CardIDType));
			
			if (add)
				cardID.num++;
			else if (cardID.num > 0)
				cardID.num--;
			
			DmWrite(
				cardIDP, sizeof(CardIDType) * i,
				&cardID, sizeof(CardIDType));
			
			break;
		}

	MemPtrUnlock(cardIDP);
}

static UInt16 VfsFindPrefs(Char* path, UInt16* posP)
{
	Int16		first, mid, last, cmp;
	MemHandle	recordH;
	UInt8*		recordP;
	UInt16		volRef;
	Char		label[vbookMaxLabelLen];
	
	volRef = GetVolRef(label);
	VfsGetSavedCardInfo(label, &first, &last);

	if (last < first) // No found.
	{
		if (posP) *posP = first;
		return 0;
	}
	
	while (first <= last)
	{
		mid = (first + last) / 2;
		if (mid < first)
			break;
		
		recordH = DmQueryRecord(l_dbRef, mid);
		recordP = MemHandleLock(recordH);
		recordP += sizeof(BookPrefType);
		cmp = StrCompare(path, (Char*)recordP);
		MemHandleUnlock(recordH);
		
		if (cmp < 0)
			last = mid - 1;
		else if (cmp > 0)
			first = mid + 1;
		else
			return (UInt16)mid;
	}

	if (posP)
		*posP = (UInt16)((cmp < 0) ? first : last + 1);

	return 0;
}

static void VfsGetLastPos(Char* path, UInt16* chapterP, UInt32* offsetP)
{
	MemHandle		record;
	BookPrefType*	prefP;
	UInt16			pos;
	
	pos = VfsFindPrefs(path, NULL);
	l_currentBmk = pos;

	if (pos == 0)
	{
		*chapterP = *offsetP = 0;
		return;
	}
	
	record = DmQueryRecord(l_dbRef, pos);
	prefP = MemHandleLock(record);
	*chapterP = prefP->chapter;
	*offsetP = prefP->offset;
	MemHandleUnlock(record);
}

static void VfsSaveLastPos(Char* path, UInt16 chapter, UInt32 offset)
{
	BookPrefType	*prefP, pref;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			pos, at, distance;
	Char			label[vbookMaxLabelLen];

	if (l_currentBmk)
		pos = l_currentBmk;
	else
		pos = VfsFindPrefs(path, &at);
	
	if (pos)
	{
		record = DmGetRecord(l_dbRef, pos);
		prefP = MemHandleLock(record);
		MyMemCopy((UInt8*)&pref, (UInt8*)prefP, sizeof(BookPrefType));
		pref.chapter = chapter;
		pref.offset = offset;
		DmWrite(prefP, 0, &pref, sizeof(BookPrefType));
		MemHandleUnlock(record);
		DmReleaseRecord(l_dbRef, pos, true);
	}
	else // Add a new record.
	{
		pref.pathLen = StrLen(path) + 1;
		if (pref.pathLen % 2)
			pref.pathLen++;
		
		pref.historyNum = 0;
		pref.bookmarkNum = (l_tempBmk) ? MemHandleSize(l_tempBmk) / sizeof(BmkDataType) : 0;
		pref.chapter = chapter;
		pref.offset = offset;
		
		record = DmNewRecord(
			l_dbRef, &at,
			sizeof(BookPrefType) + pref.pathLen +
			((l_tempBmk) ? MemHandleSize(l_tempBmk) : 0));

		if (record == 0)
			return;
		
		l_currentBmk = at;
		
		recordP = MemHandleLock(record);

		// Write prefs hreader.
		distance = 0;
		DmWrite(recordP, distance, &pref, sizeof(BookPrefType));

		// Write path.
		distance += sizeof(BookPrefType);
		DmWrite(recordP, distance, path, pref.pathLen);
		
		// Write bookmarks
		if (l_tempBmk)
		{
			distance += pref.pathLen;
			DmWrite(recordP, distance, MemHandleLock(l_tempBmk), MemHandleSize(l_tempBmk));
			MemHandleUnlock(l_tempBmk);
		}

		MemHandleUnlock(record);
		DmReleaseRecord(l_dbRef, at, true);
		
		GetVolRef(label);
		VfsCardInfoCounter(label, true);
	}
	
	if (l_tempBmk)
	{
		MemHandleFree(l_tempBmk);
		l_tempBmk = NULL;
	}
}

static void VfsGetBmkName(Char** namePP, Int16* numP)
{
	BookPrefType*	prefP;
	BmkDataType*	bmkP;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			i;
	UInt16			distance;

	if (l_currentBmk == 0 && l_tempBmk == NULL)
		return;
	
	if (l_currentBmk)
	{
		record = DmQueryRecord(l_dbRef, l_currentBmk);
		recordP = MemHandleLock(record);
		prefP = (BookPrefType*)recordP;
		*numP = prefP->bookmarkNum;
		distance =
			sizeof(BookPrefType) +
			prefP->pathLen +
			sizeof(HistoryDataType) * prefP->historyNum + ((prefP->historyNum) ? 2 : 0);
	}
	else
	{
		*numP = MemHandleSize(l_tempBmk) / sizeof(BmkDataType);
		recordP = MemHandleLock(l_tempBmk);
		distance = 0;
	}

	if (namePP)
	{
		bmkP = (BmkDataType*)(recordP + distance);
		for (i=0; i < *numP; i++)
			StrNCopy(namePP[i], bmkP[i].name, 16);
	}

	if (l_currentBmk)
		MemHandleUnlock(record);
	else
		MemHandleUnlock(l_tempBmk);
}

static void VfsGetBmk(UInt16 idx, UInt16* chapterP, UInt32* offsetP)
{
	BookPrefType*	prefP;
	BmkDataType*	bmkP;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			distance;
	
	if (!l_currentBmk && !l_tempBmk)
		return;
	
	if (l_currentBmk)
	{
		record = DmQueryRecord(l_dbRef, l_currentBmk);
		recordP = MemHandleLock(record);
		prefP = (BookPrefType*)recordP;
		distance =
			sizeof(BookPrefType) +
			prefP->pathLen +
			sizeof(HistoryDataType) * prefP->historyNum + ((prefP->historyNum) ? 2 : 0);
	}
	else
	{
		record = l_tempBmk;
		recordP = MemHandleLock(record);
		distance = 0;
	}

	bmkP = (BmkDataType*)(recordP + distance);
	*chapterP = bmkP[idx].chapter;
	*offsetP = bmkP[idx].offset;
	
	MemHandleUnlock(record);
}

// When book or Palmdoc saved in expansion card,
// I save their history and bookmarks in VBook's prefs database.
// Save method:
//   case 1: When prefs database has its record, save in it directly.
//   case 2: When prefs database doesn't has its record,
//           I save bookmark in a l_tempBmk mem handle temporarily,
//           then when this book is close, SaveLastPos() will insert this
//           record into prefs database.
static void VfsAddBmk(Char* nameP, UInt16 chapter, UInt32 offset)
{
	BookPrefType	*prefP, pref;
	BmkDataType		*bmkP, bmk;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			i, num, distance;
	
	StrNCopy(bmk.name, nameP, 16);
	bmk.chapter = chapter;
	bmk.offset = offset;

	record = (l_currentBmk) ? DmGetRecord(l_dbRef, l_currentBmk) : l_tempBmk;
	if (record == NULL)
	{
		// This is the first bookmark save in temp bookmark handle.
		l_tempBmk = MemHandleNew(sizeof(BmkDataType));
		recordP = MemHandleLock(l_tempBmk);
		MyMemCopy(recordP, (UInt8*)&bmk, sizeof(BmkDataType));
		MemHandleUnlock(l_tempBmk);
		
		return;
	}
	
	recordP = MemHandleLock(record);
	
	if (l_currentBmk)
	{
		prefP = (BookPrefType*)recordP;
		num = prefP->bookmarkNum;

		distance =
			sizeof(BookPrefType) +
			prefP->pathLen +
			sizeof(HistoryDataType) * prefP->historyNum + ((prefP->historyNum) ? 2 : 0);
	}
	else
	{
		num = MemHandleSize(record) / sizeof(BmkDataType);
		distance = 0;
	}

	bmkP = (BmkDataType*)(recordP + distance);

	for (i=0; i < num; i++)
	{
		if (!StrNCompare(nameP, bmkP[i].name, 16))
		{
			if (l_currentBmk)
				DmWrite(
					recordP, distance + sizeof(BmkDataType) * i,
					&bmk, sizeof(BmkDataType));
			else
				MyMemCopy((UInt8*)&bmkP[i], (UInt8*)&bmk, sizeof(BmkDataType));
			
			break;
		}
	}
	if (i == num)
	{
		// This is a new bookmark, add it to the end.
		MemHandleUnlock(record);
		MemHandleResize(record, MemHandleSize(record) + sizeof(BmkDataType));
		if (l_tempBmk)
			l_tempBmk = record;
		recordP = MemHandleLock(record);
		
		if (l_currentBmk)
		{
			prefP = (BookPrefType*)recordP;
			MyMemCopy((UInt8*)&pref, recordP, sizeof(BookPrefType));
			pref.bookmarkNum++;
			DmWrite(recordP, 0, &pref, sizeof(BookPrefType));
			
			DmWrite(
				recordP, distance + sizeof(BmkDataType) * i,
				&bmk, sizeof(BmkDataType));
		}
		else
		{
			bmkP = (BmkDataType*)recordP;
			MyMemCopy((UInt8*)&bmkP[i], (UInt8*)&bmk, sizeof(BmkDataType));
		}
	}
		
	MemHandleUnlock(record);
	if (l_currentBmk)
		DmReleaseRecord(l_dbRef, l_currentBmk, true);
}

static void VfsDelBmk(UInt16 idx)
{
	BookPrefType	*prefP, pref;
	BmkDataType		*bmkP;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			i, num, distance;
	
	record = (l_currentBmk) ? DmGetRecord(l_dbRef, l_currentBmk) : l_tempBmk;
	recordP = MemHandleLock(record);
	
	if (l_currentBmk)
	{
		prefP = (BookPrefType*)recordP;
		num = prefP->bookmarkNum;

		distance =
			sizeof(BookPrefType) +
			prefP->pathLen +
			sizeof(HistoryDataType) * prefP->historyNum + ((prefP->historyNum) ? 2 : 0);
	}
	else
	{
		num = MemHandleSize(record) / sizeof(BmkDataType);
		distance = 0;
	}
	
	if (num == 1 && !l_currentBmk)
	{
		MemHandleUnlock(record);
		MemHandleFree(record);
		l_tempBmk = NULL;

		return;
	}

	bmkP = (BmkDataType*)(recordP + distance);
	distance += sizeof(BmkDataType) * idx;
	for (i = idx+1; i < num; i++)
	{
		if (l_currentBmk)
		{
			// Shift up.
			DmWrite(recordP, distance, &bmkP[i], sizeof(BmkDataType));
			distance += sizeof(BmkDataType);
		}
		else
			MyMemCopy((UInt8*)&bmkP[i-1], (UInt8*)&bmkP[i], sizeof(BmkDataType));
	}

	MemHandleUnlock(record);
	MemHandleResize(record, MemHandleSize(record) - sizeof(BmkDataType));
	
	if (l_currentBmk)
	{
		prefP = MemHandleLock(record);
		MyMemCopy((UInt8*)&pref, (UInt8*)prefP, sizeof(BookPrefType));
		pref.bookmarkNum--;
		DmWrite(prefP, 0, &pref, sizeof(BookPrefType));
		MemHandleUnlock(record);
		DmReleaseRecord(l_dbRef, l_currentBmk, true);
	}
}

static void VfsDelAllBmk()
{
	BookPrefType	*prefP, pref;
	MemHandle		record;
	UInt16			num;
	
	if (l_currentBmk)
	{
		record = DmGetRecord(l_dbRef, l_currentBmk);
		prefP = MemHandleLock(record);
		num = prefP->bookmarkNum;
		
		if (num)
		{
			MyMemCopy((UInt8*)&pref, (UInt8*)prefP, sizeof(BookPrefType));
			pref.bookmarkNum = 0;
			DmWrite(prefP, 0, &pref, sizeof(BookPrefType));
			MemHandleUnlock(record);
			MemHandleResize(record, MemHandleSize(record) - sizeof(BmkDataType) * num);
			DmReleaseRecord(l_dbRef, l_currentBmk, true);
		}
		else
		{
			MemHandleUnlock(record);
			DmReleaseRecord(l_dbRef, l_currentBmk, false);
		}
	}
	else if (l_tempBmk)
	{
		MemHandleFree(l_tempBmk);
		l_tempBmk = NULL;
	}
}

static void VfsGetHistory(HistoryDataType* histP, Int16 histNum, Int16* numP, Int16* posP)
{
	BookPrefType	*prefP;
	HistoryDataType	*hsP;
	MemHandle		record;
	UInt8*			recordP;
	UInt16			i;
	
	if (l_currentBmk == 0)
		return;
	
	record = DmQueryRecord(l_dbRef, l_currentBmk);
	recordP = MemHandleLock(record);
	
	prefP = (BookPrefType*)recordP;
	
	if (prefP->historyNum)
	{
		hsP = (HistoryDataType*)(recordP + sizeof(BookPrefType) + prefP->pathLen);
		for (i=0; i < prefP->historyNum && i < (UInt16)histNum; i++)
		{
			histP[i].chapter = hsP[i].chapter;
			histP[i].offset = hsP[i].offset;
		}
		
		*numP = prefP->historyNum;
		
		recordP = (UInt8*)hsP;
		recordP += sizeof(HistoryDataType) * prefP->historyNum;
		*posP = *((Int16*)recordP);
	}
	
	MemHandleUnlock(record);
}

static void VfsSaveHistory(HistoryDataType* histP, Int16 start, Int16 end, Int16 pos)
{
	BookPrefType	*prefP, pref;
	MemHandle		record, oldH;
	UInt8			*recordP, *oldP;
	Int16			i, num;
	UInt16			distance;
	
	if (l_currentBmk == 0)
		return;
	
	for (i=start, num=0; i != end; i = CYCLIC_INCREASE(i, HISTORY_NUM))
		num++;
	
	record = DmGetRecord(l_dbRef, l_currentBmk);
	recordP = MemHandleLock(record);
	
	prefP = (BookPrefType*)recordP;

	if (prefP->historyNum == num)
	{
		if (num)
		{
			distance = sizeof(BookPrefType) + prefP->pathLen;
			for (i=start; i != end; i = CYCLIC_INCREASE(i, HISTORY_NUM))
			{
				DmWrite(recordP, distance, &histP[i], sizeof(HistoryDataType));
				distance += sizeof(HistoryDataType);
			}
			DmWrite(recordP, distance, &pos, 2);
		}
		MemHandleUnlock(record);
		DmReleaseRecord(l_dbRef, l_currentBmk, true);
	}
	else
	{
		distance =
			sizeof(BookPrefType) +
			prefP->pathLen +
			sizeof(HistoryDataType) * num + ((num) ? 2 : 0) +
			sizeof(BmkDataType) * prefP->bookmarkNum;
		
		MemHandleUnlock(record);
		DmReleaseRecord(l_dbRef, l_currentBmk, false);
		
		record = DmNewHandle(l_dbRef, distance);
		recordP = MemHandleLock(record);
		oldH = DmQueryRecord(l_dbRef, l_currentBmk);
		oldP = MemHandleLock(oldH);
		prefP = (BookPrefType*)oldP;
		
		// Copy pref header.
		distance = 0;
		MyMemCopy((UInt8*)&pref, oldP, sizeof(BookPrefType));
		pref.historyNum = num;
		DmWrite(recordP, distance, &pref, sizeof(BookPrefType));
		
		// Copy path.
		oldP += sizeof(BookPrefType);
		distance += sizeof(BookPrefType);
		DmWrite(recordP, distance, oldP, pref.pathLen);
		
		// Save new history info.
		oldP += pref.pathLen;
		distance += pref.pathLen;
		for (i=start; i != end; i = CYCLIC_INCREASE(i, HISTORY_NUM))
		{
			DmWrite(recordP, distance, &histP[i], sizeof(HistoryDataType));
			distance += sizeof(HistoryDataType);
		}
		if (num)
		{
			DmWrite(recordP, distance, &pos, 2);
			distance += 2;
		}
		
		// Copy bookmarks.
		oldP += sizeof(HistoryDataType) * prefP->historyNum + ((prefP->historyNum) ? 2 : 0);
		DmWrite(recordP, distance, oldP, sizeof(BmkDataType) * prefP->bookmarkNum);
		
		MemHandleUnlock(record);
		MemHandleUnlock(oldH);
		
		DmAttachRecord(l_dbRef, &l_currentBmk, record, &oldH);
		MemHandleFree(oldH);
	}
}

static void VfsPrefDelete(Char* path)
{
	UInt16	i;
	Char	label[vbookMaxLabelLen];
	
	i = VfsFindPrefs(path, NULL);
	if (i == 0)
		return;
	
	DmRemoveRecord(l_dbRef, i);
	
	GetVolRef(label);
	VfsCardInfoCounter(label, false);
}

#pragma mark === Sync ===

static void SyncMemDoc()
{
	DocPrefType	*prefP, pref;
	MemHandle	newH, oldH;
	UInt8		*newP, *oldP;
	UInt16		num, i, j;
	LocalID		dbID;
	
	oldH = DmQueryRecord(l_dbRef, 0);
	num = MemHandleSize(oldH) / sizeof(DocPrefType);

	if (num == 0)
		return;
	
	newH = DmNewHandle(l_dbRef, MemHandleSize(oldH));
	if (newH == NULL)
		return;
	
	newP = MemHandleLock(newH);
	oldP = MemHandleLock(oldH);
	prefP = (DocPrefType*)oldP;
	for (i=0, j=0; i < num; i++)
	{
		dbID = DmFindDatabase(0, prefP[i].name);
		if (dbID)
		{
			MyMemCopy((UInt8*)&pref, (UInt8*)&prefP[i], sizeof(DocPrefType));
			DmWrite(newP, sizeof(DocPrefType) * j, &pref, sizeof(DocPrefType));
			j++;
		}
	}
	MemHandleUnlock(newH);
	MemHandleUnlock(oldH);
	
	if (j != num)
	{
		if (j == 0)
			MemHandleResize(newH, 2);
		else
			MemHandleResize(newH, sizeof(DocPrefType) * j);
		i = 0;
		DmAttachRecord(l_dbRef, &i, newH, &oldH);
		MemHandleFree(oldH);
	}
	else
	{
		MemHandleFree(newH);
	}
}

static void SyncVfsCleanEmptyCardInfo()
{
	LocalID		appInfoID, dbID;
	MemHandle	appInfoH;
	CardIDType	cardID, *cardIDP, *cardIDNewP;
	Int16		i, j, num, usedNum;
	
	appInfoID = DmGetAppInfoID(l_dbRef);
	if (appInfoID == 0) return;
	
	cardIDP = MemLocalIDToLockedPtr(appInfoID, 0);
	num = MemPtrSize(cardIDP) / sizeof(CardIDType);
	
	if (num == 1) // Must keep one card info at least.
	{
		MemPtrUnlock(cardIDP);
		return;
	}
	
	// Total used card info.
	usedNum = 0;
	for (i = 0; i < num; i++)
		if (cardIDP[i].num)
			usedNum++;
	
	if (usedNum == num) // No card info is empty.
	{
		MemPtrUnlock(cardIDP);
		return;
	}
	
	dbID = DmFindDatabase(0, bmName);
	
	appInfoH = DmNewHandle(l_dbRef, sizeof(CardIDType) * usedNum);
	cardIDNewP = MemHandleLock(appInfoH);
	
	// Copy card info.
	for (i = j = 0; i < num; i++)
		if (cardIDP[i].num)
		{
			MemMove(&cardID, &cardIDP[i], sizeof(CardIDType));
			DmWrite(
				cardIDNewP, sizeof(CardIDType) * j,
				&cardID, sizeof(CardIDType));
			j++;
		}
	
	MemHandleUnlock(appInfoH);
	appInfoID = MemHandleToLocalID(appInfoH);

	DmSetDatabaseInfo(
		0, dbID, NULL,
		NULL, NULL,
		NULL, NULL, NULL, NULL,
		&appInfoID, NULL,
		NULL, NULL);
	
	MemPtrFree(cardIDP);
}

static void SyncVfsBook()
{
	MemHandle	recordH;
	Char*		pathP;
	Int16		first, last, i;
	FileRef		fileRef;
	Char		label[vbookMaxLabelLen];
	UInt16		volRef;
	
	volRef = GetVolRef(label);
	VfsGetSavedCardInfo(label, &first, &last);
	
	if (last >= first)
	{
		for (i = first; i <= last;)
		{
			recordH = DmQueryRecord(l_dbRef, i);
			pathP = MemHandleLock(recordH);
			pathP += sizeof(BookPrefType);
			
			if (VFSFileOpen(volRef, pathP, vfsModeRead, &fileRef) != errNone)
			{
				MemHandleUnlock(recordH);
				DmRemoveRecord(l_dbRef, i);
				
				VfsCardInfoCounter(label, false);
				last--;
			}
			else
			{
				MemHandleUnlock(recordH);
				VFSFileClose(fileRef);
				i++;
			}
		}
	}
	
	SyncVfsCleanEmptyCardInfo();
}

#pragma mark === Entry Points ===

void MyMemCopy(UInt8* desP, UInt8* srcP, UInt16 size)
{
	UInt16	i;
	for (i=0; i < size; i++)
		*desP++ = *srcP++;
}

void PrefOpenDB()
{
	LocalID		dbID, appInfoID;
	UInt16		attributes, num;
	MemHandle	record;
	UInt16		at;
	Err			err;
	
	dbID = DmFindDatabase(0, bmName);

	if (dbID == 0)
	{
		// Create a new database.
		err = DmCreateDatabase(0, bmName, appFileCreator, bmType, false);
		ErrNonFatalDisplayIf(err, "DmCreateDatabase");

		dbID = DmFindDatabase (0, bmName);

		attributes = dmHdrAttrBackup;
		DmSetDatabaseInfo(0, dbID,
			NULL, &attributes, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL);
	}

	l_dbRef = DmOpenDatabase(0, dbID, dmModeReadWrite);

	num = DmNumRecords(l_dbRef);
	if (num == 0)
	{
		at = 0;
		record = DmNewRecord(l_dbRef, &at, 2);
		DmReleaseRecord(l_dbRef, at, true);
		num++;
	}
	
	appInfoID = DmGetAppInfoID(l_dbRef);
	if (appInfoID == 0)
	{
		UInt16	volRef;
		Char	label[vbookMaxLabelLen];
		
		volRef = GetVolRef(label);
		if (volRef != vfsInvalidVolRef)
			VfsGetSavedCardInfo(label, NULL, NULL);
	}
	
	l_currentBmk = 0;
	l_tempBmk = NULL;
}

void PrefCloseDB()
{
	DmCloseDatabase(l_dbRef);
	if (l_tempBmk)
		MemHandleFree(l_tempBmk);
}

void PrefSync(UInt16 vol)
{
	if (vol == 0)
		SyncMemDoc();
	else
		SyncVfsBook();
}

void PrefGetLastPos(Char* path, UInt16* chapterP, UInt32* offsetP)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	*chapterP = 0;
	*offsetP = 0;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookGetLastPos(memRef, chapterP, offsetP);
	else if (memRef && !isBook && REGISTERED)
		MemDocGetLastPos(path, offsetP);
	else if (vfsRef && REGISTERED)
		VfsGetLastPos(path, chapterP, offsetP);
}

void PrefSaveLastPos(Char* path, UInt16 chapter, UInt32 offset)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookSaveLastPos(memRef, chapter, offset);
	else if (memRef && !isBook && REGISTERED)
		MemDocSaveLastPos(path, offset);
	else if (vfsRef && REGISTERED)
		VfsSaveLastPos(path, chapter, offset);
}

void PrefGetBmkName(Char** namePP, Int16* numP)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	*numP = 0;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookGetBmkName(memRef, namePP, numP);
	else if (memRef && !isBook)
		MemDocGetBmkName(memRef, namePP, numP);
	else if (vfsRef)
		VfsGetBmkName(namePP, numP);
}

void PrefGetBmk(UInt16 idx, UInt16* chapterP, UInt32* offsetP)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	*chapterP = 0;
	*offsetP = 0;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookGetBmk(memRef, idx, chapterP, offsetP);
	else if (memRef && !isBook)
		MemDocGetBmk(memRef, idx, offsetP);
	else if (vfsRef)
		VfsGetBmk(idx, chapterP, offsetP);
}

void PrefAddBmk(Char* nameP, UInt16 chapter, UInt32 offset)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookAddBmk(memRef, nameP, chapter, offset);
	else if (memRef && !isBook)
		MemDocAddBmk(memRef, nameP, offset);
	else if (vfsRef)
		VfsAddBmk(nameP, chapter, offset);
}

void PrefDelBmk(UInt16 idx)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookDelBmk(memRef, idx);
	else if (memRef && !isBook)
		MemDocDelBmk(memRef, idx);
	else if (vfsRef)
		VfsDelBmk(idx);
}

void PrefDelAllBmk()
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	BookGetReference(&memRef, &vfsRef, &isBook);

	if (memRef && isBook)
		MemBookDelAllBmk(memRef);
	else if (memRef && !isBook)
		MemDocDelAllBmk(memRef);
	else if (vfsRef)
		VfsDelAllBmk();
}

void PrefGetHistory(HistoryDataType* histP, Int16 histNum, Int16* numP, Int16* posP)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;
	
	*numP = 0;
	*posP = -1;

	BookGetReference(&memRef, &vfsRef, &isBook);
	
	if (memRef && isBook)
		MemBookGetHistory(memRef, histP, histNum, numP, posP);
	else if (vfsRef && isBook && REGISTERED)
		VfsGetHistory(histP, histNum, numP, posP);
}

void PrefSaveHistory(HistoryDataType* histP, Int16 start, Int16 end, Int16 pos)
{
	DmOpenRef	memRef;
	FileRef		vfsRef;
	Boolean		isBook;

	BookGetReference(&memRef, &vfsRef, &isBook);
	
	if (memRef && isBook)
		MemBookSaveHistory(memRef, histP, start, end, pos);
	else if (vfsRef && isBook && REGISTERED)
		VfsSaveHistory(histP, start, end, pos);
}

void PrefDelete(Char* path, Boolean isBook)
{
	if (path[0] == '/')
		VfsPrefDelete(path);
	else if (!isBook)
		MemDocPrefDelete(path);
}

void PrefMemDocGetCategory(Char* nameP, Char* categoryP)
{
	Int16		i;
	MemHandle	recordH;
	DocPrefType	*prefP;

	categoryP[0] = 0;

	i = MemDocFindPrefs(nameP, NULL);
	if (i == -1)
		return;
	
	recordH = DmQueryRecord(l_dbRef, 0);
	prefP = MemHandleLock(recordH);
	prefP += i;
	StrNCopy(categoryP, prefP->category, dmCategoryLength);
	MemHandleUnlock(recordH);
}

void PrefMemDocSetCategory(Char* nameP, Char* categoryP)
{
	Int16		i;
	DocPrefType	pref;
	MemHandle	recordH;
	UInt8*		recordP;
	
	i = MemDocGetPrefs(nameP);
	
	recordH = DmGetRecord(l_dbRef, 0);
	recordP = MemHandleLock(recordH);

	MyMemCopy((UInt8*)&pref, recordP + sizeof(DocPrefType) * i, sizeof(DocPrefType));
	StrNCopy(pref.name, nameP, dmDBNameLength);
	StrNCopy(pref.category, categoryP, dmCategoryLength);
	DmWrite(recordP, sizeof(DocPrefType) * i, &pref, sizeof(DocPrefType));

	MemHandleUnlock(recordH);
	DmReleaseRecord(l_dbRef, 0, true);
}

void PrefMemDocChangeCategory(Char* newCategoryP, Char* oldCategoryP)
{
	DocPrefType	*prefP, pref;
	MemHandle	recordH;
	UInt8*		recordP;
	Int16		i, num, distance;
	
	recordH = DmGetRecord(l_dbRef, 0);
	num = MemHandleSize(recordH) / sizeof(DocPrefType);
	if (num == 0)
	{
		DmReleaseRecord(l_dbRef, 0, false);
		return;
	}
	
	recordP = MemHandleLock(recordH);
	prefP = (DocPrefType*)recordP;
	for (i=0, distance=0; i < num; i++, distance += sizeof(DocPrefType))
	{
		if (!StrCompare(oldCategoryP, prefP[i].category))
		{
			MyMemCopy((UInt8*)&pref, (UInt8*)&prefP[i], sizeof(DocPrefType));
			StrNCopy(pref.category, newCategoryP, dmCategoryLength);
			DmWrite(recordP, distance, &pref, sizeof(DocPrefType));
		}
	}
	
	MemHandleUnlock(recordH);
	DmReleaseRecord(l_dbRef, 0, true);
}

void PrefMemDocDelCategory(Char* categoryP)
{
	DocPrefType	*prefP, pref;
	MemHandle	recordH;
	UInt8*		recordP;
	Int16		i, num, distance;
	
	recordH = DmGetRecord(l_dbRef, 0);
	num = MemHandleSize(recordH) / sizeof(DocPrefType);
	if (num == 0)
	{
		DmReleaseRecord(l_dbRef, 0, false);
		return;
	}
	
	recordP = MemHandleLock(recordH);
	prefP = (DocPrefType*)recordP;
	for (i=0, distance=0; i < num; i++, distance += sizeof(DocPrefType))
	{
		if (!StrCompare(categoryP, prefP[i].category))
		{
			MyMemCopy((UInt8*)&pref, (UInt8*)&prefP[i], sizeof(DocPrefType));
			pref.category[0] = 0;
			DmWrite(recordP, distance, &pref, sizeof(DocPrefType));
		}
	}
	
	MemHandleUnlock(recordH);
	DmReleaseRecord(l_dbRef, 0, true);
}
