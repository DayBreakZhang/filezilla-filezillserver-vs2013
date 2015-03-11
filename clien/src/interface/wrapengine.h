#ifndef __WRAPENGINE_H__
#define __WRAPENGINE_H__

class CWrapEngine
{
public:
	CWrapEngine();
	virtual ~CWrapEngine() {}

	bool LoadCache();

	static void ClearCache();

	enum wrap_result {
		wrap_failed = 0x01,
		wrap_didwrap = 0x02
	};
	// Find all wxStaticText controls in the given window(s) and wrap them, so
	// that the window has the right aspect ratio...
	bool WrapRecursive(wxWindow* wnd, double ratio, const char* name = "", wxSize canvas = wxSize(), wxSize minRequested = wxSize());
	bool WrapRecursive(std::vector<wxWindow*>& windows, double ratio, const char* name = "", wxSize canvas = wxSize(), wxSize const& minRequested = wxSize());

	// .. or does not exceed the given maximum length.
	int WrapRecursive(wxWindow* wnd, wxSizer* sizer, int max);

	// Find all wxStaticText controls in the window and unwrap them
	bool UnwrapRecursive(wxWindow* wnd, wxSizer* sizer);

	// Wrap the given text so its length in pixels does not exceed maxLength
	bool WrapText(wxWindow* parent, wxString &text, unsigned long maxLength);
	bool WrapTextChinese(wxWindow* parent, wxString &text, unsigned long maxLength);
	bool WrapText(wxWindow* parent, int id, unsigned long maxLength);
	wxString UnwrapText(const wxString& text);

	int GetWidthFromCache(const char* name);

	void CheckLanguage();

protected:
	void UnwrapRecursive_Wrapped(const std::list<int> &wrapped, std::vector<wxWindow*> &windows, bool remove_fitting = false);

	void SetWidthToCache(const char* name, int width);

	std::map<wxChar, unsigned int> m_charWidths;

	bool CanWrapBefore(const wxChar& c);
	bool m_wrapOnEveryChar{};
	const wxChar* m_noWrapChars{};

	wxFont m_font;
	int m_spaceWidth{-1};

	static bool m_use_cache;
};

#endif //__WRAPENGINE_H__
