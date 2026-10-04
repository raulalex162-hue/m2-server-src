// Comenzile pentru recompense (declarate si inregistrate in cmd.cpp):
//   /reward                              orice jucator: livreaza din cutia de recompense ce incape acum
//   /reward_test <vnum> <count> [gold]   GM: da o recompensa de test prin RewardService (sursa "gm_test")

// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../char.h"
#include "../cmd.h"

#include "core/log/Log.h"
#include "core/reward/Reward.h"
#include "Reward.h"

namespace
{
	std::vector<std::string> SplitArgs(const char* argument)
	{
		std::vector<std::string> args;
		std::istringstream in(argument ? argument : "");
		std::string word;
		while (in >> word)
			args.push_back(word);
		return args;
	}

	bool ParseNumber(const std::string& text, uint64_t& out)
	{
		if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 12)
			return false;
		out = std::strtoull(text.c_str(), nullptr, 10);
		return true;
	}

	// O comanda pe care o poate folosi orice jucator are nevoie de o pauza minima.
	// (Mutat in core/guard cand acesta va exista.)
	bool CooldownPassed(uint32_t pid, std::chrono::milliseconds cooldown)
	{
		static std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> last;
		const auto now = std::chrono::steady_clock::now();
		auto it = last.find(pid);
		if (it != last.end() && now - it->second < cooldown)
			return false;
		last[pid] = now;
		if (last.size() > 4096) // curatenie simpla: jucatorii vechi nu raman la nesfarsit
			last.clear();
		return true;
	}
}

ACMD(do_reward)
{
	if (!CooldownPassed(ch->GetPlayerID(), std::chrono::seconds(3)))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] Asteapta cateva secunde.");
		return;
	}

	const core::log::Ctx ctx{ core::log::NewTrace(), ch->GetPlayerID() };
	const int waiting = game::reward::DeliverPending(ch, ctx);
	if (waiting < 0)
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] Datele tale inca se incarca. Incearca din nou peste cateva secunde.");
	else if (waiting == 0)
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] Nu mai ai recompense in asteptare.");
	else
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] Inca %d recompense asteapta loc in inventar.", waiting);
}

ACMD(do_reward_test)
{
	const auto args = SplitArgs(argument);
	uint64_t vnum = 0, count = 1, gold = 0;
	if (args.size() < 2 || args.size() > 3 || !ParseNumber(args[0], vnum) || !ParseNumber(args[1], count)
		|| (args.size() == 3 && !ParseNumber(args[2], gold)))
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] folosire: /reward_test <vnum> <count> [gold]");
		return;
	}

	core::reward::Reward reward;
	if (vnum > 0)
	{
		if (count < 1 || count > core::reward::kMaxItemCount)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, "[reward] count trebuie sa fie intre 1 si %u", core::reward::kMaxItemCount);
			return;
		}
		std::string why;
		if (!game::reward::ItemProtoValidator()(static_cast<uint32_t>(vnum), why))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, "[reward] item %llu: %s", static_cast<unsigned long long>(vnum), why.c_str());
			return;
		}
		reward.items.push_back(core::reward::Item{ static_cast<uint32_t>(vnum), static_cast<uint32_t>(count) });
	}
	if (gold > core::reward::kMaxGold)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "[reward] gold prea mare");
		return;
	}
	reward.gold = gold;

	const core::log::Ctx ctx{ core::log::NewTrace(), ch->GetPlayerID() };
	const auto result = game::reward::Grant(ch, reward, "gm_test", ctx);
	ch->ChatPacket(CHAT_TYPE_INFO, "[reward] %s%s%s", game::reward::ToString(result.status),
		result.reason[0] ? ": " : "", result.reason);
}
