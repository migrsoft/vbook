#ifndef VIEW_H_
#define VIEW_H_

#define CYCLIC_INCREASE(i, bound) ((i+1 == bound) ? 0 : i+1)
#define CYCLIC_DECREASE(i, bound) ((i == 0) ? bound-1 : i-1)
#define MAX(a, b) ((a > b) ? a : b)

Boolean	ViewPrepare();
void	ViewRelease();
void	ViewEnableLink(Boolean linkState);
void	ViewSetOffset(UInt16 chapIdx, UInt32 offset);
void	ViewSetAnchor(UInt16 chapIdx, UInt16 anchor);
void	ViewInit();
void	ViewHistoryInit();
void	ViewSaveHistory();
void	GetLastPosition(UInt16* chapterP, UInt32* offsetP);
void	ViewStopScroll(EventPtr eventP);
UInt16	ViewGetChapterIndex();

Boolean	ViewFormHandleEvent(EventType* eventP);
Boolean	ViewFormHandleKey(EventPtr keyDownEventP);
Boolean	BmkEditFormHandleEvent(EventPtr eventP);
Boolean	EditFormHandleEvent(EventType* eventP);

#endif // VIEW_H_
