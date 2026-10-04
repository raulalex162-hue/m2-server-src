#pragma once
// core/events/GameEvents.h — evenimentele de joc publicate de codul de baza.
//
// Reguli:
//   - contin doar identificatori si valori (pid, vnum, map), niciodata pointeri
//     la CHARACTER sau ITEM: un sistem care are nevoie de caracter il cauta el;
//   - se publica din exact aceleasi locuri in care m2dev anunta quest-urile,
//     deci au acelasi inteles ca "when login", "when logout", "when kill".

#include <cstdint>

namespace core::events
{
	// Caracterul a intrat complet in joc pe acest core (efectele sunt incarcate).
	// ATENTIE: se publica si la fiecare warp / schimbare de channel, nu doar la
	// primul login. "Prima intrare din zi" se decide din datele salvate, nu de aici.
	struct EnterGame
	{
		uint32_t pid = 0;
		uint32_t accountId = 0;
		int32_t mapIndex = 0;
		uint32_t level = 0;
		uint8_t empire = 0;
	};

	// Caracterul paraseste acest core (logout, iesire la selectie, warp, deconectare).
	struct LeaveGame
	{
		uint32_t pid = 0;
		int32_t mapIndex = 0;
	};

	// Un monstru a murit si recompensa merge la acest jucator (acelasi jucator
	// pe care il vad quest-urile la "when kill": cel care primeste exp-ul principal,
	// nu neaparat cel care a dat ultima lovitura). Nu se publica pentru monstrii
	// fara recompensa (atacati de garzi, in razboi de breasla pe camp).
	struct MobKill
	{
		uint32_t killerPid = 0;
		uint32_t mobVnum = 0;
		uint32_t mobLevel = 0;
		uint8_t mobRank = 0;  // 0 = normal ... boss / king, ca in mob_proto
		int32_t mapIndex = 0;
		bool inDungeon = false;
	};

	// Datele per jucator ale sistemelor (PlayerSystemData) au sosit de la db pe acest core.
	// Abia de acum un sistem poate citi si scrie datele jucatorului (game::playerdata::Load/Store).
	// Vine dupa EnterGame (de obicei la cateva milisecunde), la fiecare intrare pe un core.
	struct SystemDataReady
	{
		uint32_t pid = 0;
	};

	// Numele evenimentelor, pentru log.
	constexpr const char* NameOf(const EnterGame&) { return "EnterGame"; }
	constexpr const char* NameOf(const LeaveGame&) { return "LeaveGame"; }
	constexpr const char* NameOf(const MobKill&) { return "MobKill"; }
	constexpr const char* NameOf(const SystemDataReady&) { return "SystemDataReady"; }
}
