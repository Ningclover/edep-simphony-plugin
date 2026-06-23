cmake_minimum_required(VERSION 3.18)
project(edep-simphony-plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths ──────────────────────────────────────────────────────────────────────
# These are machine-specific. Override at configure time, e.g.
#   cmake -B build -DOPTIC_GPU_ROOT=/path/to/optic-gpu -DHACK_LOCAL=/path/to/hack/local
# or export the matching environment variables before running cmake.
set(OPTIC_GPU_ROOT "$ENV{OPTIC_GPU_ROOT}" CACHE PATH "Root of the optic-gpu checkout (contains env/, edep-sim/, simphony/)")
if(NOT OPTIC_GPU_ROOT)
    message(FATAL_ERROR "OPTIC_GPU_ROOT is not set. Pass -DOPTIC_GPU_ROOT=... or export OPTIC_GPU_ROOT (see setup_env.example.sh).")
endif()

set(SPACK_VIEW   "${OPTIC_GPU_ROOT}/env/.spack-env/view"  CACHE PATH "spack view")
set(EDEPSIM_INST "${OPTIC_GPU_ROOT}/edep-sim/install"     CACHE PATH "edep-sim install prefix")
set(EICOPT_INST  "${OPTIC_GPU_ROOT}/simphony/install"  CACHE PATH "simphony (formerly eic-opticks) install prefix")
set(HACK_LOCAL   "$ENV{HACK_LOCAL}" CACHE PATH "local prefix with plog + OptiX headers")
if(NOT HACK_LOCAL)
    message(FATAL_ERROR "HACK_LOCAL is not set. Pass -DHACK_LOCAL=... or export HACK_LOCAL (plog + OptiX headers).")
endif()

list(APPEND CMAKE_PREFIX_PATH "${SPACK_VIEW}" "${EDEPSIM_INST}")

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Geant4 REQUIRED)
find_package(ROOT REQUIRED COMPONENTS Core Tree RIO)
find_package(EDepSim REQUIRED)

# ── Plugin library ────────────────────────────────────────────────────────────
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL OPTICKS_U4 STANDALONE)

add_library(edep-simphony-plugin SHARED
    src/SimphonyRunAction.cc
    src/SimphonyEventAction.cc
    src/SimphonyStepAction.cc
    src/SimphonyPhysicsSwap.cc
    src/SimphonyScintProcess.cc
    src/SimphonyCpuPhotonTracker.cc
    src/plugin_entry.cc
)

# eic-opticks/simphony include dirs (all headers installed under include/simphony/)
target_include_directories(edep-simphony-plugin PRIVATE
    ${EICOPT_INST}/include/simphony/sysrap
    ${EICOPT_INST}/include/simphony/CSG
    ${EICOPT_INST}/include/simphony/qudarap
    ${EICOPT_INST}/include/simphony/CSGOptiX
    ${EICOPT_INST}/include/simphony/gdxml
    ${EICOPT_INST}/include/simphony/u4
    ${EICOPT_INST}/include/simphony/g4cx
    ${HACK_LOCAL}/include        # plog
    ${EDEPSIM_INST}/include      # EDepSim headers
    /usr/local/cuda/include      # CUDA headers (vector_types.h etc.)
    src/
)

# eic-opticks compiled libraries
target_link_directories(edep-simphony-plugin PRIVATE ${EICOPT_INST}/lib)

target_link_libraries(edep-simphony-plugin PRIVATE
    # Geant4 + ROOT
    ${Geant4_LIBRARIES}
    ROOT::Core ROOT::Tree ROOT::RIO
    # edep-sim
    EDepSim::edepsim
    EDepSim::edepsim_io
    # eic-opticks (order matters for static deps)
    G4CX
    U4
    CSGOptiX
    QUDARap
    CSG
    SysRap
    # CUDA runtime
    cuda
)

install(TARGETS edep-simphony-plugin LIBRARY DESTINATION lib)
