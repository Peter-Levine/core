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


#include "cacheddynamicresultset.hxx"
#include <com/sun/star/sdbc/XResultSet.hpp>
#include "cachedcontentresultset.hxx"
#include <osl/diagnose.h>
#include <cppuhelper/queryinterface.hxx>
#include <comphelper/processfactory.hxx>
#include <ucbhelper/getcomponentcontext.hxx>

using namespace com::sun::star::lang;
using namespace com::sun::star::sdbc;
using namespace com::sun::star::ucb;
using namespace com::sun::star::uno;


CachedDynamicResultSet::CachedDynamicResultSet(
        Reference< XDynamicResultSet > const & xOrigin
        , const Reference< XContentIdentifierMapping > & xContentMapping
        , const Reference< XComponentContext > & xContext )
        : DynamicResultSetWrapper( xOrigin, xContext )
        , m_xContentIdentifierMapping( xContentMapping )
{
    impl_init();
}

CachedDynamicResultSet::~CachedDynamicResultSet()
{
    impl_deinit();
}

//virtual
void CachedDynamicResultSet
    ::impl_InitResultSetOne( const Reference< XResultSet >& xResultSet )
{
    DynamicResultSetWrapper::impl_InitResultSetOne( xResultSet );
    OSL_ENSURE( m_xSourceResultOne.is(), "need source resultset" );

    Reference< XResultSet > xCache(
        new CachedContentResultSet( m_xContext, m_xSourceResultOne, m_xContentIdentifierMapping ) );

    osl::Guard< osl::Mutex > aGuard( m_aMutex );
    m_xMyResultOne = xCache;
}

//virtual
void CachedDynamicResultSet
    ::impl_InitResultSetTwo( const Reference< XResultSet >& xResultSet )
{
    DynamicResultSetWrapper::impl_InitResultSetTwo( xResultSet );
    OSL_ENSURE( m_xSourceResultTwo.is(), "need source resultset" );

    Reference< XResultSet > xCache(
        new CachedContentResultSet( m_xContext, m_xSourceResultTwo, m_xContentIdentifierMapping ) );

    osl::Guard< osl::Mutex > aGuard( m_aMutex );
    m_xMyResultTwo = xCache;
}


// XInterface methods.
void SAL_CALL CachedDynamicResultSet::acquire()
    throw()
{
    OWeakObject::acquire();
}

void SAL_CALL CachedDynamicResultSet::release()
    throw()
{
    OWeakObject::release();
}

Any SAL_CALL CachedDynamicResultSet
    ::queryInterface( const Type&  rType )
{
    //list all interfaces inclusive baseclasses of interfaces

    Any aRet = DynamicResultSetWrapper::queryInterface( rType );
    if( aRet.hasValue() )
        return aRet;

    aRet = cppu::queryInterface( rType,
                static_cast< XTypeProvider* >( this )
                , static_cast< XServiceInfo* >( this )
                );
    return aRet.hasValue() ? aRet : OWeakObject::queryInterface( rType );
}


// XTypeProvider methods.

//list all interfaces exclusive baseclasses
XTYPEPROVIDER_IMPL_4( CachedDynamicResultSet
                    , XTypeProvider
                    , XServiceInfo
                    , XDynamicResultSet
                    , XSourceInitialization
                    );


// XServiceInfo methods.

OUString SAL_CALL CachedDynamicResultSet::getImplementationName()
{
    return "com.sun.star.comp.ucb.CachedDynamicResultSet";
}

sal_Bool SAL_CALL CachedDynamicResultSet::supportsService( const OUString& ServiceName )
{
    return cppu::supportsService( this, ServiceName );
}

css::uno::Sequence< OUString > SAL_CALL CachedDynamicResultSet::getSupportedServiceNames()
{
    return { CACHED_DRS_SERVICE_NAME };
}


// own methods. ( inherited )

//virtual
void CachedDynamicResultSet
    ::impl_disposing( const EventObject& Source )
{
    DynamicResultSetWrapper::impl_disposing( Source );
    m_xContentIdentifierMapping.clear();
}


// class CachedDynamicResultSetFactory


CachedDynamicResultSetFactory::CachedDynamicResultSetFactory(
        const Reference< XComponentContext > & xContext )
{
    m_xContext = xContext;
}

CachedDynamicResultSetFactory::~CachedDynamicResultSetFactory()
{
}


// CachedDynamicResultSetFactory XInterface methods.
void SAL_CALL CachedDynamicResultSetFactory::acquire()
    throw()
{
    OWeakObject::acquire();
}

void SAL_CALL CachedDynamicResultSetFactory::release()
    throw()
{
    OWeakObject::release();
}

css::uno::Any SAL_CALL CachedDynamicResultSetFactory::queryInterface( const css::uno::Type & rType )
{
    css::uno::Any aRet = cppu::queryInterface( rType,
                                               static_cast< XTypeProvider* >(this),
                                               static_cast< XServiceInfo* >(this),
                                               static_cast< XCachedDynamicResultSetFactory* >(this)
                                               );
    return aRet.hasValue() ? aRet : OWeakObject::queryInterface( rType );
}

// CachedDynamicResultSetFactory XTypeProvider methods.


XTYPEPROVIDER_IMPL_3( CachedDynamicResultSetFactory,
                      XTypeProvider,
                         XServiceInfo,
                      XCachedDynamicResultSetFactory );


// CachedDynamicResultSetFactory XServiceInfo methods.

XSERVICEINFO_COMMOM_IMPL( CachedDynamicResultSetFactory,
                         "com.sun.star.comp.ucb.CachedDynamicResultSetFactory" )
/// @throws css::uno::Exception
static css::uno::Reference< css::uno::XInterface >
CachedDynamicResultSetFactory_CreateInstance( const css::uno::Reference< css::lang::XMultiServiceFactory> & rSMgr )
{
    css::lang::XServiceInfo* pX =
        static_cast<css::lang::XServiceInfo*>(new CachedDynamicResultSetFactory( ucbhelper::getComponentContext(rSMgr) ));
    return css::uno::Reference< css::uno::XInterface >::query( pX );
}

css::uno::Sequence< OUString >
CachedDynamicResultSetFactory::getSupportedServiceNames_Static()
{
    css::uno::Sequence< OUString > aSNS { CACHED_DRS_FACTORY_NAME };
    return aSNS;
}

// Service factory implementation.


ONE_INSTANCE_SERVICE_FACTORY_IMPL( CachedDynamicResultSetFactory );


// CachedDynamicResultSetFactory XCachedDynamicResultSetFactory methods.


//virtual
Reference< XDynamicResultSet > SAL_CALL CachedDynamicResultSetFactory
    ::createCachedDynamicResultSet(
          const Reference< XDynamicResultSet > & SourceStub
        , const Reference< XContentIdentifierMapping > & ContentIdentifierMapping )
{
    Reference< XDynamicResultSet > xRet = new CachedDynamicResultSet( SourceStub, ContentIdentifierMapping, m_xContext );
    return xRet;
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
