#include <doctest.h>

#include <set>
#include <string>
#include <vector>

#include "core/config/Config.h"
#include "core/reward/Reward.h"

using core::reward::Deliver;
using core::reward::Item;
using core::reward::Mailbox;
using core::reward::Pending;
using core::reward::Reward;

namespace
{
	// Un inventar fals: un numar de locuri libere si yang cu limita.
	struct FakeInventory : core::reward::IReceiver
	{
		int freeSlots = 0;
		uint64_t gold = 0;
		uint64_t goldLimit = core::reward::kMaxGold;
		std::vector<Item> received;

		bool CanReceiveItem(const Item&) override { return freeSlots > 0; }
		void GiveItem(const Item& item) override { --freeSlots; received.push_back(item); }
		uint64_t GoldCapacity() override { return gold >= goldLimit ? 0 : goldLimit - gold; }
		void GiveGold(uint64_t amount) override { gold += amount; }
	};

	Reward Make(std::vector<Item> items, uint64_t gold = 0)
	{
		Reward r;
		r.items = std::move(items);
		r.gold = gold;
		return r;
	}
}

TEST_CASE("reward: cu loc suficient, totul ajunge la jucator")
{
	FakeInventory inv;
	inv.freeSlots = 5;
	const auto d = Deliver(inv, Make({ { 27002, 50 }, { 27005, 20 } }, 1000));
	CHECK(d.remaining.Empty());
	CHECK(d.delivered.items.size() == 2);
	CHECK(inv.gold == 1000);
	CHECK(inv.received.size() == 2);
}

TEST_CASE("reward: ce nu incape ramane, in ordine, iar yang-ul se da separat")
{
	FakeInventory inv;
	inv.freeSlots = 1;
	const auto d = Deliver(inv, Make({ { 1, 1 }, { 2, 1 }, { 3, 1 } }, 500));
	REQUIRE(d.delivered.items.size() == 1);
	CHECK(d.delivered.items[0].vnum == 1);
	REQUIRE(d.remaining.items.size() == 2);
	CHECK(d.remaining.items[0].vnum == 2);
	CHECK(d.remaining.items[1].vnum == 3);
	CHECK(d.delivered.gold == 500);
	CHECK(d.remaining.gold == 0);
}

TEST_CASE("reward: yang-ul se da pana la limita, iar diferenta ramane in asteptare")
{
	FakeInventory inv;
	inv.freeSlots = 5;
	inv.gold = core::reward::kMaxGold - 1000;
	const auto d = Deliver(inv, Make({}, 5000));
	CHECK(inv.gold == core::reward::kMaxGold);
	CHECK(d.delivered.gold == 1000);
	CHECK(d.remaining.gold == 4000);
}

TEST_CASE("reward: la limita exacta, tot yang-ul ramane in asteptare")
{
	FakeInventory inv;
	inv.gold = core::reward::kMaxGold;
	const auto d = Deliver(inv, Make({}, 5000));
	CHECK(d.delivered.gold == 0);
	CHECK(d.remaining.gold == 5000);
}

TEST_CASE("mailbox: cutia da tot ce incape, din toate intrarile, si pastreaza exact restul")
{
	Mailbox box(10);
	box.Add(Pending{ "a", Make({ { 1, 1 }, { 2, 1 } }, 3000), 1 });
	box.Add(Pending{ "b", Make({ { 3, 1 } }, 2000), 2 });

	FakeInventory inv;
	inv.freeSlots = 2;
	inv.gold = core::reward::kMaxGold - 4000;
	const Reward got = box.DeliverAll(inv);

	CHECK(got.items.size() == 2);       // itemele 1 si 2 au incaput, 3 nu
	CHECK(got.gold == 4000);            // 3000 din "a" + 1000 din "b"
	REQUIRE(box.Size() == 1);           // "a" a iesit complet din cutie
	CHECK(box.Entries()[0].source == "b");
	REQUIRE(box.Entries()[0].reward.items.size() == 1);
	CHECK(box.Entries()[0].reward.items[0].vnum == 3);
	CHECK(box.Entries()[0].reward.gold == 1000);
}

TEST_CASE("mailbox: livrarea partiala pastreaza restul, cea completa scoate intrarea")
{
	Mailbox box(10);
	REQUIRE(box.Add(Pending{ "a", Make({ { 1, 1 }, { 2, 1 } }), 1 }));
	REQUIRE(box.Add(Pending{ "b", Make({ { 3, 1 } }, 100), 2 }));

	FakeInventory inv;
	inv.freeSlots = 1;
	const Reward first = box.DeliverAll(inv);
	CHECK(first.items.size() == 1);
	CHECK(first.gold == 100); // yang-ul din "b" nu cere loc in inventar
	REQUIRE(box.Size() == 2);
	CHECK(box.Entries()[0].reward.items.size() == 1);
	CHECK(box.Entries()[0].reward.items[0].vnum == 2);
	CHECK(box.Entries()[1].reward.items.size() == 1);
	CHECK(box.Entries()[1].reward.gold == 0);

	inv.freeSlots = 5;
	const Reward second = box.DeliverAll(inv);
	CHECK(second.items.size() == 2);
	CHECK(box.Size() == 0);
}

TEST_CASE("mailbox: limita e respectata, iar o recompensa goala nu ocupa loc")
{
	Mailbox box(2);
	CHECK(box.Add(Pending{ "x", Make({ { 1, 1 } }), 0 }));
	CHECK(box.Add(Pending{ "x", Make({ { 2, 1 } }), 0 }));
	CHECK(box.Full());
	CHECK_FALSE(box.Add(Pending{ "x", Make({ { 3, 1 } }), 0 }));
	CHECK(box.Add(Pending{ "x", Reward{}, 0 }));
	CHECK(box.Size() == 2);
}

TEST_CASE("reward: ReadReward citeste itemele si yang-ul, cu count implicit 1")
{
	core::config::Report report;
	auto json = core::config::Parse(R"({ "items": [ { "vnum": 27002, "count": 50 }, { "vnum": 70001 } ], "gold": 1000 })", report);
	REQUIRE(json);
	core::config::Reader reader(*json, report);
	const Reward r = core::reward::ReadReward(reader);
	CHECK(report.Ok());
	REQUIRE(r.items.size() == 2);
	CHECK((r.items[0] == Item{ 27002, 50 }));
	CHECK((r.items[1] == Item{ 70001, 1 }));
	CHECK(r.gold == 1000);
}

TEST_CASE("reward: ReadReward refuza cantitati gresite, campuri necunoscute si recompense goale")
{
	core::config::Report report;
	auto json = core::config::Parse(R"({ "items": [ { "vnum": 1, "count": 0 }, { "vnum": 2, "count": 201, "cnt": 1 } ] })", report);
	REQUIRE(json);
	core::config::Reader reader(*json, report);
	core::reward::ReadReward(reader);

	std::set<std::string> paths;
	for (const auto& issue : report.Issues())
		paths.insert(issue.path);
	CHECK(paths.count("items[0].count") == 1);
	CHECK(paths.count("items[1].count") == 1);
	CHECK(paths.count("items[1].cnt") == 1);

	core::config::Report empty;
	auto json2 = core::config::Parse(R"({ "gold": 0 })", empty);
	REQUIRE(json2);
	core::config::Reader reader2(*json2, empty);
	core::reward::ReadReward(reader2);
	CHECK_FALSE(empty.Ok());
}

TEST_CASE("reward: validatorul de vnum e folosit si motivul ajunge in raport")
{
	core::config::Report report;
	auto json = core::config::Parse(R"({ "items": [ { "vnum": 999999 } ] })", report);
	REQUIRE(json);
	core::config::Reader reader(*json, report);
	core::reward::ReadReward(reader, [](uint32_t vnum, std::string& why) {
		if (vnum == 999999) { why = "nu exista in item_proto"; return false; }
		return true;
	});
	REQUIRE(report.Issues().size() == 1);
	CHECK(report.Issues()[0].path == "items[0].vnum");
	CHECK(report.Issues()[0].message.find("item_proto") != std::string::npos);
}
