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

#ifndef INCLUDED_DBACCESS_SOURCE_UI_INC_OPENDOCCONTROLS_HXX
#define INCLUDED_DBACCESS_SOURCE_UI_INC_OPENDOCCONTROLS_HXX

#include <vcl/button.hxx>
#include <vcl/weld.hxx>
#include <rtl/ustring.hxx>
#include <map>

namespace dbaui
{

    // OpenDocumentButton
    /** a button which can be used to open a document

        The text of the button is the same as for the "Open" command in the application
        UI. Additionally, the icon for this command is also displayed on the button.
    */
    class OpenDocumentButton
    {
    private:
        OUString     m_sModule;

        std::unique_ptr<weld::Button> m_xControl;
    public:
        OpenDocumentButton(std::unique_ptr<weld::Button> xControl, const sal_Char* _pAsciiModuleName);

        void set_sensitive(bool bSensitive) { m_xControl->set_sensitive(bSensitive); }
        bool get_sensitive() const { return m_xControl->get_sensitive(); }
        void connect_clicked(const Link<weld::Button&, void>& rLink) { m_xControl->connect_clicked(rLink); }

    private:
        void    impl_init( const sal_Char* _pAsciiModuleName );
    };

    // OpenDocumentListBox
    class OpenDocumentListBox
    {
    private:
        typedef std::pair< OUString, OUString >       StringPair;

        std::vector<StringPair> m_aURLs;

        std::unique_ptr<weld::ComboBox> m_xControl;

    public:
        OpenDocumentListBox(std::unique_ptr<weld::ComboBox> xControl, const sal_Char* _pAsciiModuleName);

        OUString  GetSelectedDocumentURL() const;

        void set_sensitive(bool bSensitive) { m_xControl->set_sensitive(bSensitive); }
        bool get_sensitive() const { return m_xControl->get_sensitive(); }
        void grab_focus() { m_xControl->grab_focus(); }
        int get_count() { return m_xControl->get_count(); }
        void set_active(int nPos) { m_xControl->set_active(nPos); }
        void connect_changed(const Link<weld::ComboBox&, void>& rLink) { m_xControl->connect_changed(rLink); }

    private:
        StringPair  impl_getDocumentAtIndex( sal_uInt16 _nListIndex, bool _bSystemNotation = false ) const;

        void    impl_init( const sal_Char* _pAsciiModuleName );
    };

} // namespace dbaui

#endif // INCLUDED_DBACCESS_SOURCE_UI_INC_OPENDOCCONTROLS_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
