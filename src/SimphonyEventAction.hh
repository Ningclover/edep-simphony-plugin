#pragma once
#include <G4UserEventAction.hh>
#include <vector>
#include <cstdint>

class SimphonyRunAction;

/// Runs the GPU optical photon simulation at end of each event and fills
/// the GPUPhotonHits TTree with the resulting hits + provenance.
class SimphonyEventAction : public G4UserEventAction
{
public:
    explicit SimphonyEventAction(SimphonyRunAction* runAction);
    ~SimphonyEventAction() override = default;

    void BeginOfEventAction(const G4Event* event) override;
    void EndOfEventAction(const G4Event* event) override;

    /// Called by SimphonyStepAction to record genstep→TrackID provenance.
    void RecordGenstep(int64_t genstepIdx, int trackId);

    /// Register the step action so BeginOfEventAction can reset its baseline.
    void SetStepAction(class SimphonyStepAction* sa) { fStepAction = sa; }

private:
    SimphonyRunAction*   fRunAction;
    class SimphonyStepAction* fStepAction = nullptr;

    std::vector<std::pair<int64_t, int>> fGenstepTrackIds;
    int64_t fGenstepCountAtEventStart = 0;

    int RecoverTrackId(int photonIndex) const;
};
