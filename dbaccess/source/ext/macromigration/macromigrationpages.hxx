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

#ifndef INCLUDED_DBACCESS_SOURCE_EXT_MACROMIGRATION_MACROMIGRATIONPAGES_HXX
#define INCLUDED_DBACCESS_SOURCE_EXT_MACROMIGRATION_MACROMIGRATIONPAGES_HXX

#include "migrationprogress.hxx"
#include "rangeprogressbar.hxx"

#include <svtools/urlcontrol.hxx>
#include <vcl/wizardmachine.hxx>
#include <svx/databaselocationinput.hxx>
#include <vcl/fixed.hxx>
#include <vcl/edit.hxx>
#include <vcl/vclmedit.hxx>

namespace vcl
{
    class RoadmapWizard;
}

namespace dbmm
{

    class MacroMigrationDialog;

    // MacroMigrationPage
    typedef ::vcl::OWizardPage  MacroMigrationPage_Base;
    class MacroMigrationPage : public MacroMigrationPage_Base
    {
    public:
        MacroMigrationPage(vcl::Window *pParent, const OString& rID, const OUString& rUIXMLDescription);

    protected:
        MacroMigrationDialog& getDialog();
    };

    // PreparationPage
    class PreparationPage final : public MacroMigrationPage
    {
    public:
        explicit PreparationPage(vcl::Window *pParent);
        virtual ~PreparationPage() override;
        virtual void dispose() override;

        static VclPtr<TabPage> Create( ::vcl::RoadmapWizard& _rParentDialog );

        void    showCloseDocsError(bool _bShow);

    private:
        VclPtr<FixedText>  m_pCloseDocError;
    };

    // SaveDBDocPage
    class SaveDBDocPage final : public MacroMigrationPage
    {
    public:
        explicit SaveDBDocPage(MacroMigrationDialog& _rParentDialog);
        virtual ~SaveDBDocPage() override;
        virtual void dispose() override;
        static VclPtr<TabPage> Create( ::vcl::RoadmapWizard& _rParentDialog );

        OUString getBackupLocation() const { return m_pLocationController->getURL(); }
        void            grabLocationFocus() { m_pSaveAsLocation->GrabFocus(); }

    private:
        VclPtr< ::svt::OFileURLControl>  m_pSaveAsLocation;
        VclPtr<PushButton>             m_pBrowseSaveAsLocation;
        VclPtr<FixedText>              m_pStartMigration;
        std::unique_ptr<svx::DatabaseLocationInputController> m_pLocationController;

        // IWizardPageController overridables
        virtual void        initializePage() override;
        virtual bool        commitPage( ::vcl::WizardTypes::CommitPageReason _eReason ) override;
        virtual bool        canAdvance() const override;

        DECL_LINK( OnLocationModified, Edit&, void );
        void impl_updateLocationDependentItems();
    };

    // ProgressPage
    class ProgressPage : public MacroMigrationPage, public IMigrationProgress
    {
    public:
        explicit ProgressPage(vcl::Window *pParent);
        virtual ~ProgressPage() override;
        virtual void dispose() override;

        static VclPtr<TabPage> Create( ::vcl::RoadmapWizard& _rParentDialog );

        void    setDocumentCounts( const sal_Int32 _nForms, const sal_Int32 _nReports );
        void    onFinishedSuccessfully();

    protected:
        // IMigrationProgress
        virtual void    startObject( const OUString& _rObjectName, const OUString& _rCurrentAction, const sal_uInt32 _bRange ) override;
        virtual void    setObjectProgressText( const OUString& _rText ) override;
        virtual void    setObjectProgressValue( const sal_uInt32 _nValue ) override;
        virtual void    endObject() override;
        virtual void    start( const sal_uInt32 _nOverallRange ) override;
        virtual void    setOverallProgressText( const OUString& _rText ) override;
        virtual void    setOverallProgressValue( const sal_uInt32 _nValue ) override;

    private:
        VclPtr<FixedText>          m_pObjectCount;
        VclPtr<FixedText>          m_pCurrentObject;
        VclPtr<FixedText>          m_pCurrentAction;
        RangeProgressBar           m_aCurrentProgress;
        VclPtr<FixedText>          m_pAllProgressText;
        RangeProgressBar           m_aAllProgress;
        VclPtr<FixedText>          m_pMigrationDone;
    };

    // ResultPage
    class ResultPage : public MacroMigrationPage
    {
    public:
        explicit ResultPage(vcl::Window *pParent);
        virtual ~ResultPage() override;
        virtual void dispose() override;

        static VclPtr<TabPage> Create( ::vcl::RoadmapWizard& _rParentDialog );

        void            displayMigrationLog( const bool _bSuccessful, const OUString& _rLog );

    private:
        VclPtr<FixedText>        m_pSuccessLabel;
        VclPtr<FixedText>        m_pFailureLabel;
        VclPtr<VclMultiLineEdit> m_pChanges;
    };

} // namespace dbmm

#endif // INCLUDED_DBACCESS_SOURCE_EXT_MACROMIGRATION_MACROMIGRATIONPAGES_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
