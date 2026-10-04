#pragma once
// core/log — canale de log per sistem, cu trace id per actiune.
//
// Utilizare:
//   auto& log = core::log::Get("DAILY");
//   const core::log::Ctx ctx{ core::log::NewTrace(), pid };
//   log.Info(ctx, "claim day={} result={}", day, "OK");
//
// Linia rezultata in systems.log:
//   [2026-10-03 14:02:11.123] [DAILY     ] [info] t=00a3f1 pid=1 claim day=3 result=OK

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <spdlog/spdlog.h>

namespace core::log
{
	enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Off };

	// Identifica o singura actiune a unui jucator prin toate sistemele.
	struct TraceId { uint32_t value = 0; };

	// Genereaza un trace id nou, unic in procesul curent.
	TraceId NewTrace();

	// Contextul unei linii de log: actiunea si jucatorul (0 = fara jucator).
	struct Ctx
	{
		TraceId trace{};
		uint32_t pid = 0;
	};

	class Channel
	{
	public:
		Channel(std::string name, std::shared_ptr<spdlog::logger> logger);

		const std::string& Name() const { return m_name; }

		void SetLevel(Level level) { m_level = level; }
		Level GetLevel() const { return m_level; }
		bool Enabled(Level level) const { return level != Level::Off && level >= m_level; }

		template <typename... Args>
		void Log(Level level, const Ctx& ctx, spdlog::format_string_t<Args...> fmt, Args&&... args)
		{
			if (!Enabled(level))
				return;
			Write(level, ctx, spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...));
		}

		template <typename... Args>
		void Trace(const Ctx& c, spdlog::format_string_t<Args...> f, Args&&... a) { Log(Level::Trace, c, f, std::forward<Args>(a)...); }
		template <typename... Args>
		void Debug(const Ctx& c, spdlog::format_string_t<Args...> f, Args&&... a) { Log(Level::Debug, c, f, std::forward<Args>(a)...); }
		template <typename... Args>
		void Info(const Ctx& c, spdlog::format_string_t<Args...> f, Args&&... a) { Log(Level::Info, c, f, std::forward<Args>(a)...); }
		template <typename... Args>
		void Warn(const Ctx& c, spdlog::format_string_t<Args...> f, Args&&... a) { Log(Level::Warn, c, f, std::forward<Args>(a)...); }
		// Erorile ajung si in syserr, daca acel logger exista in proces.
		template <typename... Args>
		void Error(const Ctx& c, spdlog::format_string_t<Args...> f, Args&&... a) { Log(Level::Error, c, f, std::forward<Args>(a)...); }

	private:
		void Write(Level level, const Ctx& ctx, std::string_view message);

		std::string m_name;
		std::shared_ptr<spdlog::logger> m_logger;
		Level m_level;
	};

	// Canalul cu numele dat; creat la prima cerere, aceeasi instanta dupa aceea.
	// Numele se scriu cu majuscule, unul per sistem: "DAILY", "REWARD", "EVENTS".
	Channel& Get(std::string_view name);

	// "trace" | "debug" | "info" | "warn" | "error" | "off"; altceva -> fallback.
	Level ParseLevel(std::string_view text, Level fallback);

	// Doar pentru teste: destinatia folosita de canalele create de acum inainte.
	// nullptr revine la fisierul implicit systems.log.
	void SetSinkForTests(spdlog::sink_ptr sink);
}
