/////////////////////////////////////////////////////////////////////////////
// Name:        view_neume.cpp
// Author:      Andrew Tran, Juliette Regimbal
// Created:     2017
// Copyright (c) Author and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "view.h"

//----------------------------------------------------------------------------

#include <algorithm>
#include <assert.h>
#include <math.h>

//----------------------------------------------------------------------------

#include "devicecontext.h"
#include "doc.h"
#include "layer.h"
#include "layerelement.h"
#include "mrpt.h"
#include "nc.h"
#include "neume.h"
#include "note.h"
#include "smufl.h"
#include "staff.h"
#include "syllable.h"
#include "vrv.h"

namespace vrv {

void View::DrawSyllable(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Syllable *syllable = dynamic_cast<Syllable *>(element);
    assert(syllable);

    /******************************************************************/
    // Start the Beam graphic and draw the children

    dc->StartGraphic(element, "", element->GetUuid());

    /******************************************************************/
    // Draw the children

    DrawLayerChildren(dc, syllable, layer, staff, measure);

    dc->EndGraphic(element, this);
}

void View::DrawNc(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure,
    wchar_t fontNo, int xOffset, int yOffset)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Nc *nc = dynamic_cast<Nc *>(element);
    assert(nc);

    dc->StartGraphic(element, "", element->GetUuid());

    /******************************************************************/
    // Draw the children
    DrawLayerChildren(dc, nc, layer, staff, measure);

    // Intializing necessary variables
    Clef *clef = layer->GetClef(element);
    int staffSize = m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize);
    int staffLineNumber = staff->m_drawingLines;
    int clefLine = clef->GetLine();

    int noteY = element->GetDrawingY();
    int noteX = element->GetDrawingX();

    // Calculating proper y offset based on pname, clef, and staff
    int clefYPosition = noteY - (staffSize * (staffLineNumber - clefLine));
    int pitchOffset = 0;
    int octaveOffset = (nc->GetOct() - 3) * ((staffSize / 2) * 7);

    if (clef->GetShape() == CLEFSHAPE_C) {
        pitchOffset = (nc->GetPname() - 1) * (staffSize / 2);
    }
    else if (clef->GetShape() == CLEFSHAPE_F) {
        pitchOffset = (nc->GetPname() - 4) * (staffSize / 2);
    }

    int yValue = clefYPosition + pitchOffset + octaveOffset;

    DrawSmuflCode(dc, noteX + xOffset, yValue + yOffset, fontNo, staff->m_drawingStaffSize, false, true);

    dc->EndGraphic(element, this);
}

void View::DrawNeume(DeviceContext *dc, LayerElement *element, Layer *layer, Staff *staff, Measure *measure)
{
    assert(dc);
    assert(layer);
    assert(staff);
    assert(measure);

    Neume *neume = dynamic_cast<Neume *>(element);
    assert(neume);

    /******************************************************************/
    // Initialization

    // Obtaining list of NC to extract intm data
    // Format of *neumechildren, first object is <neume>, all objects after are <nc>
    const ListOfObjects neumeChildren = neume->GetList(neume)[0];
    if (neumeChildren.empty()) {
        return;
    }

    std::vector<Nc *> ncVector;
    std::vector<int> pitchDifferences;
    for (int i = 1; i < (int)neumeChildren.size(); i++) {
        Nc *nc = dynamic_cast<Nc *>(neumeChildren[i]);
        ncVector.push_back(nc);
        if (i >= 2) {
            // Calculate the pitch differences between each nc pair
            Nc *prev_nc = dynamic_cast<Nc *>(neumeChildren[i - 1]);
            int pitchDifference = nc->GetPname() - prev_nc->GetPname();
            pitchDifference += (nc->GetOct() - prev_nc->GetOct()) * 7;
            pitchDifferences.push_back(pitchDifference);
        }
    }

    // Obtaining the NeumeGroup enum name from the map
    NeumeGroup neumeName = neume->GetNeumeGroup();

    // If the shape cannot be found in the map, NeumeGroup::ERROR will be returned since its
    // value is 0 and std::map returns 0 by default for missing keys.
    if (neumeName == NEUME_ERROR) {
        // TODO: Error Handling for unfound neumes
        LogError("Neume Grouping not found");
    }

    /******************************************************************/
    // Start the Neume graphic and draw the children

    dc->StartGraphic(element, "", element->GetUuid());
    // If there is a defined way to draw the neume grouping, we should use that to draw the specialized SVG shape,
    // or else we draw each nc in the neume as a punctum.

    //// THIS DOESNT WORK YET BECAUSE GLYPHS NOT IN LEIPZIG
    // int glyphSize = m_doc->GetGlyphWidth(SMUFL_E990_chantPunctum, staff->m_drawingStaffSize, 0);
    int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
    int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);
    switch (neumeName) {
        case PES:
        case PES_SUBPUNCTIS: 
        {
            int xOffset = 0;
            if (pitchDifferences.at(0) > 1) {
                xOffset = -1;
            }
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure, SMUFL_E990_chantPunctum);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure, SMUFL_E990_chantPunctum,
                xOffset * noteWidth);

            if (neumeName == PES_SUBPUNCTIS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
            }
            break;
        }
        case PORRECTUS:
        case PORRECTUS_FLEXUS:
        case PORRECTUS_SUBPUNCTIS:
        {   
            wchar_t lineCode;
            wchar_t ligatureCode;
            switch (pitchDifferences.at(0)) {
                case -1:
                    lineCode = SMUFL_E9B4_chantEntryLineAsc2nd;
                    ligatureCode = SMUFL_E9B9_chantLigaturaDesc2nd;
                    break;
                case -2:
                    lineCode = SMUFL_E9B5_chantEntryLineAsc3rd;
                    ligatureCode = SMUFL_E9BA_chantLigaturaDesc3rd;
                    break;
                case -3:
                    lineCode = SMUFL_E9B6_chantEntryLineAsc4th;
                    ligatureCode = SMUFL_E9BB_chantLigaturaDesc4th;
                    break;
                case -4:
                    lineCode = SMUFL_E9B7_chantEntryLineAsc5th;
                    ligatureCode = SMUFL_E9BC_chantLigaturaDesc5th;
                    break;
                default:
                    // TODO: Make default draw all 3 punctums individually
                    lineCode = SMUFL_E9B4_chantEntryLineAsc2nd;
                    ligatureCode = SMUFL_E9B9_chantLigaturaDesc2nd;
                    break;
            }
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure, lineCode, 0,
                noteHeight * pitchDifferences.at(0));
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure, ligatureCode);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure);
            if (neumeName == PORRECTUS_FLEXUS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure);
            }
            else if (neumeName == PORRECTUS_SUBPUNCTIS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(4)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
            }
            break;
        }
        case CLIVIS: {
            wchar_t lineCode;
            switch (pitchDifferences.at(0)) {
                case -1: lineCode = SMUFL_E9BD_chantConnectingLineAsc2nd; break;
                case -2: lineCode = SMUFL_E9BE_chantConnectingLineAsc3rd; break;
                case -3: lineCode = SMUFL_E9BF_chantConnectingLineAsc4th; break;
                case -4: lineCode = SMUFL_E9C0_chantConnectingLineAsc5th; break;
                default:
                    // TODO: Draw each punctum separately
                    lineCode = SMUFL_E9BD_chantConnectingLineAsc2nd;
                    break;
            }
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure, SMUFL_E990_chantPunctum,
                noteHeight / 4, 0);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure, lineCode, 0, 0);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure);
            break;
        }
        case CLIMACUS:
        case CLIMACUS_RESUPINUS:
        {
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector[0]), layer, staff, measure);
            int lastInclinatumIndex = (neumeName == CLIMACUS_RESUPINUS) ? ncVector.size() - 1 : ncVector.size();
            for (int i = 1; i < lastInclinatumIndex; i++)
            {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(i)), layer, staff, measure,
                    SMUFL_E991_chantPunctumInclinatum);
            }
            
            if (neumeName == CLIMACUS_RESUPINUS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure);
            }
            break;
        }
        case SCANDICUS:
        case SCANDICUS_FLEXUS:
        case SCANDICUS_SUBPUNCTIS:
        {
            int xOffset = 0;
            wchar_t lineCode;
            switch (pitchDifferences.at(1)) {
                case 2: lineCode = SMUFL_E9BE_chantConnectingLineAsc3rd; break;
                case 3: lineCode = SMUFL_E9BF_chantConnectingLineAsc4th; break;
                case 4: lineCode = SMUFL_E9C0_chantConnectingLineAsc5th; break;
                default:
                    // TODO: Draw each punctum separately
                    lineCode = SMUFL_E9BD_chantConnectingLineAsc2nd;
                    break;
            }
            if (pitchDifferences.at(1) > 1) {
                xOffset = -1;
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure, lineCode,
                    -noteWidth / 10, -noteHeight * pitchDifferences.at(1) - 1);
            }
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure, SMUFL_E990_chantPunctum);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure, SMUFL_E990_chantPunctum);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure, SMUFL_E990_chantPunctum,
                xOffset * noteWidth);

            if (neumeName == SCANDICUS_FLEXUS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure);
            }
            else if (neumeName == SCANDICUS_SUBPUNCTIS) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(4)), layer, staff, measure, SMUFL_E991_chantPunctumInclinatum);
            }

            break;
        }
        case TORCULUS_RESUPINUS:
        {
            // For difference of 1 or 2 draw a ligature
            // For a greater difference render separate punctums with a connecting line
            // The first and last neume components will always be rendered as punctums.
            wchar_t lineCode;
            switch (pitchDifferences.at(1)) {
                case -1:
                    lineCode = SMUFL_E9B9_chantLigaturaDesc2nd;
                    break;
                case -2:
                    lineCode = SMUFL_E9BA_chantLigaturaDesc3rd;
                    break;
                case -3:
                    lineCode = SMUFL_E9BF_chantConnectingLineAsc4th;
                    break;
                default:
                    lineCode = SMUFL_E9B7_chantEntryLineAsc5th;
                    break;
            }
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure);

            if (lineCode == SMUFL_E9B9_chantLigaturaDesc2nd || lineCode == SMUFL_E9BA_chantLigaturaDesc3rd) {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure, lineCode, -noteHeight / 4, 0);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure, SMUFL_E990_chantPunctum, noteWidth, 0);
            }
            else {
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure, SMUFL_E990_chantPunctum, noteHeight/4, 0);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure, lineCode);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure, SMUFL_E990_chantPunctum);
                DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(3)), layer, staff, measure);
            }
            break;
        }
        case PRESSUS:
        {
            // Two punctums followed by a punctum inclinatum
            // No additional logic required
            
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(0)), layer, staff, measure);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(1)), layer, staff, measure);
            DrawNc(dc, dynamic_cast<LayerElement *>(ncVector.at(2)), layer, staff, measure,
                    SMUFL_E991_chantPunctumInclinatum);
            break;
        }
        default: DrawLayerChildren(dc, neume, layer, staff, measure);
    }

    dc->EndGraphic(element, this);
}

} // namespace vrv
