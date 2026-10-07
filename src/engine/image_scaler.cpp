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

#include "image_scaler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <thread>

namespace
{
    // ---------------------------------------------------------------------------------------------
    // Shared helpers
    // ---------------------------------------------------------------------------------------------

    constexpr int32_t maxWorkerThreads{ 8 };

    // Linear interpolation of every byte of two packed pixels: result = ( col * num + pix * ( den - num ) ) / den.
    inline uint32_t mixPixel( const uint32_t pix, const uint32_t col, const uint32_t num, const uint32_t den )
    {
        uint32_t result = 0;
        for ( uint32_t shift = 0; shift < 32; shift += 8 ) {
            const uint32_t a = ( pix >> shift ) & 0xFFU;
            const uint32_t b = ( col >> shift ) & 0xFFU;
            const uint32_t value = ( b * num + a * ( den - num ) ) / den;
            result |= ( value & 0xFFU ) << shift;
        }
        return result;
    }

    // ---------------------------------------------------------------------------------------------
    // Edge-directed pixel-art scaler (xBRZ family)
    //
    // For every source pixel the four corners are examined. A corner is "cut" when a diagonal edge
    // runs through the 2 x 2 block the corner belongs to. The cut is then rendered into the output
    // block as a thin anti-aliased line whose slope (shallow, steep, diagonal) is chosen from the
    // surrounding 3 x 3 neighbourhood. Everything is formulated on palette indices; colours are only
    // touched when output pixels are blended.
    // ---------------------------------------------------------------------------------------------

    enum class Blend : uint8_t
    {
        NONE = 0,
        NORMAL = 1,
        DOMINANT = 2
    };

    // Corner identifiers. The order matters: a clockwise rotation of the kernel by 90 degrees moves
    // the corner with index i to the position of the corner with index ( i + 3 ) % 4.
    enum Corner : int
    {
        BOTTOM_RIGHT = 0,
        TOP_RIGHT = 1,
        TOP_LEFT = 2,
        BOTTOM_LEFT = 3
    };

    inline Blend getBlend( const uint8_t info, const int corner )
    {
        return static_cast<Blend>( ( info >> ( corner * 2 ) ) & 3U );
    }

    inline void setBlend( uint8_t & info, const int corner, const Blend blend )
    {
        info = static_cast<uint8_t>( ( info & ~( 3U << ( corner * 2 ) ) ) | ( static_cast<uint8_t>( blend ) << ( corner * 2 ) ) );
    }

    // Thresholds expressed in the 1/32 units used by the distance table.
    constexpr uint32_t distanceScale{ 32 };
    constexpr uint32_t equalColorTolerance{ 30 * distanceScale };
    // Multiplied by 10 to stay in integers: a gradient is dominant when the other direction is 3.6 times more expensive.
    constexpr uint32_t dominantDirectionThresholdX10{ 36 };
    // A line is considered shallow or steep when one gradient is 2.2 times smaller than the other.
    constexpr uint32_t steepDirectionThresholdX10{ 22 };

    class EdgeScalerContext
    {
    public:
        EdgeScalerContext( const uint8_t * src, const int32_t srcPitch, const int32_t width, const int32_t height, const uint16_t * distance, const uint32_t * palette )
            : _src( src )
            , _srcPitch( srcPitch )
            , _width( width )
            , _height( height )
            , _distance( distance )
            , _palette( palette )
        {}

        uint8_t at( int32_t x, int32_t y ) const
        {
            x = std::clamp( x, 0, _width - 1 );
            y = std::clamp( y, 0, _height - 1 );
            return _src[y * _srcPitch + x];
        }

        uint32_t dist( const uint8_t a, const uint8_t b ) const
        {
            return _distance[static_cast<size_t>( a ) * 256 + b];
        }

        bool eq( const uint8_t a, const uint8_t b ) const
        {
            return dist( a, b ) < equalColorTolerance;
        }

        uint32_t color( const uint8_t index ) const
        {
            return _palette[index];
        }

        // Examines the 2 x 2 block whose top-left pixel is ( x, y ) together with its 4 x 4 surrounding and
        // returns the blend state for the four corners that meet in the middle of the block:
        // the bottom-right corner of the top-left pixel, and so on.
        void analyzeBlock( const int32_t x, const int32_t y, Blend & topLeftPixel, Blend & topRightPixel, Blend & bottomLeftPixel, Blend & bottomRightPixel ) const
        {
            topLeftPixel = topRightPixel = bottomLeftPixel = bottomRightPixel = Blend::NONE;

            // The 4 x 4 neighbourhood:
            //   p00 p10 p20 p30
            //   p01 p11 p21 p31
            //   p02 p12 p22 p32
            //   p03 p13 p23 p33
            // The examined block is p11 p21 / p12 p22.
            const uint8_t p11 = at( x, y );
            const uint8_t p21 = at( x + 1, y );
            const uint8_t p12 = at( x, y + 1 );
            const uint8_t p22 = at( x + 1, y + 1 );

            // Horizontal or vertical structure: no diagonal edge is possible.
            if ( ( eq( p11, p21 ) && eq( p12, p22 ) ) || ( eq( p11, p12 ) && eq( p21, p22 ) ) ) {
                return;
            }

            const uint8_t p10 = at( x, y - 1 );
            const uint8_t p20 = at( x + 1, y - 1 );
            const uint8_t p01 = at( x - 1, y );
            const uint8_t p31 = at( x + 2, y );
            const uint8_t p02 = at( x - 1, y + 1 );
            const uint8_t p32 = at( x + 2, y + 1 );
            const uint8_t p13 = at( x, y + 2 );
            const uint8_t p23 = at( x + 1, y + 2 );

            // Cost of a line running along the p21 - p12 diagonal (from top-right to bottom-left) and
            // cost of a line running along the p11 - p22 diagonal (from top-left to bottom-right).
            // Each cost sums the colour changes along the line and its two extensions; the central
            // segment is weighted 4 times.
            const uint32_t costAntiDiagonal = dist( p02, p12 ) + dist( p12, p13 ) + dist( p20, p21 ) + dist( p21, p31 ) + 4 * dist( p21, p12 );
            const uint32_t costDiagonal = dist( p01, p11 ) + dist( p11, p10 ) + dist( p23, p22 ) + dist( p22, p32 ) + 4 * dist( p11, p22 );

            if ( costAntiDiagonal < costDiagonal ) {
                // A line separates p11 from p22. Their corners get cut.
                const Blend blend = ( dominantDirectionThresholdX10 * costAntiDiagonal < 10 * costDiagonal ) ? Blend::DOMINANT : Blend::NORMAL;
                if ( !eq( p11, p21 ) && !eq( p11, p12 ) ) {
                    topLeftPixel = blend;
                }
                if ( !eq( p22, p21 ) && !eq( p22, p12 ) ) {
                    bottomRightPixel = blend;
                }
            }
            else if ( costDiagonal < costAntiDiagonal ) {
                // A line separates p21 from p12.
                const Blend blend = ( dominantDirectionThresholdX10 * costDiagonal < 10 * costAntiDiagonal ) ? Blend::DOMINANT : Blend::NORMAL;
                if ( !eq( p21, p11 ) && !eq( p21, p22 ) ) {
                    topRightPixel = blend;
                }
                if ( !eq( p12, p11 ) && !eq( p12, p22 ) ) {
                    bottomLeftPixel = blend;
                }
            }
        }

    private:
        const uint8_t * _src;
        const int32_t _srcPitch;
        const int32_t _width;
        const int32_t _height;
        const uint16_t * _distance;
        const uint32_t * _palette;
    };

    // A view of the N x N output block of one source pixel, rotated so that the corner being processed
    // is the bottom-right one. ref( row, column ) addresses the rotated block.
    class OutputBlock
    {
    public:
        OutputBlock( uint32_t * origin, const int32_t pitch, const int32_t size, const int rotation )
            : _origin( origin )
            , _pitch( pitch )
            , _size( size )
            , _rotation( rotation )
        {}

        uint32_t & ref( const int32_t row, const int32_t col ) const
        {
            int32_t r = row;
            int32_t c = col;
            switch ( _rotation ) {
            case 1:
                // The kernel was rotated once clockwise. Rotate the coordinates back.
                r = _size - 1 - col;
                c = row;
                break;
            case 2:
                r = _size - 1 - row;
                c = _size - 1 - col;
                break;
            case 3:
                r = col;
                c = _size - 1 - row;
                break;
            default:
                break;
            }
            return _origin[r * _pitch + c];
        }

        void mix( const int32_t row, const int32_t col, const uint32_t color, const uint32_t num, const uint32_t den ) const
        {
            uint32_t & pixel = ref( row, col );
            pixel = mixPixel( pixel, color, num, den );
        }

        void set( const int32_t row, const int32_t col, const uint32_t color ) const
        {
            ref( row, col ) = color;
        }

    private:
        uint32_t * _origin;
        const int32_t _pitch;
        const int32_t _size;
        const int _rotation;
    };

    enum class LineKind
    {
        SHALLOW,
        STEEP,
        STEEP_AND_SHALLOW,
        DIAGONAL,
        CORNER
    };

    void drawCut2x( const OutputBlock & out, const uint32_t col, const LineKind kind )
    {
        switch ( kind ) {
        case LineKind::SHALLOW:
            out.mix( 1, 0, col, 1, 4 );
            out.mix( 1, 1, col, 3, 4 );
            break;
        case LineKind::STEEP:
            out.mix( 0, 1, col, 1, 4 );
            out.mix( 1, 1, col, 3, 4 );
            break;
        case LineKind::STEEP_AND_SHALLOW:
            out.mix( 1, 0, col, 1, 4 );
            out.mix( 0, 1, col, 1, 4 );
            out.mix( 1, 1, col, 5, 6 );
            break;
        case LineKind::DIAGONAL:
            out.mix( 1, 1, col, 1, 2 );
            break;
        case LineKind::CORNER:
            // The area of a quarter circle corner cut: 1 - pi / 4.
            out.mix( 1, 1, col, 21, 100 );
            break;
        default:
            break;
        }
    }

    void drawCut3x( const OutputBlock & out, const uint32_t col, const LineKind kind )
    {
        switch ( kind ) {
        case LineKind::SHALLOW:
            out.mix( 2, 0, col, 1, 4 );
            out.mix( 1, 2, col, 1, 4 );
            out.mix( 2, 1, col, 3, 4 );
            out.set( 2, 2, col );
            break;
        case LineKind::STEEP:
            out.mix( 0, 2, col, 1, 4 );
            out.mix( 2, 1, col, 1, 4 );
            out.mix( 1, 2, col, 3, 4 );
            out.set( 2, 2, col );
            break;
        case LineKind::STEEP_AND_SHALLOW:
            out.mix( 2, 0, col, 1, 4 );
            out.mix( 0, 2, col, 1, 4 );
            out.mix( 2, 1, col, 3, 4 );
            out.mix( 1, 2, col, 3, 4 );
            out.set( 2, 2, col );
            break;
        case LineKind::DIAGONAL:
            out.mix( 1, 2, col, 1, 8 );
            out.mix( 2, 1, col, 1, 8 );
            out.mix( 2, 2, col, 7, 8 );
            break;
        case LineKind::CORNER:
            out.mix( 2, 2, col, 45, 100 );
            break;
        default:
            break;
        }
    }

    void drawCut4x( const OutputBlock & out, const uint32_t col, const LineKind kind )
    {
        switch ( kind ) {
        case LineKind::SHALLOW:
            out.mix( 3, 1, col, 3, 4 );
            out.mix( 2, 2, col, 3, 4 );
            out.mix( 3, 0, col, 1, 4 );
            out.mix( 1, 2, col, 1, 4 );
            out.set( 3, 2, col );
            out.set( 3, 3, col );
            break;
        case LineKind::STEEP:
            out.mix( 1, 3, col, 3, 4 );
            out.mix( 2, 2, col, 3, 4 );
            out.mix( 0, 3, col, 1, 4 );
            out.mix( 2, 1, col, 1, 4 );
            out.set( 2, 3, col );
            out.set( 3, 3, col );
            break;
        case LineKind::STEEP_AND_SHALLOW:
            out.mix( 3, 1, col, 3, 4 );
            out.mix( 3, 0, col, 1, 4 );
            out.mix( 1, 3, col, 3, 4 );
            out.mix( 0, 3, col, 1, 4 );
            out.mix( 2, 2, col, 1, 3 );
            out.set( 3, 2, col );
            out.set( 2, 3, col );
            out.set( 3, 3, col );
            break;
        case LineKind::DIAGONAL:
            out.mix( 3, 2, col, 1, 2 );
            out.mix( 2, 3, col, 1, 2 );
            out.set( 3, 3, col );
            break;
        case LineKind::CORNER:
            out.mix( 3, 3, col, 68, 100 );
            out.mix( 3, 2, col, 9, 100 );
            out.mix( 2, 3, col, 9, 100 );
            break;
        default:
            break;
        }
    }

    // The 3 x 3 neighbourhood of a pixel, rotated so that the processed corner is the bottom-right one:
    //   a b c
    //   d e f
    //   g h i
    struct Kernel3x3
    {
        uint8_t a;
        uint8_t b;
        uint8_t c;
        uint8_t d;
        uint8_t e;
        uint8_t f;
        uint8_t g;
        uint8_t h;
        uint8_t i;

        // Rotates the kernel 90 degrees clockwise.
        void rotate()
        {
            const Kernel3x3 k = *this;
            a = k.g;
            b = k.d;
            c = k.a;
            d = k.h;
            f = k.b;
            g = k.i;
            h = k.f;
            i = k.c;
        }
    };

    void scaleCorner( const EdgeScalerContext & ctx, const Kernel3x3 & k, const uint8_t info, const int rotation, const OutputBlock & out,
                      void ( *drawCut )( const OutputBlock &, uint32_t, LineKind ) )
    {
        // After 'rotation' clockwise turns the bottom-right corner of the rotated kernel is the
        // original corner with index 'rotation'. Its neighbours in the rotated frame are:
        const int cornerId = rotation % 4;
        const int topRightId = ( rotation + 1 ) % 4;
        const int bottomLeftId = ( rotation + 3 ) % 4;

        const Blend blend = getBlend( info, cornerId );
        if ( blend == Blend::NONE ) {
            return;
        }

        bool doLineBlend = true;
        if ( blend != Blend::DOMINANT ) {
            // Avoid double blending of single isolated pixels: if an adjacent corner is also cut and the
            // pixel does not continue in that direction, only round this corner.
            if ( getBlend( info, topRightId ) != Blend::NONE && !ctx.eq( k.e, k.g ) ) {
                doLineBlend = false;
            }
            else if ( getBlend( info, bottomLeftId ) != Blend::NONE && !ctx.eq( k.e, k.c ) ) {
                doLineBlend = false;
            }
            // Do not draw a full line across an L-shaped feature (keeps small details such as eyes intact).
            else if ( !ctx.eq( k.e, k.i ) && ctx.eq( k.g, k.h ) && ctx.eq( k.h, k.i ) && ctx.eq( k.i, k.f ) && ctx.eq( k.f, k.c ) ) {
                doLineBlend = false;
            }
        }

        // The colour painted into the cut is the one of the closer orthogonal neighbour.
        const uint32_t col = ctx.color( ctx.dist( k.e, k.f ) <= ctx.dist( k.e, k.h ) ? k.f : k.h );

        if ( !doLineBlend ) {
            drawCut( out, col, LineKind::CORNER );
            return;
        }

        const uint32_t fg = ctx.dist( k.f, k.g );
        const uint32_t hc = ctx.dist( k.h, k.c );

        const bool haveShallowLine = steepDirectionThresholdX10 * fg <= 10 * hc && !ctx.eq( k.e, k.g ) && !ctx.eq( k.d, k.g );
        const bool haveSteepLine = steepDirectionThresholdX10 * hc <= 10 * fg && !ctx.eq( k.e, k.c ) && !ctx.eq( k.b, k.c );

        if ( haveShallowLine ) {
            drawCut( out, col, haveSteepLine ? LineKind::STEEP_AND_SHALLOW : LineKind::SHALLOW );
        }
        else {
            drawCut( out, col, haveSteepLine ? LineKind::STEEP : LineKind::DIAGONAL );
        }
    }

    void scaleRows( const EdgeScalerContext & ctx, const fheroes2::Rect & roi, const int32_t rowBegin, const int32_t rowEnd, uint32_t * dst, const int32_t dstPitch,
                    const int32_t scale )
    {
        void ( *drawCut )( const OutputBlock &, uint32_t, LineKind ) = nullptr;
        switch ( scale ) {
        case 2:
            drawCut = drawCut2x;
            break;
        case 3:
            drawCut = drawCut3x;
            break;
        case 4:
            drawCut = drawCut4x;
            break;
        default:
            assert( 0 );
            return;
        }

        // Blend information of the pixels of the previous and current row. Every pixel needs the state of
        // its four corners, which are computed from the 2 x 2 blocks starting at ( x - 1, y - 1 ), ( x, y - 1 ),
        // ( x - 1, y ) and ( x, y ). We analyse blocks row by row and keep two rows of per-pixel information.
        const int32_t columns = roi.width + 2;
        std::vector<uint8_t> infoNext( static_cast<size_t>( columns ), 0 );
        std::vector<uint8_t> infoCurrent( static_cast<size_t>( columns ), 0 );

        auto analyzeRow = [&]( const int32_t y, std::vector<uint8_t> & upper, std::vector<uint8_t> & lower ) {
            // Blocks whose top-left pixel is at row 'y' contribute to the bottom corners of pixels in row 'y'
            // and the top corners of pixels in row 'y + 1'.
            // 'upper' holds pixels of row 'y', 'lower' pixels of row 'y + 1'. Column index 0 corresponds to x = roi.x - 1.
            for ( int32_t bx = roi.x - 1; bx <= roi.x + roi.width - 1; ++bx ) {
                Blend tl = Blend::NONE;
                Blend tr = Blend::NONE;
                Blend bl = Blend::NONE;
                Blend br = Blend::NONE;
                ctx.analyzeBlock( bx, y, tl, tr, bl, br );

                const int32_t c = bx - ( roi.x - 1 );
                setBlend( upper[c], BOTTOM_RIGHT, tl );
                setBlend( upper[c + 1], BOTTOM_LEFT, tr );
                setBlend( lower[c], TOP_RIGHT, bl );
                setBlend( lower[c + 1], TOP_LEFT, br );
            }
        };

        // Prime: blocks of the row above the first processed row give the top corners of the first row.
        {
            std::vector<uint8_t> dummy( static_cast<size_t>( columns ), 0 );
            analyzeRow( rowBegin - 1, dummy, infoCurrent );
        }

        for ( int32_t y = rowBegin; y < rowEnd; ++y ) {
            // Blocks starting in this row: bottom corners of this row, top corners of the next row.
            std::fill( infoNext.begin(), infoNext.end(), 0 );
            analyzeRow( y, infoCurrent, infoNext );

            for ( int32_t x = roi.x; x < roi.x + roi.width; ++x ) {
                const uint8_t info = infoCurrent[x - ( roi.x - 1 )];

                Kernel3x3 k;
                k.a = ctx.at( x - 1, y - 1 );
                k.b = ctx.at( x, y - 1 );
                k.c = ctx.at( x + 1, y - 1 );
                k.d = ctx.at( x - 1, y );
                k.e = ctx.at( x, y );
                k.f = ctx.at( x + 1, y );
                k.g = ctx.at( x - 1, y + 1 );
                k.h = ctx.at( x, y + 1 );
                k.i = ctx.at( x + 1, y + 1 );

                uint32_t * blockOrigin = dst + static_cast<ptrdiff_t>( y ) * scale * dstPitch + static_cast<ptrdiff_t>( x ) * scale;

                // Fill the block with the pixel colour.
                const uint32_t color = ctx.color( k.e );
                for ( int32_t r = 0; r < scale; ++r ) {
                    std::fill_n( blockOrigin + static_cast<ptrdiff_t>( r ) * dstPitch, scale, color );
                }

                if ( info == 0 ) {
                    continue;
                }

                for ( int rotation = 0; rotation < 4; ++rotation ) {
                    const OutputBlock out( blockOrigin, dstPitch, scale, rotation );
                    scaleCorner( ctx, k, info, rotation, out, drawCut );
                    k.rotate();
                }
            }

            infoCurrent.swap( infoNext );
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Bicubic scaler with adaptive sharpening
    // ---------------------------------------------------------------------------------------------

    struct CubicTap
    {
        int32_t index[4];
        float weight[4];
    };

    // Catmull-Rom spline weights for a sample position with fractional offset t in [0, 1).
    void cubicWeights( const float t, float ( &w )[4] )
    {
        const float t2 = t * t;
        const float t3 = t2 * t;
        w[0] = 0.5F * ( -t3 + 2.0F * t2 - t );
        w[1] = 0.5F * ( 3.0F * t3 - 5.0F * t2 + 2.0F );
        w[2] = 0.5F * ( -3.0F * t3 + 4.0F * t2 + t );
        w[3] = 0.5F * ( t3 - t2 );
    }

    // Precomputes the 4 source indices and weights for every destination coordinate in [dstBegin, dstEnd).
    std::vector<CubicTap> buildTaps( const int32_t dstBegin, const int32_t dstEnd, const int32_t srcSize, const int32_t dstSize )
    {
        std::vector<CubicTap> taps( static_cast<size_t>( dstEnd - dstBegin ) );
        const float ratio = static_cast<float>( srcSize ) / static_cast<float>( dstSize );

        for ( int32_t d = dstBegin; d < dstEnd; ++d ) {
            const float pos = ( static_cast<float>( d ) + 0.5F ) * ratio - 0.5F;
            const float base = std::floor( pos );
            const float t = pos - base;
            const int32_t b = static_cast<int32_t>( base );

            CubicTap & tap = taps[d - dstBegin];
            cubicWeights( t, tap.weight );
            for ( int32_t i = 0; i < 4; ++i ) {
                tap.index[i] = std::clamp( b - 1 + i, 0, srcSize - 1 );
            }
        }

        return taps;
    }

    inline uint32_t packPixel( const float r, const float g, const float b, const uint32_t other, const fheroes2::PixelLayout & layout )
    {
        const uint32_t ri = static_cast<uint32_t>( std::clamp( r + 0.5F, 0.0F, 255.0F ) );
        const uint32_t gi = static_cast<uint32_t>( std::clamp( g + 0.5F, 0.0F, 255.0F ) );
        const uint32_t bi = static_cast<uint32_t>( std::clamp( b + 0.5F, 0.0F, 255.0F ) );
        const uint32_t colorMask = ( 0xFFU << layout.redShift ) | ( 0xFFU << layout.greenShift ) | ( 0xFFU << layout.blueShift );
        return ( other & ~colorMask ) | ( ri << layout.redShift ) | ( gi << layout.greenShift ) | ( bi << layout.blueShift );
    }

    inline void unpackPixel( const uint32_t pixel, const fheroes2::PixelLayout & layout, int32_t & r, int32_t & g, int32_t & b )
    {
        r = static_cast<int32_t>( ( pixel >> layout.redShift ) & 0xFFU );
        g = static_cast<int32_t>( ( pixel >> layout.greenShift ) & 0xFFU );
        b = static_cast<int32_t>( ( pixel >> layout.blueShift ) & 0xFFU );
    }
}

namespace fheroes2
{
    void forEachRowBand( const int32_t rowBegin, const int32_t rowEnd, const int32_t minBandHeight, const std::function<void( int32_t, int32_t )> & task )
    {
        const int32_t rows = rowEnd - rowBegin;
        if ( rows <= 0 ) {
            return;
        }

        int32_t threadCount = static_cast<int32_t>( std::thread::hardware_concurrency() );
        threadCount = std::clamp( threadCount, 1, maxWorkerThreads );
        threadCount = std::min( threadCount, std::max( 1, rows / std::max( 1, minBandHeight ) ) );

        if ( threadCount <= 1 ) {
            task( rowBegin, rowEnd );
            return;
        }

        const int32_t bandHeight = ( rows + threadCount - 1 ) / threadCount;

        std::vector<std::thread> workers;
        workers.reserve( static_cast<size_t>( threadCount ) - 1 );

        int32_t bandBegin = rowBegin;
        for ( int32_t i = 1; i < threadCount && bandBegin + bandHeight < rowEnd; ++i ) {
            const int32_t bandEnd = bandBegin + bandHeight;
            workers.emplace_back( [&task, bandBegin, bandEnd]() { task( bandBegin, bandEnd ); } );
            bandBegin = bandEnd;
        }

        // The calling thread processes the last band.
        task( bandBegin, rowEnd );

        for ( std::thread & worker : workers ) {
            worker.join();
        }
    }

    void PaletteScaler::setPalette( const uint32_t * palette, const PixelLayout & layout )
    {
        assert( palette != nullptr );

        _layout = layout;
        std::copy_n( palette, 256, _palette.begin() );

        // Y, Cb and Cr of every palette entry (ITU-R BT.601 weights).
        std::array<float, 256> luma{};
        std::array<float, 256> chromaBlue{};
        std::array<float, 256> chromaRed{};

        for ( size_t i = 0; i < 256; ++i ) {
            int32_t r = 0;
            int32_t g = 0;
            int32_t b = 0;
            unpackPixel( _palette[i], layout, r, g, b );

            _rgb[i * 3] = static_cast<uint8_t>( r );
            _rgb[i * 3 + 1] = static_cast<uint8_t>( g );
            _rgb[i * 3 + 2] = static_cast<uint8_t>( b );

            const float y = 0.299F * static_cast<float>( r ) + 0.587F * static_cast<float>( g ) + 0.114F * static_cast<float>( b );
            luma[i] = y;
            chromaBlue[i] = ( static_cast<float>( b ) - y ) * ( 0.5F / ( 1.0F - 0.114F ) );
            chromaRed[i] = ( static_cast<float>( r ) - y ) * ( 0.5F / ( 1.0F - 0.299F ) );
        }

        _distance.assign( 256 * 256, 0 );
        for ( size_t i = 0; i < 256; ++i ) {
            _distance[i * 256 + i] = 0;
            for ( size_t j = i + 1; j < 256; ++j ) {
                const float dy = luma[i] - luma[j];
                const float db = chromaBlue[i] - chromaBlue[j];
                const float dr = chromaRed[i] - chromaRed[j];
                const float d = std::sqrt( dy * dy + db * db + dr * dr ) * static_cast<float>( distanceScale );
                const uint16_t value = static_cast<uint16_t>( std::min( d + 0.5F, 65535.0F ) );
                _distance[i * 256 + j] = value;
                _distance[j * 256 + i] = value;
            }
        }

        _isPaletteSet = true;
    }

    void PaletteScaler::scaleXBRZ( const uint8_t * src, const int32_t srcPitch, const int32_t width, const int32_t height, const Rect & roi, uint32_t * dst,
                                   const int32_t dstPitch, const int32_t scale ) const
    {
        assert( _isPaletteSet && src != nullptr && dst != nullptr );
        assert( scale >= 2 && scale <= 4 );
        assert( roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= width && roi.y + roi.height <= height );

        if ( roi.width <= 0 || roi.height <= 0 ) {
            return;
        }

        const EdgeScalerContext ctx( src, srcPitch, width, height, _distance.data(), _palette.data() );

        forEachRowBand( roi.y, roi.y + roi.height, 16,
                        [&]( const int32_t rowBegin, const int32_t rowEnd ) { scaleRows( ctx, roi, rowBegin, rowEnd, dst, dstPitch, scale ); } );
    }

    Rect PaletteScaler::scaleSharp( const uint8_t * src, const int32_t srcPitch, const int32_t width, const int32_t height, const Rect & roi, uint32_t * dst,
                                    const int32_t dstPitch, const int32_t dstWidth, const int32_t dstHeight ) const
    {
        assert( _isPaletteSet && src != nullptr && dst != nullptr );
        assert( roi.x >= 0 && roi.y >= 0 && roi.x + roi.width <= width && roi.y + roi.height <= height );

        if ( roi.width <= 0 || roi.height <= 0 || dstWidth <= 0 || dstHeight <= 0 ) {
            return {};
        }

        const float scaleX = static_cast<float>( dstWidth ) / static_cast<float>( width );
        const float scaleY = static_cast<float>( dstHeight ) / static_cast<float>( height );

        // Destination pixels whose 4-tap footprint touches the changed source area, plus one pixel for the sharpening pass.
        auto dstRange = [&]( const int32_t srcBegin, const int32_t srcEnd, const float scaleFactor, const int32_t dstSize, int32_t & dstBegin, int32_t & dstEnd ) {
            dstBegin = std::clamp( static_cast<int32_t>( std::floor( static_cast<float>( srcBegin - 2 ) * scaleFactor ) ) - 1, 0, dstSize );
            dstEnd = std::clamp( static_cast<int32_t>( std::ceil( static_cast<float>( srcEnd + 2 ) * scaleFactor ) ) + 1, 0, dstSize );
        };

        int32_t dstX0 = 0;
        int32_t dstX1 = 0;
        int32_t dstY0 = 0;
        int32_t dstY1 = 0;
        dstRange( roi.x, roi.x + roi.width, scaleX, dstWidth, dstX0, dstX1 );
        dstRange( roi.y, roi.y + roi.height, scaleY, dstHeight, dstY0, dstY1 );

        if ( dstX1 <= dstX0 || dstY1 <= dstY0 ) {
            return {};
        }

        // The bicubic pass covers one extra pixel around the output area so the sharpening pass has valid neighbours.
        const int32_t cubX0 = std::max( 0, dstX0 - 1 );
        const int32_t cubX1 = std::min( dstWidth, dstX1 + 1 );
        const int32_t cubY0 = std::max( 0, dstY0 - 1 );
        const int32_t cubY1 = std::min( dstHeight, dstY1 + 1 );
        const int32_t cubW = cubX1 - cubX0;
        const int32_t cubH = cubY1 - cubY0;

        const std::vector<CubicTap> tapsX = buildTaps( cubX0, cubX1, width, dstWidth );
        const std::vector<CubicTap> tapsY = buildTaps( cubY0, cubY1, height, dstHeight );

        // Intermediate bicubic result as packed pixels.
        std::vector<uint32_t> cubic( static_cast<size_t>( cubW ) * cubH );

        const PixelLayout layout = _layout;
        const uint8_t * rgb = _rgb.data();
        const uint32_t other = _palette[0];

        forEachRowBand( cubY0, cubY1, 8, [&]( const int32_t rowBegin, const int32_t rowEnd ) {
            // Horizontal pass: resample every source row needed by this band into float RGB rows.
            int32_t srcRowBegin = tapsY[rowBegin - cubY0].index[0];
            int32_t srcRowEnd = tapsY[rowEnd - 1 - cubY0].index[3] + 1;
            srcRowBegin = std::clamp( srcRowBegin, 0, height );
            srcRowEnd = std::clamp( srcRowEnd, srcRowBegin, height );
            const int32_t srcRows = srcRowEnd - srcRowBegin;

            std::vector<float> rows( static_cast<size_t>( srcRows ) * cubW * 3 );
            for ( int32_t sy = 0; sy < srcRows; ++sy ) {
                const uint8_t * srcRow = src + static_cast<ptrdiff_t>( srcRowBegin + sy ) * srcPitch;
                float * outRow = rows.data() + static_cast<ptrdiff_t>( sy ) * cubW * 3;
                for ( int32_t dx = 0; dx < cubW; ++dx ) {
                    const CubicTap & tap = tapsX[dx];
                    float r = 0;
                    float g = 0;
                    float b = 0;
                    for ( int32_t i = 0; i < 4; ++i ) {
                        const uint8_t * p = rgb + static_cast<ptrdiff_t>( srcRow[tap.index[i]] ) * 3;
                        r += tap.weight[i] * p[0];
                        g += tap.weight[i] * p[1];
                        b += tap.weight[i] * p[2];
                    }
                    outRow[dx * 3] = r;
                    outRow[dx * 3 + 1] = g;
                    outRow[dx * 3 + 2] = b;
                }
            }

            // Vertical pass.
            for ( int32_t dy = rowBegin; dy < rowEnd; ++dy ) {
                const CubicTap & tap = tapsY[dy - cubY0];
                uint32_t * outRow = cubic.data() + static_cast<ptrdiff_t>( dy - cubY0 ) * cubW;
                for ( int32_t dx = 0; dx < cubW; ++dx ) {
                    float r = 0;
                    float g = 0;
                    float b = 0;
                    for ( int32_t i = 0; i < 4; ++i ) {
                        const float * p = rows.data() + ( static_cast<ptrdiff_t>( tap.index[i] - srcRowBegin ) * cubW + dx ) * 3;
                        r += tap.weight[i] * p[0];
                        g += tap.weight[i] * p[1];
                        b += tap.weight[i] * p[2];
                    }
                    outRow[dx] = packPixel( r, g, b, other, layout );
                }
            }
        } );

        // Sharpening pass: a 5-tap cross with a negative lobe, clamped to the local range to avoid ringing.
        // 'lobe' is the weight of each of the four neighbours; 0.8 sharpness gives -0.1.
        constexpr float lobe = -0.1F;
        constexpr float normalization = 1.0F / ( 1.0F + 4.0F * lobe );

        forEachRowBand( dstY0, dstY1, 16, [&]( const int32_t rowBegin, const int32_t rowEnd ) {
            for ( int32_t dy = rowBegin; dy < rowEnd; ++dy ) {
                const int32_t cy = dy - cubY0;
                const int32_t cyUp = std::max( 0, cy - 1 );
                const int32_t cyDown = std::min( cubH - 1, cy + 1 );
                uint32_t * dstRow = dst + static_cast<ptrdiff_t>( dy ) * dstPitch;

                for ( int32_t dx = dstX0; dx < dstX1; ++dx ) {
                    const int32_t cx = dx - cubX0;
                    const int32_t cxLeft = std::max( 0, cx - 1 );
                    const int32_t cxRight = std::min( cubW - 1, cx + 1 );

                    const uint32_t e = cubic[static_cast<size_t>( cy ) * cubW + cx];
                    const uint32_t n = cubic[static_cast<size_t>( cyUp ) * cubW + cx];
                    const uint32_t s = cubic[static_cast<size_t>( cyDown ) * cubW + cx];
                    const uint32_t w = cubic[static_cast<size_t>( cy ) * cubW + cxLeft];
                    const uint32_t o = cubic[static_cast<size_t>( cy ) * cubW + cxRight];

                    int32_t er = 0;
                    int32_t eg = 0;
                    int32_t eb = 0;
                    unpackPixel( e, layout, er, eg, eb );

                    float sum[3] = { 0, 0, 0 };
                    int32_t mn[3] = { er, eg, eb };
                    int32_t mx[3] = { er, eg, eb };
                    for ( const uint32_t p : { n, s, w, o } ) {
                        int32_t r = 0;
                        int32_t g = 0;
                        int32_t b = 0;
                        unpackPixel( p, layout, r, g, b );
                        sum[0] += static_cast<float>( r );
                        sum[1] += static_cast<float>( g );
                        sum[2] += static_cast<float>( b );
                        mn[0] = std::min( mn[0], r );
                        mn[1] = std::min( mn[1], g );
                        mn[2] = std::min( mn[2], b );
                        mx[0] = std::max( mx[0], r );
                        mx[1] = std::max( mx[1], g );
                        mx[2] = std::max( mx[2], b );
                    }

                    const float r = std::clamp( ( static_cast<float>( er ) + lobe * sum[0] ) * normalization, static_cast<float>( mn[0] ), static_cast<float>( mx[0] ) );
                    const float g = std::clamp( ( static_cast<float>( eg ) + lobe * sum[1] ) * normalization, static_cast<float>( mn[1] ), static_cast<float>( mx[1] ) );
                    const float b = std::clamp( ( static_cast<float>( eb ) + lobe * sum[2] ) * normalization, static_cast<float>( mn[2] ), static_cast<float>( mx[2] ) );

                    dstRow[dx] = packPixel( r, g, b, e, layout );
                }
            }
        } );

        return { dstX0, dstY0, dstX1 - dstX0, dstY1 - dstY0 };
    }
}
