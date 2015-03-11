#include <filezilla.h>

#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_themes.h"
#include "../themeprovider.h"

#include <wx/dcclient.h>
#include <wx/scrolwin.h>

BEGIN_EVENT_TABLE(COptionsPageThemes, COptionsPage)
EVT_CHOICE(XRCID("ID_THEME"), COptionsPageThemes::OnThemeChange)
END_EVENT_TABLE()

const int BORDER = 5;

class CIconPreview : public wxScrolledWindow
{
public:
	CIconPreview(wxWindow* pParent)
		: wxScrolledWindow(pParent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSTATIC_BORDER | wxVSCROLL)
	{
		m_sizeInitialized = false;
		m_extra_padding = 0;
	}

	void LoadIcons(const wxString& theme, const wxSize& iconSize)
	{
		m_iconSize = iconSize;
		m_icons = CThemeProvider::GetAllImages(theme, m_iconSize);
	}

	void CalcSize()
	{
		if (m_sizeInitialized)
			return;
		m_sizeInitialized = true;

		wxSize size = GetClientSize();

		if (!m_icons.empty()) {
			int icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));

			// Number of lines and line height
			int lines = (m_icons.size() - 1) / icons_per_line + 1;
			int vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
			if (vheight > size.GetHeight()) {
				// Scroll bar would appear, need to adjust width
				size.SetHeight(vheight);
				SetVirtualSize(size);
				SetScrollRate(0, m_iconSize.GetHeight() + BORDER);

				wxSize size2 = GetClientSize();
				size.SetWidth(size2.GetWidth());

				icons_per_line = wxMax(1, (size.GetWidth() - BORDER) / (m_iconSize.GetWidth() + BORDER));
				lines = (m_icons.size() - 1) / icons_per_line + 1;
				vheight = lines * (m_iconSize.GetHeight() + BORDER) + BORDER;
				if (vheight > size.GetHeight())
					size.SetHeight(vheight);
			}

			// Calculate extra padding
			if (icons_per_line > 1) {
				int extra = size.GetWidth() - BORDER - icons_per_line * (m_iconSize.GetWidth() + BORDER);
				m_extra_padding = extra / (icons_per_line - 1);
			}
		}
		SetVirtualSize(size);
		SetScrollRate(0, m_iconSize.GetHeight() + BORDER);
	}

protected:
	DECLARE_EVENT_TABLE()
	virtual void OnPaint(wxPaintEvent& event)
	{
		CalcSize();

		wxPaintDC dc(this);
		PrepareDC(dc);

		wxSize size = GetClientSize();

		if (m_icons.empty()) {
			dc.SetFont(GetFont());
			wxString text = _("No images available");
			wxCoord w, h;
			dc.GetTextExtent(text, &w, &h);
			dc.DrawText(text, (size.GetWidth() - w) / 2, (size.GetHeight() - h) / 2);
			return;
		}

		int x = BORDER;
		int y = BORDER;

		for( auto const& bmp : m_icons ) {
			dc.DrawBitmap(*bmp, x, y, true);
			x += m_iconSize.GetWidth() + BORDER + m_extra_padding;
			if ((x + m_iconSize.GetWidth() + BORDER) > size.GetWidth()) {
				x = BORDER;
				y += m_iconSize.GetHeight() + BORDER;
			}
		}
	}

	std::vector<std::unique_ptr<wxBitmap>> m_icons;
	wxSize m_iconSize;
	bool m_sizeInitialized;
	int m_extra_padding;
};

BEGIN_EVENT_TABLE(CIconPreview, wxScrolledWindow)
EVT_PAINT(CIconPreview::OnPaint)
END_EVENT_TABLE()

COptionsPageThemes::~COptionsPageThemes()
{
}

bool COptionsPageThemes::LoadPage()
{
	return true;
}

bool COptionsPageThemes::SavePage()
{
	if (!m_was_selected)
		return true;

	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);

	const int sel = pTheme->GetSelection();
	const wxString theme = ((wxStringClientData*)pTheme->GetClientObject(sel))->GetData();

	m_pOptions->SetOption(OPTION_THEME, theme);

	wxNotebook *pPreview = XRCCTRL(*this, "ID_PREVIEW", wxNotebook);
	wxString size = pPreview->GetPageText(pPreview->GetSelection());
	m_pOptions->SetOption(OPTION_THEME_ICONSIZE, size);

	return true;
}

bool COptionsPageThemes::Validate()
{
	return true;
}

bool COptionsPageThemes::DisplayTheme(const wxString& theme)
{
	wxString name, author, mail;
	if (!CThemeProvider::GetThemeData(theme, name, author, mail)) {
		return false;
	}
	if (name.empty()) {
		return false;
	}

	if (author.empty()) {
		author = _("N/a");
	}
	if (mail.empty()) {
		mail = _("N/a");
	}

	bool failure = false;
	SetStaticText(XRCID("ID_AUTHOR"), author, failure);
	SetStaticText(XRCID("ID_EMAIL"), mail, failure);

	wxNotebook *pPreview = XRCCTRL(*this, "ID_PREVIEW", wxNotebook);

	wxString selected_size;
	int selected_page = pPreview->GetSelection();
	if (selected_page != -1)
		selected_size = pPreview->GetPageText(selected_page);
	else
		selected_size = COptions::Get()->GetOption(OPTION_THEME_ICONSIZE);

	pPreview->DeleteAllPages();

	for( auto const& size : CThemeProvider::GetThemeSizes(theme) ) {
		int pos = size.Find('x');
		if (pos <= 0 || pos >= (int)size.Len() - 1)
			continue;

		long width = 0;
		long height = 0;
		if (!size.Left(pos).ToLong(&width) ||
			!size.Mid(pos + 1).ToLong(&height) ||
			width <= 0 || height <= 0)
		{
			continue;
		}

		wxSize iconSize(width, height);

		wxPanel* pPanel = new wxPanel();
#ifdef __WXMSW__
		// Drawing glitch on MSW
		pPanel->Hide();
#endif
		pPanel->Create(pPreview);
		wxSizer* pSizer = new wxBoxSizer(wxVERTICAL);
		pPanel->SetSizer(pSizer);
		pSizer->Add(new wxStaticText(pPanel, wxID_ANY, _("Preview:")), 0, wxLEFT|wxRIGHT|wxTOP, 5);

		CIconPreview *pIconPreview = new CIconPreview(pPanel);
		pIconPreview->LoadIcons(theme, iconSize);

		pSizer->Add(pIconPreview, 1, wxGROW | wxALL, 5);

		pPreview->AddPage(pPanel, size, selected_size == size);
	}

	return !failure;
}

void COptionsPageThemes::OnThemeChange(wxCommandEvent& event)
{
	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);

	const int sel = pTheme->GetSelection();
	const wxString theme = ((wxStringClientData*)pTheme->GetClientObject(sel))->GetData();

	DisplayTheme(theme);
}

bool COptionsPageThemes::OnDisplayedFirstTime()
{
	bool failure = false;

	wxChoice* pTheme = XRCCTRL(*this, "ID_THEME", wxChoice);
	if (!pTheme)
		return false;

	if (!pTheme || !XRCCTRL(*this, "ID_PREVIEW", wxNotebook) ||
		!XRCCTRL(*this, "ID_AUTHOR", wxStaticText) ||
		!XRCCTRL(*this, "ID_EMAIL", wxStaticText))
		return false;

	auto const themes = CThemeProvider::GetThemes();
	if (themes.empty())
		return false;

	wxString activeTheme = m_pOptions->GetOption(OPTION_THEME);
	wxString firstName;
	for (auto const& theme : themes) {
		wxString name, author, mail;
		if (!CThemeProvider::GetThemeData(theme, name, author, mail))
			continue;
		if (firstName.empty())
			firstName = name;
		int n = pTheme->Append(name, new wxStringClientData(theme));
		if (theme == activeTheme)
			pTheme->SetSelection(n);
	}
	if (pTheme->GetSelection() == wxNOT_FOUND)
		pTheme->SetSelection(pTheme->FindString(firstName));
	activeTheme = ((wxStringClientData*)pTheme->GetClientObject(pTheme->GetSelection()))->GetData();

	if (!DisplayTheme(activeTheme))
		failure = true;

	pTheme->GetContainingSizer()->Layout();

	return !failure;
}
