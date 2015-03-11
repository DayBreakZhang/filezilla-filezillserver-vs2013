#ifndef __ABOUTDIALOG_H__
#define __ABOUTDIALOG_H__

#include "dialogex.h"

class CAboutDialog : public wxDialogEx
{
public:
	CAboutDialog() {}
	virtual ~CAboutDialog() {}

	bool Create(wxWindow* parent);

protected:

	DECLARE_EVENT_TABLE()
	void OnOK(wxCommandEvent&);
	void OnCopy(wxCommandEvent&);
};

#endif //__ABOUTDIALOG_H__
