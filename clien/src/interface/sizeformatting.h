#ifndef __SIZEFORMATTING_H__
#define __SIZEFORMATTING_H__

#include <sizeformatting_base.h>

class CSizeFormat : public CSizeFormatBase
{
public:
	static wxString FormatNumber(int64_t size, bool* thousands_separator = 0);

	static wxString GetUnitWithBase(_unit unit, int base);
	static wxString GetUnit(_unit unit, enum _format = formats_count);
	static wxString FormatUnit(int64_t size, _unit unit, int base = 1024);

	static wxString Format(int64_t size, bool add_bytes_suffix, enum _format format, bool thousands_separator, int num_decimal_places);
	static wxString Format(int64_t size, bool add_bytes_suffix = false);
};

#endif //__SIZEFORMATTING_H__
