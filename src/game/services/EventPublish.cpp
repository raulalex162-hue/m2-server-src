// Fisierul sta in src/game/services, deci headerele jocului se includ cu "../".
#include "../stdafx.h"
#include "EventPublish.h"

#include "../char.h"
#include "../desc.h"

#include "core/events/Bus.h"
#include "core/events/GameEvents.h"

namespace game::events
{
	void PublishEnterGame(CHARACTER* ch)
	{
		if (!ch || !ch->IsPC())
			return;

		core::events::EnterGame e;
		e.pid = ch->GetPlayerID();
		e.accountId = ch->GetDesc() ? ch->GetDesc()->GetAccountTable().id : 0;
		e.mapIndex = static_cast<int32_t>(ch->GetMapIndex());
		e.level = static_cast<uint32_t>(ch->GetLevel());
		e.empire = ch->GetEmpire();

		core::events::Global().Publish(e, core::log::Ctx{ core::log::NewTrace(), e.pid });
	}

	void PublishLeaveGame(CHARACTER* ch)
	{
		if (!ch || !ch->IsPC())
			return;

		core::events::LeaveGame e;
		e.pid = ch->GetPlayerID();
		e.mapIndex = static_cast<int32_t>(ch->GetMapIndex());

		core::events::Global().Publish(e, core::log::Ctx{ core::log::NewTrace(), e.pid });
	}

	void PublishMobKill(CHARACTER* killer, CHARACTER* mob)
	{
		if (!killer || !mob || !killer->IsPC() || mob->IsPC())
			return;

		core::events::MobKill e;
		e.killerPid = killer->GetPlayerID();
		e.mobVnum = mob->GetRaceNum();
		e.mobLevel = static_cast<uint32_t>(mob->GetLevel());
		e.mobRank = mob->GetMobRank();
		e.mapIndex = static_cast<int32_t>(mob->GetMapIndex());
		e.inDungeon = mob->GetDungeon() != nullptr;

		core::events::Global().Publish(e, core::log::Ctx{ core::log::NewTrace(), e.killerPid });
	}
}
