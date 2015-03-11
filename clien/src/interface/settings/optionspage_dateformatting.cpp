#include <filezilla.h>

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_dateformatting.h"

BEGIN_EVENT_TABLE(COptionsPageDateFormatting, COptionsPage)
EVT_RADIOBUTTON(wxID_ANY, COptionsPageDateFormatting::OnRadioChanged)
END_EVENT_TABLE()

bool COptionsPageDateFormatting::LoadPage()
{
	bool failure = false;

	const wxString& dateFormat = m_pOptions->GetOption(OPTION_DATE_FORMAT);
	if (dateFormat == _T("1"))
		SetRCheck(XRCID("ID_DATEFORMAT_ISO"), true, failure);
	else if (!dateFormat.empty() && dateFormat[0] == '2')
	{
		SetRCheck(XRCID("ID_DATEFORMAT_CUSTOM"), true, failure);
		SetText(XRCID("ID_CUSTOM_DATEFORMAT"), dateFormat.Mid(1), failure);
	}
	else
		SetRCheck(XRCID("ID_DATEFORMAT_DEFAULT"), true, failure);

	const wxString& timeFormat = m_pOptions->GetOption(OPTION_TIME_FORMAT);
	if (timeFormat == _T("1"))
		SetRCheck(XRCID("ID_TIMEFORMAT_ISO"), true, failure);
	else if (!timeFormat.empty() && timeFormat[0] == '2')
	{
		SetRCheck(XRCID("ID_TIMEFORMAT_CUSTOM"), true, failure);
		SetText(XRCID("ID_CUSTOM_TIMEFORMAT"), timeFormat.Mid(1), failure);
	}
	else
		SetRCheck(XRCID("ID_TIMEFORMAT_DEFAULT"), true, failure);

	if (!failure)
		SetCtrlState();

	return !failure;
}

bool COptionsPageDateFormatting::SavePage()
{
	wxString dateFormat;
	if (GetRCheck(XRCID("ID_DATEFORMAT_CUSTOM")))
		dateFormat = _T("2") + XRCCTRL(*this, "ID_CUSTOM_DATEFORMAT", wxTextCtrl)->GetValue();
	else if (GetRCheck(XRCID("ID_DATEFORMAT_ISO")))
		dateFormat = _T("1");
	else
		dateFormat = _T("0");
	m_pOptions->SetOption(OPTION_DATE_FORMAT, dateFormat);

	wxString timeFormat;
	if (GetRCheck(XRCID("ID_TIMEFORMAT_CUSTOM")))
		timeFormat = _T("2") + XRCCTRL(*this, "ID_CUSTOM_TIMEFORMAT", wxTextCtrl)->GetValue();
	else if (GetRCheck(XRCID("ID_TIMEFORMAT_ISO")))
		timeFormat = _T("1");
	else
		timeFormat = _T("0");
	m_pOptions->SetOption(OPTION_TIME_FORMAT, timeFormat);

	return true;
}

bool COptionsPageDateFormatting::Validate()
{
	if (GetRCheck(XRCID("ID_DATEFORMAT_CUSTOM"))) {
		wxString const dateformat = XRCCTRL(*this, "ID_CUSTOM_DATEFORMAT", wxTextCtrl)->GetValue();
		if (dateformat.empty()) {
			return DisplayError(_T("ID_CUSTOM_DATEFORMAT"), _("Please enter a custom date format."));
		}
		if (!CDateTime::VerifyFormat(dateformat)) {
			return DisplayError(_T("ID_CUSTOM_DATEFORMAT"), _("The custom date format is invalid or contains unsupported format specifiers."));
		}
	}

	if (GetRCheck(XRCID("ID_TIMEFORMAT_CUSTOM"))) {
		wxString const timeformat = XRCCTRL(*this, "ID_CUSTOM_TIMEFORMAT", wxTextCtrl)->GetValue();
		if (timeformat.empty()) {
			return DisplayError(_T("ID_CUSTOM_TIMEFORMAT"), _("Please enter a custom time format."));
		}
		if (!CDateTime::VerifyFormat(timeformat)) {
			return DisplayError(_T("ID_CUSTOM_TIMEFORMAT"), _("The custom time format is invalid or contains unsupported format specifiers."));
		}
	}

	return true;
}

void COptionsPageDateFormatting::OnRadioChanged(wxCommandEvent& event)
{
	SetCtrlState();
}

void COptionsPageDateFormatting::SetCtrlState()
{
	XRCCTRL(*this, "ID_CUSTOM_DATEFORMAT", wxTextCtrl)->Enable(GetRCheck(XRCID("ID_DATEFORMAT_CUSTOM")));
	XRCCTRL(*this, "ID_CUSTOM_TIMEFORMAT", wxTextCtrl)->Enable(GetRCheck(XRCID("ID_TIMEFORMAT_CUSTOM")));
}
