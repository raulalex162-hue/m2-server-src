#include "core/reward/Reward.h"

#include <utility>

namespace core::reward
{
	Delivery Deliver(IReceiver& receiver, const Reward& reward)
	{
		Delivery result;
		for (const Item& item : reward.items)
		{
			if (receiver.CanReceiveItem(item))
			{
				receiver.GiveItem(item);
				result.delivered.items.push_back(item);
			}
			else
			{
				result.remaining.items.push_back(item);
			}
		}

		if (reward.gold > 0)
		{
			const uint64_t capacity = receiver.GoldCapacity();
			const uint64_t give = reward.gold < capacity ? reward.gold : capacity;
			if (give > 0)
				receiver.GiveGold(give);
			result.delivered.gold = give;
			result.remaining.gold = reward.gold - give;
		}
		return result;
	}

	bool Mailbox::Add(Pending pending)
	{
		if (pending.reward.Empty())
			return true; // nimic de pastrat
		if (Full())
			return false;
		m_entries.push_back(std::move(pending));
		return true;
	}

	Reward Mailbox::DeliverAll(IReceiver& receiver)
	{
		Reward total;
		for (auto it = m_entries.begin(); it != m_entries.end();)
		{
			Delivery delivery = Deliver(receiver, it->reward);
			total.items.insert(total.items.end(), delivery.delivered.items.begin(), delivery.delivered.items.end());
			total.gold += delivery.delivered.gold;

			if (delivery.remaining.Empty())
			{
				it = m_entries.erase(it);
			}
			else
			{
				it->reward = std::move(delivery.remaining);
				++it;
			}
		}
		return total;
	}

	Reward ReadReward(config::Reader& reader, const ItemValidator& validator)
	{
		Reward reward;

		if (reader.Has("items"))
		{
			for (auto& entry : reader.RequireArray("items"))
			{
				Item item;
				item.vnum = static_cast<uint32_t>(entry.RequireInt("vnum", 1, 4294967295LL));
				item.count = static_cast<uint32_t>(entry.OptionalInt("count", 1, 1, kMaxItemCount));
				entry.RejectUnknown({ "vnum", "count" });

				std::string why;
				if (item.vnum != 0 && validator && !validator(item.vnum, why))
					entry.Fail("vnum", "item " + std::to_string(item.vnum) + " nu poate fi recompensa: " + why);

				reward.items.push_back(item);
			}
		}

		reward.gold = static_cast<uint64_t>(reader.OptionalInt("gold", 0, 0, static_cast<int64_t>(kMaxGold)));
		reader.RejectUnknown({ "items", "gold" });

		if (reward.Empty())
			reader.Fail("", "recompensa goala: trebuie cel putin un item sau gold > 0");
		return reward;
	}
}
