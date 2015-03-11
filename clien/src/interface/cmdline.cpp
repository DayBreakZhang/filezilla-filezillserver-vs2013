#include <filezilla.h>
#include "cmdline.h"

CCommandLine::CCommandLine(int argc, wxChar** argv)
{
	m_parser.AddSwitch(_T("h"), _T("help"), _("Shows this help dialog"), wxCMD_LINE_OPTION_HELP);
	m_parser.AddSwitch(_T("s"), _T("site-manager"), _("Start with opened Site Manager"));
	m_parser.AddOption(_T("c"), _T("site"), _("Connect to specified Site Manager site"));
	m_parser.AddOption(_T("a"), _T("local"), _("Starts the local site in the given path"));

	wxString desc = wxString::Format(_("Logontype, can only be used together with FTP URL. Argument has to be either '%s' or '%s'"), _T("ask"), _T("interactive"));
	m_parser.AddOption(_T("l"), _T("logontype"), desc);

#ifdef __WXMSW__
	m_parser.AddSwitch(_T(""), _T("close"), _("Close all running instances of FileZilla"));
#endif
	m_parser.AddSwitch(_T(""), _T("verbose"), _("Verbose log messages from wxWidgets"));
	m_parser.AddSwitch(_T("v"), _T("version"), _("Print version information to stdout and exit"));
	m_parser.AddSwitch(_T(""), _T("debug-startup"), _("Print diagnostic information related to startup of FileZilla"));
	wxString str = _T("<");
	str += _("FTP URL");
	str += _T(">");
	m_parser.AddParam(str, wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	m_parser.SetCmdLine(argc, argv);

	m_parser.SetSwitchChars(_T("-"));
}

bool CCommandLine::HasSwitch(enum CCommandLine::t_switches s) const
{
	if (s == sitemanager)
		return m_parser.Found(_T("s"));
#ifdef __WXMSW__
	else if (s == close)
		return m_parser.Found(_T("close"));
#endif
	else if (s == version)
		return m_parser.Found(_T("v"));
	else if (s == debug_startup)
		return m_parser.Found(_T("debug-startup"));

	return false;
}

wxString CCommandLine::GetOption(enum CCommandLine::t_option option) const
{
	wxString value;
	switch (option)
	{
	case site:
		if (m_parser.Found(_T("c"), &value))
			return value;
		break;
	case logontype:
		if (m_parser.Found(_T("l"), &value))
			return value;
		break;
	case local:
		if (m_parser.Found(_T("a"), &value))
			return value;
		break;
	}

	return wxString();
}

bool CCommandLine::Parse()
{
	int res = m_parser.Parse(false);
	if (res != 0)
		return false;

	if (HasSwitch(sitemanager) && !GetOption(site).empty())
	{
		wxMessageBoxEx(_("-s and -c cannot be present at the same time."), _("Syntax error in command line"));
		return false;
	}

	if (HasSwitch(sitemanager) && m_parser.GetParamCount())
	{
		wxMessageBoxEx(_("-s cannot be used together with an FTP URL."), _("Syntax error in command line"));
		return false;
	}

	if (!GetOption(site).empty() && m_parser.GetParamCount())
	{
		wxMessageBoxEx(_("-c cannot be used together with an FTP URL."), _("Syntax error in command line"));
		return false;
	}

	wxString type = GetOption(logontype);
	if (!type.empty())
	{
		if (!m_parser.GetParamCount())
		{
			wxMessageBoxEx(_("-l can only be used together with an FTP URL."), _("Syntax error in command line"));
			return false;
		}

		if (type != _T("ask") && type != _T("interactive"))
		{
			wxMessageBoxEx(_("Logontype has to be either 'ask' or 'interactive' (without the quotes)."), _("Syntax error in command line"));
			return false;
		}
	}

	if (m_parser.Found(_T("verbose")))
		wxLog::SetVerbose(true);

	return true;
}

void CCommandLine::DisplayUsage()
{
	m_parser.Usage();
}

wxString CCommandLine::GetParameter() const
{
	if (!m_parser.GetParamCount())
		return wxString();

	return m_parser.GetParam();
}
