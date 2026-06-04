#ifndef OPTICS_SCINT_PROCESS_HH
#define OPTICS_SCINT_PROCESS_HH

#include <G4VDiscreteProcess.hh>
#include <G4ForceCondition.hh>

class G4Track;
class G4Step;
class G4ParticleDefinition;
class G4VParticleChange;

class SimphonyScintProcess : public G4VDiscreteProcess
{
public:
    explicit SimphonyScintProcess(const G4String& name = "Scintillation");
    ~SimphonyScintProcess() override = default;

    G4bool IsApplicable(const G4ParticleDefinition& p) override;

    G4double GetMeanFreePath(const G4Track& aTrack,
                             G4double previousStepSize,
                             G4ForceCondition* condition) override;

    G4VParticleChange* PostStepDoIt(const G4Track& aTrack,
                                    const G4Step& aStep) override;
};

#endif // OPTICS_SCINT_PROCESS_HH
