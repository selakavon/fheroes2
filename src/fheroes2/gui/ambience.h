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

#include <cstdint>
#include <vector>

#include "image_palette.h"
#include "timing.h"

namespace fheroes2
{
    // Ambience computes the lighting mood of the game world (day/night cycle and seasonal tint) and applies it as a uniform
    // tint to the RGB palette which reaches the renderer. It costs nothing per pixel and composes with colour cycling.
    class Ambience
    {
    public:
        enum class Scene : uint8_t
        {
            // Main menu, editor, campaign screens: the tint is never applied.
            NONE,
            ADVENTURE_MAP,
            BATTLE
        };

        // Sets a scene for its lifetime and restores the previous one afterwards.
        class SceneGuard
        {
        public:
            explicit SceneGuard( const Scene scene );

            SceneGuard( const SceneGuard & ) = delete;
            SceneGuard & operator=( const SceneGuard & ) = delete;

            ~SceneGuard();

        private:
            const Scene _previousScene;
        };

        Ambience( const Ambience & ) = delete;
        Ambience & operator=( const Ambience & ) = delete;

        static Ambience & instance();

        void setScene( const Scene scene );

        Scene scene() const
        {
            return _scene;
        }

        // Modal dialogs drawn on top of the game world are rendered without the tint. Every StandardWindow reports itself here.
        void pushDialog();
        void popDialog();

        // Treats all the dialogs opened so far as part of the scene background (used by the battle screen which is itself a window).
        void rebaseDialogDepth();

        // Applies the current tint to 'palette' (the default game palette on input). Returns false when no tint is active,
        // in which case the palette is left untouched.
        bool apply( std::vector<RGB> & palette );

        // Returns true when a tint transition is in progress and a new frame should be rendered now. Must be polled
        // frequently; it limits the render rate to about 30 frames per second on its own.
        bool isRenderRequired();

    private:
        struct Tint
        {
            float r{ 1.0F };
            float g{ 1.0F };
            float b{ 1.0F };
            float brightness{ 1.0F };
            float saturation{ 1.0F };

            bool operator==( const Tint & other ) const;

            bool operator!=( const Tint & other ) const
            {
                return !( *this == other );
            }

            bool isIdentity() const
            {
                return *this == Tint{};
            }
        };

        Ambience() = default;

        bool _isActive() const;

        Tint _targetTint() const;

        static Tint _lerp( const Tint & from, const Tint & to, const float t );

        Scene _scene{ Scene::NONE };

        int32_t _dialogDepth{ 0 };
        int32_t _dialogDepthBaseline{ 0 };

        // The tint shown at the moment, the tint the transition started from and the one it moves to.
        Tint _current;
        Tint _from;
        Tint _to;

        bool _isInitialized{ false };
        bool _isInTransition{ false };

        Time _transitionTimer;
        Time _lastRenderRequest;

        // Transition duration when the in-game day changes.
        static constexpr uint64_t _transitionDurationMs{ 600 };

        // Minimum interval between two full-frame renders during a transition.
        static constexpr uint64_t _transitionFrameIntervalMs{ 33 };
    };
}
