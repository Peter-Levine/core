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

#include <memory>
#include <MarkManager.hxx>
#include <bookmrk.hxx>
#include <cntfrm.hxx>
#include <crossrefbookmark.hxx>
#include <annotationmark.hxx>
#include <dcontact.hxx>
#include <doc.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentState.hxx>
#include <IDocumentUndoRedo.hxx>
#include <docary.hxx>
#include <xmloff/odffields.hxx>
#include <editsh.hxx>
#include <fmtanchr.hxx>
#include <frmfmt.hxx>
#include <functional>
#include <hintids.hxx>
#include <mvsave.hxx>
#include <ndtxt.hxx>
#include <node.hxx>
#include <pam.hxx>
#include <redline.hxx>
#include <rolbck.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>
#include <sal/log.hxx>
#include <sortedobjs.hxx>
#include <sfx2/linkmgr.hxx>
#include <swserv.hxx>
#include <swundo.hxx>
#include <UndoBookmark.hxx>
#include <unocrsr.hxx>
#include <viscrs.hxx>
#include <edimp.hxx>
#include <tools/datetimeutils.hxx>
#include <view.hxx>

using namespace ::sw::mark;

std::vector<::sw::mark::MarkBase*>::const_iterator const&
IDocumentMarkAccess::iterator::get() const
{
    return *m_pIter;
}

IDocumentMarkAccess::iterator::iterator(std::vector<::sw::mark::MarkBase*>::const_iterator const& rIter)
    : m_pIter(new std::vector<::sw::mark::MarkBase*>::const_iterator(rIter))
{
}

IDocumentMarkAccess::iterator::iterator(iterator const& rOther)
    : m_pIter(new std::vector<::sw::mark::MarkBase*>::const_iterator(*rOther.m_pIter))
{
}

auto IDocumentMarkAccess::iterator::operator=(iterator const& rOther) -> iterator&
{
    m_pIter.reset(new std::vector<::sw::mark::MarkBase*>::const_iterator(*rOther.m_pIter));
    return *this;
}

IDocumentMarkAccess::iterator::iterator(iterator && rOther)
    : m_pIter(std::move(rOther.m_pIter))
{
}

auto IDocumentMarkAccess::iterator::operator=(iterator && rOther) -> iterator&
{
    m_pIter = std::move(rOther.m_pIter);
    return *this;
}

IDocumentMarkAccess::iterator::~iterator()
{
}

// ARGH why does it *need* to return const& ?
::sw::mark::IMark* /*const&*/
IDocumentMarkAccess::iterator::operator*() const
{
    return static_cast<sw::mark::IMark*>(**m_pIter);
}

auto IDocumentMarkAccess::iterator::operator++() -> iterator&
{
    ++(*m_pIter);
    return *this;
}
auto IDocumentMarkAccess::iterator::operator++(int) -> iterator
{
    iterator tmp(*this);
    ++(*m_pIter);
    return tmp;
}

bool IDocumentMarkAccess::iterator::operator==(iterator const& rOther) const
{
    return *m_pIter == *rOther.m_pIter;
}

bool IDocumentMarkAccess::iterator::operator!=(iterator const& rOther) const
{
    return *m_pIter != *rOther.m_pIter;
}

IDocumentMarkAccess::iterator::iterator()
    : m_pIter(new std::vector<::sw::mark::MarkBase*>::const_iterator())
{
}

auto IDocumentMarkAccess::iterator::operator--() -> iterator&
{
    --(*m_pIter);
    return *this;
}

auto IDocumentMarkAccess::iterator::operator--(int) -> iterator
{
    iterator tmp(*this);
    --(*m_pIter);
    return tmp;
}

auto IDocumentMarkAccess::iterator::operator+=(difference_type const n) -> iterator&
{
    (*m_pIter) += n;
    return *this;
}

auto IDocumentMarkAccess::iterator::operator+(difference_type const n) const -> iterator
{
    return iterator(*m_pIter + n);
}

auto IDocumentMarkAccess::iterator::operator-=(difference_type const n) -> iterator&
{
    (*m_pIter) -= n;
    return *this;
}

auto IDocumentMarkAccess::iterator::operator-(difference_type const n) const -> iterator
{
    return iterator(*m_pIter - n);
}

auto IDocumentMarkAccess::iterator::operator-(iterator const& rOther) const -> difference_type
{
    return *m_pIter - *rOther.m_pIter;
}

auto IDocumentMarkAccess::iterator::operator[](difference_type const n) const -> value_type
{
    return static_cast<sw::mark::IMark*>((*m_pIter)[n]);
}

bool IDocumentMarkAccess::iterator::operator<(iterator const& rOther) const
{
    return *m_pIter < *rOther.m_pIter;
}
bool IDocumentMarkAccess::iterator::operator>(iterator const& rOther) const
{
    return *m_pIter > *rOther.m_pIter;
}
bool IDocumentMarkAccess::iterator::operator<=(iterator const& rOther) const
{
    return *m_pIter <= *rOther.m_pIter;
}
bool IDocumentMarkAccess::iterator::operator>=(iterator const& rOther) const
{
    return *m_pIter >= *rOther.m_pIter;
}


namespace
{
    bool lcl_GreaterThan( const SwPosition& rPos, const SwNodeIndex& rNdIdx, const SwIndex* pIdx )
    {
        return pIdx != nullptr
               ? ( rPos.nNode > rNdIdx
                   || ( rPos.nNode == rNdIdx
                        && rPos.nContent >= pIdx->GetIndex() ) )
               : rPos.nNode >= rNdIdx;
    }

    bool lcl_Lower( const SwPosition& rPos, const SwNodeIndex& rNdIdx, const SwIndex* pIdx )
    {
        return rPos.nNode < rNdIdx
               || ( pIdx != nullptr
                    && rPos.nNode == rNdIdx
                    && rPos.nContent < pIdx->GetIndex() );
    }

    bool lcl_MarkOrderingByStart(const ::sw::mark::MarkBase *const pFirst,
                                 const ::sw::mark::MarkBase *const pSecond)
    {
        auto const& rFirstStart(pFirst->GetMarkStart());
        auto const& rSecondStart(pSecond->GetMarkStart());
        if (rFirstStart.nNode != rSecondStart.nNode)
        {
            return rFirstStart.nNode < rSecondStart.nNode;
        }
        const sal_Int32 nFirstContent = rFirstStart.nContent.GetIndex();
        const sal_Int32 nSecondContent = rSecondStart.nContent.GetIndex();
        if (nFirstContent != 0 || nSecondContent != 0)
        {
            return nFirstContent < nSecondContent;
        }
        auto *const pCRFirst (dynamic_cast<::sw::mark::CrossRefBookmark const*>(pFirst));
        auto *const pCRSecond(dynamic_cast<::sw::mark::CrossRefBookmark const*>(pSecond));
        if ((pCRFirst == nullptr) == (pCRSecond == nullptr))
        {
            return false; // equal
        }
        return pCRFirst != nullptr; // cross-ref sorts *before*
    }

    bool lcl_MarkOrderingByEnd(const ::sw::mark::MarkBase *const pFirst,
                               const ::sw::mark::MarkBase *const pSecond)
    {
        return pFirst->GetMarkEnd() < pSecond->GetMarkEnd();
    }

    void lcl_InsertMarkSorted(MarkManager::container_t& io_vMarks,
                              ::sw::mark::MarkBase *const pMark)
    {
        io_vMarks.insert(
            lower_bound(
                io_vMarks.begin(),
                io_vMarks.end(),
                pMark,
                &lcl_MarkOrderingByStart),
            pMark);
    }

    std::unique_ptr<SwPosition> lcl_PositionFromContentNode(
        SwContentNode * const pContentNode,
        const bool bAtEnd)
    {
        std::unique_ptr<SwPosition> pResult(new SwPosition(*pContentNode));
        pResult->nContent.Assign(pContentNode, bAtEnd ? pContentNode->Len() : 0);
        return pResult;
    }

    // return a position at the begin of rEnd, if it is a ContentNode
    // else set it to the begin of the Node after rEnd, if there is one
    // else set it to the end of the node before rStt
    // else set it to the ContentNode of the Pos outside the Range
    std::unique_ptr<SwPosition> lcl_FindExpelPosition(
        const SwNodeIndex& rStt,
        const SwNodeIndex& rEnd,
        const SwPosition& rOtherPosition)
    {
        SwContentNode * pNode = rEnd.GetNode().GetContentNode();
        bool bPosAtEndOfNode = false;
        if ( pNode == nullptr)
        {
            SwNodeIndex aEnd = rEnd;
            pNode = rEnd.GetNodes().GoNext( &aEnd );
            bPosAtEndOfNode = false;
        }
        if ( pNode == nullptr )
        {
            SwNodeIndex aStt = rStt;
            pNode = SwNodes::GoPrevious(&aStt);
            bPosAtEndOfNode = true;
        }
        if ( pNode != nullptr )
        {
            return lcl_PositionFromContentNode( pNode, bPosAtEndOfNode );
        }

        return std::make_unique<SwPosition>(rOtherPosition);
    }

    struct CompareIMarkStartsBefore
    {
        bool operator()(SwPosition const& rPos,
                        const sw::mark::IMark* pMark)
        {
            return rPos < pMark->GetMarkStart();
        }
        bool operator()(const sw::mark::IMark* pMark,
                        SwPosition const& rPos)
        {
            return pMark->GetMarkStart() < rPos;
        }
    };

    // Apple llvm-g++ 4.2.1 with _GLIBCXX_DEBUG won't eat boost::bind for this
    // Neither will MSVC 2008 with _DEBUG
    struct CompareIMarkStartsAfter
    {
        bool operator()(SwPosition const& rPos,
                        const sw::mark::IMark* pMark)
        {
            return pMark->GetMarkStart() > rPos;
        }
    };


    IMark* lcl_getMarkAfter(const MarkManager::container_t& rMarks, const SwPosition& rPos)
    {
        auto const pMarkAfter = upper_bound(
            rMarks.begin(),
            rMarks.end(),
            rPos,
            CompareIMarkStartsAfter());
        if(pMarkAfter == rMarks.end())
            return nullptr;
        return *pMarkAfter;
    };

    IMark* lcl_getMarkBefore(const MarkManager::container_t& rMarks, const SwPosition& rPos)
    {
        // candidates from which to choose the mark before
        MarkManager::container_t vCandidates;
        // no need to consider marks starting after rPos
        auto const pCandidatesEnd = upper_bound(
            rMarks.begin(),
            rMarks.end(),
            rPos,
            CompareIMarkStartsAfter());
        vCandidates.reserve(pCandidatesEnd - rMarks.begin());
        // only marks ending before are candidates
        remove_copy_if(
            rMarks.begin(),
            pCandidatesEnd,
            back_inserter(vCandidates),
            [&rPos] (const ::sw::mark::MarkBase *const pMark) { return !(pMark->GetMarkEnd() < rPos); } );
        // no candidate left => we are in front of the first mark or there are none
        if(vCandidates.empty()) return nullptr;
        // return the highest (last) candidate using mark end ordering
        return *max_element(vCandidates.begin(), vCandidates.end(), &lcl_MarkOrderingByEnd);
    }

    bool lcl_FixCorrectedMark(
        const bool bChangedPos,
        const bool bChangedOPos,
        MarkBase* io_pMark )
    {
        if ( IDocumentMarkAccess::GetType(*io_pMark) == IDocumentMarkAccess::MarkType::ANNOTATIONMARK )
        {
            // annotation marks are allowed to span a table cell range.
            // but trigger sorting to be save
            return true;
        }

        if ( ( bChangedPos || bChangedOPos )
             && io_pMark->IsExpanded()
             && io_pMark->GetOtherMarkPos().nNode.GetNode().FindTableBoxStartNode() !=
                    io_pMark->GetMarkPos().nNode.GetNode().FindTableBoxStartNode() )
        {
            if ( !bChangedOPos )
            {
                io_pMark->SetMarkPos( io_pMark->GetOtherMarkPos() );
            }
            io_pMark->ClearOtherMarkPos();
            DdeBookmark * const pDdeBkmk = dynamic_cast< DdeBookmark*>(io_pMark);
            if ( pDdeBkmk != nullptr
                 && pDdeBkmk->IsServer() )
            {
                pDdeBkmk->SetRefObject(nullptr);
            }
            return true;
        }
        return false;
    }

    bool lcl_MarkEqualByStart(const ::sw::mark::MarkBase *const pFirst,
                              const ::sw::mark::MarkBase *const pSecond)
    {
        return !lcl_MarkOrderingByStart(pFirst, pSecond) &&
               !lcl_MarkOrderingByStart(pSecond, pFirst);
    }

    MarkManager::container_t::const_iterator lcl_FindMark(
        MarkManager::container_t& rMarks,
        const ::sw::mark::MarkBase *const pMarkToFind)
    {
        auto ppCurrentMark = lower_bound(
            rMarks.begin(), rMarks.end(),
            pMarkToFind, &lcl_MarkOrderingByStart);
        // since there are usually not too many marks on the same start
        // position, we are not doing a bisect search for the upper bound
        // but instead start to iterate from pMarkLow directly
        while (ppCurrentMark != rMarks.end() && lcl_MarkEqualByStart(*ppCurrentMark, pMarkToFind))
        {
            if(*ppCurrentMark == pMarkToFind)
            {
                return ppCurrentMark;
            }
            ++ppCurrentMark;
        }
        // reached a mark starting on a later start pos or the end of the
        // vector => not found
        return rMarks.end();
    };

    MarkManager::container_t::const_iterator lcl_FindMarkAtPos(
        MarkManager::container_t& rMarks,
        const SwPosition& rPos,
        const IDocumentMarkAccess::MarkType eType)
    {
        for (auto ppCurrentMark = lower_bound(
                rMarks.begin(), rMarks.end(),
                rPos,
                CompareIMarkStartsBefore());
            ppCurrentMark != rMarks.end();
            ++ppCurrentMark)
        {
            // Once we reach a mark starting after the target pos
            // we do not need to continue
            if((*ppCurrentMark)->GetMarkStart() > rPos)
                break;
            if(IDocumentMarkAccess::GetType(**ppCurrentMark) == eType)
            {
                return ppCurrentMark;
            }
        }
        // reached a mark starting on a later start pos or the end of the
        // vector => not found
        return rMarks.end();
    };

    MarkManager::container_t::const_iterator lcl_FindMarkByName(
        const OUString& rName,
        const MarkManager::container_t::const_iterator& ppMarksBegin,
        const MarkManager::container_t::const_iterator& ppMarksEnd)
    {
        return find_if(
            ppMarksBegin,
            ppMarksEnd,
            [&rName] (::sw::mark::MarkBase const*const pMark) { return pMark->GetName() == rName; } );
    }

    void lcl_DebugMarks(MarkManager::container_t const& rMarks)
    {
#if OSL_DEBUG_LEVEL > 0
        SAL_INFO("sw.core", rMarks.size() << " Marks");
        for (auto ppMark = rMarks.begin();
             ppMark != rMarks.end();
             ++ppMark)
        {
            IMark* pMark = *ppMark;
            const SwPosition* const pStPos = &pMark->GetMarkStart();
            const SwPosition* const pEndPos = &pMark->GetMarkEnd();
            SAL_INFO("sw.core",
                pStPos->nNode.GetIndex() << "," <<
                pStPos->nContent.GetIndex() << " " <<
                pEndPos->nNode.GetIndex() << "," <<
                pEndPos->nContent.GetIndex() << " " <<
                typeid(*pMark).name() << " " <<
                pMark->GetName());
        }
#else
        (void) rMarks;
#endif
        assert(std::is_sorted(rMarks.begin(), rMarks.end(), lcl_MarkOrderingByStart));
    };
}

IDocumentMarkAccess::MarkType IDocumentMarkAccess::GetType(const IMark& rBkmk)
{
    const std::type_info* const pMarkTypeInfo = &typeid(rBkmk);
    // not using dynamic_cast<> here for performance
    if(*pMarkTypeInfo == typeid(UnoMark))
        return MarkType::UNO_BOOKMARK;
    else if(*pMarkTypeInfo == typeid(DdeBookmark))
        return MarkType::DDE_BOOKMARK;
    else if(*pMarkTypeInfo == typeid(Bookmark))
        return MarkType::BOOKMARK;
    else if(*pMarkTypeInfo == typeid(CrossRefHeadingBookmark))
        return MarkType::CROSSREF_HEADING_BOOKMARK;
    else if(*pMarkTypeInfo == typeid(CrossRefNumItemBookmark))
        return MarkType::CROSSREF_NUMITEM_BOOKMARK;
    else if(*pMarkTypeInfo == typeid(AnnotationMark))
        return MarkType::ANNOTATIONMARK;
    else if(*pMarkTypeInfo == typeid(TextFieldmark))
        return MarkType::TEXT_FIELDMARK;
    else if(*pMarkTypeInfo == typeid(CheckboxFieldmark))
        return MarkType::CHECKBOX_FIELDMARK;
    else if(*pMarkTypeInfo == typeid(DropDownFieldmark))
        return MarkType::DROPDOWN_FIELDMARK;
    else if(*pMarkTypeInfo == typeid(DateFieldmark))
        return MarkType::DATE_FIELDMARK;
    else if(*pMarkTypeInfo == typeid(NavigatorReminder))
        return MarkType::NAVIGATOR_REMINDER;
    else
    {
        assert(false && "IDocumentMarkAccess::GetType(..)"
            " - unknown MarkType. This needs to be fixed!");
        return MarkType::UNO_BOOKMARK;
    }
}

OUString IDocumentMarkAccess::GetCrossRefHeadingBookmarkNamePrefix()
{
    return "__RefHeading__";
}

bool IDocumentMarkAccess::IsLegalPaMForCrossRefHeadingBookmark( const SwPaM& rPaM )
{
    return rPaM.Start()->nNode.GetNode().IsTextNode() &&
           rPaM.Start()->nContent.GetIndex() == 0 &&
           ( !rPaM.HasMark() ||
             ( rPaM.GetMark()->nNode == rPaM.GetPoint()->nNode &&
               rPaM.End()->nContent.GetIndex() == rPaM.End()->nNode.GetNode().GetTextNode()->Len() ) );
}

namespace sw { namespace mark
{
    MarkManager::MarkManager(SwDoc& rDoc)
        : m_vAllMarks()
        , m_vBookmarks()
        , m_vFieldmarks()
        , m_vAnnotationMarks()
        , m_pDoc(&rDoc)
        , m_pLastActiveFieldmark(nullptr)
    { }

    ::sw::mark::IMark* MarkManager::makeMark(const SwPaM& rPaM,
        const OUString& rName,
        const IDocumentMarkAccess::MarkType eType,
        sw::mark::InsertMode const eMode)
    {
#if OSL_DEBUG_LEVEL > 0
        {
            const SwPosition* const pPos1 = rPaM.GetPoint();
            const SwPosition* pPos2 = pPos1;
            if(rPaM.HasMark())
                pPos2 = rPaM.GetMark();
            SAL_INFO("sw.core",
                rName << " " <<
                pPos1->nNode.GetIndex() << "," <<
                pPos1->nContent.GetIndex() << " " <<
                pPos2->nNode.GetIndex() << "," <<
                pPos2->nContent.GetIndex());
        }
#endif
        // There should only be one CrossRefBookmark per Textnode per Type
        if ((eType == MarkType::CROSSREF_NUMITEM_BOOKMARK || eType == MarkType::CROSSREF_HEADING_BOOKMARK)
            && (lcl_FindMarkAtPos(m_vBookmarks, *rPaM.Start(), eType) != m_vBookmarks.end()))
        {   // this can happen via UNO API
            SAL_WARN("sw.core", "MarkManager::makeMark(..)"
                " - refusing to create duplicate CrossRefBookmark");
            return nullptr;
        }

        // create mark
        std::unique_ptr<::sw::mark::MarkBase> pMark;
        switch(eType)
        {
            case IDocumentMarkAccess::MarkType::TEXT_FIELDMARK:
                pMark = std::make_unique<TextFieldmark>(rPaM, rName);
                break;
            case IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK:
                pMark = std::make_unique<CheckboxFieldmark>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK:
                pMark = std::make_unique<DropDownFieldmark>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::DATE_FIELDMARK:
                pMark = std::make_unique<DateFieldmark>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER:
                pMark = std::make_unique<NavigatorReminder>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::BOOKMARK:
                pMark = std::make_unique<Bookmark>(rPaM, vcl::KeyCode(), rName);
                break;
            case IDocumentMarkAccess::MarkType::DDE_BOOKMARK:
                pMark = std::make_unique<DdeBookmark>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
                pMark = std::make_unique<CrossRefHeadingBookmark>(rPaM, vcl::KeyCode(), rName);
                break;
            case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
                pMark = std::make_unique<CrossRefNumItemBookmark>(rPaM, vcl::KeyCode(), rName);
                break;
            case IDocumentMarkAccess::MarkType::UNO_BOOKMARK:
                pMark = std::make_unique<UnoMark>(rPaM);
                break;
            case IDocumentMarkAccess::MarkType::ANNOTATIONMARK:
                pMark = std::make_unique<AnnotationMark>( rPaM, rName );
                break;
        }
        assert(pMark.get() &&
            "MarkManager::makeMark(..)"
            " - Mark was not created.");

        if(pMark->GetMarkPos() != pMark->GetMarkStart())
            pMark->Swap();

        // for performance reasons, we trust UnoMarks to have a (generated) unique name
        if ( eType != IDocumentMarkAccess::MarkType::UNO_BOOKMARK )
            pMark->SetName( getUniqueMarkName( pMark->GetName() ) );

        // register mark
        lcl_InsertMarkSorted(m_vAllMarks, pMark.get());
        switch(eType)
        {
            case IDocumentMarkAccess::MarkType::BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
                lcl_InsertMarkSorted(m_vBookmarks, pMark.get());
                break;
            case IDocumentMarkAccess::MarkType::TEXT_FIELDMARK:
            case IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DATE_FIELDMARK:
                lcl_InsertMarkSorted(m_vFieldmarks, pMark.get());
                break;
            case IDocumentMarkAccess::MarkType::ANNOTATIONMARK:
                lcl_InsertMarkSorted( m_vAnnotationMarks, pMark.get() );
                break;
            case IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER:
            case IDocumentMarkAccess::MarkType::DDE_BOOKMARK:
            case IDocumentMarkAccess::MarkType::UNO_BOOKMARK:
                // no special array for these
                break;
        }
        pMark->InitDoc(m_pDoc, eMode);
        SAL_INFO("sw.core", "--- makeType ---");
        SAL_INFO("sw.core", "Marks");
        lcl_DebugMarks(m_vAllMarks);
        SAL_INFO("sw.core", "Bookmarks");
        lcl_DebugMarks(m_vBookmarks);
        SAL_INFO("sw.core", "Fieldmarks");
        lcl_DebugMarks(m_vFieldmarks);

        return pMark.release();
    }

    ::sw::mark::IFieldmark* MarkManager::makeFieldBookmark(
        const SwPaM& rPaM,
        const OUString& rName,
        const OUString& rType )
    {
        // Disable undo, because we handle it using SwUndoInsTextFieldmark
        bool bUndoIsEnabled = m_pDoc->GetIDocumentUndoRedo().DoesUndo();
        m_pDoc->GetIDocumentUndoRedo().DoUndo(false);

        sw::mark::IMark* pMark = nullptr;
        if(rType == ODF_FORMDATE)
        {
            pMark = makeMark(rPaM, rName,
                             IDocumentMarkAccess::MarkType::DATE_FIELDMARK,
                              sw::mark::InsertMode::New);
        }
        else
        {
            pMark = makeMark(rPaM, rName,
                             IDocumentMarkAccess::MarkType::TEXT_FIELDMARK,
                             sw::mark::InsertMode::New);
        }
        sw::mark::IFieldmark* pFieldMark = dynamic_cast<sw::mark::IFieldmark*>( pMark );
        if (pFieldMark)
            pFieldMark->SetFieldname( rType );

        if (bUndoIsEnabled)
        {
            m_pDoc->GetIDocumentUndoRedo().DoUndo(bUndoIsEnabled);
            if (pFieldMark)
                m_pDoc->GetIDocumentUndoRedo().AppendUndo(std::make_unique<SwUndoInsTextFieldmark>(*pFieldMark));
        }

        return pFieldMark;
    }

    ::sw::mark::IFieldmark* MarkManager::makeNoTextFieldBookmark(
        const SwPaM& rPaM,
        const OUString& rName,
        const OUString& rType)
    {
        // Disable undo, because we handle it using SwUndoInsNoTextFieldmark
        bool bUndoIsEnabled = m_pDoc->GetIDocumentUndoRedo().DoesUndo();
        m_pDoc->GetIDocumentUndoRedo().DoUndo(false);

        bool bEnableSetModified = m_pDoc->getIDocumentState().IsEnableSetModified();
        m_pDoc->getIDocumentState().SetEnableSetModified(false);

        sw::mark::IMark* pMark = nullptr;
        if(rType == ODF_FORMCHECKBOX)
        {
            pMark = makeMark( rPaM, rName,
                    IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK,
                    sw::mark::InsertMode::New);
        }
        else if(rType == ODF_FORMDROPDOWN)
        {
            pMark = makeMark( rPaM, rName,
                    IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK,
                    sw::mark::InsertMode::New);
        }
        else if(rType == ODF_FORMDATE)
        {
            pMark = makeMark( rPaM, rName,
                    IDocumentMarkAccess::MarkType::DATE_FIELDMARK,
                    sw::mark::InsertMode::New);
        }

        sw::mark::IFieldmark* pFieldMark = dynamic_cast<sw::mark::IFieldmark*>( pMark );
        if (pFieldMark)
            pFieldMark->SetFieldname( rType );

        if (bUndoIsEnabled)
        {
            m_pDoc->GetIDocumentUndoRedo().DoUndo(bUndoIsEnabled);
            if (pFieldMark)
                m_pDoc->GetIDocumentUndoRedo().AppendUndo(std::make_unique<SwUndoInsNoTextFieldmark>(*pFieldMark));
        }

        m_pDoc->getIDocumentState().SetEnableSetModified(bEnableSetModified);
        m_pDoc->getIDocumentState().SetModified();

        return pFieldMark;
    }

    ::sw::mark::IMark* MarkManager::getMarkForTextNode(
        const SwTextNode& rTextNode,
        const IDocumentMarkAccess::MarkType eType )
    {
        SwPosition aPos(rTextNode);
        aPos.nContent.Assign(&const_cast<SwTextNode&>(rTextNode), 0);
        auto const ppExistingMark = lcl_FindMarkAtPos(m_vBookmarks, aPos, eType);
        if(ppExistingMark != m_vBookmarks.end())
            return *ppExistingMark;
        const SwPaM aPaM(aPos);
        return makeMark(aPaM, OUString(), eType, sw::mark::InsertMode::New);
    }

    sw::mark::IMark* MarkManager::makeAnnotationMark(
        const SwPaM& rPaM,
        const OUString& rName )
    {
        return makeMark(rPaM, rName, IDocumentMarkAccess::MarkType::ANNOTATIONMARK,
                sw::mark::InsertMode::New);
    }

    void MarkManager::repositionMark(
        ::sw::mark::IMark* const io_pMark,
        const SwPaM& rPaM)
    {
        assert(io_pMark->GetMarkPos().GetDoc() == m_pDoc &&
            "<MarkManager::repositionMark(..)>"
            " - Mark is not in my doc.");
        MarkBase* const pMarkBase = dynamic_cast< MarkBase* >(io_pMark);
        if (!pMarkBase)
            return;

        pMarkBase->SetMarkPos(*(rPaM.GetPoint()));
        if(rPaM.HasMark())
            pMarkBase->SetOtherMarkPos(*(rPaM.GetMark()));
        else
            pMarkBase->ClearOtherMarkPos();

        if(pMarkBase->GetMarkPos() != pMarkBase->GetMarkStart())
            pMarkBase->Swap();

        sortMarks();
    }

    bool MarkManager::renameMark(
        ::sw::mark::IMark* io_pMark,
        const OUString& rNewName )
    {
        assert(io_pMark->GetMarkPos().GetDoc() == m_pDoc &&
            "<MarkManager::renameMark(..)>"
            " - Mark is not in my doc.");
        if ( io_pMark->GetName() == rNewName )
            return true;
        if (lcl_FindMarkByName(rNewName, m_vAllMarks.begin(), m_vAllMarks.end()) != m_vAllMarks.end())
            return false;
        if (::sw::mark::MarkBase* pMarkBase = dynamic_cast< ::sw::mark::MarkBase* >(io_pMark))
        {
            const OUString sOldName(pMarkBase->GetName());
            pMarkBase->SetName(rNewName);

            if (dynamic_cast< ::sw::mark::Bookmark* >(io_pMark))
            {
                if (m_pDoc->GetIDocumentUndoRedo().DoesUndo())
                {
                    m_pDoc->GetIDocumentUndoRedo().AppendUndo(
                            std::make_unique<SwUndoRenameBookmark>(sOldName, rNewName, m_pDoc));
                }
                m_pDoc->getIDocumentState().SetModified();
            }
        }
        return true;
    }

    void MarkManager::correctMarksAbsolute(
        const SwNodeIndex& rOldNode,
        const SwPosition& rNewPos,
        const sal_Int32 nOffset)
    {
        const SwNode* const pOldNode = &rOldNode.GetNode();
        SwPosition aNewPos(rNewPos);
        aNewPos.nContent += nOffset;
        bool isSortingNeeded = false;

        for (auto ppMark = m_vAllMarks.begin();
            ppMark != m_vAllMarks.end();
            ++ppMark)
        {
            ::sw::mark::MarkBase *const pMark = *ppMark;
            // correction of non-existent non-MarkBase instances cannot be done
            assert(pMark);
            // is on position ??
            bool bChangedPos = false;
            if(&pMark->GetMarkPos().nNode.GetNode() == pOldNode)
            {
                pMark->SetMarkPos(aNewPos);
                bChangedPos = true;
                isSortingNeeded = true;
            }
            bool bChangedOPos = false;
            if (pMark->IsExpanded() &&
                &pMark->GetOtherMarkPos().nNode.GetNode() == pOldNode)
            {
                // shift the OtherMark to aNewPos
                pMark->SetOtherMarkPos(aNewPos);
                bChangedOPos= true;
                isSortingNeeded = true;
            }
            // illegal selection? collapse the mark and restore sorting later
            isSortingNeeded |= lcl_FixCorrectedMark(bChangedPos, bChangedOPos, pMark);
        }

        // restore sorting if needed
        if(isSortingNeeded)
            sortMarks();

        SAL_INFO("sw.core", "correctMarksAbsolute");
        lcl_DebugMarks(m_vAllMarks);
    }

    void MarkManager::correctMarksRelative(const SwNodeIndex& rOldNode, const SwPosition& rNewPos, const sal_Int32 nOffset)
    {
        const SwNode* const pOldNode = &rOldNode.GetNode();
        SwPosition aNewPos(rNewPos);
        aNewPos.nContent += nOffset;
        bool isSortingNeeded = false;

        for (auto ppMark = m_vAllMarks.begin();
            ppMark != m_vAllMarks.end();
            ++ppMark)
        {
            // is on position ??
            bool bChangedPos = false, bChangedOPos = false;
            ::sw::mark::MarkBase* const pMark = *ppMark;
            // correction of non-existent non-MarkBase instances cannot be done
            assert(pMark);
            if(&pMark->GetMarkPos().nNode.GetNode() == pOldNode)
            {
                SwPosition aNewPosRel(aNewPos);
                if (dynamic_cast< ::sw::mark::CrossRefBookmark *>(pMark))
                {
                    // ensure that cross ref bookmark always starts at 0
                    aNewPosRel.nContent = 0; // HACK for WW8 import
                    isSortingNeeded = true; // and sort them to be safe...
                }
                aNewPosRel.nContent += pMark->GetMarkPos().nContent.GetIndex();
                pMark->SetMarkPos(aNewPosRel);
                bChangedPos = true;
            }
            if(pMark->IsExpanded() &&
                &pMark->GetOtherMarkPos().nNode.GetNode() == pOldNode)
            {
                SwPosition aNewPosRel(aNewPos);
                aNewPosRel.nContent += pMark->GetOtherMarkPos().nContent.GetIndex();
                pMark->SetOtherMarkPos(aNewPosRel);
                bChangedOPos = true;
            }
            // illegal selection? collapse the mark and restore sorting later
            isSortingNeeded |= lcl_FixCorrectedMark(bChangedPos, bChangedOPos, pMark);
        }

        // restore sorting if needed
        if(isSortingNeeded)
            sortMarks();

        SAL_INFO("sw.core", "correctMarksRelative");
        lcl_DebugMarks(m_vAllMarks);
    }

    void MarkManager::deleteMarks(
            const SwNodeIndex& rStt,
            const SwNodeIndex& rEnd,
            std::vector<SaveBookmark>* pSaveBkmk,
            const SwIndex* pSttIdx,
            const SwIndex* pEndIdx )
    {
        std::vector<const_iterator_t> vMarksToDelete;
        bool bIsSortingNeeded = false;

        // boolean indicating, if at least one mark has been moved while collecting marks for deletion
        bool bMarksMoved = false;
        // have marks in the range been skipped instead of deleted
        bool bMarksSkipDeletion = false;

        // copy all bookmarks in the move area to a vector storing all position data as offset
        // reassignment is performed after the move
        for (auto ppMark = m_vAllMarks.begin();
            ppMark != m_vAllMarks.end();
            ++ppMark)
        {
            // navigator marks should not be moved
            // TODO: Check if this might make them invalid
            if(IDocumentMarkAccess::GetType(**ppMark) == MarkType::NAVIGATOR_REMINDER)
                continue;

            ::sw::mark::MarkBase *const pMark = *ppMark;

            if (!pMark)
                continue;

            // on position ??
            bool bIsPosInRange = lcl_GreaterThan(pMark->GetMarkPos(), rStt, pSttIdx)
                                 && lcl_Lower(pMark->GetMarkPos(), rEnd, pEndIdx);
            bool bIsOtherPosInRange = pMark->IsExpanded()
                                      && lcl_GreaterThan(pMark->GetOtherMarkPos(), rStt, pSttIdx)
                                      && lcl_Lower(pMark->GetOtherMarkPos(), rEnd, pEndIdx);
            // special case: completely in range, touching the end?
            if ( pEndIdx != nullptr
                 && ( ( bIsOtherPosInRange
                        && pMark->GetMarkPos().nNode == rEnd
                        && pMark->GetMarkPos().nContent == *pEndIdx )
                      || ( bIsPosInRange
                           && pMark->IsExpanded()
                           && pMark->GetOtherMarkPos().nNode == rEnd
                           && pMark->GetOtherMarkPos().nContent == *pEndIdx ) ) )
            {
                bIsPosInRange = true;
                bIsOtherPosInRange = true;
            }

            if ( bIsPosInRange
                 && ( bIsOtherPosInRange
                      || !pMark->IsExpanded() ) )
            {
                // completely in range

                bool bDeleteMark = true;
                {
                    switch ( IDocumentMarkAccess::GetType( *pMark ) )
                    {
                    case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
                    case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
                        // no delete of cross-reference bookmarks, if range is inside one paragraph
                        bDeleteMark = rStt != rEnd;
                        break;
                    case IDocumentMarkAccess::MarkType::UNO_BOOKMARK:
                        // no delete of UNO mark, if it is not expanded and only touches the start of the range
                        bDeleteMark = bIsOtherPosInRange
                                      || pMark->IsExpanded()
                                      || pSttIdx == nullptr
                                      || !( pMark->GetMarkPos().nNode == rStt
                                            && pMark->GetMarkPos().nContent == *pSttIdx );
                        break;
                    default:
                        bDeleteMark = true;
                        break;
                    }
                }

                if ( bDeleteMark )
                {
                    if ( pSaveBkmk )
                    {
                        pSaveBkmk->push_back( SaveBookmark( *pMark, rStt, pSttIdx ) );
                    }
                    vMarksToDelete.emplace_back(ppMark);
                }
                else
                {
                    bMarksSkipDeletion = true;
                }
            }
            else if ( bIsPosInRange != bIsOtherPosInRange )
            {
                // the bookmark is partially in the range
                // move position of that is in the range out of it

                std::unique_ptr< SwPosition > pNewPos;
                {
                    if ( pEndIdx != nullptr )
                    {
                        pNewPos = std::make_unique< SwPosition >( rEnd, *pEndIdx );
                    }
                    else
                    {
                        pNewPos =
                            lcl_FindExpelPosition( rStt, rEnd, bIsPosInRange ? pMark->GetOtherMarkPos() : pMark->GetMarkPos() );
                    }
                }

                bool bMoveMark = true;
                {
                    switch ( IDocumentMarkAccess::GetType( *pMark ) )
                    {
                    case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
                    case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
                        // no move of cross-reference bookmarks, if move occurs inside a certain node
                        bMoveMark = pMark->GetMarkPos().nNode != pNewPos->nNode;
                        break;
                    case IDocumentMarkAccess::MarkType::ANNOTATIONMARK:
                        // no move of annotation marks, if method is called to collect deleted marks
                        bMoveMark = pSaveBkmk == nullptr;
                        break;
                    default:
                        bMoveMark = true;
                        break;
                    }
                }
                if ( bMoveMark )
                {
                    if ( bIsPosInRange )
                        pMark->SetMarkPos(*pNewPos);
                    else
                        pMark->SetOtherMarkPos(*pNewPos);
                    bMarksMoved = true;

                    // illegal selection? collapse the mark and restore sorting later
                    bIsSortingNeeded |= lcl_FixCorrectedMark( bIsPosInRange, bIsOtherPosInRange, pMark );
                }
            }
        }

        {
            // fdo#61016 delay the deletion of the fieldmark characters
            // to prevent that from deleting the marks on that position
            // which would invalidate the iterators in vMarksToDelete
            std::vector< std::unique_ptr<ILazyDeleter> > vDelay;
            vDelay.reserve(vMarksToDelete.size());

            // If needed, sort mark containers containing subsets of the marks
            // in order to assure sorting.  The sorting is critical for the
            // deletion of a mark as it is searched in these container for
            // deletion.
            if ( !vMarksToDelete.empty() && bMarksMoved )
            {
                sortSubsetMarks();
            }
            // we just remembered the iterators to delete, so we do not need to search
            // for the shared_ptr<> (the entry in m_vAllMarks) again
            // reverse iteration, since erasing an entry invalidates iterators
            // behind it (the iterators in vMarksToDelete are sorted)
            for ( std::vector< const_iterator_t >::reverse_iterator pppMark = vMarksToDelete.rbegin();
                  pppMark != vMarksToDelete.rend();
                  ++pppMark )
            {
                vDelay.push_back(deleteMark(*pppMark));
            }
        } // scope to kill vDelay

        // also need to sort if both marks were moved and not-deleted because
        // the not-deleted marks could be in wrong order vs. the moved ones
        if (bIsSortingNeeded || (bMarksMoved && bMarksSkipDeletion))
        {
            sortMarks();
        }

        SAL_INFO("sw.core", "deleteMarks");
        lcl_DebugMarks(m_vAllMarks);
    }

    struct LazyFieldmarkDeleter : public IDocumentMarkAccess::ILazyDeleter
    {
        std::unique_ptr<Fieldmark> m_pFieldmark;
        SwDoc * m_pDoc;
        LazyFieldmarkDeleter(Fieldmark* pMark, SwDoc *const pDoc)
            : m_pFieldmark(pMark), m_pDoc(pDoc)
        {
            assert(m_pFieldmark);
        }
        virtual ~LazyFieldmarkDeleter() override
        {
            m_pFieldmark->ReleaseDoc(m_pDoc);
        }
    };

    std::unique_ptr<IDocumentMarkAccess::ILazyDeleter>
        MarkManager::deleteMark(const const_iterator_t& ppMark)
    {
        std::unique_ptr<ILazyDeleter> ret;
        if (ppMark.get() == m_vAllMarks.end())
            return ret;
        IMark* pMark = *ppMark;

        switch(IDocumentMarkAccess::GetType(*pMark))
        {
            case IDocumentMarkAccess::MarkType::BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_HEADING_BOOKMARK:
            case IDocumentMarkAccess::MarkType::CROSSREF_NUMITEM_BOOKMARK:
                {
                    auto const ppBookmark = lcl_FindMark(m_vBookmarks, *ppMark.get());
                    if ( ppBookmark != m_vBookmarks.end() )
                    {
                        m_vBookmarks.erase(ppBookmark);
                    }
                    else
                    {
                        assert(false &&
                            "<MarkManager::deleteMark(..)> - Bookmark not found in Bookmark container.");
                    }
                }
                break;

            case IDocumentMarkAccess::MarkType::TEXT_FIELDMARK:
            case IDocumentMarkAccess::MarkType::CHECKBOX_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DROPDOWN_FIELDMARK:
            case IDocumentMarkAccess::MarkType::DATE_FIELDMARK:
                {
                    auto const ppFieldmark = lcl_FindMark(m_vFieldmarks, *ppMark.get());
                    if ( ppFieldmark != m_vFieldmarks.end() )
                    {
                        if(m_pLastActiveFieldmark == *ppFieldmark)
                            ClearFieldActivation();

                        m_vFieldmarks.erase(ppFieldmark);
                        ret.reset(new LazyFieldmarkDeleter(dynamic_cast<Fieldmark*>(pMark), m_pDoc));
                    }
                    else
                    {
                        assert(false &&
                            "<MarkManager::deleteMark(..)> - Fieldmark not found in Fieldmark container.");
                    }
                }
                break;

            case IDocumentMarkAccess::MarkType::ANNOTATIONMARK:
                {
                    auto const ppAnnotationMark = lcl_FindMark(m_vAnnotationMarks, *ppMark.get());
                    assert(ppAnnotationMark != m_vAnnotationMarks.end() &&
                        "<MarkManager::deleteMark(..)> - Annotation Mark not found in Annotation Mark container.");
                    m_vAnnotationMarks.erase(ppAnnotationMark);
                }
                break;

            case IDocumentMarkAccess::MarkType::DDE_BOOKMARK:
            case IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER:
            case IDocumentMarkAccess::MarkType::UNO_BOOKMARK:
                // no special marks container
                break;
        }
        DdeBookmark* const pDdeBookmark = dynamic_cast<DdeBookmark*>(pMark);
        if (pDdeBookmark)
            pDdeBookmark->DeregisterFromDoc(m_pDoc);
        //Effective STL Item 27, get a non-const iterator aI at the same
        //position as const iterator ppMark was
        auto aI = m_vAllMarks.begin();
        std::advance(aI, std::distance<container_t::const_iterator>(aI, ppMark.get()));

        m_vAllMarks.erase(aI);
        // If we don't have a lazy deleter
        if (!ret)
            // delete after we remove from the list, because the destructor can
            // recursively call into this method.
            delete pMark;
        return ret;
    }

    void MarkManager::deleteMark(const IMark* const pMark)
    {
        assert(pMark->GetMarkPos().GetDoc() == m_pDoc &&
            "<MarkManager::deleteMark(..)>"
            " - Mark is not in my doc.");
        // finds the last Mark that is starting before pMark
        // (pMarkLow < pMark)
        auto [it, endIt] = equal_range(
                m_vAllMarks.begin(),
                m_vAllMarks.end(),
                pMark->GetMarkStart(),
                CompareIMarkStartsBefore());
        for ( ; it != endIt; ++it)
            if (*it == pMark)
            {
                deleteMark(iterator(it));
                break;
            }
    }

    void MarkManager::clearAllMarks()
    {
        ClearFieldActivation();
        m_vFieldmarks.clear();
        m_vBookmarks.clear();
        m_vAnnotationMarks.clear();
        for (auto & p : m_vAllMarks)
            delete p;
        m_vAllMarks.clear();
    }

    IDocumentMarkAccess::const_iterator_t MarkManager::findMark(const OUString& rName) const
    {
        auto const ret = lcl_FindMarkByName(rName, m_vAllMarks.begin(), m_vAllMarks.end());
        return IDocumentMarkAccess::iterator(ret);
    }

    IDocumentMarkAccess::const_iterator_t MarkManager::findBookmark(const OUString& rName) const
    {
        auto const ret = lcl_FindMarkByName(rName, m_vBookmarks.begin(), m_vBookmarks.end());
        return IDocumentMarkAccess::iterator(ret);
    }

    IDocumentMarkAccess::const_iterator_t MarkManager::getAllMarksBegin() const
        { return m_vAllMarks.begin(); }

    IDocumentMarkAccess::const_iterator_t MarkManager::getAllMarksEnd() const
        { return m_vAllMarks.end(); }

    sal_Int32 MarkManager::getAllMarksCount() const
        { return m_vAllMarks.size(); }

    IDocumentMarkAccess::const_iterator_t MarkManager::getBookmarksBegin() const
        { return m_vBookmarks.begin(); }

    IDocumentMarkAccess::const_iterator_t MarkManager::getBookmarksEnd() const
        { return m_vBookmarks.end(); }

    sal_Int32 MarkManager::getBookmarksCount() const
        { return m_vBookmarks.size(); }

    // finds the first that is starting after
    IDocumentMarkAccess::const_iterator_t MarkManager::findFirstBookmarkStartsAfter(const SwPosition& rPos) const
    {
        return std::upper_bound(
            m_vBookmarks.begin(),
            m_vBookmarks.end(),
            rPos,
            CompareIMarkStartsAfter());
    }

    IFieldmark* MarkManager::getFieldmarkFor(const SwPosition& rPos) const
    {
        auto const pFieldmark = find_if(
            m_vFieldmarks.begin(),
            m_vFieldmarks.end(),
            [&rPos] (const ::sw::mark::MarkBase *const pMark) { return pMark->IsCoveringPosition(rPos); } );
        if(pFieldmark == m_vFieldmarks.end())
            return nullptr;
        return dynamic_cast<IFieldmark*>(*pFieldmark);
    }

    void MarkManager::deleteFieldmarkAt(const SwPosition& rPos)
    {
        auto const pFieldmark = find_if(
            m_vFieldmarks.begin(),
            m_vFieldmarks.end(),
            [&rPos] (const ::sw::mark::IMark* pMark) { return pMark->IsCoveringPosition(rPos); } );
        if(pFieldmark == m_vFieldmarks.end())
            return;

        deleteMark(lcl_FindMark(m_vAllMarks, *pFieldmark));
    }

    ::sw::mark::IFieldmark* MarkManager::changeFormFieldmarkType(::sw::mark::IFieldmark* pFieldmark, const OUString& rNewType)
    {
        bool bActualChange = false;
        if(rNewType == ODF_FORMDROPDOWN)
        {
            if (!dynamic_cast<::sw::mark::DropDownFieldmark*>(pFieldmark))
                bActualChange = true;
            if (!dynamic_cast<::sw::mark::CheckboxFieldmark*>(pFieldmark)) // only allowed converting between checkbox <-> dropdown
                return nullptr;
        }
        else if(rNewType == ODF_FORMCHECKBOX)
        {
            if (!dynamic_cast<::sw::mark::CheckboxFieldmark*>(pFieldmark))
                bActualChange = true;
            if (!dynamic_cast<::sw::mark::DropDownFieldmark*>(pFieldmark)) // only allowed converting between checkbox <-> dropdown
                return nullptr;
        }
        else if(rNewType == ODF_FORMDATE)
        {
            if (!dynamic_cast<::sw::mark::DateFieldmark*>(pFieldmark))
                bActualChange = true;
            if (!dynamic_cast<::sw::mark::TextFieldmark*>(pFieldmark)) // only allowed converting between date field <-> text field
                return nullptr;
        }

        if (!bActualChange)
            return nullptr;

        // Store attributes needed to create the new fieldmark
        OUString sName = pFieldmark->GetName();
        SwPaM aPaM(pFieldmark->GetMarkPos());

        // Remove the old fieldmark and create a new one with the new type
        if(aPaM.GetPoint()->nContent > 0 && (rNewType == ODF_FORMDROPDOWN || rNewType == ODF_FORMCHECKBOX))
        {
            --aPaM.GetPoint()->nContent;
            SwPosition aNewPos (aPaM.GetPoint()->nNode, aPaM.GetPoint()->nContent);
            deleteFieldmarkAt(aNewPos);
            return makeNoTextFieldBookmark(aPaM, sName, rNewType);
        }
        else if(rNewType == ODF_FORMDATE)
        {
            SwPosition aPos (aPaM.GetPoint()->nNode, aPaM.GetPoint()->nContent);
            SwPaM aNewPaM(pFieldmark->GetMarkStart(), pFieldmark->GetMarkEnd());
            deleteFieldmarkAt(aPos);
            return makeFieldBookmark(aNewPaM, sName, rNewType);
        }
        return nullptr;
    }

    void MarkManager::NotifyCursorUpdate(const SwCursorShell& rCursorShell)
    {
        SwView* pSwView = dynamic_cast<SwView *>(rCursorShell.GetSfxViewShell());
        if(!pSwView)
            return;

        SwEditWin& rEditWin = pSwView->GetEditWin();
        SwPosition aPos(*rCursorShell.GetCursor()->GetPoint());
        IFieldmark* pFieldBM = getFieldmarkFor(aPos);
        FieldmarkWithDropDownButton* pNewActiveFieldmark = nullptr;
        if ((!pFieldBM || (pFieldBM->GetFieldname() != ODF_FORMDROPDOWN && pFieldBM->GetFieldname() != ODF_FORMDATE))
            && aPos.nContent.GetIndex() > 0 )
        {
            --aPos.nContent;
            pFieldBM = getFieldmarkFor(aPos);
        }

        if ( pFieldBM && (pFieldBM->GetFieldname() == ODF_FORMDROPDOWN ||
                          pFieldBM->GetFieldname() == ODF_FORMDATE))
        {
            if (m_pLastActiveFieldmark != pFieldBM)
            {
                FieldmarkWithDropDownButton& rFormField = dynamic_cast<FieldmarkWithDropDownButton&>(*pFieldBM);
                rFormField.ShowButton(&rEditWin);
                pNewActiveFieldmark = &rFormField;
            }
            else
            {
                pNewActiveFieldmark = m_pLastActiveFieldmark;
            }
        }

        if(pNewActiveFieldmark != m_pLastActiveFieldmark)
        {
            ClearFieldActivation();
            m_pLastActiveFieldmark = pNewActiveFieldmark;
        }
    }

    void MarkManager::ClearFieldActivation()
    {
        if(m_pLastActiveFieldmark)
            m_pLastActiveFieldmark->RemoveButton();

        m_pLastActiveFieldmark = nullptr;
    }

    IFieldmark* MarkManager::getDropDownFor(const SwPosition& rPos) const
    {
        IFieldmark *pMark = getFieldmarkFor(rPos);
        if (!pMark || pMark->GetFieldname() != ODF_FORMDROPDOWN)
            return nullptr;
        return pMark;
    }

    std::vector<IFieldmark*> MarkManager::getDropDownsFor(const SwPaM &rPaM) const
    {
        std::vector<IFieldmark*> aRet;

        for (auto aI = m_vFieldmarks.begin(),
            aEnd = m_vFieldmarks.end(); aI != aEnd; ++aI)
        {
            ::sw::mark::IMark* pI = *aI;
            const SwPosition &rStart = pI->GetMarkPos();
            if (!rPaM.ContainsPosition(rStart))
                continue;

            IFieldmark *pMark = dynamic_cast<IFieldmark*>(pI);
            if (!pMark || pMark->GetFieldname() != ODF_FORMDROPDOWN)
                continue;

            aRet.push_back(pMark);
        }

        return aRet;
    }

    IFieldmark* MarkManager::getFieldmarkAfter(const SwPosition& rPos) const
        { return dynamic_cast<IFieldmark*>(lcl_getMarkAfter(m_vFieldmarks, rPos)); }

    IFieldmark* MarkManager::getFieldmarkBefore(const SwPosition& rPos) const
        { return dynamic_cast<IFieldmark*>(lcl_getMarkBefore(m_vFieldmarks, rPos)); }

    IDocumentMarkAccess::const_iterator_t MarkManager::getAnnotationMarksBegin() const
    {
        return m_vAnnotationMarks.begin();
    }

    IDocumentMarkAccess::const_iterator_t MarkManager::getAnnotationMarksEnd() const
    {
        return m_vAnnotationMarks.end();
    }

    sal_Int32 MarkManager::getAnnotationMarksCount() const
    {
        return m_vAnnotationMarks.size();
    }

    IDocumentMarkAccess::const_iterator_t MarkManager::findAnnotationMark( const OUString& rName ) const
    {
        auto const ret = lcl_FindMarkByName( rName, m_vAnnotationMarks.begin(), m_vAnnotationMarks.end() );
        return IDocumentMarkAccess::iterator(ret);
    }

    IMark* MarkManager::getAnnotationMarkFor(const SwPosition& rPos) const
    {
        auto const pAnnotationMark = find_if(
            m_vAnnotationMarks.begin(),
            m_vAnnotationMarks.end(),
            [&rPos] (const ::sw::mark::MarkBase *const pMark) { return pMark->IsCoveringPosition(rPos); } );
        if (pAnnotationMark == m_vAnnotationMarks.end())
            return nullptr;
        return *pAnnotationMark;
    }

    // finds the first that is starting after
    IDocumentMarkAccess::const_iterator_t MarkManager::findFirstAnnotationStartsAfter(const SwPosition& rPos) const
    {
        return std::upper_bound(
            m_vAnnotationMarks.begin(),
            m_vAnnotationMarks.end(),
            rPos,
            CompareIMarkStartsAfter());
    }

    OUString MarkManager::getUniqueMarkName(const OUString& rName) const
    {
        OSL_ENSURE(rName.getLength(),
            "<MarkManager::getUniqueMarkName(..)> - a name should be proposed");
        if( m_pDoc->IsInMailMerge())
        {
            OUString newName = rName + "MailMergeMark"
                    + OStringToOUString( DateTimeToOString( DateTime( DateTime::SYSTEM )), RTL_TEXTENCODING_ASCII_US )
                    + OUString::number( m_vAllMarks.size() + 1 );
            return newName;
        }

        if (lcl_FindMarkByName(rName, m_vAllMarks.begin(), m_vAllMarks.end()) == m_vAllMarks.end())
        {
            return rName;
        }
        OUStringBuffer sBuf;
        OUString sTmp;

        // try the name "<rName>XXX" (where XXX is a number starting from 1) unless there is
        // an unused name. Due to performance-reasons (especially in mailmerge-scenarios) there
        // is a map m_aMarkBasenameMapUniqueOffset which holds the next possible offset (XXX) for
        // rName (so there is no need to test for nCnt-values smaller than the offset).
        sal_Int32 nCnt = 1;
        MarkBasenameMapUniqueOffset_t::const_iterator aIter = m_aMarkBasenameMapUniqueOffset.find(rName);
        if(aIter != m_aMarkBasenameMapUniqueOffset.end()) nCnt = aIter->second;
        while(nCnt < SAL_MAX_INT32)
        {
            sTmp = sBuf.append(rName).append(nCnt).makeStringAndClear();
            nCnt++;
            if (lcl_FindMarkByName(sTmp, m_vAllMarks.begin(), m_vAllMarks.end()) == m_vAllMarks.end())
            {
                break;
            }
        }
        m_aMarkBasenameMapUniqueOffset[rName] = nCnt;

        return sTmp;
    }

    void MarkManager::assureSortedMarkContainers() const
    {
        const_cast< MarkManager* >(this)->sortMarks();
    }

    void MarkManager::sortSubsetMarks()
    {
        sort(m_vBookmarks.begin(), m_vBookmarks.end(), &lcl_MarkOrderingByStart);
        sort(m_vFieldmarks.begin(), m_vFieldmarks.end(), &lcl_MarkOrderingByStart);
        sort(m_vAnnotationMarks.begin(), m_vAnnotationMarks.end(), &lcl_MarkOrderingByStart);
    }

    void MarkManager::sortMarks()
    {
        sort(m_vAllMarks.begin(), m_vAllMarks.end(), &lcl_MarkOrderingByStart);
        sortSubsetMarks();
    }

void MarkManager::dumpAsXml(xmlTextWriterPtr pWriter) const
{
    struct
    {
        const char* pName;
        const container_t* pContainer;
    } aContainers[] =
    {
        // UNO marks are only part of all marks.
        {"allmarks", &m_vAllMarks},
        {"bookmarks", &m_vBookmarks},
        {"fieldmarks", &m_vFieldmarks},
        {"annotationmarks", &m_vAnnotationMarks}
    };

    xmlTextWriterStartElement(pWriter, BAD_CAST("MarkManager"));
    for (const auto & rContainer : aContainers)
    {
        if (!rContainer.pContainer->empty())
        {
            xmlTextWriterStartElement(pWriter, BAD_CAST(rContainer.pName));
            for (auto it = rContainer.pContainer->begin(); it != rContainer.pContainer->end(); ++it)
                (*it)->dumpAsXml(pWriter);
            xmlTextWriterEndElement(pWriter);
        }
    }
    xmlTextWriterEndElement(pWriter);
}

}} // namespace ::sw::mark

namespace
{
    bool lcl_Greater( const SwPosition& rPos, const SwNodeIndex& rNdIdx, const SwIndex* pIdx )
    {
        return rPos.nNode > rNdIdx || ( pIdx && rPos.nNode == rNdIdx && rPos.nContent > pIdx->GetIndex() );
    }
}

// IDocumentMarkAccess for SwDoc
IDocumentMarkAccess* SwDoc::getIDocumentMarkAccess()
    { return static_cast< IDocumentMarkAccess* >(mpMarkManager.get()); }

const IDocumentMarkAccess* SwDoc::getIDocumentMarkAccess() const
    { return static_cast< IDocumentMarkAccess* >(mpMarkManager.get()); }

SaveBookmark::SaveBookmark(
    const IMark& rBkmk,
    const SwNodeIndex & rMvPos,
    const SwIndex* pIdx)
    : m_aName(rBkmk.GetName())
    , m_aShortName()
    , m_aCode()
    , m_eOrigBkmType(IDocumentMarkAccess::GetType(rBkmk))
{
    const IBookmark* const pBookmark = dynamic_cast< const IBookmark* >(&rBkmk);
    if(pBookmark)
    {
        m_aShortName = pBookmark->GetShortName();
        m_aCode = pBookmark->GetKeyCode();

        ::sfx2::Metadatable const*const pMetadatable(
                dynamic_cast< ::sfx2::Metadatable const* >(pBookmark));
        if (pMetadatable)
        {
            m_pMetadataUndo = pMetadatable->CreateUndo();
        }
    }
    m_nNode1 = rBkmk.GetMarkPos().nNode.GetIndex();
    m_nContent1 = rBkmk.GetMarkPos().nContent.GetIndex();

    m_nNode1 -= rMvPos.GetIndex();
    if(pIdx && !m_nNode1)
        m_nContent1 -= pIdx->GetIndex();

    if(rBkmk.IsExpanded())
    {
        m_nNode2 = rBkmk.GetOtherMarkPos().nNode.GetIndex();
        m_nContent2 = rBkmk.GetOtherMarkPos().nContent.GetIndex();

        m_nNode2 -= rMvPos.GetIndex();
        if(pIdx && !m_nNode2)
            m_nContent2 -= pIdx->GetIndex();
    }
    else
    {
        m_nNode2 = ULONG_MAX;
        m_nContent2 = -1;
    }
}

void SaveBookmark::SetInDoc(
    SwDoc* pDoc,
    const SwNodeIndex& rNewPos,
    const SwIndex* pIdx)
{
    SwPaM aPam(rNewPos.GetNode());
    if(pIdx)
        aPam.GetPoint()->nContent = *pIdx;

    if(ULONG_MAX != m_nNode2)
    {
        aPam.SetMark();

        aPam.GetMark()->nNode += m_nNode2;
        if(pIdx && !m_nNode2)
            aPam.GetMark()->nContent += m_nContent2;
        else
            aPam.GetMark()->nContent.Assign(aPam.GetContentNode(false), m_nContent2);
    }

    aPam.GetPoint()->nNode += m_nNode1;

    if(pIdx && !m_nNode1)
        aPam.GetPoint()->nContent += m_nContent1;
    else
        aPam.GetPoint()->nContent.Assign(aPam.GetContentNode(), m_nContent1);

    if(!aPam.HasMark()
        || CheckNodesRange(aPam.GetPoint()->nNode, aPam.GetMark()->nNode, true))
    {
        ::sw::mark::IBookmark* const pBookmark = dynamic_cast<::sw::mark::IBookmark*>(
            pDoc->getIDocumentMarkAccess()->makeMark(aPam, m_aName,
                m_eOrigBkmType, sw::mark::InsertMode::New));
        if(pBookmark)
        {
            pBookmark->SetKeyCode(m_aCode);
            pBookmark->SetShortName(m_aShortName);
            if (m_pMetadataUndo)
            {
                ::sfx2::Metadatable * const pMeta(
                    dynamic_cast< ::sfx2::Metadatable* >(pBookmark));
                assert(pMeta && "metadata undo, but not metadatable?");
                if (pMeta)
                {
                    pMeta->RestoreMetadata(m_pMetadataUndo);
                }
            }
        }
    }
}

// DelBookmarks

void DelBookmarks(
    const SwNodeIndex& rStt,
    const SwNodeIndex& rEnd,
    std::vector<SaveBookmark> * pSaveBkmk,
    const SwIndex* pSttIdx,
    const SwIndex* pEndIdx)
{
    // illegal range ??
    if(rStt.GetIndex() > rEnd.GetIndex()
        || (rStt == rEnd && (!pSttIdx || !pEndIdx || pSttIdx->GetIndex() >= pEndIdx->GetIndex())))
        return;
    SwDoc* const pDoc = rStt.GetNode().GetDoc();

    pDoc->getIDocumentMarkAccess()->deleteMarks(rStt, rEnd, pSaveBkmk, pSttIdx, pEndIdx);

    // Copy all Redlines which are in the move area into an array
    // which holds all position information as offset.
    // Assignment happens after moving.
    SwRedlineTable& rTable = pDoc->getIDocumentRedlineAccess().GetRedlineTable();
    for(SwRangeRedline* pRedl : rTable)
    {
        // Is at position?
        SwPosition *const pRStt = pRedl->Start();
        SwPosition *const pREnd = pRedl->End();

        if( lcl_Greater( *pRStt, rStt, pSttIdx ) && lcl_Lower( *pRStt, rEnd, pEndIdx ))
        {
            pRStt->nNode = rEnd;
            if( pEndIdx )
                pRStt->nContent = *pEndIdx;
            else
            {
                bool bStt = true;
                SwContentNode* pCNd = pRStt->nNode.GetNode().GetContentNode();
                if( !pCNd && nullptr == ( pCNd = pDoc->GetNodes().GoNext( &pRStt->nNode )) )
                {
                    bStt = false;
                    pRStt->nNode = rStt;
                    if( nullptr == ( pCNd = SwNodes::GoPrevious( &pRStt->nNode )) )
                    {
                        pRStt->nNode = pREnd->nNode;
                        pCNd = pRStt->nNode.GetNode().GetContentNode();
                    }
                }
                pRStt->nContent.Assign( pCNd, bStt ? 0 : pCNd->Len() );
            }
        }
        if( lcl_Greater( *pREnd, rStt, pSttIdx ) && lcl_Lower( *pREnd, rEnd, pEndIdx ))
        {
            pREnd->nNode = rStt;
            if( pSttIdx )
                pREnd->nContent = *pSttIdx;
            else
            {
                bool bStt = false;
                SwContentNode* pCNd = pREnd->nNode.GetNode().GetContentNode();
                if( !pCNd && nullptr == ( pCNd = SwNodes::GoPrevious( &pREnd->nNode )) )
                {
                    bStt = true;
                    pREnd->nNode = rEnd;
                    if( nullptr == ( pCNd = pDoc->GetNodes().GoNext( &pREnd->nNode )) )
                    {
                        pREnd->nNode = pRStt->nNode;
                        pCNd = pREnd->nNode.GetNode().GetContentNode();
                    }
                }
                pREnd->nContent.Assign( pCNd, bStt ? 0 : pCNd->Len() );
            }
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
