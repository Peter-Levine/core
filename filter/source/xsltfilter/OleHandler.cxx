/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */


#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <vector>
#include <iostream>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlIO.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <libxslt/variables.h>

#include <rtl/ustrbuf.hxx>

#include <sax/tools/converter.hxx>

#include <package/Inflater.hxx>
#include <package/Deflater.hxx>

#include <cppuhelper/factory.hxx>
#include <comphelper/base64.hxx>
#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/XInterface.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/io/TempFile.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>

#include "OleHandler.hxx"
#include <memory>

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::embed;


namespace XSLT
{
    Reference<XStream> OleHandler::createTempFile() {
        Reference<XStream> tempFile = TempFile::create(m_xContext);
        OSL_ASSERT(tempFile.is());
        return tempFile;
    }

    void OleHandler::ensureCreateRootStorage()
    {
        if (m_storage == nullptr || m_rootStream == nullptr)
        {
            m_rootStream = createTempFile();
            Sequence<Any> args(1);
            args[0] <<= m_rootStream->getInputStream();

            Reference<XNameContainer> cont(
                 Reference<XMultiServiceFactory>(m_xContext->getServiceManager(), UNO_QUERY_THROW)
                     ->createInstanceWithArguments("com.sun.star.embed.OLESimpleStorage", args), UNO_QUERY);
            m_storage = cont;
        }
    }

    void OleHandler::initRootStorageFromBase64(const OString& content)
    {
        Sequence<sal_Int8> oleData;
        ::comphelper::Base64::decode(oleData, OStringToOUString(
            content, RTL_TEXTENCODING_UTF8));
        m_rootStream = createTempFile();
        Reference<XOutputStream> xOutput = m_rootStream->getOutputStream();
        xOutput->writeBytes(oleData);
        xOutput->flush();
        //Get the input stream and seek to begin
        Reference<XSeekable> xSeek(m_rootStream->getInputStream(), UNO_QUERY);
        xSeek->seek(0);

        //create a com.sun.star.embed.OLESimpleStorage from the temp stream
        Sequence<Any> args(1);
        args[0] <<= xSeek;
        Reference<XNameContainer> cont(
             Reference<XMultiServiceFactory>(m_xContext->getServiceManager(), UNO_QUERY_THROW)
                 ->createInstanceWithArguments("com.sun.star.embed.OLESimpleStorage", args), UNO_QUERY);
        m_storage = cont;
    }

    OString
    OleHandler::encodeSubStorage(const OUString& streamName)
    {
        if (!m_storage || !m_storage->hasByName(streamName))
        {
            return "Not Found:";// + streamName;
        }

        Reference<XInputStream> subStream(m_storage->getByName(streamName), UNO_QUERY);
        if (!subStream.is())
        {
            return "Not Found:";// + streamName;
        }
        //The first four byte are the length of the uncompressed data
        Sequence<sal_Int8> aLength(4);
        Reference<XSeekable> xSeek(subStream, UNO_QUERY);
        xSeek->seek(0);
        //Get the uncompressed length
        int readbytes = subStream->readBytes(aLength, 4);
        if (4 != readbytes)
        {
            return "Can not read the length.";
        }
        sal_Int32 const oleLength = (static_cast<sal_uInt8>(aLength[0]) <<  0U)
                                  | (static_cast<sal_uInt8>(aLength[1]) <<  8U)
                                  | (static_cast<sal_uInt8>(aLength[2]) << 16U)
                                  | (static_cast<sal_uInt8>(aLength[3]) << 24U);
        if (oleLength < 0)
        {
            return "invalid oleLength";
        }
        Sequence<sal_Int8> content(oleLength);
        //Read all bytes. The compressed length should less then the uncompressed length
        readbytes = subStream->readBytes(content, oleLength);
        if (oleLength < readbytes)
        {
            return "oleLength";// +oleLength + readBytes;
        }

        // Decompress the bytes
        std::unique_ptr< ::ZipUtils::Inflater> decompresser(new ::ZipUtils::Inflater(false));
        decompresser->setInput(content);
        Sequence<sal_Int8> result(oleLength);
        decompresser->doInflateSegment(result, 0, oleLength);
        decompresser->end();
        decompresser.reset();
        //return the base64 string of the uncompressed data
        OUStringBuffer buf(oleLength);
        ::comphelper::Base64::encode(buf, result);
        return OUStringToOString(buf.toString(), RTL_TEXTENCODING_UTF8);
    }

    void
    OleHandler::insertByName(const OUString& streamName, const OString& content)
    {
        if ( streamName == "oledata.mso" )
        {
            initRootStorageFromBase64(content);
        }
        else
        {
            ensureCreateRootStorage();
            insertSubStorage(streamName, content);
        }
    }

    const OString
    OleHandler::getByName(const OUString& streamName)
    {
        if ( streamName == "oledata.mso" )
        {
            //get the length and seek to 0
            Reference<XSeekable> xSeek (m_rootStream, UNO_QUERY);
            int oleLength = static_cast<int>(xSeek->getLength());
            xSeek->seek(0);
            //read all bytes
            Reference<XInputStream> xInput = m_rootStream->getInputStream();
            Sequence<sal_Int8> oledata(oleLength);
            xInput->readBytes(oledata, oleLength);
            //return the base64 encoded string
            OUStringBuffer buf(oleLength);
            ::comphelper::Base64::encode(buf, oledata);
            return OUStringToOString(buf.toString(), RTL_TEXTENCODING_UTF8);
        }
        return encodeSubStorage(streamName);
    }

    void
    OleHandler::insertSubStorage(const OUString& streamName, const OString& content)
    {
        //decode the base64 string
        Sequence<sal_Int8> oledata;
        ::comphelper::Base64::decode(oledata,
                OStringToOUString(content, RTL_TEXTENCODING_ASCII_US));
        //create a temp stream to write data to
        Reference<XStream> subStream = createTempFile();
        Reference<XInputStream> xInput = subStream->getInputStream();
        Reference<XOutputStream> xOutput = subStream->getOutputStream();
        //write the length to the temp stream
        Sequence<sal_Int8> header(4);
        header[0] = static_cast<sal_Int8>(oledata.getLength() >> 0) & 0xFF;
        header[1] = static_cast<sal_Int8>(oledata.getLength() >> 8) & 0xFF;
        header[2] = static_cast<sal_Int8>(oledata.getLength() >> 16) & 0xFF;
        header[3] = static_cast<sal_Int8>(oledata.getLength() >> 24) & 0xFF;
        xOutput->writeBytes(header);

        // Compress the bytes
        Sequence<sal_Int8> output(oledata.getLength());
        std::unique_ptr< ::ZipUtils::Deflater> compresser(new ::ZipUtils::Deflater(sal_Int32(3), false));
        compresser->setInputSegment(oledata);
        compresser->finish();
        int compressedDataLength = compresser->doDeflateSegment(output, oledata.getLength());
        compresser.reset();
        //realloc the data length
        Sequence<sal_Int8> compressed(compressedDataLength);
        for (int i = 0; i < compressedDataLength; i++) {
            compressed[i] = output[i];
        }

        //write the compressed data to the temp stream
        xOutput->writeBytes(compressed);
        //seek to 0
        Reference<XSeekable> xSeek(xInput, UNO_QUERY);
        xSeek->seek(0);

        //insert the temp stream as a sub stream and use an XTransactedObject to commit it immediately
        Reference<XTransactedObject> xTransact(m_storage, UNO_QUERY);
        Any entry;
        entry <<= xInput;
        m_storage->insertByName(streamName, entry);
        xTransact->commit();
    }


}
/* vim:set shiftwidth=4 softtabstop=4 expandtab: */

