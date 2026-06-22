#include "SimphonyPhysicsSwap.hh"

#include <G4ParticleTable.hh>
#include <G4ProcessManager.hh>
#include <G4ProcessVector.hh>
#include <G4VProcess.hh>

// eic-opticks instrumented processes (compiled with -DSTANDALONE in libU4.so)
#include "Local_G4Cerenkov_modified.hh"
#include "Local_DsG4Scintillation.hh"

// Plugin-owned thin scintillation process (DokeBirks visE → genstep)
#include "SimphonyScintProcess.hh"

// Stock Geant4 scintillation, used only in DUAL debug mode so the CPU also
// generates + tracks real optical photons alongside the GPU genstep path.
#include "G4Scintillation.hh"
#include "G4EmParameters.hh"
#include "G4EmSaturation.hh"

#include <cstdlib>
#include <iostream>
#include <string>

SimphonyPhysicsSwap::SimphonyPhysicsSwap()
    : G4VPhysicsConstructor("SimphonyPhysicsSwap")
{}

void SimphonyPhysicsSwap::ConstructProcess()
{
    // EDEP_SIMPHONY_CERENKOV gates the instrumented Cerenkov process:
    //   "1"/"true"/"on" (default) → install instrumented Cerenkov
    //   "0"/"false"/"off"         → skip it (scintillation-only comparison)
    const char* cer_c = std::getenv("EDEP_SIMPHONY_CERENKOV");
    const std::string cer = cer_c ? std::string(cer_c) : std::string("1");
    const bool useCerenkov = !(cer == "0" || cer == "false" || cer == "off");

    // Allocate the instrumented Cerenkov process once (only if enabled)
    Local_G4Cerenkov_modified* cerenkov = nullptr;
    if (useCerenkov) {
        cerenkov = new Local_G4Cerenkov_modified();
        cerenkov->SetMaxNumPhotonsPerStep(10000);
        cerenkov->SetMaxBetaChangePerStep(10.0);
        cerenkov->SetTrackSecondariesFirst(true);
    } else {
        std::cout << "[SimphonyPlugin] Cerenkov DISABLED "
                     "— EDEP_SIMPHONY_CERENKOV=" << cer << "\n";
    }

    // EDEP_SIMPHONY_SCINT selects which scintillation process to install:
    //   "thin" (default) → plugin-owned SimphonyScintProcess (DokeBirks visE)
    //   "fork"           → legacy Local_DsG4Scintillation from eic-opticks/u4
    const char* mode_c = std::getenv("EDEP_SIMPHONY_SCINT");
    const std::string mode = mode_c ? std::string(mode_c) : std::string("thin");
    const bool useFork = (mode == "fork");

    // EDEP_SIMPHONY_DUAL: debug mode that ALSO installs stock G4Scintillation
    // (with photon stacking on) next to the genstep emitter, so the CPU
    // generates + tracks real optical photons into Event.PhotonDetectors while
    // the GPU path fills GPUPhotonHits — both in one ROOT file from one run.
    // NOTE: generation is sampled twice (CPU Poisson vs genstep), so the
    // per-event CPU/GPU comparison is statistical, not photon-for-photon.
    // Pairs with SetKillOpticalPhotons(false) in SimphonyRunAction (also gated).
    const char* dual_c = std::getenv("EDEP_SIMPHONY_DUAL");
    const std::string dual = dual_c ? std::string(dual_c) : std::string("0");
    const bool useDual = (dual == "1" || dual == "true" || dual == "on");

    G4VProcess* scintillation = nullptr;
    if (useFork) {
        auto* s = new Local_DsG4Scintillation(1);
        s->SetTrackSecondariesFirst(true);
        scintillation = s;
        std::cout << "[SimphonyPlugin] Using Local_DsG4Scintillation (fork) "
                     "— EDEP_SIMPHONY_SCINT=fork\n";
    } else {
        scintillation = new SimphonyScintProcess();
        std::cout << "[SimphonyPlugin] Using SimphonyScintProcess (thin, "
                     "DokeBirks visE) — EDEP_SIMPHONY_SCINT=" << mode << "\n";
    }

    // Stock G4Scintillation for the DUAL debug path: produces real CPU
    // optical-photon secondaries (stacked + tracked to the SurfaceSD).
    G4Scintillation* cpuScint = nullptr;
    if (useDual) {
        cpuScint = new G4Scintillation();
        cpuScint->SetStackPhotons(true);
        cpuScint->SetTrackSecondariesFirst(true);
        // Apply the SAME DokeBirks quenching the GPU genstep path uses. Without
        // this, a bare G4Scintillation has fEmSaturation=nullptr and counts raw
        // TotalEnergyDeposit (visE=dE) -> ~3x too many photons. The EmSaturation
        // in G4EmParameters is the one SimphonyScintProcess reads via
        // GetEmSaturation(), so CPU and GPU then use identical visible energy.
        G4EmSaturation* emSat = G4EmParameters::Instance()->GetEmSaturation();
        if (emSat) {
            cpuScint->AddSaturation(emSat);
            std::cout << "[SimphonyPlugin] DUAL: AddSaturation(DokeBirks) on "
                         "CPU G4Scintillation\n";
        } else {
            std::cout << "[SimphonyPlugin] DUAL WARNING: no EmSaturation in "
                         "G4EmParameters — CPU photons will be UNQUENCHED\n";
        }
        std::cout << "[SimphonyPlugin] DUAL mode: stock G4Scintillation "
                     "installed alongside genstep emitter (CPU tracks photons "
                     "→ PhotonDetectors; GPU → GPUPhotonHits)\n";
    }

    G4ParticleTable* table = G4ParticleTable::GetParticleTable();
    G4ParticleTable::G4PTblDicIterator* iter = table->GetIterator();
    iter->reset();

    while ((*iter)()) {
        G4ParticleDefinition* particle = iter->value();
        G4ProcessManager* pmanager = particle->GetProcessManager();
        if (!pmanager) continue;

        G4String pname = particle->GetParticleName();

        // ── Remove standard Cerenkov/Scintillation if present ──────────────
        G4ProcessVector* procs = pmanager->GetProcessList();
        std::vector<G4VProcess*> toRemove;
        for (int i = 0; i < (int)procs->size(); ++i) {
            G4String procName = (*procs)[i]->GetProcessName();
            if (procName == "Cerenkov" || procName == "Scintillation") {
                toRemove.push_back((*procs)[i]);
            }
        }
        for (auto* p : toRemove) {
            pmanager->RemoveProcess(p);
        }

        // ── Add instrumented processes to charged particles ─────────────────
        if (pname != "opticalphoton") {
            if (cerenkov && cerenkov->IsApplicable(*particle)) {
                pmanager->AddProcess(cerenkov);
                pmanager->SetProcessOrdering(cerenkov, idxPostStep);
            }
            if (scintillation->IsApplicable(*particle)) {
                pmanager->AddProcess(scintillation);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
                // Both the fork and the thin SimphonyScintProcess are now
                // G4VRestDiscreteProcess, so both need AtRest ordering for the
                // final stopped step of a charged track (matches stock
                // G4Scintillation, which fires AtRest too).
                pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
            }
            // DUAL: stock G4Scintillation produces the CPU-tracked photons.
            if (cpuScint && cpuScint->IsApplicable(*particle)) {
                pmanager->AddProcess(cpuScint);
                pmanager->SetProcessOrderingToLast(cpuScint, idxPostStep);
                pmanager->SetProcessOrderingToLast(cpuScint, idxAtRest);
            }
        } else {
            // opticalphoton: scintillation (for re-emission) only.
            // Only the fork is applicable to opticalphoton (re-emission/WLS).
            // The thin process declines via IsApplicable(opticalphoton)=false.
            if (scintillation->IsApplicable(*particle)) {
                pmanager->AddProcess(scintillation);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
                pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
            }
            // DUAL: stock G4Scintillation also handles opticalphoton re-emission.
            if (cpuScint && cpuScint->IsApplicable(*particle)) {
                pmanager->AddProcess(cpuScint);
                pmanager->SetProcessOrderingToLast(cpuScint, idxPostStep);
                pmanager->SetProcessOrderingToLast(cpuScint, idxAtRest);
            }
        }
    }

    std::cout << "[SimphonyPlugin] Instrumented "
              << (useCerenkov ? "Cerenkov + " : "")
              << "Scintillation processes installed\n";
}
