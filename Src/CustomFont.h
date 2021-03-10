#ifndef CUSTOMFONT_H_
#define CUSTOMFONT_H_

Boolean	CfnInit();
void	CfnUpdateRowBytes();
void	CfnSetDepth(UInt32 depth);
void	CfnEnd();

Int16	CfnCharWidth(Char *chars);
Int16	CfnCharHeight();
Int16	CfnCharSize();
Int16	CfnCharsWidth(Char *chars, Int16 len);
Int16	CfnCharsHeight(Char *chars, Int16 len);
void	CfnDrawChars(Char *chars, Int16 len, Coord x, Coord y, UInt8 fColor, UInt8 bColor);
void	CfnDrawTruncChars(Char *chars, Int16 len, Coord x, Coord y, Coord maxWidth, UInt8 fColor, UInt8 bColor);
FontID	CfnGetFont();
void	CfnSetFont(Int8 fontIndex);
void	CfnSetEngFont(UInt16 font);
Int8	CfnGetAvailableFonts();
Char*	CfnGetFontName(Int8 index);

UInt32	CfnFontLoad();

#endif // CUSTOMFONT_H_
