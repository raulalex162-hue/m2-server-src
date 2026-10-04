#pragma once
// core/scheduler — sarcini periodice si zilnice, pe ora serverului.
//
//   auto& s = core::scheduler::Global();
//   s.Every("heartbeat", 60, [](const core::log::Ctx& ctx) { ... });          // la fiecare 60 s
//   s.DailyAt("daily_reward", 0, [](const core::log::Ctx& ctx) { ... });      // zilnic la 00:00
//
// Garantii si limite:
//   - Tick(now) e apelat de game o data pe secunda; sarcinile ruleaza pe firul principal;
//   - o sarcina care arunca o exceptie e logata si nu le opreste pe celelalte;
//   - sarcinile ratate cat serverul a fost oprit NU se recupereaza: DailyAt ruleaza o singura data
//     cand observa o zi noua. Ce nu are voie sa rateze o zi foloseste core::time::DayKey, nu DailyAt;
//   - un sistem oprit isi pierde automat sarcinile (registry-ul apeleaza CancelOwner);
//   - fiecare core are propriul planificator: o sarcina DailyAt ruleaza pe fiecare core.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/log/Log.h"
#include "core/time/Calendar.h"

namespace core::scheduler
{
	using TaskId = uint64_t;
	using Task = std::function<void(const log::Ctx& ctx)>;
	using time::Seconds;

	class Scheduler
	{
	public:
		// Prima rulare la now + interval, apoi la fiecare interval (minim 1 s).
		TaskId Every(std::string owner, Seconds interval, Task task, Seconds now = time::Now());

		// Ruleaza cand incepe o zi noua de joc (DayKey cu resetMinute se schimba). Nu ruleaza la inregistrare.
		TaskId DailyAt(std::string owner, int resetMinute, Task task, Seconds now = time::Now());

		void Cancel(TaskId id);
		void CancelOwner(std::string_view owner);

		void Tick(Seconds now);

		size_t Count() const;
		size_t OwnerCount(std::string_view owner) const;

	private:
		enum class Kind { Every, Daily };

		struct Entry
		{
			TaskId id;
			std::string owner;
			Kind kind;
			Seconds interval = 0;  // Every
			Seconds nextRun = 0;   // Every
			int resetMinute = 0;   // Daily
			int64_t lastDay = 0;   // Daily
			Task task;
			bool active = true;
		};

		static log::Channel& Log() { return log::Get("SCHEDULER"); }
		void Run(Entry& entry, const char* what);
		void Compact();

		std::vector<Entry> m_entries;
		TaskId m_nextId = 0;
		int m_depth = 0;
	};

	Scheduler& Global();
}
