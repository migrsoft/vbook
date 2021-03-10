#ifndef BOOKLIST_H_
#define BOOKLIST_H_

Boolean	BookListPrepare();
void	BookListRelease();
void	ResumeLastReading(UInt16, LocalID);
void	SetSaveLastBook(Boolean save);
Boolean	BookListFormHandleEvent(EventType * eventP);
Boolean	BookListFormHandleKey(EventPtr keyDownEventP);
Boolean	CategoryFormHandleEvent(EventType * eventP);

Boolean	BookGetContents(MemPtr* indexPP, MemPtr* titlePP);
void	BookReleaseContents();
Boolean	BookOpenChapter(UInt16 chapIndex);
void	BookCloseChapter();
Boolean	BookGetBlock(UInt16 dataIndex, UInt8* bufP, UInt16* dataLenP, Int16 bufLen, UInt32* endLenP);
Boolean	BookGetDataIndexByOffset(UInt32 offset, UInt16* indexP, UInt16* offsetP);
Boolean	BookGetDataIndexByAnchor(UInt16 anchorIndex, UInt16* indexP, UInt16* offsetP);
UInt16	BookGetChapterNum();
UInt16	BookGetChapterIndex();
UInt32	BookGetChapterLen();
UInt16	BookGetChapterDataNum();
void	BookGetReference(DmOpenRef* memRefP, FileRef* vfsRefP, Boolean* isBookP);
Char*	BookGetPath();
UInt16	GetVolRef(Char *IDStrP);
void	BookListFontChanged();

#endif // BOOKLIST_H_
