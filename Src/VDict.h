#ifndef VDICT_H_
#define VDICT_H_

void VDictPrepare();
void VDictRelease();
Boolean VDictFormHandleEvent(EventType* eventP);
void VDictSetWordToFind(Char *wordP, Int16 len);

#endif
