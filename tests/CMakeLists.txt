# Testele unitare ale CORE-ului si ale sistemelor (fara DB, fara retea).
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(core_tests ${TEST_SOURCES})
target_link_libraries(core_tests PRIVATE m2core)

if (NOT WIN32)
	target_link_libraries(core_tests PRIVATE pthread)
endif()

add_test(NAME core_tests COMMAND core_tests)
