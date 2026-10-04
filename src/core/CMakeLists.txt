# m2core: componentele comune (CORE), fara nicio dependenta de codul jocului.
# Le folosesc game, db (daca va fi nevoie) si testele unitare.
file(GLOB_RECURSE M2CORE_SOURCES CONFIGURE_DEPENDS "*.h" "*.cpp")

add_library(m2core STATIC ${M2CORE_SOURCES})

target_include_directories(m2core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(m2core PUBLIC spdlog)

if (WIN32)
	set_target_properties(m2core PROPERTIES FOLDER lib)
endif()
