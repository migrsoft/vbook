#include "VBook.h"
#include "LCD.h"

#ifdef USE_SONY_HIRES
static UInt16	l_hiResRef = 0;
#endif // USE_SONY_HIRES

#ifdef USE_SONY_SILKSCREEN
static UInt16	l_silkRef = 0;
#endif // USE_SONY_SILKSCREEN

// ********************************************************************
// Startup / Shutdown API
// ********************************************************************
#pragma mark === Startup/Shutdown API ===

Boolean LCDInit()
{
	Boolean		result = true;
	Err			error = errNone;
	UInt32		vskVersion;
	
	#ifdef USE_SONY_HIRES

	error = SysLibFind(sonySysLibNameHR, &l_hiResRef);
	if (error == sysErrLibNotFound)
		SysLibLoad('libr', sonySysFileCHRLib, &l_hiResRef);
	error = HROpen(l_hiResRef);
	result = (error == errNone) ? true : false;

	#endif // USE_SONY_HIRES


	#ifdef USE_SONY_SILKSCREEN

	error = SysLibFind(sonySysLibNameSilk, &l_silkRef);
	if (error == sysErrLibNotFound)
		error = SysLibLoad('libr', sonySysFileCSilkLib, &l_silkRef);

	if (!error)
	{
		error = FtrGet(sonySysFtrCreator, sonySysFtrNumVskVersion, &vskVersion);
		if (error)
		{
			/* Version 1 is installed
			only resize is available */
			if (SilkLibOpen(l_silkRef) == errNone)
				SilkLibEnableResize(l_silkRef);
		}
		else if (vskVersion == vskVersionNum2)
		{
			/* Version 2 is installed */
			if (VskOpen(l_silkRef) == errNone)
				VskSetState(l_silkRef, vskStateEnable, vskResizeVertically);
		}
		else
		{
			/* Version 3 or up is installed
			Horizontal screen is available */
			if (VskOpen(l_silkRef) == errNone)
				VskSetState(l_silkRef, vskStateEnable, vskResizeHorizontally);
		}
	}

	result = (error == errNone) ? true : false;

	#endif // USE_SONY_SILKSCREEN

	return result;
}

void LCDEnd()
{
	#ifdef USE_SONY_HIRES
	HRClose(l_hiResRef);
	#endif // USE_SONY_HIRES

	#ifdef USE_SONY_SILKSCREEN
	SilkLibClose(l_silkRef);
	#endif // USE_SONY_SILKSCREEN
}


// ********************************************************************
// Window API
// ********************************************************************
#pragma mark === Window API ===

void LCDWinCopyRectangle(WinHandle srcWin, WinHandle dstWin,
	RectangleType *srcRect, Coord destX, Coord destY, WinDrawOperation mode)
{
	#ifdef USE_SONY_HIRES
	HRWinCopyRectangle(l_hiResRef, srcWin, dstWin, srcRect, destX, destY, mode);
	#else
	WinCopyRectangle(srcWin, dstWin, srcRect, destX, destY, mode);
	#endif
}

WinHandle LCDWinCreateOffscreenWindow(Coord width, Coord height,
	WindowFormatType format, UInt16 *error)
{
	#ifdef USE_SONY_HIRES
	return HRWinCreateOffscreenWindow(l_hiResRef, width, height, format, error);
	#else
	return WinCreateOffscreenWindow(width, height, format, error);
	#endif
}

void LCDWinDrawBitmap(BitmapPtr bitmapP, Coord x, Coord y)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawBitmap(l_hiResRef, bitmapP, x, y);
	#else
	WinDrawBitmap(bitmapP, x, y);
	#endif
}

void LCDWinDrawChar(Char theChar, Coord x, Coord y)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawChar(l_hiResRef, theChar, x, y);
	#else
	WinDrawChar(theChar, x, y);
	#endif
}

void LCDWinDrawChars(Char *chars, Int16 len, Coord x, Coord y)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawChars(l_hiResRef, chars, len, x, y);
	#else
	WinDrawChars(chars, len, x, y);
	#endif
}

void LCDWinDrawPixel(Coord x, Coord y)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawPixel(l_hiResRef, x, y);
	#else
	WinDrawPixel(x, y);
	#endif
}

void LCDWinDrawRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawRectangle(l_hiResRef, rP, cornerDiam);
	#else
	WinDrawRectangle(rP, cornerDiam);
	#endif
}

void LCDWinDrawRectangleFrame(FrameType frame, RectangleType *rP)
{
	#ifdef USE_SONY_HIRES
	HRWinDrawRectangleFrame(l_hiResRef, frame, rP);
	#else
	WinDrawRectangleFrame(frame, rP);
	#endif
}

void LCDWinDrawTruncChars(Char *chars, Int16 len, Coord x, Coord y, Coord maxWidth)
{
	#ifdef PALMOS_30
	UInt8	*p;
	Int16	step, slen, width, totalWidth;
	
	p = (UInt8*)chars;
	slen = totalWidth = 0;
	while (*p)
	{
		step = (*p > 0x80) ? 2 : 1;
		width = FntCharsWidth((Char*)p, step);
		if (totalWidth + width > maxWidth)
			break;
		
		totalWidth += width;
		slen += step;
		p += step;
	}
	WinDrawChars(chars, slen, x, y);
	
	#else
		#ifdef USE_SONY_HIRES
		HRWinDrawTruncChars(l_hiResRef, chars, len, x, y, maxWidth);
		#else
		WinDrawTruncChars(chars, len, x, y, maxWidth);
		#endif
	#endif
}

void LCDWinEraseRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	#ifdef USE_SONY_HIRES
	HRWinEraseRectangle(l_hiResRef, rP, cornerDiam);
	#else
	WinEraseRectangle(rP, cornerDiam);
	#endif
}

void LCDWinGetClip(RectangleType *rP)
{
	#ifdef USE_SONY_HIRES
	HRWinGetClip(l_hiResRef, rP);
	#else
	WinGetClip(rP);
	#endif
}

void LCDWinGetDisplayExtent(Coord *extentX, Coord *extentY)
{
	#ifdef USE_SONY_HIRES
	HRWinGetDisplayExtent(l_hiResRef, extentX, extentY);
	#else
	WinGetDisplayExtent(extentX, extentY);
	#endif
}

void LCDWinInvertLine(Coord x1, Coord y1, Coord x2, Coord y2)
{
	#ifdef USE_SONY_HIRES
	HRWinInvertLine(l_hiResRef, x1, y1, x2, y2);
	#else
	WinInvertLine(x1, y1, x2, y2);
	#endif
}

void LCDWinPaintPixel(Coord x, Coord y)
{
	#ifdef USE_SONY_HIRES
	HRWinPaintPixel(l_hiResRef, x, y);
	#else
	WinPaintPixel(x, y);
	#endif
}

void LCDWinPaintLine(Coord x1, Coord y1, Coord x2, Coord y2)
{
	#ifdef USE_SONY_HIRES
	HRWinPaintLine(l_hiResRef, x1, y1, x2, y2);
	#else
	WinPaintLine(x1, y1, x2, y2);
	#endif
}

void LCDWinPaintRectangle(RectangleType *rP, UInt16 cornerDiam)
{
	#ifdef USE_SONY_HIRES
	HRWinPaintRectangle(l_hiResRef, rP, cornerDiam);
	#else
	WinPaintRectangle(rP, cornerDiam);
	#endif
}

Err LCDWinScreenMode(WinScreenModeOperation operation,
	UInt32 *widthP, UInt32 *heightP, UInt32 *depthP, Boolean *enableColorP)
{
	#ifdef USE_SONY_HIRES
	return HRWinScreenMode(l_hiResRef, operation, widthP, heightP, depthP, enableColorP);
	#else
	return WinScreenMode(operation, widthP, heightP, depthP, enableColorP);
	#endif
}

void LCDWinScrollRectangle(RectangleType *rP,
	WinDirectionType direction, Coord distance, RectangleType *vacatedP)
{
	#ifdef USE_SONY_HIRES
	HRWinScrollRectangle(l_hiResRef, rP, direction, distance, vacatedP);
	#else
	WinScrollRectangle(rP, direction, distance, vacatedP);
	#endif
}

void LCDWinSetClip(RectangleType *rP)
{
	#ifdef USE_SONY_HIRES
	HRWinSetClip(l_hiResRef, rP);
	#else
	WinSetClip(rP);
	#endif
}

// ********************************************************************
// Font API
// ********************************************************************
#pragma mark === Font API ===

FontID LCDFntGetFont(void)
{
	#ifdef USE_SONY_HIRES
	return (HRFntGetFont(l_hiResRef) - 8);
	#else
	return FntGetFont();
	#endif
}

FontID LCDFntSetFont(FontID font)
{
	#ifdef USE_SONY_HIRES
	if (font < fntAppFontCustomBase)
		return HRFntSetFont(l_hiResRef, font + 8);
	else
		return HRFntSetFont(l_hiResRef, font);
	#else
	return FntSetFont(font);
	#endif
}

// ********************************************************************
// Bitmap API
// ********************************************************************
#pragma mark === Bitmap API ===

#ifdef USE_SONY_HIRES

BitmapType* LCDBmpCreate(
	Coord width, Coord height, UInt8 depth,
	ColorTableType *colortableP, UInt16 *error)
{
	return HRBmpCreate(l_hiResRef, width, height, depth, colortableP, error);
}

#endif

// ********************************************************************
// SilkScreen API
// ********************************************************************
#pragma mark === SilkScreen API ===

#ifdef USE_SONY_SILKSCREEN

Err LCDEnableResize()
{
	return SilkLibEnableResize(l_silkRef);
}

Err LCDDisableResize()
{
	return SilkLibDisableResize(l_silkRef);
}

Err LCDResizeDispWin(UInt8 pos)
{
	return SilkLibResizeDispWin(l_silkRef, pos);
}

#endif // USE_SONY_SILKSCREEN
