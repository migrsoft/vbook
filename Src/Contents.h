#ifndef CONTENTS_H_
#define CONTENTS_H_

Boolean	ContentsPrepare();
void	ContentsRelease();
void	ContentsRefresh();
void	ContentsSetReturnForm(UInt16 formId);
Boolean	ContentsFormHandleEvent(EventType* eventP);
Boolean	ContentsFormHandleKey(EventPtr keyDownEventP);
void	ContentsFontChanged();

#endif // CONTENTS_H_
