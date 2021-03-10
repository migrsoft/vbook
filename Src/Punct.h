#ifndef PUNCT_H_
#define PUNCT_H_

UInt8 engPunctBegin[] = {
	'"',
	'(',	'{',	'[',
	0
};

UInt8 engPunctEnd[] = {
	',',	':',	';',	'.',	'!',	'?',	'\'',
	')',	'}',	']',
	0
};

UInt16 chsPunctBegin[] = {
	'¡°',	'¡®',
	'¡¶',	'£¨',	'¡´',	'¡²',
	'¡¸',	'¡º',	'¡¾',	'¡¼',	'£Û',
	0
};

UInt16 chsPunctEnd[] = {
	'£¬',	'¡£',	'¡¢',	'£¡',	'£¿',
	'¡¯',	'¡±',	'¡ã',
	'¡·',	'£©',	'¡µ',	'¡³',
	'¡¹',	'¡»',	'¡¿',	'¡½',	'£Ý',
	'£®',	'£º',	'£»',
	0
};

UInt16 chsPunctTwin[] = {
	'¡ª',	'¡­',
	0
};


typedef enum {
	WC_ENG_WORD,
	WC_CHS_WORD,
	WC_NUMBER,
	WC_PUNCT_BEGIN,
	WC_PUNCT_END,
	WC_PUNCT_TWIN,
	WC_MISC
} WordClass;

#endif
