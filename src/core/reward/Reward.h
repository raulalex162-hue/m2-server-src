#pragma once
// core/reward — recompensele: ce contin, cum se livreaza si unde asteapta cand nu incap.
//
// Un sistem decide CAND si CE da; RewardService (game/services/Reward) decide CUM ajunge la jucator.
// Regula de baza: o recompensa nu se pierde niciodata. Ce nu incape in inventar (sau yang-ul care
// ar depasi limita) intra in cutia de recompense in asteptare (Mailbox) si se livreaza mai tarziu.
//
// Partea de aici nu stie de CHARACTER: inventarul real e vazut prin IReceiver, implementat in game.
// Asa poate fi testata singura.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/config/Config.h"

namespace core::reward
{
	constexpr uint32_t kMaxItemCount = 200;          // marimea maxima a unui teanc in m2dev
	constexpr uint64_t kMaxGold = 2000000000ULL - 1; // GOLD_MAX din m2dev este exclusiv

	struct Item
	{
		uint32_t vnum = 0;
		uint32_t count = 0;

		bool operator==(const Item&) const = default;
	};

	struct Reward
	{
		std::vector<Item> items;
		uint64_t gold = 0;

		bool Empty() const { return items.empty() && gold == 0; }
		bool operator==(const Reward&) const = default;
	};

	// Inventarul unui jucator, vazut de logica de livrare. Implementat in game peste CHARACTER.
	class IReceiver
	{
	public:
		virtual ~IReceiver() = default;
		virtual bool CanReceiveItem(const Item& item) = 0;
		virtual void GiveItem(const Item& item) = 0;
		// Cat yang mai poate primi jucatorul pana la limita (0 = deloc).
		virtual uint64_t GoldCapacity() = 0;
		virtual void GiveGold(uint64_t gold) = 0;
	};

	struct Delivery
	{
		Reward delivered; // a ajuns la jucator
		Reward remaining; // nu a incaput; trebuie pastrat
	};

	// Da tot ce incape, in ordine; intoarce ce s-a dat si ce a ramas.
	// Itemele se dau intregi (un item cu count <= 200 ocupa cel mult o celula); yang-ul se da
	// pana la limita jucatorului, iar diferenta ramane.
	Delivery Deliver(IReceiver& receiver, const Reward& reward);

	// O recompensa in asteptare in cutie.
	struct Pending
	{
		std::string source;     // ex. "daily_reward", "achievement", "gm_test"
		Reward reward;
		uint64_t createdMs = 0; // ora serverului la intrarea in cutie
	};

	class Mailbox
	{
	public:
		explicit Mailbox(size_t limit) : m_limit(limit) {}

		// false daca cutia e plina (recompensa NU intra; apelantul trebuie sa logheze eroarea).
		bool Add(Pending pending);

		// Incearca sa livreze tot ce asteapta, in ordinea sosirii. Intrarile livrate complet ies din
		// cutie; cele livrate partial raman cu restul. Intoarce ce s-a livrat, adunat.
		Reward DeliverAll(IReceiver& receiver);

		const std::vector<Pending>& Entries() const { return m_entries; }
		std::vector<Pending>& MutableEntries() { return m_entries; }
		size_t Size() const { return m_entries.size(); }
		size_t Limit() const { return m_limit; }
		bool Full() const { return m_entries.size() >= m_limit; }

	private:
		size_t m_limit;
		std::vector<Pending> m_entries;
	};

	// Verifica un vnum (ex. ca exista in item_proto). false + motiv daca nu e valid.
	using ItemValidator = std::function<bool(uint32_t vnum, std::string& why)>;

	// Citeste un obiect de forma { "items": [ { "vnum": 27002, "count": 50 } ], "gold": 1000 }.
	// Problemele merg in Report-ul reader-ului; validator e optional.
	Reward ReadReward(config::Reader& reader, const ItemValidator& validator = {});
}
