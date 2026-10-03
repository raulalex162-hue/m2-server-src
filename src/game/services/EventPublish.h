#pragma once
// game/services/EventPublish — singurul loc in care codul jocului construieste
// evenimentele din core/events. Codul de baza (char.cpp, input_db.cpp,
// char_battle.cpp) apeleaza doar aceste functii, cu cate o linie.

class CHARACTER;

namespace game::events
{
	// Dupa ce caracterul a intrat complet in joc (acolo unde quest-urile primesc "login").
	void PublishEnterGame(CHARACTER* ch);

	// Cand caracterul paraseste core-ul (acolo unde quest-urile primesc "logout").
	void PublishLeaveGame(CHARACTER* ch);

	// Cand un monstru moare si recompensa merge la killer (acolo unde quest-urile primesc "kill").
	void PublishMobKill(CHARACTER* killer, CHARACTER* mob);
}
