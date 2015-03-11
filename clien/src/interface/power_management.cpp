#include <filezilla.h>
#include "power_management.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#ifdef WITH_LIBDBUS
#include "../dbus/power_management_inhibitor.h"
#endif
#ifdef __WXMAC__
	// >= 10.5 Required for Power Management
	#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
		#include <IOKit/pwr_mgt/IOPMLib.h>
	#endif
#endif

CPowerManagement* CPowerManagement::m_pPowerManagement = 0;

void CPowerManagement::Create(CMainFrame* pMainFrame)
{
	if (!m_pPowerManagement)
		m_pPowerManagement = new CPowerManagement(pMainFrame);
}

void CPowerManagement::Destroy()
{
	delete m_pPowerManagement;
	m_pPowerManagement = 0;
}

CPowerManagement::CPowerManagement(CMainFrame* pMainFrame)
	: CStateEventHandler(0)
{
	m_pMainFrame = pMainFrame;

	CContextManager::Get()->RegisterHandler(this, STATECHANGE_QUEUEPROCESSING, false, false);
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOTE_IDLE, false, false);

	m_busy = false;

#ifdef WITH_LIBDBUS
	m_inhibitor = new CPowerManagementInhibitor();
#endif
}

CPowerManagement::~CPowerManagement()
{
#ifdef WITH_LIBDBUS
	delete m_inhibitor;
#endif
}

void CPowerManagement::OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString& data, const void* data2)
{
	if (m_pMainFrame->GetQueue()->IsActive())
	{
		DoSetBusy();
		return;
	}

	const std::vector<CState*> *states = CContextManager::Get()->GetAllStates();
	for (std::vector<CState*>::const_iterator iter = states->begin(); iter != states->end(); ++iter)
	{
		if (!(*iter)->IsRemoteIdle())
		{
			DoSetBusy();
			return;
		}
	}

	DoSetIdle();
}

void CPowerManagement::DoSetBusy()
{
	if (m_busy)
		return;

	if (!COptions::Get()->GetOptionVal(OPTION_PREVENT_IDLESLEEP))
		return;

	m_busy = true;

#ifdef __WXMSW__
	SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
#elif defined(WITH_LIBDBUS)
	m_inhibitor->RequestBusy();
#elif defined(__WXMAC__)
	// >= 10.5 Required for Power Management
	#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
		IOReturn success = IOPMAssertionCreate(kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn, &m_assertionID);
		if (success != kIOReturnSuccess)
			m_busy = false;
	#endif
#endif
}

void CPowerManagement::DoSetIdle()
{
	if (!m_busy)
		return;
	m_busy = false;

#ifdef __WXMSW__
	SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(WITH_LIBDBUS)
	m_inhibitor->RequestIdle();
#elif defined(__WXMAC__)
	// >= 10.5 Required for Power Management
	#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
		IOPMAssertionRelease(m_assertionID);
	#endif
#endif
}

bool CPowerManagement::IsSupported()
{
#ifdef __WXMSW__
	return true;
#endif
#if defined(__WXMAC__) &&  MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5
	return true;
#endif
#ifdef WITH_LIBDBUS
	return true;
#endif

	return false;
}
