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

#include "weather.h"

#include <algorithm>
#include <cmath>

#include "ambience.h"
#include "game_assets.h"
#include "icn.h"
#include "image_palette.h"
#include "settings.h"

namespace
{
    constexpr float pi{ 3.14159265F };

    // Palette colours (KB.PAL): 10, 11, 13 are white to light gray; 66, 67 are light blue; 242 is a gray-blue.
    constexpr std::array<uint8_t, 3> snowColors{ 10, 11, 13 };
    constexpr std::array<uint8_t, 3> rainColors{ 66, 67, 242 };

    // The time step is capped so that a hiccup (for example a dialog shown for a while) does not teleport the particles.
    constexpr float maxStepMs{ 100.0F };
}

namespace fheroes2
{
    Weather & Weather::instance()
    {
        static Weather weather;
        return weather;
    }

    float Weather::_random()
    {
        // xorshift32
        _randomState ^= _randomState << 13U;
        _randomState ^= _randomState >> 17U;
        _randomState ^= _randomState << 5U;
        return static_cast<float>( _randomState & 0xFFFFFFU ) / static_cast<float>( 0x1000000U );
    }

    bool Weather::isActive() const
    {
        if ( !Settings::Get().isFxWeatherEnabled() ) {
            return false;
        }

        if ( Ambience::instance().scene() != Ambience::Scene::ADVENTURE_MAP ) {
            return false;
        }

        return Ambience::instance().weatherType() != WeatherType::NONE;
    }

    void Weather::_spawn( Particle & particle, const Rect & roi, const bool atTop )
    {
        // Particles are spawned inside the visible area plus a margin of 32 pixels so that they enter the screen smoothly.
        constexpr float margin{ 32.0F };

        particle.x = -margin + _random() * ( static_cast<float>( roi.width ) + 2 * margin );
        particle.y = atTop ? -margin + _random() * margin : _random() * static_cast<float>( roi.height );
        particle.phase = _random() * 2 * pi;

        switch ( _type ) {
        case WeatherType::RAIN:
            // Pixels per second.
            particle.vy = 380.0F + _random() * 120.0F;
            particle.vx = -60.0F - _random() * 20.0F;
            particle.size = static_cast<uint8_t>( 5 + static_cast<int>( _random() * 5 ) );
            particle.color = rainColors[static_cast<size_t>( _random() * rainColors.size() ) % rainColors.size()];
            break;
        case WeatherType::SNOW:
            particle.vy = 35.0F + _random() * 45.0F;
            particle.vx = 8.0F + _random() * 10.0F;
            particle.size = ( _random() < 0.35F ) ? 2 : 1;
            particle.color = snowColors[static_cast<size_t>( _random() * snowColors.size() ) % snowColors.size()];
            break;
        case WeatherType::LEAVES:
            particle.vy = 55.0F + _random() * 50.0F;
            particle.vx = 25.0F + _random() * 30.0F;
            // Leaf sprite: 3 shapes.
            particle.size = static_cast<uint8_t>( static_cast<int>( _random() * 3 ) % 3 );
            particle.color = 0;
            break;
        case WeatherType::FOG:
        case WeatherType::NONE:
        default:
            break;
        }
    }

    void Weather::_reset( const WeatherType type, const Rect & roi )
    {
        _type = type;
        _roi = roi;
        _timeMs = 0;
        _updateTimer.reset();

        switch ( type ) {
        case WeatherType::RAIN:
            _activeParticles = 400;
            break;
        case WeatherType::SNOW:
            _activeParticles = 300;
            break;
        case WeatherType::LEAVES:
            _activeParticles = 70;
            break;
        case WeatherType::FOG:
        case WeatherType::NONE:
        default:
            _activeParticles = 0;
            break;
        }

        // The particle density is defined for a 640x480 game area, scale it with the visible area.
        const float areaFactor = static_cast<float>( roi.width ) * static_cast<float>( roi.height ) / ( 640.0F * 480.0F );
        _activeParticles = std::min( maxParticles, static_cast<size_t>( static_cast<float>( _activeParticles ) * areaFactor ) );

        for ( size_t i = 0; i < _activeParticles; ++i ) {
            _spawn( _particles[i], roi, false );
        }
    }

    void Weather::update( const Rect & roi )
    {
        if ( !isActive() ) {
            _type = WeatherType::NONE;
            return;
        }

        const WeatherType type = Ambience::instance().weatherType();
        if ( type != _type || roi.width != _roi.width || roi.height != _roi.height ) {
            _reset( type, roi );
            return;
        }

        const float stepMs = std::min( static_cast<float>( _updateTimer.getMs() ), maxStepMs );
        _updateTimer.reset();
        _timeMs += stepMs;

        const float step = stepMs / 1000.0F;
        const float height = static_cast<float>( roi.height );
        const float width = static_cast<float>( roi.width );

        if ( _type == WeatherType::FOG ) {
            _fogOffsetX += 9.0F * step;
            _fogOffsetY += 3.0F * step;
            if ( _fogWidth > 0 && _fogOffsetX >= static_cast<float>( _fogWidth ) ) {
                _fogOffsetX -= static_cast<float>( _fogWidth );
            }
            if ( _fogHeight > 0 && _fogOffsetY >= static_cast<float>( _fogHeight ) ) {
                _fogOffsetY -= static_cast<float>( _fogHeight );
            }
            return;
        }

        const float time = _timeMs / 1000.0F;

        for ( size_t i = 0; i < _activeParticles; ++i ) {
            Particle & particle = _particles[i];

            float wobble = 0;
            if ( _type == WeatherType::SNOW ) {
                wobble = 18.0F * std::sin( time * 1.7F + particle.phase );
            }
            else if ( _type == WeatherType::LEAVES ) {
                wobble = 40.0F * std::sin( time * 2.3F + particle.phase );
            }

            particle.x += ( particle.vx + wobble ) * step;
            particle.y += particle.vy * step;

            if ( particle.y > height + 8 || particle.x < -40 || particle.x > width + 40 ) {
                _spawn( particle, roi, true );
            }
        }
    }

    void Weather::draw( Image & dst, const Rect & roi )
    {
        if ( !isActive() || _type == WeatherType::NONE ) {
            return;
        }

        if ( roi.width != _roi.width || roi.height != _roi.height ) {
            _reset( _type, roi );
        }

        if ( _type == WeatherType::FOG ) {
            _drawFog( dst, roi );
        }
        else {
            _drawParticles( dst, roi );
        }
    }

    void Weather::_drawParticles( Image & dst, const Rect & roi ) const
    {
        const int32_t minX = roi.x;
        const int32_t minY = roi.y;
        const int32_t maxX = roi.x + roi.width;
        const int32_t maxY = roi.y + roi.height;

        for ( size_t i = 0; i < _activeParticles; ++i ) {
            const Particle & particle = _particles[i];

            const int32_t x = roi.x + static_cast<int32_t>( std::lround( particle.x ) );
            const int32_t y = roi.y + static_cast<int32_t>( std::lround( particle.y ) );

            switch ( _type ) {
            case WeatherType::RAIN: {
                // A streak along the velocity vector.
                const float length = static_cast<float>( particle.size );
                const float speed = std::sqrt( particle.vx * particle.vx + particle.vy * particle.vy );
                const int32_t dx = static_cast<int32_t>( std::lround( particle.vx / speed * length ) );
                const int32_t dy = static_cast<int32_t>( std::lround( particle.vy / speed * length ) );
                DrawLine( dst, { x - dx, y - dy }, { x, y }, particle.color, roi );
                break;
            }
            case WeatherType::SNOW:
                if ( x >= minX && y >= minY && x < maxX && y < maxY ) {
                    SetPixel( dst, x, y, particle.color );
                    if ( particle.size == 2 ) {
                        if ( x + 1 < maxX ) {
                            SetPixel( dst, x + 1, y, particle.color );
                        }
                        if ( y + 1 < maxY ) {
                            SetPixel( dst, x, y + 1, particle.color );
                        }
                        if ( x + 1 < maxX && y + 1 < maxY ) {
                            SetPixel( dst, x + 1, y + 1, particle.color );
                        }
                    }
                }
                break;
            case WeatherType::LEAVES: {
                // Two animation frames per leaf shape, switching every 300 ms with a per-particle offset.
                const uint32_t frame = ( static_cast<uint32_t>( _timeMs / 300.0F ) + static_cast<uint32_t>( particle.phase * 10 ) ) % 2;
                const Sprite & leaf = Assets::getImage( ICN::WEATHER_LEAVES, particle.size * 2 + frame );
                if ( leaf.empty() ) {
                    break;
                }

                // Clip the sprite to the game area.
                const int32_t left = std::max( x, minX );
                const int32_t top = std::max( y, minY );
                const int32_t right = std::min( x + leaf.width(), maxX );
                const int32_t bottom = std::min( y + leaf.height(), maxY );
                if ( left < right && top < bottom ) {
                    Blit( leaf, left - x, top - y, dst, left, top, right - left, bottom - top );
                }
                break;
            }
            case WeatherType::FOG:
            case WeatherType::NONE:
            default:
                break;
            }
        }
    }

    void Weather::_prepareFog()
    {
        _isFogReady = true;

        const Sprite & noise = Assets::getImage( ICN::WEATHER_FOG_NOISE, 0 );
        if ( noise.empty() ) {
            return;
        }

        // The colour remapping of the engine's transform tables is not exposed, so sample it: tables 8, 7 and 6 blend
        // a colour towards white with increasing strength (tables 2 to 5 are shadows). Table 6 flattens most colours
        // to the same gray so only the two lighter ones are used, the denser one for the cores of the fog banks.
        constexpr std::array<uint8_t, 3> fogTransformIds{ 8, 7, 7 };
        Image ramp;
        ramp._disableTransformLayer();
        ramp.resize( 256, 1 );
        for ( size_t level = 0; level < fogTransformIds.size(); ++level ) {
            uint8_t * data = ramp.image();
            for ( int32_t i = 0; i < 256; ++i ) {
                data[i] = static_cast<uint8_t>( i );
            }
            ApplyTransform( ramp, 0, 0, 256, 1, fogTransformIds[level] );
            std::copy( data, data + 256, _fogTables[level].begin() );

            // Near-black colours (the unexplored part of the map) are left untouched.
            const RGB * palette = getRGBGamePalette();
            for ( size_t i = 0; i < 256; ++i ) {
                // The game palette stores 6-bit channels.
                if ( palette[i].r + palette[i].g + palette[i].b < 4 ) {
                    _fogTables[level][i] = static_cast<uint8_t>( i );
                }
            }
        }

        _fogWidth = noise.width();
        _fogHeight = noise.height();
        _fogRuns.clear();
        _fogRowStart.clear();
        _fogRowStart.reserve( static_cast<size_t>( _fogHeight ) + 1 );

        // The noise is stored as palette grays: index 10 is white (dense fog) and index 36 is black (no fog).
        const auto fogLevel = []( const uint8_t gray ) -> uint8_t {
            const float density = static_cast<float>( 36 - std::clamp<int32_t>( gray, 10, 36 ) ) / 26.0F;
            if ( density > 0.82F ) {
                return 2;
            }
            if ( density > 0.66F ) {
                return 1;
            }
            return 0;
        };

        const uint8_t * in = noise.image();
        for ( int32_t y = 0; y < _fogHeight; ++y ) {
            _fogRowStart.push_back( static_cast<uint32_t>( _fogRuns.size() ) );

            const uint8_t * row = in + static_cast<ptrdiff_t>( y ) * _fogWidth;
            for ( int32_t x = 0; x < _fogWidth; ) {
                const uint8_t level = fogLevel( row[x] );

                int32_t end = x + 1;
                while ( end < _fogWidth && fogLevel( row[end] ) == level ) {
                    ++end;
                }

                if ( level > 0 ) {
                    _fogRuns.push_back( { static_cast<uint16_t>( x ), static_cast<uint16_t>( end - x ), static_cast<uint8_t>( level - 1 ) } );
                }

                x = end;
            }
        }

        _fogRowStart.push_back( static_cast<uint32_t>( _fogRuns.size() ) );
    }

    void Weather::_drawFog( Image & dst, const Rect & roi )
    {
        if ( !_isFogReady ) {
            _prepareFog();
        }

        if ( _fogRuns.empty() || dst.empty() ) {
            return;
        }

        // Clip the ROI to the destination image.
        const int32_t minX = std::max( roi.x, 0 );
        const int32_t minY = std::max( roi.y, 0 );
        const int32_t maxX = std::min( roi.x + roi.width, dst.width() );
        const int32_t maxY = std::min( roi.y + roi.height, dst.height() );
        if ( minX >= maxX || minY >= maxY ) {
            return;
        }

        const int32_t offsetX = static_cast<int32_t>( _fogOffsetX );
        const int32_t offsetY = static_cast<int32_t>( _fogOffsetY );

        const int32_t dstWidth = dst.width();
        uint8_t * image = dst.image();
        const uint8_t * transform = dst.singleLayer() ? nullptr : dst.transform();

        // The mask column of the first visible pixel: the first mask tile starts at 'minX - firstMaskX'.
        const int32_t firstMaskX = ( minX - roi.x + offsetX ) % _fogWidth;

        for ( int32_t y = minY; y < maxY; ++y ) {
            const int32_t maskY = ( y - roi.y + offsetY ) % _fogHeight;
            uint8_t * rowOut = image + static_cast<ptrdiff_t>( y ) * dstWidth;
            const uint8_t * rowTransform = ( transform == nullptr ) ? nullptr : transform + static_cast<ptrdiff_t>( y ) * dstWidth;

            for ( int32_t tileX = minX - firstMaskX; tileX < maxX; tileX += _fogWidth ) {
                for ( uint32_t runId = _fogRowStart[maskY]; runId < _fogRowStart[maskY + 1]; ++runId ) {
                    const FogRun & run = _fogRuns[runId];
                    const int32_t left = std::max( tileX + run.start, minX );
                    const int32_t right = std::min( tileX + run.start + run.length, maxX );
                    if ( left >= right ) {
                        continue;
                    }

                    const uint8_t * table = _fogTables[run.table].data();
                    uint8_t * out = rowOut + left;
                    const uint8_t * outEnd = rowOut + right;
                    if ( rowTransform == nullptr ) {
                        for ( ; out != outEnd; ++out ) {
                            *out = table[*out];
                        }
                    }
                    else {
                        const uint8_t * transformIn = rowTransform + left;
                        for ( ; out != outEnd; ++out, ++transformIn ) {
                            if ( *transformIn == 0 ) {
                                *out = table[*out];
                            }
                        }
                    }
                }
            }
        }
    }
}
