#include <filezilla.h>
#include "recursive_operation.h"
#include "commandqueue.h"
#include "chmoddialog.h"
#include "filter.h"
#include "Options.h"
#include "queue.h"
#include "local_filesys.h"

CRecursiveOperation::CNewDir::CNewDir()
{
	recurse = true;
	second_try = false;
	link = 0;
	doVisit = true;
}

CRecursiveOperation::CRecursiveOperation(CState* pState)
	: CStateEventHandler(pState),
	  m_operationMode(recursive_none)
{
	pState->RegisterHandler(this, STATECHANGE_REMOTE_DIR, false);
	pState->RegisterHandler(this, STATECHANGE_REMOTE_LINKNOTDIR, false);
}

CRecursiveOperation::~CRecursiveOperation()
{
	if (m_pChmodDlg)
	{
		m_pChmodDlg->Destroy();
		m_pChmodDlg = 0;
	}
}

void CRecursiveOperation::OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString&, const void* data2)
{
	if (notification == STATECHANGE_REMOTE_LINKNOTDIR)
	{
		wxASSERT(data2);
		LinkIsNotDir();
	}
	else
	{
		wxASSERT(pState);
		wxASSERT(notification == STATECHANGE_REMOTE_DIR);
		ProcessDirectoryListing(pState->GetRemoteDir().get());
	}
}

void CRecursiveOperation::StartRecursiveOperation(enum OperationMode mode, const CServerPath& startDir, const std::list<CFilter>& filters, bool allowParent /*=false*/, const CServerPath& finalDir /*=CServerPath()*/)
{
	wxCHECK_RET(m_operationMode == recursive_none, _T("StartRecursiveOperation called with m_operationMode != recursive_none"));
	wxCHECK_RET(m_pState->IsRemoteConnected(), _T("StartRecursiveOperation while disconnected"));
	wxCHECK_RET(!startDir.empty(), _T("Empty startDir in StartRecursiveOperation"));

	if (mode == recursive_chmod && !m_pChmodDlg)
		return;

	if ((mode == recursive_download || mode == recursive_addtoqueue || mode == recursive_download_flatten || mode == recursive_addtoqueue_flatten) && !m_pQueue)
		return;

	if (m_dirsToVisit.empty())
	{
		// Nothing to do in this case
		return;
	}

	m_operationMode = mode;
	m_pState->NotifyHandlers(STATECHANGE_REMOTE_IDLE);

	m_startDir = startDir;

	if (finalDir.empty())
		m_finalDir = startDir;
	else
		m_finalDir = finalDir;

	m_allowParent = allowParent;

	m_filters = filters;

	NextOperation();
}

void CRecursiveOperation::AddDirectoryToVisit(const CServerPath& path, const wxString& subdir, const CLocalPath& localDir /*=CLocalPath()*/, bool is_link /*=false*/)
{
	CNewDir dirToVisit;

	dirToVisit.localDir = localDir;
	dirToVisit.parent = path;
	dirToVisit.subdir = subdir;
	dirToVisit.link = is_link ? 2 : 0;
	m_dirsToVisit.push_back(dirToVisit);
}

void CRecursiveOperation::AddDirectoryToVisitRestricted(const CServerPath& path, const wxString& restrict, bool recurse)
{
	CNewDir dirToVisit;
	dirToVisit.parent = path;
	dirToVisit.recurse = recurse;
	dirToVisit.restrict = restrict;
	m_dirsToVisit.push_back(dirToVisit);
}

bool CRecursiveOperation::NextOperation()
{
	if (m_operationMode == recursive_none)
		return false;

	while (!m_dirsToVisit.empty())
	{
		const CNewDir& dirToVisit = m_dirsToVisit.front();
		if (m_operationMode == recursive_delete && !dirToVisit.doVisit)
		{
			m_pState->m_pCommandQueue->ProcessCommand(new CRemoveDirCommand(dirToVisit.parent, dirToVisit.subdir));
			m_dirsToVisit.pop_front();
			continue;
		}

		CListCommand* cmd = new CListCommand(dirToVisit.parent, dirToVisit.subdir, dirToVisit.link ? LIST_FLAG_LINK : 0);
		m_pState->m_pCommandQueue->ProcessCommand(cmd);
		return true;
	}

	StopRecursiveOperation();
	m_pState->m_pCommandQueue->ProcessCommand(new CListCommand(m_finalDir));
	return false;
}

bool CRecursiveOperation::BelowRecursionRoot(const CServerPath& path, CNewDir &dir)
{
	if (!dir.start_dir.empty())
	{
		if (path.IsSubdirOf(dir.start_dir, false))
			return true;
		else
			return false;
	}

	if (path.IsSubdirOf(m_startDir, false))
		return true;

	// In some cases (chmod from tree for example) it is neccessary to list the
	// parent first
	if (path == m_startDir && m_allowParent)
		return true;

	if (dir.link == 2)
	{
		dir.start_dir = path;
		return true;
	}

	return false;
}

// Defined in RemoteListView.cpp
extern wxString StripVMSRevision(const wxString& name);

void CRecursiveOperation::ProcessDirectoryListing(const CDirectoryListing* pDirectoryListing)
{
	if (!pDirectoryListing)
	{
		StopRecursiveOperation();
		return;
	}

	if (m_operationMode == recursive_none)
		return;

	if (pDirectoryListing->failed())
	{
		// Ignore this.
		// It will get handled by the failed command in ListingFailed
		return;
	}

	wxASSERT(!m_dirsToVisit.empty());

	if (!m_pState->IsRemoteConnected() || m_dirsToVisit.empty())
	{
		StopRecursiveOperation();
		return;
	}

	CNewDir dir = m_dirsToVisit.front();
	m_dirsToVisit.pop_front();

	if (!BelowRecursionRoot(pDirectoryListing->path, dir))
	{
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete && dir.doVisit && !dir.subdir.empty())
	{
		// After recursing into directory to delete its contents, delete directory itself
		// Gets handled in NextOperation
		CNewDir dir2 = dir;
		dir2.doVisit = false;
		m_dirsToVisit.push_front(dir2);
	}

	if (dir.link && !dir.recurse)
	{
		NextOperation();
		return;
	}

	// Check if we have already visited the directory
	if (!m_visitedDirs.insert(pDirectoryListing->path).second)
	{
		NextOperation();
		return;
	}

	const CServer* pServer = m_pState->GetServer();
	wxASSERT(pServer);

	if (!pDirectoryListing->GetCount())
	{
		if (m_operationMode == recursive_download)
		{
			wxFileName::Mkdir(dir.localDir.GetPath(), 0777, wxPATH_MKDIR_FULL);
			m_pState->RefreshLocalFile(dir.localDir.GetPath());
		}
		else if (m_operationMode == recursive_addtoqueue)
		{
			m_pQueue->QueueFile(true, true, _T(""), _T(""), dir.localDir, CServerPath(), *pServer, -1);
			m_pQueue->QueueFile_Finish(false);
		}
	}

	CFilterManager filter;

	// Is operation restricted to a single child?
	bool restrict = !dir.restrict.empty();

	std::list<wxString> filesToDelete;

	const wxString path = pDirectoryListing->path.GetPath();

	bool added = false;

	for (int i = pDirectoryListing->GetCount() - 1; i >= 0; --i)
	{
		const CDirentry& entry = (*pDirectoryListing)[i];

		if (restrict)
		{
			if (entry.name != dir.restrict)
				continue;
		}
		else if (filter.FilenameFiltered(m_filters, entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time))
			continue;

		if (entry.is_dir() && (!entry.is_link() || m_operationMode != recursive_delete))
		{
			if (dir.recurse)
			{
				CNewDir dirToVisit;
				dirToVisit.parent = pDirectoryListing->path;
				dirToVisit.subdir = entry.name;
				dirToVisit.localDir = dir.localDir;
				dirToVisit.start_dir = dir.start_dir;

				if (m_operationMode == recursive_download || m_operationMode == recursive_addtoqueue)
					dirToVisit.localDir.AddSegment(CQueueView::ReplaceInvalidCharacters(entry.name));
				if (entry.is_link())
				{
					dirToVisit.link = 1;
					dirToVisit.recurse = false;
				}
				m_dirsToVisit.push_front(dirToVisit);
			}
		}
		else
		{
			switch (m_operationMode)
			{
			case recursive_download:
			case recursive_download_flatten:
				{
					wxString localFile = CQueueView::ReplaceInvalidCharacters(entry.name);
					if (pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
						localFile = StripVMSRevision(localFile);
					m_pQueue->QueueFile(m_operationMode == recursive_addtoqueue, true,
						entry.name, (entry.name == localFile) ? wxString() : localFile,
						dir.localDir, pDirectoryListing->path, *pServer, entry.size);
					added = true;
				}
				break;
			case recursive_addtoqueue:
			case recursive_addtoqueue_flatten:
				{
					wxString localFile = CQueueView::ReplaceInvalidCharacters(entry.name);
					if (pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION))
						localFile = StripVMSRevision(localFile);
					m_pQueue->QueueFile(true, true,
						entry.name, (entry.name == localFile) ? wxString() : localFile,
						dir.localDir, pDirectoryListing->path, *pServer, entry.size);
					added = true;
				}
				break;
			case recursive_delete:
				filesToDelete.push_back(entry.name);
				break;
			default:
				break;
			}
		}

		if (m_operationMode == recursive_chmod && m_pChmodDlg)
		{
			const int applyType = m_pChmodDlg->GetApplyType();
			if (!applyType ||
				(!entry.is_dir() && applyType == 1) ||
				(entry.is_dir() && applyType == 2))
			{
				char permissions[9];
				bool res = m_pChmodDlg->ConvertPermissions(*entry.permissions, permissions);
				wxString newPerms = m_pChmodDlg->GetPermissions(res ? permissions : 0, entry.is_dir());
				m_pState->m_pCommandQueue->ProcessCommand(new CChmodCommand(pDirectoryListing->path, entry.name, newPerms));
			}
		}
	}
	if (added)
		m_pQueue->QueueFile_Finish(m_operationMode != recursive_addtoqueue && m_operationMode != recursive_addtoqueue_flatten);

	if (m_operationMode == recursive_delete && !filesToDelete.empty())
		m_pState->m_pCommandQueue->ProcessCommand(new CDeleteCommand(pDirectoryListing->path, filesToDelete));

	NextOperation();
}

void CRecursiveOperation::SetChmodDialog(CChmodDialog* pChmodDialog)
{
	wxASSERT(pChmodDialog);

	if (m_pChmodDlg)
		m_pChmodDlg->Destroy();

	m_pChmodDlg = pChmodDialog;
}

void CRecursiveOperation::StopRecursiveOperation()
{
	if (m_operationMode != recursive_none)
	{
		m_operationMode = recursive_none;
		m_pState->NotifyHandlers(STATECHANGE_REMOTE_IDLE);
	}
	m_dirsToVisit.clear();
	m_visitedDirs.clear();

	if (m_pChmodDlg)
	{
		m_pChmodDlg->Destroy();
		m_pChmodDlg = 0;
	}
}

void CRecursiveOperation::ListingFailed(int error)
{
	if (m_operationMode == recursive_none)
		return;

	if( (error & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
		// User has cancelled operation
		StopRecursiveOperation();
		return;
	}

	wxASSERT(!m_dirsToVisit.empty());
	if (m_dirsToVisit.empty())
		return;

	CNewDir dir = m_dirsToVisit.front();
	m_dirsToVisit.pop_front();
	if ((error & FZ_REPLY_CRITICALERROR) != FZ_REPLY_CRITICALERROR && !dir.second_try)
	{
		// Retry, could have been a temporary socket creating failure
		// (e.g. hitting a blocked port) or a disconnect (e.g. no-filetransfer-timeout)
		dir.second_try = true;
		m_dirsToVisit.push_front(dir);
	}

	NextOperation();
}

void CRecursiveOperation::SetQueue(CQueueView* pQueue)
{
	m_pQueue = pQueue;
}

bool CRecursiveOperation::ChangeOperationMode(enum OperationMode mode)
{
	if (mode != recursive_addtoqueue && m_operationMode != recursive_download && mode != recursive_addtoqueue_flatten && m_operationMode != recursive_download_flatten)
		return false;

	m_operationMode = mode;

	return true;
}

void CRecursiveOperation::LinkIsNotDir()
{
	if (m_operationMode == recursive_none)
		return;

	wxASSERT(!m_dirsToVisit.empty());
	if (m_dirsToVisit.empty())
		return;

	CNewDir dir = m_dirsToVisit.front();
	m_dirsToVisit.pop_front();

	const CServer* pServer = m_pState->GetServer();
	if (!pServer)
	{
		NextOperation();
		return;
	}

	if (m_operationMode == recursive_delete)
	{
		if (!dir.subdir.empty())
		{
			std::list<wxString> files;
			files.push_back(dir.subdir);
			m_pState->m_pCommandQueue->ProcessCommand(new CDeleteCommand(dir.parent, files));
		}
		NextOperation();
		return;
	}
	else if (m_operationMode != recursive_list )
	{
		CLocalPath localPath = dir.localDir;
		wxString localFile = dir.subdir;
		if (m_operationMode != recursive_addtoqueue_flatten && m_operationMode != recursive_download_flatten)
			localPath.MakeParent();
		m_pQueue->QueueFile(m_operationMode == recursive_addtoqueue || m_operationMode == recursive_addtoqueue_flatten, true, dir.subdir, (dir.subdir == localFile) ? wxString() : localFile, localPath, dir.parent, *pServer, -1);
		m_pQueue->QueueFile_Finish(m_operationMode != recursive_addtoqueue);
	}

	NextOperation();
}
