#!/bin/bash
# setup_env.example.sh — template for setup_env.sh
#
# Copy to setup_env.sh (which is .gitignored) and edit the paths in the
# "User-specific paths" block for your machine. Everything else should work
# unchanged.
#
# Usage after copying:  source ./setup_env.sh

# ── User-specific paths ────────────────────────────────────────────────────
# These point into your local checkout of optic-gpu and into the spack view
# that provides Geant4, ROOT, etc. Edit to match your machine.
PREFIX=/path/to/optic-gpu                                    # repo root
SPACK_ENV=${PREFIX}/.envrc                                   # spack activation
EICOPT_INST=${PREFIX}/eic-opticks/install
EDEPSIM_INST=${PREFIX}/edep-sim/install
PLUGIN_BUILD=${PREFIX}/edep-opticks-plugin/build

# OpenSSL is needed by ROOT on Debian 12 spack builds. Point this at your
# spack view's openssl libdir, or remove from LD_LIBRARY_PATH below if your
# system openssl is already on the path.
OPENSSL_DIR=/path/to/spack/view/openssl/lib64

# Output folder for eic-opticks event files (genstep.npy, hit.npy, etc.)
OPTICKS_OUT_FOLD=/path/to/scratch/opticks_output

# ── Activate the spack environment (Geant4, ROOT, CMake, GCC) ──────────────
source ${SPACK_ENV}

# ── Runtime library path ──────────────────────────────────────────────────
# ORDER MATTERS: the correctly-versioned edep-sim, eic-opticks, and plugin
# libraries must come BEFORE anything else. Do not add any directory that
# contains an older copy of these libraries — it will override the right one
# and cause runtime symbol errors that are hard to diagnose.
export LD_LIBRARY_PATH=\
${EDEPSIM_INST}/lib:\
${EICOPT_INST}/lib:\
${PLUGIN_BUILD}:\
/usr/local/cuda/lib64:\
${OPENSSL_DIR}:\
${LD_LIBRARY_PATH}

# ── Plugin shared library path (referenced in macro via $(PLUGIN_LIB)) ─────
export PLUGIN_LIB=${PLUGIN_BUILD}/libedep-opticks-plugin.so

# Physics process swap: replaces G4Cerenkov/G4Scintillation with instrumented
# versions that call U4::CollectGenstep_*() → SEvt::AddGenstep() at each step.
export EXTRAPHYSICS="EXTERN:${PLUGIN_LIB}:CreatePhysicsConstructor"

# ── eic-opticks runtime config ────────────────────────────────────────────
# Mode 1 = GPU-only simulation (eic-opticks handles optical photons on GPU,
# CPU kills them at production).
export OPTICKS_INTEGRATION_MODE=1

# Drift-field-aware photon yield. When set, Local_DsG4Scintillation calls
# G4EmParameters::Instance()->GetEmSaturation()->VisibleEnergyDepositionAtAStep(&aStep)
# (which dispatches to edep-sim's DokeBirks) and uses W=19.5 eV to compute the
# GPU photon count. Unset to revert to the legacy field-blind path
# (SCINTILLATIONYIELD × inline-Birks).
# LAr-specific (the W=19.5 eV is hardcoded in the patch).
export EDEPSIM_DOKEBIRKS_VISE=1

# Cap GPU photon-buffer slots. Default sizes for a 24 GB card and tries to
# allocate ~12 GB; on a partly-used GPU this OOMs at QEvt::device_alloc_photon.
# M1 = 1e6 slots ≈ 64 MB, enough for debug runs (<1e6 photons per launch).
# Raise (e.g. M10, M50) for production-scale runs.
export OPTICKS_MAX_SLOT=M1

# PTX kernel for OptiX ray tracing. Must be the kernel built against OptiX
# 8.1.0 headers (ABI 93), NOT 9.0.0 (ABI 105) — driver 555.42.06 only supports
# ABI 93. Check with:
#   nm -D ${EICOPT_INST}/lib/libCSGOptiX.so | grep g_optixFunctionTable
export CSGOptiX__ptxpath=${EICOPT_INST}/lib/CSGOptiX7.ptx

# Optional OPTICKS_KEY (geometry cache identifier). The hostname placeholder
# below works on most setups; adjust if eic-opticks complains about the key.
export OPTICKS_KEY=GeoChain.X4GeoChain.${HOSTNAME:-localhost}

# Output folder for eic-opticks event files (genstep.npy, hit.npy, etc.)
export OPTICKS_OUT_FOLD
mkdir -p ${OPTICKS_OUT_FOLD}

echo "[setup_env] Environment configured:"
echo "  PLUGIN_LIB               = ${PLUGIN_LIB}"
echo "  OPTICKS_INTEGRATION_MODE = ${OPTICKS_INTEGRATION_MODE}"
echo "  OPTICKS_OUT_FOLD         = ${OPTICKS_OUT_FOLD}"
echo "  OPTICKS_MAX_SLOT         = ${OPTICKS_MAX_SLOT}"
echo "  EDEPSIM_DOKEBIRKS_VISE   = ${EDEPSIM_DOKEBIRKS_VISE}"
