#ifndef __LISTCTRLEX_H__
#define __LISTCTRLEX_H__

#include "systemimagelist.h"
#include <wx/dnd.h>

#ifdef __WXMSW__
typedef wxNavigationEnabled<wxListCtrl> wxListCtrlExBase;
#else
typedef wxListCtrl wxListCtrlExBase;
#endif
class wxListCtrlEx : public wxListCtrlExBase, public CSystemImageList
{
public:
	typedef int Item;

	wxListCtrlEx(wxWindow *parent,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxLC_ICON,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxListCtrlNameStr);
	~wxListCtrlEx();

	// Ensure that the given item is the first in the list
	void ScrollTopItem(int item);

	void EnablePrefixSearch(bool enable) { m_prefixSearch_enabled = enable; }

	// Reducing item count does not reset the focused item
	// if using the generic list control. Work around it.
	void SaveSetItemCount(long count);

	void ShowColumnEditor();

	void ShowColumn(unsigned int col, bool show);

	// Moves column. Target position includes both hidden
	// as well as shown columns
	void MoveColumn(unsigned int col, unsigned int before);

	// Do not call after calling LoadColumnSettings
	void AddColumn(const wxString& name, int align, int initialWidth, bool fixed = false);

	// LoadColumnSettings needs to be called exactly once after adding
	// all columns
	void LoadColumnSettings(int widthsOptionId, int visibilityOptionId, int sortOptionId);
	void SaveColumnSettings(int widthsOptionId, int visibilityOptionId, int sortOptionId);

	int GetColumnVisibleIndex(int col);

	// Refresh list but not header
	void RefreshListOnly(bool eraseBackground = true);

	void CancelLabelEdit();
	void SetLabelEditBlock(bool block);

	wxWindow* GetMainWindow();
	wxWindow const* GetMainWindow() const;

	bool HasSelection() const;

	// Returns the true rect of the area containing the actual list items
	wxRect GetActualClientRect() const;

	bool Valid(int i) const { return i >= 0 && i < GetItemCount(); }
	int GetFirstItem() const { return GetItemCount() ? 0 : -1; }
	int GetLastItem() const { return GetItemCount() - 1; }
	int GetBottomItem() const {
		if (GetItemCount()) {
			return wxMin(GetLastItem(), GetTopItem() + GetCountPerPage() - 1);
		}
		return -1;
	}

	int GetNextItemSimple(int i) const {
		if (!Valid(++i)) {
			i = -1;
		}
		return i;
	}

	int GetPrevItemSimple(int i) const {
		if (!Valid(--i)) {
			i = -1;
		}
		return i;
	}

protected:
	virtual void OnPostScroll();
	virtual void OnPreEmitPostScrollEvent();
	void EmitPostScrollEvent();

	virtual wxString GetItemText(int, unsigned int) { return _T(""); }

	virtual wxString OnGetItemText(long item, long column) const;
	void ResetSearchPrefix();

	// Argument is visible column index
	int GetHeaderSortIconIndex(int col);
	void SetHeaderSortIconIndex(int col, int icon);

	// Has to be called after setting the image list for the items
	void InitHeaderSortImageList();
#ifdef __WXMSW__
	wxImageListEx *m_pHeaderImageList{};
#endif
	struct _header_icon_index
	{
		int up{-1};
		int down{-1};
	} m_header_icon_index;

private:
	// Keyboard prefix search
	void HandlePrefixSearch(wxChar character);
	int FindItemWithPrefix(const wxString& searchPrefix, int start);

	DECLARE_EVENT_TABLE()
	void OnPostScrollEvent(wxCommandEvent& event);
	void OnScrollEvent(wxScrollWinEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnSelectionChanged(wxListEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnBeginLabelEdit(wxListEvent& event);
	void OnEndLabelEdit(wxListEvent& event);
	void OnColumnDragging(wxListEvent& event);

	bool m_prefixSearch_enabled{};
	wxDateTime m_prefixSearch_lastKeyPress;
	wxString m_prefixSearch_prefix;

	bool ReadColumnWidths(unsigned int optionId);
	void SaveColumnWidths(unsigned int optionId);

	void CreateVisibleColumnMapping();

	virtual bool OnBeginRename(const wxListEvent& event);
	virtual bool OnAcceptRename(const wxListEvent& event);

	struct t_columnInfo
	{
		wxString name;
		int align;
		int width;
		bool shown;
		unsigned int order;
		bool fixed;
	};
	std::vector<t_columnInfo> m_columnInfo;
	unsigned int *m_pVisibleColumnMapping{};

#ifdef __WXMSW__
	virtual bool MSWOnNotify(int idCtrl, WXLPARAM lParam, WXLPARAM *result);
	bool m_columnDragging{};
#endif

#ifndef __WXMSW__
	bool m_editing;
#endif
	int m_blockedLabelEditing;
};

class CLabelEditBlocker
{
public:
	CLabelEditBlocker(wxListCtrlEx& listCtrl);
	virtual ~CLabelEditBlocker();
private:
	wxListCtrlEx& m_listCtrl;
};

#endif //__LISTCTRLEX_H__
