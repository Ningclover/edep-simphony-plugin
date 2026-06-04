#pragma once
#include <G4UserSteppingAction.hh>
#include <cstdint>

class SimphonyEventAction;

/// Per-step hook: after each step, checks how many new gensteps the
/// instrumented Cerenkov/Scintillation processes added to SEvt and records
/// the corresponding G4 TrackID for provenance.
class SimphonyStepAction : public G4UserSteppingAction
{
public:
    SimphonyStepAction(SimphonyEventAction* eventAction);
    ~SimphonyStepAction() override = default;

    void UserSteppingAction(const G4Step* step) override;

    /// Called by SimphonyEventAction::BeginOfEventAction to sync the baseline count.
    void ResetGenstepBaseline(int64_t baseline) { fLastGenstepCount = baseline; }

private:
    SimphonyEventAction* fEventAction;
    int64_t fLastGenstepCount = 0;
};
