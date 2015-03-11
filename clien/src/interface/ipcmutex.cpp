#include <filezilla.h>
#include "ipcmutex.h"
#include "Options.h"

#ifndef __WXMSW__
#include <errno.h>
int CInterProcessMutex::m_fd = -1;
int CInterProcessMutex::m_instanceCount = 0;
#endif

std::list<CReentrantInterProcessMutexLocker::t_data> CReentrantInterProcessMutexLocker::m_mutexes;

CInterProcessMutex::CInterProcessMutex(enum t_ipcMutexType mutexType, bool initialLock /*=true*/)
{
	m_locked = false;
#ifdef __WXMSW__
	// Create mutex_
	hMutex = ::CreateMutex(0, false, wxString::Format(_T("FileZilla 3 Mutex Type %d"), mutexType));
#else
	if (!m_instanceCount) {
		// Open file only if this is the first instance
		wxFileName fn(COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR), _T("lockfile"));
		m_fd = open(fn.GetFullPath().mb_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
	}
	m_instanceCount++;
#endif
	m_type = mutexType;
	if (initialLock)
		Lock();
}

CInterProcessMutex::~CInterProcessMutex()
{
	if (m_locked)
		Unlock();
#ifdef __WXMSW__
	if (hMutex)
		::CloseHandle(hMutex);
#else
	m_instanceCount--;
	// Close file only if this is the last instance. At least under
	// Linux, closing the lock file has the affect of removing all locks.
	if (!m_instanceCount && m_fd >= 0)
		close(m_fd);
#endif
}

bool CInterProcessMutex::Lock()
{
	wxASSERT(!m_locked);
#ifdef __WXMSW__
	if (hMutex)
		::WaitForSingleObject(hMutex, INFINITE);
#else
	if (m_fd >= 0)
	{
		// Lock 1 byte region in the lockfile. m_type specifies the byte to lock.
		struct flock f = {0};
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = m_type;
		f.l_len = 1;
		f.l_pid = getpid();

		while (fcntl(m_fd, F_SETLKW, &f) == -1)
		{
			if (errno == EINTR) // Interrupted by signal, retry
				continue;

			// Can't do any locking in this case
			return false;
		}
	}
#endif

	m_locked = true;

	return true;
}

int CInterProcessMutex::TryLock()
{
	wxASSERT(!m_locked);

#ifdef __WXMSW__
	if (!hMutex)
	{
		m_locked = false;
		return 0;
	}

	int res = ::WaitForSingleObject(hMutex, 1);
	if (res == WAIT_OBJECT_0)
	{
		m_locked = true;
		return 1;
	}
#else
	if (m_fd >= 0)
	{
		// Try to lock 1 byte region in the lockfile. m_type specifies the byte to lock.
		struct flock f = {0};
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = m_type;
		f.l_len = 1;
		f.l_pid = getpid();
		while (fcntl(m_fd, F_SETLK, &f) == -1)
		{
			if (errno == EINTR) // Interrupted by signal, retry
				continue;

			if (errno == EAGAIN || errno == EACCES) // Lock held by other process
				return 0;

			// Can't do any locking in this case
			return -1;
		}

		m_locked = true;
		return 1;
	}
#endif

	return 0;
}

void CInterProcessMutex::Unlock()
{
	if (!m_locked)
		return;
	m_locked = false;

#ifdef __WXMSW__
	if (hMutex)
		::ReleaseMutex(hMutex);
#else
	if (m_fd >= 0)
	{
		// Unlock region specified by m_type.
		struct flock f = {0};
		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = m_type;
		f.l_len = 1;
		f.l_pid = getpid();
		while (fcntl(m_fd, F_SETLKW, &f) == -1)
		{
			if (errno == EINTR) // Interrupted by signal, retry
				continue;

			// Can't do any locking in this case
			return;
		}
	}
#endif
}

CReentrantInterProcessMutexLocker::CReentrantInterProcessMutexLocker(enum t_ipcMutexType mutexType)
{
	m_type = mutexType;

	std::list<t_data>::iterator iter;
	for (iter = m_mutexes.begin(); iter != m_mutexes.end(); ++iter)
	{
		if (iter->pMutex->GetType() == mutexType)
			break;
	}

	if (iter != m_mutexes.end())
	{
		iter->lockCount++;
	}
	else
	{
		t_data data;
		data.lockCount = 1;
		data.pMutex = new CInterProcessMutex(mutexType);
		m_mutexes.push_back(data);
	}
}

CReentrantInterProcessMutexLocker::~CReentrantInterProcessMutexLocker()
{
	std::list<t_data>::iterator iter;
	for (iter = m_mutexes.begin(); iter != m_mutexes.end(); ++iter)
	{
		if (iter->pMutex->GetType() == m_type)
			break;
	}

	wxASSERT(iter != m_mutexes.end());
	if (iter == m_mutexes.end())
		return;

	if (iter->lockCount == 1)
	{
		delete iter->pMutex;
		m_mutexes.erase(iter);
	}
	else
		iter->lockCount--;
}
