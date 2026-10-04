#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/registry/Registry.h"

namespace fs = std::filesystem;
using core::registry::Registry;
using core::registry::State;

namespace
{
	// Folder temporar cu config-uri de test, sters la final.
	struct ConfigDir
	{
		fs::path dir = fs::temp_directory_path() / "m2core_registry_tests";

		ConfigDir() { fs::remove_all(dir); fs::create_directories(dir); }
		~ConfigDir() { fs::remove_all(dir); }

		void Write(const std::string& system, const std::string& json) const
		{
			std::ofstream(dir / (system + ".json")) << json;
		}

		std::function<std::string(std::string_view)> PathFor() const
		{
			const fs::path base = dir;
			return [base](std::string_view name) { return (base / (std::string(name) + ".json")).string(); };
		}
	};

	struct DemoConfig
	{
		int value = 0;
	};

	// Ce s-a intamplat cu un sistem de test, vazut din afara lui.
	struct Counters
	{
		int starts = 0;
		int stops = 0;
		int reloads = 0;
		int lastValue = 0;
	};

	class DemoSystem : public core::registry::System<DemoConfig>
	{
	public:
		DemoSystem(std::string name, Counters& counters, std::vector<std::string>* order = nullptr)
			: m_name(std::move(name)), m_counters(counters), m_order(order) {}

		std::string_view Name() const override { return m_name; }

	protected:
		void Read(core::config::Reader& root, DemoConfig& out) override
		{
			out.value = static_cast<int>(root.RequireInt("value", 1, 10));
			root.RejectUnknown({ "enabled", "log_level", "value" });
		}
		void OnStart() override { ++m_counters.starts; m_counters.lastValue = Config().value; }
		void OnStop() override { ++m_counters.stops; if (m_order) m_order->push_back(m_name); }
		void OnConfigReloaded() override { ++m_counters.reloads; m_counters.lastValue = Config().value; }

	private:
		std::string m_name;
		Counters& m_counters;
		std::vector<std::string>* m_order;
	};
}

TEST_CASE("registry: config valid si activat -> sistemul porneste cu valorile din config")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 4 })");
	Counters c;
	Registry registry(dir.PathFor());
	REQUIRE(registry.Register(std::make_unique<DemoSystem>("demo", c)));

	registry.StartAll();
	CHECK(registry.Find("demo")->state == State::Running);
	CHECK(c.starts == 1);
	CHECK(c.lastValue == 4);
	registry.StopAll();
	CHECK(c.stops == 1);
}

TEST_CASE("registry: enabled false -> sistemul nu porneste")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": false, "value": 4 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));

	registry.StartAll();
	CHECK(registry.Find("demo")->state == State::Disabled);
	CHECK(c.starts == 0);
}

TEST_CASE("registry: config invalid -> sistemul nu porneste, eroarea spune campul")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 99 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));

	registry.StartAll();
	const auto* status = registry.Find("demo");
	CHECK(status->state == State::Failed);
	CHECK(status->lastError.find("value") != std::string::npos);
	CHECK(c.starts == 0);
}

TEST_CASE("registry: fisier lipsa -> Failed, celelalte sisteme pornesc normal")
{
	ConfigDir dir;
	dir.Write("good", R"({ "enabled": true, "value": 1 })");
	Counters missing, good;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("missing", missing));
	registry.Register(std::make_unique<DemoSystem>("good", good));

	registry.StartAll();
	CHECK(registry.Find("missing")->state == State::Failed);
	CHECK(registry.Find("good")->state == State::Running);
	CHECK(good.starts == 1);
	registry.StopAll();
}

TEST_CASE("registry: reload valid schimba config-ul fara restart")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 2 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));
	registry.StartAll();

	dir.Write("demo", R"({ "enabled": true, "value": 7 })");
	CHECK(registry.Reload("demo"));
	CHECK(c.starts == 1);
	CHECK(c.reloads == 1);
	CHECK(c.lastValue == 7);
	CHECK(registry.Find("demo")->state == State::Running);
	registry.StopAll();
}

TEST_CASE("registry: reload invalid pastreaza config-ul vechi si starea")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 2 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));
	registry.StartAll();

	dir.Write("demo", R"({ "enabled": true, "valeu": 7 })");
	std::string error;
	CHECK_FALSE(registry.Reload("demo", &error));
	CHECK(error.find("valeu") != std::string::npos);
	CHECK(registry.Find("demo")->state == State::Running);
	CHECK(c.reloads == 0);

	// config-ul vechi e inca cel activ
	dir.Write("demo", R"({ "enabled": true, "value": 2 })");
	CHECK(registry.Reload("demo"));
	CHECK(c.lastValue == 2);
	registry.StopAll();
}

TEST_CASE("registry: reload poate opri si reporni un sistem")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 3 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));
	registry.StartAll();

	dir.Write("demo", R"({ "enabled": false, "value": 3 })");
	CHECK(registry.Reload("demo"));
	CHECK(registry.Find("demo")->state == State::Disabled);
	CHECK(c.stops == 1);

	dir.Write("demo", R"({ "enabled": true, "value": 5 })");
	CHECK(registry.Reload("demo"));
	CHECK(registry.Find("demo")->state == State::Running);
	CHECK(c.starts == 2);
	CHECK(c.lastValue == 5);
	registry.StopAll();
}

TEST_CASE("registry: un sistem Failed porneste dupa ce config-ul e reparat")
{
	ConfigDir dir;
	dir.Write("demo", R"({ "enabled": true, "value": 0 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("demo", c));
	registry.StartAll();
	CHECK(registry.Find("demo")->state == State::Failed);

	dir.Write("demo", R"({ "enabled": true, "value": 1 })");
	CHECK(registry.Reload("demo"));
	CHECK(registry.Find("demo")->state == State::Running);
	CHECK(registry.Find("demo")->lastError.empty());
	registry.StopAll();
}

TEST_CASE("registry: StopAll opreste in ordinea inversa pornirii")
{
	ConfigDir dir;
	dir.Write("first", R"({ "enabled": true, "value": 1 })");
	dir.Write("second", R"({ "enabled": true, "value": 1 })");
	Counters a, b;
	std::vector<std::string> order;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("first", a, &order));
	registry.Register(std::make_unique<DemoSystem>("second", b, &order));

	registry.StartAll();
	registry.StopAll();
	REQUIRE(order.size() == 2);
	CHECK(order[0] == "second");
	CHECK(order[1] == "first");
}

TEST_CASE("registry: nume invalide si duplicate sunt refuzate")
{
	Counters c;
	Registry registry;
	CHECK_FALSE(registry.Register(std::make_unique<DemoSystem>("Daily", c)));
	CHECK_FALSE(registry.Register(std::make_unique<DemoSystem>("daily-reward", c)));
	CHECK_FALSE(registry.Register(std::make_unique<DemoSystem>("", c)));
	CHECK(registry.Register(std::make_unique<DemoSystem>("daily_reward", c)));
	CHECK_FALSE(registry.Register(std::make_unique<DemoSystem>("daily_reward", c)));
	CHECK(registry.List().size() == 1);
}

TEST_CASE("registry: log_level din config se aplica pe canalul sistemului")
{
	ConfigDir dir;
	dir.Write("leveldemo", R"({ "enabled": true, "log_level": "debug", "value": 1 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("leveldemo", c));

	registry.StartAll();
	CHECK(core::log::Get("LEVELDEMO").GetLevel() == core::log::Level::Debug);
	CHECK(registry.Find("leveldemo")->logLevel == core::log::Level::Debug);
	registry.StopAll();
}

TEST_CASE("registry: reload pe un nume necunoscut intoarce false")
{
	Registry registry;
	std::string error;
	CHECK_FALSE(registry.Reload("nu_exista", &error));
	CHECK(error.find("necunoscut") != std::string::npos);
}

namespace
{
	// Sistem care isi raporteaza starea si contextul primit la pornire.
	class DescribedSystem : public core::registry::System<DemoConfig>
	{
	public:
		std::string_view Name() const override { return "described"; }
		void Describe(std::vector<std::string>& lines) const override
		{
			lines.push_back("value=" + std::to_string(Config().value));
			lines.push_back("start_trace=" + std::to_string(m_startTrace));
		}

	protected:
		void Read(core::config::Reader& root, DemoConfig& out) override
		{
			out.value = static_cast<int>(root.RequireInt("value", 1, 10));
		}
		void OnStart() override { m_startTrace = LifecycleCtx().trace.value; }

	private:
		uint32_t m_startTrace = 0;
	};
}

TEST_CASE("registry: OnStart primeste trace id-ul registry-ului, Describe raporteaza starea")
{
	ConfigDir dir;
	dir.Write("described", R"({ "enabled": true, "value": 6 })");
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DescribedSystem>());
	registry.StartAll();

	const auto lines = registry.Describe("described");
	REQUIRE(lines.size() == 2);
	CHECK(lines[0] == "value=6");
	CHECK(lines[1] != "start_trace=0");
	CHECK(registry.Describe("nu_exista").empty());
	registry.StopAll();
}

TEST_CASE("registry: SetLogLevel schimba nivelul canalului pana la urmatorul reload")
{
	ConfigDir dir;
	dir.Write("leveltwo", R"({ "enabled": true, "log_level": "info", "value": 1 })");
	Counters c;
	Registry registry(dir.PathFor());
	registry.Register(std::make_unique<DemoSystem>("leveltwo", c));
	registry.StartAll();

	CHECK(registry.SetLogLevel("leveltwo", core::log::Level::Debug));
	CHECK(core::log::Get("LEVELTWO").GetLevel() == core::log::Level::Debug);
	CHECK(registry.Find("leveltwo")->logLevel == core::log::Level::Debug);
	CHECK_FALSE(registry.SetLogLevel("nu_exista", core::log::Level::Debug));

	CHECK(registry.Reload("leveltwo"));
	CHECK(core::log::Get("LEVELTWO").GetLevel() == core::log::Level::Info);
	registry.StopAll();
}
