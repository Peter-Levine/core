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

#include "ooxmldocpropimport.hxx"

#include <vector>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/XHierarchicalStorageAccess.hpp>
#include <com/sun/star/embed/XRelationshipAccess.hpp>
#include <com/sun/star/embed/XStorage.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <oox/core/fastparser.hxx>
#include <oox/core/relations.hxx>
#include <oox/helper/containerhelper.hxx>
#include <oox/helper/helper.hxx>
#include "docprophandler.hxx"

#include <cppuhelper/supportsservice.hxx>

using namespace ::com::sun::star;

namespace oox {
namespace docprop {

using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::document;
using namespace ::com::sun::star::embed;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::xml::sax;

namespace {

/// @throws RuntimeException
/// @throws css::io::IOException
Sequence< InputSource > lclGetRelatedStreams( const Reference< XStorage >& rxStorage, const OUString& rStreamType )
{
    Reference< XRelationshipAccess > xRelation( rxStorage, UNO_QUERY_THROW );
    Reference< XHierarchicalStorageAccess > xHierarchy( rxStorage, UNO_QUERY_THROW );

    Sequence< Sequence< StringPair > > aPropsInfo = xRelation->getRelationshipsByType( rStreamType );

    ::std::vector< InputSource > aResult;

    for( sal_Int32 nIndex = 0, nLength = aPropsInfo.getLength(); nIndex < nLength; ++nIndex )
    {
        const Sequence< StringPair >& rEntries = aPropsInfo[ nIndex ];
        for( sal_Int32 nEntryIndex = 0, nEntryLength = rEntries.getLength(); nEntryIndex < nEntryLength; ++nEntryIndex )
        {
            const StringPair& rEntry = rEntries[ nEntryIndex ];
            if ( rEntry.First == "Target" )
            {
                // The stream path is always a relative one, ignore the leading "/" if it's there.
                OUString aStreamPath = rEntry.Second;
                if (aStreamPath.startsWith("/"))
                    aStreamPath = aStreamPath.copy(1);

                Reference< XExtendedStorageStream > xExtStream(
                    xHierarchy->openStreamElementByHierarchicalName( aStreamPath, ElementModes::READ ), UNO_SET_THROW );
                Reference< XInputStream > xInStream = xExtStream->getInputStream();
                if( xInStream.is() )
                {
                    aResult.emplace_back();
                    aResult.back().sSystemId = rEntry.Second;
                    aResult.back().aInputStream = xExtStream->getInputStream();
                }
                break;
            }
        }
    }

    return ContainerHelper::vectorToSequence( aResult );
}

} // namespace

DocumentPropertiesImport::DocumentPropertiesImport( const Reference< XComponentContext >& rxContext ) :
    mxContext( rxContext )
{
}

// XServiceInfo
OUString SAL_CALL DocumentPropertiesImport::getImplementationName()
{
    return "com.sun.star.comp.oox.docprop.DocumentPropertiesImporter";
}

sal_Bool SAL_CALL DocumentPropertiesImport::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

Sequence< OUString > SAL_CALL DocumentPropertiesImport::getSupportedServiceNames()
{
    Sequence<OUString> aServices { "com.sun.star.document.OOXMLDocumentPropertiesImporter" };
    return aServices;
}

// XOOXMLDocumentPropertiesImporter
void SAL_CALL DocumentPropertiesImport::importProperties(
        const Reference< XStorage >& rxSource, const Reference< XDocumentProperties >& rxDocumentProperties )
{
    if( !mxContext.is() )
        throw RuntimeException();

    if( !rxSource.is() || !rxDocumentProperties.is() )
        throw IllegalArgumentException();

    Sequence< InputSource > aCoreStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE( "metadata/core-properties" ) );
    // OOXML strict
    if( !aCoreStreams.hasElements() )
        aCoreStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE_STRICT( "metadata/core-properties" ) );
    // MS Office seems to have a bug, so we have to do similar handling
    if( !aCoreStreams.hasElements() )
        aCoreStreams = lclGetRelatedStreams( rxSource, "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" );

    Sequence< InputSource > aExtStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE( "extended-properties" ) );
    // OOXML strict
    if( !aExtStreams.hasElements() )
        aExtStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE_STRICT( "extended-properties" ) );
    Sequence< InputSource > aCustomStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE( "custom-properties" ) );
    // OOXML strict
    if( !aCustomStreams.hasElements() )
        aCustomStreams = lclGetRelatedStreams( rxSource, CREATE_OFFICEDOC_RELATION_TYPE_STRICT( "custom-properties" ) );

    if( aCoreStreams.hasElements() || aExtStreams.hasElements() || aCustomStreams.hasElements() )
    {
        if( aCoreStreams.getLength() > 1 )
            throw IOException( "Unexpected core properties stream!" );

        ::oox::core::FastParser aParser;
        aParser.registerNamespace( NMSP_packageMetaCorePr );
        aParser.registerNamespace( NMSP_dc );
        aParser.registerNamespace( NMSP_dcTerms );
        aParser.registerNamespace( NMSP_officeExtPr );
        aParser.registerNamespace( NMSP_officeCustomPr );
        aParser.registerNamespace( NMSP_officeDocPropsVT );
        aParser.setDocumentHandler( new OOXMLDocPropHandler( mxContext, rxDocumentProperties ) );

        if( aCoreStreams.hasElements() )
            aParser.parseStream( aCoreStreams[ 0 ], true );
        for( sal_Int32 nIndex = 0; nIndex < aExtStreams.getLength(); ++nIndex )
            aParser.parseStream( aExtStreams[ nIndex ], true );
        for( sal_Int32 nIndex = 0; nIndex < aCustomStreams.getLength(); ++nIndex )
            aParser.parseStream( aCustomStreams[ nIndex ], true );
    }
}

} // namespace docprop
} // namespace oox

extern "C" SAL_DLLPUBLIC_EXPORT uno::XInterface*
com_sun_star_comp_oox_docprop_DocumentPropertiesImporter_get_implementation(
    uno::XComponentContext* pCtx, uno::Sequence<uno::Any> const& /*rSeq*/)
{
    return cppu::acquire(new oox::docprop::DocumentPropertiesImport(pCtx));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
