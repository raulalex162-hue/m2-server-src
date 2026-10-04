#include <doctest.h>

#include <cctype>
#include <string>

#include "m2/ProtocolHash.h"
#include "m2/heartbeat.pb.h"
#include "m2/system_ids.pb.h"

TEST_CASE("proto: Ping se serializeaza si se citeste inapoi identic")
{
	m2::heartbeat::Ping ping;
	ping.set_client_time_ms(1234567890123ULL);
	ping.set_protocol_hash("abcdef0123456789");

	const std::string bytes = ping.SerializeAsString();
	CHECK_FALSE(bytes.empty());

	m2::heartbeat::Ping parsed;
	REQUIRE(parsed.ParseFromString(bytes));
	CHECK(parsed.client_time_ms() == 1234567890123ULL);
	CHECK(parsed.protocol_hash() == "abcdef0123456789");
}

TEST_CASE("proto: un mesaj gol e valid si ocupa 0 octeti")
{
	m2::heartbeat::Pong pong;
	CHECK(pong.SerializeAsString().empty());
	m2::heartbeat::Pong parsed;
	CHECK(parsed.ParseFromString(std::string()));
	CHECK_FALSE(parsed.protocol_ok());
}

TEST_CASE("proto: octetii invalizi sunt refuzati")
{
	m2::heartbeat::Ping parsed;
	// tag 1 (varint) urmat de un varint neterminat
	CHECK_FALSE(parsed.ParseFromString(std::string("\x08\xFF\xFF", 3)));
}

TEST_CASE("proto: identificatorii sistemelor si ai mesajelor sunt cei asteptati")
{
	CHECK(m2::SYSTEM_HEARTBEAT == 1);
	CHECK(m2::heartbeat::PING == 1);
	CHECK(m2::heartbeat::PONG == 2);
}

TEST_CASE("proto: hash-ul protocolului are 16 caractere hex")
{
	const std::string hash = m2::kProtocolHash;
	REQUIRE(hash.size() == 16);
	for (char c : hash)
		CHECK(std::isxdigit(static_cast<unsigned char>(c)));
}
