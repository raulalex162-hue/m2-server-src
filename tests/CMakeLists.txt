# Testele unitare ale CORE-ului, ale mesajelor si ale sistemelor (fara DB, fara retea).
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(core_tests ${TEST_SOURCES})
target_link_libraries(core_tests PRIVATE m2core m2proto)

if (NOT WIN32)
	target_link_libraries(core_tests PRIVATE pthread)
endif()

add_test(NAME core_tests COMMAND core_tests)
