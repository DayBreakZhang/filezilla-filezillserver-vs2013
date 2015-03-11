#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"

bool COptionsPage::CreatePage(COptions* pOptions, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize)
{
	m_pOwner = pOwner;
	m_pOptions = pOptions;

	if (!wxXmlResource::Get()->LoadPanel(this, parent, GetResourceName()))
		return false;

	wxSize size = GetSize();

#ifdef __WXGTK__
	// wxStaticBox draws its own border coords -1.
	// Adjust this window so that the left border is fully visible.
	Move(1, 0);
	size.x += 1;
#endif

	if (size.GetWidth() > maxSize.GetWidth())
		maxSize.SetWidth(size.GetWidth());
	if (size.GetHeight() > maxSize.GetHeight())
		maxSize.SetHeight(size.GetHeight());

	m_was_selected = false;

	return true;
}

void COptionsPage::SetCheck(int id, bool checked, bool& failure)
{
	wxCheckBox* pCheckBox = wxDynamicCast(FindWindow(id), wxCheckBox);
	if (!pCheckBox)
	{
		failure = true;
		return;
	}

	pCheckBox->SetValue(checked);
}

void COptionsPage::SetCheckFromOption(int control_id, int option_id, bool& failure)
{
	SetCheck(control_id, m_pOptions->GetOptionVal(option_id) != 0, failure);
}

bool COptionsPage::GetCheck(int id) const
{
	wxCheckBox* pCheckBox = wxDynamicCast(FindWindow(id), wxCheckBox);
	wxASSERT(pCheckBox);

	return pCheckBox->GetValue();
}

void COptionsPage::SetOptionFromCheck(int control_id, int option_id)
{
	m_pOptions->SetOption(option_id, GetCheck(control_id) ? 1 : 0);
}

void COptionsPage::SetTextFromOption(int ctrlId, int optionId, bool& failure)
{
	if (ctrlId == -1)
	{
		failure = true;
		return;
	}

	wxTextCtrl* pTextCtrl = wxDynamicCast(FindWindow(ctrlId), wxTextCtrl);
	if (!pTextCtrl)
	{
		failure = true;
		return;
	}

	const wxString& text = m_pOptions->GetOption(optionId);
	pTextCtrl->ChangeValue(text);
}

wxString COptionsPage::GetText(int id) const
{
	wxTextCtrl* pTextCtrl = wxDynamicCast(FindWindow(id), wxTextCtrl);
	wxASSERT(pTextCtrl);

	return pTextCtrl->GetValue();
}

bool COptionsPage::SetText(int id, const wxString& text, bool& failure)
{
	wxTextCtrl* pTextCtrl = wxDynamicCast(FindWindow(id), wxTextCtrl);
	if (!pTextCtrl)
	{
		failure = true;
		return false;
	}

	pTextCtrl->ChangeValue(text);

	return true;
}

void COptionsPage::SetRCheck(int id, bool checked, bool& failure)
{
	wxRadioButton* pRadioButton = wxDynamicCast(FindWindow(id), wxRadioButton);
	if (!pRadioButton)
	{
		failure = true;
		return;
	}

	pRadioButton->SetValue(checked);
}

bool COptionsPage::GetRCheck(int id) const
{
	wxRadioButton* pRadioButton = wxDynamicCast(FindWindow(id), wxRadioButton);
	wxASSERT(pRadioButton);

	return pRadioButton->GetValue();
}

void COptionsPage::SetStaticText(int id, const wxString& text, bool& failure)
{
	wxStaticText* pStaticText = wxDynamicCast(FindWindow(id), wxStaticText);
	if (!pStaticText)
	{
		failure = true;
		return;
	}

	pStaticText->SetLabel(text);
}

wxString COptionsPage::GetStaticText(int id) const
{
	wxStaticText* pStaticText = wxDynamicCast(FindWindow(id), wxStaticText);
	wxASSERT(pStaticText);

	return pStaticText->GetLabel();
}

void COptionsPage::ReloadSettings()
{
	m_pOwner->LoadSettings();
}

void COptionsPage::SetOptionFromText(int ctrlId, int optionId)
{
	const wxString& value = GetText(ctrlId);
	m_pOptions->SetOption(optionId, value);
}

void COptionsPage::SetIntOptionFromText(int ctrlId, int optionId)
{
	const wxString& value = GetText(ctrlId);

	long n;
	wxCHECK_RET(value.ToLong(&n), _T("Some options page did not validate user input!"));

	m_pOptions->SetOption(optionId, n);
}

void COptionsPage::SetChoice(int id, int selection, bool& failure)
{
	if (selection < -1)
	{
		failure = true;
		return;
	}

	wxChoice* pChoice = wxDynamicCast(FindWindow(id), wxChoice);
	if (!pChoice)
	{
		failure = true;
		return;
	}

	if (selection >= (int)pChoice->GetCount())
	{
		failure = true;
		return;
	}

	pChoice->SetSelection(selection);
}

int COptionsPage::GetChoice(int id) const
{
	wxChoice* pChoice = wxDynamicCast(FindWindow(id), wxChoice);
	wxASSERT(pChoice);
	if (!pChoice)
		return 0;

	return pChoice->GetSelection();
}

bool COptionsPage::DisplayError(const wxString& controlToFocus, const wxString& error)
{
	int id = wxXmlResource::GetXRCID(controlToFocus);
	if (id == -1)
		DisplayError(0, error);
	else
		DisplayError(FindWindow(id), error);

	return false;
}

bool COptionsPage::DisplayError(wxWindow* pWnd, const wxString& error)
{
	if (pWnd)
		pWnd->SetFocus();

	wxMessageBoxEx(error, _("Failed to validate settings"), wxICON_EXCLAMATION, this);

	return false;
}

bool COptionsPage::Display()
{
	if (!m_was_selected)
	{
		if (!OnDisplayedFirstTime())
			return false;
		m_was_selected = true;
	}
	Show();

	return true;
}

bool COptionsPage::OnDisplayedFirstTime()
{
	return true;
}
