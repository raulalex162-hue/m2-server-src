#include "core/time/Calendar.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace core::time
{
	namespace
	{
		// Zile de la 1970-01-01 pentru o data calendaristica (algoritmul lui Howard Hinnant).
		int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
		{
			y -= m <= 2;
			const int64_t era = (y >= 0 ? y : y - 399) / 400;
			const unsigned yoe = static_cast<unsigned>(y - era * 400);
			const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + static_cast<int64_t>(doe) - 719468;
		}

		void CivilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d)
		{
			z += 719468;
			const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
			const unsigned doe = static_cast<unsigned>(z - era * 146097);
			const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
			y = static_cast<int64_t>(yoe) + era * 400;
			const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
			const unsigned mp = (5 * doy + 2) / 153;
			d = doy - (153 * mp + 2) / 5 + 1;
			m = mp + (mp < 10 ? 3 : -9);
			y += m <= 2;
		}

		std::tm Local(Seconds t)
		{
			std::time_t tt = static_cast<std::time_t>(t);
			std::tm out{};
#ifdef _WIN32
			localtime_s(&out, &tt);
#else
			localtime_r(&tt, &out);
#endif
			return out;
		}

		int64_t FloorDiv(int64_t a, int64_t b)
		{
			int64_t q = a / b;
			if ((a % b != 0) && ((a < 0) != (b < 0)))
				--q;
			return q;
		}
	}

	Seconds Now()
	{
		using namespace std::chrono;
		return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
	}

	int64_t DayKey(Seconds unixTime, int resetMinute)
	{
		const std::tm tm = Local(unixTime);
		int64_t day = DaysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1), static_cast<unsigned>(tm.tm_mday));
		if (tm.tm_hour * 60 + tm.tm_min < resetMinute)
			--day; // inainte de reset inca e ziua de joc precedenta
		return day;
	}

	int64_t WeekKey(Seconds unixTime, int resetMinute)
	{
		// 1970-01-01 a fost joi; +3 aliniaza saptamanile la luni.
		return FloorDiv(DayKey(unixTime, resetMinute) + 3, 7);
	}

	Seconds NextReset(Seconds unixTime, int resetMinute)
	{
		int64_t y;
		unsigned m, d;
		CivilFromDays(DayKey(unixTime, resetMinute) + 1, y, m, d);

		std::tm tm{};
		tm.tm_year = static_cast<int>(y - 1900);
		tm.tm_mon = static_cast<int>(m - 1);
		tm.tm_mday = static_cast<int>(d);
		tm.tm_hour = resetMinute / 60;
		tm.tm_min = resetMinute % 60;
		tm.tm_isdst = -1; // lasa sistemul sa decida ora de vara
		return static_cast<Seconds>(std::mktime(&tm));
	}

	std::string FormatDayKey(int64_t dayKey)
	{
		int64_t y;
		unsigned m, d;
		CivilFromDays(dayKey, y, m, d);
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u", static_cast<long long>(y), m, d);
		return buf;
	}

	int ParseMinuteOfDay(const std::string& text)
	{
		if (text.size() != 5 || text[2] != ':')
			return -1;
		for (size_t i : { 0, 1, 3, 4 })
			if (text[i] < '0' || text[i] > '9')
				return -1;
		const int h = (text[0] - '0') * 10 + (text[1] - '0');
		const int mi = (text[3] - '0') * 10 + (text[4] - '0');
		if (h > 23 || mi > 59)
			return -1;
		return h * 60 + mi;
	}
}
