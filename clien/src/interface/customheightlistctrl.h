#ifndef __CUSTOMHIGHTLISTCTRL_H__
#define __CUSTOMHIGHTLISTCTRL_H__

#include <set>
#include <wx/scrolwin.h>

class wxCustomHeightListCtrl : public wxScrolledWindow
{
public:
	wxCustomHeightListCtrl(wxWindow* parent, wxWindowID id = -1, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxHSCROLL | wxVSCROLL, const wxString& name = _T("scrolledWindow"));

	void SetLineHeight(int height);
	void SetLineCount(int count);

	virtual void SetFocus();

	void ClearSelection();

	std::set<int> GetSelection() const;
	void SelectLine(int line);

	void AllowSelection(bool allow_selection);

protected:
	virtual void OnDraw(wxDC& dc);

	DECLARE_EVENT_TABLE()
	void OnMouseEvent(wxMouseEvent& event);

	int m_lineHeight;
	int m_lineCount;

	std::set<int> m_selectedLines;
	int m_focusedLine;

	bool m_allow_selection;
};

#endif //__CUSTOMHIGHTLISTCTRL_H__
