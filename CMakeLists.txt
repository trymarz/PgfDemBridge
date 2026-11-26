# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(PgfDemBridge VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # linked into OpenFOAM and Yade shared libraries
option(PGFDB_WERROR "Treat warnings as errors" OFF)
set(PGFDB_MPIEXEC_PREFLAGS "" CACHE STRING "Extra mpiexec flags for the tests, e.g. --oversubscribe")

find_package(MPI 3.1 REQUIRED COMPONENTS CXX)

add_library(PgfDemBridge STATIC src/PgfDemBridge.cpp)
target_include_directories(PgfDemBridge PUBLIC src)
target_link_libraries(PgfDemBridge PUBLIC MPI::MPI_CXX)
target_compile_options(PgfDemBridge PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${PGFDB_WERROR}>:-Werror>)

include(CTest)
if(BUILD_TESTING)
    foreach(t unitTests mockFoam mockDem)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} PRIVATE PgfDemBridge)
        target_compile_options(${t} PRIVATE -Wall -Wextra $<$<BOOL:${PGFDB_WERROR}>:-Werror>)
    endforeach()
    add_test(NAME unit COMMAND unitTests)

    separate_arguments(preflags UNIX_COMMAND "${PGFDB_MPIEXEC_PREFLAGS}")
    # pgf_mpi_test(<name> <nDem> <nFoam> <demArgs> <foamArgs...>): launch option B via tests/role.sh.
    # Set foamFirst to put that many Foam ranks before the DEM ranks.
    set(foamFirst 0)
    function(pgf_mpi_test name ndem nfoam demargs)
        math(EXPR np "${ndem} + ${nfoam}")
        add_test(NAME ${name} COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${np} ${preflags}
            env NDEM=${ndem} NFOAMFIRST=${foamFirst} MOCKDEM=$<TARGET_FILE:mockDem> "DEMARGS=${demargs}"
            sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/role.sh $<TARGET_FILE:mockFoam> ${ARGN})
        set_tests_properties(${name} PROPERTIES TIMEOUT 60)
    endfunction()

    foreach(layout "1 1" "2 2" "1 3" "3 1")
        separate_arguments(l UNIX_COMMAND "${layout}")
        list(GET l 0 nd)
        list(GET l 1 nf)
        pgf_mpi_test(rate_${nd}x${nf} ${nd} ${nf} "" -mpi-threads -mode 0)
        pgf_mpi_test(apparentVolume_${nd}x${nf} ${nd} ${nf} "" -mpi-threads -mode 1)
    endforeach()
    pgf_mpi_test(closedSlabs_2x2 2 2 "-closed" -mpi-threads -closed)
    pgf_mpi_test(closedSlabs_1x3 1 3 "-closed" -mpi-threads -closed)
    pgf_mpi_test(demStopsEarly_2x2 2 2 "-stop-at 3" -mpi-threads -steps 8)
    pgf_mpi_test(foamStopsEarly_2x2 2 2 "" -mpi-threads -steps 8 -stop-at 3)
    pgf_mpi_test(stopAtStart_1x1 1 1 "-stop-at 0" -mpi-threads)
    pgf_mpi_test(oneStep_2x1 2 1 "" -mpi-threads -steps 1)
    pgf_mpi_test(oneDemRankStops_2x2 2 2 "-stop-at 2 -stop-rank 0" -mpi-threads -steps 8)
    set(foamFirst 2)
    pgf_mpi_test(foamFirst_2x2 2 2 "" -mpi-threads -mode 1)
    set(foamFirst 1)
    pgf_mpi_test(interleaved_2x2 2 2 "-closed" -mpi-threads -closed)
    set(foamFirst 0)

    # Handshake failures must abort with a clear message, never hang.
    pgf_mpi_test(modeMismatch_1x1 1 1 "-expect-mode 1" -mpi-threads -mode 0)
    pgf_mpi_test(threadMismatch_1x1 1 1 "" -mode 0)
    pgf_mpi_test(noDemRank_0x2 0 2 "" -mpi-threads)
    set_tests_properties(modeMismatch_1x1 PROPERTIES PASS_REGULAR_EXPRESSION "lambdaMode mismatch")
    set_tests_properties(threadMismatch_1x1 PROPERTIES PASS_REGULAR_EXPRESSION "thread level mismatch")
    set_tests_properties(noDemRank_0x2 PROPERTIES PASS_REGULAR_EXPRESSION "at least one Foam and one DEM")
endif()
