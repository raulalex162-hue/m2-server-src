// Fisierul sta in src/db/systemdata, deci headerele db se includ cu "../".
#include "../stdafx.h"
#include "SystemData.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../ClientManager.h"
#include "../DBManager.h"
#include "../Peer.h"
#include "../QID.h"
#include "packet_headers.h"

#include "core/log/Log.h"
#include "core/storage/SystemDataCache.h"

#include "ProtoBegin.h"
#include "server/system_data.pb.h"
#include "ProtoEnd.h"

namespace db::systemdata
{
	namespace
	{
		namespace pb = m2::server::storage;
		using core::storage::Blob;
		using core::storage::Clock;
		using core::storage::SystemMap;

		// Jucatorii neaccesati de atat timp ies din cache (datele sunt deja in MariaDB).
		constexpr auto kEvictAfter = std::chrono::minutes(30);

		core::log::Channel& Log() { return core::log::Get("SYSDATA"); }

		// Ce tinem minte pentru un query de citire, pana vine rezultatul.
		struct LoadInfo
		{
			uint32_t pid;
		};

		std::string Hex(const Blob& data)
		{
			static const char* digits = "0123456789abcdef";
			std::string out;
			out.reserve(data.size() * 2);
			for (unsigned char c : data)
			{
				out.push_back(digits[c >> 4]);
				out.push_back(digits[c & 0x0F]);
			}
			return out;
		}

		void QueryLoad(uint32_t pid)
		{
			char query[128];
			std::snprintf(query, sizeof(query), "SELECT system_id, data FROM player_system_data WHERE pid=%u", pid);
			CDBManager::instance().ReturnQuery(query, QID_SYSTEM_DATA_LOAD, 0, new LoadInfo{ pid });
		}

		void Persist(uint32_t pid, uint32_t system, const Blob& data)
		{
			std::string query;
			if (data.empty())
			{
				query = "DELETE FROM player_system_data WHERE pid=" + std::to_string(pid)
					+ " AND system_id=" + std::to_string(system);
			}
			else
			{
				// Hex: datele sunt binare (Protobuf), iar 0x... nu are nevoie de escapare.
				query = "REPLACE INTO player_system_data (pid, system_id, data) VALUES ("
					+ std::to_string(pid) + "," + std::to_string(system) + ",0x" + Hex(data) + ")";
			}
			CDBManager::instance().AsyncQuery(query.c_str());
		}

		void Reply(core::storage::Requester to, uint32_t pid, const SystemMap& data, bool ok)
		{
			CPeer* peer = CClientManager::instance().FindPeer(static_cast<IDENT>(to));
			if (!peer)
			{
				Log().Debug(core::log::Ctx{ {}, pid }, "raspuns abandonat: core-ul {} s-a deconectat", to);
				return;
			}

			pb::Envelope envelope;
			auto* response = envelope.mutable_load_response();
			response->set_pid(pid);
			response->set_ok(ok);
			for (const auto& [system, blob] : data)
			{
				auto* entry = response->add_entries();
				entry->set_system_id(system);
				entry->set_data(blob);
			}

			const std::string bytes = envelope.SerializeAsString();
			peer->EncodeHeader(DG::SYSTEM_DATA, 0, static_cast<DWORD>(bytes.size()));
			peer->Encode(bytes.data(), static_cast<uint32_t>(bytes.size()));
			Log().Debug(core::log::Ctx{ {}, pid }, "send LoadResponse ok={} sisteme={} catre core {}", ok, data.size(), to);
		}

		core::storage::SystemDataCache& Cache()
		{
			static core::storage::SystemDataCache cache{ core::storage::SystemDataCache::Callbacks{ QueryLoad, Persist, Reply } };
			return cache;
		}
	}

	void OnPacket(CPeer* peer, const char* data, uint32_t size)
	{
		pb::Envelope envelope;
		if (!envelope.ParseFromArray(data, static_cast<int>(size)))
		{
			Log().Error({}, "GD::SYSTEM_DATA invalid ({} octeti) de la core {}", size, peer->GetHandle());
			return;
		}

		const auto now = Clock::now();
		switch (envelope.body_case())
		{
			case pb::Envelope::kLoadRequest:
			{
				const uint32_t pid = envelope.load_request().pid();
				Log().Debug(core::log::Ctx{ {}, pid }, "recv LoadRequest de la core {}", peer->GetHandle());
				Cache().OnLoadRequest(peer->GetHandle(), pid, now);
				break;
			}

			case pb::Envelope::kSave:
			{
				const auto& save = envelope.save();
				for (const auto& entry : save.entries())
				{
					if (!Cache().OnSave(save.pid(), entry.system_id(), entry.data(), now))
						Log().Error(core::log::Ctx{ {}, save.pid() }, "Save refuzat: sistemul {} are {} octeti (max {})",
							entry.system_id(), entry.data().size(), core::storage::kMaxBlobSize);
				}
				Log().Debug(core::log::Ctx{ {}, save.pid() }, "recv Save sisteme={}", save.entries_size());
				break;
			}

			default:
				Log().Warn({}, "GD::SYSTEM_DATA cu mesaj neasteptat ({}) de la core {}",
					static_cast<int>(envelope.body_case()), peer->GetHandle());
				break;
		}
	}

	void OnLoadResult(_SQLMsg* msg, CQueryInfo* qi)
	{
		auto* info = static_cast<LoadInfo*>(qi->pvData);
		const uint32_t pid = info ? info->pid : 0;
		delete info;
		qi->pvData = nullptr;

		if (msg->uiSQLErrno != 0)
		{
			Log().Error(core::log::Ctx{ {}, pid }, "citirea din MariaDB a esuat (errno {})", msg->uiSQLErrno);
			Cache().OnLoadFailed(pid);
			return;
		}

		SystemMap data;
		if (MYSQL_RES* result = msg->Get()->pSQLResult)
		{
			while (MYSQL_ROW row = mysql_fetch_row(result))
			{
				unsigned long* lengths = mysql_fetch_lengths(result);
				if (!row[0] || !row[1] || !lengths)
					continue;
				const auto system = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				data[system] = Blob(row[1], lengths[1]);
			}
		}

		Log().Debug(core::log::Ctx{ {}, pid }, "citit din MariaDB: {} sisteme", data.size());
		Cache().OnLoadResult(pid, std::move(data), Clock::now());
	}

	void Update()
	{
		const size_t removed = Cache().Evict(Clock::now(), kEvictAfter);
		if (removed > 0)
			Log().Debug({}, "eliminati din cache: {} jucatori, ramasi: {}", removed, Cache().Size());
	}
}
