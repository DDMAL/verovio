/////////////////////////////////////////////////////////////////////////////
// Name:        note.h
// Author:      Laurent Pugin
// Created:     2011
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#ifndef __VRV_NOTE_H__
#define __VRV_NOTE_H__

#include "atts_mensural.h"
#include "atts_shared.h"
#include "beam.h"
#include "chord.h"
#include "durationinterface.h"
#include "layerelement.h"
#include "pitchinterface.h"

namespace vrv {

class Chord;
class Slur;
class Tie;
class Verse;
class Note;
typedef std::vector<Note*> ChordCluster;
    
//----------------------------------------------------------------------------
// Note
//----------------------------------------------------------------------------

/** 
 * This class models the MEI <note> element. 
 */

// embellishments
// for the moment only the trill is implemented
// to be change to MEI Att
#define EMB_NONE 0
#define EMB_TRILL 1
#define EMB_MORDENT 2

class Note: public LayerElement, public DurationInterface, public PitchInterface,
    public AttColoration,
    public AttNoteLogMensural,
    public AttStemmed,
    public AttTiepresent
{
public:
    /**
     * @name Constructors, destructors, reset and class name methods
     * Reset method reset all attribute classes
     */
    ///@{
    Note();
    virtual ~Note();
    virtual void Reset();
    virtual std::string GetClassName( ) { return "Note"; };
    ///@}
    
     /**
      * Set the duration.
      * Because we need to perform additonal check, this method override the LayerElement::SetValue method.
      * The DurationInterface::SetDuration method has to be called explicitly.
      */
    virtual void SetValue( int value, int flag = 0 );
    
    /**
     * Comparison operator. 
     * Check if the LayerElement if a Note and compare attributes
     */
    virtual bool operator==(Object& other);
    
    /**
     * Add an element (a verse or an accid) to a note.
     * Only Verse and Accid elements will be actually added to the note.
     */
    void AddLayerElement(LayerElement *element);
    
    /**
     * @name Setter and getter for tie attribute
     */
    ///@{
    void ResetDrawingTieAttr( );
    void SetDrawingTieAttr( );
    Tie *GetDrawingTieAttr( ) { return m_drawingTieAttr; };
    ///@}
    
    /**
     * Overriding functions to return information from chord parent if 
     * note is direct child of a chord.
     */
    ///@{
    Chord* IsChordTone( );
    int GetDrawingDur( );
    bool IsClusterExtreme( ); //used to find if is the highest or lowest note in a cluster
    
    bool HasDrawingStemDir( );
    data_STEMDIRECTION GetDrawingStemDir( );
    char GetDrawingDots( );
    
    
    ///@}
    
    /**
     * Returns a single integer representing pitch and octave.
     */
    int GetDiatonicPitch( ) { return this->GetPname() + (int)this->GetOct() * 7; };
    
    //----------//
    // Functors //
    //----------//
    
    /**
     * See Object::PrepareTieAttr
     */
    virtual int PrepareTieAttr( ArrayPtrVoid params );
    
    /**
     * Functor for setting wordpos and connector ends
     * The functor is process by staff/layer/verse using an ArrayOfAttComparisons filter.
     */
    virtual int PrepareLyrics( ArrayPtrVoid params );
    
    /**
     */
    virtual int FillStaffCurrentTimeSpanning( ArrayPtrVoid params );
    
private:
    
protected:
    
    /**
     * @name Tie attributes are represented a pointers to Tie objects.
     * There is one pointer for the initial attribute (TIE_i or TIE_m).
     * The note with the initial attribute owns the Tie object and take care of deleting it
     */
    ///@{
    Tie *m_drawingTieAttr;
    ///@}
    
public:
    /** indicates if the appoggiatura is slashed (i.e. it is an acciaccatura)
     used with cueSize = true */
    bool m_acciaccatura; // To be changed to Att grace="acc"
    /** embellishment on this note **/
    unsigned int m_embellishment; // To be changed to Att
    
    /** drawing stem direction */
    data_STEMDIRECTION m_drawingStemDir;
    /** drawing stem length */
    int d_stemLen;
    
    /** flags for determining clusters in chord **/
    ChordCluster* m_cluster; //cluster this belongs to
    int m_clusterPosition; //1-indexed position in said cluster; 0 if does not have position

    
private:
    
};

} // namespace vrv
    
#endif
