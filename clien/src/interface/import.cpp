#include <filezilla.h>
#include "filezillaapp.h"
#include "import.h"
#include "xmlfunctions.h"
#include "ipcmutex.h"
#include "Options.h"
#include "queue.h"

CImportDialog::CImportDialog(wxWindow* parent, CQueueView* pQueueView)
	: m_parent(parent), m_pQueueView(pQueueView)
{
}

void CImportDialog::Run()
{
	wxFileDialog dlg(m_parent, _("Select file to import settings from"), wxString(),
					_T("FileZilla.xml"), _T("XML files (*.xml)|*.xml"),
					wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	dlg.CenterOnParent();

	if (dlg.ShowModal() != wxID_OK)
		return;

	wxFileName fn(dlg.GetPath());
	wxString const path = fn.GetPath();
	wxString const settings(COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR));
	if (path == settings) {
		wxMessageBoxEx(_("You cannot import settings from FileZilla's own settings directory."), _("Error importing"), wxICON_ERROR, m_parent);
		return;
	}

	CXmlFile fz3(dlg.GetPath());
	TiXmlElement* fz3Root = fz3.Load();
	if (fz3Root) {
		bool settings = fz3Root->FirstChildElement("Settings") != 0;
		bool queue = fz3Root->FirstChildElement("Queue") != 0;
		bool sites = fz3Root->FirstChildElement("Servers") != 0;

		if (settings || queue || sites) {
			if (!Load(m_parent, _T("ID_IMPORT"))) {
				wxBell();
				return;
			}
			if (!queue)
				XRCCTRL(*this, "ID_QUEUE", wxCheckBox)->Hide();
			if (!sites)
				XRCCTRL(*this, "ID_SITEMANAGER", wxCheckBox)->Hide();
			if (!settings)
				XRCCTRL(*this, "ID_SETTINGS", wxCheckBox)->Hide();
			GetSizer()->Fit(this);

			if (ShowModal() != wxID_OK) {
				return;
			}

			if (queue && XRCCTRL(*this, "ID_QUEUE", wxCheckBox)->IsChecked()) {
				m_pQueueView->ImportQueue(fz3Root->FirstChildElement("Queue"), true);
			}

			if (sites && XRCCTRL(*this, "ID_SITEMANAGER", wxCheckBox)->IsChecked()) {
				ImportSites(fz3Root->FirstChildElement("Servers"));
			}

			if (settings && XRCCTRL(*this, "ID_SETTINGS", wxCheckBox)->IsChecked()) {
				COptions::Get()->Import(fz3Root->FirstChildElement("Settings"));
				wxMessageBoxEx(_("The settings have been imported. You have to restart FileZilla for all settings to have effect."), _("Import successful"), wxOK, this);
			}

			wxMessageBoxEx(_("The selected categories have been imported."), _("Import successful"), wxOK, this);
			return;
		}
	}

	CXmlFile fz2(dlg.GetPath(), _T("FileZilla"));
	TiXmlElement* fz2Root = fz2.Load();
	if (fz2Root) {
		TiXmlElement* sites_fz2 = fz2Root->FirstChildElement("Sites");
		if (sites_fz2) {
			int res = wxMessageBoxEx(_("The file you have selected contains site manager data from a previous version of FileZilla.\nDue to differences in the storage format, only host, port, username and password will be imported.\nContinue with the import?"),
				_("Import data from older version"), wxICON_QUESTION | wxYES_NO);

			if (res == wxYES)
				ImportLegacySites(sites_fz2);
			return;
		}
	}

	wxMessageBoxEx(_("File does not contain any importable data."), _("Error importing"), wxICON_ERROR, m_parent);
}

bool CImportDialog::ImportLegacySites(TiXmlElement* pSites)
{
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	TiXmlElement* pDocument = file.Load();
	if (!pDocument) {
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager will not be saved."), file.GetFileName());
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	TiXmlElement* pCurrentSites = pDocument->FirstChildElement("Servers");
	if (!pCurrentSites)
		pCurrentSites = pDocument->LinkEndChild(new TiXmlElement("Servers"))->ToElement();

	if (!ImportLegacySites(pSites, pCurrentSites))
		return false;

	return file.Save(true);
}

wxString CImportDialog::DecodeLegacyPassword(wxString pass)
{
	if( pass.size() % 3 ) {
		return wxString();
	}

	wxString output;
	const char* key = "FILEZILLA1234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	int pos = (pass.Length() / 3) % strlen(key);
	for (unsigned int i = 0; i < pass.Length(); i += 3)
	{
		if (pass[i] < '0' || pass[i] > '9' ||
			pass[i + 1] < '0' || pass[i + 1] > '9' ||
			pass[i + 2] < '0' || pass[i + 2] > '9')
			return wxString();
		int number = (pass[i] - '0') * 100 +
						(pass[i + 1] - '0') * 10 +
						(pass[i + 2] - '0');
		wxChar c = number ^ key[(i / 3 + pos) % strlen(key)];
		output += c;
	}

	return output;
}

bool CImportDialog::ImportLegacySites(TiXmlElement* pSitesToImport, TiXmlElement* pExistingSites)
{
	for (TiXmlElement* pImportFolder = pSitesToImport->FirstChildElement("Folder"); pImportFolder; pImportFolder = pImportFolder->NextSiblingElement("Folder")) {
		wxString name = GetTextAttribute(pImportFolder, "Name");
		if (name.empty())
			continue;

		wxString newName = name;
		int i = 2;
		TiXmlElement* pFolder;
		while (!(pFolder = GetFolderWithName(pExistingSites, newName))) {
			newName = wxString::Format(_T("%s %d"), name, i++);
		}

		ImportLegacySites(pImportFolder, pFolder);
	}

	for (TiXmlElement* pImportSite = pSitesToImport->FirstChildElement("Site"); pImportSite; pImportSite = pImportSite->NextSiblingElement("Site")) {
		wxString name = GetTextAttribute(pImportSite, "Name");
		if (name.empty())
			continue;

		wxString host = GetTextAttribute(pImportSite, "Host");
		if (host.empty())
			continue;

		int port = GetAttributeInt(pImportSite, "Port");
		if (port < 1 || port > 65535)
			continue;

		int serverType = GetAttributeInt(pImportSite, "ServerType");
		if (serverType < 0 || serverType > 4)
			continue;

		int protocol;
		switch (serverType)
		{
		default:
		case 0:
			protocol = 0;
			break;
		case 1:
			protocol = 3;
			break;
		case 2:
		case 4:
			protocol = 4;
			break;
		case 3:
			protocol = 1;
			break;
		}

		bool dontSavePass = GetAttributeInt(pImportSite, "DontSavePass") == 1;

		int logontype = GetAttributeInt(pImportSite, "Logontype");
		if (logontype < 0 || logontype > 2)
			continue;
		if (logontype == 2)
			logontype = 4;
		if (logontype == 1 && dontSavePass)
			logontype = 2;

		wxString user = GetTextAttribute(pImportSite, "User");
		wxString pass = DecodeLegacyPassword(GetTextAttribute(pImportSite, "Pass"));
		wxString account = GetTextAttribute(pImportSite, "Account");
		if (logontype && user.empty())
			continue;

		// Find free name
		wxString newName = name;
		int i = 2;
		while (HasEntryWithName(pExistingSites, newName)) {
			newName = wxString::Format(_T("%s %d"), name, i++);
		}

		TiXmlElement* pServer = pExistingSites->LinkEndChild(new TiXmlElement("Server"))->ToElement();
		AddTextElement(pServer, newName);

		AddTextElement(pServer, "Host", host);
		AddTextElement(pServer, "Port", port);
		AddTextElement(pServer, "Protocol", protocol);
		AddTextElement(pServer, "Logontype", logontype);
		AddTextElement(pServer, "User", user);
		AddTextElement(pServer, "Pass", pass);
		AddTextElement(pServer, "Account", account);
	}

	return true;
}

bool CImportDialog::HasEntryWithName(TiXmlElement* pElement, const wxString& name)
{
	TiXmlElement* pChild;
	for (pChild = pElement->FirstChildElement("Server"); pChild; pChild = pChild->NextSiblingElement("Server")) {
		wxString childName = GetTextElement(pChild);
		childName.Trim(true);
		childName.Trim(false);
		if (!name.CmpNoCase(childName))
			return true;
	}
	for (pChild = pElement->FirstChildElement("Folder"); pChild; pChild = pChild->NextSiblingElement("Folder")) {
		wxString childName = GetTextElement(pChild);
		childName.Trim(true);
		childName.Trim(false);
		if (!name.CmpNoCase(childName))
			return true;
	}

	return false;
}

TiXmlElement* CImportDialog::GetFolderWithName(TiXmlElement* pElement, const wxString& name)
{
	TiXmlElement* pChild;
	for (pChild = pElement->FirstChildElement("Server"); pChild; pChild = pChild->NextSiblingElement("Server")) {
		wxString childName = GetTextElement(pChild);
		childName.Trim(true);
		childName.Trim(false);
		if (!name.CmpNoCase(childName))
			return 0;
	}

	for (pChild = pElement->FirstChildElement("Folder"); pChild; pChild = pChild->NextSiblingElement("Folder")) {
		wxString childName = GetTextElement(pChild);
		childName.Trim(true);
		childName.Trim(false);
		if (!name.CmpNoCase(childName))
			return pChild;
	}

	pChild = pElement->LinkEndChild(new TiXmlElement("Folder"))->ToElement();
	AddTextElement(pChild, name);

	return pChild;
}

bool CImportDialog::ImportSites(TiXmlElement* pSites)
{
	CInterProcessMutex mutex(MUTEX_SITEMANAGER);

	CXmlFile file(wxGetApp().GetSettingsFile(_T("sitemanager")));
	TiXmlElement* pDocument = file.Load();
	if (!pDocument) {
		wxString msg = wxString::Format(_("Could not load \"%s\", please make sure the file is valid and can be accessed.\nAny changes made in the Site Manager will not be saved."), file.GetFileName());
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);

		return false;
	}

	TiXmlElement* pCurrentSites = pDocument->FirstChildElement("Servers");
	if (!pCurrentSites)
		pCurrentSites = pDocument->LinkEndChild(new TiXmlElement("Servers"))->ToElement();

	if (!ImportSites(pSites, pCurrentSites))
		return false;

	return file.Save(true);
}

bool CImportDialog::ImportSites(TiXmlElement* pSitesToImport, TiXmlElement* pExistingSites)
{
	for (TiXmlElement* pImportFolder = pSitesToImport->FirstChildElement("Folder"); pImportFolder; pImportFolder = pImportFolder->NextSiblingElement("Folder")) {
		wxString name = GetTextElement_Trimmed(pImportFolder, "Name");
		if (name.empty())
			name = GetTextElement_Trimmed(pImportFolder);
		if (name.empty())
			continue;

		wxString newName = name;
		int i = 2;
		TiXmlElement* pFolder;
		while (!(pFolder = GetFolderWithName(pExistingSites, newName)))
		{
			newName = wxString::Format(_T("%s %d"), name, i++);
		}

		ImportSites(pImportFolder, pFolder);
	}

	for (TiXmlElement* pImportSite = pSitesToImport->FirstChildElement("Server"); pImportSite; pImportSite = pImportSite->NextSiblingElement("Server")) {
		wxString name = GetTextElement_Trimmed(pImportSite, "Name");
		if (name.empty())
			name = GetTextElement_Trimmed(pImportSite);
		if (name.empty())
			continue;

		// Find free name
		wxString newName = name;
		int i = 2;
		while (HasEntryWithName(pExistingSites, newName)) {
			newName = wxString::Format(_T("%s %d"), name, i++);
		}

		TiXmlElement* pServer = pExistingSites->InsertEndChild(*pImportSite)->ToElement();
		AddTextElement(pServer, "Name", newName, true);
		AddTextElement(pServer, newName);
	}

	return true;
}
