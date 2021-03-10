#include "VBook.h"
#include "LCD.h"
#include "CustomFont.h"
#include "BookList.h"
#include "Contents.h"
#include "View.h"
#include "CommonDlg.h"

#ifdef HIGH_DENSITY
	#define ITEM_CORNER		5
#else
	#define ITEM_CORNER		3
#endif

typedef struct TitleType {
	Char*	titleP;
	Int16	len;
	Int16	idx;
} TitleType;

typedef struct TitleListType {
	MemPtr			titleIndexP;
	MemPtr			titleDataP;
	UInt16			parent;
	TitleType*		listP;
	Int16			num;
	Int16			max;
	Int16			top;
	Int16			bottom;
	Int16			pos;
	RectangleType	origBounds;
	RectangleType	bounds;
	Coord			col1;
	Coord			height;
	Int16			lines;
} TitleListType;

Boolean	ContentsDoAction(UInt8 action);

static TitleListType	titleList;
static UInt16			returnFormId;

#pragma mark === Internal Functions ===

static Int16 ContentsSplitTitle(Char* title, TitleType* titlesP)
{
	Coord	maxWidth;
	Int16	num;
	Coord	width;
	Int16	len;
	UInt8	*p, *q;
	Int8	step;

	maxWidth = titleList.bounds.extent.x - titleList.col1;
	num = 0;
	width = 0;
	len = 0;
	p = (UInt8*)title;
	q = p;
	while (*p)
	{
		step = (*p <= 0x80) ? 1 : 2;
		len += step;
		width += CfnCharWidth((Char*)p);
		if (width > maxWidth)
		{
			if (titlesP)
			{
				titlesP->titleP = (Char*)q;
				titlesP->len = len - step;
				titlesP++;
			}
			num++;
			q = p;
			width = 0;
			len = 0;
			continue;
		}
		p += step;
	}
	if (titlesP)
	{
		titlesP->titleP = (Char*)q;
		titlesP->len = len;
	}
	num++;
	return num;
}

static void ContentsLoad()
{
	VBOOKTITLEINDEX*	pti;
	Int16				total, count, i, j;
	
	titleList.num = 0;

	if (titleList.titleIndexP == NULL)
	{
		// This book doesn't include table of contents.
		if (!BookGetContents(&titleList.titleIndexP, &titleList.titleDataP))
			return;
	}
	
	total = MemPtrSize(titleList.titleIndexP) / sizeof(VBOOKTITLEINDEX);

	// Calculate how many items is need by this level titles.
	count = 0;
	pti = titleList.titleIndexP;
	for (i=0; i < total; i++, pti++)
	{
		if (pti->nParentID != titleList.parent)
			continue;

		count += ContentsSplitTitle(
			(Char*)titleList.titleDataP + pti->nOffset, NULL);
	}

	// Allocate sufficient memory for store them.
	if (titleList.max < count)
	{
		if (titleList.max)
			MemPtrFree(titleList.listP);

		titleList.listP = (TitleType*)MemPtrNew(sizeof(TitleType) * count);
		titleList.max = count;
	}
	
	// Fill titles.
	pti = titleList.titleIndexP;
	for (i=0; i < total; i++, pti++)
	{
		if (pti->nParentID != titleList.parent)
			continue;
		
		count = ContentsSplitTitle(
			(Char*)titleList.titleDataP + pti->nOffset,
			&titleList.listP[titleList.num]);
		
		for (j=0; j < count; j++)
			titleList.listP[titleList.num + j].idx = i;
		
		titleList.num += count;
	}
	
	titleList.top = -1;
	titleList.bottom = -1;
	titleList.pos = -1;
}

static void ContentsCurrentPos()
{
	VBOOKTITLEINDEX*	pti;
	Int16				i;
	UInt16				chapter = ViewGetChapterIndex();

	if (titleList.num == 0)
		return;
	
	pti = titleList.titleIndexP;
	for (i=0; i < titleList.num; i++)
	{
		if (pti[titleList.listP[i].idx].nChapter == chapter)
		{
			titleList.pos = i;
			break;
//			return;
		}
	}
//	titleList.pos = -1;
//	return;
	
	if (titleList.pos < titleList.top || titleList.pos > titleList.bottom)
	{
		titleList.top = (titleList.pos + titleList.lines - 1 < titleList.num) ?
			titleList.pos : titleList.num - titleList.lines;
		if (titleList.top < 0)
			titleList.top = 0;
	}
}


static void ContentsSetScrollBar()
{
	FormType*		frmP;
	ScrollBarType*	barP;
	
	frmP = FrmGetActiveForm();
	barP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ContentsVsbarScrollBar));

	if (titleList.num <= titleList.lines)
		SclSetScrollBar(barP, 0, 0, 0, 0);
	else
		SclSetScrollBar(barP,
			titleList.top, 0, titleList.num - titleList.lines, titleList.lines-1);
	
	#ifdef USE_SILKSCREEN
	SclDrawScrollBar(barP);
	#endif
}

static void ContentsDrawRow(
	Int16 idx, Coord x, Coord y,
	IndexedColorType foreColor, IndexedColorType backColor)
{
	RectangleType rect;
	
	WinSetTextColor(foreColor);
	WinSetForeColor(foreColor);
	WinSetBackColor(backColor);
	
	rect.topLeft.x = x;
	rect.topLeft.y = y;
	rect.extent.x = titleList.bounds.extent.x;
	rect.extent.y = titleList.height;
	LCDWinEraseRectangle(&rect, 0);
	
	if (idx == 0 ||
		titleList.listP[idx].idx != titleList.listP[idx - 1].idx)
	{
		rect.extent.x = rect.extent.y = 5;

		#ifdef HIGH_DENSITY
		rect.extent.x <<= 1;
		rect.extent.y <<= 1;
		#endif

		rect.topLeft.x = x;
		rect.topLeft.y = y + (titleList.height / 2 - rect.extent.y / 2);
		LCDWinDrawRectangle(&rect, ITEM_CORNER);
	}
	
	CfnDrawChars(
		titleList.listP[idx].titleP,
		titleList.listP[idx].len,
		x + titleList.col1, y,
		foreColor, backColor);
}


static void ContentsHighlightRow(Boolean highlight)
{
	Int16				i, j, y;
	IndexedColorType	foreColor, backColor;
	RectangleType		rect;
	
	if (titleList.pos == -1 ||
		titleList.pos < titleList.top ||
		titleList.pos > titleList.bottom)
		return;
	
	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());
	
	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif
	
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

	i = titleList.pos;
	while (i >= titleList.top &&
		titleList.listP[i].idx == titleList.listP[titleList.pos].idx)
		i--;
	i++;

	for (j = i; j <= titleList.bottom; j++)
		if (titleList.listP[j].idx != titleList.listP[titleList.pos].idx)
			break;
	j -= i;

	rect.topLeft.x = titleList.bounds.topLeft.x;
	rect.topLeft.y = titleList.bounds.topLeft.y +
		(i - titleList.top) * (titleList.height + ITEM_SPACE);
	rect.extent.x = titleList.bounds.extent.x;
	rect.extent.y = (titleList.height + ITEM_SPACE) * j - ITEM_SPACE;
	
	WinSetBackColor(backColor);
	LCDWinEraseRectangle(&rect, 0);
	
	y = titleList.bounds.topLeft.y +
		(i - titleList.top) * (titleList.height + ITEM_SPACE);

	for (; i <= titleList.bottom &&
		titleList.listP[i].idx == titleList.listP[titleList.pos].idx;
		i++)
	{
		ContentsDrawRow(i, titleList.bounds.topLeft.x, y, foreColor, backColor);
		y += titleList.height + ITEM_SPACE;
	}
	
	WinPopDrawState();
}


static void ContentsDrawScreen()
{
	Int16				i, j;
	Coord				y;
	IndexedColorType	foreColor, backColor;

	WinPushDrawState();

	#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	WinSetBackColor(g_prefs.backColor);
	LCDWinEraseRectangle(&titleList.bounds, 0);

	WinPopDrawState();

	if (titleList.num == 0)
		return;

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
		WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	y = titleList.bounds.topLeft.y;
	foreColor = g_prefs.textColor;
	backColor = g_prefs.backColor;
	
	for (i = 0; i < titleList.lines &&
		titleList.top + i < titleList.num; i++)
	{
		j = titleList.top + i;
		ContentsDrawRow(j, titleList.bounds.topLeft.x, y, foreColor, backColor);
		titleList.bottom = j;
		y += titleList.height + ITEM_SPACE;
	}

	WinPopDrawState();

	ContentsHighlightRow(true);
	ContentsSetScrollBar();
}


static void ContentsPageUp()
{
	if (titleList.num == 0 ||
		titleList.num <= titleList.lines ||
		titleList.top == 0)
		return;
	
	titleList.top -= titleList.lines - 1;
	if (titleList.top < 0)
		titleList.top = 0;

	ContentsDrawScreen();
}

static void ContentsPageDown()
{
	if (titleList.num == 0 || 
		titleList.num <= titleList.lines ||
		titleList.bottom == titleList.num-1)
		return;
	
	if (titleList.bottom + titleList.lines - 1 < titleList.num)
		titleList.top = titleList.bottom;
	else
		titleList.top = titleList.num - titleList.lines;

	ContentsDrawScreen();
}

static void ContentsItemPrev()
{
	Int16			i;
	Int16			step;
	RectangleType	vacated;

	if (titleList.num == 0)
		return;

	if (titleList.pos == -1 ||
		titleList.pos < titleList.top ||
		titleList.pos > titleList.bottom)
	{
		titleList.pos = titleList.bottom;
		ContentsHighlightRow(true);
		ContentsSetScrollBar();
		return;
	}
	
	ContentsHighlightRow(false);

	i = titleList.pos;
	while (i >= 0 &&
		titleList.listP[i].idx == titleList.listP[titleList.pos].idx)
		i--;
	if (i >= 0)
	{
		step = (titleList.pos - i) - (titleList.pos - titleList.top);
		titleList.pos = i;
		while (i >= 0 &&
			titleList.listP[i].idx == titleList.listP[titleList.pos].idx)
			i--;
		i++;
		step += titleList.pos - i;
		titleList.pos = i;
	}
	else
	{
		step = titleList.pos;
		titleList.pos = 0;
	}

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif

	if (titleList.pos < titleList.top)
	{
		titleList.top -= step;
		titleList.bottom -= step;
		LCDWinScrollRectangle(
			&titleList.bounds, winDown,
			(titleList.height + ITEM_SPACE) * step, &vacated);
	}

	WinPopDrawState();

	ContentsHighlightRow(true);
	ContentsSetScrollBar();
}

static void ContentsItemNext()
{
	Int16			i;
	Int16			step;
	RectangleType	vacated;

	if (titleList.num == 0)
		return;

	if (titleList.pos == -1 ||
		titleList.pos < titleList.top ||
		titleList.pos > titleList.bottom)
	{
		titleList.pos = titleList.top;
		ContentsHighlightRow(true);
		return;
	}
	
	ContentsHighlightRow(false);

	i = titleList.pos;
	while (i < titleList.num &&
		titleList.listP[i].idx == titleList.listP[titleList.pos].idx)
		i++;
	if (i < titleList.num)
	{
		titleList.pos = i;
		while (i < titleList.num &&
			titleList.listP[i].idx == titleList.listP[titleList.pos].idx)
			i++;
		i--;
		step = i - titleList.bottom;
	}
	else
	{
		i--;
		step = i - titleList.bottom;
	}
	titleList.pos = i;

	WinPushDrawState();
	LCDFntSetFont(CfnGetFont());

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif
	
	if (titleList.pos > titleList.bottom)
	{
		titleList.top += step;
		titleList.bottom += step;
		LCDWinScrollRectangle(
			&titleList.bounds, winUp,
			(titleList.height + ITEM_SPACE) * step, &vacated);
	}

	WinPopDrawState();
	
	ContentsHighlightRow(true);
	ContentsSetScrollBar();
}

static void ContentsSelect(Int16 item, Boolean tapped)
{
	VBOOKTITLEINDEX*	pti;
	EventType			event;

	pti = titleList.titleIndexP;

	if (pti[titleList.listP[item].idx].nChapter == 0xFFFF)
		return;
	
	ViewSetAnchor(
		pti[titleList.listP[item].idx].nChapter,
		pti[titleList.listP[item].idx].nAnchor);

	if (tapped)
	{
		ContentsHighlightRow(true);
		do {
			EvtGetEvent(&event, SysTicksPerSecond() / 2);
		} while (event.eType != penUpEvent && event.eType != nilEvent);
	}

	ViewInit();
	FrmGotoForm(ViewForm);
}

static void ContentsTapped(Coord x, Coord y)
{
	Int16			i;
	RectangleType	rect;
	
	if (titleList.num == 0)
		return;
	
	ContentsHighlightRow(false);

	rect.topLeft.x	= titleList.bounds.topLeft.x;
	rect.topLeft.y	= titleList.bounds.topLeft.y;
	rect.extent.x	= titleList.bounds.extent.x;
	rect.extent.y	= titleList.height;

	for (i = titleList.top; i <= titleList.bottom; i++)
	{
		if (RctPtInRectangle(x, y, &rect))
		{
			titleList.pos = i;
			
			if (x < titleList.bounds.topLeft.x + titleList.col1)
			{
			}
			else
				ContentsSelect(i, true);
			break;
		}
		rect.extent.y += titleList.height + ITEM_SPACE;
	}
}

static void ContentsFormLayout(FormPtr frmP)
{
	RectangleType	rect;
	
	if (titleList.origBounds.extent.x == 0)
	{
		FrmGetObjectBounds(
			frmP,
			FrmGetObjectIndex(frmP, ContentsTitlesGadget),
			&titleList.origBounds);

		#ifdef HIGH_DENSITY
		titleList.origBounds.topLeft.x <<= 1;
		titleList.origBounds.topLeft.y <<= 1;
		titleList.origBounds.extent.x <<= 1;
		titleList.origBounds.extent.y <<= 1;
		#endif
	}
	
	MemMove(&titleList.bounds, &titleList.origBounds, sizeof(RectangleType));

	if (titleList.height == 0)
	{
		FontID	oldFont;
		
		oldFont = LCDFntSetFont(CfnGetFont());
		titleList.height = FntCharHeight();
		LCDFntSetFont(oldFont);

		#ifdef PALMOS_50

		#ifdef HIGH_DENSITY
		titleList.height <<= 1;
		#endif

		if (titleList.height < CfnCharSize())
			titleList.height = CfnCharSize();
		#endif
	}
	
	titleList.lines =
		(titleList.origBounds.extent.y + ITEM_SPACE) /
		(titleList.height + ITEM_SPACE);

	// Adjust title list to new bound.
	titleList.bounds.extent.y =
		titleList.height * titleList.lines + ITEM_SPACE * (titleList.lines - 1);

	MemMove(&rect, &titleList.bounds, sizeof(RectangleType));

	#ifdef HIGH_DENSITY
	rect.topLeft.y >>= 1;
	rect.extent.x >>= 1;
	rect.extent.y >>= 1;
	#endif
	
	WinPushDrawState();

	#ifdef PALMOS_50
	WinSetCoordinateSystem(kCoordinatesNative);
	#endif
	
	LCDWinEraseRectangle(&titleList.origBounds, 0);
	FrmSetObjectBounds(frmP, FrmGetObjectIndex(frmP, ContentsTitlesGadget), &rect);

	// Adjust scrollbar's height same with list.
	FrmGetObjectBounds(frmP, FrmGetObjectIndex(frmP, ContentsVsbarScrollBar), &rect);

	#ifdef HIGH_DENSITY
	rect.topLeft.x <<= 1;
	rect.topLeft.y <<= 1;
	rect.extent.x <<= 1;
	rect.extent.y <<= 1;
	#endif

	LCDWinEraseRectangle(&rect, 0);
	rect.extent.y = titleList.bounds.extent.y;

	#ifdef HIGH_DENSITY
	rect.topLeft.x >>= 1;
	rect.topLeft.y >>= 1;
	rect.extent.x >>= 1;
	rect.extent.y >>= 1;
	#endif

	FrmSetObjectBounds(frmP, FrmGetObjectIndex(frmP, ContentsVsbarScrollBar), &rect);
	
	WinPopDrawState();
}

static Boolean ContentsFormDoCommand(UInt16 command)
{
	Boolean		handled = true;

	MenuEraseStatus(0);
	switch (command)
	{
	case F2TOCBack:
		ContentsDoAction(ACTRETURN);
		break;
	
	case F2OptionsButtons:
		CmnDlgSetReturnForm(ContentsForm);
		FrmGotoForm(ButtonsForm);
		break;
	
	default:
		handled = false;
		break;
	}
	
	return handled;
}

static Boolean ContentsDoAction(UInt8 action)
{
	Boolean done = true;
	
	switch (action)
	{
	case ACTLINEUP:
		ContentsItemPrev();
		break;
		
	case ACTLINEDOWN:
		ContentsItemNext();
		break;
		
	case ACTPAGEUP:
		ContentsPageUp();
		break;
		
	case ACTPAGEDOWN:
		ContentsPageDown();
		break;
		
	case ACTRETURN:
		FrmGotoForm(returnFormId);
		break;
		
	case ACTSELECT:
		if (titleList.pos != -1)
			ContentsSelect(titleList.pos, false);
		break;
	
	default:
		done = false;
	}
	
	return done;
}

#pragma mark === Entry Points ===

Boolean ContentsPrepare()
{
	MemSet(&titleList, sizeof(TitleListType), 0);

	titleList.col1 = 10;
	#ifdef HIGH_DENSITY
	titleList.col1 <<= 1;
	#endif

	return true;
}

void ContentsRelease()
{
	if (titleList.listP)
		MemPtrFree(titleList.listP);
}

void ContentsRefresh()
{
	titleList.titleIndexP	= NULL;
	titleList.titleDataP	= NULL;
	titleList.num			= 0;
	titleList.pos			= -1;
}

void ContentsSetReturnForm(UInt16 formId)
{
	returnFormId = formId;
}

Boolean ContentsFormHandleEvent(EventType* eventP)
{
	Boolean		handled = false;
	FormType*	frmP;

	#ifdef HIGH_DENSITY
	Coord		x, y;
	#endif
	
	switch (eventP->eType)
	{
	case menuEvent:
		return ContentsFormDoCommand(eventP->data.menu.itemID);

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

		if (ResizeScreen())
		{
			titleList.origBounds.extent.x = 0;
			FrmDrawForm(frmP);
			ContentsFormLayout(frmP);
			ContentsLoad();
		}
		else
		{
			FrmDrawForm(frmP);
			ContentsFormLayout(frmP);
		}

		if (titleList.num == 0)
		{
			FontID	oldFont;

			#ifdef PALMOS_50
			UInt16	coordSys;
			#endif
			
			oldFont = LCDFntSetFont(CfnGetFont());

			#ifdef PALMOS_50
			coordSys = WinSetCoordinateSystem(kCoordinatesNative);
			#endif

			ContentsLoad();

			#ifdef PALMOS_50
			WinSetCoordinateSystem(coordSys);
			#endif

			LCDFntSetFont(oldFont);
		}
		ContentsCurrentPos();
		ContentsDrawScreen();
		handled = true;
		break;
	
/*	case winEnterEvent:
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
*/
	#ifdef USE_SILKSCREEN
	case dispChangeEvent:
		handled = true;
	case winDisplayChangedEvent:
		if (ResizeScreen())
		{
			FontID	oldFont;

			#ifdef PALMOS_50
			UInt16	coordSys;
			#endif

			frmP = FrmGetActiveForm();
			FrmEraseForm(frmP);
			FrmDrawForm(frmP);

			titleList.origBounds.extent.x = 0;
			ContentsFormLayout(frmP);

			oldFont = LCDFntSetFont(CfnGetFont());

			#ifdef PALMOS_50
			coordSys = WinSetCoordinateSystem(kCoordinatesNative);
			#endif

			ContentsLoad();

			#ifdef PALMOS_50
			WinSetCoordinateSystem(coordSys);
			#endif

			LCDFntSetFont(oldFont);

			ContentsCurrentPos();
			ContentsDrawScreen();
		}
		break;
	#endif // USE_SILKSCREEN

	case frmUpdateEvent:
		FrmDrawForm(FrmGetActiveForm());
		ContentsDrawScreen();
		handled = true;
		break;
	
	case frmObjectFocusTakeEvent:
		if (eventP->data.frmObjectFocusTake.objectID == ContentsTitlesGadget)
		{
			RectangleType	bounds;
			
			frmP = FrmGetActiveForm();
			FrmGetObjectBounds(
				frmP,
				FrmGetObjectIndex(frmP, ContentsTitlesGadget),
				&bounds);
			FrmGlueNavDrawFocusRing(
				frmP,
				ContentsTitlesGadget,
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
		if (RctPtInRectangle(x, y, &titleList.bounds))
		{
			ContentsTapped(x, y);
			handled = true;
		}
		#else
		if (RctPtInRectangle(eventP->screenX, eventP->screenY, &titleList.bounds))
		{
			ContentsTapped(eventP->screenX, eventP->screenY);
			handled = true;
		}
		#endif
		break;

	case keyDownEvent:
		handled = ContentsFormHandleKey(eventP);
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case ContentsReturnButton:
			ContentsDoAction(ACTRETURN);
			handled = true;
			break;
		}
		break;
	
	case sclRepeatEvent:
		if (eventP->data.sclRepeat.value != eventP->data.sclRepeat.newValue)
		{
			titleList.top = eventP->data.sclRepeat.newValue;
			ContentsDrawScreen();
		}
		break;
	}
	
	return handled;
}

Boolean ContentsFormHandleKey(EventPtr keyDownEventP)
{
	Boolean handled;
	Boolean processKey = true;
	
	switch (keyDownEventP->data.keyDown.chr)
	{
	case pageUpChr:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[4]);
		break;

	case pageDownChr:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[5]);
		break;

	case vchrHard1:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[0]);
		break;
		
	case vchrHard2:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[1]);
		break;
		
	case vchrHard3:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[2]);
		break;
		
	case vchrHard4:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[3]);
		break;

	case vchrJogUp:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[6]);
		break;

	case vchrJogDown:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[7]);
		break;

	case vchrJogRelease:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[8]);
		break;
	
	case vchrCalc:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[14]);
		break;
		
	case vchrFind:
		handled = ContentsDoAction(g_prefs.uiTOC.buttons[15]);
		break;

	case vchrNavChange:
		if (keyDownEventP->data.keyDown.keyCode & navBitUp)
			ContentsDoAction(g_prefs.uiTOC.buttons[9]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitDown))
			ContentsDoAction(g_prefs.uiTOC.buttons[10]);
		else if (keyDownEventP->data.keyDown.keyCode & navBitLeft)
			ContentsDoAction(g_prefs.uiTOC.buttons[11]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitRight))
			ContentsDoAction(g_prefs.uiTOC.buttons[12]);
		else if ((keyDownEventP->data.keyDown.keyCode & navBitSelect))
			ContentsDoAction(g_prefs.uiTOC.buttons[13]);
		else
			handled = false;
		break;

	default:
		if (IsFiveWayNavEvent(keyDownEventP))
		{
			if (FrmGlueNavIsSupported())
			{
				FormPtr frmP = FrmGetActiveForm();
				if (FrmGetObjectId(frmP, FrmGetFocus(frmP)) == ContentsTitlesGadget)
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
				ContentsDoAction(g_prefs.uiTOC.buttons[9]);
				break;
				
			case vchrRockerDown:
				ContentsDoAction(g_prefs.uiTOC.buttons[10]);
				break;
			
			case vchrRockerLeft:
				ContentsDoAction(g_prefs.uiTOC.buttons[11]);
				break;
				
			case vchrRockerRight:
				ContentsDoAction(g_prefs.uiTOC.buttons[12]);
				break;
			
			case vchrRockerCenter:
				ContentsDoAction(g_prefs.uiTOC.buttons[13]);
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

void ContentsFontChanged()
{
	titleList.height = 0;
	titleList.num = 0;
}
