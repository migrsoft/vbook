#ifndef LCD_H_
#define LCD_H_

#include "VBook.h"

Boolean		LCDInit();
void		LCDEnd();

void		LCDWinCopyRectangle(WinHandle srcWin, WinHandle dstWin,
				RectangleType *srcRect, Coord destX, Coord destY, WinDrawOperation mode);
WinHandle	LCDWinCreateOffscreenWindow(Coord width, Coord height,
				WindowFormatType format, UInt16 *error);
void		LCDWinDrawBitmap(BitmapPtr bitmapP, Coord x, Coord y);
void		LCDWinDrawChar(Char theChar, Coord x, Coord y);
void		LCDWinDrawChars(Char *chars, Int16 len, Coord x, Coord y);
void		LCDWinDrawPixel(Coord x, Coord y);
void		LCDWinDrawRectangle(RectangleType *rP, UInt16 cornerDiam);
void		LCDWinDrawRectangleFrame(FrameType frame, RectangleType *rP);
void		LCDWinDrawTruncChars(Char *chars, Int16 len, Coord x, Coord y, Coord maxWidth);
void		LCDWinEraseRectangle(RectangleType *rP, UInt16 cornerDiam);
void		LCDWinGetClip(RectangleType *rP);
void		LCDWinGetDisplayExtent(Coord *extentX, Coord *extentY);
void		LCDWinInvertLine(Coord x1, Coord y1, Coord x2, Coord y2);
void		LCDWinPaintPixel(Coord x, Coord y);
void		LCDWinPaintLine(Coord x1, Coord y1, Coord x2, Coord y2);
void		LCDWinPaintRectangle(RectangleType *rP, UInt16 cornerDiam);
Err			LCDWinScreenMode(WinScreenModeOperation operation,
				UInt32 *widthP, UInt32 *heightP, UInt32 *depthP, Boolean *enableColorP);
void		LCDWinScrollRectangle(RectangleType *rP,
				WinDirectionType direction, Coord distance, RectangleType *vacatedP);
void		LCDWinSetClip(RectangleType *rP);

FontID		LCDFntGetFont(void);
FontID		LCDFntSetFont(FontID font);

#ifdef USE_SONY_HIRES

BitmapType*	LCDBmpCreate(Coord width, Coord height, UInt8 depth,
				ColorTableType *colortableP, UInt16 *error);

#endif

#ifdef USE_SONY_SILKSCREEN

Err			LCDEnableResize();
Err			LCDDisableResize();
Err			LCDResizeDispWin(UInt8 pos);

#endif // USE_SONY_SILKSCREEN

#endif // LCD_H_
