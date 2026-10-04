#pragma once
// core/net — routerul mesajelor sistemelor noi.
//
// Toate sistemele noi comunica printr-un singur pachet m2dev, CG::SYSTEM / GC::SYSTEM.
// In interiorul lui, primii 4 octeti spun carui sistem ii apartine mesajul:
//
//   pachet m2dev:  [header:2 = SYSTEM][lungime:2][ system:2 ][ type:2 ][ body... ]
//                                                \__________ payload ____________/
//
// "body" e mesajul Protobuf serializat (pasul 6b-2). Routerul nu stie ce e in el:
// verifica doar dimensiunea si il trimite handler-ului inregistrat pentru (system, type).
//
// Garantii:
//   - un mesaj necunoscut, prea scurt sau prea mare NU ajunge la niciun sistem;
//   - fiecare mesaj are o dimensiune maxima proprie (implicit 4 KB);
//   - un handler care arunca o exceptie e logat si nu opreste serverul;
//   - un sistem oprit isi pierde automat handler-ele (registry-ul apeleaza UnregisterOwner).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/log/Log.h"

namespace core::net
{
	using Body = std::span<const uint8_t>;

	// Dimensiunea prefixului (system + type) din payload.
	constexpr size_t kPrefixSize = 4;
	// Limita pachetului m2dev (lungimea e pe 2 octeti) minus framing-ul si prefixul.
	constexpr size_t kMaxBodySize = 65535 - 4 - kPrefixSize;
	constexpr size_t kDefaultMaxBody = 4096;

	enum class Result
	{
		Ok,
		TooShort,       // payload mai mic decat prefixul
		UnknownMessage, // nimeni nu a inregistrat (system, type)
		TooLarge,       // body peste limita mesajului
		HandlerError    // handler-ul a aruncat o exceptie
	};

	const char* ToString(Result result);

	class Router
	{
	public:
		// pid = jucatorul care a trimis mesajul.
		using Handler = std::function<void(uint32_t pid, Body body, const log::Ctx& ctx)>;

		// false daca (system, type) e deja luat sau system/type e 0.
		bool Register(uint16_t system, uint16_t type, std::string owner, Handler handler,
			size_t maxBody = kDefaultMaxBody);
		void UnregisterOwner(std::string_view owner);

		// Trimite payload-ul (fara framing-ul m2dev) la handler-ul potrivit.
		Result Dispatch(uint32_t pid, Body payload);

		size_t Count() const { return m_routes.size(); }
		// Cate mesaje are inregistrate un sistem (pentru /sysinfo).
		size_t OwnerCount(std::string_view owner) const;
		bool Has(uint16_t system, uint16_t type) const;

	private:
		struct Route
		{
			std::string owner;
			Handler handler;
			size_t maxBody;
		};

		static log::Channel& Log() { return log::Get("NET"); }

		std::map<std::pair<uint16_t, uint16_t>, Route> m_routes;
	};

	// Construieste payload-ul unui mesaj: [system][type][body], little-endian.
	std::vector<uint8_t> EncodePayload(uint16_t system, uint16_t type, Body body);

	// Routerul procesului (cel folosit de game).
	Router& Global();
}
