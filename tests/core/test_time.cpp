#include <doctest.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/registry/Registry.h"
#include "core/scheduler/Scheduler.h"
#include "core/time/Calendar.h"

using core::time::DayKey;
using core::time::FormatDayKey;
using core::time::NextReset;
using core::time::WeekKey;

namespace
{
	// Seteaza fusul orar pe durata unui test si il readuce la final.
	struct TimeZone
	{
		std::string previous;
		bool hadPrevious = false;

		explicit TimeZone(const char* tz)
		{
			if (const char* old = std::getenv("TZ")) { previous = old; hadPrevious = true; }
			setenv("TZ", tz, 1);
			tzset();
		}
		~TimeZone()
		{
			if (hadPrevious) setenv("TZ", previous.c_str(), 1); else unsetenv("TZ");
			tzset();
		}
	};

	constexpr int64_t kDay = 86400;
}

TEST_CASE("time: in UTC, ziua de joc se schimba la ora resetului")
{
	TimeZone tz("UTC");
	CHECK(DayKey(0, 0) == 0);
	CHECK(DayKey(kDay - 1, 0) == 0);
	CHECK(DayKey(kDay, 0) == 1);

	// reset la 06:00: 1970-01-02 05:59 e inca ziua 0
	CHECK(DayKey(kDay + 6 * 3600 - 60, 360) == 0);
	CHECK(DayKey(kDay + 6 * 3600, 360) == 1);
}

TEST_CASE("time: in ora Romaniei, 00:30 e deja ziua noua, 23:59 e ziua precedenta")
{
	TimeZone tz("Europe/Bucharest");
	CHECK(DayKey(1791063000, 0) == 20730); // 2026-10-04 00:30
	CHECK(FormatDayKey(DayKey(1791063000, 0)) == "2026-10-04");
	CHECK(FormatDayKey(DayKey(1791061140, 0)) == "2026-10-03"); // 2026-10-03 23:59
}

TEST_CASE("time: urmatorul reset tine cont de trecerea la ora de vara")
{
	TimeZone tz("Europe/Bucharest");
	CHECK(NextReset(1774692000, 0) == 1774735200); // din 28 martie 12:00 -> 29 martie 00:00
	CHECK(NextReset(1774774800, 0) == 1774818000); // din 29 martie 12:00 -> 30 martie 00:00
	CHECK(1774818000 - 1774735200 == 23 * 3600);   // 29 martie are doar 23 de ore
}

TEST_CASE("time: saptamana de joc incepe luni")
{
	TimeZone tz("Europe/Bucharest");
	const int64_t sunday = 1791147540;  // duminica 2026-10-04 23:59
	const int64_t monday = 1791147600;  // luni 2026-10-05 00:00
	const int64_t nextSunday = 1791752340;
	const int64_t nextMonday = 1791752400;
	CHECK(WeekKey(sunday, 0) + 1 == WeekKey(monday, 0));
	CHECK(WeekKey(monday, 0) == WeekKey(nextSunday, 0));
	CHECK(WeekKey(nextSunday, 0) + 1 == WeekKey(nextMonday, 0));
}

TEST_CASE("time: ParseMinuteOfDay accepta doar HH:MM valid")
{
	using core::time::ParseMinuteOfDay;
	CHECK(ParseMinuteOfDay("00:00") == 0);
	CHECK(ParseMinuteOfDay("06:30") == 390);
	CHECK(ParseMinuteOfDay("23:59") == 1439);
	CHECK(ParseMinuteOfDay("24:00") == -1);
	CHECK(ParseMinuteOfDay("6:30") == -1);
	CHECK(ParseMinuteOfDay("06-30") == -1);
	CHECK(ParseMinuteOfDay("") == -1);
}

TEST_CASE("scheduler: Every ruleaza la interval si nu recupereaza dupa o pauza lunga")
{
	core::scheduler::Scheduler s;
	int runs = 0;
	s.Every("t", 10, [&](const core::log::Ctx&) { ++runs; }, 1000);

	s.Tick(1005);
	CHECK(runs == 0);
	s.Tick(1010);
	CHECK(runs == 1);
	s.Tick(1015);
	CHECK(runs == 1);
	s.Tick(1020);
	CHECK(runs == 2);

	s.Tick(5000); // pauza lunga: o singura rulare, nu 398
	CHECK(runs == 3);
	s.Tick(5005);
	CHECK(runs == 3);
	s.Tick(5010);
	CHECK(runs == 4);
}

TEST_CASE("scheduler: DailyAt ruleaza o data pe zi, nu la inregistrare, si o singura data dupa mai multe zile ratate")
{
	TimeZone tz("UTC");
	core::scheduler::Scheduler s;
	int runs = 0;
	s.DailyAt("t", 0, [&](const core::log::Ctx&) { ++runs; }, 10 * kDay + 100);

	s.Tick(10 * kDay + 200);
	CHECK(runs == 0);
	s.Tick(11 * kDay);
	CHECK(runs == 1);
	s.Tick(11 * kDay + 5000);
	CHECK(runs == 1);
	s.Tick(15 * kDay); // serverul "a dormit" 4 zile
	CHECK(runs == 2);
}

TEST_CASE("scheduler: anularea dupa id si dupa sistem")
{
	core::scheduler::Scheduler s;
	int a = 0, b = 0;
	const auto idA = s.Every("a", 1, [&](const core::log::Ctx&) { ++a; }, 0);
	s.Every("b", 1, [&](const core::log::Ctx&) { ++b; }, 0);
	s.Every("b", 1, [&](const core::log::Ctx&) { ++b; }, 0);
	CHECK(s.Count() == 3);
	CHECK(s.OwnerCount("b") == 2);

	s.Cancel(idA);
	s.CancelOwner("b");
	s.Tick(10);
	CHECK(a == 0);
	CHECK(b == 0);
	CHECK(s.Count() == 0);
}

TEST_CASE("scheduler: o exceptie nu le opreste pe celelalte; o sarcina se poate anula singura")
{
	core::scheduler::Scheduler s;
	int ok = 0, once = 0;
	s.Every("stricat", 1, [](const core::log::Ctx&) { throw std::runtime_error("bug de test"); }, 0);
	core::scheduler::TaskId self = 0;
	self = s.Every("once", 1, [&](const core::log::Ctx&) { ++once; s.Cancel(self); }, 0);
	s.Every("bun", 1, [&](const core::log::Ctx&) { ++ok; }, 0);

	CHECK_NOTHROW(s.Tick(1));
	CHECK_NOTHROW(s.Tick(2));
	CHECK(ok == 2);
	CHECK(once == 1);
	CHECK(s.Count() == 2);
}

TEST_CASE("scheduler: o sarcina adaugata in timpul unui Tick ruleaza abia la urmatorul")
{
	core::scheduler::Scheduler s;
	int late = 0;
	bool added = false;
	s.Every("first", 1, [&](const core::log::Ctx&) {
		if (!added)
		{
			added = true;
			for (int i = 0; i < 16; ++i) // destule cat lista sa se realoce
				s.Every("late", 1, [&](const core::log::Ctx&) { ++late; }, 0);
		}
	}, 0);

	s.Tick(1);
	CHECK(late == 0);
	s.Tick(2);
	CHECK(late == 16);
}

namespace
{
	struct NoConfig {};

	class TimedSystem : public core::registry::System<NoConfig>
	{
	public:
		std::string_view Name() const override { return "timed"; }

	protected:
		void Read(core::config::Reader& root, NoConfig&) override { root.RejectUnknown({ "enabled", "log_level" }); }
		void OnStart() override
		{
			core::scheduler::Global().Every("timed", 60, [](const core::log::Ctx&) {});
		}
	};
}

TEST_CASE("scheduler: registry-ul anuleaza automat sarcinile unui sistem oprit")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "m2core_scheduler_tests";
	fs::create_directories(dir);
	std::ofstream(dir / "timed.json") << R"({ "enabled": true })";

	core::registry::Registry registry([dir](std::string_view name) { return (dir / (std::string(name) + ".json")).string(); });
	registry.Register(std::make_unique<TimedSystem>());
	registry.StartAll();
	CHECK(core::scheduler::Global().OwnerCount("timed") == 1);

	registry.StopAll();
	CHECK(core::scheduler::Global().OwnerCount("timed") == 0);
	fs::remove_all(dir);
}
