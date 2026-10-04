#include <doctest.h>

#include <chrono>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/storage/PlayerDataStore.h"
#include "core/storage/SystemDataCache.h"

using namespace std::chrono_literals;
using core::storage::Blob;
using core::storage::Clock;
using core::storage::PlayerDataStore;
using core::storage::Requester;
using core::storage::SystemDataCache;
using core::storage::SystemMap;

namespace
{
	// O "baza de date" lenta: cererile stau in coada pana le procesam explicit.
	struct FakeDb
	{
		std::map<std::pair<uint32_t, uint32_t>, Blob> rows;
		std::deque<uint32_t> pendingLoads;
		std::deque<std::tuple<uint32_t, uint32_t, Blob>> pendingWrites;

		// Valoarea unui rand ("" daca nu exista).
		Blob Row(uint32_t pid, uint32_t system) const
		{
			auto it = rows.find({ pid, system });
			return it == rows.end() ? Blob{} : it->second;
		}

		SystemMap Read(uint32_t pid) const
		{
			SystemMap out;
			for (const auto& [key, value] : rows)
				if (key.first == pid)
					out[key.second] = value;
			return out;
		}

		void ApplyWrites()
		{
			for (auto& [pid, system, data] : pendingWrites)
			{
				if (data.empty())
					rows.erase({ pid, system });
				else
					rows[{ pid, system }] = data;
			}
			pendingWrites.clear();
		}
	};

	struct Reply
	{
		Requester to;
		uint32_t pid;
		SystemMap data;
		bool ok;
	};

	// db: cache + baza de date falsa; raspunsurile se aduna intr-o lista.
	struct DbProcess
	{
		FakeDb db;
		std::vector<Reply> replies;
		SystemDataCache cache{ SystemDataCache::Callbacks{
			[this](uint32_t pid) { db.pendingLoads.push_back(pid); },
			[this](uint32_t pid, uint32_t system, const Blob& data) { db.pendingWrites.emplace_back(pid, system, data); },
			[this](Requester to, uint32_t pid, const SystemMap& data, bool ok) { replies.push_back({ to, pid, data, ok }); } } };

		void FinishLoads()
		{
			while (!db.pendingLoads.empty())
			{
				const uint32_t pid = db.pendingLoads.front();
				db.pendingLoads.pop_front();
				cache.OnLoadResult(pid, db.Read(pid), Clock::now());
			}
		}
	};

	// Un core de game legat de db: mesajele trec sincron, cum ar trece prin retea.
	struct GameCore
	{
		Requester id;
		DbProcess& dbp;
		std::vector<uint32_t> readyEvents;
		PlayerDataStore store{ PlayerDataStore::Callbacks{
			[this](uint32_t pid) { dbp.cache.OnLoadRequest(id, pid, Clock::now()); },
			[this](uint32_t pid, const PlayerDataStore::Changes& changes) {
				for (const auto& [system, data] : changes)
					dbp.cache.OnSave(pid, system, data, Clock::now());
			},
			[this](uint32_t pid) { readyEvents.push_back(pid); },
			nullptr } };

		GameCore(Requester requester, DbProcess& d) : id(requester), dbp(d) {}

		// Livreaza raspunsurile db care sunt pentru acest core.
		void Deliver()
		{
			auto& all = dbp.replies;
			for (auto it = all.begin(); it != all.end();)
			{
				if (it->to == id)
				{
					store.OnLoadResponse(it->pid, it->data, it->ok);
					it = all.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	};
}

TEST_CASE("storage: inainte de Ready, Get si Set sunt refuzate")
{
	DbProcess dbp;
	GameCore core(1, dbp);
	core.store.OnEnter(7);
	CHECK(core.store.GetStatus(7) == PlayerDataStore::Status::Waiting);
	CHECK_FALSE(core.store.Set(7, 1, "x"));
	CHECK(core.store.Get(7, 1) == nullptr);

	dbp.FinishLoads();
	core.Deliver();
	CHECK(core.store.IsReady(7));
	CHECK(core.store.Set(7, 1, "x"));
	REQUIRE(core.store.Get(7, 1) != nullptr);
	CHECK(*core.store.Get(7, 1) == "x");
	REQUIRE(core.readyEvents.size() == 1);
	CHECK(core.readyEvents[0] == 7);
}

TEST_CASE("storage: datele existente in DB ajung la sistem")
{
	DbProcess dbp;
	dbp.db.rows[{ 7, 1 }] = "progres vechi";
	dbp.db.rows[{ 7, 2 }] = "alt sistem";
	dbp.db.rows[{ 8, 1 }] = "alt jucator";
	GameCore core(1, dbp);

	core.store.OnEnter(7);
	dbp.FinishLoads();
	core.Deliver();
	REQUIRE(core.store.Get(7, 1) != nullptr);
	CHECK(*core.store.Get(7, 1) == "progres vechi");
	REQUIRE(core.store.Get(7, 2) != nullptr);
	CHECK(core.store.Get(7, 3) == nullptr);
}

TEST_CASE("storage: warp de pe core-ul A pe B, cu DB lenta, pastreaza ultimele date")
{
	DbProcess dbp;
	dbp.db.rows[{ 7, 1 }] = "ziua 1";
	GameCore a(1, dbp), b(2, dbp);

	a.store.OnEnter(7);
	dbp.FinishLoads();
	a.Deliver();
	REQUIRE(a.store.Set(7, 1, "ziua 2"));

	// warp: A salveaza si uita jucatorul; scrierea in DB inca NU s-a terminat
	a.store.OnLeave(7);
	CHECK(dbp.db.Row(7, 1) == "ziua 1");

	// B cere datele: le primeste din cache-ul db, nu din DB-ul inca vechi
	b.store.OnEnter(7);
	dbp.FinishLoads();
	b.Deliver();
	REQUIRE(b.store.Get(7, 1) != nullptr);
	CHECK(*b.store.Get(7, 1) == "ziua 2");

	dbp.db.ApplyWrites();
	CHECK(dbp.db.Row(7, 1) == "ziua 2");
}

TEST_CASE("storage: o salvare sosita cat se citeste din DB castiga la combinare")
{
	DbProcess dbp;
	dbp.db.rows[{ 7, 1 }] = "vechi";
	dbp.db.rows[{ 7, 2 }] = "neatins";

	dbp.cache.OnLoadRequest(1, 7, Clock::now());   // citirea porneste...
	dbp.cache.OnSave(7, 1, "nou", Clock::now());   // ...si intre timp vine o salvare
	dbp.FinishLoads();

	REQUIRE(dbp.replies.size() == 1);
	CHECK(dbp.replies[0].data.at(1) == "nou");
	CHECK(dbp.replies[0].data.at(2) == "neatins");
}

TEST_CASE("storage: doua cereri in timpul citirii primesc acelasi raspuns, cu o singura citire")
{
	DbProcess dbp;
	dbp.db.rows[{ 7, 1 }] = "x";
	dbp.cache.OnLoadRequest(1, 7, Clock::now());
	dbp.cache.OnLoadRequest(2, 7, Clock::now());
	CHECK(dbp.db.pendingLoads.size() == 1);

	dbp.FinishLoads();
	REQUIRE(dbp.replies.size() == 2);
	CHECK(dbp.replies[0].to == 1);
	CHECK(dbp.replies[1].to == 2);

	// a treia cerere vine din cache, fara citire
	dbp.cache.OnLoadRequest(3, 7, Clock::now());
	CHECK(dbp.db.pendingLoads.empty());
	CHECK(dbp.replies.size() == 3);
}

TEST_CASE("storage: o incarcare esuata blocheaza scrierea si permite reincercarea")
{
	DbProcess dbp;
	GameCore core(1, dbp);
	core.store.OnEnter(7);
	dbp.db.pendingLoads.clear();
	dbp.cache.OnLoadFailed(7);
	core.Deliver();

	CHECK(core.store.GetStatus(7) == PlayerDataStore::Status::Failed);
	CHECK_FALSE(core.store.Set(7, 1, "x"));
	CHECK(core.readyEvents.empty());

	// la urmatoarea intrare, db reincearca citirea
	core.store.OnLeave(7);
	core.store.OnEnter(7);
	CHECK(dbp.db.pendingLoads.size() == 1);
}

TEST_CASE("storage: doar sistemele modificate pleaca la Flush, iar blob-ul gol sterge")
{
	DbProcess dbp;
	dbp.db.rows[{ 7, 1 }] = "a";
	dbp.db.rows[{ 7, 2 }] = "b";
	GameCore core(1, dbp);
	core.store.OnEnter(7);
	dbp.FinishLoads();
	core.Deliver();

	core.store.Set(7, 2, "");
	core.store.Set(7, 3, "c");
	CHECK(core.store.DirtyCount(7) == 2);
	CHECK(core.store.Flush(7) == 2);
	CHECK(core.store.Flush(7) == 0);

	dbp.db.ApplyWrites();
	CHECK(dbp.db.Row(7, 1) != "");
	CHECK(dbp.db.Row(7, 2) == "");
	CHECK(dbp.db.Row(7, 3) == "c");
}

TEST_CASE("storage: blob-urile prea mari sunt refuzate pe ambele parti")
{
	DbProcess dbp;
	GameCore core(1, dbp);
	core.store.OnEnter(7);
	dbp.FinishLoads();
	core.Deliver();

	const Blob big(core::storage::kMaxBlobSize + 1, 'x');
	CHECK_FALSE(core.store.Set(7, 1, big));
	CHECK_FALSE(dbp.cache.OnSave(7, 1, big, Clock::now()));
	CHECK(dbp.db.pendingWrites.empty());
}

TEST_CASE("storage: eliminarea din cache sare peste jucatorii in curs de incarcare")
{
	DbProcess dbp;
	const auto t0 = Clock::now();
	dbp.cache.OnSave(1, 1, "x", t0);          // jucator vechi, inactiv
	dbp.cache.OnLoadRequest(9, 2, t0);         // jucator in curs de incarcare
	dbp.cache.OnSave(3, 1, "y", t0 + 50min);   // jucator activ recent

	CHECK(dbp.cache.Evict(t0 + 60min, 30min) == 1);
	CHECK(dbp.cache.Size() == 2);
}

TEST_CASE("storage: un peer deconectat nu mai primeste raspunsuri")
{
	DbProcess dbp;
	dbp.cache.OnLoadRequest(1, 7, Clock::now());
	dbp.cache.OnLoadRequest(2, 7, Clock::now());
	dbp.cache.ForgetRequester(1);
	dbp.FinishLoads();
	REQUIRE(dbp.replies.size() == 1);
	CHECK(dbp.replies[0].to == 2);
}

TEST_CASE("storage: FlushAll trimite datele tuturor jucatorilor, la oprirea serverului")
{
	DbProcess dbp;
	GameCore core(1, dbp);
	for (uint32_t pid : { 7u, 8u })
		core.store.OnEnter(pid);
	dbp.FinishLoads();
	core.Deliver();
	core.store.Set(7, 1, "a");
	core.store.Set(8, 1, "b");

	CHECK(core.store.FlushAll() == 2);
	dbp.db.ApplyWrites();
	CHECK(dbp.db.Row(7, 1) == "a");
	CHECK(dbp.db.Row(8, 1) == "b");
}
