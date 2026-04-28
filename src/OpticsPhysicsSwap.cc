#include "OpticsPhysicsSwap.hh"

#include <G4ParticleTable.hh>
#include <G4ProcessManager.hh>
#include <G4ProcessVector.hh>

// eic-opticks instrumented processes (compiled with -DSTANDALONE in libU4.so)
#include "Local_G4Cerenkov_modified.hh"
#include "Local_DsG4Scintillation.hh"

#include <iostream>

OpticsPhysicsSwap::OpticsPhysicsSwap()
    : G4VPhysicsConstructor("OpticsPhysicsSwap")
{}

void OpticsPhysicsSwap::ConstructProcess()
{
    // Allocate the instrumented processes once
    auto* cerenkov = new Local_G4Cerenkov_modified();
    cerenkov->SetMaxNumPhotonsPerStep(10000);
    cerenkov->SetMaxBetaChangePerStep(10.0);
    cerenkov->SetTrackSecondariesFirst(true);

    // opticksMode=0 means the scintillation process runs normally
    // (genstep collection is done in its PostStepDoIt via U4::CollectGenstep_*)
    auto* scintillation = new Local_DsG4Scintillation(0);
    scintillation->SetTrackSecondariesFirst(true);

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
                pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
            }
        } else {
            // opticalphoton: scintillation (for re-emission) only
            if (scintillation->IsApplicable(*particle)) {
                pmanager->AddProcess(scintillation);
                pmanager->SetProcessOrderingToLast(scintillation, idxAtRest);
                pmanager->SetProcessOrderingToLast(scintillation, idxPostStep);
            }
        }
    }

    std::cout << "[OpticsPlugin] Instrumented Cerenkov + Scintillation processes installed\n";
}
