// Fisierul sta in src/game/systems/daily_reward, deci headerele jocului se includ cu "../../".
#include "../../stdafx.h"
#include "DailyReward.h"

#include <chrono>

#include "../../char.h"
#include "../../char_manager.h"

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"
#include "core/time/Calendar.h"

#include "../../services/GameTime.h"
#include "../../services/NetMessages.h"
#include "../../services/PlayerData.h"
#include "../../services/Reward.h"

#include "ProtoBegin.h"
#include "m2/daily_reward.pb.h"
#include "m2/system_ids.pb.h"
#include "server/daily_reward_state.pb.h"
#include "ProtoEnd.h"

namespace game::systems::daily_reward
{
	namespace
	{
		namespace pb = m2::daily_reward;
		constexpr uint32_t kStorage = m2::SYSTEM_DAILY_REWARD;
		DailyReward* g_instance = nullptr;

		int64_t NowMs()
		{
			using namespace std::chrono;
			return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
		}

		void FillDay(pb::Day* day, uint32_t index, const core::reward::Reward& reward)
		{
			day->set_index(index);
			day->set_gold(reward.gold);
			for (const auto& item : reward.items)
			{
				auto* i = day->add_items();
				i->set_vnum(item.vnum);
				i->set_count(item.count);
			}
		}
	}

	DailyReward* Instance() { return g_instance; }

	void DailyReward::Read(core::config::Reader& root, DailyRewardConfig& out)
	{
		out.minLevel = root.OptionalInt("min_level", 10, 1, 250);
		out.resetOnMiss = root.OptionalBool("reset_on_miss", true);
		out.notifyOnLogin = root.OptionalBool("notify_on_login", true);
		out.openWindowOnLogin = root.OptionalBool("open_window_on_login", true);

		auto days = root.RequireArray("days", 1);
		if (days.size() > 31)
			root.Fail("days", "maxim 31 de zile intr-un ciclu, nu " + std::to_string(days.size()));
		const auto validator = game::reward::ItemProtoValidator();
		for (auto& day : days)
			out.days.push_back(core::reward::ReadReward(day, validator));

		root.RejectUnknown({ "enabled", "log_level", "min_level", "reset_on_miss", "notify_on_login",
			"open_window_on_login", "days" });
	}

	Rules DailyReward::CurrentRules() const
	{
		Rules rules;
		rules.cycleLength = static_cast<uint32_t>(Config().days.size());
		rules.resetOnMiss = Config().resetOnMiss;
		return rules;
	}

	bool DailyReward::LoadState(uint32_t pid, State& out) const
	{
		m2::server::daily_reward::State saved;
		if (!game::playerdata::Load(pid, kStorage, saved))
			return false;
		out.lastClaimDay = saved.claimed_ever() ? saved.last_claim_day() : kNeverClaimed;
		out.streak = saved.streak();
		out.totalClaims = saved.total_claims();
		return true;
	}

	bool DailyReward::SaveState(uint32_t pid, const State& state)
	{
		m2::server::daily_reward::State saved;
		if (state.lastClaimDay != kNeverClaimed)
		{
			saved.set_claimed_ever(true);
			saved.set_last_claim_day(state.lastClaimDay);
		}
		saved.set_streak(state.streak);
		saved.set_total_claims(state.totalClaims);
		// Salvare imediata: revendicarea nu trebuie sa depinda de salvarea periodica.
		return game::playerdata::Store(pid, kStorage, saved, true);
	}

	void DailyReward::SendState(CHARACTER* ch, bool autoOpen)
	{
		State state;
		if (!LoadState(ch->GetPlayerID(), state))
			return; // datele jucatorului inca nu au sosit; fereastra va cere din nou

		const int64_t today = game::gametime::DayKeyNow();
		const View view = Evaluate(state, today, CurrentRules());
		const bool levelTooLow = ch->GetLevel() < Config().minLevel;

		pb::State msg;
		for (size_t i = 0; i < Config().days.size(); ++i)
			FillDay(msg.add_days(), static_cast<uint32_t>(i + 1), Config().days[i]);
		msg.set_current_day(view.dayIndex);
		msg.set_can_claim(view.canClaim && !levelTooLow);
		msg.set_claimed_today(view.reason == Reason::AlreadyClaimedToday);
		msg.set_streak_broken(view.streakBroken);
		msg.set_streak(state.streak);
		msg.set_next_reset_unix(game::gametime::NextResetNow());
		msg.set_server_time_unix(core::time::Now());
		msg.set_min_level(static_cast<uint32_t>(Config().minLevel));
		msg.set_level_too_low(levelTooLow);
		msg.set_auto_open(autoOpen);

		game::net::SendMessage(ch, m2::SYSTEM_DAILY_REWARD, pb::STATE, msg);
	}

	void DailyReward::HandleClaim(CHARACTER* ch)
	{
		const uint32_t pid = ch->GetPlayerID();
		const core::log::Ctx ctx{ core::log::NewTrace(), pid };

		pb::ClaimResult result;
		auto finish = [&](bool ok, const char* reason) {
			result.set_ok(ok);
			result.set_reason(reason);
			if (!ok)
				++m_refused;
			Log().Info(ctx, "claim -> {} {}", ok ? "ok" : "refuzat", reason);
			game::net::SendMessage(ch, m2::SYSTEM_DAILY_REWARD, pb::CLAIM_RESULT, result);
			SendState(ch, false);
		};

		// O cerere pe secunda per jucator: dublu-click-ul sau un client modificat nu ajung la logica.
		const int64_t now = NowMs();
		if (auto it = m_lastClaimAttemptMs.find(pid); it != m_lastClaimAttemptMs.end() && now - it->second < 1000)
			return;
		m_lastClaimAttemptMs[pid] = now;

		if (ch->GetLevel() < Config().minLevel)
			return finish(false, "level_too_low");

		State state;
		if (!LoadState(pid, state))
			return finish(false, "data_not_ready");

		const int64_t today = game::gametime::DayKeyNow();
		const Rules rules = CurrentRules();
		const View view = Evaluate(state, today, rules);
		if (!view.canClaim)
			return finish(false, ToString(view.reason));

		const auto& reward = Config().days[view.dayIndex - 1];
		const auto grant = game::reward::Grant(ch, reward, "daily_reward", ctx);
		if (grant.status == game::reward::Status::Rejected)
		{
			++m_rejectedByReward;
			Log().Error(ctx, "RewardService a refuzat ziua {}: {} (starea NU avanseaza)", view.dayIndex, grant.reason);
			return finish(false, "reward_rejected");
		}

		const State next = AfterClaim(state, today, rules);
		SaveState(pid, next);
		++m_claims;
		if (grant.status == game::reward::Status::Queued)
			++m_queued;

		result.set_claimed_day(view.dayIndex);
		result.set_queued_to_mailbox(grant.status == game::reward::Status::Queued);
		Log().Info(ctx, "ziua {} revendicata (zi de joc {}, serie {}, total {})", view.dayIndex,
			core::time::FormatDayKey(today), next.streak, next.totalClaims);
		finish(true, "");
	}

	void DailyReward::OnStart()
	{
		g_instance = this;

		game::net::Handle<pb::Open>(m2::SYSTEM_DAILY_REWARD, pb::OPEN, "daily_reward",
			[this](CHARACTER* ch, const pb::Open&, const core::log::Ctx&) { SendState(ch, false); }, 16);

		game::net::Handle<pb::Claim>(m2::SYSTEM_DAILY_REWARD, pb::CLAIM, "daily_reward",
			[this](CHARACTER* ch, const pb::Claim&, const core::log::Ctx&) { HandleClaim(ch); }, 16);

		core::events::Global().Subscribe<core::events::SystemDataReady>("daily_reward",
			[this](const core::events::SystemDataReady& e, const core::log::Ctx&) {
				if (!Config().notifyOnLogin)
					return;
				CHARACTER* ch = CHARACTER_MANAGER::instance().FindByPID(e.pid);
				if (!ch || ch->GetLevel() < Config().minLevel)
					return;
				State state;
				if (!LoadState(e.pid, state))
					return;
				const View view = Evaluate(state, game::gametime::DayKeyNow(), CurrentRules());
				if (!view.canClaim)
					return;
				ch->ChatPacket(CHAT_TYPE_INFO, "[daily] Recompensa zilnica te asteapta (ziua %u din %zu).",
					view.dayIndex, Config().days.size());
				if (Config().openWindowOnLogin)
					SendState(ch, true);
			});

		core::events::Global().Subscribe<core::events::LeaveGame>("daily_reward",
			[this](const core::events::LeaveGame& e, const core::log::Ctx&) { m_lastClaimAttemptMs.erase(e.pid); });

		Log().Info(LifecycleCtx(), "pornit: {} zile, min_level={} reset_on_miss={}", Config().days.size(),
			Config().minLevel, Config().resetOnMiss);
	}

	void DailyReward::OnStop()
	{
		g_instance = nullptr;
		m_lastClaimAttemptMs.clear();
		Log().Info(LifecycleCtx(), "oprit: revendicari={} refuzate={}", m_claims, m_refused);
	}

	void DailyReward::Describe(std::vector<std::string>& lines) const
	{
		lines.push_back("zile in ciclu: " + std::to_string(Config().days.size()) + ", min_level=" + std::to_string(Config().minLevel)
			+ ", reset_on_miss=" + std::string(Config().resetOnMiss ? "true" : "false"));
		lines.push_back("revendicari: " + std::to_string(m_claims) + " (din care in cutie: " + std::to_string(m_queued)
			+ "), refuzate: " + std::to_string(m_refused) + ", refuzate de RewardService: " + std::to_string(m_rejectedByReward));
		lines.push_back("zi de joc: " + core::time::FormatDayKey(game::gametime::DayKeyNow()));
	}
}
