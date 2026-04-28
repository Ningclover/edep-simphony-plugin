#pragma once
#include <G4VPhysicsConstructor.hh>

/// G4VPhysicsConstructor that replaces G4Cerenkov + G4Scintillation (added by
/// G4OpticalPhysics) with the eic-opticks instrumented versions that call
/// U4::CollectGenstep_*() → SEvt::AddGenstep() at each step.
///
/// Loaded via EXTRAPHYSICS env var:
///   export EXTRAPHYSICS="EXTERN:$(PLUGIN_LIB):CreatePhysicsConstructor"
class OpticsPhysicsSwap : public G4VPhysicsConstructor
{
public:
    OpticsPhysicsSwap();
    ~OpticsPhysicsSwap() override = default;

    void ConstructParticle() override {}
    void ConstructProcess() override;
};
