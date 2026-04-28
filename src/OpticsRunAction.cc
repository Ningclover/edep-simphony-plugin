#include "OpticsRunAction.hh"
#include "OpticsEventAction.hh"

#include <G4Run.hh>
#include <G4RunManager.hh>
#include <G4TransportationManager.hh>
#include <G4VPersistencyManager.hh>
#include <G4UserStackingAction.hh>
#include "EDepSimUserStackingAction.hh"

#include "G4CXOpticks.hh"
#include "SEvt.hh"
#include "SEventConfig.hh"
#include "LArTPCSensorIdentifier.h"
// OPTICKS_INTEGRATION_MODE=1 must be set in the environment for GPU-only mode.
// G4CXOpticks::SetGeometry() calls SEvt::CreateOrReuse() internally.

// edep-sim ROOT persistency
#include "EDepSimRootPersistencyManager.hh"

#include <TFile.h>
#include <TTree.h>
#include <TLorentzVector.h>

#include <iostream>

// Per-event branch buffers (filled by OpticsEventAction, stored here as statics
// so TTree branches keep valid pointers across events).
static int              gEventId   = -1;
static int              gTrackId   = -1;
static int              gProcess   = -1;  // 0=Cerenkov, 2=Scintillation
static float            gWavelength = 0;  // nm
static TLorentzVector   gHitPos;          // mm, ns
static TLorentzVector   gStartPos;        // mm, ns (genstep step position)

OpticsRunAction::OpticsRunAction(const char* /*option*/)
{}

void OpticsRunAction::BeginOfRunAction(const G4Run* /*run*/)
{
    // ── 1. Translate Geant4 geometry → CSGFoundry and init OptiX ──────────
    G4Navigator* nav = G4TransportationManager::GetTransportationManager()
                           ->GetNavigatorForTracking();
    const G4VPhysicalVolume* world = nav->GetWorldVolume();
    if (!world) {
        std::cerr << "[OpticsPlugin] ERROR: world volume not available in BeginOfRunAction\n";
        return;
    }

    // Must call SEvt::CreateOrReuse() BEFORE SetGeometry so that scontext
    // initializes CUDA and sets SEventConfig::_DeviceName (HasDevice()=true).
    // Without this, SetGeometry skips CSGOptiX::Create and SEvt stays null.
    // This mirrors what G4CXOpticks::SetGeometry_JUNO does.
    // OPTICKS_INTEGRATION_MODE=1 (GPU-only) creates the EGPU SEvt instance.
    SEvt::CreateOrReuse();

    // The lighttrap.gdml SurfaceDetector registration calls
    // SetKillOpticalPhotons(false) so Geant4 would track photons on CPU.
    // Override that: we kill photons on CPU and let the GPU handle transport.
    auto* sa = dynamic_cast<EDepSim::UserStackingAction*>(
                   const_cast<G4UserStackingAction*>(
                       G4RunManager::GetRunManager()->GetUserStackingAction()));
    if (sa) {
        sa->SetKillOpticalPhotons(true);
        std::cout << "[OpticsPlugin] Forced KillOpticalPhotons=true (GPU handles transport)\n";
    }

    // Register custom sensor identifier before geometry translation so that
    // eic-opticks recognises our SiPM volumes (which have SDs but aren't
    // named "PMT" as the default identifier requires).
    G4CXOpticks::SetSensorIdentifier(new LArTPCSensorIdentifier());

    G4CXOpticks::SetGeometry(world);
    std::cout << "[OpticsPlugin] G4CXOpticks geometry set, OptiX ready\n";

    // ── 2. Create parallel GPU hit TTree in the edep-sim ROOT file ─────────
    auto* pm = dynamic_cast<EDepSim::RootPersistencyManager*>(
                   G4VPersistencyManager::GetPersistencyManager());
    if (!pm || !pm->IsOpen()) {
        std::cerr << "[OpticsPlugin] WARNING: ROOT file not open yet; "
                     "GPU hit tree will not be created.\n"
                     "  Make sure /edep/db/open is called before /run/beamOn.\n";
        return;
    }

    TFile* f = pm->GetTFile();
    f->cd();

    fGPUTree = new TTree("GPUPhotonHits", "GPU optical photon hits (eic-opticks)");
    fGPUTree->Branch("EventId",    &gEventId);
    fGPUTree->Branch("TrackId",    &gTrackId);    // G4 TrackID of charged parent
    fGPUTree->Branch("Process",    &gProcess);    // 0=Cerenkov, 2=Scintillation
    fGPUTree->Branch("Wavelength", &gWavelength); // nm
    fGPUTree->Branch("HitPos",    "TLorentzVector", &gHitPos);    // mm, ns
    fGPUTree->Branch("StartPos",  "TLorentzVector", &gStartPos);  // mm, ns

    std::cout << "[OpticsPlugin] GPUPhotonHits TTree created in ROOT file\n";
}

void OpticsRunAction::EndOfRunAction(const G4Run* /*run*/)
{
    G4CXOpticks::Finalize();
    std::cout << "[OpticsPlugin] G4CXOpticks finalized\n";
}

// ── Accessors used by OpticsEventAction ────────────────────────────────────
int&            GetGEventId()    { return gEventId;   }
int&            GetGTrackId()    { return gTrackId;   }
int&            GetGProcess()    { return gProcess;   }
float&          GetGWavelength() { return gWavelength; }
TLorentzVector& GetGHitPos()     { return gHitPos;    }
TLorentzVector& GetGStartPos()   { return gStartPos;  }
