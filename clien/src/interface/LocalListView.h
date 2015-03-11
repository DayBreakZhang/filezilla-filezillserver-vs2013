#ifndef __LOCALLISTVIEW_H__
#define __LOCALLISTVIEW_H__

#include "filelistctrl.h"
#include "state.h"

class CQueueView;
class CLocalListViewDropTarget;
#ifdef __WXMSW__
class CVolumeDescriptionEnumeratorThread;
#endif

class CLocalFileData final : public CGenericFileData
{
public:
	wxString name;
#ifdef __WXMSW__
	CSparseOptional<wxString> label;
#endif
	CDateTime time;
	int64_t size;
	int attributes;
	bool dir;
	bool is_dir() const { return dir; }
};

class CLocalListView : public CFileListCtrl<CLocalFileData>, CStateEventHandler
{
	friend class CLocalListViewDropTarget;
	friend class CLocalListViewSortType;

public:
	CLocalListView(wxWindow* parent, CState *pState, CQueueView *pQueue);
	virtual ~CLocalListView();

protected:
	void OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString& data, const void*);
	bool DisplayDir(CLocalPath const& dirname);
	void ApplyCurrentFilter();

	// Declared const due to design error in wxWidgets.
	// Won't be fixed since a fix would break backwards compatibility
	// Both functions use a const_cast<CLocalListView *>(this) and modify
	// the instance.
	virtual int OnGetItemImage(long item) const;
	virtual wxListItemAttr* OnGetItemAttr(long item) const;

	// Clears all selections and returns the list of items that were selected
	std::list<wxString> RememberSelectedItems(wxString& focused);

	// Select a list of items based in their names.
	// Sort order may not change between call to RememberSelectedItems and
	// ReselectItems
	void ReselectItems(const std::list<wxString>& selectedNames, wxString focused, bool ensureVisible = false);

#ifdef __WXMSW__
	void DisplayDrives();
	void DisplayShares(wxString computer);
#endif

public:
	virtual bool CanStartComparison(wxString* pError);
	virtual void StartComparison();
	virtual bool GetNextFile(wxString& name, bool &dir, wxLongLong &size, CDateTime& date);
	virtual void FinishComparison();

	virtual bool ItemIsDir(int index) const;
	virtual wxLongLong ItemGetSize(int index) const;

protected:
	virtual wxString GetItemText(int item, unsigned int column);

	bool IsItemValid(unsigned int item) const;
	CLocalFileData *GetData(unsigned int item);

	virtual CSortComparisonObject GetSortComparisonObject();

	void RefreshFile(const wxString& file);

	virtual void OnNavigationEvent(bool forward);

	virtual bool OnBeginRename(const wxListEvent& event);
	virtual bool OnAcceptRename(const wxListEvent& event);

	CLocalPath m_dir;

	int m_dropTarget;

	wxString MenuMkdir();

	// Event handlers
	DECLARE_EVENT_TABLE()
	void OnItemActivated(wxListEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnMenuUpload(wxCommandEvent& event);
	void OnMenuMkdir(wxCommandEvent& event);
	void OnMenuMkdirChgDir(wxCommandEvent&);
	void OnMenuDelete(wxCommandEvent& event);
	void OnMenuRename(wxCommandEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnBeginDrag(wxListEvent& event);
	void OnMenuOpen(wxCommandEvent& event);
	void OnMenuEdit(wxCommandEvent& event);
	void OnMenuEnter(wxCommandEvent& event);
#ifdef __WXMSW__
	void OnVolumesEnumerated(wxCommandEvent& event);
	CVolumeDescriptionEnumeratorThread* m_pVolumeEnumeratorThread;
#endif
	void OnMenuRefresh(wxCommandEvent& event);
};

#endif
