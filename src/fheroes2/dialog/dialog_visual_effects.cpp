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

#include "dialog_visual_effects.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "cursor.h"
#include "game_assets.h"
#include "game_hotkeys.h"
#include "icn.h"
#include "image.h"
#include "localevent.h"
#include "math_base.h"
#include "screen.h"
#include "settings.h"
#include "translations.h"
#include "ui_button.h"
#include "ui_dialog.h"
#include "ui_option_item.h"
#include "ui_text.h"
#include "ui_window.h"

namespace
{
    enum class SelectedWindow : int
    {
        Configuration,
        DayNight,
        SeasonTint,
        Weather,
        EnhancedShadows,
        UiSkin,
        PostProcessing,
        Exit
    };

    const fheroes2::Rect dayNightRoi{ fheroes2::threeOptionsOffsetX, fheroes2::optionsOffsetY, fheroes2::optionIconSize, fheroes2::optionIconSize };
    const fheroes2::Rect seasonTintRoi{ fheroes2::threeOptionsOffsetX + fheroes2::threeOptionsStepX, fheroes2::optionsOffsetY, fheroes2::optionIconSize,
                                        fheroes2::optionIconSize };
    const fheroes2::Rect weatherRoi{ fheroes2::threeOptionsOffsetX + fheroes2::threeOptionsStepX * 2, fheroes2::optionsOffsetY, fheroes2::optionIconSize,
                                     fheroes2::optionIconSize };

    const fheroes2::Rect enhancedShadowsRoi{ fheroes2::threeOptionsOffsetX, fheroes2::optionsOffsetY + fheroes2::optionsStepY, fheroes2::optionIconSize,
                                             fheroes2::optionIconSize };
    const fheroes2::Rect uiSkinRoi{ fheroes2::threeOptionsOffsetX + fheroes2::threeOptionsStepX, fheroes2::optionsOffsetY + fheroes2::optionsStepY,
                                    fheroes2::optionIconSize, fheroes2::optionIconSize };
    const fheroes2::Rect postProcessingRoi{ fheroes2::threeOptionsOffsetX + fheroes2::threeOptionsStepX * 2, fheroes2::optionsOffsetY + fheroes2::optionsStepY,
                                            fheroes2::optionIconSize, fheroes2::optionIconSize };

    bool isPostProcessingAvailable()
    {
#if defined( ENABLE_GPU_RENDERER )
        return true;
#else
        return false;
#endif
    }

    // Draws an option icon consisting of the empty icon background with a short text in the middle.
    void drawTextIconOption( const fheroes2::Rect & optionRoi, std::string title, std::string value, const char * iconText, const bool isEnabled )
    {
        fheroes2::Sprite image = Assets::getImage( ICN::EMPTY_OPTION_ICON_BACKGROUND, 0 );

        const fheroes2::FontType font( fheroes2::FontSize::NORMAL, isEnabled ? fheroes2::FontColor::YELLOW : fheroes2::FontColor::GRAY );
        const fheroes2::Text text( iconText, font );
        text.draw( ( image.width() - text.width() ) / 2, ( image.height() - text.height() ) / 2, image );

        fheroes2::drawOption( optionRoi, image, std::move( title ), std::move( value ), fheroes2::UiOptionTextWidth::THREE_ELEMENTS_ROW );
    }

    void drawToggle( const fheroes2::Rect & optionRoi, const char * title, const char * iconText, const bool isEnabled )
    {
        drawTextIconOption( optionRoi, title, isEnabled ? _( "On" ) : _( "Off" ), iconText, isEnabled );
    }

    void drawUiSkin( const fheroes2::Rect & optionRoi, const bool isModern )
    {
        drawTextIconOption( optionRoi, _( "UI Skin" ), isModern ? _( "skin|Modern" ) : _( "skin|Classic" ), _( "fxIcon|UI" ), isModern );
    }

    void drawPostProcessing( const fheroes2::Rect & optionRoi, const Settings & conf )
    {
        if ( !isPostProcessingAvailable() ) {
            drawTextIconOption( optionRoi, _( "Post-Processing" ), _( "N/A" ), _( "fxIcon|FX" ), false );
            return;
        }

        if ( !conf.isFxPostProcessingEnabled() ) {
            drawTextIconOption( optionRoi, _( "Post-Processing" ), _( "Off" ), _( "fxIcon|FX" ), false );
            return;
        }

        const char * presetName = nullptr;
        switch ( conf.getPostProcessPreset() ) {
        case PostProcessPreset::BLOOM:
            presetName = _( "postProcess|Bloom" );
            break;
        case PostProcessPreset::VIGNETTE:
            presetName = _( "postProcess|Vignette" );
            break;
        case PostProcessPreset::CRT:
            presetName = _( "postProcess|CRT" );
            break;
        case PostProcessPreset::NONE:
        default:
            presetName = _( "On" );
            break;
        }

        drawTextIconOption( optionRoi, _( "Post-Processing" ), presetName, _( "fxIcon|FX" ), true );
    }

    SelectedWindow showConfigurationWindow()
    {
        fheroes2::Display & display = fheroes2::Display::instance();

        fheroes2::StandardWindow background( 289, fheroes2::optionsStepY * 2 + 52, true, display );

        const fheroes2::Rect windowRoi = background.activeArea();

        const Settings & conf = Settings::Get();
        const bool isEvilInterface = conf.isEvilInterfaceEnabled();

        fheroes2::Button buttonOk;
        const int buttonOkIcnId = isEvilInterface ? ICN::BUTTON_SMALL_OKAY_EVIL : ICN::BUTTON_SMALL_OKAY_GOOD;
        background.renderButton( buttonOk, buttonOkIcnId, 0, 1, { 0, 11 }, fheroes2::StandardWindow::Padding::BOTTOM_CENTER );

        const fheroes2::Rect windowDayNightRoi( dayNightRoi + windowRoi.getPosition() );
        const fheroes2::Rect windowSeasonTintRoi( seasonTintRoi + windowRoi.getPosition() );
        const fheroes2::Rect windowWeatherRoi( weatherRoi + windowRoi.getPosition() );
        const fheroes2::Rect windowEnhancedShadowsRoi( enhancedShadowsRoi + windowRoi.getPosition() );
        const fheroes2::Rect windowUiSkinRoi( uiSkinRoi + windowRoi.getPosition() );
        const fheroes2::Rect windowPostProcessingRoi( postProcessingRoi + windowRoi.getPosition() );

        drawToggle( windowDayNightRoi, _( "Day/Night Cycle" ), _( "fxIcon|Sun" ), conf.isFxDayNightEnabled() );
        drawToggle( windowSeasonTintRoi, _( "Seasonal Tint" ), _( "fxIcon|Leaf" ), conf.isFxSeasonTintEnabled() );
        drawToggle( windowWeatherRoi, _( "Weather" ), _( "fxIcon|Rain" ), conf.isFxWeatherEnabled() );
        drawToggle( windowEnhancedShadowsRoi, _( "Enhanced Shadows" ), _( "fxIcon|Shade" ), conf.isFxEnhancedShadowsEnabled() );
        drawUiSkin( windowUiSkinRoi, conf.isFxModernUiEnabled() );
        drawPostProcessing( windowPostProcessingRoi, conf );

        display.render( background.totalArea() );

        LocalEvent & le = LocalEvent::Get();
        while ( le.HandleEvents() ) {
            buttonOk.drawOnState( le.isMouseLeftButtonPressedAndHeldInArea( buttonOk.area() ) );

            if ( le.MouseClickLeft( buttonOk.area() ) || Game::HotKeyCloseWindow() ) {
                break;
            }
            if ( le.MouseClickLeft( windowDayNightRoi ) ) {
                return SelectedWindow::DayNight;
            }
            if ( le.MouseClickLeft( windowSeasonTintRoi ) ) {
                return SelectedWindow::SeasonTint;
            }
            if ( le.MouseClickLeft( windowWeatherRoi ) ) {
                return SelectedWindow::Weather;
            }
            if ( le.MouseClickLeft( windowEnhancedShadowsRoi ) ) {
                return SelectedWindow::EnhancedShadows;
            }
            if ( le.MouseClickLeft( windowUiSkinRoi ) ) {
                return SelectedWindow::UiSkin;
            }
            if ( le.MouseClickLeft( windowPostProcessingRoi ) ) {
                return SelectedWindow::PostProcessing;
            }

            if ( le.isMouseRightButtonPressedInArea( windowDayNightRoi ) ) {
                fheroes2::showStandardTextMessage(
                    _( "Day/Night Cycle" ),
                    _( "Tint the Adventure Map and battles according to the time of day. One in-game week is one full cycle from dawn through noon and dusk to night." ),
                    0 );
            }
            else if ( le.isMouseRightButtonPressedInArea( windowSeasonTintRoi ) ) {
                fheroes2::showStandardTextMessage( _( "Seasonal Tint" ), _( "Give the Adventure Map a subtle seasonal colour depending on the in-game month." ), 0 );
            }
            else if ( le.isMouseRightButtonPressedInArea( windowWeatherRoi ) ) {
                fheroes2::showStandardTextMessage( _( "Weather" ), _( "Show rain, snow, fog or falling leaves over the Adventure Map. The weather changes every week." ),
                                                   0 );
            }
            else if ( le.isMouseRightButtonPressedInArea( windowEnhancedShadowsRoi ) ) {
                fheroes2::showStandardTextMessage( _( "Enhanced Shadows" ),
                                                   _( "Replace the hard shadows of map objects, heroes and creatures with soft gradient shadows." ), 0 );
            }
            else if ( le.isMouseRightButtonPressedInArea( windowUiSkinRoi ) ) {
                fheroes2::showStandardTextMessage( _( "UI Skin" ), _( "Toggle between the classic user interface and a modern, higher contrast skin." ), 0 );
            }
            else if ( le.isMouseRightButtonPressedInArea( windowPostProcessingRoi ) ) {
                if ( isPostProcessingAvailable() ) {
                    fheroes2::showStandardTextMessage(
                        _( "Post-Processing" ),
                        _( "Apply a GPU post-processing effect to the whole screen: Bloom, Vignette or CRT. Click to cycle through the presets." ), 0 );
                }
                else {
                    fheroes2::showStandardTextMessage( _( "Post-Processing" ), _( "GPU post-processing is not available in this build of the game." ), 0 );
                }
            }
            else if ( le.isMouseRightButtonPressedInArea( buttonOk.area() ) ) {
                fheroes2::showStandardTextMessage( _( "Okay" ), _( "Exit this menu." ), 0 );
            }
        }

        return SelectedWindow::Exit;
    }

    void cyclePostProcessing( Settings & conf )
    {
        // Off -> Bloom -> Vignette -> CRT -> Off.
        if ( !conf.isFxPostProcessingEnabled() ) {
            conf.setFxPostProcessing( true );
            conf.setPostProcessPreset( PostProcessPreset::BLOOM );
            return;
        }

        const int next = static_cast<int>( conf.getPostProcessPreset() ) + 1;
        if ( next >= postProcessPresetCount ) {
            conf.setFxPostProcessing( false );
            conf.setPostProcessPreset( PostProcessPreset::NONE );
        }
        else {
            conf.setPostProcessPreset( static_cast<PostProcessPreset>( next ) );
        }
    }
}

namespace fheroes2
{
    bool openVisualEffectsDialog( const std::function<void()> & updateUI )
    {
        const CursorRestorer cursorRestorer( true, ::Cursor::POINTER );

        Settings & conf = Settings::Get();

        bool saveConfiguration = false;

        SelectedWindow windowType = SelectedWindow::Configuration;
        while ( windowType != SelectedWindow::Exit ) {
            switch ( windowType ) {
            case SelectedWindow::Configuration:
                windowType = showConfigurationWindow();
                break;
            case SelectedWindow::DayNight:
                conf.setFxDayNight( !conf.isFxDayNightEnabled() );
                saveConfiguration = true;
                windowType = SelectedWindow::Configuration;
                break;
            case SelectedWindow::SeasonTint:
                conf.setFxSeasonTint( !conf.isFxSeasonTintEnabled() );
                saveConfiguration = true;
                windowType = SelectedWindow::Configuration;
                break;
            case SelectedWindow::Weather:
                conf.setFxWeather( !conf.isFxWeatherEnabled() );
                saveConfiguration = true;
                windowType = SelectedWindow::Configuration;
                break;
            case SelectedWindow::EnhancedShadows:
                conf.setFxEnhancedShadows( !conf.isFxEnhancedShadowsEnabled() );
                Assets::resetSoftShadowImages();
                saveConfiguration = true;
                windowType = SelectedWindow::Configuration;
                break;
            case SelectedWindow::UiSkin:
                conf.setFxModernUi( !conf.isFxModernUiEnabled() );
                saveConfiguration = true;
                windowType = SelectedWindow::Configuration;
                break;
            case SelectedWindow::PostProcessing:
                if ( isPostProcessingAvailable() ) {
                    cyclePostProcessing( conf );
                    saveConfiguration = true;
                }
                windowType = SelectedWindow::Configuration;
                break;
            default:
                windowType = SelectedWindow::Exit;
                break;
            }
        }

        if ( saveConfiguration ) {
            // Make every change visible on the screen below right away.
            updateUI();
        }

        return saveConfiguration;
    }
}
