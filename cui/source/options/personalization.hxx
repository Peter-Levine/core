/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_CUI_SOURCE_OPTIONS_PERSONALIZATION_HXX
#define INCLUDED_CUI_SOURCE_OPTIONS_PERSONALIZATION_HXX

#include <sfx2/tabdlg.hxx>
#include <vector>

#define MAX_DEFAULT_PERSONAS 6 // Maximum number of default personas

class SvxPersonalizationTabPage : public SfxTabPage
{
    using SfxTabPage::DeactivatePage;

private:
    std::unique_ptr<weld::RadioButton> m_xNoPersona; ///< Just the default look, without any bitmap
    std::unique_ptr<weld::RadioButton> m_xDefaultPersona; ///< Use the built-in bitmap
    std::unique_ptr<weld::Button> m_vDefaultPersonaImages
        [MAX_DEFAULT_PERSONAS]; ///< Buttons to show the default persona images
    OUString m_aPersonaSettings; ///< Header and footer images + color to be set in the settings.

    std::vector<OUString> m_vDefaultPersonaSettings;

public:
    SvxPersonalizationTabPage(TabPageParent pParent, const SfxItemSet& rSet);
    virtual ~SvxPersonalizationTabPage() override;

    static VclPtr<SfxTabPage> Create(TabPageParent pParent, const SfxItemSet* rSet);

    /// Apply the settings ([OK] button).
    virtual bool FillItemSet(SfxItemSet* rSet) override;

    /// Reset to default settings ([Revert] button).
    virtual void Reset(const SfxItemSet* rSet) override;

    /*
     * Loads the default personas from the shared personas directory
     * which resides in the shared gallery.
     * There needs to be a separate subdirectory for each default persona,
     * which includes the preview, header, and footer images.
     * And there needs to be a personas_list.txt file in the personas directory
     * which keeps the index/info of the default personas, one persona per line.
     * A line should look like this:
     * persona_slug;Persona Name;subdir/preview.jpg;subdir/header.jpg;subdir/footer.jpg;#textcolor
     * (It is recommended to keep the subdir name the same as the slug)
     * Example line:
     *  abstract;Abstract;abstract/preview.jpg;abstract/Header2.jpg;abstract/Footer2.jpg;#ffffff
     */
    void LoadDefaultImages();

private:
    /// Handle the default Persona selection
    DECL_LINK(DefaultPersona, weld::Button&, void);
};

#endif // INCLUDED_CUI_SOURCE_OPTIONS_PERSONALIZATION_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
