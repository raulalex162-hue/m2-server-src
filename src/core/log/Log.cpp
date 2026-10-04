#include "core/log/Log.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>

#include <spdlog/sinks/basic_file_sink.h>

namespace core::log
{
	namespace
	{
		std::mutex g_mutex;
		std::unordered_map<std::string, std::unique_ptr<Channel>> g_channels;
		spdlog::sink_ptr g_sink;

		uint32_t InitialTrace()
		{
			// Pornim de la o valoare aleatoare, ca doua core-uri sa nu dea aceleasi id-uri.
			std::random_device rd;
			return rd() & 0x00FFFFFFu;
		}

		std::atomic<uint32_t> g_nextTrace{ InitialTrace() };

		spdlog::sink_ptr SharedSink()
		{
			if (!g_sink)
			{
				// Toate canalele scriu in acelasi fisier, ca o actiune sa poata fi
				// urmarita prin mai multe sisteme dupa trace id.
				g_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("systems.log", false);
				g_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-10n] [%l] %v");
			}
			return g_sink;
		}

		spdlog::level::level_enum ToSpd(Level level)
		{
			switch (level)
			{
				case Level::Trace: return spdlog::level::trace;
				case Level::Debug: return spdlog::level::debug;
				case Level::Info:  return spdlog::level::info;
				case Level::Warn:  return spdlog::level::warn;
				case Level::Error: return spdlog::level::err;
				default:           return spdlog::level::off;
			}
		}

		Level DefaultLevel()
		{
#ifdef _DEBUG
			return Level::Debug;
#else
			return Level::Info;
#endif
		}
	}

	TraceId NewTrace()
	{
		uint32_t value = g_nextTrace.fetch_add(1, std::memory_order_relaxed) & 0x00FFFFFFu;
		if (value == 0) // 0 inseamna "fara trace"
			value = g_nextTrace.fetch_add(1, std::memory_order_relaxed) & 0x00FFFFFFu;
		return TraceId{ value };
	}

	Channel::Channel(std::string name, std::shared_ptr<spdlog::logger> logger)
		: m_name(std::move(name)), m_logger(std::move(logger)), m_level(DefaultLevel())
	{
	}

	void Channel::Write(Level level, const Ctx& ctx, std::string_view message)
	{
		m_logger->log(ToSpd(level), "t={:06x} pid={} {}", ctx.trace.value, ctx.pid, message);

		if (level == Level::Error)
		{
			if (auto syserr = spdlog::get("syserr"))
				syserr->error("[{}] t={:06x} pid={} {}", m_name, ctx.trace.value, ctx.pid, message);
		}
	}

	Channel& Get(std::string_view name)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		const std::string key(name);
		if (auto it = g_channels.find(key); it != g_channels.end())
			return *it->second;

		auto logger = std::make_shared<spdlog::logger>(key, SharedSink());
		logger->set_level(spdlog::level::trace); // filtrarea o face canalul
		logger->flush_on(spdlog::level::warn);

		// Inregistrat ca sa fie prins de spdlog::flush_every din log_init().
		if (!spdlog::get(key))
			spdlog::register_logger(logger);

		auto& channel = g_channels[key];
		channel = std::make_unique<Channel>(key, std::move(logger));
		return *channel;
	}

	Level ParseLevel(std::string_view text, Level fallback)
	{
		if (text == "trace") return Level::Trace;
		if (text == "debug") return Level::Debug;
		if (text == "info")  return Level::Info;
		if (text == "warn")  return Level::Warn;
		if (text == "error") return Level::Error;
		if (text == "off")   return Level::Off;
		return fallback;
	}

	void SetSinkForTests(spdlog::sink_ptr sink)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_sink = std::move(sink);
	}
}
