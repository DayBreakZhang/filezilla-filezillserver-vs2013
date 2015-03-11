#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_filetype.h"

BEGIN_EVENT_TABLE(COptionsPageFiletype, COptionsPage)
EVT_BUTTON(XRCID("ID_ADD"), COptionsPageFiletype::OnAdd)
EVT_BUTTON(XRCID("ID_REMOVE"), COptionsPageFiletype::OnRemove)
EVT_TEXT(XRCID("ID_EXTENSION"), COptionsPageFiletype::OnTextChanged)
EVT_LIST_ITEM_SELECTED(XRCID("ID_EXTENSIONS"), COptionsPageFiletype::OnSelChanged)
EVT_LIST_ITEM_DESELECTED(XRCID("ID_EXTENSIONS"), COptionsPageFiletype::OnSelChanged)
END_EVENT_TABLE()

bool COptionsPageFiletype::LoadPage()
{
	bool failure = false;

	SetCheckFromOption(XRCID("ID_ASCIIWITHOUT"), OPTION_ASCIINOEXT, failure);
	SetCheckFromOption(XRCID("ID_ASCIIDOTFILE"), OPTION_ASCIIDOTFILE, failure);

	if (failure)
		return false;

	if (!FindWindow(XRCID("ID_EXTENSION")) || !FindWindow(XRCID("ID_ADD")) ||
		!FindWindow(XRCID("ID_REMOVE")) || !FindWindow(XRCID("ID_EXTENSIONS")) ||
		!FindWindow(XRCID("ID_TYPE_AUTO")) || !FindWindow(XRCID("ID_TYPE_ASCII")) ||
		!FindWindow(XRCID("ID_TYPE_BINARY")))
		return false;

	int mode = m_pOptions->GetOptionVal(OPTION_ASCIIBINARY);
	if (mode == 1)
		SetRCheck(XRCID("ID_TYPE_ASCII"), true, failure);
	else if (mode == 2)
		SetRCheck(XRCID("ID_TYPE_BINARY"), true, failure);
	else
		SetRCheck(XRCID("ID_TYPE_AUTO"), true, failure);

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_EXTENSIONS", wxListCtrl);
	pListCtrl->ClearAll();
	pListCtrl->InsertColumn(0, wxString());

	wxString extensions = m_pOptions->GetOption(OPTION_ASCIIFILES);
	wxString ext;
	int pos = extensions.Find(_T("|"));
	while (pos != -1) {
		if (!pos) {
			if (!ext.empty()) {
				ext.Replace(_T("\\\\"), _T("\\"));
				pListCtrl->InsertItem(pListCtrl->GetItemCount(), ext);
				ext.clear();
			}
		}
		else if (extensions.c_str()[pos - 1] != '\\') {
			ext += extensions.Left(pos);
			ext.Replace(_T("\\\\"), _T("\\"));
			pListCtrl->InsertItem(pListCtrl->GetItemCount(), ext);
			ext.clear();
		}
		else {
			ext += extensions.Left(pos - 1) + _T("|");
		}
		extensions = extensions.Mid(pos + 1);
		pos = extensions.Find(_T("|"));
	}
	ext += extensions;
	ext.Replace(_T("\\\\"), _T("\\"));
	pListCtrl->InsertItem(pListCtrl->GetItemCount(), ext);

	SetCtrlState();

	return true;
}

bool COptionsPageFiletype::SavePage()
{
	SetOptionFromCheck(XRCID("ID_ASCIIWITHOUT"), OPTION_ASCIINOEXT);
	SetOptionFromCheck(XRCID("ID_ASCIIDOTFILE"), OPTION_ASCIIDOTFILE);

	int mode;
	if (GetRCheck(XRCID("ID_TYPE_ASCII")))
		mode = 1;
	else if (GetRCheck(XRCID("ID_TYPE_BINARY")))
		mode = 2;
	else
		mode = 0;
	m_pOptions->SetOption(OPTION_ASCIIBINARY, mode);

	const wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_EXTENSIONS", wxListCtrl);
	wxASSERT(pListCtrl);

	wxString extensions;

	for (int i = 0; i < pListCtrl->GetItemCount(); i++) {
		wxString ext = pListCtrl->GetItemText(i);
		ext.Replace(_T("\\"), _T("\\\\"));
		ext.Replace(_T("|"), _T("\\|"));
		if (!extensions.empty())
			extensions += _T("|");
		extensions += ext;
	}
	m_pOptions->SetOption(OPTION_ASCIIFILES, extensions);

	return true;
}

bool COptionsPageFiletype::Validate()
{
	return true;
}

void COptionsPageFiletype::SetCtrlState()
{
	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_EXTENSIONS", wxListCtrl);
	wxASSERT(pListCtrl);
	pListCtrl->SetColumnWidth(0, wxLIST_AUTOSIZE);

	FindWindow(XRCID("ID_REMOVE"))->Enable(pListCtrl->GetSelectedItemCount() != 0);
	FindWindow(XRCID("ID_ADD"))->Enable( !GetText(XRCID("ID_EXTENSION")).empty() );
}

void COptionsPageFiletype::OnRemove(wxCommandEvent& event)
{
	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_EXTENSIONS", wxListCtrl);
	wxASSERT(pListCtrl);

	int item = -1;
	item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	while (item != -1)
	{
		pListCtrl->DeleteItem(item);
		item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	}
	SetCtrlState();
}

void COptionsPageFiletype::OnAdd(wxCommandEvent& event)
{
	wxString ext = GetText(XRCID("ID_EXTENSION"));
	if (ext.empty())
	{
		wxBell();
		return;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_EXTENSIONS", wxListCtrl);
	wxASSERT(pListCtrl);

	for (int i = 0; i < pListCtrl->GetItemCount(); i++)
	{
		wxString text = pListCtrl->GetItemText(i);
		if (text == ext)
		{
			DisplayError(0, wxString::Format(_("The extension '%s' does already exist in the list"), ext));
			return;
		}
	}

	pListCtrl->InsertItem(pListCtrl->GetItemCount(), ext);

	SetCtrlState();
}

void COptionsPageFiletype::OnTextChanged(wxCommandEvent& event)
{
	SetCtrlState();
}

void COptionsPageFiletype::OnSelChanged(wxListEvent& event)
{
	SetCtrlState();
}
