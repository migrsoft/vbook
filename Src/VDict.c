/*

VDict DA allocated dynamic heap memory use.

Total: 4608 bytes

0		(1024)	: Index buffer
1024	(1024)	: Data buffer
2048	(   2)	: Reserved
2050	(  66)	: Word
2116	(  66)	: Phonetic
2182	( 122)	: Variables
2304	( 256)	: Lines of block
2560	( 768)	: Break lines array
3328	( 768)	: Search stack
4096	( 512)	: Preferences

Uncompress buffer

Total: 10240 bytes

0		(1024)	: Index
1024	(4096)	: Uncompressed data
5120	(4096)	: Compressed data
9216	(1024)	: Stack backup or z_stream

*/


#include "VBook.h"
#include "VDict.h"
#include "BookList.h"

#define dicPath "/Palm/Programs/VDict/"
#define READ_INDEX_NUM 100
#define READ_DATA_SIZE 512
#define BLOCK_SIZE 4096
#define INVALID_DATA 0xffffffff

#define ISUPPER(c) (c >= 'A' && c <= 'Z')
#define ISLOWER(c) (c >= 'a' && c <= 'z')
#define ISLETTER(c) (ISUPPER(c) || ISLOWER(c))
#define ISNUMBER(c) (c >= '0' && c <= '9')

typedef struct {
	Char	dict[10][32];
	UInt16	use;
	UInt16	info;
} VDictPrefsType, *VDictPrefsPtr;

typedef struct {
	Char	label[6];
	UInt16	ver;
	UInt32	words;
	UInt32	tree;
	UInt32	table;
	UInt32	data;
} DictType, *DictPtr;

typedef struct {
	UInt16	num;
} HeadType, *HeadPtr;

typedef struct {
	UInt16	ch;
	UInt32	data;
	UInt32	child;
} NodeType, *NodePtr;

typedef struct {
	UInt32	child;
	UInt16	index;
	UInt16	ch;
	UInt32	data;
} StackNodeType, *StackNodePtr;

typedef struct {
	UInt16	vol;
	FileRef	ref;
	
	FormPtr	frmP;

	Char*	items[10];
	Boolean	indic;

	// Variable of display.
	Int16	phonetic;
	Int16	start;
	Int16	mid;
	Int16	end;
	Int16	first; // First line in display.
	Int16	last; // Last line in display.
	Int16	top;
	Int16	bot;
	UInt8*	midP; // Middle of data buffer.
	UInt8*	botP; // Bottom of data buffer.

	UInt32	dpos; // Position of data of current word.
	Int32	cache;
} VarsType, *VarsPtr;

typedef struct {
	Int16	lines;
	UInt16	pos;
} BlockType, *BlockPtr;

typedef struct {
	UInt16	pos;
	Int16	len;
} LineType, *LinePtr;

typedef struct {
	UInt16			zlibref;

	UInt32			tree;
	UInt32			table;
	UInt32			data;
	
	UInt8*			indexP;
	UInt8*			dataP;
	UInt8*			wordP;
	UInt8*			phoneticP;
	
	UInt8*			idxBufP;
	UInt8*			ucpBufP;
	UInt8*			datBufP;
	
	VarsPtr			varsP;
	BlockPtr		numLines;
	LinePtr			brkLines;
	StackNodePtr	stack;
	Int8			si;
	Int8			near; // 0 return to up level; 1 next char

	RectangleType	rect;
	VDictPrefsPtr	prefsP;
} DADataType, *DADataPtr;


static DADataType	da;
static Boolean		useDA;
static MemHandle	fontH;
static MemHandle	wordH;


Boolean ScanDict();
Boolean OpenDict();
void QuickSwitchDict(Int16);
void ChooseDict();
void ChooseDictFinish(Boolean);
void CaseSwitch();
void Search();
Boolean SearchChar(UInt16);
Boolean AlreadyFound(UInt8*, Int8);
Boolean FindNextWord();
void Explain();
Int16 BreakLine(Int16, Int8);
void PgUp();
void PgDown();
void PrevWord();
void NextWord();
void NewWord();
void DrawScreen();
void HideDictUI();
void DictMemo();


static Boolean ScanDict()
{
	Char			*path;
	FileRef			dirref;
	FileInfoType	info;
	Err				err;
	Int8			i;
	UInt32			iter;
	Char			name[32];
	DictPtr			dictP;
	
	MemSet (da.prefsP, 320, 0);

	path = (Char*)((UInt8*)da.prefsP + sizeof (VDictPrefsType));
	StrCopy (path, dicPath);
	err = VFSFileOpen (da.varsP->vol, path, vfsModeRead, &dirref);
	if (err != errNone) return false;
	
	info.nameP = name;
	info.nameBufLen = 32;
	iter = vfsIteratorStart;
	i = 0;
	while (iter != vfsIteratorStop)
	{
		err = VFSDirEntryEnumerate (dirref, &iter, &info);
		if (err != errNone) break;

		if (info.attributes & vfsFileAttrDirectory) continue;
		
		// Check extended name .dic
		if (StrCaselessCompare (&info.nameP[StrLen (info.nameP) - 4], ".dic")) continue;
		
		StrCopy (&path[21], info.nameP);
		// Check file id.
		err = VFSFileOpen (da.varsP->vol, path, vfsModeRead, &da.varsP->ref);
		if (err != errNone) continue;
		
		VFSFileRead (da.varsP->ref, sizeof (DictType), da.indexP, NULL);
		dictP = (DictPtr)da.indexP;
		if (!StrCompare (dictP->label, "VDICT") && dictP->ver == 4)
		{
			// Find dictionary file name.
			StrCopy (da.prefsP->dict[i], info.nameP);
			i++;
		}
		VFSFileClose (da.varsP->ref);
		da.varsP->ref = 0;
	}
	
	VFSFileClose (dirref);
	if (i)
	{
		da.prefsP->use = 1;
		da.prefsP->use <<= 15;
	}
	else
		da.prefsP->use = 0;
	
	return ((i) ? true : false);
}

static Boolean OpenDict()
{
	Int8	i;
	Char	*path;
	DictPtr	dictP;
	Err		err;
	FileRef	ref;

	for (i = 0; i < 10; i++)
	{
		if (da.prefsP->dict[i][0] == 0)
		{
			i--;
			if (i == -1) return false;
			i = 0;
			break;
		}
		
		if ((da.prefsP->use >> 15 - i) & 0x1) break;
	}
	
	if (i == 10) i = 0;
	
	da.prefsP->use = 1;
	da.prefsP->use <<= 15 - i;
	
	path = (Char*)((UInt8*)da.prefsP + sizeof (VDictPrefsType));
	StrCopy (path, dicPath);
	StrCopy (&path[21], da.prefsP->dict[i]);
	err = VFSFileOpen (da.varsP->vol, path, vfsModeRead, &ref);
	if (err != errNone)
	{
		da.prefsP->use = 0;
		return false;
	}

	if (da.varsP->ref)
		VFSFileClose (da.varsP->ref);
	da.varsP->ref = ref;
	
	VFSFileRead (da.varsP->ref, sizeof (DictType), da.indexP, NULL);
	dictP = (DictPtr)da.indexP;

	da.tree			= dictP->tree;
	da.table			= dictP->table;
	da.data			= dictP->data;
	da.si				= -1;
	da.wordP[0]		= 0;
	da.phoneticP[0]	= 0;
	da.varsP->cache	= -1;
	
	return true;
}

static void HideDictUI()
{
	EventType	event;
	FontID		oldFont;
	
	FrmEraseForm(da.varsP->frmP);
	
	do {
		EvtGetEvent(&event, evtWaitForever);
	} while (event.eType != penUpEvent);
	
	FrmDrawForm(da.varsP->frmP);
	
	oldFont = FntSetFont(stdFont);
	DrawScreen();
	FntSetFont(oldFont);
}

static void SwitchUI (FormPtr frmP, Int8 type)
{
	UInt16 i, num, id;

	num = FrmGetNumberOfObjects (frmP);
	for (i = 0; i < num; i++)
	{
		id = FrmGetObjectId (frmP, i);
		
		if (type == 1) // Main UI.
		{
			if (id == VDictWordField ||
				id == VDictPrevButton ||
				id == VDictNextButton ||
				id == VDictCaseButton ||
				id == VDictNewButton ||
				id == VDictCloseButton ||
				id == VDictMeanGadget)
				FrmShowObject (frmP, i);
			else
				FrmHideObject (frmP, i);
		}
		else if (type == 2) // Dictionary choose UI.
		{
			if (id == VDictDictsList ||
				id == VDictOKButton ||
				id == VDictCancelButton)
				FrmShowObject (frmP, i);
			else
				FrmHideObject (frmP, i);
		}
	}
}

static void QuickSwitchDict (Int16 index)
{
	if (da.prefsP->dict[index][0])
	{
		da.prefsP->use = 1;
		da.prefsP->use <<= 15 - index;

		if (OpenDict())
		{
			EventType event;
			event.eType = dicSearchEvent;
			EvtAddEventToQueue(&event);
		}
	}
}

static void ChooseDict()
{
	ListPtr	lstP;
	Int16	i, sel;
	
	if (!da.varsP->indic) return;
	
	da.varsP->indic = false;
	
	i = 0;
	sel = noListSelection;
	while (da.prefsP->dict[i][0] && i < 10)
	{
		da.varsP->items[i] = da.prefsP->dict[i];
		if ((da.prefsP->use >> 15 - i) & 0x1) sel = i;
		i++;
	}

	SwitchUI(da.varsP->frmP, 2);
	lstP = FrmGetObjectPtr(da.varsP->frmP, FrmGetObjectIndex(da.varsP->frmP, VDictDictsList));
	LstSetListChoices(lstP, da.varsP->items, i);
	LstSetSelection(lstP, sel);
	FrmDrawForm(da.varsP->frmP);
	FrmSetFocus(da.varsP->frmP, FrmGetObjectIndex(da.varsP->frmP, VDictDictsList));
}

static void ChooseDictFinish (Boolean ok)
{
	ListPtr lstP;
	Int16 sel;
	FontID oldFont;
	
	if (ok)
	{
		lstP = FrmGetObjectPtr (da.varsP->frmP,
			FrmGetObjectIndex (da.varsP->frmP, VDictDictsList));
		sel = LstGetSelection (lstP);
		if (sel != noListSelection)
		{
			da.prefsP->use = 1;
			da.prefsP->use <<= 15 - sel;
			
			OpenDict ();
		}
	}
	
	SwitchUI (da.varsP->frmP, 1);
	da.varsP->indic = true;
	FrmDrawForm (da.varsP->frmP);
	FrmSetFocus (da.varsP->frmP, FrmGetObjectIndex (da.varsP->frmP, VDictWordField));
	
	if (ok && sel != noListSelection)
	{
		EventType event;
		event.eType = dicSearchEvent;
		EvtAddEventToQueue (&event);
	}
	else if (!ok && da.si != -1)
	{
		oldFont = FntSetFont (stdFont);
		DrawScreen ();
		FntSetFont (oldFont);
	}
}

static void CaseSwitch ()
{
	FieldPtr fldP = FrmGetObjectPtr (da.varsP->frmP,
		FrmGetObjectIndex (da.varsP->frmP, VDictWordField));
	MemHandle txtH;
	Char *txtP;
	Int8 i;
	EventType event;
	
	if (!da.varsP->indic) return;
	
	if (FldGetTextLength (fldP) == 0) return;
	
	txtH = FldGetTextHandle (fldP);
	txtP = MemHandleLock (txtH);
	
	if (ISLETTER (txtP[0]))
	{
		if (ISLOWER (txtP[0]) && ISLOWER (txtP[1])) // abc -> Abc
		{
			txtP[0] -= 32;
			for (i = 1; txtP[i]; i++)
				if (ISUPPER (txtP[i])) txtP[i] += 32;
		}
		else if (ISUPPER (txtP[0]) && ISLOWER (txtP[1])) // Abc -> ABC
		{
			for (i = 1; txtP[i]; i++)
				if (ISLOWER (txtP[i])) txtP[i] -= 32;
		}
		else // ??? -> abc
		{
			for (i = 0; txtP[i]; i++)
				if (ISUPPER (txtP[i])) txtP[i] += 32;
		}
	}
	
	MemHandleUnlock (txtH);
	FldDrawField (fldP);
	
	event.eType = dicSearchEvent;
	EvtAddEventToQueue (&event);
}


static void ReadIndexHeader (UInt32 pos, HeadPtr headP)
{
	VFSFileSeek (da.varsP->ref, vfsOriginBeginning, pos);
	VFSFileRead (da.varsP->ref, sizeof (HeadType), headP, NULL);
}

static UInt16 ReadIndexNodes (UInt32 pos, NodePtr nodesP, UInt16 idx)
{
	UInt32 bytes;
	UInt16 blockLen, i, num;
	UInt8 *p, *q;
	Int8 bits, shift;
	
	pos += 2; // Skip list header.
	num = idx / READ_INDEX_NUM;
	for (i = 0; i < num; i++)
	{
		VFSFileSeek (da.varsP->ref, vfsOriginBeginning, pos);
		VFSFileRead (da.varsP->ref, sizeof (UInt16), &blockLen, NULL);
		pos += 2 + blockLen;
	}
	
	VFSFileSeek (da.varsP->ref, vfsOriginBeginning, pos);
	VFSFileRead (da.varsP->ref, sizeof (UInt16), &blockLen, NULL);
	VFSFileRead (da.varsP->ref, blockLen, da.idxBufP, NULL);

	// Get keys.
	p = da.idxBufP;
	i = 0;
	while (*p) nodesP[i++].ch = *p++;
	p++;

	num = i;
	bits = num / 8;
	if (num % 8) bits++;
	
	// Get childs.
	q = p + bits;
	shift = 7;
	for (i = 0; i < num; i++)
	{
		nodesP[i].child = 0;
		if (*p >> shift & 0x1)
		{
			bytes = *q++;
			bytes <<= 24;
			nodesP[i].child |= bytes;
			
			bytes = *q++;
			bytes <<= 16;
			nodesP[i].child |= bytes;
			
			bytes = *q++;
			bytes <<= 8;
			nodesP[i].child |= bytes;
			
			nodesP[i].child |= *q++;
		}
		
		shift--;
		if (shift < 0)
		{
			shift = 7;
			p++;
		}
	}
	p = q;
	
	// Get data.
	q = p + bits;
	shift = 7;
	for (i = 0; i < num; i++)
	{
		if (*p >> shift & 0x1)
		{
			nodesP[i].data = 0;

			bytes = *q++;
			bytes <<= 24;
			nodesP[i].data |= bytes;

			bytes = *q++;
			bytes <<= 16;
			nodesP[i].data |= bytes;
			
			bytes = *q++;
			bytes <<= 8;
			nodesP[i].data |= bytes;
			
			nodesP[i].data |= *q++;
		}
		else
			nodesP[i].data = INVALID_DATA;
		
		shift--;
		if (shift < 0)
		{
			shift = 7;
			p++;
		}
	}
	
	return (idx % READ_INDEX_NUM);
}

static void ReadDataBlock (UInt32 pos, UInt8* destP, Int16 blockNo)
{
	Int32 tableIndex = pos & 0xfffff;
	Int16 segOffset = pos >> 20 & 0xfff;
	UInt32 offset;
	UInt16 blockLen, i;
	Err err;
	z_stream *streamP = (z_stream*)(da.idxBufP + 9216);
	UInt8 *p;
	
	segOffset += READ_DATA_SIZE * blockNo;
	i = segOffset / BLOCK_SIZE;
	segOffset %= BLOCK_SIZE;
	tableIndex += i;
	
	if (tableIndex != da.varsP->cache)
	{
		da.varsP->cache = tableIndex;
		
		VFSFileSeek (da.varsP->ref, vfsOriginBeginning,
			da.table + tableIndex * sizeof (UInt32));
		VFSFileRead (da.varsP->ref, sizeof (UInt32), &offset, NULL);
		VFSFileSeek (da.varsP->ref, vfsOriginBeginning, da.data + offset);
		// Read length of compressed block.
		VFSFileRead (da.varsP->ref, sizeof (UInt16), &blockLen, NULL);
		VFSFileRead (da.varsP->ref, blockLen, da.datBufP, NULL);

		streamP->zalloc	= (alloc_func)Z_NULL;
		streamP->zfree	= (free_func)Z_NULL;
		streamP->opaque	= (voidpf)Z_NULL;

		streamP->next_in	= da.datBufP;
		streamP->avail_in	= 0;
		streamP->next_out	= da.ucpBufP;

		err = ZLibinflateinit2 (da.zlibref, streamP,
			15, ZLIB_VERSION, sizeof(z_stream));

		streamP->avail_in	= blockLen;
		streamP->avail_out	= BLOCK_SIZE;

		err = ZLibinflate (da.zlibref, streamP, Z_NO_FLUSH);
		err = ZLibinflateend (da.zlibref, streamP);
		
		if (streamP->total_out < BLOCK_SIZE)
			*(da.ucpBufP + streamP->total_out) = 0;
	}
	
	p = da.ucpBufP + segOffset;
	i = 0;
	while (i < READ_DATA_SIZE && p != da.datBufP && *p)
	{
		*destP++ = *p++;
		i++;
	}
	if (i == READ_DATA_SIZE) return;
	if (*p == 0)
	{
		*destP = 0;
		return;
	}
	
	// Read data in next block.
	
	tableIndex++;
	da.varsP->cache = tableIndex;
	
	VFSFileSeek (da.varsP->ref, vfsOriginBeginning,
		da.table + tableIndex * sizeof (UInt32));
	VFSFileRead (da.varsP->ref, sizeof (UInt32), &offset, NULL);
	VFSFileSeek (da.varsP->ref, vfsOriginBeginning, da.data + offset);
	// Read length of compressed block.
	VFSFileRead (da.varsP->ref, sizeof (UInt16), &blockLen, NULL);
	VFSFileRead (da.varsP->ref, blockLen, da.datBufP, NULL);

	streamP->zalloc	= (alloc_func)Z_NULL;
	streamP->zfree	= (free_func)Z_NULL;
	streamP->opaque	= (voidpf)Z_NULL;

	streamP->next_in	= da.datBufP;
	streamP->avail_in	= 0;
	streamP->next_out	= da.ucpBufP;

	err = ZLibinflateinit2 (da.zlibref, streamP,
		15, ZLIB_VERSION, sizeof(z_stream));

	streamP->avail_in	= blockLen;
	streamP->avail_out	= BLOCK_SIZE;

	err = ZLibinflate (da.zlibref, streamP, Z_NO_FLUSH);
	err = ZLibinflateend (da.zlibref, streamP);
	
	if (streamP->total_out < BLOCK_SIZE)
		*(da.ucpBufP + streamP->total_out) = 0;

	p = da.ucpBufP;
	while (i < READ_DATA_SIZE && *p)
	{
		*destP++ = *p++;
		i++;
	}
	if (*p == 0)
		*destP = 0;
}

static void PrevWord ()
{
	HeadPtr	headP;
	NodePtr	nodeP;
	UInt32	offset;
	UInt16	i;
	
	if (da.si == -1) return;
	if (da.si == 0 && da.stack[da.si].index == 0) return;
	
	while (da.stack[da.si].index == 0)
	{
		da.si--;
		if (da.si == -1) return;
		
		if (da.stack[da.si].data != INVALID_DATA)
		{
			Explain ();
			return;
		}
	}
	
	headP = (HeadPtr)da.indexP;
	nodeP = (NodePtr)(da.indexP + sizeof (HeadType));

	da.stack[da.si].index--;

	offset = da.tree;
	if (da.si > 0)
		offset += da.stack[da.si - 1].child;

	i = ReadIndexNodes (offset, nodeP, da.stack[da.si].index);
	
	da.stack[da.si].child = nodeP[i].child;
	da.stack[da.si].ch = nodeP[i].ch;
	da.stack[da.si].data = nodeP[i].data;
	
	while (nodeP[i].child)
	{
		da.si++;
		offset = da.tree + nodeP[i].child;
		
		ReadIndexHeader (offset, headP);
		da.stack[da.si].index = headP->num - 1;
		i = ReadIndexNodes (offset, nodeP, da.stack[da.si].index);

		da.stack[da.si].child = nodeP[i].child;
		da.stack[da.si].ch = nodeP[i].ch;
		da.stack[da.si].data = nodeP[i].data;
	}
	
	Explain();
}

static void NextWord()
{
	HeadPtr headP;
	NodePtr nodeP;
	UInt32 offset;
	UInt16 i;
	Int8 save;
	
	headP = (HeadPtr)da.indexP;
	nodeP = (NodePtr)(da.indexP + sizeof (HeadType));
	i = 0;
	
	if (da.si == -1) // No word has be entered.
	{
		ReadIndexNodes (da.tree, nodeP, 0);

		da.si = 0;
		da.stack[da.si].child = nodeP->child;
		da.stack[da.si].index = 0;
		da.stack[da.si].ch = nodeP->ch;
		da.stack[da.si].data = nodeP->data;
	}
	else
	{
		if (da.stack[da.si].child == 0)
		{
			// Save search stack.
			save = da.si;
			MemMove (da.idxBufP + 9216, da.stack, sizeof (StackNodeType) * 64);
			
			offset = da.tree;
			if (da.si > 0)
				offset += da.stack[da.si - 1].child;

			ReadIndexHeader (offset, headP);

			da.stack[da.si].index++;
			while (da.stack[da.si].index == headP->num)
			{
				da.si--;
				if (da.si == -1)
				{
					// No next word exist, restore search stack.
					da.si = save;
					MemMove (da.stack, da.idxBufP + 9216, sizeof (StackNodeType) * 64);
					return;
				}
				da.stack[da.si].index++;

				offset = da.tree;
				if (da.si > 0)
					offset += da.stack[da.si - 1].child;

				ReadIndexHeader (offset, headP);
			}
			
			i = ReadIndexNodes (offset, nodeP, da.stack[da.si].index);
			
			da.stack[da.si].child = nodeP[i].child;
			da.stack[da.si].ch = nodeP[i].ch;
			da.stack[da.si].data = nodeP[i].data;
		}
		else
		{
			offset = da.tree + da.stack[da.si].child;
			ReadIndexNodes (offset, nodeP, 0);

			da.si++;
			da.stack[da.si].child = nodeP->child;
			da.stack[da.si].index = 0;
			da.stack[da.si].ch = nodeP->ch;
			da.stack[da.si].data = nodeP->data;
		}
	}
	
	while (nodeP[i].data == INVALID_DATA)
	{
		offset = da.tree + da.stack[da.si].child;
		i = ReadIndexNodes (offset, nodeP, 0);

		da.si++;
		da.stack[da.si].child = nodeP[i].child;
		da.stack[da.si].index = 0;
		da.stack[da.si].ch = nodeP[i].ch;
		da.stack[da.si].data = nodeP[i].data;
	}
	
	Explain ();
}

static void NewWord ()
{
	FieldPtr fldP = FrmGetObjectPtr (da.varsP->frmP,
		FrmGetObjectIndex (da.varsP->frmP, VDictWordField));

	da.si = -1;
	if (FldGetTextLength (fldP))
		FldDelete (fldP, 0, FldGetTextLength (fldP));
	else
		DictMemo ();
}

static void DictMemo ()
{
	FontID oldFont = FntGetFont ();

	VFSFileSeek (da.varsP->ref, vfsOriginBeginning, 24);
	VFSFileRead (da.varsP->ref, da.tree - 24, da.dataP, NULL);
	da.dataP[da.tree - 24] = 0;

	da.varsP->top = 0;
	da.varsP->bot = -1;
	da.varsP->start = 0;
	da.varsP->mid = -1;
	da.varsP->end = -1;
	da.varsP->first = 0;
	da.varsP->phonetic = -1;
	
	FntSetFont (stdFont);
	da.brkLines[0].pos = 0;
	da.varsP->end = BreakLine (0, da.rect.extent.y / FntCharHeight ());
	da.varsP->last = da.varsP->end - 1;
	DrawScreen ();
	FntSetFont (oldFont);
}

static void Search ()
{
	FieldPtr fldP;
	UInt8 *p;
	Int8 i;
	
	fldP = FrmGetObjectPtr (da.varsP->frmP,
		FrmGetObjectIndex (da.varsP->frmP, VDictWordField));
	if (FldGetTextLength (fldP) == 0)
	{
		DictMemo ();
		return;
	}
	
	p = (UInt8*)FldGetTextPtr (fldP);
	i = 0;
	while (*p)
	{
		if (i <= da.si && da.stack[i].ch == *p)
		{
			if (*(p + 1) == 0)
				da.si = i;
		}
		else
		{
			if (i <= da.si)
				da.si = i - 1;
			
			if (!SearchChar (*p)) break;
		}
		
		i++;
		p++;
	}
	
	if (da.si >= 0)
	{
		Explain ();

		if (*p && !AlreadyFound (p, p - (UInt8*)FldGetTextPtr (fldP)))
		{
			if (FindNextWord ())
				Explain ();
		}
	}
	else
	{
		if (FindNextWord ())
			Explain ();
	}
}

static Boolean SearchChar (UInt16 ch)
{
	HeadPtr headP = (HeadPtr)da.indexP;
	NodePtr nodeP = (NodePtr)(da.indexP + sizeof (HeadType));
	UInt32 offset;
	Int16 base, upperBound, first, mid, last;
	
	offset = da.tree;
	if (da.si >= 0)
	{
		if (da.stack[da.si].child == 0)
		{
			da.near = 0;
			return false;
		}
		offset += da.stack[da.si].child;
	}

	// First read head node.
	ReadIndexHeader (offset, headP);
	// Read index nodes.
	ReadIndexNodes (offset, nodeP, 0);

	base = 0;
	// If index nodes more than 100, I must insure the target char in the index buffer.
	if (headP->num > READ_INDEX_NUM)
	{
		upperBound = (headP->num - base > READ_INDEX_NUM) ?
			READ_INDEX_NUM - 1 : headP->num - base - 1;
		while (ch > nodeP[upperBound].ch && base + upperBound + 1 < headP->num)
		{
			base += READ_INDEX_NUM;
			ReadIndexNodes (offset, nodeP, base);
			upperBound = (headP->num - base > READ_INDEX_NUM) ?
				READ_INDEX_NUM - 1 : headP->num - base - 1;
		}
		if (base + upperBound + 1 == headP->num)
		{
			da.near = 0;
			return false;
		}
	}
	else
		upperBound = headP->num - 1;
	
	// Find char in index buffer with binary search.
	first = 0;
	last = upperBound;
	while (first <= last)
	{
		mid = (first + last) / 2;
		if (mid < first)
			break;
		
		if (ch < nodeP[mid].ch)
			last = mid - 1;
		else if (ch > nodeP[mid].ch)
			first = mid + 1;
		else
		{
			da.si++;
			da.stack[da.si].child = nodeP[mid].child;
			da.stack[da.si].index = base + mid;
			da.stack[da.si].ch = ch;
			da.stack[da.si].data = nodeP[mid].data;
			return true;
		}
	}
	// I don't found it, but I keep the info for the next word.
	if (first > upperBound)
	{
		da.near = 0;
	}
	else
	{
		da.near = da.si + 1;
		da.stack[da.near].child = nodeP[first].child;
		da.stack[da.near].index = base + first;
		da.stack[da.near].ch = nodeP[first].ch;
		da.stack[da.near].data = nodeP[first].data;
		da.near = 1;
	}
	return false;
}

static Boolean AlreadyFound (UInt8* wordP, Int8 offset)
{
	UInt8 *p;
	
	if (StrLen ((Char*)da.wordP) <= offset) return false;
	
	p = da.wordP + offset;
	while (*p == *wordP && *p && *wordP)
	{
		p++;
		wordP++;
	}
	
	if (*wordP == 0) return true;
	return false;
}

static Boolean FindNextWord ()
{
	UInt16 i;
	UInt32 offset;
	HeadPtr headP;
	NodePtr nodeP;
	
	if (da.near == 0)
	{
		// I don't found the next word in this level, return to up level.
		if (da.si < 0) return false;
		
		do {
			i = da.stack[da.si--].index + 1;
			offset = da.tree;
			if (da.si >= 0)
				offset += da.stack[da.si].child;
			headP = (HeadPtr)da.indexP;
			ReadIndexHeader (offset, headP);
		} while (i == headP->num && da.si != -1);
		if (i == headP->num && da.si == -1) return false;

		nodeP = (NodePtr)da.indexP;
		i = ReadIndexNodes (offset, nodeP, i);
		
		da.near = da.si + 1;
		da.stack[da.near].child = nodeP[i].child;
		da.stack[da.near].index = 0;
		da.stack[da.near].ch = nodeP[i].ch;
		da.stack[da.near].data = nodeP[i].data;
	}
	
	da.si++;
	while (da.stack[da.si].data == INVALID_DATA)
	{
		offset = da.tree + da.stack[da.si].child;
		nodeP = (NodePtr)da.indexP;
		ReadIndexNodes (offset, nodeP, 0);

		da.si++;
		da.stack[da.si].child = nodeP->child;
		da.stack[da.si].index = 0;
		da.stack[da.si].ch = nodeP->ch;
		da.stack[da.si].data = nodeP->data;
	}
	return true;
}

static void Explain ()
{
	UInt8 *p, *q;
	Int8 i;
	
	WinEraseRectangle (&da.rect, 0);
	if (da.stack[da.si].data == INVALID_DATA) return;

	da.varsP->top = 0;
	da.varsP->bot = -1;
	da.varsP->start = -1;
	da.varsP->mid = -1;
	da.varsP->end = -1;
	da.varsP->first = 0;
	da.varsP->last = 0;
	da.varsP->dpos = da.stack[da.si].data;
	
	ReadDataBlock (da.varsP->dpos, da.dataP, 0);
	
	// Get letters from search stack.
	p = da.wordP;
	for (i = 0; i <= da.si; i++)
		*p++ = da.stack[i].ch & 0xff;
	
	q = da.dataP;
	if (*q == 1) // Get additional letters.
	{
		q++;
		while (*q != 2 && *q != 3 && *q)
			*p++ = *q++;
	}
	*p = 0;
	
	// Get phonetic.
	p = da.phoneticP;
	if (*q == 2)
	{
		q++;
		while (*q != 3 && *q)
			*p++ = *q++;
	}
	*p = 0;
	
	PgDown ();
}

static void LoadNextBlock (Boolean shift, Int16* endP)
{
	Int16 i, j;
	
	if (shift)
	{
		// Reach the bottom of data buffer.
		// Shift break lines array up.
		for (i = da.varsP->start, j = da.varsP->mid; j <= *endP; i++, j++)
		{
			da.brkLines[i].pos = da.brkLines[j].pos - READ_DATA_SIZE;
			da.brkLines[i].len = da.brkLines[j].len;
		}
		*endP = i - 1;

		// Shift data buffer and load the next data block.
		da.varsP->top = da.varsP->bot;
		da.varsP->bot++;
		MemMove (da.dataP, da.varsP->midP, READ_DATA_SIZE);
		ReadDataBlock (da.varsP->dpos, da.varsP->midP, da.varsP->bot);
	}
	else
	{
		// Reach the middle of data buffer.
		if (da.varsP->bot != da.varsP->top + 1)
		{
			// Must load the next data block.
			da.varsP->bot = da.varsP->top + 1;
			ReadDataBlock (da.varsP->dpos, da.varsP->midP, da.varsP->bot);
		}
	}
}

static Int16 GetPhrase (UInt8* textP, Boolean* shiftP, Int16* endP)
{
	// Kind: 1 - Letter, 2 - Number, 3 - Misc
	UInt8 *p = textP, kind, len, step, k;
	
	if (shiftP) *shiftP = false;
	if (p == da.varsP->midP || p + 1 == da.varsP->midP)
	{
		LoadNextBlock (false, NULL);
	}
	else if (p == da.varsP->botP || p + 1 == da.varsP->botP)
	{
		if (shiftP) *shiftP = true;
		LoadNextBlock (true, endP);
		p -= READ_DATA_SIZE;
	}
	
	kind = 3;
	if (*p < 0x81)
	{
		if (ISLETTER (*p)) kind = 1;
		else if (ISNUMBER (*p)) kind = 2;
	}
	
	len = 0;
	while (*p && *p > 3)
	{
		step = (*p < 0x81) ? 1 : 2;
		len += step;
		p += step;
		// Keep all data in buffer.
		if (p == da.varsP->midP || p + 1 == da.varsP->midP)
		{
			LoadNextBlock (false, NULL);
		}
		else if (p == da.varsP->botP || p + 1 == da.varsP->botP)
		{
			if (shiftP) *shiftP = true;
			LoadNextBlock (true, endP);
			p -= READ_DATA_SIZE;
		}
		
		k = 3;
		if (*p < 0x81)
		{
			if (ISLETTER (*p)) k = 1;
			else if (ISNUMBER (*p)) k = 2;
		}
		
		if (k == 3) break;
		if (k != kind) break;
	}
	return len;
}

static Int16 SplitLongWord (UInt8* textP, Int16* lenP, Int16 maxWidth)
{
	Int16 w, c, l;
	
	for (l = w = 0; l < *lenP; l++)
	{
		c = FntCharWidth (*((Char*)textP + l));
		if (w + c > maxWidth)
			break;
		else
			w += c;
	}
	*lenP = l;
	return w;
}

// Break paragraph to several lines.
static Int16 BreakLine (
	Int16 from,		// Index of lines array.
	Int8 lines		// Maximum lines to be broke.
	)
{
	UInt8 *p;
	Boolean shift, inup;
	Int16 len, width, w, i, move;
	
	p = da.dataP + da.brkLines[from].pos;
	i = from;
	da.brkLines[i].len = 0;
	width = 0;
	inup = (p < da.varsP->midP) ? true : false;
	while (true)
	{
		move = i;
		len = GetPhrase (p, &shift, &move);
		if (len == 0) break;
		
		if (shift)
		{
			from -= (i - move);
			i = move;
			p -= READ_DATA_SIZE;
			inup = true;
		}

		w = FntCharsWidth ((Char*)p, len);
		if (w > da.rect.extent.x && da.brkLines[i].len == 0)
		{
			w = SplitLongWord (p, &len, da.rect.extent.x);
		}
		if ((width + w > da.rect.extent.x) || *p == 0xa)
		{
			i++;
			if (inup && p >= da.varsP->midP)
			{
				da.varsP->mid = i;
				da.numLines[da.varsP->top].lines = i - da.varsP->start;
				da.numLines[da.varsP->top].pos = da.brkLines[da.varsP->start].pos;
				inup = false;
			}
			
			width = 0;
			if (*p == 0xa) p++;
			da.brkLines[i].pos = p - da.dataP;
			da.brkLines[i].len = 0;
			if (i - from == lines) break;
		}
		else
		{
			width += w;
			da.brkLines[i].len += len;
			p += len;
		}
	}
	if (i - from < lines && da.brkLines[i].len)
	{
		i++;
		da.brkLines[i].pos = p - da.dataP;
		da.brkLines[i].len = 0;
	}
	return i;
}

static void DrawScreen ()
{
	Coord x, y;
	Int16 i, j;
	Char *p;
	
	WinEraseRectangle (&da.rect, 0);
	
	y = da.rect.topLeft.y;
	for (i = da.varsP->first; i <= da.varsP->last; i++)
	{
		p = (Char*)da.dataP + da.brkLines[i].pos;
		x = da.rect.topLeft.x;

		if (i == da.varsP->phonetic)
			FntSetFont (phoneticFont);
		else if (i == da.varsP->start)
			FntSetFont (stdFont);
		
		if (i >= da.varsP->phonetic && i < da.varsP->start)
		{
			for (j = 0; j < da.brkLines[i].len; j++)
			{
				WinDrawChars (p + j, 1, x, y);
				x += FntCharWidth (p[j]);
			}
		}
		else
		{
			WinDrawChars (p, da.brkLines[i].len, x, y);
		}

		y += FntCharHeight ();
	}
}

static void PgUp ()
{
	UInt8 s;
	FontID oldFont = FntGetFont ();
	Int16 i, j, k;
	
	//if (da.si == -1) return;
	
	if (da.varsP->first == 0) return;
	
	FntSetFont (stdFont);
	
	s = da.rect.extent.y / FntCharHeight ();
	da.varsP->first -= s;	
	if (da.varsP->first < da.varsP->start)
	{
		if (da.varsP->top == 0)
		{
			if (da.varsP->first < 0)
				da.varsP->first = 0;
		}
		else
		{
			// Shift break lines array down.
			da.varsP->bot = da.varsP->top;
			da.varsP->top--;
			j = da.varsP->mid;
			da.varsP->mid = da.varsP->start + da.numLines[da.varsP->top].lines;
			da.varsP->end = da.varsP->mid + da.numLines[da.varsP->bot].lines;
			k = da.varsP->end;
			for (i = 0; i <= da.numLines[da.varsP->bot].lines; i++, j--, k--)
			{
				da.brkLines[k].pos = da.brkLines[j].pos + READ_DATA_SIZE;
				if (da.brkLines[k].pos >= 1024)
					da.varsP->end--;
				else
					da.brkLines[k].len = da.brkLines[j].len;
			}
			da.varsP->first += da.numLines[da.varsP->top].lines;
			
			MemMove (da.varsP->midP, da.dataP, READ_DATA_SIZE);
			// Load data block to up buffer.
			ReadDataBlock (da.varsP->dpos, da.dataP, da.varsP->top);
			
			i = da.brkLines[da.varsP->mid].len;
			da.brkLines[da.varsP->start].pos = da.numLines[da.varsP->top].pos;
			BreakLine (da.varsP->start, da.numLines[da.varsP->top].lines);
			da.brkLines[da.varsP->mid].len = i;
		}
		da.varsP->last = da.varsP->first + s - 1;
		if (da.varsP->last >= da.varsP->end)
			da.varsP->last = da.varsP->end - 1;
	}
	else
		da.varsP->last -= s;
	
	DrawScreen ();
	FntSetFont (oldFont);
}

static void PgDown ()
{
	UInt8	*p;
	Int16	i, j, k;
	FontID	oldFont = FntGetFont();
	
	//if (da.si == -1) return;
	
	FntSetFont(stdFont);
	
	if (da.varsP->start == -1) // First loads data.
	{
		// Break word.
		// i(width total) j(len) k(char width)
		da.varsP->start = 0;
		da.brkLines[0].pos = da.wordP - da.dataP;
		da.brkLines[0].len = 0;
		p = da.wordP;
		i = 0;
		while (*p)
		{
			j = (*p < 0x81) ? 1 : 2;
			k = FntCharsWidth ((Char*)p, j);
			if (i + k > da.rect.extent.x)
			{
				i = 0;
				da.varsP->start++;
				da.brkLines[da.varsP->start].pos = p - da.dataP;
				da.brkLines[da.varsP->start].len = 0;
			}
			else
			{
				i += k;
				da.brkLines[da.varsP->start].len += j;
				p += j;
			}
		}
		da.varsP->start++;
		
		da.varsP->phonetic = da.varsP->start;
		// Break phonetic.
		if (*da.phoneticP)
		{
			FntSetFont (phoneticFont);

			da.brkLines[da.varsP->start].pos = da.phoneticP - da.dataP;
			da.brkLines[da.varsP->start].len = 0;
			p = da.phoneticP;
			i = 0;
			while (*p)
			{
				j = FntCharWidth (*p);
				if (i + j > da.rect.extent.x)
				{
					i = 0;
					da.varsP->start++;
					da.brkLines[da.varsP->start].pos = p - da.dataP;
					da.brkLines[da.varsP->start].len = 0;
				}
				else
				{
					i += j;
					da.brkLines[da.varsP->start].len++;
					p++;
				}
			}
			da.varsP->start++;

			FntSetFont (stdFont);
		}
		
		// Add a space line between word and meaning.
		da.brkLines[da.varsP->start++].len = 0;
		
		// Now da.varsP->start's value was never be changed until next data of word
		// has be loaded.
		
		// Get beginning of text. All word must have a meaning.
		p = da.dataP;
		while (*p != 3) p++;
		p++;
		// i(lines of display)
		i = da.rect.extent.y / FntCharHeight ();
		da.brkLines[da.varsP->start].pos = p - da.dataP;
		da.varsP->end = BreakLine (da.varsP->start, i - da.varsP->start);
		da.varsP->last = da.varsP->end - 1;
	}
	else
	{
		// i(block no.) j(offset) k(lines of display)
		if (da.varsP->mid == -1)
		{
			i = 0;
			j = da.varsP->last - da.varsP->start;
		}
		else
		{
			if (da.brkLines[da.varsP->last].pos >= da.brkLines[da.varsP->mid].pos)
			{
				i = da.varsP->bot;
				j = da.varsP->last - da.varsP->mid;
			}
			else
			{
				i = da.varsP->top;
				j = da.varsP->last - da.varsP->start;
			}
		}
		
		k = da.rect.extent.y / FntCharHeight ();
		da.varsP->last += k;
		if (da.varsP->last >= da.varsP->end)
		{
			da.varsP->end = BreakLine (da.varsP->end,
				da.varsP->last - da.varsP->end + 1);

			if (da.varsP->top == i)
				da.varsP->last = da.varsP->start + j;
			else
				da.varsP->last = da.varsP->mid + j;

			da.varsP->last += k;
			if (da.varsP->last >= da.varsP->end)
				da.varsP->last = da.varsP->end - 1;
		}
		da.varsP->first = da.varsP->last - k + 1;
		if (da.varsP->first < 0)
			da.varsP->first = 0;
	}
	
	DrawScreen();
	FntSetFont(oldFont);
}

static void VDictInit(FormPtr frmP)
{
	if (useDA && da.indexP == NULL)
	{
		// Allocate memory once.
		da.indexP	= MemPtrNew(4608);
		da.idxBufP	= MemPtrNew(10240);

		MemSet(da.indexP, 4608, 0);
		
		// Initialize.
		da.dataP		= da.indexP + 1024;
		da.wordP		= da.indexP + 2050;
		da.phoneticP	= da.indexP + 2116;
		da.varsP		= (VarsPtr)(da.indexP + 2182);
		da.numLines		= (BlockPtr)(da.indexP + 2304);
		da.brkLines		= (LinePtr)(da.indexP + 2560);
		da.stack		= (StackNodePtr)(da.indexP + 3328);
		da.si			= -1;
		da.prefsP		= (VDictPrefsPtr)(da.indexP + 4096);
		da.varsP->indic	= true;
		da.varsP->midP	= da.dataP + 512;
		da.varsP->botP	= da.dataP + 1024;
		
		da.varsP->cache	= -1;
		da.ucpBufP		= da.idxBufP + 1024;
		da.datBufP		= da.idxBufP + 5120;

		if (!g_expansion || !g_haszlib)
		{
			useDA = false;
		}
		else
		{
			UInt16 size;
			
			da.zlibref= ZLibRef;
			da.varsP->vol = GetVolRef();
			da.varsP->frmP = frmP;
			
			FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, VDictMeanGadget), &da.rect);

			// Load preferences.
			size = sizeof(VDictPrefsType);
			if (PrefGetAppPreferences (
				appFileCreator, appVDictPrefID,
				da.prefsP, &size, true) != noPreferenceFound &&
				size == sizeof(VDictPrefsType))
			{
				if (!OpenDict())
				{
					if (!ScanDict() || !OpenDict())
						useDA = false;
				}
			}
			else
			{
				MemSet(da.prefsP, sizeof(VDictPrefsType), 0);
				if (!ScanDict() || !OpenDict())
					useDA = false;
			}
		}
		
		if (!useDA)
		{
			MemPtrFree(da.indexP);
			MemPtrFree(da.idxBufP);
		}
	}
}

static void VDictFormInit(FormPtr frmP)
{
	FieldPtr	fldP;
	MemHandle	oldH;
	
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, VDictWordField));
	oldH = FldGetTextHandle(fldP);
	FldSetTextHandle(fldP, wordH);
	wordH = oldH;
	
	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, VDictWordField));
}

Boolean VDictFormHandleEvent(EventType* eventP)
{
	Boolean	handled = false;
	FormPtr	frmP;
	
	switch (eventP->eType)
	{
	case menuEvent:
		switch (eventP->data.menu.itemID)
		{
		case F5DictChoose:
			ChooseDict();
			break;
		
		case F5DictRefresh:
			ScanDict();
			OpenDict();
			Search();
			break;
		
		case F5DictCaseSwitch:
			CaseSwitch();
			break;
		}
		handled = true;
		break;
	
	case frmOpenEvent:
		frmP = FrmGetActiveForm();
		VDictInit(frmP);
		if (!useDA)
		{
			FrmAlert(MissDicAlert);
			FrmGotoForm(ViewForm);
		}
		else
		{
			VDictFormInit(frmP);
			FrmDrawForm(frmP);
			Search();
		}
		handled = true;
		break;
	
	case keyDownEvent:
		switch (eventP->data.keyDown.chr)
		{
		case vchrPageUp:
			if (da.varsP->indic)
				PgUp();
			handled = true;
			break;
			
		case vchrPageDown:
			if (da.varsP->indic)
				PgDown();
			handled = true;
			break;
		}
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case VDictCloseButton:
			FrmGotoForm(ViewForm);
			break;

		case VDictPrevButton:
			PrevWord();
			break;

		case VDictNextButton:
			NextWord();
			break;
		
		case VDictCaseButton:
			CaseSwitch();
			break;

		case VDictNewButton:
			NewWord();
			break;
		
		case VDictOKButton:
			ChooseDictFinish(true);
			break;
			
		case VDictCancelButton:
			ChooseDictFinish(false);
			break;
		}
		handled = true;
		break;

	case fldChangedEvent:
	case dicSearchEvent:
		if (da.varsP->indic)
			Search();
		handled = true;
		break;
	}
	
	return handled;
}

void VDictPrepare()
{
	useDA = true;
	MemSet(&da, sizeof(DADataType), 0);

	#ifdef PALMOS_50
	fontH = DmGetResource('nfnt', 3000);
	#else
	fontH = DmGetResource('NFNT', 3000);
	#endif
	
	FntDefineFont(phoneticFont, (FontPtr)MemHandleLock(fontH));
	
	wordH = NULL;
}

void VDictRelease()
{
	MemHandleUnlock(fontH);
	DmReleaseResource(fontH);
	
	if (wordH)
		MemHandleFree(wordH);

	if (useDA)
	{
		// Save preferences.
		PrefSetAppPreferences (
			appFileCreator, appVDictPrefID, appVDictPrefVersion,
			da.prefsP, sizeof(VDictPrefsType), true);

		if (da.indexP)
			MemPtrFree(da.indexP);
		if (da.idxBufP)
			MemPtrFree(da.idxBufP);
	}
}

void VDictSetWordToFind(Char *wordP, Int16 len)
{
	Char *p;
	
	if (wordH)
		MemHandleFree(wordH);
	
	wordH = MemHandleNew(len + 1);
	p = MemHandleLock(wordH);
	StrNCopy(p, wordP, len);
	p[len] = 0;
	MemHandleUnlock(wordH);
}
