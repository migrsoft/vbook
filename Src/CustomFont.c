#include "VBook.h"
#include <PceNativeCall.h>
#include <PalmDisplayExtent.h>
#include "LCD.h"
#include "CustomFont.h"
#include "DataType.h"
#include "Platform.h"

// ********************************************************************
// Internal Structures
// ********************************************************************
#pragma mark === Internal Structures ===

typedef struct {
	UInt8	start;
	UInt8	end;
} RangeType, *RangePtr;

typedef struct {
	Char		charSet[10];
	UInt8		size;
	UInt8		minSize;

	RangeType	wcodeRange;
	UInt8		codeAreaNum;
	RangePtr	codeRangeP;

	UInt16		cardNo;
	LocalID		dbID;
	Char		fileName[30];
} CustomFontType, *CustomFontPtr;

enum {frASCII=240, frUNDEFINED=250} FindResult;

// ********************************************************************
// Internal Constants
// ********************************************************************
#pragma mark === Internal Constants ===

#define FONT_TYPE			'Font'
#define FONT_CREATOR		'VBok'
#define FONT_PATH			"/PALM/PROGRAMS/VBook"

// ********************************************************************
// Internal Variables
// ********************************************************************
#pragma mark === Internal Variables ===

static UInt16			rowBytes;
static Boolean			useCustomFont = false;
static FontID			sysFontID;
static Int8				currentFont = -1;
// Maximum support 4 custom fonts.
static CustomFontType	fontList[4];
static Char				fontNameList[8][12];
static Int8				numFonts;
static UInt8			charAdjust, charTabWidth;

static UInt16		fontVolRef;
static DmOpenRef	fontDbRef = 0;
static FileRef		fontFileRef = 0;
static UInt8**		fontRecordList = NULL;
static UInt8		maxFontRecords = 0;

static void (*DrawHZ)(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,
	UInt8* displayP, Coord x, Coord y,
	Coord x1, Coord x2, Coord y1, Coord y2,
	UInt8 textColor, UInt8 backColor);


#ifdef USE_ARM

DrawCharParamPtr	l_dcParamP = NULL;
MemHandle			l_funcH = NULL;
MemPtr				l_funcP = NULL;
UInt32				l_cpuType;

#endif


static BitmapType*		l_bmpFontP		= NULL;
static BitmapTypeV3*	l_bmpFontHiP	= NULL;

// 使用 PalmOne 的直接写屏

static UInt16	l_dexRefNum = sysInvalidRefNum;


// ********************************************************************
// Internal Functions
// ********************************************************************
#pragma mark === Internal Functions ===

static void CleanFontList()
{
	UInt8	i;
	
	for (i=0; i < 4; i++)
	{
		if (fontList[i].codeRangeP)
		{
			MemPtrFree(fontList[i].codeRangeP);
			fontList[i].codeRangeP = NULL;
		}
	}
}

static void GetCustomFontInfo(CustomFontPtr fontP, UInt8 *infoP)
{
	UInt8	*tempP = infoP;
	UInt8	i;

	StrCopy(fontP->charSet, (Char*)tempP);
	tempP += 10;
	fontP->size = *tempP++;
	fontP->minSize = *tempP++;
	fontP->wcodeRange.start = *tempP++;
	fontP->wcodeRange.end = *tempP++;
	fontP->codeAreaNum = *tempP++;
	
	if (fontP->codeAreaNum > maxFontRecords)
		maxFontRecords = fontP->codeAreaNum;
	
	fontP->codeRangeP = (RangePtr)MemPtrNew(sizeof(RangeType) * fontP->codeAreaNum);

	for (i=0; i < fontP->codeAreaNum; i++)
	{
		fontP->codeRangeP[i].start = *tempP++;
		fontP->codeRangeP[i].end = *tempP++;
	}
}

// Search fonts store in handheld and expansion card.
// And keep only 4 fonts in font list even if there have more fonts.
static void SearchCustomFont()
{
	Err					err;
	DmSearchStateType	state;
	UInt16				cardNo;
	LocalID				dbID;
	DmOpenRef			openRef;
	LocalID				appInfoID;
	MemPtr				appInfoP;

	UInt32				iterator;
	FileRef				dirRef;
	FileRef				fileRef;
	FileInfoType		fileInfo;
	UInt32				type;
	UInt32				creator;
	MemHandle			appInfoH;
	Char				name[30];
	Char				extName[5];
	Char				fullPath[60];
	Int16				pos;
	Int16				ext;

	UInt8				i = 0;

	// First, searching in handheld.
	err = DmGetNextDatabaseByTypeCreator(true,
		&state, FONT_TYPE, FONT_CREATOR, false, &cardNo, &dbID);
	while (err == errNone)
	{
		fontList[i].cardNo = cardNo;
		fontList[i].dbID = dbID;
		fontList[i].fileName[0] = 0;

		openRef = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);
			
		appInfoID = DmGetAppInfoID(openRef);
		appInfoP = MemLocalIDToLockedPtr(appInfoID, cardNo);
		GetCustomFontInfo(&fontList[i], (UInt8*)appInfoP);
		MemPtrUnlock(appInfoP);
		DmCloseDatabase(openRef);

		if (++i == 4) break;

		err = DmGetNextDatabaseByTypeCreator(false,
				&state, FONT_TYPE, FONT_CREATOR, false, &cardNo, &dbID);
	}
	
	if (i == 4) return;
	if (!g_expansion) return;
	
	// Second, searching in expansion card.
	iterator = vfsIteratorStart;
	VFSVolumeEnumerate(&fontVolRef, &iterator);
	if (fontVolRef == vfsInvalidVolRef) return;

	err = VFSFileOpen(fontVolRef, FONT_PATH, vfsModeRead, &dirRef);
	if (err != errNone) return;

	StrCopy(fullPath, FONT_PATH);
	pos = StrLen(fullPath);
	fullPath[pos] = '/';
	fullPath[++pos] = 0;
	
	fileInfo.nameP = name;
	fileInfo.nameBufLen = 30;
	iterator = expIteratorStart;
	while (iterator != expIteratorStop)
	{
		err = VFSDirEntryEnumerate(dirRef, &iterator, &fileInfo);
		if (err != errNone) break;
		if (fileInfo.attributes & vfsFileAttrDirectory ||
			fileInfo.attributes & vfsFileAttrSystem ||
			fileInfo.attributes & vfsFileAttrVolumeLabel)
			continue;
		
		ext = StrLen(name) - 4;
		if (ext <= 0) continue;
		StrToLower(extName, &name[ext]);
		if (StrCompare(extName, ".pdb")) continue;

		StrCopy(&fullPath[pos], name);
		err = VFSFileOpen(fontVolRef, fullPath, vfsModeRead, &fileRef);
		if (err != errNone) continue;

		type = creator = 0;
		err = VFSFileDBInfo(fileRef, NULL,
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			&type, &creator, NULL);
		if (err != errNone || type != FONT_TYPE || creator != FONT_CREATOR)
		{
			VFSFileClose(fileRef);
			continue;
		}
		
		VFSFileDBInfo(fileRef, NULL,
			NULL, NULL, NULL, NULL, NULL, NULL, &appInfoH, NULL,
			NULL, NULL, NULL);

		fontList[i].cardNo = 0;
		fontList[i].dbID = 0;
		StrCopy(fontList[i].fileName, name);

		appInfoP = MemHandleLock(appInfoH);
		GetCustomFontInfo(&fontList[i], (UInt8*)appInfoP);
		MemHandleUnlock(appInfoH);
		MemHandleFree(appInfoH);
		VFSFileClose(fileRef);
		
		if (++i == 4) break;
	}
	VFSFileClose(dirRef);
}

static void GenerateFontNameList()
{
	Int8 i;
	
	StrCopy(fontNameList[0], "Standard");
	StrCopy(fontNameList[1], "Bold");
	StrCopy(fontNameList[2], "Large");
	StrCopy(fontNameList[3], "Large Bold");
	
	for (i = 0; i < 4; i++)
	{
		if (fontList[i].cardNo == 0 && fontList[i].dbID == 0 &&
			fontList[i].fileName[0] == 0)
			break;

		StrPrintF(fontNameList[4 + i], "%s %d", fontList[i].charSet, fontList[i].size);
	}
	
	numFonts = 4 + i;
}

static void OpenCustomFont(UInt8 index)
{
	Err			err;
	UInt8		i;
	UInt16		recordNum;
	MemHandle	record;
	Char		fullPath[60];
	
	if (fontList[index].dbID)
	{
		// Open font in handheld.
		fontDbRef = DmOpenDatabase(
			fontList[index].cardNo, fontList[index].dbID, dmModeReadOnly);

		recordNum = DmNumRecords(fontDbRef);
		for (i=0; i < recordNum; i++)
		{
			record = DmQueryRecord(fontDbRef, i);
			fontRecordList[i] = (UInt8*)MemHandleLock(record);
		}
	}
	else
	{
		// Open font in expansion card.
		StrCopy(fullPath, FONT_PATH);
		i = StrLen(fullPath);
		fullPath[i++] = '/';
		StrCopy(&fullPath[i], fontList[index].fileName);

		err = VFSFileOpen(fontVolRef, fullPath, vfsModeRead, &fontFileRef);
		// Don't load records, only load record when need it.
	}
}

static void CloseCustomFont(UInt8 index)
{
	UInt8	i;

	if (fontList[index].dbID)
	{
		// Close font in handheld.
		for (i=0; i < maxFontRecords; i++)
		{
			if (fontRecordList[i])
			{
				MemPtrUnlock(fontRecordList[i]);
				fontRecordList[i] = NULL;
			}
		}
		DmCloseDatabase(fontDbRef);
		fontDbRef = 0;
	}
	else
	{
		// Close font in expansion card.
		for (i=0; i < maxFontRecords; i++)
		{
			if (fontRecordList[i])
			{
				MemPtrUnlock(fontRecordList[i]);
				MemPtrFree(fontRecordList[i]);
				fontRecordList[i] = NULL;
			}
		}
		VFSFileClose(fontFileRef);
		fontFileRef = 0;
	}
	
	currentFont = -1;
}

static UInt8 FindCodeInRange(UInt8* chars)
{
	UInt8	index = frASCII;
	UInt8	i;
	
	if (*chars <= 0x80)
		return index;

	index = frUNDEFINED; // Character not in current charset.
	// GB2312: 0xa1 - 0xfe
	//    GBK: 0x40 - 0xfe
	if (*(chars+1) < fontList[currentFont].wcodeRange.start)
		return index;

	// Character in current charset.
	for (i=0; i < fontList[currentFont].codeAreaNum; i++)
	{
		if (*chars >= fontList[currentFont].codeRangeP[i].start &&
			*chars <= fontList[currentFont].codeRangeP[i].end)
		{
			index = i;
			break;
		}
	}
	if (i == fontList[currentFont].codeAreaNum) return index;
	
	// Load font data saved in expansion card if need.
	if (fontFileRef && fontRecordList[index] == NULL)
	{
		Err			err;
		MemHandle	record;
		
		err = VFSFileDBGetRecord(fontFileRef, index, &record, NULL, NULL);
		fontRecordList[index] = MemHandleLock(record);
	}
	
	return index;
}

#ifdef PALMOS_50

#else

static asm void DrawHZ_1_asm(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,	// Font
	UInt8* displayP, Coord x, Coord y,			// LCD
	Coord x1, Coord x2, Coord y1, Coord y2,		// Clip
	UInt8 textColor, UInt8 backColor)
{
	fralloc +
	movem.l d0-d7/a0-a6, -(sp)
	
	move.l	dotsP, a0

	move.w	y, d0
	mulu.w	rowBytes, d0
	move.l	displayP, a1
	adda.l	d0, a1			// displayP + rowBytes * y
	move.w	x, d0
	move.l	#16, d1
	divu.w	d1, d0
	move.l	d0, d1
	andi.l	#0xffff, d1
	lsl.l	#1, d1
	adda.l	d1, a1
	move.l	d0, d1
	move.l	#16, d0
	lsr.l	d0, d1			// shift

	clr.l	d0
	move.b	size, d0
	subi.b	#1, d0
	clr.l	d2				// yOffset
	move.w	y, d2
	move.l	#8, d4			// available dots

loop:
	clr.l	d3				// line dots
	move.b	size, d5		// need dots
	
get_dots:
	move.b	(a0), d6		// move one byte to buffer
	// clean unused dots
	move.l	#8, d7
	sub.l	d4, d7
	lsl.b	d7, d6
	lsr.b	d7, d6
	
	cmp.b	d4, d5
	bcc		need_dots
	sub.b	d5, d4
	lsr.b	d4, d6
	or.b	d6, d3
	bra		end_getdots
	
need_dots:
	or.b	d6, d3
	sub.b	d4, d5
	cmpi.b	#8, d5
	bcc		move_byte
	lsl.l	d5, d3
	bra		next
move_byte:
	lsl.l	#8, d3
next:
	adda.l	#1, a0
	move.l	#8, d4
	cmpi.b	#0, d5
	beq		end_getdots
	bra		get_dots

end_getdots:
	move.l	#32, d5
	sub.b	size, d5
	lsl.l	d5, d3
	// finish get a line of dots of hanzi.

	cmp.w	y1, d2
	bcs		nodraw
	cmp.w	y2, d2
	bhi		end
	
	lsr.l	d1, d3
	eor.l	d3,	(a1)

nodraw:
	addi.l	#1, d2
	adda.w	rowBytes, a1
	dbra	d0, loop

end:
	movem.l (sp)+, d0-d7/a0-a6
	frfree
	rts
}

static asm void DrawHZ_8_asm(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,	// Font
	UInt8* displayP, Coord x, Coord y,			// LCD
	Coord x1, Coord x2, Coord y1, Coord y2,		// Clip
	UInt8 textColor, UInt8 backColor)
{
	fralloc +
	movem.l d0-d7/a0-a6, -(sp)
	
	move.l	dotsP, a0		// a0: data buffer
	
	move.w	y, d0
	mulu.w	rowBytes, d0
	move.l	displayP, a1	// a1: screen buffer
	adda.l	d0, a1
	adda.w	x, a1			// a1 = y * rowBytes + x
	
	move.l	a1, a2
	
	clr.l	d0
	move.w	dotsNum, d0
	subi.w	#1, d0
	
	clr.l	d1	// offset of x
	clr.l	d2	// offset of y
	
	clr.l	d3	// shift counter
	move.b	#7, d3

loop_d0_8:

	btst	d3, (a0)
	beq		shift_d3_8
	
	move.w	y, d4
	add.w	d2, d4
	cmp.w	y1, d4
	bcs		shift_d3_8	// y + offset < y1
	
	move.w	x, d4
	add.w	d1, d4
	cmp.w	x1, d4
	bcs		shift_d3_8	// x + offset < x1
	
	cmp.w	x2, d4
	bhi		shift_d3_8	// x + offset > x2
	
	move.b	textColor, (a2)
	
shift_d3_8:

	adda.w	#1, a2

	subi.b	#1, d3
	bcc		add_d1_8

	move.b	#7, d3
	adda.l	#1, a0

add_d1_8:

	addi.w	#1, d1
	cmp.b	size, d1
	bne		next_d0_8

	clr.l	d1
	addi.w	#1, d2
	move.w	y, d4
	add.w	d2, d4
	cmp.w	y2, d4
	bhi		end_8

	adda.w	rowBytes, a1
	move.l	a1, a2

next_d0_8:

	dbra	d0, loop_d0_8

end_8:
	movem.l (sp)+, d0-d7/a0-a6
	frfree
	rts
}

#endif

static void DrawHZ_1(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,	// Font
	UInt8* displayP, Coord x, Coord y,			// LCD
	Coord x1, Coord x2, Coord y1, Coord y2,		// Clip
	UInt8 textColor, UInt8 backColor)
{
	Int8	shift;
	UInt8	xOffset, yOffset, *dispP;
	UInt16	i;
	Coord	bx, by;
	
	shift = 7;
	xOffset = yOffset = 0;

	for (i=0; i < dotsNum; i++)
	{
		if (*dotsP >> shift & 0x1)
		{
			dispP = displayP;
			bx = x+xOffset;
			by = y+yOffset;

			if (bx >= x1 && bx <= x2 && by >= y1 && by <= y2)
			{
				dispP += rowBytes * by;
				dispP += bx / 8;
				*dispP |= 1 << (7 - bx % 8);
			}
		}

		if (--shift < 0)
		{
			shift = 7;
			dotsP++;
		}
		if (++xOffset == size)
		{
			xOffset = 0;
			yOffset++;
		}
	}
}

// Draw hanzi in 8bits of depth.
static void DrawHZ_8(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,	// Font
	UInt8* displayP, Coord x, Coord y,			// LCD
	Coord x1, Coord x2, Coord y1, Coord y2,		// Clip
	UInt8 textColor, UInt8 backColor)
{
	Int8	shift;
	UInt8	xOffset, yOffset;
	UInt16	i;
	
	shift = 7;
	xOffset = yOffset = 0;
	displayP += (UInt32)rowBytes * y + x;
	for (i=0; i < dotsNum; i++)
	{
		if (*dotsP >> shift & 0x1)
		{
			if (x + xOffset >= x1 && x + xOffset <= x2 &&
				y + yOffset >= y1 && y + yOffset <= y2)
				*(displayP + xOffset) = textColor;
		}

		if (--shift < 0)
		{
			shift = 7;
			dotsP++;
		}
		if (++xOffset == size)
		{
			xOffset = 0;
			yOffset++;
			displayP += rowBytes;
		}
	}
}


// Draw font use bitmap.
static void DrawHZ_8_bmp(
	UInt8* dotsP, UInt16 dotsNum, UInt8 size,	// Font
	UInt8* displayP, Coord x, Coord y,			// LCD
	Coord x1, Coord x2, Coord y1, Coord y2,		// Clip
	UInt8 textColor, UInt8 backColor)
{
	Int8	shift;
	UInt8	xOffset;
	UInt16	i, row_bytes;
	Coord	height;

	#ifdef USE_SONY_HIRES
		displayP = BmpGetBits(l_bmpFontP);
		BmpGetDimensions(l_bmpFontP, NULL, &height, &row_bytes);
	#else
		displayP = BmpGetBits((BitmapType*)l_bmpFontHiP);
		BmpGetDimensions((BitmapType*)l_bmpFontHiP, NULL, &height, &row_bytes);
	#endif // USE_SONY_HIRES

	MemSet(displayP, (UInt32)row_bytes * height, backColor);
	
	shift = 7;
	xOffset = 0;
	for (i = 0; i < dotsNum; i++)
	{
		if (*dotsP >> shift & 0x1)
			*(displayP + xOffset) = textColor;

		if (--shift < 0)
		{
			shift = 7;
			dotsP++;
		}
		if (++xOffset == size)
		{
			xOffset = 0;
			displayP += row_bytes;
		}
	}
	
	#ifdef USE_SONY_HIRES
		LCDWinDrawBitmap(l_bmpFontP, x, y);
	#else
		WinDrawBitmap((BitmapType*)l_bmpFontHiP, x, y);
	#endif
}


static void DrawCustomChar(
	UInt8* chars, Coord x, Coord y,
	UInt8* bitsP, Coord x1, Coord x2, Coord y1, Coord y2,
	UInt8 fColor, UInt8 bColor)
{
	UInt8 fontRecordIdx = FindCodeInRange(chars);

	if (fontRecordIdx == frASCII)
	{
		#ifdef PALMOS_30
			if (g_prefs.options & OPINVERT)
				WinInvertChars((Char*)chars, 1, x, y);
			else
				WinDrawChars((Char*)chars, 1, x, y);
			#else
			LCDWinDrawChars((Char*)chars, 1, x, y);
		#endif
	}
	else if (fontRecordIdx == frUNDEFINED)
	{
		RectangleType rect;
		
		#ifdef HIGH_DENSITY
			rect.topLeft.x = x + 2;
			rect.topLeft.y = y + 2;
			rect.extent.x = rect.extent.y = fontList[currentFont].size - 4;
		#else
			rect.topLeft.x = x + 1;
			rect.topLeft.y = y + 1;
			rect.extent.x = rect.extent.y = fontList[currentFont].size - 2;
		#endif // HIGH_DENSITY
		
		LCDWinDrawRectangleFrame(simpleFrame, &rect);
	}
	else
	{
		UInt8	fontDataLen, *fontDotsP;
		UInt16	fontDotsNum;

		Coord	width, height;
		UInt8*	bmpBitsP;
		UInt16	row_bytes;

		// How many bits per font.
		fontDotsNum = fontList[currentFont].size * fontList[currentFont].size;
		fontDataLen =  fontDotsNum / 8;
		if (fontDotsNum % 8) fontDataLen++;

		// Position at begin of font matrix.
		fontDotsP = fontRecordList[fontRecordIdx];
		fontDotsP +=
			(UInt16)((*chars - fontList[currentFont].codeRangeP[fontRecordIdx].start) *
			(fontDataLen *
			(fontList[currentFont].wcodeRange.end - fontList[currentFont].wcodeRange.start+1)));
		fontDotsP += (UInt16)((*(chars+1) - fontList[currentFont].wcodeRange.start) * fontDataLen);

		// Print dots.
		#ifdef USE_ARM
			// 使用 ARM 例程将汉字绘入离屏字模缓冲区,然后拷入屏幕,以兼容屏幕方向的旋转。

			#ifdef HIGH_DENSITY
				bmpBitsP = BmpGetBits((BitmapType*)l_bmpFontHiP);
				BmpGetDimensions((BitmapType*)l_bmpFontHiP, &width, &height, &row_bytes);
			#else
				bmpBitsP = BmpGetBits((BitmapType*)l_bmpFontP);
				BmpGetDimensions((BitmapType*)l_bmpFontP, &width, &height, &row_bytes);
			#endif
			MemSet(bmpBitsP, (UInt32)row_bytes * height, bColor);

			l_dcParamP->dotsP = fontDotsP;
			l_dcParamP->dotsNum = fontDotsNum;
			l_dcParamP->size = fontList[currentFont].size;
			l_dcParamP->color = fColor;

			l_dcParamP->displayP = bmpBitsP;
			l_dcParamP->x = 0;
			l_dcParamP->y = 0;
			l_dcParamP->x1 = 0;
			l_dcParamP->x2 = width - 1;
			l_dcParamP->y1 = 0;
			l_dcParamP->y2 = height - 1;
			l_dcParamP->rowBytes = row_bytes;
			
			if (l_cpuType == sysFtrNumProcessorx86)
			{
				PceNativeCall(
				//	(NativeFuncType*)"d:\\myprj\\palm\\vbook2\\ARMletDll.dll\0ARMlet_Main",
					(NativeFuncType*)"f:\\myprj\\vbook2\\ARMletDll.dll\0ARMlet_Main",
					l_dcParamP);
			}
			else // Running on ARM cpu.
			{
				PceNativeCall(l_funcP, l_dcParamP);
			}
			
			#ifdef HIGH_DENSITY
				WinDrawBitmap((BitmapType*)l_bmpFontHiP, x, y + charAdjust);
			#else
				WinDrawBitmap((BitmapType*)l_bmpFontP, x, y + charAdjust);
			#endif
		#else // NON ARM
			DrawHZ(
				fontDotsP, fontDotsNum, fontList[currentFont].size,
				bitsP, x, y + charAdjust, x1, x2, y1, y2, fColor, bColor);
		#endif // USE_ARM
	}
}

static void DrawCustomChars(
	UInt8* chars, Int16 len, Coord x, Coord y, UInt8 fColor, UInt8 bColor)
{
	Coord			xoffset = 0, x1, x2, y1, y2;
	Int16			i = 0, step, width;
	WinHandle		origWin;
	RectangleType	clip;
	void*			bitsP;

	#ifdef PALMOS_30
	UInt32*			dispAddr;
	#else
	BitmapPtr		bitmapP;
	#endif

	// Switch to bitmap window.
	origWin = WinGetDrawWindow();

	#ifdef PALMOS_30
		// displayAddrV20
		dispAddr = (UInt32*)((UInt8*)origWin + 4);
		bitsP = (void*)*dispAddr;
	#else
		if (l_dexRefNum != sysInvalidRefNum)
		{
			bitsP = (UInt8*)DexGetDisplayAddress(l_dexRefNum);
		}
		else
		{
			bitmapP = WinGetBitmap(origWin);
			bitsP = BmpGetBits(bitmapP);
		}
	#endif

	WinGetClip(&clip);
	x1 = clip.topLeft.x;
	x2 = clip.topLeft.x + clip.extent.x - 1;
	y1 = clip.topLeft.y;
	y2 = clip.topLeft.y + clip.extent.y - 1;

	// OS5 always save clip in native coordinate.
	#ifdef USE_SONY_HIRES
	x1 <<= 1;
	x2 <<= 1;
	y1 <<= 1;
	y2 <<= 1;
	#endif

	while (i < len)
	{
		step = (chars[i] <= 0x80) ? 1 : 2;
		width = CfnCharWidth((Char*)&chars[i]);
		DrawCustomChar(
			&chars[i], x + xoffset, y, (UInt8*)bitsP,
			x1, x2, y1, y2, fColor, bColor);
		xoffset += width;
		i += step;
	}
}

static void DrawCustomTruncChars(
	UInt8* chars, Int16 len, Coord x, Coord y, Coord maxWidth, UInt8 fColor, UInt8 bColor)
{
	Coord	width;
	Int16	i, step;

	if (CfnCharsWidth((Char*)chars, len) <= maxWidth)
	{
		DrawCustomChars(chars, len, x, y, fColor, bColor);
		return;
	}

	i = 0;
	width = 0;	
	while (i < len)
	{
		step = (chars[i] <= 0x80) ? 1 : 2;
		width += CfnCharWidth((Char*)&chars[i]);
		if (width > maxWidth)
		{
			DrawCustomChars(chars, i-step+1, x, y, fColor, bColor);
			break;
		}
		i += step;
	}
}

// ********************************************************************
// Entry Points
// ********************************************************************
#pragma mark === Entry Points ===

Boolean CfnInit()
{
	#ifdef USE_ARM
		l_funcH = DmGetResource('ARMC', 1);
	#endif

	MemSet(fontList, sizeof(CustomFontType) * 4, 0);
	sysFontID = stdFont;

	SearchCustomFont();
	GenerateFontNameList();
	
	if (maxFontRecords)
	{
		fontRecordList = (UInt8**)MemPtrNew(sizeof(UInt8*) * maxFontRecords);
		MemSet(fontRecordList, sizeof(UInt8*) * maxFontRecords, 0);
	}
	
	#ifdef USE_ARM

		l_dcParamP = MemPtrNew(sizeof(DrawCharParamType));
		l_funcP = MemHandleLock(l_funcH);
		FtrGet(sysFileCSystem, sysFtrNumProcessorID, &l_cpuType);
		
	#endif

	// 当使用 Palm 的设备时可以直接获取屏幕地址
	{
/*		Err error;
		if ((error = SysLibFind(dexLibName, &l_dexRefNum)))
			if (error == sysErrLibNotFound)
				SysLibLoad(dexLibType, dexLibCreator, &l_dexRefNum);*/
	}

	return true;
}


void CfnUpdateRowBytes()
{
	if (l_dexRefNum == sysInvalidRefNum)
	{
		BitmapPtr bmpP = WinGetBitmap(WinGetDisplayWindow());
		BmpGlueGetDimensions(bmpP, NULL, NULL, &rowBytes);
	}
	else
	{
		DexGetDisplayDimensions(l_dexRefNum, NULL, NULL, &rowBytes);
	}
}


void CfnSetDepth(UInt32 depth)
{
	CfnUpdateRowBytes();
	
	switch (depth)
	{
	case 1:
		#ifdef PALMOS_50
			DrawHZ = DrawHZ_1;
		#else
			DrawHZ = DrawHZ_1_asm;
		#endif
		break;
	
	case 8:
		#ifdef PALMOS_50
			#ifdef HIGH_DENSITY
				DrawHZ = DrawHZ_8_bmp;
		//		DrawHZ = DrawHZ_8;
			#else
				DrawHZ = DrawHZ_8;
			#endif // HIGH_DENSITY
		#else
			#ifdef USE_SONY_HIRES
				DrawHZ = DrawHZ_8_bmp;
			#else
				DrawHZ = DrawHZ_8_asm;
			#endif // USE_SONY_HIRES
		#endif // PALMOS_50
		break;
	}
}

void CfnEnd()
{
	if (currentFont != -1)
		CloseCustomFont(currentFont);
	CleanFontList();
	if (fontRecordList)
		MemPtrFree(fontRecordList);

	#ifdef USE_ARM
		MemPtrFree(l_dcParamP);
		MemPtrUnlock(l_funcP);
		DmReleaseResource(l_funcH);
	#endif

	if (l_bmpFontP) BmpDelete(l_bmpFontP);
	if (l_bmpFontHiP) BmpDelete((BitmapType*)l_bmpFontHiP);
}

Int16 CfnCharWidth(Char *chars)
{
	static const UInt16 punctuation[] = {
		0xa1a2, 0xa1a3, 0xa1a4, 0xa1ae, 0xa1af, 0xa1b0, 0xa1b1, 0xa1b2,
		0xa1b3, 0xa1b4, 0xa1b5, 0xa1b6, 0xa1b7, 0xa1b8, 0xa1b9, 0xa1ba,
		0xa1bb, 0xa1bc, 0xa1bd, 0xa1be, 0xa1bf, 0xa1e3, 0xa1e4, 0xa1e5,
		
		0xa3a1, 0xa3a8, 0xa3a9, 0xa3ac, 0xa3ae, 0xa3ba, 0xa3bb, 0xa3bf,
		0xa3db, 0xa3dd,
		
		0x0
	};

	Int16	i = 0;
	Int16	width;
	UInt8	*p = (UInt8*)chars;

	if (!useCustomFont)
	{
		if (*p <= 0x80)
			width = FntCharWidth(*chars);
		else
			width = FntCharsWidth(chars, 2);

		return width;
	}

	// Use custom font
	if (*p <= 0x80)
		return ((*p == '\t') ? charTabWidth : FntCharWidth(*chars));

	if (fontList[currentFont].size == fontList[currentFont].minSize)
		return (Int16)fontList[currentFont].size + g_prefs2.wordSpace;

	width = (Int16)fontList[currentFont].size;
	if (*p == 0xa1 || *p == 0xa3)
	{
		UInt16	pattern;

		pattern = *p << 8;
		pattern += *(p+1);
		while (punctuation[i])
		{
			if (pattern == punctuation[i])
			{
				width = (Int16)fontList[currentFont].minSize;
				break;
			}
			i++;
		}
	}

	return width + g_prefs2.wordSpace;
}

Int16 CfnCharHeight()
{
	Int16	height;
	
	if (useCustomFont)
	{
		height = (fontList[currentFont].size > FntCharHeight()) ?
			fontList[currentFont].size : FntCharHeight();
	}
	else
		height = FntCharHeight();

	return height;
}

Int16 CfnCharSize()
{
	return ((useCustomFont) ? fontList[currentFont].size : 0);
}

Int16 CfnCharsWidth(Char *chars, Int16 len)
{
	Int16	i = 0;
	Int16	width = 0;
	UInt8	step;
	
	if (useCustomFont)
	{
		while (i < len)
		{
			step = ((UInt8)chars[i] <= 0x80) ? 1 : 2;
			width += CfnCharWidth(&chars[i]);
			i += step;
		}
	}
	else
		width = FntCharsWidth(chars, len);
	
	return width;
}

Int16 CfnCharsHeight(Char *chars, Int16 len)
{
	Int16	i;
	Int16	height, c_height;
	
	height = FntCharHeight();

	if (useCustomFont)
	{
		c_height = fontList[currentFont].size;
		
		for (i=0; i < len; i++)
			if ((UInt8)chars[i] > 0x80)
			{
				height = (c_height > height) ? c_height : height;
				break;
			}
	}
	
	return height;
}

void CfnDrawChars(
	Char *chars, Int16 len, Coord x, Coord y, UInt8 fColor, UInt8 bColor)
{
	if (useCustomFont)
		DrawCustomChars((UInt8*)chars, len, x, y, fColor, bColor);
	else
	{
		#ifdef PALMOS_30
		if (g_prefs.options & OPINVERT)
			WinInvertChars(chars, len, x, y);
		else
			WinDrawChars(chars, len, x, y);
		#else
		LCDWinDrawChars(chars, len, x, y);
		#endif
	}
}

void CfnDrawTruncChars(
	Char *chars, Int16 len, Coord x, Coord y, Coord maxWidth, UInt8 fColor, UInt8 bColor)
{
	if (useCustomFont)
		DrawCustomTruncChars((UInt8*)chars, len, x, y, maxWidth, fColor, bColor);
	else
		LCDWinDrawTruncChars(chars, len, x, y, maxWidth);
}

FontID CfnGetFont()
{
	return sysFontID;
}

void CfnSetFont(Int8 fontIndex)
{
	UInt16	err;
	Coord	width, height;
	
	fontIndex = (fontIndex >= numFonts) ? 0 : fontIndex;

	if (fontIndex < 4)
	{
		// Use system fonts.
		useCustomFont = false;

		switch (fontIndex)
		{
		case 0:
			sysFontID = stdFont;
			break;

		case 1:
			sysFontID = boldFont;
			break;

		case 2:
			sysFontID = largeFont;
			break;

		case 3:
			sysFontID = largeBoldFont;
			break;
		}
	}
	else
	{
		// Use custom fonts.
		useCustomFont = true;

		if (currentFont != fontIndex - 4)
		{
			if (currentFont != -1)
				CloseCustomFont(currentFont);
			currentFont = fontIndex - 4;
			OpenCustomFont(currentFont);
			
			switch (fontList[currentFont].size)
			{
			case 11:
				charAdjust = 0;
				charTabWidth = 6;
				break;
				
			case 16:
				charAdjust = 4;
				charTabWidth = 12;
				break;
				
			default:
				charAdjust = 0;
				charTabWidth = 12;
				break;
			}

			if (l_bmpFontP)
			{
				BmpGetDimensions(l_bmpFontP, &width, &height, NULL);
				if (width != fontList[currentFont].size)
				{
					BmpDelete(l_bmpFontP);
					if (l_bmpFontHiP)
						BmpDelete((BitmapType*)l_bmpFontHiP);
					l_bmpFontP = NULL;
					l_bmpFontHiP = NULL;
				}
			}
			width = height = fontList[currentFont].size;

			#ifdef PALMOS_50
				// Generate bitmap for font.
				if (l_bmpFontP == NULL)
				{
					l_bmpFontP = BmpCreate(width, height, 8, NULL, &err);
					#ifdef HIGH_DENSITY
						l_bmpFontHiP = BmpCreateBitmapV3(l_bmpFontP, kDensityDouble, BmpGetBits(l_bmpFontP), NULL);
					#endif // HIGH_DENSITY
				}
			#endif // PALMOS_50
			
			/* for Sony device */
			
			#ifdef USE_SONY_HIRES
				if (l_bmpFontP == NULL)
				{
					l_bmpFontP = LCDBmpCreate(width, height, 8, NULL, &err);
				}
			#endif // USE_SONY_HIRES
		}
	}
}

void CfnSetEngFont(UInt16 font)
{
	if (font == 1)
		sysFontID = englishFont;
}

Int8 CfnGetAvailableFonts()
{
	// Include system fonts and add fonts.
	return numFonts;
}

Char* CfnGetFontName(Int8 index)
{
	return fontNameList[index];
}

UInt32 CfnFontLoad()
{
	if (currentFont == -1)
		return 0;
	else if (fontList[currentFont].dbID)
		return 0;
	else
	{
		UInt32	load = 0;
		UInt8	i;
		
		for (i=0; i < maxFontRecords; i++)
			if (fontRecordList[i])
				load += MemPtrSize(fontRecordList[i]);
		return load;
	}
}
