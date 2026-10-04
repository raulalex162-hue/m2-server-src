// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "GameTime.h"

#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "../config.h"
#include "../event.h"

#include "core/scheduler/Scheduler.h"

namespace game::gametime
{
	namespace
	{
		struct GameTimeConfig
		{
			int dayResetMinute = 0;
		};

		int g_resetMinute = 0;
		bool g_running = false;

		std::string LocalTime(core::time::Seconds t)
		{
			std::time_t tt = static_cast<std::time_t>(t);
			std::tm tm{};
			localtime_r(&tt, &tm);
			char buf[32];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
			return buf;
		}

		std::string FormatMinute(int minute)
		{
			char buf[8];
			std::snprintf(buf, sizeof(buf), "%02d:%02d", minute / 60, minute % 60);
			return buf;
		}

		EVENTINFO(GameTimeTickInfo)
		{
		};

		EVENTFUNC(game_time_tick_event)
		{
			if (!g_running)
				return 0;
			core::scheduler::Global().Tick(core::time::Now());
			return PASSES_PER_SEC(1);
		}

		class GameTimeSystem : public core::registry::System<GameTimeConfig>
		{
		public:
			std::string_view Name() const override { return "game_time"; }

			void Describe(std::vector<std::string>& lines) const override
			{
				const auto now = core::time::Now();
				lines.push_back("acum: " + LocalTime(now));
				lines.push_back("zi de joc: " + core::time::FormatDayKey(core::time::DayKey(now, g_resetMinute))
					+ " (reset la " + FormatMinute(g_resetMinute) + ")");
				lines.push_back("urmatorul reset: " + LocalTime(core::time::NextReset(now, g_resetMinute)));
				lines.push_back("sarcini programate pe acest core: " + std::to_string(core::scheduler::Global().Count()));
			}

		protected:
			void Read(core::config::Reader& root, GameTimeConfig& out) override
			{
				const std::string reset = root.OptionalString("day_reset", "00:00");
				out.dayResetMinute = core::time::ParseMinuteOfDay(reset);
				if (out.dayResetMinute < 0)
				{
					root.Fail("day_reset", "\"" + reset + "\" nu e o ora valida (format HH:MM, ex. \"00:00\")");
					out.dayResetMinute = 0;
				}
				root.RejectUnknown({ "enabled", "log_level", "day_reset" });
			}

			void OnStart() override
			{
				g_resetMinute = Config().dayResetMinute;
				g_running = true;
				m_tickEvent = event_create(game_time_tick_event, AllocEventInfo<GameTimeTickInfo>(), PASSES_PER_SEC(1));

				const auto now = core::time::Now();
				const char* tz = std::getenv("TZ");
				Log().Info(LifecycleCtx(), "pornit: acum {} | zi de joc {} | reset la {} | TZ={}", LocalTime(now),
					core::time::FormatDayKey(core::time::DayKey(now, g_resetMinute)), FormatMinute(g_resetMinute),
					tz ? tz : "(sistem)");
			}

			void OnStop() override
			{
				g_running = false;
				event_cancel(&m_tickEvent);
				Log().Info(LifecycleCtx(), "oprit");
			}

			void OnConfigReloaded() override
			{
				if (Config().dayResetMinute != g_resetMinute)
				{
					Log().Warn(LifecycleCtx(), "day_reset schimbat {} -> {}: DayKeyNow() foloseste ora noua imediat, "
						"dar sarcinile DailyAt deja programate raman pe ora veche pana la restart",
						FormatMinute(g_resetMinute), FormatMinute(Config().dayResetMinute));
					g_resetMinute = Config().dayResetMinute;
				}
			}

		private:
			LPEVENT m_tickEvent;
		};
	}

	int DayResetMinute() { return g_resetMinute; }
	int64_t DayKeyNow() { return core::time::DayKey(core::time::Now(), g_resetMinute); }
	int64_t WeekKeyNow() { return core::time::WeekKey(core::time::Now(), g_resetMinute); }
	core::time::Seconds NextResetNow() { return core::time::NextReset(core::time::Now(), g_resetMinute); }

	std::unique_ptr<core::registry::ISystem> CreateSystem()
	{
		return std::make_unique<GameTimeSystem>();
	}
}
