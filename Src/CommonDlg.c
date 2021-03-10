#include "VBook.h"
#include "LCD.h"
#include "CustomFont.h"
#include "CommonDlg.h"

#define MAX_TAP_ACTIONS		5
#define MAX_BUT_ACTIONS		11
#define MAX_ITEM_LEN		22

#define MAX_BOOKS_ACTIONS	6
#define MAX_TOC_ACTIONS		7
#define MAX_VIEW_ACTIONS	11

#pragma mark === Struction Define ===

typedef struct {
	UInt16			form;
	UInt16			pushButton;
	UInt8			actions[MAX_BUT_ACTIONS];
	Char			items[MAX_BUT_ACTIONS][MAX_ITEM_LEN];
	Char*			itemsP[MAX_BUT_ACTIONS];
	Int16			num;
	ButtonsType		uiBooks;
	ButtonsType		uiTOC;
	ButtonsType		uiView;
} ButtonsSetType, *ButtonsSetPtr;

typedef struct {
	UInt8		actions[MAX_TAP_ACTIONS];
	AreaType	area;
} TapSetType, *TapSetPtr;

typedef struct {
	Char		text[8];
	Char*		modesList[2];
	UInt8		textColor;
	UInt8		backColor;
	UInt8		textColor2;
	UInt8		backColor2;
	UInt32		depth;
} ColorSetType, *ColorSetPtr;


#ifdef USE_SILKSCREEN

typedef enum {
	formStandard,
	formPortrait,
	formLandscape
} formRectEnum;

static formRectEnum l_mainFormRect = formStandard;
static formRectEnum l_tocFormRect = formStandard;
static formRectEnum l_viewFormRect = formStandard;

#endif // USE_SILKSCREEN


static UInt16	formID;
static UInt32*	shareP = NULL;

#pragma mark === Internal Functions ===

static void GetMemInfo(
	UInt32* dynaFreeP, UInt32* dynaTotalP,
	UInt32* storFreeP, UInt32* storTotalP)
{
	UInt16	card;
	UInt16	heap;
	UInt32	dynamicHeapTotal = 0;
	UInt32	dynamicHeapFree = 0;
	UInt32	storageHeapTotal = 0;
	UInt32	storageHeapFree = 0;
	UInt32	freeMem, maxMem;
	
	for (card=0; card < MemNumCards(); card++)
	{
		for (heap=0; heap < MemNumRAMHeaps(card); heap++)
		{
			if (MemHeapDynamic(MemHeapID(card, heap)))
			{
				dynamicHeapTotal += MemHeapSize(MemHeapID(card, heap));
				MemHeapFreeBytes(MemHeapID(card, heap), &freeMem, &maxMem);
				dynamicHeapFree += freeMem;
			}
			else
			{
				storageHeapTotal += MemHeapSize(MemHeapID(card, heap));
				MemHeapFreeBytes(MemHeapID(card, heap), &freeMem, &maxMem);
				storageHeapFree += freeMem;
			}
		}
	}
	
	if (dynaFreeP)
		*dynaFreeP = dynamicHeapFree;
	if (dynaTotalP)
		*dynaTotalP = dynamicHeapTotal;
	if (storFreeP)
		*storFreeP = storageHeapFree;
	if (storTotalP)
		*storTotalP = storageHeapTotal;
}

static void DisplayFormPreview(UInt8 textColor, UInt8 backColor)
{
	FormPtr			frmP;
	RectangleType	rect;
	ColorSetPtr		clrP = (ColorSetPtr)shareP;
	
	frmP = FrmGetActiveForm();
	FrmGetObjectBounds(frmP,
		FrmGetObjectIndex(frmP, DisplaySampleGadget), &rect);
	WinDrawRectangleFrame(simpleFrame, &rect);
	
	WinPushDrawState();
	WinSetTextColor(textColor);
	WinSetBackColor(backColor);
	
	WinEraseRectangle(&rect, 0);
	
	FntSetFont(boldFont);
	WinDrawChars(
		clrP->text, StrLen(clrP->text),
		rect.topLeft.x + 10, rect.topLeft.y + 8);

	WinPopDrawState();
}

// Show color setting.
static void DisplayFormColor(Boolean show, Boolean day)
{
	FormPtr			frmP;
	UInt16			i, num, objectId;
	ColorSetPtr		clrP = (ColorSetPtr)shareP;

	#ifndef PALMOS_50
	ControlPtr		ctlP;
	#endif
	
	frmP = FrmGetActiveForm();
	num = FrmGetNumberOfObjects(frmP);
	for (i=0; i < num; i++)
	{
		objectId = FrmGetObjectId(frmP, i);

		if (objectId == DisplayOKButton ||
			objectId == DisplayCancelButton ||
			objectId == DisplayColorLabel ||
			objectId == DisplayColorPopTrigger ||
			objectId == DisplayModesList)
			continue;

		#ifdef PALMOS_50
		if (objectId == DisplayStyleCheckbox)
			continue;
		#endif
		
		if (show)
			FrmShowObject(frmP, i);
		else
			FrmHideObject(frmP, i);
	}
	
	if (show)
	{
		if (day)
			DisplayFormPreview(clrP->textColor, clrP->backColor);
		else
			DisplayFormPreview(clrP->textColor2, clrP->backColor2);

		#ifndef PALMOS_50
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayStyleCheckbox));
		CtlSetValue(ctlP, g_prefs.options & OPOS5STYLE);
		#endif
	}
}

static void DisplayFormInit(FormPtr frmP)
{
	UInt32			depth;
	ListPtr			lstP;
	ControlPtr		ctlP;
	ColorSetPtr		clrP = (ColorSetPtr)shareP;
	
	clrP->depth			= g_prefs.depth;
	
	if (g_prefs.options & OPDON)
	{
		// In day color mode.
		clrP->textColor		= g_prefs.textColor;
		clrP->backColor		= g_prefs.backColor;
		clrP->textColor2	= g_prefs2.textColor2;
		clrP->backColor2	= g_prefs2.backColor2;
	}
	else
	{
		// In night color mode.
		clrP->textColor		= g_prefs2.textColor2;
		clrP->backColor		= g_prefs2.backColor2;
		clrP->textColor2	= g_prefs.textColor;
		clrP->backColor2	= g_prefs.backColor;
	}

	StrCopy(clrP->text, "Hello!");
	
	clrP->modesList[0] = MemPtrNew(12);
	clrP->modesList[1] = MemPtrNew(12);
	
	SysStringByIndex(ModesStringList, 0, clrP->modesList[0], 12);
	SysStringByIndex(ModesStringList, 1, clrP->modesList[1], 12);
	
	#ifdef PALMOS_30
	depth = 1;
	#else
	LCDWinScreenMode(
		winScreenModeGetSupportedDepths, NULL, NULL, &depth, NULL);
	#endif

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayModesList));
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayColorPopTrigger));

	if (depth & 0x80) // Support color.
	{
		LstSetListChoices(lstP, clrP->modesList, 2);
		LstSetHeight(lstP, 2);
	}
	else
	{
		LstSetListChoices(lstP, clrP->modesList, 1);
		LstSetHeight(lstP, 1);
	}
	
	if (g_prefs.depth == 1)
	{
		CtlSetLabel(ctlP, clrP->modesList[0]);
		LstSetSelection(lstP, 0);
	}
	else
	{
		CtlSetLabel(ctlP, clrP->modesList[1]);
		LstSetSelection(lstP, 1);
		if (g_prefs.options & OPDON)
			DisplayFormColor(true, true);
		else
			DisplayFormColor(true, false);
	}
	
	if (g_prefs.options & OPDON)
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayDayPushButton));
	else
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayNightPushButton));
	CtlSetValue(ctlP, 1);
}

#ifdef USE_SILKSCREEN

static void GetFormRect(RectangleType* boundsP, formRectEnum* fRectP)
{
	if (boundsP->extent.x == boundsP->extent.y)
		*fRectP = formStandard;
	else if (boundsP->extent.x > boundsP->extent.y)
		*fRectP = formLandscape;
	else
		*fRectP = formPortrait;
}


static void ResizeMainForm(FormPtr frmP, Coord extentX, Coord extentY)
{
	RectangleType	objBounds;
	Coord			sbarWidth, height;

	// MainVsbarScrollBar
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainVsbarScrollBar),
		&objBounds);
	
	sbarWidth = objBounds.extent.x;
	objBounds.topLeft.x = extentX - sbarWidth;
	
	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainVsbarScrollBar),
		&objBounds);


	// MainOperationList
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainOperationList),
		&objBounds);

	objBounds.topLeft.y = extentY - objBounds.extent.y;

	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainOperationList),
		&objBounds);


	// MainChoicesPopTrigger
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainChoicesPopTrigger),
		&objBounds);

	objBounds.topLeft.y = extentY - objBounds.extent.y;

	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainChoicesPopTrigger),
		&objBounds);


	// MainBooksGadget
	height = objBounds.extent.y;
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainBooksGadget),
		&objBounds);

	objBounds.extent.x = extentX - sbarWidth;
	objBounds.extent.y = extentY - objBounds.topLeft.y - height;

	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, MainBooksGadget),
		&objBounds);
}

static void ResizeContentsForm(FormPtr frmP, Coord extentX, Coord extentY)
{
	RectangleType	objBounds;
	Int16			sbarWidth;
	
	// ContentsVsbarScrollBar
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ContentsVsbarScrollBar),
		&objBounds);

	sbarWidth = objBounds.extent.x;
	objBounds.topLeft.x = extentX - sbarWidth;

	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ContentsVsbarScrollBar),
		&objBounds);


	// ContentsTitlesGadget
	FrmGetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ContentsTitlesGadget),
		&objBounds);

	objBounds.extent.x = extentX - sbarWidth;
	objBounds.extent.y = extentY - objBounds.topLeft.y;

	FrmSetObjectBounds(
		frmP,
		FrmGetObjectIndex(frmP, ContentsTitlesGadget),
		&objBounds);
}

static void ResizeViewForm(FormPtr frmP, Coord extentX, Coord extentY)
{
	UInt16			objNum, i;
	RectangleType	objBounds;
	FormObjectKind	kind;

	objNum = FrmGetNumberOfObjects(frmP);
	for (i = 0; i < objNum; i++)
	{
		MemSet(&objBounds, sizeof(RectangleType), 0);
		kind = FrmGetObjectType(frmP, i);
		
		if (kind == frmBitmapObj)
			FrmGetObjectPosition(frmP, i,
				&objBounds.topLeft.x, &objBounds.topLeft.y);
		else
			FrmGetObjectBounds(frmP, i, &objBounds);
		
		if (objBounds.extent.y == 8 ||
			(kind == frmBitmapObj && objBounds.topLeft.y != 0))
		{
			objBounds.topLeft.y = extentY - 8;
		}
		else if (FrmGetObjectId(frmP, i) == ViewViewGadget)
		{
			objBounds.extent.x = extentX;
			
			if (g_prefs.options & OPMINIBAR)
			{
				objBounds.topLeft.y = 6;
				objBounds.extent.y = extentY - 6;
			}
			else
			{
				objBounds.topLeft.y = 0;
				objBounds.extent.y = extentY;
			}

			if (g_prefs.options & OPTOOLBAR)
				objBounds.extent.y -= 9;
		}
		else
			continue;

		if (kind == frmBitmapObj)
			FrmSetObjectPosition(frmP, i, objBounds.topLeft.x, objBounds.topLeft.y);
		else
			FrmSetObjectBounds(frmP, i, &objBounds);
	}
}

#endif // USE_SILKSCREEN

static void ButUpdateActList(UInt16 form, Boolean redraw)
{
	Int16			i;
	FormPtr			frmP;
	ListPtr			lstP;
	ButtonsSetPtr	butsSetP = (ButtonsSetPtr)shareP;
	
	butsSetP->form = form;
	
	switch (form)
	{
	case MainForm:
		butsSetP->num = MAX_BOOKS_ACTIONS;
		for (i = 0; i < MAX_BOOKS_ACTIONS; i++)
			butsSetP->itemsP[i] = SysStringByIndex(
				BooksActionsStringList, i, butsSetP->items[i], MAX_ITEM_LEN);

		butsSetP->actions[0]	= ACTNONE;
		butsSetP->actions[1]	= ACTLINEUP;
		butsSetP->actions[2]	= ACTLINEDOWN;
		butsSetP->actions[3]	= ACTPAGEUP;
		butsSetP->actions[4]	= ACTPAGEDOWN;
		butsSetP->actions[5]	= ACTSELECT;
		
		butsSetP->pushButton	= ButtonsBooksPushButton;
		break;
		
	case ContentsForm:
		butsSetP->num = MAX_TOC_ACTIONS;
		for (i = 0; i < MAX_TOC_ACTIONS; i++)
			butsSetP->itemsP[i] = SysStringByIndex(
				TOCActionsStringList, i, butsSetP->items[i], MAX_ITEM_LEN);

		butsSetP->actions[0]	= ACTNONE;
		butsSetP->actions[1]	= ACTLINEUP;
		butsSetP->actions[2]	= ACTLINEDOWN;
		butsSetP->actions[3]	= ACTPAGEUP;
		butsSetP->actions[4]	= ACTPAGEDOWN;
		butsSetP->actions[5]	= ACTRETURN;
		butsSetP->actions[6]	= ACTSELECT;
		
		butsSetP->pushButton	= ButtonsTOCPushButton;
		break;
		
	case ViewForm:
		butsSetP->num = MAX_VIEW_ACTIONS;
		for (i = 0; i < MAX_VIEW_ACTIONS; i++)
			butsSetP->itemsP[i] = SysStringByIndex(
				ViewActionsStringList, i, butsSetP->items[i], MAX_ITEM_LEN);

		butsSetP->actions[0]	= ACTNONE;
		butsSetP->actions[1]	= ACTLINEUP;
		butsSetP->actions[2]	= ACTLINEDOWN;
		butsSetP->actions[3]	= ACTPAGEUP;
		butsSetP->actions[4]	= ACTPAGEDOWN;
		butsSetP->actions[5]	= ACTCLOSE;
		butsSetP->actions[6]	= ACTTOC;
		butsSetP->actions[7]	= ACTPREV;
		butsSetP->actions[8]	= ACTNEXT;
		butsSetP->actions[9]	= ACTPLAY;
		butsSetP->actions[10]	= ACTUI;
		
		butsSetP->pushButton	= ButtonsViewPushButton;
		break;
	}
	
	frmP = FrmGetActiveForm();
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsActionsList));
	LstSetListChoices(lstP, butsSetP->itemsP, butsSetP->num);
	if (redraw)
		LstDrawList(lstP);
}

static void ButSetActList(Boolean redraw)
{
	FormPtr			frmP;
	ListPtr			lstP;
	Int16			button, action;
	ButtonsSetPtr	butsSetP = (ButtonsSetPtr)shareP;
	ButtonsPtr		butsP;
	
	frmP = FrmGetActiveForm();
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsButtonsList));
	button = LstGetSelection(lstP);
	if (button == noListSelection)
		return;
	
	if (butsSetP->form == MainForm)
		butsP = &butsSetP->uiBooks;
	else if (butsSetP->form == ContentsForm)
		butsP = &butsSetP->uiTOC;
	else
		butsP = &butsSetP->uiView;
	
	for (action = 0; action < butsSetP->num; action++)
		if (butsSetP->actions[action] == butsP->buttons[button])
			break;
	
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsActionsList));
	LstSetSelection(lstP, action);
	if (redraw)
		LstDrawList(lstP);
}

static void ButSetAction()
{
	FormPtr			frmP;
	ListPtr			lstP;
	Int16			button, action;
	ButtonsSetPtr	butsSetP = (ButtonsSetPtr)shareP;
	ButtonsPtr		butsP;
	
	frmP = FrmGetActiveForm();

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsButtonsList));
	button = LstGetSelection(lstP);
	if (button == noListSelection)
		return;

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsActionsList));
	action = LstGetSelection(lstP);
	if (action == noListSelection)
		return;

	if (butsSetP->form == MainForm)
		butsP = &butsSetP->uiBooks;
	else if (butsSetP->form == ContentsForm)
		butsP = &butsSetP->uiTOC;
	else
		butsP = &butsSetP->uiView;
	
	butsP->buttons[button] = butsSetP->actions[action];
}

static void TapSetActList(UInt8 act)
{
	FormPtr		frmP;
	ListPtr		lstP;
	Int16		i;
	TapSetPtr	tapP;
	
	frmP = FrmGetActiveForm();
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, TapActionsList));
	
	tapP = (TapSetPtr)shareP;
	for (i = 0; i < MAX_TAP_ACTIONS; i++)
		if (tapP->actions[i] == act)
			break;
	
	LstSetSelection(lstP, i);
	LstDrawList(lstP);
}

static void TapSetAction()
{
	FormPtr		frmP;
	ListPtr		lstP;
	ControlPtr	ctlP;
	TapSetPtr	tapP;
	Int16		i, sel;
	
	frmP = FrmGetActiveForm();
	
	for (i = TapQ1PushButton; i <= TapQ4PushButton; i++)
	{
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, i));
		if (CtlGetValue(ctlP))
			break;
	}
	if (i > TapQ4PushButton)
		return;
	i -= TapQ1PushButton;

	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, TapActionsList));
	sel = LstGetSelection(lstP);
	if (sel == noListSelection)
		return;
	
	tapP = (TapSetPtr)shareP;
	tapP->area.areas[i] = tapP->actions[sel];
}

#pragma mark === Entry Points ===

void ButtonsDefault(UInt16 form, ButtonsPtr buttonsP)
{
	switch (form)
	{
	case MainForm:
		buttonsP->buttons[0]	= ACTNONE;	// Hard key 1
		buttonsP->buttons[1]	= ACTNONE;	// Hard key 2
		buttonsP->buttons[2]	= ACTNONE;	// Hard key 3
		buttonsP->buttons[3]	= ACTNONE;	// Hard key 4

		buttonsP->buttons[4]	= ACTPAGEUP;	// Up
		buttonsP->buttons[5]	= ACTPAGEDOWN;	// Down

		buttonsP->buttons[6]	= ACTLINEUP;	// Jog Up
		buttonsP->buttons[7]	= ACTLINEDOWN;	// Jog Down
		buttonsP->buttons[8]	= ACTSELECT;	// Jog Press

		buttonsP->buttons[9]	= ACTLINEUP;	// 5-Ways Up
		buttonsP->buttons[10]	= ACTLINEDOWN;	// 5-Ways Down
		buttonsP->buttons[11]	= ACTNONE;		// 5-Ways Left
		buttonsP->buttons[12]	= ACTNONE;		// 5-Ways Right
		buttonsP->buttons[13]	= ACTSELECT;	// 5-Ways Center
		
		buttonsP->buttons[14]	= ACTNONE;		// Soft key 3
		buttonsP->buttons[15]	= ACTNONE;		// Soft key 4
		break;
	
	case ContentsForm:
		buttonsP->buttons[0]	= ACTNONE;
		buttonsP->buttons[1]	= ACTNONE;
		buttonsP->buttons[2]	= ACTNONE;
		buttonsP->buttons[3]	= ACTNONE;

		buttonsP->buttons[4]	= ACTPAGEUP;
		buttonsP->buttons[5]	= ACTPAGEDOWN;

		buttonsP->buttons[6]	= ACTLINEUP;
		buttonsP->buttons[7]	= ACTLINEDOWN;
		buttonsP->buttons[8]	= ACTSELECT;

		buttonsP->buttons[9]	= ACTLINEUP;
		buttonsP->buttons[10]	= ACTLINEDOWN;
		buttonsP->buttons[11]	= ACTNONE;
		buttonsP->buttons[12]	= ACTNONE;
		buttonsP->buttons[13]	= ACTSELECT;

		buttonsP->buttons[14]	= ACTNONE;
		buttonsP->buttons[15]	= ACTNONE;
		break;
		
	case ViewForm:
		buttonsP->buttons[0]	= ACTNONE;
		buttonsP->buttons[1]	= ACTNONE;
		buttonsP->buttons[2]	= ACTNONE;
		buttonsP->buttons[3]	= ACTNONE;

		buttonsP->buttons[4]	= ACTPAGEUP;
		buttonsP->buttons[5]	= ACTPAGEDOWN;

		buttonsP->buttons[6]	= ACTPAGEUP;
		buttonsP->buttons[7]	= ACTPAGEDOWN;
		buttonsP->buttons[8]	= ACTPLAY;

		buttonsP->buttons[9]	= ACTPAGEUP;
		buttonsP->buttons[10]	= ACTPAGEDOWN;
		buttonsP->buttons[11]	= ACTNONE;
		buttonsP->buttons[12]	= ACTNONE;
		buttonsP->buttons[13]	= ACTNONE;

		buttonsP->buttons[14]	= ACTNONE;
		buttonsP->buttons[15]	= ACTNONE;
		break;
	}
}

void TapDefault(AreaPtr areaP)
{
	areaP->areas[0]	=	ACTPAGEUP;
	areaP->areas[1]	=	ACTLINEUP;
	areaP->areas[2]	=	ACTLINEDOWN;
	areaP->areas[3]	=	ACTPAGEDOWN;
}

void NewStyle()
{
	RGBColorType	rgb;
	
	rgb.r = 206;
	rgb.g = 255;
	rgb.b = 156;
	UIColorSetTableEntry(UIObjectFill, &rgb);
	UIColorSetTableEntry(UIFieldBackground, &rgb);

	rgb.r = 206;
	rgb.g = 207;
	rgb.b = 255;
	UIColorSetTableEntry(UIMenuFill, &rgb);

	rgb.r = 255;
	rgb.g = 207;
	rgb.b = 255;
	UIColorSetTableEntry(UIFormFill, &rgb);

	rgb.r = 255;
	rgb.g = 255;
	rgb.b = 156;
	UIColorSetTableEntry(UIDialogFill, &rgb);

	rgb.r = 255;
	rgb.g = 207;
	rgb.b = 156;
	UIColorSetTableEntry(UIAlertFill, &rgb);
}

void CmnDlgSetReturnForm(UInt16 id)
{
	formID = id;
}

void RuntimeInfo()
{
	UInt32		dynaFree, dynaTotal, storFree, storTotal;
	Char*		str;
	FormPtr		frmP;
	
	frmP = FrmInitForm(InformationForm);
	str = MemPtrNew(8);
	
	dynaFree = CfnFontLoad();
	StrPrintF(str, "%ldK", dynaFree / 1024);
	FrmCopyLabel(frmP, InformationFontLoadLabel, str);

	GetMemInfo(&dynaFree, &dynaTotal, &storFree, &storTotal);

	StrPrintF(str, "%ldK", dynaFree / 1024);
	FrmCopyLabel(frmP, InformationDynaFreeLabel, str);

	StrPrintF(str, "%ldK", dynaTotal / 1024);
	FrmCopyLabel(frmP, InformationDynaTotalLabel, str);

	StrPrintF(str, "%ldK", storFree / 1024);
	FrmCopyLabel(frmP, InformationStorFreeLabel, str);

	StrPrintF(str, "%ldK", storTotal / 1024);
	FrmCopyLabel(frmP, InformationStorTotalLabel, str);
	
	MemPtrFree(str);
	FrmDoDialog(frmP);
	FrmDeleteForm(frmP);
}

Boolean MyFontSelect()
{
	Int16		i;
	FormPtr		frmP;
	Char		*fontsName[4], *p;
	ControlPtr	ctlP;
	ListPtr		lstP;
	FieldPtr	fldP;
	MemHandle	txtH, oldTxtH;
	Boolean		result, selChsFont, selEngFont;
	
	result = false;

	selChsFont = (CfnGetAvailableFonts() - 4 > 0) ? true : false;
	#ifdef HIGH_DENSITY
	selEngFont = true;
	#else
	selEngFont = false;
	#endif
	
	for (i = 4; i < CfnGetAvailableFonts(); i++)
		fontsName[i - 4] = CfnGetFontName(i);
	
	frmP = FrmInitForm(FontForm);

	// Set system font seleciton.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontSysfontPopTrigger));
	lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontSysfontList));
	CtlSetLabel(ctlP, LstGetSelectionText(lstP, g_prefs2.sysFont));
	LstSetSelection(lstP, g_prefs2.sysFont);

	// Set line space.
	txtH = MemHandleNew(4);
	p = MemHandleLock(txtH);
	StrPrintF(p, "%d", g_prefs.lineSpace);
	MemHandleUnlock(txtH);
	fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontLinespaceField));
	oldTxtH = FldGetTextHandle(fldP);
	FldSetTextHandle(fldP, txtH);
	FldSetSelection(fldP, 0, FldGetTextLength(fldP));
	if (oldTxtH)
		MemHandleFree(oldTxtH);
	
	// Set chinese only option.
	ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontChsonlyCheckbox));
	CtlSetValue(ctlP, g_prefs.options & OPCHSONLY);
	
	// Set english font.
	if (selEngFont)
	{
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontEngfontPopTrigger));
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontEngfontList));
		CtlSetLabel(ctlP, LstGetSelectionText(lstP, g_prefs2.engFont));
		LstSetSelection(lstP, g_prefs2.engFont);
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontEngfontLabel));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontEngfontPopTrigger));
	}

	if (selChsFont)
	{
		// Use additional font.
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontUseCheckbox));
		CtlSetValue(ctlP, g_prefs.options & OPUSEADDFONT);

		// Set chinese font selection.
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontChsfontPopTrigger));
		CtlSetLabel(ctlP, fontsName[g_prefs2.chsFont]);
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontChsfontList));
		LstSetListChoices(lstP, (Char**)fontsName, CfnGetAvailableFonts() - 4);
		LstSetSelection(lstP, g_prefs2.chsFont);
		LstSetHeight(lstP, CfnGetAvailableFonts() - 4);
		
		// Set chinese word space.
		txtH = MemHandleNew(4);
		p = MemHandleLock(txtH);
		StrPrintF(p, "%d", g_prefs2.wordSpace);
		MemHandleUnlock(txtH);
		fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontWordspaceField));
		oldTxtH = FldGetTextHandle(fldP);
		FldSetTextHandle(fldP, txtH);
		if (oldTxtH)
			MemHandleFree(oldTxtH);
	}
	else
	{
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontUseCheckbox));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontChsfontLabel));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontChsfontPopTrigger));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontChsspaceLabel));
		FrmHideObject(frmP, FrmGetObjectIndex(frmP, FontWordspaceField));
	}

	FrmSetFocus(frmP, FrmGetObjectIndex(frmP, FontLinespaceField));

	if (FrmDoDialog(frmP) == FontOkButton)
	{
		result = true;
		
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontSysfontList));
		if (LstGetSelection(lstP) != noListSelection)
			g_prefs2.sysFont = LstGetSelection(lstP);
				
		fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontLinespaceField));
		p = FldGetTextPtr(fldP);
		g_prefs.lineSpace = StrAToI(p);
		if (g_prefs.lineSpace < 0)
			g_prefs.lineSpace = 0;
		if (g_prefs.lineSpace > 16)
			g_prefs.lineSpace = 16;
		
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontChsonlyCheckbox));
		g_prefs.options ^= OPCHSONLY;
		if (CtlGetValue(ctlP))
			g_prefs.options |= OPCHSONLY;
		
		if (selEngFont)
		{
			lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontEngfontList));
			if (LstGetSelection(lstP) != noListSelection)
				g_prefs2.engFont = LstGetSelection(lstP);
		}

		if (selChsFont)
		{
			ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontUseCheckbox));
			g_prefs.options |= OPUSEADDFONT;
			if (!CtlGetValue(ctlP))
				g_prefs.options ^= OPUSEADDFONT;
			
			lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontChsfontList));
			if (LstGetSelection(lstP) != noListSelection)
				g_prefs2.chsFont = LstGetSelection(lstP);

			fldP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, FontWordspaceField));
			p = FldGetTextPtr(fldP);
			g_prefs2.wordSpace = StrAToI(p);
			if (g_prefs2.wordSpace < 0)
				g_prefs2.wordSpace = 0;
			if (g_prefs2.wordSpace > 6)
				g_prefs2.wordSpace = 6;
		}
	}

	FrmDeleteForm(frmP);
	
	return result;
}

Boolean AboutFormHandleEvent(EventType* eventP)
{
	Boolean handled = false;
	FormPtr frmP;
		
	switch (eventP->eType)
	{
	case frmOpenEvent:
		frmP = FrmGetActiveForm();
		FrmCopyLabel(frmP, AboutBuildLabel, BUILD_DATE);
		
		if (g_prefs.fake != 1977)
		{
			DateTimeType date;
			Char expStr[20];
			
			if (g_prefs.fake == 11)
			{
				StrPrintF (expStr, "EXPIRED");
			}
			else
			{
				TimSecondsToDateTime (g_prefs.expired, &date);
				StrPrintF(expStr, "EXP. %d.%d.%d", date.year, date.month, date.day);
			}
			FrmCopyLabel(frmP, AboutExpLabel, expStr);
			FrmShowObject(frmP, FrmGetObjectIndex(frmP, AboutExpLabel));
		}

		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case ctlSelectEvent:
		FrmGotoForm(formID);
		handled = true;
		break;
	}

	return handled;
}

Boolean DisplayFormHandleEvent(EventType* eventP)
{
	Boolean				handled = false;
	FormPtr				frmP;
	ColorSetPtr			clrP;
	IndexedColorType	ic;
	ControlPtr			ctlP;
	UInt32				w, h;

	switch (eventP->eType)
	{
	case frmOpenEvent:
		shareP = MemPtrNew(sizeof(ColorSetType));
		frmP = FrmGetActiveForm();
		FrmDrawForm(frmP);
		DisplayFormInit(frmP);
		handled = true;
		break;

	case frmCloseEvent:
		clrP = (ColorSetPtr)shareP;
		MemPtrFree(clrP->modesList[0]);
		MemPtrFree(clrP->modesList[1]);
		MemPtrFree(shareP);
		shareP = NULL;
		break;
		
	case popSelectEvent:
		frmP = FrmGetActiveForm();
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayDayPushButton));

		if (eventP->data.popSelect.selection !=
			eventP->data.popSelect.priorSelection)
		{
			clrP = (ColorSetPtr)shareP;
			switch (eventP->data.popSelect.selection)
			{
			case 0: // Black / White
				DisplayFormColor(false, true);
				clrP->depth = 1;
				w = g_LCDWidth;
				h = g_LCDHeight;
				LCDWinScreenMode(
					winScreenModeSet, &w, &h, &clrP->depth, NULL);
				CfnSetDepth(clrP->depth);
				FrmDrawForm(frmP);
				break;
				
			case 1: // 256 Colors
				clrP->depth = 8;
				w = g_LCDWidth;
				h = g_LCDHeight;
				LCDWinScreenMode(
					winScreenModeSet, &w, &h, &clrP->depth, NULL);
				CfnSetDepth(clrP->depth);
				DisplayFormColor(true, (Boolean)CtlGetValue(ctlP));
				FrmDrawForm(frmP);
				break;
			}
		}
		break;
		
	case ctlSelectEvent:
		clrP = (ColorSetPtr)shareP;
		frmP = FrmGetActiveForm();
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, DisplayDayPushButton));

		switch (eventP->data.ctlSelect.controlID)
		{
		case DisplayTextButton:
			ic = (CtlGetValue(ctlP)) ? clrP->textColor : clrP->textColor2;
			if (UIPickColor(&ic, NULL, UIPickColorStartPalette, "Text Color", NULL))
			{
				if (CtlGetValue(ctlP))
					clrP->textColor = ic;
				else
					clrP->textColor2 = ic;
			}
			if (CtlGetValue(ctlP))
				DisplayFormPreview(clrP->textColor, clrP->backColor);
			else
				DisplayFormPreview(clrP->textColor2, clrP->backColor2);
			handled = true;
			break;
			
		case DisplayBackButton:
			ic = (CtlGetValue(ctlP)) ? clrP->backColor : clrP->backColor2;
			if (UIPickColor(&ic, NULL, UIPickColorStartPalette, "Background Color", NULL))
			{
				if (CtlGetValue(ctlP))
					clrP->backColor = ic;
				else
					clrP->backColor2 = ic;
			}
			if (CtlGetValue(ctlP))
				DisplayFormPreview(clrP->textColor, clrP->backColor);
			else
				DisplayFormPreview(clrP->textColor2, clrP->backColor2);
			handled = true;
			break;
		
		case DisplayDayPushButton:
			DisplayFormPreview(clrP->textColor, clrP->backColor);
			handled = true;
			break;
		
		case DisplayNightPushButton:
			DisplayFormPreview(clrP->textColor2, clrP->backColor2);
			handled = true;
			break;
		
		case DisplayDefaultButton:
			clrP->textColor		= 203;
			clrP->backColor		= 6;
			clrP->textColor2	= 206;
			clrP->backColor2	= 230;
			
			if (CtlGetValue(ctlP))
				DisplayFormPreview(clrP->textColor, clrP->backColor);
			else
				DisplayFormPreview(clrP->textColor2, clrP->backColor2);

			handled = true;
			break;
			
		case DisplayOKButton:
			g_prefs.depth = clrP->depth;
			if (clrP->depth > 1)
			{
				g_prefs.options |= OPDON;
				if (CtlGetValue(ctlP))
				{
					// In day color mode.
					g_prefs.textColor	= clrP->textColor;
					g_prefs.backColor	= clrP->backColor;
					g_prefs2.textColor2	= clrP->textColor2;
					g_prefs2.backColor2	= clrP->backColor2;
				}
				else
				{
					// In night color mode.
					g_prefs.textColor	= clrP->textColor2;
					g_prefs.backColor	= clrP->backColor2;
					g_prefs2.textColor2	= clrP->textColor;
					g_prefs2.backColor2	= clrP->backColor;
					g_prefs.options ^= OPDON;
				}
			}

			#ifndef PALMOS_50
			frmP = FrmGetActiveForm();
			g_prefs.options |= OPOS5STYLE;
			if (!CtlGetValue(FrmGetObjectPtr(frmP,
				FrmGetObjectIndex(frmP, DisplayStyleCheckbox))))
				g_prefs.options ^= OPOS5STYLE;
			#endif

			w = g_LCDWidth;
			h = g_LCDHeight;
			LCDWinScreenMode(
				winScreenModeSet, &w, &h, &g_prefs.depth, NULL);
			CfnSetDepth(g_prefs.depth);
			
			FrmGotoForm(formID);
			handled = true;
			break;
			
		case DisplayCancelButton:
			w = g_LCDWidth;
			h = g_LCDHeight;
			LCDWinScreenMode(
				winScreenModeSet, &w, &h, &g_prefs.depth, NULL);
			CfnSetDepth(g_prefs.depth);

			FrmGotoForm(formID);
			handled = true;
			break;
		}
		break;
	}
	
	return handled;
}

Boolean ButtonsFormHandleEvent(EventType* eventP)
{
	Boolean			handled = false;
	FormPtr			frmP;
	ControlPtr		ctlP;
	ListPtr			lstP;
	ButtonsSetPtr	butsSetP;
	
	switch (eventP->eType)
	{
	case frmOpenEvent:
		shareP = MemPtrNew(sizeof(ButtonsSetType));
		if (shareP == NULL)
			SysFatalAlert("Insufficient memory!");

		butsSetP = (ButtonsSetPtr)shareP;
		MemMove(&butsSetP->uiBooks, &g_prefs.uiBooks, sizeof(ButtonsType));
		MemMove(&butsSetP->uiTOC, &g_prefs.uiTOC, sizeof(ButtonsType));
		MemMove(&butsSetP->uiView, &g_prefs.uiView, sizeof(ButtonsType));
		ButUpdateActList(formID, false);

		frmP = FrmGetActiveForm();
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, butsSetP->pushButton));
		CtlSetValue(ctlP, 1);
		
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, ButtonsButtonsList));
		LstSetSelection(lstP, 0);
		ButSetActList(false);

		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case frmCloseEvent:
		if (shareP)
		{
			MemPtrFree(shareP);
			shareP = NULL;
		}
		break;
	
	case lstSelectEvent:
		switch (eventP->data.lstSelect.listID)
		{
		case ButtonsButtonsList:
			ButSetActList(true);
			break;
		
		case ButtonsActionsList:
			ButSetAction();
			break;
		}
		handled = true;
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		case ButtonsBooksPushButton:
			ButUpdateActList(MainForm, true);
			break;
		
		case ButtonsTOCPushButton:
			ButUpdateActList(ContentsForm, true);
			break;
		
		case ButtonsViewPushButton:
			ButUpdateActList(ViewForm, true);
			break;
			
		case ButtonsOKButton:
			butsSetP = (ButtonsSetPtr)shareP;
			MemMove(&g_prefs.uiBooks, &butsSetP->uiBooks, sizeof(ButtonsType));
			MemMove(&g_prefs.uiTOC, &butsSetP->uiTOC, sizeof(ButtonsType));
			MemMove(&g_prefs.uiView, &butsSetP->uiView, sizeof(ButtonsType));
			FrmGotoForm(formID);
			break;
		
		case ButtonsDefaultButton:
			ButtonsDefault(MainForm, &g_prefs.uiBooks);
			ButtonsDefault(ContentsForm, &g_prefs.uiTOC);
			ButtonsDefault(ViewForm, &g_prefs.uiView);
			FrmGotoForm(formID);
			break;
		
		case ButtonsCancelButton:
			FrmGotoForm(formID);
			break;
		}
		handled = true;
		break;
	}
	
	return handled;
}

Boolean ResizeScreen()
{
	Boolean			result = false;
	
	#ifdef USE_SILKSCREEN

	FormPtr			frmP;
	WinHandle		frmWinH;
	RectangleType	newBounds, oldBounds;
	formRectEnum	newFormRect, oldFormRect;

	frmP = FrmGetActiveForm();
	frmWinH = FrmGetWindowHandle(frmP);

	WinGetBounds(frmWinH, &oldBounds);
	GetFormRect(&oldBounds, &oldFormRect);

	WinGetBounds(WinGetDisplayWindow(), &newBounds);
	GetFormRect(&newBounds, &newFormRect);

	WinSetBounds(frmWinH, &newBounds);
	WinSetDrawWindow(frmWinH);
	
	switch (FrmGetFormId(frmP))
	{
	case MainForm:
		if ((newFormRect != oldFormRect) || (newFormRect != l_mainFormRect))
		{
			ResizeMainForm(frmP, newBounds.extent.x, newBounds.extent.y);
			l_mainFormRect = newFormRect;
			result = true;
		}
		break;
		
	case ContentsForm:
		if ((newFormRect != oldFormRect) || (newFormRect != l_tocFormRect))
		{
			ResizeContentsForm(frmP, newBounds.extent.x, newBounds.extent.y);
			l_tocFormRect = newFormRect;
			result = true;
		}
		break;
		
	case ViewForm:
		if ((newFormRect != oldFormRect) || (newFormRect != l_viewFormRect))
		{
			ResizeViewForm(frmP, newBounds.extent.x, newBounds.extent.y);
			l_viewFormRect = newFormRect;
			result = true;
		}
		break;
	}
	
	if (result)
		CfnUpdateRowBytes();
	
	#endif // USE_SILKSCREEN
	
	return result;
}

Boolean TapFormHandleEvent(EventPtr eventP)
{
	Boolean		handled = false;
	FormPtr		frmP;
	ListPtr		lstP;
	ControlPtr	ctlP;
	TapSetPtr	tapP;
	Int16		sel;
	
	switch (eventP->eType)
	{
	case frmOpenEvent:
		shareP = MemPtrNew(sizeof(TapSetType));
		tapP = (TapSetPtr)shareP;
		tapP->actions[0] = ACTNONE;
		tapP->actions[1] = ACTPAGEUP;
		tapP->actions[2] = ACTLINEUP;
		tapP->actions[3] = ACTLINEDOWN;
		tapP->actions[4] = ACTPAGEDOWN;
		MemMove(&tapP->area, &g_prefs.tap, sizeof(AreaType));

		frmP = FrmGetActiveForm();		
		lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, TapGWList));
		ctlP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, TapGWPopTrigger));

		sel = 0;
		if (g_prefs.options & OPGWCOPY)
			sel = 0;
		else if (g_prefs.options & OPGWROADLINGUA)
			sel = 1;
		else if (g_prefs.options & OPGWEXTDA)
			sel = 2;
		else if (g_prefs.options & OPGWVDICT)
			sel = 3;
		CtlSetLabel(ctlP, LstGetSelectionText(lstP, sel));
		LstSetSelection(lstP, sel);

		FrmDrawForm(frmP);
		handled = true;
		break;
	
	case frmCloseEvent:
		if (shareP)
		{
			MemPtrFree(shareP);
			shareP = NULL;
		}
		break;
	
	case lstSelectEvent:
		switch (eventP->data.lstSelect.listID)
		{
		case TapActionsList:
			TapSetAction();
			handled = true;
			break;
		}
		break;

	case ctlSelectEvent:
		tapP = (TapSetPtr)shareP;
		switch (eventP->data.ctlSelect.controlID)
		{
		case TapQ1PushButton:
			TapSetActList(tapP->area.areas[0]);
			handled = true;
			break;
		
		case TapQ2PushButton:
			TapSetActList(tapP->area.areas[1]);
			handled = true;
			break;
		
		case TapQ3PushButton:
			TapSetActList(tapP->area.areas[2]);
			handled = true;
			break;
		
		case TapQ4PushButton:
			TapSetActList(tapP->area.areas[3]);
			handled = true;
			break;
		
		case TapOKButton:
			MemMove(&g_prefs.tap, &tapP->area, sizeof(AreaType));
			frmP = FrmGetActiveForm();		
			lstP = FrmGetObjectPtr(frmP, FrmGetObjectIndex(frmP, TapGWList));
			sel = LstGetSelection(lstP);
			if (sel != noListSelection)
			{
				g_prefs.options |= OPGWCOPY;
				g_prefs.options |= OPGWROADLINGUA;
				g_prefs.options |= OPGWEXTDA;
				g_prefs.options |= OPGWVDICT;

				switch (sel)
				{
				case 0: // COPY
					g_prefs.options ^= OPGWROADLINGUA;
					g_prefs.options ^= OPGWEXTDA;
					g_prefs.options ^= OPGWVDICT;
					break;
				
				case 1: // Roadlingua
					g_prefs.options ^= OPGWCOPY;
					g_prefs.options ^= OPGWEXTDA;
					g_prefs.options ^= OPGWVDICT;
					break;
				
				case 2: // External DA
					g_prefs.options ^= OPGWCOPY;
					g_prefs.options ^= OPGWROADLINGUA;
					g_prefs.options ^= OPGWVDICT;
					break;
					
				case 3: // Internal Dict
					g_prefs.options ^= OPGWCOPY;
					g_prefs.options ^= OPGWROADLINGUA;
					g_prefs.options ^= OPGWEXTDA;
					break;
				}
			}

			FrmGotoForm(ViewForm);
			handled = true;
			break;
		
		case TapDefaultButton:
			TapDefault(&g_prefs.tap);

			g_prefs.options |= OPGWCOPY;
			g_prefs.options |= OPGWROADLINGUA;
			g_prefs.options ^= OPGWROADLINGUA;
			g_prefs.options |= OPGWEXTDA;
			g_prefs.options ^= OPGWEXTDA;
			g_prefs.options |= OPGWVDICT;
			g_prefs.options ^= OPGWVDICT;
			
//			g_prefs2.daName[0] = 0;

			FrmGotoForm(ViewForm);
			handled = true;
			break;
		
		case TapCancelButton:
			FrmGotoForm(ViewForm);
			handled = true;
			break;
		}
		break;
	}
	
	return handled;
}

void DayOrNight()
{
	UInt8	textColor, backColor;
	
	textColor = g_prefs.textColor;
	backColor = g_prefs.backColor;

	g_prefs.textColor = g_prefs2.textColor2;
	g_prefs.backColor = g_prefs2.backColor2;
	
	g_prefs2.textColor2 = textColor;
	g_prefs2.backColor2 = backColor;

	if (g_prefs.options & OPDON)
		g_prefs.options ^= OPDON;
	else
		g_prefs.options |= OPDON;
}
