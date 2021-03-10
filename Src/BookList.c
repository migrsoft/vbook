// Virtual Book
// Book List handle

#include "VBook.h"
#include "LCD.h"
#include "CustomFont.h"
#include "BookList.h"
#include "Contents.h"
#include "View.h"
#include "CommonDlg.h"
#include "Bookmark.h"

#pragma mark === Macro & Structure ===

#define blCache	"VBookCache"
#define blType	'Cach'

#define MAX_OP			6
#define MAX_MEM_OP		6
#define MAX_VFS_OP		3
#define MAX_OP_ITEM_LEN	12

#define ITEM_CHECK		0x1
#define ITEM_DIRECTORY	0x2
#define ITEM_VBOOK		0x4
#define ITEM_DOC		0x8
#define ITEM_TEXT		0x10

typedef struct BookItemType {
	UInt16	cardNo;
	LocalID	dbID;
	Char*	titleP;
	Char*	fileP;
	UInt32	date;
	UInt32	size;
	UInt8	status;
	UInt8	categoryId;
} BookItemType;

typedef struct MemSavedType {
	LocalID	dbID;
	UInt32	date;
	UInt32	size;
	UInt8	status;
	UInt8	categoryId;
} MemSavedType;

typedef struct VfsSavedType {
	UInt32	date;
	UInt32	size;
	UInt8	status;
	UInt8	padding;
} VfsSavedType;

typedef struct ListInfoType {
	BookItemType*	itemsP;
	Int16			pos; // Only use when scan book.
	Int16			numItems;
	UInt16			volRef;
} ListInfoType;

typedef struct BookListType {
	ListInfoType*	listP;
	Int16*			filtered; // Save filtered list item index.
	Int16			filteredNum;
	Int16			filteredMax;
	Int16			top; // First line on screen
	Int16			bottom;
	Int16			pos;
	RectangleType	origBounds;
	RectangleType	bounds;
	Coord			col1; // Column width
	Coord			col3;
	Coord			height;
	Int16			lines;
} BookListType;

typedef struct {
	Char*	items[MAX_OP];
	Int16	num;
} OperationType;

typedef struct {
	UInt16	volRefs[2];
	Char*	volLabs[2];
	Int16	volNum;
} VolumesType;


#define BOOK_TYPE_INMEM		0x1
#define BOOK_TYPE_DOC		0x2
#define BOOK_TYPE_VBOOK		0x4
#define BOOK_TYPE_TEXT		0x8

typedef struct BookType {
	UInt16			type;
	DmOpenRef		memRef;
	FileRef			vfsRef;
	// Book header
	MemPtr			infoP;
	UInt32			length;		// use for doc, when doc save in vfs and len is wrong.
	// Contents
	MemHandle		titleIndexH; // Also used as blocks index.
	MemHandle		titleDataH;
	// Data record handle
	MemPtr			chapterIndexP;
	MemPtr			chapterInfoP;
	MemPtr			chapterDataP;
	UInt16			indexRecNo;
	UInt16			infoRecNo;
	UInt16			dataRecNo;
	// State
	UInt16			chapterNum;
	UInt16			chapterIndex; // Also use by text.
	UInt8*			infoEntryP;
} BookType;

void	SelectAll(Boolean select);
void	BooksSetFilter();
void	BooksDrawScreen();
UInt16	TextDecodeLen(UInt8* decodeFrom, UInt16 decodeFromLen);
void	Sort(ListInfoType* listInfoP, UInt8 method);
void	BooksChangeCategory(UInt8 id, Char* category);
void	BooksDelCategory(UInt8 id);
void	BookFormLayout(FormPtr frmP);
Boolean	BookListDoAction(UInt8 action);
void	BookListRefresh();
void	BookFormUpdateControl();

#pragma mark === Internal Variables ===

// Category
static Char* cateAll;
static Char* cateUnfiled;
static Char* cateEdit;
static Char* categories[dmRecNumCategories];
static UInt8 cateNum;
static UInt8 cateSelection;

// Operation
static OperationType	oper;

static VolumesType		l_vols;
static UInt16			l_volRef;

static Boolean			bookInMem = true;
static ListInfoType		memBookInfo;
static ListInfoType		vfsBookInfo;
static BookListType		bookList;
static BookItemType		quickBook;

static BookType			currentBook;
static Boolean			l_isPath;
static Boolean			resume;
static Boolean			saveLastBook;

#pragma mark === Internal Functions ===

static void DebugInfo(Char* message, Err err)
{
	Char	info[8];
	
	StrPrintF(info, "%x", err);
	FrmCustomAlert(DebugAlert, message, info, "");
}

static void TransPath(Boolean purePath)
{
	Int16	pos;
	
	if (bookInMem)
		return;
	
	if (l_isPath && !purePath) // Generate path + filename.
	{
		pos = StrLen(g_prefs.path);
		if (pos > 1)
			g_prefs.path[pos++] = '/';
		StrCopy(
			&g_prefs.path[pos],
			bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP);
		l_isPath = false;
	}
	else if (!l_isPath && purePath) // Need only path.
	{
		pos = StrLen(g_prefs.path) - 1;
		while (g_prefs.path[pos] != '/')
			pos--;
		if (pos == 0)
			pos++;
		g_prefs.path[pos] = 0;
		l_isPath = true;
	}
}

static Boolean HasCache()
{
	Boolean have = false;
	
	if (bookInMem)
	{
		if (DmFindDatabase(0, blCache))
			have = true;
	}
	else
	{
		Int16	pos;
		FileRef	ref;

		TransPath(true);
		pos = StrLen(g_prefs.path);
		if (pos > 1)
			g_prefs.path[pos++] = '/';
		StrCopy(&g_prefs.path[pos], blCache);
		if (!VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &ref))
		{
			VFSFileClose(ref);
			have = true;
		}
		if (pos == 1)
			g_prefs.path[pos] = 0;
		else
			g_prefs.path[--pos] = 0;
	}
	
	return have;
}

static void DeleteCache()
{
	if (bookInMem)
	{
		LocalID id = DmFindDatabase(0, blCache);
		if (id)
			DmDeleteDatabase(0, id);
	}
	else
	{
		Int16	pos;

		TransPath(true); // Convert pathname to pure pathname.
		pos = StrLen(g_prefs.path);
		if (pos > 1)
			g_prefs.path[pos++] = '/';
		StrCopy(&g_prefs.path[pos], blCache);
		VFSFileDelete(l_volRef, g_prefs.path);
		if (pos == 1)
			g_prefs.path[pos] = 0;
		else
			g_prefs.path[--pos] = 0;
	}
}

static UInt8 CategoryAdd(Char* p)
{
	UInt8	i;

	if (cateNum == dmRecNumCategories || !*p || !StrLen(p))
		return dmUnfiledCategory;

	for (i = 1; i < cateNum - 2; i++)
	{
		// Already has this category.
		if (!StrCompare(p, categories[i]))
			return i;
	}
	
	// Add a new category.
	categories[i] = (Char*)MemPtrNew(dmCategoryLength);
	StrNCopy(categories[i], p, dmCategoryLength);
	cateNum++;
	categories[i + 1] = cateUnfiled;
	categories[i + 2] = cateEdit;

	return i;
}

static Int16 MemScanBookByType(UInt32 type, UInt32 creator, Boolean justTotal)
{
	Err					err;
	Char				title[dmDBNameLength];
	UInt16				cardNo;
	LocalID				dbID;
	DmSearchStateType	searchstate;
	Int16				count = 0;
	DmOpenRef			openRef;
	MemHandle			record;
	VBOOKINFO*			infoP;

	err = DmGetNextDatabaseByTypeCreator(
		true, &searchstate,
		type, creator, false, &cardNo, &dbID);
	if (err == dmErrCantFind)
		return 0;
	
	while (true)
	{
		if (!justTotal)
		{
			err = DmDatabaseInfo(
				cardNo, dbID,
				title, NULL, NULL,
				&memBookInfo.itemsP[memBookInfo.pos].date, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL);

			memBookInfo.itemsP[memBookInfo.pos].cardNo = cardNo;
			memBookInfo.itemsP[memBookInfo.pos].dbID = dbID;
			memBookInfo.itemsP[memBookInfo.pos].titleP = (Char*)MemPtrNew(StrLen(title)+1);
			StrCopy(memBookInfo.itemsP[memBookInfo.pos].titleP, title);
			DmDatabaseSize(cardNo, dbID, NULL,
				&memBookInfo.itemsP[memBookInfo.pos].size, NULL);

			if (type == VBookType && creator == VBookCreator)
			{
				memBookInfo.itemsP[memBookInfo.pos].status = ITEM_VBOOK;
				
				openRef = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);
				record = DmQueryRecord(openRef, 0);
				infoP = MemHandleLock(record);

				memBookInfo.itemsP[memBookInfo.pos].categoryId =
					CategoryAdd((Char*)infoP->szCategory);
				
				MemHandleUnlock(record);
				DmCloseDatabase(openRef);
			}
			else if (type == DocType &&
				(creator == DocCreator || creator == HSCreator))
			{
				memBookInfo.itemsP[memBookInfo.pos].status = ITEM_DOC;

				// Get Palmdoc's category in prefs database,
				// I use title[] for returned category name.
				PrefMemDocGetCategory(memBookInfo.itemsP[memBookInfo.pos].titleP, title);
				memBookInfo.itemsP[memBookInfo.pos].categoryId = CategoryAdd(title);
			}

			memBookInfo.pos++;
		}
		
		count++;
		err = DmGetNextDatabaseByTypeCreator(
			false, &searchstate,
			type, creator, false, &cardNo, &dbID);
		if (err == dmErrCantFind)
			break;
	}

	return count;
}

static Int16 VfsScanBookByType(
	Boolean scanDir, UInt32 type, UInt32 creator, Boolean justTotal)
{
	Err				err;
	UInt32			fileIterator;
	FileInfoType	fileInfo;
	FileRef			dirRef, fileRef;
	Char			name[dmDBNameLength];
	Int16			count = 0, pos, ext;
	UInt32			t, c;
	UInt32			date;

	err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &dirRef);
	if (err)
		return 0;

	pos = StrLen(g_prefs.path);
	l_isPath = false;
	if (pos > 1)
	{
		g_prefs.path[pos++] = '/';
		g_prefs.path[pos] = 0;
	}

	fileInfo.nameP = (Char*)MemPtrNew(256);
	fileInfo.nameBufLen = 255;
	fileIterator = expIteratorStart;
	while (fileIterator != expIteratorStop)
	{
		err = VFSDirEntryEnumerate(dirRef, &fileIterator, &fileInfo);
		if (err) break;

		if (scanDir)
		{
			if (!(fileInfo.attributes & vfsFileAttrDirectory))
				continue;

			if (!justTotal)
			{
				// titleP != NULL  fileP == NULL
				vfsBookInfo.itemsP[vfsBookInfo.pos].titleP =
					(Char*)MemPtrNew(StrLen(fileInfo.nameP) + 1);
				StrCopy(vfsBookInfo.itemsP[vfsBookInfo.pos].titleP, fileInfo.nameP);

				vfsBookInfo.itemsP[vfsBookInfo.pos].status = ITEM_DIRECTORY;

				vfsBookInfo.pos++;
			}

			count++;
		}
		else // Scan file.
		{
			if (fileInfo.attributes & vfsFileAttrDirectory ||
				fileInfo.attributes & vfsFileAttrSystem ||
				fileInfo.attributes & vfsFileAttrVolumeLabel)
				continue;
			
			ext = StrLen(fileInfo.nameP) - 4; // Find ext-name.
			if (ext <= 0)
				continue;

			// Is pdb or text?
			StrToLower(name, &fileInfo.nameP[ext]);
			
			if (type == TxtType)
			{
				if (StrCompare(name, ".txt"))
					continue;
			}
			else
			{
				if (StrCompare(name, ".pdb"))
					continue;
			}

			// Find out type of file.
			StrCopy(&g_prefs.path[pos], fileInfo.nameP);
			err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &fileRef);
			if (err) continue;

			if (type != TxtType)
			{
				VFSFileDBInfo(
					fileRef, name, NULL, NULL,
					&date, NULL, NULL, NULL, NULL, NULL,
					&t, &c, NULL);

				if (t != type || c != creator)
				{
					VFSFileClose(fileRef);
					continue;
				}
			}
			
			if (!justTotal)
			{
				VFSFileSize(fileRef, &vfsBookInfo.itemsP[vfsBookInfo.pos].size);

				if (type == TxtType)
				{
					VFSFileGetDate(
						fileRef,
						vfsFileDateCreated,
						&vfsBookInfo.itemsP[vfsBookInfo.pos].date);

					// titleP != NULL  fileP == titleP
					vfsBookInfo.itemsP[vfsBookInfo.pos].titleP =
						(Char*)MemPtrNew(StrLen(fileInfo.nameP)+1);
					StrCopy(vfsBookInfo.itemsP[vfsBookInfo.pos].titleP, fileInfo.nameP);
					
					vfsBookInfo.itemsP[vfsBookInfo.pos].fileP =
						vfsBookInfo.itemsP[vfsBookInfo.pos].titleP;
				}
				else
				{
					vfsBookInfo.itemsP[vfsBookInfo.pos].date = date;

					if (g_prefs.options & OPSHOWFNAME)
					{
						// titleP != NULL  fileP == titleP
						vfsBookInfo.itemsP[vfsBookInfo.pos].titleP =
							(Char*)MemPtrNew(StrLen(fileInfo.nameP)+1);
						StrCopy(vfsBookInfo.itemsP[vfsBookInfo.pos].titleP, fileInfo.nameP);
						
						vfsBookInfo.itemsP[vfsBookInfo.pos].fileP =
							vfsBookInfo.itemsP[vfsBookInfo.pos].titleP;
					}
					else
					{
						// titleP != NULL  fileP != NULL
						vfsBookInfo.itemsP[vfsBookInfo.pos].titleP =
							(Char*)MemPtrNew(StrLen(name)+1);
						StrCopy(vfsBookInfo.itemsP[vfsBookInfo.pos].titleP, name);

						vfsBookInfo.itemsP[vfsBookInfo.pos].fileP =
							(Char*)MemPtrNew(StrLen(fileInfo.nameP)+1);
						StrCopy(vfsBookInfo.itemsP[vfsBookInfo.pos].fileP, fileInfo.nameP);
					}
				}
				
				if (t == VBookType && c == VBookCreator)
					vfsBookInfo.itemsP[vfsBookInfo.pos].status = ITEM_VBOOK;
				else if (t == DocType && (c == DocCreator || c == HSCreator))
					vfsBookInfo.itemsP[vfsBookInfo.pos].status = ITEM_DOC;
				else
					vfsBookInfo.itemsP[vfsBookInfo.pos].status = ITEM_TEXT;
				
				// When book saved in expansion card, I ignore its category.
				vfsBookInfo.itemsP[vfsBookInfo.pos].categoryId = 0;

				vfsBookInfo.pos++;
			}

			VFSFileClose(fileRef);
			count++;
		}
	}

	VFSFileClose(dirRef);
	MemPtrFree(fileInfo.nameP);
	TransPath(true);

	return count;
}

static Boolean OpenDatabase()
{
	Err		err;
	Boolean	success;

	if (currentBook.type & BOOK_TYPE_INMEM)
	{
		if (resume)
		{
			currentBook.memRef = DmOpenDatabase(
				quickBook.cardNo, quickBook.dbID,
				dmModeReadWrite);
		}
		else
		{
			if (!HasCache() ||
				DmFindDatabase(
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].cardNo,
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP)
				)
			{
				currentBook.memRef = DmOpenDatabase(
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].cardNo,
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].dbID,
					dmModeReadWrite);
			}
			else
				currentBook.memRef = 0;
		}
		success = (currentBook.memRef) ? true : false;
	}
	else
	{
		if (resume)
		{
			err = VFSFileOpen(
				l_volRef, quickBook.fileP,
				vfsModeRead, &currentBook.vfsRef);
		}
		else
		{
			TransPath(false);
			err = VFSFileOpen(
				l_volRef, g_prefs.path,
				vfsModeRead, &currentBook.vfsRef);
		}
		success = (err == errNone) ? true : false;
	}

	return success;
}

static void CloseDatabase()
{
	if (currentBook.type & BOOK_TYPE_INMEM)
	{
		if (currentBook.memRef)
		{
			DmCloseDatabase(currentBook.memRef);
			currentBook.memRef = 0;
		}
	}
	else
	{
		if (currentBook.vfsRef)
		{
			VFSFileClose(currentBook.vfsRef);
			currentBook.vfsRef = 0;
		}
	}
}

static MemHandle QueryRecord(UInt16 recIndex)
{
	MemHandle record;
	
	if (currentBook.type & BOOK_TYPE_INMEM)
		return DmQueryRecord(currentBook.memRef, recIndex);

	VFSFileDBGetRecord(currentBook.vfsRef, recIndex, &record, NULL, NULL);

	return record;
}

static UInt16 NumRecords()
{
	UInt16 num;
	
	if (currentBook.type & BOOK_TYPE_INMEM)
		return DmNumRecords(currentBook.memRef);
	
	VFSFileDBInfo(currentBook.vfsRef,
		NULL, NULL, NULL,			// name, attribute and version
		NULL, NULL, NULL, NULL,		// time
		NULL, NULL,					// app info and sort info
		NULL, NULL,					// type and creator
		&num);
	
	return num;
}

// Decode functions set.

static Int16 TextDecode(
	UInt8* decodeTo, UInt8* decodeFrom, Int16 decodeFromLen, Int16 maxDecodeLen)
{
	UInt16	c;

	UInt8*	fromTooFar = &decodeFrom[decodeFromLen];
	UInt8*	toTooFar = &decodeTo[maxDecodeLen];
	UInt8*	decodeStart = decodeTo;

	// These are used only in B commands
	Int16	windowLen, windowDist;
	UInt8*	windowCopyFrom;

	while (decodeFrom < fromTooFar && decodeTo < toTooFar)
	{
		c = *(decodeFrom++);

		// type C command (space + char)
		if (c >= 0xC0)
		{
			*(decodeTo++) = ' ';
			if (decodeTo < toTooFar)
				*(decodeTo++) = c & 0x7F;
	    }
		// type B command (sliding window sequence)
		else if (c >= 0x80)
		{
			// Move this to high bits and read low bits
			c = (c << 8) | *(decodeFrom++);
			// 3 + low 3 bits (Beirne's 'n'+3)
			windowLen = 3 + (c & 0x7);
			// next 11 bits (Beirne's 'm')
			windowDist = (c >> 3) & 0x07FF;
			windowCopyFrom = decodeTo - windowDist;

			windowLen = (windowLen < toTooFar - decodeTo) ? windowLen : toTooFar - decodeTo;
			while (windowLen--)
				*(decodeTo++) = *(windowCopyFrom++);
	    }
		//self-representing, no command.
		else if (c >= 0x09)
		{
			*(decodeTo++) = c;
		}
		//type A command (next c chars are literal)
		else if (c >= 0x01)
		{
			c = (c < toTooFar - decodeTo) ? c : toTooFar - decodeTo;
			while (c--)
				*(decodeTo++) = *(decodeFrom++);
			}
		//c == 0, also self-representing
		else
		{
			*(decodeTo++) = c;
		}
	}
	return decodeTo - decodeStart;
}

static UInt16 TextDecodeLen(UInt8* decodeFrom, UInt16 decodeFromLen)
{
	UInt8*	fromTooFar = &decodeFrom[decodeFromLen];
	UInt16	c, len = 0;

	while(decodeFrom < fromTooFar)
	{
		c = *(decodeFrom++);

		// type C command (space + char)
		if (c >= 0xC0)
		{
			len += 2;
		}
		// type B command (sliding window sequence)
		else if (c >= 0x80)
		{
			// Move this to high bits and read low bits
			c = (c << 8) | *(decodeFrom++);
			// 3 + low 3 bits (Beirne's 'n'+3)
			len += 3 + (c & 0x7);
		}
		//self-representing, no command.
		else if (c >= 0x09)
		{
			len++;
		}
		//type A command (next c chars are literal)
		else if (c >= 0x01)
		{
			len += c;
			decodeFrom += c;
		}
		//c == 0, also self-representing
		else
		{
			len++;
		}
	}
	return len;
}

static Int16 ZLibDecode(
	UInt8* decodeTo, UInt8* decodeFrom, Int16 decodeFromLen, Int16 maxDecodeLen)
{
	Err			err;
	z_stream	stream;

	stream.zalloc	= (alloc_func)Z_NULL;
	stream.zfree	= (free_func)Z_NULL;
	stream.opaque	= (voidpf)Z_NULL;

	stream.next_in	= decodeFrom;
	stream.avail_in	= 0;
	stream.next_out	= decodeTo;

	err = inflateInit(&stream);

	stream.avail_in		= decodeFromLen;
	stream.avail_out	= maxDecodeLen;

	err = inflate(&stream, Z_NO_FLUSH);
	err = inflateEnd(&stream);
	
	return stream.total_out;
}

static Boolean VBookGetBlock(
	UInt16 dataIndex, UInt8* bufP, UInt16* dataLenP, Int16 bufLen, UInt32* endLenP)
{
	Boolean				result;
	VBOOKINFO*			infoP;
	VBOOKCHAPTERINDEX*	pci;
	VBOOKBLOCKINDEX*	pbi;
	MemHandle			record;
	UInt8*				p;
	Int16				i;
	UInt16				len;
	
	pci = currentBook.chapterIndexP;
	if (dataIndex >= pci[currentBook.chapterIndex].nBlocksNum)
		return false;

	// Chapter info compose with:
	//   External links (4 Bytes)
	//   Internal links (4 Bytes)
	//   Block index
	pbi = (VBOOKBLOCKINDEX*)(currentBook.infoEntryP +
		4 * (pci[currentBook.chapterIndex].nExtLinksNum +
		pci[currentBook.chapterIndex].nIntLinksNum));
	
	if (pbi[dataIndex].nRecNo != currentBook.dataRecNo)
	{
		if (currentBook.chapterDataP)
		{
			MemPtrUnlock(currentBook.chapterDataP);
			if (!(currentBook.type & BOOK_TYPE_INMEM))
				MemPtrFree(currentBook.chapterDataP);
		}
		
		record = QueryRecord(pbi[dataIndex].nRecNo);
		currentBook.chapterDataP = MemHandleLock(record);
		currentBook.dataRecNo = pbi[dataIndex].nRecNo;
	}
	
	if (bufP == NULL)
	{
		*dataLenP = pbi[dataIndex].nOrigLen;
		return true;
	}

	p = (UInt8*)currentBook.chapterDataP + pbi[dataIndex].nOffset;

	infoP = currentBook.infoP;
	if (infoP->nType & COMPRESS_TEXT)
	{
		len = TextDecode(bufP, p, pbi[dataIndex].nComprLen, bufLen);
		result = (len == pbi[dataIndex].nOrigLen) ? true : false;
	}
	else if (infoP->nType & COMPRESS_ZLIB)
	{
		len = ZLibDecode(bufP, p, pbi[dataIndex].nComprLen, bufLen);
		result = (len == pbi[dataIndex].nOrigLen) ? true : false;
	}
	else
	{
		for (i=0; i < pbi[dataIndex].nOrigLen; i++)
			*bufP++ = *p++;
		result = true;
	}
	
	*endLenP = pbi[dataIndex].nEndLen;

	return result;
}


static Boolean DocGetBlock(
	UInt16 dataIndex, UInt8* bufP, UInt16* dataLenP, Int16 bufLen, UInt32* endLenP)
{
	PALMDOCINFO	*docInfoP = (PALMDOCINFO*)currentBook.infoP;
	UInt16		num = currentBook.chapterIndex;
	MemHandle	recordH;
	UInt8		*recordP;
	Int16		i;
	UInt16		size;
	
	if (dataIndex >= num) return false;
	
	recordH = QueryRecord(dataIndex + 1);

	// Calcs actual length of text block.
	if (docInfoP->nVersion == 2)
	{
		size = TextDecodeLen(MemHandleLock(recordH), MemHandleSize(recordH));
		MemHandleUnlock(recordH);
	}
	else
		size = MemHandleSize(recordH);

	if (dataLenP) *dataLenP = size;
	
	if (bufP == NULL)
	{
		if (!(currentBook.type & BOOK_TYPE_INMEM))
			MemHandleFree(recordH);

		return true;
	}
	
	// Decodes contents of a text block.
	
	recordP = MemHandleLock(recordH);

	if (docInfoP->nVersion == 2)
		TextDecode(bufP, recordP, MemHandleSize(recordH), bufLen);
	else
		MemMove(bufP, recordP, MemHandleSize(recordH));
	
	MemHandleUnlock(recordH);
	if (!(currentBook.type & BOOK_TYPE_INMEM))
		MemHandleFree(recordH);
	
	// Filters contents of a text block.
	
 	for (i = 0; i < size; i++)
 	{
		if (bufP[i] == 0xd)
			bufP[i] = 0xa;

		if (bufP[i] < 0x20 && bufP[i] != 0xa)
			bufP[i] = ' ';
 	}

	*endLenP = (UInt32)dataIndex * docInfoP->nSize;
	*endLenP += size;

	return true;
}


static Boolean TxtGetBlock(
	UInt16 dataIndex, UInt8* bufP, UInt16* dataLenP, Int16 bufLen, UInt32* endLenP)
{
	UInt16		num, i;
	UInt32		c;
	
	num = currentBook.chapterIndex;
	if (dataIndex >= num) return false;
	
	if (dataLenP)
	{
		*dataLenP = (dataIndex < num - 1) ?
			4096 : currentBook.length - dataIndex * 4096;
	}
	
	if (bufP == NULL) return true;
	
	VFSFileSeek(currentBook.vfsRef, vfsOriginBeginning, (Int32)dataIndex * 4096);
	VFSFileRead(currentBook.vfsRef, 4096, bufP, &c);
	
 	for (i = 0; i < c; i++)
 	{
		if (bufP[i] == 0xd)
			bufP[i] = (bufP[i + 1] == 0xa) ? 0xe : 0xa; // Convert 0xd0xa to 0xa

		if (bufP[i] < 0x20 && bufP[i] != 0xa)
			bufP[i] = ' ';
 	}
	
	if (dataIndex < num - 1)
		*endLenP = ((UInt32)dataIndex + 1) * 4096;
	else
		*endLenP = currentBook.length;
	
	return true;
}

static Boolean VBookGetDataIndexByOffset(
	UInt32 offset, UInt16* indexP, UInt16* offsetP)
{
	VBOOKCHAPTERINDEX*	pci;
	VBOOKBLOCKINDEX*	pbi;
	UInt16				i;
	
	pci = currentBook.chapterIndexP;

	if (offset >= pci[currentBook.chapterIndex].nLength)
		return false;

	pbi = (VBOOKBLOCKINDEX*)(currentBook.infoEntryP + 4 *
		(pci[currentBook.chapterIndex].nExtLinksNum +
		pci[currentBook.chapterIndex].nIntLinksNum));
	
	for (i=0; i < pci[currentBook.chapterIndex].nBlocksNum; i++)
		if (offset < pbi[i].nEndLen)
			break;
	
	*indexP = i;
	*offsetP = (UInt16)(offset - (pbi[i].nEndLen - pbi[i].nOrigLen));
	
	return true;
}


static Boolean DocGetDataIndexByOffset(
	UInt32 offset, UInt16* indexP, UInt16* offsetP)
{
	PALMDOCINFO *docInfoP = (PALMDOCINFO*)currentBook.infoP;
	MemHandle	recordH;
	UInt16		textLen;

	if (offset > currentBook.length - 1)
		offset = currentBook.length - 1;

	*indexP = offset / docInfoP->nSize;
	*offsetP = offset % docInfoP->nSize;
	
	// Verify offset in a block.
	// Usually, each text block in a palm document will be a same amount,
	// but not all document respect this rule.
	
	recordH = QueryRecord(*indexP + 1);
	if (docInfoP->nVersion == 2)
	{
		textLen = TextDecodeLen(
			MemHandleLock(recordH),
			MemHandleSize(recordH));
		
		MemHandleUnlock(recordH);
	}
	else
		textLen = MemHandleSize(recordH);
	
	if (!(currentBook.type & BOOK_TYPE_INMEM))
		MemHandleFree(recordH);
	
	if (*offsetP >= textLen)
		*offsetP = textLen - 1;
	
	return true;
}


static void Register()
{
	FormPtr		frmP;
	FieldPtr	fieldP;
	Char*		nameP;
	Char*		codeP;
	Char		chr;
	Int16		i, j, k, m;
	Char		temp[10];
	
	if (g_prefs.fake == 1977)
	{
		FrmAlert(RegisteredAlert);
		return;
	}
	
	frmP = FrmInitForm(RegisterForm);

	nameP = MemPtrNew(dlkUserNameBufSize);
	DlkGetSyncInfo(NULL, NULL, NULL, nameP, NULL, NULL);
//	StrCopy(nameP, "wil kim long");
	if (StrLen(nameP))
		FrmCopyLabel(frmP, RegisterUserLabel, nameP);
	else
		FrmCopyLabel(frmP, RegisterUserLabel, "(none)");
	
	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, RegisterRegField));
	
	if (FrmDoDialog(frmP) == RegisterOKButton && StrLen(nameP))
	{
		fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, RegisterRegField));
		codeP = FldGetTextPtr(fieldP);
		
		if (codeP)
		{
			i = j = 0;
			while (nameP[i])
				j += (UInt8)nameP[i++];
			// Adjust.
			while (j < 1000)
				j *= 7;
			
			StrPrintF(temp, "%d", j);

			for (i=0, k=0, m=0; i < 4 && codeP[k]; i++, k++)
			{
				chr = (j / (temp[i] - 47)) % 26 + 65;
				 if (chr == codeP[k])
				 	m++;
			}

			if (codeP[k] && codeP[k] == '-')
				m++;

			for (i=3, k++; i >= 0 && codeP[k]; i--, k++)
			{
				chr = (j / (11 - (temp[i] - 47))) % 26 + 65;
				if (chr == codeP[k])
					m++;
			}
			
			if (m == 9 && codeP[k] == 0)
			{
				FrmAlert(RegSucceedAlert);
				g_prefs.options |= OPREGISTERED;
				g_prefs.fake = 1977;
			}
			else
				FrmAlert(RegErrorAlert);
		}
		else
			FrmAlert(RegErrorAlert);
	}
	
	FrmDeleteForm(frmP);
	MemPtrFree(nameP);
}

static void EnumerateVolumes()
{
	Err					err;
	Char*				labelP;
	Int16				i;
	UInt32				volIterator;
	
	if (g_expansion)
	{
		volIterator = vfsIteratorStart;
		labelP = MemPtrNew(vbookMaxLabelLen);
		
		for (i = 0; i < 2; i++)
		{
			VFSVolumeEnumerate(&l_volRef, &volIterator);
			if (l_volRef == vfsInvalidVolRef) break;
			
			l_vols.volRefs[i] = l_volRef;

			// Get volume label.
			MemSet(labelP, vbookMaxLabelLen, 0);
			err = VFSVolumeGetLabel(l_volRef, labelP, vbookMaxLabelLen);
			if (err == vfsErrBufferOverflow)
				DebugInfo("A long label!", err);
			
			labelP[31] = 0;
			if (labelP[0] == 0)
				StrCopy(labelP, "no label");

			l_vols.volLabs[i] = MemPtrNew(StrLen(labelP) + 1);
			StrCopy(l_vols.volLabs[i], labelP);
		}
		
		l_vols.volNum = i;
		MemPtrFree(labelP);
		
		l_volRef = l_vols.volRefs[0];
	}
	else
		l_volRef = vfsInvalidVolRef;
}

static Char* GetCardLabel(UInt16 volRef)
{
	Int16	i;
	
	for (i = 0; i < l_vols.volNum; i++)
		if (l_vols.volRefs[i] == volRef) break;
	
	return ((i < l_vols.volNum) ? l_vols.volLabs[i] : NULL);
}

static UInt16 GetVolumeRef(Char *labelP)
{
	Int16	i;
	
	if (*labelP == 0) return vfsInvalidVolRef;
	
	for (i = 0; i < l_vols.volNum; i++)
		if (!StrCompare(l_vols.volLabs[i], labelP))
			return l_vols.volRefs[i];
	
	return vfsInvalidVolRef;
}

static void Waiting(UInt16 rscID)
{
	RectangleType	rect;
	Char			message[30];
	
//	#ifdef PALMOS_30
//	FontID			oldFont;
//	#endif

//	#ifdef PALMOS_30
//	oldFont = FntSetFont(boldFont);
//	#else

	WinPushDrawState();

	FntSetFont(boldFont);
	WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
	WinSetBackColor(UIColorGetTableEntryIndex(UIFormFill));

//	#endif // PALMOS_30

	rect.topLeft.x	= bookList.bounds.topLeft.x;
	rect.topLeft.y	= bookList.bounds.topLeft.y;
	rect.extent.x	= bookList.bounds.extent.x;
	rect.extent.y	= FntCharHeight();
	#ifdef HIGH_DENSITY
	rect.topLeft.x >>= 1;
	rect.topLeft.y >>= 1;
	rect.extent.x >>= 1;
	#endif

	WinEraseRectangle(&rect, 0);
	SysCopyStringResource(message, rscID);
	WinDrawChars(message, StrLen(message), rect.topLeft.x, rect.topLeft.y);

//	#ifdef PALMOS_30
//	FntSetFont(oldFont);
//	#else
	WinPopDrawState();
//	#endif
}

static void CurrentPlace()
{
	FormPtr		frmP;
	FieldPtr	fldP;
	MemHandle	txtH, oldTxtH;
	Char		*txtP;
	Int16		i, j;
	
	if (bookInMem) return;
	
	TransPath(true);

	i = StrLen(g_prefs.path) - 1;
	if (i)
	{
		for (j = 0; g_prefs.path[i] != '/'; i--, j++) continue;
		i++;
	}
	else
		j = 1;
	
	txtH = MemHandleNew(j + 1);
	txtP = MemHandleLock(txtH);
	StrNCopy(txtP, &g_prefs.path[i], j);
	txtP[j] = 0;
	MemHandleUnlock(txtH);
	
	frmP = FrmGetActiveForm();
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainDirField));
	oldTxtH = FldGetTextHandle(fldP);
	FldSetTextHandle(fldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);
	FldDrawField(fldP);
}

static Boolean CacheListInMem()
{
	DmOpenRef		ref;
	LocalID			id;
	MemHandle		record;
	MemPtr			recordP;
	MemSavedType	saved;
	UInt16			at;
	Int16			i, num, len;
	UInt32			offset;
	
	if (memBookInfo.numItems == 0)
		return true;
	
	id = DmFindDatabase(0, blCache);
	if (id)
		DmDeleteDatabase(0, id);
	
	if (DmCreateDatabase(0, blCache, appFileCreator, blType, false) != errNone)
	{
		DebugInfo("Create", DmGetLastErr());
		return false;
	}
	
	id = DmFindDatabase(0, blCache);
	ref = DmOpenDatabase(0, id, dmModeWrite | dmModeExclusive);
	if (ref == 0)
	{
		DebugInfo("Open", DmGetLastErr());
		return false;
	}
	
	if (cateNum > 3)
	{
		// Has categories, save first.
		
		for (i = 1, num = cateNum - 2, len = 0; i < num; i++)
			len += StrLen(categories[i]) + 1;
		
		at = dmMaxRecordIndex;
		record = DmNewRecord(ref, &at, len);
		if (record == 0)
		{
			DebugInfo("CA NewRecord", DmGetLastErr());
		}

		recordP = MemHandleLock(record);
		offset = 0;
		for (i = 1; i < num; i++)
		{
			len = StrLen(categories[i]) + 1;
			DmWrite(recordP, offset, categories[i], len);
			offset += (UInt16)len;
		}

		MemHandleUnlock(record);
		DmReleaseRecord(ref, at, true);
	}
	
	// Save book list.
	at = dmMaxRecordIndex;
	record = DmNewRecord(ref, &at,
		sizeof(MemSavedType) * memBookInfo.numItems + sizeof(Int16));
	if (record == 0)
	{
		DebugInfo("BL NewRecord", DmGetLastErr());
	}

	recordP = MemHandleLock(record);
	DmWrite(recordP, 0, &memBookInfo.numItems, sizeof(Int16));
	offset = 2;
	len = 0;
	for (i = 0; i < memBookInfo.numItems; i++)
	{
		saved.dbID			= memBookInfo.itemsP[i].dbID;
		saved.date			= memBookInfo.itemsP[i].date;
		saved.size			= memBookInfo.itemsP[i].size;
		saved.status		= memBookInfo.itemsP[i].status;
		saved.categoryId	= memBookInfo.itemsP[i].categoryId;
		
		DmWrite(recordP, offset, &saved, sizeof(MemSavedType));
		offset += sizeof(MemSavedType);
		
		len += StrLen(memBookInfo.itemsP[i].titleP) + 1;
	}
	MemHandleUnlock(record);
	DmReleaseRecord(ref, at, true);
	
	// Save title list.
	at = dmMaxRecordIndex;
	record = DmNewRecord(ref, &at, len);
	if (record == 0)
	{
		DebugInfo("TL NewRecord", DmGetLastErr());
	}
	
	recordP = MemHandleLock(record);
	offset = 0;
	for (i = 0; i < memBookInfo.numItems; i++)
	{
		len = StrLen(memBookInfo.itemsP[i].titleP) + 1;
		DmWrite(recordP, offset, memBookInfo.itemsP[i].titleP, len);
		offset += (UInt16)len;
	}
	MemHandleUnlock(record);
	DmReleaseRecord(ref, at, true);

	DmCloseDatabase(ref);
	
	return true;
}

static Boolean CacheListInVfs()
{
	Char			pathName[256], holder[2];
	Int16			i, len;
	FileRef			ref;
	Err				err;
	VfsSavedType	saved;
	BytePtr			bufP;
	UInt16			bufLen;
	
	if (vfsBookInfo.numItems == 0)
		return true;
	
	bufP = MemPtrNew(1024);
	
	holder[0] = 1;
	holder[1] = 0;
	
	TransPath(true);
	StrCopy(pathName, g_prefs.path);
	i = StrLen(pathName);
	if (i > 1)
		pathName[i++] = '/';
	StrCopy(&pathName[i], blCache);
	
	err = VFSFileOpen(l_volRef, pathName, vfsModeRead, &ref);
	if (!err)
	{
		VFSFileClose(ref);
		VFSFileDelete(l_volRef, pathName);
	}
	else if (err != vfsErrFileNotFound)
	{
		DebugInfo("VFS Cache", err);
		return false;
	}
	
	err = VFSFileCreate(l_volRef, pathName);
	if (err)
	{
		DebugInfo("VFS Create", err);
		return false;
	}
	
	err = VFSFileOpen(l_volRef, pathName, vfsModeWrite, &ref);
	if (err)
	{
		DebugInfo("VFS Open", err);
		return false;
	}
	
	err = VFSFileWrite(ref, sizeof(Int16), &vfsBookInfo.numItems, NULL);
	if (err)
		DebugInfo("Write Num", err);
	
	// Save item list.
	bufLen = 0;
	for (i = 0; i < vfsBookInfo.numItems; i++)
	{
		saved.date		= vfsBookInfo.itemsP[i].date;
		saved.size		= vfsBookInfo.itemsP[i].size;
		saved.status	= vfsBookInfo.itemsP[i].status;
		
		if (bufLen + sizeof(VfsSavedType) > 1024)
		{
			VFSFileWrite(ref, bufLen, bufP, NULL);
			bufLen = 0;
		}
		MemMove(&bufP[bufLen], &saved, sizeof(VfsSavedType));
		bufLen += sizeof(VfsSavedType);
	}
	if (bufLen)
		VFSFileWrite(ref, bufLen, bufP, NULL);

	// Save title and filename.
	bufLen = 0;
	for (i = 0; i < vfsBookInfo.numItems; i++)
	{
		len = StrLen(vfsBookInfo.itemsP[i].titleP) + 1;

		if (bufLen + len > 1024)
		{
			VFSFileWrite(ref, bufLen, bufP, NULL);
			bufLen = 0;
		}
		MemMove(&bufP[bufLen], vfsBookInfo.itemsP[i].titleP, len);
		bufLen += len;

		if (vfsBookInfo.itemsP[i].fileP == vfsBookInfo.itemsP[i].titleP ||
			vfsBookInfo.itemsP[i].fileP == NULL)
			len = 2;
		else
			len = StrLen(vfsBookInfo.itemsP[i].fileP) + 1;
		
		if (bufLen + len > 1024)
		{
			VFSFileWrite(ref, bufLen, bufP, NULL);
			bufLen = 0;
		}
		if (len == 2)
			MemMove(&bufP[bufLen], holder, len);
		else
			MemMove(&bufP[bufLen], vfsBookInfo.itemsP[i].fileP, len);
		bufLen += len;
	}
	if (bufLen)
		VFSFileWrite(ref, bufLen, bufP, NULL);
	
	VFSFileClose(ref);
	MemPtrFree(bufP);
	
	return true;
}

static void ListInfoClean(ListInfoType* listP)
{
	Int16	i;
	
	if (listP->numItems == 0)
		return;
	
	for (i = 0; i < listP->numItems; i++)
	{
		if (listP->itemsP[i].fileP &&
			listP->itemsP[i].fileP != listP->itemsP[i].titleP)
			MemPtrFree(listP->itemsP[i].fileP);

		if (listP->itemsP[i].titleP)
			MemPtrFree(listP->itemsP[i].titleP);
	}
	MemPtrFree(listP->itemsP);

	MemSet(listP, sizeof(ListInfoType), 0);
}

static void ScanAllBooksInMem()
{
	UInt16			size;
	DmOpenRef		ref;
	LocalID			id;
	MemHandle		record;
	MemPtr			recordP;
	Char			*p, *tooFar;
	Int16			i, len;
	UInt16			start;
	MemSavedType	*savedP;

	ListInfoClean(&memBookInfo);
	
	id = DmFindDatabase(0, blCache);
	if (id)
	{
		ref = DmOpenDatabase(0, id, dmModeReadOnly);

		start = 0;
		if (DmNumRecords(ref) == 3)
		{
			// Load categories first.
			record = DmQueryRecord(ref, start);
			p = MemHandleLock(record);
			tooFar = p + MemHandleSize(record);
			i = 1;
			while (p != tooFar)
			{
				len = StrLen(p) + 1;
				categories[i] = MemPtrNew(len);
				StrCopy(categories[i], p);
				p += len;
				i++;
			}
			MemHandleUnlock(record);
			categories[i++]	= cateUnfiled;
			categories[i++]	= cateEdit;
			cateNum			= i;
			
			start++;
		}

		record = DmQueryRecord(ref, start);
		recordP = MemHandleLock(record);
		memBookInfo.numItems = *(Int16*)recordP;
		len = sizeof(BookItemType) * memBookInfo.numItems;
		memBookInfo.itemsP = MemPtrNew(len);
		MemSet(memBookInfo.itemsP, len, 0);
		savedP = (MemSavedType*)((UInt8*)recordP + sizeof(Int16));
		for (i = 0; i < memBookInfo.numItems; i++)
		{
			memBookInfo.itemsP[i].dbID			= savedP->dbID;
			memBookInfo.itemsP[i].date			= savedP->date;
			memBookInfo.itemsP[i].size			= savedP->size;
			memBookInfo.itemsP[i].status		= savedP->status;
			memBookInfo.itemsP[i].categoryId	= savedP->categoryId;
			
			savedP = (MemSavedType*)((UInt8*)savedP + sizeof(MemSavedType));
		}
		MemHandleUnlock(record);
		
		start++;
		
		record = DmQueryRecord(ref, start);
		p = MemHandleLock(record);
		tooFar = p + MemHandleSize(record);
		i = 0;
		while (p != tooFar)
		{
			len = StrLen(p) + 1;
			memBookInfo.itemsP[i].titleP = MemPtrNew(len);
			StrCopy(memBookInfo.itemsP[i].titleP, p);
			p += len;
			i++;
		}
		MemHandleUnlock(record);
		
		DmCloseDatabase(ref);
	}
	else
	{
		memBookInfo.numItems = 0;
		memBookInfo.pos = 0;
		memBookInfo.numItems += MemScanBookByType(VBookType, VBookCreator, true);
		if (g_prefs.options & OPSHOWPDOC)
		{
			memBookInfo.numItems += MemScanBookByType(DocType, DocCreator, true);
			memBookInfo.numItems += MemScanBookByType(DocType, HSCreator, true);
		}
		
		if (memBookInfo.numItems)
		{
			size = sizeof(BookItemType) * memBookInfo.numItems;
			memBookInfo.itemsP = (BookItemType*)MemPtrNew(size);
			MemSet(memBookInfo.itemsP, size, 0);

			MemScanBookByType(VBookType, VBookCreator, false);
			if (g_prefs.options & OPSHOWPDOC)
			{
				MemScanBookByType(DocType, DocCreator, false);
				MemScanBookByType(DocType, HSCreator, false);
			}
		}
		
		if (g_prefs.order)
			Sort(&memBookInfo, g_prefs.order);
	}
}

static void ScanAllBooksInVfs()
{
	UInt16			size;
	Char			pathName[256];
	FileRef			ref;
	Err				err;
	Int16			i;
	VfsSavedType	*savedP;
	
	ListInfoClean(&vfsBookInfo);

	TransPath(true);
	
	StrCopy(pathName, g_prefs.path);
	i = StrLen(pathName);
	if (i > 1) // Path != "/"
		pathName[i++] = '/';
	StrCopy(&pathName[i], blCache);
	
	err = VFSFileOpen(l_volRef, pathName, vfsModeRead, &ref);
	if (err == errNone)
	{
		// Read directory list from cached file.
		
		Byte	buf[2048];
		Int16	j, k, n, s, ss, bytes;
		UInt32	bytesRead;
		Char	*p, *q, *tooFar;

		VFSFileRead(ref, sizeof(Int16), &vfsBookInfo.numItems, NULL);
		
		vfsBookInfo.itemsP = MemPtrNew(sizeof(BookItemType) * vfsBookInfo.numItems);
		MemSet(vfsBookInfo.itemsP, sizeof(BookItemType) * vfsBookInfo.numItems, 0);

		s = 2048 / sizeof(VfsSavedType);
		n = vfsBookInfo.numItems / s;
		if (vfsBookInfo.numItems % s) n++;
		i = 0;
		for (j = 0; j < n; j++)
		{
			ss = (j == n - 1) ? vfsBookInfo.numItems % s : s;
			bytes = sizeof(VfsSavedType) * ss;
			VFSFileRead(ref, bytes, buf, NULL);
			
			savedP = (VfsSavedType*)buf;
			for (k = 0; k < ss; k++)
			{
				vfsBookInfo.itemsP[i].date		= savedP[k].date;
				vfsBookInfo.itemsP[i].size		= savedP[k].size;
				vfsBookInfo.itemsP[i].status	= savedP[k].status;
				i++;
			}
		}
		
		i = 0;
		do {
			VFSFileRead(ref, 2048, buf, &bytesRead);
			p = (Char*)buf;
			tooFar = p + bytesRead;

			while (true)
			{
				q = p;
				while (*p && p != tooFar) p++;

				if (p == tooFar)
				{
					VFSFileSeek(ref, vfsOriginCurrent, -(p - q));
					break;
				}
				p++;
				
				if (vfsBookInfo.itemsP[i].titleP == NULL)
				{
					vfsBookInfo.itemsP[i].titleP = MemPtrNew(p - q);
					StrCopy(vfsBookInfo.itemsP[i].titleP, q);
				}
				else
				{
					if (*q == 1)
					{
						vfsBookInfo.itemsP[i].fileP = vfsBookInfo.itemsP[i].titleP;
					}
					else
					{
						vfsBookInfo.itemsP[i].fileP = MemPtrNew(p - q);
						StrCopy(vfsBookInfo.itemsP[i].fileP, q);
					}
					i++;
				}
			}
		} while (bytesRead == 2048);
		
		VFSFileClose(ref);
	}
	else if (err == vfsErrFileNotFound)
	{
		vfsBookInfo.numItems = 0;
		vfsBookInfo.pos = 0;
		vfsBookInfo.numItems += VfsScanBookByType(true, 0, 0, true);
		vfsBookInfo.numItems += VfsScanBookByType(false, VBookType, VBookCreator, true);
		if (g_prefs.options & OPSHOWPDOC)
		{
			vfsBookInfo.numItems += VfsScanBookByType(false, DocType, DocCreator, true);
			vfsBookInfo.numItems += VfsScanBookByType(false, DocType, HSCreator, true);
		}
		if (g_prefs.options & OPSHOWTEXT)
			vfsBookInfo.numItems += VfsScanBookByType(false, TxtType, TxtCreator, true);

		if (vfsBookInfo.numItems)
		{
			size = sizeof(BookItemType) * vfsBookInfo.numItems;
			vfsBookInfo.itemsP = (BookItemType*)MemPtrNew(size);
			ErrFatalDisplayIf(!vfsBookInfo.itemsP, "MemPtrNew");
			MemSet(vfsBookInfo.itemsP, size, 0);

			VfsScanBookByType(true, 0, 0, false);
			VfsScanBookByType(false, VBookType, VBookCreator, false);
			if (g_prefs.options & OPSHOWPDOC)
			{
				VfsScanBookByType(false, DocType, DocCreator, false);
				VfsScanBookByType(false, DocType, HSCreator, false);
			}
			if (g_prefs.options & OPSHOWTEXT)
				VfsScanBookByType(false, TxtType, TxtCreator, false);
		}
		
		if (g_prefs.order)
			Sort(&vfsBookInfo, g_prefs.order);
	}
	else
		DebugInfo("Scan VFS", err);
}

static void ChangeDir(Boolean isCD)
{
	Err		err;
	FileRef	ref;
	Int16	pos, i;

	TransPath(true);
	pos = StrLen(g_prefs.path);

	if (isCD)
	{
		if (pos > 1) g_prefs.path[pos++] = '/';

		StrCopy(
			&g_prefs.path[pos],
			bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP);

		err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &ref);
		if (err)
		{
			// This directory isn't exist, refresh book list.
			if (pos == 1)
				g_prefs.path[pos] = 0;
			else
				g_prefs.path[--pos] = 0;

			BookListRefresh();
		}
		else
			VFSFileClose(ref);
	}
	else // return to parent directory
	{
		if (pos == 1)
			return;

		i = pos - 1;
		while (g_prefs.path[i] != '/')
			i--;
		if (i == 0)
			g_prefs.path[1] = 0;
		else
			g_prefs.path[i] = 0;
	}
	
	Waiting(ScanBookString);
	ScanAllBooksInVfs();
	vfsBookInfo.volRef = l_volRef;
//	DebugInfo("CD", 1);
	BooksSetFilter();
	BooksDrawScreen();
	CurrentPlace();
}

static void BooksSetFilter()
{
	Int16	i;
	UInt8	categoryId;
	
	bookList.top = -1;
	bookList.bottom = -1;
	bookList.pos = -1;
	
	if (bookList.listP->numItems == 0)
	{
		bookList.filteredNum = 0;
		return;
	}

	if (bookList.filteredMax < bookList.listP->numItems)
	{
		if (bookList.filteredMax)
			MemPtrFree(bookList.filtered);
		bookList.filteredMax = bookList.listP->numItems;
		bookList.filtered = (Int16*)MemPtrNew(sizeof(Int16) * bookList.filteredMax);
	}
	
	if (!bookInMem || cateSelection == 0)
	{
		// When book saved in expansion card or category is "All".
		for (i=0; i < bookList.listP->numItems; i++)
			bookList.filtered[i] = i;
		bookList.filteredNum = i;
	}
	else
	{
		categoryId = (cateSelection == cateNum - 2) ? dmUnfiledCategory : cateSelection;

		bookList.filteredNum = 0;
		for (i=0; i < bookList.listP->numItems; i++)
		{
			if (bookList.listP->itemsP[i].categoryId == categoryId)
				bookList.filtered[bookList.filteredNum++] = i;
		}
	}
}

static void SizeToString(Char* strP, UInt32 size)
{
	const Int32	mega = 1048576;
	const Int32	kilo = 1024;
	Int32		a, b;
	
	a = size / mega;
	if (a > 0)
	{
		if (a >= 10)
			StrPrintF(strP, "%ldM", a);
		else
		{
			b = size % mega * 10 / mega;
			StrPrintF(strP, "%ld.%ldM", a, b);
		}
		
		return;
	}
	
	a = size / kilo;
	if (a > 0)
	{
		if (a > 10)
			StrPrintF(strP, "%ldK", a);
		else
		{
			b = size % kilo * 10 / kilo;
			StrPrintF(strP, "%ld.%ldK", a, b);
		}
		
		return;
	}
	
	StrPrintF(strP, "%ldB", size);
	
	return;
}


/*
static void BooksHighlightRow()
{
	RectangleType rect;

	if (bookList.pos == -1 ||
		bookList.pos < bookList.top ||
		bookList.pos > bookList.bottom)
		return;
	
	rect.topLeft.x = bookList.bounds.topLeft.x + bookList.col1 - 1;
	rect.topLeft.y = bookList.bounds.topLeft.y +
		(bookList.pos - bookList.top) * (bookList.height + ITEM_SPACE);
	rect.extent.x = bookList.bounds.extent.x - bookList.col1;
	if (g_prefs.options & OPSHOWSIZE)
		rect.extent.x -= bookList.col3;
	rect.extent.y = bookList.height;

	#ifdef PALMOS_30
	WinInvertRectangle(&rect, 0);
	#else

	WinPushDrawState();

//	if (g_prefs.depth > 1)
//	{
//		WinSetForeColor(g_prefs.textColor);
//		WinSetBackColor(g_prefs.backColor);
//	}

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50
	
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedForeground));
	WinSetBackColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
	
	WinSetDrawMode(winSwap);
	LCDWinPaintRectangle(&rect, 0);
	WinPopDrawState();

	#endif
}
*/

static void BooksSetScrollBar()
{
	FormType*		frmP;
	ScrollBarType*	barP;
	
	frmP = FrmGetActiveForm();
	barP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainVsbarScrollBar));

	if (bookList.filteredNum <= bookList.lines)
		SclSetScrollBar(barP, 0, 0, 0, 0);
	else
		SclSetScrollBar(barP,
			bookList.top, 0, bookList.filteredNum - bookList.lines, bookList.lines-1);
	
	#ifdef USE_SILKSCREEN
	SclDrawScrollBar(barP);
	#endif
}


static void BooksDrawRow(
	Int16 item, Coord x, Coord y,
	IndexedColorType foreColor, IndexedColorType backColor)
{
	Int16			len, width;
	Char			text[10];
	RectangleType	rect;
	
	WinSetTextColor(foreColor);
	WinSetBackColor(backColor);

	rect.topLeft.x = x;
	rect.topLeft.y = y;
	rect.extent.x = bookList.bounds.extent.x;
	rect.extent.y = bookList.height;
	LCDWinEraseRectangle(&rect, 0);

	// Draw check tag.
	LCDFntSetFont(symbol11Font);
	if (bookList.listP->itemsP[item].status & ITEM_CHECK)
		LCDWinDrawChars("\1", 1, x, y);
	else
		LCDWinDrawChars("\0", 1, x, y);
	LCDFntSetFont(CfnGetFont());
	
	len = StrLen(bookList.listP->itemsP[item].titleP);
	if (bookList.listP->itemsP[item].status & ITEM_DIRECTORY)
	{
		CfnDrawTruncChars(
			bookList.listP->itemsP[item].titleP,
			len,
			x + bookList.col1,
			y,
			bookList.bounds.extent.x - bookList.col1,
			foreColor, backColor);

		CfnDrawChars(
			"/", 1,
			x + bookList.col1 + CfnCharsWidth(bookList.listP->itemsP[item].titleP, len),
			y,
			foreColor, backColor);
	}
	else
	{
		width = bookList.bounds.extent.x - bookList.col1;
		if (g_prefs.options & OPSHOWSIZE)
			width -= bookList.col3;

		CfnDrawTruncChars(
			bookList.listP->itemsP[item].titleP,
			len,
			x + bookList.col1,
			y,
			width,
			foreColor, backColor);
		
		if (g_prefs.options & OPSHOWSIZE)
		{
			SizeToString(text, bookList.listP->itemsP[item].size);
			len = StrLen(text);
			width = CfnCharsWidth(text, len);
			LCDWinDrawChars(
				text, len,
				bookList.bounds.extent.x - width,
				y);
		}
	}
}


static void BooksDrawScreen()
{
	Int16				i, j, y;
	IndexedColorType	foreColor, backColor;

//	#ifdef PALMOS_30
//	FontID	oldFont;
//	#endif

	// 清屏

//	#ifndef PALMOS_30
	WinPushDrawState();

//	if (g_prefs.depth > 1)
//		WinSetBackColor(g_prefs.backColor);

	#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50

//	#endif

	WinSetBackColor(g_prefs.backColor);
	LCDWinEraseRectangle(&bookList.bounds, 0);

//	#ifndef PALMOS_30
	WinPopDrawState();
//	#endif
	
	if (bookList.filteredNum == 0)
		return;

//	#ifdef PALMOS_30

//	oldFont = LCDFntSetFont(CfnGetFont());

//	#else

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

/*	if (g_prefs.depth > 1)
	{
		WinSetTextColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}*/
	
	#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50

//	#endif // PALMOS_30
	
	if (bookList.top == -1)
		bookList.top = 0;

	y = bookList.bounds.topLeft.y;
	for (i = 0; i < bookList.lines &&
		bookList.top + i < bookList.filteredNum; i++)
	{
		j = bookList.top + i;
		
		if (bookList.pos == j) {
			foreColor = g_prefs.backColor;
			backColor = g_prefs.textColor;
		}
		else {
			foreColor = g_prefs.textColor;
			backColor = g_prefs.backColor;
		}
		
		BooksDrawRow(
			bookList.filtered[j],
			bookList.bounds.topLeft.x, y,
			foreColor, backColor);

		bookList.bottom = j;
		y += bookList.height + ITEM_SPACE;
	}

//	#ifdef PALMOS_30
//	LCDFntSetFont(oldFont);
//	#else

	WinPopDrawState();

//	#endif

//	BooksHighlightRow();
	BooksSetScrollBar();
}


static void BooksHighlightRow(Boolean highlight)
{
	Coord				y;
	IndexedColorType	foreColor, backColor;

	if (bookList.pos == -1 ||
		bookList.pos < bookList.top ||
		bookList.pos > bookList.bottom)
		return;
	
	y = bookList.bounds.topLeft.y +
		(bookList.pos - bookList.top) * (bookList.height + ITEM_SPACE);
	
	if (highlight)
	{
		foreColor = g_prefs.backColor;
		backColor = g_prefs.textColor;
	}
	else
	{
		foreColor = g_prefs.textColor;
		backColor = g_prefs.backColor;
	}
	
	WinPushDrawState();
	
	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif
	
	BooksDrawRow(
		bookList.filtered[bookList.pos],
		bookList.bounds.topLeft.x, y,
		foreColor, backColor);
	
	WinPopDrawState();
}


static void BooksPageUp()
{
	if (bookList.filteredNum <= bookList.lines ||
		bookList.top == 0)
		return;
	
	bookList.top -= bookList.lines - 1;
	if (bookList.top < 0)
		bookList.top = 0;

	BooksDrawScreen();
}

static void BooksPageDown()
{
	if (bookList.filteredNum <= bookList.lines ||
		bookList.bottom == bookList.filteredNum-1)
		return;

	if (bookList.bottom + bookList.lines - 1 < bookList.filteredNum)
		bookList.top = bookList.bottom;
	else
		bookList.top = bookList.filteredNum - bookList.lines;

	BooksDrawScreen();
}

static void BooksLineUp()
{
	RectangleType	vacated;
	
	if (bookList.filteredNum == 0 ||
		bookList.pos == 0)
		return;

	if (bookList.pos == -1 ||
		bookList.pos < bookList.top||
		bookList.pos > bookList.bottom)
	{
		bookList.pos = bookList.bottom;
		BooksHighlightRow(true);
		return;
	}

	BooksHighlightRow(false);
	bookList.pos--;

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	if (bookList.pos < bookList.top)
	{
		// Scroll list down.
		bookList.top--;
		bookList.bottom--;
		LCDWinScrollRectangle(
			&bookList.bounds, winDown,
			bookList.height + ITEM_SPACE, &vacated);
	}

	WinPopDrawState();

	BooksHighlightRow(true);
	BooksSetScrollBar();
}

static void BooksLineDown()
{
	RectangleType	vacated;

	if (bookList.filteredNum == 0 ||
		bookList.pos == bookList.filteredNum - 1)
		return;

	if (bookList.pos == -1 ||
		bookList.pos < bookList.top ||
		bookList.pos > bookList.bottom)
	{
		bookList.pos = bookList.top;
		BooksHighlightRow(true);
		return;
	}

	BooksHighlightRow(false);
	bookList.pos++;

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	if (bookList.pos > bookList.bottom)
	{
		// Scroll list up.
		bookList.top++;
		bookList.bottom++;
		LCDWinScrollRectangle(
			&bookList.bounds, winUp,
			bookList.height + ITEM_SPACE, &vacated);
	}

	WinPopDrawState();

	BooksHighlightRow(true);
	BooksSetScrollBar();
}

static Int16 GetSelectedNum()
{
	Int16	i;
	Int16	count;
	
	for (i=0, count=0; i < bookList.filteredNum; i++)
	{
		if (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_CHECK)
			count++;
	}
	
	return count;
}

static void Rename()
{
	MemHandle		txtH, oldTxtH;
	FormPtr			frmP;
	FieldPtr		fieldP;
	Char*			p;
	Boolean			bText, update = false;
	FileRef			refFile;
	Err				err;
	Int16			n;
	Char			name[64];
	Char*			fullpathP = NULL;
	
	if (!bookInMem)
	{
		// Does not rename directory.
		if (bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP == NULL)
			return;

		// Get file full path.
		fullpathP = MemPtrNew(128);
		TransPath(true);
		StrCopy(fullpathP, g_prefs.path);
		n = StrLen(fullpathP);
		if (n == 1)
			n = 0;
		else
			fullpathP[n] = '/';
		StrCopy(
			&fullpathP[n + 1],
			bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP);
	}
	
	if (HasCache())
	{
		if (bookInMem)
		{
			if (!DmFindDatabase(
				bookList.listP->itemsP[bookList.filtered[bookList.pos]].cardNo,
				bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP))
			{
				BookListRefresh();
				return;
			}
		}
		else
		{
			if (VFSFileOpen(
					l_volRef, fullpathP,
					vfsModeReadWrite, &refFile) != errNone)
			{
				MemPtrFree(fullpathP);
				BookListRefresh();
				return;
			}
		}
	}
	else
	{
		if (!bookInMem)
			VFSFileOpen(
				l_volRef, fullpathP,
				vfsModeReadWrite, &refFile);
	}
	
	if (bookInMem)
		bText = false;
	else
	{
		n = StrLen(bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP) - 4;

		StrToLower(
			name,
			&bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP[n]);

		bText = (!StrCompare(name, ".txt")) ? true : false;

		if (bText || g_prefs.options & OPSHOWFNAME)
		{
			VFSFileClose(refFile);
			refFile = 0;
		}
	}
	
	txtH = MemHandleNew(dmDBNameLength);
	p = MemHandleLock(txtH);

	if (!bookInMem && (bText || g_prefs.options & OPSHOWFNAME))
	{
		if (n >= dmDBNameLength)
			n = dmDBNameLength - 1;
		
		// When destination file is a text file, copy filename without ext-name.
		StrNCopy(p, bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP, n);
		p[n] = 0;
	}
	else
		StrCopy(p, bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP);

	MemHandleUnlock(txtH);
	
	frmP = FrmInitForm(RenameForm);
	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, RenameNameField));
	FldSetMaxChars(fieldP, dmDBNameLength - 1);
	
	oldTxtH = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, txtH);
	FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
	if (oldTxtH)
		MemHandleFree(oldTxtH);
	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, RenameNameField));
	
	if (FrmDoDialog(frmP) == RenameRenameButton)
	{
		p = FldGetTextPtr(fieldP);
		if (p)
		{
			if (bookInMem)
			{
				err = DmSetDatabaseInfo(
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].cardNo,
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].dbID,
					p, NULL, NULL,
					NULL, NULL, NULL,
					NULL, NULL, NULL, NULL, NULL);
			}
			else
			{
				if (bText || g_prefs.options & OPSHOWFNAME)
				{
					// Add new name with ext-name "txt".
					StrCopy(name, p);
					n = StrLen(name);
					StrCopy(&name[n], ((bText) ? ".txt" : ".pdb"));
					err = VFSFileRename(l_volRef, fullpathP, name);
				}
				else
				{
					StrCopy(name, p);
					n = StrLen(name) + 1;
					err = VFSFileWrite(refFile, n, name, NULL);
				}
			}
			
			if (err == errNone)
			{
				MemPtrFree(bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP);
				if (bText || g_prefs.options & OPSHOWFNAME)
				{
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP =
						MemPtrNew(StrLen(name) + 1);
					StrCopy(
						bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP,
						name);
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP =
						bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP;
				}
				else
				{
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP =
						(Char*)MemPtrNew(FldGetTextLength(fieldP) + 1);
					StrCopy(
						bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP,
						p);
				}
				update = true;
			}
			else
				FrmCustomAlert(OpErrAlert, "Invalid name or file name already used.", "", "");
		}
	}
	FrmDeleteForm(frmP);
	
	if (!bookInMem)
	{
		MemPtrFree(fullpathP);
		if (!bText && !(g_prefs.options & OPSHOWFNAME))
			VFSFileClose(refFile);
	}

	if (update)
	{
		DeleteCache();
		BooksHighlightRow(true);
	}
}

static void ScanBook()
{
	EventType	event;
	
	DeleteCache();
	event.eType = listRefreshEvent;
	EvtAddEventToQueue(&event);
}

static void SetBackup(Boolean backup)
{
	Int16	i;
	UInt16	attributes;
	Boolean	cache;
	
	if (GetSelectedNum() == 0)
	{
		FrmAlert(InvalidSelAlert);
		return;
	}

	cache = HasCache();
	DeleteCache();
	
	for (i=0; i < bookList.filteredNum; i++)
	{
		if (!(bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_CHECK))
			continue;

		if (!cache ||
			DmFindDatabase(
				bookList.listP->itemsP[bookList.filtered[i]].cardNo,
				bookList.listP->itemsP[bookList.filtered[i]].titleP)
			)
		{
			DmDatabaseInfo(
				bookList.listP->itemsP[bookList.filtered[i]].cardNo,
				bookList.listP->itemsP[bookList.filtered[i]].dbID,
				NULL, &attributes, NULL,
				NULL, NULL, NULL, NULL,
				NULL, NULL,
				NULL, NULL);
			
			attributes |= dmHdrAttrBackup;
			if (!backup)
				attributes ^= dmHdrAttrBackup;

			DmSetDatabaseInfo(
				bookList.listP->itemsP[bookList.filtered[i]].cardNo,
				bookList.listP->itemsP[bookList.filtered[i]].dbID,
				NULL, &attributes, NULL,
				NULL, NULL, NULL, NULL,
				NULL, NULL,
				NULL, NULL);
		}
	}
	
	SelectAll(false);
	BooksDrawScreen();
}

static void Details()
{
	FormPtr			frmP;
	FieldPtr		fieldP;
	MemHandle		txtH, oldTxtH;
	Char			*p;
	DateTimeType	date;
	Int16			i;
	
	if (bookList.listP->itemsP[bookList.filtered[bookList.pos]].status & ITEM_DIRECTORY)
		return;

	frmP = FrmInitForm(DetailForm);
	
	// Name
	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DetailNameField));
	txtH = MemHandleNew(32);
	p = MemHandleLock(txtH);
	StrCopy(p, bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP);
	MemHandleUnlock(txtH);
	oldTxtH = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);
	
	// Date
	p = MemPtrNew(longDateStrLength + timeStringLength + 2);
	TimSecondsToDateTime(
		bookList.listP->itemsP[bookList.filtered[bookList.pos]].date,
		&date);
	DateToAscii(date.month, date.day, date.year, dfYMDLongWithSpace, p);

	i = StrLen(p);
	p[i++] = ' ';
	TimeToAscii(date.hour, date.minute, tfColonAMPM, &p[i]);
	FrmCopyLabel(frmP, DetailDateLabel, p);
	MemPtrFree(p);
	
	// Size
	p = MemPtrNew(12);
	StrPrintF(p, "%ld b", bookList.listP->itemsP[bookList.filtered[bookList.pos]].size);
	FrmCopyLabel(frmP, DetailSizeLabel, p);
	MemPtrFree(p);

	FrmDoDialog(frmP);
	FrmDeleteForm(frmP);
}

static void Delete()
{
	Int16	i, pos;
	Char*	path;
	Boolean	isBook, cache;
	
	if (GetSelectedNum() == 0)
	{
		FrmAlert(InvalidSelAlert);
		return;
	}
	else if (FrmAlert(DelBookAlert) == DelBookCancel)
		return;
	
	Waiting(DeletingString);

	cache = HasCache();
	DeleteCache();

	if (!bookInMem)
	{
		path = MemPtrNew(256);
		StrCopy(path, g_prefs.path);
		pos = StrLen(path);
		if (pos > 1)
		{
			path[pos] = '/';
			pos++;
		}
	}
	
	for (i = 0; i < bookList.filteredNum; i++)
	{
		if (!(bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_CHECK))
			continue;
		
		isBook = (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_VBOOK) ?
			true : false;
		
		if (bookInMem)
		{
			PrefDelete(bookList.listP->itemsP[bookList.filtered[i]].titleP, isBook);
			if (!cache || 
				DmFindDatabase(
					bookList.listP->itemsP[bookList.filtered[i]].cardNo,
					bookList.listP->itemsP[bookList.filtered[i]].titleP)
				)
			{
				DmDeleteDatabase(
					bookList.listP->itemsP[bookList.filtered[i]].cardNo,
					bookList.listP->itemsP[bookList.filtered[i]].dbID);
			}
		}
		else
		{
			if (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_DIRECTORY)
			{
				StrCopy(&path[pos], bookList.listP->itemsP[bookList.filtered[i]].titleP);
			}
			else
			{
				StrCopy(&path[pos], bookList.listP->itemsP[bookList.filtered[i]].fileP);
				PrefDelete(path, isBook);
			}
			VFSFileDelete(l_volRef, path);
		}
	}

	if (bookInMem)
		ScanAllBooksInMem();
	else
	{
		MemPtrFree(path);
		ScanAllBooksInVfs();
	}
	
	BooksSetFilter();
	BooksDrawScreen();
	CurrentPlace();
}

static void Category()
{
	FormPtr	frmP;
	ListPtr	listP;
	Int16	sel, i;
	
	if (GetSelectedNum() == 0)
	{
		FrmAlert(InvalidSelAlert);
		return;
	}

	frmP = FrmGetActiveForm();
	listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainClassList));

	sel = LstGetSelection(listP);
	if (sel == 0 || sel == cateNum - 1) // All or Edit
		sel = cateNum - 2; // Unfiled
	sel--;

	LstSetListChoices(listP, &categories[1], cateNum - 1);
	LstSetHeight(listP, cateNum - 1);
	LstSetSelection(listP, sel);
	
	sel = LstPopupList(listP);
	if (sel == cateNum - 2)
	{
		FrmGotoForm(CategoriesForm);
	}
	else if (sel != noListSelection)
	{
		DeleteCache();

		sel = (sel + 1 == cateNum - 2) ? dmUnfiledCategory : sel + 1;
		for (i = 0; i < bookList.filteredNum; i++)
		{
			if (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_CHECK)
			{
				bookList.listP->itemsP[bookList.filtered[i]].categoryId = sel;
				if (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_DOC)
					PrefMemDocSetCategory(
						bookList.listP->itemsP[bookList.filtered[i]].titleP,
						((sel == cateNum - 2) ? "" : categories[sel]));
			}
		}
		
		if (sel == dmUnfiledCategory) // Unfiled
			BooksChangeCategory(sel, "");
		else
			BooksChangeCategory(sel, categories[sel]);
	}
	
	LstSetListChoices(listP, categories, cateNum);
	LstSetHeight(listP, cateNum);
	LstSetSelection(listP, cateSelection);

	SelectAll(false);
	BooksSetFilter();
	BooksDrawScreen();
}

static void ResetOperation()
{
	FormPtr		frmP;
	ListPtr		lstP;
	ControlPtr	ctlP;
	
	frmP = FrmGetActiveForm();

	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainChoicesPopTrigger));
	CtlSetLabel(ctlP, oper.items[0]);
	CtlDrawControl(ctlP);
	
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainOperationList));
	LstSetSelection(lstP, 0);
}

static void OpenBook()
{
	VBOOKINFO		*infoP;
	PALMDOCINFO		*docInfoP;
	BookItemType	*itemP;
	MemHandle		record;
	UInt16			chapter, num;
	UInt32			offset, c;
	
	if (resume)
		itemP = &quickBook;
	else
		itemP = &bookList.listP->itemsP[bookList.filtered[bookList.pos]];

	if (itemP->status & ITEM_DIRECTORY)
	{
		ChangeDir(true);
	}
	else
	{
		ContentsRefresh();
		ViewInit();
		ViewHistoryInit();

		// Open selected book.
		currentBook.type = (bookInMem) ? BOOK_TYPE_INMEM : 0;

		if (!OpenDatabase())
		{
			BookListRefresh();
			return;
		}
		
		if (!(itemP->status & ITEM_TEXT))
			record = QueryRecord(0);
		
		/////////////////////////
		// OPEN BOOK OF VBOOK OWN
		if (itemP->status & ITEM_VBOOK)
		{
			currentBook.type |= BOOK_TYPE_VBOOK;

			infoP = MemHandleLock(record);
			if ((infoP->nType & COMPRESS_ZLIB) && !g_haszlib)
			{
				MemHandleUnlock(record);
				if (!(currentBook.type & BOOK_TYPE_INMEM))
					MemHandleFree(record);
				CloseDatabase();
				FrmAlert(MissZLibAlert);
				return;
			}
		
			// Get total of chpater.
			currentBook.indexRecNo = infoP->nChapterIndexRecNo + infoP->nChapterIndexNum - 1;
			record = QueryRecord(currentBook.indexRecNo);
			currentBook.chapterNum = MemHandleSize(record) / sizeof(VBOOKCHAPTERINDEX);
			currentBook.chapterNum += MAXINDEXENTRYPERRECORD * (infoP->nChapterIndexNum - 1);
			currentBook.chapterIndexP = MemHandleLock(record);

			currentBook.infoP = infoP;

			if (bookInMem)
			{
				PrefGetLastPos(itemP->titleP, &chapter, &offset);
			}
			else
			{
				if (resume)
					PrefGetLastPos(itemP->fileP, &chapter, &offset);
				else
					PrefGetLastPos(g_prefs.path, &chapter, &offset);
			}

			ViewSetOffset(chapter, offset);
			ViewEnableLink((infoP->nType & LINK_ENABLED) ? true : false);

			if (infoP->nTitleIndexRecNo && chapter == 0 && offset == 0)
			{
				ContentsSetReturnForm(MainForm);
				FrmGotoForm(ContentsForm);
			}
			else
			{
				FrmGotoForm(ViewForm);
			}
		}
		//////////////////////////////
		// OPEN PALM STANDARD DOCUMENT
		else if (itemP->status & ITEM_DOC)
		{
			num = NumRecords();
						
			currentBook.type |= BOOK_TYPE_DOC;
			docInfoP = MemHandleLock(record);

			if (docInfoP->nBlocksNum == 0 ||
				num <= 1 ||
				(docInfoP->nBlocksNum > num - 1 &&
				!(currentBook.type & BOOK_TYPE_INMEM)))
			{
				MemHandleUnlock(record);
				if (!(currentBook.type & BOOK_TYPE_INMEM))
					MemHandleFree(record);
				CloseDatabase();
				FrmAlert(InvalidDocAlert);
				return;
			}
			
			currentBook.chapterNum = 1;
			
			if (docInfoP->nBlocksNum > num - 1)
				num--;
			else
				num = docInfoP->nBlocksNum;

			currentBook.chapterIndex = num;

			currentBook.length = (UInt32)docInfoP->nSize * (num - 1);
			record = QueryRecord(num);
			if (docInfoP->nVersion == 2)
			{
				currentBook.length += TextDecodeLen(
					MemHandleLock(record),
					MemHandleSize(record));

				MemHandleUnlock(record);
			}
			else
				currentBook.length += MemHandleSize(record);
			
			if (!(currentBook.type & BOOK_TYPE_INMEM))
				MemHandleFree(record);

			currentBook.titleIndexH	= NULL;
			currentBook.titleDataH	= NULL;
			currentBook.infoP		= docInfoP;

			if (bookInMem)
				PrefGetLastPos(itemP->titleP, &chapter, &offset);
			else
			{
				if (resume)
					PrefGetLastPos(itemP->fileP, &chapter, &offset);
				else
					PrefGetLastPos(g_prefs.path, &chapter, &offset);
			}

			ViewSetOffset(0, offset);
			ViewEnableLink(false);
			
			FrmGotoForm(ViewForm);
		}
		////////////////////////
		// OPEN PC TEXT DOCUMENT
		else
		{
			currentBook.type |= BOOK_TYPE_TEXT;
			
			VFSFileSize(currentBook.vfsRef, &c);
			num = c / 4096;
			if (c % 4096) num++;
			
			currentBook.chapterNum		= 1;
			currentBook.chapterIndex	= num;
			currentBook.length			= c;
			
			if (resume)
				PrefGetLastPos(itemP->fileP, &chapter, &offset);
			else
				PrefGetLastPos(g_prefs.path, &chapter, &offset);

			ViewSetOffset(0, offset);
			ViewEnableLink(false);
			
			FrmGotoForm(ViewForm);
		}
	}
}

static void CloseBook()
{
	UInt16	chapter;
	UInt32	offset;

	BookReleaseContents();
	BookCloseChapter();

	if (currentBook.infoP)
	{
		MemPtrUnlock(currentBook.infoP);
		
		GetLastPosition(&chapter, &offset);
		if (currentBook.type & BOOK_TYPE_INMEM)
		{
			if (quickBook.titleP)
				PrefSaveLastPos(quickBook.titleP, chapter, offset);
			else
				PrefSaveLastPos(
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP,
					chapter, offset);
		}
		else
		{
			PrefSaveLastPos(BookGetPath(), chapter, offset);
			// When book save in vfs, we save reading info in VBook's database.
			MemPtrFree(currentBook.infoP);
		}

		if (currentBook.type & BOOK_TYPE_VBOOK)
			ViewSaveHistory();
			
		TransPath(true);
	}
	else if (currentBook.chapterIndex) // Pure text.
	{
		GetLastPosition(&chapter, &offset);
		PrefSaveLastPos(BookGetPath(), chapter, offset);
		TransPath(true);
	}
	
	CloseDatabase();
	MemSet(&currentBook, sizeof(BookType), 0);
}

static void BooksSelect(Int16 item, Boolean tapped)
{
	FormPtr		frmP;
	ListPtr		lstP;
	EventType	event;
	Int16		sel;

	BooksHighlightRow(false);
	bookList.pos = item;
	BooksHighlightRow(true);
	
	if (tapped)
	{
		do {
			EvtGetEvent(&event, SysTicksPerSecond() / 2);
		} while (event.eType != penUpEvent && event.eType != nilEvent);
	}

	frmP = FrmGetActiveForm();
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainOperationList));
	sel = LstGetSelection(lstP);

	if (sel == 0)
	{
		OpenBook();
	}
	else if (sel == 1)
	{
		Rename();
	}
	else
	{
		Details();
	}
}

static void BooksTapped(Coord x, Coord y)
{
	RectangleType	rect;
	Int16			i;
//	#ifdef PALMOS_30
//	FontID			oldFont;
//	#endif
	
	if (bookList.filteredNum == 0)
		return;

	rect.topLeft.x	= bookList.bounds.topLeft.x;
	rect.topLeft.y	= bookList.bounds.topLeft.y;
	rect.extent.x	= bookList.bounds.extent.x;
	rect.extent.y	= bookList.height;
	
	for (i = bookList.top; i <= bookList.bottom; i++)
	{
		if(RctPtInRectangle(x, y, &rect))
		{
			if (x < rect.topLeft.x + bookList.col1)
			{
//				#ifdef PALMOS_30
//				oldFont = LCDFntSetFont(symbol11Font);
//				#else

				WinPushDrawState();

				if (i == bookList.pos)
				{
					WinSetTextColor(UIColorGetTableEntryIndex(UIObjectSelectedForeground));
					WinSetBackColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
				}
				else
				{
					WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
					WinSetBackColor(UIColorGetTableEntryIndex(UIFormFill));
				}

				#ifdef PALMOS_50
				WinSetCoordinateSystem(kCoordinatesNative);
				#endif

				LCDFntSetFont(symbol11Font);

//				#endif

				bookList.listP->itemsP[bookList.filtered[i]].status ^= ITEM_CHECK;
				if (bookList.listP->itemsP[bookList.filtered[i]].status & ITEM_CHECK)
					LCDWinDrawChars("\1", 1, rect.topLeft.x, rect.topLeft.y);
				else
					LCDWinDrawChars("\0", 1, rect.topLeft.x, rect.topLeft.y);

//				#ifdef PALMOS_30
//				LCDFntSetFont(oldFont);
//				#else
				WinPopDrawState();
//				#endif
			}
			else
				BooksSelect(i, true);
			break;
		}

		rect.topLeft.y += bookList.height + ITEM_SPACE;
	}
}

// Direct Insert sort.
static void Sort(ListInfoType* listInfoP, UInt8 method)
{
	Int16			i, j, k;
	UInt8			*p, *q;
	BookItemType	item;
	
	if (listInfoP->numItems < 2)
		return;
	
	for (i=1; i < listInfoP->numItems; i++)
	{
		p = (UInt8*)&listInfoP->itemsP[i];
		q = (UInt8*)&item;
		for (k=0; k < sizeof(BookItemType); k++)
			*q++ = *p++;
		
		j = i-1;
		while (j >= 0)
		{
			if (method == 1) // sort on title
			{
				if (StrCompare(item.titleP, listInfoP->itemsP[j].titleP) >= 0)
					break;
			}
			else if (method == 2) // sort on date
			{
				if (item.date < listInfoP->itemsP[j].date)
					break;
			}
			else // sort on size
			{
				if (item.size >= listInfoP->itemsP[j].size)
					break;
			}

			p = (UInt8*)&listInfoP->itemsP[j];
			q = (UInt8*)&listInfoP->itemsP[j+1];
			for (k=0; k < sizeof(BookItemType); k++)
				*q++ = *p++;

			j--;
		}
		p = (UInt8*)&item;
		q = (UInt8*)&listInfoP->itemsP[j+1];
		for (k=0; k < sizeof(BookItemType); k++)
			*q++ = *p++;
	}
}

static void SelectAll(Boolean select)
{
	Int16	i;
	
	if (bookList.filteredNum == 0)
		return;
	
	for (i=0; i < bookList.filteredNum; i++)
	{
		bookList.listP->itemsP[bookList.filtered[i]].status |= ITEM_CHECK;
		if (!select)
			bookList.listP->itemsP[bookList.filtered[i]].status ^= ITEM_CHECK;
	}
}

static void SyncBookInfo()
{
	if (FrmAlert(SyncAlert) == SyncSynchronize)
	{
		Waiting(SyncBookString);
		PrefSync((bookInMem) ? 0 : l_volRef);
		BooksDrawScreen();
	}
}

static Boolean ListOptions()
{
	FormPtr		frmP;
	ControlPtr	ctlP;
	ListPtr		listP;
	Boolean		result = false;
	
	frmP = FrmInitForm(ListoptForm);

	// Set order method.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptSortPopTrigger));
	listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptSortList));
	CtlSetLabel(ctlP, LstGetSelectionText(listP, g_prefs.order));
	LstSetSelection(listP, g_prefs.order);
	
	// Set list font.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptFontPopTrigger));
	listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptFontList));
	if (g_prefs.listFont == 0xFFFF)
	{
		CtlSetLabel(ctlP, LstGetSelectionText(listP, 0));
		LstSetSelection(listP, 0);
	}
	else
	{
		CtlSetLabel(ctlP, LstGetSelectionText(listP, g_prefs.listFont + 1));
		LstSetSelection(listP, g_prefs.listFont + 1);
	}
	
	// Set show size.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptSizeCheckbox));
	CtlSetValue(ctlP, g_prefs.options & OPSHOWSIZE);
	
	// Set show file name.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptFnameCheckbox));
	CtlSetValue(ctlP, g_prefs.options & OPSHOWFNAME);
	
	// Support Palm Doc.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptPdocCheckbox));
	CtlSetValue(ctlP, g_prefs.options & OPSHOWPDOC);
	
	// Support Pure text.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptPtxtCheckbox));
	CtlSetValue(ctlP, g_prefs.options & OPSHOWTEXT);

	if (FrmDoDialog(frmP) == ListoptOkButton)
	{
		result = true;
		
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptSortList));
		if (LstGetSelection(listP) != noListSelection)
			g_prefs.order = LstGetSelection(listP);
		
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptFontList));
		if (LstGetSelection(listP) != noListSelection)
		{
			if (LstGetSelection(listP) == 0)
				g_prefs.listFont = 0xFFFF;
			else
				g_prefs.listFont = LstGetSelection(listP) - 1;
		}
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptSizeCheckbox));
		g_prefs.options |= OPSHOWSIZE;
		if (!CtlGetValue(ctlP))
			g_prefs.options ^= OPSHOWSIZE;

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptFnameCheckbox));
		g_prefs.options |= OPSHOWFNAME;
		if (!CtlGetValue(ctlP))
			g_prefs.options ^= OPSHOWFNAME;

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptPdocCheckbox));
		g_prefs.options |= OPSHOWPDOC;
		if (!CtlGetValue(ctlP))
			g_prefs.options ^= OPSHOWPDOC;

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ListoptPtxtCheckbox));
		g_prefs.options |= OPSHOWTEXT;
		if (!CtlGetValue(ctlP))
			g_prefs.options ^= OPSHOWTEXT;
	}

	FrmDeleteForm(frmP);
	
	return result;
}

static void CategoriesInit(Boolean first)
{
	FormPtr		frmP;
	ListPtr		listP;
	ControlPtr	ctlP;
	UInt8		i;
	
	if (first)
	{
		for (i = 0; i < cateNum - 1; i++)
			if (!StrCompare(categories[i], g_prefs.category)) break;
		cateSelection = (i < cateNum - 1) ? i : 0;
	}
	else
	{
		frmP = FrmGetActiveForm();
		
		if (cateSelection >= cateNum - 1)
			cateSelection = 0;

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainCategoryPopTrigger));
		CtlSetLabel(ctlP, categories[cateSelection]);
		
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainClassList));
		LstSetListChoices(listP, categories, cateNum);
		LstSetHeight(listP, cateNum);
		LstSetSelection(listP, cateSelection);
	}
}

static void SwitchToHandheld()
{
	if (!bookInMem)
	{
		bookInMem = true;
		BookFormUpdateControl();
		Waiting(ScanBookString);

		if (memBookInfo.numItems == 0)
		{
			ScanAllBooksInMem();
			CategoriesInit(false);
		}
		bookList.listP = &memBookInfo;
		BooksSetFilter();
		BooksDrawScreen();
		CurrentPlace();
	}
}

static void SwitchToCard()
{
	if (!g_expansion || l_volRef == vfsInvalidVolRef)
	{
		FrmAlert(ExpansionAlert);
	}
	else if (bookInMem)
	{
		FormPtr	frmP;
		ListPtr	lstP;
		Int16	sel;
		
		if (l_vols.volNum > 1)
		{
			frmP = FrmGetActiveForm();
			lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainVolsList));
			
			LstSetListChoices(lstP, l_vols.volLabs, l_vols.volNum);
			
			for (sel = 0; sel < l_vols.volNum; sel++)
				if (l_volRef == l_vols.volRefs[sel]) break;
			
			LstSetSelection(lstP, sel);

			sel = LstPopupList(lstP);
			if (sel == noListSelection) return;

			l_volRef = l_vols.volRefs[sel];
		}
		
		bookInMem = false;
		BookFormUpdateControl();
		Waiting(ScanBookString);

		if (vfsBookInfo.numItems == 0 || vfsBookInfo.volRef != l_volRef)
		{
			if (vfsBookInfo.volRef != l_volRef)
			{
				StrCopy(g_prefs.path, "/");
				l_isPath = true;
			}
			
			ScanAllBooksInVfs();
			vfsBookInfo.volRef = l_volRef;
		}

		bookList.listP = &vfsBookInfo;
		BooksSetFilter();
		BooksDrawScreen();
		CurrentPlace();
	}
}

static void BookFormLayout(FormPtr frmP)
{
	RectangleType	rect;
	
	MemMove(&bookList.bounds, &bookList.origBounds, sizeof(RectangleType));

	if (bookList.height == 0)
	{
		FontID	oldFont;
		Int16	chrHeight;
		
		oldFont = LCDFntSetFont(symbol11Font);
		chrHeight = FntCharHeight();
		LCDFntSetFont(oldFont);
		
		oldFont = LCDFntSetFont(CfnGetFont());
//		#ifdef PALMOS_50
		bookList.height = FntCharHeight();
//		#else
//		bookList.height = CfnCharHeight();
//		#endif

		bookList.col3 = CfnCharsWidth("000000", 6);
		LCDFntSetFont(oldFont);
		
		if (bookList.height < chrHeight)
			bookList.height = chrHeight;

		#ifdef PALMOS_50

		#ifdef HIGH_DENSITY
		bookList.height <<= 1;
		bookList.col3 <<= 1;
		#endif // HIG_DENSITY

		if (bookList.height < CfnCharSize())
			bookList.height = CfnCharSize();

		#endif // PALMOS_50
	}

	bookList.lines =
		(bookList.origBounds.extent.y + ITEM_SPACE) /
		(bookList.height + ITEM_SPACE);

	// Adjust book list to new bound.
	bookList.bounds.extent.y =
		bookList.height * bookList.lines + ITEM_SPACE * (bookList.lines - 1);
	rect.topLeft.x = bookList.bounds.topLeft.x;
	rect.topLeft.y = bookList.bounds.topLeft.y;
	rect.extent.x = bookList.bounds.extent.x;
	rect.extent.y = bookList.bounds.extent.y;

	#ifdef HIGH_DENSITY
	rect.topLeft.x >>= 1;
	rect.topLeft.y >>= 1;
	rect.extent.x >>= 1;
	rect.extent.y >>= 1;
	#endif
	
	#ifndef PALMOS_30
	WinPushDrawState();
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif
	#endif

	LCDWinEraseRectangle(&bookList.origBounds, 0);
	FrmSetObjectBounds(frmP, FrmGetObjectIndex(frmP, MainBooksGadget), &rect);

	// Adjust scrollbar's height same with list.
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, MainVsbarScrollBar), &rect);
	#ifdef HIGH_DENSITY
	rect.topLeft.x <<= 1;
	rect.topLeft.y <<= 1;
	rect.extent.x <<= 1;
	rect.extent.y <<= 1;
	#endif
	LCDWinEraseRectangle(&rect, 0);
	rect.extent.y = bookList.bounds.extent.y;
	#ifdef HIGH_DENSITY
	rect.topLeft.x >>= 1;
	rect.topLeft.y >>= 1;
	rect.extent.x >>= 1;
	rect.extent.y >>= 1;
	#endif
	FrmSetObjectBounds(frmP, FrmGetObjectIndex(frmP, MainVsbarScrollBar), &rect);
	
	#ifndef PALMOS_30
	WinPopDrawState();
	#endif
}

static void BookFormInit(FormType* frmP)
{
	if (bookList.origBounds.extent.x == 0)
	{
		FrmGetObjectBounds(
			frmP,
			FrmGetObjectIndex(frmP, MainBooksGadget),
			&bookList.origBounds);

		#ifdef HIGH_DENSITY
		bookList.origBounds.topLeft.x <<= 1;
		bookList.origBounds.topLeft.y <<= 1;
		bookList.origBounds.extent.x <<= 1;
		bookList.origBounds.extent.y <<= 1;
		#endif
	}
}

static void BookFormUpdateControl()
{
	Int16			i;
	FormPtr			frmP = FrmGetActiveForm();
	ListPtr			lstP;
	ControlPtr		ctlP;
	RectangleType	rect;

	if (bookInMem)
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainDirField));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainUpButton));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainUpBitMap));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainHandheldBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainCategoryPopTrigger));
		if (l_volRef != vfsInvalidVolRef)
			FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainCardBitMap));
		
		for (i = 0; i < MAX_MEM_OP; i++)
			SysStringByIndex(MemOpStringList, i, oper.items[i], MAX_OP_ITEM_LEN);
		oper.num = MAX_MEM_OP;
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainCardBitMap));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, MainCategoryPopTrigger));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainDirField));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainUpButton));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainUpBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, MainHandheldBitMap));
		
		for (i = 0; i < MAX_VFS_OP; i++)
			SysStringByIndex(VfsOpStringList, i, oper.items[i], MAX_OP_ITEM_LEN);
		oper.num = MAX_VFS_OP;
	}
	
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainOperationList));
	LstSetListChoices(lstP, oper.items, oper.num);
	LstSetHeight(lstP, oper.num);
	LstSetSelection(lstP, 0);
	
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, MainChoicesPopTrigger));
	CtlSetLabel(ctlP, oper.items[0]);
	
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, MainChoicesPopTrigger), &rect);
	LstSetPosition(lstP, rect.topLeft.x, rect.topLeft.y);
}

static Boolean BookFormDoCommand(UInt16 command)
{
	Boolean		handled = true;

	MenuEraseStatus(0);

	switch (command)
	{
	case F1BookInHandheld:
		SwitchToHandheld();
		break;

	case F1BookInCard:
		SwitchToCard();
		break;
	
	case F1BookScanBook:
		ScanBook();
		break;
	
	case F1BookSaveList:
		Waiting(SaveListString);
		if (bookInMem)
			CacheListInMem();
		else
			CacheListInVfs();
		BooksDrawScreen();
		break;
	
	case F1BookSelectAll:
		SelectAll(true);
		BooksDrawScreen();
		break;
	
	case F1BookUnselectAll:
		SelectAll(false);
		BooksDrawScreen();
		break;

	case F1BookDelete:
		Delete();
		break;

	case F1BookSyncBookInfo:
		SyncBookInfo();
		break;
	
	case F1ToolsSortbyTitle:
		Sort(bookList.listP, 1);
		BooksSetFilter();
		BooksDrawScreen();
		break;
	
	case F1ToolsSortbyDate:
		Sort(bookList.listP, 2);
		BooksSetFilter();
		BooksDrawScreen();
		break;
	
	case F1ToolsSortbySize:
		Sort(bookList.listP, 3);
		BooksSetFilter();
		BooksDrawScreen();
		break;
	
	case F1ToolsRuntimeInformation:
		RuntimeInfo();
		break;
	
	case F1OptionsDisplay:
		CmnDlgSetReturnForm(MainForm);
		FrmGotoForm(DisplayForm);
		break;

	case F1OptionsBookList:
		if (ListOptions())
		{
			if (g_prefs.listFont == 0xFFFF)
			{
				CfnSetFont(g_prefs2.sysFont);
				#ifdef HIGH_DENSITY
				CfnSetEngFont(g_prefs2.engFont);
				#endif
				if (g_prefs.options & OPUSEADDFONT)
					CfnSetFont(4 + g_prefs2.chsFont);
			}
			else
				CfnSetFont(g_prefs.listFont);

			Waiting(ScanBookString);
			DeleteCache();
			if (bookInMem)
			{
				ScanAllBooksInMem();
				ListInfoClean(&vfsBookInfo);
			}
			else
			{
				ListInfoClean(&memBookInfo);
				ScanAllBooksInVfs();
			}
			BooksSetFilter();
			bookList.top = -1;
			bookList.bottom = -1;
			bookList.pos = -1;
			bookList.height = 0;
			BookFormLayout(FrmGetActiveForm());
			BooksDrawScreen();
		}
		break;
		
	case F1OptionsFont:
		if (MyFontSelect())
		{
			if (g_prefs.listFont == 0xFFFF)
			{
				CfnSetFont(g_prefs2.sysFont);
				#ifdef HIGH_DENSITY
				CfnSetEngFont(g_prefs2.engFont);
				#endif
				if (g_prefs.options & OPUSEADDFONT)
					CfnSetFont(4 + g_prefs2.chsFont);
			}
			else
				CfnSetFont(g_prefs.listFont);
			
			bookList.top = -1;
			bookList.bottom = -1;
			bookList.pos = -1;
			bookList.height = 0;
			BookFormLayout(FrmGetActiveForm());
			BooksDrawScreen();
			ContentsFontChanged();
		}
		break;
	
	case F1OptionsButtons:
		CmnDlgSetReturnForm(MainForm);
		FrmGotoForm(ButtonsForm);
		break;
	
	case F1OptionsRegister:
		Register();
		break;
		
	case F1OptionsAboutVBook:
		CmnDlgSetReturnForm(MainForm);
		FrmGotoForm(AboutForm);
		break;
	
	default:
		handled = false;
	}
	
	return handled;
}

static void BooksChangeCategory(UInt8 id, Char* category)
{
	VBOOKINFO	*infoP, info;
	DmOpenRef	openRef;
	MemHandle	recordH;
	Int16		i;
	
	for (i=0; i < memBookInfo.numItems; i++)
	{		
		if (memBookInfo.itemsP[i].categoryId == id &&
			memBookInfo.itemsP[i].status & ITEM_VBOOK)
		{
			if (DmFindDatabase(
				memBookInfo.itemsP[i].cardNo,
				memBookInfo.itemsP[i].titleP))
			{
				openRef = DmOpenDatabase(
					memBookInfo.itemsP[i].cardNo,
					memBookInfo.itemsP[i].dbID,
					dmModeReadWrite);
				
				recordH = DmGetRecord(openRef, 0);
				infoP = MemHandleLock(recordH);
				MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
				StrNCopy((Char*)info.szCategory, category, dmCategoryLength);
				DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
				MemHandleUnlock(recordH);
				DmReleaseRecord(openRef, 0, true);
				DmCloseDatabase(openRef);
			}
		}
	}
}

static void BooksDelCategory(UInt8 id)
{
	VBOOKINFO	*infoP, info;
	DmOpenRef	openRef;
	MemHandle	recordH;
	Int16		i;
	
	for (i=0; i < memBookInfo.numItems; i++)
	{		
		if (memBookInfo.itemsP[i].categoryId == id)
		{
			memBookInfo.itemsP[i].categoryId = dmUnfiledCategory;
			
			if (!(memBookInfo.itemsP[i].status & ITEM_VBOOK))
				continue;
			
			openRef = DmOpenDatabase(
				memBookInfo.itemsP[i].cardNo,
				memBookInfo.itemsP[i].dbID,
				dmModeReadWrite);
			if (openRef == NULL)
				continue;
			
			recordH = DmGetRecord(openRef, 0);
			infoP = MemHandleLock(recordH);
			MyMemCopy((UInt8*)&info, (UInt8*)infoP, sizeof(VBOOKINFO));
			info.szCategory[0] = 0;
			DmWrite(infoP, 0, &info, sizeof(VBOOKINFO));
			MemHandleUnlock(recordH);
			DmReleaseRecord(openRef, 0, true);
			DmCloseDatabase(openRef);
		}
		else if (memBookInfo.itemsP[i].categoryId > id)
			memBookInfo.itemsP[i].categoryId--;
	}
}

static Boolean BookListDoAction(UInt8 action)
{
	Boolean done = true;
	
	switch (action)
	{
	case ACTLINEUP:
		BooksLineUp();
		break;
		
	case ACTLINEDOWN:
		BooksLineDown();
		break;
		
	case ACTPAGEUP:
		BooksPageUp();
		break;
		
	case ACTPAGEDOWN:
		BooksPageDown();
		break;
		
	case ACTSELECT:
		if (bookList.pos != -1)
			BooksSelect(bookList.pos, false);
		break;
	
	default:
		done = false;
	}
	
	return done;
}

static void BookListRefresh()
{
	Waiting(ScanBookString);
	DeleteCache();
	if (bookInMem)
	{
		ScanAllBooksInMem();
		bookList.listP = &memBookInfo;
	}
	else
	{
		ScanAllBooksInVfs();
		bookList.listP = &vfsBookInfo;
	}
	BooksSetFilter();
	BooksDrawScreen();
}

#pragma mark === Form Callback ===

Boolean BookListPrepare()
{
	FontID	font;
	Int16	i;

	// Category initialize.
	cateAll = (Char*)MemPtrNew(6);
	ErrNonFatalDisplayIf(!cateAll, "All");

	cateUnfiled = (Char*)MemPtrNew(8);
	ErrNonFatalDisplayIf(!cateUnfiled, "Unfiled");

	cateEdit = (Char*)MemPtrNew(20);
	ErrNonFatalDisplayIf(!cateEdit, "Edit");

	SysStringByIndex(CategoryStringList, 0, cateAll, 6);
	SysStringByIndex(CategoryStringList, 1, cateUnfiled, 8);
	SysStringByIndex(CategoryStringList, 2, cateEdit, 20);

	MemSet(categories, sizeof(Char*) * dmRecNumCategories, 0);

	categories[0] = cateAll;
	categories[1] = cateUnfiled;
	categories[2] = cateEdit;

	cateNum = 3;
	cateSelection = 0;

	MemSet(&l_vols, sizeof(VolumesType), 0);
	EnumerateVolumes();

	MemSet(&memBookInfo, sizeof(ListInfoType), 0);
	MemSet(&vfsBookInfo, sizeof(ListInfoType), 0);
	MemSet(&bookList, sizeof(BookListType), 0);
	MemSet(&currentBook, sizeof(BookType), 0);

	MemSet(&oper, sizeof(OperationType), 0);
	for (i = 0; i < MAX_OP; i++)
	{
		oper.items[i] = (Char*)MemPtrNew(MAX_OP_ITEM_LEN);
		ErrNonFatalDisplayIf(!oper.items[i], "OP New");
	}

	font = LCDFntSetFont(symbol11Font);

	#ifdef PALMOS_50

	#ifdef HIGH_DENSITY
	bookList.col1 = FntCharWidth(1) * 2 + ITEM_SPACE;
	#else
	bookList.col1 = FntCharWidth(1) + ITEM_SPACE;
	#endif // HIGH_DENSITY

	#else

	bookList.col1 = FntCharWidth(1) + ITEM_SPACE;

	#endif // PALMOS_50

	LCDFntSetFont(font);
	
	saveLastBook = false;

	return true;
}

void BookListRelease()
{
	Int16	i;
//	Err		err;
//	MemPtr	startP, endP;
	
	CloseBook();
	
//	DebugInfo("Release", 0);
	
	if (saveLastBook)
	{
		if (bookInMem)
		{
			if (quickBook.titleP)
				StrCopy(g_prefs.pdbname, quickBook.titleP);
			else
				StrCopy(
					g_prefs.pdbname,
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].titleP);
			
			// Keep path is pure path before save it.
			TransPath(true);
			i = StrLen(g_prefs.path);
			g_prefs.path[i++] = '/';
			g_prefs.path[i] = 0;
		}
		else
		{
			if (quickBook.fileP)
				StrCopy(g_prefs.path, quickBook.fileP);
			else
			{
				TransPath(true);
				i = StrLen(g_prefs.path);
				if (i > 1)
					g_prefs.path[i++] = '/';
				StrCopy(
					&g_prefs.path[i],
					bookList.listP->itemsP[bookList.filtered[bookList.pos]].fileP);
			}
			
			g_prefs.pdbname[0] = 0;
		}
	}
	else
	{
		// When save last path, I add a '/' to end of path.
		// ex. "/lastpath/"
		TransPath(true);
		i = StrLen(g_prefs.path);
		g_prefs.path[i++] = '/';
		g_prefs.path[i] = 0;
		g_prefs.pdbname[0] = 0;
		if (bookInMem)
		{
			g_prefs.pdbname[0] = 1;
			g_prefs.pdbname[1] = 0;
		}
		
//		DebugInfo("Release", 1);
	}

	if (l_volRef != vfsInvalidVolRef)
		StrCopy(&g_prefs.path[256 - vbookMaxLabelLen], GetCardLabel(l_volRef));
	
	if (quickBook.titleP) MemPtrFree(quickBook.titleP);
	if (quickBook.fileP) MemPtrFree(quickBook.fileP);
	
//	DebugInfo("ca", cateSelection);
	
//	ErrNonFatalDisplayIf(!SysGetStackInfo(&startP, &endP), "Overflow!");

	for (i = 0; i < MAX_OP; i++)
	{
		ErrNonFatalDisplayIf(!oper.items[i], "OP NULL");
		MemPtrFree(oper.items[i]);
	}
	
//	DebugInfo("Release", 2);

	// Save current category.
	StrCopy(g_prefs.category, categories[cateSelection]);

	for (i = 1; i < cateNum - 2; i++)
		MemPtrFree(categories[i]);

	MemPtrFree(cateAll);
	MemPtrFree(cateUnfiled);
	MemPtrFree(cateEdit);
	
	for (i = 0; i < 2; i++)
	{
		if (l_vols.volLabs[i]) MemPtrFree(l_vols.volLabs[i]);
	}

	ListInfoClean(&memBookInfo);
	ListInfoClean(&vfsBookInfo);

	if (bookList.filtered)
		MemPtrFree(bookList.filtered);

//	DebugInfo("Release", 3);
}

static Boolean VerifyPath()
{
	FileRef	ref;
	Err		err;
	Int16	i;
	
	// Be sure path is a pure path, not path and filename.
	i = StrLen(g_prefs.path) - 1;
	if (g_prefs.path[i] != '/')
	{
		while (g_prefs.path[i] != '/') i--;
		if (i == 0) i++;
	}
	g_prefs.path[i] = 0;
	
	if (l_volRef == vfsInvalidVolRef)
		return false;

	err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &ref);
	while (err)
	{
		while (g_prefs.path[i] != '/') i--;
		if (i == 0) i++;
		g_prefs.path[i] = 0;
		err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &ref);
	}
	VFSFileClose(ref);
	
	return true;
}

void ResumeLastReading(UInt16 cardNo, LocalID dbID)
{
	FileRef	fileRef;
	Err		err;
	Int16	i;
	UInt32	type, creator;
	UInt16	volRef;
	
	resume = false;
	MemSet(&quickBook, sizeof(BookItemType), 0);
	
	volRef = GetVolumeRef(&g_prefs.path[256 - vbookMaxLabelLen]);
	if (volRef != vfsInvalidVolRef)
	{
		l_volRef = volRef;
		vfsBookInfo.volRef = volRef;
	}
	
	i = StrLen(g_prefs.path) - 1;
	if (!REGISTERED ||
		g_prefs.pdbname[0] == 1 ||
		(g_prefs.pdbname[0] == 0 && g_prefs.path[i] == '/'))
	{
		if (VerifyPath())
			if (g_prefs.pdbname[0] == 0)
				bookInMem = false;
		l_isPath = true;
		return;
	}
	
	if (dbID)
		err = DmDatabaseInfo(
			cardNo, dbID, g_prefs.pdbname,
			NULL, NULL,
			NULL, NULL, NULL, NULL,
			NULL, NULL,
			NULL, NULL);

	if (StrLen(g_prefs.pdbname))
	{
		// Last readed book saved in handheld.
		bookInMem = true;
		
		quickBook.dbID = DmFindDatabase(0, g_prefs.pdbname);
		if (quickBook.dbID)
		{
			err = DmDatabaseInfo(
				quickBook.cardNo, quickBook.dbID, NULL,
				NULL, NULL,
				NULL, NULL, NULL, NULL,
				NULL, NULL,
				&type, &creator);
			
			if (type == VBookType && creator == VBookCreator)
				quickBook.status = ITEM_VBOOK;
			else if (type == DocType && (creator == DocCreator || creator == HSCreator))
				quickBook.status = ITEM_DOC;
			
			if (quickBook.status)
			{
				quickBook.titleP = MemPtrNew(StrLen(g_prefs.pdbname) + 1);
				if (quickBook.titleP == NULL)
					SysFatalAlert("Insufficient memory!");
				StrCopy(quickBook.titleP, g_prefs.pdbname);
				resume = true;
			}
		}
	}
	else if ((l_volRef != vfsInvalidVolRef) && (l_volRef == volRef))
	{
		// Last readed book saved in expansion card.
		bookInMem = false;
		
		err = VFSFileOpen(l_volRef, g_prefs.path, vfsModeRead, &fileRef);
		if (!err)
		{
			i = StrLen(g_prefs.path);
			quickBook.fileP = MemPtrNew(i + 1);
			if (quickBook.fileP == NULL)
				SysFatalAlert("Insufficient memory!");
			
			// Find ext-name of file.
			i--;
			while (g_prefs.path[i] != '.') i--;
			StrToLower(quickBook.fileP, &g_prefs.path[i]);
			
			if (!StrCompare(quickBook.fileP, ".pdb"))
			{
				err = VFSFileDBInfo(
					fileRef, NULL, NULL, NULL,
					NULL, NULL, NULL, NULL,
					NULL, NULL,
					&type, &creator, NULL);
				
				if (type == VBookType && creator == VBookCreator)
					quickBook.status = ITEM_VBOOK;
				else if (type == DocType && (creator == DocCreator || creator == HSCreator))
					quickBook.status = ITEM_DOC;
			}
			else if (!StrCompare(quickBook.fileP, ".txt"))
				quickBook.status = ITEM_TEXT;
			
			if (quickBook.status)
			{
				resume = true;
				StrCopy(quickBook.fileP, g_prefs.path);
			}
			else
			{
				MemPtrFree(quickBook.fileP);
				quickBook.fileP = NULL;
			}
			
			VFSFileClose(fileRef);
		}
	}
	
	VerifyPath();
	l_isPath = true;
}

void SetSaveLastBook(Boolean save)
{
	saveLastBook = save;
}

Boolean BookListFormHandleEvent(EventType* eventP)
{
	Boolean		handled = false;
	FormPtr		frmP;

	#ifdef HIGH_DENSITY
	Coord		x, y;
	#endif
	
	switch (eventP->eType)
	{
	case menuEvent:
		return BookFormDoCommand(eventP->data.menu.itemID);

	case frmOpenEvent:
		if (resume)
		{
			WinSetDrawWindow(WinGetDisplayWindow());
			OpenBook();
			resume = false;
			return true;
		}
		
		CloseBook();
		
		if (quickBook.titleP)
		{
			MemPtrFree(quickBook.titleP);
			quickBook.titleP = NULL;
		}
		if (quickBook.fileP)
		{
			MemPtrFree(quickBook.fileP);
			quickBook.fileP = NULL;
		}

		if (g_prefs.listFont == 0xFFFF)
		{
			// Use as same font as view.
			CfnSetFont(g_prefs2.sysFont);
			#ifdef HIGH_DENSITY
			CfnSetEngFont(g_prefs2.engFont);
			#endif
			if (g_prefs.options & OPUSEADDFONT)
				CfnSetFont(4 + g_prefs2.chsFont);
		}
		else
			CfnSetFont(g_prefs.listFont);

		frmP = FrmGetActiveForm();
		
		#ifdef USE_PALM_SILKSCREEN

		WinSetConstraintsSize(
			WinGetWindowHandle(frmP),
			160, 225, 225,
			160, 160, 160);
		FrmSetDIAPolicyAttr(frmP, frmDIAPolicyCustom);
		PINSetInputTriggerState(pinInputTriggerEnabled);
		PINSetInputAreaState(pinInputAreaUser);

		#endif // USE_PALM_SILKSCREEN
		
		if (ResizeScreen())
			bookList.origBounds.extent.x = 0;
		BookFormUpdateControl();
		BookFormInit(frmP);
		FrmDrawForm(frmP);
		CategoriesInit(false);
		BookFormLayout(frmP);

		CurrentPlace();
		Waiting(ScanBookString);

		if (bookInMem)
		{
			if (memBookInfo.numItems == 0)
			{
				ScanAllBooksInMem();
				CategoriesInit(true);
				CategoriesInit(false);
				bookList.listP = &memBookInfo;
				BooksSetFilter();
			}
		}
		else
		{
			if (vfsBookInfo.numItems == 0)
			{
				ScanAllBooksInVfs();
				vfsBookInfo.volRef = l_volRef;
				CategoriesInit(true);
				CategoriesInit(false);
				bookList.listP = &vfsBookInfo;
				BooksSetFilter();
			}
		}

		BooksDrawScreen();
		handled = true;
		break;
	
	#ifdef USE_SILKSCREEN
	case dispChangeEvent:
		handled = true;
	case winDisplayChangedEvent:
		if (ResizeScreen())
		{
			frmP = FrmGetActiveForm();
			FrmEraseForm(frmP);
			bookList.origBounds.extent.x = 0;
			BookFormInit(frmP);
			FrmDrawForm(frmP);
			BookFormLayout(frmP);
			BooksDrawScreen();
		}
		break;
	#endif // USE_SILKSCREEN

	case listRefreshEvent:
		BookListRefresh();
		handled = true;
		break;
		
	case frmUpdateEvent:
		FrmDrawForm(FrmGetActiveForm());
		BooksDrawScreen();
		handled = true;
		break;
		
	case frmCloseEvent:
		break;
	
	case frmObjectFocusTakeEvent:
		if (eventP->data.frmObjectFocusTake.objectID == MainBooksGadget)
		{
			RectangleType	bounds;
			
			frmP = FrmGetActiveForm();
			FrmGetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, MainBooksGadget),
				&bounds);
			FrmGlueNavDrawFocusRing(
				frmP,
				MainBooksGadget,
				frmNavFocusRingNoExtraInfo,
				&bounds,
				frmNavFocusRingStyleHorizontalBars,
				false);
		}
		break;
		
	case penDownEvent:
		#ifdef HIGH_DENSITY

		x = eventP->screenX;
		y = eventP->screenY;
		x <<= 1;
		y <<= 1;

		if (RctPtInRectangle(x, y, &bookList.bounds))
		{
			BooksTapped(x, y);
			handled = true;
		}

		#else

		if (RctPtInRectangle(eventP->screenX, eventP->screenY,
			&bookList.bounds))
		{
			BooksTapped(eventP->screenX, eventP->screenY);
			handled = true;
		}

		#endif // HIGH_DENSITY
		break;
		
	case keyDownEvent:
		handled = BookListFormHandleKey(eventP);
		break;
	
	case popSelectEvent:
		switch (eventP->data.popSelect.controlID)
		{
		case MainCategoryPopTrigger:
			if (eventP->data.popSelect.selection != cateSelection)
			{
				if (eventP->data.popSelect.selection == cateNum - 1)
				{
					eventP->data.popSelect.selection = cateSelection;
					FrmGotoForm(CategoriesForm);
				}
				else
				{
					cateSelection = eventP->data.popSelect.selection;
					BooksSetFilter();
					BooksDrawScreen();
				}
				LstSetSelection(eventP->data.popSelect.listP, cateSelection);
			}
			break;
		
		case MainChoicesPopTrigger:
			if (bookInMem)
			{
				if (eventP->data.popSelect.selection == 3)
				{
					Category();
					ResetOperation();
					handled = true;
				}
				else if (eventP->data.popSelect.selection == 4)
				{
					SetBackup(true);
					ResetOperation();
					handled = true;
				}
				else if (eventP->data.popSelect.selection == 5)
				{
					SetBackup(false);
					ResetOperation();
					handled = true;
				}
			}
			break;
		}
		break;
	
	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case MainSwitchButton:
			if (bookInMem)
				SwitchToCard();
			else
				SwitchToHandheld();
			handled = true;
			break;
			
		case MainUpButton:
			ChangeDir(false);
			handled = true;
			break;
		}
		break;
	
	case sclRepeatEvent:
		if (eventP->data.sclRepeat.value != eventP->data.sclRepeat.newValue)
		{
			bookList.top = eventP->data.sclRepeat.newValue;
			BooksDrawScreen();
		}
		break;
	}

	return handled;
}

Boolean BookListFormHandleKey(EventPtr keyDownEventP)
{
	Boolean	handled;
	Boolean processKey = true;
	
	switch (keyDownEventP->data.keyDown.chr)
	{
	case pageUpChr:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[4]);
		break;

	case pageDownChr:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[5]);
		break;

	case vchrHard1:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[0]);
		break;
		
	case vchrHard2:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[1]);
		break;
		
	case vchrHard3:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[2]);
		break;
		
	case vchrHard4:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[3]);
		break;

	case vchrJogUp:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[6]);
		break;

	case vchrJogDown:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[7]);
		break;

	case vchrJogRelease:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[8]);
		break;
		
	case vchrCalc:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[14]);
		break;
		
	case vchrFind:
		handled = BookListDoAction(g_prefs.uiBooks.buttons[15]);
		break;
	
	// 老版本的五向键处理方法
	case vchrNavChange:
		if (keyDownEventP->data.keyDown.keyCode & navBitUp)
			BookListDoAction(g_prefs.uiBooks.buttons[9]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitDown))
			BookListDoAction(g_prefs.uiBooks.buttons[10]);
		else if (keyDownEventP->data.keyDown.keyCode & navBitLeft)
			BookListDoAction(g_prefs.uiBooks.buttons[11]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitRight))
			BookListDoAction(g_prefs.uiBooks.buttons[12]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitSelect))
			BookListDoAction(g_prefs.uiBooks.buttons[13]);
		else
			handled = false;
		break;
	
	default:
		// 新版本的五向键处理方法
		if (IsFiveWayNavEvent(keyDownEventP))
		{
			if (FrmGlueNavIsSupported())
			{
				FormPtr frmP = FrmGetActiveForm();
				if (FrmGetObjectId(frmP, FrmGetFocus(frmP)) == MainBooksGadget)
					processKey = true;
				else
					processKey = false;
			}
		}
		else
			processKey = false;
		
		if (processKey)
		{
			switch (keyDownEventP->data.keyDown.chr)
			{
			case vchrRockerUp:
				BookListDoAction(g_prefs.uiBooks.buttons[9]);
				break;
				
			case vchrRockerDown:
				BookListDoAction(g_prefs.uiBooks.buttons[10]);
				break;
			
			case vchrRockerLeft:
				BookListDoAction(g_prefs.uiBooks.buttons[11]);
				break;
				
			case vchrRockerRight:
				BookListDoAction(g_prefs.uiBooks.buttons[12]);
				break;
			
			case vchrRockerCenter:
				BookListDoAction(g_prefs.uiBooks.buttons[13]);
				break;

			default:
				handled = false;
			}
		}
		else
			handled = false;
	}

	return handled;
}

Boolean CategoryFormHandleEvent(EventType* eventP)
{
	Boolean		handled = false;
	FormPtr		frmP;
	ListPtr		listP;
	FieldPtr	fieldP;
	Boolean		update = false;
	MemHandle	txtH, oldTxtH;
	Char		*p;
	Int16		sel;
	
	switch (eventP->eType)
	{
	case frmOpenEvent:
		frmP = FrmGetActiveForm();
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CategoriesNameList));
		if (cateNum > 3)
			LstSetListChoices(listP, &categories[1], cateNum-3);
		else
			LstSetListChoices(listP, NULL, 0);

		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case CategoriesOkButton:
			FrmGotoForm(MainForm);
			handled = true;
			break;
			
		case CategoriesNewButton:
			if (cateNum == dmRecNumCategories)
			{
				FrmAlert(CannotAddAlert);
				handled = true;
				break;
			}

			frmP = FrmInitForm(NewCateForm);
			FrmSetFocus(frmP, FrmGetObjectIndex(frmP, NewCateNameField));
			if (FrmDoDialog(frmP) == NewCateOkButton)
			{
				fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, NewCateNameField));
				if (FldGetTextLength(fieldP))
				{
					sel= cateNum - 2; // The last.
					categories[sel] = (Char*)MemPtrNew(dmCategoryLength);
					StrCopy(categories[sel], FldGetTextPtr(fieldP));

					if (sel <= cateSelection)
						cateSelection++;

					cateNum++;
					categories[cateNum-2] = cateUnfiled;
					categories[cateNum-1] = cateEdit;
					update = true;
				}
			}
			FrmDeleteForm(frmP);

			if (update)
			{
				frmP = FrmGetFormPtr(CategoriesForm);
				listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CategoriesNameList));
				LstSetListChoices(listP, &categories[1], cateNum-3);
				LstDrawList(listP);
			}

			handled = true;
			break;
		
		case CategoriesRenameButton:
			frmP = FrmGetActiveForm();
			listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CategoriesNameList));
			sel = LstGetSelection(listP);
			if (sel == noListSelection)
			{
				FrmAlert(CateSelAlert);
				handled = true;
				break;
			}

			sel++;

			frmP = FrmInitForm(NewCateForm);
			fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, NewCateNameField));
			FldSetMaxChars(fieldP, dmCategoryLength-1);
			
			txtH = MemHandleNew(dmCategoryLength);
			p = MemHandleLock(txtH);
			StrCopy(p, categories[sel]);
			MemHandleUnlock(txtH);

			oldTxtH = FldGetTextHandle(fieldP);
			FldSetTextHandle(fieldP, txtH);
			FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
			if (oldTxtH != NULL)
				MemHandleFree(oldTxtH);

			FrmSetFocus(frmP, FrmGetObjectIndex(frmP, NewCateNameField));
			if (FrmDoDialog(frmP) == NewCateOkButton)
			{
				p = FldGetTextPtr(fieldP);
				if (p)
				{
					BooksChangeCategory(sel, p);
					PrefMemDocChangeCategory(p, categories[sel]);
					StrCopy(categories[sel], p);
					update = true;
				}
			}
			FrmDeleteForm(frmP);

			if (update)
			{
				frmP = FrmGetFormPtr(CategoriesForm);
				listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CategoriesNameList));
				LstSetListChoices(listP, &categories[1], cateNum-3);
				LstDrawList(listP);
			}
			handled = true;
			break;
			
		case CategoriesDeleteButton:
			frmP = FrmGetActiveForm();
			listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, CategoriesNameList));
			sel = LstGetSelection(listP);
			if (sel == noListSelection)
			{
				FrmAlert(CateSelAlert);
				handled = true;
				break;
			}

			sel++;
			if (sel == cateSelection)
				cateSelection = 0;
			else if (sel < cateSelection)
				cateSelection--;

			// Change book belongs this category to unfiled.
			BooksDelCategory(sel);
			BooksSetFilter();
			PrefMemDocDelCategory(categories[sel]);
			
			MemPtrFree(categories[sel]);
			for (sel++; sel < cateNum; sel++)
				categories[sel-1] = categories[sel];
			cateNum--;

			LstSetListChoices(listP, &categories[1], cateNum-3);
			LstDrawList(listP);

			handled = true;
			break;
		}
		break;
	}

	return handled;
}

#pragma mark === Book Operations ===

// Called by ContentsForm for contents data.
Boolean BookGetContents(MemPtr* indexPP, MemPtr* titlePP)
{
	Boolean		result = false;
	VBOOKINFO*	infoP;
	
	if (currentBook.type & BOOK_TYPE_DOC ||
		currentBook.type & BOOK_TYPE_TEXT)
		return false;
	
	infoP = currentBook.infoP;
	// If book doesn't have a table of contents,
	// title record No. will be 0.
	if (infoP->nTitleIndexRecNo)
	{
		result = true;

		currentBook.titleIndexH	= QueryRecord(infoP->nTitleIndexRecNo);
		currentBook.titleDataH	= QueryRecord(infoP->nTitleIndexRecNo+1);
		
		*indexPP = MemHandleLock(currentBook.titleIndexH);
		*titlePP = MemHandleLock(currentBook.titleDataH);
	}

	return result;
}

void BookReleaseContents()
{
	if (currentBook.type & BOOK_TYPE_DOC ||
		currentBook.type & BOOK_TYPE_TEXT)
	{
		if (currentBook.titleIndexH)
			MemHandleFree(currentBook.titleIndexH);
	}
	else if (currentBook.titleIndexH)
	{
		MemHandleUnlock(currentBook.titleIndexH);
		MemHandleUnlock(currentBook.titleDataH);
		
		if (!(currentBook.type & BOOK_TYPE_INMEM))
		{
			MemHandleFree(currentBook.titleIndexH);
			MemHandleFree(currentBook.titleDataH);
		}
	}
	
	currentBook.titleIndexH = NULL;
	currentBook.titleDataH = NULL;
}

// Open information of specified chpater,
// it releases last opened information if they are not in the same record.
Boolean BookOpenChapter(UInt16 chapIndex)
{
	VBOOKINFO*			infoP;
	VBOOKCHAPTERINDEX*	pci;
	
	UInt16		recNo;
	UInt16		chapNo;
	MemHandle	record;

	if (currentBook.type & BOOK_TYPE_DOC ||
		currentBook.type & BOOK_TYPE_TEXT)
		return true;

	// Chapter index may stored in several record,
	// so, must find which record it be first.
	recNo = chapIndex / MAXINDEXENTRYPERRECORD; // Record No, 0-based.
	chapNo = chapIndex % MAXINDEXENTRYPERRECORD; // Offset in record. 0-based.
	
	infoP = currentBook.infoP;
	recNo += infoP->nChapterIndexRecNo;

	// If chapter index record has not open, open it.
	if (recNo != currentBook.indexRecNo)
	{
		if (currentBook.chapterIndexP)
		{
			MemPtrUnlock(currentBook.chapterIndexP);
		
			if (!(currentBook.type & BOOK_TYPE_INMEM))
				MemPtrFree(currentBook.chapterIndexP);
		}
		
		record = QueryRecord(recNo);
		currentBook.chapterIndexP = MemHandleLock(record);
		currentBook.indexRecNo = recNo;
	}

	currentBook.chapterIndex = chapNo;
	pci = currentBook.chapterIndexP;

	// Open chapter information record.
	if (pci[currentBook.chapterIndex].nInfoNo != currentBook.infoRecNo)
	{
		if (currentBook.chapterInfoP)
		{
			MemPtrUnlock(currentBook.chapterInfoP);

			if (!(currentBook.type & BOOK_TYPE_INMEM))
				MemPtrFree(currentBook.chapterInfoP);
		}

		record = QueryRecord(pci[currentBook.chapterIndex].nInfoNo);
		currentBook.chapterInfoP = MemHandleLock(record);
		currentBook.infoRecNo = pci[currentBook.chapterIndex].nInfoNo;
	}

	currentBook.infoEntryP =
		(UInt8*)currentBook.chapterInfoP + pci[currentBook.chapterIndex].nOffset;

	return true;
}

// Release all opened information of chpater.
// This function only need be called before close book.
void BookCloseChapter()
{
	if (currentBook.chapterIndexP)
	{
		MemPtrUnlock(currentBook.chapterIndexP);
		if (!(currentBook.type & BOOK_TYPE_INMEM))
			MemPtrFree(currentBook.chapterIndexP);
	}
	
	if (currentBook.chapterInfoP)
	{
		MemPtrUnlock(currentBook.chapterInfoP);
		MemPtrUnlock(currentBook.chapterDataP);
		if (!(currentBook.type & BOOK_TYPE_INMEM))
		{
			MemPtrFree(currentBook.chapterInfoP);
			MemPtrFree(currentBook.chapterDataP);
		}
	}

	currentBook.chapterIndexP	= NULL;
	currentBook.chapterInfoP	= NULL;
	currentBook.chapterDataP	= NULL;

	currentBook.indexRecNo		= 0;
	currentBook.infoRecNo		= 0;
	currentBook.dataRecNo		= 0;
}

// Get a text data block, uncompress if need.
Boolean BookGetBlock(
	UInt16 dataIndex, UInt8* bufP, UInt16* dataLenP, Int16 bufLen, UInt32* endLenP)
{
	if (currentBook.type & BOOK_TYPE_VBOOK)
		return VBookGetBlock(dataIndex, bufP, dataLenP, bufLen, endLenP);
	else if (currentBook.type & BOOK_TYPE_DOC)
		return DocGetBlock(dataIndex, bufP, dataLenP, bufLen, endLenP);
	else
		return TxtGetBlock(dataIndex, bufP, dataLenP, bufLen, endLenP);
}

Boolean BookGetDataIndexByOffset(
	UInt32 offset, UInt16* indexP, UInt16* offsetP)
{
	if (currentBook.type & BOOK_TYPE_VBOOK)
	{
		return VBookGetDataIndexByOffset(offset, indexP, offsetP);
	}
	else if (currentBook.type & BOOK_TYPE_DOC)
	{
		return DocGetDataIndexByOffset(offset, indexP, offsetP);
	}
	else
	{
		if (offset > currentBook.length - 1)
			offset = currentBook.length - 1;
		*indexP = offset / 4096;
		*offsetP = offset % 4096;
		return true;
	}
}

Boolean BookGetDataIndexByAnchor(
	UInt16 anchorIndex, UInt16* indexP, UInt16* offsetP)
{
	Boolean	result;
	
	VBOOKCHAPTERINDEX*	pci;
	VBOOKEXTLINK*		pel;
	UInt32*				pil;

	UInt16	chapID;
	UInt16	anchID;
	
	pci = currentBook.chapterIndexP;

	if (anchorIndex >= pci[currentBook.chapterIndex].nExtLinksNum)
		return false;

	pel = (VBOOKEXTLINK*)currentBook.infoEntryP;
	chapID = pel[anchorIndex].nChapter;
	anchID = pel[anchorIndex].nAnchor;
	
	if (BookOpenChapter(chapID))
	{
		result = true;
		if (anchID == 0xFFFF)
		{
			*indexP = 0;
			*offsetP = 0;
		}
		else
		{
			pil = (UInt32*)(currentBook.infoEntryP +
				4 * pci[currentBook.chapterIndex].nExtLinksNum);
			result = BookGetDataIndexByOffset(pil[anchID], indexP, offsetP);
		}
	}
	else
		result = false;

	return result;
}

UInt16 BookGetChapterNum()
{
	return currentBook.chapterNum;
}

UInt16 BookGetChapterIndex()
{
	VBOOKINFO*	infoP;
	UInt16		recNo;
	
	if (currentBook.type & BOOK_TYPE_VBOOK)
	{
		infoP = currentBook.infoP;
		recNo = currentBook.indexRecNo - infoP->nChapterIndexRecNo;
		return MAXINDEXENTRYPERRECORD * recNo + currentBook.chapterIndex;
	}
	else
		return 0;
}

UInt32 BookGetChapterLen()
{
	if (currentBook.type & BOOK_TYPE_VBOOK)
	{
		VBOOKCHAPTERINDEX* pci = currentBook.chapterIndexP;
		return pci[currentBook.chapterIndex].nLength;
	}
	else
		return currentBook.length;
}

UInt16 BookGetChapterDataNum()
{
	if (currentBook.type & BOOK_TYPE_VBOOK)
	{
		VBOOKCHAPTERINDEX* pci = currentBook.chapterIndexP;
		return pci[currentBook.chapterIndex].nBlocksNum;
	}
	else
		return currentBook.chapterIndex;
}

void BookGetReference(DmOpenRef* memRefP, FileRef* vfsRefP, Boolean* isBookP)
{
	*isBookP = (currentBook.type & BOOK_TYPE_VBOOK) ? true : false;
	if (currentBook.type & BOOK_TYPE_INMEM)
	{
		*memRefP = currentBook.memRef;
		*vfsRefP = NULL;
	}
	else
	{
		*memRefP = NULL;
		*vfsRefP = currentBook.vfsRef;
	}
}

Char* BookGetPath()
{
	if (quickBook.fileP)
		return quickBook.fileP;
	else
		return g_prefs.path;
}

UInt16 GetVolRef(Char *labelP)
{
	Int16	i;
	
	if (labelP)
	{
		for (i = 0; i < l_vols.volNum; i++)
			if (l_vols.volRefs[i] == l_volRef)
			{
				StrCopy(labelP, l_vols.volLabs[i]);
				break;
			}
	}
	
	return l_volRef;
}

void BookListFontChanged()
{
	bookList.height = 0;
}
