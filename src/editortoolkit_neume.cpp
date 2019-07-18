/////////////////////////////////////////////////////////////////////////////
// Name:        editortoolkit_neume.cpp
// Author:      Laurent Pugin, Juliette Regimbal, Zoe McLennan
// Created:     04/06/2019
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "editortoolkit_neume.h"

//--------------------------------------------------------------------------------

#include <locale>
#include <codecvt>
#include <set>

//--------------------------------------------------------------------------------

#include "clef.h"
#include "comparison.h"
#include "custos.h"
#include "layer.h"
#include "nc.h"
#include "neume.h"
#include "page.h"
#include "rend.h"
#include "staff.h"
#include "staffdef.h"
#include "surface.h"
#include "syl.h"
#include "syllable.h"
#include "text.h"
#include "vrv.h"

//--------------------------------------------------------------------------------

#include "jsonxx.h"

namespace vrv {

#ifdef USE_EMSCRIPTEN
bool EditorToolkitNeume::ParseEditorAction(const std::string &json_editorAction, bool isChain)
{
    jsonxx::Object json;

    // Read JSON actions
    if (!json.parse(json_editorAction)) {
        LogError("Can not parse JSON std::string.");
        return false;
    }

    if (!json.has<jsonxx::String>("action") ||
            (!json.has<jsonxx::Object>("param") && !json.has<jsonxx::Array>("param"))) {
        LogWarning("Incorrectly formatted JSON action");
        return false;
    }

    std::string action = json.get<jsonxx::String>("action");

    if (action != "chain" && json.has<jsonxx::Array>("param")) {
        LogWarning("Only 'chain' uses 'param' as an array.");
        return false;
    }

    if (action == "drag") {
        std::string elementId;
        int x,y;
        if (this->ParseDragAction(json.get<jsonxx::Object>("param"), &elementId, &x, &y)) {
            return this->Drag(elementId, x, y, isChain);
        }
        LogWarning("Could not parse the drag action");
    }
    else if (action == "insert") {
        std::string elementType, startId, endId, staffId;
        int ulx, uly, lrx, lry;
        std::vector<std::pair<std::string, std::string>> attributes;
        if (this->ParseInsertAction(json.get<jsonxx::Object>("param"), &elementType, &staffId, &ulx, &uly,
                    &lrx, &lry, &attributes)) {
            return this->Insert(elementType, staffId, ulx, uly, lrx, lry, attributes);
        }
        LogWarning("Could not parse the insert action");
    }
    else if (action == "set") {
        std::string elementId, attrType, attrValue;
        if (this->ParseSetAction(json.get<jsonxx::Object>("param"), &elementId, &attrType, &attrValue)) {
            return this->Set(elementId, attrType, attrValue);
        }
        LogWarning("Could not parse the set action");
    }
    else if (action == "setText") {
        std::string elementId, text;
        if (this->ParseSetTextAction(json.get<jsonxx::Object>("param"), &elementId, &text)) {
            return this->SetText(elementId, text);
        }
        LogWarning("Could not parse the set text action");
    }
    else if (action == "setClef") {
        std::string elementId, shape;
        if(this->ParseSetClefAction(json.get<jsonxx::Object>("param"), &elementId, &shape)) {
            return this->SetClef(elementId, shape);
        }
        LogWarning("Could not parse the set clef action");
    }
    else if (action == "remove") {
        std::string elementId;
        if (this->ParseRemoveAction(json.get<jsonxx::Object>("param"), &elementId)) {
            return this->Remove(elementId);
        }
        LogWarning("Could not parse the remove action");
    }
    else if (action == "resize") {
        std::string elementId;
        int ulx, uly, lrx, lry;
        if (this->ParseResizeAction(json.get<jsonxx::Object>("param"), &elementId, &ulx, &uly, &lrx, &lry)) {
            return this->Resize(elementId, ulx, uly, lrx, lry);
        }
        LogWarning("Could not parse the resize action");
    }
    else if (action == "chain") {
        if (!json.has<jsonxx::Array>("param")) {
            LogError("Incorrectly formatted JSON action");
            return false;
        }
        return this->Chain(json.get<jsonxx::Array>("param"));
    }
    else if (action == "group") {
        std::string groupType;
        std::vector<std::string> elementIds;
        if (this->ParseGroupAction(json.get<jsonxx::Object>("param"), &groupType, &elementIds)){
            return this->Group(groupType, elementIds);
        }
    }
    else if (action == "ungroup") {
        std::string groupType;
        std::vector<std::string> elementIds;
        if(this->ParseUngroupAction(json.get<jsonxx::Object>("param"), &groupType, &elementIds)){
            return this->Ungroup(groupType, elementIds);
        }
    }
    else if (action == "merge") {
        std::vector<std::string> elementIds;
        if (this->ParseMergeAction(json.get<jsonxx::Object>("param"), &elementIds)) {
            return this->Merge(elementIds);
        }
        LogWarning("Could not parse merge action");
    }
    else if (action == "split") {
        std::string elementId;
        int x;
        if (this->ParseSplitAction(json.get<jsonxx::Object>("param"), &elementId, &x)) {
            return this->Split(elementId, x);
        }
        LogWarning("Could not parse split action");
    }
    else if (action == "changeGroup"){
        std::string elementId;
        std::string contour;
        if(this->ParseChangeGroupAction(json.get<jsonxx::Object>("param"), &elementId, &contour)) {
            return this->ChangeGroup(elementId, contour);
        }
        LogWarning("Could not parse change group action");
    }
    else if (action == "toggleLigature"){
        std::vector<std::string> elementIds;
        std::string isLigature;
        if(this->ParseToggleLigatureAction(json.get<jsonxx::Object>("param"), &elementIds, &isLigature)){
            return this->ToggleLigature(elementIds, isLigature);
        }
        LogWarning("Could not parse toggle ligature action");
    }
    else {
        LogWarning("Unknown action type '%s'.", action.c_str());
    }
    return false;
}

bool EditorToolkitNeume::Chain(jsonxx::Array actions)
{
    bool status = true;
    std::string info = "[";
    bool runReorder = false;
    std::string id = "";
    for (int i = 0; i < actions.size(); i++) {
        if (!actions.has<jsonxx::Object>(i)) {
            LogError("Action %d was not an object", i);
            return false;
        }
        if (actions.get<jsonxx::Object>(i).get<jsonxx::String>("action") == "drag") {
            runReorder = true;
            id = actions.get<jsonxx::Object>(i).get<jsonxx::Object>("param").get<jsonxx::String>("elementId");
        }
        status |= this->ParseEditorAction(actions.get<jsonxx::Object>(i).json(), true);
        if (i != 0)
            info += ", ";
        info += "\"" + m_editInfo + "\"";
    }
    info += "]";
    m_editInfo = info;
    if (status && runReorder) {
        Object *obj = m_doc->GetDrawingPage()->FindChildByUuid(id);
        Layer *layer = NULL;
        assert(obj);
        if (obj->Is(STAFF)) {
            layer = dynamic_cast<Layer *>(obj->GetFirst(LAYER));
        } else {
            layer = dynamic_cast<Layer *>(obj->GetFirstParent(LAYER));
        }
        assert(layer);
        layer->ReorderByXPos();
    }
    return status;
}

bool EditorToolkitNeume::Drag(std::string elementId, int x, int y, bool isChain)
{
    m_editInfo = "";
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get drawing page.");
        return false;
    }

    // Try to get the element on the current drawing page
    Object *element = m_doc->GetDrawingPage()->FindChildByUuid(elementId);

    // If it wasn't there, go back up to the whole doc
    if (!element) {
        element = m_doc->FindChildByUuid(elementId);
    }
    if (!element) {
        LogWarning("element is null");
    }
    assert(element);
    // Use relative x and y for now on
    // For elements whose y-position corresponds to a certain pitch
    if (element->HasInterface(INTERFACE_PITCH)) {
        Layer *layer = dynamic_cast<Layer *>(element->GetFirstParent(LAYER));
        if(!layer) {
            LogError("Element does not have Layer parent. This should not happen.");
            return false;
        }
        Staff *staff = dynamic_cast<Staff *>(layer->GetFirstParent(STAFF));
        assert(staff);
        ClassIdComparison ac(CLEF);
        Clef *clef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindPreviousChildOfType(&ac, element));
        if (clef == NULL) {
            clef = layer->GetCurrentClef();
        }

        // Calculate pitch difference based on y difference
        int pitchDifference = round( (double) y / (double) m_doc->GetDrawingUnit(staff->m_drawingStaffSize));
        element->GetPitchInterface()->AdjustPitchByOffset(pitchDifference);

        if (element->HasInterface(INTERFACE_FACSIMILE)) {
            bool ignoreFacs = false;
            // Dont adjust the same facsimile twice. NCs in a ligature share a single zone.
            if (element->Is(NC)) {
                Nc *nc = dynamic_cast<Nc *>(element);
                if (nc->GetLigated() == BOOLEAN_true) {
                    Neume *neume = dynamic_cast<Neume *>(nc->GetFirstParent(NEUME));
                    Nc *nextNc = dynamic_cast<Nc *>(neume->GetChild(1 + neume->GetChildIndex(element)));
                    if (nextNc != NULL && nextNc->GetLigated() == BOOLEAN_true && nextNc->GetZone() == nc->GetZone())
                        ignoreFacs = true;
                }
            }
            if (!ignoreFacs) {
                FacsimileInterface *fi = element->GetFacsimileInterface();
                assert(fi);
                Zone *zone = fi->GetZone();
                assert(zone);
                zone->ShiftByXY(x, pitchDifference * staff->m_drawingStaffSize);
            }
        }

        Layer *clone = layer->Clone();
        clone->ReorderByXPos();
        Clef *newClef = dynamic_cast<Clef *>(clone->FindPreviousChildOfType(&ac, element));

        if (newClef == NULL) {
            newClef = layer->GetCurrentClef();
        }

        element->GetPitchInterface()->AdjustPitchForNewClef(clef, newClef);
    }
    // TODO Make more generic
    else if (element->Is(NEUME)) {
        Neume *neume = dynamic_cast<Neume *>(element);
        assert(neume);
        Layer *layer = dynamic_cast<Layer *>(neume->GetFirstParent(LAYER));
        if (!layer) {
            LogError("Element does not have Layer parent. This should not occur.");
            return false;
        }
        Staff *staff = dynamic_cast<Staff *>(layer->GetFirstParent(STAFF));
        assert(staff);
        // Calculate difference in pitch based on y difference
        int pitchDifference = round( (double)y / (double)m_doc->GetDrawingUnit(staff->m_drawingStaffSize));

        // Get components of neume
        ClassIdComparison ac(NC);
        ArrayOfObjects objects;
        neume->FindAllChildByComparison(&objects, &ac);
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            Nc *nc = dynamic_cast<Nc *>(*it);
            // Update the neume component
            nc->AdjustPitchByOffset(pitchDifference);
        }

        if (neume->HasFacs()) {
            Zone *zone = neume->GetZone();
            assert(zone);
            zone->ShiftByXY(x, pitchDifference * staff->m_drawingStaffSize);
        }
        else if (dynamic_cast<Nc*>(neume->FindChildByType(NC))->HasFacs()) {
            std::set<Zone *> childZones;    // Sets do not contain duplicate entries
            for (Object *child = neume->GetFirst(); child != NULL; child = neume->Object::GetNext()) {
                FacsimileInterface *fi = child->GetFacsimileInterface();
                if (fi != NULL) {
                    childZones.insert(fi->GetZone());
                }
            }
            for (auto it = childZones.begin(); it != childZones.end(); it++) {
                (*it)->ShiftByXY(x, pitchDifference * staff->m_drawingStaffSize);
            }
        }
    }
    else if(element->Is(SYLLABLE)) {
        Syllable *syllable = dynamic_cast<Syllable *>(element);
        assert(syllable);
        Layer *layer = dynamic_cast<Layer *>(syllable->GetFirstParent(LAYER));
        if (!layer) return false;

        Staff *staff = dynamic_cast<Staff *>(layer->GetFirstParent(STAFF));
        assert(staff);

        int pitchDifference = round( (double)y / (double)m_doc->GetDrawingUnit(staff->m_drawingStaffSize));

        //Get components of syllable
        ClassIdComparison ac(NEUME);
        ArrayOfObjects neumes;
        syllable->FindAllChildByComparison(&neumes, &ac);
        for (auto it = neumes.begin(); it != neumes.end(); ++it) {
            Neume *neume = dynamic_cast<Neume *>(*it);
            assert(neume);
            ClassIdComparison ac(NC);
            ArrayOfObjects ncs;
            neume->FindAllChildByComparison(&ncs, &ac);
            for (auto it = ncs.begin(); it != ncs.end(); ++it) {
                Nc *nc = dynamic_cast<Nc *>(*it);
                // Update the neume component
                nc->AdjustPitchByOffset(pitchDifference);
            }
            if (neume->HasFacs()) {
            Zone *zone = neume->GetZone();
            assert(zone);
            zone->ShiftByXY(x, pitchDifference * staff->m_drawingStaffSize);
            }
            else if (dynamic_cast<Nc*>(neume->FindChildByType(NC))->HasFacs()) {
                std::set<Zone *> childZones;
                for (Object *child = neume->GetFirst(); child != NULL; child = neume->Object::GetNext()) {
                    FacsimileInterface *fi = child->GetFacsimileInterface();
                    if (fi != NULL) {
                        childZones.insert(fi->GetZone());
                    }
                }
                for (auto it = childZones.begin(); it != childZones.end(); it++) {
                    (*it)->ShiftByXY(x, pitchDifference * staff->m_drawingStaffSize);
                }
            }
        }
    }
    else if (element->Is(CLEF)) {
        Clef *clef = dynamic_cast<Clef *>(element);
        assert(clef);
        Layer *layer = dynamic_cast<Layer *>(clef->GetFirstParent(LAYER));
        if (!layer) return false;

        Staff *staff = dynamic_cast<Staff *>(layer->GetFirstParent(STAFF));
        assert(staff);
        // Note that y param is relative to initial position for clefs
        int initialClefLine = clef->GetLine();
        int clefLine = round((double) y / (double) m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) + initialClefLine);

        //////////////////////////////////////////////////////////////////////////////////////////////////////
        // The rest of this if branch (element->Is(CLEF)) is dedicated to ensuring that pitched elements
        // retain their position on the staves by adjusting their pitch position to match their new clefs.
        // With the respect to to this goal there are two main cases. Throughout this comment "this clef" 
        // refers to the clef being dragged.
        //
        //  Case 1:
        //      The clef you're dragging stays between the same two bounding clefs. In this case
        //      elements that are newly associated to this clef need to have their pitch changed from
        //      the clef preceding this clef (previousClef) to this clef. The elements that are associated
        //      with this clef before and after the drag need to have their pitch changed only if the line
        //      of the clef changed during the drag.
        //  Case 2:
        //      The clef you're dragging moves across other clefs. In other words the preceding and 
        //      subsequent clefs are different before and after the drag. In this case elements that were
        //      associated with this clef before the drag need to be reassociated to the clef that preceded
        //      this clef before the drag. Elements that become newly associated with the clef after the drag
        //      need to be reassociated from the clef that preceeds this clef after the drag to this clef.
        //
        // Extracting the exact elements that need to have their pitch modified in each of these cases is
        // tricky, and required some dicey naming. 
        //
        // The clefs we're interested are named precedingClefBefore
        // (meaning the clef that preceded this clef before the drag action), precedingClefAfter (meaning the 
        // clef that preceded this clef after the drag action), nextClefBefore (meaning the clef that came 
        // after this clef before the drag action), and nextClefAfter. And of course clef just refers to
        // this clef, the clef being dragged. 
        // 
        // There are also ArrayOfObjects which refer to which elements
        // were associated to different clefs at different times. with{ClefName} just refers to the pitched
        // elements that were associated to that clef at that time. For example withThisClefBefore refers
        // to the elements that were associated with this clef before the drag action took place. 
        //
        // There are also some slightly trickier array names like withNewPrecedingClefBefore.
        // In this case the the before/after part refers to what elements were associated to
        // this clef at what time, while the new/old part refers to the time of the clef having the 
        // preceding/next relationship to this clef. Let's take withNewPrecedingClefBefore as an example. 
        // Let's say the order of the clefs before the drag is:
        //
        //          A B C D
        //
        // with C being the clef we're dragging. And after the drag action the order of the clefs is:
        //
        //          A C B D
        //
        // withNewPrecedingClefBefore would refer to the elements between A and B, since A is the 
        // new preceding clef (it becomes the clef that precedes this clef after the drag action)
        // and before the drag action all the elements between A and B were associated with A.
        // By comparison, withPrecedingClefAfter (or withNewPrecedingClefAfter) would refer to only
        // the elements between A and C in the lower depiction.
        //
        // The final piece of the naming scheme is the naming of the arrays whose pitch values actually
        // need to be changed. These should be pretty clear. stillWithThisClef for example, refers to
        // the elements that were associated with this clef before and after the drag action, and thus whose
        // pitch values only needs to be changed if the line of this clef changed.
        //
        // The algorithm is implemented by finding all of the aforementioned clefs and the elements between 
        // them, and then using std::set_difference to find the elements whose pitch values may need to change.
        // For example: in case 2 noLongerWithThisClef is found by taking the difference between 
        // withOldPrecedingClefAfter and withPrecedingClefBefore, since that difference is the stuff that
        // became associated with the clef that used to preceed this clef, meaning the stuff that was associated
        // with clef, but no longer is.
        //
        // One other aspect that might seem confusing is exactly when clef->SetLine() gets called. The reason
        // that these calls are oddly placed is that AdjustPitchForNewClef() uses the line of the clef.
        // So if we're changing an element's pitch from this clef to something else, we need the line of 
        // this clef to be what it was before the drag. On the other hand, if we're reassociating an element
        // from some clef to the clef we're dragging, we need the line of this clef to be the one it is after
        // the drag action. Each of the clef->SetLine() calls are placed so as to accommodate this.
        //////////////////////////////////////////////////////////////////////////////////////////////////////

        int lineDiff = clefLine - initialClefLine;

        ArrayOfObjects withThisClefBefore;
        ArrayOfObjects withPrecedingClefBefore;

        ClassIdComparison ac(CLEF);
        InterfaceComparison ic(INTERFACE_PITCH);

        Clef *precedingClefBefore = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindPreviousChildOfType(&ac, clef));
        Clef *nextClefBefore = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindNextChildOfType(&ac, clef));

        if (precedingClefBefore == NULL) {
            precedingClefBefore = layer->GetCurrentClef();
        }

        m_doc->GetDrawingPage()->FindAllChildBetween(&withThisClefBefore, &ic, clef,
            (nextClefBefore != NULL) ? nextClefBefore : m_doc->GetDrawingPage()->GetLast());

        m_doc->GetDrawingPage()->FindAllChildBetween(&withPrecedingClefBefore, &ic, precedingClefBefore, clef);

        if (clef->HasFacs()) { // adjust facsimile for clef (if it exists)
            Zone *zone = clef->GetZone();
            assert(zone);
            zone->ShiftByXY(x, (clefLine - initialClefLine) * 2 * staff->m_drawingStaffSize);
        }

        layer->ReorderByXPos();

        Clef *precedingClefAfter = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindPreviousChildOfType(&ac, clef));
        Clef *nextClefAfter = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindNextChildOfType(&ac, clef));

        if (precedingClefAfter == NULL) {
            precedingClefAfter = layer->GetCurrentClef();
        }

        // case 1
        if (precedingClefAfter == precedingClefBefore && nextClefAfter == nextClefBefore) {
            ArrayOfObjects withThisClefAfter;
            ArrayOfObjects withPrecedingClefAfter;

            m_doc->GetDrawingPage()->FindAllChildBetween(&withThisClefAfter, &ic, clef, 
                (nextClefAfter != NULL) ? nextClefAfter : m_doc->GetDrawingPage()->GetLast());
            m_doc->GetDrawingPage()->FindAllChildBetween(&withPrecedingClefAfter, &ic, precedingClefBefore, clef);   

            if (withPrecedingClefBefore.size() > withPrecedingClefAfter.size()) {
                ArrayOfObjects newlyWithThisClef; 

                clef->SetLine(clefLine);

                std::set_difference(withPrecedingClefBefore.begin(), withPrecedingClefBefore.end(),
                    withPrecedingClefAfter.begin(), withPrecedingClefAfter.end(), 
                    std::inserter(newlyWithThisClef, newlyWithThisClef.begin()));

                for (auto iter = newlyWithThisClef.begin(); iter != newlyWithThisClef.end(); ++iter) {
                    (*iter)->GetPitchInterface()->AdjustPitchForNewClef(precedingClefBefore, clef);
                }

                if (lineDiff != 0) {
                    for (auto iter = withThisClefBefore.begin(); iter != withThisClefBefore.end(); ++iter) {
                        (*iter)->GetPitchInterface()->AdjustPitchByOffset(lineDiff * -2);
                    }
                }
            } 
            else if (withPrecedingClefBefore.size() < withPrecedingClefAfter.size()) {
                ArrayOfObjects noLongerWithThisClef;

                std::set_difference(withPrecedingClefAfter.begin(), withPrecedingClefAfter.end(),
                    withPrecedingClefBefore.begin(), withPrecedingClefBefore.end(),
                    std::inserter(noLongerWithThisClef, noLongerWithThisClef.begin()));

                for (auto iter = noLongerWithThisClef.begin(); iter != noLongerWithThisClef.end(); ++iter) {
                    (*iter)->GetPitchInterface()->AdjustPitchForNewClef(clef, precedingClefBefore);
                }

                if (lineDiff != 0) {
                    for (auto iter = withThisClefAfter.begin(); iter != withThisClefAfter.end(); ++iter) {
                        (*iter)->GetPitchInterface()->AdjustPitchByOffset(lineDiff * -2);
                    }
                }
                clef->SetLine(clefLine);
            }
            else {
                clef->SetLine(clefLine);
                if (lineDiff != 0) {
                    for (auto iter = withThisClefBefore.begin(); iter != withThisClefBefore.end(); ++iter) {
                        (*iter)->GetPitchInterface()->AdjustPitchByOffset(lineDiff * -2);
                    }
                }
            }
        }
        // case 2
        else {
            ArrayOfObjects withOldPrecedingClefAfter;
            ArrayOfObjects withNewPrecedingClefBefore;
            ArrayOfObjects withNewPrecedingClefAfter;
            ArrayOfObjects noLongerWithThisClef; 
            ArrayOfObjects newlyWithThisClef; 

            m_doc->GetDrawingPage()->FindAllChildBetween(&withOldPrecedingClefAfter, &ic, precedingClefBefore, 
                (nextClefBefore != NULL) ? nextClefBefore : m_doc->GetDrawingPage()->GetLast());

            m_doc->GetDrawingPage()->FindAllChildBetween(&withNewPrecedingClefBefore, &ic, precedingClefAfter, 
                (nextClefAfter != NULL) ? nextClefAfter : m_doc->GetDrawingPage()->GetLast());

            m_doc->GetDrawingPage()->FindAllChildBetween(&withNewPrecedingClefAfter, &ic, precedingClefAfter, clef);

            std::set_difference(withOldPrecedingClefAfter.begin(), withOldPrecedingClefAfter.end(),
                withPrecedingClefBefore.begin(), withPrecedingClefBefore.end(), 
                std::inserter(noLongerWithThisClef, noLongerWithThisClef.begin()));

            std::set_difference(withNewPrecedingClefBefore.begin(), withNewPrecedingClefBefore.end(),
                withNewPrecedingClefAfter.begin(), withNewPrecedingClefAfter.end(), 
                std::inserter(newlyWithThisClef, newlyWithThisClef.begin()));

            for (auto iter = noLongerWithThisClef.begin(); iter != noLongerWithThisClef.end(); ++iter) {
                (*iter)->GetPitchInterface()->AdjustPitchForNewClef(clef, precedingClefBefore);
            }

            clef->SetLine(clefLine);

            for (auto iter = newlyWithThisClef.begin(); iter != newlyWithThisClef.end(); ++iter) {
                (*iter)->GetPitchInterface()->AdjustPitchForNewClef(precedingClefAfter, clef);
            }

        }
    }
    else if (element->Is(STAFF)) {
        Staff *staff = dynamic_cast<Staff *>(element);
        if (!staff->HasFacs()) {
            LogError("Staff dragging is only supported for staves with facsimiles!");
            return false;
        }

        // Move staff and all staff children with facsimiles
        ArrayOfObjects children;
        InterfaceComparison ic(INTERFACE_FACSIMILE);
        staff->FindAllChildByComparison(&children, &ic);
        std::set<Zone *> zones;
        zones.insert(staff->GetZone());
        for (auto it = children.begin(); it != children.end(); ++it) {
            FacsimileInterface *fi = (*it)->GetFacsimileInterface();
            assert(fi);
            if (fi->GetZone() != NULL)
                zones.insert(fi->GetZone());
        }
        for (auto it = zones.begin(); it != zones.end(); ++it) {
            // Transform y to device context
            (*it)->ShiftByXY(x, -y);
        }

        //TODO Reorder by left-to-right, top-to-bottom

        return true; // Can't reorder by layer since staves contain layers
    }
    else if (element->Is(SYL)) {
        Syl *syl = dynamic_cast<Syl *>(element);
        if (!syl->HasFacs()) {
            LogError("Syl (boundingbox) dragging is only supported for syls with facsimiles!");
            return false;
        }
        FacsimileInterface *fi = (*syl).GetFacsimileInterface();
        assert(fi);
        if (fi->GetZone() != NULL) {
            fi->GetZone()->ShiftByXY(x, -y);
        }
    }
    else {
        LogWarning("Unsupported element for dragging.");
        return false;
    }
    if (!isChain) {
        Layer *layer = dynamic_cast<Layer *>(element->GetFirstParent(LAYER));
        layer->ReorderByXPos(); // Reflect position order of elements internally (and in the resulting output file)
    }
    return true;
}

bool EditorToolkitNeume::Insert(std::string elementType, std::string staffId, int ulx, int uly,
        int lrx, int lry, std::vector<std::pair<std::string, std::string>> attributes)
{
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get drawing page");
        return false;
    }
    if (m_doc->GetType() != Facs) {
        LogError("Drawing page without facsimile");
        return false;
    }

    Staff *staff;


    // Find closest valid staff
    if (staffId == "auto") {
        ArrayOfObjects staves;
        ClassIdComparison ac(STAFF);
        m_doc->FindAllChildByComparison(&staves, &ac);

        ClosestBB comp;
        comp.x = ulx;
        comp.y = uly;

        if (staves.size() > 0) {
            std::sort(staves.begin(), staves.end(), comp);
            staff = dynamic_cast<Staff *>(staves.at(0));
        }
        else {
            staff = NULL;
        }
    }
    else {
        staff = dynamic_cast<Staff *>(m_doc->FindChildByUuid(staffId));
    }

    Facsimile *facsimile = m_doc->GetFacsimile();
    Zone *zone = new Zone();


    if (elementType == "staff") {
        Object *parent;
        Staff *newStaff;
        // Use closest existing staff (if there is one)
        if (staff) {
            parent = staff->GetParent();
            assert(parent);
            int n = parent->GetChildCount() + 1;
            newStaff = new Staff(n);
            newStaff->m_drawingStaffDef = staff->m_drawingStaffDef;
            newStaff->m_drawingNotationType = staff->m_drawingNotationType;
            newStaff->m_drawingLines = staff->m_drawingLines;
        }
        else {
            parent = m_doc->GetDrawingPage()->FindChildByType(MEASURE);
            assert(parent);
            newStaff = new Staff(1);
            newStaff->m_drawingStaffDef = dynamic_cast<StaffDef *>(m_doc->m_scoreDef.FindChildByType(STAFFDEF));
            newStaff->m_drawingNotationType = NOTATIONTYPE_neume;
            newStaff->m_drawingLines = 4;
        }
        newStaff->m_drawingStaffSize = (uly - lry) / (newStaff->m_drawingLines - 1);
        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(lrx);
        zone->SetLry(lry);
        Surface *surface = dynamic_cast<Surface *>(m_doc->GetFacsimile()->FindChildByType(SURFACE));
        assert(surface);
        surface->AddChild(zone);
        newStaff->SetZone(zone);
        newStaff->SetFacs(zone->GetUuid());
        Layer *newLayer = new Layer();
        newStaff->AddChild(newLayer);

        // Find index to insert new staff
        ArrayOfObjects staves;
        ClassIdComparison ac(STAFF);
        parent->FindAllChildByComparison(&staves, &ac);
        staves.push_back(newStaff);
        StaffSort staffSort;
        std::stable_sort(staves.begin(), staves.end(), staffSort);
        for (int i = 0; i < staves.size(); i++) {
            if (staves.at(i) == newStaff) {
                newStaff->SetParent(parent);
                parent->InsertChild(newStaff, i);
                parent->Modify();
                m_editInfo = newStaff->GetUuid();
                return true;
            }
        }
        LogMessage("Failed to insert newStaff into staff");
        parent->AddChild(newStaff);
        parent->Modify();
        m_editInfo = newStaff->GetUuid();
        return true;
    }

    if (staff == NULL) {
        LogError("A staff must exist in the page to add a non-staff element.");
        delete zone;
        return false;
    }
    Layer *layer = dynamic_cast<Layer *>(staff->FindChildByType(LAYER));
    assert(layer);

    if (elementType == "nc" || elementType == "grouping") {
        Syllable *syllable = new Syllable();
        Syl *syl = new Syl();
        Neume *neume = new Neume();
        Nc *nc = new Nc();
        std::string contour = "";
        nc->SetZone(zone);
        nc->SetFacs(zone->GetUuid());

        Surface *surface = dynamic_cast<Surface *>(facsimile->FindChildByType(SURFACE));
        surface->AddChild(zone);
        zone->SetUlx(ulx);

        neume->AddChild(nc);
        syllable->AddChild(neume);
        syllable->AddChild(syl);
        layer->AddChild(syllable);

        // add syl bounding box if the option is true
        if (m_doc->GetOptions()->m_createDefaultSylBBox.GetValue()) {
            FacsimileInterface *fi = dynamic_cast<FacsimileInterface *>(syl->GetFacsimileInterface());
            assert(fi);
            Text *text = new Text();
            syl->AddChild(text);
            Zone *sylZone = new Zone();

            // these constants are just to improve visibility of the default boundingbox
            int offSetUlx = 150;
            int offSetUly = 50;
            int offSetLrx = 350;
            int offSetLry = 250;

            sylZone->SetUlx(ulx + offSetUlx);
            sylZone->SetUly(uly + offSetUly);
            sylZone->SetLrx(ulx + offSetLrx);
            sylZone->SetLry(uly + offSetLry);
            surface->AddChild(sylZone);
            fi->SetZone(sylZone);
            syl->SetFacs(zone->GetUuid());

        }

        // Find closest valid clef
        Clef *clef = NULL;
        clef = layer->GetClef(nc);
        if (clef == NULL) {
            LogError("There is no valid clef available.");
            delete syllable;
            delete neume;
            delete nc;
            return false;
        }

        nc->SetOct(3);
        if (clef->GetShape() == CLEFSHAPE_C) {
            nc->SetPname(PITCHNAME_c);
        }
        else if (clef->GetShape() == CLEFSHAPE_F) {
            nc->SetPname(PITCHNAME_f);
        }

        // Set as inclinatum or virga (if necessary), or get contour for grouping
        for (auto it = attributes.begin(); it != attributes.end(); ++it) {
            if (it->first == "tilt") {
                if (it->second == "n") {
                    data_COMPASSDIRECTION direction;
                    direction.SetBasic(COMPASSDIRECTION_basic_n);
                    nc->SetTilt(direction);
                }
                else if (it->second == "se") {
                    data_COMPASSDIRECTION direction;
                    direction.SetExtended(COMPASSDIRECTION_extended_se);
                    nc->SetTilt(direction);
                }
            }
            else if (it->first == "contour") {
                contour = it->second;
            }
        }

        const int staffSize = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
        const int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
        const int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);
        const int pitchDifference = round((double) (staff->GetZone()->GetUly() + (2 * staffSize * (staff->m_drawingLines - clef->GetLine())) - (uly)) / (double) (staffSize));

        nc->AdjustPitchByOffset(pitchDifference);
        ulx -= noteWidth / 2;
        uly -= noteHeight / 2;
        // Set up facsimile
        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(ulx + noteWidth);
        zone->SetLry(uly + noteHeight);

        //If inserting grouping, add the remaining nc children to the neume.
        if(contour != ""){
            Nc *prevNc = nc;
            for(auto it = contour.begin(); it != contour.end(); ++it) {
                Nc *newNc = new Nc();
                Zone *newZone = new Zone();
                int newUlx = ulx + noteWidth;
                int newUly;

                newNc->SetPname(prevNc->GetPname());
                newNc->SetOct(prevNc->GetOct());

                if((*it) == 'u'){
                    newUly = uly - noteHeight;
                    newNc->AdjustPitchByOffset(1);
                }
                else if((*it) == 'd'){
                    newUly = uly + noteHeight;
                    newNc->AdjustPitchByOffset(-1);
                }
                else if((*it) == 's'){
                    newUly = uly;
                }
                else{
                    LogMessage("Unsupported character in contour.");
                    return false;
                }
                newZone->SetUlx(newUlx);
                newZone->SetUly(newUly);;
                newZone->SetLrx(newUlx + noteWidth);
                newZone->SetLry(newUly + noteHeight);

                newNc->SetZone(newZone);
                newNc->SetFacs(newZone->GetUuid());

                assert(surface);
                surface->AddChild(newZone);

                neume->AddChild(newNc);

                ulx = newUlx;
                uly = newUly;
                prevNc = newNc;
            }
        }
        if(elementType == "nc"){
            m_editInfo = nc->GetUuid();
        }
        else{
            m_editInfo = neume->GetUuid();
        }
    }
    else if (elementType == "clef") {
        Clef *clef = new Clef();
        data_CLEFSHAPE clefShape = CLEFSHAPE_NONE;

        for (auto it = attributes.begin(); it != attributes.end(); ++it) {
            if (it->first == "shape") {
                if (it->second == "C") {
                    clefShape = CLEFSHAPE_C;
                    break;
                }
                else if (it->second == "F") {
                    clefShape = CLEFSHAPE_F;
                    break;
                }
            }
        }
        if (clefShape == CLEFSHAPE_NONE) {
            LogError("A clef shape must be specified.");
            delete clef;
            return false;
        }
        clef->SetShape(clefShape);
        const int staffSize = m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize);
        int yDiff = -staff->GetZone()->GetUly() + uly;
        int clefLine = staff->m_drawingLines - round((double) yDiff / (double) staffSize);
        clef->SetLine(clefLine);

        Zone *zone = new Zone();
        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(ulx + staffSize / 1.4);
        zone->SetLry(uly + staffSize / 2);
        clef->SetZone(zone);
        clef->SetFacs(zone->GetUuid());
        Surface *surface = dynamic_cast<Surface *>(facsimile->FindChildByType(SURFACE));
        assert(surface);
        surface->AddChild(zone);
        layer->AddChild(clef);
        m_editInfo = clef->GetUuid();
        layer->ReorderByXPos();

        // ensure pitched elements associated with this clef keep their x,y positions

        ClassIdComparison ac(CLEF);
        Clef *previousClef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindPreviousChildOfType(&ac, clef));
        Clef *nextClef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindNextChildOfType(&ac, clef));

        if (previousClef == NULL) {
            // if there is no previous clef, get the default one from the staff def
            previousClef = layer->GetCurrentClef();
        }

        // adjust pitched elements whose clef is changing
        ArrayOfObjects elements;
        InterfaceComparison ic(INTERFACE_PITCH);

        m_doc->GetDrawingPage()->FindAllChildBetween(&elements, &ic, clef, 
            (nextClef != NULL) ? nextClef : m_doc->GetDrawingPage()->GetLast());

        for (auto it = elements.begin(); it != elements.end(); ++it) {
            PitchInterface *pi = (*it)->GetPitchInterface();
            assert(pi);
            pi->AdjustPitchForNewClef(previousClef, clef);
        }
    }
    else if (elementType == "custos") {
        Custos *custos = new Custos();
        zone->SetUlx(ulx);
        Surface *surface = dynamic_cast<Surface *>(facsimile->GetFirst(SURFACE));
        surface->AddChild(zone);
        custos->SetZone(zone);
        custos->SetFacs(zone->GetUuid());
        layer->AddChild(custos);
        // Find closest valid clef
        Clef *clef = NULL;
        clef = layer->GetClef(custos);
        if (clef == NULL) {
            LogError("There is no valid clef available.");
            delete custos;
            return false;
        }

        custos->SetOct(3);
        if (clef->GetShape() == CLEFSHAPE_C)
            custos->SetPname(PITCHNAME_c);
        else if (clef->GetShape() == CLEFSHAPE_F)
            custos->SetPname(PITCHNAME_f);

        const int staffSize = m_doc->GetDrawingUnit(staff->m_drawingStaffSize);
        const int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
        const int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);
        const int pitchDifference = round((double) (staff->GetZone()->GetUly() + (2 * staffSize * (staff->m_drawingLines - clef->GetLine())) - (uly)) / (double) (staffSize));

        custos->AdjustPitchByOffset(pitchDifference);
        ulx -= noteWidth / 2;
        uly -= noteHeight / 2;

        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(ulx + noteWidth);
        zone->SetLry(uly + noteHeight);
        m_editInfo = custos->GetUuid();
    }
    else {
        LogError("Unsupported type '%s' for insertion", elementType.c_str());
        return false;
    }
    layer->ReorderByXPos();
    return true;
}

bool EditorToolkitNeume::Merge(std::vector<std::string> elementIds)
{
    m_editInfo = "";
    if (!m_doc->GetDrawingPage()) return false;
    ArrayOfObjects staves;
    int ulx = INT_MAX, uly = 0, lrx = 0, lry = 0;

    // Get the staves by element ID and fail if a staff does not exist.
    for (auto it = elementIds.begin(); it != elementIds.end(); ++it) {
        Object *obj = m_doc->GetDrawingPage()->FindChildByUuid(*it);
        if (obj != NULL && obj->Is(STAFF)) {
            staves.push_back(obj);
            Zone *zone = obj->GetFacsimileInterface()->GetZone();
            ulx = ulx < zone->GetUlx() ? ulx : zone->GetUlx();
            lrx = lrx > zone->GetLrx() ? lrx : zone->GetLrx();
            uly += zone->GetUly();
            lry += zone->GetLry();
        }
        else {
            LogWarning("Staff with ID '%s' does not exist!", it->c_str());
            return false;
        }
    }
    if (staves.size() < 2) {
        LogWarning("At least two staves must be provided.");
        return false;
    }

    uly /= staves.size();
    lry /= staves.size();
    StaffSort staffSort;
    std::sort(staves.begin(), staves.end(), staffSort);

    // Move children to the first staff (in order)
    auto stavesIt = staves.begin();
    Staff *fillStaff = dynamic_cast<Staff *>(*stavesIt);
    Layer *fillLayer = dynamic_cast<Layer *>(fillStaff->GetFirst(LAYER));
    assert(fillLayer);
    stavesIt++;
    for (; stavesIt != staves.end(); ++stavesIt) {
        Staff *sourceStaff = dynamic_cast<Staff *>(*stavesIt);
        Layer *sourceLayer = dynamic_cast<Layer *>(sourceStaff->GetFirst(LAYER));
        fillLayer->MoveChildrenFrom(sourceLayer);
        assert(sourceLayer->GetChildCount() == 0);
        Object *parent = sourceStaff->GetParent();
        parent->DeleteChild(sourceStaff);
    }
    // Set the bounding box for the staff to the new bounds
    Zone *staffZone = fillStaff->GetZone();
    staffZone->SetUlx(ulx);
    staffZone->SetUly(uly);
    staffZone->SetLrx(lrx);
    staffZone->SetLry(lry);

    fillLayer->ReorderByXPos();

    m_editInfo = fillStaff->GetUuid();

    // TODO change zones for staff children

    return true;
}

bool EditorToolkitNeume::Set(std::string elementId, std::string attrType, std::string attrValue)
{
    if (!m_doc->GetDrawingPage()) return false;
    Object *element = m_doc->GetDrawingPage()->FindChildByUuid(elementId);
    bool success = false;
    if (Att::SetAnalytical(element, attrType, attrValue))
        success = true;
    else if (Att::SetCmn(element, attrType, attrValue))
        success = true;
    else if (Att::SetCmnornaments(element, attrType, attrValue))
        success = true;
    else if (Att::SetCritapp(element, attrType, attrValue))
        success = true;
    else if (Att::SetExternalsymbols(element, attrType, attrValue))
        success = true;
    else if (Att::SetGestural(element, attrType, attrValue))
        success = true;
    else if (Att::SetMei(element, attrType, attrValue))
        success = true;
    else if (Att::SetMensural(element, attrType, attrValue))
        success = true;
    else if (Att::SetMidi(element, attrType, attrValue))
        success = true;
    else if (Att::SetNeumes(element, attrType, attrValue))
        success = true;
    else if (Att::SetPagebased(element, attrType, attrValue))
        success = true;
    else if (Att::SetShared(element, attrType, attrValue))
        success = true;
    else if (Att::SetVisual(element, attrType, attrValue))
        success = true;
    if (success && m_doc->GetType() != Facs) {
        m_doc->PrepareDrawing();
        m_doc->GetDrawingPage()->LayOut(true);
    }
    return success;
}

// Update the text of a TextElement by its syl
bool EditorToolkitNeume::SetText(std::string elementId, std::string text)
{
    m_editInfo = "";
    std::wstring wtext;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
    wtext = conv.from_bytes(text);
    if (!m_doc->GetDrawingPage()) return false;
    Object *element = m_doc->GetDrawingPage()->FindChildByUuid(elementId);
    if (element == NULL) {
        LogWarning("No element with ID '%s' exists", elementId.c_str());
        return false;
    }

    bool success = false;
    if (element->Is(SYL)) {
        Syl *syl = dynamic_cast<Syl *>(element);
        assert(syl);
        Object *child = syl->GetFirst();
        if(child == NULL) {
            Text *text = new Text();
            syl->AddChild(text);
            text->SetText(wtext);
            success = true;
        }
        else {
            while(child != NULL) {
                if (child->Is(TEXT)) {
                    Text *text = dynamic_cast<Text *>(child);
                    text->SetText(wtext);
                    success = true;
                    break;
                }
                else if (child->Is(REND)) {
                    Rend *rend = dynamic_cast<Rend *>(child);
                    Object *rendChild = rend->GetFirst();
                    if (rendChild->Is(TEXT)) {
                        Text *rendText = dynamic_cast<Text *>(rendChild);
                        rendText->SetText(wtext);
                        success = true;
                    }
                }
                child = syl->Object::GetNext();
            }
        }

    }
    else if (element->Is(SYLLABLE)) {
        Syllable *syllable = dynamic_cast<Syllable *>(element);
        assert(syllable);
        Object *syl = syllable->GetFirst(SYL);
        if (syl == NULL) {
            syl = new Syl();
            syllable->AddChild(syl);
            Text *textChild = new Text();
            textChild->SetText(wtext);
            syl->AddChild(textChild);
            success = true;
        }
        else {
            success = SetText(syl->GetUuid(), text);
        }
    }
    else {
        LogWarning("Element type '%s' is unsupported for SetText", element->GetClassName().c_str());
        return false;
    }
    return success;
}

bool EditorToolkitNeume::SetClef(std::string elementId, std::string shape)
{
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    ArrayOfObjects objects;
    bool success = false;
    data_CLEFSHAPE clefShape = CLEFSHAPE_NONE;
    int shift = 0;
    Clef *clef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindChildByUuid(elementId));
    assert(clef);

    if (shape == "C") {
        clefShape = CLEFSHAPE_C;
        shift = -3;
    }
    else if (shape== "F") {
        clefShape = CLEFSHAPE_F;
        shift = 3;
    }

    if(clef->GetShape() != clefShape){
        success = Att::SetShared(clef, "shape", shape);
        if(!success){
            LogWarning("Unable to set clef shape");
            return false;
        }

        Layer *layer = dynamic_cast<Layer *>(clef->GetFirstParent(LAYER));
        assert(layer);

        Object *nextClef = m_doc->GetDrawingPage()->GetNext(clef, CLEF);
        assert(nextClef);
        InterfaceComparison ic(INTERFACE_PITCH);

        m_doc->GetDrawingPage()->FindAllChildBetween(&objects, &ic, clef,
            (nextClef != NULL) ? nextClef : m_doc->GetDrawingPage()->GetLast());

        // Adjust all elements who are positioned relative to clef by pitch
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            Object *child = dynamic_cast<Object *>(*it);
            if (child == NULL || layer->GetClef(dynamic_cast<LayerElement *>(child)) != clef) continue;
            PitchInterface *pi = child->GetPitchInterface();
            assert(pi);
            pi->AdjustPitchByOffset(shift);
        }
    }
    if (success && m_doc->GetType() != Facs) {
        m_doc->PrepareDrawing();
        m_doc->GetDrawingPage()->LayOut(true);
    }
    return true;
}

bool EditorToolkitNeume::Split(std::string elementId, int x)
{
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page");
        return false;
    }
    Staff *staff = dynamic_cast<Staff *>(m_doc->GetDrawingPage()->FindChildByUuid(elementId));
    // Validate parameters
    if (staff == NULL) {
        LogError("Either no element exists with ID '%s' or it is not a staff.", elementId.c_str());
        return false;
    }

    if (staff->GetZone()->GetUlx() > x || staff->GetZone()->GetLrx() < x) {
        LogError("The 'x' parameter is not within the bounds of the original staff.");
        return false;
    }

    // Resize current staff and insert new one filling remaining area.
    int newUlx = x;
    int newLrx = staff->GetZone()->GetLrx();
    std::vector<std::pair<std::string, std::string>> v;

    if (!this->Insert("staff", "auto", newUlx, staff->GetZone()->GetUly(), newLrx, staff->GetZone()->GetLry(), v)) {
        LogError("Failed to create a second staff.");
        return false;
    }
    Staff *splitStaff = dynamic_cast<Staff *>(m_doc->GetDrawingPage()->FindChildByUuid(m_editInfo));
    assert(splitStaff);
    if (splitStaff == NULL) {
        LogMessage("Split staff is null");
    }

    staff->GetZone()->SetLrx(x);
    Layer *layer = dynamic_cast<Layer *>(staff->GetFirst(LAYER));
    Layer *splitLayer = dynamic_cast<Layer *>(splitStaff->GetFirst(LAYER));

    // Move any elements that should be on the second staff there.
    for (Object *child = layer->GetFirst(); child != NULL; child = layer->GetNext()) {
        assert(child);
        FacsimileInterface *fi = dynamic_cast<FacsimileInterface *>(child);
        if (fi == NULL || !fi->HasFacs()) {
            fi = NULL;
            ArrayOfObjects facsimileInterfaces;
            InterfaceComparison ic(INTERFACE_FACSIMILE);
            child->FindAllChildByComparison(&facsimileInterfaces, &ic);

            for (auto it = facsimileInterfaces.begin(); it != facsimileInterfaces.end(); ++it) {
                FacsimileInterface *temp = dynamic_cast<FacsimileInterface *>(*it);
                assert(temp);
                if (temp->HasFacs() && (fi == NULL || temp->GetZone()->GetUlx() < fi->GetZone()->GetUlx())) {
                    fi = temp;
                }
            }
        }

        if (fi == NULL) {
            continue;
        }
        else if (fi->GetZone()->GetUlx() > x) {
            child->MoveItselfTo(splitLayer);
        }
    }
    layer->ClearRelinquishedChildren();
    m_editInfo = splitStaff->GetUuid();
    return true;
}

bool EditorToolkitNeume::Remove(std::string elementId)
{
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    Object *obj = m_doc->GetDrawingPage()->FindChildByUuid(elementId);
    assert(obj);
    bool result, isNeume, isClef;
    isNeume = (obj->Is(NC) || obj->Is(NEUME));
    isClef = obj->Is(CLEF);
    Object *parent = obj->GetParent();
    assert(parent);
    m_editInfo = elementId;
    // Remove Zone for element (if any)
    InterfaceComparison ic(INTERFACE_FACSIMILE);
    ArrayOfObjects fiChildren;
    obj->FindAllChildByComparison(&fiChildren, &ic);
    FacsimileInterface *fi = dynamic_cast<FacsimileInterface *>(obj);
    if (fi != NULL && fi->HasFacs()) {
        fi->SetZone(NULL);
    }
    for (auto it = fiChildren.begin(); it != fiChildren.end(); ++it) {
        fi = dynamic_cast<FacsimileInterface *>(*it);
        if (fi != NULL && fi->HasFacs()) {
            fi->SetZone(NULL);
        }
    }
    if (isClef) {
        // y position of pitched elements (like neumes) is determined by their pitches
        // so when deleting a clef, the position on a page that a pitch value is associated with could change
        // so we need to change the pitch value of any elements whose clef is going to change
        Clef *clef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindChildByUuid(elementId));
        assert(clef);
        ClassIdComparison ac(CLEF);
        Clef *previousClef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindPreviousChildOfType(&ac, clef));
        Clef *nextClef = dynamic_cast<Clef *>(m_doc->GetDrawingPage()->FindNextChildOfType(&ac, clef));

        if (previousClef == NULL) {
            // if there is no previous clef, get the default one from the staff def
            Layer *layer = dynamic_cast<Layer *>(clef->GetFirstParent(LAYER));
            previousClef = layer->GetCurrentClef();
        }

        ArrayOfObjects elements;
        InterfaceComparison ic(INTERFACE_PITCH);

        m_doc->GetDrawingPage()->FindAllChildBetween(&elements, &ic, clef, 
            (nextClef != NULL) ? nextClef : m_doc->GetDrawingPage()->GetLast());

        for (auto it = elements.begin(); it != elements.end(); ++it) {
            PitchInterface *pi = (*it)->GetPitchInterface();
            assert(pi);
            // removing the current clef, and so the new clef for all of these elements is previousClef
            pi->AdjustPitchForNewClef(clef, previousClef);
        }

    }
    result = parent->DeleteChild(obj);
    if (isNeume && result) {
        if (!parent->Is(SYLLABLE)) {
            parent = parent->GetFirstParent(SYLLABLE);
            if (parent == NULL) { LogMessage("Failed to get syllable parent!"); return false; }
        }
        assert(parent->Is(SYLLABLE));
        if (parent->FindChildByType(NC) == NULL) {
            obj = parent;
            parent = parent->GetParent();
            if (parent == NULL) { LogMessage("Null parent!"); return false; }
            // Remove Zone for element (if any)
            fi = dynamic_cast<FacsimileInterface *>(obj);
            if (fi != NULL && fi->HasFacs()) {
                fi->SetZone(NULL);
            }
            result &= parent->DeleteChild(obj);
        }
    }
    
    return result;
}

bool EditorToolkitNeume::Resize(std::string elementId, int ulx, int uly, int lrx, int lry)
{
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    if (m_doc->GetType() != Facs) {
        LogWarning("Resizing is only available in facsimile mode.");
        return false;
    }

    Object *obj = m_doc->GetDrawingPage()->FindChildByUuid(elementId);
    if (obj == NULL) {
        LogError("Object with ID '%s' not found.", elementId.c_str());
        return false;
    }
    if (obj->Is(STAFF)) {
        Staff *staff = dynamic_cast<Staff *>(obj);
        assert(staff);
        if (!staff->HasFacs()) {
            LogError("This staff does not have a facsimile.");
            return false;
        }
        Zone *zone = staff->GetZone();
        assert(zone);
        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(lrx);
        zone->SetLry(lry);
        zone->Modify();
    }
    else if (obj->Is(SYL)) {
        Syl *syl = dynamic_cast<Syl *>(obj);
        assert(syl);
        if (!syl->HasFacs()) {
            LogError("This syl (bounding box) does not have a facsimile");
            return false;
        }
        Zone *zone = syl->GetZone();
        assert(zone);
        zone->SetUlx(ulx);
        zone->SetUly(uly);
        zone->SetLrx(lrx);
        zone->SetLry(lry);
        zone->Modify();
    }
    else {
        LogMessage("Element of type '%s' is unsupported.", obj->GetClassName().c_str());
        return false;
    }
    return true;
}

bool EditorToolkitNeume::Group(std::string groupType, std::vector<std::string> elementIds)
{
    m_editInfo = "";
    Object *parent = NULL, *doubleParent = NULL;
    std::map<Object *, int> parents;
    std::set<Object *> elements;
    std::vector<Object *> fullParents;

    //Get the current drawing page
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    if (elementIds.size() == 0) {
        LogWarning("No element IDs to group!");
        return true;
    }
    ClassId elementClass;
    if (groupType == "nc") {
        elementClass = NC;
    } else if (groupType == "neume") {
        elementClass = NEUME;
    } else {
        LogError("Invalid groupType: %s", groupType.c_str());
        return false;
    }

    // Determine what the parents are
    for (auto it = elementIds.begin(); it != elementIds.end(); ++it) {
        // Verify that the children are of the same type
        Object *el = m_doc->GetDrawingPage()->FindChildByUuid(*it);
        if (el == NULL) {
            LogError("Could not get element with ID %s", it->c_str());
            return false;
        }
        if (el->GetClassId() != elementClass) {
            LogError("Element %s was of class %s. Expected class %s",
                el->GetUuid().c_str(), el->GetClassName().c_str(), groupType.c_str());
            return false;
        }

        // Get a set of parents and the number of children they have
        Object *par = el->GetParent();
        if (par == NULL) {
            LogError("Parent of %s is null!", el->GetUuid().c_str());
            return false;
        }
        if (doubleParent == NULL) {
            doubleParent = par->GetParent();
            if (doubleParent == NULL) {
                LogError("No second level parent!");
                return false;
            }
        }
        else {
            if (par->GetParent() != doubleParent) {
                LogError("No shared second level parent!");
                return false;
            }
        }
        auto possibleEntry = parents.find(el->GetParent());
        if (possibleEntry == parents.end()) {
            parents.emplace(el->GetParent(), 1);
        }
        else {
            possibleEntry->second += 1;
        }
        elements.insert(el);
    }

    // find parents where all of their children are being grouped
    for (auto it = parents.begin(); it != parents.end(); ++it) {
        auto parentPair = *it;
        Object *par = parentPair.first;
        int expected;
        if (par->GetClassId() == SYLLABLE) {
            expected = par->GetChildCount(NEUME);
        } else {
            expected = par->GetChildCount();
        }
        if (parentPair.second == expected) {
            fullParents.push_back(parentPair.first);
        }
    }
    //if there are no full parents we need to make a new one to attach everything to
    if (fullParents.empty()) {
        if (elementClass == NC) {
            parent = new Neume();
        }
        else if (elementClass == NEUME) {
            parent = new Syllable();

            for (auto it = elements.begin(); it != elements.end(); ++it) {
                if ((*it)->GetParent() != parent && !(*it)->Is(SYL)) {
                    (*it)->MoveItselfTo(parent);
                }
            }

            //make sure to add an empty syl if the option is provided
            if (m_doc->GetOptions()->m_createDefaultSyl.GetValue()) {
                Syl *syl = new Syl();
                Text *text = new Text();
                syl->AddChild(text);
                parent->AddChild(syl);

                //add a default bounding box if you need to
                if (m_doc->GetOptions()->m_createDefaultSylBBox.GetValue()) {
                    Zone *zone = new Zone();

                    // if it's syllable parent has position values just use those
                    FacsimileInterface *syllableFi = NULL;
                    if (syl->GetFirstParent(SYLLABLE)->GetFacsimileInterface()->HasFacs()) {
                        syllableFi = syl->GetFirstParent(SYLLABLE)->GetFacsimileInterface();
                        Zone *tempZone = dynamic_cast<Zone *>(syllableFi->GetZone());
                        zone->SetUlx(tempZone->GetUlx());
                        zone->SetUly(tempZone->GetUly());
                        zone->SetLrx(tempZone->GetLrx());
                        zone->SetLry(tempZone->GetLry());
                    }
                    // otherwise get a boundingbox that comprises all the neumes in the syllable
                    else {
                        ArrayOfObjects children;
                        InterfaceComparison comp(INTERFACE_FACSIMILE);
                        syl->GetFirstParent(SYLLABLE)->FindAllChildByComparison(&children, &comp);
                        for (auto iter2 = children.begin(); iter2 != children.end(); ++iter2) {
                            FacsimileInterface *temp = dynamic_cast<FacsimileInterface *>(*iter2);
                            assert(temp);
                            Zone *tempZone = dynamic_cast<Zone *>(temp->GetZone());
                            assert(tempZone);
                            if (temp->HasFacs()) {
                                if (syllableFi == NULL) {
                                    zone->SetUlx(tempZone->GetUlx());
                                    zone->SetUly(tempZone->GetUly());
                                    zone->SetLrx(tempZone->GetLrx());
                                    zone->SetLry(tempZone->GetLry());
                                }
                                else {
                                    if (tempZone->GetUlx() < zone->GetUlx()) {
                                        zone->SetUlx(tempZone->GetUlx());
                                    }
                                    if (tempZone->GetUly() < zone->GetUly()) {
                                        zone->SetUly(tempZone->GetUly());
                                    }
                                    if (tempZone->GetLrx() > zone->GetLrx()) {
                                        zone->SetLrx(tempZone->GetLrx());
                                    }
                                    if (tempZone->GetLry() > zone->GetLry()) {
                                        zone->SetLry(tempZone->GetLry());
                                    }
                                }
                            }
                        }
                    }

                    //make the bounding box a little bigger and lower so it's easier to edit
                    int offSetUly = 100;
                    int offSetLrx = 100;
                    int offSetLry = 200;

                    zone->SetUly(zone->GetUly() + offSetUly);
                    zone->SetLrx(zone->GetLrx() + offSetLrx);
                    zone->SetLry(zone->GetLry() + offSetLry);

                    assert(m_doc->GetFacsimile());
                    m_doc->GetFacsimile()->FindChildByType(SURFACE)->AddChild(zone);
                    FacsimileInterface *fi = dynamic_cast<FacsimileInterface *>((*syl).GetFacsimileInterface());
                    assert(fi);
                    fi->SetZone(zone);

                    syl->ResetFacsimile();
                    syl->SetFacs(zone->GetUuid());
                }
            }
        }

        parent->ReorderByXPos();
        doubleParent->AddChild(parent);

        Layer *layer = dynamic_cast<Layer *> (parent->GetFirstParent(LAYER));
        assert(layer);
        layer->ReorderByXPos();
    }

    //if there's only one full parent we just add the other elements to it
    //except don't move syl tags since we want them to stay attached to the first parent
    else if(fullParents.size() == 1) {
        auto iter = fullParents.begin();
        parent = *iter;
        for (auto it = elements.begin(); it != elements.end(); ++it) {
            if ((*it)->GetParent() != parent && !(*it)->Is(SYL)) {
                (*it)->MoveItselfTo(parent);
            }
        }
        parent->ReorderByXPos();
    }

    //if there are more than 1 full parent we need to concat syl's
    //unless we're just grouping NC's in which case no need to worry about syl's of course
    //also in this case we need to make sure that the facsimile of the resulting syl is correct
    else {
        if (elementClass == NC) {
            parent = new Neume();
            for (auto it = elements.begin(); it != elements.end(); ++it) {
                if ((*it)->GetParent() != parent && !(*it)->Is(SYL)) {
                    (*it)->MoveItselfTo(parent);
                }
            }
            doubleParent->AddChild(parent);
        }
        else {
            std::sort(fullParents.begin(), fullParents.end(), Object::sortByUlx);
            Syllable *fullSyllable = new Syllable();
            Syl *fullSyl;

            //construct concatenated string of all the syls
            std::wstring fullString = L"";
            for (auto it = fullParents.begin(); it != fullParents.end(); ++it) {
                Syl *syl = dynamic_cast<Syl *> ((*it)->FindChildByType(SYL));
                if (fullSyl == NULL && syl != NULL) {
                    fullSyl = syl;
                }
                Text *text = dynamic_cast<Text *> (syl->FindChildByType(TEXT));
                if (text != NULL) {
                    std::wstring currentString = text->GetText();
                    fullString = fullString + currentString;
                }
            }
            //find the new boundingbox comprising all of the text
            int ulx = -1, uly = -1, lrx = -1, lry = -1;
            for (auto it = fullParents.begin(); it != fullParents.end(); ++it) {
                FacsimileInterface *facsInter = dynamic_cast<FacsimileInterface *> ((*it)->FindChildByType(SYL)->GetFacsimileInterface());
                if (facsInter != NULL) {
                    if (ulx == -1) {
                        ulx = facsInter->GetDrawingX();
                        uly = facsInter->GetDrawingY();
                        lrx = facsInter->GetWidth() + ulx;
                        lry = facsInter->GetHeight() + uly;
                    }
                    else {
                        lrx = facsInter->GetWidth() + facsInter->GetDrawingX();
                        lry = facsInter->GetHeight() + facsInter->GetDrawingY();
                    }
                }
            }
            Text *text = dynamic_cast<Text *> (fullSyl->FindChildByType(TEXT));
            assert(text);
            text->SetText(fullString);
            fullSyllable->AddChild(fullSyl);
            for (auto it = elements.begin(); it != elements.end(); ++it) {
                if ((*it)->GetParent() != fullSyllable && !(*it)->Is(SYL)) {
                    (*it)->MoveItselfTo(fullSyllable);
                }
            }
            doubleParent->AddChild(fullSyllable);
            Layer *layer = dynamic_cast<Layer *> (fullSyllable->GetFirstParent(LAYER));
            assert(layer);
            if (ulx >= 0 && uly >= 0 && lrx >= 0 && lry >= 0) {
                FacsimileInterface *facsInter = dynamic_cast <FacsimileInterface *> (fullSyl->GetFacsimileInterface());
                assert(facsInter);
                Zone *zone = dynamic_cast <Zone *> (facsInter->GetZone());
                assert(zone);
                assert(ulx >= 0);
                zone->SetUlx(ulx);
                assert(uly >= 0);
                zone->SetUly(uly);
                assert(lrx >= 0);
                zone->SetLrx(lrx);
                assert(lry >= 0);
                zone->SetLry(lry);
            }
            layer->ReorderByXPos();
            parent = fullSyllable;
        }
    }
    // Delete any empty parents
    for (auto it = parents.begin(); it != parents.end(); ++it) {
        Object *obj = (*it).first;
        obj->ClearRelinquishedChildren();
        if (obj->GetChildCount() == 0) {
            doubleParent->DeleteChild(obj);
        } else if (obj->GetChildCount() == obj->GetChildCount(SYL)) {
            Object *syl;
            while ((syl = obj->FindChildByType(SYL)) != NULL) {
                obj->DeleteChild(syl);
            }
            doubleParent->DeleteChild(obj);
        }
    }

    m_editInfo = parent->GetUuid();
    return true;
}

bool EditorToolkitNeume::Ungroup(std::string groupType, std::vector<std::string> elementIds)
{
    m_editInfo = "";
    Object *fparent, *sparent, *currentParent;
    Nc *firstNc, *secondNc;
    bool success1, success2;
    int ligCount = 0;
    bool firstIsSyl = false;

    //Check if you can get drawing page
    if (!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    for (auto it = elementIds.begin(); it != elementIds.end(); ++it) {
        Object *el = m_doc->GetDrawingPage()->FindChildByUuid(*it);
        //Check for ligatures and toggle them before ungrouping
        //only if the ligature is the entire selection
        if(groupType == "nc" && elementIds.size() == 2){
            Nc *nc = dynamic_cast<Nc *> (el);
            if(nc->HasLigated() && nc->GetLigated() == BOOLEAN_true){
                nc->SetLigated(BOOLEAN_false);
                ligCount++;
                if(ligCount == 1){
                    firstNc = nc;
                    assert(firstNc);
                }
                else if(ligCount == 2){
                    secondNc = nc;
                    assert(secondNc);
                    Zone *zone = new Zone();

                    Staff *staff = dynamic_cast<Staff *> (firstNc->GetFirstParent(STAFF));
                    assert(staff);
                    Facsimile *facsimile = m_doc->GetFacsimile();
                    assert(facsimile);
                    Surface *surface = dynamic_cast<Surface *>(facsimile->FindChildByType(SURFACE));
                    assert(surface);

                    const int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
                    const int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);

                    if (Att::SetNeumes(firstNc, "ligated", "false")) success1 = true;

                    int ligUlx = firstNc->GetZone()->GetUlx();
                    int ligUly = firstNc->GetZone()->GetUly();
                    int ligLrx = firstNc->GetZone()->GetLrx();
                    int ligLry = firstNc->GetZone()->GetLry();

                    zone->SetUlx(ligUlx + noteWidth);
                    zone->SetUly(ligUly + noteHeight);
                    zone->SetLrx(ligLrx + noteWidth);
                    zone->SetLry(ligLry + noteHeight);

                    Zone *origZoneUuid = secondNc->GetZone();
                    surface->DeleteChild(origZoneUuid);

                    secondNc->SetZone(zone);
                    secondNc->ResetFacsimile();
                    secondNc->SetFacs(zone->GetUuid());

                    if (Att::SetNeumes(secondNc, "ligated", "false")) success2 = true;
                    if(success1 && success2){
                        ligCount = 0;
                        firstNc = NULL;
                        secondNc = NULL;
                    }
                    else{
                        LogWarning("Unable to toggle ligature within ungroup ncs!");
                        return false;
                    }
                }
            }
        }
        if (elementIds.begin() == it || firstIsSyl){
            //if the element is a syl we want it to stay attached to the first element
            //we'll still need to initialize all the parents, thus the bool
            if (el->Is(SYL)) {
                firstIsSyl = true;
                continue;
            }
            else if (groupType == "nc"){
                fparent = el->GetFirstParent(NEUME);
                assert(fparent);
                m_editInfo = m_editInfo + fparent->GetUuid();
                sparent = fparent->GetFirstParent(SYLLABLE);
                assert(sparent);
                currentParent = dynamic_cast<Neume *>(fparent);
                assert(currentParent);
                firstIsSyl = false;
            }
            else if (groupType == "neume"){
                fparent = el->GetFirstParent(SYLLABLE);
                assert(fparent);
                m_editInfo = m_editInfo + fparent->GetUuid();
                sparent = fparent->GetFirstParent(LAYER);
                assert(sparent);
                currentParent = dynamic_cast<Syllable *>(fparent);
                assert(currentParent);
                firstIsSyl = false;

            }
            else{
                LogError("Invalid groupType for ungrouping");
                m_editInfo = "";
                return false;
            }
        }
        else {
            if (groupType == "nc") {
                Nc *nc = dynamic_cast<Nc*>(el);
                assert(nc);
                if (nc->HasLigated()) continue;
            }

            //if the element is a syl then we want to keep it attached to the first node

            if (el->Is(SYL)) {
                continue;
            }
            Object *newParent = currentParent->Clone();
            assert(newParent);
            newParent->ClearChildren();

            el->MoveItselfTo(newParent);
            fparent->ClearRelinquishedChildren();

            if (newParent->Is(SYLLABLE) && m_doc->GetOptions()->m_createDefaultSyl.GetValue()) {
                Syl *syl = new Syl();
                Text *text = new Text();
                syl->AddChild(text);
                newParent->AddChild(syl);

                //add a default bounding box if you need to
                if (m_doc->GetOptions()->m_createDefaultSylBBox.GetValue()) {
                    Zone *zone = new Zone();

                    // if it's syllable parent has position values just use those
                    FacsimileInterface *syllableFi = NULL;
                    if (syl->GetFirstParent(SYLLABLE)->GetFacsimileInterface()->HasFacs()) {
                        syllableFi = syl->GetFirstParent(SYLLABLE)->GetFacsimileInterface();
                        Zone *tempZone = dynamic_cast<Zone *>(syllableFi->GetZone());
                        zone->SetUlx(tempZone->GetUlx());
                        zone->SetUly(tempZone->GetUly());
                        zone->SetLrx(tempZone->GetLrx());
                        zone->SetLry(tempZone->GetLry());
                    }
                    // otherwise get a boundingbox that comprises all the neumes in the syllable
                    else {
                        ArrayOfObjects children;
                        InterfaceComparison comp(INTERFACE_FACSIMILE);
                        syl->GetFirstParent(SYLLABLE)->FindAllChildByComparison(&children, &comp);
                        for (auto iter2 = children.begin(); iter2 != children.end(); ++iter2) {
                            FacsimileInterface *temp = dynamic_cast<FacsimileInterface *>(*iter2);
                            assert(temp);
                            Zone *tempZone = dynamic_cast<Zone *>(temp->GetZone());
                            assert(tempzone);
                            if (temp->HasFacs()) {
                                if (syllableFi == NULL) {
                                    zone->SetUlx(tempZone->GetUlx());
                                    zone->SetUly(tempZone->GetUly());
                                    zone->SetLrx(tempZone->GetLrx());
                                    zone->SetLry(tempZone->GetLry());
                                }
                                else {
                                    if (tempZone->GetUlx() < zone->GetUlx()) {
                                        zone->SetUlx(tempZone->GetUlx());
                                    }
                                    if (tempZone->GetUly() < zone->GetUly()) {
                                        zone->SetUly(tempZone->GetUly());
                                    }
                                    if (tempZone->GetLrx() > zone->GetLrx()) {
                                        zone->SetLrx(tempZone->GetLrx());
                                    }
                                    if (tempZone->GetLry() > zone->GetLry()) {
                                        zone->SetLry(tempZone->GetLry());
                                    }
                                }
                            }
                        }
                    }

                    //make the bounding box a little bigger and lower so it's easier to edit
                    zone->SetUly(zone->GetUly() + 100);
                    zone->SetLrx(zone->GetLrx() + 100);
                    zone->SetLry(zone->GetLry() + 200);

                    assert(m_doc->GetFacsimile());
                    m_doc->GetFacsimile()->FindChildByType(SURFACE)->AddChild(zone);
                    FacsimileInterface *fi = dynamic_cast<FacsimileInterface *>((*syl).GetFacsimileInterface());
                    assert(fi);
                    fi->SetZone(zone);

                    syl->ResetFacsimile();
                    syl->SetFacs(zone->GetUuid());
                }
            }
            m_editInfo = m_editInfo + " " + newParent->GetUuid();

            sparent->AddChild(newParent);
            sparent->ReorderByXPos();
        }
    }
    return true;
}

bool EditorToolkitNeume::ChangeGroup(std::string elementId, std::string contour)
{
    m_editInfo = "";
    //Check if you can get drawing page
    if(!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }
    Neume *el = dynamic_cast<Neume *> (m_doc->GetDrawingPage()->FindChildByUuid(elementId));
    if(el == NULL){
        LogError("Unable to find neume with id %s", elementId.c_str());
        return false;
    }
    Nc *firstChild, *prevNc;

    //Get children of neume. Keep the first child and delete the others.
    ClassIdComparison ac(NC);
    ArrayOfObjects children;
    el->FindAllChildByComparison(&children, &ac);
    for (auto it = children.begin(); it != children.end(); ++it) {
        if(children.begin() == it){
            firstChild = dynamic_cast<Nc *> (*it);
        }
        else{
            el->DeleteChild(*it);
        }
    }
    //Get the coordinates of the remaining child.
    int initialUlx = firstChild->GetZone()->GetUlx();
    int initialUly = firstChild->GetZone()->GetUly();
    int initialLrx = firstChild->GetZone()->GetLrx();
    int initialLry = firstChild->GetZone()->GetLry();

    Staff *staff = dynamic_cast<Staff *> (el->GetFirstParent(STAFF));
    assert(staff);
    Facsimile *facsimile = m_doc->GetFacsimile();

    const int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
    const int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);
    prevNc = firstChild;

    //Iterate throught the contour and build the new grouping.
    for(auto it = contour.begin(); it != contour.end(); ++it) {
        Nc *newNc = new Nc();
        Zone *zone = new Zone();
        int newUlx = initialUlx + noteWidth;
        int newLrx = initialLrx + noteWidth;
        int newUly, newLry;

        newNc->SetPname(prevNc->GetPname());
        newNc->SetOct(prevNc->GetOct());

        if((*it) == 'u'){
            newUly = initialUly - noteHeight;
            newLry = initialLry - noteHeight;
            newNc->AdjustPitchByOffset(1);
        }
        else if((*it) == 'd'){
            newUly = initialUly + noteHeight;
            newLry = initialLry + noteHeight;
            newNc->AdjustPitchByOffset(-1);
        }
        else if((*it) == 's'){
            newUly = initialUly;
            newLry = initialLry;
        }
        else{
            LogMessage("Unsupported character in contour.");
            return false;
        }
        zone->SetUlx(newUlx);
        zone->SetUly(newUly);;
        zone->SetLrx(newLrx);
        zone->SetLry(newLry);

        newNc->SetZone(zone);
        newNc->SetFacs(zone->GetUuid());

        Surface *surface = dynamic_cast<Surface *>(facsimile->FindChildByType(SURFACE));
        assert(surface);
        surface->AddChild(zone);

        el->AddChild(newNc);

        initialUlx = newUlx;
        initialUly = newUly;
        initialLrx = newLrx;
        initialLry = newLry;
        prevNc = newNc;
    }
    m_editInfo = el->GetUuid();
    return true;
}

bool EditorToolkitNeume::ToggleLigature(std::vector<std::string> elementIds, std::string isLigature)
{
    m_editInfo = "";
    bool success1 = false;
    bool success2 = false;
    Facsimile *facsimile = m_doc->GetFacsimile();
    assert(facsimile);
    Surface *surface = dynamic_cast<Surface *>(facsimile->FindChildByType(SURFACE));
    assert(surface);
    std::string firstNcId = elementIds[0];
    std::string secondNcId = elementIds[1];
    //Check if you can get drawing page
    if(!m_doc->GetDrawingPage()) {
        LogError("Could not get the drawing page.");
        return false;
    }

    Nc *firstNc = dynamic_cast<Nc *> (m_doc->GetDrawingPage()->FindChildByUuid(firstNcId));
    assert(firstNc);
    Nc *secondNc = dynamic_cast<Nc *> (m_doc->GetDrawingPage()->FindChildByUuid(secondNcId));
    assert(secondNc);
    Zone *zone = new Zone();
    //set ligature to false and update zone of second Nc
    if(isLigature == "true"){
        if (Att::SetNeumes(firstNc, "ligated", "false")) success1 = true;

        int ligUlx = firstNc->GetZone()->GetUlx();
        int ligUly = firstNc->GetZone()->GetUly();
        int ligLrx = firstNc->GetZone()->GetLrx();
        int ligLry = firstNc->GetZone()->GetLry();

        Staff *staff = dynamic_cast<Staff *> (firstNc->GetFirstParent(STAFF));
        assert(staff);

        const int noteHeight = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 2);
        const int noteWidth = (int)(m_doc->GetDrawingDoubleUnit(staff->m_drawingStaffSize) / 1.4);

        zone->SetUlx(ligUlx + noteWidth);
        zone->SetUly(ligUly + noteHeight);
        zone->SetLrx(ligLrx + noteWidth);
        zone->SetLry(ligLry + noteHeight);

        secondNc->SetZone(zone);
        secondNc->ResetFacsimile();
        secondNc->SetFacs(zone->GetUuid());

        if (Att::SetNeumes(secondNc, "ligated", "false")) success2 = true;
    }
    //set ligature to true and update zones to be the same
    else if (isLigature == "false"){
        if (Att::SetNeumes(firstNc, "ligated", "true")) success1 = true;

        zone->SetUlx(firstNc->GetZone()->GetUlx());
        zone->SetUly(firstNc->GetZone()->GetUly());
        zone->SetLrx(firstNc->GetZone()->GetLrx());
        zone->SetLry(firstNc->GetZone()->GetLry());

        secondNc->SetZone(zone);
        secondNc->ResetFacsimile();
        secondNc->SetFacs(zone->GetUuid());

        if (Att::SetNeumes(secondNc, "ligated", "true")) success2 = true;
    }
    else {
        LogWarning("isLigature is invalid!");
        return false;
    }
    if (success1 && success2 && m_doc->GetType() != Facs) {
        m_doc->PrepareDrawing();
        m_doc->GetDrawingPage()->LayOut(true);
    }
    if(!(success1 && success2)){
        LogWarning("Unable to update ligature attribute");
    }

    surface->AddChild(zone);
    return success1 && success2;
}

bool EditorToolkitNeume::ParseDragAction(jsonxx::Object param, std::string *elementId, int *x, int *y)
{
    if (!param.has<jsonxx::String>("elementId")) return false;
    (*elementId) = param.get<jsonxx::String>("elementId");
    if (!param.has<jsonxx::Number>("x")) return false;
    (*x) = param.get<jsonxx::Number>("x");
    if (!param.has<jsonxx::Number>("y")) return false;
    (*y) = param.get<jsonxx::Number>("y");
    return true;
}

bool EditorToolkitNeume::ParseInsertAction(
    jsonxx::Object param, std::string *elementType, std::string *startId, std::string *endId)
{
    if (!param.has<jsonxx::String>("elementType")) return false;
    (*elementType) = param.get<jsonxx::String>("elementType");
    if (!param.has<jsonxx::String>("startid")) return false;
    (*startId) = param.get<jsonxx::String>("startid");
    if (!param.has<jsonxx::String>("endid")) return false;
    (*endId) = param.get<jsonxx::String>("endid");
    return true;
}

bool EditorToolkitNeume::ParseInsertAction(
    jsonxx::Object param, std::string *elementType, std::string *staffId, int *ulx, int *uly,
    int *lrx, int *lry, std::vector<std::pair<std::string, std::string>> *attributes)
{
    if (!param.has<jsonxx::String>("elementType")) return false;
    (*elementType) = param.get<jsonxx::String>("elementType");
    if (!param.has<jsonxx::String>("staffId")) return false;
    (*staffId) = param.get<jsonxx::String>("staffId");
    if (!param.has<jsonxx::Number>("ulx")) return false;
    (*ulx) = param.get<jsonxx::Number>("ulx");
    if (!param.has<jsonxx::Number>("uly")) return false;
    (*uly) = param.get<jsonxx::Number>("uly");
    if (param.has<jsonxx::Object>("attributes")) {
        jsonxx::Object o = param.get<jsonxx::Object>("attributes");
        auto m = o.kv_map();
        for (auto it = m.begin(); it != m.end(); it++) {
            if (o.has<jsonxx::String>(it->first)) {
                attributes->emplace(attributes->end(), it->first, o.get<jsonxx::String>(it->first));
            }
        }
    }

    if (*elementType != "staff") {
        if (!param.has<jsonxx::Number>("lrx") || !param.has<jsonxx::Number>("lry")) {
            *lrx = -1;
            *lry = -1;
        }
    }
    else {
        if (!param.has<jsonxx::Number>("lrx")) return false;
        *lrx = param.get<jsonxx::Number>("lrx");
        if (!param.has<jsonxx::Number>("lry")) return false;
        *lry = param.get<jsonxx::Number>("lry");
    }
    return true;
}

bool EditorToolkitNeume::ParseMergeAction(
    jsonxx::Object param, std::vector<std::string> *elementIds)
{
    if (!param.has<jsonxx::Array>("elementIds")) return false;
    jsonxx::Array array = param.get<jsonxx::Array>("elementIds");
    for (int i = 0; i < array.size(); i++) {
        elementIds->push_back(array.get<jsonxx::String>(i));
    }
    return true;
}

bool EditorToolkitNeume::ParseSplitAction(
    jsonxx::Object param, std::string *elementId, int *x)
{
    if (!param.has<jsonxx::String>("elementId")) {
        LogWarning("Could not parse 'elementId'.");
        return false;
    }
    (*elementId) = param.get<jsonxx::String>("elementId");

    if (!param.has<jsonxx::Number>("x")) {
        LogWarning("Could not parse 'x'.");
        return false;
    }
    (*x) = param.get<jsonxx::Number>("x");

    return true;
}

bool EditorToolkitNeume::ParseSetAction(
    jsonxx::Object param, std::string *elementId, std::string *attrType, std::string *attrValue)
{
    if (!param.has<jsonxx::String>("elementId")) {
        LogWarning("Could not parse 'elementId'");
        return false;
    }
    (*elementId) = param.get<jsonxx::String>("elementId");
    if (!param.has<jsonxx::String>("attrType")) {
        LogWarning("Could not parse 'attrType'");
        return false;
    }
    (*attrType) = param.get<jsonxx::String>("attrType");
    if (!param.has<jsonxx::String>("attrValue")) {
        LogWarning("Could not parse 'attrValue'");
        return false;
    }
    (*attrValue) = param.get<jsonxx::String>("attrValue");
    return true;
}

bool EditorToolkitNeume::ParseSetTextAction(
    jsonxx::Object param, std::string *elementId, std::string *text)
{
    if (!param.has<jsonxx::String>("elementId")) {
        LogWarning("Could not parse 'elementId'");
        return false;
    }
    *elementId = param.get<jsonxx::String>("elementId");
    if(!param.has<jsonxx::String>("text")) {
        LogWarning("Could not parse 'text'");
        return false;
    }
    *text = param.get<jsonxx::String>("text");
    return true;
}

bool EditorToolkitNeume::ParseSetClefAction(
    jsonxx::Object param, std::string *elementId, std::string *shape)
{
    if(!param.has<jsonxx::String>("elementId")) {
        LogWarning("Could not parse 'elementId'");
        return false;
    }
    *elementId = param.get<jsonxx::String>("elementId");
    if(!param.has<jsonxx::String>("shape")) {
        LogWarning("Could not parse 'shape'");
        return false;
    }
    *shape = param.get<jsonxx::String>("shape");
    return true;
}

bool EditorToolkitNeume::ParseRemoveAction(
    jsonxx::Object param, std::string *elementId)
{
    if (!param.has<jsonxx::String>("elementId")) return false;
    (*elementId) = param.get<jsonxx::String>("elementId");
    return true;
}

bool EditorToolkitNeume::ParseResizeAction(
    jsonxx::Object param, std::string *elementId, int *ulx, int *uly, int *lrx, int *lry)
{
    if(!param.has<jsonxx::String>("elementId")) return false;
    *elementId = param.get<jsonxx::String>("elementId");
    if(!param.has<jsonxx::Number>("ulx")) return false;
    *ulx = param.get<jsonxx::Number>("ulx");
    if(!param.has<jsonxx::Number>("uly")) return false;
    *uly = param.get<jsonxx::Number>("uly");
    if(!param.has<jsonxx::Number>("lrx")) return false;
    *lrx = param.get<jsonxx::Number>("lrx");
    if(!param.has<jsonxx::Number>("lry")) return false;
    *lry = param.get<jsonxx::Number>("lry");
    return true;
}

bool EditorToolkitNeume::ParseGroupAction(
    jsonxx::Object param, std::string *groupType, std::vector<std::string> *elementIds)
{
    if(!param.has<jsonxx::String>("groupType")) return false;
    (*groupType) = param.get<jsonxx::String>("groupType");
    if(!param.has<jsonxx::Array>("elementIds")) return false;
    jsonxx::Array array = param.get<jsonxx::Array>("elementIds");
    for (int i = 0; i < array.size(); i++) {
        elementIds->push_back(array.get<jsonxx::String>(i));
    }

    return true;
}

bool EditorToolkitNeume::ParseUngroupAction(
    jsonxx::Object param, std::string *groupType, std::vector<std::string> *elementIds)
{
    if(!param.has<jsonxx::String>("groupType")) return false;
    (*groupType) = param.get<jsonxx::String>("groupType");
    if(!param.has<jsonxx::Array>("elementIds")) return false;
    jsonxx::Array array = param.get<jsonxx::Array>("elementIds");
    for (int i = 0; i < array.size(); i++) {
        elementIds->push_back(array.get<jsonxx::String>(i));
    }

    return true;
}

bool EditorToolkitNeume::ParseChangeGroupAction(
    jsonxx::Object param, std::string *elementId, std::string *contour)
{
    if(!param.has<jsonxx::String>("elementId")) return false;
    (*elementId) = param.get<jsonxx::String>("elementId");
    if(!param.has<jsonxx::String>("contour")) return false;
    (*contour) = param.get<jsonxx::String>("contour");
    return true;
}

bool EditorToolkitNeume::ParseToggleLigatureAction(
    jsonxx::Object param, std::vector<std::string> *elementIds, std::string *isLigature)
{
    if(!param.has<jsonxx::Array>("elementIds")) return false;
    jsonxx::Array array = param.get<jsonxx::Array>("elementIds");
    for (int i = 0; i < array.size(); i++) {
        elementIds->push_back(array.get<jsonxx::String>(i));
    }
    if(!param.has<jsonxx::String>("isLigature")) return false;
    (*isLigature) = param.get<jsonxx::String>("isLigature");

    return true;
}

#endif
// USE_EMSCRIPTEN
}// namespace vrv
