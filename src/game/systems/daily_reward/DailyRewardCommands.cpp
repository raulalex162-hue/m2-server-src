// /daily_test <show|claim|next|skip|reset>  (GM) — testarea daily_reward fara fereastra din client.
//   show   starea ta: ultima zi revendicata, seria, daca poti revendica azi si ce zi din ciclu urmeaza
//   claim  revendica, exact ca butonul din fereastra (aceleasi verificari, prin RewardService)
//   next   simuleaza trecerea unei zile: ultima revendicare devine "ieri" (seria continua)
//   skip   simuleaza o zi ratata: ultima revendicare devine "alaltaieri" (seria se rupe)
//   reset  sterge starea ta (ca un jucator care n-a revendicat niciodata)

// Fisierul sta in src/game/systems/daily_reward, deci headerele jocului se includ cu "../../".
#include "../../stdafx.h"

#include <string>

#include "../../char.h"
#include "../../cmd.h"
#include "../../utils.h"

#include "core/time/Calendar.h"

#include "../../services/GameTime.h"
#include "DailyReward.h"

namespace
{
	using namespace game::systems::daily_reward;

	void Show(CHARACTER* ch, DailyReward& daily)
	{
		State state;
		if (!daily.LoadState(ch->GetPlayerID(), state))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, "[daily] datele tale inca nu au sosit");
			return;
		}
		const int64_t today = game::gametime::DayKeyNow();
		const View view = Evaluate(state, today, daily.CurrentRules());
		const std::string last = state.lastClaimDay == kNeverClaimed ? "niciodata" : core::time::FormatDayKey(state.lastClaimDay);

		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] azi: %s | ultima revendicare: %s | serie: %u | total: %u",
			core::time::FormatDayKey(today).c_str(), last.c_str(), state.streak, state.totalClaims);
		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] poate revendica: %s (%s) | ziua din ciclu: %u%s",
			view.canClaim ? "da" : "nu", ToString(view.reason), view.dayIndex, view.streakBroken ? " | seria s-a rupt" : "");
	}

	// Muta ultima revendicare cu `days` zile in urma.
	void Shift(CHARACTER* ch, DailyReward& daily, int64_t days, const char* what)
	{
		State state;
		if (!daily.LoadState(ch->GetPlayerID(), state))
		{
			ch->ChatPacket(CHAT_TYPE_INFO, "[daily] datele tale inca nu au sosit");
			return;
		}
		if (state.lastClaimDay == kNeverClaimed)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, "[daily] n-ai revendicat inca nimic; foloseste intai /daily_test claim");
			return;
		}
		state.lastClaimDay -= days;
		daily.SaveState(ch->GetPlayerID(), state);
		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] %s", what);
		Show(ch, daily);
	}
}

ACMD(do_daily_test)
{
	DailyReward* daily = Instance();
	if (!daily)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] sistemul daily_reward e oprit");
		return;
	}

	char arg[64] = {};
	one_argument(argument, arg, sizeof(arg));
	const std::string sub = arg;

	if (sub == "show" || sub.empty())
		Show(ch, *daily);
	else if (sub == "claim")
	{
		daily->HandleClaim(ch);
		Show(ch, *daily);
	}
	else if (sub == "next")
		Shift(ch, *daily, 1, "simulat: ultima revendicare a fost ieri");
	else if (sub == "skip")
		Shift(ch, *daily, 2, "simulat: o zi ratata");
	else if (sub == "reset")
	{
		daily->SaveState(ch->GetPlayerID(), State{});
		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] starea ta a fost stearsa");
		Show(ch, *daily);
	}
	else
		ch->ChatPacket(CHAT_TYPE_INFO, "[daily] folosire: /daily_test <show|claim|next|skip|reset>");
}
