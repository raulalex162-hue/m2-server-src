#include "core/storage/PlayerDataStore.h"

namespace core::storage
{
	PlayerDataStore::PlayerDataStore(Callbacks callbacks)
		: m_callbacks(std::move(callbacks))
	{
	}

	void PlayerDataStore::OnEnter(uint32_t pid)
	{
		auto it = m_players.find(pid);
		if (it != m_players.end() && it->second.status == Status::Ready)
			return; // deja incarcat pe acest core (ex. EnterGame repetat fara LeaveGame)

		Player& player = m_players[pid];
		player.status = Status::Waiting;
		player.data.clear();
		player.dirty.clear();
		m_callbacks.requestLoad(pid);
	}

	void PlayerDataStore::OnLoadResponse(uint32_t pid, SystemMap data, bool ok)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.status != Status::Waiting)
			return; // jucatorul a plecat intre timp, sau raspuns dublat

		Player& player = it->second;
		if (!ok)
		{
			player.status = Status::Failed;
			if (m_callbacks.onLoadFailed)
				m_callbacks.onLoadFailed(pid);
			return;
		}

		player.data = std::move(data);
		player.status = Status::Ready;
		if (m_callbacks.onReady)
			m_callbacks.onReady(pid);
	}

	void PlayerDataStore::OnLeave(uint32_t pid)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end())
			return;
		Flush(pid);
		m_players.erase(it);
	}

	PlayerDataStore::Status PlayerDataStore::GetStatus(uint32_t pid) const
	{
		auto it = m_players.find(pid);
		return it == m_players.end() ? Status::Unknown : it->second.status;
	}

	const Blob* PlayerDataStore::Get(uint32_t pid, uint32_t system) const
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.status != Status::Ready)
			return nullptr;
		auto entry = it->second.data.find(system);
		return entry == it->second.data.end() ? nullptr : &entry->second;
	}

	bool PlayerDataStore::Set(uint32_t pid, uint32_t system, Blob data)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.status != Status::Ready)
			return false;
		if (data.size() > kMaxBlobSize)
			return false;

		Player& player = it->second;
		if (data.empty())
			player.data.erase(system);
		else
			player.data[system] = std::move(data);
		player.dirty.insert(system);
		return true;
	}

	size_t PlayerDataStore::Flush(uint32_t pid)
	{
		auto it = m_players.find(pid);
		if (it == m_players.end() || it->second.status != Status::Ready || it->second.dirty.empty())
			return 0;

		Player& player = it->second;
		Changes changes;
		changes.reserve(player.dirty.size());
		for (uint32_t system : player.dirty)
		{
			auto entry = player.data.find(system);
			changes.emplace_back(system, entry == player.data.end() ? Blob{} : entry->second);
		}
		player.dirty.clear();
		m_callbacks.sendSave(pid, changes);
		return changes.size();
	}

	size_t PlayerDataStore::FlushAll()
	{
		size_t total = 0;
		for (auto& [pid, player] : m_players)
			total += Flush(pid);
		return total;
	}

	size_t PlayerDataStore::DirtyCount(uint32_t pid) const
	{
		auto it = m_players.find(pid);
		return it == m_players.end() ? 0 : it->second.dirty.size();
	}
}
