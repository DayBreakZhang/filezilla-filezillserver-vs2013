#ifndef __VIEW_H__
#define __VIEW_H__

class CViewHeader;
class CView : public wxNavigationEnabled<wxWindow>
{
public:
	CView(wxWindow* pParent);

	void SetWindow(wxWindow* pWnd);
	void SetHeader(CViewHeader* pWnd);
	CViewHeader* GetHeader() { return m_pHeader; }
	CViewHeader* DetachHeader();
	void SetStatusBar(wxStatusBar* pStatusBar);
	wxStatusBar* GetStatusBar() { return m_pStatusBar; }

protected:
	void FixTabOrder();

	wxWindow* m_pWnd{};
	CViewHeader* m_pHeader{};
	wxStatusBar* m_pStatusBar{};

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent&);
};

#endif //__VIEW_H__
