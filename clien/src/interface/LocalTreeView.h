#ifndef __LOCALTREEVIEW_H__
#define __LOCALTREEVIEW_H__

#include <option_change_event_handler.h>
#include "systemimagelist.h"
#include "state.h"
#include "treectrlex.h"

class CQueueView;

#ifdef __WXMSW__
class CVolumeDescriptionEnumeratorThread;
#endif

class CLocalTreeView : public wxTreeCtrlEx, CSystemImageList, CStateEventHandler, COptionChangeEventHandler
{
	DECLARE_CLASS(CLocalTreeView)

	friend class CLocalTreeViewDropTarget;

public:
	CLocalTreeView(wxWindow* parent, wxWindowID id, CState *pState, CQueueView *pQueueView);
	virtual ~CLocalTreeView();

#ifdef __WXMSW__
	// React to changed drive letters
	void OnDevicechange(WPARAM wParam, LPARAM lParam);
#endif

protected:
	virtual void OnStateChange(CState* pState, enum t_statechange_notifications notification, const wxString& data, const void* data2);

	void SetDir(wxString localDir);
	void RefreshListing();

#ifdef __WXMSW__
	bool CreateRoot();
	bool DisplayDrives(wxTreeItemId parent);
	wxString GetSpecialFolder(int folder, int &iconIndex, int &openIconIndex);

	wxTreeItemId m_desktop;
	wxTreeItemId m_drives;
	wxTreeItemId m_documents;
#endif

	void UpdateSortMode();

	virtual void OnOptionsChanged(changed_options_t const& options);

	wxTreeItemId GetNearestParent(wxString& localDir);
	wxTreeItemId GetSubdir(wxTreeItemId parent, const wxString& subDir);
	void DisplayDir(wxTreeItemId parent, const wxString& dirname, const wxString& knownSubdir = _T(""));
	wxString HasSubdir(const wxString& dirname);
	wxTreeItemId MakeSubdirs(wxTreeItemId parent, wxString dirname, wxString subDir);
	wxString m_currentDir;

	bool CheckSubdirStatus(wxTreeItemId& item, const wxString& path);

	wxString MenuMkdir();

	DECLARE_EVENT_TABLE()
	void OnItemExpanding(wxTreeEvent& event);
#ifdef __WXMSW__
	void OnSelectionChanging(wxTreeEvent& event);
#endif
	void OnSelectionChanged(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
#ifdef __WXMSW__
	void OnVolumesEnumerated(wxCommandEvent& event);
	CVolumeDescriptionEnumeratorThread* m_pVolumeEnumeratorThread;
#endif
	void OnContextMenu(wxTreeEvent& event);
	void OnMenuUpload(wxCommandEvent& event);
	void OnMenuMkdir(wxCommandEvent& event);
	void OnMenuMkdirChgDir(wxCommandEvent& event);
	void OnMenuRename(wxCommandEvent& event);
	void OnMenuDelete(wxCommandEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnMenuOpen(wxCommandEvent&);

#ifdef __WXMSW__
	// React to changed drive letters
	wxTreeItemId AddDrive(wxChar letter);
	void RemoveDrive(wxChar letter);
#endif

	wxString GetDirFromItem(wxTreeItemId item);

	CQueueView* m_pQueueView;

	wxTreeItemId m_contextMenuItem;
	wxTreeItemId m_dropHighlight;
};

#endif
