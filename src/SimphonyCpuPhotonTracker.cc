#include "SimphonyCpuPhotonTracker.hh"
#include "SimphonyRunAction.hh"

#include <G4Track.hh>
#include <G4Step.hh>
#include <G4StepPoint.hh>
#include <G4VProcess.hh>
#include <G4ParticleDefinition.hh>
#include <G4OpBoundaryProcess.hh>
#include <G4ProcessManager.hh>
#include <G4EventManager.hh>
#include <G4Event.hh>
#include <G4PhysicalConstants.hh>
#include <G4SystemOfUnits.hh>

#include <TTree.h>
#include <TLorentzVector.h>

#include <cstring>
#include <string>

// Branch-buffer accessors defined in SimphonyRunAction.cc
int&            GetGcEventId();
int&            GetGcTrackId();
int&            GetGcParentId();
float&          GetGcWavelength();
float&          GetGcEnergy();
int&            GetGcDetected();
char*           GetGcFate();
char*           GetGcEndVolume();
int&            GetGcNStep();
TLorentzVector& GetGcStartPos();
TLorentzVector& GetGcEndPos();

static void set_cstr(char* dst, const std::string& s, size_t cap)
{
    std::strncpy(dst, s.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

void SimphonyCpuPhotonTracker::PreUserTrackingAction(const G4Track* track)
{
    // Only optical photons (PDG -22).
    if (track->GetParticleDefinition()->GetPDGEncoding() != -22) return;
    // Record the creation point as the start of the trajectory.
    GetGcStartPos().SetXYZT(track->GetVertexPosition().x(),
                            track->GetVertexPosition().y(),
                            track->GetVertexPosition().z(),
                            track->GetGlobalTime());
}

void SimphonyCpuPhotonTracker::PostUserTrackingAction(const G4Track* track)
{
    if (track->GetParticleDefinition()->GetPDGEncoding() != -22) return;

    SimphonyRunAction* ra = SimphonyRunAction::Instance();
    if (!ra) return;
    TTree* tree = ra->GetCPUTrackTree();
    if (!tree) return;

    const G4Step* step = track->GetStep();
    if (!step) return;
    const G4StepPoint* post = step->GetPostStepPoint();

    // Cache the boundary process once (per particle definition's manager).
    if (!fBoundaryCached) {
        fBoundaryCached = true;
        G4ProcessManager* pm =
            track->GetParticleDefinition()->GetProcessManager();
        if (pm) {
            fBoundary = dynamic_cast<G4OpBoundaryProcess*>(
                pm->GetProcess("OpBoundary"));
        }
    }

    // ── Determine the fate ────────────────────────────────────────────────
    std::string fate = "??";
    int detected = 0;

    // Name of the process that ended this (final) step.
    const G4VProcess* proc = post ? post->GetProcessDefinedStep() : nullptr;
    const std::string procName = proc ? proc->GetProcessName() : "";

    // Boundary status. G4OpBoundaryProcess::GetStatus() is a stateful global
    // that holds the LAST boundary interaction's result — it is only valid for
    // THIS photon if the step actually ended on a geometry boundary. edep-sim's
    // own EDepSim::UserTrackingAction guards on fGeomBoundary before trusting
    // it; without that guard a stale 'Detection' from a prior boundary leaks
    // onto bulk steps (OpRayleigh/OpAbsorption) and fakes detections — e.g. 128
    // nm photons "detected" inside the LAr. So only read bstat at a boundary.
    const bool atBoundary =
        post && post->GetStepStatus() == G4StepStatus::fGeomBoundary;
    G4OpBoundaryProcessStatus bstat =
        (fBoundary && atBoundary) ? fBoundary->GetStatus() : Undefined;

    const G4VPhysicalVolume* postPV = post ? post->GetPhysicalVolume() : nullptr;

    if (bstat == Detection) {
        fate = "SD";  detected = 1;                 // detected on a sensor
    } else if (bstat == Absorption) {
        fate = "SA";                                // absorbed at a surface
    } else if (procName == "OpAbsorption") {
        fate = "AB";                                // bulk absorption in LAr
    } else if (procName == "OpWLS") {
        fate = "WLS";                               // absorbed by wavelength shifter
    } else if (postPV == nullptr) {
        fate = "MI";                                // left the world (escaped)
    } else if (bstat == FresnelReflection ||
               bstat == TotalInternalReflection ||
               bstat == LambertianReflection || bstat == LobeReflection ||
               bstat == SpikeReflection || bstat == BackScattering) {
        // Track ended right after a reflection without a clear kill — rare;
        // record it so it isn't silently lumped with the others.
        fate = "Reflect";
    } else if (procName == "OpRayleigh") {
        fate = "Rayleigh";                          // ended on a scatter (rare)
    } else {
        // Fall back to the process name so nothing is hidden.
        fate = procName.empty() ? "Unknown" : procName;
    }

    // ── Fill the buffers ──────────────────────────────────────────────────
    GetGcTrackId()  = track->GetTrackID();
    GetGcParentId() = track->GetParentID();

    const double e_eV = track->GetKineticEnergy() / eV;
    GetGcEnergy()     = static_cast<float>(e_eV);
    GetGcWavelength() = e_eV > 0 ? static_cast<float>(1239.84 / e_eV) : 0.f;

    GetGcDetected() = detected;
    set_cstr(GetGcFate(), fate, 16);
    set_cstr(GetGcEndVolume(), postPV ? postPV->GetName() : "OutOfWorld", 64);
    GetGcNStep() = track->GetCurrentStepNumber();

    if (post) {
        GetGcEndPos().SetXYZT(post->GetPosition().x(), post->GetPosition().y(),
                              post->GetPosition().z(), post->GetGlobalTime());
    }

    // EventId: derived from the run manager's current event.
    GetGcEventId() = G4EventManager::GetEventManager()
                         ->GetConstCurrentEvent()
                         ->GetEventID();

    tree->Fill();
}
