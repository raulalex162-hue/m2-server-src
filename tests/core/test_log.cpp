#include <doctest.h>

#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>

#include "core/log/Log.h"

namespace
{
	// Trimite log-urile intr-un string, ca testul sa poata citi ce s-a scris.
	struct CapturedLog
	{
		std::ostringstream out;
		std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);

		CapturedLog()
		{
			sink->set_pattern("[%n] [%l] %v");
			core::log::SetSinkForTests(sink);
		}

		std::string Text() { sink->flush(); return out.str(); }
	};
}

TEST_CASE("log: linia contine canalul, trace id-ul, jucatorul si mesajul")
{
	CapturedLog captured;
	auto& channel = core::log::Get("TEST_FORMAT");
	channel.SetLevel(core::log::Level::Info);

	const core::log::Ctx ctx{ core::log::TraceId{ 0x00ab12 }, 7 };
	channel.Info(ctx, "claim day={} result={}", 3, "OK");

	const std::string text = captured.Text();
	CHECK(text.find("[TEST_FORMAT]") != std::string::npos);
	CHECK(text.find("[info]") != std::string::npos);
	CHECK(text.find("t=00ab12") != std::string::npos);
	CHECK(text.find("pid=7") != std::string::npos);
	CHECK(text.find("claim day=3 result=OK") != std::string::npos);
}

TEST_CASE("log: mesajele sub nivelul canalului nu se scriu")
{
	CapturedLog captured;
	auto& channel = core::log::Get("TEST_LEVEL");
	channel.SetLevel(core::log::Level::Warn);

	channel.Debug({}, "nu trebuie sa apara");
	channel.Info({}, "nici asta");
	channel.Warn({}, "asta da");

	const std::string text = captured.Text();
	CHECK(text.find("nu trebuie sa apara") == std::string::npos);
	CHECK(text.find("nici asta") == std::string::npos);
	CHECK(text.find("asta da") != std::string::npos);
}

TEST_CASE("log: nivelul off opreste tot canalul")
{
	CapturedLog captured;
	auto& channel = core::log::Get("TEST_OFF");
	channel.SetLevel(core::log::Level::Off);

	channel.Error({}, "oprit");

	CHECK(captured.Text().find("oprit") == std::string::npos);
}

TEST_CASE("log: acelasi nume intoarce acelasi canal")
{
	CapturedLog captured;
	auto& first = core::log::Get("TEST_SAME");
	auto& second = core::log::Get("TEST_SAME");
	CHECK(&first == &second);

	first.SetLevel(core::log::Level::Error);
	CHECK(second.GetLevel() == core::log::Level::Error);
}

TEST_CASE("log: trace id-urile sunt diferite si niciodata 0")
{
	const auto a = core::log::NewTrace();
	const auto b = core::log::NewTrace();
	CHECK(a.value != b.value);
	CHECK(a.value != 0);
	CHECK(b.value != 0);
}

TEST_CASE("log: ParseLevel intelege toate nivelurile si foloseste fallback-ul")
{
	using core::log::Level;
	CHECK(core::log::ParseLevel("trace", Level::Info) == Level::Trace);
	CHECK(core::log::ParseLevel("debug", Level::Info) == Level::Debug);
	CHECK(core::log::ParseLevel("info", Level::Error) == Level::Info);
	CHECK(core::log::ParseLevel("warn", Level::Info) == Level::Warn);
	CHECK(core::log::ParseLevel("error", Level::Info) == Level::Error);
	CHECK(core::log::ParseLevel("off", Level::Info) == Level::Off);
	CHECK(core::log::ParseLevel("DEBUG", Level::Warn) == Level::Warn); // doar litere mici
	CHECK(core::log::ParseLevel("", Level::Warn) == Level::Warn);
}
