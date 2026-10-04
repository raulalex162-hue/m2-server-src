#pragma once
// core/storage/PlayerDataStore — datele per jucator ale sistemelor, in procesul game.
//
// Ciclul unui jucator pe un core:
//   OnEnter(pid)          -> cere datele de la db (requestLoad)
//   OnLoadResponse(...)   -> datele sosesc; jucatorul devine Ready (onReady)
//   Get / Set             -> sistemele citesc si modifica datele lor
//   Flush(pid)            -> trimite la db ce s-a modificat (sendSave)
//   OnLeave(pid)          -> Flush + uita jucatorul pe acest core
//
// Regula de siguranta: inainte de Ready, Set e refuzat. Altfel un sistem ar putea scrie
// valori goale peste progresul real, care inca nu a sosit de la db. Daca incarcarea esueaza,
// jucatorul ramane Failed pe acest core si nimic nu se scrie pentru el.
//
// Clasa nu stie de retea: primeste functii (Callbacks), ca sa poata fi testata singura.

#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/storage/SystemDataCache.h"

namespace core::storage
{
	class PlayerDataStore
	{
	public:
		using Changes = std::vector<std::pair<uint32_t, Blob>>; // (system_id, date); blob gol = sterge

		struct Callbacks
		{
			std::function<void(uint32_t pid)> requestLoad;
			std::function<void(uint32_t pid, const Changes& changes)> sendSave;
			std::function<void(uint32_t pid)> onReady;     // datele au sosit; sistemele pot lucra
			std::function<void(uint32_t pid)> onLoadFailed; // optional
		};

		enum class Status
		{
			Unknown, // jucatorul nu e pe acest core
			Waiting, // datele au fost cerute, inca nu au sosit
			Ready,
			Failed
		};

		explicit PlayerDataStore(Callbacks callbacks);

		void OnEnter(uint32_t pid);
		void OnLoadResponse(uint32_t pid, SystemMap data, bool ok);
		void OnLeave(uint32_t pid);

		Status GetStatus(uint32_t pid) const;
		bool IsReady(uint32_t pid) const { return GetStatus(pid) == Status::Ready; }

		// nullptr daca jucatorul nu e Ready sau sistemul nu are date pentru el.
		const Blob* Get(uint32_t pid, uint32_t system) const;

		// false daca jucatorul nu e Ready sau blob-ul depaseste kMaxBlobSize. Blob gol = sterge.
		bool Set(uint32_t pid, uint32_t system, Blob data);

		// Trimite la db sistemele modificate ale jucatorului. Intoarce cate sisteme au plecat.
		size_t Flush(uint32_t pid);
		size_t FlushAll();

		size_t PlayerCount() const { return m_players.size(); }
		size_t DirtyCount(uint32_t pid) const;

	private:
		struct Player
		{
			Status status = Status::Waiting;
			SystemMap data;
			std::set<uint32_t> dirty;
		};

		Callbacks m_callbacks;
		std::unordered_map<uint32_t, Player> m_players;
	};
}
