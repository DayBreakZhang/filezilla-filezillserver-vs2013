#include <filezilla.h>
#include "locale_initializer.h"
#ifdef HAVE_LIBTINYXML
#include <tinyxml.h>
#else
#include "../tinyxml/tinyxml.h"
#endif
#include <string>
#include <locale.h>
#include <sys/stat.h>

#ifdef ENABLE_BINRELOC
	#define BR_PTHREADS 0
	#include "prefix.h"
#endif

struct t_fallbacks
{
	const char* locale;
	const char* fallback;
};

struct t_fallbacks fallbacks[] = {

	// The following entries are needed due to missing language codes wxWidgets
	{ "ka", "ka_GE" },
	{ "ku", "ku_TR" },
	{ "ne", "ne_NP" },

	// Fallback chain for English
	{ "en", "en_US" },
	{ "en_US", "en_GB" },
	{ "en_GB", "C" },

	// Base names
	{ "ar", "ar_EG" },
	{ "ca", "ca_ES" },
	{ "cy", "cy_GB" },
	{ "de", "de_DE" },
	{ "el", "el_GR" },
	{ "es", "es_ES" },
	{ "et", "et_EE" },
	{ "fr", "fr_FR" },
	{ "it", "it_IT" },
	{ "nl", "nl_NL" },
	{ "ru", "ru_RU" },
	{ "sv", "sv_SE" },
	{ "tr", "tr_TR" },

	{ 0, 0 }
};

wxString GetFallbackLocale( wxString const& locale )
{
	int i = 0;
	while (fallbacks[i].locale) {
		if (fallbacks[i].locale == locale)
			return fallbacks[i].fallback;
		i++;
	}

	return wxString();
}

// Custom main method to initialize proper locale
#ifdef __WXGTK__

bool CInitializer::error = false;

static std::string mkstr(const char* str)
{
	if (!str)
		return "";
	else
		return str;
}

int main(int argc, char** argv)
{
	std::string locale = CInitializer::GetLocaleOption();
	if (locale != "")
	{
		if (!CInitializer::SetLocale(locale))
		{
#ifdef __WXDEBUG__
			printf("failed to set locale\n");
#endif
			CInitializer::error = true;
		}
		else
		{
#ifdef __WXDEBUG__
			printf("locale set to %s\n", setlocale(LC_ALL, 0));
#endif
		}
	}

	return wxEntry(argc, argv);
}

bool CInitializer::SetLocaleReal(const std::string& locale)
{
	if (!setlocale(LC_ALL, locale.c_str()))
		return false;

#ifdef __WXDEBUG__
	printf("setlocale %s successful\n", locale.c_str());
#endif
#ifdef HAVE_SETENV
	setenv("LC_ALL", locale.c_str(), 1);
#else
	std::string str("LC_ALL=");
	str += locale;
	putenv(str.c_str());
#endif
	return true;
}

bool CInitializer::SetLocale(const std::string& arg)
{
	const char *encodings[] = {
		"UTF-8",
		"UTF8",
		"utf-8",
		"utf8",
		0
	};

	for (int i = 0; encodings[i]; i++)
	{
		std::string locale = CInitializer::LocaleAddEncoding(arg, encodings[i]);
		if (SetLocaleReal(locale))
			return true;
	}

	if (CInitializer::SetLocaleReal(arg))
		return true;

	int i = 0;
	while (fallbacks[i].locale)
	{
		if (fallbacks[i].locale == arg)
			return SetLocale(fallbacks[i].fallback);
		i++;
	}

	return false;
}

std::string CInitializer::CheckPathForDefaults(std::string path, int strip, std::string suffix)
{
	if (path.empty())
		return "";

	if (path[path.size() - 1] == '/')
		path = path.substr(0, path.size() - 1);
	while (strip--)
	{
		int p = path.rfind('/');
		if (p == -1)
			return "";
		path = path.substr(0, p);
	}

	path += '/' + suffix;
	struct stat buf;
	if (!stat(path.c_str(), &buf))
		return path;

	return "";
}

std::string CInitializer::GetDefaultsXmlFile()
{
	std::string fzdatadir = mkstr(getenv("FZ_DATADIR"));
	std::string file = CheckPathForDefaults(fzdatadir, 0, "fzdefaults.xml");
	if (!file.empty())
		return file;
	file = CheckPathForDefaults(fzdatadir, 1, "fzdefaults.xml");
	if (!file.empty())
		return file;

	file = GetUnadjustedSettingsDir() + "fzdefaults.xml";

	{
		struct stat buf{};
		if (!stat(file.c_str(), &buf)) {
			return file;
		}
	}

	file = "/etc/filezilla/fzdefaults.xml";

	{
		struct stat buf{};
		if (!stat(file.c_str(), &buf))
			return file;
	}


	file = CheckPathForDefaults(mkstr(SELFPATH), 2, "share/filezilla/fzdefaults.xml");
	if (!file.empty())
		return file;
	file = CheckPathForDefaults(mkstr(DATADIR), 0, "filezilla/fzdefaults.xml");
	if (!file.empty())
		return file;

	std::string path = mkstr(getenv("PATH"));
	while (!path.empty())
	{
		std::string segment;
		int pos = path.find(':');
		if (pos == -1)
			segment.swap(path);
		else
		{
			segment = path.substr(0, pos);
			path = path.substr(pos + 1);
		}

		file = CheckPathForDefaults(segment, 1, "share/filezilla/fzdefaults.xml");
		if (!file.empty())
			return file;
	}

	return "";
}

std::string CInitializer::ReadSettingsFromDefaults(std::string file)
{
	std::string dir = CInitializer::GetSettingFromFile(file, "Config Location");

	if (dir.empty())
		return "";

	std::string result;
	while (!dir.empty())
	{
		std::string token;
		int pos = dir.find('/');
		if (pos == -1)
			token.swap(dir);
		else
		{
			token = dir.substr(0, pos);
			dir = dir.substr(pos + 1);
		}

		if (!token.empty() && token[0] == '$')
		{
			if (token[1] == '$')
				result += token.substr(1);
			else if (token.size() > 1)
			{
				std::string value = mkstr(getenv(token.substr(1).c_str()));
				result += value;
			}
		}
		else
			result += token;

		result += '/';
	}

	struct stat buf;
	if (stat(result.c_str(), &buf))
		return "";

	if (result[result.size() - 1] != '/')
		result += '/';

	return result;
}

std::string CInitializer::GetSettingFromFile(std::string file, const std::string& name)
{
	TiXmlDocument xmldoc;
	if (!xmldoc.LoadFile(file.c_str()))
		return "";

	TiXmlElement* main = xmldoc.FirstChildElement("FileZilla3");
	if (!main)
		return "";

	TiXmlElement* settings = main->FirstChildElement("Settings");
	if (!settings)
		return "";

	for (TiXmlElement* setting = settings->FirstChildElement("Setting"); setting; setting = setting->NextSiblingElement("Setting"))
	{
		const char* nodeVal = setting->Attribute("name");
		if (!nodeVal || strcmp(nodeVal, name.c_str()))
			continue;

		TiXmlNode* textNode = setting->FirstChild();
		if (!textNode || !textNode->ToText())
			continue;

		return mkstr(textNode->Value());
	}

	return "";
}

namespace {
std::string TryDirectory( std::string const& env, std::string const& suffix, bool check_exists )
{
	std::string path = mkstr(getenv(env.c_str()));
	if( !path.empty() && path[0] == '/' ) {
		if( path[path.size()-1] != '/' ) {
			path += '/';
		}

		path += suffix;

		if( check_exists ) {
			struct stat buf{};
			int res = stat(path.c_str(), &buf);
			if( res || !S_ISDIR(buf.st_mode) ) {
				path.clear();
			}
		}
	}
	else {
		path.clear();
	}
	return path;
}
}

std::string CInitializer::GetUnadjustedSettingsDir()
{
	std::string cfg = TryDirectory("XDG_CONFIG_HOME", "filezilla/", true);
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".config/filezilla/", true);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".filezilla/", true);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("XDG_CONFIG_HOME", "filezilla/", false);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".config/filezilla/", false);
	}
	if( cfg.empty() ) {
		cfg = TryDirectory("HOME", ".filezilla/", false);
	}

	return cfg;
}

std::string CInitializer::GetAdjustedSettingsDir()
{
	std::string defaults = GetDefaultsXmlFile();
	if (!defaults.empty())
	{
		std::string dir = CInitializer::ReadSettingsFromDefaults(defaults);
		if (!dir.empty())
			return dir;
	}

	return GetUnadjustedSettingsDir();
}

std::string CInitializer::GetLocaleOption()
{
	const std::string dir = GetAdjustedSettingsDir();
	if (dir.empty())
		return "";

#ifdef __WXDEBUG__
	printf("Reading locale option from %sfilezilla.xml\n", dir.c_str());
#endif
	std::string locale = GetSettingFromFile(dir + "filezilla.xml", "Language Code");

	return locale;
}

std::string CInitializer::LocaleAddEncoding(const std::string& locale, const std::string& encoding)
{
	int pos = locale.find('@');
	if (pos == -1)
		return locale + '.' + encoding;

	return locale.substr(0, pos) + '.' + encoding + locale.substr(pos);
}

#endif //__WXGTK__
