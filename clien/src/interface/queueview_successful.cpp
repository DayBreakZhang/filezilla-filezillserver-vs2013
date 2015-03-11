#include <filezilla.h>
#include "queue.h"
#include "queueview_successful.h"
#include "Options.h"

BEGIN_EVENT_TABLE(CQueueViewSuccessful, CQueueViewFailed)
EVT_CONTEXT_MENU(CQueueViewSuccessful::OnContextMenu)
EVT_MENU(XRCID("ID_AUTOCLEAR"), CQueueViewSuccessful::OnMenuAutoClear)
END_EVENT_TABLE()

CQueueViewSuccessful::CQueueViewSuccessful(CQueue* parent, int index)
	: CQueueViewFailed(parent, index, _("Successful transfers"))
{
	std::list<ColumnId> extraCols;
	extraCols.push_back(colTime);
	CreateColumns(extraCols);

	m_autoClear = COptions::Get()->GetOptionVal(OPTION_QUEUE_SUCCESSFUL_AUTOCLEAR) ? true : false;
}

void CQueueViewSuccessful::OnContextMenu(wxContextMenuEvent& event)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_QUEUE_SUCCESSFUL"));
	if (!pMenu)
		return;

	bool has_selection = HasSelection();

	pMenu->Enable(XRCID("ID_REMOVE"), has_selection);
	pMenu->Enable(XRCID("ID_REQUEUE"), has_selection);
	pMenu->Enable(XRCID("ID_REQUEUEALL"), !m_serverList.empty());
	pMenu->Check(XRCID("ID_AUTOCLEAR"), m_autoClear);

	PopupMenu(pMenu);

	delete pMenu;
}

void CQueueViewSuccessful::OnMenuAutoClear(wxCommandEvent&)
{
	m_autoClear = !m_autoClear;
	COptions::Get()->SetOption(OPTION_QUEUE_SUCCESSFUL_AUTOCLEAR, m_autoClear ? true : false);
}
