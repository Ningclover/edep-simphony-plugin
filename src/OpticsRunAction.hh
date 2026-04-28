#pragma once
#include <G4UserRunAction.hh>

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

private:
    TTree* fGPUTree = nullptr;
};
