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

#include <sal/config.h>
#include <sal/log.hxx>

#include <vcl/errinf.hxx>
#include <com/sun/star/uri/UriReferenceFactory.hpp>
#include <com/sun/star/util/MeasureUnit.hpp>
#include <com/sun/star/util/XNumberFormatsSupplier.hpp>
#include <com/sun/star/packages/WrongPasswordException.hpp>
#include <com/sun/star/packages/zip/ZipIOException.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/sdb/XOfficeDatabaseDocument.hpp>
#include "xmlfilter.hxx"
#include "xmlservices.hxx"
#include <flt_reghelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <xmloff/xmlnmspe.hxx>
#include <xmloff/xmlscripti.hxx>
#include <xmloff/xmltoken.hxx>
#include <xmloff/txtimp.hxx>
#include <xmloff/nmspmap.hxx>
#include <com/sun/star/xml/sax/InputSource.hpp>
#include <com/sun/star/xml/sax/Parser.hpp>
#include <com/sun/star/xml/sax/SAXParseException.hpp>
#include <xmloff/ProgressBarHelper.hxx>
#include <sfx2/docfile.hxx>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/uno/XNamingService.hpp>
#include "xmlDatabase.hxx"
#include "xmlEnums.hxx"
#include <stringconstants.hxx>
#include <strings.hxx>
#include <xmloff/DocumentSettingsContext.hxx>
#include "xmlStyleImport.hxx"
#include <xmloff/xmluconv.hxx>
#include "xmlHelper.hxx"
#include <com/sun/star/util/XModifiable.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <svtools/sfxecode.hxx>
#include <tools/diagnose_ex.h>
#include <osl/diagnose.h>
#include <comphelper/processfactory.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/types.hxx>
#include <comphelper/namedvaluecollection.hxx>
#include <cppuhelper/exc_hlp.hxx>
#include <connectivity/DriversConfig.hxx>
#include <dsntypes.hxx>
#include <rtl/strbuf.hxx>
#include <rtl/uri.hxx>

using namespace ::com::sun::star;

extern "C" void createRegistryInfo_ODBFilter( )
{
    static ::dbaxml::OMultiInstanceAutoRegistration< ::dbaxml::ODBFilter > aAutoRegistration;
}


namespace dbaxml
{
    using namespace ::com::sun::star::util;
    /// read a component (file + filter version)
static ErrCode ReadThroughComponent(
    const uno::Reference<XInputStream>& xInputStream,
    const uno::Reference<XComponent>& xModelComponent,
    const uno::Reference<XComponentContext> & rxContext,
    const uno::Reference< XDocumentHandler >& _xFilter )
{
    OSL_ENSURE(xInputStream.is(), "input stream missing");
    OSL_ENSURE(xModelComponent.is(), "document missing");
    OSL_ENSURE(rxContext.is(), "factory missing");

    // prepare ParserInputSrouce
    InputSource aParserInput;
    aParserInput.aInputStream = xInputStream;

    // get parser
    uno::Reference< XParser > xParser = Parser::create(rxContext);
    SAL_INFO("dbaccess", "parser created" );

    // get filter
    OSL_ENSURE( _xFilter.is(), "Can't instantiate filter component." );
    if( !_xFilter.is() )
        return ErrCode(1);

    // connect parser and filter
    xParser->setDocumentHandler( _xFilter );

    // connect model and filter
    uno::Reference < XImporter > xImporter( _xFilter, UNO_QUERY );
    xImporter->setTargetDocument( xModelComponent );

    // finally, parser the stream
    try
    {
        xParser->parseStream( aParserInput );
    }
    catch (const SAXParseException&)
    {
#if OSL_DEBUG_LEVEL > 0
        TOOLS_WARN_EXCEPTION("dbaccess", "SAX parse exception caught while importing");
#endif
        return ErrCode(1);
    }
    catch (const SAXException&)
    {
        return ErrCode(1);
    }
    catch (const packages::zip::ZipIOException&)
    {
        return ERRCODE_IO_BROKENPACKAGE;
    }
    catch (const Exception&)
    {
        DBG_UNHANDLED_EXCEPTION("dbaccess");
    }

    // success!
    return ERRCODE_NONE;
}


/// read a component (storage version)
static ErrCode ReadThroughComponent(
    const uno::Reference< embed::XStorage >& xStorage,
    const uno::Reference<XComponent>& xModelComponent,
    const sal_Char* pStreamName,
    const sal_Char* pCompatibilityStreamName,
    const uno::Reference<XComponentContext> & rxContext,
    const uno::Reference< XDocumentHandler >& _xFilter)
{
    OSL_ENSURE( xStorage.is(), "Need storage!");
    OSL_ENSURE(nullptr != pStreamName, "Please, please, give me a name!");

    if ( xStorage.is() )
    {
        uno::Reference< io::XStream > xDocStream;

        try
        {
            // open stream (and set parser input)
            OUString sStreamName = OUString::createFromAscii(pStreamName);
            if ( !xStorage->hasByName( sStreamName ) || !xStorage->isStreamElement( sStreamName ) )
            {
                // stream name not found! Then try the compatibility name.
                // if no stream can be opened, return immediately with OK signal

                // do we even have an alternative name?
                if ( nullptr == pCompatibilityStreamName )
                    return ERRCODE_NONE;

                // if so, does the stream exist?
                sStreamName = OUString::createFromAscii(pCompatibilityStreamName);
                if ( !xStorage->hasByName( sStreamName ) || !xStorage->isStreamElement( sStreamName ) )
                    return ERRCODE_NONE;
            }

            // get input stream
            xDocStream = xStorage->openStreamElement( sStreamName, embed::ElementModes::READ );
        }
        catch (const packages::WrongPasswordException&)
        {
            return ERRCODE_SFX_WRONGPASSWORD;
        }
        catch (const uno::Exception&)
        {
            return ErrCode(1); // TODO/LATER: error handling
        }

        uno::Reference< XInputStream > xInputStream = xDocStream->getInputStream();
        // read from the stream
        return ReadThroughComponent( xInputStream
                                    ,xModelComponent
                                    ,rxContext
                                    ,_xFilter );
    }

    // TODO/LATER: better error handling
    return ErrCode(1);
}


ODBFilter::ODBFilter( const uno::Reference< XComponentContext >& _rxContext )
    : SvXMLImport(_rxContext, getImplementationName_Static())
    , m_bNewFormat(false)
{

    GetMM100UnitConverter().SetCoreMeasureUnit(util::MeasureUnit::MM_10TH);
    GetMM100UnitConverter().SetXMLMeasureUnit(util::MeasureUnit::CM);
    GetNamespaceMap().Add( "_db",
                        GetXMLToken(XML_N_DB),
                        XML_NAMESPACE_DB );

    GetNamespaceMap().Add( "__db",
                        GetXMLToken(XML_N_DB_OASIS),
                        XML_NAMESPACE_DB );
}


ODBFilter::~ODBFilter() throw()
{

}


OUString ODBFilter::getImplementationName_Static()
{
    return "com.sun.star.comp.sdb.DBFilter";
}


css::uno::Sequence<OUString> ODBFilter::getSupportedServiceNames_Static()
{
    css::uno::Sequence<OUString> s { "com.sun.star.document.ImportFilter" };
    return s;
}


css::uno::Reference< css::uno::XInterface >
    ODBFilter::Create(const css::uno::Reference< css::lang::XMultiServiceFactory >& _rxORB)
{
    return static_cast< XServiceInfo* >(new ODBFilter( comphelper::getComponentContext(_rxORB)));
}

namespace {
class FocusWindowWaitGuard
{
public:
    FocusWindowWaitGuard()
    {
        SolarMutexGuard aGuard;
        mpWindow.set(Application::GetFocusWindow());
        if (mpWindow)
            mpWindow->EnterWait();
    }
    ~FocusWindowWaitGuard()
    {
        if (mpWindow)
        {
            SolarMutexGuard aGuard;
            mpWindow->LeaveWait();
        }
    }
private:
    VclPtr<vcl::Window> mpWindow;
};
}

sal_Bool SAL_CALL ODBFilter::filter( const Sequence< PropertyValue >& rDescriptor )
{
    FocusWindowWaitGuard aWindowFocusGuard;
    bool    bRet = false;

    if ( GetModel().is() )
        bRet = implImport( rDescriptor );

    return bRet;
}


bool ODBFilter::implImport( const Sequence< PropertyValue >& rDescriptor )
{
    OUString sFileName;
    ::comphelper::NamedValueCollection aMediaDescriptor( rDescriptor );

    uno::Reference<embed::XStorage> xStorage = GetSourceStorage();

    bool bRet = true;
    if (!xStorage.is())
    {
        if (aMediaDescriptor.has("URL"))
            sFileName = aMediaDescriptor.getOrDefault("URL", OUString());
        if (sFileName.isEmpty() && aMediaDescriptor.has("FileName"))
            sFileName = aMediaDescriptor.getOrDefault("FileName", sFileName);

        OSL_ENSURE(!sFileName.isEmpty(), "ODBFilter::implImport: no URL given!");
        bRet = !sFileName.isEmpty();
    }

    if ( bRet )
    {
        uno::Reference<XComponent> xCom(GetModel(),UNO_QUERY);

        tools::SvRef<SfxMedium> pMedium;
        if (!xStorage.is())
        {
            OUString sStreamRelPath;
            if (sFileName.startsWithIgnoreAsciiCase("vnd.sun.star.pkg:"))
            {
                // In this case the authority contains the real path, and the path is the embedded stream name.
                auto const uri = css::uri::UriReferenceFactory::create(GetComponentContext())
                    ->parse(sFileName);
                if (uri.is() && uri->isAbsolute() && uri->isHierarchical()
                    && uri->hasAuthority() && !uri->hasQuery() && !uri->hasFragment())
                {
                    auto const auth = uri->getAuthority();
                    auto const decAuth = rtl::Uri::decode(
                        auth, rtl_UriDecodeStrict, RTL_TEXTENCODING_UTF8);
                    auto path = uri->getPath();
                    if (!path.isEmpty()) {
                        assert(path[0] == '/');
                        path = path.copy(1);
                    }
                    auto const decPath = rtl::Uri::decode(
                        path, rtl_UriDecodeStrict, RTL_TEXTENCODING_UTF8);
                        //TODO: really decode path?
                    if (auth.isEmpty() == decAuth.isEmpty() && path.isEmpty() == decPath.isEmpty())
                    {
                        // Decoding of auth and path to UTF-8 succeeded:
                        sFileName = decAuth;
                        sStreamRelPath = decPath;
                    } else {
                        SAL_WARN(
                            "dbaccess",
                            "<" << sFileName << "> cannot be parse as vnd.sun.star.pkg URL");
                    }
                } else {
                    SAL_WARN(
                        "dbaccess",
                        "<" << sFileName << "> cannot be parse as vnd.sun.star.pkg URL");
                }
            }

            pMedium = new SfxMedium(sFileName, (StreamMode::READ | StreamMode::NOCREATE));
            try
            {
                xStorage.set(pMedium->GetStorage(false), UNO_SET_THROW);

                if (!sStreamRelPath.isEmpty())
                    xStorage = xStorage->openStorageElement(sStreamRelPath, embed::ElementModes::READ);
            }
            catch (const RuntimeException&)
            {
                throw;
            }
            catch (const Exception&)
            {
                Any aError = ::cppu::getCaughtException();
                throw lang::WrappedTargetRuntimeException(OUString(), *this, aError);
            }
        }

        uno::Reference<sdb::XOfficeDatabaseDocument> xOfficeDoc(GetModel(),UNO_QUERY_THROW);
        m_xDataSource.set(xOfficeDoc->getDataSource(),UNO_QUERY_THROW);
        uno::Reference< XNumberFormatsSupplier > xNum(m_xDataSource->getPropertyValue(PROPERTY_NUMBERFORMATSSUPPLIER),UNO_QUERY);
        SetNumberFormatsSupplier(xNum);

        uno::Reference<XComponent> xModel(GetModel(),UNO_QUERY);
        ErrCode nRet = ReadThroughComponent( xStorage
                                    ,xModel
                                    ,"settings.xml"
                                    ,"Settings.xml"
                                    ,GetComponentContext()
                                    ,this
                                    );

        if ( nRet == ERRCODE_NONE )
            nRet = ReadThroughComponent( xStorage
                                    ,xModel
                                    ,"content.xml"
                                    ,"Content.xml"
                                    ,GetComponentContext()
                                    ,this
                                    );

        bRet = nRet == ERRCODE_NONE;

        if ( bRet )
        {
            uno::Reference< XModifiable > xModi(GetModel(),UNO_QUERY);
            if ( xModi.is() )
                xModi->setModified(false);
        }
        else
        {
            if ( nRet == ERRCODE_IO_BROKENPACKAGE )
                    ;// TODO/LATER: no way to transport the error outside from the filter!
            else
            {
                // TODO/LATER: this is completely wrong! Filter code should never call ErrorHandler directly! But for now this is the only way!
                ErrorHandler::HandleError( nRet );
                if( nRet.IsWarning() )
                    bRet = true;
            }
        }
    }

    return bRet;
}

class DBXMLDocumentSettingsContext : public SvXMLImportContext
{
public:
    DBXMLDocumentSettingsContext(SvXMLImport & rImport,
           sal_uInt16 const nPrefix,
           const OUString& rLocalName)
        : SvXMLImportContext(rImport, nPrefix, rLocalName)
    {
    }

    virtual SvXMLImportContextRef CreateChildContext(sal_uInt16 const nPrefix,
           const OUString& rLocalName,
           const uno::Reference<xml::sax::XAttributeList> & xAttrList) override
    {
        if (nPrefix == XML_NAMESPACE_OFFICE && IsXMLToken(rLocalName, XML_SETTINGS))
        {
            return new XMLDocumentSettingsContext(GetImport(), nPrefix, rLocalName, xAttrList);
        }
        else
        {
            return new SvXMLImportContext(GetImport(), nPrefix, rLocalName);
        }
    }
};

class DBXMLDocumentStylesContext : public SvXMLImportContext
{
public:
    DBXMLDocumentStylesContext(SvXMLImport & rImport,
            sal_uInt16 const nPrefix,
            const OUString& rLocalName)
        : SvXMLImportContext(rImport, nPrefix, rLocalName)
    {
    }

    virtual SvXMLImportContextRef CreateChildContext(sal_uInt16 const nPrefix,
        const OUString& rLocalName,
        const uno::Reference<xml::sax::XAttributeList> & xAttrList) override
    {
        SvXMLImportContext *pContext = nullptr;

        ODBFilter & rImport(static_cast<ODBFilter&>(GetImport()));
        const SvXMLTokenMap& rTokenMap = rImport.GetDocContentElemTokenMap();
        switch (rTokenMap.Get(nPrefix, rLocalName))
        {
            case XML_TOK_CONTENT_STYLES:
                rImport.GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                pContext = rImport.CreateStylesContext(nPrefix, rLocalName, xAttrList, false);
                break;
            case XML_TOK_CONTENT_AUTOSTYLES:
                rImport.GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                pContext = rImport.CreateStylesContext(nPrefix, rLocalName, xAttrList, true);
                break;
            default:
                break;
        }

        if (!pContext)
            pContext = new SvXMLImportContext(GetImport(), nPrefix, rLocalName);

        return pContext;
    }
};

class DBXMLDocumentBodyContext : public SvXMLImportContext
{
public:
    DBXMLDocumentBodyContext(SvXMLImport & rImport,
           sal_uInt16 const nPrefix,
           const OUString& rLocalName)
        : SvXMLImportContext(rImport, nPrefix, rLocalName)
    {
    }

    virtual SvXMLImportContextRef CreateChildContext(sal_uInt16 const nPrefix,
           const OUString& rLocalName,
           const uno::Reference<xml::sax::XAttributeList> &) override
    {
        if ((XML_NAMESPACE_OFFICE == nPrefix || XML_NAMESPACE_OOO == nPrefix)
            && IsXMLToken(rLocalName, XML_DATABASE))
        {
            ODBFilter & rImport(static_cast<ODBFilter&>(GetImport()));
            rImport.GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
            return new OXMLDatabase(rImport, nPrefix, rLocalName );
        }
        else
        {
            return new SvXMLImportContext(GetImport(), nPrefix, rLocalName);
        }
    }
};

class DBXMLDocumentContentContext : public SvXMLImportContext
{
public:
    DBXMLDocumentContentContext(SvXMLImport & rImport,
            sal_uInt16 const nPrefix,
            const OUString& rLocalName)
        : SvXMLImportContext(rImport, nPrefix, rLocalName)
    {
    }

    virtual SvXMLImportContextRef CreateChildContext(sal_uInt16 const nPrefix,
        const OUString& rLocalName,
        const uno::Reference<xml::sax::XAttributeList> & xAttrList) override
    {
        SvXMLImportContext *pContext = nullptr;

        ODBFilter & rImport(static_cast<ODBFilter&>(GetImport()));
        const SvXMLTokenMap& rTokenMap = rImport.GetDocContentElemTokenMap();
        switch (rTokenMap.Get(nPrefix, rLocalName))
        {
            case XML_TOK_CONTENT_AUTOSTYLES:
                rImport.GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
                pContext = rImport.CreateStylesContext(nPrefix, rLocalName, xAttrList, true);
                break;
            case XML_TOK_CONTENT_SCRIPTS:
                pContext = new XMLScriptContext(GetImport(), rLocalName, rImport.GetModel());
                break;
            case XML_TOK_CONTENT_BODY:
                pContext = new DBXMLDocumentBodyContext(rImport, nPrefix, rLocalName);
                break;
            default:
                break;
        }

        if (!pContext)
            pContext = new SvXMLImportContext(GetImport(), nPrefix, rLocalName);

        return pContext;
    }
};

SvXMLImportContext* ODBFilter::CreateDocumentContext(sal_uInt16 const nPrefix,
                                      const OUString& rLocalName,
                                      const uno::Reference< css::xml::sax::XAttributeList >& xAttrList )
{
    SvXMLImportContext *pContext = nullptr;

    const SvXMLTokenMap& rTokenMap = GetDocElemTokenMap();
    switch( rTokenMap.Get( nPrefix, rLocalName ) )
    {
        case XML_TOK_DOC_SETTINGS:
            GetProgressBarHelper()->Increment( PROGRESS_BAR_STEP );
            pContext = new DBXMLDocumentSettingsContext(*this, nPrefix, rLocalName);
            break;
        case XML_TOK_DOC_STYLES:
            pContext = new DBXMLDocumentStylesContext(*this, nPrefix, rLocalName);
            break;
        case XML_TOK_DOC_CONTENT:
            pContext = new DBXMLDocumentContentContext(*this, nPrefix, rLocalName);
            break;
        default:
            break;
    }

    if ( !pContext )
        pContext = SvXMLImport::CreateDocumentContext( nPrefix, rLocalName, xAttrList );

    return pContext;
}


void ODBFilter::SetViewSettings(const Sequence<PropertyValue>& aViewProps)
{
    const PropertyValue *pIter = aViewProps.getConstArray();
    const PropertyValue *pEnd = pIter + aViewProps.getLength();
    for (; pIter != pEnd; ++pIter)
    {
        if ( pIter->Name == "Queries" )
        {
            fillPropertyMap(pIter->Value,m_aQuerySettings);
        }
        else if ( pIter->Name == "Tables" )
        {
            fillPropertyMap(pIter->Value,m_aTablesSettings);
        }
    }
}


void ODBFilter::SetConfigurationSettings(const Sequence<PropertyValue>& aConfigProps)
{
    const PropertyValue *pIter = aConfigProps.getConstArray();
    const PropertyValue *pEnd = pIter + aConfigProps.getLength();
    for (; pIter != pEnd; ++pIter)
    {
        if ( pIter->Name == "layout-settings" )
        {
            Sequence<PropertyValue> aWindows;
            pIter->Value >>= aWindows;
            uno::Reference<XPropertySet> xProp(getDataSource());
            if ( xProp.is() )
                xProp->setPropertyValue(PROPERTY_LAYOUTINFORMATION,makeAny(aWindows));
        }
    }
}


void ODBFilter::fillPropertyMap(const Any& _rValue,TPropertyNameMap& _rMap)
{
    Sequence<PropertyValue> aWindows;
    _rValue >>= aWindows;
    const PropertyValue *pIter = aWindows.getConstArray();
    const PropertyValue *pEnd = pIter + aWindows.getLength();
    for (; pIter != pEnd; ++pIter)
    {
        Sequence<PropertyValue> aValue;
        pIter->Value >>= aValue;
        _rMap.emplace( pIter->Name,aValue );
    }

}


const SvXMLTokenMap& ODBFilter::GetDocElemTokenMap() const
{
    if (!m_pDocElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_OFFICE, XML_DOCUMENT_SETTINGS,  XML_TOK_DOC_SETTINGS    },
            { XML_NAMESPACE_OOO,    XML_DOCUMENT_SETTINGS,  XML_TOK_DOC_SETTINGS    },
            { XML_NAMESPACE_OFFICE, XML_DOCUMENT_STYLES,    XML_TOK_DOC_STYLES      },
            { XML_NAMESPACE_OOO,    XML_DOCUMENT_STYLES,    XML_TOK_DOC_STYLES      },
            { XML_NAMESPACE_OFFICE, XML_DOCUMENT_CONTENT,   XML_TOK_DOC_CONTENT     },
            { XML_NAMESPACE_OOO,    XML_DOCUMENT_CONTENT,   XML_TOK_DOC_CONTENT     },
            XML_TOKEN_MAP_END
        };
        m_pDocElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDocElemTokenMap;
}

const SvXMLTokenMap& ODBFilter::GetDocContentElemTokenMap() const
{
    if (!m_pDocContentElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_OFFICE, XML_STYLES,             XML_TOK_CONTENT_STYLES      },
            { XML_NAMESPACE_OOO,    XML_STYLES,             XML_TOK_CONTENT_STYLES      },
            { XML_NAMESPACE_OFFICE, XML_AUTOMATIC_STYLES,   XML_TOK_CONTENT_AUTOSTYLES  },
            { XML_NAMESPACE_OOO,    XML_AUTOMATIC_STYLES,   XML_TOK_CONTENT_AUTOSTYLES  },
            { XML_NAMESPACE_OFFICE, XML_SCRIPTS,            XML_TOK_CONTENT_SCRIPTS     },
            { XML_NAMESPACE_OFFICE, XML_BODY,               XML_TOK_CONTENT_BODY        },
            { XML_NAMESPACE_OOO,    XML_BODY,               XML_TOK_CONTENT_BODY        },
            XML_TOKEN_MAP_END
        };
        m_pDocContentElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDocContentElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetDatabaseElemTokenMap() const
{
    if (!m_pDatabaseElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_DATASOURCE,             XML_TOK_DATASOURCE  },
            { XML_NAMESPACE_DB, XML_FORMS,                  XML_TOK_FORMS},
            { XML_NAMESPACE_DB, XML_REPORTS,                XML_TOK_REPORTS},
            { XML_NAMESPACE_DB, XML_QUERIES,                XML_TOK_QUERIES},
            { XML_NAMESPACE_DB, XML_TABLES,                 XML_TOK_TABLES},
            { XML_NAMESPACE_DB, XML_TABLE_REPRESENTATIONS,  XML_TOK_TABLES},
            { XML_NAMESPACE_DB, XML_SCHEMA_DEFINITION,      XML_TOK_SCHEMA_DEFINITION},
            XML_TOKEN_MAP_END
        };
        m_pDatabaseElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDatabaseElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetDataSourceElemTokenMap() const
{
    if (!m_pDataSourceElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB,     XML_CONNECTION_RESOURCE,            XML_TOK_CONNECTION_RESOURCE},
            { XML_NAMESPACE_DB,     XML_SUPPRESS_VERSION_COLUMNS,       XML_TOK_SUPPRESS_VERSION_COLUMNS},
            { XML_NAMESPACE_DB,     XML_JAVA_DRIVER_CLASS,              XML_TOK_JAVA_DRIVER_CLASS},
            { XML_NAMESPACE_DB,     XML_EXTENSION,                      XML_TOK_EXTENSION},
            { XML_NAMESPACE_DB,     XML_IS_FIRST_ROW_HEADER_LINE,       XML_TOK_IS_FIRST_ROW_HEADER_LINE},
            { XML_NAMESPACE_DB,     XML_SHOW_DELETED,                   XML_TOK_SHOW_DELETED},
            { XML_NAMESPACE_DB,     XML_IS_TABLE_NAME_LENGTH_LIMITED,   XML_TOK_IS_TABLE_NAME_LENGTH_LIMITED},
            { XML_NAMESPACE_DB,     XML_SYSTEM_DRIVER_SETTINGS,         XML_TOK_SYSTEM_DRIVER_SETTINGS},
            { XML_NAMESPACE_DB,     XML_ENABLE_SQL92_CHECK,             XML_TOK_ENABLE_SQL92_CHECK},
            { XML_NAMESPACE_DB,     XML_APPEND_TABLE_ALIAS_NAME,        XML_TOK_APPEND_TABLE_ALIAS_NAME},
            { XML_NAMESPACE_DB,     XML_PARAMETER_NAME_SUBSTITUTION,    XML_TOK_PARAMETER_NAME_SUBSTITUTION},
            { XML_NAMESPACE_DB,     XML_IGNORE_DRIVER_PRIVILEGES,       XML_TOK_IGNORE_DRIVER_PRIVILEGES},
            { XML_NAMESPACE_DB,     XML_BOOLEAN_COMPARISON_MODE,        XML_TOK_BOOLEAN_COMPARISON_MODE},
            { XML_NAMESPACE_DB,     XML_USE_CATALOG,                    XML_TOK_USE_CATALOG},
            { XML_NAMESPACE_DB,     XML_BASE_DN,                        XML_TOK_BASE_DN},
            { XML_NAMESPACE_DB,     XML_MAX_ROW_COUNT,                  XML_TOK_MAX_ROW_COUNT},
            { XML_NAMESPACE_DB,     XML_LOGIN,                          XML_TOK_LOGIN},
            { XML_NAMESPACE_DB,     XML_TABLE_FILTER,                   XML_TOK_TABLE_FILTER},
            { XML_NAMESPACE_DB,     XML_TABLE_TYPE_FILTER,              XML_TOK_TABLE_TYPE_FILTER},
            { XML_NAMESPACE_DB,     XML_AUTO_INCREMENT,                 XML_TOK_AUTO_INCREMENT},
            { XML_NAMESPACE_DB,     XML_DELIMITER,                      XML_TOK_DELIMITER},
            { XML_NAMESPACE_DB,     XML_DATA_SOURCE_SETTINGS,           XML_TOK_DATA_SOURCE_SETTINGS},
            { XML_NAMESPACE_DB,     XML_FONT_CHARSET,                   XML_TOK_FONT_CHARSET},
            // db odf 12
            { XML_NAMESPACE_DB,     XML_CONNECTION_DATA,                XML_TOK_CONNECTION_DATA},
            { XML_NAMESPACE_DB,     XML_DATABASE_DESCRIPTION,           XML_TOK_DATABASE_DESCRIPTION},
            { XML_NAMESPACE_DB,     XML_COMPOUND_DATABASE,              XML_TOK_COMPOUND_DATABASE},
            { XML_NAMESPACE_XLINK,  XML_HREF,                           XML_TOK_DB_HREF},
            { XML_NAMESPACE_DB,     XML_MEDIA_TYPE,                     XML_TOK_MEDIA_TYPE},
            { XML_NAMESPACE_DB,     XML_TYPE,                           XML_TOK_DB_TYPE},
            { XML_NAMESPACE_DB,     XML_HOSTNAME,                       XML_TOK_HOSTNAME},
            { XML_NAMESPACE_DB,     XML_PORT,                           XML_TOK_PORT},
            { XML_NAMESPACE_DB,     XML_LOCAL_SOCKET,                   XML_TOK_LOCAL_SOCKET},
            { XML_NAMESPACE_DB,     XML_DATABASE_NAME,                  XML_TOK_DATABASE_NAME},
            { XML_NAMESPACE_DB,     XML_DRIVER_SETTINGS,                XML_TOK_DRIVER_SETTINGS},
            { XML_NAMESPACE_DB,     XML_JAVA_CLASSPATH,                 XML_TOK_JAVA_CLASSPATH},
            { XML_NAMESPACE_DB,     XML_CHARACTER_SET,                  XML_TOK_CHARACTER_SET},
            { XML_NAMESPACE_DB,     XML_APPLICATION_CONNECTION_SETTINGS,XML_TOK_APPLICATION_CONNECTION_SETTINGS},
            XML_TOKEN_MAP_END
        };
        m_pDataSourceElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDataSourceElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetLoginElemTokenMap() const
{
    if (!m_pLoginElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_USER_NAME,              XML_TOK_USER_NAME},
            { XML_NAMESPACE_DB, XML_IS_PASSWORD_REQUIRED,   XML_TOK_IS_PASSWORD_REQUIRED},
            { XML_NAMESPACE_DB, XML_USE_SYSTEM_USER,        XML_TOK_USE_SYSTEM_USER},
            { XML_NAMESPACE_DB, XML_LOGIN_TIMEOUT,          XML_TOK_LOGIN_TIMEOUT},
            XML_TOKEN_MAP_END
        };
        m_pLoginElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pLoginElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetDatabaseDescriptionElemTokenMap() const
{
    if (!m_pDatabaseDescriptionElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_FILE_BASED_DATABASE,    XML_TOK_FILE_BASED_DATABASE},
            { XML_NAMESPACE_DB, XML_SERVER_DATABASE,        XML_TOK_SERVER_DATABASE},
            XML_TOKEN_MAP_END
        };
        m_pDatabaseDescriptionElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDatabaseDescriptionElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetDataSourceInfoElemTokenMap() const
{
    if (!m_pDataSourceInfoElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_ADDITIONAL_COLUMN_STATEMENT,XML_TOK_ADDITIONAL_COLUMN_STATEMENT},
            { XML_NAMESPACE_DB, XML_ROW_RETRIEVING_STATEMENT,   XML_TOK_ROW_RETRIEVING_STATEMENT},
            { XML_NAMESPACE_DB, XML_STRING,                     XML_TOK_STRING},
            { XML_NAMESPACE_DB, XML_FIELD,                      XML_TOK_FIELD},
            { XML_NAMESPACE_DB, XML_DECIMAL,                    XML_TOK_DECIMAL},
            { XML_NAMESPACE_DB, XML_THOUSAND,                   XML_TOK_THOUSAND},
            { XML_NAMESPACE_DB, XML_DATA_SOURCE_SETTING,        XML_TOK_DATA_SOURCE_SETTING},
            { XML_NAMESPACE_DB, XML_DATA_SOURCE_SETTING_VALUE,  XML_TOK_DATA_SOURCE_SETTING_VALUE},
            { XML_NAMESPACE_DB, XML_DATA_SOURCE_SETTING_IS_LIST,XML_TOK_DATA_SOURCE_SETTING_IS_LIST},
            { XML_NAMESPACE_DB, XML_DATA_SOURCE_SETTING_TYPE,   XML_TOK_DATA_SOURCE_SETTING_TYPE},
            { XML_NAMESPACE_DB, XML_DATA_SOURCE_SETTING_NAME,   XML_TOK_DATA_SOURCE_SETTING_NAME},
            { XML_NAMESPACE_DB, XML_FONT_CHARSET,               XML_TOK_FONT_CHARSET},
            { XML_NAMESPACE_DB, XML_ENCODING,                   XML_TOK_ENCODING},
            XML_TOKEN_MAP_END
        };
        m_pDataSourceInfoElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDataSourceInfoElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetDocumentsElemTokenMap() const
{
    if (!m_pDocumentsElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_COMPONENT,              XML_TOK_COMPONENT},
            { XML_NAMESPACE_DB, XML_COMPONENT_COLLECTION,   XML_TOK_COMPONENT_COLLECTION},
            { XML_NAMESPACE_DB, XML_QUERY_COLLECTION,       XML_TOK_QUERY_COLLECTION},
            { XML_NAMESPACE_DB, XML_QUERY,                  XML_TOK_QUERY},
            { XML_NAMESPACE_DB, XML_TABLE,                  XML_TOK_TABLE},
            { XML_NAMESPACE_DB, XML_TABLE_REPRESENTATION,   XML_TOK_TABLE},
            { XML_NAMESPACE_DB, XML_COLUMN,                 XML_TOK_COLUMN},
            XML_TOKEN_MAP_END
        };
        m_pDocumentsElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pDocumentsElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetComponentElemTokenMap() const
{
    if (!m_pComponentElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_XLINK,  XML_HREF,           XML_TOK_HREF    },
            { XML_NAMESPACE_XLINK,  XML_TYPE,           XML_TOK_TYPE    },
            { XML_NAMESPACE_XLINK,  XML_SHOW,           XML_TOK_SHOW    },
            { XML_NAMESPACE_XLINK,  XML_ACTUATE,        XML_TOK_ACTUATE},
            { XML_NAMESPACE_DB, XML_AS_TEMPLATE,    XML_TOK_AS_TEMPLATE },
            { XML_NAMESPACE_DB, XML_NAME,           XML_TOK_COMPONENT_NAME  },
            XML_TOKEN_MAP_END
        };
        m_pComponentElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pComponentElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetQueryElemTokenMap() const
{
    if (!m_pQueryElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_COMMAND,            XML_TOK_COMMAND },
            { XML_NAMESPACE_DB, XML_ESCAPE_PROCESSING,  XML_TOK_ESCAPE_PROCESSING   },
            { XML_NAMESPACE_DB, XML_NAME,               XML_TOK_QUERY_NAME  },
            { XML_NAMESPACE_DB, XML_FILTER_STATEMENT,   XML_TOK_FILTER_STATEMENT    },
            { XML_NAMESPACE_DB, XML_ORDER_STATEMENT,    XML_TOK_ORDER_STATEMENT },
            { XML_NAMESPACE_DB, XML_CATALOG_NAME,       XML_TOK_CATALOG_NAME    },
            { XML_NAMESPACE_DB, XML_SCHEMA_NAME,        XML_TOK_SCHEMA_NAME },
            { XML_NAMESPACE_DB, XML_STYLE_NAME,         XML_TOK_STYLE_NAME},
            { XML_NAMESPACE_DB, XML_APPLY_FILTER,       XML_TOK_APPLY_FILTER},
            { XML_NAMESPACE_DB, XML_APPLY_ORDER,        XML_TOK_APPLY_ORDER},
            { XML_NAMESPACE_DB, XML_COLUMNS,            XML_TOK_COLUMNS},
            XML_TOKEN_MAP_END
        };
        m_pQueryElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pQueryElemTokenMap;
}


const SvXMLTokenMap& ODBFilter::GetColumnElemTokenMap() const
{
    if (!m_pColumnElemTokenMap)
    {
        static const SvXMLTokenMapEntry aElemTokenMap[]=
        {
            { XML_NAMESPACE_DB, XML_NAME,                       XML_TOK_COLUMN_NAME             },
            { XML_NAMESPACE_DB, XML_STYLE_NAME,                 XML_TOK_COLUMN_STYLE_NAME       },
            { XML_NAMESPACE_DB, XML_HELP_MESSAGE,               XML_TOK_COLUMN_HELP_MESSAGE     },
            { XML_NAMESPACE_DB, XML_VISIBILITY,                 XML_TOK_COLUMN_VISIBILITY       },
            { XML_NAMESPACE_DB, XML_DEFAULT_VALUE,              XML_TOK_COLUMN_DEFAULT_VALUE    },
            { XML_NAMESPACE_DB, XML_TYPE_NAME,                  XML_TOK_COLUMN_TYPE_NAME        },
            { XML_NAMESPACE_DB, XML_VISIBLE,                    XML_TOK_COLUMN_VISIBLE          },
            { XML_NAMESPACE_DB, XML_DEFAULT_CELL_STYLE_NAME,    XML_TOK_DEFAULT_CELL_STYLE_NAME },
            XML_TOKEN_MAP_END
        };
        m_pColumnElemTokenMap.reset(new SvXMLTokenMap( aElemTokenMap ));
    }
    return *m_pColumnElemTokenMap;
}


SvXMLImportContext* ODBFilter::CreateStylesContext(sal_uInt16 _nPrefix,const OUString& rLocalName,
                                     const uno::Reference< XAttributeList>& xAttrList, bool bIsAutoStyle )
{
    SvXMLImportContext *pContext = new OTableStylesContext(*this, _nPrefix, rLocalName, xAttrList, bIsAutoStyle);
    if (bIsAutoStyle)
        SetAutoStyles(static_cast<SvXMLStylesContext*>(pContext));
    else
        SetStyles(static_cast<SvXMLStylesContext*>(pContext));

    return pContext;
}


rtl::Reference < XMLPropertySetMapper > const & ODBFilter::GetTableStylesPropertySetMapper() const
{
    if ( !m_xTableStylesPropertySetMapper.is() )
    {
        m_xTableStylesPropertySetMapper = OXMLHelper::GetTableStylesPropertySetMapper( false);
    }
    return m_xTableStylesPropertySetMapper;
}


rtl::Reference < XMLPropertySetMapper > const & ODBFilter::GetColumnStylesPropertySetMapper() const
{
    if ( !m_xColumnStylesPropertySetMapper.is() )
    {
        m_xColumnStylesPropertySetMapper = OXMLHelper::GetColumnStylesPropertySetMapper( false);
    }
    return m_xColumnStylesPropertySetMapper;
}


rtl::Reference < XMLPropertySetMapper > const & ODBFilter::GetCellStylesPropertySetMapper() const
{
    if ( !m_xCellStylesPropertySetMapper.is() )
    {
        m_xCellStylesPropertySetMapper = OXMLHelper::GetCellStylesPropertySetMapper( false);
    }
    return m_xCellStylesPropertySetMapper;
}


void ODBFilter::setPropertyInfo()
{
    Reference<XPropertySet> xDataSource(getDataSource());
    if ( !xDataSource.is() )
        return;

    ::connectivity::DriversConfig aDriverConfig(GetComponentContext());
    const OUString sURL = ::comphelper::getString(xDataSource->getPropertyValue(PROPERTY_URL));
    ::comphelper::NamedValueCollection aDataSourceSettings = aDriverConfig.getProperties( sURL );

    Sequence<PropertyValue> aInfo;
    if ( !m_aInfoSequence.empty() )
        aInfo = comphelper::containerToSequence(m_aInfoSequence);
    aDataSourceSettings.merge( ::comphelper::NamedValueCollection( aInfo ), true );

    aDataSourceSettings >>= aInfo;
    if ( aInfo.hasElements() )
    {
        try
        {
            xDataSource->setPropertyValue(PROPERTY_INFO,makeAny(aInfo));
        }
        catch (const Exception&)
        {
            DBG_UNHANDLED_EXCEPTION("dbaccess");
        }
    }
}

} // namespace dbaxml

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
