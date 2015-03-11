#include <filezilla.h>
#include "sizeformatting.h"
#include "speedlimits_dialog.h"
#include "Options.h"

BEGIN_EVENT_TABLE(CSpeedLimitsDialog, wxDialogEx)
EVT_BUTTON(wxID_OK, CSpeedLimitsDialog::OnOK)
EVT_CHECKBOX(XRCID("ID_ENABLE_SPEEDLIMITS"), CSpeedLimitsDialog::OnToggleEnable)
END_EVENT_TABLE()

void CSpeedLimitsDialog::Run(wxWindow* parent)
{
	if (!Load(parent, _T("ID_SPEEDLIMITS")))
		return;

	int downloadlimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_INBOUND);
	int uploadlimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_OUTBOUND);
	bool enable = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) != 0;
	if (!downloadlimit && !uploadlimit)
		enable = false;

	XRCCTRL(*this, "ID_ENABLE_SPEEDLIMITS", wxCheckBox)->SetValue(enable);

	wxTextCtrl* pCtrl = XRCCTRL(*this, "ID_DOWNLOADLIMIT", wxTextCtrl);
	pCtrl->Enable(enable);
	pCtrl->SetMaxLength(9);
	pCtrl->ChangeValue(wxString::Format(_T("%d"), downloadlimit));

	pCtrl = XRCCTRL(*this, "ID_UPLOADLIMIT", wxTextCtrl);
	pCtrl->Enable(enable);
	pCtrl->SetMaxLength(9);
	pCtrl->ChangeValue(wxString::Format(_T("%d"), uploadlimit));

	const wxString unit = CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024);

	wxStaticText* pUnit = XRCCTRL(*this, "ID_DOWNLOADLIMIT_UNIT", wxStaticText);
	if (pUnit)
		pUnit->SetLabel(wxString::Format(pUnit->GetLabel(), unit));

	pUnit = XRCCTRL(*this, "ID_UPLOADLIMIT_UNIT", wxStaticText);
	if (pUnit)
		pUnit->SetLabel(wxString::Format(pUnit->GetLabel(), unit));

	ShowModal();
}

void CSpeedLimitsDialog::OnOK(wxCommandEvent& event)
{
	long download, upload;
	wxTextCtrl* pCtrl = XRCCTRL(*this, "ID_DOWNLOADLIMIT", wxTextCtrl);
	if (!pCtrl->GetValue().ToLong(&download) || (download < 0))
	{
		const wxString unit = CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024);
		wxMessageBoxEx(wxString::Format(_("Please enter a download speed limit greater or equal to 0 %s/s."), unit), _("Speed Limits"), wxOK, this);
		return;
	}

	pCtrl = XRCCTRL(*this, "ID_UPLOADLIMIT", wxTextCtrl);
	if (!pCtrl->GetValue().ToLong(&upload) || (upload < 0))
	{
		const wxString unit = CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024);
		wxMessageBoxEx(wxString::Format(_("Please enter an upload speed limit greater or equal to 0 %s/s."), unit), _("Speed Limits"), wxOK, this);
		return;
	}

	COptions::Get()->SetOption(OPTION_SPEEDLIMIT_INBOUND, download);
	COptions::Get()->SetOption(OPTION_SPEEDLIMIT_OUTBOUND, upload);

	bool enable = XRCCTRL(*this, "ID_ENABLE_SPEEDLIMITS", wxCheckBox)->GetValue() ? 1 : 0;
	COptions::Get()->SetOption(OPTION_SPEEDLIMIT_ENABLE, enable && (download || upload));

	EndDialog(wxID_OK);
}

void CSpeedLimitsDialog::OnToggleEnable(wxCommandEvent& event)
{
	XRCCTRL(*this, "ID_DOWNLOADLIMIT", wxTextCtrl)->Enable(event.IsChecked());
	XRCCTRL(*this, "ID_UPLOADLIMIT", wxTextCtrl)->Enable(event.IsChecked());
}
