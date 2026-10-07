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

// A development tool which exercises the CPU upscalers of the engine on an image file.
// It writes the nearest, xBRZ and sharp results as PPM files, reports timings and verifies
// that scaling a sub-rectangle produces exactly the same pixels as scaling the whole image.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "image.h"
#include "image_palette.h"
#include "image_scaler.h"
#include "image_tool.h"
#include "math_base.h"
#include "serialize.h"
#include "system.h"

namespace
{
    constexpr fheroes2::PixelLayout layout{ 0, 8, 16 };

    bool writePPM( const std::string & path, const std::vector<uint32_t> & pixels, const int32_t width, const int32_t height )
    {
        std::ofstream file( path, std::ios::binary );
        if ( !file ) {
            return false;
        }

        file << "P6\n" << width << ' ' << height << "\n255\n";
        std::vector<uint8_t> row( static_cast<size_t>( width ) * 3 );
        for ( int32_t y = 0; y < height; ++y ) {
            for ( int32_t x = 0; x < width; ++x ) {
                const uint32_t p = pixels[static_cast<size_t>( y ) * width + x];
                row[x * 3] = static_cast<uint8_t>( ( p >> layout.redShift ) & 0xFF );
                row[x * 3 + 1] = static_cast<uint8_t>( ( p >> layout.greenShift ) & 0xFF );
                row[x * 3 + 2] = static_cast<uint8_t>( ( p >> layout.blueShift ) & 0xFF );
            }
            file.write( reinterpret_cast<const char *>( row.data() ), static_cast<std::streamsize>( row.size() ) );
        }

        return file.good();
    }

    double milliseconds( const std::chrono::steady_clock::time_point & start )
    {
        return std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count();
    }

    template <typename Func>
    double bestOfThree( const Func & func )
    {
        double best = 1e9;
        for ( int i = 0; i < 3; ++i ) {
            const auto start = std::chrono::steady_clock::now();
            func();
            best = std::min( best, milliseconds( start ) );
        }
        return best;
    }

    bool checkRegions( const std::string & name, const std::vector<uint32_t> & reference, const int32_t width, const int32_t height, const int32_t srcWidth,
                       const int32_t srcHeight, const std::function<fheroes2::Rect( const fheroes2::Rect &, std::vector<uint32_t> & )> & scaleRegion )
    {
        std::mt19937 rng( 12345 );
        std::uniform_int_distribution<int32_t> distX( 0, srcWidth - 1 );
        std::uniform_int_distribution<int32_t> distY( 0, srcHeight - 1 );

        for ( int i = 0; i < 50; ++i ) {
            fheroes2::Rect roi;
            roi.x = distX( rng );
            roi.y = distY( rng );
            roi.width = std::min( srcWidth - roi.x, 1 + distX( rng ) % 64 );
            roi.height = std::min( srcHeight - roi.y, 1 + distY( rng ) % 64 );

            // Start from garbage so that any pixel which is not rewritten shows up.
            std::vector<uint32_t> output( static_cast<size_t>( width ) * height, 0xDEADBEEF );
            const fheroes2::Rect written = scaleRegion( roi, output );

            for ( int32_t y = written.y; y < written.y + written.height; ++y ) {
                for ( int32_t x = written.x; x < written.x + written.width; ++x ) {
                    const size_t idx = static_cast<size_t>( y ) * width + x;
                    if ( output[idx] != reference[idx] ) {
                        std::cerr << name << ": mismatch at (" << x << ", " << y << ") for source ROI " << roi.x << ", " << roi.y << ", " << roi.width << ", "
                                  << roi.height << std::endl;
                        return false;
                    }
                }
            }
        }

        std::cout << name << ": 50 random regions match the full-frame result." << std::endl;
        return true;
    }
}

int main( int argc, char ** argv )
{
    if ( argc < 5 ) {
        std::cerr << "Syntax: " << System::GetFileName( argv[0] ) << " palette_file.pal input_image scale output_prefix" << std::endl;
        return EXIT_FAILURE;
    }

    {
        StreamFile paletteStream;
        if ( !paletteStream.open( argv[1], "rb" ) ) {
            std::cerr << "Cannot open file " << argv[1] << std::endl;
            return EXIT_FAILURE;
        }

        const std::vector<uint8_t> palette = paletteStream.getRaw( 0 );
        if ( palette.size() != fheroes2::paletteSizeBytes ) {
            std::cerr << "Invalid palette size of " << palette.size() << std::endl;
            return EXIT_FAILURE;
        }

        fheroes2::setGamePalette( palette );
    }

    fheroes2::Image input;
    if ( !fheroes2::Load( argv[2], input ) || input.empty() ) {
        std::cerr << "Cannot load image " << argv[2] << std::endl;
        return EXIT_FAILURE;
    }

    const int32_t scale = std::atoi( argv[3] );
    if ( scale < 2 || scale > 4 ) {
        std::cerr << "Scale must be 2, 3 or 4." << std::endl;
        return EXIT_FAILURE;
    }

    const std::string prefix = argv[4];

    const int32_t width = input.width();
    const int32_t height = input.height();
    const int32_t outWidth = width * scale;
    const int32_t outHeight = height * scale;

    std::cout << "Input: " << width << " x " << height << ", output: " << outWidth << " x " << outHeight << std::endl;

    std::array<uint32_t, fheroes2::paletteSize> palette32{};
    {
        const std::array<fheroes2::RGB, fheroes2::paletteSize> palette = fheroes2::getNormalizedRGBGamePalette();
        for ( size_t i = 0; i < palette.size(); ++i ) {
            palette32[i] = palette[i].getRGBA();
        }
    }

    fheroes2::PaletteScaler scaler;
    const double paletteMs = bestOfThree( [&]() { scaler.setPalette( palette32.data(), layout ); } );
    std::cout << "Palette tables: " << paletteMs << " ms" << std::endl;

    const uint8_t * src = input.image();
    const fheroes2::Rect fullRoi( 0, 0, width, height );

    // Nearest neighbour reference.
    {
        std::vector<uint32_t> nearest( static_cast<size_t>( outWidth ) * outHeight );
        for ( int32_t y = 0; y < outHeight; ++y ) {
            for ( int32_t x = 0; x < outWidth; ++x ) {
                nearest[static_cast<size_t>( y ) * outWidth + x] = palette32[src[( y / scale ) * width + x / scale]];
            }
        }
        writePPM( prefix + "_nearest.ppm", nearest, outWidth, outHeight );
    }

    bool ok = true;

    // xBRZ.
    {
        std::vector<uint32_t> out( static_cast<size_t>( outWidth ) * outHeight );
        const double ms = bestOfThree( [&]() { scaler.scaleXBRZ( src, width, width, height, fullRoi, out.data(), outWidth, scale ); } );
        std::cout << "xBRZ " << scale << "x full frame: " << ms << " ms" << std::endl;
        writePPM( prefix + "_xbrz.ppm", out, outWidth, outHeight );

        ok = checkRegions( "xBRZ", out, outWidth, outHeight, width, height,
                           [&]( const fheroes2::Rect & roi, std::vector<uint32_t> & output ) {
                               scaler.scaleXBRZ( src, width, width, height, roi, output.data(), outWidth, scale );
                               return fheroes2::Rect( roi.x * scale, roi.y * scale, roi.width * scale, roi.height * scale );
                           } )
             && ok;
    }

    // Sharp at the integer scale and at a non-integer scale.
    for ( const double factor : { static_cast<double>( scale ), scale - 0.5 } ) {
        const int32_t dstWidth = static_cast<int32_t>( width * factor );
        const int32_t dstHeight = static_cast<int32_t>( height * factor );
        std::vector<uint32_t> out( static_cast<size_t>( dstWidth ) * dstHeight );

        const double ms = bestOfThree( [&]() { scaler.scaleSharp( src, width, width, height, fullRoi, out.data(), dstWidth, dstWidth, dstHeight ); } );
        std::cout << "Sharp " << factor << "x full frame (" << dstWidth << " x " << dstHeight << "): " << ms << " ms" << std::endl;
        writePPM( prefix + "_sharp_" + std::to_string( factor ).substr( 0, 3 ) + ".ppm", out, dstWidth, dstHeight );

        ok = checkRegions( "Sharp " + std::to_string( factor ).substr( 0, 3 ), out, dstWidth, dstHeight, width, height,
                           [&]( const fheroes2::Rect & roi, std::vector<uint32_t> & output ) {
                               return scaler.scaleSharp( src, width, width, height, roi, output.data(), dstWidth, dstWidth, dstHeight );
                           } )
             && ok;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
