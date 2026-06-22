#include "SimphonyScintProcess.hh"

#include <G4Step.hh>
#include <G4Track.hh>
#include <G4DynamicParticle.hh>
#include <G4ParticleDefinition.hh>
#include <G4Material.hh>
#include <G4MaterialPropertiesTable.hh>
#include <G4MaterialPropertyVector.hh>
#include <G4OpticalPhoton.hh>
#include <G4EmParameters.hh>
#include <G4EmSaturation.hh>
#include <G4Poisson.hh>
#include <Randomize.hh>
#include <G4SystemOfUnits.hh>
#include <G4PhysicalConstants.hh>
#include <G4ParticleChange.hh>

#include "U4.hh"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
// W = 19.5 eV per scintillation photon in LAr. Must match
// EDepSim::DokeBirksSaturation::VisibleEnergyDeposition (line 322), which
// encodes visE = NumPhotons * W. We divide by the same W to recover N.
constexpr G4double kW_LAr_MeV = 19.5e-6;

// Resolution scale on Gaussian sampling of NumPhotons (matches the fork).
constexpr G4double kResolutionScale = 1.0;
}

SimphonyScintProcess::SimphonyScintProcess(const G4String& name)
    : G4VRestDiscreteProcess(name, fElectromagnetic)
{
    // Subtype 22 is Geant4's stock fScintillation enumerator value. Set
    // explicitly so process queries see a recognisable type.
    SetProcessSubType(22);
    std::cout << "[SimphonyScintProcess] constructed (DokeBirks visE → genstep)\n";
}

G4bool SimphonyScintProcess::IsApplicable(const G4ParticleDefinition& p)
{
    // Charged particles only. Optical photons are killed on CPU; any WLS
    // happens on the GPU.
    if (&p == G4OpticalPhoton::OpticalPhoton()) return false;
    return p.GetPDGCharge() != 0.0;
}

G4double SimphonyScintProcess::GetMeanFreePath(const G4Track& /*aTrack*/,
                                             G4double /*previousStepSize*/,
                                             G4ForceCondition* condition)
{
    *condition = StronglyForced;
    return DBL_MAX;
}

G4double SimphonyScintProcess::GetMeanLifeTime(const G4Track& /*aTrack*/,
                                               G4ForceCondition* condition)
{
    // Force the AtRest action on every stopped charged particle (mirrors stock
    // G4Scintillation, which is StronglyForced AtRest too) so a track that
    // deposits its last energy at rest still scintillates.
    *condition = StronglyForced;
    return DBL_MAX;
}

// PostStep (in-flight) and AtRest (stopped) both run identical scintillation
// logic on the step's energy deposit — funnel both through Scintillate().
G4VParticleChange*
SimphonyScintProcess::PostStepDoIt(const G4Track& aTrack, const G4Step& aStep)
{
    return Scintillate(aTrack, aStep);
}

G4VParticleChange*
SimphonyScintProcess::AtRestDoIt(const G4Track& aTrack, const G4Step& aStep)
{
    return Scintillate(aTrack, aStep);
}

G4VParticleChange*
SimphonyScintProcess::Scintillate(const G4Track& aTrack, const G4Step& aStep)
{
    aParticleChange.Initialize(aTrack);
    aParticleChange.SetNumberOfSecondaries(0);

    const G4double TotalEnergyDeposit = aStep.GetTotalEnergyDeposit();
    if (TotalEnergyDeposit <= 0.0) {
        return &aParticleChange;  // no scintillation this step
    }

    const G4Material* aMaterial = aTrack.GetMaterial();
    if (!aMaterial) return &aParticleChange;  // no scintillation this step

    G4MaterialPropertiesTable* mpt = aMaterial->GetMaterialPropertiesTable();
    if (!mpt) return &aParticleChange;  // no scintillation this step

    // Must have at least one scintillation component vector to be a
    // scintillator in the GDML sense.
    const G4MaterialPropertyVector* fastComp = mpt->GetProperty("FASTCOMPONENT");
    const G4MaterialPropertyVector* slowComp = mpt->GetProperty("SLOWCOMPONENT");
    if (!fastComp && !slowComp) {
        return &aParticleChange;  // no scintillation this step
    }

    // ── 1. NumPhotons from DokeBirks visE ────────────────────────────────
    G4EmSaturation* emSat = G4EmParameters::Instance()->GetEmSaturation();
    G4double visE = (emSat != nullptr)
                    ? emSat->VisibleEnergyDepositionAtAStep(&aStep)
                    : 0.0;
    if (visE <= 0.0) {
        return &aParticleChange;  // no scintillation this step
    }

    const G4double MeanNumberOfPhotons = visE / kW_LAr_MeV;

    std::cout << "[SimphonyScintProcess] MeanNumberOfPhotons="
              << MeanNumberOfPhotons
              << " (visE=" << visE/MeV << " MeV, W=19.5 eV)"
              << " trk=" << aTrack.GetTrackID() << "\n";

    G4int NumTracks = 0;
    if (MeanNumberOfPhotons > 10.0) {
        G4double sigma = kResolutionScale * std::sqrt(MeanNumberOfPhotons);
        NumTracks = G4int(G4RandGauss::shoot(MeanNumberOfPhotons, sigma) + 0.5);
    } else {
        NumTracks = G4int(G4Poisson(MeanNumberOfPhotons));
    }

    if (NumTracks <= 0) {
        return &aParticleChange;  // no scintillation this step
    }

    // ── 2. Resolve Ratio_timeconstant for this particle ──────────────────
    const G4String& aParticleName =
        aTrack.GetDynamicParticle()->GetDefinition()->GetParticleName();

    G4MaterialPropertyVector* Ratio_timeconstant = nullptr;
    if (aParticleName == "gamma" || aParticleName == "e+" || aParticleName == "e-") {
        Ratio_timeconstant = mpt->GetProperty("GammaCONSTANT");
    } else if (aParticleName == "alpha") {
        Ratio_timeconstant = mpt->GetProperty("AlphaCONSTANT");
    } else {
        Ratio_timeconstant = mpt->GetProperty("NeutronCONSTANT");
    }
    if (!Ratio_timeconstant) {
        // No fast/slow split data → cannot build gensteps with the fork's
        // payload shape. Same early-out as the fork.
        return &aParticleChange;  // no scintillation this step
    }

    // ── 3. Per-photon Poisson split across components ────────────────────
    const G4int nscnt = Ratio_timeconstant->GetVectorLength();
    std::vector<G4int> NumVec(nscnt, 0);
    for (G4int i = 0; i < NumTracks; ++i) {
        G4double p = G4UniformRand();
        G4double p_count = 0.0;
        for (G4int j = 0; j < nscnt; ++j) {
            p_count += (*Ratio_timeconstant)[j];
            if (p < p_count) {
                NumVec[j]++;
                break;
            }
        }
    }

    // ── 4. Push one genstep per non-empty component ──────────────────────
    for (G4int scnt = 0; scnt < nscnt; ++scnt) {
        const G4int NumPhoton = NumVec[scnt];
        if (NumPhoton <= 0) continue;
        const G4double ScintillationTime = Ratio_timeconstant->Energy(scnt);

        U4::CollectGenstep_DsG4Scintillation_r4695(
            &aTrack, &aStep, NumPhoton, scnt, ScintillationTime);

        std::cout << "[SimphonyScintProcess] genstep collected: trk="
                  << aTrack.GetTrackID()
                  << " scnt=" << scnt
                  << " NumPhoton=" << NumPhoton
                  << " t=" << ScintillationTime/ns << " ns\n";
    }

    std::cout << "[SimphonyScintProcess] step trk=" << aTrack.GetTrackID()
              << " " << aParticleName
              << " dE=" << TotalEnergyDeposit/MeV << " MeV"
              << " visE=" << visE/MeV << " MeV"
              << " MeanNph=" << MeanNumberOfPhotons
              << " NumTracks=" << NumTracks
              << "\n";

    return &aParticleChange;  // gensteps collected for the GPU; no CPU secondaries
}
