/**
 * plugin_entry.cc
 *
 * extern "C" factory functions loaded by edep-sim via dlopen() / dlsym().
 * Load in macro with:
 *   /edep/actions/loadUserRunAction   $(PLUGIN_LIB) CreateUserRunAction   ""
 *   /edep/actions/loadUserEventAction $(PLUGIN_LIB) CreateUserEventAction ""
 *   /edep/actions/loadUserStepAction  $(PLUGIN_LIB) CreateUserStepAction  ""
 *
 * The run action is created first; event and step actions receive a pointer
 * to it so they can access the GPU hit TTree.
 */

#include "SimphonyRunAction.hh"
#include "SimphonyEventAction.hh"
#include "SimphonyStepAction.hh"
#include "SimphonyPhysicsSwap.hh"
#include "SimphonyCpuPhotonTracker.hh"

#include <G4UserRunAction.hh>
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <G4UserTrackingAction.hh>
#include <G4VPhysicsConstructor.hh>

// Singleton pointers so event/step actions can find the run action.
static SimphonyRunAction*   gRunAction   = nullptr;
static SimphonyEventAction* gEventAction = nullptr;

extern "C" {

G4UserRunAction* CreateUserRunAction(const char* option)
{
    gRunAction = new SimphonyRunAction(option);
    return gRunAction;
}

G4UserEventAction* CreateUserEventAction(const char* option)
{
    // Run action must be created first (via the macro ordering).
    gEventAction = new SimphonyEventAction(gRunAction);
    return gEventAction;
}

G4UserSteppingAction* CreateUserStepAction(const char* option)
{
    // Event action must be created first.
    auto* sa = new SimphonyStepAction(gEventAction);
    // Let the event action reset the step action's baseline each event.
    if (gEventAction) gEventAction->SetStepAction(sa);
    return sa;
}

G4VPhysicsConstructor* CreatePhysicsConstructor(const char* /*option*/)
{
    return new SimphonyPhysicsSwap();
}

// Records the FATE of every CPU-tracked optical photon (DUAL mode). Installed
// as an external tracking action via:
//   /edep/actions/loadUserTrackAction $(PLUGIN_LIB) CreateUserTrackAction ""
G4UserTrackingAction* CreateUserTrackAction(const char* /*option*/)
{
    return new SimphonyCpuPhotonTracker();
}

} // extern "C"
