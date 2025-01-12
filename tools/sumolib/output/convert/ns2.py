"""
@file    ns2.py
@author  Daniel Krajzewicz
@author  Jakob Erdmann
@author  Michael Behrisch
@date    2013-01-15
@version $Id$

This module includes functions for converting SUMO's fcd-output into
data files read by ns2.

SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
Copyright (C) 2013-2015 DLR (http://www.dlr.de/) and contributors

This file is part of SUMO.
SUMO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.
"""
from __future__ import print_function
import math
import datetime
import sumolib.output
import sumolib.net


def fcd2ns2mobility(inpFCD, outSTRM, further):
    vIDm = sumolib._Running(further["orig-ids"], True)
    begin = -1
    end = None
    area = [None, None, None, None]
    vehInfo = {}
    removed = set()
    ignoring = set()
    for timestep in inpFCD:
        if begin < 0:
            begin = timestep.time
        end = timestep.time
        seen = set()
        if not timestep.vehicle:
            _writeMissing(timestep.time, vIDm, seen, vehInfo, removed)
            continue
        for v in timestep.vehicle:
            if v.id in ignoring:
                continue
            if v.id in removed:
                print(
                    "Warning: vehicle %s reappeared after being gone and will be ignored" % v.id)
                ignoring.add(v.id)
                continue

            seen.add(v.id)
            if not vIDm.k(v.id):
                nid = vIDm.g(v.id)
                if outSTRM:
                    print("$node_(%s) set X_ %s" % (nid, v.x), file=outSTRM)
                    print("$node_(%s) set Y_ %s" % (nid, v.y), file=outSTRM)
                    print("$node_(%s) set Z_ %s" % (nid, 0), file=outSTRM)
                vehInfo[v.id] = [nid, timestep.time, 0]
            nid = vIDm.g(v.id)
            if outSTRM:
                print('$ns_ at %s "$node_(%s) setdest %s %s %s"' %
                      (timestep.time, nid, v.x, v.y, v.speed), file=outSTRM)
            if not area[0]:
                area[0] = v.x
                area[1] = v.y
                area[2] = v.x
                area[3] = v.y
            area[0] = min(area[0], v.x)
            area[1] = min(area[1], v.y)
            area[2] = max(area[2], v.x)
            area[3] = max(area[3], v.y)
        _writeMissing(timestep.time, vIDm, seen, vehInfo, removed)
    return vIDm, vehInfo, begin, end, area


def writeNS2activity(outSTRM, vehInfo):
    for v in vehInfo:
        i = vehInfo[v]
        print('$ns_ at %s "$g(%s) start"; # SUMO-ID: %s' %
              (i[1], i[0], v), file=outSTRM)
        print('$ns_ at %s "$g(%s) stop"; # SUMO-ID: %s' %
              (i[2], i[0], v), file=outSTRM)


def writeNS2config(outSTRM, vehInfo, ns2activityfile, ns2mobilityfile, begin, end, area):
    print("# set number of nodes\nset opt(nn) %s\n" %
          len(vehInfo), file=outSTRM)
    if ns2activityfile:
        print("# set activity file\nset opt(af) $opt(config-path)\nappend opt(af) /%s\n" %
              ns2activityfile, file=outSTRM)
    if ns2mobilityfile:
        print("# set mobility file\nset opt(mf) $opt(config-path)\nappend opt(mf) /%s\n" %
              ns2mobilityfile, file=outSTRM)
    xmin = area[0]
    ymin = area[1]
    xmax = area[2]
    ymax = area[3]
    print("# set start/stop time\nset opt(start) %s\nset opt(stop) %s\n" %
          (begin, end), file=outSTRM)
    print("# set floor size\nset opt(x) %s\nset opt(y) %s\nset opt(min-x) %s\nset opt(min-y) %s\n" %
          (xmax, ymax, xmin, ymin), file=outSTRM)


def _writeMissing(t, vIDm, seen, vehInfo, removed):
    toDel = []
    for v in vIDm._m:
        if v in seen:
            continue
        nid = vIDm.g(v)
        vehInfo[v][2] = t
        toDel.append(v)
        removed.add(v)
    for v in toDel:
        vIDm.d(v)
