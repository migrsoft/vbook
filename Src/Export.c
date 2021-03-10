#include "Export.h"

Boolean ExportFormHandleEvent(EventPtr eventP)
{
	Boolean	handled = true;
	
	switch (eventP->eType)
	{
	case frmOpenEvent:
		break;
	
	case lstSelectEvent:
		switch (eventP->data.lstSelect.listID)
		{
		}
		break;
	
	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID)
		{
		}
		break;
	}
	
	return handled;
}
