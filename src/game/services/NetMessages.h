#pragma once
// game/services/NetMessages — mesaje Protobuf tipizate peste core/net + NetTransport.
//
// Un sistem inregistreaza un handler pentru un mesaj:
//   game::net::Handle<m2::heartbeat::Ping>(m2::SYSTEM_HEARTBEAT, m2::heartbeat::PING, "heartbeat",
//       [](CHARACTER* ch, const m2::heartbeat::Ping& ping, const core::log::Ctx& ctx) { ... });
// si raspunde:
//   game::net::SendMessage(ch, m2::SYSTEM_HEARTBEAT, m2::heartbeat::PONG, pong);
//
// Handler-ul e apelat doar daca:
//   - jucatorul care a trimis mesajul inca e online pe acest core;
//   - mesajul se parseaza corect ca tipul cerut (altfel e logat si ignorat).

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

#include "core/log/Log.h"
#include "core/net/Router.h"
#include "NetTransport.h"

#include "../char.h"
#include "../char_manager.h"

namespace game::net
{
	template <typename Message>
	using MessageHandler = std::function<void(CHARACTER* ch, const Message& message, const core::log::Ctx& ctx)>;

	template <typename Message>
	bool Handle(uint16_t system, uint16_t type, std::string owner, MessageHandler<Message> handler,
		size_t maxBody = core::net::kDefaultMaxBody)
	{
		const std::string name = owner;
		return core::net::Global().Register(system, type, std::move(owner),
			[name, system, type, handler = std::move(handler)](uint32_t pid, core::net::Body body, const core::log::Ctx& ctx) {
				CHARACTER* ch = CHARACTER_MANAGER::instance().FindByPID(pid);
				if (!ch)
					return; // jucatorul a iesit intre timp

				Message message;
				if (!message.ParseFromArray(body.data(), static_cast<int>(body.size())))
				{
					core::log::Get("NET").Warn(ctx, "{}: mesaj {}:{} invalid ({} octeti), ignorat", name, system, type, body.size());
					return;
				}
				handler(ch, message, ctx);
			},
			maxBody);
	}

	template <typename Message>
	bool SendMessage(CHARACTER* ch, uint16_t system, uint16_t type, const Message& message)
	{
		const std::string bytes = message.SerializeAsString();
		return Send(ch, system, type,
			std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
	}
}
