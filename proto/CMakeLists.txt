# m2proto: codul C++ generat din fisierele .proto.
#   proto/m2/      protocolul cu clientul (aceleasi fisiere si in client; intra in hash)
#   proto/server/  mesaje interne ale serverului, ex. game <-> db (doar pe server; NU intra in hash)

file(GLOB_RECURSE M2_PROTO_PUBLIC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/m2/*.proto")
file(GLOB_RECURSE M2_PROTO_SERVER CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/server/*.proto")
list(SORT M2_PROTO_PUBLIC)
list(SORT M2_PROTO_SERVER)
set(M2_PROTO_FILES ${M2_PROTO_PUBLIC} ${M2_PROTO_SERVER})

set(M2_PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${M2_PROTO_GEN_DIR}/m2")

add_library(m2proto STATIC ${M2_PROTO_FILES})

protobuf_generate(
	TARGET m2proto
	LANGUAGE cpp
	IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
	PROTOC_OUT_DIR "${M2_PROTO_GEN_DIR}"
)

target_include_directories(m2proto PUBLIC "${M2_PROTO_GEN_DIR}")
target_link_libraries(m2proto PUBLIC protobuf::libprotobuf-lite)

# --- Hash-ul protocolului (doar proto/m2/) ---
# Calculat din numele si continutul fisierelor, cu finalurile de linie normalizate
# (Git pe Windows poate transforma LF in CRLF; hash-ul trebuie sa iasa la fel).
set(M2_PROTO_ALL "")
foreach(proto_file ${M2_PROTO_PUBLIC})
	file(READ "${proto_file}" proto_text)
	string(REPLACE "\r\n" "\n" proto_text "${proto_text}")
	file(RELATIVE_PATH proto_name "${CMAKE_CURRENT_SOURCE_DIR}" "${proto_file}")
	string(APPEND M2_PROTO_ALL "${proto_name}\n${proto_text}\n")
endforeach()
string(SHA256 M2_PROTO_SHA "${M2_PROTO_ALL}")
string(SUBSTRING "${M2_PROTO_SHA}" 0 16 M2_PROTOCOL_HASH)

configure_file(ProtocolHash.h.in "${M2_PROTO_GEN_DIR}/m2/ProtocolHash.h" @ONLY)
# CMake se reconfigureaza (si recalculeaza hash-ul) cand se schimba un fisier .proto.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${M2_PROTO_FILES})

list(LENGTH M2_PROTO_SERVER M2_PROTO_SERVER_COUNT)
message(STATUS "m2proto: ${CMAKE_CURRENT_LIST_DIR} -> hash protocol ${M2_PROTOCOL_HASH} (+${M2_PROTO_SERVER_COUNT} fisiere interne)")

if (WIN32)
	set_target_properties(m2proto PROPERTIES FOLDER lib)
endif()
