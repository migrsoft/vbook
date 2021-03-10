#ifdef WIN32

#include "..\ARMletDLL\SimNative.h"
#include "..\ARMletDLL\PceNativeCall.h"
#include "..\ARMletDLL\endianutils.h"

#else

#include <PceNativeCall.h>
#include <endianutils.h>

#endif // WIN32

#include "DataType.h"


unsigned long
ARMlet_Main(
	const void		*emulStateP,
	void			*userData68KP,
	Call68KFuncType	*call68KFuncP)
{
	DrawCharParamPtr	paramP = (DrawCharParamPtr)userData68KP;
	unsigned char 		*displayP, *dotsP, \
						xOffset, yOffset, textColor;
	char				shift;
	unsigned short		i, x, y, x1, x2, y1, y2, \
						rowBytes, dotsNum, size;

	displayP	= (unsigned char*)ByteSwap32(paramP->displayP);
	dotsP		= (unsigned char*)ByteSwap32(paramP->dotsP);
	textColor	= (unsigned char)ByteSwap16(paramP->color);
	x			= ByteSwap16(paramP->x);
	y			= ByteSwap16(paramP->y);
	x1			= ByteSwap16(paramP->x1);
	x2			= ByteSwap16(paramP->x2);
	y1			= ByteSwap16(paramP->y1);
	y2			= ByteSwap16(paramP->y2);
	rowBytes	= ByteSwap16(paramP->rowBytes);
	dotsNum		= ByteSwap16(paramP->dotsNum);
	size		= ByteSwap16(paramP->size);
	
	shift = 7;
	xOffset = yOffset = 0;
	displayP += (unsigned int)rowBytes * y + x;
	
	for (i = 0; i < dotsNum; i++)
	{
		if ((*dotsP >> shift) & 0x1)
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

	return 0;
}
