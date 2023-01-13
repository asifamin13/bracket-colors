/* -----------------------------------------------------------------------------

    Asif Amin
    asifamin@utexas.edu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301, USA.

----------------------------------------------------------------------------- */


/* --------------------------------- INCLUDES ------------------------------- */
#include <array>
#include <set>
#include <map>

#define G_LOG_USE_STRUCTURED
#define G_LOG_DOMAIN "bracket-colors"
#include <glib.h>

#include <geanyplugin.h>

#include "BracketMap.h"
#include "utils.h"

#define BC_NUM_COLORS 3
#define BC_NO_ARG 0
#define BC_STOP_ACTION TRUE
#define BC_CONTINUE_ACTION FALSE

#define SSM(s, m, w, l) scintilla_send_message(s, m, w, l)


/* --------------------------------- CONSTANTS ------------------------------ */

    typedef std::array<std::string, BC_NUM_COLORS> BracketColorArray;

    static const BracketColorArray sDarkBackgroundColors {
        { "#FF00FF", "#FFFF00", "#00FFFF" }
    };

    static const BracketColorArray sLightBackgroundColors {
        { "#008000", "#000080", "#800000"}
    };

    // styles that indicate comment, string, docstring, etc.
    // discovered from trial and error, better way to get this?
    static const std::set<int> sIgnoreStyles { 1, 2, 3, 4, 6, 7, 9 };

    // start index of indicators our plugin will use
    static const unsigned sIndicatorIndex = INDICATOR_IME - BC_NUM_COLORS;

/* ----------------------------------- TYPES -------------------------------- */

    enum BracketType {
        PAREN = 0,
        BRACE,
        BRACKET,
        ANGLE,
        COUNT
    };

    struct BracketColorsData {

        /*
         * Associated with every document
         */

        GeanyDocument *doc;

        guint32 backgroundColor;
        BracketColorArray bracketColors;

        gboolean init;

        guint computeTimeoutID, computeInterval;
        guint drawTimeoutID;

        gboolean updateUI;
        std::set<BracketMap::Index> recomputeIndicies, redrawIndicies;

        gboolean bracketColorsEnable[BracketType::COUNT];
        BracketMap bracketMaps[BracketType::COUNT];

        BracketColorsData() :
            doc(NULL),
            updateUI(FALSE),
            computeTimeoutID(0),
            drawTimeoutID(0),
            computeInterval(500),
            bracketColors(sLightBackgroundColors),
            init(FALSE)
        {
            for (int i = 0; i < BracketType::COUNT; i++) {
                bracketColorsEnable[i] = TRUE;
            }
            // color matching angle brackets seems to cause
            // more confusion than its worth
            bracketColorsEnable[BracketType::ANGLE] = FALSE;
        }

        ~BracketColorsData() {}

        void RemoveFromQueues(BracketMap::Index index) {
            {
                auto it = recomputeIndicies.find(index);
                if (it != recomputeIndicies.end()) {
                    recomputeIndicies.erase(it);
                }
            }
            {
                auto it = redrawIndicies.find(index);
                if (it != redrawIndicies.end()) {
                    redrawIndicies.erase(it);
                }
            }
        }
    };

/* ---------------------------------- EXTERNS ------------------------------- */

    GeanyPlugin	*geany_plugin;
    GeanyData *geany_data;

/* ---------------------------------- GLOBALS ------------------------------- */

    /*
     * TODO: figure out how to remove this.
     * Troubles with custom GObject when
     * loading and unloading plugin.
     */
    static std::map<uintptr_t, BracketColorsData *> sAllBracketColorsData;

/* --------------------------------- PROTOTYPES ----------------------------- */

/* ------------------------------ IMPLEMENTATION ---------------------------- */


// -----------------------------------------------------------------------------
    static gboolean isCurrDocument(
        BracketColorsData *data
    )
/*
    check if this document is currently opened
----------------------------------------------------------------------------- */
{
    GtkNotebook *notebook = GTK_NOTEBOOK(geany_data->main_widgets->notebook);
    gint currPage = gtk_notebook_get_current_page(notebook);
    GeanyDocument *currDoc = document_get_from_page(currPage);

    if (currDoc == data->doc) {
        return TRUE;
    }

    return FALSE;
}


// -----------------------------------------------------------------------------
    static void bracket_colors_data_purge_all()
/*

----------------------------------------------------------------------------- */
{
    for (auto it : sAllBracketColorsData) {
        g_debug(
            "%s: Purging BracketColorsData for key '%d'",
            __FUNCTION__, it.first
        );

        delete it.second;
    }

    sAllBracketColorsData.clear();
}



// -----------------------------------------------------------------------------
    static BracketColorsData* bracket_colors_data_new(GeanyDocument *doc)
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *newBCD = new BracketColorsData();

    uintptr_t key = reinterpret_cast<uintptr_t>(doc);

    auto it = sAllBracketColorsData.find(key);
    if (it != sAllBracketColorsData.end()) {
        delete it->second;
        it->second = newBCD;
    }
    else {
        sAllBracketColorsData.insert(
            std::make_pair(key, newBCD)
        );
    }

    return newBCD;
}


// -----------------------------------------------------------------------------
    static gboolean is_bracket_type(
        gchar ch,
        BracketType type
    )
/*
    check if char is bracket type
----------------------------------------------------------------------------- */
{
    static const std::set<gchar> sAllBrackets {
        '(', ')',
        '[', ']',
        '{', '}',
        '<', '>',
    };

    switch (type) {
        case (BracketType::PAREN): {
            if (ch == '(' or ch == ')') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACE): {
            if (ch == '[' or ch == ']') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACKET): {
            if (ch == '{' or ch == '}') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::ANGLE): {
            if (ch == '<' or ch == '>') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::COUNT): {
            return sAllBrackets.find(ch) != sAllBrackets.end() ? TRUE : FALSE;
        }
        default:
            return FALSE;
    }

}



// -----------------------------------------------------------------------------
    static gboolean is_open_bracket(
        gchar ch,
        BracketType type
    )
/*
    check if char is open bracket type
----------------------------------------------------------------------------- */
{
    static const std::set<gchar> sAllOpenBrackets {
        '(',
        '[',
        '{',
        '<',
    };

    switch (type) {
        case (BracketType::PAREN): {
            if (ch == '(') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACE): {
            if (ch == '[') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::BRACKET): {
            if (ch == '{') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::ANGLE): {
            if (ch == '<') {
                return TRUE;
            }
            return FALSE;
        }
        case(BracketType::COUNT): {
            return sAllOpenBrackets.find(ch) != sAllOpenBrackets.end() ? TRUE : FALSE;
        }
        default:
            return FALSE;
    }

}


// -----------------------------------------------------------------------------
    static gint compute_bracket_at(
        ScintillaObject *sci,
        BracketMap &bracketMap,
        gint position,
        bool updateInvalidMapping = true
    )
/*
    compute bracket at position
----------------------------------------------------------------------------- */
{
    int matchedBrace = SSM(sci, SCI_BRACEMATCH, position, BC_NO_ARG);
    int braceIdentity = position;

    if (matchedBrace != -1) {

        int length = matchedBrace - position;

        // g_debug(
        //     "%s: bracket at %d matched at %d",
        //     __FUNCTION__, position, matchedBrace
        // );

        if (length > 0) {
            // matched from start brace
            bracketMap.Update(position, length);
        }
        else {
            // matched from end brace
            length = -length;
            braceIdentity = position - length;
            bracketMap.Update(braceIdentity, length);
        }
    }
    else {
        // invalid mapping
        // g_debug("%s: bracket at %d invalid", __FUNCTION__, position);

        if (is_open_bracket(char_at(sci, position), BracketType::COUNT)) {
            if (updateInvalidMapping) {
                bracketMap.Update(position, BracketMap::UNDEFINED);
            }
        }
        else {
            // unknown start brace
            braceIdentity = -1;
        }
    }

    return braceIdentity;
}


// -----------------------------------------------------------------------------
    static gboolean isIgnoreStyle(
        ScintillaObject *sci,
        gint position
    )
/*
    check if position is part of non source section
----------------------------------------------------------------------------- */
{
    int style = SSM(sci, SCI_GETSTYLEAT, position, BC_NO_ARG);
    if (sIgnoreStyles.find(style) != sIgnoreStyles.end()) {
        return TRUE;
    }
    return FALSE;
}



// -----------------------------------------------------------------------------
    static void find_all_brackets(
        BracketColorsData &data
    )
/*
    brute force search for brackets
----------------------------------------------------------------------------- */
{
    ScintillaObject *sci = data.doc->editor->sci;

    gint length = sci_get_length(sci);
    for (int i = 0; i < length; i++) {
        gchar ch = char_at(sci, i);
        if (is_bracket_type(ch, BracketType::COUNT)) {
            for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                if (data.bracketColorsEnable[bracketType] == TRUE) {
                    if (is_bracket_type(ch, static_cast<BracketType>(bracketType))) {
                        data.recomputeIndicies.insert(i);
                        data.updateUI = TRUE;
                        break;
                    }
                }
            }
        }
    }
}



// -----------------------------------------------------------------------------
    static void remove_bc_indicators(
        ScintillaObject *sci
    )
/*
    remove indicators associated with this plugin
----------------------------------------------------------------------------- */
{
    gint length = sci_get_length(sci);
    for (int i = 0; i < BC_NUM_COLORS; i++) {
        SSM(sci, SCI_SETINDICATORCURRENT, sIndicatorIndex + i, BC_NO_ARG);
        SSM(sci, SCI_INDICATORCLEARRANGE, 0, length);
    }
}



// -----------------------------------------------------------------------------
    static void assign_bc_indicators(
        ScintillaObject *sci,
        const BracketColorsData &data
    )
/*
    assign all indicators
----------------------------------------------------------------------------- */
{
    for (int i = 0; i < BracketType::COUNT; i++) {

        const BracketMap &bracketMap = data.bracketMaps[i];

        for(
            auto it = bracketMap.mBracketMap.cbegin();
            it != bracketMap.mBracketMap.end();
            it++
        )
        {
            auto position = it->first;
            auto bracket = it->second;

            if (BracketMap::GetLength(bracket) != BracketMap::UNDEFINED) {

                std::array<int, 2> positions {
                    { position, position + BracketMap::GetLength(bracket) }
                };

                for (auto position : positions) {
                    SSM(
                        sci,
                        SCI_SETINDICATORCURRENT,
                        sIndicatorIndex + \
                            ((BracketMap::GetOrder(bracket) + i) % BC_NUM_COLORS),
                        BC_NO_ARG
                    );
                    SSM(sci, SCI_INDICATORFILLRANGE, position, 1);
                }
            }
        }
    }
}



// -----------------------------------------------------------------------------
    static void set_bc_indicators(
        ScintillaObject *sci,
        const BracketColorsData &data
    )
/*
    assign all indicators, check if already correct
----------------------------------------------------------------------------- */
{
    for (int i = 0; i < BracketType::COUNT; i++) {

        const BracketMap &bracketMap = data.bracketMaps[i];

        for(
            auto it = bracketMap.mBracketMap.cbegin();
            it != bracketMap.mBracketMap.end();
            it++
        )
        {
            auto position = it->first;
            auto bracket = it->second;

            if (BracketMap::GetLength(bracket) != BracketMap::UNDEFINED) {

                std::array<int, 2> positions {
                    { position, position + BracketMap::GetLength(bracket) }
                };

                for (auto position : positions) {

                    unsigned correctIndicatorIndex = sIndicatorIndex + \
                        ((BracketMap::GetOrder(bracket) + i) % BC_NUM_COLORS);

                    int curr = SSM(sci, SCI_INDICATORVALUEAT, correctIndicatorIndex, position);
                    if (not curr) {
                        // g_debug(
                        //     "%s: Setting indicator %d at %d",
                        //     __FUNCTION__,
                        //     correctIndicatorIndex, position
                        // );
                        SSM(
                            sci,
                            SCI_SETINDICATORCURRENT,
                            correctIndicatorIndex,
                            BC_NO_ARG
                        );
                        SSM(sci, SCI_INDICATORFILLRANGE, position, 1);
                    }

                    // make sure there arent any other indicators at position
                    for (
                        int indicatorIndex = sIndicatorIndex;
                        indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
                        indicatorIndex++
                    )
                    {
                        if (indicatorIndex == correctIndicatorIndex) {
                            continue;
                        }

                        int hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, position);
                        if (hasIndicator) {
                            SSM(
                                sci,
                                SCI_SETINDICATORCURRENT,
                                indicatorIndex,
                                BC_NO_ARG
                            );
                            SSM(sci, SCI_INDICATORCLEARRANGE, position, 1);
                        }
                    }
                }
            }
        }
    }
}




// -----------------------------------------------------------------------------
    static void set_bc_indicators_at(
        ScintillaObject *sci,
        const BracketColorsData &data,
        gint index
    )
/*
    assign indicator at position, check if already correct
----------------------------------------------------------------------------- */
{
    for (int i = 0; i < BracketType::COUNT; i++) {

        const BracketMap &bracketMap = data.bracketMaps[i];

        auto it = bracketMap.mBracketMap.find(index);
        if (it == bracketMap.mBracketMap.end()) {
            continue;
        }

        auto bracket = it->second;

        if (BracketMap::GetLength(bracket) != BracketMap::UNDEFINED) {

            std::array<int, 2> positions {
                { index, index + BracketMap::GetLength(bracket) }
            };

            for (auto position : positions) {

                unsigned correctIndicatorIndex = sIndicatorIndex + \
                    ((BracketMap::GetOrder(bracket) + i) % BC_NUM_COLORS);

                int curr = SSM(sci, SCI_INDICATORVALUEAT, correctIndicatorIndex, position);
                if (not curr) {
                    // g_debug(
                    //     "%s: Setting indicator %d at %d",
                    //     __FUNCTION__,
                    //     correctIndicatorIndex, position
                    // );
                    SSM(
                        sci,
                        SCI_SETINDICATORCURRENT,
                        correctIndicatorIndex,
                        BC_NO_ARG
                    );
                    SSM(sci, SCI_INDICATORFILLRANGE, position, 1);
                }

                // make sure there arent any other indicators at position
                for (
                    int indicatorIndex = sIndicatorIndex;
                    indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
                    indicatorIndex++
                )
                {
                    if (indicatorIndex == correctIndicatorIndex) {
                        continue;
                    }

                    int hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, position);
                    if (hasIndicator) {
                        SSM(
                            sci,
                            SCI_SETINDICATORCURRENT,
                            indicatorIndex,
                            BC_NO_ARG
                        );
                        SSM(sci, SCI_INDICATORCLEARRANGE, position, 1);
                    }
                }
            }
        }
    }
}




// -----------------------------------------------------------------------------
    static void clear_bc_indicators(
        ScintillaObject *sci,
        gint position, gint length
    )
/*
    clear bracket indicators in range
----------------------------------------------------------------------------- */
{
    for (int i = position; i < position + length; i++) {
        for (
            int indicatorIndex = sIndicatorIndex;
            indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
            indicatorIndex++
        )
        {
            int hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, i);
            // g_debug("%s: Indicator %d: %d", __FUNCTION__, indicatorIndex, hasIndicator);
            if (hasIndicator) {
                // g_debug("%s: Clearing bracket at %d", __FUNCTION__, i);
                SSM(
                    sci,
                    SCI_SETINDICATORCURRENT,
                    indicatorIndex,
                    BC_NO_ARG
                );
                SSM(sci, SCI_INDICATORCLEARRANGE, i, 1);
            }
        }
    }
}



// -----------------------------------------------------------------------------
    static gboolean move_brackets(
        ScintillaObject *sci,
        BracketColorsData &bracketColorsData,
        gint position, gint length,
        BracketType type
    )
/*
    handle when text is added
----------------------------------------------------------------------------- */
{
    if (bracketColorsData.bracketColorsEnable[type] == FALSE) {
        return FALSE;
    }

    BracketMap &bracketMap = bracketColorsData.bracketMaps[type];

    std::set<BracketMap::Index> indiciesToAdjust, indiciesToRecompute;

    /*
     * Look through existing bracket map and check if addition of characters
     * will require adjustment
     */

    for (const auto &it : bracketMap.mBracketMap) {
        const auto &bracket = it.second;
        int endPos = it.first + BracketMap::GetLength(bracket);
        if (it.first >= position) {
            indiciesToAdjust.insert(it.first);
        }
        else if (
            endPos >= position or
            BracketMap::GetLength(bracket) == BracketMap::UNDEFINED
        ) {
            indiciesToRecompute.insert(it.first);
        }
    }

    gboolean madeChange = FALSE;

    // Check if the new characters that are added were brackets
    for (int i = position; i < position + length; i++) {
        gchar newChar = char_at(sci, i);
        if (is_bracket_type(newChar, type)) {
            // g_debug("%s: Handling new bracket character", __FUNCTION__);
            madeChange = TRUE;
            bracketColorsData.recomputeIndicies.insert(i);
        }
    }

    g_debug(
        "%s: Need to adjust %d brackets, recompute %d brackets",
        __FUNCTION__, indiciesToAdjust.size(), indiciesToRecompute.size()
    );

    if (not indiciesToAdjust.size() and not indiciesToRecompute.size()) {
        //g_debug("%s: Nothing to do", __FUNCTION__);
        return madeChange;
    }

    for (const auto &it : indiciesToRecompute) {
        //g_debug("%s: Recomputing bracket at %d", __FUNCTION__, it);
        bracketColorsData.recomputeIndicies.insert(it);
    }

    for (const auto &it : indiciesToAdjust) {
        //g_debug("%s: Moved brace at %d to %d", __FUNCTION__, it, it + length);
        bracketMap.mBracketMap.insert(
            std::make_pair(
                it + length,
                bracketMap.mBracketMap.at(it)
            )
        );

        bracketMap.mBracketMap.erase(it);
        bracketColorsData.RemoveFromQueues(it);
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static gboolean remove_brackets(
        ScintillaObject *sci,
        BracketColorsData &bracketColorsData,
        gint position, gint length,
        BracketType type
    )
/*
    handle when text is removed
----------------------------------------------------------------------------- */
{
    if (bracketColorsData.bracketColorsEnable[type] == FALSE) {
        return FALSE;
    }

    BracketMap &bracketMap = bracketColorsData.bracketMaps[type];

    std::set<BracketMap::Index> indiciesToRemove, indiciesToRecompute;

    for (const auto &it : bracketMap.mBracketMap) {
        const auto &bracket = it.second;
        int endPos = it.first + BracketMap::GetLength(bracket);
        // start bracket was deleted
        if ( (it.first >= position) and (it.first < position + length) ) {
            indiciesToRemove.insert(it.first);
        }
        // end bracket removed or space removed
        else if (it.first >= position or endPos >= position) {
            indiciesToRecompute.insert(it.first);
        }
    }

    g_debug(
        "%s: Need to remove %d brackets, adjust %d brackets",
        __FUNCTION__, indiciesToRemove.size(), indiciesToRecompute.size()
    );

    if (
        not indiciesToRemove.size() and
        not indiciesToRecompute.size()
    ) {
        g_debug("%s: Nothing to do", __FUNCTION__);
        return FALSE;
    }

    for (const auto &it : indiciesToRemove) {
        g_debug("%s: Removing brace at %d", __FUNCTION__, it);
        bracketMap.mBracketMap.erase(it);
        bracketColorsData.RemoveFromQueues(it);
    }

    for (const auto &it : indiciesToRecompute) {
        const auto &bracket = bracketMap.mBracketMap.at(it);
        int endPos = it + BracketMap::GetLength(bracket);

        // first bracket was moved backwards
        if (it >= position) {
            //g_debug("%s: Moved brace at %d to %d", __FUNCTION__, it, it - length);
            bracketMap.mBracketMap.insert(
                std::make_pair(
                    it - length,
                    bracketMap.mBracketMap.at(it)
                )
            );
            bracketMap.mBracketMap.erase(it);
            bracketColorsData.RemoveFromQueues(it);
        }
        // last bracket was moved
        else {
            //g_debug("%s: Recomputing bracket at %d", __FUNCTION__, it);
            bracketColorsData.recomputeIndicies.insert(it);
        }
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static gboolean snoop_at_key_press(
        GtkWidget *widget,
        GdkEventKey *event,
        gpointer user_data
    )
/*
    for debugging to snoop styles
----------------------------------------------------------------------------- */
{
    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);
    ScintillaObject *sci = data->doc->editor->sci;
    g_return_val_if_fail(sci, BC_CONTINUE_ACTION);

    int pos = sci_get_current_position(sci);
    int style = SSM(sci, SCI_GETSTYLEAT, pos, BC_NO_ARG);
    gchar newChar = sci_get_char_at(sci, pos);

    switch(event->keyval) {
        case(GDK_Shift_R): {
            ScintillaObject *sci = data->doc->editor->sci;
            g_debug(
                "%s: caught right shift at %d, style: %d, char: '%c'",
                __FUNCTION__, pos, style, newChar
            );

            for (
                int indicatorIndex = sIndicatorIndex;
                indicatorIndex < sIndicatorIndex + BC_NUM_COLORS;
                indicatorIndex++
            ) {
                int hasIndicator = SSM(sci, SCI_INDICATORVALUEAT, indicatorIndex, pos);
                g_debug("%s: Indicator %d: %d", __FUNCTION__, indicatorIndex, hasIndicator);
            }

            break;
        }
    }

    return BC_CONTINUE_ACTION;
}



// -----------------------------------------------------------------------------
    static void render_document(
        ScintillaObject *sci,
        BracketColorsData *data
    )
/*

----------------------------------------------------------------------------- */
{
    if (data->updateUI) {
        g_debug(
            "%s: Need to update %d indicies",
            __FUNCTION__, data->redrawIndicies.size()
        );

        for (
            auto position = data->redrawIndicies.begin();
            position != data->redrawIndicies.end();
            position++
        )
        {
            // if this bracket has been reinserted into the work queue, ignore
            if (data->recomputeIndicies.find(*position) == data->recomputeIndicies.end()) {
                set_bc_indicators_at(sci, *data, *position);
            }
        }

        data->redrawIndicies.clear();
        data->updateUI = FALSE;
    }
}



// -----------------------------------------------------------------------------
    static void compute_document(
        ScintillaObject *sci,
        BracketColorsData *data,
        bool doRender = true
    )
/*

----------------------------------------------------------------------------- */
{
    if (data->recomputeIndicies.size()) {

        g_debug(
            "%s: Recomputing %d brackets",
            __FUNCTION__, data->recomputeIndicies.size()
        );

        for (const auto &position : data->recomputeIndicies) {

            for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {

                BracketMap &bracketMap = data->bracketMaps[bracketType];

                if (
                    is_bracket_type(
                        char_at(sci, position),
                        static_cast<BracketType>(bracketType)
                    )
                ) {
                    // check if in a comment
                    if (isIgnoreStyle(sci, position)) {
                        clear_bc_indicators(sci, position, 1);
                        bracketMap.mBracketMap.erase(position);
                    }
                    else {
                        gint brace = compute_bracket_at(sci, bracketMap, position);
                        if (brace >= 0) {
                            data->redrawIndicies.insert(brace);
                        }
                    }
                }

                bracketMap.ComputeOrder();
            }
        }

        data->recomputeIndicies.clear();
        data->updateUI = TRUE;
    }

    if (doRender) {
        render_document(sci, data);
    }
}



// -----------------------------------------------------------------------------
    static void on_sci_notify(
        ScintillaObject *sci,
        gint scn,
        SCNotification *nt,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);

    switch(nt->nmhdr.code) {

        case(SCN_UPDATEUI): {

            if (nt->updated & SC_UPDATE_CONTENT) {

                if (isCurrDocument(data)) {
                    render_document(sci, data);
                }
            }

            break;
        }

        case(SCN_MODIFIED):
        {
            if (nt->modificationType & SC_MOD_INSERTTEXT) {
                g_debug(
                    "%s: Text added. Position: %d, Length: %d",
                    __FUNCTION__, nt->position, nt->length
                );

                // if we insert into position that had bracket
                clear_bc_indicators(sci, nt->position, nt->length);

                /*
                 * Check to adjust current bracket positions
                 */

                for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                    if (
                        move_brackets(
                            sci,
                            *data,
                            nt->position, nt->length,
                            static_cast<BracketType>(bracketType)
                        )
                    ) {
                        data->updateUI = TRUE;
                    }
                }
            }

            if (nt->modificationType & SC_MOD_DELETETEXT) {
                g_debug(
                    "%s: Text removed. Position: %d, Length: %d",
                    __FUNCTION__, nt->position, nt->length
                );

                for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                    if (
                        remove_brackets(
                            sci,
                            *data,
                            nt->position, nt->length,
                            static_cast<BracketType>(bracketType)
                        )
                    ) {
                        data->updateUI = TRUE;
                    }
                }
            }

            if (nt->modificationType & SC_MOD_CHANGESTYLE) {
                g_debug(
                    "%s: Style change. Position: %d, Length: %d",
                    __FUNCTION__, nt->position, nt->length
                );

                if (data->init == TRUE) {
                    for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {
                        if (data->bracketColorsEnable[bracketType] == FALSE) {
                            continue;
                        }
                        for (int i = nt->position; i < nt->position + nt->length; i++) {
                            gchar currChar = char_at(sci, i);
                            if (is_bracket_type(currChar, static_cast<BracketType>(bracketType))) {
                                //g_debug("%s: Handling style change for bracket at %d", __FUNCTION__, i);
                                data->recomputeIndicies.insert(i);
                            }
                        }
                    }
                }
            }

            break;
        }
    }
}



// -----------------------------------------------------------------------------
    static gboolean render_brackets_timeout(
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);
    ScintillaObject *sci = data->doc->editor->sci;

    if (not isCurrDocument(data)) {
        data->drawTimeoutID = 0;
        return FALSE;
    }

    /*
     * check if background color changed
     */

    guint32 currBGColor = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, BC_NO_ARG);
    if (currBGColor != data->backgroundColor) {
        g_debug("%s: background color changed: %#04x", __FUNCTION__, currBGColor);

        gboolean currDark = utils_is_dark(currBGColor);
        gboolean wasDark = utils_is_dark(data->backgroundColor);

        if (currDark != wasDark) {
            g_debug("%s: Need to change colors scheme!", __FUNCTION__);
            data->bracketColors = currDark ? sDarkBackgroundColors : sLightBackgroundColors;
            for (int i = 0; i < data->bracketColors.size(); i++) {
                gint index = sIndicatorIndex + i;
                const std::string &spec = data->bracketColors.at(i);
                gint color = utils_parse_color_to_bgr(spec.c_str());
                SSM(sci, SCI_INDICSETSTYLE, index, INDIC_TEXTFORE);
                SSM(sci, SCI_INDICSETFORE, index, color);
            }

        }

        data->backgroundColor = currBGColor;
    }

    if (data->updateUI) {
        g_debug(
            "%s: have to redraw %d indicies",
            __FUNCTION__, data->redrawIndicies.size()
        );
        render_document(sci, data);
    }


    return TRUE;
}



// -----------------------------------------------------------------------------
    static gboolean recompute_brackets_timeout(
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    static const unsigned sIterationLimit = 50;

    BracketColorsData *data = reinterpret_cast<BracketColorsData *>(user_data);
    ScintillaObject *sci = data->doc->editor->sci;

    if (not isCurrDocument(data)) {
        data->computeTimeoutID = 0;
        return FALSE;
    }

    if (data->init == FALSE) {
        // for (int i = 0; i < BracketType::COUNT; i++) {
        //     find_all_brackets(sci, *data, static_cast<BracketType>(i));
        // }
        find_all_brackets(*data);
        data->init = TRUE;
    }

    if (data->recomputeIndicies.size()) {

        g_debug(
            "%s: have to recompute %d indicies",
            __FUNCTION__, data->recomputeIndicies.size()
        );

        unsigned numIterations = 0;
        for (
            auto position = data->recomputeIndicies.begin();
            position != data->recomputeIndicies.end();
            numIterations++
        )
        {
            for (int bracketType = 0; bracketType < BracketType::COUNT; bracketType++) {

                BracketMap &bracketMap = data->bracketMaps[bracketType];

                if (
                    is_bracket_type(
                        char_at(sci, *position),
                        static_cast<BracketType>(bracketType)
                    )
                ) {
                    // check if in a comment
                    if (isIgnoreStyle(sci, *position)) {
                        bracketMap.mBracketMap.erase(*position);
                        clear_bc_indicators(sci, *position, 1);
                    }
                    else {
                        gint brace = compute_bracket_at(sci, bracketMap, *position);
                        if (brace >= 0) {
                            data->redrawIndicies.insert(brace);
                        }
                        data->updateUI = TRUE;
                    }
                }

                bracketMap.ComputeOrder();
            }

            position = data->recomputeIndicies.erase(position);
            if (numIterations >= sIterationLimit) {
                break;
            }
        }
    }

    return TRUE;
}



// -----------------------------------------------------------------------------
    static void on_document_close(
        GObject *obj,
        GeanyDocument *doc,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    g_return_if_fail(DOC_VALID(doc));
    g_debug("%s: closing document '%d'", __FUNCTION__, doc->id);

    auto it = sAllBracketColorsData.find(reinterpret_cast<uintptr_t>(doc));
    if (it != sAllBracketColorsData.end()) {
        BracketColorsData *data = it->second;
        if (data->computeTimeoutID > 0) {
            g_source_remove(data->computeTimeoutID);
            data->computeTimeoutID = 0;
        }

        if (data->drawTimeoutID > 0) {
            g_source_remove(data->drawTimeoutID);
            data->drawTimeoutID = 0;
        }

        delete data;
        sAllBracketColorsData.erase(reinterpret_cast<uintptr_t>(doc));
    }

    ScintillaObject *sci = doc->editor->sci;
    remove_bc_indicators(sci);
}



// -----------------------------------------------------------------------------
    static void on_notebook_page_switch(
        GtkNotebook *self,
        GtkWidget *page,
        guint page_num,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    g_debug("%s: handling page switch to %d", __FUNCTION__, page_num);

    GeanyDocument *doc = document_get_from_page(page_num);
    if (doc == NULL) {
        g_debug("%s: Null doc", __FUNCTION__);
        return;
    }

    auto it = sAllBracketColorsData.find(reinterpret_cast<uintptr_t>(doc));
    if (it == sAllBracketColorsData.end()) {
        g_debug("%s: No data", __FUNCTION__);
        return;
    }

    BracketColorsData *data = it->second;
    g_debug("%s: got page switch to '%d'", __FUNCTION__, data->doc->id);

    data->computeTimeoutID = g_timeout_add_full(
        G_PRIORITY_LOW,
        20,
        recompute_brackets_timeout,
        data,
        NULL
    );

    data->drawTimeoutID = g_timeout_add_full(
        G_PRIORITY_LOW,
        100,
        render_brackets_timeout,
        data,
        NULL
    );
}



// -----------------------------------------------------------------------------
    static void on_document_open(
        GObject *obj,
        GeanyDocument *doc,
        gpointer user_data
    )
/*

----------------------------------------------------------------------------- */
{
    g_return_if_fail(DOC_VALID(doc));
    g_debug("%s: opening document '%d'", __FUNCTION__, doc->id);

    BracketColorsData *data = bracket_colors_data_new(doc);
    ScintillaObject *sci = doc->editor->sci;
    data->doc = doc;

    plugin_signal_connect(
        geany_plugin,
        G_OBJECT(sci), "sci-notify",
        FALSE,
        G_CALLBACK(on_sci_notify), data
    );

    /*
     * Setup our bracket indicators
     */

    data->backgroundColor = SSM(sci, SCI_STYLEGETBACK, STYLE_DEFAULT, BC_NO_ARG);
    if (utils_is_dark(data->backgroundColor)) {
        data->bracketColors = sDarkBackgroundColors;
    }

    for (int i = 0; i < data->bracketColors.size(); i++) {
        gint index = sIndicatorIndex + i;
        const std::string &spec = data->bracketColors.at(i);
        gint color = utils_parse_color_to_bgr(spec.c_str());
        SSM(sci, SCI_INDICSETSTYLE, index, INDIC_TEXTFORE);
        SSM(sci, SCI_INDICSETFORE, index, color);
    }

    /*
     * timeout to recompute brackets
     */

    data->computeTimeoutID = g_timeout_add_full(
        G_PRIORITY_LOW,
        20,
        recompute_brackets_timeout,
        data,
        NULL
    );

    data->drawTimeoutID = g_timeout_add_full(
        G_PRIORITY_LOW,
        100,
        render_brackets_timeout,
        data,
        NULL
    );

}



// -----------------------------------------------------------------------------
    static gboolean plugin_bracketcolors_init (
        GeanyPlugin *plugin,
        gpointer pdata
    )
/*

----------------------------------------------------------------------------- */
{
    g_debug("%s: seting up plugin", __FUNCTION__);
    g_log_set_writer_func(g_log_writer_default, NULL, NULL);

    geany_plugin = plugin;
    geany_data = plugin->geany_data;

    guint i = 0;
    foreach_document(i)
    {
        on_document_open(NULL, documents[i], NULL);
    }

    plugin_signal_connect(
        plugin,
        G_OBJECT(geany_data->main_widgets->notebook), "switch-page",
        TRUE,
        G_CALLBACK(on_notebook_page_switch), NULL
    );

    plugin_signal_connect(
        plugin,
        NULL, "document-close",
        FALSE,
        G_CALLBACK(on_document_close), NULL
    );

    return TRUE;
}



// -----------------------------------------------------------------------------
    static void plugin_bracketcolors_cleanup (
        GeanyPlugin *plugin,
        gpointer pdata
    )
/*

----------------------------------------------------------------------------- */
{
    g_debug("%s: leaving bracket colors", __FUNCTION__);

    guint i = 0;
    foreach_document(i)
    {
        on_document_close(NULL, documents[i], NULL);
    }

    bracket_colors_data_purge_all();
}



// -----------------------------------------------------------------------------
    static PluginCallback plugin_bracketcolors_callbacks[] =
/*

----------------------------------------------------------------------------- */
{
    { "document-open",  (GCallback) &on_document_open, FALSE, NULL },
    { "document-new",   (GCallback) &on_document_open, FALSE, NULL },
    { NULL, NULL, FALSE, NULL }
};



// -----------------------------------------------------------------------------
    extern "C" void geany_load_module(GeanyPlugin *plugin)
/*
    Load module
----------------------------------------------------------------------------- */
{
    g_debug("%s: loading module", __FUNCTION__);

    /* Set metadata */
    plugin->info->name          = "Bracket Colors";
    plugin->info->description   = "Color nested brackets, braces, parenthesis";
    plugin->info->version       = "0.1";
    plugin->info->author        = "Asif Amin";

    /* Set functions */
    plugin->funcs->init         = plugin_bracketcolors_init;
    plugin->funcs->cleanup      = plugin_bracketcolors_cleanup;
    plugin->funcs->callbacks    = plugin_bracketcolors_callbacks;

    /* Register! */
    GEANY_PLUGIN_REGISTER(plugin, 225);
}
