/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_SW_SOURCE_CORE_TEXT_DATEFORMEFIELDBUTTO_HXX
#define INCLUDED_SW_SOURCE_CORE_TEXT_DATEFORMEFIELDBUTTO_HXX

#include <vcl/menubtn.hxx>
#include <swrect.hxx>
#include "FormFieldButton.hxx"

class SwEditWin;
class FloatingWindow;
class SvNumberFormatter;
namespace sw
{
namespace mark
{
class DateFieldmark;
}
} // namespace sw

/**
 * This button is shown when the cursor is on a date form field.
 * The user can select a date from a date picker while filling in a form.
 */
class DateFormFieldButton : public FormFieldButton
{
public:
    DateFormFieldButton(SwEditWin* pEditWin, sw::mark::DateFieldmark& rFieldMark,
                        SvNumberFormatter* pNumberFormatter);
    virtual ~DateFormFieldButton() override;

    virtual void InitPopup() override;

private:
    SvNumberFormatter* m_pNumberFormatter;
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
