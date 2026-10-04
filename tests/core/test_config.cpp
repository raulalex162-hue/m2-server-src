#include <doctest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "core/config/Config.h"

using core::config::Parse;
using core::config::Reader;
using core::config::Report;

namespace
{
	// Adevarat daca raportul contine o problema la calea data.
	bool HasIssue(const Report& report, const std::string& path)
	{
		for (const auto& issue : report.Issues())
			if (issue.path == path)
				return true;
		return false;
	}
}

TEST_CASE("config: un fisier valid se citeste fara probleme")
{
	Report report;
	auto json = Parse(R"({
		// comentariile sunt permise
		"enabled": true,
		"log_level": "debug",
		"max_claims": 3,
		"title": "Recompensa zilnica",
		"rewards": [ { "vnum": 50850, "count": 2 }, { "vnum": 27884, "count": 4 } ]
	})", report);
	REQUIRE(json);

	Reader root(*json, report);
	const auto common = core::config::ReadCommon(root);
	CHECK(common.enabled);
	CHECK(common.logLevel == core::log::Level::Debug);
	CHECK(root.RequireInt("max_claims", 1, 10) == 3);
	CHECK(root.RequireString("title") == "Recompensa zilnica");

	auto rewards = root.RequireArray("rewards", 1);
	REQUIRE(rewards.size() == 2);
	CHECK(rewards[1].RequireInt("vnum", 1, 999999) == 27884);
	CHECK(rewards[1].Path() == "rewards[1]");

	root.RejectUnknown({ "enabled", "log_level", "max_claims", "title", "rewards" });
	CHECK(report.Ok());
}

TEST_CASE("config: camp obligatoriu lipsa e raportat cu calea lui")
{
	Report report;
	auto json = Parse(R"({ "log_level": "info" })", report);
	REQUIRE(json);

	Reader root(*json, report);
	core::config::ReadCommon(root);
	CHECK_FALSE(report.Ok());
	CHECK(HasIssue(report, "enabled"));
}

TEST_CASE("config: tipul gresit e raportat")
{
	Report report;
	auto json = Parse(R"({ "enabled": "da", "count": "5" })", report);
	REQUIRE(json);

	Reader root(*json, report);
	CHECK(root.RequireBool("enabled") == false);
	CHECK(root.RequireInt("count", 0, 10) == 0);
	CHECK(HasIssue(report, "enabled"));
	CHECK(HasIssue(report, "count"));
}

TEST_CASE("config: valoare in afara intervalului, intr-o lista imbricata")
{
	Report report;
	auto json = Parse(R"({ "rewards": [ { "count": 2 }, { "count": 0 } ] })", report);
	REQUIRE(json);

	Reader root(*json, report);
	auto rewards = root.RequireArray("rewards");
	REQUIRE(rewards.size() == 2);
	CHECK(rewards[0].RequireInt("count", 1, 200) == 2);
	CHECK(rewards[1].RequireInt("count", 1, 200) == 0);
	CHECK(HasIssue(report, "rewards[1].count"));
	CHECK_FALSE(HasIssue(report, "rewards[0].count"));
}

TEST_CASE("config: o lista prea scurta e raportata")
{
	Report report;
	auto json = Parse(R"({ "days": [ {}, {} ] })", report);
	REQUIRE(json);

	Reader root(*json, report);
	root.RequireArray("days", 7);
	CHECK(HasIssue(report, "days"));
}

TEST_CASE("config: campurile necunoscute sunt raportate")
{
	Report report;
	auto json = Parse(R"({ "enabeld": true })", report);
	REQUIRE(json);

	Reader root(*json, report);
	root.RejectUnknown({ "enabled", "log_level" });
	CHECK(HasIssue(report, "enabeld"));
}

TEST_CASE("config: log_level necunoscut e raportat, campurile optionale au valori implicite")
{
	Report report;
	auto json = Parse(R"({ "enabled": false, "log_level": "verbose" })", report);
	REQUIRE(json);

	Reader root(*json, report);
	const auto common = core::config::ReadCommon(root);
	CHECK_FALSE(common.enabled);
	CHECK(HasIssue(report, "log_level"));
	CHECK(root.OptionalInt("cooldown", 30, 0, 3600) == 30);
	CHECK(root.OptionalBool("announce", true) == true);
	CHECK(root.OptionalString("title", "implicit") == "implicit");
}

TEST_CASE("config: JSON invalid e raportat cu linia")
{
	Report report;
	auto json = Parse("{\n  \"enabled\": true,\n  \"count\": ,\n}", report);
	CHECK_FALSE(json);
	REQUIRE(report.Issues().size() == 1);
	CHECK(report.Issues()[0].message.find("linia 3") != std::string::npos);
}

TEST_CASE("config: fisier inexistent e raportat, fisier cu BOM se citeste")
{
	Report missing;
	CHECK_FALSE(core::config::LoadFile("nu_exista_niciodata.json", missing));
	CHECK_FALSE(missing.Ok());

	const char* path = "test_config_bom.json";
	{
		std::ofstream out(path, std::ios::binary);
		out << "\xEF\xBB\xBF{ \"enabled\": true }";
	}
	Report report;
	auto json = core::config::LoadFile(path, report);
	std::remove(path);
	REQUIRE(json);
	CHECK(report.Ok());
}

TEST_CASE("config: PathFor construieste calea relativa la core")
{
	CHECK(core::config::PathFor("daily_reward") == "conf/systems/daily_reward.json");
}
