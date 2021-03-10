#include "VBook.h"
#include "LCD.h"
#include "CustomFont.h"
#include "BookList.h"
#include "Contents.h"
#include "View.h"
#include "Punct.h"
#include "Bookmark.h"
#include "CommonDlg.h"
#include "VDict.h"

#pragma mark === Macro Define ===

#define	DICTPROGNAME	"ZDic"
//#define	DICTPROGNAME	"UITest"

#define ISLINEFEED(c) (c == 0xA || c == 0xD)
#define ISCOMMAND(c) (c >= CMDBEGIN && c < CMDEND)
#define ISCMD0P(c) (c >= CMDBEGIN && c < CMDFONT)
#define ISCMD1P(c) (c >= CMDFONT && c < CMDLINK)
#define ISCMD2P(c) (c >= CMDLINK && c < CMDEND)

#define ISUPPER(c) (c >= 0x41 && c <= 0x5A)
#define ISLOWER(c) (c >= 0x61 && c <= 0x7A)

#define ISPARABEGIN(p) (((*(p - 1) == 0 && *(p - 3) != CMDLINK) || *(p - 1) == 0xa) ? true : false)
#define PARAINDENT() (g_LCDWidth >> 4)

#define CMDNONE			0
#define CMDLINEFEED		1

#ifdef HIGH_DENSITY
	#define SPACE_LINE_HEIGHT	8
	#define UNDERLINE_FOR_ENG	2
	#define UNDERLINE_FOR_CHS	4
	#define MINIBAR_HEIGHT		12
	#define TOOLBAR_HEIGHT		18
	#define MAX_PIECES			200
#else
	#define SPACE_LINE_HEIGHT	4
	#define UNDERLINE_FOR_ENG	1
	#define UNDERLINE_FOR_CHS	2
	#define MINIBAR_HEIGHT		6
	#define TOOLBAR_HEIGHT		9
	#define MAX_PIECES			100
#endif

#define BUFFER_SIZE			8200 // 4096 * 2 + 8
#define PAGE_DISTANCE		1000
#define MIN_SLIDER_LEN		3

#define CLOCK_UPDATE		120 // 1 minute
#define BATTERY_UPDATE		360 // 3 minutes

#pragma mark === Struct Define ===

typedef struct ScrollDataType {
	Int32	maxValue;
	Int32	value;
	Int16	pageSize;
	Int16	pos;
} ScrollDataType;

typedef struct ScrollType {
	ScrollDataType	*dataP;
	RectangleType	bounds;
} ScrollType;

typedef struct PieceType {
	RectangleType	bounds;
	Char			*textP;
	Int8			len;
	Int8			parsedLen;
	UInt8			state;
	UInt8			cmd;
	UInt16			data;
	UInt8			chs;
} PieceType;

typedef struct CmdStateType {
	Boolean			isLink;
	UInt16			linkData;
} CmdStateType;

typedef struct BufferType {
	Boolean		openOnLocation;

	UInt16		chapterIndex;
	UInt32		offset;
	UInt16		anchor;

	UInt32		startPos;
	UInt8*		mainBuf;
	UInt8*		startP;		// Points to begin of main buffer.
	UInt8*		midP;		// Points to half of main buffer.

	Int16		topBufNo;
	Int16		topBufLen;
	Int16		botBufNo;
	Int16		botBufLen;
} BufferType;

typedef struct ViewType {
	RectangleType	bounds;
	Int16			top;
	Int16			bottom;
} ViewType;

typedef struct HistoryType {
	HistoryDataType	list[HISTORY_NUM];
	Int16			head;
	Int16			tail;
	Int16			pos;
} HistoryType;

typedef struct FindType {
	Char*		token;
	UInt16		chapter;
	UInt32		offset;
	Boolean		matchCase;
	Boolean		searchAll;
	Boolean		found;
} FindType;

typedef struct AutoScrollType {
	Boolean		scroll;
	Boolean		pause;
	UInt16		seconds;
	Int16		guide;
	Int8		ticks;
	ButtonsPtr	savedKeysP;
} AutoScrollType;

typedef struct ClockType {
	UInt8		hours;
	UInt8		minutes;
	Int16		ticks;
} ClockType;

typedef struct BatteryType {
	Int16		ticks;
} BatteryType;

typedef struct SelCellType {
	RectangleType	bounds;
	Int8			i;
	Int8			selected;
} SelCellType, *SelCellPtr;

typedef struct SelPieceType {
	Int16		pi;
	SelCellPtr	listP;
	Int16		num;
} SelPieceType, *SelPiecePtr;

#pragma mark === Function Declare ===

void	BufferWindowInit();
void	BufferWindowEnd();

WordClass GetWordClass(UInt8* paragraph);

void	ViewParsingInit();
void	ViewBlockCopy(UInt8* desP, UInt8* srcP, UInt16 size);
Boolean	ViewLoadPrevBlock();
Boolean	ViewLoadNextBlock();
Char*	ViewGetPrevParagraph(Char* textP);
Char*	ViewGetNextParagraph(Char* textP);
UInt32	ViewGetCorrectStartPoint(UInt32 offset);
void	ViewBreakParagraph(Char* textP, UInt8 state);
Boolean	ViewMoveUp();
void	ViewMoveDown();
void	ViewDrawScreen();
void	ViewLoadChapter(Boolean reload);
void	ViewLoadByPos(Int32 offset);
Boolean	ViewClickLink(Coord x, Coord y);
Boolean	ViewDoAction(UInt8 action);

void	ScrollDraw();
void	ScrollTapped(Coord x);

Boolean	ViewFormDoCommand(UInt16 command);
void	PrevChapter();
void	NextChpater();
void	Back();
void	Forward();
void	FindText();
Boolean	GetWord(Coord x, Coord y);
void	ShowBmkList();
void	AddBookmark();
void	SetScroll();
Boolean	ViewSetting();

MemPtr	FormPointerArray(Char* text, Int16 size, Int16 num);
void	StartPlay();
void	StopPlay();
UInt8*	ViewParseCmd(UInt8* cmdP, Int8* cmdLenP);
Boolean IsChapterEnd (Int16);

void	BatteryAndClock();
void	ViewUpdate();
void	ExternalDA(Boolean choose);

#pragma mark === Internal Variables ===

static ScrollDataType	vertData;
static ScrollType		scrollBar;

static ViewType			view;

static CmdStateType		cmdState;
static BufferType		buffer;
static PieceType		pieces[MAX_PIECES];
static Int16			piecesIndex;
static HistoryType		history;
static Boolean			isLinked;
static Boolean			getword;
static Boolean			scrcopy;
static FindType			find;
static AutoScrollType	autoScroll;

#ifdef PALMOS_50
static Boolean			updateContent = false;
#endif // PALMOS_50

static WinHandle		l_bufferWinH = NULL;

static ClockType		clock;
static BatteryType		battery;

#pragma mark === Entry Points ===

Boolean ViewPrepare()
{
	vertData.value		= -1;
	scrollBar.dataP		= &vertData;
	
	buffer.mainBuf		= (UInt8*)MemPtrNew(BUFFER_SIZE);
	buffer.midP			= buffer.mainBuf + BUFFER_SIZE / 2;
	buffer.startP		= NULL;
	buffer.topBufNo		= -1;
	buffer.botBufNo		= -1;
	
	MemSet(&find, sizeof(find), 0);
	
	autoScroll.scroll		= false;
	autoScroll.savedKeysP	= NULL;
	
	#ifdef PALMOS_50
	BufferWindowInit();
	#endif // PALMOS_50
	
	return true;
}

void ViewRelease()
{
	#ifdef PALMOS_50
	BufferWindowEnd();
	#endif // PALMOS_50
	
	MemPtrFree(buffer.mainBuf);
	if (find.token)
		MemPtrFree(find.token);
}

void ViewSetOffset(UInt16 chapIdx, UInt32 offset)
{
	buffer.openOnLocation = true;
	buffer.chapterIndex = chapIdx;
	buffer.offset = offset;
}

void ViewSetAnchor(UInt16 chapIdx, UInt16 anchor)
{
	buffer.openOnLocation = false;
	buffer.chapterIndex = chapIdx;
	buffer.anchor = anchor;
}

void ViewInit()
{
	vertData.value	= -1;
}

void ViewHistoryInit()
{
	history.head	= -1;
	history.tail	= 0;
	history.pos		= -1;
}

void ViewEnableLink(Boolean linkState)
{
	isLinked = linkState;

	if (isLinked)
	{
		// Load history.
		PrefGetHistory(history.list, HISTORY_NUM-1, &history.tail, &history.pos);
		if (history.tail)
			history.head = 0;
	}
}

void ViewSaveHistory()
{
	if (isLinked && history.head != -1)
		PrefSaveHistory(history.list, history.head, history.tail, history.pos);
}

void GetLastPosition(UInt16* chapterP, UInt32* offsetP)
{
	if (view.top == -1)
	{
		*chapterP = 0;
		*offsetP = 0;
	}
	else
	{
		*chapterP = buffer.chapterIndex;
		*offsetP = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
	}
}

void ViewStopScroll(EventPtr eventP)
{
	FormPtr			frmP;
	RectangleType	bounds;
	
	if (!autoScroll.scroll) return;

	if (g_prefs.options & OPTOOLBAR)
	{
		// When penDownEvent occur, if pen isn't tap in play button
		// then stop play.
		frmP = FrmGetActiveForm();

		FrmGetObjectBounds(
			frmP, FrmGetObjectIndex(frmP, ViewPlayButton), &bounds);

		if (!RctPtInRectangle(eventP->screenX, eventP->screenY, &bounds))
			StopPlay();
	}
	else
		StopPlay();
}

UInt16 ViewGetChapterIndex()
{
	return buffer.chapterIndex;
}

#pragma mark === Internal Fuctions ===

static void BufferWindowInit()
{
	Coord			width, height;
	BitmapType		*bmpP;
	BitmapTypeV3	*bmpPV3;
	UInt32			density;
	Err				error;
	
	l_bufferWinH = NULL;
	
	return;

	WinScreenGetAttribute(winScreenDensity, &density);
	WinGetDisplayExtent(&width, &height);

#ifdef USE_PALM_SILKSCREEN
	height = 225;
#endif

	if (density == kDensityDouble)
	{
		width <<= 1;
		height <<= 1;
	}

	bmpP = BmpCreate(width, height, g_prefs.depth, NULL, &error);
	if (!error)
	{
		bmpPV3 = BmpCreateBitmapV3(bmpP, (UInt16)density, BmpGetBits(bmpP), NULL);
		BmpDelete(bmpP);
		l_bufferWinH = WinCreateBitmapWindow((BitmapPtr)bmpPV3, &error);
	}
}


static void BufferWindowEnd()
{
	if (l_bufferWinH)
	{
		BitmapType *bmpP = WinGetBitmap(l_bufferWinH);
		BmpDelete(bmpP);
		WinDeleteWindow(l_bufferWinH, false);
	}
}


static void HighlightResult()
{
	RectangleType	rect, oldClip;
	Int16			i, max, len, offset;
	UInt8			*rstP, *srcP, *tooFar, lineState, step;

	#ifdef PALMOS_30
	FontID			oldFont;
	#endif
	
	rstP = buffer.startP + (find.offset - buffer.startPos);
	
	i = view.top;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	while (i != max)
	{
		if ((Char*)rstP >= pieces[i].textP &&
			(Char*)rstP < pieces[i].textP + pieces[i].len)
			break;
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
	if (i == max) return;
	
	#ifdef PALMOS_30
	oldFont = LCDFntSetFont(CfnGetFont());
	#else
	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());
	WinSetDrawMode(winSwap);
	if (g_prefs.depth > 1)
	{
		WinSetForeColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif
	#endif

	LCDWinGetClip(&oldClip);
	LCDWinSetClip(&view.bounds);

	// Find top left coordinate of pattern.
	srcP = (UInt8*)pieces[i].textP;
	rect.topLeft.x = pieces[i].bounds.topLeft.x;
	while (srcP < rstP)
	{
		step = (*srcP > 0x80) ? 2 : 1;
		rect.topLeft.x += CfnCharWidth((Char*)srcP);
		srcP += step;
	}
	
	offset = 0;
	len = StrLen(find.token);
	tooFar = (UInt8*)pieces[i].textP + pieces[i].len;
	lineState = pieces[i].state;
	rect.topLeft.y = pieces[i].bounds.topLeft.y;
	rect.extent.x = 0;
	rect.extent.y = pieces[i].bounds.extent.y;
	while (true)
	{
		if (offset == len)
		{
			#ifdef PALMOS_30
			WinInvertRectangle(&rect, 0);
			#else
			LCDWinPaintRectangle(&rect, 0);
			#endif
			break;
		}
		
		if (srcP == tooFar)
		{
			i = CYCLIC_INCREASE(i, MAX_PIECES);

			srcP = (UInt8*)pieces[i].textP;
			tooFar = srcP + pieces[i].len;

			if (i == view.bottom)
			{
				#ifdef PALMOS_30
				WinInvertRectangle(&rect, 0);
				#else
				LCDWinPaintRectangle(&rect, 0);
				#endif
				break;
			}
			
			if (pieces[i].state != lineState)
			{
				lineState = pieces[i].state;
				#ifdef PALMOS_30
				WinInvertRectangle(&rect, 0);
				#else
				LCDWinPaintRectangle(&rect, 0);
				#endif
				
				rect.topLeft.x = pieces[i].bounds.topLeft.x;
				rect.topLeft.y = pieces[i].bounds.topLeft.y;
				rect.extent.x = 0;
				rect.extent.y = pieces[i].bounds.extent.y;
			}
		}
		
		step = (*srcP > 0x80) ? 2 : 1;
		rect.extent.x += CfnCharWidth((Char*)srcP);
		srcP += step;
		offset += step;
	}

	LCDWinSetClip(&oldClip);

	#ifdef PALMOS_30
	LCDFntSetFont(oldFont);
	#else
	WinPopDrawState();
	#endif

	find.offset = buffer.startPos + (srcP - buffer.startP);
}


static Boolean GetWord(Coord x, Coord y)
{
	Int16			i, max, len, step;
	Boolean			found = false;
	Coord			leftX;
	UInt8			*p, *tooFar;
	RectangleType	rect;
	Char*			wordP;
	EventType		event;

	#ifdef PALMOS_30
	FontID			oldFont;
	#endif
	
	i = view.top;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	while (i != max)
	{
		if (pieces[i].cmd != CMDLINEFEED &&
			RctPtInRectangle(x, y, &pieces[i].bounds))
		{
			found = true;
			break;
		}
		
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
	if (!found)
		return false;

	#ifdef PALMOS_30
	oldFont = LCDFntSetFont(CfnGetFont());
	#else

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());
	if (g_prefs.depth > 1)
	{
		WinSetForeColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif

	#endif // PALMOS_30

	leftX = pieces[i].bounds.topLeft.x;
	rect.topLeft.y = pieces[i].bounds.topLeft.y;
	rect.extent.y = pieces[i].bounds.extent.y;
	p = (UInt8*)pieces[i].textP;
	tooFar = p + pieces[i].len;
	i = 0;
	while (true)
	{
		if (leftX > x)
			break;
		
		rect.topLeft.x = leftX;
		leftX += CfnCharWidth((Char*)&p[i]);
		step = (p[i] <= 0x80) ? 1 : 2;
		i += step;
	}

	rect.extent.x = 0;
	i -= step;
	if (GetWordClass(&p[i]) == WC_ENG_WORD)
	{
		i--;
		while (
			p[i] &&
			!ISCOMMAND(p[i-2]) &&
			(GetWordClass(&p[i]) == WC_ENG_WORD))
		{
			rect.topLeft.x -= CfnCharWidth((Char*)&p[i]);
			i--;
		}
		wordP = (Char*)&p[++i];
		p =  (UInt8*)wordP;
		i = 0;
		while (p < tooFar && p[i] && (GetWordClass(&p[i]) == WC_ENG_WORD))
		{
			rect.extent.x += CfnCharWidth((Char*)&p[i]);
			i++;
		}
		len = i;
	}
	else
	{
		wordP = (Char*)&p[i];
		rect.extent.x = CfnCharWidth(wordP);
		len = ((UInt8)*wordP <= 0x80) ? 1 : 2;
	}

	#ifdef PALMOS_30
	LCDFntSetFont(oldFont);
	WinInvertRectangle(&rect, 0);
	#else	
	WinSetDrawMode(winSwap);
	LCDWinPaintRectangle(&rect, 0);
	#endif
	
	do {
		EvtGetEvent(&event, SysTicksPerSecond() >> 1);
	} while (event.eType != penUpEvent && event.eType != nilEvent);
	
	#ifdef PALMOS_30
	WinInvertRectangle(&rect, 0);
	#else
	LCDWinPaintRectangle(&rect, 0);
	WinPopDrawState();
	#endif // PALMOS_30

	if ((g_prefs.options & OPGWCOPY) ||
		(g_prefs.options & OPGWEXTDA))
	{
		ClipboardAddItem(clipboardText, wordP, len);
	}
	
	if (g_prefs.options & OPGWROADLINGUA)
	{
		LocalID	id = DmFindDatabase(0, DICTPROGNAME);
		if (id)
		{
			UInt32 result = errNone;
			
			ClipboardAddItem(clipboardText, wordP, len);
			SysAppLaunch(0, id, 0, sysAppLaunchCmdCustomBase, NULL, &result);
		}
		else
			FrmCustomAlert(DebugAlert, "Not found ZDic.prc!", "", "");
	}
	else if (g_prefs.options & OPGWEXTDA)
	{
		ExternalDA(false);
	}
/*	else if (g_prefs.options & OPGWVDICT)
	{
		VDictSetWordToFind(wordP, len);
		FrmGotoForm(VDictForm);
	}*/

	return true;
}

static MemHandle EditGetEditText()
{
	UInt32		currentPos, endPos;
	UInt16		dataLen;
	UInt32		endLen;
	MemHandle	txtH;
	UInt8		*srcP, *dstP, step;
	Int16		topBufNo, botBufNo;
	
	txtH = MemHandleNew(2048);
	if (txtH == NULL)
		return NULL;
	
	topBufNo = buffer.topBufNo;
	botBufNo = buffer.botBufNo;
	
	currentPos = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
	endPos = buffer.startPos + buffer.topBufLen;
	if (botBufNo != -1)
		endPos += buffer.botBufLen;

	if (currentPos + 2048 > endPos && endPos < BookGetChapterLen())
	{
		if (buffer.botBufNo != -1)
		{
			buffer.startPos += buffer.topBufLen;
			// Move bottom buffer to top.
			buffer.startP = buffer.midP - buffer.botBufLen;
			*(buffer.startP-1) = 0;
			ViewBlockCopy(buffer.startP, buffer.midP, buffer.botBufLen);
			buffer.topBufNo = buffer.botBufNo;
			buffer.topBufLen = buffer.botBufLen;
		}
		else
			buffer.botBufNo = buffer.topBufNo;
		
		buffer.botBufNo++;

		// Get block length.
		BookGetBlock(buffer.botBufNo, NULL, &dataLen, 0, NULL);
		// Get block data.
		BookGetBlock(buffer.botBufNo, buffer.midP, NULL, dataLen, &endLen);
		buffer.midP[dataLen] = 0;
		buffer.botBufNo = buffer.botBufNo;
		buffer.botBufLen = dataLen;
	}
	
	srcP = buffer.startP + (currentPos - buffer.startPos);
	dstP = MemHandleLock(txtH);
	dataLen = 0;
	while (dataLen < 2044 && *srcP)
	{
		if (ISCOMMAND(*srcP))
		{
			if (ISCMD0P(*srcP))
				srcP++;
			else if (ISCMD1P(*srcP))
				srcP += 2;
			else if (ISCMD2P(*srcP))
				srcP += 3;
			
			continue;
		}
		
		step = (*srcP > 0x80) ? 2 : 1;

		*dstP++ = *srcP++;
		if (step == 2)
			*dstP++ = *srcP++;
		
		dataLen += step;
	}
	*dstP = 0;
	
	MemHandleUnlock(txtH);

	// Restore top text buffer.
	if (buffer.topBufNo != topBufNo)
	{
		BookGetBlock(topBufNo, NULL, &dataLen, 0, NULL);
		buffer.startP = buffer.midP - dataLen;
		*(buffer.startP - 1) = 0;
		BookGetBlock(topBufNo, buffer.startP, NULL, dataLen, &endLen);
		*buffer.midP = 0;
		buffer.topBufNo = topBufNo;
		buffer.topBufLen = dataLen;			
		buffer.startPos = endLen - dataLen;
	}
	// Restore bottom text buffer.
	if (buffer.botBufNo != botBufNo)
	{
		if (botBufNo == -1)
		{
			buffer.botBufNo = -1;
			buffer.botBufLen = 0;
		}
		else
		{
			BookGetBlock(botBufNo, NULL, &dataLen, 0, NULL);
			BookGetBlock(botBufNo, buffer.midP, NULL, dataLen, &endLen);
			buffer.midP[dataLen] = 0;
			buffer.botBufNo = botBufNo;
			buffer.botBufLen = dataLen;
		}
	}
	
	return txtH;
}

/*
static void EffScrollArea(RectanglePtr rectP, WinDirectionType direction)
{
	Int16			i;
	EventType		event;
	RectangleType	vacated;
	
	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	for (i = 0; i < rectP->extent.y; i++)
	{
		EvtGetEvent(&event, 5);
		LCDWinScrollRectangle(rectP, direction, 1, &vacated);
	}

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesStandard);
	#endif
}
*/

static void Minibar()
{
	FormPtr			frmP;
	RectangleType	rect;
	Coord			dispWidth, dispHeight;
	
	WinGetDisplayExtent(&dispWidth, &dispHeight);
	
	#ifdef HIGH_DENSITY
	dispWidth <<= 1;
	dispHeight <<= 1;
	#endif
	
	WinPushDrawState();
	
	if (g_prefs.depth > 1)
	{
		if (g_prefs.options & OPINVERT)
			WinSetBackColor(g_prefs.textColor);
		else
			WinSetBackColor(g_prefs.backColor);
	}
	else
	{
		if (g_prefs.options & OPINVERT)
			WinSetBackColor(1);
		else
			WinSetBackColor(0);
	}

	frmP = FrmGetActiveForm();
	
	// Calculate size of minibar.
	rect.topLeft.x = 0;
	rect.topLeft.y = 0;
	rect.extent.x = dispWidth;
	rect.extent.y = MINIBAR_HEIGHT;

	view.bounds.extent.y = dispHeight;
	if (g_prefs.options & OPTOOLBAR)
		view.bounds.extent.y = dispHeight - TOOLBAR_HEIGHT;
	
	if (g_prefs.options & OPMINIBAR)
	{
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif

		LCDWinEraseRectangle(&rect, 0);

		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesStandard);
		#endif
		
		if (g_prefs.depth == 1 && g_prefs.options & OPINVERT)
		{
			FrmGetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, ViewBatteryBitMap),
				&rect);
			rect.topLeft.x -= 1;
			rect.extent.x += 2;
			rect.extent.y += 1;
			WinSetBackColor(0);
			LCDWinEraseRectangle(&rect, 0);
		}

		FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewBatteryBitMap));
		
		view.bounds.topLeft.y = MINIBAR_HEIGHT;
		view.bounds.extent.y -= MINIBAR_HEIGHT;

		battery.ticks = BATTERY_UPDATE;
		clock.ticks = CLOCK_UPDATE;
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewBatteryBitMap));
		view.bounds.topLeft.y = 0;
	}
	
	WinPopDrawState();
}


static void Toolbar()
{
	FormPtr			frmP;
	UInt16			objNum, objId, i;
	RectangleType	rect;
	Coord			dispWidth, dispHeight;
	
	WinGetDisplayExtent(&dispWidth, &dispHeight);
	
	#ifdef HIGH_DENSITY
	dispWidth <<= 1;
	dispHeight <<= 1;
	#endif
	
	frmP = FrmGetActiveForm();
	objNum = FrmGetNumberOfObjects(frmP);

	// Calculate size of toolbar.
	rect.topLeft.x = 0;
	rect.topLeft.y = dispHeight - TOOLBAR_HEIGHT;
	rect.extent.x = dispWidth;
	rect.extent.y = TOOLBAR_HEIGHT;

	view.bounds.extent.y = dispHeight;
	if (g_prefs.options & OPMINIBAR)
	{
		view.bounds.topLeft.y = MINIBAR_HEIGHT;
		view.bounds.extent.y = dispHeight - MINIBAR_HEIGHT;
	}
	else
		view.bounds.topLeft.y = 0;

	if (g_prefs.options & OPTOOLBAR)
	{
		// Show toolbar.
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif

		LCDWinEraseRectangle(&rect, 0);
		
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesStandard);
		#endif

		for (i = 0; i < objNum; i++)
		{
			objId = FrmGetObjectId(frmP, i);

			// Skip other controls not belong toolbar.
			if (objId == ViewBatteryBitMap ||
				objId == ViewMarkList || objId == ViewCopyList ||
				objId == ViewStopBitMap ||
				objId == ViewPrgCancelButton)
				continue;
			
			if ((objId == ViewCopyBitMap) && scrcopy)
				continue;
			if ((objId == ViewCopyPressedBitMap) && !scrcopy)
				continue;
			
			FrmShowObject(frmP, i);
		}
		
		view.bounds.extent.y -= TOOLBAR_HEIGHT;
		ScrollDraw();
	}
	else
	{
		// Hide toolbar.
		for (i = 0; i < objNum; i++)
		{
			objId = FrmGetObjectId(frmP, i);

			if (objId == ViewViewGadget ||
				objId == ViewBatteryGadget || objId == ViewBatteryBitMap ||
				objId == ViewClockGadget ||
				objId == ViewProgressGadget)
				continue;
			
			FrmHideObject(frmP, i);
		}
	}
}


static void RedrawScreen()
{
	view.bottom = -2;
	ViewMoveDown();
	ViewDrawScreen();
}

static void BatteryAndClock()
{
	FormPtr			frmP;
	RectangleType	bounds, rect;
	DateTimeType	time;
	Boolean			updateClock, updateBattery;
	Char			timeStr[8];
	UInt8			percent;
	FontID			oldFont;
	
	updateClock = updateBattery = false;

	clock.ticks++;
	if (clock.ticks >= CLOCK_UPDATE)
	{
		TimSecondsToDateTime(TimGetSeconds(), &time);
		clock.ticks = 0;
		clock.hours = time.hour;
		clock.minutes = time.minute;
		updateClock = true;
	}
	
	battery.ticks++;
	if (battery.ticks >= BATTERY_UPDATE)
	{
		battery.ticks = 0;
		SysBatteryInfo(false, NULL, NULL, NULL, NULL, NULL, &percent);
		updateBattery = true;
	}
	
	if (!(g_prefs.options & OPMINIBAR)) return;
	if (!updateClock && !updateBattery) return;

	frmP = FrmGetActiveForm();

	#ifndef PALMOS_30
	WinPushDrawState();
	#endif

	if (g_prefs.depth > 1)
	{
		if (g_prefs.options & OPINVERT)
		{
			WinSetTextColor(g_prefs.backColor);
			WinSetForeColor(g_prefs.backColor);
			WinSetBackColor(g_prefs.textColor);
		}
		else
		{
			WinSetTextColor(g_prefs.textColor);
			WinSetForeColor(g_prefs.textColor);
			WinSetBackColor(g_prefs.backColor);
		}
	}
	else
	{
		if (g_prefs.options & OPINVERT)
		{
			WinSetTextColor(0);
			WinSetBackColor(1);
		}
		else
		{
			WinSetTextColor(1);
			WinSetBackColor(0);
		}
	}

	if (updateClock)
	{
		TimeToAscii(clock.hours, clock.minutes, tfColon, timeStr);
		FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewClockGadget), &bounds);
		oldFont = FntSetFont(numberFont);
		WinEraseRectangle(&bounds, 0);
		#ifdef USE_SONY_HIRES
		bounds.topLeft.x <<= 1;
		bounds.topLeft.y <<= 1;
		#endif
		LCDWinDrawChars(timeStr, StrLen(timeStr), bounds.topLeft.x, bounds.topLeft.y);
	}
	
	if (updateBattery)
	{
		if (g_prefs.depth == 1)
		{
			if (g_prefs.options & OPINVERT)
			{
				WinSetTextColor(1);
				WinSetBackColor(0);
			}
		}
		else
			WinSetForeColor(199);

		FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewBatteryGadget), &bounds);
		rect.topLeft.x = bounds.topLeft.x;
		rect.topLeft.y = bounds.topLeft.y;
		rect.extent.x = percent / 5;
		rect.extent.y = bounds.extent.y;
		WinDrawRectangle(&rect, 0);
		if (g_prefs.depth == 1)
		{
			WinInvertLine(
				rect.topLeft.x, rect.topLeft.y,
				rect.topLeft.x + rect.extent.x - 2, rect.topLeft.y);
		}
		rect.topLeft.x += rect.extent.x;
		rect.extent.x = bounds.extent.x - rect.extent.x;
		WinEraseRectangle(&rect, 0);
	}

	#ifdef PALMOS_30
	FntSetFont(oldFont);
	#else
	WinPopDrawState();
	#endif
}

static Int16 SearchAllDA(Char* titleP[])
{
	DmSearchStateType	state;
	Err					err;
	UInt16				cardNo;
	LocalID				id;
	Int16				count;
	
	count = 0;
	err = DmGetNextDatabaseByTypeCreator(
		true, &state, 'DAcc', 0, false, &cardNo, &id);
	if (err == dmErrCantFind) return count;
	
	while (err != dmErrCantFind)
	{
		if (titleP)
		{
			DmDatabaseInfo(cardNo, id,
				titleP[count], NULL, NULL,
				NULL, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL);
		}
		count++;
		err = DmGetNextDatabaseByTypeCreator(
			false, &state, 'DAcc', 0, false, &cardNo, &id);
	}
	
	return count;
}

static void ExternalDA(Boolean choose)
{
	LocalID		id;
	DmOpenRef	ref;
	MemHandle	codeH;
	void		(*DAMain)();
	FormPtr		saved_form;
	UInt16		saved_form_id;
	WinHandle	saved_active_win;
	WinHandle	saved_draw_win;
	FontID		saved_font;
	
/*
	id = DmFindDatabase(0, "VBookDAL");
	if (id)
	{
		SysAppLaunch(0, id, sysAppLaunchFlagSubCall,
			sysAppLaunchCmdCustomBase, NULL, NULL);
	}
	return;
	
	FrmGotoForm(VDictForm);
	return;
*/	
	
	if (g_prefs2.daName[0] == 0 || choose)
	{
		// Choose dictionary.
		Int16	i, count;
		Char	**titlePP;
		FormPtr	frmP;
		ListPtr	lstP;
		
		count = SearchAllDA(NULL);
		if (count == 0) return;
		
		titlePP = (Char**)MemPtrNew(sizeof(Char*) * count);
		for (i = 0; i < count; i++)
			titlePP[i] = (Char*)MemPtrNew(dmDBNameLength);
		SearchAllDA(titlePP);
		
		frmP = FrmInitForm(DicDAForm);
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DicDADAList));
		LstSetListChoices(lstP, titlePP, count);
		LstSetSelection(lstP, 0);
		if (FrmDoDialog(frmP) == DicDAChooseButton)
		{
			i = LstGetSelection(lstP);
			if (i != noListSelection)
				StrCopy(g_prefs2.daName, titlePP[i]);
		}
		FrmDeleteForm(frmP);
		
		for (i = 0; i < count; i++)
			MemPtrFree(titlePP[i]);
		MemPtrFree(titlePP);
		
		if (g_prefs2.daName[0] == 0)
			return;
	}

	id = DmFindDatabase(0, g_prefs2.daName);
	if (id != 0)
	{
		ref = DmOpenDatabase(0, id, dmModeReadOnly);
		codeH = DmGet1Resource('code', 1000);
		if (codeH)
		{
			DAMain = MemHandleLock(codeH);
			saved_form = FrmGetActiveForm();
			saved_form_id = FrmGetActiveFormID();
			saved_active_win = WinGetActiveWindow();
			saved_draw_win = WinGetDrawWindow();
			saved_font = FntSetFont(stdFont);
			DAMain();
			MemHandleUnlock(codeH);
			if (saved_form_id != 0) FrmSetActiveForm(saved_form);
			WinSetActiveWindow(saved_active_win);
			WinSetDrawWindow(saved_draw_win);
			FntSetFont(saved_font);
			DmReleaseResource(codeH);
		}
		DmCloseDatabase(ref);
	}
	else
	{
		g_prefs2.daName[0] = 0;
	}
}


#pragma mark === Scroll Bar control ===

static Int16 ScrollGetSliderLen()
{
	Int16	len;
	
	len = (Int16)(
		((Int32)scrollBar.dataP->pageSize * scrollBar.bounds.extent.x) /
		scrollBar.dataP->maxValue);

	if (len < MIN_SLIDER_LEN)
		len = MIN_SLIDER_LEN;

	return len;
}

static Int16 ScrollGetSliderPos(Int16 sliderLen)
{
	if (scrollBar.dataP->maxValue - scrollBar.dataP->pageSize <= 0)
		return 0;

	return (Int16)((scrollBar.dataP->value * (scrollBar.bounds.extent.x - sliderLen)) /
			(scrollBar.dataP->maxValue - scrollBar.dataP->pageSize));
}

static void ScrollDraw()
{
	RectangleType rect;
	
	if (!(g_prefs.options & OPTOOLBAR))
		return;
	
	#ifdef PALMOS_30

	WinEraseRectangle(&scrollBar.bounds, 0);
	rect.topLeft.x = scrollBar.bounds.topLeft.x + 1;
	rect.topLeft.y = scrollBar.bounds.topLeft.y + 1;
	rect.extent.x = scrollBar.bounds.extent.x - 2;
	rect.extent.y = scrollBar.bounds.extent.y - 2;
	WinDrawRectangleFrame(rectangleFrame, &rect);

	#else

	WinPushDrawState();

	WinSetForeColor(UIColorGetTableEntryIndex(UIFormFrame));
	WinSetBackColor(UIColorGetTableEntryIndex(UIFormFill));

	WinSetPatternType(grayPattern);
	WinPaintRectangle(&scrollBar.bounds, 0);
	WinSetPatternType(blackPattern);

	#endif

	if (scrollBar.dataP->value != -1)
	{
		rect.topLeft.x = scrollBar.bounds.topLeft.x;
		rect.topLeft.y = scrollBar.bounds.topLeft.y;
		rect.extent.x = ScrollGetSliderLen();
		rect.topLeft.x += ScrollGetSliderPos(rect.extent.x);
		rect.extent.y = scrollBar.bounds.extent.y;
		scrollBar.dataP->pos = rect.topLeft.x;

		#ifdef PALMOS_30
		WinDrawRectangle(&rect, 0);
		#else
		WinPaintRectangle(&rect, 0);
		#endif
	}

	#ifndef PALMOS_30
	WinPopDrawState();
	#endif
}

static void ScrollTapped(Coord x)
{
	Int32	offset;
	
	if (scrollBar.bounds.extent.x == ScrollGetSliderLen())
		return;

	offset = (Int32)(x - scrollBar.bounds.topLeft.x) * (Int32)BookGetChapterLen() /
		scrollBar.bounds.extent.x;
	
	ViewLoadByPos(offset);
}

#pragma mark === Data Process ===

static WordClass GetWordClass(UInt8* wordP)
{
	UInt8	*p = wordP, i;
	
	if (*p <= 0x80)
	{
		if (*p >= 0x30 && *p <= 0x39)
			return WC_NUMBER;
		else if ((*p >= 0x41 && *p <= 0x5a) || (*p >= 0x61 && *p <= 0x7a))
			return WC_ENG_WORD;
		
		for (i=0; engPunctBegin[i]; i++)
			if (*p == engPunctBegin[i])
				return WC_PUNCT_BEGIN;
		
		for (i=0; engPunctEnd[i]; i++)
			if (*p == engPunctEnd[i])
				return WC_PUNCT_END;
	}
	else
	{
		UInt16	pattern;
		
		pattern = *p << 8;
		pattern |= *(p + 1);
		
		if (*p == 0xa1 || *p == 0xa3)
		{
			for (i=0; chsPunctBegin[i]; i++)
				if (pattern == chsPunctBegin[i])
					return WC_PUNCT_BEGIN;
			
			for (i=0; chsPunctEnd[i]; i++)
				if (pattern == chsPunctEnd[i])
					return WC_PUNCT_END;
			
			for (i=0; chsPunctTwin[i]; i++)
				if (pattern == chsPunctTwin[i])
					return WC_PUNCT_TWIN;
		}
		else
			return WC_CHS_WORD;
	}

	return WC_MISC;
}

static void GetBreakPoint(UInt8* p, UInt8** bp, WordClass* lwc)
{
	static Int8	c = 0;
	WordClass	wc = GetWordClass(p);
	
	if (wc == WC_PUNCT_END)
		return;
	
	switch (*lwc)
	{
	case WC_CHS_WORD:
	case WC_MISC:
		*bp = p;
		*lwc = wc;
		break;
	
	case WC_NUMBER:
		if (wc != WC_NUMBER)
		{
			*bp = p;
			*lwc = wc;
		}
		break;
	
	case WC_ENG_WORD:
		if (wc != WC_ENG_WORD)
		{
			*bp = p;
			*lwc = wc;
		}
		break;
	
	case WC_PUNCT_BEGIN:
		if (wc == WC_PUNCT_BEGIN && p - *bp > 1)
		{
			*bp = p;
			*lwc = wc;
		}
		else if (wc != WC_PUNCT_BEGIN)
		{
			if (wc == WC_ENG_WORD || wc == WC_NUMBER)
			{
				c = 1;
				break;
			}

			if (wc == WC_PUNCT_TWIN)
			{
				if (c == 1)
				{
					*bp = p;
					*lwc = wc;
					c = 0;
				}
				else
					c = 1;
				break;
			}

			c++;
			if (c >= 2)
			{
				*bp = p;
				*lwc = wc;
				c = 0;
			}
		}
		break;
	
	case WC_PUNCT_TWIN:
		if (wc != WC_PUNCT_TWIN)
		{
			*bp = p;
			*lwc = wc;
		}
		break;
	}
}

static Boolean UseBreakPoint(UInt8* wordP, UInt8* bp, WordClass lwc)
{
	WordClass	wc;
	Boolean		use = false;
	
	if (bp == NULL) return false;
	
	wc = GetWordClass(wordP);
	
	if (lwc == WC_ENG_WORD && wc == WC_ENG_WORD)
	{
		use = true;
	}
	else if (lwc == WC_NUMBER && wc == WC_NUMBER)
	{
		use = true;
	}
	else if (lwc == WC_PUNCT_BEGIN && wc != WC_PUNCT_END)
	{
		if (wc == WC_CHS_WORD && wordP - bp == 2)
			use = true;
		else if (wc == WC_ENG_WORD || wc == WC_NUMBER)
			use = true;
	}
	else if (lwc == WC_PUNCT_TWIN)
	{
		use = true;
	}
	else if (wc == WC_PUNCT_END)
	{
		use = true;
	}
	
	return use;
}

static void HistoryAdd()
{
	history.list[history.tail].chapter = buffer.chapterIndex;
	history.list[history.tail].offset =
		buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);

	if (history.head == -1)
		history.head = history.tail;
	history.tail = CYCLIC_INCREASE(history.tail, HISTORY_NUM);
	if (history.tail == history.head)
		history.head = CYCLIC_INCREASE(history.head, HISTORY_NUM);
}

static void ViewBlockCopy(UInt8* desP, UInt8* srcP, UInt16 size)
{
	UInt16	i;
	
	for (i=0; i < size; i++)
		*desP++ = *srcP++;
}

static void ViewParsingInit()
{
	cmdState.isLink = false;
	MemSet(pieces, sizeof(PieceType) * MAX_PIECES, 0);
	piecesIndex = 0;
	view.top = view.bottom = -1;
}

// Command always in a data block.
static UInt8* ViewParseCmd(UInt8* cmdP, Int8* cmdLenP)
{
	switch (*cmdP)
	{
	case CMDLINK:
		cmdState.isLink = true;
		cmdP++;
		cmdState.linkData = *cmdP++;
		cmdState.linkData <<= 8;
		cmdState.linkData |= *cmdP++;
		if (cmdLenP) *cmdLenP = 3;
		break;
		
	case CMDLINKEND:
		cmdState.isLink = false;
		cmdP++;
		if (cmdLenP) *cmdLenP = 1;
		break;

	default:
		if (ISCMD0P(*cmdP))
		{
			cmdP++;
			if (cmdLenP) *cmdLenP = 1;
		}
		else if (ISCMD1P(*cmdP))
		{
			cmdP += 2;
			if (cmdLenP) *cmdLenP = 2;
		}
		else if (ISCMD2P(*cmdP))
		{
			cmdP += 3;
			if (cmdLenP) *cmdLenP = 3;
		}
	}

	return cmdP;
}

static void ViewInvalidatePieces(Int16 from)
{
	UInt8 lineState = pieces[from].state;
	
	while (pieces[from].textP && pieces[from].state == lineState)
	{
		pieces[from].textP = NULL;
		from = CYCLIC_INCREASE(from, MAX_PIECES);
	}
}

// Return false if no block can be load.
static Boolean ViewLoadPrevBlock()
{
	UInt16	dataLen;
	UInt32	endLen;
	Char*	tooFar;
	Int16	i;

	if (buffer.topBufNo == 0) // Alreay at first block, just return.
		return false;
	
	// Move top buffer to bottom.
	ViewBlockCopy(buffer.midP, buffer.startP, buffer.topBufLen);
	buffer.midP[buffer.topBufLen] = 0;
	buffer.botBufNo = buffer.topBufNo;
	buffer.botBufLen = buffer.topBufLen;

	// Load data block to top buffer.
	buffer.topBufNo--;
	// Get data block length first.
	BookGetBlock(buffer.topBufNo, NULL, &dataLen, 0, NULL);
	buffer.startP = buffer.midP - dataLen;
	*(buffer.startP - 1) = 0;
	// Get data.
	BookGetBlock(buffer.topBufNo, buffer.startP, NULL, dataLen, &endLen);
	buffer.topBufLen = dataLen;
	buffer.startPos = endLen - dataLen;

	// Adjust parsed pieces.
	
	tooFar = (Char*)buffer.midP + buffer.botBufLen;
	i = piecesIndex;
	while (pieces[i].textP)
	{
		pieces[i].textP += buffer.botBufLen;
		if (pieces[i].textP >= tooFar)
			pieces[i].textP = NULL;
		i = CYCLIC_DECREASE(i, MAX_PIECES);
		if (i == piecesIndex)
			break;
	}

	return true;
}

// Return false if next block can be load.
static Boolean ViewLoadNextBlock()
{
	UInt8	lineState;
	Int16	nextBlockIndex, i;
	UInt16	dataLen;
	UInt32	endLen;
	
	nextBlockIndex = (buffer.botBufNo == -1) ? buffer.topBufNo : buffer.botBufNo;
	nextBlockIndex++;
	
	if (nextBlockIndex == (Int16)BookGetChapterDataNum())
		return false;
	
	if (buffer.botBufNo >= 0)
	{
		buffer.startPos += buffer.topBufLen;
		// Move bottom buffer to top.
		buffer.startP = buffer.midP - buffer.botBufLen;
		*(buffer.startP - 1) = 0;
		ViewBlockCopy(buffer.startP, buffer.midP, buffer.botBufLen);
		buffer.topBufNo = buffer.botBufNo;
		buffer.topBufLen = buffer.botBufLen;

		// Adjust parsed pieces.
		if (pieces[piecesIndex].textP)
			i = piecesIndex;
		else
			i = CYCLIC_DECREASE(piecesIndex, MAX_PIECES);
		while (pieces[i].textP)
		{
			pieces[i].textP -= buffer.botBufLen;
			if (pieces[i].textP < (Char*)buffer.startP)
			{
				lineState = pieces[i].state;
				while (pieces[i].state == lineState)
				{
					pieces[i].textP = NULL;
					i = CYCLIC_INCREASE(i, MAX_PIECES);
				}
				break;
			}

			i = CYCLIC_DECREASE(i, MAX_PIECES);
			if (i == piecesIndex)
				break;
		}
	}

	// Get block length.
	BookGetBlock(nextBlockIndex, NULL, &dataLen, 0, NULL);
	// Get block data.
	BookGetBlock(nextBlockIndex, buffer.midP, NULL, dataLen, &endLen);
	buffer.midP[dataLen] = 0;
	buffer.botBufNo = nextBlockIndex;
	buffer.botBufLen = dataLen;
	
	return true;
}

static void ViewLoadChapter(Boolean reload)
{
	UInt16	dataIndex, offset, dataLen;
	UInt32	endLen;

	ViewParsingInit();
	
	offset = find.offset = 0;

	BookOpenChapter(buffer.chapterIndex);

	if (buffer.offset >= BookGetChapterLen())
		buffer.offset = BookGetChapterLen() - 1;

	if (reload)
	{
		if (buffer.openOnLocation)
		{
			BookGetDataIndexByOffset(buffer.offset, &dataIndex, &offset);
		}
		else
		{
			BookGetDataIndexByAnchor(buffer.anchor, &dataIndex, &offset);
			buffer.chapterIndex = BookGetChapterIndex();
		}
		
		buffer.topBufNo = dataIndex;
		buffer.botBufNo = -1;

		// Get block length.
		BookGetBlock(dataIndex, NULL, &dataLen, 0, NULL);
		buffer.startP = buffer.midP - dataLen;
		*(buffer.startP - 1) = 0;
		// Get data.
		BookGetBlock(dataIndex, buffer.startP, NULL, dataLen, &endLen);
		buffer.startP[dataLen] = 0;
		buffer.topBufLen = dataLen;
		buffer.startPos = endLen - dataLen;
	}
	else
	{
		offset = (UInt16)(buffer.offset - buffer.startPos);
	}

	buffer.offset = 0;
	if (offset)
		buffer.offset = ViewGetCorrectStartPoint(buffer.startPos + offset);
	
	if (reload)
	{
		vertData.maxValue = (Int32)BookGetChapterLen();
		vertData.value = -1;
		vertData.pageSize = -1;
	}
}


static Char* ViewGetPrevParagraph(Char* textP)
{
	UInt8	*p, *tooFar, *paraStart;
	
	p = buffer.startP;
	tooFar = (UInt8*)textP;
	paraStart = NULL;
	
	while (true)
	{
		while (p < tooFar)
		{
			if (ISLINEFEED(*p))
				paraStart = p + 1;

			if (ISCOMMAND(*p))
				p = ViewParseCmd(p, NULL);
			else
				p++;
		}
		
		if (paraStart) break;

		if (ViewLoadPrevBlock())
		{
			p = buffer.startP;
			tooFar = buffer.midP;
		}
		else // At the begin of buffer.
		{
			paraStart = buffer.startP;
			break;
		}
	}

	return (Char*)paraStart;
}

/*
static Char* ViewGetPrevParagraph(Char* textP)
{
	UInt8	*p, *q, *tooFar;
	Int32	prevParagraph;
	UInt32	absOffset;
	Boolean	inBuffer, found;

	p = buffer.startP;
	cmdState.isLink = false;
	tooFar = (buffer.botBufNo == -1) ? buffer.midP : buffer.midP + buffer.botBufLen;
	q = (UInt8*)textP;
	absOffset = buffer.startPos + ((UInt8*)textP - buffer.startP);
	prevParagraph = -1;
	inBuffer = true;
	found = false;

	while (true)
	{	
		while (!ISLINEFEED(*p) && p < tooFar)
		{
			if (inBuffer && p >= q)
			{
				if (prevParagraph != -1)
					found = true;
				else
				{
					if (ViewLoadPrevBlock())
					{
						p = buffer.startP;
						tooFar = buffer.midP + buffer.botBufLen;
						inBuffer = (absOffset >= buffer.startPos &&
							absOffset < buffer.startPos + buffer.topBufLen + buffer.botBufLen)
							? true : false;
						if (inBuffer)
							q = buffer.startP + (absOffset - buffer.startPos);
						prevParagraph = -1;
						continue;
					}
					else // At the begin of buffer.
					{
						prevParagraph = 0;
						found = true;
					}
				}
				
				break;
			}
			
			if (ISCOMMAND(*p))
				p = ViewParseCmd(p, NULL);
			else
				p++;
		}
		
		if (found) break;
		
		if (ISLINEFEED(*p))
		{
			p++;
			if (buffer.startPos + (p - buffer.startP) >= absOffset)
			{
				if (prevParagraph != -1)
					break;
				
				if (ViewLoadPrevBlock())
				{
					p = buffer.startP;
					tooFar = buffer.midP + buffer.botBufLen;
					inBuffer = (absOffset >= buffer.startPos &&
						absOffset < buffer.startPos + buffer.topBufLen + buffer.botBufLen)
						? true : false;
					if (inBuffer)
						q = buffer.startP + (absOffset - buffer.startPos);
				}
				else // At the begin of buffer.
					prevParagraph = 0;
				continue;
			}
			prevParagraph = (Int32)(buffer.startPos + (p - buffer.startP));
		}
		else // Reached the bottom of buffer.
		{
			ViewLoadNextBlock();
			p = buffer.midP;
			tooFar = buffer.midP + buffer.botBufLen;
			inBuffer = (absOffset >= buffer.startPos &&
				absOffset < buffer.startPos + buffer.topBufLen + buffer.botBufLen)
				? true : false;
			if (inBuffer)
				q = buffer.startP + (absOffset - buffer.startPos);
		}
	}
	
	absOffset = buffer.startPos + buffer.topBufLen;
	if (buffer.botBufNo != -1)
		absOffset += buffer.botBufLen;
	if (prevParagraph < (Int32)buffer.startPos ||
		prevParagraph >= (Int32)absOffset)
	{
		buffer.openOnLocation = true;
		buffer.offset = (UInt32)prevParagraph;
		ViewLoadChapter(true);
	}
	
	return (Char*)buffer.startP + ((UInt32)prevParagraph - buffer.startPos);
}
*/

static Char* ViewGetNextParagraph(Char* textP)
{
	UInt8	*p, *tooFar;

	p = (UInt8*)textP;
	tooFar = (buffer.botBufNo != -1) ? buffer.midP + buffer.botBufLen : buffer.midP;
	
	while (!ISLINEFEED(*p))
	{
		if (p == tooFar)
		{
			if (ViewLoadNextBlock())
			{
				p = buffer.midP;
				tooFar = buffer.midP + buffer.botBufLen;
			}
			else
				return NULL;
		}

		if (ISCOMMAND(*p))
			p = ViewParseCmd(p, NULL);
		else
			p++;
	}
	p++;

	return (Char*)p;
}

// This function needs a clean pieces array.
static UInt32 ViewGetCorrectStartPoint(UInt32 offset)
{
	Char	*p;
	Int16	i, head;
	UInt8	lineState;
	UInt32	pos;
	UInt16	len;

	#ifdef PALMOS_30
	FontID	oldFont;
	#endif

	#ifdef PALMOS_30
	oldFont = LCDFntSetFont(CfnGetFont());
	#else
	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif // PALMOS_50
	#endif // PALMOS_30
	
	// Make sure the offset is in buffer.
	if (offset < buffer.startPos ||
		offset >= buffer.startPos + buffer.topBufLen +
		((buffer.botBufNo != -1) ? buffer.botBufLen : 0))
	{
		UInt16	dataIndex, dataOffset, dataLen;
		UInt32	endLen;
		
		BookGetDataIndexByOffset(offset, &dataIndex, &dataOffset);
		
		buffer.topBufNo = dataIndex;
		buffer.botBufNo = -1;
		
		BookGetBlock(dataIndex, NULL, &dataLen, 0, NULL);
		buffer.startP = buffer.midP - dataLen;
		*(buffer.startP - 1) = 0;
		BookGetBlock(dataIndex, buffer.startP, NULL, dataLen, &endLen);
		buffer.startP[dataLen] = 0;
		buffer.topBufLen = dataLen;
		buffer.startPos = endLen - dataLen;
	}
	
	p = (Char*)buffer.startP + (offset - buffer.startPos);
	p = ViewGetPrevParagraph(p);
	
	ViewParsingInit();
	
	lineState = 0;
	head = piecesIndex;
	
	while (true)
	{
		i = piecesIndex;
		ViewBreakParagraph(p, lineState);
		if (i == piecesIndex) break;
		
		while (i != piecesIndex)
		{
			pos = len = 0;
			head = i;
			while (i != piecesIndex && pieces[i].state == lineState)
			{
				if (pos == 0)
					pos = buffer.startPos + ((UInt8*)pieces[i].textP - buffer.startP);
				len += pieces[i].parsedLen;
				i = CYCLIC_INCREASE(i, MAX_PIECES);
			}
			if (offset >= pos && offset < pos + len)
			{
				i = head;
				break;
			}
			lineState = !lineState;
		}
		if (i != piecesIndex) break;
		
		i = CYCLIC_DECREASE(i, MAX_PIECES);
		p = pieces[i].textP + pieces[i].parsedLen;
	}

	#ifdef PALMOS_30
	LCDFntSetFont(oldFont);
	#else
	WinPopDrawState();
	#endif
	
	piecesIndex = head;
	cmdState.isLink = (pieces[head].cmd == CMDLINK) ? true : false;
	if (cmdState.isLink)
		cmdState.linkData = pieces[head].data;
	
	return buffer.startPos + ((UInt8*)pieces[head].textP - buffer.startP);
}

static void ViewInitCmd(Int16 pi)
{
	UInt8	*p, *q;
	
	cmdState.isLink = (pieces[pi].cmd == CMDLINK) ? true : false;
	if (cmdState.isLink)
		cmdState.linkData = pieces[pi].data;
	p = (UInt8*)pieces[pi].textP + pieces[pi].len;
	q = (UInt8*)pieces[pi].textP + pieces[pi].parsedLen;
	while (p < q)
	{
		p = ViewParseCmd(p, NULL);
		if (ISLINEFEED(*p)) p++;
	}
}

// Parsing correct word break points, and save them in pieces array.
// Parsing terminated when reached the end of paragraph or
// parsed data can be displayed in a whole screen.
static void ViewBreakParagraph(Char* textP, UInt8 state)
{
	Int8		cmdLen;
	UInt8		*p, *tooFar, *breakPoint, step, lineState, chs;
	Int16		charWidth, pieceWidth, totalWidth, maxWidth,
				pieceHeight, totalHeight, bpi, lineStart;
	Boolean		move, split;
	WordClass	lwc;
	
	p = (UInt8*)textP;
	if (*p == 0)
	{
		if (ViewLoadNextBlock())
			p = buffer.midP;
		else
			return;
	}
	
	tooFar = (buffer.botBufNo != -1) ? buffer.midP + buffer.botBufLen : buffer.midP;
	lineState = state;
	pieces[piecesIndex].textP = (ISCOMMAND(*p) || ISLINEFEED(*p)) ? NULL : (Char*)p;
	pieces[piecesIndex].len = 0;
	pieces[piecesIndex].parsedLen = 0;
	pieces[piecesIndex].bounds.extent.x = 0;
	pieces[piecesIndex].chs = 0;
	pieces[piecesIndex].state = state;
	maxWidth = view.bounds.extent.x - g_prefs2.leftMargin - g_prefs2.rightMargin;
	pieceWidth = totalWidth = totalHeight = 0;
	lineStart = piecesIndex;
	breakPoint = NULL;
	split = false;
	lwc = WC_MISC;
	
	if (g_prefs2.indent && ISPARABEGIN(p))
		totalWidth += PARAINDENT();
	
	while (*p)
	{
		if (ISLINEFEED(*p))
		{
			if (pieces[piecesIndex].textP)
			{
				if (pieces[piecesIndex].bounds.extent.x == 0)
				{
					pieces[piecesIndex].bounds.extent.x = pieceWidth;
					pieces[piecesIndex].bounds.extent.y =
						CfnCharsHeight(pieces[piecesIndex].textP, pieces[piecesIndex].len);
					if (cmdState.isLink)
					{
						pieces[piecesIndex].cmd = CMDLINK;
						pieces[piecesIndex].data = cmdState.linkData;
						if (pieces[piecesIndex].chs)
							pieces[piecesIndex].bounds.extent.y += UNDERLINE_FOR_CHS;
					}
					else
						pieces[piecesIndex].cmd = CMDNONE;
				}
			}
			else // ¿ÕÐÐ
			{
				pieces[piecesIndex].textP = (Char*)p;
				pieces[piecesIndex].bounds.extent.x = maxWidth;
				pieces[piecesIndex].bounds.extent.y = SPACE_LINE_HEIGHT;
				pieces[piecesIndex].cmd = CMDLINEFEED;
				pieces[piecesIndex].chs = 1;
			}
			pieces[piecesIndex].parsedLen++;
			piecesIndex = CYCLIC_INCREASE(piecesIndex, MAX_PIECES);
			ViewInvalidatePieces(piecesIndex);
			break;
		}
		else if (ISCOMMAND(*p)) // Process commands.
		{
			if (pieces[piecesIndex].textP)
			{
				pieces[piecesIndex].bounds.extent.x = pieceWidth;
				pieces[piecesIndex].bounds.extent.y =
					CfnCharsHeight(pieces[piecesIndex].textP, pieces[piecesIndex].len);
				if (cmdState.isLink)
				{
					pieces[piecesIndex].cmd = CMDLINK;
					pieces[piecesIndex].data = cmdState.linkData;
					if (pieces[piecesIndex].chs)
						pieces[piecesIndex].bounds.extent.y += UNDERLINE_FOR_CHS;
				}
				else
					pieces[piecesIndex].cmd = CMDNONE;
				
				totalWidth += pieceWidth;
				pieceWidth = 0;
				split = true;
			}
			p = ViewParseCmd(p, &cmdLen);
			if (pieces[piecesIndex].textP)
				pieces[piecesIndex].parsedLen += cmdLen;
		}
		else // Process text.
		{
			if (pieces[piecesIndex].textP == NULL)
			{
				pieces[piecesIndex].textP = (Char*)p;
				pieces[piecesIndex].len = 0;
				pieces[piecesIndex].parsedLen = 0;
				pieces[piecesIndex].bounds.extent.x = 0;
				pieces[piecesIndex].chs = 0;
				pieces[piecesIndex].state = lineState;
			}
			else if (split)
			{
				split = false;
				piecesIndex = CYCLIC_INCREASE(piecesIndex, MAX_PIECES);
				ViewInvalidatePieces(piecesIndex);
				pieces[piecesIndex].textP = (Char*)p;
				pieces[piecesIndex].len = 0;
				pieces[piecesIndex].parsedLen = 0;
				pieces[piecesIndex].bounds.extent.x = 0;
				pieces[piecesIndex].chs = 0;
				pieces[piecesIndex].state = lineState;
			}
			
//			if (*p < 0x20) *p = 0x20;
			
			step = (*p <= 0x80) ? 1 : 2;
			charWidth = CfnCharWidth((Char*)p);
			
			if (totalWidth + pieceWidth + charWidth > maxWidth)
			{
				// Must start a new line.
				// Be sure to use a correct break point.
				
				bpi = piecesIndex;
				if (UseBreakPoint(p, breakPoint, lwc))
				{
					while ((Char*)breakPoint < pieces[bpi].textP)
						bpi = CYCLIC_DECREASE(bpi, MAX_PIECES);

					if (bpi == lineStart)
					{
						if ((Char*)breakPoint == pieces[bpi].textP)
						{
							// Ignore break point, just break it.
							if (pieces[bpi].bounds.extent.x == 0)
							{
								pieces[bpi].bounds.extent.x = pieceWidth;
								pieces[bpi].bounds.extent.y =
									CfnCharsHeight(pieces[bpi].textP, pieces[bpi].len);
								if (cmdState.isLink)
								{
									pieces[bpi].cmd = CMDLINK;
									pieces[bpi].data = cmdState.linkData;
									if (pieces[bpi].chs)
										pieces[bpi].bounds.extent.y += UNDERLINE_FOR_CHS;
								}
								else
									pieces[bpi].cmd = CMDNONE;
							}
							p = (UInt8*)pieces[bpi].textP + pieces[bpi].len;
						}
						else
						{
							pieces[bpi].len = (Char*)breakPoint - pieces[bpi].textP;
							pieces[bpi].parsedLen = pieces[bpi].len;
							pieces[bpi].bounds.extent.x =
								CfnCharsWidth(pieces[bpi].textP, pieces[bpi].len);
							pieces[bpi].bounds.extent.y =
								CfnCharsHeight(pieces[bpi].textP, pieces[bpi].len);
							if (bpi == piecesIndex)
							{
								if (cmdState.isLink)
								{
									pieces[bpi].cmd = CMDLINK;
									pieces[bpi].data = cmdState.linkData;
									if (pieces[bpi].chs)
										pieces[bpi].bounds.extent.y += UNDERLINE_FOR_CHS;
								}
								else
									pieces[bpi].cmd = CMDNONE;
							}
							p = breakPoint;
						}
					}
					else // bpi != lineStart
					{
						if ((Char*)breakPoint == pieces[bpi].textP)
						{
							bpi = CYCLIC_DECREASE(bpi, MAX_PIECES);
							p = (UInt8*)pieces[bpi].textP + pieces[bpi].len;
						}
						else
						{
							pieces[bpi].len = (Char*)breakPoint - pieces[bpi].textP;
							pieces[bpi].parsedLen = pieces[bpi].len;
							pieces[bpi].bounds.extent.x =
								CfnCharsWidth(pieces[bpi].textP, pieces[bpi].len);
							pieces[bpi].bounds.extent.y =
								CfnCharsHeight(pieces[bpi].textP, pieces[bpi].len);
							if (bpi == piecesIndex)
							{
								if (cmdState.isLink)
								{
									pieces[bpi].cmd = CMDLINK;
									pieces[bpi].data = cmdState.linkData;
									if (pieces[bpi].chs)
										pieces[bpi].bounds.extent.y += UNDERLINE_FOR_CHS;
								}
								else
									pieces[bpi].cmd = CMDNONE;
							}
							p = breakPoint;
						}
					}
				}
				else // No use break point.
				{
					if (pieceWidth)
					{
						pieces[bpi].bounds.extent.x = pieceWidth;
						pieces[bpi].bounds.extent.y =
							CfnCharsHeight(pieces[bpi].textP, pieces[bpi].len);
						if (cmdState.isLink)
						{
							pieces[bpi].cmd = CMDLINK;
							pieces[bpi].data = cmdState.linkData;
							if (pieces[bpi].chs)
								pieces[bpi].bounds.extent.y += UNDERLINE_FOR_CHS;
						}
						else
							pieces[bpi].cmd = CMDNONE;
					}
					else
					{
						bpi = CYCLIC_DECREASE(bpi, MAX_PIECES);
						p = (UInt8*)pieces[bpi].textP + pieces[bpi].len;
					}
				}
				
				piecesIndex = CYCLIC_INCREASE(bpi, MAX_PIECES);
				ViewInvalidatePieces(piecesIndex);
				lineState = !pieces[bpi].state;
				cmdState.isLink = (pieces[bpi].cmd == CMDLINK) ? true : false;
				if (cmdState.isLink)
					cmdState.linkData = pieces[bpi].data;
				breakPoint = NULL;
				lwc = WC_MISC;
				
				// Calculate maximum line height.
				pieceHeight = chs = 0;
				bpi = lineStart;
				lineStart = piecesIndex;
				while (pieces[bpi].textP && pieces[bpi].state != lineState)
				{
					pieceHeight = MAX(pieceHeight, pieces[bpi].bounds.extent.y);
					if (pieces[bpi].chs) chs = 1;
					bpi = CYCLIC_INCREASE(bpi, MAX_PIECES);
				}

				totalHeight += pieceHeight;
				if ((g_prefs.options & OPCHSONLY) && chs ||
					!(g_prefs.options & OPCHSONLY))
					totalHeight += g_prefs.lineSpace;

				if (totalHeight >= view.bounds.extent.y)
				{
					ViewInvalidatePieces(piecesIndex);
					break;
				}

				pieceWidth = totalWidth = pieceHeight = 0;
				
				/*
				if (*p == ' ')
					p++;*/
				continue;
			}

			GetBreakPoint(p, &breakPoint, &lwc);
			
			pieces[piecesIndex].len += step;
			pieces[piecesIndex].parsedLen += step;
			if (step == 2)
				pieces[piecesIndex].chs = 1;
			pieceWidth += charWidth;
			p += step;
		}

		// Adjust when be edge of data block.
		if ((*p > 0x80 && p == tooFar - 1) || p == tooFar)
		{
			move = (buffer.botBufNo != -1) ? true : false;
			if (ViewLoadNextBlock())
			{
				p = (p == tooFar) ? buffer.midP : buffer.midP - 1;
				tooFar = buffer.midP + buffer.botBufLen;
				if (move) // If data has shifted, the breakPoint must shifts also.
					breakPoint -= buffer.topBufLen;
			}
			else
			{
				if (pieces[piecesIndex].bounds.extent.x == 0)
				{
					pieces[piecesIndex].bounds.extent.x = pieceWidth;
					pieces[piecesIndex].bounds.extent.y =
						CfnCharsHeight(pieces[piecesIndex].textP, pieces[piecesIndex].len);
					if (cmdState.isLink)
					{
						pieces[piecesIndex].cmd = CMDLINK;
						pieces[piecesIndex].data = cmdState.linkData;
						if (pieces[piecesIndex].chs)
							pieces[piecesIndex].bounds.extent.y += UNDERLINE_FOR_CHS;
					}
					else
						pieces[piecesIndex].cmd = CMDNONE;
				}
				piecesIndex = CYCLIC_INCREASE(piecesIndex, MAX_PIECES);
				ViewInvalidatePieces(piecesIndex);
			}
		}
	}
}

// Return true if need redraw.
static Boolean ViewMoveUp()
{
	UInt32	firstLinePos, startPos;
	Int16	i, j, height, totalHeight;
	Coord	x, y;
	UInt8	lineState;
	Char	*p;
	Boolean	found, chs, pb;

	firstLinePos = buffer.startPos +
		((UInt8*)pieces[view.top].textP - buffer.startP);
	
	if (firstLinePos < 10)
	{
		// Reach top of page if only has commands in front of top line.
		p = (Char*)buffer.startP;
		cmdState.isLink = false;
		while (ISCOMMAND(*p))
			p = (Char*)ViewParseCmd((UInt8*)p, NULL);

		if (p == pieces[view.top].textP)
			return false;
	}

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50
	
	found = false;
	while (true)
	{
		// Check if have enough pieces in buffer.
		totalHeight = 0;
		i = CYCLIC_DECREASE(view.top, MAX_PIECES);
		lineState = pieces[i].state;
		while (pieces[i].textP)
		{
			height = 0;
			chs = false;
			while (pieces[i].textP && pieces[i].state == lineState)
			{
				height = MAX(height, pieces[i].bounds.extent.y);
				if (pieces[i].chs) chs = true;
				j = i;
				i = CYCLIC_DECREASE(i, MAX_PIECES);
			}

			totalHeight += height;
			if ((g_prefs.options & OPCHSONLY) && chs ||
				!(g_prefs.options & OPCHSONLY))
				totalHeight += g_prefs.lineSpace;

			if (totalHeight > view.bounds.extent.y)
			{
				i = j;
				while (pieces[i].state == pieces[j].state)
					i = CYCLIC_INCREASE(i, MAX_PIECES);
				found = true;
				break;
			}
			lineState = pieces[i].state;
		}
		
		if (found)
		{
			if (totalHeight <= view.bounds.extent.y)
			{
				WinPopDrawState();
				
				// Already at beginning of chapter, no enough lines to display.
				buffer.offset = 0;
				ViewParsingInit();
				ViewMoveDown();

				return true;
			}
			break;
		}

		// Not enough lines to display.
		ViewParsingInit();
		// Find enough data to display previous page.
		startPos = (firstLinePos > PAGE_DISTANCE) ?
			ViewGetCorrectStartPoint(firstLinePos - PAGE_DISTANCE) : 0;
		p = (Char*)buffer.startP + (startPos - buffer.startPos);
		
		// Now, parsing line break point.
		lineState = pieces[piecesIndex].state;
		while (true)
		{
			i = piecesIndex;
			ViewBreakParagraph(p, lineState);
			if (i == piecesIndex)
				break;

			// Find next start point.
			j = CYCLIC_DECREASE(piecesIndex, MAX_PIECES);
			p = pieces[j].textP + pieces[j].parsedLen;

			while (i != piecesIndex)
			{
				if (pieces[i].state == lineState)
				{
					startPos = buffer.startPos + ((UInt8*)pieces[i].textP - buffer.startP);
					if (startPos >= firstLinePos)
					{
						found = true;
						break;
					}
					lineState = !pieces[i].state;
				}
				i = CYCLIC_INCREASE(i, MAX_PIECES);
			}

			if (found)
				break;
		}

		view.top = i;
		found = true;
	}

	// Calculate actual coordinates on LCD.
	view.top = i;
	y = view.bounds.topLeft.y;
	while (pieces[i].textP)
	{
		x = view.bounds.topLeft.x + g_prefs2.leftMargin;
		lineState = pieces[i].state;
		height = 0;
		chs = false;
		if (g_prefs2.indent)
			pb = ISPARABEGIN(pieces[i].textP);
		else
			pb = false;
		while (pieces[i].textP && pieces[i].state == lineState)
		{
			if (pb)
			{
				x += PARAINDENT();
				pb = false;
			}
			pieces[i].bounds.topLeft.x = x;
			pieces[i].bounds.topLeft.y = y;
			x += pieces[i].bounds.extent.x;
			height = MAX(height, pieces[i].bounds.extent.y);
			if (pieces[i].chs) chs = true;
			view.bottom = i;
			i = CYCLIC_INCREASE(i, MAX_PIECES);
		}
		
		y += height;
		if ((g_prefs.options & OPCHSONLY) && chs ||
			!(g_prefs.options & OPCHSONLY))
			y += g_prefs.lineSpace;

		if (y >= view.bounds.topLeft.y + view.bounds.extent.y)
			break;
	}

	if (y > view.bounds.topLeft.y + view.bounds.extent.y)
	{
		lineState = pieces[view.bottom].state;
		while (pieces[view.bottom].state == lineState)
			view.bottom = CYCLIC_DECREASE(view.bottom, MAX_PIECES);
	}

	piecesIndex = view.bottom;

	WinPopDrawState();
	
	return true;
}

static void ViewMoveDown()
{
	Int16	i, height;
	Coord	x, y;
	Boolean	run, chs, pb;
	Char	*startP;
	UInt8	lineState;

	if (view.bottom == -1)
	{
		view.top = view.bottom = piecesIndex;
		if (buffer.offset == 0)
		{
			// Start from top of chapter.
			lineState = 0;
			startP = (Char*)buffer.startP;
		}
		else
		{
			lineState = pieces[piecesIndex].state;
			startP = (Char*)buffer.startP + (buffer.offset - buffer.startPos);
		}
	}
	else if (view.bottom == -2) // When redraws current screen.
	{
		lineState = pieces[view.top].state;
		startP = pieces[view.top].textP;
		cmdState.isLink = (pieces[view.top].cmd == CMDLINK) ? true : false;
		if (cmdState.isLink) cmdState.linkData = pieces[view.top].data;
		piecesIndex = view.bottom = view.top;
	}
	else // Moves to the next screen.
	{
		lineState = !pieces[view.bottom].state;
		startP = pieces[view.bottom].textP + pieces[view.bottom].parsedLen;
		ViewInitCmd(view.bottom);
		piecesIndex = view.top = view.bottom = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	}

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50
	
	y = view.bounds.topLeft.y;
	run = true;
	while (true)
	{
		i = piecesIndex;
		if (g_prefs2.indent)
			pb = ISPARABEGIN(startP);
		else
			pb = false;
		ViewBreakParagraph(startP, lineState);
		if (i == piecesIndex) // No line can be break up.
			break;

		// Calculate actual coordinates on LCD.
		while (i != piecesIndex)
		{
			x = view.bounds.topLeft.x + g_prefs2.leftMargin;
			lineState = pieces[i].state;
			height = 0;
			chs = false;
			while (i != piecesIndex && pieces[i].state == lineState)
			{
				if (pb)
				{
					x += PARAINDENT();
					pb = false;
				}
				pieces[i].bounds.topLeft.x = x;
				pieces[i].bounds.topLeft.y = y;
				x += pieces[i].bounds.extent.x;
				if (pieces[i].chs) chs = true;
				height = MAX(height, pieces[i].bounds.extent.y);
				view.bottom = i;
				i = CYCLIC_INCREASE(i, MAX_PIECES);
			}

			y += height;
			if ((g_prefs.options & OPCHSONLY) && chs ||
				!(g_prefs.options & OPCHSONLY))
				y += g_prefs.lineSpace;

			if (y >= view.bounds.topLeft.y + view.bounds.extent.y)
			{
				run = false;
				break;
			}
		}

		if (!run) break;

		i = CYCLIC_DECREASE(i, MAX_PIECES);
		startP = pieces[i].textP + pieces[i].parsedLen;
		lineState = !pieces[i].state;
	}

	// Keep no clipped line.
	y = pieces[view.bottom].bounds.topLeft.y + height;
	if ((g_prefs.options & OPCHSONLY) && chs ||
		!(g_prefs.options & OPCHSONLY))
		y += g_prefs.lineSpace;
	if (y > view.bounds.topLeft.y + view.bounds.extent.y)
	{
		lineState = pieces[view.bottom].state;
		while (pieces[view.bottom].state == lineState)
			view.bottom = CYCLIC_DECREASE(view.bottom, MAX_PIECES);
	}
	piecesIndex = view.bottom;

	WinPopDrawState();
}

static void ViewMoveUpLine()
{
	UInt32	firstLinePos;
	Int16	i, j, height;
	UInt8	lineState;
	Boolean	chs, pb;
	Coord	x, y;
	
	firstLinePos = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
	
	if (!ViewMoveUp()) return;
	j = view.top;
	ViewMoveDown();
	
	// Find the line in the top of view.
	firstLinePos -= buffer.startPos;
	i = j;
	while (true)
	{
		lineState = pieces[i].state;
		while (pieces[i].state == lineState)
			i = CYCLIC_INCREASE(i, MAX_PIECES);
		if ((UInt8*)pieces[i].textP - buffer.startP >= firstLinePos)
			break;
		j = i;
	}	
	view.top = j;
	
	i = j;
	y = view.bounds.topLeft.y;
	while (pieces[i].textP)
	{
		x = view.bounds.topLeft.x + g_prefs2.leftMargin;
		lineState = pieces[i].state;
		height = 0;
		chs = false;
		if (g_prefs2.indent)
			pb = ISPARABEGIN(pieces[i].textP);
		else
			pb = false;
		while (pieces[i].textP && pieces[i].state == lineState)
		{
			if (pb)
			{
				x += PARAINDENT();
				pb = false;
			}
			pieces[i].bounds.topLeft.x = x;
			pieces[i].bounds.topLeft.y = y;
			x += pieces[i].bounds.extent.x;
			height = MAX(height, pieces[i].bounds.extent.y);
			if (pieces[i].chs) chs = true;
			view.bottom = i;
			i = CYCLIC_INCREASE(i, MAX_PIECES);
		}
		
		y += height;
		if ((g_prefs.options & OPCHSONLY) && chs ||
			!(g_prefs.options & OPCHSONLY))
			y += g_prefs.lineSpace;
		
		if (y >= view.bounds.topLeft.y + view.bounds.extent.y)
			break;
	}
	
	if (y > view.bounds.topLeft.y + view.bounds.extent.y)
	{
		lineState = pieces[view.bottom].state;
		while (pieces[view.bottom].state == lineState)
			view.bottom = CYCLIC_DECREASE(view.bottom, MAX_PIECES);
	}
	
	piecesIndex = view.bottom;
	
	ViewDrawScreen();
}

static void ViewMoveDownLine()
{
	Int16	i, first, height;
	UInt8	lineState;
	Coord	x, y;
	Boolean	chs, pb;

	// Find next line from top.
	first = view.top;
	lineState = pieces[first].state;
	while (pieces[first].textP && pieces[first].state == lineState)
		first = CYCLIC_INCREASE(first, MAX_PIECES);
	if (pieces[first].textP == NULL) // No more line can be display.
		return;

	// Keep enough parsed lines in buffer.
	ViewMoveDown();
	
	y = view.bounds.topLeft.y;
	view.top = first;
	lineState = pieces[first].state;
	i = first;
	while (pieces[i].textP)
	{
		x = view.bounds.topLeft.x + g_prefs2.leftMargin;
		height = 0;
		chs = false;
		if (g_prefs2.indent)
			pb = ISPARABEGIN(pieces[i].textP);
		else
			pb = false;
		while (pieces[i].textP && pieces[i].state == lineState)
		{
			if (pb)
			{
				x += PARAINDENT();
				pb = false;
			}
			pieces[i].bounds.topLeft.x = x;
			pieces[i].bounds.topLeft.y = y;
			x += pieces[i].bounds.extent.x;
			height = MAX(height, pieces[i].bounds.extent.y);
			if (pieces[i].chs)
				chs = true;
			i = CYCLIC_INCREASE(i, MAX_PIECES);
		}

		lineState = !lineState;
		y += height;
		if ((g_prefs.options & OPCHSONLY) && chs ||
			!(g_prefs.options & OPCHSONLY))
			y += g_prefs.lineSpace;
		
		if (y >= view.bounds.topLeft.y + view.bounds.extent.y)
			break;
	}
	i = CYCLIC_DECREASE(i, MAX_PIECES);

	if (y > view.bounds.topLeft.y + view.bounds.extent.y)
	{
		lineState = pieces[i].state;
		while (pieces[i].state == lineState)
			i = CYCLIC_DECREASE(i, MAX_PIECES);
	}

	piecesIndex = view.bottom = i;
	
	ViewDrawScreen();
}

static void ViewDrawLine(Int16 pi, RectanglePtr rectP)
{
	Int16	i;
	UInt8	fColor, bColor, lineState;
	
	#ifdef PALMOS_30
	FontID	oldFont = LCDFntSetFont(CfnGetFont());
	#else
	WinPushDrawState();
	WinSetPatternType(grayPattern);
	LCDFntSetFont(CfnGetFont());
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif // PALMOS_50
	#endif

	if (g_prefs.depth > 1)
	{
		if (g_prefs.options & OPINVERT)
		{
			WinSetTextColor(g_prefs.backColor);
			WinSetForeColor(g_prefs.backColor);
			WinSetBackColor(g_prefs.textColor);
			fColor = g_prefs.backColor;
			bColor = g_prefs.textColor;
		}
		else
		{
			WinSetTextColor(g_prefs.textColor);
			WinSetForeColor(g_prefs.textColor);
			WinSetBackColor(g_prefs.backColor);
			fColor = g_prefs.textColor;
			bColor = g_prefs.backColor;
		}
	}
	else
	{
		if (g_prefs.options & OPINVERT)
		{
			#ifndef PALMOS_30
			WinSetTextColor (0);
			WinSetBackColor (1);
			#endif
			fColor = 0;
			bColor = 1;
		}
		else
		{
			#ifndef PALMOS_30
			WinSetTextColor (1);
			WinSetBackColor (0);
			#endif
			fColor = 1;
			bColor = 0;
		}
	}
	
	LCDWinEraseRectangle(rectP, 0);
	
	i = pi;
	lineState = pieces[pi].state;
	while (pieces[i].textP && pieces[i].state == lineState)
	{
		if (pieces[i].cmd != CMDLINEFEED)
		{
			CfnDrawChars(
				pieces[i].textP, pieces[i].len,
				pieces[i].bounds.topLeft.x, pieces[i].bounds.topLeft.y,
				fColor, bColor);

			if (pieces[i].cmd == CMDLINK)
			{
				#ifdef PALMOS_30
				WinDrawGrayLine(
					pieces[i].bounds.topLeft.x,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1,
					pieces[i].bounds.topLeft.x + pieces[i].bounds.extent.x - 1,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1);
				#else
				LCDWinPaintLine(
					pieces[i].bounds.topLeft.x,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1,
					pieces[i].bounds.topLeft.x + pieces[i].bounds.extent.x - 1,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1);
				#endif
			}
		}
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}

	#ifdef PALMOS_30
	LCDFntSetFont(oldFont);
	#else
	WinPopDrawState();
	#endif

	vertData.value = buffer.startPos +
		((UInt8*)pieces[view.top].textP - buffer.startP);
	vertData.pageSize =
		pieces[view.bottom].textP + pieces[view.bottom].parsedLen - pieces[view.top].textP;
	ScrollDraw();
}

static void ViewDrawScreen()
{
	Int16			i, max;
	RectangleType	oldClip, rect;
	UInt8			fColor, bColor, lineState;
	WinHandle		saveWinH/*, bufferWinH*/;
	
//	UInt32	t1, t2; // »æÍ¼ËÙ¶È²âÊÔ

	#ifdef PALMOS_50
//	Int16			j;
//	Boolean			chs;
	#endif // PALMOS_50
	
	#ifdef PALMOS_30
		FontID oldFont = LCDFntSetFont(CfnGetFont());
	#else

	WinPushDrawState();
	WinSetPatternType(grayPattern);
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
	#endif // PALMOS_50

	#endif // PALMOS_30

	if (g_prefs.depth > 1)
	{
		if (g_prefs.options & OPINVERT)
		{
			WinSetTextColor(g_prefs.backColor);
			WinSetForeColor(g_prefs.backColor);
			WinSetBackColor(g_prefs.textColor);
			fColor = g_prefs.backColor;
			bColor = g_prefs.textColor;
		}
		else
		{
			WinSetTextColor(g_prefs.textColor);
			WinSetForeColor(g_prefs.textColor);
			WinSetBackColor(g_prefs.backColor);
			fColor = g_prefs.textColor;
			bColor = g_prefs.backColor;
		}
	}
	else
	{
		if (g_prefs.options & OPINVERT)
		{
			#ifndef PALMOS_30
				WinSetTextColor(0);
				WinSetBackColor(1);
			#endif
			fColor = 0;
			bColor = 1;
		}
		else
		{
			#ifndef PALMOS_30
				WinSetTextColor(1);
				WinSetBackColor(0);
			#endif
			fColor = 1;
			bColor = 0;
		}
	}
	
	LCDWinGetClip(&oldClip);

//	bufferWinH = LCDGetBufferWindow();
//	if (bufferWinH)
	if (l_bufferWinH)
	{
		saveWinH = WinSetDrawWindow(l_bufferWinH);
	}

	LCDWinSetClip(&view.bounds);

	i = view.top;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	lineState = pieces[i].state;
	rect.topLeft.x = 0;

/*	#ifdef PALMOS_50
	rect.extent.x = view.bounds.extent.x;
	#else
	LCDWinEraseRectangle(&view.bounds, 0);
	#endif
*/	

	LCDWinEraseRectangle(&view.bounds, 0);
//	t1 = TimGetTicks();
	while (i != max)
	{
/*		#ifdef PALMOS_50
		// Clear screen line by line.
		if (pieces[i].state == lineState)
		{
			rect.topLeft.y = pieces[i].bounds.topLeft.y;
			rect.extent.y = 0;
			chs = false;
			j = i;
			while (j != max && pieces[j].state == lineState)
			{
				rect.extent.y = MAX(rect.extent.y, pieces[j].bounds.extent.y);
				if (pieces[j].chs) chs = true;
				j = CYCLIC_INCREASE(j, MAX_PIECES);
			}
			lineState = !lineState;

			if ((g_prefs.options & OPCHSONLY) && chs ||
				!(g_prefs.options & OPCHSONLY))
				rect.extent.y += g_prefs.lineSpace;

			LCDWinEraseRectangle(&rect, 0);
			#ifdef PALMOS_30
			if (g_prefs.options & OPINVERT)
				WinInvertRectangle(&rect, 0);
			#endif
		}
		#endif // PALMOS_50
*/		
		if (pieces[i].cmd != CMDLINEFEED)
		{
			CfnDrawChars(
				pieces[i].textP, pieces[i].len,
				pieces[i].bounds.topLeft.x, pieces[i].bounds.topLeft.y,
				fColor, bColor);

			if (pieces[i].cmd == CMDLINK)
			{
/*				#ifdef PALMOS_30
				WinDrawGrayLine(
					pieces[i].bounds.topLeft.x,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1,
					pieces[i].bounds.topLeft.x + pieces[i].bounds.extent.x - 1,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1);
				#else*/

				LCDWinPaintLine(
					pieces[i].bounds.topLeft.x,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1,
					pieces[i].bounds.topLeft.x + pieces[i].bounds.extent.x - 1,
					pieces[i].bounds.topLeft.y + pieces[i].bounds.extent.y - 1);

//				#endif
			}
		}
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
/*	t2 = TimGetTicks();
	{
		Char msg[32];
		StrPrintF(msg, "speed:[ %ld ]", t2 - t1);
		WinDrawChars(msg, StrLen(msg), 0, 0);
	}*/
	
/*	#ifdef PALMOS_50
	if (rect.topLeft.y + rect.extent.y <
		view.bounds.topLeft.y + view.bounds.extent.y)
	{
		rect.topLeft.y += rect.extent.y;
		rect.extent.y = view.bounds.topLeft.y + view.bounds.extent.y - rect.extent.y;
		LCDWinEraseRectangle(&rect, 0);
		#ifdef PALMOS_30
		if (g_prefs.options & OPINVERT)
			WinInvertRectangle(&rect, 0);
		#endif
	}
	#endif
*/	

//	if (bufferWinH)
	if (l_bufferWinH)
	{
		WinSetDrawWindow(saveWinH);
		WinCopyRectangle(
//			bufferWinH, NULL,
			l_bufferWinH, NULL,
			&view.bounds,
			view.bounds.topLeft.x,
			view.bounds.topLeft.y,
			winPaint);
	}
	
	LCDWinSetClip(&oldClip);

	#ifdef PALMOS_30
		LCDFntSetFont(oldFont);
	#else
		WinPopDrawState();
	#endif
	
	vertData.value = buffer.startPos +
		((UInt8*)pieces[view.top].textP - buffer.startP);
	vertData.pageSize =
		pieces[view.bottom].textP + pieces[view.bottom].parsedLen - pieces[view.top].textP;
	ScrollDraw();
}

static void ViewLoadByPos(Int32 offset)
{
	Int32	tooFar;
	Boolean	reload;
	
	tooFar = buffer.startPos + buffer.topBufLen;
	if (buffer.botBufNo != -1)
		tooFar += buffer.botBufLen;
	
	reload = (offset >= buffer.startPos && offset < tooFar) ? false : true;
	
	buffer.offset = (UInt32)offset;
	buffer.openOnLocation = true;
	ViewLoadChapter(reload);
	ViewMoveDown();
	ViewDrawScreen();
}

static void ViewHighlightLink(Int16 linkIndex)
{
	Int16			i, cp, start, end;
	RectangleType	oldClip;

	i = CYCLIC_DECREASE(linkIndex, MAX_PIECES);
	cp = CYCLIC_DECREASE(view.top, MAX_PIECES);
	start = end = -1;
	while (
		i != cp &&
		pieces[i].cmd == pieces[linkIndex].cmd &&
		pieces[i].data == pieces[linkIndex].data &&
		pieces[i].len == pieces[i].parsedLen)
	{
		start = i;
		i = CYCLIC_DECREASE(i, MAX_PIECES);
	}
	start = (start == -1) ? linkIndex : start;

	i = linkIndex;
	cp = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	while (
		i != cp &&
		pieces[i].cmd == pieces[linkIndex].cmd &&
		pieces[i].data == pieces[linkIndex].data)
	{
		end = i;
		if (pieces[i].len != pieces[i].parsedLen) break;
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}

	#ifndef PALMOS_30
	WinPushDrawState();
	if (g_prefs.depth > 1)
	{
		WinSetForeColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}
		#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
		#endif
	WinSetDrawMode(winSwap);
	#endif

	LCDWinGetClip(&oldClip);
	LCDWinSetClip(&view.bounds);

	cp = CYCLIC_INCREASE(end, MAX_PIECES);
	i = start;
	while (i != cp)
	{
		#ifdef PALMOS_30
		WinInvertRectangle(&pieces[i].bounds, 0);
		#else
		LCDWinPaintRectangle(&pieces[i].bounds, 0);
		#endif

		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}

	LCDWinSetClip(&oldClip);

	#ifndef PALMOS_30
	WinPopDrawState();
	#endif
}

static Boolean ViewClickLink(Coord x, Coord y)
{
	Int16		i, max;
	Boolean		found = false;
	EventType	event;
	
	i = view.top;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	while (i != max)
	{
		if (pieces[i].cmd == CMDLINK &&
			RctPtInRectangle(x, y, &pieces[i].bounds))
		{
			found = true;
			break;
		}
		
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
	if (!found)
		return false;

	ViewHighlightLink(i);

	do {
		EvtGetEvent(&event, SysTicksPerSecond() / 4);
	} while (event.eType != penUpEvent && event.eType != nilEvent);

	ViewHighlightLink(i);
	
	buffer.openOnLocation = false;
	buffer.anchor = pieces[i].data;

	// Add current position to history.
	if (history.pos != -1 && history.pos != history.tail)
		history.tail = history.pos;
	HistoryAdd();
	history.pos = history.tail;
	
	ViewUpdate();
	
	return true;
}

static void ViewTap(Coord y)
{
	Int16	i, s, h;
	
	y -= view.bounds.topLeft.y;
	s = 0;
	h = view.bounds.extent.y / 4;
	for (i = 0; i < 3; i++, s += h)
		if (y >= s && y < s + h)
			break;
	
	ViewDoAction(g_prefs.tap.areas[i]);
}

static void ViewUpdate()
{
	if (buffer.openOnLocation == false)
	{
		// Update according anchor.
		if (buffer.anchor == 0xFFFF)
		{
			buffer.openOnLocation = true;
			buffer.offset = 0;
		}
	}
	ViewLoadChapter(true);
	ViewMoveDown();
	ViewDrawScreen();
}

static void UpdateCopyIcon()
{
	FormPtr	frmP = FrmGetActiveForm();
	
	if (!(g_prefs.options & OPTOOLBAR)) return;
	
	if (scrcopy)
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewCopyBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewCopyPressedBitMap));
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewCopyPressedBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewCopyBitMap));
	}
}


static void Goto()
{
	FormPtr		frmP;
	FieldPtr	fieldP;
	MemHandle	txtH, oldTxtH;
	Int16		value;
	Boolean		update = false;
	Char*		p;

	frmP = FrmInitForm(GotoForm);

	// Display total of pages.
	p = MemPtrNew(6);
	StrPrintF(p, "%d", BookGetChapterNum());
	FrmCopyLabel(frmP, GotoTotalLabel, p);
	MemPtrFree(p);

	// Set current page.
	txtH = MemHandleNew(6);
	p = MemHandleLock(txtH);
	StrPrintF(p, "%d", buffer.chapterIndex+1);
	MemHandleUnlock(txtH);

	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, GotoPageField));
	oldTxtH = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);

	if (BookGetChapterNum() > 1)
	{
		FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
		FrmSetFocus(frmP, FrmGetObjectIndex(frmP, GotoPageField));
	}

	// Set precent of current page.
	txtH = MemHandleNew(4);
	p = MemHandleLock(txtH);
	value = (buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP)) *
		100 / BookGetChapterLen();
	StrPrintF(p, "%d", value);
	MemHandleUnlock(txtH);
	
	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, GotoPercentField));
	oldTxtH = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);

	if (BookGetChapterNum() == 1)
	{
		FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
		FrmSetFocus(frmP, FrmGetObjectIndex(frmP, GotoPercentField));
	}

	switch (FrmDoDialog(frmP))
	{
	case GotoGo1Button:
		fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, GotoPageField));
		p = FldGetTextPtr(fieldP);
		if (p)
		{
			value = StrAToI(p);
			if (value > 0 && value <= (Int16)BookGetChapterNum())
			{
				buffer.openOnLocation = true;
				buffer.chapterIndex = (UInt16)value - 1;
				buffer.offset = 0;
				update = true;
			}
		}
		break;
	
	case GotoFirstButton:
		buffer.openOnLocation = true;
		buffer.chapterIndex = 0;
		buffer.offset = 0;
		update = true;
		break;
	
	case GotoLastButton:
		buffer.openOnLocation = true;
		buffer.chapterIndex = BookGetChapterNum()-1;
		buffer.offset = 0;
		update = true;
		break;
		
	case GotoGo2Button:
		fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, GotoPercentField));
		p = FldGetTextPtr(fieldP);
		if (p)
		{
			value = StrAToI(p);
			if (value >= 0 && value < 100)
			{
				buffer.openOnLocation = true;
				buffer.offset = BookGetChapterLen() / 100 * value;
				update = true;
			}
		}
		break;
		
	case GotoTopButton:
		buffer.openOnLocation = true;
		buffer.offset = 0;
		update = true;
		break;
	
	case GotoEndButton:
		buffer.openOnLocation = true;
		buffer.offset = BookGetChapterLen() - 1;
		update = true;
		break;
	}

	FrmDeleteForm(frmP);

	if (update)
		ViewUpdate();
}


#pragma mark === Callbacks ===

static Boolean IsChapterEnd(Int16 pi)
{
	UInt8	*p, *tooFar;
	Int16	blockIndex;

	p = (UInt8*)pieces[pi].textP + pieces[pi].parsedLen;

	if (p <= buffer.midP)
	{
		blockIndex = buffer.topBufNo;
		tooFar = buffer.midP;
	}
	else
	{
		blockIndex = buffer.botBufNo;
		tooFar = buffer.midP + buffer.botBufLen;
	}
	
	if (blockIndex == BookGetChapterDataNum() - 1)
		return (p < tooFar) ? false : true;
	else
		return false;
}


static void ViewFormLayout(FormPtr frmP)
{
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ViewPositionGadget),
		&scrollBar.bounds);

	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ViewViewGadget),
		&view.bounds);

	#ifdef HIGH_DENSITY
	view.bounds.topLeft.x <<= 1;
	view.bounds.topLeft.y <<= 1;
	view.bounds.extent.x <<= 1;
	view.bounds.extent.y <<= 1;
	#endif
}


static Boolean FindText2Callback()
{
	UInt8			*textP, *tooFar, *p, *q;
	UInt32			endPos, endLen;
	UInt16			dataIndex, nextDataIndex, offset, dataLen;
	FormPtr			frmP;
	RectangleType	rect;

	// Do initial work when be called first.
	
	if (find.searchAll)
	{
		if (find.chapter != buffer.chapterIndex)
		{
			buffer.chapterIndex = find.chapter;
			BookOpenChapter(buffer.chapterIndex);
			
			// Get first block of data even if useless.
			buffer.topBufNo = 0;
			buffer.botBufNo = -1;
			
			BookGetBlock(0, NULL, &dataLen, 0, NULL);
			buffer.startP = buffer.midP - dataLen;
			*(buffer.startP - 1) = 0;
			BookGetBlock(0, buffer.startP, NULL, dataLen, &endLen);
			buffer.startP[dataLen] = 0;
			buffer.topBufLen = dataLen;
			buffer.startPos = 0;
		}
	}
	
	endPos = buffer.startPos + buffer.topBufLen;
	if (buffer.botBufNo != -1)
		endPos += buffer.botBufLen;
	
	if (find.offset < buffer.startPos ||
		find.offset >= endPos)
	{
		// When the data to search is not in current buffer,
		// Clean out of current buffer and load a data block in top buffer.
		BookGetDataIndexByOffset(find.offset, &dataIndex, &offset);
		BookGetBlock(dataIndex, NULL, &dataLen, 0, NULL);
		buffer.startP = buffer.midP - dataLen;
		*(buffer.startP - 1) = 0;
		BookGetBlock(dataIndex, buffer.startP, NULL, dataLen, &endLen);
		*buffer.midP = 0;
		
		buffer.startPos = endLen - dataLen;
		buffer.topBufNo = dataIndex;
		buffer.topBufLen = dataLen;
		buffer.botBufNo = -1;
		buffer.botBufLen = 0;
	}
	
	textP = buffer.startP + (find.offset - buffer.startPos);
	tooFar = buffer.startP + buffer.topBufLen;
	if (buffer.botBufNo != -1)
		tooFar += buffer.botBufLen;
	
	p = textP;
	q = (UInt8*)find.token;
	
	while (p < tooFar)
	{
		if (*q == 0)
		{
			find.found = true;
			break;
		}

		if (ISCOMMAND(*p))
		{
			if (ISCMD0P(*p))
				p += 1;
			else if (ISCMD1P(*p))
				p += 2;
			else if (ISCMD2P(*p))
				p += 3;
			
			if (q == (UInt8*)find.token)
				textP = p;
			
			continue;
		}
		
		if (*q == *p)
		{
			q++;
			p++;
		}
		else
		{
			if (!find.matchCase)
			{
				if (ISUPPER(*q)) // A-Z
				{
					if (*q + 0x20 == *p)
					{
						q++;
						p++;
						continue;
					}
				}
				else if (ISLOWER(*q)) // a-z
				{
					if (*q - 0x20 == *p)
					{
						q++;
						p++;
						continue;
					}
				}
			}

			q = (UInt8*)find.token;
			textP++;
			p = textP;
		}
	}
	
	nextDataIndex = (p <= buffer.midP) ? buffer.topBufNo : buffer.botBufNo;
	nextDataIndex++;
	
	frmP = FrmGetActiveForm();

	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewChapterGadget), &rect);
	rect.topLeft.x	+= 1;
	rect.topLeft.y	+= 1;
	rect.extent.x	-= 2;
	rect.extent.y	-= 2;
	rect.extent.x = ((Int32)nextDataIndex * rect.extent.x) / BookGetChapterDataNum();
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
	WinDrawRectangle(&rect, 0);
	
	if (nextDataIndex == BookGetChapterDataNum())
	{
		// Already search at the end of this chapter.
		find.offset = buffer.startPos + (textP - buffer.startP);

		if (find.searchAll)
		{
			if (!find.found && find.chapter + 1 < BookGetChapterNum())
			{
				find.chapter++;
				find.offset = 0;

				FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewChapterGadget), &rect);
				WinSetBackColor(UIColorGetTableEntryIndex(UIObjectFill));
				WinEraseRectangle(&rect, 0);
				rect.topLeft.x	+= 1;
				rect.topLeft.y	+= 1;
				rect.extent.x	-= 2;
				rect.extent.y	-= 2;
				WinSetForeColor(UIColorGetTableEntryIndex(UIObjectFrame));
				WinDrawRectangleFrame(simpleFrame, &rect);

				FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewFileGadget), &rect);
				rect.topLeft.x	+= 1;
				rect.topLeft.y	+= 1;
				rect.extent.x	-= 2;
				rect.extent.y	-= 2;
				rect.extent.x = ((Int32)find.chapter * rect.extent.x) / BookGetChapterNum();
				WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
				WinDrawRectangle(&rect, 0);

				return false;
			}
			else
				return true;
		}
		else
			return true;
	}

	if (!find.found)
	{
		if (buffer.botBufNo >= 0)
		{
			buffer.startPos += buffer.topBufLen;
			buffer.startP = buffer.midP - buffer.botBufLen;
			*(buffer.startP - 1) = 0;
			ViewBlockCopy(buffer.startP, buffer.midP, buffer.botBufLen);
			buffer.topBufNo = buffer.botBufNo;
			buffer.topBufLen = buffer.botBufLen;
			
			textP -= buffer.botBufLen;
		}

		BookGetBlock(nextDataIndex, NULL, &dataLen, 0, NULL);
		BookGetBlock(nextDataIndex, buffer.midP, NULL, dataLen, &endLen);
		buffer.midP[dataLen] = 0;
		buffer.botBufNo = nextDataIndex;
		buffer.botBufLen = dataLen;
	}

	find.offset = buffer.startPos + (textP - buffer.startP);
	
	return find.found;
}


static void FindText2()
{
	UInt16			error;
	EventType		event;
	FormPtr			frmP;
	RectangleType	rect;
	WinHandle		savedWinH;
	Boolean			cancel = false;
	
	// Save context.
	Int16			chapNo, topBufNo, botBufNo;
	UInt32			scrStart, scrEnd;

	chapNo		= buffer.chapterIndex;
	topBufNo	= buffer.topBufNo;
	botBufNo	= buffer.botBufNo;
	scrStart	= buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
	scrEnd 		= buffer.startPos + \
				((UInt8*)pieces[view.bottom].textP + pieces[view.bottom].len - buffer.startP);
	
	// Initialize find state.
	find.found = false;
	
	frmP = FrmGetActiveForm();
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewProgressGadget), &rect);
	savedWinH = WinSaveBits(&rect, &error);
	if (error) return;
	
	WinPushDrawState();
	
	WinSetForeColor(UIColorGetTableEntryIndex(UIDialogFrame));
	WinSetBackColor(UIColorGetTableEntryIndex(UIDialogFill));
	
	WinEraseRectangle(&rect, 0);
	rect.topLeft.x	+= 2;
	rect.topLeft.y	+= 2;
	rect.extent.x	-= 4;
	rect.extent.y	-= 4;
	WinDrawRectangleFrame(boldRoundFrame, &rect);	

	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewChapterGadget), &rect);
	WinSetBackColor(UIColorGetTableEntryIndex(UIObjectFill));
	WinEraseRectangle(&rect, 0);
	rect.topLeft.x	+= 1;
	rect.topLeft.y	+= 1;
	rect.extent.x	-= 2;
	rect.extent.y	-= 2;
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectFrame));
	WinDrawRectangleFrame(simpleFrame, &rect);
	
	if (find.searchAll)
	{
		FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewFileGadget), &rect);
		WinSetBackColor(UIColorGetTableEntryIndex(UIObjectFill));
		WinEraseRectangle(&rect, 0);
		rect.topLeft.x	+= 1;
		rect.topLeft.y	+= 1;
		rect.extent.x	-= 2;
		rect.extent.y	-= 2;
		WinSetForeColor(UIColorGetTableEntryIndex(UIObjectFrame));
		WinDrawRectangleFrame(simpleFrame, &rect);
	}

	FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewPrgCancelButton));
	
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewPrgCancelButton), &rect);

	do {
		EvtGetEvent(&event, 1);
		
		if (!SysHandleEvent(&event))
			if ((event.eType == penDownEvent && RctPtInRectangle(event.screenX, event.screenY, &rect)) ||
				(event.eType == keyDownEvent && NavKeyPressed(&event, Select)))
			{
				cancel = true;
				break;
			}
	
	} while (!FindText2Callback());
	
	WinPopDrawState();

	FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewPrgCancelButton));
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewProgressGadget), &rect);
	WinRestoreBits(savedWinH, rect.topLeft.x, rect.topLeft.y);
	
	if (find.found)
	{
		// Is result in current screen?
		if (find.searchAll || find.offset < scrStart || find.offset >= scrEnd)
		{
			buffer.offset = ViewGetCorrectStartPoint(find.offset);
			ViewMoveDown();
			
			if (find.searchAll && buffer.chapterIndex != chapNo)
				vertData.maxValue = (Int32)BookGetChapterLen();
		}

		ViewDrawScreen();
		HighlightResult();
	}
	else // Restore context.
	{
		UInt16	dataLen;
		UInt32	endLen;
		
		if (find.searchAll && buffer.chapterIndex != chapNo)
		{
			buffer.chapterIndex = chapNo;
			BookOpenChapter(buffer.chapterIndex);
			buffer.topBufNo = -1;
			buffer.botBufNo = -1;
		}
		
		// Restore top text buffer.
		if (buffer.topBufNo != topBufNo)
		{
			BookGetBlock(topBufNo, NULL, &dataLen, 0, NULL);
			buffer.startP = buffer.midP - dataLen;
			*(buffer.startP - 1) = 0;
			BookGetBlock(topBufNo, buffer.startP, NULL, dataLen, &endLen);
			*buffer.midP = 0;
			buffer.topBufNo = topBufNo;
			buffer.topBufLen = dataLen;			
			buffer.startPos = endLen - dataLen;
		}
		// Restore bottom text buffer.
		if (buffer.botBufNo != botBufNo)
		{
			if (botBufNo == -1)
			{
				buffer.botBufNo = -1;
				buffer.botBufLen = 0;
			}
			else
			{
				BookGetBlock(botBufNo, NULL, &dataLen, 0, NULL);
				BookGetBlock(botBufNo, buffer.midP, NULL, dataLen, &endLen);
				buffer.midP[dataLen] = 0;
				buffer.botBufNo = botBufNo;
				buffer.botBufLen = dataLen;
			}
		}

		ViewDrawScreen();
		if (!cancel)
			FrmAlert(NofoundAlert);
	}
}


static void FindText()
{
	FormPtr			frmP;
	FieldPtr		fieldP;
	ControlPtr		ctlP;
	MemHandle		txtH, oldTxtH;
	Char			*p;
	Boolean			doSearch;

	frmP = FrmInitForm(FindForm);
	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindTextField));
	
	if (find.token)
	{
		txtH = MemHandleNew(StrLen(find.token) + 1);
		p = MemHandleLock(txtH);
		StrCopy(p, find.token);
		MemHandleUnlock(txtH);
		
		oldTxtH = FldGetTextHandle(fieldP);
		FldSetTextHandle(fieldP, txtH);
		if (oldTxtH)
			MemHandleFree(oldTxtH);

		FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
	}
	
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindCaseCheckbox));
	CtlSetValue(ctlP, find.matchCase);

	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindWholeCheckbox));
	CtlSetValue(ctlP, find.searchAll);
	
	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, FindTextField));

	doSearch = false;
	switch (FrmDoDialog(frmP))
	{
	case FindFindButton:
		if (FldGetTextPtr(fieldP) == NULL)
			break;
		
		if (find.token)
			MemPtrFree(find.token);
		find.token = MemPtrNew(FldGetTextLength(fieldP) + 1);
		StrCopy(find.token, FldGetTextPtr(fieldP));

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindCaseCheckbox));
		find.matchCase = CtlGetValue(ctlP);
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindWholeCheckbox));
		find.searchAll = CtlGetValue(ctlP);
		
		find.chapter = buffer.chapterIndex;
		find.offset = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);

		doSearch = true;
		break;
		
	case FindTopButton:
		if (FldGetTextPtr(fieldP) == NULL)
			break;

		if (find.token)
			MemPtrFree(find.token);
		find.token = MemPtrNew(FldGetTextLength(fieldP) + 1);
		StrCopy(find.token, FldGetTextPtr(fieldP));

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindCaseCheckbox));
		find.matchCase = CtlGetValue(ctlP);
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindWholeCheckbox));
		find.searchAll = CtlGetValue(ctlP);
		
		find.chapter = (find.searchAll) ? 0 : buffer.chapterIndex;
		find.offset = 0;

		doSearch = true;
		break;
		
	case FindNextButton:
		if (find.token == NULL)
			break;
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindCaseCheckbox));
		find.matchCase = CtlGetValue(ctlP);
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FindWholeCheckbox));
		find.searchAll = CtlGetValue(ctlP);
		
		if (!find.searchAll && find.chapter != buffer.chapterIndex)
			break;

		doSearch = true;
		break;
		
	case FindCancelButton:
		break;
	}
	
	FrmDeleteForm(frmP);
	
	if (!doSearch)
		return;

	#ifdef USE_PALM_SILKSCREEN
	
	frmP = FrmGetFormPtr(ViewForm);

	FrmSetDIAPolicyAttr(frmP, frmDIAPolicyCustom);
	PINSetInputTriggerState(pinInputTriggerEnabled);
	PINSetInputAreaState(pinInputAreaUser);

	WinSetConstraintsSize(
		WinGetWindowHandle(frmP),
		160, 225, 225,
		160, 160, 160);

	#endif // USE_PALM_SILKSCREEN

	FindText2();
}


static Boolean ViewFormDoCommand(UInt16 command)
{
	Boolean handled = true;

	MenuEraseStatus(0);

	switch (command)
	{
	case F3BookClose:
		ViewDoAction(ACTCLOSE);
		break;
	
	case F3BookTOC:
		ViewDoAction(ACTTOC);
		break;
	
	case F3BookAddBookmark:
		if (REGISTERED)
			AddBookmark();
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case F3BookEditBookmarks:
		if (REGISTERED)
			FrmGotoForm(BmkEditForm);
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case F3BookExternalDA:
		ExternalDA(true);
		break;
	
	case F3BookDayNight:
		DayOrNight();
		Minibar();
		ViewDrawScreen();
		break;
	
	case F3EditFind:
		ViewDoAction(ACTFIND);
		break;
	
	case F3EditFindNext:
		if (find.token && REGISTERED)
			FindText2();
		break;
	
	case F3EditScreenCopy:
		if (REGISTERED)
		{
			scrcopy = !scrcopy;
			UpdateCopyIcon();
		}
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case F3EditCopyMode:
		if (REGISTERED)
			FrmGotoForm(EditForm);
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case F3EditGetWord:
		getword = true;
		break;
	
	case F3EditParagraphIndent:
		g_prefs2.indent = !g_prefs2.indent;
		buffer.openOnLocation = true;
		buffer.offset = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
		ViewUpdate();
		break;
	
	case F3ViewHome:
		ViewDoAction(ACTHOME);
		break;
	
	case F3ViewBack:
		ViewDoAction(ACTPREV);
		break;
	
	case F3ViewForward:
		ViewDoAction(ACTNEXT);
		break;
	
	case F3ViewGotoTop:
		buffer.openOnLocation = true;
		buffer.offset = 0;
		ViewLoadChapter(true);
		ViewMoveDown();
		ViewDrawScreen();
		break;
	
	case F3ViewGotoBottom:
		buffer.openOnLocation = true;
		buffer.offset = BookGetChapterLen() - 1;
		ViewLoadChapter(true);
		ViewMoveDown();
		ViewDrawScreen();
		break;
	
	case F3ViewGoto:
		Goto();
		break;
	
	case F3ViewAutoScroll:
		ViewDoAction(ACTPLAY);
		break;
	
	case F3ViewInvert:
		g_prefs.options ^= OPINVERT;
		Minibar();
		ViewDrawScreen ();
		break;

	case F3ViewBatteryClock:
		g_prefs.options ^= OPMINIBAR;
		Minibar();
		SetTimer((g_prefs.options & OPMINIBAR) ? true : false);
		RedrawScreen();
		break;
		
	case F3ViewToolbar:
		g_prefs.options ^= OPTOOLBAR;
		Toolbar();
		RedrawScreen();
		break;
	
	case F3OptionsDisplay:
		CmnDlgSetReturnForm(ViewForm);
		FrmGotoForm(DisplayForm);
		break;
	
	case F3OptionsFont:
		if (MyFontSelect())
		{
			CfnSetFont(g_prefs2.sysFont);
			#ifdef HIGH_DENSITY
			CfnSetEngFont(g_prefs2.engFont);
			#endif
			if (g_prefs.options & OPUSEADDFONT)
				CfnSetFont(4 + g_prefs2.chsFont);
			
			buffer.openOnLocation = true;
			buffer.offset = buffer.startPos +
				((UInt8*)pieces[view.top].textP - buffer.startP);
			
			#ifndef USE_SILKSCREEN
			ViewUpdate();
			#endif
			BookListFontChanged();
			ContentsFontChanged();
		}

		clock.ticks = CLOCK_UPDATE;
		battery.ticks = BATTERY_UPDATE;
		break;
	
	case F3OptionsScroll:
		SetScroll();
		break;
	
	case F3OptionsButtons:
		CmnDlgSetReturnForm(ViewForm);
		FrmGotoForm(ButtonsForm);
		break;
	
	case F3OptionsTap:
		FrmGotoForm(TapForm);
		break;
	
	case F3OptionsView:
		ViewSetting();
		clock.ticks = CLOCK_UPDATE;
		battery.ticks = BATTERY_UPDATE;
		break;
	
	case F3OptionsAboutVBook:
		CmnDlgSetReturnForm(ViewForm);
		FrmGotoForm(AboutForm);
		break;
	
	default:
		handled = false;
	}
	
	return handled;
}

static void PrevChapter()
{
	if (buffer.chapterIndex == 0)
		return;
	
	buffer.openOnLocation = true;
	buffer.chapterIndex--;
	buffer.offset = 0;
	ViewUpdate();
}

static void NextChpater()
{
	if (buffer.chapterIndex == BookGetChapterNum()-1)
		return;
	
	buffer.openOnLocation = true;
	buffer.chapterIndex++;
	buffer.offset = 0;
	ViewUpdate();
}

static void Forward()
{
	if (history.pos == -1 ||
		history.pos == history.tail ||
		history.pos == CYCLIC_DECREASE(history.tail, HISTORY_NUM))
		return;
	
	history.pos = CYCLIC_INCREASE(history.pos, HISTORY_NUM);
	
	buffer.openOnLocation = true;
	buffer.chapterIndex = history.list[history.pos].chapter;
	buffer.offset = history.list[history.pos].offset;
	ViewUpdate();
}

static void Back()
{
	if (history.pos == -1 ||
		history.pos == history.head)
		return;

	if (history.pos == history.tail)
		HistoryAdd();
	
	history.pos = CYCLIC_DECREASE(history.pos, HISTORY_NUM);
	
	buffer.openOnLocation = true;
	buffer.chapterIndex = history.list[history.pos].chapter;
	buffer.offset = history.list[history.pos].offset;
	ViewUpdate();
}


static void SetScroll()
{
	FormPtr			frmP;
	ScrollBarPtr	sclP;
	ControlPtr		ctlP;
	ListPtr			lstP;
	Int16			value, max, min, pageSize;
	
	frmP = FrmInitForm(AutosclForm);
	
	sclP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AutosclSpeedScrollBar));
	SclGetScrollBar(sclP, &value, &min, &max, &pageSize);
	value = g_prefs.timeOut;
	SclSetScrollBar(sclP, value, min, max, pageSize);
	
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AutosclMethodList));
	value = (g_prefs.sclOpts & SCLOPPAGE) ? 0 : 1;
	LstSetSelection(lstP, value);
	
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AutosclMethodPopTrigger));
	CtlSetLabel(ctlP, LstGetSelectionText(lstP, value));
	
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AutosclGuideCheckbox));
	value = (g_prefs.sclOpts & SCLOPGUIDE) ? 1 : 0;
	CtlSetValue(ctlP, value);
	
	if (FrmDoDialog(frmP) == AutosclOKButton)
	{
		SclGetScrollBar(sclP, &value, &min, &max, &pageSize);
		g_prefs.timeOut = (Int8)value;
		
		value = LstGetSelection(lstP);
		g_prefs.sclOpts |= SCLOPPAGE;
		g_prefs.sclOpts |= SCLOPRTA;
		if (value == 0)
			g_prefs.sclOpts ^= SCLOPRTA;
		else
			g_prefs.sclOpts ^= SCLOPPAGE;

		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AutosclGuideCheckbox));
		g_prefs.sclOpts |= SCLOPGUIDE;
		if (!CtlGetValue(ctlP))
			g_prefs.sclOpts ^= SCLOPGUIDE;
	}
	
	FrmDeleteForm(frmP);
}

static Boolean ViewSetting()
{
	FormPtr		frmP;
	FieldPtr	fldP;
	MemHandle	txtH, oldTxtH;
	Char		*txtP;
	Int32		value;
	EventType	event;
	Boolean		result = false;
	
	frmP = FrmInitForm(ViewAreaForm);

	// Left margin
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewAreaLeftField));
	txtH = MemHandleNew(3);
	txtP = MemHandleLock(txtH);
	StrPrintF(txtP, "%d", g_prefs2.leftMargin);
	MemHandleUnlock(txtH);
	oldTxtH = FldGetTextHandle(fldP);
	FldSetTextHandle(fldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);

	// Right margin
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewAreaRightField));
	txtH = MemHandleNew(3);
	txtP = MemHandleLock(txtH);
	StrPrintF(txtP, "%d", g_prefs2.rightMargin);
	MemHandleUnlock(txtH);
	oldTxtH = FldGetTextHandle(fldP);
	FldSetTextHandle(fldP, txtH);
	if (oldTxtH)
		MemHandleFree(oldTxtH);

	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, ViewAreaLeftField));
	
	if (FrmDoDialog(frmP) == ViewAreaOKButton)
	{
		fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewAreaLeftField));
		value = StrAToI(FldGetTextPtr(fldP));
		g_prefs2.leftMargin = (value > 20) ? 20 : (Int8)value;

		fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewAreaRightField));
		value = StrAToI(FldGetTextPtr(fldP));
		g_prefs2.rightMargin = (value > 20) ? 20 : (Int8)value;
		
		event.eType = viewUpdateEvent;
		buffer.openOnLocation = true;
		buffer.offset = buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP);
		EvtAddEventToQueue(&event);
		
		result = true;
	}
	
	FrmDeleteForm(frmP);
	
	return result;
}

static void ShowBmkList()
{
	Char			**namePP, *items;
	Int16			num, sel;
	FormPtr			frmP;
	ListPtr			listP;
	RectangleType	rect, bmRect;
	
	PrefGetBmkName(NULL, &num);
	num += 2;
	items = MemPtrNew(20 * num);
	namePP = FormPointerArray(items, 20, num);

	SysStringByIndex(BookmarkStringList, 0, namePP[num-2], 20);
	SysStringByIndex(BookmarkStringList, 1, namePP[num-1], 20);
	
	PrefGetBmkName(namePP, &num);
	num += 2;
	
	frmP = FrmGetActiveForm();
	listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewMarkList));
	LstSetListChoices(listP, namePP, num);
	if (num < 10)
		LstSetHeight(listP, num);
	else
		LstSetHeight(listP, 10);
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewMarkList), &rect);
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ViewBMButton), &bmRect);
	LstSetPosition(listP, rect.topLeft.x, bmRect.topLeft.y - rect.extent.y);

	sel = LstPopupList(listP);

	MemPtrFree(namePP);
	MemPtrFree(items);
	
	if (sel == -1)
		return;
	
	if (sel == num - 1)
		FrmGotoForm(BmkEditForm);
	else if (sel == num - 2)
		AddBookmark();
	else
	{
		buffer.openOnLocation = true;
		PrefGetBmk(sel, &buffer.chapterIndex, &buffer.offset);
		ViewUpdate();
	}
}

static void AddBookmark()
{
	FormPtr		frmP;
	FieldPtr	fieldP;
	MemHandle	txtH, oldTxtH;
	UInt8		*nameP, *p;
	Int8		i;
	
	frmP = FrmInitForm(AddBmkForm);
	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, AddBmkNameField));

	txtH = MemHandleNew(16);
	nameP = MemHandleLock(txtH);
	i = 0;
	p = (UInt8*)pieces[view.top].textP;
	while (*p)
	{
		if (i && ISLINEFEED(*p))
			break;
		
		if (ISLINEFEED(*p) || ISCMD0P(*p))
		{
			p++;
			continue;
		}
		else if (ISCMD1P(*p))
		{
			p += 2;
			continue;
		}
		else if (ISCMD2P(*p))
		{
			p += 3;
			continue;
		}
		
		nameP[i++] = *p++;
		if (i == 15)
			break;
	}
	nameP[i] = 0;
	p = nameP;
	while (*p)
	{
		if (*p > 0x80)
		{
			if (*(p + 1) == 0)
			{
				*p = 0;
				break;
			}
			else
				p += 2;
		}
		else
			p++;
	}
	MemHandleUnlock(txtH);

	oldTxtH = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, txtH);
	FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
	if (oldTxtH)
		MemHandleFree(oldTxtH);
	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, AddBmkNameField));
	
	if (FrmDoDialog(frmP) == AddBmkAddButton)
	{
		nameP = (UInt8*)FldGetTextPtr(fieldP);
		if (StrLen((Char*)nameP) > 0)
		{
			PrefAddBmk(
				(Char*)nameP,
				buffer.chapterIndex,
				buffer.startPos + ((UInt8*)pieces[view.top].textP - buffer.startP));
		}
	}
	FrmDeleteForm(frmP);
}

static MemPtr FormPointerArray(Char* text, Int16 size, Int16 num)
{
	Char**	array;
	Int16	i;
	
	array = MemPtrNew(sizeof(Char*) * num);
	
	for (i=0; i < num; i++)
		array[i] = text + size * i;
	
	return array;
}

static Coord GetGuideLinePos()
{
	Int16	i, j, max, height;
	UInt8	lineState;

	i = 0;
	j = view.top;
	lineState = pieces[j].state;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	while (i < autoScroll.guide)
	{
		while (j != max && pieces[j].state == lineState)
			j = CYCLIC_INCREASE(j, MAX_PIECES);
		if (j == max)
			return -1;
		
		lineState = pieces[j].state;
		i++;
	}

	i = j;
	
	// Calculate line height.
	height = 0;
	while (j != max && pieces[j].state == lineState)
	{
		height = MAX (height, pieces[j].bounds.extent.y);
		j = CYCLIC_INCREASE(j, MAX_PIECES);
	}
	
	return(pieces[i].bounds.topLeft.y + height);
}

static void DrawGuideLine(Boolean erase)
{
	UInt8			lineState;
	Int16			i, height;
	Boolean			chs;
	RectangleType	rect;
	
	if (autoScroll.guide == -1) return;
	
	rect.topLeft.x = view.bounds.topLeft.x;
	rect.extent.x = view.bounds.extent.x;
	rect.topLeft.y = pieces[autoScroll.guide].bounds.topLeft.y;

	i = autoScroll.guide;
	lineState = pieces[i].state;
	height = 0;
	chs = false;
	while (pieces[i].textP && pieces[i].state == lineState)
	{
		height = MAX(height, pieces[i].bounds.extent.y);
		if (pieces[i].chs) chs = true;
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
	
	rect.extent.y = height;
	if ((g_prefs.options & OPCHSONLY) && chs ||
		!(g_prefs.options & OPCHSONLY))
		rect.extent.y += g_prefs.lineSpace;
	
	if ((g_prefs.sclOpts & SCLOPRTA) && !erase)
		ViewDrawLine(autoScroll.guide, &rect);
	
	// Draw guide line.
	if (g_prefs.sclOpts & SCLOPGUIDE)
	{
		#ifndef PALMOS_30
		WinPushDrawState();
		WinSetDrawMode(winSwap);
		if (g_prefs.depth > 1)
		{
			WinSetForeColor(g_prefs.textColor);
			WinSetBackColor(g_prefs.backColor);
		}
			#ifdef PALMOS_50
			WinSetCoordinateSystem(kCoordinatesNative);
			#endif // PALMOS_50
		#endif

		#ifdef PALMOS_30
		WinInvertLine(
			rect.topLeft.x,
			rect.topLeft.y + height,
			rect.topLeft.x + rect.extent.x - 1,
			rect.topLeft.y + height);
		#else
		LCDWinPaintLine(
			rect.topLeft.x,
			rect.topLeft.y + height,
			rect.topLeft.x + rect.extent.x - 1,
			rect.topLeft.y + height);
		WinPopDrawState();
		#endif
	}
}

static void UpdatePlayControl()
{
	FormPtr	frmP;
	
	if (!(g_prefs.options & OPTOOLBAR))
		return;
	
	frmP = FrmGetActiveForm();

	if (autoScroll.scroll)
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewPlayBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewStopBitMap));
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, ViewStopBitMap));
		FrmShowObject(frmP, FrmGetObjectIndex(frmP, ViewPlayBitMap));
	}
}

static void StartPlay()
{
	if (autoScroll.scroll) return;
	
	autoScroll.scroll = true;
	autoScroll.pause = false;
	UpdatePlayControl();
	autoScroll.seconds = SysSetAutoOffTime(0);
	autoScroll.guide = -1;
	autoScroll.ticks = g_prefs.timeOut;
	SetTimer(true);
	
	autoScroll.savedKeysP = MemPtrNew(sizeof(ButtonsType));
	if (autoScroll.savedKeysP)
	{
		// Save old settings.
		MemMove(autoScroll.savedKeysP, &g_prefs.uiView, sizeof(ButtonsType));
		
		g_prefs.uiView.buttons[3]	= ACTPLAYPAUSE;		// Memo Pad

		// For standard devices.
		g_prefs.uiView.buttons[4]	= ACTPLAYSPEEDDOWN;	// Up
		g_prefs.uiView.buttons[5]	= ACTPLAYSPEEDUP;	// Down

		// For Sony's devices.
		g_prefs.uiView.buttons[6]	= ACTPLAYSPEEDDOWN;	// Jog Up
		g_prefs.uiView.buttons[7]	= ACTPLAYSPEEDUP;	// Jog Down

		// For palmOne's devices.
		g_prefs.uiView.buttons[9]	= ACTPLAYSPEEDDOWN;	// 5-Ways Up
		g_prefs.uiView.buttons[10]	= ACTPLAYSPEEDUP;	// 5-Ways Down
	}
}

static void StopPlay()
{
	if (!autoScroll.scroll) return;
	
	autoScroll.scroll = false;
	UpdatePlayControl();
	if (g_prefs.sclOpts & SCLOPRTA)
		ViewDrawScreen();
	else
		DrawGuideLine(true);
	SysSetAutoOffTime(autoScroll.seconds);
	if (!(g_prefs.options & OPMINIBAR))
		SetTimer(false);
	
	if (autoScroll.savedKeysP)
	{
		// Restore old settings.
		MemMove(&g_prefs.uiView, autoScroll.savedKeysP, sizeof(ButtonsType));
		
		MemPtrFree(autoScroll.savedKeysP);
		autoScroll.savedKeysP = NULL;
	}
}

static void AutoPlay()
{
	UInt8	lineState;
	Int16	i, max;
	
	autoScroll.ticks++;
	if (autoScroll.ticks < g_prefs.timeOut) return;
	autoScroll.ticks = 0;
	
	if (autoScroll.guide == -1)
		autoScroll.guide = view.top;
	else
	{
		DrawGuideLine(true);
		
		i = autoScroll.guide;
		max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
		lineState = pieces[i].state;
		while (i != max && pieces[i].state == lineState)
			i = CYCLIC_INCREASE(i, MAX_PIECES);
		
		if (i == max)
		{
			if (IsChapterEnd(view.bottom))
			{
				if (buffer.chapterIndex + 1 < BookGetChapterNum())
				{
					buffer.chapterIndex++;
					buffer.openOnLocation = true;
					buffer.offset = 0;
					ViewLoadChapter(true);
					ViewMoveDown();
					if (!(g_prefs.sclOpts & SCLOPRTA))
						ViewDrawScreen();
					autoScroll.guide = view.top;
					DrawGuideLine(false);
				}
				else
				{
					autoScroll.guide = -1;
					StopPlay();
				}
			}
			else
			{
				ViewMoveDown();
				if (!(g_prefs.sclOpts & SCLOPRTA))
					ViewDrawScreen();
				autoScroll.guide = view.top;
				DrawGuideLine(false);
			}
			
			return;
		}
		autoScroll.guide = i;
	}

	DrawGuideLine(false);
}

static void EditUpdateScrollbar()
{
	FormPtr			frmP;
	FieldPtr		fldP;
	ScrollBarPtr	barP;
	UInt16			scrollPos, maxValue, textHeight, fieldHeight;

	frmP = FrmGetActiveForm();
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditTextField));
	barP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditVertScrollBar));
	FldGetScrollValues (fldP, &scrollPos, &textHeight, &fieldHeight);
	if (textHeight > fieldHeight)
		maxValue = (textHeight - fieldHeight) + FldGetNumberOfBlankLines(fldP);
	else if (scrollPos)
		maxValue = scrollPos;
	else
		maxValue = 0;
	SclSetScrollBar(barP, scrollPos, 0, maxValue, fieldHeight-1);
}

static Int16 PieceCharsNum(Int16 pi)
{
	UInt8	*p, *tooFar, step;
	Int16	num;
	
	p = (UInt8*)pieces[pi].textP;
	tooFar = p + pieces[pi].len;
	num = 0;
	while (p != tooFar)
	{
		num++;
		step = (*p <= 0x80) ? 1 : 2;
		p += step;
	}
	return num;
}

static void SelParseCharsWidth(SelCellPtr cellP, Int16 num, Int16 pi)
{
	UInt8	*p, step;
	Int16	i, x;
	
	p = (UInt8*)pieces[pi].textP;
	x = pieces[pi].bounds.topLeft.x;
	for (i = 0; i < num; i++)
	{
		cellP[i].bounds.topLeft.x = x;
		cellP[i].bounds.topLeft.y = pieces[pi].bounds.topLeft.y;
		cellP[i].bounds.extent.x = CfnCharWidth((Char*)p);
		cellP[i].bounds.extent.y = pieces[pi].bounds.extent.y;
		cellP[i].i = (Char*)p - pieces[pi].textP;
		cellP[i].selected = 0;
		
		x += cellP[i].bounds.extent.x;
		step = (*p <= 0x80) ? 1 : 2;
		p += step;
	}
}

static Int16 SelInPiece(SelPiecePtr pieP, Int16 num, Coord x, Coord y)
{
	Int16	i;

	for (i = 0; i < num; i++)
		if (RctPtInRectangle(x, y, &pieces[pieP[i].pi].bounds))
			return i;
	return -1;
}

static Int16 SelInChar(SelCellPtr cellP, Int16 num, Coord x, Coord y)
{
	Int16	i;
	
	for (i = 0; i < num; i++)
	{
		if (RctPtInRectangle(x, y, &cellP[i].bounds))
		{
			if (cellP[i].selected == 0)
			{
				cellP[i].selected = 1;
				#ifdef PALMOS_30
				WinInvertRectangle(&cellP[i].bounds, 0);
				#else
				LCDWinPaintRectangle(&cellP[i].bounds, 0);
				#endif
			}
			break;
		}
	}
	return i;
}

static void InvertSelect(
	SelPiecePtr pieP, Int16 num,
	Int16 sp, Int16 sl, Int16 ep, Int16 el)
{
	Int16	i, j;
	
	for (i = 0; i < sp; i++)
		for (j = 0; j < pieP[i].num; j++)
			if (pieP[i].listP[j].selected)
			{
				pieP[i].listP[j].selected = 0;
				#ifdef PALMOS_30
				WinInvertRectangle(&pieP[i].listP[j].bounds, 0);
				#else
				LCDWinPaintRectangle(&pieP[i].listP[j].bounds, 0);
				#endif
			}
	
	if (sp == ep)
	{
		for (j = 0; j < pieP[sp].num; j++)
			if (j >= sl && j < el)
			{
				if (!pieP[sp].listP[j].selected)
				{
					pieP[sp].listP[j].selected = 1;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[sp].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[sp].listP[j].bounds, 0);
					#endif
				}
			}
			else
			{
				if (pieP[sp].listP[j].selected)
				{
					pieP[sp].listP[j].selected = 0;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[sp].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[sp].listP[j].bounds, 0);
					#endif
				}
			}
	}
	else
	{
		for (j = 0; j < pieP[sp].num; j++)
			if (j < sl)
			{
				if (pieP[sp].listP[j].selected)
				{
					pieP[sp].listP[j].selected = 0;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[sp].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[sp].listP[j].bounds, 0);
					#endif
				}
			}
			else
			{
				if (!pieP[sp].listP[j].selected)
				{
					pieP[sp].listP[j].selected = 1;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[sp].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[sp].listP[j].bounds, 0);
					#endif
				}
			}
		
		for (i = sp + 1; i < ep; i++)
			for (j = 0; j < pieP[i].num; j++)
				if (!pieP[i].listP[j].selected)
				{
					pieP[i].listP[j].selected = 1;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[i].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[i].listP[j].bounds, 0);
					#endif
				}

		for (j = 0; j < pieP[ep].num; j++)
			if (j < el)
			{
				if (!pieP[ep].listP[j].selected)
				{
					pieP[ep].listP[j].selected = 1;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[ep].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[ep].listP[j].bounds, 0);
					#endif
				}
			}
			else
			{
				if (pieP[ep].listP[j].selected)
				{
					pieP[ep].listP[j].selected = 0;
					#ifdef PALMOS_30
					WinInvertRectangle(&pieP[ep].listP[j].bounds, 0);
					#else
					LCDWinPaintRectangle(&pieP[ep].listP[j].bounds, 0);
					#endif
				}
			}
	}
	
	for (i = ep + 1; i < num; i++)
		for (j = 0; j < pieP[i].num; j++)
			if (pieP[i].listP[j].selected)
			{
				pieP[i].listP[j].selected = 0;
				#ifdef PALMOS_30
				WinInvertRectangle(&pieP[i].listP[j].bounds, 0);
				#else
				LCDWinPaintRectangle(&pieP[i].listP[j].bounds, 0);
				#endif
			}
}

static MemPtr SelGetText(
	Int16 sp, Int16 sl, Int16 ep, Int16 el, UInt32 *offsetP)
{
	MemPtr	txtP;
	UInt8	*p, *tooFar, *q;
	
	p = (UInt8*)&pieces[sp].textP[sl];
	tooFar = (UInt8*)&pieces[ep].textP[el];
	if ((UInt8)pieces[ep].textP[el] > 0x80)
		tooFar += 2;
	else
		tooFar++;
	
	*offsetP = buffer.startPos + (tooFar - buffer.startP);

	txtP = MemPtrNew(tooFar - p + 1);
	if (txtP == NULL) return NULL;
	
	q = (UInt8*)txtP;
	while (p != tooFar)
	{
		if (ISCOMMAND(*p))
		{
			if (ISCMD0P(*p))
				p++;
			else if (ISCMD1P(*p))
				p += 2;
			else
				p += 3;
		}
		else
			*q++ = *p++;
	}
	*q = 0;
	
	return txtP;
}

static void TextSelect(Coord initX, Coord initY, Coord moveX, Coord moveY)
{
	Int16		i, j, num, max, initPie, initLetter, lastPie, lastLetter;
	SelPiecePtr	pieP;
	EventType	event;
	FormPtr		frmP;
	ListPtr		lstP;
	MemPtr		txtP;
	UInt32		result;
	
	WinPushDrawState();
	WinSetDrawMode(winSwap);
	LCDFntSetFont(CfnGetFont());
	if (g_prefs.depth > 1)
	{
		WinSetForeColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif
	
	i = view.top;
	max = CYCLIC_INCREASE(view.bottom, MAX_PIECES);
	num = 0;
	while (i != max)
	{
		num++;
		i = CYCLIC_INCREASE(i, MAX_PIECES);
	}
	
	pieP = (SelPiecePtr)MemPtrNew(sizeof(SelPieceType) * num);
	ErrFatalDisplayIf(!pieP, "Insufficient memory!");
	
	i = view.top;
	j = 0;
	initPie = -1;
	while (i != max)
	{
		pieP[j].pi = i;
		if (RctPtInRectangle(initX, initY, &pieces[i].bounds))
			initPie = j;
		pieP[j].num = PieceCharsNum(i);
		if (pieP[j].num)
		{
			pieP[j].listP = (SelCellPtr)MemPtrNew(sizeof(SelCellType) * pieP[j].num);
			SelParseCharsWidth(pieP[j].listP, pieP[j].num, i);
		}
		else
			pieP[j].listP = NULL;
		i = CYCLIC_INCREASE(i, MAX_PIECES);
		j++;
	}
	if (initPie == -1) goto TextSelectOver;
	
	initLetter = SelInChar(pieP[initPie].listP, pieP[initPie].num, initX, initY);
	lastPie = initPie;
	lastLetter = initLetter;
	event.eType = penMoveEvent;
	event.screenX = moveX;
	event.screenY = moveY;
	do {
		if (event.eType == penMoveEvent)
		{
			if (RctPtInRectangle(
				event.screenX, event.screenY, &pieces[pieP[lastPie].pi].bounds))
				i = lastPie;
			else
				i = SelInPiece(pieP, num, event.screenX, event.screenY);

			if (i != -1)
			{
				lastPie = i;
				lastLetter = SelInChar(
					pieP[lastPie].listP, pieP[lastPie].num,
					event.screenX, event.screenY);
				
				if (lastPie < initPie)
				{
					InvertSelect(pieP, num,
						lastPie, lastLetter, initPie, initLetter);
				}
				else if (lastPie > initPie)
				{
					InvertSelect(pieP, num,
						initPie, initLetter, lastPie, lastLetter + 1);
				}
				else
				{
					if (lastLetter >= initLetter)
					{
						InvertSelect(pieP, num,
							initPie, initLetter, lastPie, lastLetter + 1);
					}
					else
					{
						InvertSelect(pieP, num,
							lastPie, lastLetter, initPie, initLetter);
					}
				}
			}
		}

		EvtGetEvent(&event, evtWaitForever);
		
		#ifdef HIGH_DENSITY
		event.screenX <<= 1;
		event.screenY <<= 1;
		#endif

	} while (event.eType != penUpEvent);
	
	WinPopDrawState();
	
	frmP = FrmGetActiveForm();

	#ifdef HIGH_DENSITY
	event.screenX >>= 1;
	event.screenY >>= 1;
	#endif

	FrmSetObjectPosition(frmP,
		FrmGetObjectIndex(frmP, ViewCopyList),
		event.screenX, event.screenY);
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ViewCopyList));
	j = LstPopupList(lstP);
	if (j != noListSelection)
	{
		if (initPie > lastPie)
		{
			i = lastPie;
			lastPie = initPie;
			initPie = i;
			i = lastLetter;
			lastLetter = initLetter;
			initLetter = i;
		}
		else if ((initPie == lastPie) && (initLetter > lastLetter))
		{
			i = lastLetter;
			lastLetter = initLetter;
			initLetter = i;
		}
		
		txtP = SelGetText(
			pieP[initPie].pi, pieP[initPie].listP[initLetter].i,
			pieP[lastPie].pi, pieP[lastPie].listP[lastLetter].i,
			&result);

		if (txtP)
		{
			if (j == 0) // Copy to clipboard.
			{
				ClipboardAddItem(clipboardText, txtP, StrLen((Char*)txtP));
				MemPtrFree(txtP);
			}
			else if (j == 1) // Load ZDic.
			{
				LocalID id = DmFindDatabase(0, DICTPROGNAME);
				if (id)
				{
					ClipboardAddItem(clipboardText, txtP, StrLen((Char*)txtP));
					SysAppLaunch(0, id, 0, sysAppLaunchCmdCustomBase, NULL, &result);
				}
				else
					FrmCustomAlert(DebugAlert, "Not found ZDic.prc!", "", "");
				MemPtrFree(txtP);
			}
			else // Search this word.
			{
				EventType	event;
				
				if (find.token)
					MemPtrFree(find.token);
				
				find.token	= txtP;
				find.offset	= result;
				
				MemSet(&event, sizeof(EventType), 0);
				event.eType = dicSearchEvent;
				EvtAddUniqueEventToQueue(&event, 0, false);
			}
		}
	}
	
	WinPushDrawState();
	WinSetDrawMode(winSwap);
	if (g_prefs.depth > 1)
	{
		WinSetForeColor(g_prefs.textColor);
		WinSetBackColor(g_prefs.backColor);
	}

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	for (i = 0; i < num; i++)
	{
		for (j = 0; j < pieP[i].num; j++)
		{
			if (pieP[i].listP[j].selected)
			{
				LCDWinPaintRectangle(&pieP[i].listP[j].bounds, 0);
			}
		}
	}
	
TextSelectOver:

	WinPopDrawState();
	
	for (i = 0; i < num; i++)
		if (pieP[i].listP)
			MemPtrFree(pieP[i].listP);
	MemPtrFree(pieP);
}

static Boolean ViewDoAction(UInt8 action)
{
	Boolean done = true;
	
	if (action != ACTPLAY &&
		action != ACTPLAYSPEEDUP &&
		action != ACTPLAYSPEEDDOWN &&
		action != ACTPLAYPAUSE &&
		action != ACTNONE)
		StopPlay();
	
	switch (action)
	{
	case ACTPAGEUP:
		if (ViewMoveUp())
			ViewDrawScreen();
		else
		{
			if (buffer.chapterIndex >= 1)
			{
				buffer.chapterIndex--;
				buffer.openOnLocation = true;
				buffer.offset = 0;
				ViewLoadChapter(true);
				buffer.offset = BookGetChapterLen() - 1;
				ViewLoadChapter(true);
				ViewMoveDown();
				ViewDrawScreen();
			}
		}
		break;
		
	case ACTPAGEDOWN:
		if (IsChapterEnd(view.bottom))
		{
			if (buffer.chapterIndex + 1 < BookGetChapterNum())
			{
				buffer.chapterIndex++;
				buffer.openOnLocation = true;
				buffer.offset = 0;
				ViewLoadChapter(true);
				ViewMoveDown();
				ViewDrawScreen();
			}
		}
		else
		{
			ViewMoveDown();
			ViewDrawScreen();
		}
		break;
		
	case ACTLINEUP:
		ViewMoveUpLine();
		break;
		
	case ACTLINEDOWN:
		ViewMoveDownLine();
		break;
		
	case ACTCLOSE:
		FrmGotoForm(MainForm);
		break;
		
	case ACTTOC:
		ContentsSetReturnForm(ViewForm);
		FrmGotoForm(ContentsForm);
		break;
		
	case ACTHOME:
		buffer.openOnLocation = true;
		buffer.chapterIndex = 0;
		buffer.offset = 0;
		ViewLoadChapter(true);
		ViewMoveDown();
		ViewDrawScreen();
		break;
		
	case ACTPREV:
		if (isLinked)
			Back();
		else
			PrevChapter();
		break;
		
	case ACTNEXT:
		if (isLinked)
			Forward();
		else
			NextChpater();
		break;
		
	case ACTPLAY:
		if (REGISTERED)
		{
			if (autoScroll.scroll)
				StopPlay();
			else
				StartPlay();
		}
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case ACTPLAYSPEEDUP:
		g_prefs.timeOut--;
		if (g_prefs.timeOut < 1) g_prefs.timeOut = 1;
		break;
	
	case ACTPLAYSPEEDDOWN:
		g_prefs.timeOut++;
		if (g_prefs.timeOut > 9) g_prefs.timeOut = 9;
		break;
	
	case ACTPLAYPAUSE:
		autoScroll.pause = !autoScroll.pause;
		break;
	
	case ACTFIND:
		if (REGISTERED)
			FindText();
		else
			FrmAlert(NeedRegAlert);
		break;
	
	case ACTUI:
		/*	UI convert table
			T	B		T	B
			0	0	->	1	1
			0	1	->	0	0
		x	1	0
			1	1	->	0	1
		*/
		if (!(g_prefs.options & OPMINIBAR) && !(g_prefs.options & OPTOOLBAR))
		{
			g_prefs.options |= OPMINIBAR;
			g_prefs.options |= OPTOOLBAR;
			Minibar();
			Toolbar();
			SetTimer((g_prefs.options & OPMINIBAR) ? true : false);
		}
		else if ((g_prefs.options & OPMINIBAR) && (g_prefs.options & OPTOOLBAR))
		{
			g_prefs.options ^= OPTOOLBAR;
			Toolbar();
		}
		else
		{
			g_prefs.options ^= OPMINIBAR;
			Minibar();
			SetTimer((g_prefs.options & OPMINIBAR) ? true : false);
		}
		
		RedrawScreen();
		break;
	
	default:
		done = false;
	}
	
	return done;
}

#pragma mark === Event Handlers ===

Boolean ViewFormHandleEvent(EventType* eventP)
{
	Boolean			handled = false;
	FormPtr			frmP;
	Coord			x1, y1, x2, y2;
	EventType		event;
	RectangleType	rect;
	
	switch (eventP->eType)
	{
	case menuEvent:
		return ViewFormDoCommand(eventP->data.menu.itemID);

	case frmOpenEvent:
		CfnSetFont(g_prefs2.sysFont);
		#ifdef HIGH_DENSITY
		CfnSetEngFont(g_prefs2.engFont);
		#endif
		if (g_prefs.options & OPUSEADDFONT)
			CfnSetFont(4 + g_prefs2.chsFont);

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

		ResizeScreen();

		FrmDrawForm(frmP);
		if (g_prefs.depth > 1)
		{
			IndexedColorType	color;
			
			#ifdef PALMOS_35
			rect.topLeft.x	= 0;
			rect.topLeft.y	= 0;
			rect.extent.x	= 160;
			rect.extent.y	= 160;
			#else
			WinGetBounds(WinGetDrawWindow(), &rect);
			#endif
			color = (g_prefs.options & OPINVERT) ? g_prefs.textColor : g_prefs.backColor;
			color = WinSetBackColor(color);
			WinEraseRectangle(&rect, 0);
			WinSetBackColor(color);
		}
		
		getword = false;
		scrcopy = false;

		ViewFormLayout(frmP);
		Minibar();
		Toolbar();

		if (scrollBar.dataP->value == -1)
		{
			ScrollDraw();
			ViewUpdate();
		}
		else
			ViewDrawScreen();
		
		battery.ticks = BATTERY_UPDATE;
		clock.ticks = CLOCK_UPDATE;
		if (g_prefs.options & OPMINIBAR)
			SetTimer(true);

		handled = true;
		break;
	
	case frmCloseEvent:
		SetTimer(false);
		break;

	case nilEvent:
		if (g_prefs.options & OPMINIBAR)
			BatteryAndClock();
		if (autoScroll.scroll && !autoScroll.pause)
			AutoPlay();
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
			FrmDrawForm(frmP);
			ViewFormLayout(frmP);
			Minibar();
			Toolbar();

			buffer.openOnLocation = true;
			buffer.offset = buffer.startPos +
				((UInt8*)pieces[view.top].textP - buffer.startP);
			ViewUpdate();

			battery.ticks = BATTERY_UPDATE;
			clock.ticks = CLOCK_UPDATE;
			BatteryAndClock();
		}
		break;
	#endif // USE_SILKSCREEN
	

	case frmUpdateEvent:
		FrmDrawForm(FrmGetActiveForm());
		if (scrollBar.dataP->value == -1)
			ScrollDraw();
		else
		{
			WinResetClip();
			Minibar();
			ViewDrawScreen();
		}
		handled = true;
		break;
	
	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case ViewCloseButton:
			FrmGotoForm(MainForm);
			handled = true;
			break;
		
		case ViewTOCButton:
			ViewDoAction(ACTTOC);
			handled = true;
			break;
		
		case ViewBMButton:
			if (REGISTERED)
				ShowBmkList();
			else
				FrmAlert(NeedRegAlert);
			handled = true;
			break;
		
		case ViewBackButton:
			ViewDoAction(ACTPREV);
			handled = true;
			break;
		
		case ViewHomeButton:
			ViewDoAction(ACTHOME);
			handled = true;
			break;
		
		case ViewForwardButton:
			ViewDoAction(ACTNEXT);
			handled = true;
			break;
		
		case ViewPlayButton:
			ViewDoAction(ACTPLAY);
			handled = true;
			break;
		
		case ViewFindButton:
			ViewDoAction(ACTFIND);
			handled = true;
			break;
			
		case ViewGotoButton:
			Goto();
			handled = true;
			break;
		
		case ViewCaptureButton:
//			getword = true;
			if (REGISTERED)
			{
				scrcopy = !scrcopy;
				UpdateCopyIcon();
			}
			else
				FrmAlert(NeedRegAlert);
			handled = true;
			break;
		}
		break;

	case penDownEvent:
		if ((g_prefs.options & OPTOOLBAR) &&
			RctPtInRectangle(eventP->screenX, eventP->screenY, &scrollBar.bounds))
		{
			ScrollTapped(eventP->screenX);
			handled = true;
			break;
		}

		x1 = eventP->screenX;
		y1 = eventP->screenY;
		#ifdef HIGH_DENSITY
		x1 <<= 1;
		y1 <<= 1;
		#endif

		if (g_prefs.options & OPMINIBAR)
		{
			rect.topLeft.x	= 0;
			rect.topLeft.y	= 0;
			
			LCDWinGetDisplayExtent(&rect.extent.x, &rect.extent.y);
			#ifdef HIGH_DENSITY
			rect.extent.x <<= 1;
			#endif
			rect.extent.y = MINIBAR_HEIGHT;
			
			if (RctPtInRectangle(x1, y1, &rect))
			{
				event.eType = keyDownEvent;
				event.data.keyDown.chr = vchrMenu;
				EvtAddEventToQueue(&event);
				handled = true;
				break;
			}
		}
		
		if (!RctPtInRectangle(x1, y1, &view.bounds)) break;
		
		if (getword)
		{
			getword = false;
			GetWord(x1, y1);
		}
		else if (scrcopy)
		{
			do {
				EvtGetEvent(&event, evtWaitForever);
			} while (event.eType != penUpEvent && event.eType != penMoveEvent);
		
			x2 = event.screenX;
			y2 = event.screenY;
			#ifdef HIGH_DENSITY
			x2 <<= 1;
			y2 <<= 1;
			#endif

			TextSelect(x1, y1, x2, y2);
		}
		else
		{
			if (!ViewClickLink(x1, y1))
			{
				do {
					EvtGetEvent(&event, evtWaitForever);
				} while (event.eType != penUpEvent);
				
				if (event.screenX - eventP->screenX >= 5)
					GetWord(x1, y1);
				else
					ViewTap(y1);
			}
		}
		handled = true;
		break;
		
	case keyDownEvent:
		handled = ViewFormHandleKey(eventP);
		break;
	
	case viewUpdateEvent:
		ViewUpdate();
		handled = true;
		break;
	
	case dicSearchEvent:
		FindText2();
		handled = true;
		break;
	}
	
	return handled;
}

Boolean ViewFormHandleKey(EventPtr keyDownEventP)
{
	Boolean	handled;
	
	switch (keyDownEventP->data.keyDown.chr)
	{
	case pageUpChr:
		handled = ViewDoAction(g_prefs.uiView.buttons[4]);
		break;

	case pageDownChr:
		handled = ViewDoAction(g_prefs.uiView.buttons[5]);
		break;

	case vchrHard1:
		handled = ViewDoAction(g_prefs.uiView.buttons[0]);
		break;
		
	case vchrHard2:
		handled = ViewDoAction(g_prefs.uiView.buttons[1]);
		break;
		
	case vchrHard3:
		handled = ViewDoAction(g_prefs.uiView.buttons[2]);
		break;
		
	case vchrHard4:
		handled = ViewDoAction(g_prefs.uiView.buttons[3]);
		break;
	
	case vchrJogUp:
		handled = ViewDoAction(g_prefs.uiView.buttons[6]);
		break;

	case vchrJogDown:
		handled = ViewDoAction(g_prefs.uiView.buttons[7]);
		break;

	case vchrJogRelease:
		handled = ViewDoAction(g_prefs.uiView.buttons[8]);
		break;

	case vchrCalc:
		handled = ViewDoAction(g_prefs.uiView.buttons[14]);
		break;
		
	case vchrFind:
		handled = ViewDoAction(g_prefs.uiView.buttons[15]);
		break;

	case vchrNavChange:
		if (keyDownEventP->data.keyDown.keyCode & navBitUp)
			ViewDoAction(g_prefs.uiView.buttons[9]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitDown))
			ViewDoAction(g_prefs.uiView.buttons[10]);
		else if (keyDownEventP->data.keyDown.keyCode & navBitLeft)
			ViewDoAction(g_prefs.uiView.buttons[11]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitRight))
			ViewDoAction(g_prefs.uiView.buttons[12]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitSelect))
			ViewDoAction(g_prefs.uiView.buttons[13]);
		else
			handled = false;
		break;

	default:
		if (IsFiveWayNavEvent(keyDownEventP))
		{
			switch (keyDownEventP->data.keyDown.chr)
			{
			case vchrRockerUp:
				ViewDoAction(g_prefs.uiView.buttons[9]);
				break;
				
			case vchrRockerDown:
				ViewDoAction(g_prefs.uiView.buttons[10]);
				break;
			
			case vchrRockerLeft:
				ViewDoAction(g_prefs.uiView.buttons[11]);
				break;
				
			case vchrRockerRight:
				ViewDoAction(g_prefs.uiView.buttons[12]);
				break;
			
			case vchrRockerCenter:
				ViewDoAction(g_prefs.uiView.buttons[13]);
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


Boolean BmkEditFormHandleEvent(EventPtr eventP)
{
	static Char**	namePP = NULL;
	static Char*	items = NULL;

	Boolean	handled = false;
	FormPtr	frmP;
	ListPtr	listP;
	Int16	num;
	Boolean	close, update;
	
	switch (eventP->eType)
	{
	case frmOpenEvent:
		frmP = FrmGetActiveForm();
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, BmkEditMarkList));
		PrefGetBmkName(NULL, &num);
		if (num)
		{
			items = MemPtrNew(16 * num);
			namePP = FormPointerArray(items, 16, num);
			PrefGetBmkName(namePP, &num);
			LstSetListChoices(listP, namePP, num);
		}
		else
			LstSetListChoices(listP, NULL, 0);
		
		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case ctlSelectEvent:
		frmP = FrmGetActiveForm();
		listP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, BmkEditMarkList));
		close = update = false;

		switch (eventP->data.ctlSelect.controlID)
		{
		case BmkEditOpenButton:
			num = LstGetSelection(listP);
			if (num != noListSelection)
			{
				buffer.openOnLocation = true;
				PrefGetBmk(num, &buffer.chapterIndex, &buffer.offset);
				update = true;
			}
			close = true;
			break;
			
		case BmkEditDelButton:
			num = LstGetSelection(listP);
			if (num != noListSelection)
			{
				MemPtrFree(namePP);
				MemPtrFree(items);
				namePP = NULL;
				items = NULL;
				PrefDelBmk(num);
				PrefGetBmkName(NULL, &num);
				if (num)
				{
					items = MemPtrNew(16 * num);
					namePP = FormPointerArray(items, 16, num);
					PrefGetBmkName(namePP, &num);
					LstSetListChoices(listP, namePP, num);
				}
				else
					LstSetListChoices(listP, NULL, 0);
				LstDrawList(listP);
			}
			break;
		
		case BmkEditDelAllButton:
			if (namePP && FrmAlert(DelAllBmkAlert) == DelAllBmkYes)
			{
				MemPtrFree(namePP);
				MemPtrFree(items);
				namePP = NULL;
				items = NULL;
				PrefDelAllBmk();
				LstSetListChoices(listP, NULL, 0);
				LstDrawList(listP);
			}
			break;

		case BmkEditCloseButton:
			close = true;
			break;
		}
		
		if (close)
		{
			if (namePP)
			{
				MemPtrFree(namePP);
				MemPtrFree(items);
				namePP = NULL;
				items = NULL;
			}
			FrmGotoForm(ViewForm);
			if (update)
				ViewUpdate();
		}
		handled = true;
		break;
	}

	return handled;
}

Boolean EditFormHandleEvent(EventType* eventP)
{
	Boolean		handled = false;
	FormPtr		frmP = FrmGetActiveForm();
	FieldPtr	fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditTextField));
	MemHandle	txtH, oldTxtH;
	UInt16		blankLines;
	Int16		linesToScroll;

	switch (eventP->eType)
	{
	case menuEvent:
		switch(eventP->data.menu.itemID)
		{
		case F4EditUndo:
		case sysEditMenuUndoCmd:
			FldUndo (fieldP);
			break;
		
		case F4EditCut:
		case sysEditMenuCutCmd:
			FldCut (fieldP);
			break;
		
		case F4EditCopy:
		case sysEditMenuCopyCmd:
			FldCopy (fieldP);
			break;
		
		case F4EditPaste:
		case sysEditMenuPasteCmd:
			FldPaste (fieldP);
			break;

		case F4EditSelectAll:
		case sysEditMenuSelectAllCmd:
			FldSetSelection(fieldP, 0, FldGetTextLength(fieldP));
			break;
		}
		handled = true;
		break;

	case frmOpenEvent:
		frmP = FrmGetActiveForm();

		#ifdef USE_SILKSCREEN

		#ifdef USE_PALM_SILKSCREEN
		
		WinSetConstraintsSize(
			WinGetWindowHandle(frmP),
			160, 225, 225,
			160, 160, 160);
		FrmSetDIAPolicyAttr(frmP, frmDIAPolicyCustom);
		PINSetInputTriggerState(pinInputTriggerDisabled);
		PINSetInputAreaState(pinInputAreaClosed);
		
		#endif // USE_PALM_SILKSCREEN
		
		{
			WinHandle		frmWinH;
			RectangleType	fromBounds, toBounds;
			Int16			heightDelta, widthDelta;
			
			frmWinH = FrmGetWindowHandle(frmP);
			WinGetBounds(frmWinH, &fromBounds);
			WinGetBounds(WinGetDisplayWindow(), &toBounds);
			
			toBounds.topLeft.x = toBounds.topLeft.y = 2;
			toBounds.extent.x -= 4;
			toBounds.extent.y -= 4;
			
			heightDelta = \
				(toBounds.extent.y - toBounds.topLeft.y) - \
				(fromBounds.extent.y - fromBounds.topLeft.y);
			
			widthDelta = \
				(toBounds.extent.x - toBounds.topLeft.x) - \
				(fromBounds.extent.x - fromBounds.topLeft.x);

			WinSetBounds(frmWinH, &toBounds);
			
			// Done button

			FrmGetObjectPosition(
				frmP,
				FrmGetObjectIndex(frmP, EditDoneButton),
				&fromBounds.topLeft.x,
				&fromBounds.topLeft.y);

			FrmSetObjectPosition(
				frmP,
				FrmGetObjectIndex(frmP, EditDoneButton),
				fromBounds.topLeft.x,
				fromBounds.topLeft.y + heightDelta);
			
			// Text field
			
			FrmGetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, EditTextField),
				&fromBounds);
			
			fromBounds.extent.x += widthDelta;
			fromBounds.extent.y += heightDelta;

			FrmSetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, EditTextField),
				&fromBounds);
			
			// Scrollbar

			FrmGetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, EditVertScrollBar),
				&fromBounds);
			
			fromBounds.topLeft.x = toBounds.extent.x - fromBounds.extent.x - 2;
			fromBounds.extent.y += heightDelta;

			FrmSetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, EditVertScrollBar),
				&fromBounds);
		}

		#endif // USE_SILKSCREEN

		txtH = EditGetEditText();
		if (txtH)
		{
			fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditTextField));
			oldTxtH = FldGetTextHandle(fieldP);
			FldSetTextHandle(fieldP, txtH);
			if (oldTxtH)
				MemHandleFree(oldTxtH);
		}
		EditUpdateScrollbar();
		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case winEnterEvent:
		#ifdef USE_PALM_SILKSCREEN
		frmP = FrmGetActiveForm();
		if ((FormPtr)eventP->data.winEnter.enterWindow == frmP)
		{
			EventType event;
			MemSet(&event, sizeof(EventType), 0);
			event.eType = winDisplayChangedEvent;
			EvtAddUniqueEventToQueue(&event, 0, true);
		}
		#endif // USE_PALM_SILKSCREEN
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case EditDoneButton:
			FrmGotoForm(ViewForm);
			handled = true;
			break;
		}
		break;

	case keyDownEvent:
		frmP = FrmGetActiveForm();
		fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditTextField));
		switch (eventP->data.keyDown.chr)
		{
		case vchrPageUp:
			if (FldScrollable(fieldP, winUp))
			{
				linesToScroll = FldGetVisibleLines(fieldP) - 1;
				FldScrollField(fieldP, linesToScroll, winUp);
				EditUpdateScrollbar();
			}
			handled = true;
			break;
			
		case vchrPageDown:
			if (FldScrollable(fieldP, winDown))
			{
				linesToScroll = FldGetVisibleLines(fieldP) - 1;
				FldScrollField(fieldP, linesToScroll, winDown);
				EditUpdateScrollbar();
			}
			handled = true;
			break;
		}
		break;

	case fldChangedEvent:
		EditUpdateScrollbar();
		handled = true;
		break;
		
	case sclRepeatEvent:
		frmP = FrmGetActiveForm();
		fieldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, EditTextField));
		blankLines = FldGetNumberOfBlankLines(fieldP);
		linesToScroll = eventP->data.sclRepeat.newValue - eventP->data.sclRepeat.value;
		if (linesToScroll < 0)
			FldScrollField(fieldP, -linesToScroll, winUp);
		else if (linesToScroll > 0)
			FldScrollField(fieldP, linesToScroll, winDown);
		break;
	}
	
	return handled;
}
