#ifndef __DATATYPE_H__
#define __DATATYPE_H__

typedef struct {

	unsigned char	*dotsP;
	
	unsigned short	dotsNum;
	unsigned short	size;
	
	unsigned char	*displayP;
	
	unsigned short	x;
	unsigned short	y;
	
	unsigned short	x1;
	unsigned short	x2;
	
	unsigned short	y1;
	unsigned short	y2;
	
	unsigned short	color;
	unsigned short	rowBytes;
	
} DrawCharParamType, *DrawCharParamPtr;

#endif // __DATATYPE_H__
