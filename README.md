# edep-sim + eic-opticks GPU Optical Integration

## Project Summary

This project integrates **edep-sim** (Geant4 CPU simulation) with **eic-opticks** (GPU optical photon transport via NVIDIA OptiX) into a single pipeline. The result is a simulation where:

- edep-sim handles charged-particle physics (ionisation, EM showers) on CPU
- When Cerenkov/Scintillation photons would be generated, they are collected as "gensteps" and handed to eic-opticks
- eic-opticks ray-traces all photons in the event on GPU using OptiX
- Both the CPU physics results and the GPU photon hits are written into one ROOT file

**Demonstrated result**: 1000 × 1 MeV electrons in a LArTPC (lighttrap geometry), 13,229 GPU-simulated photons, 8 SiPM hits recorded — wavelengths 394–530 nm (visible Cerenkov), hit positions confirmed at the SiPM plane, TrackId=1 tracing every hit back to the primary electron.

---

## Architecture

```
edep-sim process (one event)
  │
  ├─ CPU: Geant4 tracks electron step by step
  │     Cerenkov/Scintillation fires at each step
  │     → instrumented process records genstep in SEvt buffer (GPU-side)
  │     → optical photon secondaries KILLED on CPU (GPU handles them)
  │
  ├─ CPU: OpticsStepAction records genstepIdx → G4 TrackID map
  │
  ├─ CPU: EndOfEventAction calls G4CXOpticks::simulate()  [blocking]
  │         GPU: OptiX ray-traces all photons in this event
  │         GPU: photon hits surface with EFFICIENCY>0 → SURFACE_DETECT flag
  │       CPU resumes after GPU finishes
  │
  ├─ CPU: reads hits from SEvt → fills GPUPhotonHits TTree
  │       recovers TrackId via genstep provenance map
  │
  └─ edep-sim ROOT file
       EDepSimEvents TTree      ← CPU ionisation hits, trajectories, primaries
       GPUPhotonHits TTree      ← GPU optical hits: EventId, TrackId, Process,
                                   Wavelength(nm), HitPos(mm,ns), StartPos
```

**CPU and GPU are strictly sequential**: the CPU blocks during `simulate()`, then resumes to collect hits. There is no overlap between events.

---

## Key Components

### 1. eic-opticks (rebuilt from source)
- Source: `<prefix>/eic-opticks/`
- Install: `<prefix>/eic-opticks/install/`
- Built with **OptiX 8.1.0** headers (ABI 93), compatible with driver 555.42.06
  - OptiX 9.0.0 (ABI 105) is NOT compatible with this driver
  - OptiX 8.1.0 headers live at: `<prefix>/optix810-sdk/`
- PTX kernel: `eic-opticks/install/lib/CSGOptiX7.ptx`

### 2. edep-sim (one source change)
- Source: `<prefix>/edep-sim/`
- Install: `<prefix>/edep-sim/install/`
- **One line changed** in `src/EDepSimUserEventAction.cc`: the external action loop
  was moved before the `if (!HCofEvent) return` early exit, so the GPU plugin
  runs even on events with no ionisation hits.

### 3. Plugin library
- Source: this repository
- Build output: `build/libedep-opticks-plugin.so`

| File | Role |
|---|---|
| `src/OpticsRunAction.cc` | Forces `KillOpticalPhotons=true`; registers `LArTPCSensorIdentifier`; calls `SEvt::CreateOrReuse()` then `G4CXOpticks::SetGeometry()`; creates GPUPhotonHits TTree |
| `src/OpticsEventAction.cc` | Calls `simulate()`; collects hits into TTree; recovers TrackId |
| `src/OpticsStepAction.cc` | Records genstep index → G4 TrackID provenance map |
| `src/OpticsPhysicsSwap.cc` | Replaces G4Cerenkov/G4Scintillation with instrumented versions |
| `src/LArTPCSensorIdentifier.h` | Custom sensor identifier: finds volumes with a G4 SensitiveDetector (works for any geometry, not just PMT-named volumes) |
| `src/plugin_entry.cc` | `extern "C"` factory entry points |
| `macro/run_3gev_electron.mac` | Run macro (particle gun, geometry update, plugin load) |
| `macro/opticks_plugin.mac` | Loads plugin actions via `/edep/actions/load*` |
| `setup_env.sh` | Sets all required environment variables |

### 4. Geometry
- **lighttrap.gdml**: `/nfs/data/1/xning/optic-gpu/light_trap_ggd/lighttrap.gdml`
  - 100×100×100 cm LAr world
  - 30 SiPMs (0.6×0.6 cm each) with EFFICIENCY=1.0, REFLECTIVITY=0.0
  - Vikuiti reflective foil on back plane and edges
  - pTP wavelength-shifter and blue WLS acrylic plates
  - LAr RINDEX properly defined (1.23–1.45 across optical/VUV range)

---

## Non-Obvious Bugs Fixed

| Bug | Symptom | Fix |
|---|---|---|
| OptiX 9.0.0 ABI mismatch | `OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` at startup | Rebuilt CSGOptiX with OptiX 8.1.0 headers from `optix810-sdk/` |
| SEvt not initialized | `SEvt::Get_EGPU() = null` | Call `SEvt::CreateOrReuse()` BEFORE `G4CXOpticks::SetGeometry()` |
| External actions never called | GPU plugin skipped on events with no ionisation hits | Moved external action loop before `if (!HCofEvent) return` in `EDepSimUserEventAction.cc` |
| Old libs overriding new ones | Symbol lookup errors, wrong edepsim loaded | Removed `hack/local/lib` from `LD_LIBRARY_PATH`; prepended correct lib paths |
| Sensors not recognized | `sensor_count=0`, 0 hits | Default sensor ID requires "PMT" in name; wrote `LArTPCSensorIdentifier` using SD check |
| Photons tracked on CPU | Run freezes after 2 events | `SurfaceDetector` aux tag sets `KillOpticalPhotons=false`; plugin forces it back to `true` in `BeginOfRunAction` |
| LAr RINDEX=1.0 on GPU | `sampledRI=1.0`, cosTheta>1, unphysical | Switched to lighttrap.gdml which has proper LAr material properties |

---

## Environment Setup

### Step 1: Activate the base spack environment

```bash
source <prefix>/.envrc
# or: cd <prefix> && direnv allow
```

This loads Geant4 11.2.2, ROOT 6.32.02, CMake 3.30.2, and GCC from the spack view.

### Step 2: Set required environment variables

```bash
EICOPT_INST=<prefix>/eic-opticks/install
EDEPSIM_INST=<prefix>/edep-sim/install
PLUGIN_BUILD=<path-to-this-repo>/build

# Library search path (ORDER MATTERS — correct versions must come first)
export LD_LIBRARY_PATH=\
${EDEPSIM_INST}/lib:\
${EICOPT_INST}/lib:\
${PLUGIN_BUILD}:\
/usr/local/cuda/lib64:\
${LD_LIBRARY_PATH}

# Plugin shared library path
export PLUGIN_LIB=${PLUGIN_BUILD}/libedep-opticks-plugin.so

# Instrumented Cerenkov/Scintillation physics (loaded before /run/initialize)
export EXTRAPHYSICS="EXTERN:${PLUGIN_LIB}:CreatePhysicsConstructor"

# eic-opticks mode: 1 = GPU-only (no CPU photon tracking)
export OPTICKS_INTEGRATION_MODE=1

# PTX kernel path
export CSGOptiX__ptxpath=${EICOPT_INST}/lib/CSGOptiX7.ptx

# Output folder for eic-opticks numpy arrays (genstep.npy, hit.npy etc.)
export OPTICKS_OUT_FOLD=/tmp/opticks_output
```

> **Warning**: Do NOT add any directory containing old conflicting versions of
> Geant4, edepsim, or eic-opticks to `LD_LIBRARY_PATH`. These will override the
> correctly-versioned libraries and cause symbol errors.

### Step 3: Run

```bash
edep-sim -p QGSP_BERT \
         -g <path-to-geometry>/lighttrap.gdml \
         -o output.root \
         -e 1000 \
         macro/run_3gev_electron.mac
```

The particle gun is configured in `run_3gev_electron.mac`. Edit `/gps/energy` and
`/gps/position` there to change the beam.

---

## How to Recompile

### Recompile the plugin only (most common)

Do this after editing any file in `src/`:

```bash
source <prefix>/.envrc
cd <path-to-this-repo>/build
make -j4
# No install needed — edep-sim loads the .so directly from the build directory
```

### Recompile edep-sim

Do this only if you change files in `edep-sim/src/`:

```bash
source <prefix>/.envrc
cd <prefix>/edep-sim/build
make -j4 install   # must install, not just make
```

The installed binary is at `edep-sim/install/bin/edep-sim`.

### Recompile eic-opticks

The build directory at `eic-opticks/build/` is intact and has a valid cmake cache,
so there are three cases depending on what you changed.

#### Case 1: Changed a `.cc` or `.hh` file (most common)

CMake tracks dependencies automatically — just rebuild:

```bash
source <prefix>/.envrc
cd <prefix>/eic-opticks/build
make -j8 install
```

Install is required. The plugin and edep-sim load from `eic-opticks/install/lib/`.

#### Case 2: Changed a CUDA kernel (`.cu` file, especially in `CSGOptiX/`)

Same command as Case 1 — `make -j8 install` rebuilds the PTX too. Afterwards verify:

```bash
# PTX timestamp should be recent
ls -lh <prefix>/eic-opticks/install/lib/CSGOptiX7.ptx

# ABI must still be 93 (OptiX 8.1.0) — NOT 105 (OptiX 9.0.0)
nm -D <prefix>/eic-opticks/install/lib/libCSGOptiX.so | grep g_optixFunctionTable
# expected output:  g_optixFunctionTable_93
# if you see:       g_optixFunctionTable_105  → wrong OptiX headers picked up, see Case 3
```

#### Case 3: Full clean rebuild from scratch

Only needed if cmake is confused or you changed `CMakeLists.txt`:

```bash
source <prefix>/.envrc

cd <prefix>/eic-opticks
rm -rf build && mkdir build && cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=<prefix>/eic-opticks/install \
  -DCMAKE_PREFIX_PATH="<spack-view>;<prefix>/optix810-sdk" \
  -DOptiX_INSTALL_DIR=<prefix>/optix810-sdk \
  -DCMAKE_CUDA_ARCHITECTURES=89

make -j8 install
```

> **The single most critical flag**: `-DOptiX_INSTALL_DIR=<prefix>/optix810-sdk`
>
> This must point to the OptiX **8.1.0** SDK headers (ABI 93), NOT the 9.0.0 SDK
> (ABI 105). The build will succeed with either, but at runtime OptiX 9.0.0 will
> fail with `OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` because the installed GPU driver
> (555.42.06) only supports up to ABI 93.

#### After any eic-opticks rebuild — also rebuild the plugin

The plugin links against eic-opticks headers and libraries, so always follow an
eic-opticks rebuild with:

```bash
source <prefix>/.envrc
cd <path-to-this-repo>/build
make -j4
```

### Reconfigure the plugin cmake from scratch

Only needed if you add new source files or change `CMakeLists.txt`:

```bash
source <prefix>/.envrc
cd <path-to-this-repo>
rm -rf build && mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j4
```

---

## Reading the Output ROOT File

```python
import uproot
import numpy as np

f = uproot.open("output.root")

# GPU optical photon hits
hits = f["GPUPhotonHits"]
print(hits.keys())
# ['EventId', 'TrackId', 'Process', 'Wavelength', 'HitPos', 'StartPos']

df = hits.arrays(library="pd")
print(df.head())

# CPU ionisation + trajectory info
events = f["EDepSimEvents"]
```

With ROOT/PyROOT:

```python
import ROOT
f = ROOT.TFile("output.root")
t = f.Get("GPUPhotonHits")
for entry in t:
    print(entry.EventId, entry.TrackId, entry.Wavelength,
          entry.HitPos.X(), entry.HitPos.Y(), entry.HitPos.Z())
```

**Branch descriptions:**

| Branch | Type | Description |
|---|---|---|
| `EventId` | int | Geant4 event number |
| `TrackId` | int | G4 TrackID of the charged parent (1 = primary) |
| `Process` | int | 0 = Cerenkov, 2 = Scintillation, -1 = unknown |
| `Wavelength` | float | Photon wavelength in nm |
| `HitPos` | TLorentzVector | Hit position (x,y,z in mm; t in ns) |
| `StartPos` | TLorentzVector | Genstep origin (reserved; currently zero-filled) |

---

## Key File Locations

| Item | Path |
|---|---|
| Plugin source | this repository |
| Plugin build | `build/` |
| Run macro | `macro/run_3gev_electron.mac` |
| eic-opticks source | `<prefix>/eic-opticks/` |
| eic-opticks install | `<prefix>/eic-opticks/install/` |
| edep-sim source | `<prefix>/edep-sim/` |
| edep-sim install | `<prefix>/edep-sim/install/` |
| OptiX 8.1.0 headers | `<prefix>/optix810-sdk/` |
| LightTrap geometry | `<prefix>/light_trap_ggd/lighttrap.gdml` |
| LArTPC geometry | `<prefix>/lartpc-geo/lartpc.gdml` |
| Spack environment | `<prefix>/.envrc` |
