/****************************************************************************/
/// @file    MFXMenuHeader.cpp
/// @author  Daniel Krajzewicz
/// @date    2004-07-02
/// @version $Id$
///
// missing_desc
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
// Copyright (C) 2004-2015 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include "MFXMenuHeader.h"

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS

MFXMenuHeader::MFXMenuHeader(FXComposite* p, FXFont* fnt,
                             const FXString& text,
                             FXIcon* ic, FXObject* tgt,
                             FXSelector sel, FXuint opts)
    : FXMenuCommand(p, text, ic, tgt, sel, opts) {
    setFont(fnt);
}


MFXMenuHeader::~MFXMenuHeader() {}



/****************************************************************************/

