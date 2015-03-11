#ifndef __OPTIONSPAGE_TRANSFER_H__
#define __OPTIONSPAGE_TRANSFER_H__

class COptionsPageTransfer : public COptionsPage
{
public:
	virtual wxString GetResourceName() { return _T("ID_SETTINGS_TRANSFER"); }
	virtual bool LoadPage();
	virtual bool SavePage();
	virtual bool Validate();

protected:
	DECLARE_EVENT_TABLE()
	void OnToggleSpeedLimitEnable(wxCommandEvent& event);
};

#endif //__OPTIONSPAGE_TRANSFER_H__
