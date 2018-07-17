/////////////////////////////////////////////////////////////////////////////
// Name:        facsimileinterface.cpp
// Author:      Juliette Regimbal 
// Created:     2018
// Copyright (c) Authors and others. All rights reserved.
/////////////////////////////////////////////////////////////////////////////

#include "facsimileinterface.h"

//---------------------------------------------------------------------------

#include <assert.h>

//---------------------------------------------------------------------------

#include "facsimile.h"
#include "view.h"
#include "vrv.h"

namespace vrv {

FacsimileInterface::FacsimileInterface() : Interface(), AttFacsimile()
{
    RegisterInterfaceAttClass(ATT_FACSIMILE);
    Reset();
}

FacsimileInterface::~FacsimileInterface() {}

void FacsimileInterface::Reset()
{
    ResetFacsimile();
    this->SetZone(nullptr);
}

int FacsimileInterface::GetDrawingX() const
{
    assert(m_zone);
    return m_zone->GetUlx();
}

int FacsimileInterface::GetDrawingY() const
{
    assert(m_zone);
    int y = ( m_zone->GetLogicalUly());
    return y;
}

int FacsimileInterface::GetWidth() const
{
    assert(m_zone);
    return m_zone->GetLrx() - m_zone->GetUlx();
}

int FacsimileInterface::GetSurfaceY() const
{
    assert(m_zone);
    Surface *surface = dynamic_cast<Surface *>(m_zone->GetFirstParent(SURFACE));
    assert(surface);
    if (surface->HasLry()) {
        return surface->GetLry();
    }
    else {
        return surface->GetMaxY();
    }
}
}
