#include <filezilla.h>
#include "listingcomparison.h"
#include "filter.h"
#include "Options.h"
#include "state.h"

CComparableListing::CComparableListing(wxWindow* pParent)
{
	m_pComparisonManager = 0;
	m_pParent = pParent;

	// Init backgrounds for directory comparison
	wxColour background = m_pParent->GetBackgroundColour();
	if (background.Red() + background.Green() + background.Blue() >= 384)
	{
		// Light background
		m_comparisonBackgrounds[0].SetBackgroundColour(wxColour(255, 128, 128));
		m_comparisonBackgrounds[1].SetBackgroundColour(wxColour(255, 255, 128));
		m_comparisonBackgrounds[2].SetBackgroundColour(wxColour(128, 255, 128));
	}
	else
	{
		// Light background
		m_comparisonBackgrounds[0].SetBackgroundColour(wxColour(192, 64, 64));
		m_comparisonBackgrounds[1].SetBackgroundColour(wxColour(192, 192, 64));
		m_comparisonBackgrounds[2].SetBackgroundColour(wxColour(64, 192, 64));
	}

	m_pOther = 0;
}

bool CComparableListing::IsComparing() const
{
	if (!m_pComparisonManager)
		return false;

	return m_pComparisonManager->IsComparing();
}

void CComparableListing::ExitComparisonMode()
{
	if (!m_pComparisonManager)
		return;

	m_pComparisonManager->ExitComparisonMode();
}

void CComparableListing::RefreshComparison()
{
	if (!m_pComparisonManager)
		return;

	if (!IsComparing())
		return;

	if (!CanStartComparison(0) || !GetOther() || !GetOther()->CanStartComparison(0))
	{
		ExitComparisonMode();
		return;
	}

	m_pComparisonManager->CompareListings();
}

bool CComparisonManager::CompareListings()
{
	if (!m_pLeft || !m_pRight)
		return false;

	CFilterManager filters;
	if (filters.HasActiveFilters() && !filters.HasSameLocalAndRemoteFilters())
	{
		m_pState->NotifyHandlers(STATECHANGE_COMPARISON);
		wxMessageBoxEx(_("Cannot compare directories, different filters for local and remote directories are enabled"), _("Directory comparison failed"), wxICON_EXCLAMATION);
		return false;
	}

	wxString error;
	if (!m_pLeft->CanStartComparison(&error))
	{
		m_pState->NotifyHandlers(STATECHANGE_COMPARISON);
		wxMessageBoxEx(error, _("Directory comparison failed"), wxICON_EXCLAMATION);
		return false;
	}
	if (!m_pRight->CanStartComparison(&error))
	{
		m_pState->NotifyHandlers(STATECHANGE_COMPARISON);
		wxMessageBoxEx(error, _("Directory comparison failed"), wxICON_EXCLAMATION);
		return false;
	}

	const int mode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
	const wxTimeSpan threshold = wxTimeSpan::Minutes( COptions::Get()->GetOptionVal(OPTION_COMPARISON_THRESHOLD) );

	m_pLeft->m_pComparisonManager = this;
	m_pRight->m_pComparisonManager = this;

	m_isComparing = true;

	m_pState->NotifyHandlers(STATECHANGE_COMPARISON);

	m_pLeft->StartComparison();
	m_pRight->StartComparison();

	wxString localFile, remoteFile;
	bool localDir = false;
	bool remoteDir = false;
	wxLongLong localSize, remoteSize;
	CDateTime localDate, remoteDate;

	const int dirSortMode = COptions::Get()->GetOptionVal(OPTION_FILELIST_DIRSORT);

	const bool hide_identical = COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0;

	bool gotLocal = m_pLeft->GetNextFile(localFile, localDir, localSize, localDate);
	bool gotRemote = m_pRight->GetNextFile(remoteFile, remoteDir, remoteSize, remoteDate);

	while (gotLocal && gotRemote)
	{
		int cmp = CompareFiles(dirSortMode, localFile, remoteFile, localDir, remoteDir);
		if (!cmp)
		{
			if (!mode)
			{
				const CComparableListing::t_fileEntryFlags flag = (localDir || localSize == remoteSize) ? CComparableListing::normal : CComparableListing::different;

				if (!hide_identical || flag != CComparableListing::normal || localFile == _T(".."))
				{
					m_pLeft->CompareAddFile(flag);
					m_pRight->CompareAddFile(flag);
				}
			}
			else
			{
				if (!localDate.IsValid() || !remoteDate.IsValid())
				{
					if (!hide_identical || localDate.IsValid() || remoteDate.IsValid() || localFile == _T(".."))
					{
						const CComparableListing::t_fileEntryFlags flag = CComparableListing::normal;
						m_pLeft->CompareAddFile(flag);
						m_pRight->CompareAddFile(flag);
					}
				}
				else
				{
					CComparableListing::t_fileEntryFlags localFlag, remoteFlag;

					int cmp = localDate.Compare(remoteDate);
					if( cmp < 0 )
						localDate += threshold;
					else if( cmp > 0 ) {
						remoteDate += threshold;
					}
					int cmp2 = localDate.Compare(remoteDate);
					if( cmp && cmp == -cmp2) {
						cmp = 0;
					}

					localFlag = CComparableListing::normal;
					remoteFlag = CComparableListing::normal;
					if( cmp < 0 ) {
						remoteFlag = CComparableListing::newer;
					}
					else if( cmp > 0 ) {
						localFlag = CComparableListing::newer;
					}
					if (!hide_identical || localFlag != CComparableListing::normal || remoteFlag != CComparableListing::normal || localFile == _T(".."))
					{
						m_pLeft->CompareAddFile(localFlag);
						m_pRight->CompareAddFile(remoteFlag);
					}
				}
			}
			gotLocal = m_pLeft->GetNextFile(localFile, localDir, localSize, localDate);
			gotRemote = m_pRight->GetNextFile(remoteFile, remoteDir, remoteSize, remoteDate);
			continue;
		}

		if (cmp < 0) {
			m_pLeft->CompareAddFile(CComparableListing::lonely);
			m_pRight->CompareAddFile(CComparableListing::fill);
			gotLocal = m_pLeft->GetNextFile(localFile, localDir, localSize, localDate);
		}
		else {
			m_pLeft->CompareAddFile(CComparableListing::fill);
			m_pRight->CompareAddFile(CComparableListing::lonely);
			gotRemote = m_pRight->GetNextFile(remoteFile, remoteDir, remoteSize, remoteDate);
		}
	}
	while (gotLocal) {
		m_pLeft->CompareAddFile(CComparableListing::lonely);
		m_pRight->CompareAddFile(CComparableListing::fill);
		gotLocal = m_pLeft->GetNextFile(localFile, localDir, localSize, localDate);
	}
	while (gotRemote)
	{
		m_pLeft->CompareAddFile(CComparableListing::fill);
		m_pRight->CompareAddFile(CComparableListing::lonely);
		gotRemote = m_pRight->GetNextFile(remoteFile, remoteDir, remoteSize, remoteDate);
	}

	m_pRight->FinishComparison();
	m_pLeft->FinishComparison();

	return true;
}

int CComparisonManager::CompareFiles(const int dirSortMode, const wxString& local, const wxString& remote, bool localDir, bool remoteDir)
{
	switch (dirSortMode)
	{
	default:
		if (localDir)
		{
			if (!remoteDir)
				return -1;
		}
		else if (remoteDir)
			return 1;
		break;
	case 2:
		// Inline
		break;
	}

#ifdef __WXMSW__
	return local.CmpNoCase(remote);
#else
	return local.Cmp(remote);
#endif

	return 0;
}

CComparisonManager::CComparisonManager(CState* pState)
	: m_pState(pState), m_pLeft(0), m_pRight(0)
{
	m_isComparing = false;
}

void CComparisonManager::SetListings(CComparableListing* pLeft, CComparableListing* pRight)
{
	wxASSERT((pLeft && pRight) || (!pLeft && !pRight));

	if (IsComparing())
		ExitComparisonMode();

	if (m_pLeft)
		m_pLeft->SetOther(0);
	if (m_pRight)
		m_pRight->SetOther(0);

	m_pLeft = pLeft;
	m_pRight = pRight;

	if (m_pLeft)
		m_pLeft->SetOther(m_pRight);
	if (m_pRight)
		m_pRight->SetOther(m_pLeft);
}

void CComparisonManager::ExitComparisonMode()
{
	if (!IsComparing())
		return;

	m_isComparing = false;
	if (m_pLeft)
		m_pLeft->OnExitComparisonMode();
	if (m_pRight)
		m_pRight->OnExitComparisonMode();

	m_pState->NotifyHandlers(STATECHANGE_COMPARISON);
}
