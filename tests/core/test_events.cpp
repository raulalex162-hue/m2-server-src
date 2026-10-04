#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"
#include "core/registry/Registry.h"

using core::events::Bus;
using core::events::EnterGame;
using core::events::LeaveGame;
using core::events::MobKill;
using core::log::Ctx;

TEST_CASE("events: abonatul primeste evenimentul cu datele si contextul lui")
{
	Bus bus;
	MobKill received{};
	Ctx receivedCtx{};
	bus.Subscribe<MobKill>("test", [&](const MobKill& e, const Ctx& ctx) { received = e; receivedCtx = ctx; });

	MobKill kill;
	kill.killerPid = 7;
	kill.mobVnum = 101;
	kill.mobLevel = 15;
	bus.Publish(kill, Ctx{ core::log::TraceId{ 0x42 }, 7 });

	CHECK(received.killerPid == 7);
	CHECK(received.mobVnum == 101);
	CHECK(received.mobLevel == 15);
	CHECK(receivedCtx.trace.value == 0x42);
	CHECK(receivedCtx.pid == 7);
}

TEST_CASE("events: abonatii sunt apelati in ordinea abonarii")
{
	Bus bus;
	std::vector<std::string> order;
	bus.Subscribe<EnterGame>("a", [&](const EnterGame&, const Ctx&) { order.push_back("a"); });
	bus.Subscribe<EnterGame>("b", [&](const EnterGame&, const Ctx&) { order.push_back("b"); });
	bus.Subscribe<EnterGame>("c", [&](const EnterGame&, const Ctx&) { order.push_back("c"); });

	bus.Publish(EnterGame{}, Ctx{});
	REQUIRE(order.size() == 3);
	CHECK(order[0] == "a");
	CHECK(order[1] == "b");
	CHECK(order[2] == "c");
}

TEST_CASE("events: publicare fara abonati nu face nimic")
{
	Bus bus;
	bus.Publish(LeaveGame{}, Ctx{});
	CHECK(bus.SubscriberCount<LeaveGame>() == 0);
}

TEST_CASE("events: tipurile de evenimente sunt separate")
{
	Bus bus;
	int enters = 0, kills = 0;
	bus.Subscribe<EnterGame>("t", [&](const EnterGame&, const Ctx&) { ++enters; });
	bus.Subscribe<MobKill>("t", [&](const MobKill&, const Ctx&) { ++kills; });

	bus.Publish(MobKill{}, Ctx{});
	bus.Publish(MobKill{}, Ctx{});
	CHECK(enters == 0);
	CHECK(kills == 2);
}

TEST_CASE("events: dezabonarea dupa id si dupa sistem")
{
	Bus bus;
	int a = 0, b = 0;
	const auto idA = bus.Subscribe<EnterGame>("sys_a", [&](const EnterGame&, const Ctx&) { ++a; });
	bus.Subscribe<EnterGame>("sys_b", [&](const EnterGame&, const Ctx&) { ++b; });
	bus.Subscribe<MobKill>("sys_b", [&](const MobKill&, const Ctx&) { ++b; });

	bus.Unsubscribe(idA);
	bus.Publish(EnterGame{}, Ctx{});
	CHECK(a == 0);
	CHECK(b == 1);

	CHECK(bus.OwnerCount("sys_b") == 2);
	bus.UnsubscribeOwner("sys_b");
	CHECK(bus.OwnerCount("sys_b") == 0);
	bus.Publish(EnterGame{}, Ctx{});
	bus.Publish(MobKill{}, Ctx{});
	CHECK(b == 1);
}

TEST_CASE("events: o exceptie intr-un abonat nu ii opreste pe ceilalti")
{
	Bus bus;
	int after = 0;
	bus.Subscribe<MobKill>("stricat", [](const MobKill&, const Ctx&) { throw std::runtime_error("bug de test"); });
	bus.Subscribe<MobKill>("bun", [&](const MobKill&, const Ctx&) { ++after; });

	CHECK_NOTHROW(bus.Publish(MobKill{}, Ctx{}));
	CHECK(after == 1);
}

TEST_CASE("events: un abonat se poate dezabona in timpul publicarii")
{
	Bus bus;
	int once = 0, other = 0;
	core::events::SubscriptionId id = 0;
	id = bus.Subscribe<EnterGame>("once", [&](const EnterGame&, const Ctx&) { ++once; bus.Unsubscribe(id); });
	bus.Subscribe<EnterGame>("other", [&](const EnterGame&, const Ctx&) { ++other; });

	bus.Publish(EnterGame{}, Ctx{});
	bus.Publish(EnterGame{}, Ctx{});
	CHECK(once == 1);
	CHECK(other == 2);
	CHECK(bus.SubscriberCount<EnterGame>() == 1);
}

TEST_CASE("events: un abonat nou adaugat in timpul publicarii primeste doar evenimentele urmatoare")
{
	Bus bus;
	int late = 0;
	bool added = false;
	bus.Subscribe<EnterGame>("first", [&](const EnterGame&, const Ctx&) {
		if (!added)
		{
			added = true;
			// mai multe abonari, ca lista sa fie fortata sa se realoce
			for (int i = 0; i < 16; ++i)
				bus.Subscribe<EnterGame>("late", [&](const EnterGame&, const Ctx&) { ++late; });
			bus.Subscribe<MobKill>("late", [](const MobKill&, const Ctx&) {});
		}
	});

	bus.Publish(EnterGame{}, Ctx{});
	CHECK(late == 0);
	bus.Publish(EnterGame{}, Ctx{});
	CHECK(late == 16);
}

namespace
{
	struct EmptyConfig {};

	// Sistem care se aboneaza la pornire si "uita" sa se dezaboneze la oprire.
	class ForgetfulSystem : public core::registry::System<EmptyConfig>
	{
	public:
		std::string_view Name() const override { return "forgetful"; }

	protected:
		void Read(core::config::Reader& root, EmptyConfig&) override
		{
			root.RejectUnknown({ "enabled", "log_level" });
		}
		void OnStart() override
		{
			core::events::Global().Subscribe<MobKill>("forgetful", [](const MobKill&, const Ctx&) {});
		}
	};
}

TEST_CASE("events: registry-ul dezaboneaza automat un sistem oprit")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "m2core_events_tests";
	fs::create_directories(dir);
	std::ofstream(dir / "forgetful.json") << R"({ "enabled": true })";

	core::registry::Registry registry([dir](std::string_view name) { return (dir / (std::string(name) + ".json")).string(); });
	registry.Register(std::make_unique<ForgetfulSystem>());
	registry.StartAll();
	CHECK(core::events::Global().OwnerCount("forgetful") == 1);

	registry.StopAll();
	CHECK(core::events::Global().OwnerCount("forgetful") == 0);
	fs::remove_all(dir);
}
