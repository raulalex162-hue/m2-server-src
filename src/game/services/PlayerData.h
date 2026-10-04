#pragma once
// game/services/PlayerData — datele per jucator ale sistemelor (partea din game a PlayerSystemData).
//
// Un sistem isi tine starea per jucator ca mesaj Protobuf, identificat prin numarul lui din
// m2::SystemId. Fluxul:
//   EnterGame          -> datele sunt cerute automat de la db
//   SystemDataReady    -> datele au sosit: de acum Load/Store functioneaza pentru acel jucator
//   LeaveGame          -> sistemele inca pot face Store; dupa ce l-au primit toate, datele pleaca la db
//
// Folosire intr-un sistem (headerele .pb.h intre ProtoBegin.h / ProtoEnd.h):
//   m2::server::heartbeat::State state;
//   if (game::playerdata::Load(pid, m2::SYSTEM_HEARTBEAT, state)) {   // false = datele nu au sosit
//       state.set_total_pings(state.total_pings() + 1);
//       game::playerdata::Store(pid, m2::SYSTEM_HEARTBEAT, state);       // salvat la LeaveGame sau periodic
//   }
//   game::playerdata::Store(pid, sistem, stare, true);                   // true = trimite imediat (ex. o revendicare)
//
// Un mesaj cu toate campurile implicite se serializeaza gol si inseamna "fara date": Load intoarce
// atunci mesajul implicit, deci nu e nevoie de cazuri speciale pentru jucatorii noi.

#include <cstdint>
#include <memory>
#include <string>

#include "core/registry/Registry.h"

namespace game::playerdata
{
	bool IsReady(uint32_t pid);

	// Datele brute ale unui sistem: nullptr daca jucatorul nu e Ready sau sistemul nu are date.
	const std::string* GetRaw(uint32_t pid, uint32_t system);
	// false daca jucatorul nu e Ready sau datele sunt prea mari. flushNow = trimite imediat la db.
	bool SetRaw(uint32_t pid, uint32_t system, std::string data, bool flushNow = false);

	template <typename Message>
	bool Load(uint32_t pid, uint32_t system, Message& out)
	{
		if (!IsReady(pid))
			return false;
		out.Clear();
		if (const std::string* raw = GetRaw(pid, system))
			return out.ParseFromString(*raw);
		return true; // fara date: mesajul implicit
	}

	template <typename Message>
	bool Store(uint32_t pid, uint32_t system, const Message& message, bool flushNow = false)
	{
		return SetRaw(pid, system, message.SerializeAsString(), flushNow);
	}

	// Apelate doar din EventPublish, in ordinea garantata (vezi PlayerData.cpp).
	void OnEnter(uint32_t pid);
	void OnLeave(uint32_t pid);

	// Apelat din CInputDB pentru DG::SYSTEM_DATA.
	void OnDbPacket(const char* data, uint32_t size);

	// Sistemul din registry (lista din systems/Systems.cpp, primul la pornire, ultimul la oprire).
	std::unique_ptr<core::registry::ISystem> CreateSystem();
}
