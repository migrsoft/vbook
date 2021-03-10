// Virtual Book

#include <CWCallbackThunks.h>

#include "VBook.h"
#include "LCD.h"
#include "CustomFont.h"
#include "BookList.h"
#include "Contents.h"
#include "View.h"
#include "CommonDlg.h"
#include "Bookmark.h"
#include "VDict.h"

//#ifdef PALMOS_30
//#define ourMinVersion	 sysMakeROMVersion(3,0,0,sysROMStageDevelopment,0)
//#else
#define ourMinVersion	sysMakeROMVersion(3,5,0,sysROMStageDevelopment,0)
//#endif

#define kPalmOS10Version sysMakeROMVersion(1,0,0,sysROMStageRelease,0)

VBookPreferenceType		g_prefs;
VBookPreferenceType2	g_prefs2;

Coord		g_LCDWidth, g_LCDHeight;
Boolean		g_expansion;
Boolean		g_haszlib;
UInt		ZLibRef;

static MemHandle fontH;
#ifdef HIGH_DENSITY
static MemHandle englishFontH;
#endif

static Boolean	(*KeyHandler)(EventPtr keyDownEventP);
static Int32	timeout = evtWaitForever;

////////////////////////////////////////////////////////////
// Internal Functions

static void CheckExpansion()
{
	Err		err;
	UInt32	expMgrVersion, vfsMgrVersion;

	// Check Expansion Manager presence.
	err = FtrGet(sysFileCExpansionMgr, expFtrIDVersion, &expMgrVersion);
	if (err)
	{
		g_expansion = false;
	}
	else
	{
		// Check VFS Manager presence.
		err = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &vfsMgrVersion);
		g_expansion = (err) ? false : true;
	}
}

static Boolean AppHandleEvent(EventType* eventP)
{
	UInt16		formId;
	FormPtr		frmP;

	if (eventP->eType == frmLoadEvent)
	{
		formId = eventP->data.frmLoad.formID;
		frmP = FrmInitForm(formId);
		FrmSetActiveForm(frmP);

		switch (formId)
		{
		case MainForm:
			FrmSetEventHandler(frmP, BookListFormHandleEvent);
			break;
		
		case CategoriesForm:
			FrmSetEventHandler(frmP, CategoryFormHandleEvent);
			break;
		
		case ContentsForm:
			FrmSetEventHandler(frmP, ContentsFormHandleEvent);
			break;

		case ViewForm:
			FrmSetEventHandler(frmP, ViewFormHandleEvent);
			break;
		
		case BmkEditForm:
			FrmSetEventHandler(frmP, BmkEditFormHandleEvent);
			break;
		
		case AboutForm:
			FrmSetEventHandler(frmP, AboutFormHandleEvent);
			break;
		
		case EditForm:
			FrmSetEventHandler(frmP, EditFormHandleEvent);

			break;
		
		case DisplayForm:
			FrmSetEventHandler(frmP, DisplayFormHandleEvent);
			break;
		
		case ButtonsForm:
			FrmSetEventHandler(frmP, ButtonsFormHandleEvent);
			break;
		
		case TapForm:
			FrmSetEventHandler(frmP, TapFormHandleEvent);
			break;
		
/*		case VDictForm:
			FrmSetEventHandler(frmP, VDictFormHandleEvent);
			break;*/
		}
		return true;
	}

	return false;
}

static void AppEventLoop(void)
{
	UInt16		err;
	EventType	event;

	do {
		EvtGetEvent(&event, timeout);

		if (timeout != evtWaitForever && event.eType == penDownEvent)
			ViewStopScroll(&event);

		// Preprocess event before system handles them.
		switch (event.eType)
		{
		case keyDownEvent:
			switch (FrmGetFormId(FrmGetActiveForm()))
			{
			case MainForm:
				KeyHandler = BookListFormHandleKey;
				break;
			
			case ContentsForm:
				KeyHandler = ContentsFormHandleKey;
				break;
			
			case ViewForm:
				KeyHandler = ViewFormHandleKey;
				break;
			
/*			case VDictForm:
				if ((event.data.keyDown.chr == 0x8 || // Backspace
					(event.data.keyDown.chr >= 0x20 && event.data.keyDown.chr < 0xff)))
				{
					EventType e;
					e.eType = dicSearchEvent;
					EvtAddEventToQueue (&e);
				}*/

			default:
				KeyHandler = NULL;
			}
			
			if (KeyHandler)
			{
				// We must process these keys before system handles them.
				switch (event.data.keyDown.chr)
				{
				case vchrHard1:
				case vchrHard2:
				case vchrHard3:
				case vchrHard4:
				case vchrJogUp:
				case vchrJogDown:
				case vchrJogRelease:
				case vchrCalc:
				case vchrFind:
					if (KeyHandler(&event))
						continue;
					break;
				}
			}
		}

		if (!SysHandleEvent(&event))
			if (!MenuHandleEvent(0, &event, &err))
				if (!AppHandleEvent(&event))
					FrmDispatchEvent(&event);
		
		if (event.eType == appStopEvent)
			if (FrmGetActiveFormID() == ViewForm)
				SetSaveLastBook(true);

	} while (event.eType != appStopEvent);
}

static void PrefsInit()
{
	UInt32			days;
	DateType		date;
	DateTimeType	expire;
	
	StrCopy(g_prefs.path, "//");
	StrCopy(g_prefs.pdbname, "\x1");
	
	g_prefs.category[0]	= 0;
	g_prefs.listFont	= stdFont;
	g_prefs.order		= 0;
	g_prefs.sclOpts		= SCLOPGUIDE | SCLOPPAGE;
	g_prefs.timeOut		= 4;
	g_prefs.options		=	OPCHSONLY | OPMINIBAR | OPTOOLBAR | OPGWCOPY | OPDON | \
							OPSHOWPDOC | OPSHOWTEXT;
	g_prefs.textColor	= 203;
	g_prefs.backColor	= 6;
	
	#ifdef HIGH_DENSITY
	g_prefs.lineSpace	= 0;
	#else
	g_prefs.lineSpace	= 4;
	#endif

	#ifdef PALMOS_30
	g_prefs.depth = 1;
	#else
	LCDWinScreenMode(
		winScreenModeGetSupportedDepths,
		NULL, NULL, &g_prefs.depth, NULL);
	g_prefs.depth = (g_prefs.depth & 0x80) ? 8 : 1;
	#endif

	ButtonsDefault(MainForm, &g_prefs.uiBooks);
	ButtonsDefault(ContentsForm, &g_prefs.uiTOC);
	ButtonsDefault(ViewForm, &g_prefs.uiView);
	TapDefault(&g_prefs.tap);
	
	TimSecondsToDateTime(TimGetSeconds(), &expire);
	date.year	= expire.year - 1904;
	date.month	= expire.month;
	date.day	= expire.day;
	days = DateToDays(date) + TRIAL_TERM;
	DateDaysToDate(days, &date);
	expire.year		= date.year + 1904;
	expire.month	= date.month;
	expire.day 		= date.day;
	expire.hour		= 0;
	expire.minute	= 0;
	expire.second	= 0;
	g_prefs.expired	= TimDateTimeToSeconds(&expire);
	
	g_prefs.fake	= 0;
	
	// 默认为已注册，2010年11月起改为免费软件
	g_prefs.options |= OPREGISTERED;
	g_prefs.fake = 1977;
}

static void Prefs2Init()
{
	g_prefs2.leftMargin		= 0;
	g_prefs2.rightMargin	= 0;
	g_prefs2.wordSpace		= 0;
	
	g_prefs2.sysFont		= stdFont;
	g_prefs2.chsFont		= 0;
	g_prefs2.engFont		= 0;
	
	g_prefs2.daName[0]		= 0;
	
	g_prefs2.textColor2		= 206;
	g_prefs2.backColor2		= 230;
}

static Err AppStart(void)
{
	UInt16	prefsSize;
	UInt32	w, h;

	CheckExpansion();
	LCDInit();
	CfnInit();

	#ifdef USE_SONY_SILKSCREEN
	LCDEnableResize();
	#endif

	// Clock font.
#ifdef PALMOS_50
	fontH = DmGetResource('nfnt', 1000);
#else

#ifdef USE_SONY_HIRES
	fontH = DmGetResource('NFNT', 1001);
#else
	fontH = DmGetResource('NFNT', 1000);
#endif // USE_SONY_HIRES

#endif // PALMOS_50

	FntDefineFont(numberFont, (FontPtr)MemHandleLock(fontH));

	// Verdana english font.
#ifdef HIGH_DENSITY

#ifdef USE_SONY_HIRES // Sony HiRes
	englishFontH = DmGetResource('NFNT', 2000);
#endif

#ifdef PALMOS_50 // OS5 High density
	englishFontH = DmGetResource('nfnt', 2000);
#endif

	FntDefineFont(englishFont, (FontPtr)MemHandleLock(englishFontH));

#endif // HIGH_DENSITY

	if (SysLibFind("Z.lib", &ZLibRef) == sysErrLibNotFound)
		g_haszlib = (SysLibLoad('libr', 'ZLib', &ZLibRef) == errNone) ? true : false;

	if (g_haszlib)
		ZLibOpen(ZLibRef);

	// Loading preferences.
	prefsSize = sizeof(VBookPreferenceType);
	if (PrefGetAppPreferences(
		appFileCreator, appPrefID, &g_prefs, &prefsSize, true) != 
		noPreferenceFound)
	{
		if (prefsSize != sizeof(VBookPreferenceType))
			PrefsInit();
		else
		{
			// Verify saved prefs.
			if (CfnGetAvailableFonts() - 4 == 0)
			{
				g_prefs.options |= OPUSEADDFONT;
				g_prefs.options ^= OPUSEADDFONT;
			}
			else if (g_prefs2.chsFont >= CfnGetAvailableFonts() - 4)
			{
				g_prefs2.chsFont = 0;
			}
		}
	}
	else
		PrefsInit();
	
	prefsSize = sizeof(VBookPreferenceType2);
	if (PrefGetAppPreferences(
		appFileCreator, appPrefID2, &g_prefs2, &prefsSize, true) !=
		noPreferenceFound)
	{
		if (prefsSize != sizeof(VBookPreferenceType2))
			Prefs2Init();
	}
	else
		Prefs2Init();
	
	//////////
	
	LCDWinGetDisplayExtent(&g_LCDWidth, &g_LCDHeight);
	#ifdef HIGH_DENSITY
	g_LCDWidth <<= 1;
	g_LCDHeight <<= 1;
	#endif
	
	w = g_LCDWidth;
	h = g_LCDHeight;

	LCDWinScreenMode(
		winScreenModeSet,
		&w, &h, &g_prefs.depth, NULL);

	CfnSetDepth(g_prefs.depth);

	#ifndef PALMOS_50
	if (g_prefs.depth > 1 && g_prefs.options & OPOS5STYLE)
		NewStyle();
	#endif

/*
	if (g_prefs.fake == 0)
	{
		g_prefs.fake = 3;
		g_prefs.options |= OPREGISTERED;
		FrmAlert(WelcomeAlert);
	}
	else if (!REGISTERED)
	{
		g_prefs.options |= OPREGISTERED;
		if (TimGetSeconds() > g_prefs.expired)
		{
			if (g_prefs.fake == 3)
			{
				g_prefs.fake = 11;
				FrmAlert(ExpireAlert);
			}
			g_prefs.options ^= OPREGISTERED;
		}
	}
*/
	
	BookListPrepare();
	PrefOpenDB();
	ContentsPrepare();
	ViewPrepare();
//	VDictPrepare();

	return errNone;
}

static void AppStop(void)
{
	FrmCloseAllForms();

	#ifdef PALMOS_50
	#endif // PALMOS_50

	ViewRelease();
	ContentsRelease();
	BookListRelease();
	PrefCloseDB();
//	VDictRelease();
	
	if (g_prefs.fake != 1977)
	{
		g_prefs.options |= OPREGISTERED;
		g_prefs.options ^= OPREGISTERED;
	}

	// Saving preferences.
	PrefSetAppPreferences(
		appFileCreator, appPrefID, appPrefVersion,
		&g_prefs, sizeof(VBookPreferenceType), true);
	
	PrefSetAppPreferences(
		appFileCreator, appPrefID2, appPrefVersion2,
		&g_prefs2, sizeof(VBookPreferenceType2), true);
		
	LCDWinScreenMode(winScreenModeSetToDefaults, NULL, NULL, NULL, NULL);

	MemHandleUnlock(fontH);
	DmReleaseResource(fontH);

	#ifdef HIGH_DENSITY
	MemHandleUnlock(englishFontH);
	DmReleaseResource(englishFontH);
	#endif // HIGH_DENSITY

	CfnEnd();
	LCDEnd();
	if (g_haszlib) ZLTeardown;
}

void SetTimer(Boolean start)
{
	if (start)
		timeout = SysTicksPerSecond() / 2;
	else
		timeout = evtWaitForever;
}

/*
#ifdef USE_PALM_SILKSCREEN

Err DisplayResizedEventCallback(SysNotifyParamType *notifyParamsP)
{
	EventType	eventToAdd;
	
	MemSet(&eventToAdd, sizeof(EventType), 0);
	eventToAdd.eType = (eventsEnum)winDisplayChangedEvent;
	EvtAddUniqueEventToQueue(&eventToAdd, 0, true);
	
	return 0;
}

#endif // USE_PALM_SILKSCREEN
*/


#pragma mark === Main Entry ===
// all code from here to end of file should use no global variables.
#pragma warn_a5_access on

static Err RomVersionCompatible(UInt32 requiredVersion, UInt16 launchFlags)
{
	UInt32	romVersion;

	// See if we're on in minimum required version of the ROM or later.
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
	if (romVersion < requiredVersion)
	{
		if ((launchFlags & 
			(sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp)) ==
			(sysAppLaunchFlagNewGlobals | sysAppLaunchFlagUIApp))
		{
//			#ifdef PALMOS_30
//			FrmCustomAlert(RomIncompatibleAlert, "3.0", "", "");
//			#else
			FrmCustomAlert(RomIncompatibleAlert, "3.5", "", "");
//			#endif

			// Palm OS 1.0 will continuously relaunch this app unless 
			// we switch to another safe one.
			if (romVersion <= kPalmOS10Version)
			{
				AppLaunchWithCommand(
					sysFileCDefaultApp, 
					sysAppLaunchCmdNormalLaunch, NULL);
			}
		}

		return sysErrRomIncompatible;
	}

	return errNone;
}

static UInt32 VBookPalmMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	Err							err;
	SysAppLaunchCmdOpenDBType	*openP;
	UInt16						cardNo;
	LocalID						dbID;
	EventType					event;
	SysNotifyParamType			*paramP;
	
	#ifdef USE_PALM_SILKSCREEN

	UInt32						version;

	#endif // USE_PALM_SILKSCREEN


	err = RomVersionCompatible(ourMinVersion, launchFlags);
	if (err) return (err);

	switch (cmd)
	{
	case sysAppLaunchCmdOpenDB:
	case sysAppLaunchCmdNormalLaunch:

		#ifdef USE_PALM_SILKSCREEN
		err = FtrGet(pinCreator, pinFtrAPIVersion, &version);
		if (err) return err; // PalmSource PinMgr not exists.
		#endif // USE_PALM_SILKSCREEN


		err = SysCurAppDatabase(&cardNo, &dbID);
		if (err) return err;


		#ifdef USE_SONY_SILKSCREEN
		err = SysNotifyRegister(
			cardNo, dbID,
			sysNotifyDisplayChangeEvent, NULL,
			sysNotifyNormalPriority, NULL);
		#endif // USE_SONY_SILKSCREEN

		#ifdef USE_PALM_SILKSCREEN
		err = SysNotifyRegister(
			cardNo, dbID,
//			sysNotifyDisplayResizedEvent, DisplayResizedEventCallback,
			sysNotifyDisplayResizedEvent, NULL,
			sysNotifyNormalPriority, NULL);
		#endif // USE_PALM_SILKSCREEN


		err = AppStart();
		if (err) return err;
		
		
		SysNotifyRegister(
			cardNo, dbID,
			sysNotifyVolumeMountedEvent, NULL,
			sysNotifyNormalPriority, NULL);
		
		SysNotifyRegister(
			cardNo, dbID,
			sysNotifyVolumeUnmountedEvent, NULL,
			sysNotifyNormalPriority, NULL);


		if (cmd == sysAppLaunchCmdOpenDB)
		{
			openP = (SysAppLaunchCmdOpenDBType*)cmdPBP;
			ResumeLastReading(openP->cardNo, openP->dbID);
		}
		else
			ResumeLastReading(0, 0);

		FrmGotoForm(MainForm);
		AppEventLoop();
		
		
		SysNotifyUnregister(
			cardNo, dbID,
			sysNotifyVolumeMountedEvent, sysNotifyNormalPriority);
		
		SysNotifyUnregister(
			cardNo, dbID,
			sysNotifyVolumeUnmountedEvent, sysNotifyNormalPriority);

		#ifdef USE_SONY_SILKSCREEN
		SysNotifyUnregister(
			cardNo, dbID,
			sysNotifyDisplayChangeEvent, sysNotifyNormalPriority);
		#endif // USE_SONY_SILKSCREEN

		#ifdef USE_PALM_SILKSCREEN
		SysNotifyUnregister(
			cardNo, dbID,
			sysNotifyDisplayResizedEvent, sysNotifyNormalPriority);
		#endif // USE_PALM_SILKSCREEN

		AppStop();
		break;
	
	case sysAppLaunchCmdNotify:
		paramP = (SysNotifyParamType*)cmdPBP;
		MemSet(&event, sizeof(EventType), 0);

		switch (paramP->notifyType)
		{
		case sysNotifyVolumeMountedEvent:
		case sysNotifyVolumeUnmountedEvent:
			event.eType = appStopEvent;
			EvtAddUniqueEventToQueue(&event, 0, true);
			break;
	
	#ifdef USE_PALM_SILKSCREEN
		case sysNotifyDisplayResizedEvent:
			event.eType = winDisplayChangedEvent;
			EvtAddUniqueEventToQueue(&event, 0, true);
			break;
	#endif // USE_PALM_SILKSCREEN
		
	#ifdef USE_SONY_SILKSCREEN
		case sysNotifyDisplayChangeEvent:
			event.eType = dispChangeEvent;
			EvtAddUniqueEventToQueue(&event,0, true);
			break;
	#endif // USE_SONY_SILKSCREEN
		}
		break;

	default:
		break;
	}

	return errNone;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	return VBookPalmMain(cmd, cmdPBP, launchFlags);
}

// turn a5 warning off to prevent it being set off by C++
// static initializer code generation
#pragma warn_a5_access reset
