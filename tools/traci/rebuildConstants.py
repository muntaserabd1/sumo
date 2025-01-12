#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
@file    rebuildConstants.py
@author  Daniel Krajzewicz
@author  Michael Behrisch
@date    2009-07-24
@version $Id$

This script extracts definitions from <SUMO>/src/traci-server/TraCIConstants.h
 and builds an according constants definition python file "constants.py".

SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
Copyright (C) 2009-2015 DLR (http://www.dlr.de/) and contributors

This file is part of SUMO.
SUMO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.
"""

from __future__ import print_function
import os
import sys
import datetime
from optparse import OptionParser

dirname = os.path.dirname(__file__)
optParser = OptionParser()
optParser.add_option("-j", "--java", action="store_true",
                     default=False, help="generate Java output")
optParser.add_option("-o", "--output", default=os.path.join(dirname, "constants.py"),
                     help="File to save constants into", metavar="FILE")
(options, args) = optParser.parse_args()


fdo = open(options.output, "w")
if options.java:
    print("/**", file=fdo)
else:
    print('"""', file=fdo)
print("""@file    %s
@author  generated by "%s"
@date    %s
@version $Id$

This script contains TraCI constant definitions from <SUMO_HOME>/src/traci-server/TraCIConstants.h.

SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
Copyright (C) 2009-2015 DLR (http://www.dlr.de/) and contributors

This file is part of SUMO.
SUMO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.""" % (os.path.basename(options.output), os.path.basename(__file__), datetime.datetime.now()), file=fdo)

if options.java:
    print("*/\n", file=fdo)
else:
    print('"""\n', file=fdo)

fdi = open(
    os.path.join(dirname, "..", "..", "src", "traci-server", "TraCIConstants.h"))
started = False
for line in fdi:
    if started:
        if line.find("#endif") >= 0:
            started = False
            if options.java:
                fdo.write("}")
            continue
        if line.find("#define ") >= 0:
            vals = line.split(" ")
            if options.java:
                line = "    public static final int " + \
                    vals[1] + " = " + vals[2].strip() + ";\n"
            else:
                line = vals[1] + " = " + vals[2]
        if options.java:
            line = line.replace("//", "    //")
        else:
            line = line.replace("//", "#")
        fdo.write(line)
    if line.find("#define TRACICONSTANTS_H") >= 0:
        started = True
        if options.java:
            fdo.write("public class TraCIConstants {")
fdi.close()
fdo.close()
