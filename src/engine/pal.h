/***************************************************************************
 *   fheroes2: https://github.com/ihhub/fheroes2                           *
 *   Copyright (C) 2020 - 2025                                             *
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

namespace PAL
{
    enum class PaletteType : int
    {
        STANDARD, // default
        YELLOW_FONT,
        WHITE_FONT,
        GRAY_FONT,
        RED, // for Blood Lust spell animation
        GRAY, // for Petrify spell effect
        BROWN,
        TAN, // for Puzzle image generation
        NO_CYCLE,
        MIRROR_IMAGE,
        DARKENING, // for disabled buttons
        GOOD_TO_EVIL_INTERFACE, // a custom palette for converting Good Interface images into Evil Interface images.
        GOOD_TO_EVIL_BUTTON, // for converting Good style buttons into Evil style buttons.
        PURPLE, // For random object images.
        CUSTOM
    };

    std::vector<uint8_t> GetCyclingPalette( const uint32_t stepId );
    const std::vector<uint8_t> & GetPalette( const PaletteType type );
    std::vector<uint8_t> CombinePalettes( const std::vector<uint8_t> & first, const std::vector<uint8_t> & second );

    // Returns a copy of the RGB palette with a uniform colour tint applied: every entry is first desaturated towards its
    // luminance by 'saturation' (1.0 keeps the colour, 0.0 makes it gray), then each channel is multiplied by the
    // corresponding factor and by 'brightness'. The result is clamped to 0..255. Entry 0 is never modified.
    // Since the tint is uniform the colour cycling indexes keep animating correctly after the tint is applied.
    std::vector<fheroes2::RGB> tintPalette( const std::vector<fheroes2::RGB> & in, const float r, const float g, const float b, const float brightness,
                                            const float saturation );
}
