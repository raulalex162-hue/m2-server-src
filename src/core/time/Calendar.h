#pragma once
// core/time/Calendar — "ziua de joc" si "saptamana de joc", in ora locala a serverului.
//
// Un sistem zilnic (ex. daily reward) NU se bazeaza pe un eveniment de reset la miezul noptii:
// daca serverul e oprit atunci, evenimentul nu vine. In schimb, compara ziua salvata la jucator
// cu DayKey(acum). Functioneaza corect si dupa o noapte cu serverul oprit.
//
// Ora locala vine din fusul orar al sistemului (TZ), cu ora de vara inclusa.
// resetMinute = minutul din zi la care incepe ziua de joc (0 = 00:00, 360 = 06:00).

#include <cstdint>
#include <string>

namespace core::time
{
	using Seconds = int64_t; // secunde Unix

	Seconds Now();

	// Zile de la 1970-01-01 (in ora locala, cu ziua incepand la resetMinute).
	int64_t DayKey(Seconds unixTime, int resetMinute);

	// Saptamani de joc, cu saptamana incepand luni la resetMinute.
	int64_t WeekKey(Seconds unixTime, int resetMinute);

	// Momentul (secunde Unix) la care incepe urmatoarea zi de joc.
	Seconds NextReset(Seconds unixTime, int resetMinute);

	// "2026-10-04" pentru un DayKey, pentru log si /sysinfo.
	std::string FormatDayKey(int64_t dayKey);

	// "HH:MM" -> minutul din zi; -1 daca textul nu e valid.
	int ParseMinuteOfDay(const std::string& text);
}
