#pragma once
#include <G4UserSteppingAction.hh>
#include <cstdint>

class OpticsEventAction;

/// Per-step hook: after each step, checks how many new gensteps the
/// instrumented Cerenkov/Scintillation processes added to SEvt and records
/// the corresponding G4 TrackID for provenance.
class OpticsStepAction : public G4UserSteppingAction
{
public:
    OpticsStepAction(OpticsEventAction* eventAction);
    ~OpticsStepAction() override = default;

    void UserSteppingAction(const G4Step* step) override;

    /// Called by OpticsEventAction::BeginOfEventAction to sync the baseline count.
    void ResetGenstepBaseline(int64_t baseline) { fLastGenstepCount = baseline; }

private:
    OpticsEventAction* fEventAction;
    int64_t fLastGenstepCount = 0;
};
