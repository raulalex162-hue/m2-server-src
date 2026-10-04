#pragma once
// game/services/NetTransport — legatura dintre pachetul m2dev CG/GC::SYSTEM si core/net.
//
// Primire:  CInputMain::HandleSystem (definit in NetTransport.cpp) scoate payload-ul
//           din pachet si il da lui core::net::Global().Dispatch(pid, payload).
// Trimitere: game::net::Send(ch, system, type, body) construieste pachetul GC::SYSTEM.

#include <cstdint>
#include <span>

class CHARACTER;

namespace game::net
{
	// Trimite un mesaj catre clientul caracterului. false daca nu are conexiune
	// sau mesajul nu incape intr-un pachet (body > core::net::kMaxBodySize).
	bool Send(CHARACTER* ch, uint16_t system, uint16_t type, std::span<const uint8_t> body);
}
