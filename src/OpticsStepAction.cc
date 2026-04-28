#include "OpticsStepAction.hh"
#include "OpticsEventAction.hh"

#include <G4Step.hh>
#include <G4Track.hh>

#include "SEvt.hh"

OpticsStepAction::OpticsStepAction(OpticsEventAction* eventAction)
    : fEventAction(eventAction)
{}

void OpticsStepAction::UserSteppingAction(const G4Step* step)
{
    // This action runs AFTER all process PostStepDoIt calls for this step.
    // If the instrumented Cerenkov or Scintillation process fired, it will
    // have already called U4::CollectGenstep_*() → SEvt::AddGenstep().
    // We compare the current genstep count to what it was before the step.

    SEvt* sev = SEvt::Get_EGPU();
    if (!sev) return;

    int64_t currentCount = sev->getNumGenstepCollected();
    if (currentCount <= fLastGenstepCount) {
        fLastGenstepCount = currentCount;
        return;
    }

    // New gensteps were added by this step — record TrackID for each
    int trackId = step->GetTrack()->GetTrackID();
    for (int64_t idx = fLastGenstepCount; idx < currentCount; ++idx) {
        fEventAction->RecordGenstep(idx, trackId);
    }
    fLastGenstepCount = currentCount;
}
