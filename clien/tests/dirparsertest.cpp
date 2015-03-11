#include <libfilezilla.h>
#include <directorylistingparser.h>

#include <cppunit/extensions/HelperMacros.h>
#include <list>

/*
 * This testsuite asserts the correctness of the directory listing parser.
 * It's main purpose is to ensure that all known formats are recognized and
 * parsed as expected. Due to the high amount of variety and unfortunately
 * also ambiguity, the parser is very fragile.
 */

typedef struct
{
	std::string data;
	CDirentry reference;
	enum ServerType serverType;
} t_entry;

class CDirectoryListingParserTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(CDirectoryListingParserTest);
	InitEntries();
	for (unsigned int i = 0; i < m_entries.size(); i++)
		CPPUNIT_TEST(testIndividual);
	CPPUNIT_TEST(testAll);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp();
	void tearDown() {}

	void testIndividual();
	void testAll();
	void testSpecial();

	static std::vector<t_entry> m_entries;

	static wxCriticalSection m_sync;

protected:
	static void InitEntries();

	t_entry m_entry;
};

wxCriticalSection CDirectoryListingParserTest::m_sync;
std::vector<t_entry> CDirectoryListingParserTest::m_entries;

CPPUNIT_TEST_SUITE_REGISTRATION(CDirectoryListingParserTest);

typedef CRefcountObject<wxString> R;
typedef CSparseOptional<wxString> O;

static int calcYear(int month, int day)
{
	const int cur_year = wxDateTime::GetCurrentYear();
	const int cur_month = wxDateTime::GetCurrentMonth() + 1;
	const int cur_day = wxDateTime::Now().GetDay();

	// Not exact but good enough for our purpose
	const int day_of_year = month * 31 + day;
	const int cur_day_of_year = cur_month * 31 + cur_day;
	if (day_of_year > (cur_day_of_year + 1))
		return cur_year - 1;
	else
		return cur_year;
}

void CDirectoryListingParserTest::InitEntries()
{
	// Unix-style listings
	// -------------------

	// We start with a perfect example of a unix style directory listing without anomalies.
	m_entries.push_back((t_entry){
			"dr-xr-xr-x   2 root     other        512 Apr  8  1994 01-unix-std dir",
			{
				_T("01-unix-std dir"),
				512,
				R(_T("dr-xr-xr-x")),
				R(_T("root other")),
				CDirentry::flag_dir,
				O(),
				CDateTime(1994, 4, 8)
			},
			DEFAULT
		});

	// This one is a recent file with a time instead of the year.
	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root     other        531 3 29 03:26 02-unix-std file",
			{
				_T("02-unix-std file"),
				531,
				R(_T("-rw-r--r--")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(calcYear(3, 29), 3, 29, 3, 26)
			},
			DEFAULT
		});

	// Group omitted
	m_entries.push_back((t_entry){
			"dr-xr-xr-x   2 root                  512 Apr  8  1994 03-unix-nogroup dir",
			{
				_T("03-unix-nogroup dir"),
				512,
				R(_T("dr-xr-xr-x")),
				R(_T("root")),
				CDirentry::flag_dir,
				O(),
				CDateTime(1994, 4, 8)
			},
			DEFAULT
		});

	// Symbolic link
	m_entries.push_back((t_entry){
			"lrwxrwxrwx   1 root     other          7 Jan 25 00:17 04-unix-std link -> usr/bin",
			{
				_T("04-unix-std link"),
				7,
				R(_T("lrwxrwxrwx")),
				R(_T("root other")),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(_T("usr/bin")),
				CDateTime(calcYear(1, 25), 1, 25, 0, 17)
			},
			DEFAULT
		});

	// Some listings with uncommon date/time format
	// --------------------------------------------

	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root     other        531 09-26 2000 05-unix-date file",
			{
				_T("05-unix-date file"),
				531,
				R(_T("-rw-r--r--")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(2000, 9, 26)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root     other        531 09-26 13:45 06-unix-date file",
			{
				_T("06-unix-date file"),
				531,
				R(_T("-rw-r--r--")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(calcYear(9, 26), 9, 26, 13, 45)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root     other        531 2005-06-07 21:22 07-unix-date file",
			{
				_T("07-unix-date file"),
				531,
				R(_T("-rw-r--r--")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(2005, 6, 7, 21, 22)
			},
			DEFAULT
		});


	// Unix style with size information in kilobytes
	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root     other  33.5k Oct 5 21:22 08-unix-namedsize file",
			{
				_T("08-unix-namedsize file"),
				335 * 1024 / 10,
				R(_T("-rw-r--r--")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(calcYear(10, 5), 10, 5, 21, 22)
			},
			DEFAULT
		});

	// NetWare style listings
	// ----------------------

	m_entries.push_back((t_entry){
			"d [R----F--] supervisor            512       Jan 16 18:53    09-netware dir",
			{
				_T("09-netware dir"),
				512,
				R(_T("d [R----F--]")),
				R(_T("supervisor")),
				CDirentry::flag_dir,
				O(),
				CDateTime(calcYear(1, 16), 1, 16, 18, 53)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"- [R----F--] rhesus             214059       Oct 20 15:27    10-netware file",
			{
				_T("10-netware file"),
				214059,
				R(_T("- [R----F--]")),
				R(_T("rhesus")),
				0,
				O(),
				CDateTime(calcYear(10, 20), 10, 20, 15, 27)
			},
			DEFAULT
		});

	// NetPresenz for the Mac
	// ----------------------

	// Actually this one isn't parsed properly:
	// The numerical username is mistaken as size. However,
	// this is ambiguous to the normal unix style listing.
	// It's not possible to recognize both formats the right way.
	m_entries.push_back((t_entry){
			"-------r--         326  1391972  1392298 Nov 22  1995 11-netpresenz file",
			{
				_T("11-netpresenz file"),
				1392298,
				R(_T("-------r--")),
				R(_T("1391972")),
				0,
				O(),
				CDateTime(1995, 11, 22)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"drwxrwxr-x               folder        2 May 10  1996 12-netpresenz dir",
			{
				_T("12-netpresenz dir"),
				2,
				R(_T("drwxrwxr-x")),
				R(_T("folder")),
				CDirentry::flag_dir,
				O(),
				CDateTime(1996, 5, 10)
			},
			DEFAULT
		});

	// A format with domain field some windows servers send
	m_entries.push_back((t_entry){
			"-rw-r--r--   1 group domain user 531 Jan 29 03:26 13-unix-domain file",
			{
				_T("13-unix-domain file"),
				531,
				R(_T("-rw-r--r--")),
				R(_T("group domain user")),
				0,
				O(),
				CDateTime(calcYear(1, 29), 1, 29, 3, 26)
			},
			DEFAULT
		});

	// EPLF directory listings
	// -----------------------

	// See http://cr.yp.to/ftp/list/eplf.html (mirrored at https://filezilla-project.org/specs/eplf.html)

	wxDateTime utc(1, wxDateTime::Mar, 1996, 22, 15, 3);
	utc.MakeFromTimezone(wxDateTime::UTC);
	m_entries.push_back((t_entry){
			"+i8388621.48594,m825718503,r,s280,up755\t14-eplf file",
			{
				_T("14-eplf file"),
				280,
				R(_T("755")),
				R(),
				0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

	utc = wxDateTime(13, wxDateTime::Feb, 1996, 23, 58, 27);
	utc.MakeFromTimezone(wxDateTime::UTC);
	m_entries.push_back((t_entry){
			"+i8388621.50690,m824255907,/,\t15-eplf dir",
			{
				_T("15-eplf dir"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir | 0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

	// MSDOS type listing used by old IIS
	// ----------------------------------

	m_entries.push_back((t_entry){
			"04-27-00  12:09PM       <DIR>          16-dos-dateambiguous dir",
			{
				_T("16-dos-dateambiguous dir"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2000, 4, 27, 12, 9)
			},
			DEFAULT
		});

	// Ambiguous date and AM/PM crap. Some evil manager must have forced the poor devs to implement this
	m_entries.push_back((t_entry){
			"04-06-00  03:47PM                  589 17-dos-dateambiguous file",
			{
				_T("17-dos-dateambiguous file"),
				589,
				R(),
				R(),
				0,
				O(),
				CDateTime(2000, 4, 6, 15, 47)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"2002-09-02  18:48       <DIR>          18-dos-longyear dir",
			{
				_T("18-dos-longyear dir"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2002, 9, 2, 18, 48)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"2002-09-02  19:06                9,730 19-dos-longyear file",
			{
				_T("19-dos-longyear file"),
				9730,
				R(),
				R(),
				0,
				O(),
				CDateTime(2002, 9, 2, 19, 6)
			},
			DEFAULT
		});

	// Numerical unix style listing
	utc = wxDateTime(29, wxDateTime::Nov, 1973, 21, 33, 9);
	utc.MakeFromTimezone(wxDateTime::UTC);
	m_entries.push_back((t_entry){
			"0100644   500  101   12345    123456789       20-unix-numerical file",
			{
				_T("20-unix-numerical file"),
				12345,
				R(_T("0100644")),
				R(_T("500 101")),
				0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

	// VShell servers
	// --------------

	m_entries.push_back((t_entry){
			"206876  Apr 04, 2000 21:06 21-vshell-old file",
			{
				_T("21-vshell-old file"),
				206876,
				R(),
				R(),
				0,
				O(),
				CDateTime(2000, 4, 4, 21, 6)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"0  Dec 12, 2002 02:13 22-vshell-old dir/",
			{
				_T("22-vshell-old dir"),
				0,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2002, 12, 12, 2, 13)
			},
			DEFAULT
		});

	/* This type of directory listings is sent by some newer versions of VShell
	 * both year and time in one line is uncommon. */
	m_entries.push_back((t_entry){
			"-rwxr-xr-x    1 user group        9 Oct 08, 2002 09:47 23-vshell-new file",
			{
				_T("23-vshell-new file"),
				9,
				R(_T("-rwxr-xr-x")),
				R(_T("user group")),
				0,
				O(),
				CDateTime(2002, 10, 8, 9, 47)
			},
			DEFAULT
		});

	// OS/2 server format
	// ------------------

	// This server obviously isn't Y2K aware
	m_entries.push_back((t_entry){
			"36611      A    04-23-103  10:57  24-os2 file",
			{
				_T("24-os2 file"),
				36611,
				R(),
				R(),
				0,
				O(),
				CDateTime(2003, 4, 23, 10, 57)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			" 1123      A    07-14-99   12:37  25-os2 file",
			{
				_T("25-os2 file"),
				1123,
				R(),
				R(),
				0,
				O(),
				CDateTime(1999, 7, 14, 12, 37)
			},
			DEFAULT
		});

	// Another server not aware of Y2K
	m_entries.push_back((t_entry){
			"    0 DIR       02-11-103  16:15  26-os2 dir",
			{
				_T("26-os2 dir"),
				0,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2003, 2, 11, 16, 15)
			},
			DEFAULT
		});

	// Again Y2K
	m_entries.push_back((t_entry){
			" 1123 DIR  A    10-05-100  23:38  27-os2 dir",
			{
				_T("27-os2 dir"),
				1123,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2000, 10, 5, 23, 38)
			},
			DEFAULT
		});

	// Localized date formats
	// ----------------------

	m_entries.push_back((t_entry){
			"dr-xr-xr-x   2 root     other      2235 26. Juli, 20:10 28-datetest-ger dir",
			{
				_T("28-datetest-ger dir"),
				2235,
				R(_T("dr-xr-xr-x")),
				R(_T("root other")),
				CDirentry::flag_dir,
				O(),
				CDateTime(calcYear(7, 26), 7, 26, 20, 10)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"dr-xr-xr-x   2 root     other      2235 szept 26 20:10 28b-datetest-hungarian dir",
			{
				_T("28b-datetest-hungarian dir"),
				2235,
				R(_T("dr-xr-xr-x")),
				R(_T("root other")),
				CDirentry::flag_dir,
				O(),
				CDateTime(calcYear(9, 26), 9, 26, 20, 10)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r-xr-xr-x   2 root     other      2235 2.   Okt.  2003 29-datetest-ger file",
			{
				_T("29-datetest-ger file"),
				2235,
				R(_T("-r-xr-xr-x")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(2003, 10, 2)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r-xr-xr-x   2 root     other      2235 1999/10/12 17:12 30-datetest file",
			{
				_T("30-datetest file"),
				2235,
				R(_T("-r-xr-xr-x")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(1999, 10, 12, 17, 12)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r-xr-xr-x   2 root     other      2235 24-04-2003 17:12 31-datetest file",
			{
				_T("31-datetest file"),
				2235,
				R(_T("-r-xr-xr-x")),
				R(_T("root other")),
				0,
				O(),
				CDateTime(2003, 4, 24, 17, 12)
			},
			DEFAULT
		});

	// Japanese listing
	// Remark: I'v no idea in which encoding the foreign characters are, but
	// it's not valid UTF-8. Parser has to be able to cope with it somehow.
	m_entries.push_back((t_entry){
			"-rw-r--r--   1 root       sys           8473  4\x8c\x8e 18\x93\xfa 2003\x94\x4e 32-datatest-japanese file",
			{
				_T("32-datatest-japanese file"),
				8473,
				R(_T("-rw-r--r--")),
				R(_T("root sys")),
				0,
				O(),
				CDateTime(2003, 4, 18)
			},
			DEFAULT
		});

	// Some other asian listing format. Those >127 chars are just examples

	m_entries.push_back((t_entry){
			"-rwxrwxrwx   1 root     staff          0 2003   3\xed\xef 20 33-asian date file",
			{
				_T("33-asian date file"),
				0,
				R(_T("-rwxrwxrwx")),
				R(_T("root staff")),
				0,
				O(),
				CDateTime(2003, 3, 20)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r--r--r-- 1 root root 2096 8\xed 17 08:52 34-asian date file",
			{
				_T("34-asian date file"),
				2096,
				R(_T("-r--r--r--")),
				R(_T("root root")),
				0,
				O(),
				CDateTime(calcYear(8, 17), 8, 17, 8, 52)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r-xr-xr-x   2 root  root  96 2004.07.15   35-dotted-date file",
			{
				_T("35-dotted-date file"),
				96,
				R(_T("-r-xr-xr-x")),
				R(_T("root root")),
				0,
				O(),
				CDateTime(2004, 7, 15)
			},
			DEFAULT
		});

	// VMS listings
	// ------------

	m_entries.push_back((t_entry){
			"36-vms-dir.DIR;1  1 19-NOV-2001 21:41 [root,root] (RWE,RWE,RE,RE)",
			{
				_T("36-vms-dir"),
				512,
				R(_T("RWE,RWE,RE,RE")),
				R(_T("root,root")),
				CDirentry::flag_dir,
				O(),
				CDateTime(2001, 11, 19, 21, 41)
			},
			DEFAULT
		});


	m_entries.push_back((t_entry){
			"37-vms-file;1       155   2-JUL-2003 10:30:13.64",
			{
				_T("37-vms-file;1"),
				79360,
				R(),
				R(),
				0,
				O(),
				CDateTime(2003, 7, 2, 10, 30, 13)
			},
			DEFAULT
		});

	/* VMS style listing without time */
	m_entries.push_back((t_entry){
			"38-vms-notime-file;1    2/8    7-JAN-2000    [IV2_XXX]   (RWED,RWED,RE,)",
			{
				_T("38-vms-notime-file;1"),
				1024,
				R(_T("RWED,RWED,RE,")),
				R(_T("IV2_XXX")),
				0,
				O(),
				CDateTime(2000, 1, 7)
			},
			DEFAULT
		});

	/* Localized month */
	m_entries.push_back((t_entry){
			"39-vms-notime-file;1    6/8    15-JUI-2002    PRONAS   (RWED,RWED,RE,)",
			{
				_T("39-vms-notime-file;1"),
				3072,
				R(_T("RWED,RWED,RE,")),
				R(_T("PRONAS")),
				0,
				O(),
				CDateTime(2002, 7, 15)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"40-vms-multiline-file;1\r\n170774/170775     24-APR-2003 08:16:15  [FTP_CLIENT,SCOT]      (RWED,RWED,RE,)",
			{
				_T("40-vms-multiline-file;1"),
				87436288,
				R(_T("RWED,RWED,RE,")),
				R(_T("FTP_CLIENT,SCOT")),
				0,
				O(),
				CDateTime(2003, 4, 24, 8, 16, 15)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"41-vms-multiline-file;1\r\n10     2-JUL-2003 10:30:08.59  [FTP_CLIENT,SCOT]      (RWED,RWED,RE,)",
			{
				_T("41-vms-multiline-file;1"),
				5120,
				R(_T("RWED,RWED,RE,")),
				R(_T("FTP_CLIENT,SCOT")),
				0,
				O(),
				CDateTime(2003, 7, 2, 10, 30, 8)
			},
			DEFAULT
		});

	// VMS style listings with a different field order
	m_entries.push_back((t_entry){
			"42-vms-alternate-field-order-file;1   [SUMMARY]    1/3     2-AUG-2006 13:05  (RWE,RWE,RE,)",
			{
				_T("42-vms-alternate-field-order-file;1"),
				512,
				R(_T("RWE,RWE,RE,")),
				R(_T("SUMMARY")),
				0,
				O(),
				CDateTime(2006, 8, 2, 13, 5)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"43-vms-alternate-field-order-file;1       17-JUN-1994 17:25:37     6308/13     (RWED,RWED,R,)",
			{
				_T("43-vms-alternate-field-order-file;1"),
				3229696,
				R(_T("RWED,RWED,R,")),
				R(),
				0,
				O(),
				CDateTime(1994, 6, 17, 17, 25, 37)
			},
			DEFAULT
		});

	// Miscellaneous listings
	// ----------------------

	/* IBM AS/400 style listing */
	m_entries.push_back((t_entry){
			"QSYS            77824 02/23/00 15:09:55 *DIR 44-ibm-as400 dir/",
			{
				_T("44-ibm-as400 dir"),
				77824,
				R(),
				R(_T("QSYS")),
				CDirentry::flag_dir | 0,
				O(),
				CDateTime(2000, 2, 23, 15, 9, 55)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"QSYS            77824 23/02/00 15:09:55 *FILE 45-ibm-as400-date file",
			{
				_T("45-ibm-as400-date file"),
				77824,
				R(),
				R(_T("QSYS")),
				0,
				O(),
				CDateTime(2000, 2, 23, 15, 9, 55)
			},
			DEFAULT
		});

	/* aligned directory listing with too long size */
	m_entries.push_back((t_entry){
			"-r-xr-xr-x longowner longgroup123456 Feb 12 17:20 46-unix-concatsize file",
			{
				_T("46-unix-concatsize file"),
				123456,
				R(_T("-r-xr-xr-x")),
				R(_T("longowner longgroup")),
				0,
				O(),
				CDateTime(calcYear(2, 12), 2, 12, 17, 20)
			},
			DEFAULT
		});

	/* short directory listing with month name */
	m_entries.push_back((t_entry){
			"-r-xr-xr-x 2 owner group 4512 01-jun-99 47_unix_shortdatemonth file",
			{
				_T("47_unix_shortdatemonth file"),
				4512,
				R(_T("-r-xr-xr-x")),
				R(_T("owner group")),
				0,
				O(),
				CDateTime(1999, 6, 1)
			},
			DEFAULT
		});

	/* Nortel wfFtp router */
	m_entries.push_back((t_entry){
			"48-nortel-wfftp-file       1014196  06/03/04  Thur.   10:20:03",
			{
				_T("48-nortel-wfftp-file"),
				1014196,
				R(),
				R(),
				0,
				O(),
				CDateTime(2004, 6, 3, 10, 20, 3)
			},
			DEFAULT
		});

	/* VxWorks based server used in Nortel routers */
	m_entries.push_back((t_entry){
			"2048    Feb-28-1998  05:23:30   49-nortel-vxworks dir <DIR>",
			{
				_T("49-nortel-vxworks dir"),
				2048,
				R(),
				R(),
				CDirentry::flag_dir | 0,
				O(),
				CDateTime(1998, 2, 28, 5, 23, 30)
			},
			DEFAULT
		});

	/* the following format is sent by the Connect:Enterprise server by Sterling Commerce */
	m_entries.push_back((t_entry){
			"-C--E-----FTP B BCC3I1       7670  1294495 Jan 13 07:42 50-conent file",
			{
				_T("50-conent file"),
				1294495,
				R(_T("-C--E-----FTP")),
				R(_T("B BCC3I1 7670")),
				0,
				O(),
				CDateTime(calcYear(1, 13), 1, 13, 7, 42)
			},
			DEFAULT
		});

	/* Microware OS-9
	 * Notice the yy/mm/dd date format */
	m_entries.push_back((t_entry){
			"20.20 07/03/29 1026 d-ewrewr 2650 85920 51-OS-9 dir",
			{
				_T("51-OS-9 dir"),
				85920,
				R(_T("d-ewrewr")),
				R(_T("20.20")),
				CDirentry::flag_dir,
				O(),
				CDateTime(2007, 3, 29)
			},
			DEFAULT
		});

	/* Localised Unix style listing. Month and day fields are swapped */
	m_entries.push_back((t_entry){
			"drwxr-xr-x 3 user group 512 01 oct 2004 52-swapped-daymonth dir",
			{
				_T("52-swapped-daymonth dir"),
				512,
				R(_T("drwxr-xr-x")),
				R(_T("user group")),
				CDirentry::flag_dir,
				O(),
				CDateTime(2004, 10, 1)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			"-r--r--r-- 0125039 12 Nov 11 2005 53-noownergroup file",
			{
				_T("53-noownergroup file"),
				12,
				R(_T("-r--r--r--")),
				R(),
				0,
				O(),
				CDateTime(2005, 11, 11)
			},
			DEFAULT
		});

	m_entries.push_back((t_entry){
			// Valid UTF-8 encoding
			"drwxr-xr-x   5 root     sys          512 2005\xEB\x85\x84  1\xEC\x9B\x94  6\xEC\x9D\xBC 54-asian date year first dir",
			{
				_T("54-asian date year first dir"),
				512,
				R(_T("drwxr-xr-x")),
				R(_T("root sys")),
				CDirentry::flag_dir,
				O(),
				CDateTime(2005, 1, 6)
			},
			DEFAULT
		});

	/* IBM AS/400 style listing with localized date*/
	m_entries.push_back((t_entry){
			"QPGMR           36864 18.09.06 14:21:26 *FILE      55-AS400.FILE",
			{
				_T("55-AS400.FILE"),
				36864,
				R(),
				R(_T("QPGMR")),
				0,
				O(),
				CDateTime(2006, 9, 18, 14, 21, 26)
			},
			DEFAULT
		});

	/* VMS style listing with complex size */
	m_entries.push_back((t_entry){
			"56-VMS-complex-size;1 2KB 23-SEP-2005 14:57:07.27",
			{
				_T("56-VMS-complex-size;1"),
				2048,
				R(),
				R(),
				0,
				O(),
				CDateTime(2005, 9, 23, 14, 57, 7)
			},
			DEFAULT
		});

	/* HP NonStop */
	m_entries.push_back((t_entry){
			"57-HP_NonStop 101 528 6-Apr-07 14:21:18 255, 0 \"oooo\"",
			{
				_T("57-HP_NonStop"),
				528,
				R(_T("\"oooo\"")),
				R(_T("255, 0")),
				0,
				O(),
				CDateTime(2007, 4, 6, 14, 21, 18)
			},
			HPNONSTOP
		});

	// Only difference is in the owner/group field, no delimiting space.
	m_entries.push_back((t_entry){
			"58-HP_NonStop 101 528 6-Apr-07 14:21:18 255,255 \"oooo\"",
			{
				_T("58-HP_NonStop"),
				528,
				R(_T("\"oooo\"")),
				R(_T("255,255")),
				0,
				O(),
				CDateTime(2007, 4, 6, 14, 21, 18)
			},
			HPNONSTOP
		});


	m_entries.push_back((t_entry){
			"drwxr-xr-x 6 user sys 1024 30. Jan., 12:40 59-localized-date-dir",
			{
				_T("59-localized-date-dir"),
				1024,
				R(_T("drwxr-xr-x")),
				R(_T("user sys")),
				CDirentry::flag_dir,
				O(),
				CDateTime(calcYear(1, 30), 1, 30, 12, 40)
			},
			DEFAULT
		});

	// MVS variants
	//
	// Note: I am not quite sure of these get parsed correctly, but so far
	//       nobody did complain. Formats added here with what I think
	//       is at least somewhat correct, so that there won't be any
	//       regressions at least.

	// The following 5 are loosely based on this format:
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.push_back((t_entry){
			"WYOSPT 3420   2003/05/21  1  200  FB      80  8053  PS  60-MVS.FILE",
			{
				_T("60-MVS.FILE"),
				100,
				R(),
				R(),
				0,
				O(),
				CDateTime(2003, 5, 21)
		},
		DEFAULT
	});

	m_entries.push_back((t_entry){
			"WPTA01 3290   2004/03/04  1    3  FB      80  3125  PO  61-MVS.DATASET",
			{
				_T("61-MVS.DATASET"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2004, 3, 04)
		},
		DEFAULT
	});

	m_entries.push_back((t_entry){
			"NRP004 3390   **NONE**    1   15  NONE     0     0  PO  62-MVS-NONEDATE.DATASET",
			{
				_T("62-MVS-NONEDATE.DATASET"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime()
		},
		DEFAULT
	});

	m_entries.push_back((t_entry){
			"TSO005 3390   2005/06/06 213000 U 0 27998 PO 63-MVS.DATASET",
			{
				_T("63-MVS.DATASET"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2005, 6, 6)
		},
		DEFAULT
	});

	m_entries.push_back((t_entry){
			"TSO004 3390   VSAM 64-mvs-file",
			{
				_T("64-mvs-file"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
		},
		DEFAULT
	});

	// MVS Dataset members
	//
	// As common with IBM misdesign, multiple styles exist.

	// Speciality: Some members have no attributes at all.
	// Requires servertype to be MVS or it won't be parsed, as
	// it would conflict with lots of other servers.
	m_entries.push_back((t_entry){
			"65-MVS-PDS-MEMBER",
			{
				_T("65-MVS-PDS-MEMBER"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
		},
		MVS
	});

	// Name         VV.MM   Created      Changed       Size  Init  Mod Id
	m_entries.push_back((t_entry){
			"66-MVSPDSMEMBER 01.01 2004/06/22 2004/06/22 16:32   128   128    0 BOBY12",
			{
				_T("66-MVSPDSMEMBER"),
				128,
				R(),
				R(),
				0,
				O(),
				CDateTime(2004, 6, 22, 16, 32)
		},
		MVS
	});

	// Hexadecimal size
	m_entries.push_back((t_entry){
			"67-MVSPDSMEMBER2 00B308 000411  00 FO                31    ANY",
			{
				_T("67-MVSPDSMEMBER2"),
				45832,
				R(),
				R(),
				0,
				O(),
				CDateTime()
		},
		MVS
	});

	m_entries.push_back((t_entry){
			"68-MVSPDSMEMBER3 00B308 000411  00 FO        RU      ANY    24",
			{
				_T("68-MVSPDSMEMBER3"),
				45832,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			MVS
		});

	// Migrated MVS file
	m_entries.push_back((t_entry){
			"Migrated				69-SOME.FILE",
			{
				_T("69-SOME.FILE"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			MVS
		});

	// z/VM, another IBM abomination. Description by Alexandre Charbey
	// Requires type set to ZVM or it cannot be parsed.
	//
	// 70-ZVMFILE
	//   is a filename
	// TRACE
	//   is a filetype (extension, like exe or com or jpg...)
	// V
	//   is the file format. Designates how records are arranged in a file. F=Fixed and V=Variable. I don't think you care
	// 65
	//   is the logical record length.
	// 107
	//   is Number of records in a file.
	// 2
	//   (seems wrong) is the block size ( iirc 1 is 127, 2 is 381, 3 is 1028 and 4 is 4072 - not sure - the numbers are not the usual binary numbers)
	// there is the date/time
	// 060191
	//   I think it is some internal stuff saying who the file belongs to.  191 is the "handle" of the user's disk. I don't know what 060 is. This 060191 is what FZ shows in its file list.
	m_entries.push_back((t_entry){
			"70-ZVMFILE  TRACE   V        65      107        2 2005-10-04 15:28:42 060191",
			{
				_T("70-ZVMFILE.TRACE"),
				6955,
				R(),
				R(_T("060191")),
				0,
				O(),
				CDateTime(2005, 10, 4, 15, 28, 42)
			},
			ZVM
		});

	m_entries.push_back((t_entry){
			"drwxr-xr-x 3 slopri devlab 512 71-unix-dateless",
			{
				_T("71-unix-dateless"),
				512,
				R(_T("drwxr-xr-x")),
				R(_T("slopri devlab")),
				CDirentry::flag_dir,
				O(),
				CDateTime()
			},
			DEFAULT
		});

	utc = wxDateTime(5, wxDateTime::Nov, 2008, 16, 52, 15);
	utc.MakeFromTimezone(wxDateTime::UTC);
	m_entries.push_back((t_entry){
			"Type=file;mOdIfY=20081105165215;size=1234; 72-MLSD-file",
			{
				_T("72-MLSD-file"),
				1234,
				R(),
				R(),
				0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

	// Yet another MVS format.
	// Follows the below structure but with all but the first two and the last field empty.
	// Furthermore, Unit is "Tape"
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.push_back((t_entry) {
			"V43525 Tape                                             73-MSV-TAPE.FILE",
			{
				_T("73-MSV-TAPE.FILE"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			MVS
		});

	m_entries.push_back((t_entry){
			"Type=file; 74-MLSD-whitespace trailing\t ",
			{
				_T("74-MLSD-whitespace trailing\t "),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			DEFAULT
		});

		m_entries.push_back((t_entry){
			"Type=file; \t 75-MLSD-whitespace leading",
			{
				_T("\t 75-MLSD-whitespace leading"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			DEFAULT
		});

		utc = wxDateTime(26, wxDateTime::Apr, 2008, 13, 55, 01);
		utc.MakeFromTimezone(wxDateTime::UTC);
		m_entries.push_back((t_entry){
			"modify=20080426135501;perm=;size=65718921;type=file;unique=802U1066013B;UNIX.group=1179;UNIX.mode=00;UNIX.owner=1179; 75 MLSD file with empty permissions",
			{
				_T("75 MLSD file with empty permissions"),
				65718921,
				R(_T("00")),
				R(_T("1179 1179")),
				0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

		m_entries.push_back((t_entry){
			"type=OS.unix=slink:/foo; 76 MLSD symlink",
			{
				_T("76 MLSD symlink"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(_T("/foo")),
				CDateTime()
			},
			DEFAULT
		});

		m_entries.push_back((t_entry){
			"type=OS.UNIX=symlink; 76b MLSD symlink",
			{
				_T("76b MLSD symlink"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(),
				CDateTime()
			},
			DEFAULT
		});

		// Old ietf draft for MLST earlier than mlst-07 has no trailing semicolon after facts
		m_entries.push_back((t_entry){
			"type=file 77 MLSD file no trailing semicolon after facts < mlst-07",
			{
				_T("77 MLSD file no trailing semicolon after facts < mlst-07"),
				-1,
				R(),
				R(),
				0,
				O(),
				CDateTime()
			},
			DEFAULT
		});

		m_entries.push_back((t_entry){
			"type=OS.unix=slink; 77 MLSD symlink notarget",
			{
				_T("77 MLSD symlink notarget"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir | CDirentry::flag_link,
				O(),
				CDateTime()
			},
			DEFAULT
		});

		utc = wxDateTime(22, wxDateTime::Jul, 2009, 9, 25, 10);
		utc.MakeFromTimezone(wxDateTime::UTC);
		m_entries.push_back((t_entry){
			"size=1365694195;type=file;modify=20090722092510;\tadsl TV 2009-07-22 08-25-10 78 mlsd file that can get parsed as unix.file",
			{
				_T("adsl TV 2009-07-22 08-25-10 78 mlsd file that can get parsed as unix.file"),
				1365694195,
				R(),
				R(),
				0,
				O(),
				CDateTime(utc, CDateTime::seconds)
			},
			DEFAULT
		});

	// MVS entry with a large number of used blocks:
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.push_back((t_entry){
			"WYOSPT 3420   2003/05/21  1 ????  FB      80  8053  PS  79-MVS.FILE",
			{
				_T("79-MVS.FILE"),
				100,
				R(),
				R(),
				0,
				O(),
				CDateTime(2003, 5, 21)
		},
		DEFAULT
	});

	// MVS entry with a large number of used blocks:
	// https://forum.filezilla-project.org/viewtopic.php?t=21667
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.push_back((t_entry){
			"GISBWI 3390   2011/08/25  2 ++++  FB     904 18080  PS  80-MVS.FILE",
			{
				_T("80-MVS.FILE"),
				100,
				R(),
				R(),
				0,
				O(),
				CDateTime(2011, 8, 25)
		},
		DEFAULT
	});

	// MVS entry with PO-E Dsorg indicating direrctory. See
	// https://forum.filezilla-project.org/viewtopic.php?t=19374 for reference.
	// Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname
	m_entries.push_back((t_entry){
			"WYOSPT 3420   2003/05/21  1 3 U 6447    6447  PO-E 81-MVS.DIR",
			{
				_T("81-MVS.DIR"),
				-1,
				R(),
				R(),
				CDirentry::flag_dir,
				O(),
				CDateTime(2003, 5, 21)
		},
		DEFAULT
	});

	m_entries.push_back((t_entry){
			"drwxrwxrwx   1 0        0               0 29 Jul 02:27 2014 Invoices",
			{
				_T("2014 Invoices"),
				0,
				R(_T("drwxrwxrwx")),
				R(_T("0 0")),
				CDirentry::flag_dir,
				O(),
				CDateTime(calcYear(7, 29), 7, 29, 2, 27)
		},
		DEFAULT
	});

/*
	wxString name;
	wxLongLong size;
	wxString permissions;
	wxString ownerGroup;
	int flags;
	wxString target; // Set to linktarget it link is true

	wxDateTime time;
*/

	// Fix line endings
	for (auto iter = m_entries.begin(); iter != m_entries.end(); iter++)
		iter->data += "\r\n";
}

void CDirectoryListingParserTest::testIndividual()
{
	m_sync.Enter();

	static int index = 0;
	const t_entry &entry = m_entries[index++];

	m_sync.Leave();

	CServer server;
	server.SetType(entry.serverType);

	CDirectoryListingParser parser(0, server);

	const char* str = entry.data.c_str();
	const int len = strlen(str);
	char* data = new char[len];
	memcpy(data, str, len);
	parser.AddData(data, len);

	CDirectoryListing listing = parser.Parse(CServerPath());

	wxString msg = wxString::Format(_T("Data: %s, count: %d"), wxString(entry.data.c_str(), wxConvUTF8).c_str(), listing.GetCount());
	msg.Replace(_T("\r"), _T(""));
	msg.Replace(_T("\n"), _T(""));
	wxWX2MBbuf mb_buf = msg.mb_str(wxConvUTF8);
	CPPUNIT_ASSERT_MESSAGE((const char*)mb_buf, listing.GetCount() == 1);

	msg = wxString::Format(_T("Data: %s  Expected:\n%s\n  Got:\n%s"), wxString(entry.data.c_str(), wxConvUTF8).c_str(), entry.reference.dump().c_str(), listing[0].dump().c_str());
	mb_buf = msg.mb_str(wxConvUTF8);
	CPPUNIT_ASSERT_MESSAGE((const char*)mb_buf, listing[0] == entry.reference);
}

void CDirectoryListingParserTest::testAll()
{
	CServer server;
	CDirectoryListingParser parser(0, server);
	for (std::vector<t_entry>::const_iterator iter = m_entries.begin(); iter != m_entries.end(); iter++)
	{
		server.SetType(iter->serverType);
		parser.SetServer(server);
		const char* str = iter->data.c_str();
		const int len = strlen(str);
		char* data = new char[len];
		memcpy(data, str, len);
		parser.AddData(data, len);
	}
	CDirectoryListing listing = parser.Parse(CServerPath());

	CPPUNIT_ASSERT(listing.GetCount() == m_entries.size());

	unsigned int i = 0;
	for (std::vector<t_entry>::const_iterator iter = m_entries.begin(); iter != m_entries.end(); iter++, i++)
	{
		wxString msg = wxString::Format(_T("Data: %s  Expected:\n%s\n  Got:\n%s"), wxString(iter->data.c_str(), wxConvUTF8).c_str(), iter->reference.dump().c_str(), listing[i].dump().c_str());

		CPPUNIT_ASSERT_MESSAGE((const char*)msg.mb_str(wxConvUTF8), listing[i] == iter->reference);
	}
}

void CDirectoryListingParserTest::setUp()
{
}
