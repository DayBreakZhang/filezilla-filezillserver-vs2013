#include <filezilla.h>

#define FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION

#include "RemoteListView.h"
#include "commandqueue.h"
#include "queue.h"
#include "filezillaapp.h"
#include "inputdialog.h"
#include "chmoddialog.h"
#include "filter.h"
#include <algorithm>
#include <wx/dcclient.h>
#include <wx/dnd.h>
#include "dndobjects.h"
#include "Options.h"
#include "recursive_operation.h"
#include "edithandler.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include <wx/clipbrd.h>
#include "sizeformatting.h"
#include "timeformatting.h"
#ifdef __WXMSW__
#include "shellapi.h"
#include "commctrl.h"
#endif

class CRemoteListViewDropTarget : public CScrollableDropTarget<wxListCtrlEx>
{
public:
	CRemoteListViewDropTarget(CRemoteListView* pRemoteListView)
		: CScrollableDropTarget<wxListCtrlEx>(pRemoteListView)
		, m_pRemoteListView(pRemoteListView),
		  m_pFileDataObject(new wxFileDataObject()),
		  m_pRemoteDataObject(new CRemoteDataObject()),
		  m_pDataObject(new wxDataObjectComposite())
	{
		m_pDataObject->Add(m_pRemoteDataObject, true);
		m_pDataObject->Add(m_pFileDataObject);
		SetDataObject(m_pDataObject);
	}

	void ClearDropHighlight()
	{
		const int dropTarget = m_pRemoteListView->m_dropTarget;
		if (dropTarget != -1)
		{
			m_pRemoteListView->m_dropTarget = -1;
#ifdef __WXMSW__
			m_pRemoteListView->SetItemState(dropTarget, 0, wxLIST_STATE_DROPHILITED);
#else
			m_pRemoteListView->RefreshItem(dropTarget);
#endif
		}
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = FixupDragResult(def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
			return def;

		if (!m_pRemoteListView->m_pDirectoryListing)
			return wxDragError;

		if (!GetData())
			return wxDragError;

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager)
			pDragDropManager->pDropTarget = m_pRemoteListView;

		if (m_pDataObject->GetReceivedFormat() == m_pFileDataObject->GetFormat())
		{
			wxString subdir;
			int flags = 0;
			int hit = m_pRemoteListView->HitTest(wxPoint(x, y), flags, 0);
			if (hit != -1 && (flags & wxLIST_HITTEST_ONITEM))
			{
				int index = m_pRemoteListView->GetItemIndex(hit);
				if (index != -1 && m_pRemoteListView->m_fileData[index].comparison_flags != CComparableListing::fill)
				{
					if (index == (int)m_pRemoteListView->m_pDirectoryListing->GetCount())
						subdir = _T("..");
					else if ((*m_pRemoteListView->m_pDirectoryListing)[index].is_dir())
						subdir = (*m_pRemoteListView->m_pDirectoryListing)[index].name;
				}
			}

			m_pRemoteListView->m_pState->UploadDroppedFiles(m_pFileDataObject, subdir, false);
			return wxDragCopy;
		}

		// At this point it's the remote data object.

		if (m_pRemoteDataObject->GetProcessId() != (int)wxGetProcessId())
		{
			wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
			return wxDragNone;
		}

		if (!m_pRemoteDataObject->GetServer().EqualsNoPass(*m_pRemoteListView->m_pState->GetServer()))
		{
			wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
			return wxDragNone;
		}

		// Find drop directory (if it exists)
		wxString subdir;
		int flags = 0;
		int hit = m_pRemoteListView->HitTest(wxPoint(x, y), flags, 0);
		if (hit != -1 && (flags & wxLIST_HITTEST_ONITEM))
		{
			int index = m_pRemoteListView->GetItemIndex(hit);
			if (index != -1 && m_pRemoteListView->m_fileData[index].comparison_flags != CComparableListing::fill)
			{
				if (index == (int)m_pRemoteListView->m_pDirectoryListing->GetCount())
					subdir = _T("..");
				else if ((*m_pRemoteListView->m_pDirectoryListing)[index].is_dir())
					subdir = (*m_pRemoteListView->m_pDirectoryListing)[index].name;
			}
		}

		// Get target path
		CServerPath target = m_pRemoteListView->m_pDirectoryListing->path;
		if (subdir == _T(".."))
		{
			if (target.HasParent())
				target = target.GetParent();
		}
		else if (!subdir.empty())
			target.AddSegment(subdir);

		// Make sure target path is valid
		if (target == m_pRemoteDataObject->GetServerPath())
		{
			wxMessageBoxEx(_("Source and target of the drop operation are identical"));
			return wxDragNone;
		}

		const std::list<CRemoteDataObject::t_fileInfo>& files = m_pRemoteDataObject->GetFiles();
		for (std::list<CRemoteDataObject::t_fileInfo>::const_iterator iter = files.begin(); iter != files.end(); ++iter)
		{
			const CRemoteDataObject::t_fileInfo& info = *iter;
			if (info.dir)
			{
				CServerPath dir = m_pRemoteDataObject->GetServerPath();
				dir.AddSegment(info.name);
				if (dir == target)
					return wxDragNone;
				else if (dir.IsParentOf(target, false))
				{
					wxMessageBoxEx(_("A directory cannot be dragged into one of its subdirectories."));
					return wxDragNone;
				}
			}
		}

		for (std::list<CRemoteDataObject::t_fileInfo>::const_iterator iter = files.begin(); iter != files.end(); ++iter)
		{
			const CRemoteDataObject::t_fileInfo& info = *iter;
			m_pRemoteListView->m_pState->m_pCommandQueue->ProcessCommand(
				new CRenameCommand(m_pRemoteDataObject->GetServerPath(), info.name, target, info.name)
				);
		}

		// Refresh remote listing
		m_pRemoteListView->m_pState->m_pCommandQueue->ProcessCommand(
			new CListCommand()
			);

		return wxDragNone;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CScrollableDropTarget<wxListCtrlEx>::OnDrop(x, y);
		ClearDropHighlight();

		if (!m_pRemoteListView->m_pDirectoryListing)
			return false;

		return true;
	}

	virtual int DisplayDropHighlight(wxPoint point)
	{
		DoDisplayDropHighlight(point);
		return -1;
	}

	int DoDisplayDropHighlight(wxPoint point)
	{
		int flags;
		int hit = m_pRemoteListView->HitTest(point, flags, 0);
		if (!(flags & wxLIST_HITTEST_ONITEM))
			hit = -1;

		if (hit != -1)
		{
			int index = m_pRemoteListView->GetItemIndex(hit);
			if (index == -1 || m_pRemoteListView->m_fileData[index].comparison_flags == CComparableListing::fill)
				hit = -1;
			else if (index != (int)m_pRemoteListView->m_pDirectoryListing->GetCount())
			{
				if (!(*m_pRemoteListView->m_pDirectoryListing)[index].is_dir())
					hit = -1;
				else
				{
					const CDragDropManager* pDragDropManager = CDragDropManager::Get();
					if (pDragDropManager && pDragDropManager->pDragSource == m_pRemoteListView)
					{
						if (m_pRemoteListView->GetItemState(hit, wxLIST_STATE_SELECTED))
							hit = -1;
					}
				}
			}
		}
		if (hit != m_pRemoteListView->m_dropTarget)
		{
			ClearDropHighlight();
			if (hit != -1)
			{
				m_pRemoteListView->m_dropTarget = hit;
#ifdef __WXMSW__
				m_pRemoteListView->SetItemState(hit, wxLIST_STATE_DROPHILITED, wxLIST_STATE_DROPHILITED);
#else
				m_pRemoteListView->RefreshItem(hit);
#endif
			}
		}
		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			ClearDropHighlight();
			return def;
		}

		if (!m_pRemoteListView->m_pDirectoryListing) {
			ClearDropHighlight();
			return wxDragNone;
		}

		const CServer* const pServer = m_pRemoteListView->m_pState->GetServer();
		wxASSERT(pServer);

		int hit = DoDisplayDropHighlight(wxPoint(x, y));
		const CDragDropManager* pDragDropManager = CDragDropManager::Get();

		if (hit == -1 && pDragDropManager &&
			pDragDropManager->remoteParent == m_pRemoteListView->m_pDirectoryListing->path &&
			*pServer == pDragDropManager->server)
			return wxDragNone;

		return wxDragCopy;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget<wxListCtrlEx>::OnLeave();
		ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

protected:
	CRemoteListView *m_pRemoteListView;
	wxFileDataObject* m_pFileDataObject;
	CRemoteDataObject* m_pRemoteDataObject;

	wxDataObjectComposite* m_pDataObject;
};

class CInfoText : public wxWindow
{
public:
	CInfoText(wxWindow* parent, const wxString& text)
		: wxWindow(parent, wxID_ANY, wxPoint(0, 60), wxDefaultSize),
		m_text(text)
	{
		SetForegroundColour(parent->GetForegroundColour());
		SetBackgroundColour(parent->GetBackgroundColour());
		GetTextExtent(m_text, &m_textSize.x, &m_textSize.y);
	}

	void SetText(const wxString &text)
	{
		if (text == m_text)
			return;

		m_text = text;

		GetTextExtent(m_text, &m_textSize.x, &m_textSize.y);
	}

	wxSize GetTextSize() const { return m_textSize; }

	bool AcceptsFocus() const { return false; }
protected:
	wxString m_text;

	void OnPaint(wxPaintEvent&)
	{
		wxPaintDC paintDc(this);

		paintDc.SetFont(GetFont());
		paintDc.SetTextForeground(GetForegroundColour());

		paintDc.DrawText(m_text, 0, 0);
	};

	wxSize m_textSize;

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(CInfoText, wxWindow)
EVT_PAINT(CInfoText::OnPaint)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CRemoteListView, CFileListCtrl<CGenericFileData>)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, CRemoteListView::OnItemActivated)
	EVT_CONTEXT_MENU(CRemoteListView::OnContextMenu)
	// Map both ID_DOWNLOAD and ID_ADDTOQUEUE to OnMenuDownload, code is identical
	EVT_MENU(XRCID("ID_DOWNLOAD"), CRemoteListView::OnMenuDownload)
	EVT_MENU(XRCID("ID_ADDTOQUEUE"), CRemoteListView::OnMenuDownload)
	EVT_MENU(XRCID("ID_MKDIR"), CRemoteListView::OnMenuMkdir)
	EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CRemoteListView::OnMenuMkdirChgDir)
	EVT_MENU(XRCID("ID_NEW_FILE"), CRemoteListView::OnMenuNewfile)
	EVT_MENU(XRCID("ID_DELETE"), CRemoteListView::OnMenuDelete)
	EVT_MENU(XRCID("ID_RENAME"), CRemoteListView::OnMenuRename)
	EVT_MENU(XRCID("ID_CHMOD"), CRemoteListView::OnMenuChmod)
	EVT_KEY_DOWN(CRemoteListView::OnKeyDown)
	EVT_SIZE(CRemoteListView::OnSize)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, CRemoteListView::OnBeginDrag)
	EVT_MENU(XRCID("ID_EDIT"), CRemoteListView::OnMenuEdit)
	EVT_MENU(XRCID("ID_ENTER"), CRemoteListView::OnMenuEnter)
	EVT_MENU(XRCID("ID_GETURL"), CRemoteListView::OnMenuGeturl)
	EVT_MENU(XRCID("ID_CONTEXT_REFRESH"), CRemoteListView::OnMenuRefresh)
END_EVENT_TABLE()

CRemoteListView::CRemoteListView(wxWindow* pParent, CState *pState, CQueueView* pQueue)
	: CFileListCtrl<CGenericFileData>(pParent, pState, pQueue),
	CStateEventHandler(pState)
{
	pState->RegisterHandler(this, STATECHANGE_REMOTE_DIR);
	pState->RegisterHandler(this, STATECHANGE_REMOTE_DIR_MODIFIED);
	pState->RegisterHandler(this, STATECHANGE_APPLYFILTER);
	pState->RegisterHandler(this, STATECHANGE_REMOTE_LINKNOTDIR);

	m_dropTarget = -1;

	m_pInfoText = 0;
	m_pDirectoryListing = 0;

	const unsigned long widths[6] = { 80, 75, 80, 100, 80, 80 };

	AddColumn(_("Filename"), wxLIST_FORMAT_LEFT, widths[0], true);
	AddColumn(_("Filesize"), wxLIST_FORMAT_RIGHT, widths[1]);
	AddColumn(_("Filetype"), wxLIST_FORMAT_LEFT, widths[2]);
	AddColumn(_("Last modified"), wxLIST_FORMAT_LEFT, widths[3]);
	AddColumn(_("Permissions"), wxLIST_FORMAT_LEFT, widths[4]);
	AddColumn(_("Owner/Group"), wxLIST_FORMAT_LEFT, widths[5]);
	LoadColumnSettings(OPTION_REMOTEFILELIST_COLUMN_WIDTHS, OPTION_REMOTEFILELIST_COLUMN_SHOWN, OPTION_REMOTEFILELIST_COLUMN_ORDER);

	InitSort(OPTION_REMOTEFILELIST_SORTORDER);

	m_dirIcon = GetIconIndex(iconType::dir);
	SetImageList(GetSystemImageList(), wxIMAGE_LIST_SMALL);

	InitHeaderSortImageList();

	SetDirectoryListing(0);

	SetDropTarget(new CRemoteListViewDropTarget(this));

	EnablePrefixSearch(true);
}

CRemoteListView::~CRemoteListView()
{
	wxString str = wxString::Format(_T("%d %d"), m_sortDirection, m_sortColumn);
	COptions::Get()->SetOption(OPTION_REMOTEFILELIST_SORTORDER, str);
}

// See comment to OnGetItemText
int CRemoteListView::OnGetItemImage(long item) const
{
	CRemoteListView *pThis = const_cast<CRemoteListView *>(this);
	int index = GetItemIndex(item);
	if (index == -1)
		return -1;

	int &icon = pThis->m_fileData[index].icon;

	if (icon != -2)
		return icon;

	icon = pThis->GetIconIndex(iconType::file, (*m_pDirectoryListing)[index].name, false, (*m_pDirectoryListing)[index].is_dir());
	return icon;
}

int CRemoteListView::GetItemIndex(unsigned int item) const
{
	if (item >= m_indexMapping.size())
		return -1;

	unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size())
		return -1;

	return index;
}

bool CRemoteListView::IsItemValid(unsigned int item) const
{
	if (item >= m_indexMapping.size())
		return false;

	unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size())
		return false;

	return true;
}

void CRemoteListView::UpdateDirectoryListing_Added(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	const unsigned int to_add = pDirectoryListing->GetCount() - m_pDirectoryListing->GetCount();
	m_pDirectoryListing = pDirectoryListing;

	m_indexMapping[0] = pDirectoryListing->GetCount();

	std::list<unsigned int> added;

	CFilterManager filter;
	const wxString path = m_pDirectoryListing->path.GetPath();

	CGenericFileData last = m_fileData.back();
	m_fileData.pop_back();

	for (unsigned int i = pDirectoryListing->GetCount() - to_add; i < pDirectoryListing->GetCount(); i++)
	{
		const CDirentry& entry = (*pDirectoryListing)[i];
		CGenericFileData data;
		if (entry.is_dir())
		{
			data.icon = m_dirIcon;
#ifndef __WXMSW__
			if (entry.is_link())
				data.icon += 3;
#endif
		}
		m_fileData.push_back(data);

		if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time))
			continue;

		if (m_pFilelistStatusBar)
		{
			if (entry.is_dir())
				m_pFilelistStatusBar->AddDirectory();
			else
				m_pFilelistStatusBar->AddFile(entry.size);
		}

		// Find correct position in index mapping
		std::vector<unsigned int>::iterator start = m_indexMapping.begin();
		if (m_hasParent)
			++start;
		CFileListCtrl<CGenericFileData>::CSortComparisonObject compare = GetSortComparisonObject();
		std::vector<unsigned int>::iterator insertPos = std::lower_bound(start, m_indexMapping.end(), i, compare);
		compare.Destroy();

		const int item = insertPos - m_indexMapping.begin();
		m_indexMapping.insert(insertPos, i);

		for (auto iter = added.begin(); iter != added.end(); ++iter)
		{
			unsigned int &pos = *iter;
			if (pos >= (unsigned int)item)
				pos++;
		}
		added.push_back(item);
	}

	m_fileData.push_back(last);

	std::list<bool> selected;
	unsigned int start;
	added.push_back(m_indexMapping.size());
	start = added.front();

	SetItemCount(m_indexMapping.size());

	for (unsigned int i = start; i < m_indexMapping.size(); i++)
	{
		if (i == added.front())
		{
			selected.push_front(false);
			added.pop_front();
		}
		bool is_selected = GetItemState(i, wxLIST_STATE_SELECTED) != 0;
		selected.push_back(is_selected);

		bool should_selected = selected.front();
		selected.pop_front();
		if (is_selected != should_selected)
			SetSelection(i, should_selected);
	}

	if (m_pFilelistStatusBar)
		m_pFilelistStatusBar->SetHidden(m_pDirectoryListing->GetCount() + 1 - m_indexMapping.size());

	wxASSERT(m_indexMapping.size() <= pDirectoryListing->GetCount() + 1);
}

void CRemoteListView::UpdateDirectoryListing_Removed(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	const unsigned int removed = m_pDirectoryListing->GetCount() - pDirectoryListing->GetCount();
	if (!removed)
	{
		m_pDirectoryListing = pDirectoryListing;
		return;
	}
	wxASSERT(!IsComparing());

	std::list<unsigned int> removedItems;

	// Get indexes of the removed items in the listing
	unsigned int j = 0;
	unsigned int i = 0;
	while (i < pDirectoryListing->GetCount() && j < m_pDirectoryListing->GetCount())
	{
		const CDirentry& oldEntry = (*m_pDirectoryListing)[j];
		const wxString& oldName = oldEntry.name;
		const wxString& newName = (*pDirectoryListing)[i].name;
		if (oldName == newName)
		{
			i++;
			j++;
			continue;
		}

		removedItems.push_back(j++);
	}
	for (; j < m_pDirectoryListing->GetCount(); j++)
		removedItems.push_back(j);

	wxASSERT(removedItems.size() == removed);

	std::list<int> selectedItems;

	// Number of items left to remove
	unsigned int toRemove = removed;

	std::list<int> removedIndexes;

	const int size = m_indexMapping.size();
	for (int i = size - 1; i >= 0; i--)
	{
		bool removed = false;

		unsigned int& index = m_indexMapping[i];

		// j is the offset the index has to be adjusted
		int j = 0;
		for (std::list<unsigned int>::const_iterator iter = removedItems.begin(); iter != removedItems.end(); ++iter, ++j)
		{
			if (*iter > index)
				break;

			if (*iter == index)
			{
				removedIndexes.push_back(i);
				removed = true;
				toRemove--;
				break;
			}
		}

		// Get old selection
		bool isSelected = GetItemState(i, wxLIST_STATE_SELECTED) != 0;

		// Update statusbar info
		if (removed && m_pFilelistStatusBar)
		{
			const CDirentry& oldEntry = (*m_pDirectoryListing)[index];
			if (isSelected)
			{
				if (oldEntry.is_dir())
					m_pFilelistStatusBar->UnselectDirectory();
				else
					m_pFilelistStatusBar->UnselectFile(oldEntry.size);
			}
			if (oldEntry.is_dir())
				m_pFilelistStatusBar->RemoveDirectory();
			else
				m_pFilelistStatusBar->RemoveFile(oldEntry.size);
		}

		// Update index
		index -= j;

		// Update selections
		bool needSelection;
		if (selectedItems.empty())
			needSelection = false;
		else if (selectedItems.front() == i)
		{
			needSelection = true;
			selectedItems.pop_front();
		}
		else
			needSelection = false;

		if (isSelected)
		{
			if (!needSelection && (toRemove || removed))
				SetSelection(i, false);

			if (!removed)
				selectedItems.push_back(i - toRemove);
		}
		else if (needSelection)
			SetSelection(i, true);
	}

	// Erase file data
	for (std::list<unsigned int>::reverse_iterator iter = removedItems.rbegin(); iter != removedItems.rend(); ++iter)
	{
		m_fileData.erase(m_fileData.begin() + *iter);
	}

	// Erase indexes
	wxASSERT(!toRemove);
	wxASSERT(removedIndexes.size() == removed);
	for (auto iter = removedIndexes.begin(); iter != removedIndexes.end(); ++iter)
	{
		m_indexMapping.erase(m_indexMapping.begin() + *iter);
	}

	wxASSERT(m_indexMapping.size() == pDirectoryListing->GetCount() + 1);

	m_pDirectoryListing = pDirectoryListing;

	if (m_pFilelistStatusBar)
		m_pFilelistStatusBar->SetHidden(m_pDirectoryListing->GetCount() + 1 - m_indexMapping.size());

	SaveSetItemCount(m_indexMapping.size());
}

bool CRemoteListView::UpdateDirectoryListing(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	wxASSERT(!IsComparing());

	const int unsure = pDirectoryListing->get_unsure_flags() & ~(CDirectoryListing::unsure_unknown);

	if (!unsure)
		return false;

	if (unsure & CDirectoryListing::unsure_invalid)
		return false;

	if (!(unsure & ~(CDirectoryListing::unsure_dir_changed | CDirectoryListing::unsure_file_changed)))
	{
		if (m_sortColumn && m_sortColumn != 2)
		{
			// If not sorted by file or type, changing file attributes can influence
			// sort order.
			return false;
		}

		if (CFilterManager::HasActiveFilters())
			return false;

		wxASSERT(pDirectoryListing->GetCount() == m_pDirectoryListing->GetCount());
		if (pDirectoryListing->GetCount() != m_pDirectoryListing->GetCount())
			return false;

		m_pDirectoryListing = pDirectoryListing;

		// We don't have to do anything
		return true;
	}

	if (unsure & (CDirectoryListing::unsure_dir_added | CDirectoryListing::unsure_file_added))
	{
		if (unsure & (CDirectoryListing::unsure_dir_removed | CDirectoryListing::unsure_file_removed))
			return false; // Cannot handle both at the same time unfortunately
		UpdateDirectoryListing_Added(pDirectoryListing);
		return true;
	}

	wxASSERT(pDirectoryListing->GetCount() <= m_pDirectoryListing->GetCount());
	UpdateDirectoryListing_Removed(pDirectoryListing);
	return true;
}

void CRemoteListView::SetDirectoryListing(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	CancelLabelEdit();

	bool reset = false;
	if (!pDirectoryListing || !m_pDirectoryListing)
		reset = true;
	else if (m_pDirectoryListing->path != pDirectoryListing->path)
		reset = true;
	else if (m_pDirectoryListing->m_firstListTime == pDirectoryListing->m_firstListTime && !IsComparing()
		&& m_pDirectoryListing->GetCount() > 200)
	{
		// Updated directory listing. Check if we can use process it in a different,
		// more efficient way.
		// Makes only sense for big listings though.
		if (UpdateDirectoryListing(pDirectoryListing))
		{
			wxASSERT(GetItemCount() == (int)m_indexMapping.size());
			wxASSERT(GetItemCount() <= (int)m_fileData.size());
			wxASSERT(GetItemCount() == (int)m_fileData.size() || CFilterManager::HasActiveFilters());
			wxASSERT(m_pDirectoryListing->GetCount() + 1 >= (unsigned int)GetItemCount());
			wxASSERT(m_indexMapping[0] == m_pDirectoryListing->GetCount());

			RefreshListOnly();

			return;
		}
	}

	wxString prevFocused;
	std::list<wxString> selectedNames;
	bool ensureVisible = false;
	if (reset)
	{
		ResetSearchPrefix();

		if (IsComparing())
			ExitComparisonMode();

		ClearSelection();

		prevFocused = m_pState->GetPreviouslyVisitedRemoteSubdir();
		ensureVisible = !prevFocused.empty();
	}
	else
	{
		// Remember which items were selected
		selectedNames = RememberSelectedItems(prevFocused);
	}

	if (m_pFilelistStatusBar)
	{
		m_pFilelistStatusBar->UnselectAll();
		m_pFilelistStatusBar->SetConnected(pDirectoryListing != 0);
	}

	m_pDirectoryListing = pDirectoryListing;

	m_fileData.clear();
	m_indexMapping.clear();

	wxLongLong totalSize;
	int unknown_sizes = 0;
	int totalFileCount = 0;
	int totalDirCount = 0;
	int hidden = 0;

	bool eraseBackground = false;
	if (m_pDirectoryListing)
	{
		SetInfoText();

		m_indexMapping.push_back(m_pDirectoryListing->GetCount());

		const wxString path = m_pDirectoryListing->path.GetPath();

		CFilterManager filter;
		for (unsigned int i = 0; i < m_pDirectoryListing->GetCount(); i++)
		{
			const CDirentry& entry = (*m_pDirectoryListing)[i];
			CGenericFileData data;
			if (entry.is_dir())
			{
				data.icon = m_dirIcon;
#ifndef __WXMSW__
				if (entry.is_link())
					data.icon += 3;
#endif
			}
			m_fileData.push_back(data);

			if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time))
			{
				hidden++;
				continue;
			}

			if (entry.is_dir())
				totalDirCount++;
			else
			{
				if (entry.size == -1)
					unknown_sizes++;
				else
					totalSize += entry.size;
				totalFileCount++;
			}

			m_indexMapping.push_back(i);
		}

		CGenericFileData data;
		data.icon = m_dirIcon;
		m_fileData.push_back(data);
	}
	else
	{
		eraseBackground = true;
		SetInfoText();
	}

	if (m_pFilelistStatusBar)
		m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);

	if (m_dropTarget != -1)
	{
		bool resetDropTarget = false;
		int index = GetItemIndex(m_dropTarget);
		if (index == -1)
			resetDropTarget = true;
		else if (index != (int)m_pDirectoryListing->GetCount())
			if (!(*m_pDirectoryListing)[index].is_dir())
				resetDropTarget = true;

		if (resetDropTarget)
		{
			SetItemState(m_dropTarget, 0, wxLIST_STATE_DROPHILITED);
			m_dropTarget = -1;
		}
	}

	if (!IsComparing())
	{
		if ((unsigned int)GetItemCount() > m_indexMapping.size())
			eraseBackground = true;
		if ((unsigned int)GetItemCount() != m_indexMapping.size())
			SetItemCount(m_indexMapping.size());

		if (GetItemCount() && reset)
		{
			EnsureVisible(0);
			SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		}
	}

	SortList(-1, -1, false);

	if (IsComparing()) {
		m_originalIndexMapping.clear();
		RefreshComparison();
		ReselectItems(selectedNames, prevFocused, ensureVisible);
	}
	else {
		ReselectItems(selectedNames, prevFocused, ensureVisible);
		RefreshListOnly(eraseBackground);
	}
}

// Filenames on VMS systems have a revision suffix, e.g.
// foo.bar;1
// foo.bar;2
// foo.bar;3
wxString StripVMSRevision(const wxString& name)
{
	int pos = name.Find(';', true);
	if (pos < 1)
		return name;

	const int len = name.Len();
	if (pos == len - 1)
		return name;

	int p = pos;
	while (++p < len)
	{
		const wxChar& c = name[p];
		if (c < '0' || c > '9')
			return name;
	}

	return name.Left(pos);
}


void CRemoteListView::OnItemActivated(wxListEvent &event)
{
	if (!m_pState->IsRemoteIdle()) {
		wxBell();
		return;
	}

	int count = 0;
	bool back = false;

	int item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill)
			continue;

		count++;

		if (!item)
			back = true;
	}
	if (!count) {
		wxBell();
		return;
	}
	if (count > 1) {
		if (back) {
			wxBell();
			return;
		}

		wxCommandEvent cmdEvent;
		OnMenuDownload(cmdEvent);
		return;
	}

	item = event.GetIndex();

	if (item) {
		int index = GetItemIndex(item);
		if (index == -1)
			return;
		if (m_fileData[index].comparison_flags == fill)
			return;

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		const wxString& name = entry.name;

		const CServer* pServer = m_pState->GetServer();
		if (!pServer) {
			wxBell();
			return;
		}

		if (entry.is_dir()) {
			const int action = COptions::Get()->GetOptionVal(OPTION_DOUBLECLICK_ACTION_DIRECTORY);
			if (action == 3) {
				// No action
				wxBell();
				return;
			}

			if (!action) {
				if (entry.is_link()) {
					m_pLinkResolveState.reset(new t_linkResolveState);
					m_pLinkResolveState->remote_path = m_pDirectoryListing->path;
					m_pLinkResolveState->link = name;
					m_pLinkResolveState->local_path = m_pState->GetLocalDir();
					m_pLinkResolveState->server = *pServer;
				}
				m_pState->ChangeRemoteDir(m_pDirectoryListing->path, name, entry.is_link() ? LIST_FLAG_LINK : 0);
			}
			else {
				wxCommandEvent evt(0, action == 1 ? XRCID("ID_DOWNLOAD") : XRCID("ID_ADDTOQUEUE"));
				OnMenuDownload(evt);
			}
		}
		else {
			const int action = COptions::Get()->GetOptionVal(OPTION_DOUBLECLICK_ACTION_FILE);
			if (action == 3) {
				// No action
				wxBell();
				return;
			}

			if (action == 2) {
				// View / Edit action
				wxCommandEvent evt;
				OnMenuEdit(evt);
				return;
			}

			const bool queue_only = action == 1;

			const CLocalPath local_path = m_pState->GetLocalDir();
			if (!local_path.IsWriteable()) {
				wxBell();
				return;
			}

			wxString localFile = CQueueView::ReplaceInvalidCharacters(name);
			if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
				localFile = StripVMSRevision(localFile);
			m_pQueue->QueueFile(queue_only, true, name,
				(name == localFile) ? wxString() : localFile,
				local_path, m_pDirectoryListing->path, *pServer, entry.size);
			m_pQueue->QueueFile_Finish(true);
		}
	}
	else {
		m_pState->ChangeRemoteDir(m_pDirectoryListing->path, _T(".."));
	}
}

void CRemoteListView::OnMenuEnter(wxCommandEvent &)
{
	if (!m_pState->IsRemoteIdle()) {
		wxBell();
		return;
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	if (item) {
		int index = GetItemIndex(item);
		if (index == -1) {
			wxBell();
			return;
		}
		if (m_fileData[index].comparison_flags == fill) {
			wxBell();
			return;
		}

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		const wxString& name = entry.name;

		const CServer* pServer = m_pState->GetServer();
		if (!pServer) {
			wxBell();
			return;
		}

		if (!entry.is_dir()) {
			wxBell();
			return;
		}

		if (entry.is_link()) {
			m_pLinkResolveState.reset(new t_linkResolveState);
			m_pLinkResolveState->remote_path = m_pDirectoryListing->path;
			m_pLinkResolveState->link = name;
			m_pLinkResolveState->local_path = m_pState->GetLocalDir();
			m_pLinkResolveState->server = *pServer;
		}
		m_pState->ChangeRemoteDir(m_pDirectoryListing->path, name, entry.is_link() ? LIST_FLAG_LINK : 0);
	}
	else {
		m_pState->ChangeRemoteDir(m_pDirectoryListing->path, _T(".."));
	}
}

void CRemoteListView::OnContextMenu(wxContextMenuEvent& event)
{
	if (GetEditControl())
	{
		event.Skip();
		return;
	}

	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_REMOTEFILELIST"));
	if (!pMenu)
		return;

	if (!m_pState->IsRemoteConnected() || !m_pState->IsRemoteIdle())
	{
		pMenu->Delete(XRCID("ID_ENTER"));
		pMenu->Enable(XRCID("ID_DOWNLOAD"), false);
		pMenu->Enable(XRCID("ID_ADDTOQUEUE"), false);
		pMenu->Enable(XRCID("ID_MKDIR"), false);
		pMenu->Enable(XRCID("ID_MKDIR_CHGDIR"), false);
		pMenu->Enable(XRCID("ID_DELETE"), false);
		pMenu->Enable(XRCID("ID_RENAME"), false);
		pMenu->Enable(XRCID("ID_CHMOD"), false);
		pMenu->Enable(XRCID("ID_EDIT"), false);
		pMenu->Enable(XRCID("ID_GETURL"), false);
		pMenu->Enable(XRCID("ID_CONTEXT_REFRESH"), false);
		pMenu->Enable(XRCID("ID_NEW_FILE"), false);
	}
	else if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1)
	{
		pMenu->Delete(XRCID("ID_ENTER"));
		pMenu->Enable(XRCID("ID_DOWNLOAD"), false);
		pMenu->Enable(XRCID("ID_ADDTOQUEUE"), false);
		pMenu->Enable(XRCID("ID_DELETE"), false);
		pMenu->Enable(XRCID("ID_RENAME"), false);
		pMenu->Enable(XRCID("ID_CHMOD"), false);
		pMenu->Enable(XRCID("ID_EDIT"), false);
		pMenu->Enable(XRCID("ID_GETURL"), false);
	}
	else
	{
		if ((GetItemCount() && GetItemState(0, wxLIST_STATE_SELECTED)))
		{
			pMenu->Enable(XRCID("ID_RENAME"), false);
			pMenu->Enable(XRCID("ID_CHMOD"), false);
			pMenu->Enable(XRCID("ID_EDIT"), false);
			pMenu->Enable(XRCID("ID_GETURL"), false);
		}

		int count = 0;
		int fillCount = 0;
		bool selectedDir = false;
		int item = -1;
		while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
		{
			if (!item)
			{
				++count;
				++fillCount;
				continue;
			}

			int index = GetItemIndex(item);
			if (index == -1)
				continue;
			count++;
			if (m_fileData[index].comparison_flags == fill)
			{
				fillCount++;
				continue;
			}
			if ((*m_pDirectoryListing)[index].is_dir())
				selectedDir = true;
		}
		if (!count || fillCount == count)
		{
			pMenu->Delete(XRCID("ID_ENTER"));
			pMenu->Enable(XRCID("ID_DOWNLOAD"), false);
			pMenu->Enable(XRCID("ID_ADDTOQUEUE"), false);
			pMenu->Enable(XRCID("ID_DELETE"), false);
			pMenu->Enable(XRCID("ID_RENAME"), false);
			pMenu->Enable(XRCID("ID_CHMOD"), false);
			pMenu->Enable(XRCID("ID_EDIT"), false);
			pMenu->Enable(XRCID("ID_GETURL"), false);
		}
		else
		{
			if (selectedDir)
				pMenu->Enable(XRCID("ID_EDIT"), false);
			else
				pMenu->Delete(XRCID("ID_ENTER"));
			if (count > 1)
			{
				if (selectedDir)
					pMenu->Delete(XRCID("ID_ENTER"));
				pMenu->Enable(XRCID("ID_RENAME"), false);
			}

			if (!m_pState->GetLocalDir().IsWriteable())
			{
				pMenu->Enable(XRCID("ID_DOWNLOAD"), false);
				pMenu->Enable(XRCID("ID_ADDTOQUEUE"), false);
			}
		}
	}

	PopupMenu(pMenu);
	delete pMenu;
}

void CRemoteListView::OnMenuDownload(wxCommandEvent& event)
{
	// Make sure selection is valid
	bool idle = m_pState->IsRemoteIdle();

	const CLocalPath localDir = m_pState->GetLocalDir();
	if (!localDir.IsWriteable())
	{
		wxBell();
		return;
	}

	long item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		if (!item)
			continue;

		int index = GetItemIndex(item);
		if (index == -1)
			continue;
		if (m_fileData[index].comparison_flags == fill)
			continue;
		if ((*m_pDirectoryListing)[index].is_dir() && !idle)
		{
			wxBell();
			return;
		}
	}

	TransferSelectedFiles(localDir, event.GetId() == XRCID("ID_ADDTOQUEUE"));
}

void CRemoteListView::TransferSelectedFiles(const CLocalPath& local_parent, bool queueOnly)
{
	bool idle = m_pState->IsRemoteIdle();

	CRecursiveOperation* pRecursiveOperation = m_pState->GetRecursiveOperationHandler();
	wxASSERT(pRecursiveOperation);

	wxASSERT(local_parent.IsWriteable());

	const CServer* pServer = m_pState->GetServer();
	if (!pServer)
	{
		wxBell();
		return;
	}

	bool added = false;
	bool startRecursive = false;
	long item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;
		if (!item)
			continue;

		int index = GetItemIndex(item);
		if (index == -1)
			continue;
		if (m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		const wxString& name = entry.name;

		if (entry.is_dir())
		{
			if (!idle)
				continue;
			CLocalPath local_path(local_parent);
			local_path.AddSegment(CQueueView::ReplaceInvalidCharacters(name));
			CServerPath remotePath = m_pDirectoryListing->path;
			if (remotePath.AddSegment(name))
			{
				pRecursiveOperation->AddDirectoryToVisit(m_pDirectoryListing->path, name, local_path, entry.is_link());
				startRecursive = true;
			}
		}
		else
		{
			wxString localFile = CQueueView::ReplaceInvalidCharacters(name);
			if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
				localFile = StripVMSRevision(localFile);
			m_pQueue->QueueFile(queueOnly, true,
				name, (name == localFile) ? wxString() : localFile,
				local_parent, m_pDirectoryListing->path, *pServer, entry.size);
			added = true;
		}
	}
	if (added)
		m_pQueue->QueueFile_Finish(!queueOnly);

	if (startRecursive)
	{
		if (IsComparing())
			ExitComparisonMode();
		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(queueOnly ? CRecursiveOperation::recursive_addtoqueue : CRecursiveOperation::recursive_download, m_pDirectoryListing->path, filter.GetActiveFilters(false));
	}
}

// Create a new Directory
void CRemoteListView::OnMenuMkdir(wxCommandEvent&)
{
	MenuMkdir();
}

// Create a new Directory and enter the new Directory
void CRemoteListView::OnMenuMkdirChgDir(wxCommandEvent&)
{
	CServerPath newdir = MenuMkdir();
	if (!newdir.empty()) {
		m_pState->ChangeRemoteDir(newdir, wxString(), 0, true);
	}
}

// Help-Function to create a new Directory
// Returns the name of the new directory
CServerPath CRemoteListView::MenuMkdir()
{
	if (!m_pDirectoryListing || !m_pState->IsRemoteIdle()) {
		wxBell();
		return CServerPath();
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:")))
		return CServerPath();

	CServerPath path = m_pDirectoryListing->path;

	// Append a long segment which does (most likely) not exist in the path and
	// replace it with "New directory" later. This way we get the exact position of
	// "New directory" and can preselect it in the dialog.
	wxString tmpName = _T("25CF809E56B343b5A12D1F0466E3B37A49A9087FDCF8412AA9AF8D1E849D01CF");
	if (path.AddSegment(tmpName)) {
		wxString pathName = path.GetPath();
		int pos = pathName.Find(tmpName);
		wxASSERT(pos != -1);
		wxString newName = _("New directory");
		pathName.Replace(tmpName, newName);
		dlg.SetValue(pathName);
		dlg.SelectText(pos, pos + newName.Length());
	}

	const CServerPath oldPath = m_pDirectoryListing->path;

	if (dlg.ShowModal() != wxID_OK)
		return CServerPath();

	if (!m_pDirectoryListing || oldPath != m_pDirectoryListing->path ||
		!m_pState->IsRemoteIdle())
	{
		wxBell();
		return CServerPath();
	}

	path = m_pDirectoryListing->path;
	if (!path.ChangePath(dlg.GetValue())) {
		wxBell();
		return CServerPath();
	}

	m_pState->m_pCommandQueue->ProcessCommand(new CMkdirCommand(path));

	// Return name of the New Directory
	return path;
}

void CRemoteListView::OnMenuDelete(wxCommandEvent&)
{
	if (!m_pState->IsRemoteIdle()) {
		wxBell();
		return;
	}

	int count_dirs = 0;
	int count_files = 0;
	bool selected_link = false;

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (!item)
			continue;
		if (item == -1)
			break;

		if (!IsItemValid(item))
		{
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1)
			continue;
		if (m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir())
		{
			count_dirs++;
			if (entry.is_link())
				selected_link = true;
		}
		else
			count_files++;
	}

	wxString question;
	if (!count_dirs)
		question.Printf(wxPLURAL("Really delete %d file from the server?", "Really delete %d files from the server?", count_files), count_files);
	else if (!count_files)
		question.Printf(wxPLURAL("Really delete %d directory with its contents from the server?", "Really delete %d directories with their contents from the server?", count_dirs), count_dirs);
	else
	{
		wxString files = wxString::Format(wxPLURAL("%d file", "%d files", count_files), count_files);
		wxString dirs = wxString::Format(wxPLURAL("%d directory with its contents", "%d directories with their contents", count_dirs), count_dirs);
		question.Printf(_("Really delete %s and %s from the server?"), files, dirs);
	}

	if (wxMessageBoxEx(question, _("Confirmation needed"), wxICON_QUESTION | wxYES_NO, this) != wxYES)
		return;

	bool follow_symlink = false;
	if (selected_link)
	{
		wxDialogEx dlg;
		if (!dlg.Load(this, _T("ID_DELETE_SYMLINK")))
		{
			wxBell();
			return;
		}
		if (dlg.ShowModal() != wxID_OK)
			return;

		follow_symlink = XRCCTRL(dlg, "ID_RECURSE", wxRadioButton)->GetValue();
	}

	CRecursiveOperation* pRecursiveOperation = m_pState->GetRecursiveOperationHandler();
	wxASSERT(pRecursiveOperation);

	std::list<wxString> filesToDelete;

	bool startRecursive = false;
	item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (!item)
			continue;
		if (item == -1)
			break;

		int index = GetItemIndex(item);
		if (index == -1)
			continue;
		if (m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		const wxString& name = entry.name;

		if (entry.is_dir() && (follow_symlink || !entry.is_link()))
		{
			CServerPath remotePath = m_pDirectoryListing->path;
			if (remotePath.AddSegment(name))
			{
				pRecursiveOperation->AddDirectoryToVisit(m_pDirectoryListing->path, name, CLocalPath(), true);
				startRecursive = true;
			}
		}
		else
			filesToDelete.push_back(name);
	}

	if (!filesToDelete.empty())
		m_pState->m_pCommandQueue->ProcessCommand(new CDeleteCommand(m_pDirectoryListing->path, filesToDelete));

	if (startRecursive)
	{
		if (IsComparing())
			ExitComparisonMode();
		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_delete, m_pDirectoryListing->path, filter.GetActiveFilters(false));
	}
}

void CRemoteListView::OnMenuRename(wxCommandEvent&)
{
	if (GetEditControl()) {
		GetEditControl()->SetFocus();
		return;
	}

	if (!m_pState->IsRemoteIdle()) {
		wxBell();
		return;
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item <= 0) {
		wxBell();
		return;
	}

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill) {
		wxBell();
		return;
	}

	EditLabel(item);
}

void CRemoteListView::OnKeyDown(wxKeyEvent& event)
{
#ifdef __WXMAC__
#define CursorModifierKey wxMOD_CMD
#else
#define CursorModifierKey wxMOD_ALT
#endif

	int code = event.GetKeyCode();
	if (code == WXK_DELETE || code == WXK_NUMPAD_DELETE) {
		if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
			wxBell();
			return;
		}

		wxCommandEvent tmp;
		OnMenuDelete(tmp);
		return;
	}
	else if (code == WXK_F2) {
		wxCommandEvent tmp;
		OnMenuRename(tmp);
	}
	else if (code == WXK_RIGHT && event.GetModifiers() == CursorModifierKey) {
		wxListEvent evt;
		evt.m_itemIndex = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
		OnItemActivated(evt);
	}
	else if (code == WXK_DOWN && event.GetModifiers() == CursorModifierKey) {
		wxCommandEvent cmdEvent;
		OnMenuDownload(cmdEvent);
	}
	else if (code == 'N' && event.GetModifiers() == (wxMOD_CONTROL | wxMOD_SHIFT)) {
		MenuMkdir();
	}
	else
		event.Skip();
}

bool CRemoteListView::OnBeginRename(const wxListEvent& event)
{
	if (!m_pState->IsRemoteIdle())
	{
		wxBell();
		return false;
	}

	if (!m_pDirectoryListing)
	{
		wxBell();
		return false;
	}

	int item = event.GetIndex();
	if (!item)
		return false;

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill)
		return false;

	return true;
}

bool CRemoteListView::OnAcceptRename(const wxListEvent& event)
{
	if (!m_pState->IsRemoteIdle())
	{
		wxBell();
		return false;
	}

	if (!m_pDirectoryListing)
	{
		wxBell();
		return false;
	}

	int item = event.GetIndex();
	if (!item)
		return false;

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill)
	{
		wxBell();
		return false;
	}

	const CDirentry& entry = (*m_pDirectoryListing)[index];

	wxString newFile = event.GetLabel();

	CServerPath newPath = m_pDirectoryListing->path;
	if (!newPath.ChangePath(newFile, true))
	{
		wxMessageBoxEx(_("Filename invalid"), _("Cannot rename file"), wxICON_EXCLAMATION);
		return false;
	}

	if (newPath == m_pDirectoryListing->path)
	{
		if (entry.name == newFile)
			return false;

		// Check if target file already exists
		for (unsigned int i = 0; i < m_pDirectoryListing->GetCount(); i++)
		{
			if (newFile == (*m_pDirectoryListing)[i].name)
			{
				if (wxMessageBoxEx(_("Target filename already exists, really continue?"), _("File exists"), wxICON_QUESTION | wxYES_NO) != wxYES)
					return false;

				break;
			}
		}
	}

	m_pState->m_pCommandQueue->ProcessCommand(new CRenameCommand(m_pDirectoryListing->path, entry.name, newPath, newFile));

	return true;
}

void CRemoteListView::OnMenuChmod(wxCommandEvent&)
{
	if (!m_pState->IsRemoteConnected() || !m_pState->IsRemoteIdle())
	{
		wxBell();
		return;
	}

	int fileCount = 0;
	int dirCount = 0;
	wxString name;

	char permissions[9] = {0};

	long item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		if (!item)
			return;

		int index = GetItemIndex(item);
		if (index == -1)
			return;
		if (m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];

		if (entry.is_dir())
			dirCount++;
		else
			fileCount++;
		name = entry.name;

		char file_perms[9];
		if (CChmodDialog::ConvertPermissions(*entry.permissions, file_perms))
		{
			for (int i = 0; i < 9; i++)
			{
				if (!permissions[i] || permissions[i] == file_perms[i])
					permissions[i] = file_perms[i];
				else
					permissions[i] = -1;
			}
		}
	}
	if (!dirCount && !fileCount)
	{
		wxBell();
		return;
	}

	for (int i = 0; i < 9; i++)
		if (permissions[i] == -1)
			permissions[i] = 0;

	CChmodDialog* pChmodDlg = new CChmodDialog;
	if (!pChmodDlg->Create(this, fileCount, dirCount, name, permissions))
	{
		pChmodDlg->Destroy();
		pChmodDlg = 0;
		return;
	}

	if (pChmodDlg->ShowModal() != wxID_OK)
	{
		pChmodDlg->Destroy();
		pChmodDlg = 0;
		return;
	}

	// State may have changed while chmod dialog was shown
	if (!m_pState->IsRemoteConnected() || !m_pState->IsRemoteIdle())
	{
		pChmodDlg->Destroy();
		pChmodDlg = 0;
		wxBell();
		return;
	}

	const int applyType = pChmodDlg->GetApplyType();

	CRecursiveOperation* pRecursiveOperation = m_pState->GetRecursiveOperationHandler();
	wxASSERT(pRecursiveOperation);

	item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		if (!item)
		{
			pChmodDlg->Destroy();
			pChmodDlg = 0;
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1)
		{
			pChmodDlg->Destroy();
			pChmodDlg = 0;
			return;
		}
		if (m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];

		if (!applyType ||
			(!entry.is_dir() && applyType == 1) ||
			(entry.is_dir() && applyType == 2))
		{
			char permissions[9];
			bool res = pChmodDlg->ConvertPermissions(*entry.permissions, permissions);
			wxString newPerms = pChmodDlg->GetPermissions(res ? permissions : 0, entry.is_dir());

			m_pState->m_pCommandQueue->ProcessCommand(new CChmodCommand(m_pDirectoryListing->path, entry.name, newPerms));
		}

		if (pChmodDlg->Recursive() && entry.is_dir())
			pRecursiveOperation->AddDirectoryToVisit(m_pDirectoryListing->path, entry.name);
	}

	if (pChmodDlg->Recursive())
	{
		if (IsComparing())
			ExitComparisonMode();

		pRecursiveOperation->SetChmodDialog(pChmodDlg);
		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_chmod, m_pDirectoryListing->path, filter.GetActiveFilters(false));

		// Refresh listing. This gets done implicitely by the recursive operation, so
		// only it if not recursing.
		if (pRecursiveOperation->GetOperationMode() != CRecursiveOperation::recursive_chmod)
			m_pState->ChangeRemoteDir(m_pDirectoryListing->path);
	}
	else
	{
		pChmodDlg->Destroy();
		m_pState->ChangeRemoteDir(m_pDirectoryListing->path, _T(""), 0, true);
	}

}

void CRemoteListView::ApplyCurrentFilter()
{
	CFilterManager filter;

	if (!filter.HasSameLocalAndRemoteFilters() && IsComparing())
		ExitComparisonMode();

	if (m_fileData.size() <= 1)
		return;

	wxString focused;
	std::list<wxString> selectedNames = RememberSelectedItems(focused);

	if (m_pFilelistStatusBar)
		m_pFilelistStatusBar->UnselectAll();

	wxLongLong totalSize;
	int unknown_sizes = 0;
	int totalFileCount = 0;
	int totalDirCount = 0;
	int hidden = 0;

	const wxString path = m_pDirectoryListing->path.GetPath();

	m_indexMapping.clear();
	const unsigned int count = m_pDirectoryListing->GetCount();
	m_indexMapping.push_back(count);
	for (unsigned int i = 0; i < count; i++)
	{
		const CDirentry& entry = (*m_pDirectoryListing)[i];
		if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time))
		{
			hidden++;
			continue;
		}

		if (entry.is_dir())
			totalDirCount++;
		else
		{
			if (entry.size == -1)
				unknown_sizes++;
			else
				totalSize += entry.size;
			totalFileCount++;
		}

		m_indexMapping.push_back(i);
	}

	if (m_pFilelistStatusBar)
		m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);

	SetItemCount(m_indexMapping.size());

	SortList(-1, -1, false);

	if (IsComparing())
	{
		m_originalIndexMapping.clear();
		RefreshComparison();
	}

	ReselectItems(selectedNames, focused);
	if (!IsComparing())
		RefreshListOnly();
}

std::list<wxString> CRemoteListView::RememberSelectedItems(wxString& focused)
{
	wxASSERT(GetItemCount() == (int)m_indexMapping.size());
	std::list<wxString> selectedNames;
	// Remember which items were selected
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		while (item != -1)
		{
			SetSelection(item, false);
			if (!item)
			{
				selectedNames.push_back(_T(".."));
				item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
				continue;
			}
			int index = GetItemIndex(item);
			if (index == -1 || m_fileData[index].comparison_flags == fill)
			{
				item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
				continue;
			}
			const CDirentry& entry = (*m_pDirectoryListing)[index];

			if (entry.is_dir())
				selectedNames.push_back(_T("d") + entry.name);
			else
				selectedNames.push_back(_T("-") + entry.name);

			item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		}
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
	if (item != -1)
	{
		int index = GetItemIndex(item);
		if (index != -1 && m_fileData[index].comparison_flags != fill)
		{
			if (!item)
				focused = _T("..");
			else
				focused = (*m_pDirectoryListing)[index].name;
		}

		SetItemState(item, 0, wxLIST_STATE_FOCUSED);
	}

	return selectedNames;
}

void CRemoteListView::ReselectItems(std::list<wxString>& selectedNames, wxString focused, bool ensureVisible)
{
	if (!GetItemCount())
		return;

	if (focused == _T(".."))
	{
		focused = _T("");
		SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
	}

	if (selectedNames.empty())
	{
		if (focused.empty())
			return;

		for (unsigned int i = 1; i < m_indexMapping.size(); i++)
		{
			const int index = m_indexMapping[i];
			if (m_fileData[index].comparison_flags == fill)
				continue;

			if ((*m_pDirectoryListing)[index].name == focused)
			{
				SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				if (ensureVisible)
					EnsureVisible(i);
				return;
			}
		}
		return;
	}

	if (selectedNames.front() == _T(".."))
	{
		selectedNames.pop_front();
		SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}

	int firstSelected = -1;

	// Reselect previous items if neccessary.
	// Sorting direction did not change. We just have to scan through items once
	unsigned int i = 0;
	for (std::list<wxString>::const_iterator iter = selectedNames.begin(); iter != selectedNames.end(); ++iter)
	{
		while (++i < m_indexMapping.size())
		{
			int index = GetItemIndex(i);
			if (index == -1 || m_fileData[index].comparison_flags == fill)
				continue;
			const CDirentry& entry = (*m_pDirectoryListing)[index];
			if (entry.name == focused)
			{
				SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				if (ensureVisible)
					EnsureVisible(i);
				focused = _T("");
			}
			if (entry.is_dir() && *iter == (_T("d") + entry.name))
			{
				if (firstSelected == -1)
					firstSelected = i;
				if (m_pFilelistStatusBar)
					m_pFilelistStatusBar->SelectDirectory();
				SetSelection(i, true);
				break;
			}
			else if (*iter == (_T("-") + entry.name))
			{
				if (firstSelected == -1)
					firstSelected = i;
				if (m_pFilelistStatusBar)
					m_pFilelistStatusBar->SelectFile(entry.size);
				SetSelection(i, true);
				break;
			}
		}
		if (i == m_indexMapping.size())
			break;
	}
	if (!focused.empty())
	{
		if (firstSelected != -1)
			SetItemState(firstSelected, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		else
			SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
	}
}

void CRemoteListView::OnSize(wxSizeEvent& event)
{
	event.Skip();
	RepositionInfoText();
}

void CRemoteListView::RepositionInfoText()
{
	if (!m_pInfoText)
		return;

	wxRect rect = GetClientRect();

	wxSize size = m_pInfoText->GetTextSize();

	if (m_indexMapping.empty())
		rect.y = 60;
	else
	{
		wxRect itemRect;
		GetItemRect(0, itemRect);
		rect.y = wxMax(60, itemRect.GetBottom() + 1);
	}
	rect.x = rect.x + (rect.width - size.x) / 2;
	rect.width = size.x;
	rect.height = size.y;

	m_pInfoText->SetSize(rect);
#ifdef __WXMSW__
	if (GetLayoutDirection() != wxLayout_RightToLeft)
	{
		m_pInfoText->Refresh(true);
		m_pInfoText->Update();
	}
	else
#endif
		m_pInfoText->Refresh(false);

}

void CRemoteListView::OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString& data, const void* data2)
{
	wxASSERT(pState);
	if (notification == STATECHANGE_REMOTE_DIR)
		SetDirectoryListing(pState->GetRemoteDir());
	else if (notification == STATECHANGE_REMOTE_DIR_MODIFIED)
		SetDirectoryListing(pState->GetRemoteDir());
	else if (notification == STATECHANGE_REMOTE_LINKNOTDIR)
	{
		wxASSERT(data2);
		LinkIsNotDir(*(CServerPath*)data2, data);
	}
	else
	{
		wxASSERT(notification == STATECHANGE_APPLYFILTER);
		ApplyCurrentFilter();
	}
}

void CRemoteListView::SetInfoText()
{
	wxString text;
	if (!IsComparing())
	{
		if (!m_pDirectoryListing)
			text = _("Not connected to any server");
		else if (m_pDirectoryListing->failed())
			text = _("Directory listing failed");
		else if (!m_pDirectoryListing->GetCount())
			text = _("Empty directory listing");
	}

	if (text.empty())
	{
		delete m_pInfoText;
		m_pInfoText = 0;
		return;
	}

	if (!m_pInfoText)
	{
		m_pInfoText = new CInfoText(this, text);
#ifdef __WXMSW__
		if (GetLayoutDirection() != wxLayout_RightToLeft)
			m_pInfoText->SetDoubleBuffered(true);
#endif

		RepositionInfoText();
		return;
	}

	m_pInfoText->SetText(text);
	RepositionInfoText();
}

void CRemoteListView::OnBeginDrag(wxListEvent&)
{
	if (!m_pState->IsRemoteIdle())
	{
		wxBell();
		return;
	}

	if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1)
	{
		// Nothing selected
		return;
	}

	bool idle = m_pState->m_pCommandQueue->Idle();

	long item = -1;
	int count = 0;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		if (!item)
		{
			// Can't drag ".."
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill)
			continue;
		if ((*m_pDirectoryListing)[index].is_dir() && !idle)
		{
			// Drag could result in recursive operation, don't allow at this point
			wxBell();
			return;
		}
		count++;
	}
	if (!count)
	{
		wxBell();
		return;
	}

	wxDataObjectComposite object;

	const CServer* const pServer = m_pState->GetServer();
	if (!pServer)
		return;
	const CServer server = *pServer;
	const CServerPath path = m_pDirectoryListing->path;

	CRemoteDataObject *pRemoteDataObject = new CRemoteDataObject(*pServer, m_pDirectoryListing->path);

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;
	pDragDropManager->server = server;
	pDragDropManager->remoteParent = m_pDirectoryListing->path;

	// Add files to remote data object
	item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill)
			continue;
		const CDirentry& entry = (*m_pDirectoryListing)[index];

		pRemoteDataObject->AddFile(entry.name, entry.is_dir(), entry.size, entry.is_link());
	}

	pRemoteDataObject->Finalize();

	object.Add(pRemoteDataObject, true);

#if FZ3_USESHELLEXT
	CShellExtensionInterface* ext = CShellExtensionInterface::CreateInitialized();
	if (ext)
	{
		const wxString& file = ext->GetDragDirectory();

		wxASSERT(!file.empty());

		wxFileDataObject *pFileDataObject = new wxFileDataObject;
		pFileDataObject->AddFile(file);

		object.Add(pFileDataObject);
	}
#endif

	CLabelEditBlocker b(*this);

	wxDropSource source(this);
	source.SetData(object);

	int res = source.DoDragDrop();

	pDragDropManager->Release();

	if (res != wxDragCopy)
	{
#if FZ3_USESHELLEXT
		delete ext;
		ext = 0;
#endif
		return;
	}

#if FZ3_USESHELLEXT
	if (ext)
	{
		if (!pRemoteDataObject->DidSendData())
		{
			const CServer* pServer = m_pState->GetServer();
			if (!m_pState->IsRemoteIdle() ||
				!pServer || *pServer != server ||
				!m_pDirectoryListing || m_pDirectoryListing->path != path)
			{
				// Remote listing has changed since drag started
				wxBell();
				delete ext;
				ext = 0;
				return;
			}

			// Same checks as before
			bool idle = m_pState->m_pCommandQueue->Idle();

			long item = -1;
			for (;;)
			{
				item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
				if (item == -1)
					break;

				if (!item)
				{
					// Can't drag ".."
					wxBell();
					delete ext;
					ext = 0;
					return;
				}

				int index = GetItemIndex(item);
				if (index == -1 || m_fileData[index].comparison_flags == fill)
					continue;
				if ((*m_pDirectoryListing)[index].is_dir() && !idle)
				{
					// Drag could result in recursive operation, don't allow at this point
					wxBell();
					delete ext;
					ext = 0;
					return;
				}
			}

			CLocalPath target(ext->GetTarget());
			if (target.empty())
			{
				delete ext;
				ext = 0;
				wxMessageBoxEx(_("Could not determine the target of the Drag&Drop operation.\nEither the shell extension is not installed properly or you didn't drop the files into an Explorer window."));
				return;
			}

			TransferSelectedFiles(target, false);

			delete ext;
			ext = 0;
			return;
		}
		delete ext;
		ext = 0;
	}
#endif
}

void CRemoteListView::OnMenuEdit(wxCommandEvent&)
{
	if (!m_pState->IsRemoteConnected() || !m_pDirectoryListing) {
		wxBell();
		return;
	}

	long item = -1;

	std::vector<CEditHandler::FileData> selected_items;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item) {
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir()) {
			wxBell();
			return;
		}

		selected_items.push_back({entry.name, entry.size});
	}

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		wxBell();
		return;
	}

	const CServerPath path = m_pDirectoryListing->path;
	const CServer server = *m_pState->GetServer();
	pEditHandler->Edit(CEditHandler::remote, selected_items, path, server, this);
}

#ifdef __WXDEBUG__
void CRemoteListView::ValidateIndexMapping()
{
	// This ensures that the index mapping is a bijection.
	// Beware:
	// - NO filter may be used!
	// - Doesn't work in comparison mode

	char* buffer = new char[m_pDirectoryListing->GetCount() + 1];
	memset(buffer, 0, m_pDirectoryListing->GetCount() + 1);

	// Injectivity
	for (auto const& item : m_indexMapping) {
		if (item > m_pDirectoryListing->GetCount()) {
			abort();
		}
		else if (buffer[item]) {
			abort();
		}

		buffer[item] = 1;
	}

	// Surjectivity
	for (unsigned int i = 0; i < m_pDirectoryListing->GetCount() + 1; i++) {
		wxASSERT(buffer[i] != 0);
	}

	delete [] buffer;
}
#endif

bool CRemoteListView::CanStartComparison(wxString* pError)
{
	if (!m_pDirectoryListing) {
		if (pError)
			*pError = _("Cannot compare directories, not connected to a server.");
		return false;
	}

	return true;
}

void CRemoteListView::StartComparison()
{
	if (m_sortDirection || m_sortColumn)
	{
		wxASSERT(m_originalIndexMapping.empty());
		SortList(0, 0);
	}

	ComparisonRememberSelections();

	if (m_originalIndexMapping.empty())
		m_originalIndexMapping.swap(m_indexMapping);
	else
		m_indexMapping.clear();

	m_comparisonIndex = -1;

	const CGenericFileData& last = m_fileData[m_fileData.size() - 1];
	if (last.comparison_flags != fill)
	{
		CGenericFileData data;
		data.icon = -1;
		data.comparison_flags = fill;
		m_fileData.push_back(data);
	}
}

bool CRemoteListView::GetNextFile(wxString& name, bool& dir, wxLongLong& size, CDateTime& date)
{
	if (++m_comparisonIndex >= (int)m_originalIndexMapping.size())
		return false;

	const unsigned int index = m_originalIndexMapping[m_comparisonIndex];
	if (index >= m_fileData.size())
		return false;

	if (index == m_pDirectoryListing->GetCount())
	{
		name = _T("..");
		dir = true;
		size = -1;
		return true;
	}

	const CDirentry& entry = (*m_pDirectoryListing)[index];

	name = entry.name;
	dir = entry.is_dir();
	size = entry.size;
	date = entry.time;

	return true;
}

void CRemoteListView::FinishComparison()
{
	SetInfoText();

	SetItemCount(m_indexMapping.size());

	ComparisonRestoreSelections();

	RefreshListOnly();
}

wxListItemAttr* CRemoteListView::OnGetItemAttr(long item) const
{
	CRemoteListView *pThis = const_cast<CRemoteListView *>(this);
	int index = GetItemIndex(item);

	if (index == -1)
		return 0;

#ifndef __WXMSW__
	if (item == m_dropTarget)
		return &pThis->m_dropHighlightAttribute;
#endif

	const CGenericFileData& data = m_fileData[index];

	switch (data.comparison_flags)
	{
	case different:
		return &pThis->m_comparisonBackgrounds[0];
	case lonely:
		return &pThis->m_comparisonBackgrounds[1];
	case newer:
		return &pThis->m_comparisonBackgrounds[2];
	default:
		return 0;
	}
}

wxString CRemoteListView::GetItemText(int item, unsigned int column)
{
	int index = GetItemIndex(item);
	if (index == -1)
		return wxString();

	if (!column)
	{
		if ((unsigned int)index == m_pDirectoryListing->GetCount())
			return _T("..");
		else if ((unsigned int)index < m_pDirectoryListing->GetCount())
			return (*m_pDirectoryListing)[index].name;
		else
			return wxString();
	}
	if (!item)
		return wxString(); //.. has no attributes

	if ((unsigned int)index >= m_pDirectoryListing->GetCount())
		return wxString();

	if (column == 1)
	{
		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir() || entry.size < 0)
			return wxString();
		else
			return CSizeFormat::Format(entry.size.GetValue());
	}
	else if (column == 2)
	{
		CGenericFileData& data = m_fileData[index];
		if (data.fileType.empty())
		{
			const CDirentry& entry = (*m_pDirectoryListing)[index];
			if (m_pDirectoryListing->path.GetType() == VMS)
				data.fileType = GetType(StripVMSRevision(entry.name), entry.is_dir());
			else
				data.fileType = GetType(entry.name, entry.is_dir());
		}

		return data.fileType;
	}
	else if (column == 3)
	{
		const CDirentry& entry = (*m_pDirectoryListing)[index];
		return CTimeFormat::Format(entry.time);
	}
	else if (column == 4)
		return *(*m_pDirectoryListing)[index].permissions;
	else if (column == 5)
		return *(*m_pDirectoryListing)[index].ownerGroup;
	return wxString();
}

CFileListCtrl<CGenericFileData>::CSortComparisonObject CRemoteListView::GetSortComparisonObject()
{
	CFileListCtrlSort<CDirectoryListing>::DirSortMode dirSortMode = GetDirSortMode();
	CFileListCtrlSort<CDirectoryListing>::NameSortMode nameSortMode = GetNameSortMode();

	CDirectoryListing const& directoryListing = *m_pDirectoryListing;
	if (!m_sortDirection)
	{
		if (m_sortColumn == 1)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortSize<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 2)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortType<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 3)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortTime<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 4)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortPermissions<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 5)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortOwnerGroup<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CFileListCtrlSortName<CDirectoryListing, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
	}
	else
	{
		if (m_sortColumn == 1)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortSize<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 2)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortType<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 3)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortTime<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 4)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortPermissions<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else if (m_sortColumn == 5)
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortOwnerGroup<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
		else
			return CFileListCtrl<CGenericFileData>::CSortComparisonObject(new CReverseSort<CFileListCtrlSortName<CDirectoryListing, CGenericFileData>, CGenericFileData>(directoryListing, m_fileData, dirSortMode, nameSortMode, this));
	}
}

void CRemoteListView::OnExitComparisonMode()
{
	CFileListCtrl<CGenericFileData>::OnExitComparisonMode();
	SetInfoText();
}

bool CRemoteListView::ItemIsDir(int index) const
{
	return (*m_pDirectoryListing)[index].is_dir();
}

wxLongLong CRemoteListView::ItemGetSize(int index) const
{
	return (*m_pDirectoryListing)[index].size;
}

void CRemoteListView::LinkIsNotDir(const CServerPath& path, const wxString& link)
{
	if (m_pLinkResolveState && m_pLinkResolveState->remote_path == path && m_pLinkResolveState->link == link) {
		wxString localFile = CQueueView::ReplaceInvalidCharacters(link);
		if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
			localFile = StripVMSRevision(localFile);
		m_pQueue->QueueFile(false, true,
			link, (link != localFile) ? localFile : wxString(),
			m_pLinkResolveState->local_path, m_pLinkResolveState->remote_path, m_pLinkResolveState->server, -1);
		m_pQueue->QueueFile_Finish(true);
	}

	m_pLinkResolveState.reset();
}

void CRemoteListView::OnMenuGeturl(wxCommandEvent&)
{
	if (!m_pDirectoryListing)
		return;

	const CServer* pServer = m_pState->GetServer();
	if (!pServer)
		return;

	long item = -1;

	std::list<CDirentry> selected_item_list;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
	{
		if (!item)
		{
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill)
			continue;

		const CDirentry& entry = (*m_pDirectoryListing)[index];

		selected_item_list.push_back(entry);
	}
	if (selected_item_list.empty())
	{
		wxBell();
		return;
	}

	if (!wxTheClipboard->Open())
	{
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy URLs"), wxICON_EXCLAMATION);
		return;
	}

	const CServerPath& path = m_pDirectoryListing->path;
	const wxString server = pServer->FormatServer(true);
	if (selected_item_list.size() == 1)
	{
		wxString url = server;
		url += path.FormatFilename(selected_item_list.front().name, false);

		// Poor mans URLencode
		url.Replace(_T(" "), _T("%20"));

		wxTheClipboard->SetData(new wxURLDataObject(url));
	}
	else
	{
		wxString urls;
		for (std::list<CDirentry>::const_iterator iter = selected_item_list.begin(); iter != selected_item_list.end(); ++iter)
		{
			urls += server;
			urls += path.FormatFilename(iter->name, false);
#ifdef __WXMSW__
			urls += _T("\r\n");
#else
			urls += _T("\n");
#endif
		}

		// Poor mans URLencode
		urls.Replace(_T(" "), _T("%20"));

		wxTheClipboard->SetData(new wxTextDataObject(urls));
	}

	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}

#ifdef __WXMSW__
int CRemoteListView::GetOverlayIndex(int item)
{
	int index = GetItemIndex(item);
	if (index == -1)
		return 0;
	if ((unsigned int)index >= m_pDirectoryListing->GetCount())
		return 0;

	if ((*m_pDirectoryListing)[index].is_link())
		return GetLinkOverlayIndex();

	return 0;
}
#endif

void CRemoteListView::OnMenuRefresh(wxCommandEvent&)
{
	if (m_pState)
		m_pState->RefreshRemote();
}

void CRemoteListView::OnNavigationEvent(bool forward)
{
	if (!forward) {
		if (!m_pState->IsRemoteIdle()) {
			wxBell();
			return;
		}

		if (!m_pDirectoryListing) {
			wxBell();
			return;
		}

		m_pState->ChangeRemoteDir(m_pDirectoryListing->path, _T(".."));
	}
}

void CRemoteListView::OnMenuNewfile(wxCommandEvent&)
{
	if (!m_pState->IsRemoteIdle() || !m_pDirectoryListing) {
		wxBell();
		return;
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create empty file"), _("Please enter the name of the file which should be created:")))
		return;

	if (dlg.ShowModal() != wxID_OK)
		return;

	if (dlg.GetValue().empty()) {
		wxBell();
		return;
	}

	wxString newFileName = dlg.GetValue();

	// Check if target file already exists
	for (unsigned int i = 0; i < m_pDirectoryListing->GetCount(); ++i) {
		if (newFileName == (*m_pDirectoryListing)[i].name) {
			wxMessageBoxEx(_("Target filename already exists!"));
			return;
		}
	}

	CEditHandler* edithandler = CEditHandler::Get(); // Used to get the temporary folder

	wxString emptyfile_name = _T("empty_file_yq744zm");
	wxString emptyfile = edithandler->GetLocalDirectory() + emptyfile_name;

	// Create the empty temporary file
	{
		wxFile file;
		wxLogNull log;
		file.Create(emptyfile);
	}

	const CServer* pServer = m_pState->GetServer();
	if (!pServer) {
		wxBell();
		return;
	}

	CFileTransferCommand *cmd = new CFileTransferCommand(emptyfile, m_pDirectoryListing->path, newFileName, false, CFileTransferCommand::t_transferSettings());
	m_pState->m_pCommandQueue->ProcessCommand(cmd);
}
