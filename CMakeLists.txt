cmake_minimum_required(VERSION 3.18)
project(edep-opticks-plugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths ──────────────────────────────────────────────────────────────────────
set(SPACK_VIEW   "/nfs/data/1/xning/optic-gpu/env/.spack-env/view")
set(EDEPSIM_INST "/nfs/data/1/xning/optic-gpu/edep-sim/install")
set(EICOPT_INST  "/nfs/data/1/xning/optic-gpu/eic-opticks/install")
set(HACK_LOCAL   "/nfs/data/1/xning/edep_optic/hack/local")   # plog, OptiX headers

list(APPEND CMAKE_PREFIX_PATH "${SPACK_VIEW}" "${EDEPSIM_INST}")

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Geant4 REQUIRED)
find_package(ROOT REQUIRED COMPONENTS Core Tree RIO)
find_package(EDepSim REQUIRED)

# ── Plugin library ────────────────────────────────────────────────────────────
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL OPTICKS_U4 STANDALONE)

add_library(edep-opticks-plugin SHARED
    src/OpticsRunAction.cc
    src/OpticsEventAction.cc
    src/OpticsStepAction.cc
    src/OpticsPhysicsSwap.cc
    src/plugin_entry.cc
)

# eic-opticks include dirs (all headers installed under include/eic-opticks/)
target_include_directories(edep-opticks-plugin PRIVATE
    ${EICOPT_INST}/include/eic-opticks/sysrap
    ${EICOPT_INST}/include/eic-opticks/CSG
    ${EICOPT_INST}/include/eic-opticks/qudarap
    ${EICOPT_INST}/include/eic-opticks/CSGOptiX
    ${EICOPT_INST}/include/eic-opticks/gdxml
    ${EICOPT_INST}/include/eic-opticks/u4
    ${EICOPT_INST}/include/eic-opticks/g4cx
    ${HACK_LOCAL}/include        # plog
    ${EDEPSIM_INST}/include      # EDepSim headers
    /usr/local/cuda/include      # CUDA headers (vector_types.h etc.)
    src/
)

# eic-opticks compiled libraries
target_link_directories(edep-opticks-plugin PRIVATE ${EICOPT_INST}/lib)

target_link_libraries(edep-opticks-plugin PRIVATE
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

install(TARGETS edep-opticks-plugin LIBRARY DESTINATION lib)
