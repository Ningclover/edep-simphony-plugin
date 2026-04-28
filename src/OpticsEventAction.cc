#include "OpticsEventAction.hh"
#include "OpticsRunAction.hh"
#include "OpticsStepAction.hh"

#include <G4Event.hh>

#include "G4CXOpticks.hh"
#include "SEvt.hh"
#include "sphoton.h"
#include "sgs.h"
#include "squad.h"

#include <TTree.h>
#include <TLorentzVector.h>

#include <iostream>

// Branch buffer accessors defined in OpticsRunAction.cc
int&            GetGEventId();
int&            GetGTrackId();
int&            GetGProcess();
float&          GetGWavelength();
TLorentzVector& GetGHitPos();
TLorentzVector& GetGStartPos();

// Geant4 mm → ROOT mm (same), Geant4 ns → ROOT ns (same)
static constexpr double kMM = 1.0;
static constexpr double kNS = 1.0;
// Photon energy → wavelength: E [MeV] → wavelength [nm]
// hc = 1.23984e-3 MeV·nm
static constexpr double kHC_MeV_nm = 1.23984e-3;

// OpticksGenstep gentype codes
static constexpr int kGenCerenkov      = 1;  // OpticksGenstep_G4Cerenkov_1042
static constexpr int kGenScintillation = 2;  // OpticksGenstep_DsG4Scintillation_r4695

OpticsEventAction::OpticsEventAction(OpticsRunAction* runAction)
    : fRunAction(runAction)
{}

void OpticsEventAction::BeginOfEventAction(const G4Event* event)
{
    fGenstepTrackIds.clear();

    SEvt* sev = SEvt::Get_EGPU();
    fGenstepCountAtEventStart = sev ? sev->getNumGenstepCollected() : 0;

    // Sync the step action's baseline to match
    if (fStepAction) fStepAction->ResetGenstepBaseline(fGenstepCountAtEventStart);
}

void OpticsEventAction::RecordGenstep(int64_t genstepIdx, int trackId)
{
    fGenstepTrackIds.emplace_back(genstepIdx, trackId);
}

int OpticsEventAction::RecoverTrackId(int photonIndex) const
{
    // Walk the genstep summary vector to find which genstep produced photon[photonIndex]
    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) return -1;

    const std::vector<sgs>& gs_vec = sev->gs;
    for (const sgs& gs : gs_vec) {
        if (photonIndex >= gs.offset && photonIndex < gs.offset + gs.photons) {
            // Prefer the provenance map recorded by OpticsStepAction
            for (const auto& [idx, tid] : fGenstepTrackIds) {
                if (idx == gs.index) return tid;
            }
            // Fallback: read trackid directly from the quad6 genstep data
            const quad6* gsdata = sev->getGenstepVecData();
            if (gsdata) return static_cast<int>(gsdata[gs.index].trackid());
            return -1;
        }
    }
    return -1;
}

void OpticsEventAction::EndOfEventAction(const G4Event* event)
{
    G4CXOpticks* gx = G4CXOpticks::Get();
    if (!gx) { std::cout << "[OpticsPlugin] EndOfEvent: G4CXOpticks::Get() = null\n"; return; }

    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) { std::cout << "[OpticsPlugin] EndOfEvent: SEvt::Get_EGPU() = null\n"; return; }

    int64_t ngenstep = sev->getNumGenstepCollected();
    std::cout << "[OpticsPlugin] EndOfEvent: ngenstep=" << ngenstep << "\n";
    if (ngenstep == 0) {
        gx->reset(event->GetEventID());
        return;
    }

    // ── 1. Run GPU optical photon simulation ──────────────────────────────
    int64_t nphoton = sev->getNumPhotonCollected();
    std::cout << "[OpticsPlugin] EndOfEvent: nphoton=" << nphoton << "\n";
    gx->simulate(event->GetEventID(), /*reset=*/false);

    // ── 2. Collect hits ───────────────────────────────────────────────────
    int64_t nhit = SEvt::GetNumHit_EGPU();
    std::cout << "[OpticsPlugin] EndOfEvent: nhit=" << nhit << "\n";
    TTree* tree = fRunAction->GetGPUTree();

    if (nhit > 0 && tree) {
        int& evId    = GetGEventId();
        int& trackId = GetGTrackId();
        int& proc    = GetGProcess();
        float& wl    = GetGWavelength();
        TLorentzVector& hitPos   = GetGHitPos();
        TLorentzVector& startPos = GetGStartPos();

        evId = event->GetEventID();

        for (int64_t i = 0; i < nhit; ++i) {
            sphoton p;
            sev->getHit(p, static_cast<unsigned>(i));

            // Photon index within the event (used to find its genstep)
            int pIdx = static_cast<int>(p.get_index());

            trackId = RecoverTrackId(pIdx);

            // Process type from flagmask
            unsigned fm = p.flagmask;
            if      (fm & 0x0001) proc = 0;  // Cerenkov
            else if (fm & 0x0002) proc = 2;  // Scintillation
            else                  proc = -1;

            // Wavelength [nm] (stored directly in sphoton)
            wl = p.wavelength; // already in nm

            // Hit position (Geant4 mm, ns)
            hitPos.SetXYZT(p.pos.x * kMM, p.pos.y * kMM, p.pos.z * kMM,
                           p.time * kNS);

            // Start position: not stored per-hit; zero-fill
            startPos.SetXYZT(0, 0, 0, 0);

            tree->Fill();
        }
    }

    // ── 3. Reset SEvt for next event ─────────────────────────────────────
    gx->reset(event->GetEventID());
    fGenstepTrackIds.clear();
}
