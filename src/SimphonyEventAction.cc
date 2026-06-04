#include "SimphonyEventAction.hh"
#include "SimphonyRunAction.hh"
#include "SimphonyStepAction.hh"

#include <G4Event.hh>
#include <G4PrimaryVertex.hh>
#include <G4PrimaryParticle.hh>
#include <G4ParticleDefinition.hh>
#include <G4SystemOfUnits.hh>

#include "G4CXOpticks.hh"
#include "SEvt.hh"
#include "sphoton.h"
#include "sgs.h"
#include "squad.h"

#include <TTree.h>
#include <TLorentzVector.h>

#include <iostream>

// Branch buffer accessors defined in SimphonyRunAction.cc
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

SimphonyEventAction::SimphonyEventAction(SimphonyRunAction* runAction)
    : fRunAction(runAction)
{}

void SimphonyEventAction::BeginOfEventAction(const G4Event* event)
{
    fGenstepTrackIds.clear();

    SEvt* sev = SEvt::Get_EGPU();
    fGenstepCountAtEventStart = sev ? sev->getNumGenstepCollected() : 0;

    // Sync the step action's baseline to match
    if (fStepAction) fStepAction->ResetGenstepBaseline(fGenstepCountAtEventStart);

    // ── (1) Report primary particle for this event ───────────────────────
    int eid = event->GetEventID();
    int nVtx = event->GetNumberOfPrimaryVertex();
    for (int iv = 0; iv < nVtx; ++iv) {
        G4PrimaryVertex* v = event->GetPrimaryVertex(iv);
        if (!v) continue;
        for (G4PrimaryParticle* p = v->GetPrimary(); p != nullptr; p = p->GetNext()) {
            const G4ParticleDefinition* pdef = p->GetParticleDefinition();
            G4String pname = pdef ? pdef->GetParticleName() : G4String("(unknown)");
            double  ke_MeV = p->GetKineticEnergy() / MeV;
            std::cout << "[SimphonyPlugin][DBG] Event " << eid
                      << " primary: " << pname
                      << "  KE=" << ke_MeV << " MeV"
                      << "  dir=(" << p->GetMomentumDirection().x() << ","
                                   << p->GetMomentumDirection().y() << ","
                                   << p->GetMomentumDirection().z() << ")\n";
        }
    }
}

void SimphonyEventAction::RecordGenstep(int64_t genstepIdx, int trackId)
{
    fGenstepTrackIds.emplace_back(genstepIdx, trackId);
}

int SimphonyEventAction::RecoverTrackId(int photonIndex) const
{
    // Walk the genstep summary vector to find which genstep produced photon[photonIndex]
    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) return -1;

    const std::vector<sgs>& gs_vec = sev->gs;
    for (const sgs& gs : gs_vec) {
        if (photonIndex >= gs.offset && photonIndex < gs.offset + gs.photons) {
            // Prefer the provenance map recorded by SimphonyStepAction
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

void SimphonyEventAction::EndOfEventAction(const G4Event* event)
{
    G4CXOpticks* gx = G4CXOpticks::Get();
    if (!gx) { std::cout << "[SimphonyPlugin] EndOfEvent: G4CXOpticks::Get() = null\n"; return; }

    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) { std::cout << "[SimphonyPlugin] EndOfEvent: SEvt::Get_EGPU() = null\n"; return; }

    int64_t ngenstep = sev->getNumGenstepCollected();
    std::cout << "[SimphonyPlugin] EndOfEvent: ngenstep=" << ngenstep << "\n";
    if (ngenstep == 0) {
        gx->reset(event->GetEventID());
        return;
    }

    // ── 1. Run GPU optical photon simulation ──────────────────────────────
    int64_t nphoton = sev->getNumPhotonCollected();
    std::cout << "[SimphonyPlugin] EndOfEvent: nphoton=" << nphoton << "\n";
    gx->simulate(event->GetEventID(), /*reset=*/false);

    // ── 2. Collect hits ───────────────────────────────────────────────────
    int64_t nhit = SEvt::GetNumHit_EGPU();
    std::cout << "[SimphonyPlugin] EndOfEvent: nhit=" << nhit << "\n";
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
