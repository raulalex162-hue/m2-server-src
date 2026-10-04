#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/net/Router.h"
#include "core/registry/Registry.h"

using core::net::Body;
using core::net::EncodePayload;
using core::net::Result;
using core::net::Router;

namespace
{
	std::vector<uint8_t> Bytes(std::initializer_list<uint8_t> list) { return std::vector<uint8_t>(list); }
}

TEST_CASE("net: EncodePayload pune system si type little-endian inaintea body-ului")
{
	const auto body = Bytes({ 0xAA, 0xBB });
	const auto payload = EncodePayload(0x0102, 0x0304, body);
	REQUIRE(payload.size() == 6);
	CHECK(payload[0] == 0x02);
	CHECK(payload[1] == 0x01);
	CHECK(payload[2] == 0x04);
	CHECK(payload[3] == 0x03);
	CHECK(payload[4] == 0xAA);
	CHECK(payload[5] == 0xBB);
}

TEST_CASE("net: mesajul ajunge la handler-ul lui cu pid-ul si body-ul corect")
{
	Router router;
	uint32_t gotPid = 0;
	std::vector<uint8_t> gotBody;
	REQUIRE(router.Register(1, 2, "demo", [&](uint32_t pid, Body body, const core::log::Ctx& ctx) {
		gotPid = pid;
		gotBody.assign(body.begin(), body.end());
		CHECK(ctx.pid == pid);
		CHECK(ctx.trace.value != 0);
	}));

	const auto payload = EncodePayload(1, 2, Bytes({ 7, 8, 9 }));
	CHECK(router.Dispatch(42, payload) == Result::Ok);
	CHECK(gotPid == 42);
	CHECK(gotBody == Bytes({ 7, 8, 9 }));
}

TEST_CASE("net: body gol e permis")
{
	Router router;
	bool called = false;
	router.Register(1, 1, "demo", [&](uint32_t, Body body, const core::log::Ctx&) { called = true; CHECK(body.empty()); });
	CHECK(router.Dispatch(1, EncodePayload(1, 1, {})) == Result::Ok);
	CHECK(called);
}

TEST_CASE("net: mesajele invalide nu ajung la niciun handler")
{
	Router router;
	int calls = 0;
	router.Register(1, 1, "demo", [&](uint32_t, Body, const core::log::Ctx&) { ++calls; }, 8);

	CHECK(router.Dispatch(1, Bytes({ 1, 0, 1 })) == Result::TooShort);
	CHECK(router.Dispatch(1, Bytes({})) == Result::TooShort);
	CHECK(router.Dispatch(1, EncodePayload(1, 2, {})) == Result::UnknownMessage);
	CHECK(router.Dispatch(1, EncodePayload(9, 1, {})) == Result::UnknownMessage);
	CHECK(router.Dispatch(1, EncodePayload(1, 1, std::vector<uint8_t>(9, 0))) == Result::TooLarge);
	CHECK(calls == 0);

	CHECK(router.Dispatch(1, EncodePayload(1, 1, std::vector<uint8_t>(8, 0))) == Result::Ok);
	CHECK(calls == 1);
}

TEST_CASE("net: aceeasi pereche (system, type) nu poate fi inregistrata de doua ori; 0 e refuzat")
{
	Router router;
	auto noop = [](uint32_t, Body, const core::log::Ctx&) {};
	CHECK(router.Register(3, 1, "a", noop));
	CHECK_FALSE(router.Register(3, 1, "b", noop));
	CHECK_FALSE(router.Register(0, 1, "c", noop));
	CHECK_FALSE(router.Register(3, 0, "c", noop));
	CHECK(router.Register(3, 2, "b", noop));
	CHECK(router.Count() == 2);
}

TEST_CASE("net: o exceptie in handler e prinsa si raportata")
{
	Router router;
	router.Register(1, 1, "stricat", [](uint32_t, Body, const core::log::Ctx&) { throw std::runtime_error("bug de test"); });
	CHECK(router.Dispatch(1, EncodePayload(1, 1, {})) == Result::HandlerError);
}

TEST_CASE("net: UnregisterOwner scoate doar rutele sistemului respectiv")
{
	Router router;
	auto noop = [](uint32_t, Body, const core::log::Ctx&) {};
	router.Register(1, 1, "a", noop);
	router.Register(1, 2, "a", noop);
	router.Register(2, 1, "b", noop);

	router.UnregisterOwner("a");
	CHECK_FALSE(router.Has(1, 1));
	CHECK_FALSE(router.Has(1, 2));
	CHECK(router.Has(2, 1));
}

TEST_CASE("net: un handler se poate dezinregistra in timp ce ruleaza")
{
	Router router;
	int calls = 0;
	router.Register(1, 1, "once", [&](uint32_t, Body, const core::log::Ctx&) { ++calls; router.UnregisterOwner("once"); });
	CHECK(router.Dispatch(1, EncodePayload(1, 1, {})) == Result::Ok);
	CHECK(router.Dispatch(1, EncodePayload(1, 1, {})) == Result::UnknownMessage);
	CHECK(calls == 1);
}

namespace
{
	struct NoConfig {};

	class NetSystem : public core::registry::System<NoConfig>
	{
	public:
		std::string_view Name() const override { return "netdemo"; }

	protected:
		void Read(core::config::Reader& root, NoConfig&) override { root.RejectUnknown({ "enabled", "log_level" }); }
		void OnStart() override
		{
			core::net::Global().Register(77, 1, "netdemo", [](uint32_t, Body, const core::log::Ctx&) {});
		}
	};
}

TEST_CASE("net: registry-ul scoate automat rutele unui sistem oprit")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "m2core_net_tests";
	fs::create_directories(dir);
	std::ofstream(dir / "netdemo.json") << R"({ "enabled": true })";

	core::registry::Registry registry([dir](std::string_view name) { return (dir / (std::string(name) + ".json")).string(); });
	registry.Register(std::make_unique<NetSystem>());
	registry.StartAll();
	CHECK(core::net::Global().Has(77, 1));

	registry.StopAll();
	CHECK_FALSE(core::net::Global().Has(77, 1));
	fs::remove_all(dir);
}

TEST_CASE("net: OwnerCount numara mesajele unui sistem")
{
	Router router;
	auto noop = [](uint32_t, Body, const core::log::Ctx&) {};
	router.Register(5, 1, "a", noop);
	router.Register(5, 2, "a", noop);
	router.Register(6, 1, "b", noop);
	CHECK(router.OwnerCount("a") == 2);
	CHECK(router.OwnerCount("b") == 1);
	CHECK(router.OwnerCount("c") == 0);
}
