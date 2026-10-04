#pragma once
// db/systemdata — partea din db a PlayerSystemData.
//
// Primeste GD::SYSTEM_DATA (LoadRequest / Save) de la core-urile de game, tine datele
// in core::storage::SystemDataCache si raspunde cu DG::SYSTEM_DATA (LoadResponse).
// Citirea din MariaDB e asincrona (QID_SYSTEM_DATA_LOAD), scrierea e write-through
// (REPLACE INTO / DELETE), iar cache-ul e mereu versiunea cea mai noua.

#include <cstdint>

class CPeer;
class CQueryInfo;
struct _SQLMsg;

namespace db::systemdata
{
	// Din CClientManager::ProcessPackets, pentru GD::SYSTEM_DATA.
	void OnPacket(CPeer* peer, const char* data, uint32_t size);

	// Din CClientManager::AnalyzeQueryResult, pentru QID_SYSTEM_DATA_LOAD. Nu sterge qi.
	void OnLoadResult(_SQLMsg* msg, CQueryInfo* qi);

	// O data pe minut, din CClientManager::Process: elimina jucatorii inactivi din cache.
	void Update();
}
