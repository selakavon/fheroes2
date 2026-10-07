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
#include <functional>
#include <vector>

#include "math_base.h"

namespace fheroes2
{
    // Bit positions of the colour channels inside a 32-bit pixel. The remaining byte (alpha or padding)
    // is copied through unchanged.
    struct PixelLayout
    {
        uint8_t redShift{ 0 };
        uint8_t greenShift{ 8 };
        uint8_t blueShift{ 16 };
    };

    // CPU upscalers for 8-bit palette indexed frames. The frame produced by the engine is always indexed,
    // so colour comparisons can be done through a precomputed 256 x 256 distance table which makes the
    // edge-directed scaler several times faster than a generic 32-bit implementation.
    //
    // All methods are thread-safe once the palette has been set. The scaling work itself is split
    // across multiple threads internally.
    class PaletteScaler
    {
    public:
        // 'palette' must point to 256 entries already converted to the output pixel format.
        void setPalette( const uint32_t * palette, const PixelLayout & layout );

        bool isPaletteSet() const
        {
            return _isPaletteSet;
        }

        // Edge-directed pixel-art upscale by an integer factor of 2, 3 or 4.
        // 'src' is the top-left pixel of the full source frame of 'width' x 'height' pixels with 'srcPitch' pixels per row.
        // Only the pixels inside 'roi' (in source coordinates) are produced; up to 2 source pixels around it are read.
        // 'dst' is the top-left pixel of the full destination frame of ( width * scale ) x ( height * scale ) pixels.
        void scaleXBRZ( const uint8_t * src, const int32_t srcPitch, const int32_t width, const int32_t height, const Rect & roi, uint32_t * dst, const int32_t dstPitch,
                        const int32_t scale ) const;

        // Bicubic upscale with adaptive sharpening to an arbitrary destination size of 'dstWidth' x 'dstHeight' pixels.
        // Only the destination pixels which depend on the source pixels inside 'roi' are produced.
        // Returns the destination rectangle which was written.
        Rect scaleSharp( const uint8_t * src, const int32_t srcPitch, const int32_t width, const int32_t height, const Rect & roi, uint32_t * dst, const int32_t dstPitch,
                         const int32_t dstWidth, const int32_t dstHeight ) const;

    private:
        std::array<uint32_t, 256> _palette{};
        // Red, green and blue of every palette entry, used by the bicubic scaler.
        std::array<uint8_t, 256 * 3> _rgb{};
        // Perceptual distance between every pair of palette entries, in 1/32 units.
        std::vector<uint16_t> _distance;
        PixelLayout _layout;
        bool _isPaletteSet{ false };
    };

    // Runs 'task( bandBegin, bandEnd )' for disjoint bands covering [rowBegin, rowEnd) on several threads and waits for completion.
    // Bands are never smaller than 'minBandHeight' rows. Small inputs run on the calling thread.
    void forEachRowBand( const int32_t rowBegin, const int32_t rowEnd, const int32_t minBandHeight, const std::function<void( int32_t, int32_t )> & task );
}
