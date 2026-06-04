#include "SimphonyStepAction.hh"
#include "SimphonyEventAction.hh"
#include "SimphonyRunAction.hh"

#include <G4Step.hh>
#include <G4Track.hh>
#include <G4ParticleDefinition.hh>
#include <G4Material.hh>
#include <G4SystemOfUnits.hh>

#include "SEvt.hh"
#include "sgs.h"

#include <iostream>
#include <iomanip>
#include <cctype>

// DokeBirks constants (mirrors EDepSimDokeBirksSaturation.cc — see
// my_optic_wiki "EDepSim Photon Yield Pipeline")
static constexpr double kW_LAr_MeV     = 19.5e-6;   // 19.5 eV per quantum
static constexpr double kNexOverNq_LAr = 0.21 / 1.21;

// Sampling: print dE/dx etc. for 1 in every kSampleStride charged-in-LAr steps,
// and for the first kAlwaysPrintFirstN steps regardless.
static constexpr int64_t kSampleStride       = 5;
static constexpr int64_t kAlwaysPrintFirstN  = 3;

SimphonyStepAction::SimphonyStepAction(SimphonyEventAction* eventAction)
    : fEventAction(eventAction)
{}

void SimphonyStepAction::UserSteppingAction(const G4Step* step)
{
    const G4Track* trk = step->GetTrack();
    const G4ParticleDefinition* pdef = trk ? trk->GetParticleDefinition() : nullptr;
    bool isCharged = pdef && pdef->GetPDGCharge() != 0.0;

    const G4StepPoint* prePt = step->GetPreStepPoint();
    const G4Material*  mat   = prePt ? prePt->GetMaterial() : nullptr;
    G4String matName = mat ? mat->GetName() : G4String("");
    G4String matLower = matName;
    for (auto& c : matLower) c = std::tolower(c);
    // matches "G4_lAr", "LAr", "liquidArgon", "lar", "liquid_argon", etc.
    bool inLAr = matLower.find("lar") != std::string::npos
                 || matLower.find("liquidargon") != std::string::npos
                 || matLower.find("liquid_argon") != std::string::npos;

    // ── (2) Random-ish sampled print: dE, dE/dx, recombination, N_photons ─
    if (isCharged && inLAr) {
        SimphonyRunAction::sStepCountInLAr += 1;

        double dE      = step->GetTotalEnergyDeposit();          // MeV
        double dEvis   = step->GetNonIonizingEnergyDeposit();    // MeV (DokeBirks visE)
        double dx      = step->GetStepLength();                  // mm
        double dEdx    = (dx > 0.0) ? (dE / dx) : 0.0;           // MeV/mm
        double Nph     = (dEvis > 0.0) ? (dEvis / kW_LAr_MeV) : 0.0;

        // Accumulate run total (request item 4)
        SimphonyRunAction::sTotalEdepSimPhotons += Nph;

        // Recombination factor r recovered from the visE/dE ratio:
        //   visE/dE = Nph/Nq = Nex/Nq + (Nion/Nq) * r
        //           = 0.21/1.21 + (1 - 0.21/1.21) * r
        // → r = (visE/dE - Nex/Nq) / (1 - Nex/Nq)
        double r = -1.0;
        if (dE > 0.0) {
            double ratio = dEvis / dE;
            r = (ratio - kNexOverNq_LAr) / (1.0 - kNexOverNq_LAr);
        }

        bool doPrint = (SimphonyRunAction::sStepCountInLAr <= kAlwaysPrintFirstN)
                       || (SimphonyRunAction::sStepCountInLAr % kSampleStride == 0);
        if (doPrint) {
            std::cout << std::fixed << std::setprecision(4)
                      << "[SimphonyPlugin][DBG] step#" << SimphonyRunAction::sStepCountInLAr
                      << " trk=" << trk->GetTrackID()
                      << " " << pdef->GetParticleName()
                      << "  dE="    << dE   * 1e3 << " keV"
                      << "  dx="    << dx   << " mm"
                      << "  dE/dx=" << dEdx << " MeV/mm"
                      << "  visE="  << dEvis * 1e3 << " keV"
                      << "  r="     << r
                      << "  Nph="   << Nph
                      << "\n";
            std::cout.unsetf(std::ios::fixed);
        }
    }

    // ── Genstep tracking (existing behavior) + (3) per-step photons-to-GPU ─
    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) return;

    int64_t currentCount = sev->getNumGenstepCollected();
    if (currentCount <= fLastGenstepCount) {
        fLastGenstepCount = currentCount;
        return;
    }

    // New gensteps were added by this step — record TrackID for each and
    // print how many photons each genstep handed to eic-opticks.
    int trackId = trk ? trk->GetTrackID() : -1;
    const std::vector<sgs>& gs_vec = sev->gs;
    int64_t stepPhotons = 0;
    int64_t stepNewGS   = currentCount - fLastGenstepCount;

    for (int64_t idx = fLastGenstepCount; idx < currentCount; ++idx) {
        fEventAction->RecordGenstep(idx, trackId);
        // sgs index `idx` matches sgs.index field; the vector is push_back-ordered
        // so the i-th entry has index == i. Look up by position.
        if (idx >= 0 && static_cast<size_t>(idx) < gs_vec.size()) {
            stepPhotons += gs_vec[idx].photons;
        }
    }
    SimphonyRunAction::sTotalSimphonyGenstepPhotons += stepPhotons;
    SimphonyRunAction::sNewGenstepsThisRun        += stepNewGS;

    std::cout << "[SimphonyPlugin][DBG] step trk=" << trackId
              << " new_gensteps=" << stepNewGS
              << " photons_to_optix=" << stepPhotons << "\n";

    fLastGenstepCount = currentCount;
}
