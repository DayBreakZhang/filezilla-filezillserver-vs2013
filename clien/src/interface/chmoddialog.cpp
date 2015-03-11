#include <filezilla.h>
#include "chmoddialog.h"

BEGIN_EVENT_TABLE(CChmodDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CChmodDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CChmodDialog::OnCancel)
EVT_TEXT(XRCID("ID_NUMERIC"), CChmodDialog::OnNumericChanged)
EVT_CHECKBOX(XRCID("ID_RECURSE"), CChmodDialog::OnRecurseChanged)
END_EVENT_TABLE()

CChmodDialog::CChmodDialog()
	: m_noUserTextChange()
	, lastChangedNumeric()
	, m_recursive()
	, m_applyType()
{
	for (int i = 0; i < 9; ++i)
	{
		m_checkBoxes[i] = 0;
		m_permissions[i] = 0;
	}
}

bool CChmodDialog::Create(wxWindow* parent, int fileCount, int dirCount,
						  const wxString& name, const char permissions[9])
{
	m_noUserTextChange = false;
	lastChangedNumeric = false;

	memcpy(m_permissions, permissions, 9);

	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	SetParent(parent);

	wxString title;
	if (!dirCount) {
		if (fileCount == 1) {
			title = wxString::Format(_("Please select the new attributes for the file \"%s\"."), name);
		}
		else
			title = _("Please select the new attributes for the selected files.");
	}
	else {
		if (!fileCount) {
			if (dirCount == 1) {
				title = wxString::Format(_("Please select the new attributes for the directory \"%s\"."), name);
			}
			else
				title = _("Please select the new attributes for the selected directories.");
		}
		else
			title = _("Please select the new attributes for the selected files and directories.");
	}

	if (!Load(parent, _T("ID_CHMODDIALOG"))) {
		return false;
	}

	if (!SetChildLabel(XRCID("ID_DESC"), title, 300)) {
		wxFAIL_MSG(_T("Could not set ID_DESC"));
	}

	if (!XRCCTRL(*this, "wxID_OK", wxButton))
		return false;

	if (!XRCCTRL(*this, "wxID_CANCEL", wxButton))
		return false;

	if (!XRCCTRL(*this, "ID_NUMERIC", wxTextCtrl))
		return false;

	if (!WrapText(this, XRCID("ID_NUMERICTEXT"), 300)) {
		wxFAIL_MSG(_T("Wrapping of ID_NUMERICTEXT failed"));
	}

	wxCheckBox* pRecurse = XRCCTRL(*this, "ID_RECURSE", wxCheckBox);
	wxRadioButton* pApplyAll = XRCCTRL(*this, "ID_APPLYALL", wxRadioButton);
	wxRadioButton* pApplyFiles = XRCCTRL(*this, "ID_APPLYFILES", wxRadioButton);
	wxRadioButton* pApplyDirs = XRCCTRL(*this, "ID_APPLYDIRS", wxRadioButton);
	if (!pRecurse || !pApplyAll || !pApplyFiles || !pApplyDirs)
		return false;

	if (!dirCount) {
		pRecurse->Hide();
		pApplyAll->Hide();
		pApplyFiles->Hide();
		pApplyDirs->Hide();
	}

	pApplyAll->Enable(false);
	pApplyFiles->Enable(false);
	pApplyDirs->Enable(false);

	const wxChar* IDs[9] = { _T("ID_OWNERREAD"), _T("ID_OWNERWRITE"), _T("ID_OWNEREXECUTE"),
						   _T("ID_GROUPREAD"), _T("ID_GROUPWRITE"), _T("ID_GROUPEXECUTE"),
						   _T("ID_PUBLICREAD"), _T("ID_PUBLICWRITE"), _T("ID_PUBLICEXECUTE")
						 };

	for (int i = 0; i < 9; ++i) {
		int id = wxXmlResource::GetXRCID(IDs[i]);
		m_checkBoxes[i] = wxDynamicCast(FindWindow(id), wxCheckBox);

		if (!m_checkBoxes[i])
			return false;

		Connect(id, wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(CChmodDialog::OnCheckboxClick));

		switch (permissions[i])
		{
		default:
		case 0:
			m_checkBoxes[i]->Set3StateValue(wxCHK_UNDETERMINED);
			break;
		case 1:
			m_checkBoxes[i]->Set3StateValue(wxCHK_UNCHECKED);
			break;
		case 2:
			m_checkBoxes[i]->Set3StateValue(wxCHK_CHECKED);
			break;
		}
	}

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	wxCommandEvent evt;
	OnCheckboxClick(evt);

	return true;
}

void CChmodDialog::OnOK(wxCommandEvent&)
{
	wxCheckBox* pRecurse = XRCCTRL(*this, "ID_RECURSE", wxCheckBox);
	m_recursive = pRecurse->GetValue();
	wxRadioButton* pApplyFiles = XRCCTRL(*this, "ID_APPLYFILES", wxRadioButton);
	wxRadioButton* pApplyDirs = XRCCTRL(*this, "ID_APPLYDIRS", wxRadioButton);
	if (pApplyFiles->GetValue())
		m_applyType = 1;
	else if (pApplyDirs->GetValue())
		m_applyType = 2;
	else
		m_applyType = 0;
	EndModal(wxID_OK);
}

void CChmodDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);
}

void CChmodDialog::OnCheckboxClick(wxCommandEvent&)
{
	lastChangedNumeric = false;
	for (int i = 0; i < 9; i++)
	{
		wxCheckBoxState state = m_checkBoxes[i]->Get3StateValue();
		switch (state)
		{
		default:
		case wxCHK_UNDETERMINED:
			m_permissions[i] = 0;
			break;
		case wxCHK_UNCHECKED:
			m_permissions[i] = 1;
			break;
		case wxCHK_CHECKED:
			m_permissions[i] = 2;
			break;
		}
	}

	wxString numericValue;
	for (int i = 0; i < 3; i++)
	{
		if (!m_permissions[i * 3] || !m_permissions[i * 3 + 1] || !m_permissions[i * 3 + 2])
		{
			numericValue += 'x';
			continue;
		}

		numericValue += wxString::Format(_T("%d"), (m_permissions[i * 3] - 1) * 4 + (m_permissions[i * 3 + 1] - 1) * 2 + (m_permissions[i * 3 + 2] - 1) * 1);
	}

	wxTextCtrl *pTextCtrl = XRCCTRL(*this, "ID_NUMERIC", wxTextCtrl);
	wxString oldValue = pTextCtrl->GetValue();

	m_noUserTextChange = true;
	pTextCtrl->SetValue(oldValue.Left(oldValue.Length() - 3) + numericValue);
	m_noUserTextChange = false;
	oldNumeric = numericValue;
}

void CChmodDialog::OnNumericChanged(wxCommandEvent&)
{
	if (m_noUserTextChange)
		return;

	lastChangedNumeric = true;

	wxTextCtrl *pTextCtrl = XRCCTRL(*this, "ID_NUMERIC", wxTextCtrl);
	wxString numeric = pTextCtrl->GetValue();
	if (numeric.Length() < 3)
		return;

	numeric = numeric.Right(3);
	for (int i = 0; i < 3; i++)
	{
		if ((numeric[i] < '0' || numeric[i] > '9') && numeric[i] != 'x')
			return;
	}
	for (int i = 0; i < 3; i++)
	{
		if (!oldNumeric.empty() && numeric[i] == oldNumeric[i])
			continue;
		if (numeric[i] == 'x')
		{
			m_permissions[i * 3] = 0;
			m_permissions[i * 3 + 1] = 0;
			m_permissions[i * 3 + 2] = 0;
		}
		else
		{
			int value = numeric[i] - '0';
			m_permissions[i * 3] = (value & 4) ? 2 : 1;
			m_permissions[i * 3 + 1] = (value & 2) ? 2 : 1;
			m_permissions[i * 3 + 2] = (value & 1) ? 2 : 1;
		}
	}

	oldNumeric = numeric;

	for (int i = 0; i < 9; i++)
	{
		switch (m_permissions[i])
		{
		default:
		case 0:
			m_checkBoxes[i]->Set3StateValue(wxCHK_UNDETERMINED);
			break;
		case 1:
			m_checkBoxes[i]->Set3StateValue(wxCHK_UNCHECKED);
			break;
		case 2:
			m_checkBoxes[i]->Set3StateValue(wxCHK_CHECKED);
			break;
		}
	}
}

wxString CChmodDialog::GetPermissions(const char* previousPermissions, bool dir)
{
	// Construct a new permission string

	wxTextCtrl *pTextCtrl = XRCCTRL(*this, "ID_NUMERIC", wxTextCtrl);
	wxString numeric = pTextCtrl->GetValue();
	if (numeric.Length() < 3)
		return numeric;

	for (unsigned int i = numeric.Length() - 3; i < numeric.Length(); i++)
	{
		if ((numeric[i] < '0' || numeric[i] > '9') && numeric[i] != 'x')
			return numeric;
	}
	if (!previousPermissions)
	{
		// Use default of  (0...0)755 for dirs and
		// 644 for files
		if (numeric[numeric.Length() - 1] == 'x')
			numeric[numeric.Length() - 1] = dir ? '5' : '4';
		if (numeric[numeric.Length() - 2] == 'x')
			numeric[numeric.Length() - 2] = dir ? '5' : '4';
		if (numeric[numeric.Length() - 3] == 'x')
			numeric[numeric.Length() - 3] = dir ? '7' : '6';
		numeric.Replace(_T("x"), _T("0"));
		return numeric;
	}

	// 2 set, 1 unset, 0 keep

	const char defaultPerms[9] = { 2, 2, 2, 2, 1, 2, 2, 1, 2 };
	char perms[9];
	memcpy(perms, m_permissions, 9);

	wxString permission = numeric.Left(numeric.Length() - 3);
	unsigned int k = 0;
	for (unsigned int i = numeric.Length() - 3; i < numeric.Length(); i++, k++)
	{
		for (unsigned int j = k * 3; j < k * 3 + 3; j++)
		{
			if (!perms[j])
			{
				if (previousPermissions[j])
					perms[j] = previousPermissions[j];
				else
					perms[j] = defaultPerms[j];
			}
		}
		permission += wxString::Format(_T("%d"), (int)(perms[k * 3] - 1) * 4 + (perms[k * 3 + 1] - 1) * 2 + (perms[k * 3 + 2] - 1) * 1);
	}

	return permission;
}

bool CChmodDialog::Recursive() const
{
	return m_recursive;
}

void CChmodDialog::OnRecurseChanged(wxCommandEvent&)
{
	wxCheckBox* pRecurse = XRCCTRL(*this, "ID_RECURSE", wxCheckBox);
	wxRadioButton* pApplyAll = XRCCTRL(*this, "ID_APPLYALL", wxRadioButton);
	wxRadioButton* pApplyFiles = XRCCTRL(*this, "ID_APPLYFILES", wxRadioButton);
	wxRadioButton* pApplyDirs = XRCCTRL(*this, "ID_APPLYDIRS", wxRadioButton);
	pApplyAll->Enable(pRecurse->GetValue());
	pApplyFiles->Enable(pRecurse->GetValue());
	pApplyDirs->Enable(pRecurse->GetValue());
}

bool CChmodDialog::ConvertPermissions(wxString rwx, char* permissions)
{
	if (!permissions)
		return false;

	int pos = rwx.Find('(');
	if (pos != -1 && rwx.Last() == ')') {
		// MLSD permissions:
		//   foo (0644)
		rwx.RemoveLast();
		rwx = rwx.Mid(pos + 1);
	}

	if (rwx.Len() < 3)
		return false;
	size_t i;
	for (i = 0; i < rwx.Len(); i++)
		if (rwx[i] < '0' || rwx[i] > '9')
			break;
	if (i == rwx.Len())
	{
		// Mode, e.g. 0723
		for (i = 0; i < 3; i++)
		{
			int m = rwx[rwx.Len() - 3 + i] - '0';

			for (int j = 0; j < 3; j++)
			{
				if (m & (4 >> j))
					permissions[i * 3 + j] = 2;
				else
					permissions[i * 3 + j] = 1;
			}
		}

		return true;
	}

	const unsigned char permchars[3] = {'r', 'w', 'x'};

	if (rwx.Length() != 10)
		return false;

	for (int i = 0; i < 9; i++)
	{
		bool set = rwx[i + 1] == permchars[i % 3];
		permissions[i] = set ? 2 : 1;
	}
	if (rwx[3] == 's')
		permissions[2] = 2;
	if (rwx[6] == 's')
		permissions[5] = 2;
	if (rwx[9] == 't')
		permissions[8] = 2;

	return true;
}
