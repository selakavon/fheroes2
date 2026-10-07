/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2026                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "image.h"
#include "math_base.h"
#include "timing.h"

namespace fheroes2
{
    enum class WeatherType : uint8_t
    {
        NONE,
        RAIN,
        SNOW,
        FOG,
        LEAVES
    };

    // Weather renders a particle layer (rain, snow, falling leaves) or a drifting fog over the visible part of the
    // Adventure Map. Particles live in logical pixels relative to the game area and are drawn directly into the frame
    // after the fog of war and before the cursor. No memory is allocated while the effect is running.
    class Weather
    {
    public:
        Weather( const Weather & ) = delete;
        Weather & operator=( const Weather & ) = delete;

        static Weather & instance();

        // Returns true when the weather effect is enabled in the settings, the Adventure Map is shown and the current weather is not NONE.
        bool isActive() const;

        // Advances the simulation by the time elapsed since the previous call. 'roi' is the game area in logical pixels.
        void update( const Rect & roi );

        // Draws the current weather state into 'dst' clipped to 'roi' (the game area). Does nothing when the weather is not active.
        void draw( Image & dst, const Rect & roi );

    private:
        struct Particle
        {
            float x{ 0 };
            float y{ 0 };
            float vx{ 0 };
            float vy{ 0 };
            // Per-particle phase for the horizontal wobble.
            float phase{ 0 };
            // Rain: streak length in pixels. Snow: 1 or 2 pixel dot. Leaves: sprite index.
            uint8_t size{ 1 };
            uint8_t color{ 0 };
        };

        Weather() = default;

        void _reset( const WeatherType type, const Rect & roi );
        void _spawn( Particle & particle, const Rect & roi, const bool atTop );

        void _drawParticles( Image & dst, const Rect & roi ) const;
        void _drawFog( Image & dst, const Rect & roi );

        static constexpr size_t maxParticles{ 600 };

        std::array<Particle, maxParticles> _particles{};
        size_t _activeParticles{ 0 };

        WeatherType _type{ WeatherType::NONE };
        Rect _roi;

        // Random generator state (xorshift) kept here to avoid affecting the game's own random number generator.
        uint32_t _randomState{ 0x9E3779B9U };

        float _random();

        Time _updateTimer;
        float _timeMs{ 0 };

        // Fog: the noise mask converted into runs of pixels per row that share the same whitening transform table, drifting slowly.
        struct FogRun
        {
            uint16_t start{ 0 };
            uint16_t length{ 0 };
            uint8_t table{ 0 };
        };

        bool _isFogReady{ false };
        int32_t _fogWidth{ 0 };
        int32_t _fogHeight{ 0 };
        std::vector<FogRun> _fogRuns;
        // Index of the first run of each row plus one trailing end index.
        std::vector<uint32_t> _fogRowStart;
        // Colour remapping tables for the fog densities (light to dense).
        std::array<std::array<uint8_t, 256>, 3> _fogTables{};
        float _fogOffsetX{ 0 };
        float _fogOffsetY{ 0 };

        void _prepareFog();
    };
}
