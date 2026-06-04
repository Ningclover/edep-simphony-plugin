# Plan — Option (a2): Thin plugin-owned scintillation process

Date drafted: 2026-05-26
Status: design approved, implementation pending

## Goal

Let edep-sim's **DokeBirks model** own the photon-count physics; the plugin
becomes a thin adapter that reads visE from DokeBirks, computes
`NumPhotons = visE / W`, splits fast/slow, and pushes gensteps directly
to `SEvt` for the GPU. Stock `Local_DsG4Scintillation` (the eic-opticks
fork) is kept in the build but dormant, selectable via env var for A/B
comparison.

## Why this design

1. **DokeBirks already computes NumPhotons internally.** See
   [edep-sim/src/EDepSimDokeBirksSaturation.cc:322](../edep-sim/src/EDepSimDokeBirksSaturation.cc#L322):

   ```cpp
   G4double ScintillationYield = 1 / (19.5*eV);
   G4double NumQuanta   = ScintillationYield * totalEnergyDeposit;
   G4double NumExcitons = NumQuanta * 0.21/1.21;
   G4double NumIons     = NumQuanta - NumExcitons;
   G4double NumPhotons  = NumExcitons + NumIons * recombProb;
   G4double nonIonizingEnergy = totalEnergyDeposit * NumPhotons / NumQuanta;
   return nonIonizingEnergy;   // = visE
   ```

   `visE` is just `NumPhotons × 19.5 eV` by construction — the photon count
   round-trips losslessly.

2. **No process-ordering risk.** Calling
   `G4EmSaturation::VisibleEnergyDepositionAtAStep(&aStep)` recomputes visE
   on the spot from `dE`, the volume's electric field, LET, and DokeBirks
   constants. It does not read `step->GetNonIonizingEnergyDeposit()`, so it
   does not depend on whether `EDepSim::SecondaryEnergy` ran first.

3. **Same yield formula as today's working path.** The current run with
   `EDEPSIM_DOKEBIRKS_VISE=1` already uses this code path inside
   `Local_DsG4Scintillation` ([eic-opticks/u4/Local_DsG4Scintillation.cc:517-523](../eic-opticks/u4/Local_DsG4Scintillation.cc#L517-L523)),
   giving CPU/GPU KS p=0.52 in 5-event tests (see
   [agent_log/2026-05-26-cpu-gpu-plugin-walkthrough.md](../agent_log/2026-05-26-cpu-gpu-plugin-walkthrough.md)).
   We're extracting that same formula into a smaller, edep-sim-owned process.

## Behavioural contract

The new process must reproduce, per scintillating step:

- The **same `NumPhotons` distribution** as
  [eic-opticks/u4/Local_DsG4Scintillation.cc:519-548](../eic-opticks/u4/Local_DsG4Scintillation.cc#L519-L548)
  under `EDEPSIM_DOKEBIRKS_VISE=1`.
- The **same fast/slow split** as
  [eic-opticks/u4/Local_DsG4Scintillation.cc:605-627](../eic-opticks/u4/Local_DsG4Scintillation.cc#L605-L627)
  (per-photon Poisson over `Ratio_timeconstant` MPT vector).
- The **same genstep payload** by calling
  `U4::CollectGenstep_DsG4Scintillation_r4695` exactly as the fork does at
  [eic-opticks/u4/Local_DsG4Scintillation.cc:705](../eic-opticks/u4/Local_DsG4Scintillation.cc#L705).
- **Zero secondaries.** No CPU `G4Track` creation; GPU does all transport.

## Files

### New: `src/SimphonyScintProcess.hh` / `SimphonyScintProcess.cc`

`G4VDiscreteProcess` subclass.

- **Constructor:** `SimphonyScintProcess(const G4String& name = "Scintillation")`.
  No physics tables to build — wavelength/time sampling lives on the GPU
  and reads the GDML MPT.

- **`IsApplicable(const G4ParticleDefinition& p)`:** true for any particle
  with non-zero charge. (Exclude `opticalphoton`: re-emission/WLS is handled
  on GPU.)

- **`GetMeanFreePath(const G4Track&, G4double, G4ForceCondition* cond)`:**
  set `*cond = StronglyForced`, return `DBL_MAX`. Same idiom as
  `G4Scintillation` — fires every step regardless of MFP.

- **`PostStepDoIt(const G4Track& aTrack, const G4Step& aStep)`:**
  1. Initialise `aParticleChange` from track; default to no change.
  2. Early-return if `aStep.GetTotalEnergyDeposit() <= 0`, material has no
     MPT, or material has no scintillation properties.
  3. Read `visE`:
     ```cpp
     G4EmSaturation* emSat = G4EmParameters::Instance()->GetEmSaturation();
     G4double visE = emSat ? emSat->VisibleEnergyDepositionAtAStep(&aStep) : 0.0;
     if (visE <= 0.0) return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);
     ```
  4. Compute `MeanNph = visE / kW_LAr_MeV` (with `kW_LAr_MeV = 19.5e-6`),
     then Gaussian or Poisson sample → `NumTracks`
     ([mirrors fork lines 542-548](../eic-opticks/u4/Local_DsG4Scintillation.cc#L542-L548)).
  5. Look up `Ratio_timeconstant` MPV by particle name
     ([mirrors fork lines 368-380](../eic-opticks/u4/Local_DsG4Scintillation.cc#L368-L380)):
     gamma/e±→`GammaCONSTANT`, alpha→`AlphaCONSTANT`,
     opticalphoton→`OpticalCONSTANT` (not reached for charged), other→`NeutronCONSTANT`.
     Early-return if null.
  6. Per-photon Poisson split into `NumVec[scnt]`
     ([mirrors fork lines 605-627](../eic-opticks/u4/Local_DsG4Scintillation.cc#L605-L627)).
  7. Loop `scnt = 0 .. nscnt-1`:
     ```cpp
     G4int NumPhoton = NumVec[scnt];
     if (NumPhoton <= 0) continue;
     G4double scintTime = Ratio_timeconstant->Energy(scnt);
     U4::CollectGenstep_DsG4Scintillation_r4695(
         &aTrack, &aStep, NumPhoton, scnt, scintTime);
     ```
  8. `aParticleChange.SetNumberOfSecondaries(0);`
     `return G4VDiscreteProcess::PostStepDoIt(aTrack, aStep);`

- **Debug print:** emit one line per step matching the fork's
  `[DsG4Scint][DBG-EICOPTICKS] genstep collected: …` so existing log scrapers
  keep working. Tag with `[SimphonyScintProcess]` for clarity.

- **`G4VDiscreteProcess` vs `G4VRestDiscreteProcess`.** Start with
  `G4VDiscreteProcess` (PostStep only) — sufficient for the e⁻/gamma test
  cases. Stopped-alpha scintillation (which needs AtRestDoIt) is out of
  scope for now; documented as a known gap.

### Modified: `src/SimphonyPhysicsSwap.cc`

Env-gated selection between thin and fork processes:

```cpp
#include "SimphonyScintProcess.hh"
#include <cstdlib>
#include <string>

// ...
const char* mode_c = std::getenv("EDEP_SIMPHONY_SCINT");
std::string mode = mode_c ? mode_c : "thin";

G4VProcess* scintillation = nullptr;
if (mode == "fork") {
    auto* s = new Local_DsG4Scintillation(1);
    s->SetTrackSecondariesFirst(true);
    scintillation = s;
    std::cout << "[SimphonyPlugin] Using Local_DsG4Scintillation (fork) — "
                 "EDEP_SIMPHONY_SCINT=fork\n";
} else {
    scintillation = new SimphonyScintProcess();
    std::cout << "[SimphonyPlugin] Using SimphonyScintProcess (thin, DokeBirks visE)"
                 " — EDEP_SIMPHONY_SCINT=thin (default)\n";
}
```

The rest of the process-attach loop is unchanged: still removes stock
`Scintillation`/`Cerenkov` from each particle's process manager, still adds
the chosen scintillation process (and `Local_G4Cerenkov_modified`) where
applicable.

Note: for `opticalphoton` the fork is added for re-emission. The thin
process is not applicable to `opticalphoton`, so under `mode == "thin"`,
opticalphoton has no scintillation process — fine because optical photons
are killed on the CPU side anyway.

### Modified: `CMakeLists.txt`

Add the new source:

```cmake
add_library(edep-simphony-plugin SHARED
    src/SimphonyRunAction.cc
    src/SimphonyEventAction.cc
    src/SimphonyStepAction.cc
    src/SimphonyPhysicsSwap.cc
    src/SimphonyScintProcess.cc      # ← NEW
    src/plugin_entry.cc
)
```

No new dependencies. The thin process uses only Geant4 headers, plus
`U4.hh` from eic-opticks (already on the include path).

## Files NOT modified

- `src/SimphonyRunAction.cc` — `SEvt::CreateOrReuse()` and
  `G4CXOpticks::SetGeometry(world)` are still required prerequisites for
  `gx->simulate()`.
- `src/SimphonyEventAction.cc` — `gx->simulate(...)` and hit readout
  unchanged. The thin process pushes gensteps into the same
  `SEvt::Get_EGPU()->gs` vector that the fork did, so EventAction's
  consumer side is unaffected.
- `src/SimphonyStepAction.cc` — still snoops new gensteps in
  `SEvt::Get_EGPU()->gs` and records TrackID provenance. Works unchanged
  because the genstep struct format is identical (same
  `CollectGenstep_DsG4Scintillation_r4695` call).
- `src/plugin_entry.cc` — only `CreatePhysicsConstructor` matters for the
  scint swap, and that still hands back an `SimphonyPhysicsSwap` instance.
- GDML, including `SCINTILLATIONYIELD = 51282 photons/MeV`. Under the thin
  path this becomes dead data for the photon-count calculation (visE comes
  from DokeBirks directly). It is still read by stock `G4Scintillation` if
  the fork is selected, so we leave it in place.

## Environment variables after this change

| Env var | Old behaviour | New behaviour |
|---|---|---|
| `EDEPSIM_DOKEBIRKS_VISE` | Switches fork between DokeBirks-visE path and inline-Birks path | Unchanged. Only meaningful when `EDEP_SIMPHONY_SCINT=fork`. Irrelevant to the thin path (which always uses DokeBirks visE). |
| `EDEP_SIMPHONY_SCINT` | n/a | `thin` (default) → new `SimphonyScintProcess`. `fork` → legacy `Local_DsG4Scintillation`. |
| `OPTICKS_INTEGRATION_MODE` | `=1` required for EGPU SEvt | Unchanged. |

## Validation plan

After build & install:

1. **Smoke test:** run `tests/run.sh` (or the GPU subset). Expect plugin
   load messages including
   `[SimphonyPlugin] Using SimphonyScintProcess (thin, DokeBirks visE)`.

2. **Per-event hit counts vs fork.** Run the same N events under
   `EDEP_SIMPHONY_SCINT=thin` and `EDEP_SIMPHONY_SCINT=fork`. Distributions
   should be statistically consistent (Poisson check on mean; KS on
   per-event hit count).

3. **Per-step photon counts vs fork.** Grep
   `[SimphonyPlugin][DBG] step trk=… photons_to_optix=…` from both runs;
   ratios should be ~1 ± Poisson.

4. **Wavelength spectrum vs fork.** KS test on `GPUPhotonHits.Wavelength`
   from the two runs. Expect p > 0.05.

5. **Sanity: dormant fork still works.** With `EDEP_SIMPHONY_SCINT=fork`,
   reproduce the 2026-05-25 reference numbers (CPU=25 hits, GPU=34 hits
   over 5 events).

## Known limitations / future work

- **No AtRest scintillation.** Stopped alphas / muons in LAr would need
  `G4VRestDiscreteProcess` and an `AtRestDoIt`. Add later if needed; not
  required for 1 MeV e⁻ tests.
- **No re-emission/WLS in the thin process.** WLS is handled on GPU via
  OptiX shaders; CPU optical photons are killed by `SetKillOpticalPhotons(true)`
  in [SimphonyRunAction.cc:95](src/SimphonyRunAction.cc#L95). Consistent with
  current fork behaviour in `m_opticksMode=1`.
- **W=19.5 eV appears in three places** (DokeBirks line 322, GDML
  `SCINTILLATIONYIELD = 51282 = 1/19.5e-6`, thin process `kW_LAr_MeV`).
  Under the thin path only DokeBirks and the thin process need to agree;
  GDML becomes dead documentation. Could be unified into a shared header
  in a later refactor.
- **Cerenkov untouched.** Still uses `Local_G4Cerenkov_modified` from the
  fork. Out of scope for this change.

## Open questions resolved before drafting

- Leave fork dormant (selectable via env var), not deleted. ✓
- Match current behaviour: two gensteps (fast + slow), CPU-side Poisson
  split. ✓
- Approach (a2): plugin-owned thin process reading DokeBirks visE,
  *not* (a1) which would rely on Geant4 process ordering. ✓
