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


#include <sot/formats.hxx>
#include <svl/urlbmk.hxx>
#include <svl/stritem.hxx>
#include <svl/intitem.hxx>
#include <svl/eitem.hxx>
#include <vcl/transfer.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/sfxsids.hrc>
#include <vcl/graphicfilter.hxx>
#include <editeng/brushitem.hxx>
#include <helpids.h>
#include <svx/gallery.hxx>
#include <svx/svxids.hrc>
#include <galobj.hxx>
#include <svx/gallery1.hxx>
#include <svx/galtheme.hxx>
#include <svx/galctrl.hxx>
#include <svx/galmisc.hxx>
#include <galbrws2.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/weld.hxx>
#include <svx/fmmodel.hxx>
#include <svx/dialmgr.hxx>
#include <svx/svxdlg.hxx>
#include <svx/strings.hrc>
#include <GalleryControl.hxx>
#include <bitmaps.hlst>
#include <svx/galleryitem.hxx>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/frame/FrameSearchFlag.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/gallery/GalleryItemType.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/style/GraphicLocation.hpp>
#include <map>
#include <memory>
#include <cppuhelper/implbase.hxx>

#undef GALLERY_USE_CLIPBOARD

#define TBX_ID_ICON 1
#define TBX_ID_LIST 2

GalleryBrowserMode GalleryBrowser2::meInitMode = GALLERYBROWSERMODE_ICON;

struct DispatchInfo
{
    css::util::URL                                  TargetURL;
    css::uno::Sequence< css::beans::PropertyValue > Arguments;
    css::uno::Reference< css::frame::XDispatch >    Dispatch;
};

IMPL_STATIC_LINK( GalleryBrowser2, AsyncDispatch_Impl, void*, p, void )
{
    DispatchInfo* pDispatchInfo = static_cast<DispatchInfo*>(p);
    if ( pDispatchInfo && pDispatchInfo->Dispatch.is() )
    {
        try
        {
            pDispatchInfo->Dispatch->dispatch( pDispatchInfo->TargetURL,
                                               pDispatchInfo->Arguments );
        }
        catch ( const css::uno::Exception& )
        {
        }
    }

    delete pDispatchInfo;
}

namespace
{

struct CommandInfo
{
    css::util::URL                               URL;
    css::uno::Reference< css::frame::XDispatch > Dispatch;

    explicit CommandInfo( const OUString &rURL )
    {
        URL.Complete = rURL;
    }
};

class GalleryThemePopup : public ::cppu::WeakImplHelper< css::frame::XStatusListener >
{
private:
    const GalleryTheme* mpTheme;
    sal_uInt32 const    mnObjectPos;
    bool const          mbPreview;
    VclBuilder          maBuilder;
    VclPtr<PopupMenu> mpPopupMenu;
    VclPtr<PopupMenu> mpBackgroundPopup;
    VclPtr<GalleryBrowser2> mpBrowser;

    typedef std::map< int, CommandInfo > CommandInfoMap;
    CommandInfoMap   m_aCommandInfo;

    static void Execute( const CommandInfo &rCmdInfo,
                  const css::uno::Sequence< css::beans::PropertyValue > &rArguments );

    DECL_LINK( MenuSelectHdl, Menu*, bool );
    DECL_LINK( BackgroundMenuSelectHdl, Menu*, bool );
public:
    GalleryThemePopup( const GalleryTheme* pTheme,
                       sal_uInt32 nObjectPos,
                       bool bPreview,
                       GalleryBrowser2* pBrowser );

    void ExecutePopup( vcl::Window *pParent, const ::Point &aPos );

    virtual void SAL_CALL statusChanged( const css::frame::FeatureStateEvent &rEvent) override;
    virtual void SAL_CALL disposing( const css::lang::EventObject &rSource) override;
};


GalleryThemePopup::GalleryThemePopup(
    const GalleryTheme* pTheme,
    sal_uInt32 nObjectPos,
    bool bPreview,
    GalleryBrowser2* pBrowser )
    : mpTheme( pTheme )
    , mnObjectPos( nObjectPos )
    , mbPreview( bPreview )
    , maBuilder(nullptr, VclBuilderContainer::getUIRootDir(), "svx/ui/gallerymenu2.ui", "")
    , mpPopupMenu(maBuilder.get_menu("menu"))
    , mpBackgroundPopup( VclPtr<PopupMenu>::Create() )
    , mpBrowser( pBrowser )
{
    mpPopupMenu->SetPopupMenu(mpPopupMenu->GetItemId("background"), mpBackgroundPopup);

    // SID_GALLERY_ENABLE_ADDCOPY
    m_aCommandInfo.emplace(
            SID_GALLERY_ENABLE_ADDCOPY,
            CommandInfo( ".uno:GalleryEnableAddCopy" ));
    // SID_GALLERY_BG_BRUSH
    m_aCommandInfo.emplace(
            SID_GALLERY_BG_BRUSH,
            CommandInfo( ".uno:BackgroundImage" ));
    // SID_GALLERY_FORMATS
    m_aCommandInfo.emplace(
            SID_GALLERY_FORMATS,
            CommandInfo( ".uno:InsertGalleryPic" ));

}

void SAL_CALL GalleryThemePopup::statusChanged(
    const css::frame::FeatureStateEvent &rEvent )
{
    const OUString &rURL = rEvent.FeatureURL.Complete;
    if ( rURL == ".uno:GalleryEnableAddCopy" )
    {
        if ( !rEvent.IsEnabled )
        {
            mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("add"), false);
        }
    }
    else if ( rURL == ".uno:BackgroundImage" )
    {
        mpBackgroundPopup->Clear();
        if ( rEvent.IsEnabled )
        {
            OUString sItem;
            css::uno::Sequence< OUString > sItems;
            if ( ( rEvent.State >>= sItem ) && sItem.getLength() )
            {
                mpBackgroundPopup->InsertItem( 1, sItem );
            }
            else if ( ( rEvent.State >>= sItems ) && sItems.hasElements() )
            {
                sal_uInt16 nId = 1;
                for ( const OUString& rStr : std::as_const(sItems) )
                {
                    mpBackgroundPopup->InsertItem( nId, rStr );
                    nId++;
                }
            }
        }
    }
}

void SAL_CALL GalleryThemePopup::disposing(
    const css::lang::EventObject &/*rSource*/)
{
}

void GalleryThemePopup::Execute(
    const CommandInfo &rCmdInfo,
    const css::uno::Sequence< css::beans::PropertyValue > &rArguments )
{
    if ( rCmdInfo.Dispatch.is() )
    {
        std::unique_ptr<DispatchInfo> pInfo(new DispatchInfo);
        pInfo->TargetURL = rCmdInfo.URL;
        pInfo->Arguments = rArguments;
        pInfo->Dispatch = rCmdInfo.Dispatch;

        if ( Application::PostUserEvent(
                LINK( nullptr, GalleryBrowser2, AsyncDispatch_Impl), pInfo.get() ) )
            pInfo.release();
    }
}

void GalleryThemePopup::ExecutePopup( vcl::Window *pWindow, const ::Point &aPos )
{
    css::uno::Reference< css::frame::XStatusListener > xThis( this );

    const SgaObjKind eObjKind = mpTheme->GetObjectKind( mnObjectPos );
    INetURLObject    aURL;

    const_cast< GalleryTheme* >( mpTheme )->GetURL( mnObjectPos, aURL );
    const bool bValidURL = ( aURL.GetProtocol() != INetProtocol::NotValid );

    mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("add"), bValidURL && SgaObjKind::Sound != eObjKind);

    mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("preview"), bValidURL);
    mpPopupMenu->CheckItem("preview", mbPreview);

    if( mpTheme->IsReadOnly() || !mpTheme->GetObjectCount() )
    {
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("delete"), false);
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("title"), false);
        if (mpTheme->IsReadOnly())
            mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("paste"), false);

        if (!mpTheme->GetObjectCount())
            mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("copy"), false);
    }
    else
    {
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("delete"), !mbPreview);
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("title"));
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("copy"));
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("paste"));
    }

    // update status
    css::uno::Reference< css::frame::XDispatchProvider> xDispatchProvider(
        GalleryBrowser2::GetFrame(), css::uno::UNO_QUERY );
    css::uno::Reference< css::util::XURLTransformer > xTransformer(
        mpBrowser->GetURLTransformer() );
    for ( auto& rInfo : m_aCommandInfo )
    {
        try
        {
            CommandInfo &rCmdInfo = rInfo.second;
            if ( xTransformer.is() )
                xTransformer->parseStrict( rCmdInfo.URL );

            if ( xDispatchProvider.is() )
            {
                rCmdInfo.Dispatch = xDispatchProvider->queryDispatch(
                    rCmdInfo.URL,
                    "_self",
                    css::frame::FrameSearchFlag::SELF );
            }

            if ( rCmdInfo.Dispatch.is() )
            {
                rCmdInfo.Dispatch->addStatusListener( this, rCmdInfo.URL );
                rCmdInfo.Dispatch->removeStatusListener( this, rCmdInfo.URL );
            }
        }
        catch ( ... )
        {}
    }

    if( !mpBackgroundPopup->GetItemCount() || ( eObjKind == SgaObjKind::SvDraw ) || ( eObjKind == SgaObjKind::Sound ) )
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("background"), false);
    else
    {
        mpPopupMenu->EnableItem(mpPopupMenu->GetItemId("background"));
        mpBackgroundPopup->SetSelectHdl( LINK( this, GalleryThemePopup, BackgroundMenuSelectHdl ) );
    }

    mpPopupMenu->RemoveDisabledEntries();

    mpPopupMenu->SetSelectHdl( LINK( this, GalleryThemePopup, MenuSelectHdl ) );
    mpPopupMenu->Execute( pWindow, aPos );
}

IMPL_LINK( GalleryThemePopup, MenuSelectHdl, Menu*, pMenu, bool )
{
    if( !pMenu )
        return false;

    OString sIdent(pMenu->GetCurItemIdent());
    if (sIdent == "add")
    {
        const CommandInfoMap::const_iterator it = m_aCommandInfo.find( SID_GALLERY_FORMATS );
        if (it != m_aCommandInfo.end())
            mpBrowser->DispatchAdd(it->second.Dispatch, it->second.URL);
    }
    else
        mpBrowser->Execute(sIdent);

    return false;
}

IMPL_LINK( GalleryThemePopup, BackgroundMenuSelectHdl, Menu*, pMenu, bool )
{
    if( !pMenu )
        return false;

    sal_uInt16 nPos( pMenu->GetCurItemId() - 1 );
    OUString aURL( mpBrowser->GetURL().GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
    OUString aFilterName( mpBrowser->GetFilterName() );

    css::uno::Sequence< css::beans::PropertyValue > aArgs( 6 );
    aArgs[0].Name = "Background.Transparent";
    aArgs[0].Value <<= sal_Int32( 0 ); // 0 - 100
    aArgs[1].Name = "Background.BackColor";
    aArgs[1].Value <<= sal_Int32( - 1 );
    aArgs[2].Name = "Background.URL";
    aArgs[2].Value <<= aURL;
    aArgs[3].Name = "Background.Filtername"; // FIXME should be FilterName
    aArgs[3].Value <<= aFilterName;
    aArgs[4].Name = "Background.Position";
    aArgs[4].Value <<= css::style::GraphicLocation_TILED;
    aArgs[5].Name = "Position";
    aArgs[5].Value <<= nPos;

    const CommandInfoMap::const_iterator it = m_aCommandInfo.find( SID_GALLERY_BG_BRUSH );
    if ( it != m_aCommandInfo.end() )
        Execute( it->second, aArgs );

    return false;
}

} // end anonymous namespace


GalleryToolBox::GalleryToolBox( GalleryBrowser2* pParent ) :
    ToolBox( pParent, WB_TABSTOP )
{
}

void GalleryToolBox::KeyInput( const KeyEvent& rKEvt )
{
    if( !static_cast< GalleryBrowser2* >( GetParent() )->KeyInput( rKEvt, this ) )
    {
        if( KEY_ESCAPE != rKEvt.GetKeyCode().GetCode() )
            ToolBox::KeyInput(rKEvt);
    }
}


GalleryBrowser2::GalleryBrowser2( vcl::Window* pParent, Gallery* pGallery ) :
    Control             ( pParent, WB_TABSTOP ),
    mpGallery           ( pGallery ),
    mpCurTheme          ( nullptr ),
    mpIconView          ( VclPtr<GalleryIconView>::Create( this, nullptr ) ),
    mpListView          ( VclPtr<GalleryListView>::Create( this, nullptr ) ),
    mpPreview           ( VclPtr<GalleryPreview>::Create(this) ),
    maViewBox           ( VclPtr<GalleryToolBox>::Create(this) ),
    maSeparator         ( VclPtr<FixedLine>::Create(this, WB_VERT) ),
    maInfoBar           ( VclPtr<FixedText>::Create(this, WB_LEFT | WB_VCENTER) ),
    mnCurActionPos      ( 0xffffffff ),
    meMode              ( GALLERYBROWSERMODE_NONE ),
    meLastMode          ( GALLERYBROWSERMODE_NONE )
{

    m_xContext.set( ::comphelper::getProcessComponentContext() );

    m_xTransformer.set(
        m_xContext->getServiceManager()->createInstanceWithContext(
            "com.sun.star.util.URLTransformer", m_xContext ),
        css::uno::UNO_QUERY );

    Image      aDummyImage;
    vcl::Font  aInfoFont( maInfoBar->GetControlFont() );

    maMiscOptions.AddListenerLink( LINK( this, GalleryBrowser2, MiscHdl ) );

    maViewBox->InsertItem( TBX_ID_ICON, aDummyImage );
    maViewBox->SetItemBits( TBX_ID_ICON, ToolBoxItemBits::RADIOCHECK | ToolBoxItemBits::AUTOCHECK );
    maViewBox->SetHelpId( TBX_ID_ICON, HID_GALLERY_ICONVIEW );
    maViewBox->SetQuickHelpText( TBX_ID_ICON, SvxResId(RID_SVXSTR_GALLERY_ICONVIEW) );

    maViewBox->InsertItem( TBX_ID_LIST, aDummyImage );
    maViewBox->SetItemBits( TBX_ID_LIST, ToolBoxItemBits::RADIOCHECK | ToolBoxItemBits::AUTOCHECK );
    maViewBox->SetHelpId( TBX_ID_LIST, HID_GALLERY_LISTVIEW );
    maViewBox->SetQuickHelpText( TBX_ID_LIST, SvxResId(RID_SVXSTR_GALLERY_LISTVIEW) );

    MiscHdl( nullptr );
    maViewBox->SetSelectHdl( LINK( this, GalleryBrowser2, SelectTbxHdl ) );
    maViewBox->Show();

    mpIconView->SetAccessibleName(SvxResId(RID_SVXSTR_GALLERY_THEMEITEMS));
    mpListView->SetAccessibleName(SvxResId(RID_SVXSTR_GALLERY_THEMEITEMS));

    maInfoBar->Show();
    maSeparator->Show();

    mpIconView->SetSelectHdl( LINK( this, GalleryBrowser2, SelectObjectValueSetHdl ) );
    mpListView->SetSelectHdl( LINK( this, GalleryBrowser2, SelectObjectHdl ) );

    InitSettings();

    SetMode( ( GALLERYBROWSERMODE_PREVIEW != GalleryBrowser2::meInitMode ) ? GalleryBrowser2::meInitMode : GALLERYBROWSERMODE_ICON );

    if(maInfoBar->GetText().isEmpty())
        mpIconView->SetAccessibleRelationLabeledBy(mpIconView);
    else
        mpIconView->SetAccessibleRelationLabeledBy(maInfoBar.get());
}

GalleryBrowser2::~GalleryBrowser2()
{
    disposeOnce();
}

void GalleryBrowser2::dispose()
{
    maMiscOptions.RemoveListenerLink( LINK( this, GalleryBrowser2, MiscHdl ) );

    mpPreview.disposeAndClear();
    mpListView.disposeAndClear();
    mpIconView.disposeAndClear();

    if( mpCurTheme )
        mpGallery->ReleaseTheme( mpCurTheme, *this );
    maSeparator.disposeAndClear();
    maInfoBar.disposeAndClear();
    maViewBox.disposeAndClear();
    Control::dispose();
}

void GalleryBrowser2::InitSettings()
{
    vcl::Font  aInfoFont( maInfoBar->GetControlFont() );

    aInfoFont.SetWeight( WEIGHT_BOLD );
    aInfoFont.SetColor( GALLERY_FG_COLOR );
    maInfoBar->SetControlFont( aInfoFont );

    maInfoBar->SetBackground( Wallpaper( GALLERY_DLG_COLOR ) );
    maInfoBar->SetControlBackground( GALLERY_DLG_COLOR );

    maSeparator->SetBackground( Wallpaper( GALLERY_BG_COLOR ) );
    maSeparator->SetControlBackground( GALLERY_BG_COLOR );
    maSeparator->SetControlForeground( GALLERY_FG_COLOR );
}

void GalleryBrowser2::DataChanged( const DataChangedEvent& rDCEvt )
{
    if ( ( rDCEvt.GetType() == DataChangedEventType::SETTINGS ) && ( rDCEvt.GetFlags() & AllSettingsFlags::STYLE ) )
        InitSettings();
    else
        Control::DataChanged( rDCEvt );
}

void GalleryBrowser2::Resize()
{
    Control::Resize();

    mpIconView->Hide();
    mpListView->Hide();
    mpPreview->Hide();

    const Size  aOutSize( GetOutputSizePixel() );
    const Size  aBoxSize( maViewBox->GetOutputSizePixel() );
    const long  nOffset = 2, nSepWidth = 2;
    const long  nInfoBarX = aBoxSize.Width() + ( nOffset * 3 ) + nSepWidth;
    const Point aPt( 0, aBoxSize.Height() + 3 );
    const Size  aSz( aOutSize.Width(), aOutSize.Height() - aPt.Y() );

    maSeparator->SetPosSizePixel( Point( aBoxSize.Width() + nOffset, 0 ), Size( nSepWidth, aBoxSize.Height() ) );
    maInfoBar->SetPosSizePixel( Point( nInfoBarX, 0 ), Size( aOutSize.Width() - nInfoBarX, aBoxSize.Height() ) );

    mpIconView->SetPosSizePixel( aPt, aSz );
    mpListView->SetPosSizePixel( aPt, aSz );
    mpPreview->SetPosSizePixel( aPt, aSz );

    switch( GetMode() )
    {
        case GALLERYBROWSERMODE_ICON: mpIconView->Show(); break;
        case GALLERYBROWSERMODE_LIST: mpListView->Show(); break;
        case GALLERYBROWSERMODE_PREVIEW: mpPreview->Show(); break;

        default:
        break;
    }
}

void GalleryBrowser2::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    const GalleryHint& rGalleryHint = static_cast<const GalleryHint&>(rHint);

    switch( rGalleryHint.GetType() )
    {
        case GalleryHintType::THEME_UPDATEVIEW:
        {
            if( GALLERYBROWSERMODE_PREVIEW == GetMode() )
                SetMode( meLastMode );

            ImplUpdateViews( reinterpret_cast<size_t>(rGalleryHint.GetData1()) + 1 );
        }
        break;

        default:
        break;
    }
}

sal_Int8 GalleryBrowser2::AcceptDrop( DropTargetHelper& rTarget )
{
    sal_Int8 nRet = DND_ACTION_NONE;

    if( mpCurTheme && !mpCurTheme->IsReadOnly() )
    {
        if( !mpCurTheme->IsDragging() )
        {
            if( rTarget.IsDropFormatSupported( SotClipboardFormatId::DRAWING ) ||
                rTarget.IsDropFormatSupported( SotClipboardFormatId::FILE_LIST ) ||
                rTarget.IsDropFormatSupported( SotClipboardFormatId::SIMPLE_FILE ) ||
                rTarget.IsDropFormatSupported( SotClipboardFormatId::SVXB ) ||
                rTarget.IsDropFormatSupported( SotClipboardFormatId::GDIMETAFILE ) ||
                rTarget.IsDropFormatSupported( SotClipboardFormatId::BITMAP ) )
            {
                nRet = DND_ACTION_COPY;
            }
        }
        else
            nRet = DND_ACTION_COPY;
    }

    return nRet;
}

sal_Int8 GalleryBrowser2::ExecuteDrop( const ExecuteDropEvent& rEvt )
{
    sal_Int8 nRet = DND_ACTION_NONE;

    if( mpCurTheme )
    {
        Point       aSelPos;
        const sal_uInt32 nItemId = ImplGetSelectedItemId( &rEvt.maPosPixel, aSelPos );
        const sal_uInt32 nInsertPos = (nItemId ? (nItemId - 1) : mpCurTheme->GetObjectCount());

        if( mpCurTheme->IsDragging() )
            mpCurTheme->ChangeObjectPos( mpCurTheme->GetDragPos(), nInsertPos );
        else
            nRet = mpCurTheme->InsertTransferable( rEvt.maDropEvent.Transferable, nInsertPos ) ? 1 : 0;
    }

    return nRet;
}

void GalleryBrowser2::StartDrag( const Point* pDragPoint )
{
    if( mpCurTheme )
    {
        Point       aSelPos;
        const sal_uInt32 nItemId = ImplGetSelectedItemId( pDragPoint, aSelPos );

        if( nItemId )
            mpCurTheme->StartDrag( this, nItemId - 1 );
    }
}

void GalleryBrowser2::TogglePreview()
{
    SetMode( ( GALLERYBROWSERMODE_PREVIEW != GetMode() ) ? GALLERYBROWSERMODE_PREVIEW : meLastMode );
    GetViewWindow()->GrabFocus();
}

void GalleryBrowser2::ShowContextMenu( const Point* pContextPoint )
{
    Point aSelPos;
    const sal_uInt32 nItemId = ImplGetSelectedItemId( pContextPoint, aSelPos );

    if( mpCurTheme && nItemId && ( nItemId <= mpCurTheme->GetObjectCount() ) )
    {
        ImplSelectItemId( nItemId );

        css::uno::Reference< css::frame::XFrame > xFrame( GetFrame() );
        if ( xFrame.is() )
        {
            mnCurActionPos = nItemId - 1;
            rtl::Reference< GalleryThemePopup > rPopup(
                new GalleryThemePopup(
                    mpCurTheme,
                    mnCurActionPos,
                    GALLERYBROWSERMODE_PREVIEW == GetMode(),
                    this ) );
            rPopup->ExecutePopup( this, aSelPos  );
        }
    }
}

bool GalleryBrowser2::KeyInput( const KeyEvent& rKEvt, vcl::Window* /*pWindow*/ )
{
    Point       aSelPos;
    const sal_uInt32 nItemId = ImplGetSelectedItemId( nullptr, aSelPos );
    bool bRet = false;
    svx::sidebar::GalleryControl* pParentControl = dynamic_cast<svx::sidebar::GalleryControl*>(GetParent());
    if (pParentControl != nullptr)
        bRet = pParentControl->GalleryKeyInput(rKEvt);

    if( !bRet && !maViewBox->HasFocus() && nItemId && mpCurTheme )
    {
        OString sExecuteIdent;
        INetURLObject       aURL;

        mpCurTheme->GetURL( nItemId - 1, aURL );

        const bool  bValidURL = ( aURL.GetProtocol() != INetProtocol::NotValid );
        bool        bPreview = bValidURL;
        bool        bDelete = false;
        bool        bTitle = false;

        if( !mpCurTheme->IsReadOnly() && mpCurTheme->GetObjectCount() )
        {
            bDelete = ( GALLERYBROWSERMODE_PREVIEW != GetMode() );
            bTitle = true;
        }

        switch( rKEvt.GetKeyCode().GetCode() )
        {
            case KEY_SPACE:
            case KEY_RETURN:
            case KEY_P:
            {
                if( bPreview )
                {
                    TogglePreview();
                    bRet = true;
                }
            }
            break;

            case KEY_INSERT:
            case KEY_I:
            {
                // Inserting a gallery item in the document must be dispatched
                if( bValidURL )
                {
                    DispatchAdd(css::uno::Reference<css::frame::XDispatch>(), css::util::URL());
                    return true;
                }
            }
            break;

            case KEY_DELETE:
            case KEY_D:
            {
                if( bDelete )
                    sExecuteIdent = "delete";
            }
            break;

            case KEY_T:
            {
                if( bTitle )
                    sExecuteIdent = "title";
            }
            break;

            default:
            break;
        }

        if (!sExecuteIdent.isEmpty())
        {
            Execute(sExecuteIdent);
            bRet = true;
        }
    }

    return bRet;
}

void GalleryBrowser2::SelectTheme( const OUString& rThemeName )
{
    mpIconView.disposeAndClear();
    mpListView.disposeAndClear();
    mpPreview.disposeAndClear();

    if( mpCurTheme )
        mpGallery->ReleaseTheme( mpCurTheme, *this );

    mpCurTheme = mpGallery->AcquireTheme( rThemeName, *this );

    mpIconView = VclPtr<GalleryIconView>::Create( this, mpCurTheme );
    mpListView = VclPtr<GalleryListView>::Create( this, mpCurTheme );
    mpPreview = VclPtr<GalleryPreview>::Create( this, WB_TABSTOP | WB_BORDER, mpCurTheme );

    mpIconView->SetAccessibleName(SvxResId(RID_SVXSTR_GALLERY_THEMEITEMS));
    mpListView->SetAccessibleName(SvxResId(RID_SVXSTR_GALLERY_THEMEITEMS));
    mpPreview->SetAccessibleName(SvxResId(RID_SVXSTR_GALLERY_PREVIEW));

    mpIconView->SetSelectHdl( LINK( this, GalleryBrowser2, SelectObjectValueSetHdl ) );
    mpListView->SetSelectHdl( LINK( this, GalleryBrowser2, SelectObjectHdl ) );

    if( GALLERYBROWSERMODE_PREVIEW == GetMode() )
        meMode = meLastMode;

    Resize();
    ImplUpdateViews( 1 );

    maViewBox->EnableItem( TBX_ID_ICON );
    maViewBox->EnableItem( TBX_ID_LIST );
    maViewBox->CheckItem( ( GALLERYBROWSERMODE_ICON == GetMode() ) ? TBX_ID_ICON : TBX_ID_LIST );

    if(maInfoBar->GetText().isEmpty())
        mpIconView->SetAccessibleRelationLabeledBy(mpIconView);
    else
        mpIconView->SetAccessibleRelationLabeledBy(maInfoBar.get());
}

void GalleryBrowser2::SetMode( GalleryBrowserMode eMode )
{
    if( GetMode() != eMode )
    {
        meLastMode = GetMode();

        switch( eMode )
        {
            case GALLERYBROWSERMODE_ICON:
            {
                mpListView->Hide();

                mpPreview->Hide();
                mpPreview->SetGraphic( Graphic() );
                GalleryPreview::PreviewMedia( INetURLObject() );

                mpIconView->Show();

                maViewBox->EnableItem( TBX_ID_ICON );
                maViewBox->EnableItem( TBX_ID_LIST );

                maViewBox->CheckItem( TBX_ID_ICON );
                maViewBox->CheckItem( TBX_ID_LIST, false );
            }
            break;

            case GALLERYBROWSERMODE_LIST:
            {
                mpIconView->Hide();

                mpPreview->Hide();
                mpPreview->SetGraphic( Graphic() );
                GalleryPreview::PreviewMedia( INetURLObject() );

                mpListView->Show();

                maViewBox->EnableItem( TBX_ID_ICON );
                maViewBox->EnableItem( TBX_ID_LIST );

                maViewBox->CheckItem( TBX_ID_ICON, false );
                maViewBox->CheckItem( TBX_ID_LIST );
            }
            break;

            case GALLERYBROWSERMODE_PREVIEW:
            {
                Graphic     aGraphic;
                Point       aSelPos;
                const sal_uInt32 nItemId = ImplGetSelectedItemId( nullptr, aSelPos );

                if( nItemId )
                {
                    const sal_uInt32 nPos = nItemId - 1;

                    mpIconView->Hide();
                    mpListView->Hide();

                    if( mpCurTheme )
                        mpCurTheme->GetGraphic( nPos, aGraphic );

                    mpPreview->SetGraphic( aGraphic );
                    mpPreview->Show();

                    if( mpCurTheme && mpCurTheme->GetObjectKind( nPos ) == SgaObjKind::Sound )
                        GalleryPreview::PreviewMedia( mpCurTheme->GetObjectURL( nPos ) );

                    maViewBox->EnableItem( TBX_ID_ICON, false );
                    maViewBox->EnableItem( TBX_ID_LIST, false );
                }
            }
            break;

            default:
                break;
        }

        GalleryBrowser2::meInitMode = meMode = eMode;
    }
}

vcl::Window* GalleryBrowser2::GetViewWindow() const
{
    vcl::Window* pRet;

    switch( GetMode() )
    {
        case GALLERYBROWSERMODE_LIST: pRet = mpListView; break;
        case GALLERYBROWSERMODE_PREVIEW: pRet = mpPreview; break;

        default:
            pRet = mpIconView;
        break;
    }

    return pRet;
}

void GalleryBrowser2::Travel( GalleryBrowserTravel eTravel )
{
    if( mpCurTheme )
    {
        Point       aSelPos;
        const sal_uInt32 nItemId = ImplGetSelectedItemId( nullptr, aSelPos );

        if( nItemId )
        {
            sal_uInt32 nNewItemId = nItemId;

            switch( eTravel )
            {
                case GalleryBrowserTravel::First:     nNewItemId = 1; break;
                case GalleryBrowserTravel::Last:      nNewItemId = mpCurTheme->GetObjectCount(); break;
                case GalleryBrowserTravel::Previous:  nNewItemId--; break;
                case GalleryBrowserTravel::Next:      nNewItemId++; break;
                default:
                    break;
            }

            if( nNewItemId < 1 )
                nNewItemId = 1;
            else if( nNewItemId > mpCurTheme->GetObjectCount() )
                nNewItemId = mpCurTheme->GetObjectCount();

            if( nNewItemId != nItemId )
            {
                ImplSelectItemId( nNewItemId );
                ImplUpdateInfoBar();

                if( GALLERYBROWSERMODE_PREVIEW == GetMode() )
                {
                    Graphic     aGraphic;
                    const sal_uInt32 nPos = nNewItemId - 1;

                    mpCurTheme->GetGraphic( nPos, aGraphic );
                    mpPreview->SetGraphic( aGraphic );

                    if( SgaObjKind::Sound == mpCurTheme->GetObjectKind( nPos ) )
                        GalleryPreview::PreviewMedia( mpCurTheme->GetObjectURL( nPos ) );

                    mpPreview->Invalidate();
                }
            }
        }
    }
}

void GalleryBrowser2::ImplUpdateViews( sal_uInt16 nSelectionId )
{
    mpIconView->Hide();
    mpListView->Hide();
    mpPreview->Hide();

    mpIconView->Clear();
    mpListView->Clear();

    if( mpCurTheme )
    {
        for (sal_uInt32 i = 0, nCount = mpCurTheme->GetObjectCount(); i < nCount;)
        {
            mpListView->RowInserted( i++ );
            mpIconView->InsertItem( static_cast<sal_uInt16>(i) );
        }

        ImplSelectItemId( std::min<sal_uInt16>( nSelectionId, mpCurTheme->GetObjectCount() ) );
    }

    switch( GetMode() )
    {
        case GALLERYBROWSERMODE_ICON: mpIconView->Show(); break;
        case GALLERYBROWSERMODE_LIST: mpListView->Show(); break;
        case GALLERYBROWSERMODE_PREVIEW: mpPreview->Show(); break;

        default:
        break;
    }

    ImplUpdateInfoBar();
}

void GalleryBrowser2::ImplUpdateInfoBar()
{
    if( mpCurTheme )
         maInfoBar->SetText( mpCurTheme->GetName() );
}

sal_uInt32 GalleryBrowser2::ImplGetSelectedItemId( const Point* pSelPos, Point& rSelPos )
{
    const Size  aOutputSizePixel( GetOutputSizePixel() );
    sal_uInt32 nRet = 0;

    if( GALLERYBROWSERMODE_PREVIEW == GetMode() )
    {
        nRet = ( ( GALLERYBROWSERMODE_ICON == meLastMode ) ? mpIconView->GetSelectedItemId() : ( mpListView->FirstSelectedRow() + 1 ) );

        if( pSelPos )
            rSelPos = GetPointerPosPixel();
        else
            rSelPos = Point( aOutputSizePixel.Width() >> 1, aOutputSizePixel.Height() >> 1 );
    }
    else if( GALLERYBROWSERMODE_ICON == GetMode() )
    {
        if( pSelPos )
        {
            nRet = mpIconView->GetItemId( *pSelPos );
            rSelPos = GetPointerPosPixel();
        }
        else
        {
            nRet = mpIconView->GetSelectedItemId();
            rSelPos = mpIconView->GetItemRect(nRet).Center();
        }
    }
    else
    {
        if( pSelPos )
        {
            nRet = mpListView->GetRowAtYPosPixel( pSelPos->Y() ) + 1;
            rSelPos = GetPointerPosPixel();
        }
        else
        {
            nRet = mpListView->FirstSelectedRow() + 1;
            rSelPos = mpListView->GetFieldRectPixel( static_cast<sal_uInt16>(nRet), 1 ).Center();
        }
    }

    rSelPos.setX( std::max( std::min( rSelPos.X(), aOutputSizePixel.Width() - 1 ), 0L ) );
    rSelPos.setY( std::max( std::min( rSelPos.Y(), aOutputSizePixel.Height() - 1 ), 0L ) );

    if( nRet && ( !mpCurTheme || ( nRet > mpCurTheme->GetObjectCount() ) ) )
    {
        nRet = 0;
    }

    return nRet;
}

void GalleryBrowser2::ImplSelectItemId(sal_uInt32 nItemId)
{
    if( nItemId )
    {

        mpIconView->SelectItem(nItemId);
        mpListView->SelectRow( nItemId - 1 );
    }
}

css::uno::Reference< css::frame::XFrame >
GalleryBrowser2::GetFrame()
{
    css::uno::Reference< css::frame::XFrame > xFrame;
    SfxViewFrame* pCurrentViewFrame = SfxViewFrame::Current();
    if ( pCurrentViewFrame )
    {
        SfxBindings& rBindings = pCurrentViewFrame->GetBindings();
        xFrame.set( rBindings.GetActiveFrame() );
    }

    return xFrame;
}

void GalleryBrowser2::DispatchAdd(
    const css::uno::Reference< css::frame::XDispatch > &rxDispatch,
    const css::util::URL &rURL)
{
    Point aSelPos;
    const sal_uInt32 nItemId = ImplGetSelectedItemId( nullptr, aSelPos );

    if( !mpCurTheme || !nItemId )
        return;

    mnCurActionPos = nItemId - 1;

    css::uno::Reference< css::frame::XDispatch > xDispatch( rxDispatch );
    css::util::URL aURL = rURL;

    if ( !xDispatch.is() )
    {
        css::uno::Reference< css::frame::XDispatchProvider > xDispatchProvider(
            GetFrame(), css::uno::UNO_QUERY );
        if ( !xDispatchProvider.is() || !m_xTransformer.is() )
            return;

        aURL.Complete = ".uno:InsertGalleryPic";
        m_xTransformer->parseStrict( aURL );
        xDispatch = xDispatchProvider->queryDispatch(
            aURL,
            "_self",
            css::frame::FrameSearchFlag::SELF );
    }

    if ( !xDispatch.is() )
        return;

    sal_Int8 nType = 0;
    OUString aFilterName;
    css::uno::Reference< css::lang::XComponent > xDrawing;
    css::uno::Reference< css::graphic::XGraphic > xGraphic;

    aFilterName = GetFilterName();

    switch( mpCurTheme->GetObjectKind( mnCurActionPos ) )
    {
        case SgaObjKind::Bitmap:
        case SgaObjKind::Animation:
        case SgaObjKind::Inet:
        // TODO drawing objects are inserted as drawings only via drag&drop
        case SgaObjKind::SvDraw:
            nType = css::gallery::GalleryItemType::GRAPHIC;
        break;

        case SgaObjKind::Sound :
            nType = css::gallery::GalleryItemType::MEDIA;
        break;

        default:
            nType = css::gallery::GalleryItemType::EMPTY;
        break;
    }

    Graphic aGraphic;
    bool bGraphic = mpCurTheme->GetGraphic( mnCurActionPos, aGraphic );
    if ( bGraphic && !aGraphic.IsNone() )
        xGraphic.set( aGraphic.GetXGraphic() );
    OSL_ENSURE( xGraphic.is(), "gallery item is graphic, but the reference is invalid!" );

    css::uno::Sequence< css::beans::PropertyValue > aSeq( SVXGALLERYITEM_PARAMS );

    aSeq[0].Name = SVXGALLERYITEM_TYPE;
    aSeq[0].Value <<= nType;
    aSeq[1].Name = SVXGALLERYITEM_URL;
    aSeq[1].Value <<= OUString();
    aSeq[2].Name = SVXGALLERYITEM_FILTER;
    aSeq[2].Value <<= aFilterName;
    aSeq[3].Name = SVXGALLERYITEM_DRAWING;
    aSeq[3].Value <<= xDrawing;
    aSeq[4].Name = SVXGALLERYITEM_GRAPHIC;
    aSeq[4].Value <<= xGraphic;

    css::uno::Sequence< css::beans::PropertyValue > aArgs( 1 );
    aArgs[0].Name = SVXGALLERYITEM_ARGNAME;
    aArgs[0].Value <<= aSeq;

    std::unique_ptr<DispatchInfo> pInfo(new DispatchInfo);
    pInfo->TargetURL = aURL;
    pInfo->Arguments = aArgs;
    pInfo->Dispatch = xDispatch;

    if ( Application::PostUserEvent(
            LINK( nullptr, GalleryBrowser2, AsyncDispatch_Impl), pInfo.get() ) )
        pInfo.release();
}

void GalleryBrowser2::Execute(const OString &rIdent)
{
    Point       aSelPos;
    const sal_uInt32 nItemId = ImplGetSelectedItemId( nullptr, aSelPos );

    if( mpCurTheme && nItemId )
    {
        mnCurActionPos = nItemId - 1;

        if (rIdent == "preview")
            SetMode( ( GALLERYBROWSERMODE_PREVIEW != GetMode() ) ? GALLERYBROWSERMODE_PREVIEW : meLastMode );
        else if (rIdent == "delete")
        {
            if (!mpCurTheme->IsReadOnly())
            {
                std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(GetFrameWeld(), "svx/ui/querydeleteobjectdialog.ui"));
                std::unique_ptr<weld::MessageDialog> xQuery(xBuilder->weld_message_dialog("QueryDeleteObjectDialog"));
                if (xQuery->run() == RET_YES)
                {
                    mpCurTheme->RemoveObject( mnCurActionPos );
                }
            }
        }
        else if (rIdent == "title")
        {
            std::unique_ptr<SgaObject> pObj = mpCurTheme->AcquireObject( mnCurActionPos );

            if( pObj )
            {
                const OUString  aOldTitle( GetItemText( *pObj, GalleryItemFlags::Title ) );

                SvxAbstractDialogFactory* pFact = SvxAbstractDialogFactory::Create();
                ScopedVclPtr<AbstractTitleDialog> aDlg(pFact->CreateTitleDialog(GetFrameWeld(), aOldTitle));
                if( aDlg->Execute() == RET_OK )
                {
                    OUString aNewTitle( aDlg->GetTitle() );

                    if( ( aNewTitle.isEmpty() && !pObj->GetTitle().isEmpty() ) || ( aNewTitle != aOldTitle ) )
                    {
                        if( aNewTitle.isEmpty() )
                            aNewTitle = "__<empty>__";

                        pObj->SetTitle( aNewTitle );
                        mpCurTheme->InsertObject( *pObj );
                    }
                }
            }
        }
        else if (rIdent == "copy")
        {
            vcl::Window* pWindow;

            switch( GetMode() )
            {
                case GALLERYBROWSERMODE_ICON: pWindow = static_cast<vcl::Window*>(mpIconView); break;
                case GALLERYBROWSERMODE_LIST: pWindow = static_cast<vcl::Window*>(mpListView); break;
                case GALLERYBROWSERMODE_PREVIEW: pWindow = static_cast<vcl::Window*>(mpPreview); break;

                default:
                    pWindow = nullptr;
                break;
            }

            mpCurTheme->CopyToClipboard( pWindow, mnCurActionPos );
        }
        else if (rIdent == "paste")
        {
            if( !mpCurTheme->IsReadOnly() )
            {
                TransferableDataHelper aDataHelper( TransferableDataHelper::CreateFromSystemClipboard( this ) );
                mpCurTheme->InsertTransferable( aDataHelper.GetTransferable(), mnCurActionPos );
             }
        }
    }
}

OUString GalleryBrowser2::GetItemText( const SgaObject& rObj, GalleryItemFlags nItemTextFlags )
{
    OUString          aRet;

    const INetURLObject& aURL(rObj.GetURL());

    if( nItemTextFlags & GalleryItemFlags::Title )
    {
        OUString aTitle( rObj.GetTitle() );

        if( aTitle.isEmpty() )
            aTitle = aURL.getBase( INetURLObject::LAST_SEGMENT, true, INetURLObject::DecodeMechanism::Unambiguous );

        if( aTitle.isEmpty() )
        {
            aTitle = aURL.GetMainURL( INetURLObject::DecodeMechanism::Unambiguous );
            aTitle = aTitle.copy( aTitle.lastIndexOf('/')+1 );
        }

        aRet += aTitle;
    }

    if( nItemTextFlags & GalleryItemFlags::Path )
    {
        const OUString aPath( aURL.getFSysPath( FSysStyle::Detect ) );

        if( !aPath.isEmpty() && ( nItemTextFlags & GalleryItemFlags::Title ) )
            aRet += " (";

        aRet += aURL.getFSysPath( FSysStyle::Detect );

        if( !aPath.isEmpty() && ( nItemTextFlags & GalleryItemFlags::Title ) )
            aRet += ")";
    }

    return aRet;
}

INetURLObject GalleryBrowser2::GetURL() const
{
    INetURLObject aURL;

    if( mpCurTheme && mnCurActionPos != 0xffffffff )
        aURL = mpCurTheme->GetObjectURL( mnCurActionPos );

    return aURL;
}

OUString GalleryBrowser2::GetFilterName() const
{
    OUString aFilterName;

    if( mpCurTheme && mnCurActionPos != 0xffffffff )
    {
        const SgaObjKind eObjKind = mpCurTheme->GetObjectKind( mnCurActionPos );

        if( ( SgaObjKind::Bitmap == eObjKind ) || ( SgaObjKind::Animation == eObjKind ) )
        {
            GraphicFilter& rFilter = GraphicFilter::GetGraphicFilter();
            INetURLObject       aURL;
            mpCurTheme->GetURL( mnCurActionPos, aURL );
            sal_uInt16 nFilter = rFilter.GetImportFormatNumberForShortName(aURL.GetFileExtension());

            if( GRFILTER_FORMAT_DONTKNOW != nFilter )
                aFilterName = rFilter.GetImportFormatName( nFilter );
        }
    }

    return aFilterName;
}


IMPL_LINK_NOARG(GalleryBrowser2, SelectObjectValueSetHdl, ValueSet*, void)
{
    ImplUpdateInfoBar();
}

IMPL_LINK_NOARG(GalleryBrowser2, SelectObjectHdl, GalleryListView*, void)
{
    ImplUpdateInfoBar();
}

IMPL_LINK( GalleryBrowser2, SelectTbxHdl, ToolBox*, pBox, void )
{
    if( pBox->GetCurItemId() == TBX_ID_ICON )
        SetMode( GALLERYBROWSERMODE_ICON );
    else if( pBox->GetCurItemId() == TBX_ID_LIST )
        SetMode( GALLERYBROWSERMODE_LIST );
}

IMPL_LINK_NOARG(GalleryBrowser2, MiscHdl, LinkParamNone*, void)
{
    maViewBox->SetOutStyle( maMiscOptions.GetToolboxStyle() );

    BitmapEx aIconBmpEx(RID_SVXBMP_GALLERY_VIEW_ICON);
    BitmapEx aListBmpEx(RID_SVXBMP_GALLERY_VIEW_LIST);

    if( maMiscOptions.AreCurrentSymbolsLarge() )
    {
        const Size aLargeSize( 24, 24);

        aIconBmpEx.Scale( aLargeSize );
        aListBmpEx.Scale( aLargeSize );
    }

    maViewBox->SetItemImage(TBX_ID_ICON, Image(aIconBmpEx));
    maViewBox->SetItemImage(TBX_ID_LIST, Image(aListBmpEx));
    maViewBox->SetSizePixel( maViewBox->CalcWindowSizePixel() );

    Resize();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
