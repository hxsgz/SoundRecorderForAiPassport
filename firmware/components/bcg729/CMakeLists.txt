set(BCG_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/bcg729")
file(GLOB BCG_SOURCES "${BCG_ROOT}/src/*.c")
idf_component_register(SRCS ${BCG_SOURCES} INCLUDE_DIRS "${BCG_ROOT}/include" PRIV_INCLUDE_DIRS "${BCG_ROOT}/src")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -O2)
