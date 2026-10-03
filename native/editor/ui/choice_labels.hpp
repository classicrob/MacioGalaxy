#pragma once

#include <petari/editor/catalog.hpp>
#include <petari/launch_stage.hpp>
#include <string>

namespace PetariEditor::Ui {

    inline std::string galaxyChoiceLabel(const std::string& id) {
        int count = 0;
        const auto* galaxies = PetariNative::App::LaunchStage::galaxies(&count);
        for (int i = 0; i < count; ++i)
            if (id == galaxies[i].stage && galaxies[i].name && galaxies[i].name[0])
                return galaxies[i].name;
        return id;
    }

    inline std::string skyChoiceLabel(const std::string& id) {
        const auto* info = Catalog::find(id.c_str());
        if (info && !info->displayName.empty() && info->displayName != id)
            return info->displayName;
        static constexpr struct {
            const char* id;
            const char* label;
        } skies[] = {
            {"AuroraSky", "Aurora Sky"},
            {"ChildRoomSky", "Child Room Sky"},
            {"CloudSky", "Cloud Sky"},
            {"DesertSky", "Desert Sky"},
            {"FamicomMarioSky", "Famicom Mario Sky"},
            {"GalaxySky", "Galaxy Sky"},
            {"GreenPlanetOrbitSky", "Green Planet Orbit Sky"},
            {"HalfGalaxySky", "Half Galaxy Sky"},
            {"MilkyWaySky", "Milky Way Sky"},
            {"PhantomSky", "Phantom Sky"},
            {"RockPlanetOrbitSky", "Rock Planet Orbit Sky"},
            {"StarrySky", "Starry Sky"},
            {"SummerSky", "Summer Sky"},
        };
        for (const auto& sky : skies)
            if (id == sky.id)
                return sky.label;
        return id;
    }

}  // namespace PetariEditor::Ui
