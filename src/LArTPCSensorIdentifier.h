#pragma once
/**
 * LArTPCSensorIdentifier.h
 *
 * Identifies optical sensor volumes for eic-opticks by checking whether a
 * physical volume (or any of its daughters) has a Geant4 sensitive detector
 * registered — rather than checking for the "PMT" string used by the default
 * U4SensorIdentifierDefault.
 *
 * Sensors are assigned sequential IDs starting from 0.  This works for any
 * geometry that sets up SDs via GDML auxiliary tags (SurfaceDetector /
 * SensDet) without requiring a specific volume-name convention.
 */

#include "U4SensorIdentifier.h"

#include <G4VPhysicalVolume.hh>
#include <G4LogicalVolume.hh>
#include <G4VSensitiveDetector.hh>

#include <iostream>
#include <vector>

struct LArTPCSensorIdentifier : public U4SensorIdentifier
{
    int level = 0 ;
    mutable int instance_counter = 0 ;
    int         global_counter   = 0 ;

    void setLevel(int _level) override { level = _level ; }

    // Recursive SD search (mirrors U4SensorIdentifierDefault::FindSD_r)
    static bool HasSD_r(const G4VPhysicalVolume* pv)
    {
        if (!pv) return false ;
        const G4LogicalVolume* lv = pv->GetLogicalVolume() ;
        if (lv->GetSensitiveDetector()) return true ;
        for (int i = 0 ; i < lv->GetNoDaughters() ; ++i)
            if (HasSD_r(lv->GetDaughter(i))) return true ;
        return false ;
    }

    // Called once per outer PV of every factorized (repeated LV) instance.
    // Returns a unique sequential ID for each sensor, -1 for non-sensors.
    int getInstanceIdentity(const G4VPhysicalVolume* pv) const override
    {
        bool is_sensor = HasSD_r(pv) ;
        int  id        = is_sensor ? instance_counter++ : -1 ;

        if (level > 0) {
            const char* pvn = pv ? pv->GetName().c_str() : "-" ;
            std::cout << "LArTPCSensorIdentifier::getInstanceIdentity"
                      << "  pvn=" << pvn
                      << "  is_sensor=" << is_sensor
                      << "  id=" << id << "\n" ;
        }
        return id ;
    }

    // Called for non-factorized (global remainder) PVs.
    int getGlobalIdentity(const G4VPhysicalVolume* pv,
                          const G4VPhysicalVolume* /*ppv*/) override
    {
        bool is_sensor = HasSD_r(pv) ;
        int  id        = is_sensor ? global_counter++ : -1 ;

        if (level > 0) {
            const char* pvn = pv ? pv->GetName().c_str() : "-" ;
            std::cout << "LArTPCSensorIdentifier::getGlobalIdentity"
                      << "  pvn=" << pvn
                      << "  is_sensor=" << is_sensor
                      << "  id=" << id << "\n" ;
        }
        return id ;
    }
};
