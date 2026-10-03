#pragma once
// core/registry — ciclul de viata al sistemelor: inregistrare, config, pornire,
// reincarcare, oprire.
//
// Un sistem nou mosteneste core::registry::System<TConfig> si scrie doar:
//   - Read():    citeste config-ul in structura lui (validarea o face Reader-ul);
//   - OnStart(), OnStop(): ce face la pornire si la oprire;
//   - OnConfigReloaded(): optional, daca trebuie sa reactioneze la /sysreload.
//
// Reguli garantate de registry:
//   - config invalid -> sistemul NU porneste, motivul ajunge in log (canalul REGISTRY);
//   - reincarcare cu config invalid -> sistemul ramane pe config-ul vechi, nimic nu se strica;
//   - "enabled": false -> sistemul e oprit fara recompilare;
//   - oprirea se face in ordinea inversa pornirii.

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/config/Config.h"
#include "core/log/Log.h"

namespace core::registry
{
	enum class State
	{
		NotLoaded, // inregistrat, inca nepornit
		Disabled,  // config valid, "enabled": false
		Running,   // pornit
		Failed     // config invalid sau lipsa
	};

	const char* ToString(State state);

	// Canalul de log al unui sistem: numele cu majuscule ("daily_reward" -> "DAILY_REWARD").
	std::string ChannelName(std::string_view name);

	// Interfata pe care o vede registry-ul. Sistemele nu o implementeaza direct,
	// ci prin System<TConfig> de mai jos.
	class ISystem
	{
	public:
		virtual ~ISystem() = default;

		// Numele: litere mici, cifre si _ ("daily_reward"). Da numele fisierului
		// de config si al canalului de log (cu majuscule: "DAILY_REWARD").
		virtual std::string_view Name() const = 0;

		// Faza 1: citeste config-ul intr-o copie de rezerva. Problemele merg in Report.
		virtual void Stage(config::Reader& root) = 0;
		// Faza 2a: config-ul de rezerva devine cel activ (doar daca Report e curat).
		virtual void Commit() = 0;
		// Faza 2b: config-ul de rezerva e aruncat.
		virtual void Discard() = 0;

		virtual void Start() = 0;
		virtual void Stop() = 0;
		virtual void ConfigReloaded() = 0;
	};

	// Baza pentru un sistem cu config-ul de tip TConfig.
	template <typename TConfig>
	class System : public ISystem
	{
	public:
		void Stage(config::Reader& root) final
		{
			m_staged = TConfig{};
			Read(root, m_staged);
		}
		void Commit() final { m_active = m_staged; }
		void Discard() final { m_staged = TConfig{}; }

		void Start() final { OnStart(); }
		void Stop() final { OnStop(); }
		void ConfigReloaded() final { OnConfigReloaded(); }

	protected:
		// Config-ul activ, validat. Sistemul il citeste de aici de fiecare data.
		const TConfig& Config() const { return m_active; }

		// Canalul de log al sistemului.
		log::Channel& Log() const { return log::Get(ChannelName(Name())); }

		virtual void Read(config::Reader& root, TConfig& out) = 0;
		virtual void OnStart() {}
		virtual void OnStop() {}
		virtual void OnConfigReloaded() {}

	private:
		TConfig m_active{};
		TConfig m_staged{};
	};

	// Ce afiseaza /sysinfo despre un sistem.
	struct Status
	{
		std::string name;
		State state = State::NotLoaded;
		log::Level logLevel = log::Level::Info;
		std::string lastError; // gol daca ultima incarcare a reusit
	};

	class Registry
	{
	public:
		// pathFor: calea config-ului pentru un nume de sistem (implicit config::PathFor).
		explicit Registry(std::function<std::string(std::string_view)> pathFor = config::PathFor);
		// Destructorul NU opreste sistemele: game apeleaza StopAll() explicit la shutdown,
		// cat timp obiectele jocului inca exista.
		~Registry() = default;

		Registry(const Registry&) = delete;
		Registry& operator=(const Registry&) = delete;

		// Adauga un sistem. Refuza (si logheaza) nume invalide sau duplicate.
		bool Register(std::unique_ptr<ISystem> system);

		// Incarca config-ul fiecarui sistem si porneste ce e valid si activat.
		void StartAll();
		// Opreste tot ce ruleaza, in ordinea inversa inregistrarii.
		void StopAll();

		// /sysreload: reciteste config-ul unui sistem. false = nume necunoscut sau config invalid
		// (in al doilea caz sistemul ramane neschimbat). Motivul ajunge in errorOut, daca e dat.
		bool Reload(std::string_view name, std::string* errorOut = nullptr);

		std::vector<Status> List() const;
		const Status* Find(std::string_view name) const;

	private:
		struct Entry
		{
			std::unique_ptr<ISystem> system;
			Status status;
		};

		Entry* FindEntry(std::string_view name);
		// Incarca si valideaza; daca reuseste face Commit si intoarce config-ul comun.
		bool LoadConfig(Entry& entry, config::Common& common, std::string& error);
		void StartEntry(Entry& entry, const log::Ctx& ctx);
		void StopEntry(Entry& entry, const log::Ctx& ctx);

		std::function<std::string(std::string_view)> m_pathFor;
		std::vector<Entry> m_entries;
	};

	// Registry-ul global al procesului (cel folosit de game).
	Registry& Global();

	// true pentru nume de forma "daily_reward": a-z, 0-9, _, primul caracter litera, max 32.
	bool IsValidName(std::string_view name);
}
