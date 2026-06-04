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

#include <cstdlib>
#include <iostream>
#include <string>

SimphonyPhysicsSwap::SimphonyPhysicsSwap()
    : G4VPhysicsConstructor("SimphonyPhysicsSwap")
{}

void SimphonyPhysicsSwap::ConstructProcess()
{
    // Allocate the instrumented processes once
    auto* cerenkov = new Local_G4Cerenkov_modified();
    cerenkov->SetMaxNumPhotonsPerStep(10000);
    cerenkov->SetMaxBetaChangePerStep(10.0);
    cerenkov->SetTrackSecondariesFirst(true);

    // EDEP_SIMPHONY_SCINT selects which scintillation process to install:
    //   "thin" (default) → plugin-owned SimphonyScintProcess (DokeBirks visE)
    //   "fork"           → legacy Local_DsG4Scintillation from eic-opticks/u4
    const char* mode_c = std::getenv("EDEP_SIMPHONY_SCINT");
    const std::string mode = mode_c ? std::string(mode_c) : std::string("thin");
    const bool useFork = (mode == "fork");

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
            if (cerenkov->IsApplicable(*particle)) {
                pmanager->AddProcess(cerenkov);
                pmanager->SetProcessOrdering(cerenkov, idxPostStep);
            }
            if (scintillation->IsApplicable(*particle)) {
                pmanager->AddProcess(scintillation);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
                // Fork is G4VRestDiscreteProcess and needs AtRest ordering for
                // stopped alphas etc. Thin process is PostStep-only.
                if (useFork) {
                    pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
                }
            }
        } else {
            // opticalphoton: scintillation (for re-emission) only.
            // Only the fork is applicable to opticalphoton (re-emission/WLS).
            // The thin process declines via IsApplicable(opticalphoton)=false.
            if (scintillation->IsApplicable(*particle)) {
                pmanager->AddProcess(scintillation);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
                if (useFork) {
                    pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
                }
            }
        }
    }

    std::cout << "[SimphonyPlugin] Instrumented Cerenkov + Scintillation processes installed\n";
}
