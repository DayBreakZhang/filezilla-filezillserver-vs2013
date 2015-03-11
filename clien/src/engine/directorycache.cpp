#include <filezilla.h>
#include "directorycache.h"

CDirectoryCache::CDirectoryCache()
{
}

CDirectoryCache::~CDirectoryCache()
{
	for( auto & serverEntry : m_serverList ) {
		for (auto & cacheEntry : serverEntry.cacheList ) {
#ifdef __WXDEBUG__
			m_totalFileCount -= cacheEntry.listing.GetCount();
#endif
			tLruList::iterator* lruIt = (tLruList::iterator*)cacheEntry.lruIt;
			if (lruIt) {
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}
		}
	}
#ifdef __WXDEBUG__
	wxASSERT(m_totalFileCount == 0);
#endif
}

void CDirectoryCache::Store(const CDirectoryListing &listing, const CServer &server)
{
	scoped_lock lock(mutex_);

	tServerIter sit = CreateServerEntry(server);
	wxASSERT(sit != m_serverList.end());

	m_totalFileCount += listing.GetCount();

	tCacheIter cit;
	bool unused;
	if (Lookup(cit, sit, listing.path, true, unused)) {
		cit->modificationTime = CMonotonicTime::Now();

		m_totalFileCount -= cit->listing.GetCount();
		cit->listing = listing;

		return;
	}

	sit->cacheList.emplace_front(listing);

	UpdateLru(sit, sit->cacheList.begin());

	Prune();
}

bool CDirectoryCache::Lookup(CDirectoryListing &listing, const CServer &server, const CServerPath &path, bool allowUnsureEntries, bool& is_outdated)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	tCacheIter iter;
	if (Lookup(iter, sit, path, allowUnsureEntries, is_outdated)) {
		listing = iter->listing;
		return true;
	}

	return false;
}

bool CDirectoryCache::Lookup(tCacheIter &cacheIter, tServerIter &sit, const CServerPath &path, bool allowUnsureEntries, bool& is_outdated)
{
	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter) {
		const CCacheEntry &entry = *iter;

		if (entry.listing.path != path)
			continue;

		UpdateLru(sit, iter);

		if (!allowUnsureEntries && entry.listing.get_unsure_flags())
			return false;

		cacheIter = iter;
		is_outdated = (CDateTime::Now() - entry.listing.m_firstListTime.GetTime()).GetSeconds() > CACHE_TIMEOUT;
		return true;
	}

	return false;
}

bool CDirectoryCache::DoesExist(const CServer &server, const CServerPath &path, int &hasUnsureEntries, bool &is_outdated)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	tCacheIter iter;
	if (Lookup(iter, sit, path, true, is_outdated)) {
		hasUnsureEntries = iter->listing.get_unsure_flags();
		return true;
	}

	return false;
}

bool CDirectoryCache::LookupFile(CDirentry &entry, const CServer &server, const CServerPath &path, const wxString& file, bool &dirDidExist, bool &matchedCase)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end()) {
		dirDidExist = false;
		return false;
	}

	tCacheIter iter;
	bool unused;
	if (!Lookup(iter, sit, path, true, unused)) {
		dirDidExist = false;
		return false;
	}
	dirDidExist = true;

	const CCacheEntry &cacheEntry = *iter;
	const CDirectoryListing &listing = cacheEntry.listing;

	int i = listing.FindFile_CmpCase(file);
	if (i >= 0) {
		entry = listing[i];
		matchedCase = true;
		return true;
	}
	i = listing.FindFile_CmpNoCase(file);
	if (i >= 0) {
		entry = listing[i];
		matchedCase = false;
		return true;
	}

	return false;
}

CDirectoryCache::CCacheEntry& CDirectoryCache::CCacheEntry::operator=(const CDirectoryCache::CCacheEntry &a)
{
	lruIt = a.lruIt;
	listing = a.listing;
	modificationTime = a.modificationTime;

	return *this;
}

CDirectoryCache::CCacheEntry::CCacheEntry(const CDirectoryCache::CCacheEntry &entry)
	: lruIt()
{
	listing = entry.listing;
	modificationTime = entry.modificationTime;
}

bool CDirectoryCache::InvalidateFile(const CServer &server, const CServerPath &path, const wxString& filename, bool *wasDir /*=false*/)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter) {
		CCacheEntry &entry = *iter;
		if (path.CmpNoCase(entry.listing.path))
			continue;

		UpdateLru(sit, iter);

		for (unsigned int i = 0; i < entry.listing.GetCount(); i++) {
			if (!filename.CmpNoCase(((const CCacheEntry&)entry).listing[i].name)) {
				if (wasDir)
					*wasDir = entry.listing[i].is_dir();
				entry.listing[i].flags |= CDirentry::flag_unsure;
			}
		}
		entry.listing.m_flags |= CDirectoryListing::unsure_unknown;
		entry.modificationTime = CMonotonicTime::Now();
	}

	return true;
}

bool CDirectoryCache::UpdateFile(const CServer &server, const CServerPath &path, const wxString& filename, bool mayCreate, enum Filetype type /*=file*/, wxLongLong size /*=-1*/)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	bool updated = false;

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter)
	{
		CCacheEntry &entry = *iter;
		const CCacheEntry &cEntry = *iter;
		if (path.CmpNoCase(entry.listing.path))
			continue;

		UpdateLru(sit, iter);

		bool matchCase = false;
		unsigned int i;
		for (i = 0; i < entry.listing.GetCount(); i++)
		{
			if (!filename.CmpNoCase(cEntry.listing[i].name))
			{
				entry.listing[i].flags |= CDirentry::flag_unsure;
				if (cEntry.listing[i].name == filename)
				{
					matchCase = true;
					break;
				}
			}
		}

		if (matchCase)
		{
			enum Filetype old_type = entry.listing[i].is_dir() ? dir : file;
			if (type != old_type)
				entry.listing.m_flags |= CDirectoryListing::unsure_invalid;
			else if (type == dir)
				entry.listing.m_flags |= CDirectoryListing::unsure_dir_changed;
			else
				entry.listing.m_flags |= CDirectoryListing::unsure_file_changed;
		}
		else if (type != unknown && mayCreate)
		{
			const unsigned int count = entry.listing.GetCount();
			entry.listing.SetCount(count + 1);
			CDirentry& direntry = entry.listing[count];
			direntry.name = filename;
			if (type == dir)
				direntry.flags = CDirentry::flag_dir | CDirentry::flag_unsure;
			else
				direntry.flags = CDirentry::flag_unsure;
			direntry.size = size;
			switch (type)
			{
			case dir:
				entry.listing.m_flags |= CDirectoryListing::unsure_dir_added;
				break;
			case file:
				entry.listing.m_flags |= CDirectoryListing::unsure_file_added;
				break;
			default:
				entry.listing.m_flags |= CDirectoryListing::unsure_invalid;
				break;
			}

			++m_totalFileCount;
		}
		else
			entry.listing.m_flags |= CDirectoryListing::unsure_unknown;
		entry.modificationTime = CMonotonicTime::Now();

		updated = true;
	}

	return updated;
}

bool CDirectoryCache::RemoveFile(const CServer &server, const CServerPath &path, const wxString& filename)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ++iter)
	{
		const CCacheEntry &entry = *iter;
		if (path.CmpNoCase(entry.listing.path))
			continue;

		UpdateLru(sit, iter);

		bool matchCase = false;
		for (unsigned int i = 0; i < entry.listing.GetCount(); i++)
		{
			if (entry.listing[i].name == filename)
				matchCase = true;
		}

		if (matchCase)
		{
			unsigned int i;
			for (i = 0; i < entry.listing.GetCount(); i++)
				if (entry.listing[i].name == filename)
					break;
			wxASSERT(i != entry.listing.GetCount());

			CDirectoryListing& listing = iter->listing;
			listing.RemoveEntry(i); // This does set m_hasUnsureEntries
			--m_totalFileCount;
		}
		else
		{
			for (unsigned int i = 0; i < entry.listing.GetCount(); i++)
			{
				if (!filename.CmpNoCase(entry.listing[i].name))
					iter->listing[i].flags |= CDirentry::flag_unsure;
			}
			iter->listing.m_flags |= CDirectoryListing::unsure_invalid;
		}
		iter->modificationTime = CMonotonicTime::Now();
	}

	return true;
}

void CDirectoryCache::InvalidateServer(const CServer& server)
{
	scoped_lock lock(mutex_);

	for (auto iter = m_serverList.begin(); iter != m_serverList.end(); ++iter)
	{
		if (iter->server != server)
			continue;

		for (tCacheIter cit = iter->cacheList.begin(); cit != iter->cacheList.end(); ++cit)
		{
			tLruList::iterator* lruIt = (tLruList::iterator*)cit->lruIt;
			if (lruIt)
			{
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}

			m_totalFileCount -= cit->listing.GetCount();
		}

		m_serverList.erase(iter);
		break;
	}
}

bool CDirectoryCache::GetChangeTime(CMonotonicTime& time, const CServer &server, const CServerPath &path)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return false;

	tCacheIter iter;
	bool unused;
	if (Lookup(iter, sit, path, true, unused)) {
		time = iter->modificationTime;
		return true;
	}

	return false;
}

void CDirectoryCache::RemoveDir(const CServer& server, const CServerPath& path, const wxString& filename, const CServerPath&)
{
	scoped_lock lock(mutex_);

	// TODO: This is not 100% foolproof and may not work properly
	// Perhaps just throw away the complete cache?

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return;

	CServerPath absolutePath = path;
	if (!absolutePath.AddSegment(filename))
		absolutePath.clear();

	for (tCacheIter iter = sit->cacheList.begin(); iter != sit->cacheList.end(); ) {
		CCacheEntry &entry = *iter;
		// Delete exact matches and subdirs
		if (!absolutePath.empty() && (entry.listing.path == absolutePath || absolutePath.IsParentOf(entry.listing.path, true))) {
			m_totalFileCount -= entry.listing.GetCount();
			tLruList::iterator* lruIt = (tLruList::iterator*)iter->lruIt;
			if (lruIt) {
				m_leastRecentlyUsedList.erase(*lruIt);
				delete lruIt;
			}
			sit->cacheList.erase(iter++);
		}
		else {
			++iter;
		}
	}

	RemoveFile(server, path, filename);
}

void CDirectoryCache::Rename(const CServer& server, const CServerPath& pathFrom, const wxString& fileFrom, const CServerPath& pathTo, const wxString& fileTo)
{
	scoped_lock lock(mutex_);

	tServerIter sit = GetServerEntry(server);
	if (sit == m_serverList.end())
		return;

	tCacheIter iter;
	bool is_outdated = false;
	bool found = Lookup(iter, sit, pathFrom, true, is_outdated);
	if (found)
	{
		CDirectoryListing& listing = iter->listing;
		if (pathFrom == pathTo)
		{
			RemoveFile(server, pathFrom, fileTo);
			unsigned int i;
			for (i = 0; i < listing.GetCount(); i++)
			{
				if (listing[i].name == fileFrom)
					break;
			}
			if (i != listing.GetCount())
			{
				if (listing[i].is_dir())
				{
					RemoveDir(server, pathFrom, fileFrom, CServerPath());
					RemoveDir(server, pathFrom, fileTo, CServerPath());
					UpdateFile(server, pathFrom, fileTo, true, dir);
				}
				else
				{
					listing[i].name = fileTo;
					listing[i].flags |= CDirentry::flag_unsure;
					listing.m_flags |= CDirectoryListing::unsure_unknown;
					listing.ClearFindMap();
				}
			}
			return;
		}
		else {
			unsigned int i;
			for (i = 0; i < listing.GetCount(); i++) {
				if (listing[i].name == fileFrom)
					break;
			}
			if (i != listing.GetCount()) {
				if (listing[i].is_dir()) {
					RemoveDir(server, pathFrom, fileFrom, CServerPath());
					UpdateFile(server, pathTo, fileTo, true, dir);
				}
				else {
					RemoveFile(server, pathFrom, fileFrom);
					UpdateFile(server, pathTo, fileTo, true, file);
				}
			}
			return;
		}
	}

	// We know nothing, be on the safe side and invalidate everything.
	InvalidateServer(server);
}

CDirectoryCache::tServerIter CDirectoryCache::CreateServerEntry(const CServer& server)
{
	for (tServerIter iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (iter->server == server)
			return iter;
	}
	m_serverList.emplace_back(server);

	return --m_serverList.end();
}

CDirectoryCache::tServerIter CDirectoryCache::GetServerEntry(const CServer& server)
{
	tServerIter iter;
	for (iter = m_serverList.begin(); iter != m_serverList.end(); ++iter) {
		if (iter->server == server)
			break;
	}

	return iter;
}

void CDirectoryCache::UpdateLru(tServerIter const& sit, tCacheIter const& cit)
{
	tLruList::iterator* lruIt = (tLruList::iterator*)cit->lruIt;
	if (lruIt) {
		m_leastRecentlyUsedList.splice(m_leastRecentlyUsedList.end(), m_leastRecentlyUsedList, *lruIt);
		**lruIt = std::make_pair(sit, cit);
	}
	else
		cit->lruIt = (void*)new tLruList::iterator(m_leastRecentlyUsedList.emplace(m_leastRecentlyUsedList.end(), sit, cit));
}

void CDirectoryCache::Prune()
{
	while ((m_leastRecentlyUsedList.size() > 50000) ||
		(m_totalFileCount > 1000000 && m_leastRecentlyUsedList.size() > 1000) ||
		(m_totalFileCount > 5000000 && m_leastRecentlyUsedList.size() > 100))
	{
		tFullEntryPosition pos = m_leastRecentlyUsedList.front();
		tLruList::iterator* lruIt = (tLruList::iterator*)pos.second->lruIt;
		delete lruIt;

		m_totalFileCount -= pos.second->listing.GetCount();

		pos.first->cacheList.erase(pos.second);
		if (pos.first->cacheList.empty())
			m_serverList.erase(pos.first);

		m_leastRecentlyUsedList.pop_front();
	}
}
