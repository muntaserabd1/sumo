/****************************************************************************/
/// @file    NBEdge.cpp
/// @author  Daniel Krajzewicz
/// @author  Jakob Erdmann
/// @author  Sascha Krieg
/// @author  Michael Behrisch
/// @author  Laura Bieker
/// @date    Tue, 20 Nov 2001
/// @version $Id$
///
// Methods for the representation of a single edge
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
// Copyright (C) 2001-2015 DLR (http://www.dlr.de/) and contributors
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

#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include "NBEdgeCont.h"
#include "NBNode.h"
#include "NBNodeCont.h"
#include "NBContHelper.h"
#include "NBHelpers.h"
#include <cmath>
#include <iomanip>
#include "NBTypeCont.h"
#include <utils/geom/GeomHelper.h>
#include <utils/common/MsgHandler.h>
#include <utils/common/StringUtils.h>
#include <utils/common/ToString.h>
#include <utils/common/UtilExceptions.h>
#include <utils/common/StdDefs.h>
#include "NBEdge.h"
#include <utils/options/OptionsCont.h>
#include <utils/iodevices/OutputDevice.h>

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS

// ===========================================================================
// static members
// ===========================================================================
const SUMOReal NBEdge::UNSPECIFIED_WIDTH = -1;
const SUMOReal NBEdge::UNSPECIFIED_OFFSET = 0;
const SUMOReal NBEdge::UNSPECIFIED_SPEED = -1;

const SUMOReal NBEdge::UNSPECIFIED_SIGNAL_OFFSET = -1;
const SUMOReal NBEdge::UNSPECIFIED_LOADED_LENGTH = -1;
const SUMOReal NBEdge::ANGLE_LOOKAHEAD = 10.0;

// ===========================================================================
// method definitions
// ===========================================================================
std::string
NBEdge::Connection:: getInternalLaneID() const {
    return id + "_" + toString(internalLaneIndex);
}

/* -------------------------------------------------------------------------
 * NBEdge::ToEdgeConnectionsAdder-methods
 * ----------------------------------------------------------------------- */
void
NBEdge::ToEdgeConnectionsAdder::execute(const unsigned int lane, const unsigned int virtEdge) {
    // check
    assert(myTransitions.size() > virtEdge);
    // get the approached edge
    NBEdge* succEdge = myTransitions[virtEdge];
    std::vector<unsigned int> lanes;

    // check whether the currently regarded, approached edge has already
    //  a connection starting at the edge which is currently being build
    std::map<NBEdge*, std::vector<unsigned int> >::iterator i = myConnections.find(succEdge);
    if (i != myConnections.end()) {
        // if there were already lanes assigned, get them
        lanes = (*i).second;
    }

    // check whether the current lane was already used to connect the currently
    //  regarded approached edge
    std::vector<unsigned int>::iterator j = find(lanes.begin(), lanes.end(), lane);
    if (j == lanes.end()) {
        // if not, add it to the list
        lanes.push_back(lane);
    }
    // set information about connecting lanes
    myConnections[succEdge] = lanes;
}



/* -------------------------------------------------------------------------
 * NBEdge::MainDirections-methods
 * ----------------------------------------------------------------------- */
NBEdge::MainDirections::MainDirections(const EdgeVector& outgoing,
                                       NBEdge* parent, NBNode* to) {
    if (outgoing.size() == 0) {
        return;
    }
    // check whether the right turn has a higher priority
    assert(outgoing.size() > 0);
    if (outgoing[0]->getJunctionPriority(to) == 1) {
        myDirs.push_back(MainDirections::DIR_RIGHTMOST);
    }
    // check whether the left turn has a higher priority
    if (outgoing.back()->getJunctionPriority(to) == 1) {
        // ok, the left turn belongs to the higher priorised edges on the junction
        //  let's check, whether it has also a higher priority (lane number/speed)
        //  than the current
        EdgeVector tmp(outgoing);
        sort(tmp.begin(), tmp.end(), NBContHelper::edge_similar_direction_sorter(parent));
        if (outgoing.back()->getPriority() > tmp[0]->getPriority()) {
            myDirs.push_back(MainDirections::DIR_LEFTMOST);
        } else {
            if (outgoing.back()->getNumLanes() > tmp[0]->getNumLanes()) {
                myDirs.push_back(MainDirections::DIR_LEFTMOST);
            }
        }
    }
    // check whether the forward direction has a higher priority
    //  try to get the forward direction
    EdgeVector tmp(outgoing);
    sort(tmp.begin(), tmp.end(), NBContHelper::edge_similar_direction_sorter(parent));
    NBEdge* edge = *(tmp.begin());
    // check whether it has a higher priority and is going straight
    if (edge->getJunctionPriority(to) == 1 && to->getDirection(parent, edge) == LINKDIR_STRAIGHT) {
        myDirs.push_back(MainDirections::DIR_FORWARD);
    }
}


NBEdge::MainDirections::~MainDirections() {}


bool
NBEdge::MainDirections::empty() const {
    return myDirs.empty();
}


bool
NBEdge::MainDirections::includes(Direction d) const {
    return find(myDirs.begin(), myDirs.end(), d) != myDirs.end();
}


/* -------------------------------------------------------------------------
 * NBEdge::connections_relative_edgelane_sorter-methods
 * ----------------------------------------------------------------------- */
int
NBEdge::connections_relative_edgelane_sorter::operator()(const Connection& c1, const Connection& c2) const {
    if (c1.toEdge != c2.toEdge) {
        return NBContHelper::relative_outgoing_edge_sorter(myEdge)(c1.toEdge, c2.toEdge);
    }
    return c1.toLane < c2.toLane;
}


/* -------------------------------------------------------------------------
 * NBEdge-methods
 * ----------------------------------------------------------------------- */
NBEdge::NBEdge(const std::string& id, NBNode* from, NBNode* to,
               std::string type, SUMOReal speed, unsigned int nolanes,
               int priority, SUMOReal laneWidth, SUMOReal offset,
               const std::string& streetName,
               LaneSpreadFunction spread) :
    Named(StringUtils::convertUmlaute(id)),
    myStep(INIT),
    myType(StringUtils::convertUmlaute(type)),
    myFrom(from), myTo(to),
    myStartAngle(0), myEndAngle(0), myTotalAngle(0),
    myPriority(priority), mySpeed(speed),
    myTurnDestination(0),
    myPossibleTurnDestination(0),
    myFromJunctionPriority(-1), myToJunctionPriority(-1),
    myLaneSpreadFunction(spread), myEndOffset(offset), myLaneWidth(laneWidth),
    myLoadedLength(UNSPECIFIED_LOADED_LENGTH), myAmLeftHand(false),
    myAmInnerEdge(false), myAmMacroscopicConnector(false),
    myStreetName(streetName),
    mySignalOffset(UNSPECIFIED_SIGNAL_OFFSET) {
    init(nolanes, false);
}


NBEdge::NBEdge(const std::string& id, NBNode* from, NBNode* to,
               std::string type, SUMOReal speed, unsigned int nolanes,
               int priority, SUMOReal laneWidth, SUMOReal offset,
               PositionVector geom,
               const std::string& streetName,
               LaneSpreadFunction spread, bool tryIgnoreNodePositions) :
    Named(StringUtils::convertUmlaute(id)),
    myStep(INIT),
    myType(StringUtils::convertUmlaute(type)),
    myFrom(from), myTo(to),
    myStartAngle(0), myEndAngle(0), myTotalAngle(0),
    myPriority(priority), mySpeed(speed),
    myTurnDestination(0),
    myPossibleTurnDestination(0),
    myFromJunctionPriority(-1), myToJunctionPriority(-1),
    myGeom(geom), myLaneSpreadFunction(spread), myEndOffset(offset), myLaneWidth(laneWidth),
    myLoadedLength(UNSPECIFIED_LOADED_LENGTH), myAmLeftHand(false),
    myAmInnerEdge(false), myAmMacroscopicConnector(false),
    myStreetName(streetName),
    mySignalOffset(UNSPECIFIED_SIGNAL_OFFSET) {
    init(nolanes, tryIgnoreNodePositions);
}


NBEdge::NBEdge(const std::string& id, NBNode* from, NBNode* to, NBEdge* tpl, const PositionVector& geom, int numLanes) :
    Named(StringUtils::convertUmlaute(id)),
    myStep(INIT),
    myType(tpl->getTypeID()),
    myFrom(from), myTo(to),
    myStartAngle(0), myEndAngle(0), myTotalAngle(0),
    myPriority(tpl->getPriority()), mySpeed(tpl->getSpeed()),
    myTurnDestination(0),
    myPossibleTurnDestination(0),
    myFromJunctionPriority(-1), myToJunctionPriority(-1),
    myGeom(geom),
    myLaneSpreadFunction(tpl->getLaneSpreadFunction()),
    myEndOffset(tpl->getEndOffset()),
    myLaneWidth(tpl->getLaneWidth()),
    myLoadedLength(UNSPECIFIED_LOADED_LENGTH),
    myAmLeftHand(false),
    myAmInnerEdge(false),
    myAmMacroscopicConnector(false),
    myStreetName(tpl->getStreetName()),
    mySignalOffset(to == tpl->myTo ? tpl->mySignalOffset : UNSPECIFIED_SIGNAL_OFFSET) {
    init(numLanes > 0 ? numLanes : tpl->getNumLanes(), myGeom.size() > 0);
    for (unsigned int i = 0; i < getNumLanes(); i++) {
        const unsigned int tplIndex = MIN2(i, tpl->getNumLanes() - 1);
        setSpeed(i, tpl->getLaneSpeed(tplIndex));
        setPermissions(tpl->getPermissions(tplIndex), i);
        setLaneWidth(i, tpl->myLanes[tplIndex].width);
        if (to == tpl->myTo) {
            setEndOffset(i, tpl->myLanes[tplIndex].endOffset);
        }
    }
}


void
NBEdge::reinit(NBNode* from, NBNode* to, const std::string& type,
               SUMOReal speed, unsigned int nolanes, int priority,
               PositionVector geom, SUMOReal laneWidth, SUMOReal offset,
               const std::string& streetName,
               LaneSpreadFunction spread,
               bool tryIgnoreNodePositions) {
    if (myFrom != from) {
        myFrom->removeEdge(this, false);
    }
    if (myTo != to) {
        myTo->removeEdge(this, false);
    }
    myType = StringUtils::convertUmlaute(type);
    myFrom = from;
    myTo = to;
    myPriority = priority;
    //?myTurnDestination(0),
    //?myFromJunctionPriority(-1), myToJunctionPriority(-1),
    myGeom = geom;
    myLaneSpreadFunction = spread;
    myLoadedLength = UNSPECIFIED_LOADED_LENGTH;
    myStreetName = streetName;
    //?, myAmTurningWithAngle(0), myAmTurningOf(0),
    //?myAmInnerEdge(false), myAmMacroscopicConnector(false)

    // preserve lane-specific settings (geometry must be recomputed)
    // if new lanes are added they copy the values from the leftmost lane (if specified)
    const std::vector<Lane> oldLanes = myLanes;
    init(nolanes, tryIgnoreNodePositions);
    for (int i = 0; i < (int)nolanes; ++i) {
        PositionVector newShape = myLanes[i].shape;
        myLanes[i] = oldLanes[MIN2(i, (int)oldLanes.size() - 1)];
        myLanes[i].shape = newShape;
    }
    // however, if the new edge defaults are explicityly given, they override the old settings
    if (offset != UNSPECIFIED_OFFSET) {
        setEndOffset(-1, offset);
    }
    if (laneWidth != UNSPECIFIED_WIDTH) {
        setLaneWidth(-1, laneWidth);
    }
    if (speed != UNSPECIFIED_SPEED) {
        setSpeed(-1, speed);
    }
}


void
NBEdge::reinitNodes(NBNode* from, NBNode* to) {
    // connections may still be valid
    if (from == 0 || to == 0) {
        throw ProcessError("At least one of edge's '" + myID + "' nodes is not known.");
    }
    if (myFrom != from) {
        myFrom->removeEdge(this, false);
        myFrom = from;
        myFrom->addOutgoingEdge(this);
    }
    if (myTo != to) {
        myTo->removeEdge(this, false);
        myTo = to;
        myTo->addIncomingEdge(this);
    }
    computeAngle();
}


void
NBEdge::init(unsigned int noLanes, bool tryIgnoreNodePositions) {
    if (noLanes == 0) {
        throw ProcessError("Edge '" + myID + "' needs at least one lane.");
    }
    if (myFrom == 0 || myTo == 0) {
        throw ProcessError("At least one of edge's '" + myID + "' nodes is not known.");
    }
    // revisit geometry
    //  should have at least two points at the end...
    //  and in dome cases, the node positions must be added
    myGeom.removeDoublePoints();
    if (!tryIgnoreNodePositions || myGeom.size() < 2) {
        if (myGeom.size() == 0) {
            myGeom.push_back(myTo->getPosition());
            myGeom.push_front(myFrom->getPosition());
        } else {
            myGeom.push_back_noDoublePos(myTo->getPosition());
            myGeom.push_front_noDoublePos(myFrom->getPosition());
        }
    }
    if (myGeom.size() < 2) {
        myGeom.clear();
        myGeom.push_back(myTo->getPosition());
        myGeom.push_front(myFrom->getPosition());
    }
    if (myGeom.size() == 2 && myGeom[0] == myGeom[1]) {
        WRITE_ERROR("Edge's '" + myID + "' from- and to-node are at the same position.");
        myGeom[1].add(Position(POSITION_EPS, POSITION_EPS));
    }
    //
    myFrom->addOutgoingEdge(this);
    myTo->addIncomingEdge(this);
    // prepare container
    myLength = myFrom->getPosition().distanceTo(myTo->getPosition());
    assert(myGeom.size() >= 2);
    myLanes.clear();
    for (unsigned int i = 0; i < noLanes; i++) {
        myLanes.push_back(Lane(this));
    }
    computeLaneShapes();
    computeAngle();
}


NBEdge::~NBEdge() {}


// -----------  Applying offset
void
NBEdge::reshiftPosition(SUMOReal xoff, SUMOReal yoff) {
    myGeom.reshiftRotate(xoff, yoff, 0);
    for (unsigned int i = 0; i < myLanes.size(); i++) {
        myLanes[i].shape.reshiftRotate(xoff, yoff, 0);
    }
    computeAngle(); // update angles because they are numerically sensitive (especially where based on centroids)
}


// ----------- Edge geometry access and computation
const PositionVector
NBEdge::getInnerGeometry() const {
    PositionVector result = getGeometry();
    result.pop_front();
    result.pop_back();
    return result;
}


bool
NBEdge::hasDefaultGeometry() const {
    return myGeom.size() == 2 && hasDefaultGeometryEndpoints();
}


bool
NBEdge::hasDefaultGeometryEndpoints() const {
    return myGeom.front() == myFrom->getPosition() &&
           myGeom.back() == myTo->getPosition();
}


void
NBEdge::setGeometry(const PositionVector& s, bool inner) {
    Position begin = myGeom.front(); // may differ from node position
    Position end = myGeom.back(); // may differ from node position
    myGeom = s;
    if (inner) {
        myGeom.push_front(begin);
        myGeom.push_back(end);
    }
    computeLaneShapes();
    computeAngle();
}


void
NBEdge::computeEdgeShape() {
    for (unsigned int i = 0; i < myLanes.size(); i++) {
        PositionVector& shape = myLanes[i].shape;
        PositionVector old = shape;
        shape = startShapeAt(shape, myFrom);
        if (shape.size() >= 2) {
            shape = startShapeAt(shape.reverse(), myTo).reverse();
        }
        // sanity checks
        if (shape.length() < POSITION_EPS) {
            if (old.length() < 2 * POSITION_EPS) {
                shape = old;
            } else {
                const SUMOReal midpoint = old.length() / 2;
                // EPS*2 because otherwhise shape has only a single point
                shape = old.getSubpart(midpoint - POSITION_EPS, midpoint + POSITION_EPS);
                assert(shape.size() >= 2);
                assert(shape.length() > 0);
            }
        } else {
            // @note If the node shapes are overlapping we may get a shape which goes in the wrong direction
            // in this case the result shape should shortened
            Line lc(shape[0], shape[-1]);
            Line lo(old[0], old[-1]);
            if (135 < GeomHelper::getMinAngleDiff(lc.atan2DegreeAngle(), lo.atan2DegreeAngle())) {
                shape = shape.reverse();
                shape = shape.getSubpart(0, 2 * POSITION_EPS); // *2 because otherwhise shape has only a single point
            }
        }
    }
    // recompute edge's length as the average of lane lenghts
    SUMOReal avgLength = 0;
    for (unsigned int i = 0; i < myLanes.size(); i++) {
        assert(myLanes[i].shape.length() > 0);
        avgLength += myLanes[i].shape.length();
    }
    myLength = avgLength / (SUMOReal) myLanes.size();
    computeAngle(); // update angles using the finalized node and lane shapes
}


PositionVector
NBEdge::startShapeAt(const PositionVector& laneShape, const NBNode* startNode) const {
    const PositionVector& nodeShape = startNode->getShape();
    Line lb = laneShape.getBegLine();
    // this doesn't look reasonable @todo use lb.extrapolateFirstBy(100.0);
    lb.extrapolateBy(100.0);
    if (nodeShape.intersects(laneShape)) {
        // shape intersects directly
        std::vector<SUMOReal> pbv = laneShape.intersectsAtLengths2D(nodeShape);
        assert(pbv.size() > 0);
        SUMOReal pb = VectorHelper<SUMOReal>::maxValue(pbv);
        assert(pb >= 0);
        PositionVector ns = pb <= laneShape.length() ? laneShape.getSubpart2D(pb, laneShape.length()) : laneShape;
        ns[0].set(ns[0].x(), ns[0].y(), startNode->getPosition().z());
        return ns;
    } else if (nodeShape.intersects(lb.p1(), lb.p2())) {
        // extension of first segment intersects
        std::vector<SUMOReal> pbv = lb.intersectsAtLengths2D(nodeShape);
        assert(pbv.size() > 0);
        SUMOReal pb = VectorHelper<SUMOReal>::maxValue(pbv);
        assert(pb >= 0);
        PositionVector result = laneShape;
        result.eraseAt(0);
        Position np = lb.getPositionAtDistance2D(pb);
        result.push_front_noDoublePos(Position(np.x(), np.y(), startNode->getPosition().z()));
        return result;
        //if (result.size() >= 2) {
        //    return result;
        //} else {
        //    WRITE_WARNING(error + " (resulting shape is too short)");
        //    return laneShape;
        //}
    } else {
        // could not find proper intersection. Probably the edge is very short
        // and lies within nodeShape
        // @todo enable warning WRITE_WARNING(error + " (laneShape lies within nodeShape)");
        return laneShape;
    }
}


const PositionVector&
NBEdge::getLaneShape(unsigned int i) const {
    return myLanes[i].shape;
}


void
NBEdge::setLaneSpreadFunction(LaneSpreadFunction spread) {
    myLaneSpreadFunction = spread;
}


void
NBEdge::addGeometryPoint(int index, const Position& p) {
    myGeom.insertAt(index, p);
}


bool
NBEdge::splitGeometry(NBEdgeCont& ec, NBNodeCont& nc) {
    // check whether there any splits to perform
    if (myGeom.size() < 3) {
        return false;
    }
    // ok, split
    NBNode* newFrom = myFrom;
    NBNode* myLastNode = myTo;
    NBNode* newTo = 0;
    NBEdge* currentEdge = this;
    for (int i = 1; i < (int) myGeom.size() - 1; i++) {
        // build the node first
        if (i != (int)myGeom.size() - 2) {
            std::string nodename = myID + "_in_between#" + toString(i);
            if (!nc.insert(nodename, myGeom[i])) {
                throw ProcessError("Error on adding in-between node '" + nodename + "'.");
            }
            newTo = nc.retrieve(nodename);
        } else {
            newTo = myLastNode;
        }
        if (i == 1) {
            currentEdge->myTo->removeEdge(this);
            currentEdge->myTo = newTo;
            newTo->addIncomingEdge(currentEdge);
        } else {
            std::string edgename = myID + "[" + toString(i - 1) + "]";
            // @bug lane-specific width, speed, overall offset and restrictions are ignored
            currentEdge = new NBEdge(edgename, newFrom, newTo, myType, mySpeed, (unsigned int) myLanes.size(),
                                     myPriority, myLaneWidth, UNSPECIFIED_OFFSET, myStreetName, myLaneSpreadFunction);
            if (!ec.insert(currentEdge, true)) {
                throw ProcessError("Error on adding splitted edge '" + edgename + "'.");
            }
        }
        newFrom = newTo;
    }
    myGeom.clear();
    myGeom.push_back(myFrom->getPosition());
    myGeom.push_back(myTo->getPosition());
    myStep = INIT;
    return true;
}


void
NBEdge::reduceGeometry(const SUMOReal minDist) {
    myGeom.removeDoublePoints(minDist, true);
}


void
NBEdge::checkGeometry(const SUMOReal maxAngle, const SUMOReal minRadius, bool fix) {
    if (myGeom.size() < 3) {
        return;
    }
    //std::cout << "checking geometry of " << getID() << " geometry = " << toString(myGeom) << "\n";
    std::vector<SUMOReal> angles; // absolute segment angles
    //std::cout << "  absolute angles:";
    for (int i = 0; i < (int)myGeom.size() - 1; ++i) {
        angles.push_back(myGeom.lineAt(i).atan2DegreeAngle());
        //std::cout << " " << angles.back();
    }
    //std::cout << "\n  relative angles: ";
    for (int i = 0; i < (int)angles.size() - 1; ++i) {
        const SUMOReal relAngle = fabs(NBHelpers::relAngle(angles[i], angles[i + 1]));
        //std::cout << relAngle << " ";
        if (maxAngle > 0 && relAngle > maxAngle) {
            WRITE_WARNING("Found angle of " + toString(relAngle) + " degrees at edge " + getID() + ", segment " + toString(i));
        }
        if (relAngle < 1) {
            continue;
        }
        if (i == 0 || i == (int)angles.size() - 2) {
            const bool start = i == 0;
            const Line l = (start ? myGeom.getBegLine() : myGeom.getEndLine());
            const SUMOReal r = tan(DEG2RAD(90 - 0.5 * relAngle)) * l.length2D();
            //std::cout << (start ? "  start" : "  end") << " length=" << l.length2D() << " radius=" << r << "  ";
            if (minRadius > 0 && r < minRadius) {
                if (fix) {
                    WRITE_MESSAGE("Removing sharp turn with radius " + toString(r) + " at the " +
                                  (start ? "start" : "end") + " of edge " + getID());
                    myGeom.eraseAt(start ? 1 : i + 1);
                    checkGeometry(maxAngle, minRadius, fix);
                    return;
                } else {
                    WRITE_WARNING("Found sharp turn with radius " + toString(r) + " at the " +
                                  (start ? "start" : "end") + " of edge " + getID());
                }
            }
        }
    }
    //std::cout << "\n";
}


// ----------- Setting and getting connections
bool
NBEdge::addEdge2EdgeConnection(NBEdge* dest) {
    if (myStep == INIT_REJECT_CONNECTIONS) {
        return true;
    }
    // check whether the node was merged and now a connection between
    //  not matching edges is tried to be added
    //  This happens f.e. within the ptv VISSIM-example "Beijing"
    if (dest != 0 && myTo != dest->myFrom) {
        return false;
    }
    if (dest == 0) {
        invalidateConnections();
        myConnections.push_back(Connection(-1, dest, -1));
    } else if (find_if(myConnections.begin(), myConnections.end(), connections_toedge_finder(dest)) == myConnections.end()) {
        myConnections.push_back(Connection(-1, dest, -1));
    }
    if (myStep < EDGE2EDGES) {
        myStep = EDGE2EDGES;
    }
    return true;
}


bool
NBEdge::addLane2LaneConnection(unsigned int from, NBEdge* dest,
                               unsigned int toLane, Lane2LaneInfoType type,
                               bool mayUseSameDestination,
                               bool mayDefinitelyPass,
                               bool keepClear) {
    if (myStep == INIT_REJECT_CONNECTIONS) {
        return true;
    }
    // check whether the node was merged and now a connection between
    //  not matching edges is tried to be added
    //  This happens f.e. within the ptv VISSIM-example "Beijing"
    if (myTo != dest->myFrom) {
        return false;
    }
    if (!addEdge2EdgeConnection(dest)) {
        return false;
    }
    setConnection(from, dest, toLane, type, mayUseSameDestination, mayDefinitelyPass, keepClear);
    return true;
}


bool
NBEdge::addLane2LaneConnections(unsigned int fromLane,
                                NBEdge* dest, unsigned int toLane,
                                unsigned int no, Lane2LaneInfoType type,
                                bool invalidatePrevious,
                                bool mayDefinitelyPass) {
    if (invalidatePrevious) {
        invalidateConnections(true);
    }
    bool ok = true;
    for (unsigned int i = 0; i < no && ok; i++) {
        ok &= addLane2LaneConnection(fromLane + i, dest, toLane + i, type, false, mayDefinitelyPass);
    }
    return ok;
}


void
NBEdge::setConnection(unsigned int lane, NBEdge* destEdge,
                      unsigned int destLane, Lane2LaneInfoType type,
                      bool mayUseSameDestination,
                      bool mayDefinitelyPass,
                      bool keepClear) {
    if (myStep == INIT_REJECT_CONNECTIONS) {
        return;
    }
    // some kind of a misbehaviour which may occure when the junction's outgoing
    //  edge priorities were not properly computed, what may happen due to
    //  an incomplete or not proper input
    // what happens is that under some circumstances a single lane may set to
    //  be approached more than once by the one of our lanes.
    //  This must not be!
    // we test whether it is the case and do nothing if so - the connection
    //  will be refused
    //
    if (!mayUseSameDestination && hasConnectionTo(destEdge, destLane)) {
        return;
    }
    if (find_if(myConnections.begin(), myConnections.end(), connections_finder(lane, destEdge, destLane)) != myConnections.end()) {
        return;
    }
    if (myLanes.size() <= lane) {
        WRITE_ERROR("Could not set connection from '" + getLaneIDInsecure(lane) + "' to '" + destEdge->getLaneIDInsecure(destLane) + "'.");
        return;
    }
    if (destEdge->getNumLanes() <= destLane) {
        WRITE_ERROR("Could not set connection from '" + getLaneIDInsecure(lane) + "' to '" + destEdge->getLaneIDInsecure(destLane) + "'.");
        return;
    }
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end();) {
        if ((*i).toEdge == destEdge && ((*i).fromLane == -1 || (*i).toLane == -1)) {
            i = myConnections.erase(i);
        } else {
            ++i;
        }
    }
    myConnections.push_back(Connection(lane, destEdge, destLane));
    if (mayDefinitelyPass) {
        myConnections.back().mayDefinitelyPass = true;
    }
    myConnections.back().keepClear = keepClear;
    if (type == L2L_USER) {
        myStep = LANES2LANES_USER;
    } else {
        // check whether we have to take another look at it later
        if (type == L2L_COMPUTED) {
            // yes, the connection was set using an algorithm which requires a recheck
            myStep = LANES2LANES_RECHECK;
        } else {
            // ok, let's only not recheck it if we did no add something that has to be recheked
            if (myStep != LANES2LANES_RECHECK) {
                myStep = LANES2LANES_DONE;
            }
        }
    }
}


std::vector<NBEdge::Connection>
NBEdge::getConnectionsFromLane(unsigned int lane) const {
    std::vector<NBEdge::Connection> ret;
    for (std::vector<Connection>::const_iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if ((*i).fromLane == static_cast<int>(lane)) {
            ret.push_back(*i);
        }
    }
    return ret;
}


bool
NBEdge::hasConnectionTo(NBEdge* destEdge, unsigned int destLane, int fromLane) const {
    return destEdge != 0 && find_if(myConnections.begin(), myConnections.end(), connections_toedgelane_finder(destEdge, destLane, fromLane)) != myConnections.end();
}


bool
NBEdge::isConnectedTo(NBEdge* e) {
    if (e == myTurnDestination) {
        return true;
    }
    return
        find_if(myConnections.begin(), myConnections.end(), connections_toedge_finder(e))
        !=
        myConnections.end();

}


const EdgeVector*
NBEdge::getConnectedSorted() {
    // check whether connections exist and if not, use edges from the node
    EdgeVector outgoing;
    if (myConnections.size() == 0) {
        outgoing = myTo->getOutgoingEdges();
    } else {
        for (std::vector<Connection>::const_iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
            if (find(outgoing.begin(), outgoing.end(), (*i).toEdge) == outgoing.end()) {
                outgoing.push_back((*i).toEdge);
            }
        }
    }
    for (std::vector<Connection>::iterator it = myConnectionsToDelete.begin(); it != myConnectionsToDelete.end(); ++it) {
        if (it->fromLane < 0 && it->toLane < 0) {
            // found an edge that shall not be connected
            EdgeVector::iterator forbidden = find(outgoing.begin(), outgoing.end(), it->toEdge);
            if (forbidden != outgoing.end()) {
                outgoing.erase(forbidden);
            }
        }
    }
    // allocate the sorted container
    unsigned int size = (unsigned int) outgoing.size();
    EdgeVector* edges = new EdgeVector();
    edges->reserve(size);
    for (EdgeVector::const_iterator i = outgoing.begin(); i != outgoing.end(); i++) {
        NBEdge* outedge = *i;
        if (outedge != 0 && outedge != myTurnDestination) {
            edges->push_back(outedge);
        }
    }
    sort(edges->begin(), edges->end(), NBContHelper::relative_outgoing_edge_sorter(this));
    return edges;
}


EdgeVector
NBEdge::getConnectedEdges() const {
    EdgeVector ret;
    for (std::vector<Connection>::const_iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if (find(ret.begin(), ret.end(), (*i).toEdge) == ret.end()) {
            ret.push_back((*i).toEdge);
        }
    }
    return ret;
}


std::vector<int>
NBEdge::getConnectionLanes(NBEdge* currentOutgoing) const {
    std::vector<int> ret;
    if (currentOutgoing != myTurnDestination) {
        for (std::vector<Connection>::const_iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
            if ((*i).toEdge == currentOutgoing) {
                ret.push_back((*i).fromLane);
            }
        }
    }
    return ret;
}


void
NBEdge::sortOutgoingConnectionsByAngle() {
    sort(myConnections.begin(), myConnections.end(), connections_relative_edgelane_sorter(this));
}


void
NBEdge::sortOutgoingConnectionsByIndex() {
    sort(myConnections.begin(), myConnections.end(), connections_sorter);
}


void
NBEdge::remapConnections(const EdgeVector& incoming) {
    EdgeVector connected = getConnectedEdges();
    for (EdgeVector::const_iterator i = incoming.begin(); i != incoming.end(); i++) {
        NBEdge* inc = *i;
        // We have to do this
        inc->myStep = EDGE2EDGES;
        // add all connections
        for (EdgeVector::iterator j = connected.begin(); j != connected.end(); j++) {
            inc->addEdge2EdgeConnection(*j);
        }
        inc->removeFromConnections(this);
    }
}


void
NBEdge::removeFromConnections(NBEdge* toEdge, int fromLane, int toLane, bool tryLater) {
    // remove from "myConnections"
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end();) {
        Connection& c = *i;
        if (c.toEdge == toEdge
                && (fromLane < 0 || c.fromLane == fromLane)
                && (toLane < 0 || c.toLane == toLane)) {
            i = myConnections.erase(i);
            tryLater = false;
        } else {
            ++i;
        }
    }
    // check whether it was the turn destination
    if (myTurnDestination == toEdge && fromLane < 0) {
        myTurnDestination = 0;
    }
    if (myPossibleTurnDestination == toEdge && fromLane < 0) {
        myPossibleTurnDestination = 0;
    }
    if (tryLater) {
        myConnectionsToDelete.push_back(Connection(fromLane, toEdge, toLane));
    }
}


void
NBEdge::invalidateConnections(bool reallowSetting) {
    myTurnDestination = 0;
    myConnections.clear();
    if (reallowSetting) {
        myStep = INIT;
    } else {
        myStep = INIT_REJECT_CONNECTIONS;
    }
}


void
NBEdge::replaceInConnections(NBEdge* which, NBEdge* by, unsigned int laneOff) {
    UNUSED_PARAMETER(laneOff);
    // replace in "_connectedEdges"
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if ((*i).toEdge == which) {
            (*i).toEdge = by;
        }
    }
    // check whether it was the turn destination
    if (myTurnDestination == which) {
        myTurnDestination = by;
    }
}

void
NBEdge::replaceInConnections(NBEdge* which, const std::vector<NBEdge::Connection>& origConns) {
    std::map<int, int> laneMap;
    int minLane = -1;
    int maxLane = -1;
    // get lanes used to approach the edge to remap
    bool wasConnected = false;
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if ((*i).toEdge != which) {
            continue;
        }
        wasConnected = true;
        if ((*i).fromLane != -1) {
            int fromLane = (*i).fromLane;
            laneMap[(*i).toLane] = fromLane;
            if (minLane == -1 || minLane > fromLane) {
                minLane = fromLane;
            }
            if (maxLane == -1 || maxLane < fromLane) {
                maxLane = fromLane;
            }
        }
    }
    if (!wasConnected) {
        return;
    }
    // remove the remapped edge from connections
    removeFromConnections(which);
    // add new connections
    std::vector<NBEdge::Connection> conns = origConns;
    for (std::vector<NBEdge::Connection>::iterator i = conns.begin(); i != conns.end(); ++i) {
        if ((*i).toEdge == which) {
            continue;
        }
        int fromLane = (*i).fromLane;
        int toUse = -1;
        if (laneMap.find(fromLane) == laneMap.end()) {
            if (fromLane >= 0 && fromLane <= minLane) {
                toUse = minLane;
            }
            if (fromLane >= 0 && fromLane >= maxLane) {
                toUse = maxLane;
            }
        } else {
            toUse = laneMap[fromLane];
        }
        if (toUse == -1) {
            toUse = 0;
        }
        setConnection(toUse, (*i).toEdge, (*i).toLane, L2L_COMPUTED, false, (*i).mayDefinitelyPass);
    }
}


void
NBEdge::copyConnectionsFrom(NBEdge* src) {
    myStep = src->myStep;
    myConnections = src->myConnections;
}


bool
NBEdge::canMoveConnection(const Connection& con, unsigned int newFromLane, const bool buildCrossingsAndWalkingAreas) const {
    // only allow using newFromLane if at least 1 vClass is permitted to use
    // this connection. If the connection shall be moved to a sidewalk, only create the connection if there is no walking area
    const SVCPermissions common = (getPermissions(newFromLane) & con.toEdge->getPermissions(con.toLane));
    return (common > 0 && (!buildCrossingsAndWalkingAreas || common != SVC_PEDESTRIAN));
}


void
NBEdge::moveConnectionToLeft(unsigned int lane, const bool buildCrossingsAndWalkingAreas) {
    unsigned int index = 0;
    if (myAmLeftHand) {
        for (int i = (int) myConnections.size() - 1; i >= 0; --i) {
            if (myConnections[i].fromLane == (int)lane
                    && getTurnDestination() != myConnections[i].toEdge
                    && canMoveConnection(myConnections[i], lane + 1, buildCrossingsAndWalkingAreas)) {
                index = i;
            }
        }
    } else {
        for (unsigned int i = 0; i < myConnections.size(); ++i) {
            if (myConnections[i].fromLane == (int)(lane) && canMoveConnection(myConnections[i], lane + 1, buildCrossingsAndWalkingAreas)) {
                index = i;
            }
        }
    }
    std::vector<Connection>::iterator i = myConnections.begin() + index;
    Connection c = *i;
    myConnections.erase(i);
    setConnection(lane + 1, c.toEdge, c.toLane, L2L_VALIDATED, false);
}


void
NBEdge::moveConnectionToRight(unsigned int lane, const bool buildCrossingsAndWalkingAreas) {
    if (myAmLeftHand) {
        for (int i = (int) myConnections.size() - 1; i >= 0; --i) {
            if (myConnections[i].fromLane == (int)lane && getTurnDestination() != myConnections[i].toEdge && canMoveConnection(myConnections[i], lane - 1, buildCrossingsAndWalkingAreas)) {
                Connection c = myConnections[i];
                myConnections.erase(myConnections.begin() + i);
                setConnection(lane - 1, c.toEdge, c.toLane, L2L_VALIDATED, false);
                return;
            }
        }
    } else {
        for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
            if ((*i).fromLane == (int)lane && canMoveConnection(*i, lane - 1, buildCrossingsAndWalkingAreas)) {
                Connection c = *i;
                i = myConnections.erase(i);
                setConnection(lane - 1, c.toEdge, c.toLane, L2L_VALIDATED, false);
                return;
            }
        }
    }
}


void
NBEdge::buildInnerEdges(const NBNode& n, unsigned int noInternalNoSplits, unsigned int& linkIndex, unsigned int& splitIndex) {
    std::string innerID = ":" + n.getID();
    NBEdge* toEdge = 0;
    unsigned int edgeIndex = linkIndex;
    unsigned int internalLaneIndex = 0;
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        Connection& con = *i;
        con.haveVia = false; // reset first since this may be called multiple times
        if (con.toEdge == 0) {
            continue;
        }
        if (con.toEdge != toEdge) {
            // skip indices to keep some correspondence between edge ids and link indices:
            // internalEdgeIndex + internalLaneIndex = linkIndex
            edgeIndex = linkIndex;
            toEdge = (*i).toEdge;
            internalLaneIndex = 0;
        }
        PositionVector shape = n.computeInternalLaneShape(this, con);
        std::vector<unsigned int> foeInternalLinks;

        LinkDirection dir = n.getDirection(this, con.toEdge);
        const bool isRightTurn = (dir == LINKDIR_RIGHT || dir == LINKDIR_PARTRIGHT);
        const bool isTurn = (isRightTurn || dir == LINKDIR_LEFT || dir == LINKDIR_PARTLEFT);
        if (dir != LINKDIR_STRAIGHT && shape.length() < POSITION_EPS) {
            WRITE_WARNING("Connection '" + getID() + "_" + toString(con.fromLane) + "->" + con.toEdge->getID() + "_" + toString(con.toLane) + "' is only " + toString(shape.length()) + " short.");
        }

        // crossingPosition, list of foe link indices
        std::pair<SUMOReal, std::vector<unsigned int> > crossingPositions(-1, std::vector<unsigned int>());
        std::set<std::string> tmpFoeIncomingLanes;
        switch (dir) {
            case LINKDIR_RIGHT:
            case LINKDIR_PARTRIGHT:
            case LINKDIR_LEFT:
            case LINKDIR_PARTLEFT:
            case LINKDIR_TURN: {
                unsigned int index = 0;
                const std::vector<NBEdge*>& incoming = n.getIncomingEdges();
                for (EdgeVector::const_iterator i2 = incoming.begin(); i2 != incoming.end(); ++i2) {
                    const std::vector<Connection>& elv = (*i2)->getConnections();
                    for (std::vector<NBEdge::Connection>::const_iterator k2 = elv.begin(); k2 != elv.end(); k2++) {
                        if ((*k2).toEdge == 0) {
                            continue;
                        }
                        bool needsCont = n.needsCont(this, *i2, con, *k2);
                        // compute the crossing point
                        if (needsCont) {
                            crossingPositions.second.push_back(index);
                            const PositionVector otherShape = n.computeInternalLaneShape(*i2, *k2);
                            // vehicles are typically less wide than the lane
                            // they drive on but but bicycle lanes should be kept clear for their whole width
                            SUMOReal width2 = (*k2).toEdge->getLaneWidth((*k2).toLane);
                            if ((*k2).toEdge->getPermissions((*k2).toLane) != SVC_BICYCLE) {
                                width2 *= 0.5;
                            }
                            const SUMOReal minDV = firstIntersection(shape, otherShape, width2);
                            if (minDV < shape.length() - POSITION_EPS && minDV > POSITION_EPS) { // !!!?
                                assert(minDV >= 0);
                                if (crossingPositions.first < 0 || crossingPositions.first > minDV) {
                                    crossingPositions.first = minDV;
                                }
                            }
                        }
                        const bool rightTurnConflict = NBNode::rightTurnConflict(
                                                           this, con.toEdge, con.fromLane, (*i2), (*k2).toEdge, (*k2).fromLane);
                        // compute foe internal lanes
                        if (n.foes(this, con.toEdge, *i2, (*k2).toEdge) || rightTurnConflict) {
                            foeInternalLinks.push_back(index);
                        }
                        // compute foe incoming lanes
                        const bool signalised = hasSignalisedConnectionTo(con.toEdge);
                        if ((n.forbids(*i2, (*k2).toEdge, this, con.toEdge, signalised) || rightTurnConflict) && (needsCont || dir == LINKDIR_TURN)) {
                            tmpFoeIncomingLanes.insert((*i2)->getID() + "_" + toString((*k2).fromLane));
                        }
                        index++;
                    }
                }
                // foe pedestrian crossings
                const std::vector<NBNode::Crossing>& crossings = n.getCrossings();
                for (std::vector<NBNode::Crossing>::const_iterator it_c = crossings.begin(); it_c != crossings.end(); ++it_c) {
                    const NBNode::Crossing& crossing = *it_c;
                    for (EdgeVector::const_iterator it_e = crossing.edges.begin(); it_e != crossing.edges.end(); ++it_e) {
                        const NBEdge* edge = *it_e;
                        // compute foe internal lanes
                        if (this == edge || con.toEdge == edge) {
                            foeInternalLinks.push_back(index);
                            if (con.toEdge == edge &&
                                    ((isRightTurn && getJunctionPriority(&n) > 0) || (isTurn && n.isTLControlled()))) {
                                // build internal junctions (not for left turns at uncontrolled intersections)
                                PositionVector crossingShape = crossing.shape;
                                crossingShape.extrapolate(1.0); // sometimes shapes miss each other by a small margin
                                const SUMOReal minDV = firstIntersection(shape, crossingShape, crossing.width / 2);
                                if (minDV < shape.length() - POSITION_EPS && minDV > POSITION_EPS) {
                                    assert(minDV >= 0);
                                    if (crossingPositions.first < 0 || crossingPositions.first > minDV) {
                                        crossingPositions.first = minDV;
                                    }
                                }
                            }
                        }
                    }
                    index++;
                }

                if (dir == LINKDIR_TURN && crossingPositions.first < 0 && crossingPositions.second.size() != 0 && shape.length() > 2. * POSITION_EPS) {
                    // let turnarounds wait in the middle if no other crossing point was found and it has a sensible length
                    // (if endOffset is used, the crossing point is in the middle of the part within the junction shape)
                    crossingPositions.first = (SUMOReal)(shape.length() + getEndOffset(con.fromLane)) / 2.;
                }
            }
            break;
            default:
                break;
        }


        // @todo compute the maximum speed allowed based on angular velocity
        //  see !!! for an explanation (with a_lat_mean ~0.3)
        /*
        SUMOReal vmax = (SUMOReal) 0.3 * (SUMOReal) 9.80778 *
                        getLaneShape(con.fromLane).back().distanceTo(
                            con.toEdge->getLaneShape(con.toLane).front())
                        / (SUMOReal) 2.0 / (SUMOReal) M_PI;
        vmax = MIN2(vmax, ((getSpeed() + con.toEdge->getSpeed()) / (SUMOReal) 2.0));
        */
        SUMOReal vmax = (getSpeed() + con.toEdge->getSpeed()) / (SUMOReal) 2.0;
        //
        Position end = con.toEdge->getLaneShape(con.toLane).front();
        Position beg = getLaneShape(con.fromLane).back();

        assert(shape.size() >= 2);
        // get internal splits if any
        if (crossingPositions.first >= 0) {
            std::pair<PositionVector, PositionVector> split = shape.splitAt(crossingPositions.first);
            con.id = innerID + "_" + toString(edgeIndex);
            con.shape = split.first;
            con.foeIncomingLanes = joinToString(tmpFoeIncomingLanes, " ");
            con.foeInternalLinks = foeInternalLinks; // resolve link indices to lane ids later
            con.viaID = innerID + "_" + toString(splitIndex + noInternalNoSplits);
            ++splitIndex;
            con.viaVmax = vmax;
            con.viaShape = split.second;
            con.haveVia = true;
        } else {
            con.id = innerID + "_" + toString(edgeIndex);
            con.shape = shape;
        }
        con.vmax = vmax;
        con.internalLaneIndex = internalLaneIndex;
        ++internalLaneIndex;
        ++linkIndex;
    }
}


SUMOReal
NBEdge::firstIntersection(const PositionVector& v1, const PositionVector& v2, SUMOReal width2) {
    SUMOReal intersect = std::numeric_limits<double>::max();
    if (v2.length() < POSITION_EPS) {
        return intersect;
    }
    PositionVector v2Right = v2;
    v2Right.move2side(width2);

    PositionVector v2Left = v2;
    v2Left.move2side(-width2);

    // intersect center line of v1 with left and right border of v2
    std::vector<SUMOReal> tmp = v1.intersectsAtLengths2D(v2Right);
    if (tmp.size() > 0) {
        intersect = MIN2(intersect, tmp[0]);
    }
    tmp = v1.intersectsAtLengths2D(v2Left);
    if (tmp.size() > 0) {
        intersect = MIN2(intersect, tmp[0]);
    }
    return intersect;
}


// -----------
int
NBEdge::getJunctionPriority(const NBNode* const node) const {
    if (node == myFrom) {
        return myFromJunctionPriority;
    } else {
        return myToJunctionPriority;
    }
}


void
NBEdge::setJunctionPriority(const NBNode* const node, int prio) {
    if (node == myFrom) {
        myFromJunctionPriority = prio;
    } else {
        myToJunctionPriority = prio;
    }
}


SUMOReal
NBEdge::getAngleAtNode(const NBNode* const atNode) const {
    // myStartAngle, myEndAngle are in [0,360] and this returns results in [-180,180]
    if (atNode == myFrom) {
        return myGeom.getBegLine().atan2DegreeAngle();
    } else {
        assert(atNode == myTo);
        return myGeom.getEndLine().atan2DegreeAngle();
    }
}


SUMOReal
NBEdge::getAngleAtNodeToCenter(const NBNode* const atNode) const {
    if (atNode == myFrom) {
        SUMOReal res = myStartAngle - 180;
        if (res < 0) {
            res += 360;
        }
        return res;
    } else {
        assert(atNode == myTo);
        return myEndAngle;
    }
}


void
NBEdge::setTurningDestination(NBEdge* e, bool onlyPossible) {
    if (!onlyPossible) {
        myTurnDestination = e;
    }
    myPossibleTurnDestination = e;
}


SUMOReal
NBEdge::getLaneSpeed(unsigned int lane) const {
    return myLanes[lane].speed;
}


void
NBEdge::computeLaneShapes() {
    // vissim needs this
    if (myFrom == myTo) {
        return;
    }
    // compute lane offset, first
    std::vector<SUMOReal> offsets;
    for (unsigned int i = 0; i < myLanes.size(); ++i) {
        offsets.push_back(0);
    }
    SUMOReal offset = 0;
    for (int i = (int)myLanes.size() - 2; i >= 0; --i) {
        offset += (getLaneWidth(i) + getLaneWidth(i + 1)) / 2. + SUMO_const_laneOffset;
        offsets[i] = offset;
    }
    if (myLaneSpreadFunction == LANESPREAD_RIGHT) {
        SUMOReal laneWidth = myLanes.back().width != UNSPECIFIED_WIDTH ? myLanes.back().width : SUMO_const_laneWidth;
        offset = (laneWidth + SUMO_const_laneOffset) / 2.; // @todo: why is the lane offset counted in here?
    } else {
        SUMOReal width = 0;
        for (unsigned int i = 0; i < myLanes.size(); ++i) {
            width += getLaneWidth(i);
        }
        width += SUMO_const_laneOffset * SUMOReal(myLanes.size() - 1);
        offset = -width / 2. + getLaneWidth((int)myLanes.size() - 1) / 2.;
    }
    for (unsigned int i = 0; i < myLanes.size(); ++i) {
        offsets[i] += offset;
        if (myAmLeftHand) {
            offsets[i] *= -1.;
        }
    }

    // build the shape of each lane
    for (unsigned int i = 0; i < myLanes.size(); ++i) {
        try {
            myLanes[i].shape = computeLaneShape(i, offsets[i]);
        } catch (InvalidArgument& e) {
            WRITE_WARNING("In edge '" + getID() + "': lane shape could not be determined (" + e.what() + ")");
            myLanes[i].shape = myGeom;
        }
    }
}


PositionVector
NBEdge::computeLaneShape(unsigned int lane, SUMOReal offset) {
    PositionVector shape;
    bool haveWarned = false;
    for (int i = 0; i < (int) myGeom.size(); i++) {
        if (i == 0) {
            Position from = myGeom[i];
            Position to = myGeom[i + 1];
            std::pair<SUMOReal, SUMOReal> offsets = laneOffset(from, to, offset, false);
            shape.push_back(
                // (methode umbenennen; was heisst hier "-")
                Position(from.x() - offsets.first, from.y() - offsets.second, from.z()));
        } else if (i == static_cast<int>(myGeom.size() - 1)) {
            Position from = myGeom[i - 1];
            Position to = myGeom[i];
            std::pair<SUMOReal, SUMOReal> offsets = laneOffset(from, to, offset, false);
            shape.push_back(
                // (methode umbenennen; was heisst hier "-")
                Position(to.x() - offsets.first, to.y() - offsets.second, to.z()));
        } else {
            Position from = myGeom[i - 1];
            Position me = myGeom[i];
            Position to = myGeom[i + 1];
            std::pair<SUMOReal, SUMOReal> offsets = laneOffset(from, me, offset, false);
            std::pair<SUMOReal, SUMOReal> offsets2 = laneOffset(me, to, offset, false);
            Line l1(
                Position(from.x() - offsets.first, from.y() - offsets.second),
                Position(me.x() - offsets.first, me.y() - offsets.second));
            l1.extrapolateBy(100);
            Line l2(
                Position(me.x() - offsets2.first, me.y() - offsets2.second),
                Position(to.x() - offsets2.first, to.y() - offsets2.second));
            const SUMOReal angle = GeomHelper::getCWAngleDiff(l1.atan2DegreeAngle(), l2.atan2DegreeAngle());
            if (angle < 10. || angle > 350.) {
                shape.push_back(
                    // (methode umbenennen; was heisst hier "-")
                    Position(me.x() - offsets.first, me.y() - offsets.second, me.z()));
                continue;
            }
            l2.extrapolateBy(100);
            if (l1.intersects(l2)) {
                Position intersetion = l1.intersectsAt(l2);
                shape.push_back(Position(intersetion.x(), intersetion.y(), me.z()));
            } else {
                if (!haveWarned) {
                    WRITE_WARNING("In lane '" + getLaneID(lane) + "': Could not build shape.");
                    haveWarned = true;
                }
            }
        }
    }
    return shape;
}


/*std::pair<SUMOReal, SUMOReal>
NBEdge::laneOffset(const Position& from, const Position& to, SUMOReal laneCenterOffset) {
    return laneOffset(from, to, laneCenterOffset, myAmLeftHand);
}
*/

std::pair<SUMOReal, SUMOReal>
NBEdge::laneOffset(const Position& from, const Position& to, SUMOReal laneCenterOffset, bool leftHand) {
    std::pair<SUMOReal, SUMOReal> offsets = GeomHelper::getNormal90D_CW(from, to, laneCenterOffset);
    if (leftHand) {
        return std::pair<SUMOReal, SUMOReal>(-offsets.first, -offsets.second);
    } else {
        return std::pair<SUMOReal, SUMOReal>(offsets.first, offsets.second);
    }
}


void
NBEdge::computeAngle() {
    // taking the angle at the first might be unstable, thus we take the angle
    // at a certain distance. (To compare two edges, additional geometry
    // segments are considered to resolve ambiguities)
    const bool hasFromShape = myFrom->getShape().size() > 0;
    const bool hasToShape = myTo->getShape().size() > 0;
    Position fromCenter = (hasFromShape ? myFrom->getShape().getCentroid() : myFrom->getPosition());
    Position toCenter = (hasToShape ? myTo->getShape().getCentroid() : myTo->getPosition());
    PositionVector shape = ((hasFromShape || hasToShape) && getNumLanes() > 0 ?
                            (myLaneSpreadFunction == LANESPREAD_RIGHT ?
                             myLanes[getNumLanes() - 1].shape
                             : myLanes[getNumLanes() / 2].shape)
                                : myGeom);

    // if the junction shape is suspicious we cannot trust the angle to the centroid
    if ((hasFromShape && (myFrom->getShape().distance(shape[0]) > 2 * POSITION_EPS
                          || myFrom->getShape().around(shape[-1])))
            || (hasToShape && (myTo->getShape().distance(shape[-1]) > 2 * POSITION_EPS
                               || myTo->getShape().around(shape[0])))) {
        fromCenter = myFrom->getPosition();
        toCenter = myTo->getPosition();
        shape = myGeom;
    }

    const SUMOReal angleLookahead = MIN2(shape.length2D() / 2, ANGLE_LOOKAHEAD);
    const Position referencePosStart = shape.positionAtOffset2D(angleLookahead);
    myStartAngle = NBHelpers::angle(
                       fromCenter.x(), fromCenter.y(),
                       referencePosStart.x(), referencePosStart.y());
    const Position referencePosEnd = shape.positionAtOffset2D(shape.length() - angleLookahead);
    myEndAngle = NBHelpers::angle(
                     referencePosEnd.x(), referencePosEnd.y(),
                     toCenter.x(), toCenter.y());
    myTotalAngle = NBHelpers::angle(
                       myFrom->getPosition().x(), myFrom->getPosition().y(),
                       myTo->getPosition().x(), myTo->getPosition().y());

}


bool
NBEdge::hasPermissions() const {
    for (std::vector<Lane>::const_iterator i = myLanes.begin(); i != myLanes.end(); ++i) {
        if ((*i).permissions != SVCAll) {
            return true;
        }
    }
    return false;
}


bool
NBEdge::hasLaneSpecificPermissions() const {
    std::vector<Lane>::const_iterator i = myLanes.begin();
    SVCPermissions firstLanePermissions = i->permissions;
    i++;
    for (; i != myLanes.end(); ++i) {
        if (i->permissions != firstLanePermissions) {
            return true;
        }
    }
    return false;
}


bool
NBEdge::hasLaneSpecificWidth() const {
    for (std::vector<Lane>::const_iterator i = myLanes.begin(); i != myLanes.end(); ++i) {
        if (i->width != myLanes.begin()->width) {
            return true;
        }
    }
    return false;
}


bool
NBEdge::hasLaneSpecificSpeed() const {
    for (std::vector<Lane>::const_iterator i = myLanes.begin(); i != myLanes.end(); ++i) {
        if (i->speed != getSpeed()) {
            return true;
        }
    }
    return false;
}


bool
NBEdge::hasLaneSpecificEndOffset() const {
    for (std::vector<Lane>::const_iterator i = myLanes.begin(); i != myLanes.end(); ++i) {
        if (i->endOffset != myLanes.begin()->endOffset) {
            return true;
        }
    }
    return false;
}


bool
NBEdge::needsLaneSpecificOutput() const {
    return hasLaneSpecificPermissions() || hasLaneSpecificSpeed() || hasLaneSpecificWidth() || hasLaneSpecificEndOffset();
}



bool
NBEdge::computeEdge2Edges(bool noLeftMovers) {
    // return if this relationship has been build in previous steps or
    //  during the import
    if (myStep >= EDGE2EDGES) {
        return true;
    }
    if (myConnections.size() == 0) {
        const EdgeVector& o = myTo->getOutgoingEdges();
        for (EdgeVector::const_iterator i = o.begin(); i != o.end(); ++i) {
            if (noLeftMovers && myTo->isLeftMover(this, *i)) {
                continue;
            }
            myConnections.push_back(Connection(-1, *i, -1));
        }
    }
    myStep = EDGE2EDGES;
    return true;
}


bool
NBEdge::computeLanes2Edges(const bool buildCrossingsAndWalkingAreas) {
    // return if this relationship has been build in previous steps or
    //  during the import
    if (myStep >= LANES2EDGES) {
        return true;
    }
    assert(myStep == EDGE2EDGES);
    // get list of possible outgoing edges sorted by direction clockwise
    //  the edge in the backward direction (turnaround) is not in the list
    const EdgeVector* edges = getConnectedSorted();
    if (myConnections.size() != 0 && edges->size() == 0) {
        // dead end per definition!?
        myConnections.clear();
    } else {
        // divide the lanes on reachable edges
        divideOnEdges(edges, buildCrossingsAndWalkingAreas);
    }
    delete edges;
    myStep = LANES2EDGES;
    return true;
}


bool
NBEdge::recheckLanes(const bool buildCrossingsAndWalkingAreas) {
    std::vector<unsigned int> connNumbersPerLane(myLanes.size(), 0);
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end();) {
        if ((*i).toEdge == 0 || (*i).fromLane < 0 || (*i).toLane < 0) {
            i = myConnections.erase(i);
        } else {
            if ((*i).fromLane >= 0) {
                ++connNumbersPerLane[(*i).fromLane];
            }
            ++i;
        }
    }
    if (myStep != LANES2LANES_DONE && myStep != LANES2LANES_USER) {
        // check #1:
        // If there is a lane with no connections and any neighbour lane has
        //  more than one connections, try to move one of them.
        // This check is only done for edges which connections were assigned
        //  using the standard algorithm.
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            if (connNumbersPerLane[i] == 0 && !isForbidden(getPermissions((int)i))) {
                if (i > 0 && connNumbersPerLane[i - 1] > 1) {
                    moveConnectionToLeft(i - 1, buildCrossingsAndWalkingAreas);
                } else if (i < myLanes.size() - 1 && connNumbersPerLane[i + 1] > 1) {
                    moveConnectionToRight(i + 1, buildCrossingsAndWalkingAreas);
                }
            }
        }
    }
    // check delayed removals
    for (std::vector<Connection>::iterator it = myConnectionsToDelete.begin(); it != myConnectionsToDelete.end(); ++it) {
        removeFromConnections(it->toEdge, it->fromLane, it->toLane);
    }
    return true;
}


void
NBEdge::divideOnEdges(const EdgeVector* outgoing, const bool buildCrossingsAndWalkingAreas) {
    if (outgoing->size() == 0) {
        // we have to do this, because the turnaround may have been added before
        myConnections.clear();
        return;
    }
    // precompute edge priorities; needed as some kind of assumptions for
    //  priorities of directions (see preparePriorities)
    std::vector<unsigned int>* priorities = prepareEdgePriorities(outgoing);
    // compute the sum of priorities (needed for normalisation)
    unsigned int prioSum = computePrioritySum(priorities);
    // compute the indices of lanes that should have connections (excluding
    // forbidden lanes and pedestrian lanes that will be connected via walkingAreas)
    std::vector<int> availableLanes;
    for (int i = 0; i < (int)myLanes.size(); ++i) {
        const SVCPermissions perms = getPermissions(i);
        if ((perms == SVC_PEDESTRIAN && buildCrossingsAndWalkingAreas) || isForbidden(perms)) {
            continue;
        }
        availableLanes.push_back(i);
    }
    // compute the resulting number of lanes that should be used to
    //  reach the following edge
    const int numOutgoing = (int) outgoing->size();
    std::vector<SUMOReal> resultingLanes;
    resultingLanes.reserve(numOutgoing);
    SUMOReal sumResulting = 0.; // the sum of resulting lanes
    SUMOReal minResulting = 10000.; // the least number of lanes to reach an edge
    for (int i = 0; i < numOutgoing; i++) {
        // res will be the number of lanes which are meant to reach the
        //  current outgoing edge
        SUMOReal res =
            (SUMOReal)(*priorities)[i] *
            (SUMOReal) availableLanes.size() / (SUMOReal) prioSum;
        // do not let this number be greater than the number of available lanes
        if (res > availableLanes.size()) {
            res = (SUMOReal) availableLanes.size();
        }
        // add it to the list
        resultingLanes.push_back(res);
        sumResulting += res;
        if (minResulting > res && res > 0) {
            // prevent minResulting from becoming 0
            minResulting = res;
        }
    }
    // compute the number of virtual edges
    //  a virtual edge is used as a replacement for a real edge from now on
    //  it shall ollow to divide the existing lanes on this structure without
    //  regarding the structure of outgoing edges
    const int numVirtual = (int)(sumResulting / minResulting + 0.5);
    // compute the transition from virtual to real edges
    EdgeVector transition;
    transition.reserve(numOutgoing);
    for (int i = 0; i < numOutgoing; i++) {
        // tmpNo will be the number of connections from this edge
        //  to the next edge
        assert(i < (int)resultingLanes.size());
        const SUMOReal tmpNum = resultingLanes[i] / minResulting;
        for (SUMOReal j = 0; j < tmpNum; j++) {
            transition.push_back((*outgoing)[i]);
        }
    }
    // assign lanes to edges
    //  (conversion from virtual to real edges is done)
    ToEdgeConnectionsAdder adder(transition);
    Bresenham::compute(&adder, static_cast<unsigned int>(availableLanes.size()), numVirtual);
    const std::map<NBEdge*, std::vector<unsigned int> >& l2eConns = adder.getBuiltConnections();
    myConnections.clear();
    for (std::map<NBEdge*, std::vector<unsigned int> >::const_iterator i = l2eConns.begin(); i != l2eConns.end(); ++i) {
        const std::vector<unsigned int> lanes = (*i).second;
        for (std::vector<unsigned int>::const_iterator j = lanes.begin(); j != lanes.end(); ++j) {
            const int fromIndex = availableLanes[*j];
            if ((getPermissions(fromIndex) & (*i).first->getPermissions()) == 0) {
                // exclude connection if fromLane and toEdge have no common permissions
                continue;
            }
            if (buildCrossingsAndWalkingAreas && (getPermissions(fromIndex) & (*i).first->getPermissions()) == SVC_PEDESTRIAN) {
                // exclude connection if the only commonly permitted class are pedestrians and there is already a walkingArea
                continue;
            }
            if (myAmLeftHand) {
                myConnections.push_back(Connection(int(myLanes.size() - 1 - fromIndex), (*i).first, -1));
            } else {
                myConnections.push_back(Connection(fromIndex, (*i).first, -1));
            }
        }
    }
    delete priorities;
}


std::vector<unsigned int>*
NBEdge::prepareEdgePriorities(const EdgeVector* outgoing) {
    // copy the priorities first
    std::vector<unsigned int>* priorities = new std::vector<unsigned int>();
    if (outgoing->size() == 0) {
        return priorities;
    }
    priorities->reserve(outgoing->size());
    EdgeVector::const_iterator i;
    for (i = outgoing->begin(); i != outgoing->end(); i++) {
        int prio = (*i)->getJunctionPriority(myTo);
        assert((prio + 1) * 2 > 0);
        prio = (prio + 1) * 2;
        priorities->push_back(prio);
    }
    // when the right turning direction has not a higher priority, divide
    //  the importance by 2 due to the possibility to leave the junction
    //  faster from this lane
    MainDirections mainDirections(*outgoing, this, myTo);
    EdgeVector tmp(*outgoing);
    sort(tmp.begin(), tmp.end(), NBContHelper::edge_similar_direction_sorter(this));
    i = find(outgoing->begin(), outgoing->end(), *(tmp.begin()));
    unsigned int dist = (unsigned int) distance(outgoing->begin(), i);
    if (dist != 0 && !mainDirections.includes(MainDirections::DIR_RIGHTMOST)) {
        assert(priorities->size() > 0);
        (*priorities)[0] /= 2;
    }
    // HEURISTIC:
    // when no higher priority exists, let the forward direction be
    //  the main direction
    if (mainDirections.empty()) {
        assert(dist < priorities->size());
        (*priorities)[dist] *= 2;
    }
    if (mainDirections.includes(MainDirections::DIR_FORWARD)) {
        if (myLanes.size() > 2) {
            (*priorities)[dist] *= 2;
        } else {
            (*priorities)[dist] *= 3;
        }
    }
    // return
    return priorities;
}


unsigned int
NBEdge::computePrioritySum(std::vector<unsigned int>* priorities) {
    unsigned int sum = 0;
    for (std::vector<unsigned int>::iterator i = priorities->begin(); i != priorities->end(); i++) {
        sum += int(*i);
    }
    return sum;
}


void
NBEdge::appendTurnaround(bool noTLSControlled) {
    // do nothing if no turnaround is known
    if (myTurnDestination == 0) {
        return;
    }
    // do nothing if the destination node is controlled by a tls and no turnarounds
    //  shall be appended for such junctions
    if (noTLSControlled && myTo->isTLControlled()) {
        return;
    }
    setConnection((unsigned int)(myLanes.size() - 1), myTurnDestination, myTurnDestination->getNumLanes() - 1, L2L_VALIDATED);
}


bool
NBEdge::isTurningDirectionAt(const NBEdge* const edge) const {
    // maybe it was already set as the turning direction
    if (edge == myTurnDestination) {
        return true;
    } else if (myTurnDestination != 0) {
        // otherwise - it's not if a turning direction exists
        return false;
    }
    return edge == myPossibleTurnDestination;
}


NBNode*
NBEdge::tryGetNodeAtPosition(SUMOReal pos, SUMOReal tolerance) const {
    // return the from-node when the position is at the begin of the edge
    if (pos < tolerance) {
        return myFrom;
    }
    // return the to-node when the position is at the end of the edge
    if (pos > myLength - tolerance) {
        return myTo;
    }
    return 0;
}


void
NBEdge::moveOutgoingConnectionsFrom(NBEdge* e, unsigned int laneOff) {
    unsigned int lanes = e->getNumLanes();
    for (unsigned int i = 0; i < lanes; i++) {
        std::vector<NBEdge::Connection> elv = e->getConnectionsFromLane(i);
        for (std::vector<NBEdge::Connection>::iterator j = elv.begin(); j != elv.end(); j++) {
            NBEdge::Connection el = *j;
            assert(el.tlID == "");
            bool ok = addLane2LaneConnection(i + laneOff, el.toEdge, el.toLane, L2L_COMPUTED);
            assert(ok);
            UNUSED_PARAMETER(ok); // only used for assertion
        }
    }
}


bool
NBEdge::lanesWereAssigned() const {
    return myStep == LANES2LANES_DONE || myStep == LANES2LANES_USER;
}


SUMOReal
NBEdge::getMaxLaneOffset() {
    return (SUMOReal) SUMO_const_laneWidthAndOffset * myLanes.size();
}


bool
NBEdge::mayBeTLSControlled(int fromLane, NBEdge* toEdge, int toLane) const {
    TLSDisabledConnection tpl;
    tpl.fromLane = fromLane;
    tpl.to = toEdge;
    tpl.toLane = toLane;
    std::vector<TLSDisabledConnection>::const_iterator i = find_if(myTLSDisabledConnections.begin(), myTLSDisabledConnections.end(), tls_disable_finder(tpl));
    return i == myTLSDisabledConnections.end();
}


bool
NBEdge::setControllingTLInformation(const NBConnection& c, const std::string& tlID) {
    const int fromLane = c.getFromLane();
    NBEdge* toEdge = c.getTo();
    const int toLane = c.getToLane();
    const int tlIndex = c.getTLIndex();
    // check whether the connection was not set as not to be controled previously
    TLSDisabledConnection tpl;
    tpl.fromLane = fromLane;
    tpl.to = toEdge;
    tpl.toLane = toLane;
    std::vector<TLSDisabledConnection>::iterator i = find_if(myTLSDisabledConnections.begin(), myTLSDisabledConnections.end(), tls_disable_finder(tpl));
    if (i != myTLSDisabledConnections.end()) {
        return false;
    }

    assert(fromLane < 0 || fromLane < (int) myLanes.size());
    // try to use information about the connections if given
    if (fromLane >= 0 && toLane >= 0) {
        // find the specified connection
        std::vector<Connection>::iterator i =
            find_if(myConnections.begin(), myConnections.end(), connections_finder(fromLane, toEdge, toLane));
        // ok, we have to test this as on the removal of self-loop edges some connections
        //  will be reassigned
        if (i != myConnections.end()) {
            // get the connection
            Connection& connection = *i;
            // set the information about the tl
            connection.tlID = tlID;
            connection.tlLinkNo = tlIndex;
            return true;
        }
    }
    // if the original connection was not found, set the information for all
    //  connections
    unsigned int no = 0;
    bool hadError = false;
    for (std::vector<Connection>::iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if ((*i).toEdge != toEdge) {
            continue;
        }
        if (fromLane >= 0 && fromLane != (*i).fromLane) {
            continue;
        }
        if (toLane >= 0 && toLane != (*i).toLane) {
            continue;
        }
        if ((*i).tlID == "") {
            (*i).tlID = tlID;
            (*i).tlLinkNo = tlIndex;
            no++;
        } else {
            if ((*i).tlID != tlID && static_cast<int>((*i).tlLinkNo) == tlIndex) {
                WRITE_WARNING("The lane " + toString<int>((*i).fromLane) + " on edge " + getID() + " already had a traffic light signal.");
                hadError = true;
            }
        }
    }
    if (hadError && no == 0) {
        WRITE_WARNING("Could not set any signal of the traffic light '" + tlID + "' (unknown group)");
    }
    return true;
}


void
NBEdge::clearControllingTLInformation() {
    for (std::vector<Connection>::iterator it = myConnections.begin(); it != myConnections.end(); it++) {
        it->tlID = "";
    }
}


void
NBEdge::disableConnection4TLS(int fromLane, NBEdge* toEdge, int toLane) {
    TLSDisabledConnection c;
    c.fromLane = fromLane;
    c.to = toEdge;
    c.toLane = toLane;
    myTLSDisabledConnections.push_back(c);
}


PositionVector
NBEdge::getCWBoundaryLine(const NBNode& n) const {
    PositionVector ret;
    SUMOReal width;
    if (myFrom == (&n)) {
        // outgoing
        ret = !myAmLeftHand ? myLanes[0].shape : myLanes.back().shape;
        width = getLaneWidth(0);
    } else {
        // incoming
        ret = !myAmLeftHand ? myLanes.back().shape.reverse() : myLanes[0].shape.reverse();
        width = getLaneWidth((int)getNumLanes() - 1);
    }
    ret.move2side(width * 0.5);
    return ret;
}


PositionVector
NBEdge::getCCWBoundaryLine(const NBNode& n) const {
    PositionVector ret;
    SUMOReal width;
    if (myFrom == (&n)) {
        // outgoing
        ret = !myAmLeftHand ? myLanes.back().shape : myLanes[0].shape;
        width = getLaneWidth((int)getNumLanes() - 1);
    } else {
        // incoming
        ret = !myAmLeftHand ? myLanes[0].shape.reverse() : myLanes.back().shape.reverse();
        width = getLaneWidth(0);
    }
    ret.move2side(-width * 0.5);
    return ret;
}


bool
NBEdge::expandableBy(NBEdge* possContinuation) const {
    // ok, the number of lanes must match
    if (myLanes.size() != possContinuation->myLanes.size()) {
        return false;
    }
    // the priority, too (?)
    if (getPriority() != possContinuation->getPriority()) {
        return false;
    }
    // the speed allowed
    if (mySpeed != possContinuation->mySpeed) {
        return false;
    }
    // spreadtype should match or it will look ugly
    if (myLaneSpreadFunction != possContinuation->myLaneSpreadFunction) {
        return false;
    }
    // do not create self loops
    if (myFrom == possContinuation->myTo) {
        return false;
    }

    // the vehicle class constraints, too
    /*!!!
    if (myAllowedOnLanes!=possContinuation->myAllowedOnLanes
            ||
            myNotAllowedOnLanes!=possContinuation->myNotAllowedOnLanes) {
        return false;
    }
    */
    // also, check whether the connections - if any exit do allow to join
    //  both edges
    // This edge must have a one-to-one connection to the following lanes
    switch (myStep) {
        case INIT_REJECT_CONNECTIONS:
            break;
        case INIT:
            break;
        case EDGE2EDGES: {
            // the following edge must be connected
            const EdgeVector& conn = getConnectedEdges();
            if (find(conn.begin(), conn.end(), possContinuation)
                    == conn.end()) {

                return false;
            }
        }
        break;
        case LANES2EDGES:
        case LANES2LANES_RECHECK:
        case LANES2LANES_DONE:
        case LANES2LANES_USER: {
            // the possible continuation must be connected
            if (find_if(myConnections.begin(), myConnections.end(), connections_toedge_finder(possContinuation)) == myConnections.end()) {
                return false;
            }
            // all lanes must go to the possible continuation
            std::vector<int> conns = getConnectionLanes(possContinuation);
            if (conns.size() != myLanes.size()) {
                return false;
            }
        }
        break;
        default:
            break;
    }
    return true;
}


void
NBEdge::append(NBEdge* e) {
    // append geometry
    myGeom.append(e->myGeom);
    for (unsigned int i = 0; i < myLanes.size(); i++) {
        myLanes[i].shape.append(e->myLanes[i].shape);
    }
    // recompute length
    myLength += e->myLength;
    // copy the connections and the building step if given
    myStep = e->myStep;
    myConnections = e->myConnections;
    myTurnDestination = e->myTurnDestination;
    myPossibleTurnDestination = e->myPossibleTurnDestination;
    // set the node
    myTo = e->myTo;
    if (e->getSignalOffset() != UNSPECIFIED_SIGNAL_OFFSET) {
        mySignalOffset = e->getSignalOffset();
    } else {
        mySignalOffset += e->getLength();
    }
    computeAngle(); // myEndAngle may be different now
}


bool
NBEdge::hasSignalisedConnectionTo(const NBEdge* const e) const {
    for (std::vector<Connection>::const_iterator i = myConnections.begin(); i != myConnections.end(); ++i) {
        if ((*i).toEdge == e && (*i).tlID != "") {
            return true;
        }
    }
    return false;
}


NBEdge*
NBEdge::getTurnDestination(bool possibleDestination) const {
    if (myTurnDestination == 0 && possibleDestination) {
        return myPossibleTurnDestination;
    }
    return myTurnDestination;
}


std::string
NBEdge::getLaneID(unsigned int lane) const {
    assert(lane < myLanes.size());
    return myID + "_" + toString(lane);
}


std::string
NBEdge::getLaneIDInsecure(unsigned int lane) const {
    return myID + "_" + toString(lane);
}


bool
NBEdge::isNearEnough2BeJoined2(NBEdge* e, SUMOReal threshold) const {
    std::vector<SUMOReal> distances = myGeom.distances(e->getGeometry());
    assert(distances.size() > 0);
    return VectorHelper<SUMOReal>::maxValue(distances) < threshold;
}


void
NBEdge::incLaneNo(unsigned int by) {
    unsigned int newLaneNo = (unsigned int) myLanes.size() + by;
    while (myLanes.size() < newLaneNo) {
        myLanes.push_back(Lane(this));
    }
    // copy attributes
    if (myLanes.size() > 1) {
        myLanes.back().speed = myLanes[myLanes.size() - 2].speed;
        myLanes.back().permissions = myLanes[myLanes.size() - 2].permissions;
        myLanes.back().preferred = myLanes[myLanes.size() - 2].preferred;
        myLanes.back().endOffset = myLanes[myLanes.size() - 2].endOffset;
        myLanes.back().width = myLanes[myLanes.size() - 2].width;
    }
    computeLaneShapes();
    const EdgeVector& incs = myFrom->getIncomingEdges();
    for (EdgeVector::const_iterator i = incs.begin(); i != incs.end(); ++i) {
        (*i)->invalidateConnections(true);
    }
    invalidateConnections(true);
}


void
NBEdge::decLaneNo(unsigned int by, int dir) {
    unsigned int newLaneNo = (unsigned int) myLanes.size() - by;
    while (myLanes.size() > newLaneNo) {
        myLanes.pop_back();
    }
    computeLaneShapes();
    const EdgeVector& incs = myFrom->getIncomingEdges();
    for (EdgeVector::const_iterator i = incs.begin(); i != incs.end(); ++i) {
        (*i)->invalidateConnections(true);
    }
    if (dir == 0) {
        invalidateConnections(true);
    } else {
        const EdgeVector& outs = myTo->getOutgoingEdges();
        assert(outs.size() == 1);
        NBEdge* out = outs[0];
        if (dir < 0) {
            removeFromConnections(out, 0);
        } else {
            removeFromConnections(out, (int) myLanes.size());
        }
    }
}


void
NBEdge::markAsInLane2LaneState() {
    assert(myTo->getOutgoingEdges().size() == 0);
    myStep = LANES2LANES_DONE;
}


void
NBEdge::allowVehicleClass(int lane, SUMOVehicleClass vclass) {
    if (lane < 0) { // all lanes are meant...
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            allowVehicleClass((int) i, vclass);
        }
    } else {
        assert(lane < (int) myLanes.size());
        myLanes[lane].permissions |= vclass;
    }
}


void
NBEdge::disallowVehicleClass(int lane, SUMOVehicleClass vclass) {
    if (lane < 0) { // all lanes are meant...
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            disallowVehicleClass((int) i, vclass);
        }
    } else {
        assert(lane < (int) myLanes.size());
        myLanes[lane].permissions &= ~vclass;
    }
}


void
NBEdge::preferVehicleClass(int lane, SUMOVehicleClass vclass) {
    if (lane < 0) { // all lanes are meant...
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            allowVehicleClass((int) i, vclass);
        }
    } else {
        assert(lane < (int) myLanes.size());
        myLanes[lane].preferred |= vclass;
    }
}


void
NBEdge::setLaneWidth(int lane, SUMOReal width) {
    if (lane < 0) {
        // all lanes are meant...
        myLaneWidth = width;
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            // ... do it for each lane
            setLaneWidth((int) i, width);
        }
        return;
    }
    assert(lane < (int) myLanes.size());
    myLanes[lane].width = width;
}


SUMOReal
NBEdge::getLaneWidth(int lane) const {
    return myLanes[lane].width != UNSPECIFIED_WIDTH
           ? myLanes[lane].width
           : getLaneWidth() != UNSPECIFIED_WIDTH ? getLaneWidth() : SUMO_const_laneWidth;
}


SUMOReal
NBEdge::getTotalWidth() const {
    SUMOReal result = 0;
    for (unsigned int i = 0; i < myLanes.size(); i++) {
        result += getLaneWidth(i);
    }
    return result;
}

SUMOReal
NBEdge::getEndOffset(int lane) const {
    return myLanes[lane].endOffset != UNSPECIFIED_OFFSET ? myLanes[lane].endOffset : getEndOffset();
}


void
NBEdge::setEndOffset(int lane, SUMOReal offset) {
    if (lane < 0) {
        // all lanes are meant...
        myEndOffset = offset;
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            // ... do it for each lane
            setEndOffset((int) i, offset);
        }
        return;
    }
    assert(lane < (int) myLanes.size());
    myLanes[lane].endOffset = offset;
}


void
NBEdge::setSpeed(int lane, SUMOReal speed) {
    if (lane < 0) {
        // all lanes are meant...
        mySpeed = speed;
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            // ... do it for each lane
            setSpeed((int) i, speed);
        }
        return;
    }
    assert(lane < (int) myLanes.size());
    myLanes[lane].speed = speed;
}


void
NBEdge::setPermissions(SVCPermissions permissions, int lane) {
    if (lane < 0) {
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            // ... do it for each lane
            setPermissions(permissions, i);
        }
    } else {
        assert(lane < (int) myLanes.size());
        myLanes[lane].permissions = permissions;
    }
}


void
NBEdge::setPreferredVehicleClass(SVCPermissions permissions, int lane) {
    if (lane < 0) {
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            // ... do it for each lane
            setPreferredVehicleClass(permissions, i);
        }
    } else {
        assert(lane < (int) myLanes.size());
        myLanes[lane].preferred = permissions;
    }
}


SVCPermissions
NBEdge::getPermissions(int lane) const {
    if (lane < 0) {
        SVCPermissions result = 0;
        for (unsigned int i = 0; i < myLanes.size(); i++) {
            result |= getPermissions(i);
        }
        return result;
    } else {
        assert(lane < (int) myLanes.size());
        return myLanes[lane].permissions;
    }
}


void
NBEdge::setLoadedLength(SUMOReal val) {
    myLoadedLength = val;
}


void
NBEdge::dismissVehicleClassInformation() {
    for (std::vector<Lane>::iterator i = myLanes.begin(); i != myLanes.end(); ++i) {
        (*i).permissions = SVCAll;
        (*i).preferred = 0;
    }
}


bool
NBEdge::connections_sorter(const Connection& c1, const Connection& c2) {
    if (c1.fromLane != c2.fromLane) {
        return c1.fromLane < c2.fromLane;
    }
    if (c1.toEdge != c2.toEdge) {
        return false; // do not change ordering among toEdges as this is determined by angle in an earlier step
    }
    return c1.toLane < c2.toLane;
}


int
NBEdge::getFirstNonPedestrianLaneIndex(int direction, bool exclusive) const {
    assert(direction == NBNode::FORWARD || direction == NBNode::BACKWARD);
    const int start = (direction == NBNode::FORWARD ? 0 : (int)myLanes.size() - 1);
    const int end = (direction == NBNode::FORWARD ? (int)myLanes.size() : - 1);
    for (int i = start; i != end; i += direction) {
        // SVCAll, does not count as a sidewalk, green verges (permissions = 0) do not count as road
        // in the exclusive case, lanes that allow pedestrians along with any other class also count as road
        if ((exclusive && myLanes[i].permissions != SVC_PEDESTRIAN && myLanes[i].permissions != 0)
                || (myLanes[i].permissions == SVCAll || ((myLanes[i].permissions & SVC_PEDESTRIAN) == 0 && myLanes[i].permissions != 0))) {
            return i;
        }
    }
    return -1;
}


SUMOReal
NBEdge::getCrossingAngle(NBNode* node) {
    SUMOReal angle = getAngleAtNode(node) + (getFromNode() == node ? 180.0 : 0.0);
    if (angle < 0) {
        angle += 360.0;
    }
    if (angle >= 360) {
        angle -= 360.0;
    }
    if (gDebugFlag1) {
        std::cout << getID() << " angle=" << getAngleAtNode(node) << " convAngle=" << angle << "\n";
    }
    return angle;
}


NBEdge::Lane
NBEdge::getFirstNonPedestrianLane(int direction) const {
    int index = getFirstNonPedestrianLaneIndex(direction);
    if (index < 0) {
        throw ProcessError("Edge " + getID() + " allows pedestrians on all lanes");
    }
    return myLanes[index];
}

void
NBEdge::addSidewalk(SUMOReal width) {
    if (myLanes[0].permissions == SVC_PEDESTRIAN) {
        WRITE_WARNING("Edge '" + getID() + "' already has a sidewalk. Not adding another one.");
        return;
    }
    if (myLaneSpreadFunction == LANESPREAD_CENTER) {
        myGeom.move2side(width / 2);
    }
    // disallow pedestrians on all lanes to ensure that sidewalks are used and
    // crossings can be guessed
    disallowVehicleClass(-1, SVC_PEDESTRIAN);
    // add new lane
    myLanes.insert(myLanes.begin(), Lane(this));
    myLanes[0].permissions = SVC_PEDESTRIAN;
    myLanes[0].width = width;
    // shift outgoing connections to the left
    for (std::vector<Connection>::iterator it = myConnections.begin(); it != myConnections.end(); ++it) {
        Connection& c = *it;
        if (c.fromLane >= 0) {
            c.fromLane += 1;
        }
    }
    // shift incoming connections to the left
    const EdgeVector& incoming = myFrom->getIncomingEdges();
    for (EdgeVector::const_iterator it = incoming.begin(); it != incoming.end(); ++it) {
        (*it)->shiftToLanesToEdge(this, 1);
    }
    myFrom->shiftTLConnectionLaneIndex(this, 1);
    myTo->shiftTLConnectionLaneIndex(this, 1);
    computeLaneShapes();
}


void
NBEdge::shiftToLanesToEdge(NBEdge* to, unsigned int laneOff) {
    /// XXX could we repurpose the function replaceInConnections ?
    for (std::vector<Connection>::iterator it = myConnections.begin(); it != myConnections.end(); ++it) {
        if ((*it).toEdge == to && (*it).toLane >= 0) {
            (*it).toLane += laneOff;
        }
    }
}


void
NBEdge::shiftPositionAtNode(NBNode* node, NBEdge* other) {
    if (myLaneSpreadFunction == LANESPREAD_CENTER && !isRailway(getPermissions())) {
        const int i = (node == myTo ? -1 : 0);
        const int i2 = (node == myTo ? 0 : -1);
        const SUMOReal dist = myGeom[i].distanceTo2D(node->getPosition());
        const SUMOReal neededOffset = (getTotalWidth() + getNumLanes() * SUMO_const_laneOffset) / 2;
        const SUMOReal dist2 = MIN2(myGeom.distance(other->getGeometry()[i2]),
                other->getGeometry().distance(myGeom[i]));
        const SUMOReal neededOffset2 = neededOffset + (other->getTotalWidth() + other->getNumLanes() * SUMO_const_laneOffset) / 2;
        if (dist < neededOffset && dist2 < neededOffset2) {
            PositionVector tmp = myGeom;
            // @note this doesn't work well for vissim networks
            //tmp.move2side(MIN2(neededOffset - dist, neededOffset2 - dist2));
            tmp.move2side(neededOffset - dist);
            myGeom[i] = tmp[i];
        }
    }
}

/****************************************************************************/
