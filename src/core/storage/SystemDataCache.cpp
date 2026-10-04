#include "core/storage/SystemDataCache.h"

#include <algorithm>

namespace core::storage
{
	SystemDataCache::SystemDataCache(Callbacks callbacks)
		: m_callbacks(std::move(callbacks))
	{
	}

	void SystemDataCache::OnLoadRequest(Requester from, uint32_t pid, Clock::time_point now)
	{
		Player& player = m_players[pid];
		player.lastAccess = now;

		switch (player.state)
		{
			case State::Loaded:
				m_callbacks.reply(from, pid, player.data, true);
				return;

			case State::Loading:
				player.waiting.push_back(from);
				return;

			case State::Partial:
				player.state = State::Loading;
				player.waiting.push_back(from);
				m_callbacks.queryLoad(pid);
				return;
		}
	}

	void SystemDataCache::OnLoadResult(uint32_t pid, SystemMap fromDb, Clock::time_point now)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.state != State::Loading)
			return; // raspuns neasteptat (ex. jucatorul a fost eliminat intre timp)

		Player& player = it->second;
		// Ce s-a salvat cat timp se citea din DB e mai nou: insert nu suprascrie cheile existente.
		for (auto& [system, data] : fromDb)
			player.data.insert({ system, std::move(data) });

		player.state = State::Loaded;
		player.lastAccess = now;

		const auto waiting = std::move(player.waiting);
		player.waiting.clear();
		for (Requester to : waiting)
			m_callbacks.reply(to, pid, player.data, true);
	}

	void SystemDataCache::OnLoadFailed(uint32_t pid)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.state != State::Loading)
			return;

		Player& player = it->second;
		const auto waiting = std::move(player.waiting);
		player.waiting.clear();
		// Revenim la Partial: urmatoarea cerere reincearca citirea; salvarile deja primite raman.
		player.state = State::Partial;
		for (Requester to : waiting)
			m_callbacks.reply(to, pid, {}, false);
	}

	bool SystemDataCache::OnSave(uint32_t pid, uint32_t system, Blob data, Clock::time_point now)
	{
		if (data.size() > kMaxBlobSize)
			return false;

		Player& player = m_players[pid];
		player.lastAccess = now;

		if (data.empty())
			player.data.erase(system);
		else
			player.data[system] = data;

		m_callbacks.persist(pid, system, data);
		return true;
	}

	size_t SystemDataCache::Evict(Clock::time_point now, Clock::duration idle)
	{
		size_t removed = 0;
		for (auto it = m_players.begin(); it != m_players.end();)
		{
			const Player& player = it->second;
			const bool busy = player.state == State::Loading || !player.waiting.empty();
			if (!busy && now - player.lastAccess >= idle)
			{
				it = m_players.erase(it);
				++removed;
			}
			else
			{
				++it;
			}
		}
		return removed;
	}

	void SystemDataCache::ForgetRequester(Requester requester)
	{
		for (auto& [pid, player] : m_players)
		{
			auto& w = player.waiting;
			w.erase(std::remove(w.begin(), w.end(), requester), w.end());
		}
	}

	bool SystemDataCache::IsLoaded(uint32_t pid) const
	{
		auto it = m_players.find(pid);
		return it != m_players.end() && it->second.state == State::Loaded;
	}
}
