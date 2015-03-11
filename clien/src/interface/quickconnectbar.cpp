#include <filezilla.h>
#include "quickconnectbar.h"
#include "recentserverlist.h"
#include "commandqueue.h"
#include "state.h"
#include "Options.h"
#include "loginmanager.h"
#include "Mainfrm.h"
#include "asksavepassworddialog.h"

BEGIN_EVENT_TABLE(CQuickconnectBar, wxPanel)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_OK"), CQuickconnectBar::OnQuickconnect)
EVT_BUTTON(XRCID("ID_QUICKCONNECT_DROPDOWN"), CQuickconnectBar::OnQuickconnectDropdown)
EVT_MENU(wxID_ANY, CQuickconnectBar::OnMenu)
EVT_TEXT_ENTER(wxID_ANY, CQuickconnectBar::OnQuickconnect)
END_EVENT_TABLE()

CQuickconnectBar::CQuickconnectBar()
	: m_pHost()
	, m_pUser()
	, m_pPass()
	, m_pPort()
	, m_pMainFrame()
{
}

CQuickconnectBar::~CQuickconnectBar()
{
}

bool CQuickconnectBar::Create(CMainFrame* pParent)
{
	m_pMainFrame = pParent;
	if (!wxXmlResource::Get()->LoadPanel(this, pParent, _T("ID_QUICKCONNECTBAR")))
	{
		wxLogError(_("Cannot load Quickconnect bar from resource file"));
		return false;
	}

#ifdef __WXMAC__
	// Under OS X default buttons are toplevel window wide, where under Windows / GTK they stop at the parent panel.
	wxTopLevelWindow *tlw = wxDynamicCast(wxGetTopLevelParent(pParent), wxTopLevelWindow);
	if (tlw)
		tlw->SetDefaultItem(0);
#endif

	m_pHost = XRCCTRL(*this, "ID_QUICKCONNECT_HOST", wxTextCtrl);
	m_pUser = XRCCTRL(*this, "ID_QUICKCONNECT_USER", wxTextCtrl);
	m_pPass = XRCCTRL(*this, "ID_QUICKCONNECT_PASS", wxTextCtrl);
	m_pPort = XRCCTRL(*this, "ID_QUICKCONNECT_PORT", wxTextCtrl);

	if (!m_pHost || !m_pUser || !m_pPass || !m_pPort)
	{
		wxLogError(_("Cannot load Quickconnect bar from resource file"));
		return false;
	}

	return true;
}

void CQuickconnectBar::OnQuickconnect(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || !pState->m_pEngine)
	{
		wxMessageBoxEx(_("FTP Engine not initialized, can't connect"), _("FileZilla Error"), wxICON_EXCLAMATION);
		return;
	}

	wxString host = m_pHost->GetValue();
	wxString user = m_pUser->GetValue();
	wxString pass = m_pPass->GetValue();
	wxString port = m_pPort->GetValue();

	CServer server;

	wxString error;

	CServerPath path;
	if (!server.ParseUrl(host, port, user, pass, error, path))
	{
		wxString msg = _("Could not parse server address:");
		msg += _T("\n");
		msg += error;
		wxMessageBoxEx(msg, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	host = server.FormatHost(true);
	ServerProtocol protocol = server.GetProtocol();
	switch (protocol)
	{
	case FTP:
	case UNKNOWN:
		if (CServer::GetProtocolFromPort(server.GetPort()) != FTP &&
			CServer::GetProtocolFromPort(server.GetPort()) != UNKNOWN)
			host = _T("ftp://") + host;
		break;
	default:
		{
			const wxString prefix = server.GetPrefixFromProtocol(protocol);
			if (!prefix.empty())
				host = prefix + _T("://") + host;
		}
		break;
	}

	m_pHost->SetValue(host);
	if (server.GetPort() != server.GetDefaultPort(server.GetProtocol()))
		m_pPort->SetValue(wxString::Format(_T("%d"), server.GetPort()));
	else
		m_pPort->SetValue(_T(""));

	m_pUser->SetValue(server.GetUser());
	if (server.GetLogonType() != ANONYMOUS)
		m_pPass->SetValue(server.GetPass());
	else
		m_pPass->SetValue(_T(""));

	if (protocol == HTTP || protocol == HTTPS)
	{
		wxString error = _("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP with optional encryption,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over TLS (implicit) and\nftpes:// for FTP over TLS (explicit).");
		wxMessageBoxEx(error, _("Syntax error"), wxICON_EXCLAMATION);
		return;
	}

	if (event.GetId() == 1)
		server.SetBypassProxy(true);

	if (server.GetLogonType() != ANONYMOUS && !CAskSavePasswordDialog::Run(this))
		return;

	if (!m_pMainFrame->ConnectToServer(server, path))
		return;

	CRecentServerList::SetMostRecentServer(server);
}

void CQuickconnectBar::OnQuickconnectDropdown(wxCommandEvent& event)
{
	wxMenu* pMenu = new wxMenu;

	// We have to start with id 1 since menu items with id 0 don't work under OS X
	if (COptions::Get()->GetOptionVal(OPTION_FTP_PROXY_TYPE))
		pMenu->Append(1, _("Connect bypassing proxy settings"));
	pMenu->Append(2, _("Clear quickconnect bar"));
	pMenu->Append(3, _("Clear history"));

	m_recentServers = CRecentServerList::GetMostRecentServers();
	if (!m_recentServers.empty())
	{
		pMenu->AppendSeparator();

		unsigned int i = 0;
		for (std::list<CServer>::const_iterator iter = m_recentServers.begin();
			iter != m_recentServers.end();
			++iter, ++i)
		{
			wxString name(iter->FormatServer());
			name.Replace(_T("&"), _T("&&"));
			pMenu->Append(10 + i, name);
		}
	}
	else
		pMenu->Enable(3, false);

	XRCCTRL(*this, "ID_QUICKCONNECT_DROPDOWN", wxButton)->PopupMenu(pMenu);
	delete pMenu;
	m_recentServers.clear();
}

void CQuickconnectBar::OnMenu(wxCommandEvent& event)
{
	const int id = event.GetId();
	if (id == 1)
		OnQuickconnect(event);
	else if (id == 2)
		ClearFields();
	else if (id == 3)
		CRecentServerList::Clear();

	if (id < 10)
		return;

	unsigned int index = id - 10;
	if (index >= m_recentServers.size())
		return;

	std::list<CServer>::const_iterator iter = m_recentServers.begin();
	std::advance(iter, index);

	CServer server = *iter;
	if (server.GetLogonType() == ASK)
	{
		if (!CLoginManager::Get().GetPassword(server, false))
			return;
	}

	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || !pState->m_pEngine)
	{
		wxMessageBoxEx(_("FTP Engine not initialized, can't connect"), _("FileZilla Error"), wxICON_EXCLAMATION);
		return;
	}

	m_pMainFrame->ConnectToServer(server);
}

void CQuickconnectBar::ClearFields()
{
	m_pHost->SetValue(_T(""));
	m_pPort->SetValue(_T(""));
	m_pUser->SetValue(_T(""));
	m_pPass->SetValue(_T(""));
}
