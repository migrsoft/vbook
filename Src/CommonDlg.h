#ifndef COMMONDLG_H_
#define COMMONDLG_H_

#include "VBook.h"

void	ButtonsDefault(UInt16, ButtonsPtr);
void	TapDefault(AreaPtr);

void	NewStyle();
void	CmnDlgSetReturnForm(UInt16 id);
void	RuntimeInfo();
Boolean	MyFontSelect();
Boolean	AboutFormHandleEvent(EventType* eventP);
Boolean	DisplayFormHandleEvent(EventType* eventP);
Boolean	ButtonsFormHandleEvent(EventType* eventP);
Boolean	ResizeScreen();
Boolean	TapFormHandleEvent(EventPtr eventP);
void	DayOrNight();

#endif // COMMONDLG_H_
