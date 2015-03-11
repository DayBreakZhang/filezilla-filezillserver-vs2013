#ifndef __BOOKMARKS_DIALOG_H__
#define __BOOKMARKS_DIALOG_H__

#include "dialogex.h"

class CNewBookmarkDialog : public wxDialogEx
{
public:
	CNewBookmarkDialog(wxWindow* parent, wxString& site_path, const CServer* server);
	virtual ~CNewBookmarkDialog() {}

	int Run(const wxString &local_path, const CServerPath &remote_path);

protected:
	wxWindow* m_parent;
	wxString &m_site_path;
	const CServer* m_server;

	DECLARE_EVENT_TABLE()
	void OnOK(wxCommandEvent&);
	void OnBrowse(wxCommandEvent&);
};

class CBookmarksDialog : public wxDialogEx
{
public:
	CBookmarksDialog(wxWindow* parent, wxString& site_path, const CServer* server);
	virtual ~CBookmarksDialog() {}

	int Run(const wxString &local_path, const CServerPath &remote_path);

	static bool GetBookmarks(std::list<wxString> &bookmarks);
	static bool GetBookmark(const wxString& name, wxString &local_dir, CServerPath &remote_dir, bool &sync);
	static bool AddBookmark(const wxString& name, const wxString &local_dir, const CServerPath &remote_dir, bool sync);

protected:
	bool Verify();
	void UpdateBookmark();
	void DisplayBookmark();

	void LoadGlobalBookmarks();
	void LoadSiteSpecificBookmarks();

	void SaveSiteSpecificBookmarks();
	void SaveGlobalBookmarks();

	wxWindow* m_parent;
	wxString &m_site_path;
	const CServer* m_server;

	wxTreeCtrl *m_pTree;
	wxTreeItemId m_bookmarks_global;
	wxTreeItemId m_bookmarks_site;

	DECLARE_EVENT_TABLE()
	void OnSelChanging(wxTreeEvent& event);
	void OnSelChanged(wxTreeEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnBrowse(wxCommandEvent& event);
	void OnNewBookmark(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
};

#endif //__BOOKMARKS_DIALOG_H__
