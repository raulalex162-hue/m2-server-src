#pragma once
// core/config — citirea si validarea config-urilor JSON ale sistemelor.
//
// Fiecare sistem are share/conf/systems/<sistem>.json (vazut de core ca
// conf/systems/<sistem>.json). Regula: un config invalid NU porneste sistemul,
// iar raportul spune exact ce camp e gresit, de exemplu:
//   rewards[2].count: valoarea 0 e in afara intervalului [1, 200]
//
// Utilizare:
//   core::config::Report report;
//   auto json = core::config::LoadFile(core::config::PathFor("daily_reward"), report);
//   if (json) {
//       core::config::Reader root(*json, report);
//       auto common = core::config::ReadCommon(root);
//       for (auto& r : root.RequireArray("rewards", 7)) { ... r.RequireInt("vnum", 1, 999999) ... }
//       root.RejectUnknown({ "enabled", "log_level", "rewards" });
//   }
//   if (!report.Ok()) { /* sistemul nu porneste; report.Summary() merge in log */ }
//
// Comentariile // si /* */ sunt permise in fisiere, pentru documentarea valorilor.

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/log/Log.h"

namespace core::config
{
	using Json = nlohmann::json;

	// O problema gasita in config: unde (calea campului) si ce.
	struct Issue
	{
		std::string path;    // ex. "rewards[2].count"; gol = fisierul intreg
		std::string message;
	};

	// Aduna toate problemele unui config, ca sa le vedem pe toate deodata.
	class Report
	{
	public:
		void Add(std::string path, std::string message);
		bool Ok() const { return m_issues.empty(); }
		const std::vector<Issue>& Issues() const { return m_issues; }
		// Toate problemele, cate una pe linie, gata de pus in log.
		std::string Summary() const;

	private:
		std::vector<Issue> m_issues;
	};

	// Citeste un nod JSON si raporteaza orice abatere in Report.
	// La eroare intoarce o valoare neutra (false, 0, "") si noteaza problema;
	// apelantul verifica Report::Ok() la final, inainte sa foloseasca valorile.
	class Reader
	{
	public:
		Reader(const Json& node, Report& report, std::string path = "");

		const std::string& Path() const { return m_path; }
		bool Has(std::string_view key) const;

		bool RequireBool(std::string_view key);
		int64_t RequireInt(std::string_view key, int64_t min, int64_t max);
		std::string RequireString(std::string_view key, bool allowEmpty = false);

		bool OptionalBool(std::string_view key, bool fallback);
		int64_t OptionalInt(std::string_view key, int64_t fallback, int64_t min, int64_t max);
		std::string OptionalString(std::string_view key, std::string fallback);

		// Un obiect imbricat; std::nullopt daca lipseste sau nu e obiect.
		std::optional<Reader> RequireObject(std::string_view key);
		// O lista de obiecte; fiecare element devine un Reader cu calea "key[i]".
		std::vector<Reader> RequireArray(std::string_view key, size_t minCount = 0);

		// Orice cheie care nu e in lista e raportata (prinde greseli ca "enabeld").
		void RejectUnknown(std::initializer_list<std::string_view> allowed);

		// Pentru reguli proprii ale unui sistem (ex. "zilele trebuie sa fie 1..7, fara dubluri").
		void Fail(std::string_view key, std::string message);

	private:
		std::string KeyPath(std::string_view key) const;
		const Json* Find(std::string_view key) const;
		const Json* Required(std::string_view key);

		const Json* m_node;
		Report* m_report;
		std::string m_path;
	};

	// Campurile comune tuturor sistemelor.
	struct Common
	{
		bool enabled = false;
		log::Level logLevel = log::Level::Info;
	};

	// "enabled" (obligatoriu) si "log_level" (optional, implicit "info").
	Common ReadCommon(Reader& root);

	// Calea config-ului unui sistem, relativa la folderul core-ului.
	std::string PathFor(std::string_view system);

	// Citeste si parseaza un fisier; la eroare noteaza problema (cu linia) si intoarce nullopt.
	std::optional<Json> LoadFile(const std::string& path, Report& report);

	// La fel, dintr-un text (folosit de teste).
	std::optional<Json> Parse(std::string_view text, Report& report);
}
