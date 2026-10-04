// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "NetTransport.h"

#include <vector>

#include "../char.h"
#include "../desc.h"
#include "../input.h"
#include "packet_headers.h"

#include "core/net/Router.h"

// Handler-ul pachetului CG::SYSTEM. Declarat in input.h, inregistrat in
// CInputMain::RegisterHandlers. Framing-ul a fost deja validat de CInputProcessor::Process.
int CInputMain::HandleSystem(LPDESC d, const char* c_pData)
{
	LPCHARACTER ch = d ? d->GetCharacter() : nullptr;
	if (!ch)
		return 0;

	const uint16_t length = *reinterpret_cast<const uint16_t*>(c_pData + 2);
	if (length < PACKET_HEADER_SIZE)
		return 0;

	const auto* payload = reinterpret_cast<const uint8_t*>(c_pData + PACKET_HEADER_SIZE);
	core::net::Global().Dispatch(ch->GetPlayerID(), std::span<const uint8_t>(payload, length - PACKET_HEADER_SIZE));
	return 0;
}

namespace game::net
{
	bool Send(CHARACTER* ch, uint16_t system, uint16_t type, std::span<const uint8_t> body)
	{
		if (!ch || !ch->GetDesc())
			return false;
		if (body.size() > core::net::kMaxBodySize)
		{
			core::log::Get("NET").Error(core::log::Ctx{ {}, ch->GetPlayerID() },
				"send {}:{} refuzat: body de {} octeti nu incape intr-un pachet", system, type, body.size());
			return false;
		}

		const std::vector<uint8_t> payload = core::net::EncodePayload(system, type, body);
		const uint16_t length = static_cast<uint16_t>(PACKET_HEADER_SIZE + payload.size());

		std::vector<uint8_t> packet;
		packet.reserve(length);
		packet.push_back(static_cast<uint8_t>(GC::SYSTEM & 0xFF));
		packet.push_back(static_cast<uint8_t>(GC::SYSTEM >> 8));
		packet.push_back(static_cast<uint8_t>(length & 0xFF));
		packet.push_back(static_cast<uint8_t>(length >> 8));
		packet.insert(packet.end(), payload.begin(), payload.end());

		ch->GetDesc()->Packet(packet.data(), static_cast<int>(packet.size()));
		return true;
	}
}
