#include "core/scheduler/Scheduler.h"

#include <algorithm>
#include <exception>

namespace core::scheduler
{
	TaskId Scheduler::Every(std::string owner, Seconds interval, Task task, Seconds now)
	{
		Entry e;
		e.id = ++m_nextId;
		e.owner = std::move(owner);
		e.kind = Kind::Every;
		e.interval = interval < 1 ? 1 : interval;
		e.nextRun = now + e.interval;
		e.task = std::move(task);
		m_entries.push_back(std::move(e));
		return m_nextId;
	}

	TaskId Scheduler::DailyAt(std::string owner, int resetMinute, Task task, Seconds now)
	{
		Entry e;
		e.id = ++m_nextId;
		e.owner = std::move(owner);
		e.kind = Kind::Daily;
		e.resetMinute = resetMinute;
		e.lastDay = time::DayKey(now, resetMinute);
		e.task = std::move(task);
		m_entries.push_back(std::move(e));
		return m_nextId;
	}

	void Scheduler::Cancel(TaskId id)
	{
		for (auto& e : m_entries)
			if (e.id == id)
				e.active = false;
		if (m_depth == 0)
			Compact();
	}

	void Scheduler::CancelOwner(std::string_view owner)
	{
		for (auto& e : m_entries)
			if (e.owner == owner)
				e.active = false;
		if (m_depth == 0)
			Compact();
	}

	void Scheduler::Run(Entry& entry, const char* what)
	{
		// Copii locale: lista se poate realoca daca sarcina programeaza alte sarcini.
		const Task task = entry.task;
		const std::string owner = entry.owner;
		const log::Ctx ctx{ log::NewTrace(), 0 };
		Log().Trace(ctx, "run {} {}", owner, what);
		try
		{
			task(ctx);
		}
		catch (const std::exception& ex)
		{
			Log().Error(ctx, "{}: sarcina {} a aruncat o exceptie: {}", owner, what, ex.what());
		}
		catch (...)
		{
			Log().Error(ctx, "{}: sarcina {} a aruncat o exceptie necunoscuta", owner, what);
		}
	}

	void Scheduler::Tick(Seconds now)
	{
		++m_depth;
		const size_t count = m_entries.size(); // sarcinile adaugate acum ruleaza de la urmatorul Tick
		for (size_t i = 0; i < count; ++i)
		{
			if (!m_entries[i].active)
				continue;

			if (m_entries[i].kind == Kind::Every)
			{
				if (now < m_entries[i].nextRun)
					continue;
				// Fara "recuperare": dupa o pauza lunga ruleaza o singura data, apoi revine la ritm.
				m_entries[i].nextRun = now + m_entries[i].interval;
				Run(m_entries[i], "every");
			}
			else
			{
				const int64_t day = time::DayKey(now, m_entries[i].resetMinute);
				if (day == m_entries[i].lastDay)
					continue;
				m_entries[i].lastDay = day;
				Run(m_entries[i], "daily");
			}
		}
		--m_depth;
		if (m_depth == 0)
			Compact();
	}

	void Scheduler::Compact()
	{
		m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return !e.active; }), m_entries.end());
	}

	size_t Scheduler::Count() const
	{
		return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& e) { return e.active; }));
	}

	size_t Scheduler::OwnerCount(std::string_view owner) const
	{
		return static_cast<size_t>(std::count_if(m_entries.begin(), m_entries.end(),
			[owner](const Entry& e) { return e.active && e.owner == owner; }));
	}

	Scheduler& Global()
	{
		static Scheduler scheduler;
		return scheduler;
	}
}
