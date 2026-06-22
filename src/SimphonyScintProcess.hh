#ifndef OPTICS_SCINT_PROCESS_HH
#define OPTICS_SCINT_PROCESS_HH

#include <G4VRestDiscreteProcess.hh>
#include <G4ForceCondition.hh>

class G4Track;
class G4Step;
class G4ParticleDefinition;
class G4VParticleChange;

// G4VRestDiscreteProcess (matching stock G4Scintillation) so scintillation also
// fires on a charged particle's final AtRest step, not only during flight.
// Both the PostStep and AtRest paths funnel through the same Scintillate()
// helper, which builds the gensteps from the step's energy deposit.
class SimphonyScintProcess : public G4VRestDiscreteProcess
{
public:
    explicit SimphonyScintProcess(const G4String& name = "Scintillation");
    ~SimphonyScintProcess() override = default;

    G4bool IsApplicable(const G4ParticleDefinition& p) override;

    // ── PostStep (in-flight) interface ───────────────────────────────────
    G4double GetMeanFreePath(const G4Track& aTrack,
                             G4double previousStepSize,
                             G4ForceCondition* condition) override;

    G4VParticleChange* PostStepDoIt(const G4Track& aTrack,
                                    const G4Step& aStep) override;

    // ── AtRest interface (added by G4VRestDiscreteProcess) ───────────────
    G4double GetMeanLifeTime(const G4Track& aTrack,
                             G4ForceCondition* condition) override;

    G4VParticleChange* AtRestDoIt(const G4Track& aTrack,
                                  const G4Step& aStep) override;

private:
    // Shared scintillation work for both PostStepDoIt and AtRestDoIt.
    G4VParticleChange* Scintillate(const G4Track& aTrack, const G4Step& aStep);
};

#endif // OPTICS_SCINT_PROCESS_HH
