#pragma once
#include <G4UserEventAction.hh>
#include <vector>
#include <cstdint>

class OpticsRunAction;

/// Runs the GPU optical photon simulation at end of each event and fills
/// the GPUPhotonHits TTree with the resulting hits + provenance.
class OpticsEventAction : public G4UserEventAction
{
public:
    explicit OpticsEventAction(OpticsRunAction* runAction);
    ~OpticsEventAction() override = default;

    void BeginOfEventAction(const G4Event* event) override;
    void EndOfEventAction(const G4Event* event) override;

    /// Called by OpticsStepAction to record genstep→TrackID provenance.
    void RecordGenstep(int64_t genstepIdx, int trackId);

    /// Register the step action so BeginOfEventAction can reset its baseline.
    void SetStepAction(class OpticsStepAction* sa) { fStepAction = sa; }

private:
    OpticsRunAction*   fRunAction;
    class OpticsStepAction* fStepAction = nullptr;

    std::vector<std::pair<int64_t, int>> fGenstepTrackIds;
    int64_t fGenstepCountAtEventStart = 0;

    int RecoverTrackId(int photonIndex) const;
};
