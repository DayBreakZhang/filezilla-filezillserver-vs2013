#include <filezilla.h>
#include "sitemanager_dialog.h"

#include "conditionaldialog.h"
#include "drop_target_ex.h"
#include "filezillaapp.h"
#include "ipcmutex.h"
#include "Options.h"
#include "themeprovider.h"
#include "treectrlex.h"
#include "window_state_manager.h"
#include "wrapengine.h"
#include "xmlfunctions.h"

#include <wx/dcclient.h>
#include <wx/dnd.h>

#ifdef __WXMSW__
#include "commctrl.h"
#endif

BEGIN_EVENT_TABLE(CSiteManagerDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CSiteManagerDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CSiteManagerDialog::OnCancel)
EVT_BUTTON(XRCID("ID_CONNECT"), CSiteManagerDialog::OnConnect)
EVT_BUTTON(XRCID("ID_NEWSITE"), CSiteManagerDialog::OnNewSite)
EVT_BUTTON(XRCID("ID_NEWFOLDER"), CSiteManagerDialog::OnNewFolder)
EVT_BUTTON(XRCID("ID_RENAME"), CSiteManagerDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETE"), CSiteManagerDialog::OnDelete)
EVT_TREE_BEGIN_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginLabelEdit)
EVT_TREE_END_LABEL_EDIT(XRCID("ID_SITETREE"), CSiteManagerDialog::OnEndLabelEdit)
EVT_TREE_SEL_CHANGING(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnSelChanged)
EVT_CHOICE(XRCID("ID_LOGONTYPE"), CSiteManagerDialog::OnLogontypeSelChanged)
EVT_BUTTON(XRCID("ID_BROWSE"), CSiteManagerDialog::OnRemoteDirBrowse)
EVT_TREE_ITEM_ACTIVATED(XRCID("ID_SITETREE"), CSiteManagerDialog::OnItemActivated)
EVT_CHECKBOX(XRCID("ID_LIMITMULTIPLE"), CSiteManagerDialog::OnLimitMultipleConnectionsChanged)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_AUTO"), CSiteManagerDialog::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_UTF8"), CSiteManagerDialog::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_CUSTOM"), CSiteManagerDialog::OnCharsetChange)
EVT_CHOICE(XRCID("ID_PROTOCOL"), CSiteManagerDialog::OnProtocolSelChanged)
EVT_BUTTON(XRCID("ID_COPY"), CSiteManagerDialog::OnCopySite)
EVT_TREE_BEGIN_DRAG(XRCID("ID_SITETREE"), CSiteManagerDialog::OnBeginDrag)
EVT_CHAR(CSiteManagerDialog::OnChar)
EVT_TREE_ITEM_MENU(XRCID("ID_SITETREE"), CSiteManagerDialog::OnContextMenu)
EVT_MENU(XRCID("ID_EXPORT"), CSiteManagerDialog::OnExportSelected)
EVT_BUTTON(XRCID("ID_NEWBOOKMARK"), CSiteManagerDialog::OnNewBookmark)
EVT_BUTTON(XRCID("ID_BOOKMARK_BROWSE"), CSiteManagerDialog::OnBookmarkBrowse)
END_EVENT_TABLE()

class CSiteManagerDialogDataObject : public wxDataObjectSimple
{
public:
	CSiteManagerDialogDataObject()
		: wxDataObjectSimple(wxDataFormat(_T("FileZilla3SiteManagerObject")))
	{
	}

	// GTK doesn't like data size of 0
	virtual size_t GetDataSize() const { return 1; }

	virtual bool GetDataHere(void *buf) const { memset(buf, 0, 1); return true; }

	virtual bool SetData(size_t, const void *) { return true; }
};

class CSiteManagerDropTarget : public CScrollableDropTarget<wxTreeCtrlEx>
{
public:
	CSiteManagerDropTarget(CSiteManagerDialog* pSiteManager)
		: CScrollableDropTarget<wxTreeCtrlEx>(XRCCTRL(*pSiteManager, "ID_SITETREE", wxTreeCtrlEx))
	{
		SetDataObject(new CSiteManagerDialogDataObject());
		m_pSiteManager = pSiteManager;
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		ClearDropHighlight();
		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit)
			return wxDragNone;
		if (hit == m_pSiteManager->m_dropSource)
			return wxDragNone;

		const bool predefined = m_pSiteManager->IsPredefinedItem(hit);
		if (predefined)
			return wxDragNone;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		CSiteManagerItemData *pData = (CSiteManagerItemData *)pTree->GetItemData(hit);
		CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(m_pSiteManager->m_dropSource);
		if (pData)
		{
			if (pData->m_type == CSiteManagerItemData::BOOKMARK)
				return wxDragNone;
			if (!pSourceData || pSourceData->m_type != CSiteManagerItemData::BOOKMARK)
				return wxDragNone;
		}
		else if (pSourceData && pSourceData->m_type == CSiteManagerItemData::BOOKMARK)
			return wxDragNone;

		wxTreeItemId item = hit;
		while (item != pTree->GetRootItem())
		{
			if (item == m_pSiteManager->m_dropSource)
			{
				ClearDropHighlight();
				return wxDragNone;
			}
			item = pTree->GetItemParent(item);
		}

		if (def == wxDragMove && pTree->GetItemParent(m_pSiteManager->m_dropSource) == hit)
			return wxDragNone;

		if (!m_pSiteManager->MoveItems(m_pSiteManager->m_dropSource, hit, def == wxDragCopy))
			return wxDragNone;

		return def;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnDrop(x, y);
		ClearDropHighlight();

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit)
			return false;
		if (hit == m_pSiteManager->m_dropSource)
			return false;

		const bool predefined = m_pSiteManager->IsPredefinedItem(hit);
		if (predefined)
			return false;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		CSiteManagerItemData *pData = (CSiteManagerItemData *)pTree->GetItemData(hit);
		CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(m_pSiteManager->m_dropSource);
		if (pData)
		{
			if (pData->m_type == CSiteManagerItemData::BOOKMARK)
				return false;
			if (!pSourceData || pSourceData->m_type != CSiteManagerItemData::BOOKMARK)
				return false;
		}
		else if (pSourceData && pSourceData->m_type == CSiteManagerItemData::BOOKMARK)
			return false;

		wxTreeItemId item = hit;
		while (item != pTree->GetRootItem())
		{
			if (item == m_pSiteManager->m_dropSource)
			{
				ClearDropHighlight();
				return false;
			}
			item = pTree->GetItemParent(item);
		}

		return true;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget<wxTreeCtrlEx>::OnLeave();
		ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

	wxTreeItemId GetHit(const wxPoint& point)
	{
		int flags = 0;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		wxTreeItemId hit = pTree->HitTest(point, flags);

		if (flags & (wxTREE_HITTEST_ABOVE | wxTREE_HITTEST_BELOW | wxTREE_HITTEST_NOWHERE | wxTREE_HITTEST_TOLEFT | wxTREE_HITTEST_TORIGHT))
			return wxTreeItemId();

		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxTreeCtrlEx>::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			ClearDropHighlight();
			return def;
		}

		wxTreeItemId hit = GetHit(wxPoint(x, y));
		if (!hit)
		{
			ClearDropHighlight();
			return wxDragNone;
		}
		if (hit == m_pSiteManager->m_dropSource)
		{
			ClearDropHighlight();
			return wxDragNone;
		}

		const bool predefined = m_pSiteManager->IsPredefinedItem(hit);
		if (predefined)
		{
			ClearDropHighlight();
			return wxDragNone;
		}

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		CSiteManagerItemData *pData = static_cast<CSiteManagerItemData *>(pTree->GetItemData(hit));
		CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(m_pSiteManager->m_dropSource);
		if (pData)
		{
			if (pData->m_type == CSiteManagerItemData::BOOKMARK)
			{
				ClearDropHighlight();
				return wxDragNone;
			}
			if (!pSourceData || pSourceData->m_type != CSiteManagerItemData::BOOKMARK)
			{
				ClearDropHighlight();
				return wxDragNone;
			}
		}
		else if (pSourceData && pSourceData->m_type == CSiteManagerItemData::BOOKMARK)
		{
			ClearDropHighlight();
			return wxDragNone;
		}

		wxTreeItemId item = hit;
		while (item != pTree->GetRootItem())
		{
			if (item == m_pSiteManager->m_dropSource)
			{
				ClearDropHighlight();
				return wxDragNone;
			}
			item = pTree->GetItemParent(item);
		}

		if (def == wxDragMove && pTree->GetItemParent(m_pSiteManager->m_dropSource) == hit)
		{
			ClearDropHighlight();
			return wxDragNone;
		}

		DisplayDropHighlight(wxPoint(x, y));

		return def;
	}

	void ClearDropHighlight()
	{
		if (m_dropHighlight == wxTreeItemId())
			return;

		wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
		pTree->SetItemDropHighlight(m_dropHighlight, false);
		m_dropHighlight = wxTreeItemId();
	}

	wxTreeItemId DisplayDropHighlight(wxPoint p)
	{
		ClearDropHighlight();

		wxTreeItemId hit = GetHit(p);
		if (hit.IsOk()) {
			wxTreeCtrl *pTree = XRCCTRL(*m_pSiteManager, "ID_SITETREE", wxTreeCtrl);
			pTree->SetItemDropHighlight(hit, true);
			m_dropHighlight = hit;
		}

		return hit;
	}

protected:
	CSiteManagerDialog* m_pSiteManager;
	wxTreeItemId m_dropHighlight;
};

CSiteManagerDialog::CSiteManagerDialog()
	: m_connected_sites()
{
	m_pSiteManagerMutex = 0;
	m_pWindowStateManager = 0;

	m_pNotebook_Site = 0;
	m_pNotebook_Bookmark = 0;

	m_is_deleting = false;
}

CSiteManagerDialog::~CSiteManagerDialog()
{
	delete m_pSiteManagerMutex;

	if (m_pWindowStateManager)
	{
		m_pWindowStateManager->Remember(OPTION_SITEMANAGER_POSITION);
		delete m_pWindowStateManager;
	}
}

bool CSiteManagerDialog::Create(wxWindow* parent, std::vector<_connected_site> *connected_sites, const CServer* pServer /*=0*/)
{
	m_pSiteManagerMutex = new CInterProcessMutex(MUTEX_SITEMANAGERGLOBAL, false);
	if (m_pSiteManagerMutex->TryLock() == 0)
	{
		int answer = wxMessageBoxEx(_("The Site Manager is opened in another instance of FileZilla 3.\nDo you want to continue? Any changes made in the Site Manager won't be saved then."),
								  _("Site Manager already open"), wxYES_NO | wxICON_QUESTION);
		if (answer != wxYES)
			return false;

		delete m_pSiteManagerMutex;
		m_pSiteManagerMutex = 0;
	}
	CreateControls(parent);

	// Now create the imagelist for the site tree
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return false;

	wxSize s = CThemeProvider::GetIconSize(iconSizeSmall);
	wxImageList* pImageList = new wxImageList(s.x, s.y);

	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_FOLDERCLOSED"), wxART_OTHER, s));
	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_FOLDER"), wxART_OTHER, s));
	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_SERVER"), wxART_OTHER, s));
	pImageList->Add(wxArtProvider::GetBitmap(_T("ART_BOOKMARK"), wxART_OTHER, s));

	pTree->AssignImageList(pImageList);

	m_pNotebook_Site = XRCCTRL(*this, "ID_NOTEBOOK", wxNotebook);

#ifdef __WXMSW__
	// Make pages at least wide enough to fit all tabs
	HWND hWnd = (HWND)m_pNotebook_Site->GetHandle();

	int width = 4;
	for (unsigned int i = 0; i < m_pNotebook_Site->GetPageCount(); ++i)
	{
		RECT tab_rect;
		TabCtrl_GetItemRect(hWnd, i, &tab_rect);
		width += tab_rect.right - tab_rect.left;
	}
	int margin = m_pNotebook_Site->GetSize().x - m_pNotebook_Site->GetPage(0)->GetSize().x;
	m_pNotebook_Site->GetPage(0)->GetSizer()->SetMinSize(wxSize(width - margin, 0));
#else
	// Make pages at least wide enough to fit all tabs
	int width = 10; // Guessed
	wxClientDC dc(m_pNotebook_Site);
	for (unsigned int i = 0; i < m_pNotebook_Site->GetPageCount(); ++i)
	{
		wxCoord w, h;
		dc.GetTextExtent(m_pNotebook_Site->GetPageText(i), &w, &h);

		width += w;
#ifdef __WXMAC__
		width += 20; // Guessed
#else
		width += 20;
#endif
	}

	wxSize page_min_size = m_pNotebook_Site->GetPage(0)->GetSizer()->GetMinSize();
	if (page_min_size.x < width)
	{
		page_min_size.x = width;
		m_pNotebook_Site->GetPage(0)->GetSizer()->SetMinSize(page_min_size);
	}
#endif

	Layout();
	wxGetApp().GetWrapEngine()->WrapRecursive(this, 1.33, "Site Manager");

	wxSize minSize = GetSizer()->GetMinSize();

	wxSize size = GetSize();
	wxSize clientSize = GetClientSize();
	SetMinSize(GetSizer()->GetMinSize() + size - clientSize);
	SetClientSize(minSize);

	// Load bookmark notebook
	m_pNotebook_Bookmark = new wxNotebook(m_pNotebook_Site->GetParent(), -1);
	wxPanel* pPanel = new wxPanel;
	wxXmlResource::Get()->LoadPanel(pPanel, m_pNotebook_Bookmark, _T("ID_SITEMANAGER_BOOKMARK_PANEL"));
	m_pNotebook_Bookmark->Hide();
	m_pNotebook_Bookmark->AddPage(pPanel, _("Bookmark"));
	wxSizer *pSizer = m_pNotebook_Site->GetContainingSizer();
	pSizer->Add(m_pNotebook_Bookmark, 1, wxGROW);
	pSizer->SetItemMinSize(1, pSizer->GetItem((size_t)0)->GetMinSize().GetWidth(), -1);

	Load();

	XRCCTRL(*this, "ID_TRANSFERMODE_DEFAULT", wxRadioButton)->Update();
	XRCCTRL(*this, "ID_TRANSFERMODE_ACTIVE", wxRadioButton)->Update();
	XRCCTRL(*this, "ID_TRANSFERMODE_PASSIVE", wxRadioButton)->Update();

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		pTree->SafeSelectItem(m_ownSites);
	SetCtrlState();

	m_pWindowStateManager = new CWindowStateManager(this);
	m_pWindowStateManager->Restore(OPTION_SITEMANAGER_POSITION);

	pTree->SetDropTarget(new CSiteManagerDropTarget(this));

#ifdef __WXGTK__
	{
		CSiteManagerItemData* data = 0;
		wxTreeItemId item = pTree->GetSelection();
		if (item.IsOk())
			data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
		if (!data)
			XRCCTRL(*this, "wxID_OK", wxButton)->SetFocus();
	}
#endif

	m_connected_sites = connected_sites;
	MarkConnectedSites();

	if (pServer)
		CopyAddServer(*pServer);

	return true;
}

void CSiteManagerDialog::MarkConnectedSites()
{
	for (int i = 0; i < (int)m_connected_sites->size(); ++i)
		MarkConnectedSite(i);
}

void CSiteManagerDialog::MarkConnectedSite(int connected_site)
{
	wxString connected_site_path = (*m_connected_sites)[connected_site].old_path;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	if (connected_site_path.Left(1) == _T("1"))
	{
		// Default sites never change
		(*m_connected_sites)[connected_site].new_path = (*m_connected_sites)[connected_site].old_path;
		return;
	}

	if (connected_site_path.Left(1) != _T("0"))
		return;

	std::list<wxString> segments;
	if (!CSiteManager::UnescapeSitePath(connected_site_path.Mid(1), segments))
		return;

	wxTreeItemId current = m_ownSites;
	while (!segments.empty())
	{
		wxTreeItemIdValue c;
		wxTreeItemId child = pTree->GetFirstChild(current, c);
		while (child)
		{
			if (pTree->GetItemText(child) == segments.front())
				break;

			child = pTree->GetNextChild(current, c);
		}
		if (!child)
			return;

		segments.pop_front();
		current = child;
	}

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(current));
	if (!data || data->m_type != CSiteManagerItemData::SITE)
		return;

	CSiteManagerItemData_Site *site_data = static_cast<CSiteManagerItemData_Site* >(data);
	wxASSERT(site_data->connected_item == -1);
	site_data->connected_item = connected_site;
}

void CSiteManagerDialog::CreateControls(wxWindow* parent)
{
	if( !wxDialogEx::Load(parent, _T("ID_SITEMANAGER"))) {
		return;
	}

	wxChoice *pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	pProtocol->Append(_("FTP - File Transfer Protocol"));
	pProtocol->Append(CServer::GetProtocolName(SFTP));

	wxChoice *pChoice = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < SERVERTYPE_MAX; ++i)
		pChoice->Append(CServer::GetNameFromServerType((enum ServerType)i));

	pChoice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < LOGONTYPE_MAX; ++i)
		pChoice->Append(CServer::GetNameFromLogonType((enum LogonType)i));

	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);
	pEncryption->Append(_("Use explicit FTP over TLS if available"));
	pEncryption->Append(_("Require explicit FTP over TLS"));
	pEncryption->Append(_("Require implicit FTP over TLS"));
	pEncryption->Append(_("Only use plain FTP (insecure)"));
	pEncryption->SetSelection(0);
}

void CSiteManagerDialog::OnOK(wxCommandEvent&)
{
	if (!Verify())
		return;

	UpdateItem();

	Save();

	RememberLastSelected();

	EndModal(wxID_OK);
}

void CSiteManagerDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CSiteManagerDialog::OnConnect(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
	{
		wxBell();
		return;
	}

	if (!Verify())
	{
		wxBell();
		return;
	}

	UpdateItem();

	Save();

	RememberLastSelected();

	EndModal(wxID_YES);
}

class CSiteManagerXmlHandler_Tree : public CSiteManagerXmlHandler
{
public:
	CSiteManagerXmlHandler_Tree(wxTreeCtrlEx* pTree, wxTreeItemId root, const wxString& lastSelection, bool predefined)
		: m_pTree(pTree), m_item(root), m_predefined(predefined)
	{
		if (!CSiteManager::UnescapeSitePath(lastSelection, m_lastSelection))
			m_lastSelection.clear();
		m_wrong_sel_depth = 0;
		m_kiosk = COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE);
	}

	virtual ~CSiteManagerXmlHandler_Tree()
	{
		m_pTree->SortChildren(m_item);
		m_pTree->Expand(m_item);
	}

	virtual bool AddFolder(const wxString& name, bool expanded)
	{
		wxTreeItemId newItem = m_pTree->AppendItem(m_item, name, 0, 0);
		m_pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
		m_pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

		m_item = newItem;
		m_expand.push_back(expanded);

		if (!m_wrong_sel_depth && !m_lastSelection.empty())
		{
			const wxString& first = m_lastSelection.front();
			if (first == name)
			{
				m_lastSelection.pop_front();
				if (m_lastSelection.empty())
					m_pTree->SafeSelectItem(newItem);
			}
			else
				++m_wrong_sel_depth;
		}
		else
			++m_wrong_sel_depth;

		return true;
	}

	virtual bool AddSite(std::unique_ptr<CSiteManagerItemData_Site> data)
	{
		if (m_kiosk && !m_predefined &&
			data->m_server.GetLogonType() == NORMAL)
		{
			// Clear saved password
			data->m_server.SetLogonType(ASK);
			data->m_server.SetUser(data->m_server.GetUser());
		}

		const wxString name(data->m_server.GetName());

		wxTreeItemId newItem = m_pTree->AppendItem(m_item, name, 2, 2, data.release());

		m_item = newItem;
		m_expand.push_back(true);

		if (!m_wrong_sel_depth && !m_lastSelection.empty()) {
			const wxString& first = m_lastSelection.front();
			if (first == name) {
				m_lastSelection.pop_front();
				if (m_lastSelection.empty())
					m_pTree->SafeSelectItem(newItem);
			}
			else
				++m_wrong_sel_depth;
		}
		else
			++m_wrong_sel_depth;

		return true;
	}

	virtual bool AddBookmark(const wxString& name, std::unique_ptr<CSiteManagerItemData> data)
	{
		wxTreeItemId newItem = m_pTree->AppendItem(m_item, name, 3, 3, data.release());

		if (!m_wrong_sel_depth && !m_lastSelection.empty()) {
			const wxString& first = m_lastSelection.front();
			if (first == name) {
				m_lastSelection.clear();
				m_pTree->SafeSelectItem(newItem);
			}
		}

		return true;
	}

	virtual bool LevelUp()
	{
		if (m_wrong_sel_depth)
			m_wrong_sel_depth--;

		if (!m_expand.empty())
		{
			const bool expand = m_expand.back();
			m_expand.pop_back();
			if (expand)
				m_pTree->Expand(m_item);
		}
		m_pTree->SortChildren(m_item);

		wxTreeItemId parent = m_pTree->GetItemParent(m_item);
		if (!parent)
			return false;

		m_item = parent;
		return true;
	}

protected:
	wxTreeCtrlEx* m_pTree;
	wxTreeItemId m_item;

	std::list<wxString> m_lastSelection;
	int m_wrong_sel_depth;

	std::list<bool> m_expand;

	bool m_predefined;
	int m_kiosk;
};

bool CSiteManagerDialog::Load()
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return false;

	pTree->DeleteAllItems();

	// We have to synchronize access to sitemanager.xml so that multiple processed don't write
	// to the same file or one is reading while the other one writes.
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	// Load default sites
	bool hasDefaultSites = LoadDefaultSites();
	if (hasDefaultSites)
		m_ownSites = pTree->AppendItem(pTree->GetRootItem(), _("My Sites"), 0, 0);
	else
		m_ownSites = pTree->AddRoot(_("My Sites"), 0, 0);

	wxTreeItemId treeId = m_ownSites;
	pTree->SetItemImage(treeId, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(treeId, 1, wxTreeItemIcon_SelectedExpanded);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	TiXmlElement* pDocument = file.Load();
	if (!pDocument)
	{
		wxString msg = file.GetError() + _T("\n") + _("Any changes made in the Site Manager will not be saved unless you repair the file.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	TiXmlElement* pElement = pDocument->FirstChildElement("Servers");
	if (!pElement)
		return true;

	wxString lastSelection = COptions::Get()->GetOption(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '0') {
		if (lastSelection == _T("0"))
			pTree->SafeSelectItem(treeId);
		else
			lastSelection = lastSelection.Mid(1);
	}
	else
		lastSelection.clear();
	CSiteManagerXmlHandler_Tree handler(pTree, treeId, lastSelection, false);

	bool res = CSiteManager::Load(pElement, handler);

	pTree->SortChildren(treeId);
	pTree->Expand(treeId);
	if (!pTree->GetSelection())
		pTree->SafeSelectItem(treeId);

	pTree->EnsureVisible(pTree->GetSelection());

	return res;
}

bool CSiteManagerDialog::Save(TiXmlElement *pElement /*=0*/, wxTreeItemId treeId /*=wxTreeItemId()*/)
{
	if (!m_pSiteManagerMutex)
		return false;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return false;

	if (!pElement || !treeId)
	{
		// We have to synchronize access to sitemanager.xml so that multiple processed don't write
		// to the same file or one is reading while the other one writes.
		CInterProcessMutex mutex(MUTEX_SITEMANAGER);

		CXmlFile xml(wxGetApp().GetSettingsFile(_T("sitemanager")));

		TiXmlElement* pDocument = xml.Load();
		if (!pDocument) {
			wxString msg = xml.GetError() + _T("\n") + _("Any changes made in the Site Manager could not be saved.");
			wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

			return false;
		}

		TiXmlElement *pServers = pDocument->FirstChildElement("Servers");
		while (pServers) {
			pDocument->RemoveChild(pServers);
			pServers = pDocument->FirstChildElement("Servers");
		}
		pElement = pDocument->LinkEndChild(new TiXmlElement("Servers"))->ToElement();

		if (!pElement)
			return true;

		bool res = Save(pElement, m_ownSites);

		if (!xml.Save(false)) {
			if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2)
				return res;
			wxString msg = wxString::Format(_("Could not write \"%s\", any changes to the Site Manager could not be saved: %s"), xml.GetFileName(), xml.GetError());
			wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
		}

		return res;
	}

	wxTreeItemId child;
	wxTreeItemIdValue cookie;
	child = pTree->GetFirstChild(treeId, cookie);
	while (child.IsOk())
	{
		SaveChild(pElement, child);

		child = pTree->GetNextChild(treeId, cookie);
	}

	return false;
}

bool CSiteManagerDialog::SaveChild(TiXmlElement *pElement, wxTreeItemId child)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return false;

	wxString name = pTree->GetItemText(child);
	wxScopedCharBuffer utf8 = name.utf8_str();

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(child));
	if (!data)
	{
		TiXmlNode* pNode = pElement->LinkEndChild(new TiXmlElement("Folder"));
		const bool expanded = pTree->IsExpanded(child);
		SetTextAttribute(pNode->ToElement(), "expanded", expanded ? _T("1") : _T("0"));

		pNode->LinkEndChild(new TiXmlText(utf8));

		Save(pNode->ToElement(), child);
	}
	else if (data->m_type == CSiteManagerItemData::SITE)
	{
		CSiteManagerItemData_Site *site_data = static_cast<CSiteManagerItemData_Site* >(data);
		TiXmlElement* pNode = pElement->LinkEndChild(new TiXmlElement("Server"))->ToElement();
		SetServer(pNode, site_data->m_server);

		// Save comments
		AddTextElement(pNode, "Comments", site_data->m_comments);

		// Save local dir
		AddTextElement(pNode, "LocalDir", data->m_localDir);

		// Save remote dir
		AddTextElement(pNode, "RemoteDir", data->m_remoteDir.GetSafePath());

		AddTextElementRaw(pNode, "SyncBrowsing", data->m_sync ? "1" : "0");

		pNode->LinkEndChild(new TiXmlText(utf8));

		Save(pNode, child);

		if (site_data->connected_item != -1) {
			if ((*m_connected_sites)[site_data->connected_item].server == site_data->m_server) {
				(*m_connected_sites)[site_data->connected_item].new_path = GetSitePath(child);
				(*m_connected_sites)[site_data->connected_item].server = site_data->m_server;
			}
		}
	}
	else {
		TiXmlElement* pNode = pElement->LinkEndChild(new TiXmlElement("Bookmark"))->ToElement();

		AddTextElement(pNode, "Name", name);

		// Save local dir
		AddTextElement(pNode, "LocalDir", data->m_localDir);

		// Save remote dir
		AddTextElement(pNode, "RemoteDir", data->m_remoteDir.GetSafePath());

		AddTextElementRaw(pNode, "SyncBrowsing", data->m_sync ? "1" : "0");
	}

	return true;
}

void CSiteManagerDialog::OnNewFolder(wxCommandEvent&event)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	while (pTree->GetItemData(item))
		item = pTree->GetItemParent(item);

	if (!Verify())
		return;

	wxString name = FindFirstFreeName(item, _("New folder"));

	wxTreeItemId newItem = pTree->AppendItem(item, name, 0, 0);
	pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);
	pTree->SortChildren(item);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

bool CSiteManagerDialog::Verify()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return true;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return true;

	CSiteManagerItemData* data = (CSiteManagerItemData *)pTree->GetItemData(item);
	if (!data)
		return true;

	if (data->m_type == CSiteManagerItemData::SITE) {
		const wxString& host = XRCCTRL(*this, "ID_HOST", wxTextCtrl)->GetValue();
		if (host.empty()) {
			XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You have to enter a hostname."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		LogonType logon_type = CServer::GetLogonTypeFromName(XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->GetStringSelection());

		ServerProtocol protocol = GetProtocol();
		wxASSERT(protocol != UNKNOWN);

		if (protocol == SFTP &&
			logon_type == ACCOUNT)
		{
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
			wxMessageBoxEx(_("'Account' logontype not supported by selected protocol"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0 &&
			!IsPredefinedItem(item) &&
			(logon_type == ACCOUNT || logon_type == NORMAL))
		{
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
			wxString msg;
			if (COptions::Get()->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE))
				msg = _("Saving of password has been disabled by your system administrator.");
			else
				msg = _("Saving of passwords has been disabled by you.");
			msg += _T("\n");
			msg += _("'Normal' and 'Account' logontypes are not available. Your entry has been changed to 'Ask for password'.");
			XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(CServer::GetNameFromLogonType(ASK));
			XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(wxString());
			logon_type = ASK;
			wxMessageBoxEx(msg, _("Site Manager - Cannot remember password"), wxICON_INFORMATION, this);
		}

		// Set selected type
		CServer server;
		server.SetLogonType(logon_type);
		server.SetProtocol(protocol);

		wxString port = XRCCTRL(*this, "ID_PORT", wxTextCtrl)->GetValue();
		CServerPath path;
		wxString error;
		if (!server.ParseUrl(host, port, wxString(), wxString(), error, path)) {
			XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(error, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(server.FormatHost(true));
		if (server.GetPort() != CServer::GetDefaultPort(server.GetProtocol())) {
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString::Format(_T("%d"), server.GetPort()));
		}
		else {
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString());
		}

		SetProtocol(server.GetProtocol());

		if (XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->GetValue()) {
			if (XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->GetValue().empty()) {
				XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("Need to specify a character encoding"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		// Require username for non-anonymous, non-ask logon type
		const wxString user = XRCCTRL(*this, "ID_USER", wxTextCtrl)->GetValue();
		if (logon_type != ANONYMOUS &&
			logon_type != ASK &&
			logon_type != INTERACTIVE &&
			user.empty())
		{
			XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You have to specify a user name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		// The way TinyXML handles blanks, we can't use username of only spaces
		if (!user.empty()) {
			bool space_only = true;
			for (unsigned int i = 0; i < user.Len(); ++i) {
				if (user[i] != ' ') {
					space_only = false;
					break;
				}
			}
			if (space_only) {
				XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("Username cannot be a series of spaces"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		// Require account for account logon type
		if (logon_type == ACCOUNT &&
			XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->GetValue().empty())
		{
			XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You have to enter an account name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		const wxString remotePathRaw = XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->GetValue();
		if (!remotePathRaw.empty()) {
			const wxString serverType = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->GetStringSelection();

			CServerPath remotePath;
			remotePath.SetType(CServer::GetServerTypeFromName(serverType));
			if (!remotePath.SetPath(remotePathRaw))
			{
				XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("Default remote path cannot be parsed. Make sure it is a valid absolute path for the selected server type."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		const wxString localPath = XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue();
		if (XRCCTRL(*this, "ID_SYNC", wxCheckBox)->GetValue()) {
			if (remotePathRaw.empty() || localPath.empty()) {
				XRCCTRL(*this, "ID_SYNC", wxCheckBox)->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this site."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}
	}
	else {
		wxTreeItemId parent = pTree->GetItemParent(item);
		CSiteManagerItemData_Site* pServer = static_cast<CSiteManagerItemData_Site* >(pTree->GetItemData(parent));
		if (!pServer)
			return false;

		const wxString remotePathRaw = XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->GetValue();
		if (!remotePathRaw.empty()) {
			CServerPath remotePath;
			remotePath.SetType(pServer->m_server.GetType());
			if (!remotePath.SetPath(remotePathRaw)) {
				XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->SetFocus();
				wxString msg;
				if (pServer->m_server.GetType() != DEFAULT)
					msg = wxString::Format(_("Remote path cannot be parsed. Make sure it is a valid absolute path and is supported by the servertype (%s) selected on the parent site."), CServer::GetNameFromServerType(pServer->m_server.GetType()));
				else
					msg = _("Remote path cannot be parsed. Make sure it is a valid absolute path.");
				wxMessageBoxEx(msg, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		const wxString localPath = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue();

		if (remotePathRaw.empty() && localPath.empty()) {
			XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You need to enter at least one path, empty bookmarks are not supported."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}

		if (XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->GetValue()) {
			if (remotePathRaw.empty() || localPath.empty()) {
				XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this bookmark."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}
	}

	return true;
}

void CSiteManagerDialog::OnBeginLabelEdit(wxTreeEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		event.Veto();
		return;
	}

	if (event.GetItem() != pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	wxTreeItemId item = event.GetItem();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		event.Veto();
		return;
	}
}

void CSiteManagerDialog::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled())
		return;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree) {
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (item != pTree->GetSelection()) {
		if (!Verify()) {
			event.Veto();
			return;
		}
	}

	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item)) {
		event.Veto();
		return;
	}

	wxString name = event.GetLabel();

	wxTreeItemId parent = pTree->GetItemParent(item);

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = pTree->GetFirstChild(parent, cookie); child.IsOk(); child = pTree->GetNextChild(parent, cookie)) {
		if (child == item)
			continue;
		if (!name.CmpNoCase(pTree->GetItemText(child))) {
			wxMessageBoxEx(_("Name already exists"), _("Cannot rename entry"), wxICON_EXCLAMATION, this);
			event.Veto();
			return;
		}
	}

	pTree->SortChildren(parent);
}

void CSiteManagerDialog::OnRename(wxCommandEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item))
		return;

	pTree->EditLabel(item);
}

void CSiteManagerDialog::OnDelete(wxCommandEvent& event)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || item == pTree->GetRootItem() || item == m_ownSites || IsPredefinedItem(item))
		return;

	CConditionalDialog dlg(this, CConditionalDialog::sitemanager_confirmdelete, CConditionalDialog::yesno);
	dlg.SetTitle(_("Delete Site Manager entry"));

	dlg.AddText(_("Do you really want to delete selected entry?"));

	if (!dlg.Run())
		return;

	wxTreeItemId parent = pTree->GetItemParent(item);
	if (pTree->GetChildrenCount(parent) == 1)
		pTree->Collapse(parent);

	m_is_deleting = true;

	pTree->Delete(item);
	pTree->SafeSelectItem(parent);

	m_is_deleting = false;

	SetCtrlState();
}

void CSiteManagerDialog::OnSelChanging(wxTreeEvent& event)
{
	if (m_is_deleting)
		return;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	if (!Verify())
		event.Veto();

	UpdateItem();
}

void CSiteManagerDialog::OnSelChanged(wxTreeEvent& event)
{
	SetCtrlState();
}

void CSiteManagerDialog::OnNewSite(wxCommandEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || IsPredefinedItem(item))
		return;

	while (pTree->GetItemData(item))
		item = pTree->GetItemParent(item);

	if (!Verify())
		return;

	CServer server;
	AddNewSite(item, server);
}

void CSiteManagerDialog::OnLogontypeSelChanged(wxCommandEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
		return;

	XRCCTRL(*this, "ID_USER", wxTextCtrl)->Enable(event.GetString() != _("Anonymous"));
	XRCCTRL(*this, "ID_PASS", wxTextCtrl)->Enable(event.GetString() == _("Normal") || event.GetString() == _("Account"));
	XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->Enable(event.GetString() == _("Account"));
}

bool CSiteManagerDialog::UpdateItem()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return false;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return false;

	if (IsPredefinedItem(item))
		return true;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
		return false;

	if (data->m_type == CSiteManagerItemData::SITE)
		return UpdateServer(*(CSiteManagerItemData_Site *)data, pTree->GetItemText(item));
	else
	{
		wxTreeItemId parent = pTree->GetItemParent(item);
		CSiteManagerItemData_Site* pServer = static_cast<CSiteManagerItemData_Site* >(pTree->GetItemData(parent));
		if (!pServer)
			return false;
		return UpdateBookmark(*data, pServer->m_server);
	}
}

bool CSiteManagerDialog::UpdateBookmark(CSiteManagerItemData &bookmark, const CServer& server)
{
	bookmark.m_localDir = XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue();
	bookmark.m_remoteDir = CServerPath();
	bookmark.m_remoteDir.SetType(server.GetType());
	bookmark.m_remoteDir.SetPath(XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->GetValue());
	bookmark.m_sync = XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->GetValue();

	return true;
}

bool CSiteManagerDialog::UpdateServer(CSiteManagerItemData_Site &server, const wxString &name)
{
	ServerProtocol const protocol = GetProtocol();
	wxASSERT(protocol != UNKNOWN);
	server.m_server.SetProtocol(protocol);

	unsigned long port;
	if (!XRCCTRL(*this, "ID_PORT", wxTextCtrl)->GetValue().ToULong(&port) || !port || port > 65535) {
		port = CServer::GetDefaultPort(protocol);
	}
	wxString host = XRCCTRL(*this, "ID_HOST", wxTextCtrl)->GetValue();
	// SetHost does not accept URL syntax
	if (!host.empty() && host[0] == '[') {
		host.RemoveLast();
		host = host.Mid(1);
	}
	server.m_server.SetHost(host, port);

	enum LogonType logon_type = CServer::GetLogonTypeFromName(XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->GetStringSelection());
	server.m_server.SetLogonType(logon_type);

	server.m_server.SetUser(XRCCTRL(*this, "ID_USER", wxTextCtrl)->GetValue(),
						   XRCCTRL(*this, "ID_PASS", wxTextCtrl)->GetValue());
	server.m_server.SetAccount(XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->GetValue());

	server.m_comments = XRCCTRL(*this, "ID_COMMENTS", wxTextCtrl)->GetValue();

	const wxString serverType = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->GetStringSelection();
	server.m_server.SetType(CServer::GetServerTypeFromName(serverType));

	server.m_localDir = XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue();
	server.m_remoteDir = CServerPath();
	server.m_remoteDir.SetType(server.m_server.GetType());
	server.m_remoteDir.SetPath(XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->GetValue());
	server.m_sync = XRCCTRL(*this, "ID_SYNC", wxCheckBox)->GetValue();

	int hours, minutes;
	hours = XRCCTRL(*this, "ID_TIMEZONE_HOURS", wxSpinCtrl)->GetValue();
	minutes = XRCCTRL(*this, "ID_TIMEZONE_MINUTES", wxSpinCtrl)->GetValue();

	server.m_server.SetTimezoneOffset(hours * 60 + minutes);

	if (XRCCTRL(*this, "ID_TRANSFERMODE_ACTIVE", wxRadioButton)->GetValue())
		server.m_server.SetPasvMode(MODE_ACTIVE);
	else if (XRCCTRL(*this, "ID_TRANSFERMODE_PASSIVE", wxRadioButton)->GetValue())
		server.m_server.SetPasvMode(MODE_PASSIVE);
	else
		server.m_server.SetPasvMode(MODE_DEFAULT);

	if (XRCCTRL(*this, "ID_LIMITMULTIPLE", wxCheckBox)->GetValue())
	{
		server.m_server.MaximumMultipleConnections(XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->GetValue());
	}
	else
		server.m_server.MaximumMultipleConnections(0);

	if (XRCCTRL(*this, "ID_CHARSET_UTF8", wxRadioButton)->GetValue())
		server.m_server.SetEncodingType(ENCODING_UTF8);
	else if (XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->GetValue())
	{
		wxString encoding = XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->GetValue();
		server.m_server.SetEncodingType(ENCODING_CUSTOM, encoding);
	}
	else
		server.m_server.SetEncodingType(ENCODING_AUTO);

	if (XRCCTRL(*this, "ID_BYPASSPROXY", wxCheckBox)->GetValue())
		server.m_server.SetBypassProxy(true);
	else
		server.m_server.SetBypassProxy(false);

	server.m_server.SetName(name);

	return true;
}

bool CSiteManagerDialog::GetServer(CSiteManagerItemData_Site& data)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return false;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return false;

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!pData)
		return false;

	if (pData->m_type == CSiteManagerItemData::BOOKMARK)
	{
		item = pTree->GetItemParent(item);
		CSiteManagerItemData_Site* pSiteData = static_cast<CSiteManagerItemData_Site* >(pTree->GetItemData(item));

		data = *pSiteData;
		if (!pData->m_localDir.empty())
			data.m_localDir = pData->m_localDir;
		if (!pData->m_remoteDir.empty())
			data.m_remoteDir = pData->m_remoteDir;
		if (data.m_localDir.empty() || data.m_remoteDir.empty())
			data.m_sync = false;
		else
			data.m_sync = pData->m_sync;
	}
	else
		data = *(CSiteManagerItemData_Site *)pData;

	data.m_path = GetSitePath(item);

	return true;
}

void CSiteManagerDialog::OnRemoteDirBrowse(wxCommandEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
		return;

	wxDirDialog dlg(this, _("Choose the default local directory"), XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK)
	{
		XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
	}
}

void CSiteManagerDialog::OnItemActivated(wxTreeEvent& event)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
		return;

	wxCommandEvent cmdEvent;
	OnConnect(cmdEvent);
}

void CSiteManagerDialog::OnLimitMultipleConnectionsChanged(wxCommandEvent& event)
{
	XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->Enable(event.IsChecked());
}

void CSiteManagerDialog::SetCtrlState()
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();

	const bool predefined = IsPredefinedItem(item);

#ifdef __WXGTK__
	wxWindow* pFocus = FindFocus();
#endif

	CSiteManagerItemData* data = 0;
	if (item.IsOk())
		data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data) {
		m_pNotebook_Site->Show();
		m_pNotebook_Bookmark->Hide();
		m_pNotebook_Site->GetContainingSizer()->Layout();

		// Set the control states according if it's possible to use the control
		const bool root_or_predefined = (item == pTree->GetRootItem() || item == m_ownSites || predefined);

		XRCCTRL(*this, "ID_RENAME", wxWindow)->Enable(!root_or_predefined);
		XRCCTRL(*this, "ID_DELETE", wxWindow)->Enable(!root_or_predefined);
		XRCCTRL(*this, "ID_COPY", wxWindow)->Enable(false);
		m_pNotebook_Site->Enable(false);
		XRCCTRL(*this, "ID_NEWFOLDER", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWSITE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWBOOKMARK", wxWindow)->Enable(false);
		XRCCTRL(*this, "ID_CONNECT", wxWindow)->Enable(false);

		// Empty all site information
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString());
		SetProtocol(FTP);
		XRCCTRL(*this, "ID_BYPASSPROXY", wxCheckBox)->SetValue(false);
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(_("Anonymous"));
		XRCCTRL(*this, "ID_USER", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_COMMENTS", wxTextCtrl)->ChangeValue(wxString());

		XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->SetSelection(0);
		XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_SYNC", wxCheckBox)->SetValue(false);
		XRCCTRL(*this, "ID_TIMEZONE_HOURS", wxSpinCtrl)->SetValue(0);
		XRCCTRL(*this, "ID_TIMEZONE_MINUTES", wxSpinCtrl)->SetValue(0);

		XRCCTRL(*this, "ID_TRANSFERMODE_DEFAULT", wxRadioButton)->SetValue(true);
		XRCCTRL(*this, "ID_LIMITMULTIPLE", wxCheckBox)->SetValue(false);
		XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->SetValue(1);

		XRCCTRL(*this, "ID_CHARSET_AUTO", wxRadioButton)->SetValue(true);
		XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->ChangeValue(wxString());
#ifdef __WXGTK__
		XRCCTRL(*this, "wxID_OK", wxButton)->SetDefault();
#endif
	}
	else if (data->m_type == CSiteManagerItemData::SITE) {
		m_pNotebook_Site->Show();
		m_pNotebook_Bookmark->Hide();
		m_pNotebook_Site->GetContainingSizer()->Layout();

		CSiteManagerItemData_Site* site_data = (CSiteManagerItemData_Site *)data;

		// Set the control states according if it's possible to use the control
		XRCCTRL(*this, "ID_RENAME", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_DELETE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_COPY", wxWindow)->Enable(true);
		m_pNotebook_Site->Enable(true);
		XRCCTRL(*this, "ID_NEWFOLDER", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWSITE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWBOOKMARK", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_CONNECT", wxWindow)->Enable(true);

		XRCCTRL(*this, "ID_HOST", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(site_data->m_server.FormatHost(true));
		unsigned int port = site_data->m_server.GetPort();

		if (port != CServer::GetDefaultPort(site_data->m_server.GetProtocol()))
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString::Format(_T("%d"), port));
		else
			XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString());
		XRCCTRL(*this, "ID_PORT", wxWindow)->Enable(!predefined);

		SetProtocol(site_data->m_server.GetProtocol());
		XRCCTRL(*this, "ID_PROTOCOL", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_ENCRYPTION", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_BYPASSPROXY", wxCheckBox)->SetValue(site_data->m_server.GetBypassProxy());

		XRCCTRL(*this, "ID_USER", wxTextCtrl)->Enable(!predefined && site_data->m_server.GetLogonType() != ANONYMOUS);
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->Enable(!predefined && (site_data->m_server.GetLogonType() == NORMAL || site_data->m_server.GetLogonType() == ACCOUNT));
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->Enable(!predefined && site_data->m_server.GetLogonType() == ACCOUNT);

		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(CServer::GetNameFromLogonType(site_data->m_server.GetLogonType()));
		XRCCTRL(*this, "ID_LOGONTYPE", wxWindow)->Enable(!predefined);

		XRCCTRL(*this, "ID_USER", wxTextCtrl)->ChangeValue(site_data->m_server.GetUser());
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->ChangeValue(site_data->m_server.GetAccount());
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(site_data->m_server.GetPass());
		XRCCTRL(*this, "ID_COMMENTS", wxTextCtrl)->ChangeValue(site_data->m_comments);
		XRCCTRL(*this, "ID_COMMENTS", wxWindow)->Enable(!predefined);

		XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->SetSelection(site_data->m_server.GetType());
		XRCCTRL(*this, "ID_SERVERTYPE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->ChangeValue(site_data->m_localDir);
		XRCCTRL(*this, "ID_LOCALDIR", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->ChangeValue(site_data->m_remoteDir.GetPath());
		XRCCTRL(*this, "ID_REMOTEDIR", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_SYNC", wxCheckBox)->Enable(!predefined);
		XRCCTRL(*this, "ID_SYNC", wxCheckBox)->SetValue(site_data->m_sync);
		XRCCTRL(*this, "ID_TIMEZONE_HOURS", wxSpinCtrl)->SetValue(site_data->m_server.GetTimezoneOffset() / 60);
		XRCCTRL(*this, "ID_TIMEZONE_HOURS", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_TIMEZONE_MINUTES", wxSpinCtrl)->SetValue(site_data->m_server.GetTimezoneOffset() % 60);
		XRCCTRL(*this, "ID_TIMEZONE_MINUTES", wxWindow)->Enable(!predefined);

		enum PasvMode pasvMode = site_data->m_server.GetPasvMode();
		if (pasvMode == MODE_ACTIVE)
			XRCCTRL(*this, "ID_TRANSFERMODE_ACTIVE", wxRadioButton)->SetValue(true);
		else if (pasvMode == MODE_PASSIVE)
			XRCCTRL(*this, "ID_TRANSFERMODE_PASSIVE", wxRadioButton)->SetValue(true);
		else
			XRCCTRL(*this, "ID_TRANSFERMODE_DEFAULT", wxRadioButton)->SetValue(true);
		XRCCTRL(*this, "ID_TRANSFERMODE_ACTIVE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_TRANSFERMODE_PASSIVE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_TRANSFERMODE_DEFAULT", wxWindow)->Enable(!predefined);

		int maxMultiple = site_data->m_server.MaximumMultipleConnections();
		XRCCTRL(*this, "ID_LIMITMULTIPLE", wxCheckBox)->SetValue(maxMultiple != 0);
		XRCCTRL(*this, "ID_LIMITMULTIPLE", wxWindow)->Enable(!predefined);
		if (maxMultiple != 0)
		{
			XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->Enable(!predefined);
			XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->SetValue(maxMultiple);
		}
		else
		{
			XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->Enable(false);
			XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->SetValue(1);
		}

		switch (site_data->m_server.GetEncodingType())
		{
		default:
		case ENCODING_AUTO:
			XRCCTRL(*this, "ID_CHARSET_AUTO", wxRadioButton)->SetValue(true);
			break;
		case ENCODING_UTF8:
			XRCCTRL(*this, "ID_CHARSET_UTF8", wxRadioButton)->SetValue(true);
			break;
		case ENCODING_CUSTOM:
			XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->SetValue(true);
			break;
		}
		XRCCTRL(*this, "ID_CHARSET_AUTO", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_CHARSET_UTF8", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->Enable(!predefined && site_data->m_server.GetEncodingType() == ENCODING_CUSTOM);
		XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->ChangeValue(site_data->m_server.GetCustomEncoding());
#ifdef __WXGTK__
		XRCCTRL(*this, "ID_CONNECT", wxButton)->SetDefault();
#endif
	}
	else
	{
		m_pNotebook_Site->Hide();
		m_pNotebook_Bookmark->Show();
		m_pNotebook_Site->GetContainingSizer()->Layout();

		// Set the control states according if it's possible to use the control
		XRCCTRL(*this, "ID_RENAME", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_DELETE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_COPY", wxWindow)->Enable(true);
		XRCCTRL(*this, "ID_NEWFOLDER", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWSITE", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_NEWBOOKMARK", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_CONNECT", wxWindow)->Enable(true);

		XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->ChangeValue(data->m_localDir);
		XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxWindow)->Enable(!predefined);
		XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxTextCtrl)->ChangeValue(data->m_remoteDir.GetPath());
		XRCCTRL(*this, "ID_BOOKMARK_REMOTEDIR", wxWindow)->Enable(!predefined);

		XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->Enable(true);
		XRCCTRL(*this, "ID_BOOKMARK_SYNC", wxCheckBox)->SetValue(data->m_sync);
	}
#ifdef __WXGTK__
	if (pFocus && !pFocus->IsEnabled())
	{
		for (wxWindow* pParent = pFocus->GetParent(); pParent; pParent = pParent->GetParent())
		{
			if (pParent == this)
			{
				XRCCTRL(*this, "wxID_OK", wxButton)->SetFocus();
				break;
			}
		}
	}
#endif
}

void CSiteManagerDialog::OnCharsetChange(wxCommandEvent& event)
{
	bool checked = XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->GetValue();
	XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->Enable(checked);
}

void CSiteManagerDialog::OnProtocolSelChanged(wxCommandEvent& event)
{
	wxChoice* pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);
	wxStaticText* pEncryptionDesc = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxStaticText);

	pEncryption->Show(pProtocol->GetSelection() != 1);
	pEncryptionDesc->Show(pProtocol->GetSelection() != 1);
}

void CSiteManagerDialog::OnCopySite(wxCommandEvent& event)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data)
		return;

	if (!Verify())
		return;

	if (!UpdateItem())
		return;

	wxTreeItemId parent;
	if (IsPredefinedItem(item))
		parent = m_ownSites;
	else
		parent = pTree->GetItemParent(item);

	const wxString name = pTree->GetItemText(item);
	wxString newName = wxString::Format(_("Copy of %s"), name);
	int index = 2;
	for (;;) {
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = pTree->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk()) {
			wxString name = pTree->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp) {
				found = true;
				break;
			}

			child = pTree->GetNextChild(parent, cookie);
		}
		if (!found)
			break;

		newName = wxString::Format(_("Copy (%d) of %s"), ++index, name);
	}

	wxTreeItemId newItem;
	if (data->m_type == CSiteManagerItemData::SITE) {
		CSiteManagerItemData_Site* newData = new CSiteManagerItemData_Site(*(CSiteManagerItemData_Site *)data);
		newData->connected_item = -1;
		newItem = pTree->AppendItem(parent, newName, 2, 2, newData);

		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = pTree->GetFirstChild(item, cookie); child.IsOk(); child = pTree->GetNextChild(item, cookie))
		{
			CSiteManagerItemData* pData = new CSiteManagerItemData(*(CSiteManagerItemData *)pTree->GetItemData(child));
			pTree->AppendItem(newItem, pTree->GetItemText(child), 3, 3, pData);
		}
		if (pTree->IsExpanded(item))
			pTree->Expand(newItem);
	}
	else {
		CSiteManagerItemData* newData = new CSiteManagerItemData(*data);
		newItem = pTree->AppendItem(parent, newName, 3, 3, newData);
	}
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

bool CSiteManagerDialog::LoadDefaultSites()
{
	CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
	if (defaultsDir.empty())
		return false;

	CXmlFile file(defaultsDir.GetPath() + _T("fzdefaults.xml"));

	TiXmlElement* pDocument = file.Load();
	if (!pDocument)
		return false;

	TiXmlElement* pElement = pDocument->FirstChildElement("Servers");
	if (!pElement)
		return false;

	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return false;

	int style = pTree->GetWindowStyle();
	pTree->SetWindowStyle(style | wxTR_HIDE_ROOT);
	wxTreeItemId root = pTree->AddRoot(wxString(), 0, 0);

	m_predefinedSites = pTree->AppendItem(root, _("Predefined Sites"), 0, 0);
	pTree->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_Expanded);
	pTree->SetItemImage(m_predefinedSites, 1, wxTreeItemIcon_SelectedExpanded);

	wxString lastSelection = COptions::Get()->GetOption(OPTION_SITEMANAGER_LASTSELECTED);
	if (!lastSelection.empty() && lastSelection[0] == '1') {
		if (lastSelection == _T("1"))
			pTree->SafeSelectItem(m_predefinedSites);
		else
			lastSelection = lastSelection.Mid(1);
	}
	else
		lastSelection.clear();
	CSiteManagerXmlHandler_Tree handler(pTree, m_predefinedSites, lastSelection, true);

	CSiteManager::Load(pElement, handler);

	return true;
}

bool CSiteManagerDialog::IsPredefinedItem(wxTreeItemId item)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	wxASSERT(pTree);
	if (!pTree)
		return false;

	while (item)
	{
		if (item == m_predefinedSites)
			return true;
		item = pTree->GetItemParent(item);
	}

	return false;
}

void CSiteManagerDialog::OnBeginDrag(wxTreeEvent& event)
{
	if (!Verify())
	{
		event.Veto();
		return;
	}
	UpdateItem();

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
	{
		event.Veto();
		return;
	}

	wxTreeItemId item = event.GetItem();
	if (!item.IsOk())
	{
		event.Veto();
		return;
	}

	const bool predefined = IsPredefinedItem(item);
	const bool root = item == pTree->GetRootItem() || item == m_ownSites;
	if (root)
	{
		event.Veto();
		return;
	}

	CSiteManagerDialogDataObject obj;

	wxDropSource source(this);
	source.SetData(obj);

	m_dropSource = item;

	source.DoDragDrop(predefined ? wxDrag_CopyOnly : wxDrag_DefaultMove);

	m_dropSource = wxTreeItemId();

	SetCtrlState();
}

struct itempair
{
	wxTreeItemId source;
	wxTreeItemId target;
};

bool CSiteManagerDialog::MoveItems(wxTreeItemId source, wxTreeItemId target, bool copy)
{
	if (source == target)
		return false;

	if (IsPredefinedItem(target))
		return false;

	if (IsPredefinedItem(source) && !copy)
		return false;

	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);

	CSiteManagerItemData *pTargetData = (CSiteManagerItemData *)pTree->GetItemData(target);
	CSiteManagerItemData *pSourceData = (CSiteManagerItemData *)pTree->GetItemData(source);
	if (pTargetData)
	{
		if (pTargetData->m_type == CSiteManagerItemData::BOOKMARK)
			return false;
		if (!pSourceData || pSourceData->m_type != CSiteManagerItemData::BOOKMARK)
			return false;
	}
	else if (pSourceData && pSourceData->m_type == CSiteManagerItemData::BOOKMARK)
		return false;

	wxTreeItemId item = target;
	while (item != pTree->GetRootItem())
	{
		if (item == source)
			return false;
		item = pTree->GetItemParent(item);
	}

	if (!copy && pTree->GetItemParent(source) == target)
		return false;

	wxString sourceName = pTree->GetItemText(source);

	wxTreeItemId child;
	wxTreeItemIdValue cookie;
	child = pTree->GetFirstChild(target, cookie);

	while (child.IsOk())
	{
		wxString childName = pTree->GetItemText(child);

		if (!sourceName.CmpNoCase(childName))
		{
			wxMessageBoxEx(_("An item with the same name as the dragged item already exists at the target location."), _("Failed to copy or move sites"), wxICON_INFORMATION);
			return false;
		}

		child = pTree->GetNextChild(target, cookie);
	}

	std::list<itempair> work;
	itempair pair;
	pair.source = source;
	pair.target = target;
	work.push_back(pair);

	std::list<wxTreeItemId> expand;

	while (!work.empty())
	{
		itempair pair = work.front();
		work.pop_front();

		wxString name = pTree->GetItemText(pair.source);

		CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(pair.source));

		wxTreeItemId newItem = pTree->AppendItem(pair.target, name, data ? 2 : 0);
		if (!data)
		{
			pTree->SetItemImage(newItem, 1, wxTreeItemIcon_Expanded);
			pTree->SetItemImage(newItem, 1, wxTreeItemIcon_SelectedExpanded);

			if (pTree->IsExpanded(pair.source))
				expand.push_back(newItem);
		}
		else if (data->m_type == CSiteManagerItemData::SITE)
		{
			CSiteManagerItemData_Site* newData = new CSiteManagerItemData_Site(*(CSiteManagerItemData_Site *)data);
			newData->connected_item = -1;
			pTree->SetItemData(newItem, newData);
		}
		else
		{
			pTree->SetItemImage(newItem, 3, wxTreeItemIcon_Normal);
			pTree->SetItemImage(newItem, 3, wxTreeItemIcon_Selected);

			CSiteManagerItemData* newData = new CSiteManagerItemData(*data);
			pTree->SetItemData(newItem, newData);
		}

		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = pTree->GetFirstChild(pair.source, cookie);
		while (child.IsOk())
		{
			itempair newPair;
			newPair.source = child;
			newPair.target = newItem;
			work.push_back(newPair);

			child = pTree->GetNextChild(pair.source, cookie);
		}

		pTree->SortChildren(pair.target);
	}

	if (!copy)
	{
		wxTreeItemId parent = pTree->GetItemParent(source);
		if (pTree->GetChildrenCount(parent) == 1)
			pTree->Collapse(parent);

		pTree->Delete(source);
	}

	for (auto iter = expand.begin(); iter != expand.end(); ++iter)
		pTree->Expand(*iter);

	pTree->Expand(target);

	return true;
}

void CSiteManagerDialog::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() != WXK_F2)
	{
		event.Skip();
		return;
	}

	wxCommandEvent cmdEvent;
	OnRename(cmdEvent);
}

void CSiteManagerDialog::CopyAddServer(const CServer& server)
{
	if (!Verify())
		return;

	AddNewSite(m_ownSites, server, true);
}

wxString CSiteManagerDialog::FindFirstFreeName(const wxTreeItemId &parent, const wxString& name)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	wxASSERT(pTree);

	wxString newName = name;
	int index = 2;
	for (;;)
	{
		wxTreeItemId child;
		wxTreeItemIdValue cookie;
		child = pTree->GetFirstChild(parent, cookie);
		bool found = false;
		while (child.IsOk())
		{
			wxString name = pTree->GetItemText(child);
			int cmp = name.CmpNoCase(newName);
			if (!cmp)
			{
				found = true;
				break;
			}

			child = pTree->GetNextChild(parent, cookie);
		}
		if (!found)
			break;

		newName = name + wxString::Format(_T(" %d"), ++index);
	}

	return newName;
}

void CSiteManagerDialog::AddNewSite(wxTreeItemId parent, const CServer& server, bool connected /*=false*/)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxString name = FindFirstFreeName(parent, _("New site"));

	CSiteManagerItemData_Site* pData = new CSiteManagerItemData_Site(server);
	if (connected)
		pData->connected_item = 0;

	wxTreeItemId newItem = pTree->AppendItem(parent, name, 2, 2, pData);
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
#ifdef __WXMAC__
	// Need to trigger dirty processing of generic tree control.
	// Else edit control will be hidden behind item
	pTree->OnInternalIdle();
#endif
	pTree->EditLabel(newItem);
}

void CSiteManagerDialog::AddNewBookmark(wxTreeItemId parent)
{
	wxTreeCtrlEx *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrlEx);
	if (!pTree)
		return;

	wxString name = FindFirstFreeName(parent, _("New bookmark"));

	wxTreeItemId newItem = pTree->AppendItem(parent, name, 3, 3, new CSiteManagerItemData(CSiteManagerItemData::BOOKMARK));
	pTree->SortChildren(parent);
	pTree->EnsureVisible(newItem);
	pTree->SafeSelectItem(newItem);
	pTree->EditLabel(newItem);
}

void CSiteManagerDialog::RememberLastSelected()
{
	COptions::Get()->SetOption(OPTION_SITEMANAGER_LASTSELECTED, GetSitePath(false));
}

void CSiteManagerDialog::OnContextMenu(wxTreeEvent& event)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_SITEMANAGER"));
	if (!pMenu)
		return;

	m_contextMenuItem = event.GetItem();

	PopupMenu(pMenu);
	delete pMenu;
}

void CSiteManagerDialog::OnExportSelected(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Select file for exported sites"), wxString(),
					_T("sites.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if (dlg.ShowModal() != wxID_OK)
		return;

	CXmlFile xml(dlg.GetPath());

	TiXmlElement* exportRoot = xml.CreateEmpty();

	TiXmlElement* pServers = exportRoot->LinkEndChild(new TiXmlElement("Servers"))->ToElement();
	SaveChild(pServers, m_contextMenuItem);

	if (!xml.Save(false)) {
		wxString msg = wxString::Format(_("Could not write \"%s\", the selected sites could not be exported: %s"), xml.GetFileName(), xml.GetError());
		wxMessageBoxEx(msg, _("Error writing xml file"), wxICON_ERROR);
	}
}

void CSiteManagerDialog::OnBookmarkBrowse(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return;

	CSiteManagerItemData* data = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!data || data->m_type != CSiteManagerItemData::BOOKMARK)
		return;

	wxDirDialog dlg(this, _("Choose the local directory"), XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() != wxID_OK)
		return;

	XRCCTRL(*this, "ID_BOOKMARK_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
}

void CSiteManagerDialog::OnNewBookmark(wxCommandEvent&)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return;

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk() || IsPredefinedItem(item))
		return;

	CSiteManagerItemData *pData = (CSiteManagerItemData *)pTree->GetItemData(item);
	if (!pData)
		return;
	if (pData->m_type == CSiteManagerItemData::BOOKMARK)
		item = pTree->GetItemParent(item);

	if (!Verify())
		return;

	AddNewBookmark(item);
}

wxString CSiteManagerDialog::GetSitePath(wxTreeItemId item, bool stripBookmark)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return wxString();

	CSiteManagerItemData* pData = static_cast<CSiteManagerItemData* >(pTree->GetItemData(item));
	if (!pData)
		return wxString();

	if (stripBookmark && pData->m_type == CSiteManagerItemData::BOOKMARK)
		item = pTree->GetItemParent(item);

	wxString path;
	while (item)
	{
		if (item == m_predefinedSites)
			return _T("1") + path;
		else if (item == m_ownSites)
			return _T("0") + path;
		path = _T("/") + CSiteManager::EscapeSegment(pTree->GetItemText(item)) + path;

		item = pTree->GetItemParent(item);
	}

	return _T("0") + path;
}

wxString CSiteManagerDialog::GetSitePath(bool stripBookmark)
{
	wxTreeCtrl *pTree = XRCCTRL(*this, "ID_SITETREE", wxTreeCtrl);
	if (!pTree)
		return wxString();

	wxTreeItemId item = pTree->GetSelection();
	if (!item.IsOk())
		return wxString();

	return GetSitePath(item, stripBookmark);
}

void CSiteManagerDialog::SetProtocol(ServerProtocol protocol)
{
	wxChoice* pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);
	wxStaticText* pEncryptionDesc = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxStaticText);

	if (protocol == SFTP) {
		pEncryption->Hide();
		pEncryptionDesc->Hide();
		pProtocol->SetSelection(1);
	}
	else {
		switch (protocol) {
		default:
		case FTP:
			pEncryption->SetSelection(0);
			break;
		case FTPES:
			pEncryption->SetSelection(1);
			break;
		case FTPS:
			pEncryption->SetSelection(2);
			break;
		case INSECURE_FTP:
			pEncryption->SetSelection(3);
			break;
		}
		pEncryption->Show();
		pEncryptionDesc->Show();
		pProtocol->SetSelection(0);
	}
}


ServerProtocol CSiteManagerDialog::GetProtocol() const
{
	wxChoice* pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);

	if (pProtocol->GetSelection() == 1)
		return SFTP;

	switch (pEncryption->GetSelection())
	{
	default:
	case 0:
		return FTP;
	case 1:
		return FTPES;
	case 2:
		return FTPS;
	case 3:
		return INSECURE_FTP;
	}
}
