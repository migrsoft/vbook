#ifndef VBOOK_H_
#define VBOOK_H_

////////////////////////////////////////////////////////////

#include <PalmOS.h>
#include <SonyCLIE.h>
#include <DLServer.h>
#include <palmOneNavigator.h>
#include <PalmOSGlue.h>

#include "Platform.h"

#include "VBookRsc.h"
#include "VBookCMD.h"

#define NOZLIBDEFS
#include "SysZLib.h"

////////////////////////////////////////////////////////////
// Internal Structures

#define MAX_BUTTONS		16

typedef struct
{
	UInt8	buttons[MAX_BUTTONS];
} ButtonsType, *ButtonsPtr;

typedef struct
{
	UInt8	areas[4];
} AreaType, *AreaPtr;

typedef struct VBookPreferenceType
{
	Char		path[256];
	Char		pdbname[dmDBNameLength];
	Char		category[dmCategoryLength];

	UInt16		listFont;
	UInt16		reserved1;
	UInt16		reserved2;

	UInt16		order;
	Int16		lineSpace;
	UInt8		sclOpts;
	Int8		timeOut;
	UInt16		options;
	UInt32		depth;

	UInt8		textColor;
	UInt8		backColor;

	ButtonsType	uiBooks;
	ButtonsType	uiTOC;
	ButtonsType	uiView;
	AreaType	tap;

	UInt32		expired;
	UInt16		fake;
} VBookPreferenceType;

typedef struct {
	Int8		leftMargin;
	Int8		rightMargin;
	Int8		wordSpace;
	UInt8		indent;

	UInt16		sysFont;
	UInt16		chsFont;
	UInt16		engFont;
	
	Char		daName[dmDBNameLength];
	
	UInt8		textColor2;
	UInt8		backColor2;
} VBookPreferenceType2;

void SetTimer(Boolean);

////////////////////////////////////////////////////////////
// Global variables

extern VBookPreferenceType	g_prefs;
extern VBookPreferenceType2	g_prefs2;
extern Coord				g_LCDWidth;
extern Coord				g_LCDHeight;
extern Boolean				g_expansion;
extern Boolean				g_haszlib;

extern UInt					ZLibRef;

////////////////////////////////////////////////////////////
// Internal Constants

#define appFileCreator		'VBuK'
#define appName				"VBook"
#define appVersionNum		0x01

#define appPrefID			0x00
#define appPrefVersion		0x01

#define appPrefID2			0x01
#define appPrefVersion2		0x02

#define appVDictPrefID		0x02
#define appVDictPrefVersion	0x03

#define vbookMaxLabelLen	32

#define viewUpdateEvent		(firstUserEvent)
#define dispChangeEvent		(firstUserEvent + 1)
#define	listRefreshEvent	(firstUserEvent + 2)
#define dicSearchEvent		(firstUserEvent + 3)


#define VBookType		'Book'
#define VBookCreator	'BkMk'
#define DocType			'TEXt'
#define DocCreator		'REAd'
#define	HSCreator		'hsIB'
#define TxtType			'Pure'
#define TxtCreator		'Text'

#define SCLOPGUIDE		0x1
#define SCLOPPAGE		0x2
#define SCLOPRTA		0x4

#define OPUSEADDFONT	0x1
#define OPCHSONLY		0x2
#define OPTOOLBAR		0x4
#define OPSHOWSIZE		0x8
#define OPSHOWPDOC		0x10
#define OPREGISTERED	0x20
#define OPSHOWTEXT		0x40
#define OPOS5STYLE		0x80
#define OPINVERT		0x100
#define OPMINIBAR		0x200

#define OPGWCOPY		0x400
#define OPGWROADLINGUA	0x800
#define OPGWEXTDA		0x1000
#define OPGWVDICT		0x2000

#define OPDON			0x4000	// Day or Night color theme.

#define OPSHOWFNAME		0x8000	// Show file name.

// Common actions define.

#define ACTNONE		0

#define	ACTLINEUP	1
#define ACTLINEDOWN	2
#define ACTPAGEUP	3
#define ACTPAGEDOWN	4

#define ACTSELECT	5
#define ACTRETURN	6

#define ACTCLOSE	7
#define ACTTOC		8
#define ACTPREV		9
#define	ACTHOME		10
#define ACTNEXT		11
#define ACTPLAY		12
#define ACTFIND		13
#define ACTGOTO		14
#define ACTGETWORD	15

#define ACTUI		16

#define ACTPLAYSPEEDUP		20
#define ACTPLAYSPEEDDOWN	21
#define ACTPLAYPAUSE		22


#define ITEM_SPACE	2


#define REGISTERED (g_prefs.options & OPREGISTERED)

#define numberFont		fntAppFontCustomBase
#define englishFont		(fntAppFontCustomBase + 1)
//#define phoneticFont	(fntAppFontCustomBase + 2)


#endif // VBOOK_H_
