#pragma once
// core/storage/SystemDataCache — cache-ul datelor per jucator al sistemelor, in procesul db.
//
// db e singurul proprietar al datelor: game cere datele la intrarea in joc (LoadRequest)
// si le trimite inapoi cand se schimba (Save). Cache-ul e mereu versiunea cea mai noua,
// deci un LoadRequest venit dupa un Save (ex. warp de pe core-ul A pe core-ul B) primeste
// datele salvate, chiar daca scrierea in MariaDB inca nu s-a terminat.
//
// Clasa nu stie de SQL si nici de retea: le primeste ca functii (Callbacks), ca sa poata
// fi testata singura. db leaga queryLoad / persist / reply de DBManager si de peer-i.
//
// Reguli:
//   - un blob gol inseamna "sterge datele sistemului pentru acest jucator";
//   - datele salvate cat timp se incarca din DB sunt mai noi si castiga la combinare;
//   - eliminarea din cache e sigura oricand: fiecare Save a fost deja trimis spre DB.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::storage
{
	using Blob = std::string;
	using SystemMap = std::map<uint32_t, Blob>; // system_id -> date
	using Clock = std::chrono::steady_clock;

	// Cine a cerut datele (in db: identificatorul peer-ului de game). 0 nu e valid.
	using Requester = uint64_t;

	// Limita unui blob; peste ea, Save e refuzat.
	constexpr size_t kMaxBlobSize = 64 * 1024;

	class SystemDataCache
	{
	public:
		struct Callbacks
		{
			// Porneste citirea din DB a tuturor sistemelor pentru pid; raspunsul vine prin OnLoadResult/OnLoadFailed.
			std::function<void(uint32_t pid)> queryLoad;
			// Scrie (sau, cu blob gol, sterge) datele unui sistem in DB.
			std::function<void(uint32_t pid, uint32_t system, const Blob& data)> persist;
			// Trimite datele catre cel care le-a cerut. ok = false: incarcarea a esuat.
			std::function<void(Requester to, uint32_t pid, const SystemMap& data, bool ok)> reply;
		};

		explicit SystemDataCache(Callbacks callbacks);

		void OnLoadRequest(Requester from, uint32_t pid, Clock::time_point now);
		void OnLoadResult(uint32_t pid, SystemMap fromDb, Clock::time_point now);
		void OnLoadFailed(uint32_t pid);

		// false daca blob-ul depaseste kMaxBlobSize (nimic nu se schimba).
		bool OnSave(uint32_t pid, uint32_t system, Blob data, Clock::time_point now);

		// Elimina jucatorii neaccesati de cel putin `idle` si fara cereri in asteptare. Intoarce cati.
		size_t Evict(Clock::time_point now, Clock::duration idle);

		// Un peer de game s-a deconectat: nu-i mai trimitem raspunsuri.
		void ForgetRequester(Requester requester);

		size_t Size() const { return m_players.size(); }
		bool IsLoaded(uint32_t pid) const;

	private:
		enum class State
		{
			Partial, // are doar date salvate, nu a fost inca incarcat din DB
			Loading, // citirea din DB e in curs
			Loaded   // contine tot ce e in DB plus salvarile ulterioare
		};

		struct Player
		{
			State state = State::Partial;
			SystemMap data;
			std::vector<Requester> waiting;
			Clock::time_point lastAccess{};
		};

		Callbacks m_callbacks;
		std::unordered_map<uint32_t, Player> m_players;
	};
}
