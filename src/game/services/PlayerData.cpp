// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "PlayerData.h"

#include <memory>
#include <string>
#include <vector>

#include "../config.h"
#include "../desc_client.h"
#include "../event.h"
#include "../input.h"
#include "packet_headers.h"

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"
#include "core/log/Log.h"
#include "core/storage/PlayerDataStore.h"

#include "ProtoBegin.h"
#include "server/system_data.pb.h"
#include "ProtoEnd.h"

namespace game::playerdata
{
	namespace
	{
		namespace pb = m2::server::storage;
		using core::storage::PlayerDataStore;

		struct PlayerDataConfig
		{
			int64_t flushIntervalSec = 60; // salvarea periodica a datelor modificate
		};

		class PlayerDataSystem;
		PlayerDataSystem* g_instance = nullptr;

		void SendToDb(const pb::Envelope& envelope)
		{
			if (!db_clientdesc)
				return;
			const std::string bytes = envelope.SerializeAsString();
			db_clientdesc->DBPacket(GD::SYSTEM_DATA, 0, bytes.data(), static_cast<DWORD>(bytes.size()));
		}

		EVENTINFO(PlayerDataFlushInfo)
		{
		};

		EVENTFUNC(player_data_flush_event);

		class PlayerDataSystem : public core::registry::System<PlayerDataConfig>
		{
		public:
			std::string_view Name() const override { return "player_data"; }

			PlayerDataStore& Store() { return *m_store; }
			int64_t FlushIntervalSec() const { return Config().flushIntervalSec; }

			void Describe(std::vector<std::string>& lines) const override
			{
				lines.push_back("jucatori pe acest core: " + std::to_string(m_store ? m_store->PlayerCount() : 0));
				lines.push_back("cereri: " + std::to_string(m_loads) + ", raspunsuri: " + std::to_string(m_ready)
					+ ", esuate: " + std::to_string(m_failed));
				lines.push_back("salvari trimise: " + std::to_string(m_saves) + " (sisteme: " + std::to_string(m_savedEntries) + ")");
				lines.push_back("flush_interval_s=" + std::to_string(Config().flushIntervalSec));
			}

			void CountLoad() { ++m_loads; }
			void CountReady() { ++m_ready; }
			void CountFailed() { ++m_failed; }
			void CountSave(size_t entries) { ++m_saves; m_savedEntries += entries; }

		protected:
			void Read(core::config::Reader& root, PlayerDataConfig& out) override
			{
				out.flushIntervalSec = root.OptionalInt("flush_interval_s", 60, 10, 3600);
				root.RejectUnknown({ "enabled", "log_level", "flush_interval_s" });
			}

			void OnStart() override
			{
				m_store = std::make_unique<PlayerDataStore>(PlayerDataStore::Callbacks{
					[this](uint32_t pid) {
						pb::Envelope envelope;
						envelope.mutable_load_request()->set_pid(pid);
						SendToDb(envelope);
						CountLoad();
						Log().Debug(core::log::Ctx{ {}, pid }, "send LoadRequest");
					},
					[this](uint32_t pid, const PlayerDataStore::Changes& changes) {
						pb::Envelope envelope;
						auto* save = envelope.mutable_save();
						save->set_pid(pid);
						for (const auto& [system, data] : changes)
						{
							auto* entry = save->add_entries();
							entry->set_system_id(system);
							entry->set_data(data);
						}
						SendToDb(envelope);
						CountSave(changes.size());
						Log().Debug(core::log::Ctx{ {}, pid }, "send Save sisteme={}", changes.size());
					},
					[this](uint32_t pid) {
						CountReady();
						const core::log::Ctx ctx{ core::log::NewTrace(), pid };
						Log().Debug(ctx, "Ready");
						core::events::Global().Publish(core::events::SystemDataReady{ pid }, ctx);
					},
					[this](uint32_t pid) {
						CountFailed();
						Log().Error(core::log::Ctx{ {}, pid }, "incarcarea datelor a esuat; sistemele nu vor scrie nimic pentru acest jucator pe acest core");
					} });

				g_instance = this;
				m_flushEvent = event_create(player_data_flush_event, AllocEventInfo<PlayerDataFlushInfo>(),
					PASSES_PER_SEC(Config().flushIntervalSec));
				Log().Info(LifecycleCtx(), "pornit: flush_interval_s={}", Config().flushIntervalSec);
			}

			void OnStop() override
			{
				event_cancel(&m_flushEvent);
				const size_t flushed = m_store ? m_store->FlushAll() : 0;
				g_instance = nullptr;
				Log().Info(LifecycleCtx(), "oprit: salvate la oprire {} sisteme", flushed);
			}

		private:
			std::unique_ptr<PlayerDataStore> m_store;
			LPEVENT m_flushEvent;
			uint64_t m_loads = 0, m_ready = 0, m_failed = 0, m_saves = 0, m_savedEntries = 0;
		};

		EVENTFUNC(player_data_flush_event)
		{
			if (!g_instance)
				return 0; // sistemul a fost oprit: evenimentul se termina
			const size_t flushed = g_instance->Store().FlushAll();
			if (flushed > 0)
				core::log::Get("PLAYER_DATA").Debug({}, "salvare periodica: {} sisteme", flushed);
			return PASSES_PER_SEC(g_instance->FlushIntervalSec());
		}
	}

	bool IsReady(uint32_t pid)
	{
		return g_instance && g_instance->Store().IsReady(pid);
	}

	const std::string* GetRaw(uint32_t pid, uint32_t system)
	{
		return g_instance ? g_instance->Store().Get(pid, system) : nullptr;
	}

	bool SetRaw(uint32_t pid, uint32_t system, std::string data, bool flushNow)
	{
		if (!g_instance)
			return false;
		if (!g_instance->Store().Set(pid, system, std::move(data)))
			return false;
		if (flushNow)
			g_instance->Store().Flush(pid);
		return true;
	}

	void OnEnter(uint32_t pid)
	{
		if (g_instance)
			g_instance->Store().OnEnter(pid);
	}

	void OnLeave(uint32_t pid)
	{
		if (g_instance)
			g_instance->Store().OnLeave(pid);
	}

	void OnDbPacket(const char* data, uint32_t size)
	{
		pb::Envelope envelope;
		if (!envelope.ParseFromArray(data, static_cast<int>(size)) || !envelope.has_load_response())
		{
			core::log::Get("PLAYER_DATA").Error({}, "DG::SYSTEM_DATA invalid ({} octeti)", size);
			return;
		}
		if (!g_instance)
			return;

		const auto& response = envelope.load_response();
		core::storage::SystemMap map;
		for (const auto& entry : response.entries())
			map[entry.system_id()] = entry.data();
		g_instance->Store().OnLoadResponse(response.pid(), std::move(map), response.ok());
	}

	std::unique_ptr<core::registry::ISystem> CreateSystem()
	{
		return std::make_unique<PlayerDataSystem>();
	}
}

// Handler-ul pachetului DG::SYSTEM_DATA. Declarat in input.h, inregistrat in CInputDB::RegisterHandlers.
// Antetul pachetelor db este [header:2][handle:4][size:4], iar p indica imediat dupa el,
// deci lungimea datelor e cei 4 octeti dinaintea lui p.
int CInputDB::HandleSystemData(LPDESC, const char* p)
{
	const uint32_t size = *reinterpret_cast<const uint32_t*>(p - sizeof(uint32_t));
	game::playerdata::OnDbPacket(p, size);
	return 0;
}
