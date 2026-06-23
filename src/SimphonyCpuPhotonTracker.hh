#pragma once
#include <G4UserTrackingAction.hh>

class G4OpBoundaryProcess;

/// External G4UserTrackingAction (installed into edep-sim's UserTrackingAction
/// via /edep/actions/loadUserTrackAction) that records the FATE of every
/// CPU-tracked optical photon — detected or not, and the reason it ended —
/// into the CPUPhotonTracks TTree created by SimphonyRunAction.
///
/// The full step-by-step path of each photon is saved separately by edep-sim
/// itself into the Trajectories branch when
///   /edep/db/set/savePhotonTrajectories true
/// is set in the macro. This action adds only the per-photon termination
/// reason, which edep-sim does not otherwise store.
class SimphonyCpuPhotonTracker : public G4UserTrackingAction
{
public:
    SimphonyCpuPhotonTracker() = default;
    ~SimphonyCpuPhotonTracker() override = default;

    void PreUserTrackingAction(const G4Track* track) override;
    void PostUserTrackingAction(const G4Track* track) override;

private:
    bool                 fBoundaryCached = false;
    G4OpBoundaryProcess* fBoundary       = nullptr;
};
