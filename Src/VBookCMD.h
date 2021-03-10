/////////////////////////////////////////////////////////////////////////////
// VBook Document Formation
// ========================
// Book information (1Rec)
//   [...]
// Table of Contents (option)
//   Contents index information (1Rec)
//     [ParentID | SelfID | Offset | Chapter | Anchor]
//   Titles data (1Rec)
//     ...
// Chapter index (1Rec)
//   [Info Rec | Offset | Ext-links Num | Int-links Num | Blocks Num | Length]
// Chapter data block
//   Chapter info (1Rec)
//     [chapter | anchor]	-> External links list
//     [offset]				-> Internal links list
//     [ Rec | Offset | Orig Len | Compressed Len | End length] -> Block index
//   Raw data block
//     ...
// Bookmarks (1Rec option)
//   [name | chapter | offset]

#ifndef _VBOOKCMD_H_
#define _VBOOKCMD_H_

// When compile use Microsoft Visual C++
#ifdef _MSC_VER
	#define UINT32	UINT
	#define UINT16	USHORT
	#define UINT8	BYTE
#endif

// When compile use Metrowerks CodeWarrior
#ifdef __MWERKS__
	#define UINT32	UInt32
	#define UINT16	UInt16
	#define UINT8	UInt8
#endif

typedef struct tagPALMDOCINFO {
	UINT16	nVersion;
	UINT16	nReserved1;
	UINT32	nLength;
	UINT16	nBlocksNum;
	UINT16	nSize;
	UINT32	nReserved2;
} PALMDOCINFO;

#define VBOOKCATEGORYLEN		16
#define DATABLOCKLEN			4096
#define MAXINDEXENTRYPERRECORD	4000

typedef struct tagVBOOKINFO {
	UINT16	nType;
	UINT16	nTitleIndexRecNo;
	UINT16	nChapterIndexRecNo;
	UINT16	nChapterIndexNum;
	UINT8	szCategory[VBOOKCATEGORYLEN];
	UINT16	nReserved;
	UINT16	nChapter;
	UINT32	nOffset;
} VBOOKINFO;

typedef struct tagVBOOKTITLEINDEX {
	UINT16	nParentID;
	UINT16	nSelfID;
	UINT16	nOffset;
	UINT16	nChapter;
	UINT16	nAnchor;
} VBOOKTITLEINDEX;

typedef struct tagVBOOKCHAPTERINDEX {
	UINT16	nInfoNo;
	UINT16	nOffset;
	UINT16	nExtLinksNum;
	UINT16	nIntLinksNum;
	UINT16	nBlocksNum;
	UINT16	nReserved; // padding bytes inserted for alignment
	UINT32	nLength;
} VBOOKCHAPTERINDEX;

typedef struct tagVBOOKEXTLINK {
	UINT16	nChapter;
	UINT16	nAnchor;
} VBOOKEXTLINK;

typedef struct tagVBOOKBLOCKINDEX {
	UINT16	nRecNo;
	UINT16	nOffset;
	UINT16	nOrigLen;
	UINT16	nComprLen;
	UINT32	nEndLen;
	UINT16	nReserved1;
	UINT16	nReserved2;
} VBOOKBLOCKINDEX;

#define COMPRESS_NONE	0x1
#define COMPRESS_TEXT	0x2
#define COMPRESS_ZLIB	0x4
#define LINK_ENABLED	0x8
#define BOOKMARK		0x10
#define HISTORY			0x20

#define CMDBEGIN	0x0E
// Command with no parameter.
#define CMDLINKEND	0x0E	// +0
// Command with 1 parameter.
#define CMDFONT		0x0F	// +1	none-zero
#define CMDLINE		0x10	// +1	none-zero
#define CMDWARP		0x11	// +1	none-zero
#define CMDALIGN	0x12	// +1	none-zero
#define CMDINDENT	0x13	// +1
#define CMDITEM		0x14	// +1	Range 1-255 (none-zero)
// Command with 2 parameters.
#define CMDLINK		0x16	// +2
#define CMDEND		0x19

#define FONTSTANDARD	1
#define FONTBOLD		2
#define FONTLARGE		3
#define FONTLARGEBOLD	4

#define ALIGNLEFT		1
#define ALIGNCENTER		2
#define ALIGNRIGHT		3

#define WARPYES			1
#define WARPNO			2

#define LINEBOLD1		1
#define LINEBOLD2		2
#define LINEBOLD3		3
#define LINEUNDERSTART	4
#define LINEUNDEREND	5

#endif // _VBOOKCMD_H_
