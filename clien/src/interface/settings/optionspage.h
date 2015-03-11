#ifndef __OPTIONSPAGE_H__
#define __OPTIONSPAGE_H__

#define SAFE_XRCCTRL(id, type) \
	if (!XRCCTRL(*this, id, type)) \
		return false; \
	XRCCTRL(*this, id, type)

class COptions;
class CSettingsDialog;
class COptionsPage : public wxPanel
{
public:
	bool CreatePage(COptions* pOptions, CSettingsDialog* pOwner, wxWindow* parent, wxSize& maxSize);

	virtual wxString GetResourceName() = 0;
	virtual bool LoadPage() = 0;
	virtual bool SavePage() = 0;
	virtual bool Validate() { return true; }

	void SetCheck(int id, bool checked, bool& failure);
	void SetCheckFromOption(int control_id, int option_id, bool& failure);
	void SetRCheck(int id, bool checked, bool& failure);
	void SetTextFromOption(int ctrlId, int optionId, bool& failure);
	void SetStaticText(int id, const wxString& text, bool& failure);
	void SetChoice(int id, int selection, bool& failure);
	bool SetText(int id, const wxString& text, bool& failure);

	// The GetXXX functions do never return an error since the controls were
	// checked to exist while loading the dialog.
	bool GetCheck(int id) const;
	bool GetRCheck(int id) const;
	wxString GetText(int id) const;
	wxString GetStaticText(int id) const;
	int GetChoice(int id) const;

	void SetOptionFromText(int ctrlId, int optionId);
	void SetIntOptionFromText(int ctrlId, int optionId); // There's no corresponding GetTextFromIntOption as COptions::GetOption is smart enough to convert
	void SetOptionFromCheck(int control_id, int option_id);

	void ReloadSettings();

	// Always returns false
	bool DisplayError(const wxString& controlToFocus, const wxString& error);
	bool DisplayError(wxWindow* pWnd, const wxString& error);

	bool Display();

	virtual bool OnDisplayedFirstTime();

protected:
	COptions* m_pOptions;
	CSettingsDialog* m_pOwner;

	bool m_was_selected;
};

#endif //__OPTIONSPAGE_H__
