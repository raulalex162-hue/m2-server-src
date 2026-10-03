#include "core/events/Bus.h"

#include <algorithm>

namespace core::events
{
	void Bus::Deactivate(const std::function<bool(const Entry&)>& match)
	{
		for (auto& [type, list] : m_handlers)
			for (auto& entry : list)
				if (entry.active && match(entry))
					entry.active = false;

		// In afara unei publicari putem sterge imediat; altfel stergem la final.
		if (m_depth == 0)
			Compact();
	}

	void Bus::Compact()
	{
		for (auto& [type, list] : m_handlers)
			list.erase(std::remove_if(list.begin(), list.end(), [](const Entry& e) { return !e.active; }), list.end());
	}

	void Bus::Unsubscribe(SubscriptionId id)
	{
		Deactivate([id](const Entry& e) { return e.id == id; });
	}

	void Bus::UnsubscribeOwner(std::string_view owner)
	{
		Deactivate([owner](const Entry& e) { return e.owner == owner; });
	}

	size_t Bus::OwnerCount(std::string_view owner) const
	{
		size_t n = 0;
		for (const auto& [type, list] : m_handlers)
			for (const auto& entry : list)
				if (entry.active && entry.owner == owner)
					++n;
		return n;
	}

	Bus& Global()
	{
		static Bus bus;
		return bus;
	}
}
