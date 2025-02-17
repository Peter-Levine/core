/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <config_features.h>

#include <config_folders.h>
#include <rtl/bootstrap.hxx>
#include <sal/log.hxx>
#include <oox/core/xmlfilterbase.hxx>
#include <oox/export/drawingml.hxx>
#include <oox/export/utils.hxx>
#include <oox/helper/propertyset.hxx>
#include <oox/drawingml/color.hxx>
#include <drawingml/fillproperties.hxx>
#include <drawingml/textparagraph.hxx>
#include <oox/token/namespaces.hxx>
#include <oox/token/properties.hxx>
#include <oox/token/relationship.hxx>
#include <oox/token/tokens.hxx>
#include <oox/drawingml/drawingmltypes.hxx>
#include <svtools/unitconv.hxx>
#include <sax/fastattribs.hxx>
#include <tools/diagnose_ex.h>
#include <i18nlangtag/languagetag.hxx>

#include <cstdio>
#include <com/sun/star/awt/CharSet.hpp>
#include <com/sun/star/awt/FontDescriptor.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/FontStrikeout.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/awt/FontUnderline.hpp>
#include <com/sun/star/awt/Gradient.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertyState.hpp>
#include <com/sun/star/beans/Property.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/document/XStorageBasedDocument.hpp>
#include <com/sun/star/drawing/BitmapMode.hpp>
#include <com/sun/star/drawing/EnhancedCustomShapeAdjustmentValue.hpp>
#include <com/sun/star/drawing/EnhancedCustomShapeParameterType.hpp>
#include <com/sun/star/drawing/EnhancedCustomShapeParameterPair.hpp>
#include <com/sun/star/drawing/EnhancedCustomShapeSegment.hpp>
#include <com/sun/star/drawing/EnhancedCustomShapeSegmentCommand.hpp>
#include <com/sun/star/drawing/Hatch.hpp>
#include <com/sun/star/drawing/LineDash.hpp>
#include <com/sun/star/drawing/LineJoint.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/TextFitToSizeType.hpp>
#include <com/sun/star/drawing/TextHorizontalAdjust.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/i18n/ScriptType.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/style/LineSpacing.hpp>
#include <com/sun/star/style/LineSpacingMode.hpp>
#include <com/sun/star/text/WritingMode.hpp>
#include <com/sun/star/text/WritingMode2.hpp>
#include <com/sun/star/text/GraphicCrop.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/text/XTextContent.hpp>
#include <com/sun/star/text/XTextField.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/style/CaseMap.hpp>
#include <com/sun/star/xml/dom/XNodeList.hpp>
#include <com/sun/star/xml/sax/Writer.hpp>
#include <com/sun/star/xml/sax/XSAXSerializable.hpp>

#include <comphelper/processfactory.hxx>
#include <comphelper/random.hxx>
#include <comphelper/seqstream.hxx>
#include <comphelper/storagehelper.hxx>
#include <comphelper/xmltools.hxx>
#include <o3tl/any.hxx>
#include <tools/stream.hxx>
#include <unotools/fontdefs.hxx>
#include <vcl/cvtgrf.hxx>
#include <vcl/graph.hxx>
#include <vcl/settings.hxx>
#include <vcl/GraphicObject.hxx>
#include <vcl/svapp.hxx>
#include <rtl/strbuf.hxx>
#include <sfx2/app.hxx>
#include <svl/languageoptions.hxx>
#include <filter/msfilter/escherex.hxx>
#include <filter/msfilter/util.hxx>
#include <editeng/outlobj.hxx>
#include <editeng/svxenum.hxx>
#include <editeng/unonames.hxx>
#include <editeng/unoprnms.hxx>
#include <editeng/flditem.hxx>
#include <svx/sdtfsitm.hxx>
#include <svx/svdoashp.hxx>
#include <svx/svdomedia.hxx>
#include <svx/unoapi.hxx>
#include <svx/unoshape.hxx>
#include <svx/EnhancedCustomShape2d.hxx>

using namespace ::css;
using namespace ::css::beans;
using namespace ::css::drawing;
using namespace ::css::i18n;
using namespace ::css::style;
using namespace ::css::text;
using namespace ::css::uno;
using namespace ::css::container;

using ::css::io::XOutputStream;
using ::sax_fastparser::FSHelperPtr;
using ::sax_fastparser::FastSerializerHelper;

namespace
{
/// Extracts start or end alpha information from a transparency gradient.
sal_Int32 GetAlphaFromTransparenceGradient(const awt::Gradient& rGradient, bool bStart)
{
    // Our alpha is a gray color value.
    sal_uInt8 nRed = ::Color(bStart ? rGradient.StartColor : rGradient.EndColor).GetRed();
    // drawingML alpha is a percentage on a 0..100000 scale.
    return (255 - nRed) * oox::drawingml::MAX_PERCENT / 255;
}
}

namespace oox {
namespace drawingml {

URLTransformer::~URLTransformer()
{
}

OUString URLTransformer::getTransformedString(const OUString& rString) const
{
    return rString;
}

bool URLTransformer::isExternalURL(const OUString& /*rURL*/) const
{
    return true;
}

static css::uno::Any getLineDash( const css::uno::Reference<css::frame::XModel>& xModel, const OUString& rDashName )
    {
        css::uno::Reference<css::lang::XMultiServiceFactory> xFact(xModel, css::uno::UNO_QUERY);
        css::uno::Reference<css::container::XNameAccess> xNameAccess(
            xFact->createInstance("com.sun.star.drawing.DashTable"),
            css::uno::UNO_QUERY );
        if(xNameAccess.is())
        {
            if (!xNameAccess->hasByName(rDashName))
                return css::uno::Any();

            return xNameAccess->getByName(rDashName);
        }

        return css::uno::Any();
    }

namespace
{
void WriteRadialGradientPath(const awt::Gradient& rGradient, const FSHelperPtr& pFS)
{
    pFS->startElementNS(XML_a, XML_path, XML_path, "circle");

    // Write the focus rectangle. Work with the focus point, and assume
    // that it extends 50% in all directions.  The below
    // left/top/right/bottom values are percentages, where 0 means the
    // edge of the tile rectangle and 100% means the center of it.
    rtl::Reference<sax_fastparser::FastAttributeList> pAttributeList(
        sax_fastparser::FastSerializerHelper::createAttrList());
    sal_Int32 nLeftPercent = rGradient.XOffset * 2 - 50;
    pAttributeList->add(XML_l, OString::number(nLeftPercent * PER_PERCENT));
    sal_Int32 nTopPercent = rGradient.YOffset * 2 - 50;
    pAttributeList->add(XML_t, OString::number(nTopPercent * PER_PERCENT));
    sal_Int32 nRightPercent = (100 - rGradient.XOffset) * 2 - 50;
    pAttributeList->add(XML_r, OString::number(nRightPercent * PER_PERCENT));
    sal_Int32 nBottomPercent = (100 - rGradient.YOffset) * 2 - 50;
    pAttributeList->add(XML_b, OString::number(nBottomPercent * PER_PERCENT));
    sax_fastparser::XFastAttributeListRef xAttributeList(pAttributeList.get());
    pFS->singleElementNS(XML_a, XML_fillToRect, xAttributeList);

    pFS->endElementNS(XML_a, XML_path);
}
}

// not thread safe
int DrawingML::mnImageCounter = 1;
int DrawingML::mnWdpImageCounter = 1;
std::map<OUString, OUString> DrawingML::maWdpCache;

void DrawingML::ResetCounters()
{
    mnImageCounter = 1;
    mnWdpImageCounter = 1;
    maWdpCache.clear();
}

bool DrawingML::GetProperty( const Reference< XPropertySet >& rXPropertySet, const OUString& aName )
{
    try
    {
        mAny = rXPropertySet->getPropertyValue(aName);
        if (mAny.hasValue())
            return true;
    }
    catch( const Exception& )
    {
        /* printf ("exception when trying to get value of property: %s\n", aName.toUtf8()); */
    }
    return false;
}

bool DrawingML::GetPropertyAndState( const Reference< XPropertySet >& rXPropertySet, const Reference< XPropertyState >& rXPropertyState, const OUString& aName, PropertyState& eState )
{
    try
    {
        mAny = rXPropertySet->getPropertyValue(aName);
        if (mAny.hasValue())
        {
            eState = rXPropertyState->getPropertyState(aName);
            return true;
        }
    }
    catch( const Exception& )
    {
        /* printf ("exception when trying to get value of property: %s\n", aName.toUtf8()); */
    }
    return false;
}

void DrawingML::WriteColor( ::Color nColor, sal_Int32 nAlpha )
{
    // Transparency is a separate element.
    OString sColor = OString::number(  sal_uInt32(nColor) & 0x00FFFFFF, 16 );
    if( sColor.getLength() < 6 )
    {
        OStringBuffer sBuf( "0" );
        int remains = 5 - sColor.getLength();

        while( remains > 0 )
        {
            sBuf.append( "0" );
            remains--;
        }

        sBuf.append( sColor );

        sColor = sBuf.getStr();
    }
    if( nAlpha < MAX_PERCENT )
    {
        mpFS->startElementNS(XML_a, XML_srgbClr, XML_val, sColor);
        mpFS->singleElementNS(XML_a, XML_alpha, XML_val, OString::number(nAlpha));
        mpFS->endElementNS( XML_a, XML_srgbClr );

    }
    else
    {
        mpFS->singleElementNS(XML_a, XML_srgbClr, XML_val, sColor);
    }
}

void DrawingML::WriteColor( const OUString& sColorSchemeName, const Sequence< PropertyValue >& aTransformations, sal_Int32 nAlpha )
{
    // prevent writing a tag with empty val attribute
    if( sColorSchemeName.isEmpty() )
        return;

    if( aTransformations.hasElements() )
    {
        mpFS->startElementNS(XML_a, XML_schemeClr, XML_val, sColorSchemeName.toUtf8());
        WriteColorTransformations( aTransformations, nAlpha );
        mpFS->endElementNS( XML_a, XML_schemeClr );
    }
    else if(nAlpha < MAX_PERCENT)
    {
        mpFS->startElementNS(XML_a, XML_schemeClr, XML_val, sColorSchemeName.toUtf8());
        mpFS->singleElementNS(XML_a, XML_alpha, XML_val, OString::number(nAlpha));
        mpFS->endElementNS( XML_a, XML_schemeClr );
    }
    else
    {
        mpFS->singleElementNS(XML_a, XML_schemeClr, XML_val, sColorSchemeName.toUtf8());
    }
}

void DrawingML::WriteColorTransformations( const Sequence< PropertyValue >& aTransformations, sal_Int32 nAlpha )
{
    for( sal_Int32 i = 0; i < aTransformations.getLength(); i++ )
    {
        sal_Int32 nToken = Color::getColorTransformationToken( aTransformations[i].Name );
        if( nToken != XML_TOKEN_INVALID && aTransformations[i].Value.hasValue() )
        {
            if(nToken == XML_alpha && nAlpha < MAX_PERCENT)
            {
                mpFS->singleElementNS(XML_a, nToken, XML_val, OString::number(nAlpha));
            }
            else
            {
                sal_Int32 nValue = aTransformations[i].Value.get<sal_Int32>();
                mpFS->singleElementNS(XML_a, nToken, XML_val, OString::number(nValue));
            }
        }
    }
}

void DrawingML::WriteSolidFill( ::Color nColor, sal_Int32 nAlpha )
{
    mpFS->startElementNS(XML_a, XML_solidFill);
    WriteColor( nColor, nAlpha );
    mpFS->endElementNS( XML_a, XML_solidFill );
}

void DrawingML::WriteSolidFill( const OUString& sSchemeName, const Sequence< PropertyValue >& aTransformations, sal_Int32 nAlpha )
{
    mpFS->startElementNS(XML_a, XML_solidFill);
    WriteColor( sSchemeName, aTransformations, nAlpha );
    mpFS->endElementNS( XML_a, XML_solidFill );
}

void DrawingML::WriteSolidFill( const Reference< XPropertySet >& rXPropSet )
{
    // get fill color
    if ( !GetProperty( rXPropSet, "FillColor" ) )
        return;
    sal_uInt32 nFillColor = mAny.get<sal_uInt32>();

    // get InteropGrabBag and search the relevant attributes
    OUString sColorFillScheme;
    sal_uInt32 nOriginalColor = 0;
    Sequence< PropertyValue > aStyleProperties, aTransformations;
    if ( GetProperty( rXPropSet, "InteropGrabBag" ) )
    {
        Sequence< PropertyValue > aGrabBag;
        mAny >>= aGrabBag;
        for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i )
        {
            if( aGrabBag[i].Name == "SpPrSolidFillSchemeClr" )
                aGrabBag[i].Value >>= sColorFillScheme;
            else if( aGrabBag[i].Name == "OriginalSolidFillClr" )
                aGrabBag[i].Value >>= nOriginalColor;
            else if( aGrabBag[i].Name == "StyleFillRef" )
                aGrabBag[i].Value >>= aStyleProperties;
            else if( aGrabBag[i].Name == "SpPrSolidFillSchemeClrTransformations" )
                aGrabBag[i].Value >>= aTransformations;
        }
    }

    sal_Int32 nAlpha = MAX_PERCENT;
    if( GetProperty( rXPropSet, "FillTransparence" ) )
    {
        sal_Int32 nTransparency = 0;
        mAny >>= nTransparency;
        // Calculate alpha value (see oox/source/drawingml/color.cxx : getTransparency())
        nAlpha = (MAX_PERCENT - ( PER_PERCENT * nTransparency ) );
    }

    // write XML
    if ( nFillColor != nOriginalColor )
    {
        // the user has set a different color for the shape
        WriteSolidFill( ::Color(nFillColor & 0xffffff), nAlpha );
    }
    else if ( !sColorFillScheme.isEmpty() )
    {
        // the shape had a scheme color and the user didn't change it
        WriteSolidFill( sColorFillScheme, aTransformations, nAlpha );
    }
    else if ( aStyleProperties.hasElements() )
    {
        sal_uInt32 nThemeColor = 0;
        sal_Int32 nThemeAlpha = MAX_PERCENT;
        for( sal_Int32 i=0; i < aStyleProperties.getLength(); ++i )
        {
            if( aStyleProperties[i].Name == "Color" )
            {
                aStyleProperties[i].Value >>= nThemeColor;
            }
            else if(aStyleProperties[i].Name == "Transformations" )
            {
                Sequence< PropertyValue > aStyleTransformations;
                aStyleProperties[i].Value >>= aStyleTransformations;
                for( sal_Int32 j = 0; j < aStyleTransformations.getLength(); j++ )
                {
                    if (aStyleTransformations[j].Name == "alpha" )
                    {
                        aStyleTransformations[j].Value >>= nThemeAlpha;
                        break;
                    }
                }
            }
        }
        if ( nFillColor != nThemeColor || nAlpha != nThemeAlpha )
            // the shape contains a theme but it wasn't being used
            WriteSolidFill( ::Color(nFillColor & 0xffffff), nAlpha );

        // in case the shape used the style color and the user didn't change it,
        // we must not write a <a: solidFill> tag.
    }
    else
    {
        // the shape had a custom color and the user didn't change it
        WriteSolidFill( ::Color(nFillColor & 0xffffff), nAlpha );
    }
}

void DrawingML::WriteGradientStop(sal_uInt16 nStop, ::Color nColor, sal_Int32 nAlpha)
{
    mpFS->startElementNS(XML_a, XML_gs, XML_pos, OString::number(nStop * 1000));
    WriteColor(nColor, nAlpha);
    mpFS->endElementNS( XML_a, XML_gs );
}

::Color DrawingML::ColorWithIntensity( sal_uInt32 nColor, sal_uInt32 nIntensity )
{
    return ::Color(( ( ( nColor & 0xff ) * nIntensity ) / 100 )
        | ( ( ( ( ( nColor & 0xff00 ) >> 8 ) * nIntensity ) / 100 ) << 8 )
        | ( ( ( ( ( nColor & 0xff0000 ) >> 8 ) * nIntensity ) / 100 ) << 8 ));
}

bool DrawingML::EqualGradients( awt::Gradient aGradient1, awt::Gradient aGradient2 )
{
    return aGradient1.Style == aGradient2.Style &&
            aGradient1.StartColor == aGradient2.StartColor &&
            aGradient1.EndColor == aGradient2.EndColor &&
            aGradient1.Angle == aGradient2.Angle &&
            aGradient1.Border == aGradient2.Border &&
            aGradient1.XOffset == aGradient2.XOffset &&
            aGradient1.YOffset == aGradient2.YOffset &&
            aGradient1.StartIntensity == aGradient2.StartIntensity &&
            aGradient1.EndIntensity == aGradient2.EndIntensity &&
            aGradient1.StepCount == aGradient2.StepCount;
}

void DrawingML::WriteGradientFill( const Reference< XPropertySet >& rXPropSet )
{
    awt::Gradient aGradient;
    if (GetProperty(rXPropSet, "FillGradient"))
    {
        aGradient = *o3tl::doAccess<awt::Gradient>(mAny);

        // get InteropGrabBag and search the relevant attributes
        awt::Gradient aOriginalGradient;
        Sequence< PropertyValue > aGradientStops;
        if ( GetProperty( rXPropSet, "InteropGrabBag" ) )
        {
            Sequence< PropertyValue > aGrabBag;
            mAny >>= aGrabBag;
            for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i )
                if( aGrabBag[i].Name == "GradFillDefinition" )
                    aGrabBag[i].Value >>= aGradientStops;
                else if( aGrabBag[i].Name == "OriginalGradFill" )
                    aGrabBag[i].Value >>= aOriginalGradient;
        }

        // check if an ooxml gradient had been imported and if the user has modified it
        // Gradient grab-bag depends on theme grab-bag, which is implemented
        // only for DOCX.
        if( EqualGradients( aOriginalGradient, aGradient ) && GetDocumentType() == DOCUMENT_DOCX)
        {
            // If we have no gradient stops that means original gradient were defined by a theme.
            if( aGradientStops.hasElements() )
            {
                mpFS->startElementNS(XML_a, XML_gradFill, XML_rotWithShape, "0");
                WriteGrabBagGradientFill(aGradientStops, aGradient);
                mpFS->endElementNS( XML_a, XML_gradFill );
            }
        }
        else
        {
            mpFS->startElementNS(XML_a, XML_gradFill, XML_rotWithShape, "0");
            WriteGradientFill(aGradient, rXPropSet);
            mpFS->endElementNS( XML_a, XML_gradFill );
        }
    }
}

void DrawingML::WriteGrabBagGradientFill( const Sequence< PropertyValue >& aGradientStops, awt::Gradient rGradient )
{
    // write back the original gradient
    mpFS->startElementNS(XML_a, XML_gsLst);

    // get original stops and write them
    for( sal_Int32 i=0; i < aGradientStops.getLength(); ++i )
    {
        Sequence< PropertyValue > aGradientStop;
        aGradientStops[i].Value >>= aGradientStop;

        // get values
        OUString sSchemeClr;
        double nPos = 0;
        sal_Int16 nTransparency = 0;
        ::Color nRgbClr;
        Sequence< PropertyValue > aTransformations;
        for( sal_Int32 j=0; j < aGradientStop.getLength(); ++j )
        {
            if( aGradientStop[j].Name == "SchemeClr" )
                aGradientStop[j].Value >>= sSchemeClr;
            else if( aGradientStop[j].Name == "RgbClr" )
                aGradientStop[j].Value >>= nRgbClr;
            else if( aGradientStop[j].Name == "Pos" )
                aGradientStop[j].Value >>= nPos;
            else if( aGradientStop[j].Name == "Transparency" )
                aGradientStop[j].Value >>= nTransparency;
            else if( aGradientStop[j].Name == "Transformations" )
                aGradientStop[j].Value >>= aTransformations;
        }
        // write stop
        mpFS->startElementNS(XML_a, XML_gs, XML_pos, OString::number(nPos * 100000.0).getStr());
        if( sSchemeClr.isEmpty() )
        {
            // Calculate alpha value (see oox/source/drawingml/color.cxx : getTransparency())
            sal_Int32 nAlpha = MAX_PERCENT - ( PER_PERCENT * nTransparency );
            WriteColor( nRgbClr, nAlpha );
        }
        else
        {
            WriteColor( sSchemeClr, aTransformations );
        }
        mpFS->endElementNS( XML_a, XML_gs );
    }
    mpFS->endElementNS( XML_a, XML_gsLst );

    switch (rGradient.Style)
    {
        default:
            mpFS->singleElementNS(
                XML_a, XML_lin, XML_ang,
                OString::number((((3600 - rGradient.Angle + 900) * 6000) % 21600000)));
            break;
        case awt::GradientStyle_RADIAL:
            WriteRadialGradientPath(rGradient, mpFS);
            break;
    }
}

void DrawingML::WriteGradientFill(awt::Gradient rGradient,
                                  const uno::Reference<beans::XPropertySet>& rXPropSet)
{
    switch( rGradient.Style )
    {
        default:
        case awt::GradientStyle_LINEAR:
        {
            awt::Gradient aTransparenceGradient;
            bool bTransparent = false;
            if (rXPropSet.is() && GetProperty(rXPropSet, "FillTransparenceGradient"))
            {
                aTransparenceGradient = *o3tl::doAccess<awt::Gradient>(mAny);
                bTransparent = true;
            }

            mpFS->startElementNS(XML_a, XML_gsLst);
            sal_Int32 nStartAlpha = MAX_PERCENT;
            sal_Int32 nEndAlpha = MAX_PERCENT;
            if (bTransparent)
            {
                nStartAlpha = GetAlphaFromTransparenceGradient(aTransparenceGradient, true);
                nEndAlpha = GetAlphaFromTransparenceGradient(aTransparenceGradient, false);
            }
            WriteGradientStop(0, ColorWithIntensity(rGradient.StartColor, rGradient.StartIntensity),
                              nStartAlpha);
            WriteGradientStop(100, ColorWithIntensity(rGradient.EndColor, rGradient.EndIntensity),
                              nEndAlpha);
            mpFS->endElementNS( XML_a, XML_gsLst );
            mpFS->singleElementNS(
                XML_a, XML_lin, XML_ang,
                OString::number((((3600 - rGradient.Angle + 900) * 6000) % 21600000)));
            break;
        }

        case awt::GradientStyle_AXIAL:
            mpFS->startElementNS(XML_a, XML_gsLst);
            WriteGradientStop( 0, ColorWithIntensity( rGradient.EndColor, rGradient.EndIntensity ) );
            WriteGradientStop( 50, ColorWithIntensity( rGradient.StartColor, rGradient.StartIntensity ) );
            WriteGradientStop( 100, ColorWithIntensity( rGradient.EndColor, rGradient.EndIntensity ) );
            mpFS->endElementNS( XML_a, XML_gsLst );
            mpFS->singleElementNS(
                XML_a, XML_lin, XML_ang,
                OString::number((((3600 - rGradient.Angle + 900) * 6000) % 21600000)));
            break;

        case awt::GradientStyle_RADIAL:
        {
            mpFS->startElementNS(XML_a, XML_gsLst);
            WriteGradientStop(0, ColorWithIntensity(rGradient.EndColor, rGradient.EndIntensity));
            if (rGradient.Border > 0 && rGradient.Border < 100)
                // Map border to an additional gradient stop, which has the
                // same color as the final stop.
                WriteGradientStop(
                    100 - rGradient.Border,
                    ColorWithIntensity(rGradient.StartColor, rGradient.StartIntensity));
            WriteGradientStop(100,
                              ColorWithIntensity(rGradient.StartColor, rGradient.StartIntensity));
            mpFS->endElementNS(XML_a, XML_gsLst);

            WriteRadialGradientPath(rGradient, mpFS);
            break;
        }
            /* I don't see how to apply transformation to gradients, so
             * elliptical will end as radial and square as
             * rectangular. also position offsets are not applied */
        case awt::GradientStyle_ELLIPTICAL:
        case awt::GradientStyle_RECT:
        case awt::GradientStyle_SQUARE:
            mpFS->startElementNS(XML_a, XML_gsLst);
            WriteGradientStop( 0, ColorWithIntensity( rGradient.EndColor, rGradient.EndIntensity ) );
            WriteGradientStop( 100, ColorWithIntensity( rGradient.StartColor, rGradient.StartIntensity ) );
            mpFS->endElementNS( XML_a, XML_gsLst );
            mpFS->singleElementNS( XML_a, XML_path,
                    XML_path, ( rGradient.Style == awt::GradientStyle_RADIAL || rGradient.Style == awt::GradientStyle_ELLIPTICAL ) ? "circle" : "rect" );
            break;
    }
}

void DrawingML::WriteLineArrow( const Reference< XPropertySet >& rXPropSet, bool bLineStart )
{
    ESCHER_LineEnd eLineEnd;
    sal_Int32 nArrowLength;
    sal_Int32 nArrowWidth;

    if ( EscherPropertyContainer::GetLineArrow( bLineStart, rXPropSet, eLineEnd, nArrowLength, nArrowWidth ) )
    {
        const char* len;
        const char* type;
        const char* width;

        switch( nArrowLength )
        {
            case ESCHER_LineShortArrow:
                len = "sm";
                break;
            default:
            case ESCHER_LineMediumLenArrow:
                len = "med";
                break;
            case ESCHER_LineLongArrow:
                len = "lg";
                break;
        }

        switch( eLineEnd )
        {
            default:
            case ESCHER_LineNoEnd:
                type = "none";
                break;
            case ESCHER_LineArrowEnd:
                type = "triangle";
                break;
            case ESCHER_LineArrowStealthEnd:
                type = "stealth";
                break;
            case ESCHER_LineArrowDiamondEnd:
                type = "diamond";
                break;
            case ESCHER_LineArrowOvalEnd:
                type = "oval";
                break;
            case ESCHER_LineArrowOpenEnd:
                type = "arrow";
                break;
        }

        switch( nArrowWidth )
        {
            case ESCHER_LineNarrowArrow:
                width = "sm";
                break;
            default:
            case ESCHER_LineMediumWidthArrow:
                width = "med";
                break;
            case ESCHER_LineWideArrow:
                width = "lg";
                break;
        }

        mpFS->singleElementNS( XML_a, bLineStart ? XML_headEnd : XML_tailEnd,
                               XML_len, len,
                               XML_type, type,
                               XML_w, width );
    }
}

void DrawingML::WriteOutline( const Reference<XPropertySet>& rXPropSet, Reference< frame::XModel > const & xModel )
{
    drawing::LineStyle aLineStyle( drawing::LineStyle_NONE );

    if (GetProperty(rXPropSet, "LineStyle"))
        mAny >>= aLineStyle;

    sal_uInt32 nLineWidth = 0;
    ::Color nColor;
    sal_Int32 nColorAlpha = MAX_PERCENT;
    bool bColorSet = false;
    const char* cap = nullptr;
    drawing::LineDash aLineDash;
    bool bDashSet = false;
    bool bNoFill = false;

    // get InteropGrabBag and search the relevant attributes
    OUString sColorFillScheme;

    ::Color nOriginalColor;
    ::Color nStyleColor;
    sal_uInt32 nStyleLineWidth = 0;

    Sequence<PropertyValue> aStyleProperties;
    Sequence<PropertyValue> aTransformations;

    drawing::LineStyle aStyleLineStyle(drawing::LineStyle_NONE);
    drawing::LineJoint aStyleLineJoint(drawing::LineJoint_NONE);

    if (GetProperty(rXPropSet, "InteropGrabBag"))
    {
        Sequence<PropertyValue> aGrabBag;
        mAny >>= aGrabBag;

        for (sal_Int32 i=0; i < aGrabBag.getLength(); ++i)
        {
            if( aGrabBag[i].Name == "SpPrLnSolidFillSchemeClr" )
                aGrabBag[i].Value >>= sColorFillScheme;
            else if( aGrabBag[i].Name == "OriginalLnSolidFillClr" )
                aGrabBag[i].Value >>= nOriginalColor;
            else if( aGrabBag[i].Name == "StyleLnRef" )
                aGrabBag[i].Value >>= aStyleProperties;
            else if( aGrabBag[i].Name == "SpPrLnSolidFillSchemeClrTransformations" )
                aGrabBag[i].Value >>= aTransformations;
        }
        if (aStyleProperties.hasElements())
        {
            for (sal_Int32 i=0; i < aStyleProperties.getLength(); ++i)
            {
                if( aStyleProperties[i].Name == "Color" )
                    aStyleProperties[i].Value >>= nStyleColor;
                else if( aStyleProperties[i].Name == "LineStyle" )
                    aStyleProperties[i].Value >>= aStyleLineStyle;
                else if( aStyleProperties[i].Name == "LineJoint" )
                    aStyleProperties[i].Value >>= aStyleLineJoint;
                else if( aStyleProperties[i].Name == "LineWidth" )
                    aStyleProperties[i].Value >>= nStyleLineWidth;
            }
        }
    }

    if (GetProperty(rXPropSet, "LineWidth"))
        mAny >>= nLineWidth;

    switch (aLineStyle)
    {
        case drawing::LineStyle_NONE:
            bNoFill = true;
            break;
        case drawing::LineStyle_DASH:
            if (GetProperty(rXPropSet, "LineDash"))
            {
                aLineDash = mAny.get<drawing::LineDash>();
                //this query is good for shapes, but in the case of charts it returns 0 values
                if (aLineDash.Dots == 0 && aLineDash.DotLen == 0 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0 && aLineDash.Distance == 0) {
                    OUString aLineDashName;
                    if (GetProperty(rXPropSet, "LineDashName"))
                        mAny >>= aLineDashName;
                    if (!aLineDashName.isEmpty() && xModel) {
                        css::uno::Any aAny = getLineDash(xModel, aLineDashName);
                        aAny >>= aLineDash;
                    }
                }
            }
            else
            {
                //export the linestyle of chart wall (plot area) and chart page
                OUString aLineDashName;
                if (GetProperty(rXPropSet, "LineDashName"))
                    mAny >>= aLineDashName;
                if (!aLineDashName.isEmpty() && xModel) {
                    css::uno::Any aAny = getLineDash(xModel, aLineDashName);
                    aAny >>= aLineDash;
                }
            }
            bDashSet = true;
            if (aLineDash.Style == DashStyle_ROUND || aLineDash.Style == DashStyle_ROUNDRELATIVE)
            {
                cap = "rnd";
            }

            SAL_INFO("oox.shape", "dash dots: " << aLineDash.Dots << " dashes: " << aLineDash.Dashes
                    << " dotlen: " << aLineDash.DotLen << " dashlen: " << aLineDash.DashLen << " distance: " <<  aLineDash.Distance);

            [[fallthrough]];
        case drawing::LineStyle_SOLID:
        default:
            if (GetProperty(rXPropSet, "LineColor"))
            {
                nColor = ::Color(mAny.get<sal_uInt32>() & 0xffffff);
                bColorSet = true;
            }
            if (GetProperty(rXPropSet, "LineTransparence"))
            {
                nColorAlpha = MAX_PERCENT - (mAny.get<sal_Int16>() * PER_PERCENT);
            }
            if (GetProperty(rXPropSet, "LineCap"))
            {
                const LineCap aLineCap = mAny.get<drawing::LineCap>();
                if (aLineCap == LineCap_ROUND)
                    cap = "rnd";
                else if (aLineCap == LineCap_SQUARE)
                    cap = "sq";
            }
            break;
    }

    mpFS->startElementNS( XML_a, XML_ln,
                          XML_cap, cap,
                          XML_w, nLineWidth > 1 && nStyleLineWidth != nLineWidth ?
                              OString::number(oox::drawingml::convertHmmToEmu(nLineWidth)).getStr() : nullptr );

    if( bColorSet )
    {
        if( nColor != nOriginalColor )
        {
            // the user has set a different color for the line
            WriteSolidFill( nColor, nColorAlpha );
        }
        else if( !sColorFillScheme.isEmpty() )
        {
            // the line had a scheme color and the user didn't change it
            WriteSolidFill( sColorFillScheme, aTransformations );
        }
        else if( aStyleProperties.hasElements() )
        {
            if( nColor != nStyleColor )
                // the line style defines some color but it wasn't being used
                WriteSolidFill( nColor );
            // in case the shape used the style color and the user didn't change it,
            // we must not write a <a: solidFill> tag.
        }
        else
        {
            WriteSolidFill( nColor, nColorAlpha );
        }
    }

    if( bDashSet && aStyleLineStyle != drawing::LineStyle_DASH )
    {
        // convert absolute dash/dot length to relative length
        int relDotLen = nLineWidth ? aLineDash.DotLen / nLineWidth : 0;
        int relDashLen = nLineWidth ? aLineDash.DashLen / nLineWidth : 0;
        int relDistance = nLineWidth ? aLineDash.Distance / nLineWidth : 0;
        // fixing relative values in the case of linewidths smaller than 1 pt
        if (0 < nLineWidth && nLineWidth < 35) //35 HMM == 1 pt
        {
            relDotLen = relDotLen ? (relDotLen + 1) * (nLineWidth * 360.0 / 12700) : 0;
            relDashLen = relDashLen ? (relDashLen + 1) * (nLineWidth * 360.0 / 12700) : 0;
            relDistance = relDistance ? (relDistance + 1) * (nLineWidth * 360.0 / 12700) : 0;
        }
        // keep default mso preset linestyles (instead of custdash)
        if (aLineDash.Dots == 1 && relDotLen == 1 && aLineDash.Dashes == 0 && relDashLen == 0 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "dot");
        }
        else if (aLineDash.Dots == 0 && relDotLen == 0 && aLineDash.Dashes == 1 && relDashLen == 4 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "dash");
        }
        else if (aLineDash.Dots == 1 && relDotLen == 1 && aLineDash.Dashes == 1 && relDashLen == 4 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "dashDot");
        }
        else if (aLineDash.Dots == 0 && relDotLen == 0 && aLineDash.Dashes == 1 && relDashLen == 8 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "lgDash");
        }
        else if (aLineDash.Dots == 1 && relDotLen == 1 && aLineDash.Dashes == 1 && relDashLen == 8 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "lgDashDot");
        }
        else if (aLineDash.Dots == 2 && relDotLen == 1 && aLineDash.Dashes == 1 && relDashLen == 8 && relDistance == 3)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "lgDashDotDot");
        }
        else if (aLineDash.Dots == 1 && relDotLen == 1 && aLineDash.Dashes == 0 && relDashLen == 0 && relDistance == 1)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDot");
        }
        else if (aLineDash.Dots == 0 && relDotLen == 0 && aLineDash.Dashes == 1 && relDashLen == 3 && relDistance == 1)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDash");
        }
        else if (aLineDash.Dots == 1 && relDotLen == 1 && aLineDash.Dashes == 1 && relDashLen == 3 && relDistance == 1)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDashDot");
        }
        else if (aLineDash.Dots == 2 && relDotLen == 1 && aLineDash.Dashes == 1 && relDashLen == 3 && relDistance == 1)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDashDotDot");
        }
        /*convert some LO preset dashes to MSO preset dashes for oox interoperability
        LO preset dashes which don't have equivalent in MSO preset dashes: 2 Dots 3 Dashes, Line with Fine Dots, 3 Dashes 3 Dots*/
        //ultrafine Dashed, Ultrafine Dotted -> sysDot
        else if ((aLineDash.Dots == 1 && aLineDash.DotLen == 51 && aLineDash.Dashes == 1 && aLineDash.DashLen == 51 && aLineDash.Distance == 51) ||
                 (aLineDash.Dots == 1 && aLineDash.DotLen == 0 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0 && aLineDash.Distance == 50))
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDot");
        }
        //Fine Dashed -> dash
        else if (aLineDash.Dots == 1 && aLineDash.DotLen == 197 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0 && aLineDash.Distance == 197)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "dash");
        }
        //Fine Dotted -> dot
        else if (aLineDash.Dots == 1 && aLineDash.DotLen == 0 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0 && aLineDash.Distance == 457)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "dot");
        }
        //Line Style 9, Dashed -> sysDash
        else if ((aLineDash.Dots == 1 && aLineDash.DotLen == 197 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0 && aLineDash.Distance == 120) ||
                 (aLineDash.Dots == 1 && aLineDash.DotLen == 197 && aLineDash.Dashes == 0 && aLineDash.DashLen == 0  && aLineDash.Distance == 127))
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDash");
        }
        //2 Dots 1 Dash -> sysDashDotDot
        else if (aLineDash.Dots == 2 && aLineDash.DotLen == 0 && aLineDash.Dashes == 1 && aLineDash.DashLen == 203 && aLineDash.Distance == 203)
        {
            mpFS->singleElementNS(XML_a, XML_prstDash, XML_val, "sysDashDotDot");
        }
        else
        {
            mpFS->startElementNS(XML_a, XML_custDash);

            // Check that line-width is positive and distance between dashes\dots is positive
            if ( nLineWidth > 0 && aLineDash.Distance > 0 )
            {
                // Write 'dashes' first, and then 'dots'
                int i;
                sal_Int32 nSp = aLineDash.Distance * 100 / nLineWidth;
                if ( aLineDash.Dashes > 0 )
                {
                    sal_Int32 nD = aLineDash.DashLen * 100 / nLineWidth;
                    for( i = 0; i < aLineDash.Dashes; i ++ )
                    {
                        mpFS->singleElementNS( XML_a , XML_ds,
                                               XML_d , write1000thOfAPercent(nD),
                                               XML_sp, write1000thOfAPercent(nSp) );
                    }
                }
                if ( aLineDash.Dots > 0 )
                {
                    sal_Int32 nD = aLineDash.DotLen * 100 / nLineWidth;
                    for( i = 0; i < aLineDash.Dots; i ++ )
                    {
                        mpFS->singleElementNS( XML_a, XML_ds,
                                               XML_d , write1000thOfAPercent(nD),
                                               XML_sp, write1000thOfAPercent(nSp) );
                    }
                }
            }

            SAL_WARN_IF(nLineWidth <= 0,
                        "oox.shape", "while writing outline - custom dash - line width was < 0  : " << nLineWidth);
            SAL_WARN_IF(aLineDash.Dashes < 0,
                        "oox.shape", "while writing outline - custom dash - number of dashes was < 0  : " << aLineDash.Dashes);
            SAL_WARN_IF(aLineDash.Dashes > 0 && aLineDash.DashLen <= 0,
                        "oox.shape", "while writing outline - custom dash - dash length was < 0  : " << aLineDash.DashLen);
            SAL_WARN_IF(aLineDash.Dots < 0,
                        "oox.shape", "while writing outline - custom dash - number of dots was < 0  : " << aLineDash.Dots);
            SAL_WARN_IF(aLineDash.Dots > 0 && aLineDash.DotLen <= 0,
                        "oox.shape", "while writing outline - custom dash - dot length was < 0  : " << aLineDash.DotLen);
            SAL_WARN_IF(aLineDash.Distance <= 0,
                        "oox.shape", "while writing outline - custom dash - distance was < 0  : " << aLineDash.Distance);

            mpFS->endElementNS( XML_a, XML_custDash );
        }
    }

    if (!bNoFill && nLineWidth > 1 && GetProperty(rXPropSet, "LineJoint"))
    {
        LineJoint eLineJoint = mAny.get<LineJoint>();

        if( aStyleLineJoint == LineJoint_NONE || aStyleLineJoint != eLineJoint )
        {
            // style-defined line joint does not exist, or is different from the shape's joint
            switch( eLineJoint )
            {
                case LineJoint_NONE:
                case LineJoint_BEVEL:
                    mpFS->singleElementNS(XML_a, XML_bevel);
                    break;
                default:
                case LineJoint_MIDDLE:
                case LineJoint_MITER:
                    mpFS->singleElementNS(XML_a, XML_miter);
                    break;
                case LineJoint_ROUND:
                    mpFS->singleElementNS(XML_a, XML_round);
                    break;
            }
        }
    }

    if( !bNoFill )
    {
        WriteLineArrow( rXPropSet, true );
        WriteLineArrow( rXPropSet, false );
    }
    else
    {
        mpFS->singleElementNS(XML_a, XML_noFill);
    }

    mpFS->endElementNS( XML_a, XML_ln );
}

const char* DrawingML::GetComponentDir()
{
    switch ( meDocumentType )
    {
        case DOCUMENT_DOCX: return "word";
        case DOCUMENT_PPTX: return "ppt";
        case DOCUMENT_XLSX: return "xl";
    }

    return "unknown";
}

const char* DrawingML::GetRelationCompPrefix()
{
    switch ( meDocumentType )
    {
        case DOCUMENT_DOCX: return "";
        case DOCUMENT_PPTX:
        case DOCUMENT_XLSX: return "../";
    }

    return "unknown";
}

OUString DrawingML::WriteImage( const Graphic& rGraphic , bool bRelPathToMedia )
{
    GfxLink aLink = rGraphic.GetGfxLink ();
    OUString sMediaType;
    const char* pExtension = "";
    OUString sRelId;

    SvMemoryStream aStream;
    const void* aData = aLink.GetData();
    std::size_t nDataSize = aLink.GetDataSize();

    switch ( aLink.GetType() )
    {
        case GfxLinkType::NativeGif:
            sMediaType = "image/gif";
            pExtension = ".gif";
            break;

        // #i15508# added BMP type for better exports
        // export not yet active, so adding for reference (not checked)
        case GfxLinkType::NativeBmp:
            sMediaType = "image/bmp";
            pExtension = ".bmp";
            break;

        case GfxLinkType::NativeJpg:
            sMediaType = "image/jpeg";
            pExtension = ".jpeg";
            break;
        case GfxLinkType::NativePng:
            sMediaType = "image/png";
            pExtension = ".png";
            break;
        case GfxLinkType::NativeTif:
            sMediaType = "image/tiff";
            pExtension = ".tif";
            break;
        case GfxLinkType::NativeWmf:
            sMediaType = "image/x-wmf";
            pExtension = ".wmf";
            break;
        case GfxLinkType::NativeMet:
            sMediaType = "image/x-met";
            pExtension = ".met";
            break;
        case GfxLinkType::NativePct:
            sMediaType = "image/x-pict";
            pExtension = ".pct";
            break;
        case GfxLinkType::NativeMov:
            sMediaType = "application/movie";
            pExtension = ".MOV";
            break;
        default:
        {
            GraphicType aType = rGraphic.GetType();
            if ( aType == GraphicType::Bitmap || aType == GraphicType::GdiMetafile)
            {
                if ( aType == GraphicType::Bitmap )
                {
                    (void)GraphicConverter::Export( aStream, rGraphic, ConvertDataFormat::PNG );
                    sMediaType = "image/png";
                    pExtension = ".png";
                }
                else
                {
                    (void)GraphicConverter::Export( aStream, rGraphic, ConvertDataFormat::EMF );
                    sMediaType = "image/x-emf";
                    pExtension = ".emf";
                }
            }
            else
            {
                SAL_WARN("oox.shape", "unhandled graphic type " << static_cast<int>(aType) );
                /*Earlier, even in case of unhandled graphic types we were
                  proceeding to write the image, which would eventually
                  write an empty image with a zero size, and return a valid
                  relationID, which is incorrect.
                  */
                return sRelId;
            }

            aData = aStream.GetData();
            nDataSize = aStream.GetEndOfData();
            break;
        }
    }

    Reference< XOutputStream > xOutStream = mpFB->openFragmentStream( OUStringBuffer()
                                                                      .appendAscii( GetComponentDir() )
                                                                      .append( "/media/image" )
                                                                      .append( static_cast<sal_Int32>(mnImageCounter) )
                                                                      .appendAscii( pExtension )
                                                                      .makeStringAndClear(),
                                                                      sMediaType );
    xOutStream->writeBytes( Sequence< sal_Int8 >( static_cast<const sal_Int8*>(aData), nDataSize ) );
    xOutStream->closeOutput();

    const OString sRelPathToMedia = "media/image";
    OString sRelationCompPrefix;
    if ( bRelPathToMedia )
        sRelationCompPrefix = "../";
    else
        sRelationCompPrefix = GetRelationCompPrefix();
    sRelId = mpFB->addRelation( mpFS->getOutputStream(),
                                oox::getRelationship(Relationship::IMAGE),
                                OUStringBuffer()
                                .appendAscii( sRelationCompPrefix.getStr() )
                                .appendAscii( sRelPathToMedia.getStr() )
                                .append( static_cast<sal_Int32>(mnImageCounter ++) )
                                .appendAscii( pExtension )
                                .makeStringAndClear() );

    return sRelId;
}

void DrawingML::WriteMediaNonVisualProperties(const css::uno::Reference<css::drawing::XShape>& xShape)
{
    SdrMediaObj* pMediaObj = dynamic_cast<SdrMediaObj*>(GetSdrObjectFromXShape(xShape));
    if (!pMediaObj)
        return;

    // extension
    OUString aExtension;
    const OUString& rURL(pMediaObj->getURL());
    int nLastDot = rURL.lastIndexOf('.');
    if (nLastDot >= 0)
        aExtension = rURL.copy(nLastDot);

    bool bEmbed = rURL.startsWith("vnd.sun.star.Package:");
    Relationship eMediaType = Relationship::VIDEO;

    // mime type
#if HAVE_FEATURE_AVMEDIA
    OUString aMimeType(pMediaObj->getMediaProperties().getMimeType());
#else
    OUString aMimeType("none");
#endif
    if (aMimeType == "application/vnd.sun.star.media")
    {
        // try to set something better
        // TODO fix the importer to actually set the mimetype on import
        if (aExtension.equalsIgnoreAsciiCase(".avi"))
            aMimeType = "video/x-msvideo";
        else if (aExtension.equalsIgnoreAsciiCase(".flv"))
            aMimeType = "video/x-flv";
        else if (aExtension.equalsIgnoreAsciiCase(".mp4"))
            aMimeType = "video/mp4";
        else if (aExtension.equalsIgnoreAsciiCase(".mov"))
            aMimeType = "video/quicktime";
        else if (aExtension.equalsIgnoreAsciiCase(".ogv"))
            aMimeType = "video/ogg";
        else if (aExtension.equalsIgnoreAsciiCase(".wmv"))
            aMimeType = "video/x-ms-wmv";
        else if (aExtension.equalsIgnoreAsciiCase(".wav"))
        {
            aMimeType = "audio/x-wav";
            eMediaType = Relationship::AUDIO;
        }
    }

    OUString aVideoFileRelId;
    OUString aMediaRelId;

    if (bEmbed)
    {
        // copy the video stream
        Reference<XOutputStream> xOutStream = mpFB->openFragmentStream(OUStringBuffer()
                                                                       .appendAscii(GetComponentDir())
                                                                       .append("/media/media")
                                                                       .append(static_cast<sal_Int32>(mnImageCounter))
                                                                       .append(aExtension)
                                                                       .makeStringAndClear(),
                                                                       aMimeType);

        uno::Reference<io::XInputStream> xInputStream(pMediaObj->GetInputStream());
        comphelper::OStorageHelper::CopyInputToOutput(xInputStream, xOutStream);

        xOutStream->closeOutput();

        // create the relation
        OUString aPath = OUStringBuffer().appendAscii(GetRelationCompPrefix())
                                         .append("media/media")
                                         .append(static_cast<sal_Int32>(mnImageCounter++))
                                         .append(aExtension)
                                         .makeStringAndClear();
        aVideoFileRelId = mpFB->addRelation(mpFS->getOutputStream(), oox::getRelationship(eMediaType), aPath);
        aMediaRelId = mpFB->addRelation(mpFS->getOutputStream(), oox::getRelationship(Relationship::MEDIA), aPath);
    }
    else
    {
        aVideoFileRelId = mpFB->addRelation(mpFS->getOutputStream(), oox::getRelationship(eMediaType), rURL);
        aMediaRelId = mpFB->addRelation(mpFS->getOutputStream(), oox::getRelationship(Relationship::MEDIA), rURL);
    }

    GetFS()->startElementNS(XML_p, XML_nvPr);

    GetFS()->singleElementNS(XML_a, eMediaType == Relationship::VIDEO ? XML_videoFile : XML_audioFile,
                    FSNS(XML_r, XML_link), aVideoFileRelId.toUtf8());

    GetFS()->startElementNS(XML_p, XML_extLst);
    // media extensions; google this ID for details
    GetFS()->startElementNS(XML_p, XML_ext, XML_uri, "{DAA4B4D4-6D71-4841-9C94-3DE7FCFB9230}");

    GetFS()->singleElementNS(XML_p14, XML_media,
            bEmbed? FSNS(XML_r, XML_embed): FSNS(XML_r, XML_link), aMediaRelId.toUtf8());

    GetFS()->endElementNS(XML_p, XML_ext);
    GetFS()->endElementNS(XML_p, XML_extLst);

    GetFS()->endElementNS(XML_p, XML_nvPr);
}

void DrawingML::WriteImageBrightnessContrastTransparence(uno::Reference<beans::XPropertySet> const & rXPropSet)
{
    sal_Int16 nBright = 0;
    sal_Int32 nContrast = 0;
    sal_Int16 nTransparence = 0;

    if (GetProperty(rXPropSet, "AdjustLuminance"))
        nBright = mAny.get<sal_Int16>();
    if (GetProperty(rXPropSet, "AdjustContrast"))
        nContrast = mAny.get<sal_Int32>();
    if (GetProperty(rXPropSet, "Transparency"))
        nTransparence = mAny.get<sal_Int16>();


    if (nBright || nContrast)
    {
        mpFS->singleElementNS(XML_a, XML_lum,
                   XML_bright, nBright ? OString::number(nBright * 1000).getStr() : nullptr,
                   XML_contrast, nContrast ? OString::number(nContrast * 1000).getStr() : nullptr);
    }

    if (nTransparence)
    {
        sal_Int32 nAlphaMod = (100 - nTransparence ) * PER_PERCENT;
        mpFS->singleElementNS(XML_a, XML_alphaModFix, XML_amt, OString::number(nAlphaMod));
    }
}

OUString DrawingML::WriteXGraphicBlip(uno::Reference<beans::XPropertySet> const & rXPropSet,
                                      uno::Reference<graphic::XGraphic> const & rxGraphic,
                                      bool bRelPathToMedia)
{
    OUString sRelId;

    if (!rxGraphic.is())
        return sRelId;

    Graphic aGraphic(rxGraphic);
    if (mpTextExport)
    {
        BitmapChecksum nChecksum = aGraphic.GetChecksum();
        sRelId = mpTextExport->FindRelId(nChecksum);
    }
    if (sRelId.isEmpty())
    {
        sRelId = WriteImage(aGraphic, bRelPathToMedia);
        if (mpTextExport)
        {
            BitmapChecksum nChecksum = aGraphic.GetChecksum();
            mpTextExport->CacheRelId(nChecksum, sRelId);
        }
    }

    mpFS->startElementNS(XML_a, XML_blip, FSNS(XML_r, XML_embed), sRelId.toUtf8());

    WriteImageBrightnessContrastTransparence(rXPropSet);

    WriteArtisticEffect(rXPropSet);

    mpFS->endElementNS(XML_a, XML_blip);

    return sRelId;
}

void DrawingML::WriteXGraphicBlipMode(uno::Reference<beans::XPropertySet> const & rXPropSet,
                                      uno::Reference<graphic::XGraphic> const & rxGraphic)
{
    BitmapMode eBitmapMode(BitmapMode_NO_REPEAT);
    if (GetProperty(rXPropSet, "FillBitmapMode"))
        mAny >>= eBitmapMode;

    SAL_INFO("oox.shape", "fill bitmap mode: " << int(eBitmapMode));

    switch (eBitmapMode)
    {
    case BitmapMode_REPEAT:
        mpFS->singleElementNS(XML_a, XML_tile);
        break;
    case BitmapMode_STRETCH:
        WriteXGraphicStretch(rXPropSet, rxGraphic);
        break;
    default:
        break;
    }
}

void DrawingML::WriteBlipOrNormalFill( const Reference< XPropertySet >& xPropSet, const OUString& rURLPropName )
{
    // check for blip and otherwise fall back to normal fill
    // we always store normal fill properties but OOXML
    // uses a choice between our fill props and BlipFill
    if (GetProperty ( xPropSet, rURLPropName ))
        WriteBlipFill( xPropSet, rURLPropName );
    else
        WriteFill(xPropSet);
}

void DrawingML::WriteBlipFill( const Reference< XPropertySet >& rXPropSet, const OUString& sURLPropName )
{
    WriteBlipFill( rXPropSet, sURLPropName, XML_a );
}

void DrawingML::WriteBlipFill( const Reference< XPropertySet >& rXPropSet, const OUString& sURLPropName, sal_Int32 nXmlNamespace )
{
    if ( GetProperty( rXPropSet, sURLPropName ) )
    {
        uno::Reference<graphic::XGraphic> xGraphic;
        if (mAny.has<uno::Reference<awt::XBitmap>>())
        {
            uno::Reference<awt::XBitmap> xBitmap = mAny.get<uno::Reference<awt::XBitmap>>();
            if (xBitmap.is())
                xGraphic.set(xBitmap, uno::UNO_QUERY);
        }
        else if (mAny.has<uno::Reference<graphic::XGraphic>>())
        {
            xGraphic = mAny.get<uno::Reference<graphic::XGraphic>>();
        }

        if (xGraphic.is())
        {
            bool bWriteMode = false;
            if (sURLPropName == "FillBitmap" || sURLPropName == "BackGraphic")
                bWriteMode = true;
            WriteXGraphicBlipFill(rXPropSet, xGraphic, nXmlNamespace, bWriteMode);
        }
    }
}

void DrawingML::WriteXGraphicBlipFill(uno::Reference<beans::XPropertySet> const & rXPropSet,
                                      uno::Reference<graphic::XGraphic> const & rxGraphic,
                                      sal_Int32 nXmlNamespace, bool bWriteMode, bool bRelPathToMedia)
{
    if (!rxGraphic.is() )
        return;

    mpFS->startElementNS(nXmlNamespace , XML_blipFill, XML_rotWithShape, "0");

    WriteXGraphicBlip(rXPropSet, rxGraphic, bRelPathToMedia);

    if (bWriteMode)
    {
        WriteXGraphicBlipMode(rXPropSet, rxGraphic);
    }
    else if(GetProperty(rXPropSet, "FillBitmapStretch"))
    {
            bool bStretch = mAny.get<bool>();

            if (bStretch)
            {
                WriteXGraphicStretch(rXPropSet, rxGraphic);
            }
    }
    mpFS->endElementNS(nXmlNamespace, XML_blipFill);
}

void DrawingML::WritePattFill( const Reference< XPropertySet >& rXPropSet )
{
    if ( GetProperty( rXPropSet, "FillHatch" ) )
    {
        drawing::Hatch aHatch;
        mAny >>= aHatch;
        WritePattFill(rXPropSet, aHatch);
    }
}

void DrawingML::WritePattFill(const Reference<XPropertySet>& rXPropSet, const css::drawing::Hatch& rHatch)
{
        mpFS->startElementNS(XML_a, XML_pattFill, XML_prst, GetHatchPattern(rHatch));

        mpFS->startElementNS(XML_a, XML_fgClr);
        WriteColor(::Color(rHatch.Color));
        mpFS->endElementNS( XML_a , XML_fgClr );

        ::Color nColor = COL_WHITE;
        sal_Int32 nAlpha  = 0;

        if ( GetProperty( rXPropSet, "FillBackground" ) )
        {
            bool isBackgroundFilled = false;
            mAny >>= isBackgroundFilled;
            if( isBackgroundFilled )
            {
                nAlpha = MAX_PERCENT;

                if( GetProperty( rXPropSet, "FillColor" ) )
                {
                    mAny >>= nColor;
                }
            }
        }

        mpFS->startElementNS(XML_a, XML_bgClr);
        WriteColor(nColor, nAlpha);
        mpFS->endElementNS( XML_a , XML_bgClr );

        mpFS->endElementNS( XML_a , XML_pattFill );
}

void DrawingML::WriteGraphicCropProperties(uno::Reference<beans::XPropertySet> const & rXPropSet, Size const & rOriginalSize, MapMode const & rMapMode)
{
    if (GetProperty(rXPropSet, "GraphicCrop"))
    {
        Size aOriginalSize(rOriginalSize);

        // GraphicCrop is in mm100, so in case the original size is in pixels, convert it over.
        if (rMapMode.GetMapUnit() == MapUnit::MapPixel)
            aOriginalSize = Application::GetDefaultDevice()->PixelToLogic(aOriginalSize, MapMode(MapUnit::Map100thMM));

        css::text::GraphicCrop aGraphicCropStruct;
        mAny >>= aGraphicCropStruct;

        if ( (0 != aGraphicCropStruct.Left) || (0 != aGraphicCropStruct.Top) || (0 != aGraphicCropStruct.Right) || (0 != aGraphicCropStruct.Bottom) )
        {
            mpFS->singleElementNS( XML_a, XML_srcRect,
                XML_l, OString::number(rtl::math::round(aGraphicCropStruct.Left * 100000.0 / aOriginalSize.Width())),
                XML_t, OString::number(rtl::math::round(aGraphicCropStruct.Top * 100000.0 / aOriginalSize.Height())),
                XML_r, OString::number(rtl::math::round(aGraphicCropStruct.Right * 100000.0 / aOriginalSize.Width())),
                XML_b, OString::number(rtl::math::round(aGraphicCropStruct.Bottom * 100000.0 / aOriginalSize.Height())) );
        }
    }
}

void DrawingML::WriteSrcRectXGraphic(uno::Reference<beans::XPropertySet> const & rxPropertySet,
                                     uno::Reference<graphic::XGraphic> const & rxGraphic)
{
    Graphic aGraphic(rxGraphic);
    Size aOriginalSize = aGraphic.GetPrefSize();
    const MapMode& rMapMode = aGraphic.GetPrefMapMode();
    WriteGraphicCropProperties(rxPropertySet, aOriginalSize, rMapMode);
}

void DrawingML::WriteXGraphicStretch(uno::Reference<beans::XPropertySet> const & rXPropSet,
                                     uno::Reference<graphic::XGraphic> const & rxGraphic)
{
    mpFS->startElementNS(XML_a, XML_stretch);

    bool bCrop = false;
    if (GetProperty(rXPropSet, "GraphicCrop"))
    {
        css::text::GraphicCrop aGraphicCropStruct;
        mAny >>= aGraphicCropStruct;

        if ((0 != aGraphicCropStruct.Left)
         || (0 != aGraphicCropStruct.Top)
         || (0 != aGraphicCropStruct.Right)
         || (0 != aGraphicCropStruct.Bottom))
        {
            Graphic aGraphic(rxGraphic);
            Size aOriginalSize(aGraphic.GetPrefSize());
            mpFS->singleElementNS(XML_a, XML_fillRect,
                XML_l, OString::number(((aGraphicCropStruct.Left)   * 100000) / aOriginalSize.Width()),
                XML_t, OString::number(((aGraphicCropStruct.Top)    * 100000) / aOriginalSize.Height()),
                XML_r, OString::number(((aGraphicCropStruct.Right)  * 100000) / aOriginalSize.Width()),
                XML_b, OString::number(((aGraphicCropStruct.Bottom) * 100000) / aOriginalSize.Height()));
            bCrop = true;
        }
    }

    if (!bCrop)
    {
        mpFS->singleElementNS(XML_a, XML_fillRect);
    }

    mpFS->endElementNS(XML_a, XML_stretch);
}

void DrawingML::WriteTransformation(const tools::Rectangle& rRect,
        sal_Int32 nXmlNamespace, bool bFlipH, bool bFlipV, sal_Int32 nRotation, bool bIsGroupShape)
{

    mpFS->startElementNS( nXmlNamespace, XML_xfrm,
                          XML_flipH, bFlipH ? "1" : nullptr,
                          XML_flipV, bFlipV ? "1" : nullptr,
                          XML_rot, (nRotation % 21600000) ? OString::number(nRotation).getStr() : nullptr );

    sal_Int32 nLeft = rRect.Left();
    sal_Int32 nTop = rRect.Top();
    if (GetDocumentType() == DOCUMENT_DOCX && !m_xParent.is())
    {
        nLeft = 0;
        nTop = 0;
    }

    mpFS->singleElementNS(XML_a, XML_off,
        XML_x, OString::number(oox::drawingml::convertHmmToEmu(nLeft)),
        XML_y, OString::number(oox::drawingml::convertHmmToEmu(nTop)));
    mpFS->singleElementNS(XML_a, XML_ext,
        XML_cx, OString::number(oox::drawingml::convertHmmToEmu(rRect.GetWidth())),
        XML_cy, OString::number(oox::drawingml::convertHmmToEmu(rRect.GetHeight())));

    if (GetDocumentType() != DOCUMENT_DOCX && bIsGroupShape)
    {
        mpFS->singleElementNS(XML_a, XML_chOff,
            XML_x, OString::number(oox::drawingml::convertHmmToEmu(nLeft)),
            XML_y, OString::number(oox::drawingml::convertHmmToEmu(nTop)));
        mpFS->singleElementNS(XML_a, XML_chExt,
            XML_cx, OString::number(oox::drawingml::convertHmmToEmu(rRect.GetWidth())),
            XML_cy, OString::number(oox::drawingml::convertHmmToEmu(rRect.GetHeight())));
    }

    mpFS->endElementNS( nXmlNamespace, XML_xfrm );
}

void DrawingML::WriteShapeTransformation( const Reference< XShape >& rXShape, sal_Int32 nXmlNamespace, bool bFlipH, bool bFlipV, bool bSuppressRotation, bool bSuppressFlipping, bool bFlippedBeforeRotation )
{
    SAL_INFO("oox.shape",  "write shape transformation");

    sal_Int32 nRotation=0;
    awt::Point aPos = rXShape->getPosition();
    awt::Size aSize = rXShape->getSize();

    bool bFlipHWrite = bFlipH && !bSuppressFlipping;
    bool bFlipVWrite = bFlipV && !bSuppressFlipping;
    bFlipH = bFlipH && !bFlippedBeforeRotation;
    bFlipV = bFlipV && !bFlippedBeforeRotation;

    if (GetDocumentType() == DOCUMENT_DOCX && m_xParent.is())
    {
        awt::Point aParentPos = m_xParent->getPosition();
        aPos.X -= aParentPos.X;
        aPos.Y -= aParentPos.Y;
    }

    if ( aSize.Width < 0 )
        aSize.Width = 1000;
    if ( aSize.Height < 0 )
        aSize.Height = 1000;
    if (!bSuppressRotation)
    {
        SdrObject* pShape = GetSdrObjectFromXShape( rXShape );
        nRotation = pShape ? pShape->GetRotateAngle() : 0;
        if ( nRotation != 0 && GetDocumentType() != DOCUMENT_DOCX )
        {
            int faccos=bFlipV ? -1 : 1;
            int facsin=bFlipH ? -1 : 1;
            aPos.X-=(1-faccos*cos(nRotation*F_PI18000))*aSize.Width/2-facsin*sin(nRotation*F_PI18000)*aSize.Height/2;
            aPos.Y-=(1-faccos*cos(nRotation*F_PI18000))*aSize.Height/2+facsin*sin(nRotation*F_PI18000)*aSize.Width/2;
        }

        // The RotateAngle property's value is independent from any flipping, and that's exactly what we need here.
        uno::Reference<beans::XPropertySet> xPropertySet(rXShape, uno::UNO_QUERY);
        uno::Reference<beans::XPropertySetInfo> xPropertySetInfo = xPropertySet->getPropertySetInfo();
        if (xPropertySetInfo->hasPropertyByName("RotateAngle"))
            xPropertySet->getPropertyValue("RotateAngle") >>= nRotation;
    }

    // OOXML flips shapes before rotating them.
    if(bFlipH != bFlipV)
        nRotation = nRotation * -1 + 36000;

    WriteTransformation(tools::Rectangle(Point(aPos.X, aPos.Y), Size(aSize.Width, aSize.Height)), nXmlNamespace,
            bFlipHWrite, bFlipVWrite, ExportRotateClockwisify(nRotation), IsGroupShape( rXShape ));
}

void DrawingML::WriteRunProperties( const Reference< XPropertySet >& rRun, bool bIsField, sal_Int32 nElement, bool bCheckDirect,
                                    bool& rbOverridingCharHeight, sal_Int32& rnCharHeight )
{
    Reference< XPropertySet > rXPropSet = rRun;
    Reference< XPropertyState > rXPropState( rRun, UNO_QUERY );
    OUString usLanguage;
    PropertyState eState;
    SvtScriptType nScriptType = SvtLanguageOptions::GetScriptTypeOfLanguage( Application::GetSettings().GetLanguageTag().getLanguageType() );
    bool bComplex = ( nScriptType == SvtScriptType::COMPLEX );
    const char* bold = "0";
    const char* italic = nullptr;
    const char* underline = nullptr;
    const char* strikeout = nullptr;
    const char* cap = nullptr;
    sal_Int32 nSize = 1800;
    sal_Int32 nCharEscapement = 0;
    sal_Int32 nCharKerning = 0;

    if ( nElement == XML_endParaRPr && rbOverridingCharHeight )
    {
        nSize = rnCharHeight;
    }
    else if (GetProperty(rXPropSet, "CharHeight"))
    {
        nSize = static_cast<sal_Int32>(100*(*o3tl::doAccess<float>(mAny)));
        if ( nElement == XML_rPr )
        {
            rbOverridingCharHeight = true;
            rnCharHeight = nSize;
        }
    }

    if (GetProperty(rXPropSet, "CharKerning"))
        nCharKerning = static_cast<sal_Int32>(*o3tl::doAccess<sal_Int16>(mAny));
    /**  While setting values in propertymap,
    *    CharKerning converted using GetTextSpacingPoint
    *    i.e set @ https://opengrok.libreoffice.org/xref/core/oox/source/drawingml/textcharacterproperties.cxx#129
    *    therefore to get original value CharKerning need to be convert.
    *    https://opengrok.libreoffice.org/xref/core/oox/source/drawingml/drawingmltypes.cxx#95
    **/
    nCharKerning = ((nCharKerning * 720)-360) / 254;

    if ((bComplex && GetProperty(rXPropSet, "CharWeightComplex"))
        || GetProperty(rXPropSet, "CharWeight"))
    {
        if ( *o3tl::doAccess<float>(mAny) >= awt::FontWeight::SEMIBOLD )
            bold = "1";
    }

    if ((bComplex && GetProperty(rXPropSet, "CharPostureComplex"))
        || GetProperty(rXPropSet, "CharPosture"))
        switch ( *o3tl::doAccess<awt::FontSlant>(mAny) )
        {
            case awt::FontSlant_OBLIQUE :
            case awt::FontSlant_ITALIC :
                italic = "1";
                break;
            default:
                break;
        }

    if ((bCheckDirect && GetPropertyAndState(rXPropSet, rXPropState, "CharUnderline", eState)
         && eState == beans::PropertyState_DIRECT_VALUE)
        || GetProperty(rXPropSet, "CharUnderline"))
    {
        switch ( *o3tl::doAccess<sal_Int16>(mAny) )
        {
            case awt::FontUnderline::SINGLE :
                underline = "sng";
                break;
            case awt::FontUnderline::DOUBLE :
                underline = "dbl";
                break;
            case awt::FontUnderline::DOTTED :
                underline = "dotted";
                break;
            case awt::FontUnderline::DASH :
                underline = "dash";
                break;
            case awt::FontUnderline::LONGDASH :
                underline = "dashLong";
                break;
            case awt::FontUnderline::DASHDOT :
                underline = "dotDash";
                break;
            case awt::FontUnderline::DASHDOTDOT :
                underline = "dotDotDash";
                break;
            case awt::FontUnderline::WAVE :
                underline = "wavy";
                break;
            case awt::FontUnderline::DOUBLEWAVE :
                underline = "wavyDbl";
                break;
            case awt::FontUnderline::BOLD :
                underline = "heavy";
                break;
            case awt::FontUnderline::BOLDDOTTED :
                underline = "dottedHeavy";
                break;
            case awt::FontUnderline::BOLDDASH :
                underline = "dashHeavy";
                break;
            case awt::FontUnderline::BOLDLONGDASH :
                underline = "dashLongHeavy";
                break;
            case awt::FontUnderline::BOLDDASHDOT :
                underline = "dotDashHeavy";
                break;
            case awt::FontUnderline::BOLDDASHDOTDOT :
                underline = "dotDotDashHeavy";
                break;
            case awt::FontUnderline::BOLDWAVE :
                underline = "wavyHeavy";
                break;
        }
    }

    if ((bCheckDirect && GetPropertyAndState(rXPropSet, rXPropState, "CharStrikeout", eState)
         && eState == beans::PropertyState_DIRECT_VALUE)
        || GetProperty(rXPropSet, "CharStrikeout"))
    {
        switch ( *o3tl::doAccess<sal_Int16>(mAny) )
        {
            case awt::FontStrikeout::NONE :
               strikeout = "noStrike";
               break;
            case awt::FontStrikeout::SINGLE :
            // LibO supports further values of character
            // strikeout, OOXML standard (20.1.10.78,
            // ST_TextStrikeType) however specifies only
            // 3 - single, double and none. Approximate
            // the remaining ones by single strike (better
            // some strike than none at all).
            // TODO: figure out how to do this better
            case awt::FontStrikeout::BOLD :
            case awt::FontStrikeout::SLASH :
            case awt::FontStrikeout::X :
               strikeout = "sngStrike";
               break;
            case awt::FontStrikeout::DOUBLE :
               strikeout = "dblStrike";
               break;
        }
    }

    if (GetProperty(rXPropSet, "CharLocale"))
    {
        css::lang::Locale aLocale;
        mAny >>= aLocale;
        LanguageTag aLanguageTag( aLocale);
        if (!aLanguageTag.isSystemLocale())
            usLanguage = aLanguageTag.getBcp47MS();
    }

    if (GetPropertyAndState(rXPropSet, rXPropState, "CharEscapement", eState)
        && eState == beans::PropertyState_DIRECT_VALUE)
        mAny >>= nCharEscapement;

    if (nCharEscapement
        && (GetPropertyAndState(rXPropSet, rXPropState, "CharEscapementHeight", eState)
            && eState == beans::PropertyState_DIRECT_VALUE))
    {
        sal_uInt32 nCharEscapementHeight = 0;
        mAny >>= nCharEscapementHeight;
        nSize = (nSize * nCharEscapementHeight) / 100;
        // MSO uses default ~58% size
        nSize = (nSize / 0.58);
    }

    if (GetProperty(rXPropSet, "CharCaseMap"))
    {
        switch ( *o3tl::doAccess<sal_Int16>(mAny) )
        {
            case CaseMap::UPPERCASE :
                cap = "all";
                break;
            case CaseMap::SMALLCAPS :
                cap = "small";
                break;
        }
    }

    mpFS->startElementNS( XML_a, nElement,
                          XML_b, bold,
                          XML_i, italic,
                          XML_lang, usLanguage.isEmpty() ? nullptr : usLanguage.toUtf8().getStr(),
                          XML_sz, OString::number(nSize),
            // For Condensed character spacing spc value is negative.
                          XML_spc, nCharKerning ? OString::number(nCharKerning).getStr() : nullptr,
                          XML_strike, strikeout,
                          XML_u, underline,
                          XML_baseline, nCharEscapement == 0 ? nullptr : OString::number(nCharEscapement*1000).getStr(),
                          XML_cap, cap );

    // mso doesn't like text color to be placed after typeface
    if ((bCheckDirect && GetPropertyAndState(rXPropSet, rXPropState, "CharColor", eState)
         && eState == beans::PropertyState_DIRECT_VALUE)
        || GetProperty(rXPropSet, "CharColor"))
    {
        ::Color color( *o3tl::doAccess<sal_uInt32>(mAny) );
        SAL_INFO("oox.shape", "run color: " << sal_uInt32(color) << " auto: " << sal_uInt32(COL_AUTO));

        // tdf#104219 In LibreOffice and MS Office, there are two types of colors:
        // Automatic and Fixed. OOXML is setting automatic color, by not providing color.
        if( color != COL_AUTO )
        {
            color.SetTransparency(0);
            // TODO: special handle embossed/engraved
            WriteSolidFill( color );
        }
    }

    if (underline
        && ((bCheckDirect
             && GetPropertyAndState(rXPropSet, rXPropState, "CharUnderlineColor", eState)
             && eState == beans::PropertyState_DIRECT_VALUE)
            || GetProperty(rXPropSet, "CharUnderlineColor")))
    {
        ::Color color(*o3tl::doAccess<sal_uInt32>(mAny));
        // if color is automatic, then we shouldn't write information about color but to take color from character
        if( color != COL_AUTO )
        {
            mpFS->startElementNS(XML_a, XML_uFill);
            WriteSolidFill( color );
            mpFS->endElementNS( XML_a, XML_uFill );
        }
        else
        {
            mpFS->singleElementNS(XML_a, XML_uFillTx);
        }
    }

    if (GetProperty(rXPropSet, "CharFontName"))
    {
        const char* const pitch = nullptr;
        const char* const charset = nullptr;
        OUString usTypeface;

        mAny >>= usTypeface;
        OUString aSubstName( GetSubsFontName( usTypeface, SubsFontFlags::ONLYONE | SubsFontFlags::MS ) );
        if (!aSubstName.isEmpty())
            usTypeface = aSubstName;

        mpFS->singleElementNS( XML_a, XML_latin,
                               XML_typeface, usTypeface.toUtf8(),
                               XML_pitchFamily, pitch,
                               XML_charset, charset );
    }

    if ((bComplex
         && (GetPropertyAndState(rXPropSet, rXPropState, "CharFontNameComplex", eState)
             && eState == beans::PropertyState_DIRECT_VALUE))
        || (!bComplex
            && (GetPropertyAndState(rXPropSet, rXPropState, "CharFontNameAsian", eState)
                && eState == beans::PropertyState_DIRECT_VALUE)))
    {
        const char* const pitch = nullptr;
        const char* const charset = nullptr;
        OUString usTypeface;

        mAny >>= usTypeface;
        OUString aSubstName( GetSubsFontName( usTypeface, SubsFontFlags::ONLYONE | SubsFontFlags::MS ) );
        if (!aSubstName.isEmpty())
            usTypeface = aSubstName;

        mpFS->singleElementNS( XML_a, bComplex ? XML_cs : XML_ea,
                               XML_typeface, usTypeface.toUtf8(),
                               XML_pitchFamily, pitch,
                               XML_charset, charset );
    }

    if( bIsField )
    {
        Reference< XTextField > rXTextField;
        if (GetProperty(rXPropSet, "TextField"))
            mAny >>= rXTextField;
        if( rXTextField.is() )
            rXPropSet.set( rXTextField, UNO_QUERY );
    }

    // field properties starts here
    if (GetProperty(rXPropSet, "URL"))
    {
        OUString sURL;

        mAny >>= sURL;
        if( !sURL.isEmpty() ) {
            OUString sRelId = mpFB->addRelation( mpFS->getOutputStream(),
                                  oox::getRelationship(Relationship::HYPERLINK),
                                  sURL, true );

            mpFS->singleElementNS(XML_a, XML_hlinkClick, FSNS(XML_r, XML_id), sRelId.toUtf8());
        }
    }

    mpFS->endElementNS( XML_a, nElement );
}

OUString DrawingML::GetFieldValue( const css::uno::Reference< css::text::XTextRange >& rRun, bool& bIsURLField )
{
    Reference< XPropertySet > rXPropSet( rRun, UNO_QUERY );
    OUString aFieldType, aFieldValue;

    if (GetProperty(rXPropSet, "TextPortionType"))
    {
        aFieldType = *o3tl::doAccess<OUString>(mAny);
        SAL_INFO("oox.shape", "field type: " << aFieldType);
    }

    if( aFieldType == "TextField" )
    {
        Reference< XTextField > rXTextField;
        if (GetProperty(rXPropSet, "TextField"))
            mAny >>= rXTextField;
        if( rXTextField.is() )
        {
            rXPropSet.set( rXTextField, UNO_QUERY );
            if( rXPropSet.is() )
            {
                OUString aFieldKind( rXTextField->getPresentation( true ) );
                SAL_INFO("oox.shape", "field kind: " << aFieldKind);
                if( aFieldKind == "Page" )
                {
                    aFieldValue = "slidenum";
                }
                else if( aFieldKind == "Pages" )
                {
                    aFieldValue = "slidecount";
                }
                else if( aFieldKind == "PageName" )
                {
                    aFieldValue = "slidename";
                }
                else if( aFieldKind == "URL" )
                {
                    bIsURLField = true;
                    if (GetProperty(rXPropSet, "Representation"))
                        mAny >>= aFieldValue;

                }
                else if(aFieldKind == "Date")
                {
                    sal_Int32 nNumFmt = -1;
                    rXPropSet->getPropertyValue(UNO_TC_PROP_NUMFORMAT) >>= nNumFmt;
                    switch(static_cast<SvxDateFormat>(nNumFmt))
                    {
                        case SvxDateFormat::StdSmall:
                        case SvxDateFormat::A: aFieldValue = "datetime"; // 13/02/96
                                              break;
                        case SvxDateFormat::B: aFieldValue = "datetime1"; // 13/02/1996
                                              break;
                        case SvxDateFormat::StdBig:
                        case SvxDateFormat::D: aFieldValue = "datetime3"; // 13 February 1996
                                              break;
                        default: break;
                    }
                }
                else if(aFieldKind == "ExtTime")
                {
                    sal_Int32 nNumFmt = -1;
                    rXPropSet->getPropertyValue(UNO_TC_PROP_NUMFORMAT) >>= nNumFmt;
                    switch(static_cast<SvxTimeFormat>(nNumFmt))
                    {
                        case SvxTimeFormat::Standard:
                        case SvxTimeFormat::HH24_MM_SS:
                            aFieldValue = "datetime11"; // 13:49:38
                            break;
                        case SvxTimeFormat::HH24_MM:
                            aFieldValue = "datetime10"; // 13:49
                            break;
                        case SvxTimeFormat::HH12_MM:
                            aFieldValue = "datetime12"; // 01:49 PM
                            break;
                        case SvxTimeFormat::HH12_MM_SS:
                            aFieldValue = "datetime13"; // 01:49:38 PM
                            break;
                        default: break;
                    }
                }
                else if(aFieldKind == "ExtFile")
                {
                    sal_Int32 nNumFmt = -1;
                    rXPropSet->getPropertyValue(UNO_TC_PROP_FILE_FORMAT) >>= nNumFmt;
                    switch(nNumFmt)
                    {
                        case 0: aFieldValue = "file"; // Path/File name
                                break;
                        case 1: aFieldValue = "file1"; // Path
                                break;
                        case 2: aFieldValue = "file2"; // File name without extension
                                break;
                        case 3: aFieldValue = "file3"; // File name with extension
                    }
                }
                else if(aFieldKind == "Author")
                {
                    aFieldValue = "author";
                }
            }
        }
    }
    return aFieldValue;
}

void DrawingML::WriteRun( const Reference< XTextRange >& rRun,
                          bool& rbOverridingCharHeight, sal_Int32& rnCharHeight)
{
    Reference< XPropertySet > rXPropSet( rRun, UNO_QUERY );
    sal_Int16 nLevel = -1;
    if (GetProperty(rXPropSet, "NumberingLevel"))
        mAny >>= nLevel;

    bool bNumberingIsNumber = true;
    if (GetProperty(rXPropSet, "NumberingIsNumber"))
        mAny >>= bNumberingIsNumber;

    bool bIsURLField = false;
    OUString sFieldValue = GetFieldValue( rRun, bIsURLField );
    bool bWriteField  = !( sFieldValue.isEmpty() || bIsURLField );

    OUString sText = rRun->getString();

    //if there is no text following the bullet, add a space after the bullet
    if (nLevel !=-1 && bNumberingIsNumber && sText.isEmpty() )
         sText=" ";

    if ( bIsURLField )
        sText = sFieldValue;

    if( sText.isEmpty())
    {
        Reference< XPropertySet > xPropSet( rRun, UNO_QUERY );

        try
        {
            if( !xPropSet.is() || !( xPropSet->getPropertyValue( "PlaceholderText" ) >>= sText ) )
                return;
            if( sText.isEmpty() )
                return;
        }
        catch (const Exception &)
        {
            return;
        }
    }

    if (sText == "\n")
    {
        mpFS->singleElementNS(XML_a, XML_br);
    }
    else
    {
        if( bWriteField )
        {
            OString sUUID(comphelper::xml::generateGUIDString());
            mpFS->startElementNS( XML_a, XML_fld,
                                  XML_id, sUUID.getStr(),
                                  XML_type, sFieldValue.toUtf8() );
        }
        else
        {
            mpFS->startElementNS(XML_a, XML_r);
        }

        Reference< XPropertySet > xPropSet( rRun, uno::UNO_QUERY );
        WriteRunProperties( xPropSet, bIsURLField, XML_rPr, true, rbOverridingCharHeight, rnCharHeight );
        mpFS->startElementNS(XML_a, XML_t);
        mpFS->writeEscaped( sText );
        mpFS->endElementNS( XML_a, XML_t );

        if( bWriteField )
            mpFS->endElementNS( XML_a, XML_fld );
        else
            mpFS->endElementNS( XML_a, XML_r );
    }
}

static OUString GetAutoNumType(SvxNumType nNumberingType, bool bSDot, bool bPBehind, bool bPBoth)
{
    OUString sPrefixSuffix;

    if (bPBoth)
        sPrefixSuffix = "ParenBoth";
    else if (bPBehind)
        sPrefixSuffix = "ParenR";
    else if (bSDot)
        sPrefixSuffix = "Period";

    switch( nNumberingType )
    {
        case SVX_NUM_CHARS_UPPER_LETTER_N :
        case SVX_NUM_CHARS_UPPER_LETTER :
            return "alphaUc" + sPrefixSuffix;

        case SVX_NUM_CHARS_LOWER_LETTER_N :
        case SVX_NUM_CHARS_LOWER_LETTER :
            return "alphaLc" + sPrefixSuffix;

        case SVX_NUM_ROMAN_UPPER :
            return "romanUc" + sPrefixSuffix;

        case SVX_NUM_ROMAN_LOWER :
            return "romanLc" + sPrefixSuffix;

        case SVX_NUM_ARABIC :
        {
            if (sPrefixSuffix.isEmpty())
                return "arabicPlain";
            else
                return "arabic" + sPrefixSuffix;
        }
        default:
            break;
    }

    return OUString();
}

void DrawingML::WriteParagraphNumbering(const Reference< XPropertySet >& rXPropSet, float fFirstCharHeight, sal_Int16 nLevel )
{
    if (nLevel < 0 || !GetProperty(rXPropSet, "NumberingRules"))
        return;

    Reference< XIndexAccess > rXIndexAccess;

    if (!(mAny >>= rXIndexAccess) || nLevel >= rXIndexAccess->getCount())
        return;

    SAL_INFO("oox.shape", "numbering rules");

    Sequence<PropertyValue> aPropertySequence;
    rXIndexAccess->getByIndex(nLevel) >>= aPropertySequence;

    if (!aPropertySequence.hasElements())
        return;

    sal_Int32 nPropertyCount = aPropertySequence.getLength();

    const PropertyValue* pPropValue = aPropertySequence.getArray();

    SvxNumType nNumberingType = SVX_NUM_NUMBER_NONE;
    bool bSDot = false;
    bool bPBehind = false;
    bool bPBoth = false;
    sal_Unicode aBulletChar = 0x2022; // a bullet
    awt::FontDescriptor aFontDesc;
    bool bHasFontDesc = false;
    uno::Reference<graphic::XGraphic> xGraphic;
    sal_Int16 nBulletRelSize = 0;
    sal_Int16 nStartWith = 1;
    ::Color nBulletColor;
    bool bHasBulletColor = false;
    awt::Size aGraphicSize;

    for ( sal_Int32 i = 0; i < nPropertyCount; i++ )
    {
        OUString aPropName( pPropValue[ i ].Name );
        SAL_INFO("oox.shape", "pro name: " << aPropName);
        if ( aPropName == "NumberingType" )
        {
            nNumberingType = static_cast<SvxNumType>(*o3tl::doAccess<sal_Int16>(pPropValue[i].Value));
        }
        else if ( aPropName == "Prefix" )
        {
            if( *o3tl::doAccess<OUString>(pPropValue[i].Value) == ")")
                bPBoth = true;
        }
        else if ( aPropName == "Suffix" )
        {
            auto s = o3tl::doAccess<OUString>(pPropValue[i].Value);
            if( *s == ".")
                bSDot = true;
            else if( *s == ")")
                bPBehind = true;
        }
        else if(aPropName == "BulletColor")
        {
            nBulletColor = ::Color(*o3tl::doAccess<sal_uInt32>(pPropValue[i].Value));
            bHasBulletColor = true;
        }
        else if ( aPropName == "BulletChar" )
        {
            aBulletChar = (*o3tl::doAccess<OUString>(pPropValue[i].Value))[ 0 ];
        }
        else if ( aPropName == "BulletFont" )
        {
            aFontDesc = *o3tl::doAccess<awt::FontDescriptor>(pPropValue[i].Value);
            bHasFontDesc = true;

            // Our numbullet dialog has set the wrong textencoding for our "StarSymbol" font,
            // instead of a Unicode encoding the encoding RTL_TEXTENCODING_SYMBOL was used.
            // Because there might exist a lot of damaged documemts I added this two lines
            // which fixes the bullet problem for the export.
            if ( aFontDesc.Name.equalsIgnoreAsciiCase("StarSymbol") )
                aFontDesc.CharSet = RTL_TEXTENCODING_MS_1252;

        }
        else if ( aPropName == "BulletRelSize" )
        {
            nBulletRelSize = *o3tl::doAccess<sal_Int16>(pPropValue[i].Value);
        }
        else if ( aPropName == "StartWith" )
        {
            nStartWith = *o3tl::doAccess<sal_Int16>(pPropValue[i].Value);
        }
        else if (aPropName == "GraphicBitmap")
        {
            auto xBitmap = pPropValue[i].Value.get<uno::Reference<awt::XBitmap>>();
            xGraphic.set(xBitmap, uno::UNO_QUERY);
        }
        else if ( aPropName == "GraphicSize" )
        {
            aGraphicSize = *o3tl::doAccess<awt::Size>(pPropValue[i].Value);
            SAL_INFO("oox.shape", "graphic size: " << aGraphicSize.Width << "x" << aGraphicSize.Height);
        }
    }

    if (nNumberingType == SVX_NUM_NUMBER_NONE)
        return;

    Graphic aGraphic(xGraphic);
    if (xGraphic.is() && aGraphic.GetType() != GraphicType::NONE)
    {
        long nFirstCharHeightMm = TransformMetric(fFirstCharHeight * 100.f, FieldUnit::POINT, FieldUnit::MM);
        float fBulletSizeRel = aGraphicSize.Height / static_cast<float>(nFirstCharHeightMm) / OOX_BULLET_LIST_SCALE_FACTOR;

        OUString sRelationId;

        if (fBulletSizeRel < 1.0f)
        {
            // Add padding to get the bullet point centered in PPT
            Size aDestSize(64, 64);
            float fBulletSizeRelX = fBulletSizeRel / aGraphicSize.Height * aGraphicSize.Width;
            long nPaddingX = std::max<long>(0, std::lround((aDestSize.Width() - fBulletSizeRelX * aDestSize.Width()) / 2.f));
            long nPaddingY = std::lround((aDestSize.Height() - fBulletSizeRel * aDestSize.Height()) / 2.f);
            tools::Rectangle aDestRect(nPaddingX, nPaddingY, aDestSize.Width() - nPaddingX, aDestSize.Height() - nPaddingY);

            BitmapEx aSourceBitmap(aGraphic.GetBitmapEx());
            aSourceBitmap.Scale(aDestRect.GetSize());
            tools::Rectangle aSourceRect(Point(0, 0), aDestRect.GetSize());
            BitmapEx aDestBitmap(aDestSize, 24);
            aDestBitmap.CopyPixel(aDestRect, aSourceRect, &aSourceBitmap);
            Graphic aDestGraphic(aDestBitmap);
            sRelationId = WriteImage(aDestGraphic);
            fBulletSizeRel = 1.0f;
        }
        else
        {
            sRelationId = WriteImage(aGraphic);
        }

        mpFS->singleElementNS( XML_a, XML_buSzPct,
                               XML_val, OString::number(std::min<sal_Int32>(std::lround(100000.f * fBulletSizeRel), 400000)));
        mpFS->startElementNS(XML_a, XML_buBlip);
        mpFS->singleElementNS(XML_a, XML_blip, FSNS(XML_r, XML_embed), sRelationId.toUtf8());
        mpFS->endElementNS( XML_a, XML_buBlip );
    }
    else
    {
        if(bHasBulletColor)
        {
               if (nBulletColor == COL_AUTO )
               {
                   nBulletColor = ::Color(mbIsBackgroundDark ? 0xffffff : 0x000000);
               }
               mpFS->startElementNS(XML_a, XML_buClr);
               WriteColor( nBulletColor );
               mpFS->endElementNS( XML_a, XML_buClr );
        }

        if( nBulletRelSize && nBulletRelSize != 100 )
            mpFS->singleElementNS( XML_a, XML_buSzPct,
                                   XML_val, OString::number(std::clamp<sal_Int32>(1000*nBulletRelSize, 25000, 400000)));
        if( bHasFontDesc )
        {
            if ( SVX_NUM_CHAR_SPECIAL == nNumberingType )
                aBulletChar = SubstituteBullet( aBulletChar, aFontDesc );
            mpFS->singleElementNS( XML_a, XML_buFont,
                                   XML_typeface, aFontDesc.Name.toUtf8(),
                                   XML_charset, (aFontDesc.CharSet == awt::CharSet::SYMBOL) ? "2" : nullptr );
        }

        OUString aAutoNumType = GetAutoNumType( nNumberingType, bSDot, bPBehind, bPBoth );

        if (!aAutoNumType.isEmpty())
        {
            mpFS->singleElementNS(XML_a, XML_buAutoNum,
                                  XML_type, aAutoNumType.toUtf8(),
                                  XML_startAt, nStartWith > 1 ? OString::number(nStartWith).getStr() : nullptr);
        }
        else
        {
            mpFS->singleElementNS(XML_a, XML_buChar, XML_char, OUString(aBulletChar).toUtf8());
        }
    }
}

bool DrawingML::IsGroupShape( const Reference< XShape >& rXShape )
{
    bool bRet = false;
    if ( rXShape.is() )
    {
        uno::Reference<lang::XServiceInfo> xServiceInfo(rXShape, uno::UNO_QUERY_THROW);
        bRet = xServiceInfo->supportsService("com.sun.star.drawing.GroupShape");
    }
    return bRet;
}

bool DrawingML::IsDiagram(const Reference<XShape>& rXShape)
{
    uno::Reference<beans::XPropertySet> xPropSet(rXShape, uno::UNO_QUERY);
    if (!xPropSet.is())
        return false;

    // if the shape doesn't have the InteropGrabBag property, it's not a diagram
    uno::Reference<beans::XPropertySetInfo> xPropSetInfo = xPropSet->getPropertySetInfo();
    OUString aName = UNO_NAME_MISC_OBJ_INTEROPGRABBAG;
    if (!xPropSetInfo->hasPropertyByName(aName))
        return false;

    uno::Sequence<beans::PropertyValue> propList;
    xPropSet->getPropertyValue(aName) >>= propList;
    for (sal_Int32 nProp = 0; nProp < propList.getLength(); ++nProp)
    {
        // if we find any of the diagram components, it's a diagram
        OUString propName = propList[nProp].Name;
        if (propName == "OOXData" || propName == "OOXLayout" || propName == "OOXStyle"
            || propName == "OOXColor" || propName == "OOXDrawing")
            return true;
    }
    return false;
}

sal_Int32 DrawingML::getBulletMarginIndentation (const Reference< XPropertySet >& rXPropSet,sal_Int16 nLevel, const OUString& propName)
{
    if (nLevel < 0 || !GetProperty(rXPropSet, "NumberingRules"))
        return 0;

    Reference< XIndexAccess > rXIndexAccess;

    if (!(mAny >>= rXIndexAccess) || nLevel >= rXIndexAccess->getCount())
        return 0;

    SAL_INFO("oox.shape", "numbering rules");

    Sequence<PropertyValue> aPropertySequence;
    rXIndexAccess->getByIndex(nLevel) >>= aPropertySequence;

    if (!aPropertySequence.hasElements())
        return 0;

    sal_Int32 nPropertyCount = aPropertySequence.getLength();

    const PropertyValue* pPropValue = aPropertySequence.getArray();

    for ( sal_Int32 i = 0; i < nPropertyCount; i++ )
    {
        OUString aPropName( pPropValue[ i ].Name );
        SAL_INFO("oox.shape", "pro name: " << aPropName);
        if ( aPropName == propName )
            return *o3tl::doAccess<sal_Int32>(pPropValue[i].Value);
    }

    return 0;
}

const char* DrawingML::GetAlignment( style::ParagraphAdjust nAlignment )
{
    const char* sAlignment = nullptr;

    switch( nAlignment )
    {
        case style::ParagraphAdjust_CENTER:
            sAlignment = "ctr";
            break;
        case style::ParagraphAdjust_RIGHT:
            sAlignment = "r";
            break;
        case style::ParagraphAdjust_BLOCK:
            sAlignment = "just";
            break;
        default:
            ;
    }

    return sAlignment;
}

void DrawingML::WriteLinespacing( const LineSpacing& rSpacing )
{
    if( rSpacing.Mode == LineSpacingMode::PROP )
    {
        mpFS->singleElementNS( XML_a, XML_spcPct,
                               XML_val, OString::number(static_cast<sal_Int32>(rSpacing.Height)*1000));
    }
    else
    {
        mpFS->singleElementNS( XML_a, XML_spcPts,
                               XML_val, OString::number(std::lround(rSpacing.Height / 25.4 * 72)));
    }
}

void DrawingML::WriteParagraphProperties( const Reference< XTextContent >& rParagraph, float fFirstCharHeight)
{
    Reference< XPropertySet > rXPropSet( rParagraph, UNO_QUERY );
    Reference< XPropertyState > rXPropState( rParagraph, UNO_QUERY );
    PropertyState eState;

    if( !rXPropSet.is() || !rXPropState.is() )
        return;

    sal_Int16 nLevel = -1;
    if (GetProperty(rXPropSet, "NumberingLevel"))
        mAny >>= nLevel;

    sal_Int16 nTmp = sal_Int16(style::ParagraphAdjust_LEFT);
    if (GetProperty(rXPropSet, "ParaAdjust"))
        mAny >>= nTmp;
    style::ParagraphAdjust nAlignment = static_cast<style::ParagraphAdjust>(nTmp);

    bool bHasLinespacing = false;
    LineSpacing aLineSpacing;
    if (GetPropertyAndState(rXPropSet, rXPropState, "ParaLineSpacing", eState)
        && eState == beans::PropertyState_DIRECT_VALUE)
        bHasLinespacing = ( mAny >>= aLineSpacing );

    bool bRtl = false;
    if (GetProperty(rXPropSet, "WritingMode"))
    {
        sal_Int16 nWritingMode;
        if( ( mAny >>= nWritingMode ) && nWritingMode == text::WritingMode2::RL_TB )
        {
            bRtl = true;
        }
    }

    sal_Int32 nParaLeftMargin = 0;
    sal_Int32 nParaFirstLineIndent = 0;

    if (GetProperty(rXPropSet, "ParaLeftMargin"))
        mAny >>= nParaLeftMargin;
    if (GetProperty(rXPropSet, "ParaFirstLineIndent"))
        mAny >>= nParaFirstLineIndent;

    sal_Int32 nParaTopMargin = 0;
    sal_Int32 nParaBottomMargin = 0;

    if (GetProperty(rXPropSet, "ParaTopMargin"))
        mAny >>= nParaTopMargin;
    if (GetProperty(rXPropSet, "ParaBottomMargin"))
        mAny >>= nParaBottomMargin;

    sal_Int32 nLeftMargin =  getBulletMarginIndentation ( rXPropSet, nLevel,"LeftMargin");
    sal_Int32 nLineIndentation = getBulletMarginIndentation ( rXPropSet, nLevel,"FirstLineOffset");

    if( nLevel != -1
        || nAlignment != style::ParagraphAdjust_LEFT
        || bHasLinespacing )
    {
        if (nParaLeftMargin) // For Paragraph
            mpFS->startElementNS( XML_a, XML_pPr,
                               XML_lvl, nLevel > 0 ? OString::number(nLevel).getStr() : nullptr,
                               XML_marL, nParaLeftMargin > 0 ? OString::number(oox::drawingml::convertHmmToEmu(nParaLeftMargin)).getStr() : nullptr,
                               XML_indent, nParaFirstLineIndent ? OString::number(oox::drawingml::convertHmmToEmu(nParaFirstLineIndent)).getStr() : nullptr,
                               XML_algn, GetAlignment( nAlignment ),
                               XML_rtl, bRtl ? ToPsz10(bRtl) : nullptr );
        else
            mpFS->startElementNS( XML_a, XML_pPr,
                               XML_lvl, nLevel > 0 ? OString::number(nLevel).getStr() : nullptr,
                               XML_marL, nLeftMargin > 0 ? OString::number(oox::drawingml::convertHmmToEmu(nLeftMargin)).getStr() : nullptr,
                               XML_indent, nLineIndentation ? OString::number(oox::drawingml::convertHmmToEmu(nLineIndentation)).getStr() : nullptr,
                               XML_algn, GetAlignment( nAlignment ),
                               XML_rtl, bRtl ? ToPsz10(bRtl) : nullptr );


        if( bHasLinespacing )
        {
            mpFS->startElementNS(XML_a, XML_lnSpc);
            WriteLinespacing( aLineSpacing );
            mpFS->endElementNS( XML_a, XML_lnSpc );
        }

        if( nParaTopMargin != 0 )
        {
            mpFS->startElementNS(XML_a, XML_spcBef);
            {
                mpFS->singleElementNS( XML_a, XML_spcPts,
                                       XML_val, OString::number(std::lround(nParaTopMargin / 25.4 * 72)));
            }
            mpFS->endElementNS( XML_a, XML_spcBef );
        }

        if( nParaBottomMargin != 0 )
        {
            mpFS->startElementNS(XML_a, XML_spcAft);
            {
                mpFS->singleElementNS( XML_a, XML_spcPts,
                                       XML_val, OString::number(std::lround(nParaBottomMargin / 25.4 * 72)));
            }
            mpFS->endElementNS( XML_a, XML_spcAft );
        }

        WriteParagraphNumbering( rXPropSet, fFirstCharHeight, nLevel );

        mpFS->endElementNS( XML_a, XML_pPr );
    }
}

void DrawingML::WriteParagraph( const Reference< XTextContent >& rParagraph,
                                bool& rbOverridingCharHeight, sal_Int32& rnCharHeight )
{
    Reference< XEnumerationAccess > access( rParagraph, UNO_QUERY );
    if( !access.is() )
        return;

    Reference< XEnumeration > enumeration( access->createEnumeration() );
    if( !enumeration.is() )
        return;

    mpFS->startElementNS(XML_a, XML_p);

    bool bPropertiesWritten = false;
    while( enumeration->hasMoreElements() )
    {
        Reference< XTextRange > run;
        Any any ( enumeration->nextElement() );

        if (any >>= run)
        {
            if( !bPropertiesWritten )
            {
                float fFirstCharHeight = rnCharHeight / 1000.;
                Reference< XPropertySet > xFirstRunPropSet (run, UNO_QUERY);
                Reference< XPropertySetInfo > xFirstRunPropSetInfo = xFirstRunPropSet->getPropertySetInfo();
                if( xFirstRunPropSetInfo->hasPropertyByName("CharHeight") )
                    fFirstCharHeight = xFirstRunPropSet->getPropertyValue("CharHeight").get<float>();
                WriteParagraphProperties( rParagraph, fFirstCharHeight );
                bPropertiesWritten = true;
            }
            WriteRun( run, rbOverridingCharHeight, rnCharHeight );
        }
    }
    Reference< XPropertySet > rXPropSet( rParagraph, UNO_QUERY );
    WriteRunProperties( rXPropSet, false, XML_endParaRPr, false, rbOverridingCharHeight, rnCharHeight );

    mpFS->endElementNS( XML_a, XML_p );
}

void DrawingML::WriteText( const Reference< XInterface >& rXIface, const OUString& presetWarp, bool bBodyPr, bool bText, sal_Int32 nXmlNamespace )
{
    Reference< XText > xXText( rXIface, UNO_QUERY );
    Reference< XPropertySet > rXPropSet( rXIface, UNO_QUERY );

    if( !xXText.is() )
        return;

    sal_Int32 nTextRotateAngle = 0;
    bool bIsFontworkShape(presetWarp.startsWith("text") && (presetWarp != "textNoShape"));

#define DEFLRINS 254
#define DEFTBINS 127
    sal_Int32 nLeft, nRight, nTop, nBottom;
    nLeft = nRight = DEFLRINS;
    nTop = nBottom = DEFTBINS;

    // top inset looks a bit different compared to ppt export
    // check if something related doesn't work as expected
    if (GetProperty(rXPropSet, "TextLeftDistance"))
        mAny >>= nLeft;
    if (GetProperty(rXPropSet, "TextRightDistance"))
        mAny >>= nRight;
    if (GetProperty(rXPropSet, "TextUpperDistance"))
        mAny >>= nTop;
    if (GetProperty(rXPropSet, "TextLowerDistance"))
        mAny >>= nBottom;

    TextVerticalAdjust eVerticalAlignment( TextVerticalAdjust_TOP );
    const char* sVerticalAlignment = nullptr;
    if (GetProperty(rXPropSet, "TextVerticalAdjust"))
        mAny >>= eVerticalAlignment;
    if( eVerticalAlignment != TextVerticalAdjust_TOP )
        sVerticalAlignment = GetTextVerticalAdjust(eVerticalAlignment);

    const char* sWritingMode = nullptr;
    bool bVertical = false;
    if (GetProperty(rXPropSet, "TextWritingMode"))
    {
        WritingMode eMode;

        if( ( mAny >>= eMode ) && eMode == WritingMode_TB_RL )
        {
            sWritingMode = "vert";
            bVertical = true;
        }
    }

    Sequence<drawing::EnhancedCustomShapeAdjustmentValue> aAdjustmentSeq;
    uno::Sequence<beans::PropertyValue> aTextPathSeq;
    bool bScaleX(false);

    if (GetProperty(rXPropSet, "CustomShapeGeometry"))
    {
        Sequence< PropertyValue > aProps;
        if ( mAny >>= aProps )
        {
            for ( sal_Int32 i = 0, nElems = aProps.getLength(); i < nElems; ++i )
            {
                if ( aProps[ i ].Name == "TextPreRotateAngle" && ( aProps[ i ].Value >>= nTextRotateAngle ) )
                {
                    if ( nTextRotateAngle == -90 )
                    {
                        sWritingMode = "vert";
                        bVertical = true;
                    }
                    else if ( nTextRotateAngle == -270 )
                    {
                        sWritingMode = "vert270";
                        bVertical = true;
                    }
                    if (!bIsFontworkShape)
                        break;
                }
                else if (aProps[i].Name == "AdjustmentValues")
                    aProps[i].Value >>= aAdjustmentSeq;
                else if (aProps[i].Name == "TextPath")
                {
                    aProps[i].Value >>= aTextPathSeq;
                    for (int k = 0; k < aTextPathSeq.getLength(); k++)
                    {
                        if (aTextPathSeq[k].Name == "ScaleX")
                            aTextPathSeq[k].Value >>= bScaleX;
                    }
                }
            }
        }
    }

    bool bFromWordArt = !bScaleX
                        && ( presetWarp == "textArchDown" || presetWarp == "textArchUp"
                            || presetWarp == "textButton" || presetWarp == "textCircle");

    TextHorizontalAdjust eHorizontalAlignment( TextHorizontalAdjust_CENTER );
    bool bHorizontalCenter = false;
    if (GetProperty(rXPropSet, "TextHorizontalAdjust"))
        mAny >>= eHorizontalAlignment;
    if( eHorizontalAlignment == TextHorizontalAdjust_CENTER )
        bHorizontalCenter = true;
    else if( bVertical && eHorizontalAlignment == TextHorizontalAdjust_LEFT )
        sVerticalAlignment = "b";

    bool bHasWrap = false;
    bool bWrap = false;
    // Only custom shapes obey the TextWordWrap option, normal text always wraps.
    if (dynamic_cast<SvxCustomShape*>(rXIface.get()) && GetProperty(rXPropSet, "TextWordWrap"))
    {
        mAny >>= bWrap;
        bHasWrap = true;
    }

    if (bBodyPr)
    {
        const char* pWrap = bHasWrap && !bWrap ? "none" : nullptr;
        if (GetDocumentType() == DOCUMENT_DOCX)
        {
            // In case of DOCX, if we want to have the same effect as
            // TextShape's automatic word wrapping, then we need to set
            // wrapping to square.
            uno::Reference<lang::XServiceInfo> xServiceInfo(rXIface, uno::UNO_QUERY);
            if (xServiceInfo.is() && xServiceInfo->supportsService("com.sun.star.drawing.TextShape"))
                pWrap = "square";
        }
        mpFS->startElementNS( (nXmlNamespace ? nXmlNamespace : XML_a), XML_bodyPr,
                               XML_wrap, pWrap,
                               XML_fromWordArt, bFromWordArt ? "1" : nullptr,
                               XML_lIns, (nLeft != DEFLRINS) ? OString::number(oox::drawingml::convertHmmToEmu(nLeft)).getStr() : nullptr,
                               XML_rIns, (nRight != DEFLRINS) ? OString::number(oox::drawingml::convertHmmToEmu(nRight)).getStr() : nullptr,
                               XML_tIns, (nTop != DEFTBINS) ? OString::number(oox::drawingml::convertHmmToEmu(nTop)).getStr() : nullptr,
                               XML_bIns, (nBottom != DEFTBINS) ? OString::number(oox::drawingml::convertHmmToEmu(nBottom)).getStr() : nullptr,
                               XML_anchor, sVerticalAlignment,
                               XML_anchorCtr, bHorizontalCenter ? "1" : nullptr,
                               XML_vert, sWritingMode,
                               XML_rot, (nTextRotateAngle != 0) ? oox::drawingml::calcRotationValue( nTextRotateAngle * 100 ).getStr() : nullptr );
        if (bIsFontworkShape)
        {
            if (aAdjustmentSeq.getLength() > 0)
            {
                mpFS->startElementNS(XML_a, XML_prstTxWarp, XML_prst, presetWarp.toUtf8());
                mpFS->startElementNS(XML_a, XML_avLst);
                for (sal_Int32 i = 0, nElems = aAdjustmentSeq.getLength(); i < nElems; ++i )
                {
                    OString sName = OString("adj") + (( nElems > 1 ) ? OString::number(i + 1) : OString());
                    double fValue(0.0);
                    if (aAdjustmentSeq[i].Value.getValueTypeClass() == TypeClass_DOUBLE)
                        aAdjustmentSeq[i].Value >>= fValue;
                    else
                    {
                        sal_Int32 nNumber(0);
                        aAdjustmentSeq[i].Value >>= nNumber;
                        fValue = static_cast<double>(nNumber);
                    }
                    // Convert from binary coordinate system with viewBox "0 0 21600 21600" and simple degree
                    // to DrawingML with coordinate range 0..100000 and angle in 1/60000 degree.
                    // Reverse to conversion in lcl_createPresetShape in drawingml/shape.cxx on import.
                    if (presetWarp == "textArchDown" || presetWarp == "textArchUp"
                        || presetWarp == "textButton" || presetWarp == "textCircle"
                        || ((i == 0) && (presetWarp == "textArchDownPour" || presetWarp == "textArchUpPour"
                        || presetWarp == "textButtonPour" || presetWarp == "textCirclePour")))
                    {
                        fValue *= 60000.0;
                    }
                    else if ((i == 1) && (presetWarp == "textDoubleWave1" || presetWarp == "textWave1"
                            || presetWarp == "textWave2" || presetWarp == "textWave4"))
                    {
                        fValue = fValue / 0.216 - 50000.0;
                    }
                    else if ((i == 1) && (presetWarp == "textArchDownPour" || presetWarp == "textArchUpPour"
                        || presetWarp == "textButtonPour" || presetWarp == "textCirclePour"))
                    {
                        fValue /= 0.108;
                    }
                    else
                    {
                        fValue /= 0.216;
                    }
                    OString sFmla = OString("val ") + OString::number(std::lround(fValue));
                    mpFS->singleElementNS(XML_a, XML_gd, XML_name, sName, XML_fmla, sFmla);
                }
                mpFS->endElementNS( XML_a, XML_avLst );
                mpFS->endElementNS(XML_a, XML_prstTxWarp);
            }
            else
            {
                mpFS->singleElementNS(XML_a, XML_prstTxWarp, XML_prst, presetWarp.toUtf8());
            }
        }

        if (GetDocumentType() == DOCUMENT_DOCX || GetDocumentType() == DOCUMENT_XLSX)
        {
            bool bTextAutoGrowHeight = false;
            if (GetProperty(rXPropSet, "TextAutoGrowHeight"))
                mAny >>= bTextAutoGrowHeight;
            mpFS->singleElementNS(XML_a, (bTextAutoGrowHeight ? XML_spAutoFit : XML_noAutofit));
        }
        if (GetDocumentType() == DOCUMENT_PPTX)
        {
            TextFitToSizeType eFit = TextFitToSizeType_NONE;
            if (GetProperty(rXPropSet, "TextFitToSize"))
                mAny >>= eFit;

            if (eFit == TextFitToSizeType_AUTOFIT)
            {
                const sal_Int32 MAX_SCALE_VAL = 100000;
                sal_Int32 nFontScale = MAX_SCALE_VAL;
                SvxShapeText* pTextShape = dynamic_cast<SvxShapeText*>(rXIface.get());
                if (pTextShape)
                {
                    SdrTextObj* pTextObject = dynamic_cast<SdrTextObj*>(pTextShape->GetSdrObject());
                    if (pTextObject)
                    {
                        double fScaleY = pTextObject->GetFontScaleY();
                        nFontScale = static_cast<sal_uInt32>(fScaleY * 100) * 1000;
                    }
                }

                mpFS->singleElementNS(XML_a, XML_normAutofit, XML_fontScale,
                    ( nFontScale < MAX_SCALE_VAL && nFontScale > 0 ) ? OString::number(nFontScale).getStr() : nullptr);
            }
            else
            {
                bool bTextAutoGrowHeight = false;
                if (GetProperty(rXPropSet, "TextAutoGrowHeight"))
                    mAny >>= bTextAutoGrowHeight;
                mpFS->singleElementNS(XML_a, (bTextAutoGrowHeight ? XML_spAutoFit : XML_noAutofit));
            }
        }
        mpFS->endElementNS((nXmlNamespace ? nXmlNamespace : XML_a), XML_bodyPr);
    }

    Reference< XEnumerationAccess > access( xXText, UNO_QUERY );
    if( !access.is() || !bText )
        return;

    Reference< XEnumeration > enumeration( access->createEnumeration() );
    if( !enumeration.is() )
        return;

    uno::Reference<drawing::XShape> xShape(rXIface, uno::UNO_QUERY);
    SdrObject* pSdrObject = xShape.is() ? GetSdrObjectFromXShape(xShape) : nullptr;
    const SdrTextObj* pTxtObj = dynamic_cast<SdrTextObj*>( pSdrObject );
    if (pTxtObj && mpTextExport)
    {
        const OutlinerParaObject* pParaObj = nullptr;
        bool bOwnParaObj = false;

        /*
        #i13885#
        When the object is actively being edited, that text is not set into
        the objects normal text object, but lives in a separate object.
        */
        if (pTxtObj->IsTextEditActive())
        {
            pParaObj = pTxtObj->GetEditOutlinerParaObject().release();
            bOwnParaObj = true;
        }
        else
            pParaObj = pTxtObj->GetOutlinerParaObject();

        if (pParaObj)
        {
            // this is reached only in case some text is attached to the shape
            mpTextExport->WriteOutliner(*pParaObj);
            if (bOwnParaObj)
                delete pParaObj;
        }
        return;
    }

    bool bOverridingCharHeight = false;
    sal_Int32 nCharHeight = -1;

    while( enumeration->hasMoreElements() )
    {
        Reference< XTextContent > paragraph;
        Any any ( enumeration->nextElement() );

        if( any >>= paragraph)
            WriteParagraph( paragraph, bOverridingCharHeight, nCharHeight );
    }
}

void DrawingML::WritePresetShape( const char* pShape , std::vector< std::pair<sal_Int32,sal_Int32>> & rAvList )
{
    mpFS->startElementNS(XML_a, XML_prstGeom, XML_prst, pShape);
    if ( !rAvList.empty() )
    {

        mpFS->startElementNS(XML_a, XML_avLst);
        for (auto const& elem : rAvList)
        {
            OString sName = OString("adj") + ( ( elem.first > 0 ) ? OString::number(elem.first) : OString() );
            OString sFmla = OString("val ") + OString::number( elem.second );

            mpFS->singleElementNS(XML_a, XML_gd, XML_name, sName, XML_fmla, sFmla);
        }
        mpFS->endElementNS( XML_a, XML_avLst );
    }
    else
        mpFS->singleElementNS(XML_a, XML_avLst);

    mpFS->endElementNS(  XML_a, XML_prstGeom );
}

void DrawingML::WritePresetShape( const char* pShape )
{
    mpFS->startElementNS(XML_a, XML_prstGeom, XML_prst, pShape);
    mpFS->singleElementNS(XML_a, XML_avLst);
    mpFS->endElementNS(  XML_a, XML_prstGeom );
}

static std::map< OString, std::vector<OString> > lcl_getAdjNames()
{
    std::map< OString, std::vector<OString> > aRet;

    OUString aPath("$BRAND_BASE_DIR/" LIBO_SHARE_FOLDER "/filter/oox-drawingml-adj-names");
    rtl::Bootstrap::expandMacros(aPath);
    SvFileStream aStream(aPath, StreamMode::READ);
    if (aStream.GetError() != ERRCODE_NONE)
        SAL_WARN("oox.shape", "failed to open oox-drawingml-adj-names");
    OString aLine;
    bool bNotDone = aStream.ReadLine(aLine);
    while (bNotDone)
    {
        sal_Int32 nIndex = 0;
        // Each line is in a "key\tvalue" format: read the key, the rest is the value.
        OString aKey = aLine.getToken(0, '\t', nIndex);
        OString aValue = aLine.copy(nIndex);
        aRet[aKey].push_back(aValue);
        bNotDone = aStream.ReadLine(aLine);
    }
    return aRet;
}

void DrawingML::WritePresetShape( const char* pShape, MSO_SPT eShapeType, bool bPredefinedHandlesUsed, const PropertyValue& rProp )
{
    static std::map< OString, std::vector<OString> > aAdjMap = lcl_getAdjNames();
    // If there are predefined adj names for this shape type, look them up now.
    std::vector<OString> aAdjustments;
    if (aAdjMap.find(OString(pShape)) != aAdjMap.end())
        aAdjustments = aAdjMap[OString(pShape)];

    mpFS->startElementNS(XML_a, XML_prstGeom, XML_prst, pShape);
    mpFS->startElementNS(XML_a, XML_avLst);

    Sequence< drawing::EnhancedCustomShapeAdjustmentValue > aAdjustmentSeq;
    if ( ( rProp.Value >>= aAdjustmentSeq )
         && eShapeType != mso_sptActionButtonForwardNext  // we have adjustments values for these type of shape, but MSO doesn't like them
         && eShapeType != mso_sptActionButtonBackPrevious // so they are now disabled
         && OString(pShape) != "rect" //some shape types are commented out in pCustomShapeTypeTranslationTable[] & are being defaulted to rect & rect does not have adjustment values/name.
        )
    {
        SAL_INFO("oox.shape", "adj seq len: " << aAdjustmentSeq.getLength());
        sal_Int32 nAdjustmentsWhichNeedsToBeConverted = 0;
        if ( bPredefinedHandlesUsed )
            EscherPropertyContainer::LookForPolarHandles( eShapeType, nAdjustmentsWhichNeedsToBeConverted );

        sal_Int32 nValue, nLength = aAdjustmentSeq.getLength();
        // aAdjustments will give info about the number of adj values for a particular geometry. For example for hexagon aAdjustments.size() will be 2 and for circular arrow it will be 5 as per lcl_getAdjNames.
        // Sometimes there are more values than needed, so we ignore the excessive ones.
        if (aAdjustments.size() <= static_cast<sal_uInt32>(nLength))
        {
            for (sal_Int32 i = 0; i < static_cast<sal_Int32>(aAdjustments.size()); i++)
            {
                if( EscherPropertyContainer::GetAdjustmentValue( aAdjustmentSeq[ i ], i, nAdjustmentsWhichNeedsToBeConverted, nValue ) )
                {
                    // If the document model doesn't have an adjustment name (e.g. shape was created from VML), then take it from the predefined list.
                    OString aAdjName = aAdjustmentSeq[i].Name.isEmpty()
                                           ? aAdjustments[i]
                                           : aAdjustmentSeq[i].Name.toUtf8();

                    mpFS->singleElementNS( XML_a, XML_gd,
                                       XML_name, aAdjName,
                                       XML_fmla, "val " + OString::number(nValue));
                }
            }
        }
    }

    mpFS->endElementNS( XML_a, XML_avLst );
    mpFS->endElementNS(  XML_a, XML_prstGeom );
}

bool DrawingML::WriteCustomGeometry(
    const Reference< XShape >& rXShape,
    const SdrObjCustomShape& rSdrObjCustomShape)
{
    uno::Reference< beans::XPropertySet > aXPropSet;
    uno::Any aAny( rXShape->queryInterface(cppu::UnoType<beans::XPropertySet>::get()));

    if ( ! (aAny >>= aXPropSet) )
        return false;

    try
    {
        aAny = aXPropSet->getPropertyValue( "CustomShapeGeometry" );
        if ( !aAny.hasValue() )
            return false;
    }
    catch( const ::uno::Exception& )
    {
        return false;
    }


    auto pGeometrySeq = o3tl::tryAccess<uno::Sequence<beans::PropertyValue>>(aAny);

    if ( pGeometrySeq )
    {
        for( int i = 0; i < pGeometrySeq->getLength(); ++i )
        {
            const beans::PropertyValue& rProp = (*pGeometrySeq)[ i ];
            if ( rProp.Name == "Path" )
            {
                uno::Sequence<beans::PropertyValue> aPathProp;
                rProp.Value >>= aPathProp;

                uno::Sequence<drawing::EnhancedCustomShapeParameterPair> aPairs;
                uno::Sequence<drawing::EnhancedCustomShapeSegment> aSegments;
                uno::Sequence<awt::Size> aPathSize;
                for (int j = 0; j < aPathProp.getLength(); ++j )
                {
                    const beans::PropertyValue& rPathProp = aPathProp[j];
                    if (rPathProp.Name == "Coordinates")
                        rPathProp.Value >>= aPairs;
                    else if (rPathProp.Name == "Segments")
                        rPathProp.Value >>= aSegments;
                    else if (rPathProp.Name == "SubViewSize")
                        rPathProp.Value >>= aPathSize;
                }

                if ( !aPairs.hasElements() )
                    return false;

                if ( !aSegments.hasElements() )
                {
                    aSegments = uno::Sequence<drawing::EnhancedCustomShapeSegment>(4);
                    aSegments[0].Count = 1;
                    aSegments[0].Command = drawing::EnhancedCustomShapeSegmentCommand::MOVETO;
                    aSegments[1].Count = static_cast<sal_Int16>(std::min( aPairs.getLength() - 1, sal_Int32(32767) ));
                    aSegments[1].Command = drawing::EnhancedCustomShapeSegmentCommand::LINETO;
                    aSegments[2].Count = 0;
                    aSegments[2].Command = drawing::EnhancedCustomShapeSegmentCommand::CLOSESUBPATH;
                    aSegments[3].Count = 0;
                    aSegments[3].Command = drawing::EnhancedCustomShapeSegmentCommand::ENDSUBPATH;
                }

                int nExpectedPairCount = 0;
                for( int j = 0; j < aSegments.getLength(); ++j )
                {
                    nExpectedPairCount += aSegments[j].Count;
                }

                if ( nExpectedPairCount > aPairs.getLength() )
                {
                    SAL_WARN("oox.shape", "Segments need " << nExpectedPairCount << " coordinates, but Coordinates have only " << aPairs.getLength() << " pairs.");
                    return false;
                }

                mpFS->startElementNS(XML_a, XML_custGeom);
                mpFS->singleElementNS(XML_a, XML_avLst);
                mpFS->singleElementNS(XML_a, XML_gdLst);
                mpFS->singleElementNS(XML_a, XML_ahLst);
                mpFS->singleElementNS(XML_a, XML_rect, XML_l, "l", XML_t, "t",
                                      XML_r, "r", XML_b, "b");
                mpFS->startElementNS(XML_a, XML_pathLst);

                if ( aPathSize.hasElements() )
                {
                    mpFS->startElementNS( XML_a, XML_path,
                          XML_w, OString::number(aPathSize[0].Width),
                          XML_h, OString::number(aPathSize[0].Height) );
                }
                else
                {
                    sal_Int32 nXMin(0);
                    aPairs[0].First.Value >>= nXMin;
                    sal_Int32 nXMax = nXMin;
                    sal_Int32 nYMin(0);
                    aPairs[0].Second.Value >>= nYMin;
                    sal_Int32 nYMax = nYMin;

                    for ( int j = 0; j < aPairs.getLength(); ++j )
                    {
                        sal_Int32 nX = GetCustomGeometryPointValue(aPairs[j].First, rSdrObjCustomShape);
                        sal_Int32 nY = GetCustomGeometryPointValue(aPairs[j].Second, rSdrObjCustomShape);
                        if (nX < nXMin)
                            nXMin = nX;
                        if (nY < nYMin)
                            nYMin = nY;
                        if (nX > nXMax)
                            nXMax = nX;
                        if (nY > nYMax)
                            nYMax = nY;
                    }
                    mpFS->startElementNS( XML_a, XML_path,
                          XML_w, OString::number(nXMax - nXMin),
                          XML_h, OString::number(nYMax - nYMin) );
                }


                int nPairIndex = 0;
                bool bOK = true;
                for (int j = 0; j < aSegments.getLength() && bOK; ++j)
                {
                    if ( aSegments[ j ].Command == drawing::EnhancedCustomShapeSegmentCommand::CLOSESUBPATH )
                    {
                        mpFS->singleElementNS(XML_a, XML_close);
                    }
                    for (int k = 0; k < aSegments[j].Count && bOK; ++k)
                    {
                        switch( aSegments[ j ].Command )
                        {
                            case drawing::EnhancedCustomShapeSegmentCommand::MOVETO :
                            {
                                if (nPairIndex >= aPairs.getLength())
                                    bOK = false;
                                else
                                {
                                    mpFS->startElementNS(XML_a, XML_moveTo);
                                    WriteCustomGeometryPoint(aPairs[nPairIndex], rSdrObjCustomShape);
                                    mpFS->endElementNS( XML_a, XML_moveTo );
                                    nPairIndex++;
                                }
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::LINETO :
                            {
                                if (nPairIndex >= aPairs.getLength())
                                    bOK = false;
                                else
                                {
                                    mpFS->startElementNS(XML_a, XML_lnTo);
                                    WriteCustomGeometryPoint(aPairs[nPairIndex], rSdrObjCustomShape);
                                    mpFS->endElementNS( XML_a, XML_lnTo );
                                    nPairIndex++;
                                }
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::CURVETO :
                            {
                                if (nPairIndex + 2 >= aPairs.getLength())
                                    bOK = false;
                                else
                                {
                                    mpFS->startElementNS(XML_a, XML_cubicBezTo);
                                    for( sal_uInt8 l = 0; l <= 2; ++l )
                                    {
                                        WriteCustomGeometryPoint(aPairs[nPairIndex+l], rSdrObjCustomShape);
                                    }
                                    mpFS->endElementNS( XML_a, XML_cubicBezTo );
                                    nPairIndex += 3;
                                }
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::ANGLEELLIPSETO :
                            case drawing::EnhancedCustomShapeSegmentCommand::ANGLEELLIPSE :
                            {
                                nPairIndex += 3;
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::ARCTO :
                            case drawing::EnhancedCustomShapeSegmentCommand::ARC :
                            case drawing::EnhancedCustomShapeSegmentCommand::CLOCKWISEARCTO :
                            case drawing::EnhancedCustomShapeSegmentCommand::CLOCKWISEARC :
                            {
                                nPairIndex += 4;
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::ELLIPTICALQUADRANTX :
                            case drawing::EnhancedCustomShapeSegmentCommand::ELLIPTICALQUADRANTY :
                            {
                                nPairIndex++;
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::QUADRATICCURVETO :
                            {
                                if (nPairIndex + 1 >= aPairs.getLength())
                                    bOK = false;
                                else
                                {
                                    mpFS->startElementNS(XML_a, XML_quadBezTo);
                                    for( sal_uInt8 l = 0; l < 2; ++l )
                                    {
                                        WriteCustomGeometryPoint(aPairs[nPairIndex+l], rSdrObjCustomShape);
                                    }
                                    mpFS->endElementNS( XML_a, XML_quadBezTo );
                                    nPairIndex += 2;
                                }
                                break;
                            }
                            case drawing::EnhancedCustomShapeSegmentCommand::ARCANGLETO :
                            {
                                nPairIndex += 2;
                                break;
                            }
                            default:
                                // do nothing
                                break;
                        }
                    }
                }
                mpFS->endElementNS( XML_a, XML_path );
                mpFS->endElementNS( XML_a, XML_pathLst );
                mpFS->endElementNS( XML_a, XML_custGeom );
                return bOK;
            }
        }
    }
    return false;
}

void DrawingML::WriteCustomGeometryPoint(
    const drawing::EnhancedCustomShapeParameterPair& rParamPair,
    const SdrObjCustomShape& rSdrObjCustomShape)
{
    sal_Int32 nX = GetCustomGeometryPointValue(rParamPair.First, rSdrObjCustomShape);
    sal_Int32 nY = GetCustomGeometryPointValue(rParamPair.Second, rSdrObjCustomShape);

    mpFS->singleElementNS(XML_a, XML_pt, XML_x, OString::number(nX), XML_y, OString::number(nY));
}

sal_Int32 DrawingML::GetCustomGeometryPointValue(
    const css::drawing::EnhancedCustomShapeParameter& rParam,
    const SdrObjCustomShape& rSdrObjCustomShape)
{
    const EnhancedCustomShape2d aCustoShape2d(const_cast< SdrObjCustomShape& >(rSdrObjCustomShape));
    double fValue = 0.0;
    aCustoShape2d.GetParameter(fValue, rParam, false, false);
    sal_Int32 nValue(std::lround(fValue));

    return nValue;
}

void DrawingML::WritePolyPolygon( const tools::PolyPolygon& rPolyPolygon, const bool bClosed )
{
    // In case of Writer, the parent element is <wps:spPr>, and there the
    // <a:custGeom> element is not optional.
    if (rPolyPolygon.Count() < 1 && GetDocumentType() != DOCUMENT_DOCX)
        return;

    mpFS->startElementNS(XML_a, XML_custGeom);
    mpFS->singleElementNS(XML_a, XML_avLst);
    mpFS->singleElementNS(XML_a, XML_gdLst);
    mpFS->singleElementNS(XML_a, XML_ahLst);
    mpFS->singleElementNS(XML_a, XML_rect, XML_l, "0", XML_t, "0", XML_r, "r", XML_b, "b");

    mpFS->startElementNS( XML_a, XML_pathLst );

    const tools::Rectangle aRect( rPolyPolygon.GetBoundRect() );

    // Put all polygons of rPolyPolygon in the same path element
    // to subtract the overlapped areas.
    mpFS->startElementNS( XML_a, XML_path,
            XML_w, OString::number(aRect.GetWidth()),
            XML_h, OString::number(aRect.GetHeight()) );

    for( sal_uInt16 i = 0; i < rPolyPolygon.Count(); i ++ )
    {

        const tools::Polygon& rPoly = rPolyPolygon[ i ];

        if( rPoly.GetSize() > 0 )
        {
            mpFS->startElementNS(XML_a, XML_moveTo);

            mpFS->singleElementNS( XML_a, XML_pt,
                                   XML_x, OString::number(rPoly[0].X() - aRect.Left()),
                                   XML_y, OString::number(rPoly[0].Y() - aRect.Top()) );

            mpFS->endElementNS( XML_a, XML_moveTo );
        }

        for( sal_uInt16 j = 1; j < rPoly.GetSize(); j ++ )
        {
            PolyFlags flags = rPoly.GetFlags(j);
            if( flags == PolyFlags::Control )
            {
                // a:cubicBezTo can only contain 3 a:pt elements, so we need to make sure of this
                if( j+2 < rPoly.GetSize() && rPoly.GetFlags(j+1) == PolyFlags::Control && rPoly.GetFlags(j+2) != PolyFlags::Control )
                {

                    mpFS->startElementNS(XML_a, XML_cubicBezTo);
                    for( sal_uInt8 k = 0; k <= 2; ++k )
                    {
                        mpFS->singleElementNS(XML_a, XML_pt,
                                              XML_x, OString::number(rPoly[j+k].X() - aRect.Left()),
                                              XML_y, OString::number(rPoly[j+k].Y() - aRect.Top()));

                    }
                    mpFS->endElementNS( XML_a, XML_cubicBezTo );
                    j += 2;
                }
            }
            else if( flags == PolyFlags::Normal )
            {
                mpFS->startElementNS(XML_a, XML_lnTo);
                mpFS->singleElementNS( XML_a, XML_pt,
                                       XML_x, OString::number(rPoly[j].X() - aRect.Left()),
                                       XML_y, OString::number(rPoly[j].Y() - aRect.Top()) );
                mpFS->endElementNS( XML_a, XML_lnTo );
            }
        }
    }
    if (bClosed)
        mpFS->singleElementNS( XML_a, XML_close);
    mpFS->endElementNS( XML_a, XML_path );

    mpFS->endElementNS( XML_a, XML_pathLst );

    mpFS->endElementNS( XML_a, XML_custGeom );
}

void DrawingML::WriteConnectorConnections( EscherConnectorListEntry& rConnectorEntry, sal_Int32 nStartID, sal_Int32 nEndID )
{
    if( nStartID != -1 )
    {
        mpFS->singleElementNS( XML_a, XML_stCxn,
                               XML_id, OString::number(nStartID),
                               XML_idx, OString::number(rConnectorEntry.GetConnectorRule(true)) );
    }
    if( nEndID != -1 )
    {
        mpFS->singleElementNS( XML_a, XML_endCxn,
                               XML_id, OString::number(nEndID),
                               XML_idx, OString::number(rConnectorEntry.GetConnectorRule(false)) );
    }
}

sal_Unicode DrawingML::SubstituteBullet( sal_Unicode cBulletId, css::awt::FontDescriptor& rFontDesc )
{
    if ( IsStarSymbol(rFontDesc.Name) )
    {
        rtl_TextEncoding eCharSet = rFontDesc.CharSet;
        cBulletId = msfilter::util::bestFitOpenSymbolToMSFont(cBulletId, eCharSet, rFontDesc.Name);
        rFontDesc.CharSet = eCharSet;
    }

    return cBulletId;
}

sax_fastparser::FSHelperPtr DrawingML::CreateOutputStream (
    const OUString& sFullStream,
    const OUString& sRelativeStream,
    const Reference< XOutputStream >& xParentRelation,
    const char* sContentType,
    const char* sRelationshipType,
    OUString* pRelationshipId )
{
    OUString sRelationshipId;
    if (xParentRelation.is())
        sRelationshipId = GetFB()->addRelation( xParentRelation, OUString::createFromAscii( sRelationshipType), sRelativeStream );
    else
        sRelationshipId = GetFB()->addRelation( OUString::createFromAscii( sRelationshipType ), sRelativeStream );

    if( pRelationshipId )
        *pRelationshipId = sRelationshipId;

    sax_fastparser::FSHelperPtr p = GetFB()->openFragmentStreamWithSerializer( sFullStream, OUString::createFromAscii( sContentType ) );

    return p;
}

void DrawingML::WriteFill( const Reference< XPropertySet >& xPropSet )
{
    if ( !GetProperty( xPropSet, "FillStyle" ) )
        return;
    FillStyle aFillStyle( FillStyle_NONE );
    xPropSet->getPropertyValue( "FillStyle" ) >>= aFillStyle;

    if ( aFillStyle == FillStyle_SOLID && GetProperty( xPropSet, "FillTransparence" ) )
    {
        // map full transparent background to no fill
        sal_Int16 nVal = 0;
        xPropSet->getPropertyValue( "FillTransparence" ) >>= nVal;
        if ( nVal == 100 )
            aFillStyle = FillStyle_NONE;
    }

    switch( aFillStyle )
    {
    case FillStyle_SOLID :
        WriteSolidFill( xPropSet );
        break;
    case FillStyle_GRADIENT :
        WriteGradientFill( xPropSet );
        break;
    case FillStyle_BITMAP :
        WriteBlipFill( xPropSet, "FillBitmap" );
        break;
    case FillStyle_HATCH :
        WritePattFill( xPropSet );
        break;
    case FillStyle_NONE:
        mpFS->singleElementNS(XML_a, XML_noFill);
        break;
    default:
        ;
    }
}

void DrawingML::WriteStyleProperties( sal_Int32 nTokenId, const Sequence< PropertyValue >& aProperties )
{
    if( aProperties.hasElements() )
    {
        OUString sSchemeClr;
        sal_uInt32 nIdx = 0;
        Sequence< PropertyValue > aTransformations;
        for( sal_Int32 i=0; i < aProperties.getLength(); ++i)
        {
            if( aProperties[i].Name == "SchemeClr" )
                aProperties[i].Value >>= sSchemeClr;
            else if( aProperties[i].Name == "Idx" )
                aProperties[i].Value >>= nIdx;
            else if( aProperties[i].Name == "Transformations" )
                aProperties[i].Value >>= aTransformations;
        }
        mpFS->startElementNS(XML_a, nTokenId, XML_idx, OString::number(nIdx));
        WriteColor(sSchemeClr, aTransformations);
        mpFS->endElementNS( XML_a, nTokenId );
    }
    else
    {
        // write mock <a:*Ref> tag
        mpFS->singleElementNS(XML_a, nTokenId, XML_idx, OString::number(0));
    }
}

void DrawingML::WriteShapeStyle( const Reference< XPropertySet >& xPropSet )
{
    // check existence of the grab bag
    if ( !GetProperty( xPropSet, "InteropGrabBag" ) )
        return;

    // extract the relevant properties from the grab bag
    Sequence< PropertyValue > aGrabBag;
    Sequence< PropertyValue > aFillRefProperties, aLnRefProperties, aEffectRefProperties;
    mAny >>= aGrabBag;
    for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i)
    {
        if( aGrabBag[i].Name == "StyleFillRef" )
            aGrabBag[i].Value >>= aFillRefProperties;
        else if( aGrabBag[i].Name == "StyleLnRef" )
            aGrabBag[i].Value >>= aLnRefProperties;
        else if( aGrabBag[i].Name == "StyleEffectRef" )
            aGrabBag[i].Value >>= aEffectRefProperties;
    }

    WriteStyleProperties( XML_lnRef, aLnRefProperties );
    WriteStyleProperties( XML_fillRef, aFillRefProperties );
    WriteStyleProperties( XML_effectRef, aEffectRefProperties );

    // write mock <a:fontRef>
    mpFS->singleElementNS(XML_a, XML_fontRef, XML_idx, "minor");
}

void DrawingML::WriteShapeEffect( const OUString& sName, const Sequence< PropertyValue >& aEffectProps )
{
    if( !aEffectProps.hasElements() )
        return;

    // assign the proper tag and enable bContainsColor if necessary
    sal_Int32 nEffectToken = 0;
    bool bContainsColor = false;
    if( sName == "outerShdw" )
    {
        nEffectToken = FSNS( XML_a, XML_outerShdw );
        bContainsColor = true;
    }
    else if( sName == "innerShdw" )
    {
        nEffectToken = FSNS( XML_a, XML_innerShdw );
        bContainsColor = true;
    }
    else if( sName == "glow" )
    {
        nEffectToken = FSNS( XML_a, XML_glow );
        bContainsColor = true;
    }
    else if( sName == "softEdge" )
        nEffectToken = FSNS( XML_a, XML_softEdge );
    else if( sName == "reflection" )
        nEffectToken = FSNS( XML_a, XML_reflection );
    else if( sName == "blur" )
        nEffectToken = FSNS( XML_a, XML_blur );

    OUString sSchemeClr;
    ::Color nRgbClr;
    sal_Int32 nAlpha = MAX_PERCENT;
    Sequence< PropertyValue > aTransformations;
    sax_fastparser::FastAttributeList *aOuterShdwAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xOuterShdwAttrList( aOuterShdwAttrList );
    for( sal_Int32 i=0; i < aEffectProps.getLength(); ++i )
    {
        if( aEffectProps[i].Name == "Attribs" )
        {
            // read tag attributes
            uno::Sequence< beans::PropertyValue > aOuterShdwProps;
            aEffectProps[i].Value >>= aOuterShdwProps;
            for( sal_Int32 j=0; j < aOuterShdwProps.getLength(); ++j )
            {
                if( aOuterShdwProps[j].Name == "algn" )
                {
                    OUString sVal;
                    aOuterShdwProps[j].Value >>= sVal;
                    aOuterShdwAttrList->add( XML_algn, OUStringToOString( sVal, RTL_TEXTENCODING_UTF8 ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "blurRad" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_blurRad, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "dir" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_dir, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "dist" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_dist, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "kx" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_kx, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "ky" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_ky, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "rotWithShape" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_rotWithShape, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "sx" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_sx, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "sy" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_sy, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "rad" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_rad, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "endA" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_endA, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "endPos" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_endPos, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "fadeDir" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_fadeDir, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "stA" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_stA, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "stPos" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_stPos, OString::number( nVal ).getStr() );
                }
                else if( aOuterShdwProps[j].Name == "grow" )
                {
                    sal_Int32 nVal = 0;
                    aOuterShdwProps[j].Value >>= nVal;
                    aOuterShdwAttrList->add( XML_grow, OString::number( nVal ).getStr() );
                }
            }
        }
        else if(aEffectProps[i].Name == "RgbClr")
        {
            aEffectProps[i].Value >>= nRgbClr;
        }
        else if(aEffectProps[i].Name == "RgbClrTransparency")
        {
            sal_Int32 nTransparency;
            if (aEffectProps[i].Value >>= nTransparency)
                // Calculate alpha value (see oox/source/drawingml/color.cxx : getTransparency())
                nAlpha = MAX_PERCENT - ( PER_PERCENT * nTransparency );
        }
        else if(aEffectProps[i].Name == "SchemeClr")
        {
            aEffectProps[i].Value >>= sSchemeClr;
        }
        else if(aEffectProps[i].Name == "SchemeClrTransformations")
        {
            aEffectProps[i].Value >>= aTransformations;
        }
    }

    if( nEffectToken > 0 )
    {
        mpFS->startElement( nEffectToken, xOuterShdwAttrList );

        if( bContainsColor )
        {
            if( sSchemeClr.isEmpty() )
                WriteColor( nRgbClr, nAlpha );
            else
                WriteColor( sSchemeClr, aTransformations );
        }

        mpFS->endElement( nEffectToken );
    }
}

static sal_Int32 lcl_CalculateDist(const double dX, const double dY)
{
    return static_cast< sal_Int32 >(sqrt(dX*dX + dY*dY) * 360);
}

static sal_Int32 lcl_CalculateDir(const double dX, const double dY)
{
    return (static_cast< sal_Int32 >(basegfx::rad2deg(atan2(dY,dX)) * 60000) + 21600000) % 21600000;
}

void DrawingML::WriteShapeEffects( const Reference< XPropertySet >& rXPropSet )
{
    Sequence< PropertyValue > aGrabBag, aEffects, aOuterShdwProps;
    if( GetProperty( rXPropSet, "InteropGrabBag" ) )
    {
        mAny >>= aGrabBag;
        for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i )
        {
            if( aGrabBag[i].Name == "EffectProperties" )
            {
                aGrabBag[i].Value >>= aEffects;
                for( sal_Int32 j=0; j < aEffects.getLength(); ++j )
                {
                    if( aEffects[j].Name == "outerShdw" )
                    {
                        aEffects[j].Value >>= aOuterShdwProps;
                        break;
                    }
                }
                break;
            }
        }
    }

    if( !aEffects.hasElements() )
    {
        bool bHasShadow = false;
        if( GetProperty( rXPropSet, "Shadow" ) )
            mAny >>= bHasShadow;
        if( bHasShadow )
        {
            Sequence< PropertyValue > aShadowGrabBag( 3 );
            Sequence< PropertyValue > aShadowAttribsGrabBag( 2 );

            double dX = +0.0, dY = +0.0;
            rXPropSet->getPropertyValue( "ShadowXDistance" ) >>= dX;
            rXPropSet->getPropertyValue( "ShadowYDistance" ) >>= dY;

            aShadowAttribsGrabBag[0].Name = "dist";
            aShadowAttribsGrabBag[0].Value <<= lcl_CalculateDist(dX, dY);
            aShadowAttribsGrabBag[1].Name = "dir";
            aShadowAttribsGrabBag[1].Value <<= lcl_CalculateDir(dX, dY);

            aShadowGrabBag[0].Name = "Attribs";
            aShadowGrabBag[0].Value <<= aShadowAttribsGrabBag;
            aShadowGrabBag[1].Name = "RgbClr";
            aShadowGrabBag[1].Value = rXPropSet->getPropertyValue( "ShadowColor" );
            aShadowGrabBag[2].Name = "RgbClrTransparency";
            aShadowGrabBag[2].Value = rXPropSet->getPropertyValue( "ShadowTransparence" );

            mpFS->startElementNS(XML_a, XML_effectLst);
            WriteShapeEffect( "outerShdw", aShadowGrabBag );
            mpFS->endElementNS(XML_a, XML_effectLst);
        }
    }
    else
    {
        for( sal_Int32 i=0; i < aOuterShdwProps.getLength(); ++i )
        {
            if( aOuterShdwProps[i].Name == "Attribs" )
            {
                Sequence< PropertyValue > aAttribsProps;
                aOuterShdwProps[i].Value >>= aAttribsProps;

                double dX = +0.0, dY = +0.0;
                rXPropSet->getPropertyValue( "ShadowXDistance" ) >>= dX;
                rXPropSet->getPropertyValue( "ShadowYDistance" ) >>= dY;

                for( sal_Int32 j=0; j < aAttribsProps.getLength(); ++j )
                {
                    if( aAttribsProps[j].Name == "dist" )
                    {
                        aAttribsProps[j].Value <<= lcl_CalculateDist(dX, dY);
                    }
                    else if( aAttribsProps[j].Name == "dir" )
                    {
                        aAttribsProps[j].Value <<= lcl_CalculateDir(dX, dY);
                    }
                }

                aOuterShdwProps[i].Value <<= aAttribsProps;
            }
            else if( aOuterShdwProps[i].Name == "RgbClr" )
            {
                aOuterShdwProps[i].Value = rXPropSet->getPropertyValue( "ShadowColor" );
            }
            else if( aOuterShdwProps[i].Name == "RgbClrTransparency" )
            {
                aOuterShdwProps[i].Value = rXPropSet->getPropertyValue( "ShadowTransparence" );
            }
        }

        mpFS->startElementNS(XML_a, XML_effectLst);
        for( sal_Int32 i=0; i < aEffects.getLength(); ++i )
        {
            if( aEffects[i].Name == "outerShdw" )
            {
                WriteShapeEffect( aEffects[i].Name, aOuterShdwProps );
            }
            else
            {
                Sequence< PropertyValue > aEffectProps;
                aEffects[i].Value >>= aEffectProps;
                WriteShapeEffect( aEffects[i].Name, aEffectProps );
            }
        }
        mpFS->endElementNS(XML_a, XML_effectLst);
    }
}

void DrawingML::WriteShape3DEffects( const Reference< XPropertySet >& xPropSet )
{
    // check existence of the grab bag
    if( !GetProperty( xPropSet, "InteropGrabBag" ) )
        return;

    // extract the relevant properties from the grab bag
    Sequence< PropertyValue > aGrabBag, aEffectProps, aLightRigProps, aShape3DProps;
    mAny >>= aGrabBag;
    for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i )
    {
        if( aGrabBag[i].Name == "3DEffectProperties" )
        {
            Sequence< PropertyValue > a3DEffectProps;
            aGrabBag[i].Value >>= a3DEffectProps;
            for( sal_Int32 j=0; j < a3DEffectProps.getLength(); ++j )
            {
                if( a3DEffectProps[j].Name == "Camera" )
                    a3DEffectProps[j].Value >>= aEffectProps;
                else if( a3DEffectProps[j].Name == "LightRig" )
                    a3DEffectProps[j].Value >>= aLightRigProps;
                else if( a3DEffectProps[j].Name == "Shape3D" )
                    a3DEffectProps[j].Value >>= aShape3DProps;
            }
            break;
        }
    }
    if( !aEffectProps.hasElements() && !aLightRigProps.hasElements() && !aShape3DProps.hasElements() )
        return;

    bool bCameraRotationPresent = false;
    sax_fastparser::FastAttributeList *aCameraAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xCameraAttrList( aCameraAttrList );
    sax_fastparser::FastAttributeList *aCameraRotationAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xRotAttrList( aCameraRotationAttrList );
    for( sal_Int32 i=0; i < aEffectProps.getLength(); ++i )
    {
        if( aEffectProps[i].Name == "prst" )
        {
            OUString sVal;
            aEffectProps[i].Value >>= sVal;
            aCameraAttrList->add( XML_prst, OUStringToOString( sVal, RTL_TEXTENCODING_UTF8 ).getStr() );
        }
        else if( aEffectProps[i].Name == "fov" )
        {
            float fVal = 0;
            aEffectProps[i].Value >>= fVal;
            aCameraAttrList->add( XML_fov, OString::number( fVal * 60000 ).getStr() );
        }
        else if( aEffectProps[i].Name == "zoom" )
        {
            float fVal = 1;
            aEffectProps[i].Value >>= fVal;
            aCameraAttrList->add( XML_zoom, OString::number( fVal * 100000 ).getStr() );
        }
        else if( aEffectProps[i].Name == "rotLat" ||
                aEffectProps[i].Name == "rotLon" ||
                aEffectProps[i].Name == "rotRev" )
        {
            sal_Int32 nVal = 0, nToken = XML_none;
            aEffectProps[i].Value >>= nVal;
            if( aEffectProps[i].Name == "rotLat" )
                nToken = XML_lat;
            else if( aEffectProps[i].Name == "rotLon" )
                nToken = XML_lon;
            else if( aEffectProps[i].Name == "rotRev" )
                nToken = XML_rev;
            aCameraRotationAttrList->add( nToken, OString::number( nVal ).getStr() );
            bCameraRotationPresent = true;
        }
    }

    bool bLightRigRotationPresent = false;
    sax_fastparser::FastAttributeList *aLightRigAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xLightAttrList( aLightRigAttrList );
    sax_fastparser::FastAttributeList *aLightRigRotationAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xLightRotAttrList( aLightRigRotationAttrList );
    for( sal_Int32 i=0; i < aLightRigProps.getLength(); ++i )
    {
        if( aLightRigProps[i].Name == "rig" || aLightRigProps[i].Name == "dir" )
        {
            OUString sVal;
            sal_Int32 nToken = XML_none;
            aLightRigProps[i].Value >>= sVal;
            if( aLightRigProps[i].Name == "rig" )
                nToken = XML_rig;
            else if( aLightRigProps[i].Name == "dir" )
                nToken = XML_dir;
            aLightRigAttrList->add( nToken, OUStringToOString( sVal, RTL_TEXTENCODING_UTF8 ).getStr() );
        }
        else if( aLightRigProps[i].Name == "rotLat" ||
                aLightRigProps[i].Name == "rotLon" ||
                aLightRigProps[i].Name == "rotRev" )
        {
            sal_Int32 nVal = 0, nToken = XML_none;
            aLightRigProps[i].Value >>= nVal;
            if( aLightRigProps[i].Name == "rotLat" )
                nToken = XML_lat;
            else if( aLightRigProps[i].Name == "rotLon" )
                nToken = XML_lon;
            else if( aLightRigProps[i].Name == "rotRev" )
                nToken = XML_rev;
            aLightRigRotationAttrList->add( nToken, OString::number( nVal ).getStr() );
            bLightRigRotationPresent = true;
        }
    }

    mpFS->startElementNS(XML_a, XML_scene3d);

    if( aEffectProps.hasElements() )
    {
        mpFS->startElementNS( XML_a, XML_camera, xCameraAttrList );
        if( bCameraRotationPresent )
        {
            mpFS->singleElementNS( XML_a, XML_rot, xRotAttrList );
        }
        mpFS->endElementNS( XML_a, XML_camera );
    }
    else
    {
        // a:camera with Word default values - Word won't open the document if this is not present
        mpFS->singleElementNS(XML_a, XML_camera, XML_prst, "orthographicFront");
    }

    if( aEffectProps.hasElements() )
    {
        mpFS->startElementNS( XML_a, XML_lightRig, xLightAttrList );
        if( bLightRigRotationPresent )
        {
            mpFS->singleElementNS( XML_a, XML_rot, xLightRotAttrList );
        }
        mpFS->endElementNS( XML_a, XML_lightRig );
    }
    else
    {
        // a:lightRig with Word default values - Word won't open the document if this is not present
        mpFS->singleElementNS(XML_a, XML_lightRig, XML_rig, "threePt", XML_dir, "t");
    }

    mpFS->endElementNS( XML_a, XML_scene3d );

    if( !aShape3DProps.hasElements() )
        return;

    bool bBevelTPresent = false, bBevelBPresent = false;
    Sequence< PropertyValue > aExtrusionColorProps, aContourColorProps;
    sax_fastparser::FastAttributeList *aBevelTAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xBevelTAttrList( aBevelTAttrList );
    sax_fastparser::FastAttributeList *aBevelBAttrList = FastSerializerHelper::createAttrList();
    sax_fastparser::XFastAttributeListRef xBevelBAttrList( aBevelBAttrList );
    sax_fastparser::FastAttributeList *aShape3DAttrList = FastSerializerHelper::createAttrList();
    for( sal_Int32 i=0; i < aShape3DProps.getLength(); ++i )
    {
        if( aShape3DProps[i].Name == "extrusionH" || aShape3DProps[i].Name == "contourW" || aShape3DProps[i].Name == "z" )
        {
            sal_Int32 nVal = 0, nToken = XML_none;
            aShape3DProps[i].Value >>= nVal;
            if( aShape3DProps[i].Name == "extrusionH" )
                nToken = XML_extrusionH;
            else if( aShape3DProps[i].Name == "contourW" )
                nToken = XML_contourW;
            else if( aShape3DProps[i].Name == "z" )
                nToken = XML_z;
            aShape3DAttrList->add( nToken, OString::number( nVal ).getStr() );
        }
        else if( aShape3DProps[i].Name == "prstMaterial" )
        {
            OUString sVal;
            aShape3DProps[i].Value >>= sVal;
            aShape3DAttrList->add( XML_prstMaterial, OUStringToOString( sVal, RTL_TEXTENCODING_UTF8 ).getStr() );
        }
        else if( aShape3DProps[i].Name == "extrusionClr" )
        {
            aShape3DProps[i].Value >>= aExtrusionColorProps;
        }
        else if( aShape3DProps[i].Name == "contourClr" )
        {
            aShape3DProps[i].Value >>= aContourColorProps;
        }
        else if( aShape3DProps[i].Name == "bevelT" || aShape3DProps[i].Name == "bevelB" )
        {
            Sequence< PropertyValue > aBevelProps;
            aShape3DProps[i].Value >>= aBevelProps;
            if ( !aBevelProps.hasElements() )
                continue;

            sax_fastparser::FastAttributeList *aBevelAttrList = nullptr;
            if( aShape3DProps[i].Name == "bevelT" )
            {
                bBevelTPresent = true;
                aBevelAttrList = aBevelTAttrList;
            }
            else
            {
                bBevelBPresent = true;
                aBevelAttrList = aBevelBAttrList;
            }
            for( sal_Int32 j=0; j < aBevelProps.getLength(); ++j )
            {
                if( aBevelProps[j].Name == "w" || aBevelProps[j].Name == "h" )
                {
                    sal_Int32 nVal = 0, nToken = XML_none;
                    aBevelProps[j].Value >>= nVal;
                    if( aBevelProps[j].Name == "w" )
                        nToken = XML_w;
                    else if( aBevelProps[j].Name == "h" )
                        nToken = XML_h;
                    aBevelAttrList->add( nToken, OString::number( nVal ).getStr() );
                }
                else  if( aBevelProps[j].Name == "prst" )
                {
                    OUString sVal;
                    aBevelProps[j].Value >>= sVal;
                    aBevelAttrList->add( XML_prst, OUStringToOString( sVal, RTL_TEXTENCODING_UTF8 ).getStr() );
                }
            }

        }
    }

    sax_fastparser::XFastAttributeListRef xAttrList( aShape3DAttrList );
    mpFS->startElementNS( XML_a, XML_sp3d, xAttrList );
    if( bBevelTPresent )
    {
        mpFS->singleElementNS( XML_a, XML_bevelT, xBevelTAttrList );
    }
    if( bBevelBPresent )
    {
        mpFS->singleElementNS( XML_a, XML_bevelB, xBevelBAttrList );
    }
    if( aExtrusionColorProps.hasElements() )
    {
        OUString sSchemeClr;
        ::Color nColor;
        sal_Int32 nTransparency(0);
        Sequence< PropertyValue > aColorTransformations;
        for( sal_Int32 i=0; i < aExtrusionColorProps.getLength(); ++i )
        {
            if( aExtrusionColorProps[i].Name == "schemeClr" )
                aExtrusionColorProps[i].Value >>= sSchemeClr;
            else if( aExtrusionColorProps[i].Name == "schemeClrTransformations" )
                aExtrusionColorProps[i].Value >>= aColorTransformations;
            else if( aExtrusionColorProps[i].Name == "rgbClr" )
                aExtrusionColorProps[i].Value >>= nColor;
            else if( aExtrusionColorProps[i].Name == "rgbClrTransparency" )
                aExtrusionColorProps[i].Value >>= nTransparency;
        }
        mpFS->startElementNS(XML_a, XML_extrusionClr);

        if( sSchemeClr.isEmpty() )
            WriteColor( nColor, MAX_PERCENT - ( PER_PERCENT * nTransparency ) );
        else
            WriteColor( sSchemeClr, aColorTransformations );

        mpFS->endElementNS( XML_a, XML_extrusionClr );
    }
    if( aContourColorProps.hasElements() )
    {
        OUString sSchemeClr;
        ::Color nColor;
        sal_Int32 nTransparency(0);
        Sequence< PropertyValue > aColorTransformations;
        for( sal_Int32 i=0; i < aContourColorProps.getLength(); ++i )
        {
            if( aContourColorProps[i].Name == "schemeClr" )
                aContourColorProps[i].Value >>= sSchemeClr;
            else if( aContourColorProps[i].Name == "schemeClrTransformations" )
                aContourColorProps[i].Value >>= aColorTransformations;
            else if( aContourColorProps[i].Name == "rgbClr" )
                aContourColorProps[i].Value >>= nColor;
            else if( aContourColorProps[i].Name == "rgbClrTransparency" )
                aContourColorProps[i].Value >>= nTransparency;
        }
        mpFS->startElementNS(XML_a, XML_contourClr);

        if( sSchemeClr.isEmpty() )
            WriteColor( nColor, MAX_PERCENT - ( PER_PERCENT * nTransparency ) );
        else
            WriteColor( sSchemeClr, aContourColorProps );

        mpFS->endElementNS( XML_a, XML_contourClr );
    }
    mpFS->endElementNS( XML_a, XML_sp3d );
}

void DrawingML::WriteArtisticEffect( const Reference< XPropertySet >& rXPropSet )
{
    if( !GetProperty( rXPropSet, "InteropGrabBag" ) )
        return;

    PropertyValue aEffect;
    Sequence< PropertyValue > aGrabBag;
    mAny >>= aGrabBag;
    for( sal_Int32 i=0; i < aGrabBag.getLength(); ++i )
    {
        if( aGrabBag[i].Name == "ArtisticEffectProperties" )
        {
            aGrabBag[i].Value >>= aEffect;
            break;
        }
    }
    sal_Int32 nEffectToken = ArtisticEffectProperties::getEffectToken( aEffect.Name );
    if( nEffectToken == XML_none )
        return;

    Sequence< PropertyValue > aAttrs;
    aEffect.Value >>= aAttrs;
    sax_fastparser::FastAttributeList *aAttrList = FastSerializerHelper::createAttrList();
    OString sRelId;
    for( sal_Int32 i=0; i < aAttrs.getLength(); ++i )
    {
        sal_Int32 nToken = ArtisticEffectProperties::getEffectToken( aAttrs[i].Name );
        if( nToken != XML_none )
        {
            sal_Int32 nVal = 0;
            aAttrs[i].Value >>= nVal;
            aAttrList->add( nToken, OString::number( nVal ).getStr() );
        }
        else if( aAttrs[i].Name == "OriginalGraphic" )
        {
            Sequence< PropertyValue > aGraphic;
            aAttrs[i].Value >>= aGraphic;
            Sequence< sal_Int8 > aGraphicData;
            OUString sGraphicId;
            for( sal_Int32 j=0; j < aGraphic.getLength(); ++j )
            {
                if( aGraphic[j].Name == "Id" )
                    aGraphic[j].Value >>= sGraphicId;
                else if( aGraphic[j].Name == "Data" )
                    aGraphic[j].Value >>= aGraphicData;
            }
            sRelId = WriteWdpPicture( sGraphicId, aGraphicData );
        }
    }

    mpFS->startElementNS(XML_a, XML_extLst);
    mpFS->startElementNS(XML_a, XML_ext, XML_uri, "{BEBA8EAE-BF5A-486C-A8C5-ECC9F3942E4B}");
    mpFS->startElementNS( XML_a14, XML_imgProps,
                          FSNS(XML_xmlns, XML_a14), mpFB->getNamespaceURL(OOX_NS(a14)).toUtf8() );
    mpFS->startElementNS(XML_a14, XML_imgLayer, FSNS(XML_r, XML_embed), sRelId);
    mpFS->startElementNS(XML_a14, XML_imgEffect);

    sax_fastparser::XFastAttributeListRef xAttrList( aAttrList );
    mpFS->singleElementNS( XML_a14, nEffectToken, xAttrList );

    mpFS->endElementNS( XML_a14, XML_imgEffect );
    mpFS->endElementNS( XML_a14, XML_imgLayer );
    mpFS->endElementNS( XML_a14, XML_imgProps );
    mpFS->endElementNS( XML_a, XML_ext );
    mpFS->endElementNS( XML_a, XML_extLst );
}

OString DrawingML::WriteWdpPicture( const OUString& rFileId, const Sequence< sal_Int8 >& rPictureData )
{
    std::map<OUString, OUString>::iterator aCachedItem = maWdpCache.find( rFileId );
    if( aCachedItem != maWdpCache.end() )
        return OUStringToOString( aCachedItem->second, RTL_TEXTENCODING_UTF8 );

    OUString sFileName = "media/hdphoto" + OUString::number( mnWdpImageCounter++ ) + ".wdp";
    Reference< XOutputStream > xOutStream = mpFB->openFragmentStream( OUStringBuffer()
                                                                      .appendAscii( GetComponentDir() )
                                                                      .append( "/" )
                                                                      .append( sFileName )
                                                                      .makeStringAndClear(),
                                                                      "image/vnd.ms-photo" );
    OUString sId;
    xOutStream->writeBytes( rPictureData );
    xOutStream->closeOutput();

    sId = mpFB->addRelation( mpFS->getOutputStream(),
                             oox::getRelationship(Relationship::HDPHOTO),
                             OUStringBuffer()
                             .appendAscii( GetRelationCompPrefix() )
                             .append( sFileName )
                             .makeStringAndClear() );

    maWdpCache[rFileId] = sId;
    return OUStringToOString( sId, RTL_TEXTENCODING_UTF8 );
}

void DrawingML::WriteDiagram(const css::uno::Reference<css::drawing::XShape>& rXShape, int nDiagramId)
{
    uno::Reference<beans::XPropertySet> xPropSet(rXShape, uno::UNO_QUERY);

    uno::Reference<xml::dom::XDocument> dataDom;
    uno::Reference<xml::dom::XDocument> layoutDom;
    uno::Reference<xml::dom::XDocument> styleDom;
    uno::Reference<xml::dom::XDocument> colorDom;
    uno::Reference<xml::dom::XDocument> drawingDom;
    uno::Sequence<uno::Sequence<uno::Any>> xDataRelSeq;
    uno::Sequence<uno::Any> diagramDrawing;

    // retrieve the doms from the GrabBag
    uno::Sequence<beans::PropertyValue> propList;
    xPropSet->getPropertyValue(UNO_NAME_MISC_OBJ_INTEROPGRABBAG) >>= propList;
    for (sal_Int32 nProp = 0; nProp < propList.getLength(); ++nProp)
    {
        OUString propName = propList[nProp].Name;
        if (propName == "OOXData")
            propList[nProp].Value >>= dataDom;
        else if (propName == "OOXLayout")
            propList[nProp].Value >>= layoutDom;
        else if (propName == "OOXStyle")
            propList[nProp].Value >>= styleDom;
        else if (propName == "OOXColor")
            propList[nProp].Value >>= colorDom;
        else if (propName == "OOXDrawing")
        {
            propList[nProp].Value >>= diagramDrawing;
            diagramDrawing[0]
                >>= drawingDom; // if there is OOXDrawing property then set drawingDom here only.
        }
        else if (propName == "OOXDiagramDataRels")
            propList[nProp].Value >>= xDataRelSeq;
    }

    // check that we have the 4 mandatory XDocuments
    // if not, there was an error importing and we won't output anything
    if (!dataDom.is() || !layoutDom.is() || !styleDom.is() || !colorDom.is())
        return;

    // generate a unique id
    sax_fastparser::FastAttributeList* pDocPrAttrList
        = sax_fastparser::FastSerializerHelper::createAttrList();
    pDocPrAttrList->add(XML_id, OString::number(nDiagramId).getStr());
    OUString sName = "Diagram" + OUString::number(nDiagramId);
    pDocPrAttrList->add(XML_name, OUStringToOString(sName, RTL_TEXTENCODING_UTF8).getStr());
    sax_fastparser::XFastAttributeListRef xDocPrAttrListRef(pDocPrAttrList);

    if (GetDocumentType() == DOCUMENT_DOCX)
    {
        mpFS->singleElementNS(XML_wp, XML_docPr, xDocPrAttrListRef);
        mpFS->singleElementNS(XML_wp, XML_cNvGraphicFramePr);

        mpFS->startElementNS(
            XML_a, XML_graphic, FSNS(XML_xmlns, XML_a),
            mpFB->getNamespaceURL(OOX_NS(dml)).toUtf8());
    }
    else
    {
        mpFS->startElementNS(XML_p, XML_nvGraphicFramePr);

        mpFS->singleElementNS(XML_p, XML_cNvPr, xDocPrAttrListRef);
        mpFS->singleElementNS(XML_p, XML_cNvGraphicFramePr);

        mpFS->startElementNS(XML_p, XML_nvPr);
        mpFS->startElementNS(XML_p, XML_extLst);
        // change tracking extension - required in PPTX
        mpFS->startElementNS(XML_p, XML_ext, XML_uri, "{D42A27DB-BD31-4B8C-83A1-F6EECF244321}");
        mpFS->singleElementNS(XML_p14, XML_modId,
            FSNS(XML_xmlns, XML_p14), mpFB->getNamespaceURL(OOX_NS(p14)).toUtf8(),
            XML_val,
            OString::number(comphelper::rng::uniform_uint_distribution(1, SAL_MAX_UINT32)));
        mpFS->endElementNS(XML_p, XML_ext);
        mpFS->endElementNS(XML_p, XML_extLst);
        mpFS->endElementNS(XML_p, XML_nvPr);

        mpFS->endElementNS(XML_p, XML_nvGraphicFramePr);

        // store size and position of background shape instead of group shape
        // as some shapes may be outside
        css::uno::Reference<css::drawing::XShapes> xShapes(rXShape, uno::UNO_QUERY);
        if (xShapes.is() && xShapes->hasElements())
        {
            css::uno::Reference<css::drawing::XShape> xShapeBg(xShapes->getByIndex(0),
                                                               uno::UNO_QUERY);
            awt::Point aPos = xShapeBg->getPosition();
            awt::Size aSize = xShapeBg->getSize();
            WriteTransformation(
                tools::Rectangle(Point(aPos.X, aPos.Y), Size(aSize.Width, aSize.Height)),
                XML_p, false, false, 0, false);
        }

        mpFS->startElementNS(XML_a, XML_graphic);
    }

    mpFS->startElementNS(XML_a, XML_graphicData, XML_uri,
                         "http://schemas.openxmlformats.org/drawingml/2006/diagram");

    OUString sRelationCompPrefix = OUString::createFromAscii(GetRelationCompPrefix());

    // add data relation
    OUString dataFileName = "diagrams/data" + OUString::number(nDiagramId) + ".xml";
    OString dataRelId = OUStringToOString(
        mpFB->addRelation(mpFS->getOutputStream(), oox::getRelationship(Relationship::DIAGRAMDATA),
                          sRelationCompPrefix + dataFileName),
        RTL_TEXTENCODING_UTF8);

    // add layout relation
    OUString layoutFileName = "diagrams/layout" + OUString::number(nDiagramId) + ".xml";
    OString layoutRelId
        = OUStringToOString(mpFB->addRelation(mpFS->getOutputStream(),
                                              oox::getRelationship(Relationship::DIAGRAMLAYOUT),
                                              sRelationCompPrefix + layoutFileName),
                            RTL_TEXTENCODING_UTF8);

    // add style relation
    OUString styleFileName = "diagrams/quickStyle" + OUString::number(nDiagramId) + ".xml";
    OString styleRelId
        = OUStringToOString(mpFB->addRelation(mpFS->getOutputStream(),
                                              oox::getRelationship(Relationship::DIAGRAMQUICKSTYLE),
                                              sRelationCompPrefix + styleFileName),
                            RTL_TEXTENCODING_UTF8);

    // add color relation
    OUString colorFileName = "diagrams/colors" + OUString::number(nDiagramId) + ".xml";
    OString colorRelId
        = OUStringToOString(mpFB->addRelation(mpFS->getOutputStream(),
                                              oox::getRelationship(Relationship::DIAGRAMCOLORS),
                                              sRelationCompPrefix + colorFileName),
                            RTL_TEXTENCODING_UTF8);

    OUString drawingFileName;
    if (drawingDom.is())
    {
        // add drawing relation
        drawingFileName = "diagrams/drawing" + OUString::number(nDiagramId) + ".xml";
        OUString drawingRelId = mpFB->addRelation(
            mpFS->getOutputStream(), oox::getRelationship(Relationship::DIAGRAMDRAWING),
            sRelationCompPrefix + drawingFileName);

        // the data dom contains a reference to the drawing relation. We need to update it with the new generated
        // relation value before writing the dom to a file

        // Get the dsp:damaModelExt node from the dom
        uno::Reference<xml::dom::XNodeList> nodeList = dataDom->getElementsByTagNameNS(
            "http://schemas.microsoft.com/office/drawing/2008/diagram", "dataModelExt");

        // There must be one element only so get it
        uno::Reference<xml::dom::XNode> node = nodeList->item(0);

        // Get the list of attributes of the node
        uno::Reference<xml::dom::XNamedNodeMap> nodeMap = node->getAttributes();

        // Get the node with the relId attribute and set its new value
        uno::Reference<xml::dom::XNode> relIdNode = nodeMap->getNamedItem("relId");
        relIdNode->setNodeValue(drawingRelId);
    }

    mpFS->singleElementNS(XML_dgm, XML_relIds,
        FSNS(XML_xmlns, XML_dgm), mpFB->getNamespaceURL(OOX_NS(dmlDiagram)).toUtf8(),
        FSNS(XML_xmlns, XML_r), mpFB->getNamespaceURL(OOX_NS(officeRel)).toUtf8(),
        FSNS(XML_r, XML_dm), dataRelId, FSNS(XML_r, XML_lo), layoutRelId,
        FSNS(XML_r, XML_qs), styleRelId, FSNS(XML_r, XML_cs), colorRelId);

    mpFS->endElementNS(XML_a, XML_graphicData);
    mpFS->endElementNS(XML_a, XML_graphic);

    uno::Reference<xml::sax::XSAXSerializable> serializer;
    uno::Reference<xml::sax::XWriter> writer
        = xml::sax::Writer::create(comphelper::getProcessComponentContext());

    OUString sDir = OUString::createFromAscii(GetComponentDir());

    // write data file
    serializer.set(dataDom, uno::UNO_QUERY);
    uno::Reference<io::XOutputStream> xDataOutputStream = mpFB->openFragmentStream(
        sDir + "/" + dataFileName,
        "application/vnd.openxmlformats-officedocument.drawingml.diagramData+xml");
    writer->setOutputStream(xDataOutputStream);
    serializer->serialize(uno::Reference<xml::sax::XDocumentHandler>(writer, uno::UNO_QUERY_THROW),
                          uno::Sequence<beans::StringPair>());

    // write the associated Images and rels for data file
    writeDiagramRels(xDataRelSeq, xDataOutputStream, "OOXDiagramDataRels", nDiagramId);

    // write layout file
    serializer.set(layoutDom, uno::UNO_QUERY);
    writer->setOutputStream(mpFB->openFragmentStream(
        sDir + "/" + layoutFileName,
        "application/vnd.openxmlformats-officedocument.drawingml.diagramLayout+xml"));
    serializer->serialize(uno::Reference<xml::sax::XDocumentHandler>(writer, uno::UNO_QUERY_THROW),
                          uno::Sequence<beans::StringPair>());

    // write style file
    serializer.set(styleDom, uno::UNO_QUERY);
    writer->setOutputStream(mpFB->openFragmentStream(
        sDir + "/" + styleFileName,
        "application/vnd.openxmlformats-officedocument.drawingml.diagramStyle+xml"));
    serializer->serialize(uno::Reference<xml::sax::XDocumentHandler>(writer, uno::UNO_QUERY_THROW),
                          uno::Sequence<beans::StringPair>());

    // write color file
    serializer.set(colorDom, uno::UNO_QUERY);
    writer->setOutputStream(mpFB->openFragmentStream(
        sDir + "/" + colorFileName,
        "application/vnd.openxmlformats-officedocument.drawingml.diagramColors+xml"));
    serializer->serialize(uno::Reference<xml::sax::XDocumentHandler>(writer, uno::UNO_QUERY_THROW),
                          uno::Sequence<beans::StringPair>());

    // write drawing file
    if (drawingDom.is())
    {
        serializer.set(drawingDom, uno::UNO_QUERY);
        uno::Reference<io::XOutputStream> xDrawingOutputStream = mpFB->openFragmentStream(
            sDir + "/" + drawingFileName, "application/vnd.ms-office.drawingml.diagramDrawing+xml");
        writer->setOutputStream(xDrawingOutputStream);
        serializer->serialize(
            uno::Reference<xml::sax::XDocumentHandler>(writer, uno::UNO_QUERY_THROW),
            uno::Sequence<beans::StringPair>());

        // write the associated Images and rels for drawing file
        uno::Sequence<uno::Sequence<uno::Any>> xDrawingRelSeq;
        diagramDrawing[1] >>= xDrawingRelSeq;
        writeDiagramRels(xDrawingRelSeq, xDrawingOutputStream, "OOXDiagramDrawingRels", nDiagramId);
    }
}

void DrawingML::writeDiagramRels(const uno::Sequence<uno::Sequence<uno::Any>>& xRelSeq,
                                 const uno::Reference<io::XOutputStream>& xOutStream,
                                 const OUString& sGrabBagProperyName, int nDiagramId)
{
    // add image relationships of OOXData, OOXDiagram
    OUString sType(oox::getRelationship(Relationship::IMAGE));
    uno::Reference<xml::sax::XWriter> xWriter
        = xml::sax::Writer::create(comphelper::getProcessComponentContext());
    xWriter->setOutputStream(xOutStream);

    // retrieve the relationships from Sequence
    for (sal_Int32 j = 0; j < xRelSeq.getLength(); j++)
    {
        // diagramDataRelTuple[0] => RID,
        // diagramDataRelTuple[1] => xInputStream
        // diagramDataRelTuple[2] => extension
        uno::Sequence<uno::Any> diagramDataRelTuple = xRelSeq[j];

        OUString sRelId;
        OUString sExtension;
        diagramDataRelTuple[0] >>= sRelId;
        diagramDataRelTuple[2] >>= sExtension;
        OUString sContentType;
        if (sExtension.equalsIgnoreAsciiCase(".WMF"))
            sContentType = "image/x-wmf";
        else
            sContentType = "image/" + sExtension.copy(1);
        sRelId = sRelId.copy(3);

        StreamDataSequence dataSeq;
        diagramDataRelTuple[1] >>= dataSeq;
        uno::Reference<io::XInputStream> dataImagebin(
            new ::comphelper::SequenceInputStream(dataSeq));

        //nDiagramId is used to make the name unique irrespective of the number of smart arts.
        OUString sFragment = "media/" + sGrabBagProperyName + OUString::number(nDiagramId) + "_"
                             + OUString::number(j) + sExtension;

        PropertySet aProps(xOutStream);
        aProps.setAnyProperty(PROP_RelId, uno::makeAny(sRelId.toInt32()));

        mpFB->addRelation(xOutStream, sType, "../" + sFragment);

        OUString sDir = OUString::createFromAscii(GetComponentDir());
        uno::Reference<io::XOutputStream> xBinOutStream
            = mpFB->openFragmentStream(sDir + "/" + sFragment, sContentType);

        try
        {
            comphelper::OStorageHelper::CopyInputToOutput(dataImagebin, xBinOutStream);
        }
        catch (const uno::Exception&)
        {
            TOOLS_WARN_EXCEPTION("oox.drawingml", "DrawingML::writeDiagramRels Failed to copy grabbaged Image");
        }
        dataImagebin->closeInput();
    }
}

}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
