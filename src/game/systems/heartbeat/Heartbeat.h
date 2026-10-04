#pragma once
// heartbeat — sistemul-martor al Fazei 0.
//
// Nu face nimic in joc. Dovedeste ca lantul CORE functioneaza cap-coada:
//   config  -> citeste conf/systems/heartbeat.json
//   log     -> scrie pe canalul HEARTBEAT
//   events  -> asculta EnterGame / LeaveGame
//   net     -> raspunde la Ping (client) cu Pong si verifica hash-ul protocolului
//
// Ce NU face: nu salveaza nimic, nu modifica jucatorul, nu trimite nimic nesolicitat.

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/registry/Registry.h"

namespace game::systems::heartbeat
{
	struct HeartbeatConfig
	{
		bool logEnterLeave = true;      // scrie in log fiecare intrare / iesire
		int64_t minPingIntervalMs = 0;  // un jucator nu poate trimite Ping mai des de atat
	};

	class Heartbeat : public core::registry::System<HeartbeatConfig>
	{
	public:
		std::string_view Name() const override { return "heartbeat"; }
		void Describe(std::vector<std::string>& lines) const override;

	protected:
		void Read(core::config::Reader& root, HeartbeatConfig& out) override;
		void OnStart() override;
		void OnStop() override;
		void OnConfigReloaded() override;

	private:
		using Clock = std::chrono::steady_clock;

		// Ultimul Ping acceptat, per jucator (pentru limita de frecventa).
		std::unordered_map<uint32_t, Clock::time_point> m_lastPing;
		uint64_t m_pings = 0;
		uint64_t m_rejected = 0;
	};
}
