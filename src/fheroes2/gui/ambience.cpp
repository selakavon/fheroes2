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

#include "ambience.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "pal.h"
#include "settings.h"
#include "world.h"

namespace
{
    constexpr float tintEpsilon{ 0.0005F };

    bool isClose( const float a, const float b )
    {
        return std::fabs( a - b ) < tintEpsilon;
    }
}

namespace fheroes2
{
    Ambience::SceneGuard::SceneGuard( const Scene scene )
        : _previousScene( Ambience::instance().scene() )
    {
        Ambience::instance().setScene( scene );
    }

    Ambience::SceneGuard::~SceneGuard()
    {
        Ambience::instance().setScene( _previousScene );
    }

    bool Ambience::Tint::operator==( const Tint & other ) const
    {
        return isClose( r, other.r ) && isClose( g, other.g ) && isClose( b, other.b ) && isClose( brightness, other.brightness )
               && isClose( saturation, other.saturation );
    }

    Ambience & Ambience::instance()
    {
        static Ambience ambience;
        return ambience;
    }

    void Ambience::setScene( const Scene scene )
    {
        if ( _scene == Scene::NONE && scene != Scene::NONE ) {
            // A new game world is entered: start from its current lighting without a transition from the previous one.
            _isInitialized = false;
            _isInTransition = false;
        }

        _scene = scene;
        _dialogDepthBaseline = _dialogDepth;
    }

    void Ambience::pushDialog()
    {
        ++_dialogDepth;
    }

    void Ambience::popDialog()
    {
        if ( _dialogDepth > 0 ) {
            --_dialogDepth;
        }

        _dialogDepthBaseline = std::min( _dialogDepthBaseline, _dialogDepth );
    }

    void Ambience::rebaseDialogDepth()
    {
        _dialogDepthBaseline = _dialogDepth;
    }

    bool Ambience::_isActive() const
    {
        if ( _scene == Scene::NONE ) {
            return false;
        }

        if ( _dialogDepth > _dialogDepthBaseline ) {
            return false;
        }

        const Settings & conf = Settings::Get();
        return conf.isFxDayNightEnabled() || conf.isFxSeasonTintEnabled();
    }

    Ambience::Tint Ambience::_lerp( const Tint & from, const Tint & to, const float t )
    {
        const auto mix = [t]( const float a, const float b ) { return a + ( b - a ) * t; };

        Tint result;
        result.r = mix( from.r, to.r );
        result.g = mix( from.g, to.g );
        result.b = mix( from.b, to.b );
        result.brightness = mix( from.brightness, to.brightness );
        result.saturation = mix( from.saturation, to.saturation );
        return result;
    }

    Ambience::Tint Ambience::_targetTint() const
    {
        const Settings & conf = Settings::Get();

        Tint tint;

        if ( conf.isFxDayNightEnabled() ) {
            // One week is one full cycle, every day of the week has its own lighting.
            const Tint dawn{ 1.0F, 0.9F, 0.85F, 0.9F, 1.0F };
            const Tint noon{};
            const Tint dusk{ 1.05F, 0.9F, 0.75F, 0.95F, 1.0F };
            const Tint night{ 0.6F, 0.7F, 1.0F, 0.7F, 0.8F };

            const std::array<Tint, 7> dayTints{ dawn, _lerp( dawn, noon, 0.5F ), noon, _lerp( noon, dusk, 0.5F ), dusk, _lerp( dusk, night, 0.5F ), night };

            const uint32_t day = world.CountDay();
            const size_t dayOfWeek = ( day > 0 ? day - 1 : 0 ) % dayTints.size();

            tint = dayTints[dayOfWeek];
        }

        if ( conf.isFxSeasonTintEnabled() ) {
            const Tint spring{ 1.0F, 1.03F, 0.97F, 1.0F, 1.0F };
            const Tint summer{};
            const Tint autumn{ 1.05F, 0.97F, 0.85F, 1.0F, 1.0F };
            const Tint winter{ 0.92F, 0.96F, 1.05F, 1.05F, 0.85F };

            const std::array<Tint, 4> seasonTints{ spring, summer, autumn, winter };

            const uint32_t month = world.GetMonth();
            const size_t season = ( ( month > 0 ? month - 1 : 0 ) % 12 ) / 3;

            const Tint & seasonTint = seasonTints[season];
            tint.r *= seasonTint.r;
            tint.g *= seasonTint.g;
            tint.b *= seasonTint.b;
            tint.brightness *= seasonTint.brightness;
            tint.saturation *= seasonTint.saturation;
        }

        return tint;
    }

    WeatherType Ambience::weatherType() const
    {
        // A small integer hash of the map seed and the week number, mapped to 0..99.
        uint32_t hash = world.GetMapSeed() * 2654435761U + world.CountWeek() * 40503U + 0x7F4A7C15U;
        hash ^= hash >> 15U;
        hash *= 0x2C1B3C6DU;
        hash ^= hash >> 12U;
        const uint32_t roll = hash % 100U;

        const uint32_t month = world.GetMonth();
        const uint32_t season = ( ( month > 0 ? month - 1 : 0 ) % 12 ) / 3;

        switch ( season ) {
        case 0:
            // Spring: rain 40%, fog 10%.
            if ( roll < 40 ) {
                return WeatherType::RAIN;
            }
            if ( roll < 50 ) {
                return WeatherType::FOG;
            }
            return WeatherType::NONE;
        case 1:
            // Summer: rain 20%.
            return ( roll < 20 ) ? WeatherType::RAIN : WeatherType::NONE;
        case 2:
            // Autumn: leaves 30%, rain 30%, fog 10%.
            if ( roll < 30 ) {
                return WeatherType::LEAVES;
            }
            if ( roll < 60 ) {
                return WeatherType::RAIN;
            }
            if ( roll < 70 ) {
                return WeatherType::FOG;
            }
            return WeatherType::NONE;
        default:
            // Winter: snow 50%, fog 10%.
            if ( roll < 50 ) {
                return WeatherType::SNOW;
            }
            if ( roll < 60 ) {
                return WeatherType::FOG;
            }
            return WeatherType::NONE;
        }
    }

    bool Ambience::apply( std::vector<RGB> & palette )
    {
        if ( !_isActive() ) {
            return false;
        }

        const Tint target = _targetTint();

        if ( !_isInitialized ) {
            _isInitialized = true;
            _isInTransition = false;
            _current = target;
            _to = target;
        }
        else if ( target != _to ) {
            _from = _current;
            _to = target;
            _isInTransition = true;
            _transitionTimer.reset();
        }

        if ( _isInTransition ) {
            const uint64_t elapsed = _transitionTimer.getMs();
            if ( elapsed >= _transitionDurationMs ) {
                _current = _to;
                _isInTransition = false;
            }
            else {
                _current = _lerp( _from, _to, static_cast<float>( elapsed ) / static_cast<float>( _transitionDurationMs ) );
            }
        }

        if ( _current.isIdentity() ) {
            return false;
        }

        palette = PAL::tintPalette( palette, _current.r, _current.g, _current.b, _current.brightness, _current.saturation );
        return true;
    }

    bool Ambience::isRenderRequired()
    {
        if ( !_isInTransition || !_isActive() ) {
            return false;
        }

        if ( _lastRenderRequest.getMs() < _transitionFrameIntervalMs ) {
            return false;
        }

        _lastRenderRequest.reset();
        return true;
    }
}
