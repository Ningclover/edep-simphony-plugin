#pragma once
#include <G4UserRunAction.hh>
#include <cstdint>

class TTree;
class TBranch;
struct GPUHitRecord;

/// Initialises G4CXOpticks at begin-of-run, opens a parallel GPU hit TTree
/// in the same ROOT file as edep-sim, and tears everything down at end-of-run.
class OpticsRunAction : public G4UserRunAction
{
public:
    explicit OpticsRunAction(const char* option);
    ~OpticsRunAction() override = default;

    void BeginOfRunAction(const G4Run* run) override;
    void EndOfRunAction(const G4Run* run) override;

    /// Accessed by OpticsEventAction to fill the tree each event.
    TTree* GetGPUTree() const { return fGPUTree; }

    // ── Debug accumulators (process-wide, reset at BeginOfRun) ───────────
    // (4) total photon count from edep-sim's DokeBirks visE / 19.5 eV
    // (5) total photons accepted by eic-opticks (sum of sgs.photons)
    static double   sTotalEdepSimPhotons;     // photon-equivalents from DokeBirks
    static int64_t  sTotalOpticksGenstepPhotons;
    static int64_t  sStepCountInLAr;          // for sampling stats
    static int64_t  sNewGenstepsThisRun;

private:
    TTree* fGPUTree = nullptr;
};
