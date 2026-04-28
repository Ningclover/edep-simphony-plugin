#!/bin/bash
# setup_env.sh — source this before running edep-sim with the GPU optical plugin
#
# Usage: source /nfs/data/1/xning/optic-gpu/edep-opticks-plugin/setup_env.sh

# Activate the base spack environment (Geant4, ROOT, etc.)
source /nfs/data/1/xning/optic-gpu/.envrc

EICOPT_INST=/nfs/data/1/xning/optic-gpu/eic-opticks/install
EDEPSIM_INST=/nfs/data/1/xning/optic-gpu/edep-sim/install
PLUGIN_BUILD=/nfs/data/1/xning/optic-gpu/edep-opticks-plugin/build
OPENSSL_DIR=/wcwc/opt/builtin/linux-debian12-x86_64/gcc-12.2.0/openssl-3.3.1-gxvok6vlh4hlzckwlkubbeocv77ogi45/lib64
HACK_LOCAL=/nfs/data/1/xning/edep_optic/hack/local

# ── Runtime library path ──────────────────────────────────────────────────
# Note: hack/local is excluded — it has old conflicting Geant4/edepsim/eic-opticks
# libraries that override the correctly-versioned ones from the spack view and
# our own builds.
export LD_LIBRARY_PATH=\
${EDEPSIM_INST}/lib:\
${EICOPT_INST}/lib:\
${PLUGIN_BUILD}:\
/usr/local/cuda/lib64:\
${OPENSSL_DIR}:\
${LD_LIBRARY_PATH}

# ── Plugin shared library path (used in macro via $(PLUGIN_LIB)) ──────────
export PLUGIN_LIB=${PLUGIN_BUILD}/libedep-opticks-plugin.so

# Physics process swap: replaces G4Cerenkov/G4Scintillation with instrumented versions
# that call U4::CollectGenstep_*() → SEvt::AddGenstep() at each step.
export EXTRAPHYSICS="EXTERN:${PLUGIN_LIB}:CreatePhysicsConstructor"

# ── eic-opticks runtime config ────────────────────────────────────────────
# Mode 1 = GPU-only simulation (eic-opticks handles optical photons on GPU)
export OPTICKS_INTEGRATION_MODE=1

# PTX kernel for OptiX ray tracing
export OPTICKS_KEY=GeoChain.X4GeoChain.${HOSTNAME:-wcgpu1}   # may need adjustment
# Alternative: set the PTX path directly
export CSGOptiX__ptxpath=${EICOPT_INST}/lib/CSGOptiX7.ptx

# Output folder for eic-opticks event files (genstep.npy, hit.npy, etc.)
export OPTICKS_OUT_FOLD=/nfs/data/1/xning/tmp/opticks_output
mkdir -p ${OPTICKS_OUT_FOLD}

echo "[setup_env] Environment configured:"
echo "  PLUGIN_LIB      = ${PLUGIN_LIB}"
echo "  OPTICKS_INTEGRATION_MODE = ${OPTICKS_INTEGRATION_MODE}"
echo "  OPTICKS_OUT_FOLD = ${OPTICKS_OUT_FOLD}"
