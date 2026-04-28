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

#include "OpticsRunAction.hh"
#include "OpticsEventAction.hh"
#include "OpticsStepAction.hh"
#include "OpticsPhysicsSwap.hh"

#include <G4UserRunAction.hh>
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <G4VPhysicsConstructor.hh>

// Singleton pointers so event/step actions can find the run action.
static OpticsRunAction*   gRunAction   = nullptr;
static OpticsEventAction* gEventAction = nullptr;

extern "C" {

G4UserRunAction* CreateUserRunAction(const char* option)
{
    gRunAction = new OpticsRunAction(option);
    return gRunAction;
}

G4UserEventAction* CreateUserEventAction(const char* option)
{
    // Run action must be created first (via the macro ordering).
    gEventAction = new OpticsEventAction(gRunAction);
    return gEventAction;
}

G4UserSteppingAction* CreateUserStepAction(const char* option)
{
    // Event action must be created first.
    auto* sa = new OpticsStepAction(gEventAction);
    // Let the event action reset the step action's baseline each event.
    if (gEventAction) gEventAction->SetStepAction(sa);
    return sa;
}

G4VPhysicsConstructor* CreatePhysicsConstructor(const char* /*option*/)
{
    return new OpticsPhysicsSwap();
}

} // extern "C"
