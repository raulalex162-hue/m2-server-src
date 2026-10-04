#pragma once
// game/systems — lista sistemelor jocului si pornirea / oprirea lor.
//
// Un sistem nou se adauga intr-un singur loc: RegisterAll() din Systems.cpp.
// Ordinea din lista e ordinea de pornire (oprirea se face invers).

namespace game::systems
{
	// Apelat la finalul CInputDB::Boot, dupa ce toate datele de joc (item_proto, mob_proto, harti)
	// au sosit de la db. O singura data; un al doilea apel nu face nimic. Nu porneste pe core-ul de auth.
	void StartAll();

	// Apelat din main() la shutdown, dupa deconectarea jucatorilor si inainte de flush-ul catre db.
	void StopAll();
}
