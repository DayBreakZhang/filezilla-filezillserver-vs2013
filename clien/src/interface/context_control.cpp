#include <filezilla.h>
#include "commandqueue.h"
#include "context_control.h"
#include "filelist_statusbar.h"
#include "filezillaapp.h"
#include "LocalListView.h"
#include "LocalTreeView.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#include "recursive_operation.h"
#include "RemoteListView.h"
#include "RemoteTreeView.h"
#include "sitemanager.h"
#include "splitter.h"
#include "view.h"
#include "viewheader.h"

#include <wx/wupdlock.h>

DECLARE_EVENT_TYPE(fzEVT_TAB_CLOSING_DEFERRED, -1)
DEFINE_EVENT_TYPE(fzEVT_TAB_CLOSING_DEFERRED)

BEGIN_EVENT_TABLE(CContextControl, wxSplitterWindow)
EVT_MENU(XRCID("ID_TABCONTEXT_REFRESH"), CContextControl::OnTabRefresh)
EVT_COMMAND(wxID_ANY, fzEVT_TAB_CLOSING_DEFERRED, CContextControl::OnTabClosing_Deferred)
EVT_MENU(XRCID("ID_TABCONTEXT_CLOSE"), CContextControl::OnTabContextClose)
EVT_MENU(XRCID("ID_TABCONTEXT_CLOSEOTHERS"), CContextControl::OnTabContextCloseOthers)
EVT_MENU(XRCID("ID_TABCONTEXT_NEW"), CContextControl::OnTabContextNew)
END_EVENT_TABLE()

CContextControl::CContextControl(CMainFrame* pMainFrame)
	: CStateEventHandler(0),
	m_tabs(0), m_right_clicked_tab(-1), m_pMainFrame(pMainFrame)
{
	m_current_context_controls = -1;
	m_tabs = 0;

	wxASSERT(!CContextManager::Get()->HandlerCount(STATECHANGE_CHANGEDCONTEXT));
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, false, false);
}

CContextControl::~CContextControl()
{
}

void CContextControl::Create(wxWindow *parent)
{
	wxSplitterWindow::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER);
}

void CContextControl::CreateTab()
{
	wxGetApp().AddStartupProfileRecord(_T("CContextControl::CreateTab"));
#ifdef __WXMSW__
	// Some reparenting is being done when creating tabs. Reparenting of frozen windows isn't working
	// on OS X.
	wxWindowUpdateLocker lock(this);
#endif

	CState* pState = 0;

	// See if we can reuse an existing context
	for (size_t i = 0; i < m_context_controls.size(); i++) {
		if (m_context_controls[i].tab_index != -1)
			continue;

		if (m_context_controls[i].pState->IsRemoteConnected() ||
			!m_context_controls[i].pState->IsRemoteIdle())
			continue;

		pState = m_context_controls[i].pState;
		m_context_controls.erase(m_context_controls.begin() + i);
		if (m_current_context_controls > (int)i)
			m_current_context_controls--;
		break;
	}
	if (!pState) {
		pState = CContextManager::Get()->CreateState(m_pMainFrame);
		if (!pState->CreateEngine()) {
			wxMessageBoxEx(_("Failed to initialize FTP engine"));
		}
	}

	// Restore last server and path
	CServer last_server;
	CServerPath last_path;
	if (COptions::Get()->GetLastServer(last_server) && last_path.SetSafePath(COptions::Get()->GetOption(OPTION_LASTSERVERPATH)))
		pState->SetLastServer(last_server, last_path);

	CreateContextControls(pState);

	pState->GetRecursiveOperationHandler()->SetQueue(m_pMainFrame->GetQueue());

	wxString localDir = COptions::Get()->GetOption(OPTION_LASTLOCALDIR);
	if (!pState->SetLocalDir(localDir))
		pState->SetLocalDir(_T("/"));

	CContextManager::Get()->SetCurrentContext(pState);

	if (!m_pMainFrame->RestoreSplitterPositions())
		m_pMainFrame->SetDefaultSplitterPositions();

	if (m_tabs)
		m_tabs->SetSelection(m_tabs->GetPageCount() - 1);
}

void CContextControl::CreateContextControls(CState* pState)
{
	wxGetApp().AddStartupProfileRecord(_T("CContextControl::CreateContextControls"));
	wxWindow* parent = this;

#ifdef __WXGTK__
	// This prevents some ugly flickering on tab creation.
	const wxPoint initial_position(1000000, 1000000);
#else
	const wxPoint initial_position(wxDefaultPosition);
#endif

	if (!m_context_controls.empty()) {
		if (!m_tabs) {
			m_tabs = new wxAuiNotebookEx();

			wxSize splitter_size = m_context_controls[m_current_context_controls].pViewSplitter->GetSize();
			m_tabs->Create(this, wxID_ANY, initial_position, splitter_size, wxNO_BORDER | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_WINDOWLIST_BUTTON | wxAUI_NB_CLOSE_ON_ALL_TABS);
			m_tabs->SetExArtProvider();
			m_tabs->SetSelectedFont(*wxNORMAL_FONT);
			m_tabs->SetMeasuringFont(*wxNORMAL_FONT);

			m_context_controls[m_current_context_controls].pViewSplitter->Reparent(m_tabs);

			m_tabs->AddPage(m_context_controls[m_current_context_controls].pViewSplitter, m_context_controls[m_current_context_controls].pState->GetTitle());
			ReplaceWindow(m_context_controls[m_current_context_controls].pViewSplitter, m_tabs);

			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(CContextControl::OnTabChanged), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CLOSE, wxAuiNotebookEventHandler(CContextControl::OnTabClosing), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_BG_DCLICK, wxAuiNotebookEventHandler(CContextControl::OnTabBgDoubleclick), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_TAB_MIDDLE_UP, wxAuiNotebookEventHandler(CContextControl::OnTabClosing), 0, this);
			m_tabs->Connect(wxEVT_COMMAND_AUINOTEBOOK_TAB_RIGHT_UP, wxAuiNotebookEventHandler(CContextControl::OnTabRightclick), 0, this);

#ifdef __WXMAC__
			// We need to select the first page as the default selection is -1. Not doing so prevents selecting other pages later on.
			m_tabs->SetSelection(0);
#endif
		}

		m_pMainFrame->RememberSplitterPositions();
		m_context_controls[m_current_context_controls].pLocalListView->SaveColumnSettings(OPTION_LOCALFILELIST_COLUMN_WIDTHS, OPTION_LOCALFILELIST_COLUMN_SHOWN, OPTION_LOCALFILELIST_COLUMN_ORDER);
		m_context_controls[m_current_context_controls].pRemoteListView->SaveColumnSettings(OPTION_REMOTEFILELIST_COLUMN_WIDTHS, OPTION_REMOTEFILELIST_COLUMN_SHOWN, OPTION_REMOTEFILELIST_COLUMN_ORDER);

		parent = m_tabs;
	}

	struct CContextControl::_context_controls context_controls;

	context_controls.pState = pState;
	context_controls.pViewSplitter = new CSplitterWindowEx(parent, -1, initial_position, wxDefaultSize, wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pViewSplitter->SetMinimumPaneSize(50, 100);
	context_controls.pViewSplitter->SetSashGravity(0.5);

	context_controls.pLocalSplitter = new CSplitterWindowEx(context_controls.pViewSplitter, -1, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pLocalSplitter->SetMinimumPaneSize(50, 100);

	context_controls.pRemoteSplitter = new CSplitterWindowEx(context_controls.pViewSplitter, -1, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER  | wxSP_LIVE_UPDATE);
	context_controls.pRemoteSplitter->SetMinimumPaneSize(50, 100);

	context_controls.pLocalTreeViewPanel = new CView(context_controls.pLocalSplitter);
	context_controls.pLocalListViewPanel = new CView(context_controls.pLocalSplitter);
	context_controls.pLocalTreeView = new CLocalTreeView(context_controls.pLocalTreeViewPanel, -1, pState, m_pMainFrame->GetQueue());
	context_controls.pLocalListView = new CLocalListView(context_controls.pLocalListViewPanel, pState, m_pMainFrame->GetQueue());
	context_controls.pLocalTreeViewPanel->SetWindow(context_controls.pLocalTreeView);
	context_controls.pLocalListViewPanel->SetWindow(context_controls.pLocalListView);

	context_controls.pRemoteTreeViewPanel = new CView(context_controls.pRemoteSplitter);
	context_controls.pRemoteListViewPanel = new CView(context_controls.pRemoteSplitter);
	context_controls.pRemoteTreeView = new CRemoteTreeView(context_controls.pRemoteTreeViewPanel, -1, pState, m_pMainFrame->GetQueue());
	context_controls.pRemoteListView = new CRemoteListView(context_controls.pRemoteListViewPanel, pState, m_pMainFrame->GetQueue());
	context_controls.pRemoteTreeViewPanel->SetWindow(context_controls.pRemoteTreeView);
	context_controls.pRemoteListViewPanel->SetWindow(context_controls.pRemoteListView);

	bool show_filelist_statusbars = COptions::Get()->GetOptionVal(OPTION_FILELIST_STATUSBAR) != 0;

	CFilelistStatusBar* pLocalFilelistStatusBar = new CFilelistStatusBar(context_controls.pLocalListViewPanel);
	if (!show_filelist_statusbars)
		pLocalFilelistStatusBar->Hide();
	context_controls.pLocalListViewPanel->SetStatusBar(pLocalFilelistStatusBar);
	context_controls.pLocalListView->SetFilelistStatusBar(pLocalFilelistStatusBar);

	CFilelistStatusBar* pRemoteFilelistStatusBar = new CFilelistStatusBar(context_controls.pRemoteListViewPanel);
	if (!show_filelist_statusbars)
		pRemoteFilelistStatusBar->Hide();
	context_controls.pRemoteListViewPanel->SetStatusBar(pRemoteFilelistStatusBar);
	context_controls.pRemoteListView->SetFilelistStatusBar(pRemoteFilelistStatusBar);
	pRemoteFilelistStatusBar->SetConnected(false);


	const int layout = COptions::Get()->GetOptionVal(OPTION_FILEPANE_LAYOUT);
	const int swap = COptions::Get()->GetOptionVal(OPTION_FILEPANE_SWAP);

	if (layout == 1) {
		if (swap)
			context_controls.pViewSplitter->SplitHorizontally(context_controls.pRemoteSplitter, context_controls.pLocalSplitter);
		else
			context_controls.pViewSplitter->SplitHorizontally(context_controls.pLocalSplitter, context_controls.pRemoteSplitter);
	}
	else {
		if (swap)
			context_controls.pViewSplitter->SplitVertically(context_controls.pRemoteSplitter, context_controls.pLocalSplitter);
		else
			context_controls.pViewSplitter->SplitVertically(context_controls.pLocalSplitter, context_controls.pRemoteSplitter);
	}

	if (COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_LOCAL)) {
		context_controls.pLocalViewHeader = new CLocalViewHeader(context_controls.pLocalTreeViewPanel, pState);
		context_controls.pLocalTreeViewPanel->SetHeader(context_controls.pLocalViewHeader);
		if (layout == 3 && swap)
			context_controls.pLocalSplitter->SplitVertically(context_controls.pLocalListViewPanel, context_controls.pLocalTreeViewPanel);
		else if (layout)
			context_controls.pLocalSplitter->SplitVertically(context_controls.pLocalTreeViewPanel, context_controls.pLocalListViewPanel);
		else
			context_controls.pLocalSplitter->SplitHorizontally(context_controls.pLocalTreeViewPanel, context_controls.pLocalListViewPanel);
	}
	else {
		context_controls.pLocalTreeViewPanel->Hide();
		context_controls.pLocalViewHeader = new CLocalViewHeader(context_controls.pLocalListViewPanel, pState);
		context_controls.pLocalListViewPanel->SetHeader(context_controls.pLocalViewHeader);
		context_controls.pLocalSplitter->Initialize(context_controls.pLocalListViewPanel);
	}

	if (COptions::Get()->GetOptionVal(OPTION_SHOW_TREE_REMOTE)) {
		context_controls.pRemoteViewHeader = new CRemoteViewHeader(context_controls.pRemoteTreeViewPanel, pState);
		context_controls.pRemoteTreeViewPanel->SetHeader(context_controls.pRemoteViewHeader);
		if (layout == 3 && !swap)
			context_controls.pRemoteSplitter->SplitVertically(context_controls.pRemoteListViewPanel, context_controls.pRemoteTreeViewPanel);
		else if (layout)
			context_controls.pRemoteSplitter->SplitVertically(context_controls.pRemoteTreeViewPanel, context_controls.pRemoteListViewPanel);
		else
			context_controls.pRemoteSplitter->SplitHorizontally(context_controls.pRemoteTreeViewPanel, context_controls.pRemoteListViewPanel);
	}
	else {
		context_controls.pRemoteTreeViewPanel->Hide();
		context_controls.pRemoteViewHeader = new CRemoteViewHeader(context_controls.pRemoteListViewPanel, pState);
		context_controls.pRemoteListViewPanel->SetHeader(context_controls.pRemoteViewHeader);
		context_controls.pRemoteSplitter->Initialize(context_controls.pRemoteListViewPanel);
	}

	if (layout == 3) {
		if (!swap)
			context_controls.pRemoteSplitter->SetSashGravity(1.0);
		else
			context_controls.pLocalSplitter->SetSashGravity(1.0);
	}

	m_pMainFrame->ConnectNavigationHandler(context_controls.pLocalListView);
	m_pMainFrame->ConnectNavigationHandler(context_controls.pRemoteListView);
	m_pMainFrame->ConnectNavigationHandler(context_controls.pLocalTreeView);
	m_pMainFrame->ConnectNavigationHandler(context_controls.pRemoteTreeView);
	m_pMainFrame->ConnectNavigationHandler(context_controls.pLocalViewHeader);
	m_pMainFrame->ConnectNavigationHandler(context_controls.pRemoteViewHeader);

	pState->GetComparisonManager()->SetListings(context_controls.pLocalListView, context_controls.pRemoteListView);

	if (m_tabs) {
		context_controls.tab_index = m_tabs->GetPageCount();
		m_tabs->AddPage(context_controls.pViewSplitter, pState->GetTitle());

		// Copy reconnect and bookmark information
		pState->SetLastServer(
			m_context_controls[m_current_context_controls].pState->GetLastServer(),
			m_context_controls[m_current_context_controls].pState->GetLastServerPath());

		context_controls.site_bookmarks = m_context_controls[m_current_context_controls].site_bookmarks;
	}
	else {
		context_controls.tab_index = 0;
		context_controls.site_bookmarks = std::make_shared<CContextControl::_context_controls::_site_bookmarks>();

		context_controls.site_bookmarks->path = COptions::Get()->GetOption(OPTION_LAST_CONNECTED_SITE);
		CSiteManager::GetBookmarks(context_controls.site_bookmarks->path,
			context_controls.site_bookmarks->bookmarks);

		Initialize(context_controls.pViewSplitter);
	}

	m_context_controls.push_back(context_controls);
}

void CContextControl::OnTabRefresh(wxCommandEvent&)
{
	if (m_right_clicked_tab == -1)
		return;

	for (size_t j = 0; j < m_context_controls.size(); j++)
	{
		if (m_context_controls[j].tab_index != m_right_clicked_tab)
			continue;

		m_context_controls[j].pState->RefreshLocal();
		m_context_controls[j].pState->RefreshRemote();

		break;
	}
}

struct CContextControl::_context_controls* CContextControl::GetCurrentControls()
{
	if (m_current_context_controls == -1)
		return 0;

	return &m_context_controls[m_current_context_controls];
}

struct CContextControl::_context_controls* CContextControl::GetControlsFromState(CState* pState)
{
	size_t i = 0;
	for (i = 0; i < m_context_controls.size(); i++)
	{
		if (m_context_controls[i].pState == pState)
			return &m_context_controls[i];
	}
	return 0;
}

bool CContextControl::CloseTab(int tab)
{
	if (!m_tabs)
		return false;
	if (tab < 0)
		return false;

	size_t i = 0;
	for (i = 0; i < m_context_controls.size(); i++)
	{
		if (m_context_controls[i].tab_index == tab)
			break;
	}
	if (i == m_context_controls.size())
		return false;

	CState* pState = m_context_controls[i].pState;

	if (!pState->m_pCommandQueue->Idle())
	{
		if (wxMessageBoxEx(_("Cannot close tab while busy.\nCancel current operation and close tab?"), _T("FileZilla"), wxYES_NO | wxICON_QUESTION) != wxYES)
			return false;
	}

#ifndef __WXMAC__
	// Some reparenting is being done when closing tabs. Reparenting of frozen windows isn't working
	// on OS X.
	wxWindowUpdateLocker lock(this);
#endif

	pState->m_pCommandQueue->Cancel();
	pState->GetRecursiveOperationHandler()->StopRecursiveOperation();

	pState->GetComparisonManager()->SetListings(0, 0);

	if (m_tabs->GetPageCount() == 2)
	{
		// Get rid again of tab bar
		m_tabs->Disconnect(wxEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED, wxAuiNotebookEventHandler(CContextControl::OnTabChanged), 0, this);

		int keep = tab ? 0 : 1;
		m_tabs->RemovePage(keep);

		size_t j;
		for (j = 0; j < m_context_controls.size(); j++)
		{
			if (m_context_controls[j].tab_index != keep)
				continue;

			break;
		}

		m_context_controls[j].pViewSplitter->Reparent(this);
		ReplaceWindow(m_tabs, m_context_controls[j].pViewSplitter);
		m_context_controls[j].pViewSplitter->Show();
		m_context_controls[j].tab_index = 0;

		wxAuiNotebookEx *tabs = m_tabs;
		m_tabs = 0;

		m_context_controls[i].tab_index = -1;
		m_context_controls[i].site_bookmarks.reset();

		CContextManager::Get()->SetCurrentContext(m_context_controls[j].pState);

		tabs->Destroy();
	}
	else
	{
		if (pState == CContextManager::Get()->GetCurrentContext())
		{
			int newsel = tab + 1;
			if (newsel >= (int)m_tabs->GetPageCount())
				newsel = m_tabs->GetPageCount() - 2;

			for (size_t j = 0; j < m_context_controls.size(); j++)
			{
				if (m_context_controls[j].tab_index != newsel)
					continue;
				m_tabs->SetSelection(newsel);
				CContextManager::Get()->SetCurrentContext(m_context_controls[j].pState);
			}
		}
		for (size_t j = 0; j < m_context_controls.size(); j++)
		{
			if (m_context_controls[j].tab_index > tab)
				m_context_controls[j].tab_index--;
		}
		m_context_controls[i].tab_index = -1;
		m_context_controls[i].site_bookmarks.reset();
		m_tabs->DeletePage(tab);
	}

	pState->Disconnect();

	return true;
}

void CContextControl::OnTabBgDoubleclick(wxAuiNotebookEvent&)
{
	CreateTab();
}

void CContextControl::OnTabRightclick(wxAuiNotebookEvent& event)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_TABCONTEXT"));
	if (!pMenu)
	{
		wxBell();
		return;
	}

	if (!m_tabs || m_tabs->GetPageCount() < 2)
	{
		pMenu->Enable(XRCID("ID_TABCONTEXT_CLOSE"), false);
		pMenu->Enable(XRCID("ID_TABCONTEXT_CLOSEOTHERS"), false);
	}

	m_right_clicked_tab = event.GetSelection();

	PopupMenu(pMenu);

	delete pMenu;
}

void CContextControl::OnTabContextClose(wxCommandEvent& event)
{
	if (m_right_clicked_tab == -1)
		return;

	// Need to defer event, wxAUI would write to free'd memory
	// if we'd actually delete tab and potenially the notebook with it
	QueueEvent(new wxCommandEvent(fzEVT_TAB_CLOSING_DEFERRED, m_right_clicked_tab));
}

void CContextControl::OnTabContextCloseOthers(wxCommandEvent& event)
{
	QueueEvent(new wxCommandEvent (fzEVT_TAB_CLOSING_DEFERRED, -m_right_clicked_tab - 1));
}

void CContextControl::OnTabClosing_Deferred(wxCommandEvent& event)
{
	int tab = event.GetId();
	if (tab < 0)
	{
		tab++;
		int count = GetTabCount();
		for (int i = count - 1; i >= 0; i--)
		{
			if (i != -tab)
				CloseTab(i);
		}
	}
	else
		CloseTab(tab);
}


void CContextControl::OnTabChanged(wxAuiNotebookEvent&)
{
	int i = m_tabs->GetSelection();
	if (i < 0 || i >= (int)m_context_controls.size())
		return;

	for (size_t j = 0; j < m_context_controls.size(); j++)
	{
		if (m_context_controls[j].tab_index != i)
			continue;

		CContextManager::Get()->SetCurrentContext(m_context_controls[j].pState);
		break;
	}
}

void CContextControl::OnTabClosing(wxAuiNotebookEvent& event)
{
	// Need to defer event, wxAUI would write to free'd memory
	// if we'd actually delete tab and potenially the notebook with it
	QueueEvent(new wxCommandEvent(fzEVT_TAB_CLOSING_DEFERRED, event.GetSelection()));

	event.Veto();
}

int CContextControl::GetCurrentTab() const
{
	return m_tabs ? m_tabs->GetSelection() : (m_context_controls.empty() ? -1 : 0);
}

int CContextControl::GetTabCount() const
{
	return m_tabs ? m_tabs->GetPageCount() : (m_context_controls.empty() ? 0 : 1);
}

struct CContextControl::_context_controls* CContextControl::GetControlsFromTabIndex(int i)
{
	for (size_t j = 0; j < m_context_controls.size(); j++)
	{
		if (m_context_controls[j].tab_index == i)
			return &m_context_controls[j];
	}

	return 0;
}

bool CContextControl::SelectTab(int i)
{
	if (i < 0)
		return false;

	if (!m_tabs)
	{
		if (i != 0)
			return false;

		return true;
	}

	if ((int)m_tabs->GetPageCount() <= i)
		return false;

	m_tabs->SetSelection(i);

	return true;
}

void CContextControl::AdvanceTab(bool forward)
{
	if (!m_tabs)
		return;

	m_tabs->AdvanceTab(forward);
}

void CContextControl::OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString&, const void*)
{
	if (notification == STATECHANGE_CHANGEDCONTEXT)
	{
		if (!pState)
		{
			m_current_context_controls = 0;
			return;
		}

		// Get current controls for new current context
		for (m_current_context_controls = 0; m_current_context_controls < (int)m_context_controls.size(); m_current_context_controls++)
		{
			if (m_context_controls[m_current_context_controls].pState == pState)
				break;
		}
		if (m_current_context_controls == (int)m_context_controls.size())
			m_current_context_controls = -1;
	}
	else if (notification == STATECHANGE_SERVER)
	{
		if (!m_tabs)
			return;

		CContextControl::_context_controls* controls = GetControlsFromState(pState);
		if (controls && controls->tab_index != -1)
			m_tabs->SetPageText(controls->tab_index, controls->pState->GetTitle());
	}
}

void CContextControl::OnTabContextNew(wxCommandEvent&)
{
	CreateTab();
}
