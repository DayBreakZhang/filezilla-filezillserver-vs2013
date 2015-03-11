#ifndef __SETTINGSDIALOG_H__
#define __SETTINGSDIALOG_H__

#include "../dialogex.h"

class COptions;
class COptionsPage;
class CMainFrame;
class CSettingsDialog : public wxDialogEx
{
public:
	CSettingsDialog(CFileZillaEngineContext & engine_context);
	virtual ~CSettingsDialog();

	bool Create(CMainFrame* pMainFrame);
	bool LoadSettings();

	CMainFrame* m_pMainFrame{};

	CFileZillaEngineContext& GetEngineContext() { return m_engine_context; }

protected:
	bool LoadPages();

	COptions* m_pOptions;

	COptionsPage* m_activePanel{};

	void AddPage( wxString const& name, COptionsPage* page, int nest );

	struct t_page
	{
		wxTreeItemId id;
		COptionsPage* page{};
	};
	std::vector<t_page> m_pages;

	DECLARE_EVENT_TABLE()
	void OnPageChanging(wxTreeEvent& event);
	void OnPageChanged(wxTreeEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);

	CFileZillaEngineContext& m_engine_context;
};

#endif //__SETTINGSDIALOG_H__
