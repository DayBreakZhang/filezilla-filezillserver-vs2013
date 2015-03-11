#include <filezilla.h>
#include "queue.h"
#include "queueview_failed.h"
#include "edithandler.h"

BEGIN_EVENT_TABLE(CQueueViewFailed, CQueueViewBase)
EVT_CONTEXT_MENU(CQueueViewFailed::OnContextMenu)
EVT_MENU(XRCID("ID_REMOVEALL"), CQueueViewFailed::OnRemoveAll)
EVT_MENU(XRCID("ID_REMOVE"), CQueueViewFailed::OnRemoveSelected)
EVT_MENU(XRCID("ID_REQUEUE"), CQueueViewFailed::OnRequeueSelected)
EVT_MENU(XRCID("ID_REQUEUEALL"), CQueueViewFailed::OnRequeueAll)
EVT_CHAR(CQueueViewFailed::OnChar)
END_EVENT_TABLE()

CQueueViewFailed::CQueueViewFailed(CQueue* parent, int index)
	: CQueueViewBase(parent, index, _("Failed transfers"))
{
	std::list<ColumnId> extraCols;
	extraCols.push_back(colTime);
	extraCols.push_back(colErrorReason);
	CreateColumns(extraCols);
}

CQueueViewFailed::CQueueViewFailed(CQueue* parent, int index, const wxString& title)
	: CQueueViewBase(parent, index, title)
{
}

void CQueueViewFailed::OnContextMenu(wxContextMenuEvent& event)
{
	wxMenu* pMenu = wxXmlResource::Get()->LoadMenu(_T("ID_MENU_QUEUE_FAILED"));
	if (!pMenu)
		return;

#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	const bool has_selection = GetSelectedItemCount() != 0;
#else
	const bool has_selection = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1;
#endif

	pMenu->Enable(XRCID("ID_REMOVE"), has_selection);
	pMenu->Enable(XRCID("ID_REQUEUE"), has_selection);
	pMenu->Enable(XRCID("ID_REQUEUEALL"), !m_serverList.empty());

	PopupMenu(pMenu);
	delete pMenu;
}

void CQueueViewFailed::OnRemoveAll(wxCommandEvent& event)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		// First, clear all selections
		int item;
		while ((item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1)
			SetItemState(item, 0, wxLIST_STATE_SELECTED);
	}

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (pEditHandler)
		pEditHandler->RemoveAll(CEditHandler::upload_and_remove_failed);

	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
		delete *iter;
	m_serverList.clear();

	m_itemCount = 0;
	SaveSetItemCount(0);
	m_fileCount = 0;
	m_folderScanCount = 0;

	DisplayNumberQueuedFiles();

	RefreshListOnly();

	if (!m_itemCount && m_pQueue->GetQueueView()->GetItemCount())
		m_pQueue->SetSelection(0);
}

void CQueueViewFailed::OnRemoveSelected(wxCommandEvent& event)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (!GetSelectedItemCount())
		return;
#endif

	std::list<CQueueItem*> selectedItems;
	long item = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		selectedItems.push_front(GetQueueItem(item));
		SetItemState(item, 0, wxLIST_STATE_SELECTED);
	}

	CEditHandler* pEditHandler = CEditHandler::Get();

	while (!selectedItems.empty())
	{
		CQueueItem* pItem = selectedItems.front();
		selectedItems.pop_front();

		CQueueItem* pTopLevelItem = pItem->GetTopLevelItem();

		if (pItem->GetType() == QueueItemType::Server)
		{
			CServerItem* pServerItem = (CServerItem*)pItem;
			if (pEditHandler && pEditHandler->GetFileCount(CEditHandler::remote, CEditHandler::upload_and_remove_failed, &pServerItem->GetServer()))
				pEditHandler->RemoveAll(CEditHandler::upload_and_remove_failed, &pServerItem->GetServer());
		}
		else if (pItem->GetType() == QueueItemType::File)
		{
			CFileItem* pFileItem = (CFileItem*)pItem;
			if (pFileItem->m_edit == CEditHandler::remote && pEditHandler)
			{
				if (pFileItem->m_edit == CEditHandler::local)
				{
					wxString fullPath(pFileItem->GetLocalPath().GetPath() + pFileItem->GetLocalFile());
					enum CEditHandler::fileState state = pEditHandler->GetFileState(fullPath);
					if (state == CEditHandler::upload_and_remove_failed)
						pEditHandler->Remove(fullPath);
				}
				else
				{
					CServerItem* pServerItem = (CServerItem*)pFileItem->GetTopLevelItem();
					enum CEditHandler::fileState state = pEditHandler->GetFileState(pFileItem->GetRemoteFile(), pFileItem->GetRemotePath(), pServerItem->GetServer());
					if (state == CEditHandler::upload_and_remove_failed)
						pEditHandler->Remove(pFileItem->GetRemoteFile(), pFileItem->GetRemotePath(), pServerItem->GetServer());
				}
			}
		}

		if (!pTopLevelItem->GetChild(1))
		{
			// Parent will get deleted
			// If next selected item is parent, remove it from list
			if (!selectedItems.empty() && selectedItems.front() == pTopLevelItem)
				selectedItems.pop_front();
		}
		RemoveItem(pItem, true, false, false);
	}
	DisplayNumberQueuedFiles();
	SaveSetItemCount(m_itemCount);
	RefreshListOnly();

	if (!m_itemCount && m_pQueue->GetQueueView()->GetItemCount())
		m_pQueue->SetSelection(0);
}

bool CQueueViewFailed::RequeueFileItem(CFileItem* pFileItem, CServerItem* pServerItem)
{
	CQueueView* pQueueView = m_pQueue->GetQueueView();

	pFileItem->m_errorCount = 0;
	pFileItem->SetStatusMessage(CFileItem::none);

	if (!pFileItem->Download() && !wxFileName::FileExists(pFileItem->GetLocalPath().GetPath() + pFileItem->GetLocalFile()))
	{
		delete pFileItem;
		return false;
	}

	if (pFileItem->m_edit == CEditHandler::remote)
	{
		CEditHandler* pEditHandler = CEditHandler::Get();
		if (!pEditHandler)
		{
			delete pFileItem;
			return false;
		}
		enum CEditHandler::fileState state = pEditHandler->GetFileState(pFileItem->GetRemoteFile(), pFileItem->GetRemotePath(), pServerItem->GetServer());
		if (state == CEditHandler::unknown)
		{
			wxASSERT(pFileItem->Download());
			wxString file = pFileItem->GetRemoteFile();
			if (!pEditHandler->AddFile(CEditHandler::remote, file, pFileItem->GetRemotePath(), pServerItem->GetServer()))
			{
				delete pFileItem;
				return false;
			}
			pFileItem->SetTargetFile(file);
		}
		else if (state == CEditHandler::upload_and_remove_failed)
		{
			wxASSERT(!pFileItem->Download());
			bool ret = true;
			if (!pEditHandler->UploadFile(pFileItem->GetRemoteFile(), pFileItem->GetRemotePath(), pServerItem->GetServer(), true))
				ret = false;
			delete pFileItem;
			return ret;
		}
		else
		{
			delete pFileItem;
			return false;
		}
	}

	pFileItem->SetParent(pServerItem);
	pQueueView->InsertItem(pServerItem, pFileItem);

	return true;
}

bool CQueueViewFailed::RequeueServerItem(CServerItem* pServerItem)
{
	bool ret = true;

	CQueueView* pQueueView = m_pQueue->GetQueueView();

	CServerItem* pTargetServerItem = pQueueView->CreateServerItem(pServerItem->GetServer());

	unsigned int childrenCount = pServerItem->GetChildrenCount(false);
	for (unsigned int i = 0; i < childrenCount; i++)
	{
		CFileItem* pFileItem = (CFileItem*)pServerItem->GetChild(i, false);

		ret &= RequeueFileItem(pFileItem, pTargetServerItem);
	}

	m_fileCount -= childrenCount;
	m_itemCount -= childrenCount + 1;
	pServerItem->DetachChildren();

	std::vector<CServerItem*>::iterator iter;
	for (iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (*iter == pServerItem)
			break;
	}
	if (iter != m_serverList.end())
		m_serverList.erase(iter);

	delete pServerItem;

	if (!pTargetServerItem->GetChildrenCount(false)) {
		pQueueView->CommitChanges();
		pQueueView->RemoveItem(pTargetServerItem, true, true, true);
	}

	return ret;
}

void CQueueViewFailed::OnRequeueSelected(wxCommandEvent& event)
{
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (!GetSelectedItemCount())
		return;
#endif

	bool failedToRequeueAll = false;
	std::list<CQueueItem*> selectedItems;
	long item = -1;
	long skipTo = -1;
	for (;;)
	{
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;
		SetItemState(item, 0, wxLIST_STATE_SELECTED);
		if (item < skipTo)
			continue;

		CQueueItem* pItem = GetQueueItem(item);
		if (pItem->GetType() == QueueItemType::Server)
			skipTo = item + pItem->GetChildrenCount(true) + 1;
		selectedItems.push_back(GetQueueItem(item));
	}

	if (selectedItems.empty())
		return;

	CQueueView* pQueueView = m_pQueue->GetQueueView();

	while (!selectedItems.empty())
	{
		CQueueItem* pItem = selectedItems.front();
		selectedItems.pop_front();

		if (pItem->GetType() == QueueItemType::Server)
		{
			CServerItem* pServerItem = (CServerItem*)pItem;
			failedToRequeueAll |= !RequeueServerItem(pServerItem);
		}
		else
		{
			CFileItem* pFileItem = (CFileItem*)pItem;

			CServerItem* pOldServerItem = (CServerItem*)pItem->GetTopLevelItem();
			CServerItem* pServerItem = pQueueView->CreateServerItem(pOldServerItem->GetServer());
			RemoveItem(pItem, false, false, false);

			failedToRequeueAll |= !RequeueFileItem(pFileItem, pServerItem);

			if (!pServerItem->GetChildrenCount(false))
			{
				pQueueView->CommitChanges();
				pQueueView->RemoveItem(pServerItem, true, true, true);
			}
		}
	}
	m_fileCountChanged = true;

	pQueueView->CommitChanges();

	if (pQueueView->IsActive())
		pQueueView->AdvanceQueue(false);

	DisplayNumberQueuedFiles();
	SaveSetItemCount(m_itemCount);
	RefreshListOnly();

	if (!m_itemCount && m_pQueue->GetQueueView()->GetItemCount())
		m_pQueue->SetSelection(0);

	if (failedToRequeueAll)
		wxMessageBoxEx(_("Not all items could be requeued for transfer."));
}

void CQueueViewFailed::OnRequeueAll(wxCommandEvent& event)
{
	bool ret = true;
	while (!m_serverList.empty())
		ret &= RequeueServerItem(m_serverList.front());

	m_fileCountChanged = true;

	CQueueView* pQueueView = m_pQueue->GetQueueView();
	pQueueView->CommitChanges();

	if (pQueueView->IsActive())
		pQueueView->AdvanceQueue(false);

	DisplayNumberQueuedFiles();
	SaveSetItemCount(m_itemCount);
	RefreshListOnly();

	if (!m_itemCount && m_pQueue->GetQueueView()->GetItemCount())
		m_pQueue->SetSelection(0);

	if (!ret)
		wxMessageBoxEx(_("Not all items could be requeued for transfer."));
}

void CQueueViewFailed::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_DELETE || event.GetKeyCode() == WXK_NUMPAD_DELETE)
	{
		wxCommandEvent cmdEvt;
		OnRemoveSelected(cmdEvt);
	}
	else
		event.Skip();
}
