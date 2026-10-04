// Fisierul sta in src/game/systems/heartbeat, deci headerele jocului se includ cu "../../".
#include "../../stdafx.h"
#include "Heartbeat.h"

#include <string>

#include "../../char.h"

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"

#include "../../services/NetMessages.h"

#include "m2/ProtocolHash.h"

// Headerele Protobuf se includ mereu intre ProtoBegin.h si ProtoEnd.h (vezi ProtoBegin.h).
#include "../../services/ProtoBegin.h"
#include "m2/heartbeat.pb.h"
#include "m2/system_ids.pb.h"
#include "../../services/ProtoEnd.h"

namespace game::systems::heartbeat
{
	namespace
	{
		uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}
	}

	void Heartbeat::Read(core::config::Reader& root, HeartbeatConfig& out)
	{
		out.logEnterLeave = root.OptionalBool("log_enter_leave", true);
		out.minPingIntervalMs = root.OptionalInt("min_ping_interval_ms", 1000, 0, 60000);
		root.RejectUnknown({ "enabled", "log_level", "log_enter_leave", "min_ping_interval_ms" });
	}

	void Heartbeat::OnStart()
	{
		auto& bus = core::events::Global();

		bus.Subscribe<core::events::EnterGame>("heartbeat",
			[this](const core::events::EnterGame& e, const core::log::Ctx& ctx) {
				if (Config().logEnterLeave)
					Log().Info(ctx, "recv EnterGame map={} level={} empire={}", e.mapIndex, e.level, e.empire);
			});

		bus.Subscribe<core::events::LeaveGame>("heartbeat",
			[this](const core::events::LeaveGame& e, const core::log::Ctx& ctx) {
				m_lastPing.erase(e.pid);
				if (Config().logEnterLeave)
					Log().Info(ctx, "recv LeaveGame map={}", e.mapIndex);
			});

		bus.Subscribe<core::events::MobKill>("heartbeat",
			[this](const core::events::MobKill& e, const core::log::Ctx& ctx) {
				Log().Debug(ctx, "recv MobKill vnum={} level={} dungeon={}", e.mobVnum, e.mobLevel, e.inDungeon);
			});

		game::net::Handle<m2::heartbeat::Ping>(m2::SYSTEM_HEARTBEAT, m2::heartbeat::PING, "heartbeat",
			[this](CHARACTER* ch, const m2::heartbeat::Ping& ping, const core::log::Ctx& ctx) {
				const auto now = Clock::now();
				const uint32_t pid = ch->GetPlayerID();

				if (auto it = m_lastPing.find(pid); it != m_lastPing.end())
				{
					const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second).count();
					if (elapsed < Config().minPingIntervalMs)
					{
						++m_rejected;
						Log().Warn(ctx, "Ping refuzat: prea des ({} ms < {} ms)", elapsed, Config().minPingIntervalMs);
						return;
					}
				}
				m_lastPing[pid] = now;
				++m_pings;

				const bool protocolOk = ping.protocol_hash() == m2::kProtocolHash;
				if (!protocolOk)
					Log().Warn(ctx, "hash de protocol diferit: client={} server={}", ping.protocol_hash(), m2::kProtocolHash);

				m2::heartbeat::Pong pong;
				pong.set_client_time_ms(ping.client_time_ms());
				pong.set_server_time_ms(NowMs());
				pong.set_protocol_ok(protocolOk);
				pong.set_server_protocol_hash(m2::kProtocolHash);

				const bool sent = game::net::SendMessage(ch, m2::SYSTEM_HEARTBEAT, m2::heartbeat::PONG, pong);
				Log().Debug(ctx, "recv Ping -> send Pong protocol_ok={} sent={}", protocolOk, sent);
			},
			256);

		Log().Info({}, "pornit: log_enter_leave={} min_ping_interval_ms={} protocol={}",
			Config().logEnterLeave, Config().minPingIntervalMs, m2::kProtocolHash);
	}

	void Heartbeat::OnStop()
	{
		Log().Info({}, "oprit: pings={} refuzate={}", m_pings, m_rejected);
		m_lastPing.clear();
	}

	void Heartbeat::OnConfigReloaded()
	{
		Log().Info({}, "config reincarcat: log_enter_leave={} min_ping_interval_ms={}",
			Config().logEnterLeave, Config().minPingIntervalMs);
	}
}
