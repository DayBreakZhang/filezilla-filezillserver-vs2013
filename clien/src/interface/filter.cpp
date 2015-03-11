#include <filezilla.h>
#include "filter.h"
#include "filteredit.h"
#include "filezillaapp.h"
#include "inputdialog.h"
#include "ipcmutex.h"
#include "local_filesys.h"
#include "Mainfrm.h"
#include "Options.h"
#include "state.h"
#include "xmlfunctions.h"

#include <wx/regex.h>

bool CFilterManager::m_loaded = false;
std::vector<CFilter> CFilterManager::m_globalFilters;
std::vector<CFilterSet> CFilterManager::m_globalFilterSets;
unsigned int CFilterManager::m_globalCurrentFilterSet = 0;
bool CFilterManager::m_filters_disabled = false;

BEGIN_EVENT_TABLE(CFilterDialog, wxDialogEx)
EVT_BUTTON(XRCID("wxID_OK"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("wxID_CANCEL"), CFilterDialog::OnCancel)
EVT_BUTTON(XRCID("wxID_APPLY"), CFilterDialog::OnOkOrApply)
EVT_BUTTON(XRCID("ID_EDIT"), CFilterDialog::OnEdit)
EVT_CHECKLISTBOX(wxID_ANY, CFilterDialog::OnFilterSelect)
EVT_BUTTON(XRCID("ID_SAVESET"), CFilterDialog::OnSaveAs)
EVT_BUTTON(XRCID("ID_RENAMESET"), CFilterDialog::OnRename)
EVT_BUTTON(XRCID("ID_DELETESET"), CFilterDialog::OnDeleteSet)
EVT_CHOICE(XRCID("ID_SETS"), CFilterDialog::OnSetSelect)

EVT_BUTTON(XRCID("ID_LOCAL_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_LOCAL_DISABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_ENABLEALL"), CFilterDialog::OnChangeAll)
EVT_BUTTON(XRCID("ID_REMOTE_DISABLEALL"), CFilterDialog::OnChangeAll)
END_EVENT_TABLE()

CFilterCondition::CFilterCondition()
{
	type = filter_name;
	condition = 0;
	matchCase = true;
	value = 0;
}

CFilter::CFilter()
{
	matchType = all;
	filterDirs = true;
	filterFiles = true;

	// Filenames on Windows ignore case
#ifdef __WXMSW__
	matchCase = false;
#else
	matchCase = true;
#endif
}

bool CFilter::HasConditionOfType(enum t_filterType type) const
{
	for (std::vector<CFilterCondition>::const_iterator iter = filters.begin(); iter != filters.end(); ++iter)
	{
		if (iter->type == type)
			return true;
	}

	return false;
}

bool CFilter::IsLocalFilter() const
{
	 return HasConditionOfType(filter_attributes) || HasConditionOfType(filter_permissions);
}

CFilterDialog::CFilterDialog()
	: m_shiftClick()
	, m_pMainFrame()
	, m_filters(m_globalFilters)
	, m_filterSets(m_globalFilterSets)
	, m_currentFilterSet(m_globalCurrentFilterSet)
{
}

bool CFilterDialog::Create(CMainFrame* parent)
{
	m_pMainFrame = parent;

	if (!Load(parent, _T("ID_FILTER")))
		return false;

	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_LEFT_DOWN, wxMouseEventHandler(CFilterDialog::OnMouseEvent), 0, this);
	XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox)->Connect(wxID_ANY, wxEVT_KEY_DOWN, wxKeyEventHandler(CFilterDialog::OnKeyEvent), 0, this);

	DisplayFilters();

	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	wxString name = _("Custom filter set");
	pChoice->Append(_T("<") + name + _T(">"));
	for (unsigned int i = 1; i < m_filterSets.size(); i++)
		pChoice->Append(m_filterSets[i].name);
	pChoice->SetSelection(m_currentFilterSet);
	SetCtrlState();

	GetSizer()->Fit(this);

	return true;
}

void CFilterDialog::OnOkOrApply(wxCommandEvent& event)
{
	m_globalFilters = m_filters;
	CompileRegexes();
	m_globalFilterSets = m_filterSets;
	m_globalCurrentFilterSet = m_currentFilterSet;

	SaveFilters();

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);

	if (event.GetId() == wxID_OK) {
		EndModal(wxID_OK);
	}
}

void CFilterDialog::OnCancel(wxCommandEvent& event)
{
	EndModal(wxID_CANCEL);
}

void CFilterDialog::OnEdit(wxCommandEvent& event)
{
	CFilterEditDialog dlg;
	if (!dlg.Create(this, m_filters, m_filterSets))
		return;

	if (dlg.ShowModal() != wxID_OK)
		return;

	m_filters = dlg.GetFilters();
	m_filterSets = dlg.GetFilterSets();
	CompileRegexes();

	DisplayFilters();
}

void CFilterDialog::SaveFilter(TiXmlElement* pElement, const CFilter& filter)
{
	AddTextElement(pElement, "Name", filter.name);
	AddTextElement(pElement, "ApplyToFiles", filter.filterFiles ? _T("1") : _T("0"));
	AddTextElement(pElement, "ApplyToDirs", filter.filterDirs ? _T("1") : _T("0"));
	AddTextElement(pElement, "MatchType", (filter.matchType == CFilter::any) ? _T("Any") : ((filter.matchType == CFilter::none) ? _T("None") : _T("All")));
	AddTextElement(pElement, "MatchCase", filter.matchCase ? _T("1") : _T("0"));

	TiXmlElement* pConditions = pElement->LinkEndChild(new TiXmlElement("Conditions"))->ToElement();
	for (std::vector<CFilterCondition>::const_iterator conditionIter = filter.filters.begin(); conditionIter != filter.filters.end(); ++conditionIter)
	{
		const CFilterCondition& condition = *conditionIter;

		int type;
		switch (condition.type)
		{
		case filter_name:
			type = 0;
			break;
		case filter_size:
			type = 1;
			break;
		case filter_attributes:
			type = 2;
			break;
		case filter_permissions:
			type = 3;
			break;
		case filter_path:
			type = 4;
			break;
		case filter_date:
			type = 5;
			break;
		default:
			wxFAIL_MSG(_T("Unhandled filter type"));
			continue;
		}

		TiXmlElement* pCondition = pConditions->LinkEndChild(new TiXmlElement("Condition"))->ToElement();
		AddTextElement(pCondition, "Type", type);

		if (condition.type == filter_size) {
			// Backwards compatibility sucks
			int v = condition.condition;
			if (v == 2)
				v = 3;
			else if (v > 2)
				--v;
			AddTextElement(pCondition, "Condition", v);
		}
		else
			AddTextElement(pCondition, "Condition", condition.condition);
		AddTextElement(pCondition, "Value", condition.strValue);
	}
}

void CFilterDialog::SaveFilters()
{
	CInterProcessMutex mutex(MUTEX_FILTERS);

	CXmlFile xml(wxGetApp().GetSettingsFile(_T("filters")));
	TiXmlElement* pDocument = xml.Load();
	if (!pDocument) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters could not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	TiXmlElement *pFilters = pDocument->FirstChildElement("Filters");
	while (pFilters) {
		pDocument->RemoveChild(pFilters);
		pFilters = pDocument->FirstChildElement("Filters");
	}

	pFilters = pDocument->LinkEndChild(new TiXmlElement("Filters"))->ToElement();

	for (auto const& filter : m_globalFilters) {
		TiXmlElement* pElement = new TiXmlElement("Filter");
		SaveFilter(pElement, filter);
		pFilters->LinkEndChild(pElement);
	}

	TiXmlElement *pSets = pDocument->FirstChildElement("Sets");
	while (pSets) {
		pDocument->RemoveChild(pSets);
		pSets = pDocument->FirstChildElement("Sets");
	}

	pSets = pDocument->LinkEndChild(new TiXmlElement("Sets"))->ToElement();
	SetTextAttribute(pSets, "Current", wxString::Format(_T("%d"), m_currentFilterSet));

	for (auto const& set : m_globalFilterSets) {
		TiXmlElement* pSet = pSets->LinkEndChild(new TiXmlElement("Set"))->ToElement();

		if (!set.name.empty()) {
			AddTextElement(pSet, "Name", set.name);
		}

		for (unsigned int i = 0; i < set.local.size(); ++i) {
			TiXmlElement* pItem = pSet->LinkEndChild(new TiXmlElement("Item"))->ToElement();
			AddTextElement(pItem, "Local", set.local[i] ? _T("1") : _T("0"));
			AddTextElement(pItem, "Remote", set.remote[i] ? _T("1") : _T("0"));
		}
	}

	xml.Save(true);

	m_filters_disabled = false;
}

void CFilterDialog::DisplayFilters()
{
	wxCheckListBox* pLocalFilters = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemoteFilters = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	pLocalFilters->Clear();
	pRemoteFilters->Clear();

	for (unsigned int i = 0; i < m_filters.size(); ++i) {
		const CFilter& filter = m_filters[i];

		const bool localOnly = filter.IsLocalFilter();

		pLocalFilters->Append(filter.name);
		pRemoteFilters->Append(filter.name);

		pLocalFilters->Check(i, m_filterSets[m_currentFilterSet].local[i]);
		pRemoteFilters->Check(i, localOnly ? false : m_filterSets[m_currentFilterSet].remote[i]);
	}
}

void CFilterDialog::OnMouseEvent(wxMouseEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnKeyEvent(wxKeyEvent& event)
{
	m_shiftClick = event.ShiftDown();
	event.Skip();
}

void CFilterDialog::OnFilterSelect(wxCommandEvent& event)
{
	wxCheckListBox* pLocal = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
	wxCheckListBox* pRemote = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);

	int item = event.GetSelection();

	const CFilter& filter = m_filters[item];
	const bool localOnly = filter.IsLocalFilter();
	if (localOnly && event.GetEventObject() != pLocal) {
		pRemote->Check(item, false);
		wxMessageBoxEx(_("Selected filter only works for local files."), _("Cannot select filter"), wxICON_INFORMATION);
		return;
	}


	if (m_shiftClick) {
		if (event.GetEventObject() == pLocal) {
			if (!localOnly)
				pRemote->Check(item, pLocal->IsChecked(event.GetSelection()));
		}
		else
			pLocal->Check(item, pRemote->IsChecked(event.GetSelection()));
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	bool localChecked = pLocal->IsChecked(event.GetSelection());
	bool remoteChecked = pRemote->IsChecked(event.GetSelection());
	m_filterSets[0].local[item] = localChecked;
	m_filterSets[0].remote[item] = remoteChecked;
}

void CFilterDialog::OnSaveAs(wxCommandEvent& event)
{
	CInputDialog dlg;
	dlg.Create(this, _("Enter name for filterset"), _("Please enter a unique name for this filter set"));
	if (dlg.ShowModal() != wxID_OK)
		return;

	wxString name = dlg.GetValue();
	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	CFilterSet set;
	int old_pos = pChoice->GetSelection();
	if (old_pos > 0)
		set = m_filterSets[old_pos];
	else
		set = m_filterSets[0];

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES)
			return;
	}

	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else
		m_filterSets[pos] = set;

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	SetCtrlState();

	GetSizer()->Fit(this);
}

void CFilterDialog::OnRename(wxCommandEvent& event)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int old_pos = pChoice->GetSelection();
	if (old_pos == -1)
		return;

	if (!old_pos) {
		wxMessageBoxEx(_("This filter set cannot be renamed."));
		return;
	}

	CInputDialog dlg;

	wxString msg = wxString::Format(_("Please enter a new name for the filter set \"%s\""), pChoice->GetStringSelection());

	dlg.Create(this, _("Enter new name for filterset"), msg);
	if (dlg.ShowModal() != wxID_OK)
		return;

	wxString name = dlg.GetValue();

	if (name == pChoice->GetStringSelection()) {
		// Nothing changed
		return;
	}

	if (name.empty()) {
		wxMessageBoxEx(_("No name for the filterset given."), _("Cannot save filterset"), wxICON_INFORMATION);
		return;
	}

	int pos = pChoice->FindString(name);
	if (pos != wxNOT_FOUND) {
		if (wxMessageBoxEx(_("Given filterset name already exists, overwrite filter set?"), _("Filter set already exists"), wxICON_QUESTION | wxYES_NO) != wxYES)
			return;
	}

	// Remove old entry
	pChoice->Delete(old_pos);
	CFilterSet set = m_filterSets[old_pos];
	m_filterSets.erase(m_filterSets.begin() + old_pos);

	pos = pChoice->FindString(name);
	if (pos == wxNOT_FOUND) {
		pos = m_filterSets.size();
		m_filterSets.push_back(set);
		pChoice->Append(name);
	}
	else
		m_filterSets[pos] = set;

	m_filterSets[pos].name = name;

	pChoice->SetSelection(pos);
	m_currentFilterSet = pos;

	GetSizer()->Fit(this);
}

void CFilterDialog::OnDeleteSet(wxCommandEvent& event)
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
	int pos = pChoice->GetSelection();
	if (pos == -1)
		return;

	if (!pos) {
		wxMessageBoxEx(_("This filter set cannot be removed."));
		return;
	}

	m_filterSets[0] = m_filterSets[pos];

	pChoice->Delete(pos);
	m_filterSets.erase(m_filterSets.begin() + pos);
	wxASSERT(!m_filterSets.empty());

	pChoice->SetSelection(0);
	m_currentFilterSet = 0;

	SetCtrlState();
}

void CFilterDialog::OnSetSelect(wxCommandEvent& event)
{
	m_currentFilterSet = event.GetSelection();
	DisplayFilters();
	SetCtrlState();
}

void CFilterDialog::OnChangeAll(wxCommandEvent& event)
{
	bool check = true;
	if (event.GetId() == XRCID("ID_LOCAL_DISABLEALL") || event.GetId() == XRCID("ID_REMOTE_DISABLEALL"))
		check = false;

	bool local;
	std::vector<bool>* pValues;
	wxCheckListBox* pListBox;
	if (event.GetId() == XRCID("ID_LOCAL_ENABLEALL") || event.GetId() == XRCID("ID_LOCAL_DISABLEALL")) {
		pListBox = XRCCTRL(*this, "ID_LOCALFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].local;
		local = true;
	}
	else {
		pListBox = XRCCTRL(*this, "ID_REMOTEFILTERS", wxCheckListBox);
		pValues = &m_filterSets[0].remote;
		local = false;
	}

	if (m_currentFilterSet) {
		m_filterSets[0] = m_filterSets[m_currentFilterSet];
		m_currentFilterSet = 0;
		wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);
		pChoice->SetSelection(0);
	}

	for (size_t i = 0; i < pListBox->GetCount(); ++i) {
		if (!local && (m_filters[i].IsLocalFilter())) {
			pListBox->Check(i, false);
			(*pValues)[i] = false;
		}
		else {
			pListBox->Check(i, check);
			(*pValues)[i] = check;
		}
	}
}

void CFilterDialog::SetCtrlState()
{
	wxChoice* pChoice = XRCCTRL(*this, "ID_SETS", wxChoice);

	int sel = pChoice->GetSelection();
	XRCCTRL(*this, "ID_RENAMESET", wxButton)->Enable(sel > 0);
	XRCCTRL(*this, "ID_DELETESET", wxButton)->Enable(sel > 0);
}

CFilterManager::CFilterManager()
{
	LoadFilters();

	if (m_globalFilterSets.empty()) {
		CFilterSet set;
		set.local.resize(m_globalFilters.size(), false);
		set.remote.resize(m_globalFilters.size(), false);

		m_globalFilterSets.push_back(set);
	}
}

bool CFilterManager::HasActiveFilters(bool ignore_disabled /*=false*/)
{
	if (!m_loaded)
		LoadFilters();

	if (m_globalFilterSets.empty())
		return false;

	wxASSERT(m_globalCurrentFilterSet < m_globalFilterSets.size());

	if (m_filters_disabled && !ignore_disabled)
		return false;

	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (set.local[i])
			return true;

		if (set.remote[i])
			return true;
	}

	return false;
}

bool CFilterManager::HasSameLocalAndRemoteFilters() const
{
	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];
	for (unsigned int i = 0; i < m_globalFilters.size(); i++)
	{
		if (set.local[i])
		{
			if (!set.remote[i])
				return false;
		}
		else if (set.remote[i])
			return false;
	}

	return true;
}

bool CFilterManager::FilenameFiltered(const wxString& name, const wxString& path, bool dir, wxLongLong size, bool local, int attributes, CDateTime const& date) const
{
	if (m_filters_disabled)
		return false;

	wxASSERT(m_globalCurrentFilterSet < m_globalFilterSets.size());

	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];

	// Check active filters
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (local) {
			if (set.local[i])
				if (FilenameFilteredByFilter(m_globalFilters[i], name, path, dir, size, attributes, date))
					return true;
		}
		else {
			if (set.remote[i])
				if (FilenameFilteredByFilter(m_globalFilters[i], name, path, dir, size, attributes, date))
					return true;
		}
	}

	return false;
}

bool CFilterManager::FilenameFiltered(const std::list<CFilter> &filters, const wxString& name, const wxString& path, bool dir, wxLongLong size, bool local, int attributes, CDateTime const& date) const
{
	for( auto const& filter : filters ) {
		if (FilenameFilteredByFilter(filter, name, path, dir, size, attributes, date))
			return true;
	}

	return false;
}

static bool StringMatch(const wxString& subject, const wxString& filter, int condition, bool matchCase, std::shared_ptr<const wxRegEx> const& pRegEx)
{
	bool match = false;

	switch (condition)
	{
	case 0:
		if (matchCase) {
			if (subject.Contains(filter))
				match = true;
		}
		else {
			if (subject.Lower().Contains(filter.Lower()))
				match = true;
		}
		break;
	case 1:
		if (matchCase) {
			if (subject == filter)
				match = true;
		}
		else {
			if (!subject.CmpNoCase(filter))
				match = true;
		}
		break;
	case 2:
		{
			const wxString& left = subject.Left(filter.Len());
			if (matchCase) {
				if (left == filter)
					match = true;
			}
			else {
				if (!left.CmpNoCase(filter))
					match = true;
			}
		}
		break;
	case 3:
		{
			const wxString& right = subject.Right(filter.Len());
			if (matchCase) {
				if (right == filter)
					match = true;
			}
			else {
				if (!right.CmpNoCase(filter))
					match = true;
			}
		}
		break;
	case 4:
		wxASSERT(pRegEx);
		if (pRegEx && pRegEx->Matches(subject))
			match = true;
		break;
	case 5:
		if (matchCase) {
			if (!subject.Contains(filter))
				match = true;
		}
		else {
			if (!subject.Lower().Contains(filter.Lower()))
				match = true;
		}
		break;
	}

	return match;
}

bool CFilterManager::FilenameFilteredByFilter(const CFilter& filter, const wxString& name, const wxString& path, bool dir, wxLongLong size, int attributes, CDateTime const& date)
{
	if (dir && !filter.filterDirs)
		return false;
	else if (!dir && !filter.filterFiles)
		return false;

	for (std::vector<CFilterCondition>::const_iterator iter = filter.filters.begin(); iter != filter.filters.end(); ++iter)
	{
		bool match = false;
		const CFilterCondition& condition = *iter;

		switch (condition.type)
		{
		case filter_name:
			match = StringMatch(name, condition.strValue, condition.condition, filter.matchCase, condition.pRegEx);
			break;
		case filter_path:
			match = StringMatch(path, condition.strValue, condition.condition, filter.matchCase, condition.pRegEx);
			break;
		case filter_size:
			if (size == -1)
				continue;
			switch (condition.condition)
			{
			case 0:
				if (size > condition.value)
					match = true;
				break;
			case 1:
				if (size == condition.value)
					match = true;
				break;
			case 2:
				if (size != condition.value)
					match = true;
				break;
			case 3:
				if (size < condition.value)
					match = true;
				break;
			}
			break;
		case filter_attributes:
#ifndef __WXMSW__
			continue;
#else
			if (!attributes)
				continue;

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = FILE_ATTRIBUTE_ARCHIVE;
					break;
				case 1:
					flag = FILE_ATTRIBUTE_COMPRESSED;
					break;
				case 2:
					flag = FILE_ATTRIBUTE_ENCRYPTED;
					break;
				case 3:
					flag = FILE_ATTRIBUTE_HIDDEN;
					break;
				case 4:
					flag = FILE_ATTRIBUTE_READONLY;
					break;
				case 5:
					flag = FILE_ATTRIBUTE_SYSTEM;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value)
					match = true;
			}
#endif //__WXMSW__
			break;
		case filter_permissions:
#ifdef __WXMSW__
			continue;
#else
			if (attributes == -1)
				continue;

			{
				int flag = 0;
				switch (condition.condition)
				{
				case 0:
					flag = S_IRUSR;
					break;
				case 1:
					flag = S_IWUSR;
					break;
				case 2:
					flag = S_IXUSR;
					break;
				case 3:
					flag = S_IRGRP;
					break;
				case 4:
					flag = S_IWGRP;
					break;
				case 5:
					flag = S_IXGRP;
					break;
				case 6:
					flag = S_IROTH;
					break;
				case 7:
					flag = S_IWOTH;
					break;
				case 8:
					flag = S_IXOTH;
					break;
				}

				int set = (flag & attributes) ? 1 : 0;
				if (set == condition.value)
					match = true;
			}
#endif //__WXMSW__
			break;
		case filter_date:
			if (date.IsValid()) {
				int cmp = date.Compare( condition.date );
				switch (condition.condition)
				{
				case 0: // Before
					match = cmp < 0;
					break;
				case 1: // Equals
					match = cmp == 0;
					break;
				case 2: // Not equals
					match = cmp != 0;
					break;
				case 3: // After
					match = cmp > 0;
					break;
				}
			}
			break;
		default:
			wxFAIL_MSG(_T("Unhandled filter type"));
			break;
		}
		if (match) {
			if (filter.matchType == CFilter::any)
				return true;
			else if (filter.matchType == CFilter::none)
				return false;
		}
		else {
			if (filter.matchType == CFilter::all)
				return false;
		}
	}

	if (filter.matchType != CFilter::any || filter.filters.empty())
		return true;

	return false;
}

bool CFilterManager::CompileRegexes(CFilter& filter)
{
	for (auto iter = filter.filters.begin(); iter != filter.filters.end(); ++iter)
	{
		CFilterCondition& condition = *iter;
		if ((condition.type == filter_name || condition.type == filter_path) && condition.condition == 4) {
			condition.pRegEx = std::make_shared<wxRegEx>(condition.strValue);
			if (!condition.pRegEx->IsValid()) {
				condition.pRegEx.reset();
				return false;
			}
		}
		else
			condition.pRegEx.reset();
	}

	return true;
}

bool CFilterManager::CompileRegexes()
{
	for (auto & filter : m_globalFilters) {
		CompileRegexes(filter);
	}
	return true;
}

bool CFilterManager::LoadFilter(TiXmlElement* pElement, CFilter& filter)
{
	filter.name = GetTextElement(pElement, "Name");
	filter.filterFiles = GetTextElement(pElement, "ApplyToFiles") == _T("1");
	filter.filterDirs = GetTextElement(pElement, "ApplyToDirs") == _T("1");

	wxString type = GetTextElement(pElement, "MatchType");
	if (type == _T("Any"))
		filter.matchType = CFilter::any;
	else if (type == _T("None"))
		filter.matchType = CFilter::none;
	else
		filter.matchType = CFilter::all;
	filter.matchCase = GetTextElement(pElement, "MatchCase") == _T("1");

	TiXmlElement *pConditions = pElement->FirstChildElement("Conditions");
	if (!pConditions)
		return false;

	for (TiXmlElement *pCondition = pConditions->FirstChildElement("Condition"); pCondition; pCondition = pCondition->NextSiblingElement("Condition")) {
		CFilterCondition condition;
		int type = GetTextElementInt(pCondition, "Type", 0);
		switch (type) {
		case 0:
			condition.type = filter_name;
			break;
		case 1:
			condition.type = filter_size;
			break;
		case 2:
			condition.type = filter_attributes;
			break;
		case 3:
			condition.type = filter_permissions;
			break;
		case 4:
			condition.type = filter_path;
			break;
		case 5:
			condition.type = filter_date;
			break;
		default:
			continue;
		}
		condition.condition = GetTextElementInt(pCondition, "Condition", 0);
		if (condition.type == filter_size) {
			if (condition.value == 3)
				condition.value = 2;
			else if (condition.value >= 2)
				++condition.value;
		}
		condition.strValue = GetTextElement(pCondition, "Value");
		condition.matchCase = filter.matchCase;
		if (condition.strValue.empty())
			continue;

		if (condition.type == filter_size) {
			unsigned long long tmp;
			condition.strValue.ToULongLong(&tmp);
			condition.value = tmp;
		}
		else if (condition.type == filter_attributes || condition.type == filter_permissions) {
			if (condition.strValue == _T("0"))
				condition.value = 0;
			else
				condition.value = 1;
		}
		else if (condition.type == filter_date) {
			wxDateTime t;
			if (!t.ParseFormat(condition.strValue, _T("%Y-%m-%d")) || !t.IsValid())
				continue;
			condition.date = CDateTime(t, CDateTime::days);
		}

		filter.filters.push_back(condition);
	}

	return true;
}

void CFilterManager::LoadFilters()
{
	if (m_loaded)
		return;

	m_loaded = true;

	CInterProcessMutex mutex(MUTEX_FILTERS);

	wxString file(wxGetApp().GetSettingsFile(_T("filters")));
	if (CLocalFileSystem::GetSize(file) < 1) {
		file = wxGetApp().GetResourceDir().GetPath() + _T("defaultfilters.xml");
	}

	CXmlFile xml(file);
	TiXmlElement* pDocument = xml.Load();
	if (!pDocument) {
		wxString msg = xml.GetError() + _T("\n\n") + _("Any changes made to the filters will not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return;
	}

	TiXmlElement *pFilters = pDocument->FirstChildElement("Filters");

	if (!pFilters)
		return;

	TiXmlElement *pFilter = pFilters->FirstChildElement("Filter");
	while (pFilter) {
		CFilter filter;

		bool loaded = LoadFilter(pFilter, filter);

		if (loaded && !filter.name.empty() && !filter.filters.empty())
			m_globalFilters.push_back(filter);

		pFilter = pFilter->NextSiblingElement("Filter");
	}

	CompileRegexes();

	TiXmlElement* pSets = pDocument->FirstChildElement("Sets");
	if (!pSets)
		return;

	for (TiXmlElement* pSet = pSets->FirstChildElement("Set"); pSet; pSet = pSet->NextSiblingElement("Set")) {
		CFilterSet set;
		TiXmlElement* pItem = pSet->FirstChildElement("Item");
		while (pItem) {
			wxString local = GetTextElement(pItem, "Local");
			wxString remote = GetTextElement(pItem, "Remote");
			set.local.push_back(local == _T("1") ? true : false);
			set.remote.push_back(remote == _T("1") ? true : false);

			pItem = pItem->NextSiblingElement("Item");
		}

		if (!m_globalFilterSets.empty()) {
			set.name = GetTextElement(pSet, "Name");
			if (set.name.empty())
				continue;
		}

		if (set.local.size() == m_globalFilters.size())
			m_globalFilterSets.push_back(set);
	}

	wxString attribute = GetTextAttribute(pSets, "Current");
	unsigned long value;
	if (attribute.ToULong(&value)) {
		if (value < m_globalFilterSets.size())
			m_globalCurrentFilterSet = value;
	}
}

void CFilterManager::ToggleFilters()
{
	if (m_filters_disabled) {
		m_filters_disabled = false;
		return;
	}

	if (HasActiveFilters(true))
		m_filters_disabled = true;
}

std::list<CFilter> CFilterManager::GetActiveFilters(bool local)
{
	std::list<CFilter> filters;

	if (m_filters_disabled)
		return filters;

	const CFilterSet& set = m_globalFilterSets[m_globalCurrentFilterSet];

	// Check active filters
	for (unsigned int i = 0; i < m_globalFilters.size(); ++i) {
		if (local) {
			if (set.local[i])
				filters.push_back(m_globalFilters[i]);
		}
		else {
			if (set.remote[i])
				filters.push_back(m_globalFilters[i]);
		}
	}

	return filters;
}
