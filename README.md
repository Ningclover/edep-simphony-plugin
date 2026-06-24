# edep-Simphony plugin — edep-sim + Simphony GPU Optical Integration

## Project Summary

The **edep-Simphony plugin** (`libedep-simphony-plugin.so`) integrates **edep-sim** (Geant4 CPU simulation) with **Simphony** (GPU optical photon transport via NVIDIA OptiX) into a single pipeline. The result is a simulation where:

- edep-sim handles charged-particle physics (ionisation, EM showers) on CPU
- When Cerenkov/Scintillation photons would be generated, they are collected as "gensteps" and handed to Simphony
- Simphony ray-traces all photons in the event on GPU using OptiX
- Both the CPU physics results and the GPU photon hits are written into one ROOT file

> **Naming**: Simphony was formerly called *eic-opticks* (upstream is
> [BNLNPPS/simphony](https://github.com/BNLNPPS/simphony)). The local checkout
> and install live under `<prefix>/simphony/`.

---

## Architecture

```
edep-sim process (one event)
  │
  ├─ CPU: Geant4 tracks electron step by step
  │     Cerenkov/Scintillation fires at each step
  │     → instrumented process records genstep in SEvt buffer (Simphony, GPU-side)
  │     → optical photon secondaries KILLED on CPU (GPU handles them)
  │
  ├─ CPU: SimphonyStepAction records genstepIdx → G4 TrackID map
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

### Run modes at a glance

The default pipeline (above) is **GPU-only**: CPU photons are killed, only the GPU
transports light, and only detected hits (`GPUPhotonHits`) are written. Several
runtime modes — all toggled by `EDEP_SIMPHONY_*` environment variables — layer
extra behaviour on top:

| Mode | Env | What it adds |
|---|---|---|
| **GPU-only** (default) | *(none)* | Kill CPU photons; GPU genstep transport → `GPUPhotonHits` |
| **DUAL** | `EDEP_SIMPHONY_DUAL=1` | Also keep CPU optical tracking on, so a stock `G4Scintillation` tracks photons on CPU **alongside** the GPU. CPU and GPU light land in the **same** ROOT file for a per-event comparison |
| **All-photon / trajectory** | `EDEP_SIMPHONY_DEBUGHEAVY=1` | Put Opticks in `DebugHeavy` event mode → save **every** GPU photon (detected or not) + its full bounce-by-bounce path to `GPUPhotonTracks` / `GPUPhotonSteps` |
| **Input-photon** | `EDEP_SIMPHONY_INPUT_PHOTONS=1` | Capture the event's **primary optical photons** and inject them into Opticks as input photons, so the GPU transports the *identical* photons the CPU tracks (clean transport comparison). Implies `DebugHeavy` |

See **GPU Controls** below for the full variable list, and the
[`tests_benchmark` driver](../tests_benchmark/README.md) for ready-made
`run.sh` modes (`both`/`cpu`/`gpu`/`dual`/`photon`/`photon1`/`dualtraj`) that
set these for you.

---

## Key Components

### 1. Simphony (rebuilt from source)
- Source: `<prefix>/simphony/`
- Install: `<prefix>/simphony/install/` (headers under `include/simphony/`)
- Built with **OptiX 8.1.0** headers (ABI 93), compatible with driver 555.42.06
  - OptiX 9.0.0 (ABI 105) is NOT compatible with this driver
  - OptiX 8.1.0 headers live at: `<prefix>/optix810-sdk/`
- PTX kernel: `simphony/install/lib/CSGOptiX7.ptx`

### 2. edep-sim (one source change)
- Source: `<prefix>/edep-sim/`
- Install: `<prefix>/edep-sim/install/`
- **One line changed** in `src/EDepSimUserEventAction.cc`: the external action loop
  was moved before the `if (!HCofEvent) return` early exit, so the GPU plugin
  runs even on events with no ionisation hits.

### 3. Plugin library
- Source: this repository
- Build output: `build/libedep-simphony-plugin.so`

| File | Role |
|---|---|
| `src/SimphonyRunAction.cc` | Reads the `EDEP_SIMPHONY_*` knobs and configures `SEventConfig` (DebugHeavy, MaxSlot/Photon/Record/Bounce) **before** `SEvt` is created; sets `KillOpticalPhotons` (off in DUAL); registers `LArTPCSensorIdentifier`; calls `SEvt::CreateOrReuse()` then `G4CXOpticks::SetGeometry()`; creates **all five** TTrees (`GPUPhotonHits`, `GPUPhotonTracks`, `GPUPhotonSteps`, `CPUPhotonTracks`, `CPUPhotonSteps`) and owns their static branch buffers |
| `src/SimphonyEventAction.cc` | Calls `simulate()`; collects detected hits → `GPUPhotonHits`; in DebugHeavy/input-photon mode walks **all** GPU photons + their record buffer → `GPUPhotonTracks`/`GPUPhotonSteps`; recovers TrackId via genstep provenance; in input-photon mode captures primary optical photons and injects them as Opticks input photons |
| `src/SimphonyStepAction.cc` | Records genstep index → G4 TrackID provenance map; logs DokeBirks dE/dx + photon-yield sampling; fills `CPUPhotonSteps` (every step of every CPU optical photon) |
| `src/SimphonyCpuPhotonTracker.cc/.hh` | External `G4UserTrackingAction` (DUAL/CPU modes): records the **fate** of every CPU-tracked optical photon (detected, absorbed, WLS, escaped, …) → `CPUPhotonTracks`. Reads `G4OpBoundaryProcess::GetStatus()` only at a real geometry boundary to avoid stale `Detection` leaking onto bulk steps |
| `src/SimphonyPhysicsSwap.cc` | Replaces G4Cerenkov/G4Scintillation with instrumented versions; in DUAL mode also installs a stock `G4Scintillation` (+ DokeBirks `AddSaturation`, stacking on) so the CPU produces real trackable photons |
| `src/LArTPCSensorIdentifier.h` | Custom sensor identifier: finds volumes with a G4 SensitiveDetector (works for any geometry, not just PMT-named volumes) |
| `src/plugin_entry.cc` | `extern "C"` factory entry points (RunAction, EventAction, StepAction, PhysicsConstructor, **TrackAction**) |
| `macro/run_3gev_electron.mac` | Run macro (particle gun, geometry update, plugin load) |
| `macro/simphony_plugin.mac` | Loads plugin actions via `/edep/actions/load*` |
| `setup_env.example.sh` | Template for `setup_env.sh` — copy and edit the paths for your machine |
| `setup_env.sh` (gitignored) | User-local copy of the template; sets all required environment variables |

> **Geometry**: the plugin is geometry-agnostic — point `-g` at any GDML whose
> LAr has proper optical material properties (RINDEX etc.) and whose photon
> sensors carry a G4 SensitiveDetector. `LArTPCSensorIdentifier` finds the
> sensor volumes automatically (no PMT naming required).

---

## Environment Setup

### Step 0: Create your `setup_env.sh`

`setup_env.sh` is `.gitignored` because it hard-codes paths specific to one machine (spack view location, install prefixes, scratch folder, hostname). Copy the committed template and edit:

```bash
cp setup_env.example.sh setup_env.sh
${EDITOR:-vi} setup_env.sh   # edit the "User-specific paths" block at the top
```

After that, `source setup_env.sh` is the single command that prepares the environment — it handles everything in Step 2 below for you. The remaining steps in this section document what `setup_env.sh` is doing, in case you need to debug it.

### Step 1: Activate the base spack environment

```bash
source <prefix>/.envrc
# or: cd <prefix> && direnv allow
```

This loads Geant4 11.2.2, ROOT 6.32.02, CMake 3.30.2, and GCC from the spack view.

### Step 2: Set required environment variables

```bash
EICOPT_INST=<prefix>/simphony/install
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
export PLUGIN_LIB=${PLUGIN_BUILD}/libedep-simphony-plugin.so

# Instrumented Cerenkov/Scintillation physics (loaded before /run/initialize)
export EXTRAPHYSICS="EXTERN:${PLUGIN_LIB}:CreatePhysicsConstructor"

# Simphony mode: 1 = GPU-only (no CPU photon tracking)
export OPTICKS_INTEGRATION_MODE=1

# Drift-field-aware photon yield: route edep-sim DokeBirks visE through
# Local_DsG4Scintillation as the GPU photon-count source. Unset to revert
# to the legacy SCINTILLATIONYIELD × inline-Birks path. (LAr-specific; W=19.5 eV)
export EDEPSIM_DOKEBIRKS_VISE=1

# Cap GPU photon-buffer slots. Default sizes for a 24 GB card and tries to
# allocate ~12 GB; on a partly-used GPU this OOMs at QEvt::device_alloc_photon.
# M1 = 1e6 slots ≈ 64 MB, enough for debug runs (<1e6 photons total per launch).
# Raise for production-scale runs.
export OPTICKS_MAX_SLOT=M1

# PTX kernel path
export CSGOptiX__ptxpath=${EICOPT_INST}/lib/CSGOptiX7.ptx

# Output folder for Simphony numpy arrays (genstep.npy, hit.npy etc.)
# Note: /tmp is often full on wcgpu1 — setup_env.sh uses /nfs/data/1/xning/tmp/opticks_output
export OPTICKS_OUT_FOLD=/nfs/data/1/xning/tmp/opticks_output
```

The simplest path is to just `source setup_env.sh`, which sets all of the above (including `EDEPSIM_DOKEBIRKS_VISE=1` and `OPTICKS_MAX_SLOT=M1` by default) and skips the per-variable boilerplate.

> **Warning**: Do NOT add any directory containing old conflicting versions of
> Geant4, edepsim, or Simphony to `LD_LIBRARY_PATH`. These will override the
> correctly-versioned libraries and cause symbol errors.

### Step 3: Run

```bash
edep-sim -p QGSP_BERT \
         -g <path-to-geometry>.gdml \
         -o output.root \
         -e 1000 \
         macro/run_3gev_electron.mac
```

The particle gun is configured in `run_3gev_electron.mac`. Edit `/gps/energy` and
`/gps/position` there to change the beam.

> **Note**: despite its name, `run_3gev_electron.mac` currently fires a **1 MeV**
> electron (`/gps/energy 1 MeV`, `/gps/position 0 0 -400 mm`). Change `/gps/energy`
> if you want a different beam energy.

---

## GPU Controls (`EDEP_SIMPHONY_*` environment variables)

These are read by the plugin at runtime (in `SimphonyRunAction::BeginOfRunAction`,
before `SEvt` is created). All are optional — unset means the default GPU-only,
hit-only path.

| Variable | Meaning | Default |
|---|---|---|
| `EDEP_SIMPHONY_DUAL` | `1` = also track optical photons on **CPU** (stock `G4Scintillation`) next to the GPU genstep path, so both write to the same ROOT file. Keeps `KillOpticalPhotons` **off** | unset (GPU-only, CPU photons killed) |
| `EDEP_SIMPHONY_DEBUGHEAVY` | `1` = Opticks `DebugHeavy` event mode: gather photon + record + hit, so **all** GPU photons + full per-photon trajectory are saved (`GPUPhotonTracks`/`GPUPhotonSteps`). Memory-hungry — few-photon runs only | unset (hit-only `Minimal` mode) |
| `EDEP_SIMPHONY_INPUT_PHOTONS` | `1` = inject the event's **primary optical photons** into Opticks as input photons (instead of relying on scintillation gensteps). Implies DebugHeavy | unset |
| `EDEP_SIMPHONY_MAXBOUNCE` | Max GPU bounces per photon before it is killed (the transport bounce cap). Applies in **all** modes. 128 nm photons Rayleigh-scatter heavily in LAr and need a high budget; too low → photons hit the cap before being absorbed/detected | Opticks default (~31) |
| `EDEP_SIMPHONY_MAXRECORD` | Length of the saved per-photon trajectory buffer. Clamps `MaxBounce` to `MaxRecord-1` unless `MAXBOUNCE` is set after | auto (DebugHeavy) |
| `EDEP_SIMPHONY_MAXSLOT` | GPU photon-slot / photon budget for DebugHeavy (record alloc = MaxSlot × MaxRecord). Capped small for debug runs to avoid CUDA OOM | `200000` (DebugHeavy) |
| `EDEP_SIMPHONY_CERENKOV` | `0` = disable Cerenkov (scintillation-only comparison) | — |
| `EDEP_SIMPHONY_SCINT` | Scint process: `thin` (`SimphonyScintProcess`, default) or `fork` (`Local_DsG4Scintillation`) | `thin` |

> **Record-length hard cap**: the per-photon GPU trajectory is also bounded by
> Opticks `sseq::SLOTS` (baked into `simphony/sysrap/sseq.h`). To store paths
> longer than that you must raise **both** `EDEP_SIMPHONY_MAXBOUNCE` (env) **and**
> `sseq::SLOTS` (recompile simphony). The CPU side has **no** bounce cap — it
> propagates until physically absorbed/detected.

### Loading the CPU tracking action (DUAL / CPU fate)

To fill `CPUPhotonTracks` (per-photon CPU fate), the tracking-action factory must
be loaded in the macro alongside the other plugin actions:

```
/edep/actions/loadUserTrackAction $(PLUGIN_LIB) CreateUserTrackAction ""
```

`CPUPhotonSteps` (full CPU path) is filled by the step action and additionally
needs edep-sim's own trajectory saving turned on in the macro **before**
`/edep/update`:

```
/edep/db/set/savePhotonTraj  true
/edep/db/set/saveAllPrimTraj true
```

---

## How to Recompile

> **Easiest path**: the helper script `<prefix>/tests/recompile.sh` rebuilds any
> or all of the three components in the correct dependency order and verifies the
> plugin's library links. E.g. `./recompile.sh plugin`, `./recompile.sh simphony
> --clean`, or `./recompile.sh all -j8`. The manual steps below document what it
> does, in case you need to debug a build.

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

### Recompile Simphony

When the `simphony/build/` directory has a valid cmake cache (i.e. it was already
configured at the current path), there are three cases depending on what you changed.

#### Case 1: Changed a `.cc` or `.hh` file (most common)

CMake tracks dependencies automatically — just rebuild:

```bash
source <prefix>/.envrc
cd <prefix>/simphony/build
make -j8 install
```

Install is required. The plugin and edep-sim load from `simphony/install/lib/`.

#### Case 2: Changed a CUDA kernel (`.cu` file, especially in `CSGOptiX/`)

Same command as Case 1 — `make -j8 install` rebuilds the PTX too. Afterwards verify:

```bash
# PTX timestamp should be recent
ls -lh <prefix>/simphony/install/lib/CSGOptiX7.ptx

# ABI must still be 93 (OptiX 8.1.0) — NOT 105 (OptiX 9.0.0)
nm -D <prefix>/simphony/install/lib/libCSGOptiX.so | grep g_optixFunctionTable
# expected output:  g_optixFunctionTable_93
# if you see:       g_optixFunctionTable_105  → wrong OptiX headers picked up, see Case 3
```

#### Case 3: Full clean rebuild from scratch

Only needed if cmake is confused or you changed `CMakeLists.txt`:

```bash
source <prefix>/.envrc
export PATH=/usr/local/cuda/bin:$PATH

cd <prefix>/simphony
rm -rf build install

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CUDA_ARCHITECTURES=89 \
  -DCMAKE_INSTALL_PREFIX=<prefix>/simphony/install \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc \
  -DCMAKE_PREFIX_PATH="<spack-view>;<prefix>/optix810-sdk;/nfs/data/1/xning/edep_optic/hack/local" \
  -DOptiX_INSTALL_DIR=<prefix>/optix810-sdk

cmake --build build -j8 && cmake --install build
```

> **From a clean `build/` you MUST pass two things or configure fails**:
> - `-DCMAKE_PREFIX_PATH=...;/nfs/data/1/xning/edep_optic/hack/local` — otherwise
>   `find_package(plog)` fails (plog lives in `hack/local`, not the spack view).
> - `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc` — nvcc is not on `PATH` via the
>   spack env.
>
> **The single most critical flag**: `-DOptiX_INSTALL_DIR=<prefix>/optix810-sdk`
>
> This must point to the OptiX **8.1.0** SDK headers (ABI 93), NOT the 9.0.0 SDK
> (ABI 105). The build will succeed with either, but at runtime OptiX 9.0.0 will
> fail with `OPTIX_ERROR_UNSUPPORTED_ABI_VERSION` because the installed GPU driver
> (555.42.06) only supports up to ABI 93.

#### After any Simphony rebuild — also rebuild the plugin

The plugin links against Simphony headers and libraries, so always follow a
Simphony rebuild with:

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

Depending on the run mode, the file holds up to five plugin TTrees alongside
edep-sim's own `EDepSimEvents`:

| Tree | Written when | One row per | Contents |
|---|---|---|---|
| `GPUPhotonHits` | always | detected GPU photon | the detected optical hits |
| `GPUPhotonTracks` | DebugHeavy / input-photon | every GPU photon | per-photon final **fate** (detected or not + why) |
| `GPUPhotonSteps` | DebugHeavy / input-photon | every GPU step point | full bounce-by-bounce GPU path |
| `CPUPhotonTracks` | DUAL + `CreateUserTrackAction` | every CPU optical photon | per-photon CPU **fate** |
| `CPUPhotonSteps` | DUAL + photon-traj saving | every CPU step point | full CPU path |

```python
import uproot
import numpy as np

f = uproot.open("output.root")

# GPU optical photon hits (always present)
hits = f["GPUPhotonHits"]
print(hits.keys())
# ['EventId', 'TrackId', 'Process', 'Wavelength', 'HitPos', 'StartPos']

df = hits.arrays(library="pd")
print(df.head())

# All-photon fate + trajectory (DebugHeavy / input-photon modes)
tracks = f["GPUPhotonTracks"].arrays(library="pd")  # one row per photon + fate
steps  = f["GPUPhotonSteps"].arrays(library="pd")   # full GPU bounce path

# CPU side (DUAL mode)
cpu_tracks = f["CPUPhotonTracks"].arrays(library="pd")
cpu_steps  = f["CPUPhotonSteps"].arrays(library="pd")

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

**`GPUPhotonHits` branches:**

| Branch | Type | Description |
|---|---|---|
| `EventId` | int | Geant4 event number |
| `TrackId` | int | G4 TrackID of the charged parent (1 = primary) |
| `Process` | int | 0 = Cerenkov, 2 = Scintillation, -1 = unknown |
| `Wavelength` | float | Photon wavelength in nm |
| `HitPos` | TLorentzVector | Hit position (x,y,z in mm; t in ns) |
| `StartPos` | TLorentzVector | Genstep origin (reserved; currently zero-filled) |

**`GPUPhotonTracks` branches** (one row per GPU photon, detected or not):

| Branch | Type | Description |
|---|---|---|
| `EventId` | int | Geant4 event number |
| `PhotonId` | int | Photon index within the event |
| `TrackId` | int | GPU photon global index (one per photon; persists across WLS re-emission) |
| `ParentId` | int | Charged G4 parent track of the genstep, or **-1** for input/primary photons |
| `Process` | int | 0 = Cerenkov, 2 = Scintillation, 3 = TORCH (primary), -1 |
| `Wavelength` | float | nm |
| `Flag` | unsigned | Terminating Opticks flag bit |
| `FlagMask` | unsigned | OR of all history flags |
| `Detected` | int | 1 if SURFACE_DETECT / EFFICIENCY_COLLECT |
| `Fate` | char[8] | Short reason: `SD`, `AB`, `SA`, `MI`, … |
| `NStep` | int | Number of recorded trajectory points |
| `StartPos` / `EndPos` | TLorentzVector | First / last record point (mm, ns) |

**`GPUPhotonSteps` branches** (one row per GPU step point):

| Branch | Type | Description |
|---|---|---|
| `EventId`, `PhotonId` | int | Identify the photon |
| `Step` | int | Step index along the photon |
| `Flag` | unsigned | Opticks flag at this step |
| `FlagAbbr` | char[8] | Abbreviated flag |
| `Pos` | TLorentzVector | Step position (mm, ns) |

**`CPUPhotonTracks` branches** (one row per CPU-tracked optical photon):

| Branch | Type | Description |
|---|---|---|
| `EventId`, `TrackId`, `ParentId` | int | G4 identifiers |
| `Wavelength` | float | nm |
| `Energy` | float | eV |
| `Detected` | int | 1 if boundary status was `Detection` |
| `Fate` | char[16] | `SD`, `SA`, `AB`, `WLS`, `MI`, `Reflect`, `Rayleigh`, or the process name |
| `EndVolume` | char[64] | Volume where the photon ended (`OutOfWorld` if it escaped) |
| `NStep` | int | Number of geometry steps |
| `StartPos` / `EndPos` | TLorentzVector | Creation / termination point (mm, ns) |

**`CPUPhotonSteps` branches** (one row per CPU optical-photon step):

| Branch | Type | Description |
|---|---|---|
| `EventId`, `TrackId` | int | G4 identifiers |
| `Step` | int | G4 step number along the photon |
| `Process` | char[24] | Process that ended the step |
| `Volume` | char[48] | Post-step volume |
| `Pos` | TLorentzVector | Post-step position (mm, ns) |

> **Fate codes** (shared CPU/GPU vocabulary): `SD` detected on a sensor ·
> `SA` absorbed at a surface · `AB` bulk absorption in LAr · `WLS` absorbed by a
> wavelength shifter · `MI` left the world (escaped) · `Reflect`/`Rayleigh` ended
> on a reflection/scatter.

---

## Relevant Source

| Item | Path |
|---|---|
| Plugin source | this repository |
| Plugin build | `build/` |
| Simphony repo | [github.com/Ningclover/simphony](https://github.com/Ningclover/simphony) — source `<prefix>/simphony/`, install `<prefix>/simphony/install/` |
| edep-sim repo | [github.com/ClarkMcGrew/edep-sim](https://github.com/ClarkMcGrew/edep-sim) — source `<prefix>/edep-sim/`, install `<prefix>/edep-sim/install/` |
| gegede (geometry tool) | [github.com/brettviren/gegede](https://github.com/brettviren/gegede) |
| Benchmark CPU-vs-GPU driver | [github.com/Ningclover/tests_benchmark](https://github.com/Ningclover/tests_benchmark) — example geometry, `.mac` files, and visual tools that drive this plugin |
