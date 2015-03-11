#ifndef __DIRECTORYLISTINGPARSER_H__
#define __DIRECTORYLISTINGPARSER_H__

/* This class is responsible for parsing the directory listings returned by
 * the server.
 * Unfortunatly, RFC959 did not specify the format of directory listings, so
 * each server uses its own format. In addition to that, in most cases the
 * listings were not designed to be machine-parsable, they were meant to be
 * human readable by users of that particular server.
 * By far the most common format is the one returned by the Unix "ls -l"
 * command. However, legacy systems are still in place, especially in big
 * companies. These often use very exotic listing styles.
 * Another problem are localized listings containing date strings. In some
 * cases these listings are ambiguous and cannot be distinguished.
 * Example for an ambiguous date: 04-05-06. All of the 6 permutations for
 * the location of year, month and day are valid dates.
 * Some servers send multiline listings where a single entry can span two
 * lines, this has to be detected as well, as far as possible.
 *
 * Some servers send MVS style listings which can consist of just the
 * filename without any additional data. In order to prevent problems, this
 * format is only parsed if the server is in fact recognizes as MVS server.
 *
 * Please see tests/dirparsertest.cpp for a list of supported formats and the
 * expected parser result.
 *
 * If adding data to the parser, it first decomposes the raw data into lines,
 * which then are processed further. Each line gets consecutively tested for
 * different formats, starting with the most common Unix style format.
 * Lines not containing a recognized format (e.g. a part of a multiline
 * entry) are rememberd and if the next line cannot be parsed either, they
 * get concatenated to be parsed again (and discarded if not recognized).
 */

class CLine;
class CToken;
class CControlSocket;

namespace listingEncoding
{
	enum type
	{
		unknown,
		normal,
		ebcdic
	};
}


class CDirectoryListingParser final
{
public:
	CDirectoryListingParser(CControlSocket* pControlSocket, const CServer& server, listingEncoding::type encoding = listingEncoding::unknown, bool sftp_mode = false);
	~CDirectoryListingParser();

	CDirectoryListingParser(CDirectoryListingParser const&) = delete;
	CDirectoryListingParser& operator=(CDirectoryListingParser const&) = delete;

	CDirectoryListing Parse(const CServerPath &path);

	bool AddData(char *pData, int len);
	bool AddLine(const wxChar* pLine);

	void Reset();

	void SetTimezoneOffset(const wxTimeSpan& span) { m_timezoneOffset = span; }

	void SetServer(const CServer& server) { m_server = server; };

protected:
	CLine *GetLine(bool breakAtEnd, bool& error);

	bool ParseData(bool partial);

	bool ParseLine(CLine &line, const enum ServerType serverType, bool concatenated);

	bool ParseAsUnix(CLine &line, CDirentry &entry, bool expect_date);
	bool ParseAsDos(CLine &line, CDirentry &entry);
	bool ParseAsEplf(CLine &line, CDirentry &entry);
	bool ParseAsVms(CLine &line, CDirentry &entry);
	bool ParseAsIbm(CLine &line, CDirentry &entry);
	bool ParseOther(CLine &line, CDirentry &entry);
	bool ParseAsWfFtp(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_PDS(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_PDS2(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_Migrated(CLine &line, CDirentry &entry);
	bool ParseAsIBM_MVS_Tape(CLine &line, CDirentry &entry);
	int ParseAsMlsd(CLine &line, CDirentry &entry);
	bool ParseAsOS9(CLine &line, CDirentry &entry);

	// Only call this if servertype set to ZVM since it conflicts
	// with other formats.
	bool ParseAsZVM(CLine &line, CDirentry &entry);

	// Only call this if servertype set to HPNONSTOP since it conflicts
	// with other formats.
	bool ParseAsHPNonstop(CLine &line, CDirentry &entry);

	// Date / time parsers
	bool ParseUnixDateTime(CLine &line, int &index, CDirentry &entry);
	bool ParseShortDate(CToken &token, CDirentry &entry, bool saneFieldOrder = false);
	bool ParseTime(CToken &token, CDirentry &entry);

	// Parse file sizes given like this: 123.4M
	bool ParseComplexFileSize(CToken& token, wxLongLong& size, int blocksize = -1);

	bool GetMonthFromName(const wxString& name, int &month);

	void DeduceEncoding();
	void ConvertEncoding(char *pData, int len);

	CControlSocket* m_pControlSocket;

	static std::map<wxString, int> m_MonthNamesMap;

	struct t_list
	{
		t_list() = default;
		t_list(char* s, int l)
			: p(s), len(l)
		{}

		char *p;
		int len;
	};

	int m_currentOffset;

	std::list<t_list> m_DataList;
	std::deque<CRefcountObject<CDirentry>> m_entryList;
	wxLongLong m_totalData;

	CLine *m_prevLine;

	CServer m_server;

	bool m_fileListOnly;
	std::list<wxString> m_fileList;

	bool m_maybeMultilineVms;

	wxTimeSpan m_timezoneOffset;

	listingEncoding::type m_listingEncoding;

	bool sftp_mode_{};

	// If not passing a default date/time to wxDateTime::ParseFormat, it internaly uses today as reference.
	// Getting today is slow, so cache it.
	wxDateTime const today_;
};

#endif
