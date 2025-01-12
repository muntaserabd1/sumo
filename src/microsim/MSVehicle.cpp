/****************************************************************************/
/// @file    MSVehicle.cpp
/// @author  Christian Roessel
/// @author  Jakob Erdmann
/// @author  Bjoern Hendriks
/// @author  Daniel Krajzewicz
/// @author  Thimor Bohn
/// @author  Friedemann Wesner
/// @author  Laura Bieker
/// @author  Clemens Honomichl
/// @author  Michael Behrisch
/// @author  Axel Wegener
/// @author  Christoph Sommer
/// @date    Mon, 05 Mar 2001
/// @version $Id$
///
// Representation of a vehicle in the micro simulation
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

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <utils/common/ToString.h>
#include <utils/common/FileHelpers.h>
#include <utils/vehicle/DijkstraRouterTT.h>
#include <utils/common/RandHelper.h>
#include <utils/emissions/PollutantsInterface.h>
#include <utils/emissions/HelpersHarmonoise.h>
#include <utils/common/StringUtils.h>
#include <utils/common/StdDefs.h>
#include <utils/geom/GeomHelper.h>
#include <utils/geom/Line.h>
#include <utils/iodevices/OutputDevice.h>
#include <utils/iodevices/BinaryInputDevice.h>
#include <utils/xml/SUMOSAXAttributes.h>
#include <microsim/MSVehicleControl.h>
#include <microsim/MSVehicleTransfer.h>
#include <microsim/MSGlobals.h>
#include "MSStoppingPlace.h"
#include "devices/MSDevice_Person.h"
#include "devices/MSDevice_Container.h"
#include "MSEdgeWeightsStorage.h"
#include <microsim/lcmodels/MSAbstractLaneChangeModel.h>
#include "MSMoveReminder.h"
#include <microsim/pedestrians/MSPerson.h>
#include "MSPersonControl.h"
#include "MSContainer.h"
#include "MSContainerControl.h"
#include "MSLane.h"
#include "MSVehicle.h"
#include "MSEdge.h"
#include "MSVehicleType.h"
#include "MSNet.h"
#include "MSRoute.h"
#include "MSLinkCont.h"

#ifdef HAVE_INTERNAL
#include <mesosim/MESegment.h>
#include <mesosim/MELoop.h>
#include "MSGlobals.h"
#endif

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS

// enable here and in utils/gui/globjects/GUIGLObjectPopupMenu.cpp
//#define DEBUG_VEHICLE_GUI_SELECTION 1

#define BUS_STOP_OFFSET 0.5

#define CRLL_LOOK_AHEAD 5

// @todo Calibrate with real-world values / make configurable
#define DIST_TO_STOPLINE_EXPECT_PRIORITY 1.0

// ===========================================================================
// static value definitions
// ===========================================================================
std::vector<MSLane*> MSVehicle::myEmptyLaneVector;


// ===========================================================================
// method definitions
// ===========================================================================
/* -------------------------------------------------------------------------
 * methods of MSVehicle::State
 * ----------------------------------------------------------------------- */
MSVehicle::State::State(const State& state) {
    myPos = state.myPos;
    mySpeed = state.mySpeed;
}


MSVehicle::State&
MSVehicle::State::operator=(const State& state) {
    myPos   = state.myPos;
    mySpeed = state.mySpeed;
    return *this;
}


bool
MSVehicle::State::operator!=(const State& state) {
    return (myPos   != state.myPos ||
            mySpeed != state.mySpeed);
}


SUMOReal
MSVehicle::State::pos() const {
    return myPos;
}


MSVehicle::State::State(SUMOReal pos, SUMOReal speed) :
    myPos(pos), mySpeed(speed) {}


/* -------------------------------------------------------------------------
 * methods of MSVehicle::Influencer
 * ----------------------------------------------------------------------- */
#ifndef NO_TRACI
MSVehicle::Influencer::Influencer() :
    mySpeedAdaptationStarted(true),
    myConsiderSafeVelocity(true),
    myConsiderMaxAcceleration(true),
    myConsiderMaxDeceleration(true),
    myRespectJunctionPriority(true),
    myEmergencyBrakeRedLight(true),
    myAmVTDControlled(false),
    myLastVTDAccess(-TIME2STEPS(20)),
    myStrategicLC(LC_NOCONFLICT),
    myCooperativeLC(LC_NOCONFLICT),
    mySpeedGainLC(LC_NOCONFLICT),
    myRightDriveLC(LC_NOCONFLICT),
    myTraciLaneChangePriority(LCP_URGENT)
{}


MSVehicle::Influencer::~Influencer() {}


void
MSVehicle::Influencer::setSpeedTimeLine(const std::vector<std::pair<SUMOTime, SUMOReal> >& speedTimeLine) {
    mySpeedAdaptationStarted = true;
    mySpeedTimeLine = speedTimeLine;
}


void
MSVehicle::Influencer::setLaneTimeLine(const std::vector<std::pair<SUMOTime, unsigned int> >& laneTimeLine) {
    myLaneTimeLine = laneTimeLine;
}


SUMOReal
MSVehicle::Influencer::influenceSpeed(SUMOTime currentTime, SUMOReal speed, SUMOReal vSafe, SUMOReal vMin, SUMOReal vMax) {
    // keep original speed
    myOriginalSpeed = speed;
    // remove leading commands which are no longer valid
    while (mySpeedTimeLine.size() == 1 || (mySpeedTimeLine.size() > 1 && currentTime > mySpeedTimeLine[1].first)) {
        mySpeedTimeLine.erase(mySpeedTimeLine.begin());
    }
    // do nothing if the time line does not apply for the current time
    if (mySpeedTimeLine.size() < 2 || currentTime < mySpeedTimeLine[0].first) {
        return speed;
    }
    // compute and set new speed
    if (!mySpeedAdaptationStarted) {
        mySpeedTimeLine[0].second = speed;
        mySpeedAdaptationStarted = true;
    }
    currentTime += DELTA_T;
    const SUMOReal td = STEPS2TIME(currentTime - mySpeedTimeLine[0].first) / STEPS2TIME(mySpeedTimeLine[1].first + DELTA_T - mySpeedTimeLine[0].first);
    speed = mySpeedTimeLine[0].second - (mySpeedTimeLine[0].second - mySpeedTimeLine[1].second) * td;
    if (myConsiderSafeVelocity) {
        speed = MIN2(speed, vSafe);
    }
    if (myConsiderMaxAcceleration) {
        speed = MIN2(speed, vMax);
    }
    if (myConsiderMaxDeceleration) {
        speed = MAX2(speed, vMin);
    }
    return speed;
}


int
MSVehicle::Influencer::influenceChangeDecision(const SUMOTime currentTime, const MSEdge& currentEdge, const unsigned int currentLaneIndex, int state) {
    // remove leading commands which are no longer valid
    while (myLaneTimeLine.size() == 1 || (myLaneTimeLine.size() > 1 && currentTime > myLaneTimeLine[1].first)) {
        myLaneTimeLine.erase(myLaneTimeLine.begin());
    }
    ChangeRequest changeRequest = REQUEST_NONE;
    // do nothing if the time line does not apply for the current time
    if (myLaneTimeLine.size() >= 2 && currentTime >= myLaneTimeLine[0].first) {
        const unsigned int destinationLaneIndex = myLaneTimeLine[1].second;
        if (destinationLaneIndex < (unsigned int)currentEdge.getLanes().size()) {
            if (currentLaneIndex > destinationLaneIndex) {
                changeRequest = REQUEST_RIGHT;
            } else if (currentLaneIndex < destinationLaneIndex) {
                changeRequest = REQUEST_LEFT;
            } else {
                changeRequest = REQUEST_HOLD;
            }
        }
    }
    // check whether the current reason shall be canceled / overridden
    if ((state & LCA_WANTS_LANECHANGE_OR_STAY) != 0) {
        // flags for the current reason
        LaneChangeMode mode = LC_NEVER;
        if ((state & LCA_STRATEGIC) != 0) {
            mode = myStrategicLC;
        } else if ((state & LCA_COOPERATIVE) != 0) {
            mode = myCooperativeLC;
        } else if ((state & LCA_SPEEDGAIN) != 0) {
            mode = mySpeedGainLC;
        } else if ((state & LCA_KEEPRIGHT) != 0) {
            mode = myRightDriveLC;
        } else if ((state & LCA_TRACI) != 0) {
            mode = LC_NEVER;
        } else {
            WRITE_WARNING("Lane change model did not provide a reason for changing (state=" + toString(state) + ", time=" + time2string(currentTime) + "\n");
        }
        if (mode == LC_NEVER) {
            // cancel all lcModel requests
            state &= ~LCA_WANTS_LANECHANGE_OR_STAY;
            state &= ~LCA_URGENT;
        } else if (mode == LC_NOCONFLICT && changeRequest != REQUEST_NONE) {
            if (
                ((state & LCA_LEFT) != 0 && changeRequest != REQUEST_LEFT) ||
                ((state & LCA_RIGHT) != 0 && changeRequest != REQUEST_RIGHT) ||
                ((state & LCA_STAY) != 0 && changeRequest != REQUEST_HOLD)) {
                // cancel conflicting lcModel request
                state &= ~LCA_WANTS_LANECHANGE_OR_STAY;
                state &= ~LCA_URGENT;
            }
        } else if (mode == LC_ALWAYS) {
            // ignore any TraCI requests
            return state;
        }
    }
    // apply traci requests
    if (changeRequest == REQUEST_NONE) {
        return state;
    } else {
        state |= LCA_TRACI;
        // security checks
        if ((myTraciLaneChangePriority == LCP_ALWAYS)
                || (myTraciLaneChangePriority == LCP_NOOVERLAP && (state & LCA_OVERLAPPING) == 0)) {
            state &= ~(LCA_BLOCKED | LCA_OVERLAPPING);
        }
        if (changeRequest != REQUEST_HOLD && myTraciLaneChangePriority != LCP_OPPORTUNISTIC) {
            state |= LCA_URGENT;
        }
        switch (changeRequest) {
            case REQUEST_HOLD:
                return state | LCA_STAY;
            case REQUEST_LEFT:
                return state | LCA_LEFT;
            case REQUEST_RIGHT:
                return state | LCA_RIGHT;
            default:
                throw ProcessError("should not happen");
        }
    }
}


SUMOReal
MSVehicle::Influencer::changeRequestRemainingSeconds(const SUMOTime currentTime) const {
    assert(myLaneTimeLine.size() >= 2);
    assert(currentTime >= myLaneTimeLine[0].first);
    return STEPS2TIME(myLaneTimeLine[1].first - currentTime);
}


void
MSVehicle::Influencer::setConsiderSafeVelocity(bool value) {
    myConsiderSafeVelocity = value;
}


void
MSVehicle::Influencer::setConsiderMaxAcceleration(bool value) {
    myConsiderMaxAcceleration = value;
}


void
MSVehicle::Influencer::setConsiderMaxDeceleration(bool value) {
    myConsiderMaxDeceleration = value;
}


void
MSVehicle::Influencer::setRespectJunctionPriority(bool value) {
    myRespectJunctionPriority = value;
}


void
MSVehicle::Influencer::setEmergencyBrakeRedLight(bool value) {
    myEmergencyBrakeRedLight = value;
}


void
MSVehicle::Influencer::setLaneChangeMode(int value) {
    myStrategicLC = (LaneChangeMode)(value & (1 + 2));
    myCooperativeLC = (LaneChangeMode)((value & (4 + 8)) >> 2);
    mySpeedGainLC = (LaneChangeMode)((value & (16 + 32)) >> 4);
    myRightDriveLC = (LaneChangeMode)((value & (64 + 128)) >> 6);
    myTraciLaneChangePriority = (TraciLaneChangePriority)((value & (256 + 512)) >> 8);
}


void
MSVehicle::Influencer::postProcessVTD(MSVehicle* v) {
    v->onRemovalFromNet(MSMoveReminder::NOTIFICATION_TELEPORT);
    v->getLane()->removeVehicle(v, MSMoveReminder::NOTIFICATION_TELEPORT);
    if (myVTDRoute.size() != 0) {
        v->replaceRouteEdges(myVTDRoute, true);
    }
    v->myCurrEdge += myVTDEdgeOffset;
    if (myVTDPos > myVTDLane->getLength()) {
        myVTDPos = myVTDLane->getLength();
    }
    myVTDLane->forceVehicleInsertion(v, myVTDPos);
    v->updateBestLanes();
    myAmVTDControlled = false;
}

#endif


/* -------------------------------------------------------------------------
 * MSVehicle-methods
 * ----------------------------------------------------------------------- */
MSVehicle::~MSVehicle() {
    delete myLaneChangeModel;
    // other
    delete myEdgeWeights;
    for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
        (*i)->resetPartialOccupation(this);
    }
    myFurtherLanes.clear();
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    //
    if (myType->amVehicleSpecific()) {
        delete myType;
    }
#ifndef NO_TRACI
    delete myInfluencer;
#endif
}


MSVehicle::MSVehicle(SUMOVehicleParameter* pars, const MSRoute* route,
                     const MSVehicleType* type, const SUMOReal speedFactor) :
    MSBaseVehicle(pars, route, type, speedFactor),
    myWaitingTime(0),
    myState(0, 0), //
    myLane(0),
    myLastBestLanesEdge(0),
    myLastBestLanesInternalLane(0),
    myPersonDevice(0),
    myContainerDevice(0),
    myAcceleration(0),
    mySignals(0),
    myAmOnNet(false),
    myAmRegisteredAsWaitingForPerson(false),
    myAmRegisteredAsWaitingForContainer(false),
    myHaveToWaitOnNextLink(false),
    myCachedPosition(Position::INVALID),
    myEdgeWeights(0)
#ifndef NO_TRACI
    , myInfluencer(0)
#endif
{
    if ((*myCurrEdge)->getPurpose() != MSEdge::EDGEFUNCTION_DISTRICT) {
        if (pars->departLaneProcedure == DEPART_LANE_GIVEN) {
            if ((*myCurrEdge)->getDepartLane(*this) == 0) {
                throw ProcessError("Invalid departlane definition for vehicle '" + pars->id + "'.");
            }
        } else {
            if ((*myCurrEdge)->allowedLanes(type->getVehicleClass()) == 0) {
                throw ProcessError("Vehicle '" + pars->id + "' is not allowed to depart on any lane of its first edge.");
            }
        }
        if (pars->departSpeedProcedure == DEPART_SPEED_GIVEN && pars->departSpeed > type->getMaxSpeed()) {
            throw ProcessError("Departure speed for vehicle '" + pars->id +
                               "' is too high for the vehicle type '" + type->getID() + "'.");
        }
    }
    myLaneChangeModel = MSAbstractLaneChangeModel::build(type->getLaneChangeModel(), *this);
    myCFVariables = type->getCarFollowModel().createVehicleVariables();
}


void
MSVehicle::onRemovalFromNet(const MSMoveReminder::Notification reason) {
    MSVehicleTransfer::getInstance()->remove(this);
    workOnMoveReminders(myState.myPos - SPEED2DIST(myState.mySpeed), myState.myPos, myState.mySpeed);
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    leaveLane(reason);
}


// ------------ interaction with the route
bool
MSVehicle::hasArrived() const {
    return myCurrEdge == myRoute->end() - 1 && (myStops.empty() || myStops.front().edge != myCurrEdge)
           && myState.myPos > myArrivalPos - POSITION_EPS;
}


bool
MSVehicle::replaceRoute(const MSRoute* newRoute, bool onInit, int offset) {
    const ConstMSEdgeVector& edges = newRoute->getEdges();
    // assert the vehicle may continue (must not be "teleported" or whatever to another position)
    if (!onInit && !newRoute->contains(*myCurrEdge)) {
        return false;
    }

    // rebuild in-vehicle route information
    if (onInit) {
        myCurrEdge = newRoute->begin();
    } else {
        myCurrEdge = find(edges.begin() + offset, edges.end(), *myCurrEdge);
    }
    // check whether the old route may be deleted (is not used by anyone else)
    newRoute->addReference();
    myRoute->release();
    // assign new route
    myRoute = newRoute;
    myLastBestLanesEdge = 0;
    myLastBestLanesInternalLane = 0;
    updateBestLanes(true, onInit ? (*myCurrEdge)->getLanes().front() : 0);
    // update arrival definition
    calculateArrivalPos();
    // save information that the vehicle was rerouted
    myNumberReroutes++;
    MSNet::getInstance()->informVehicleStateListener(this, MSNet::VEHICLE_STATE_NEWROUTE);
    // recheck old stops
    for (std::list<Stop>::iterator iter = myStops.begin(); iter != myStops.end();) {
        if (find(myCurrEdge, edges.end(), &iter->lane->getEdge()) == edges.end()) {
            iter = myStops.erase(iter);
        } else {
            iter->edge = find(myCurrEdge, edges.end(), &iter->lane->getEdge());
            ++iter;
        }
    }
    // add new stops
    for (std::vector<SUMOVehicleParameter::Stop>::const_iterator i = newRoute->getStops().begin(); i != newRoute->getStops().end(); ++i) {
        std::string error;
        addStop(*i, error);
        if (error != "") {
            WRITE_WARNING(error);
        }
    }
    return true;
}


bool
MSVehicle::willPass(const MSEdge* const edge) const {
    return find(myCurrEdge, myRoute->end(), edge) != myRoute->end();
}


unsigned int
MSVehicle::getRoutePosition() const {
    return (unsigned int) std::distance(myRoute->begin(), myCurrEdge);
}


void
MSVehicle::resetRoutePosition(unsigned int index) {
    myCurrEdge = myRoute->begin() + index;
    // !!! hack
    myArrivalPos = (*(myRoute->end() - 1))->getLanes()[0]->getLength();
}



const MSEdgeWeightsStorage&
MSVehicle::getWeightsStorage() const {
    return _getWeightsStorage();
}


MSEdgeWeightsStorage&
MSVehicle::getWeightsStorage() {
    return _getWeightsStorage();
}


MSEdgeWeightsStorage&
MSVehicle::_getWeightsStorage() const {
    if (myEdgeWeights == 0) {
        myEdgeWeights = new MSEdgeWeightsStorage();
    }
    return *myEdgeWeights;
}


// ------------ Interaction with move reminders
void
MSVehicle::workOnMoveReminders(SUMOReal oldPos, SUMOReal newPos, SUMOReal newSpeed) {
    // This erasure-idiom works for all stl-sequence-containers
    // See Meyers: Effective STL, Item 9
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (!rem->first->notifyMove(*this, oldPos + rem->second, newPos + rem->second, newSpeed)) {
#ifdef _DEBUG
            if (myTraceMoveReminders) {
                traceMoveReminder("notifyMove", rem->first, rem->second, false);
            }
#endif
            rem = myMoveReminders.erase(rem);
        } else {
#ifdef _DEBUG
            if (myTraceMoveReminders) {
                traceMoveReminder("notifyMove", rem->first, rem->second, true);
            }
#endif
            ++rem;
        }
    }
}


void
MSVehicle::adaptLaneEntering2MoveReminder(const MSLane& enteredLane) {
    // save the old work reminders, patching the position information
    //  add the information about the new offset to the old lane reminders
    const SUMOReal oldLaneLength = myLane->getLength();
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end(); ++rem) {
        rem->second += oldLaneLength;
#ifdef _DEBUG
        if (myTraceMoveReminders) {
            traceMoveReminder("adaptedPos", rem->first, rem->second, true);
        }
#endif
    }
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane.getMoveReminders().begin(); rem != enteredLane.getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
}


// ------------ Other getter methods
SUMOReal
MSVehicle::getSlope() const {
    if (myLane == 0) {
        return 0;
    }
    const SUMOReal lp = getPositionOnLane();
    const SUMOReal gp = myLane->interpolateLanePosToGeometryPos(lp);
    return myLane->getShape().slopeDegreeAtOffset(gp);
}


Position
MSVehicle::getPosition(const SUMOReal offset) const {
    if (myLane == 0) {
        return Position::INVALID;
    }
    if (isParking()) {
        PositionVector shp = myLane->getEdge().getLanes()[0]->getShape();
        shp.move2side(SUMO_const_laneWidth);
        return shp.positionAtOffset(myLane->interpolateLanePosToGeometryPos(getPositionOnLane() + offset));
    }
    const bool changingLanes = getLaneChangeModel().isChangingLanes();
    if (offset == 0. && !changingLanes) {
        if (myCachedPosition == Position::INVALID) {
            myCachedPosition = myLane->geometryPositionAtOffset(myState.myPos);
        }
        return myCachedPosition;
    }
    Position result = myLane->geometryPositionAtOffset(getPositionOnLane() + offset);
    if (changingLanes) {
        const Position other = getLaneChangeModel().getShadowLane()->geometryPositionAtOffset(getPositionOnLane() + offset);
        Line line = getLaneChangeModel().isLaneChangeMidpointPassed() ?  Line(other, result) : Line(result, other);
        return line.getPositionAtDistance(getLaneChangeModel().getLaneChangeCompletion() * line.length());
    }
    return result;
}


const MSEdge*
MSVehicle::getRerouteOrigin() const {
    // too close to the next junction, so avoid an emergency brake here
    if (myLane != 0 && (myCurrEdge + 1) != myRoute->end() &&
            myState.myPos > myLane->getLength() - getCarFollowModel().brakeGap(myState.mySpeed, getCarFollowModel().getMaxDecel(), 0.)) {
        return *(myCurrEdge + 1);
    }
#ifdef HAVE_INTERNAL_LANES
    if (myLane != 0) {
        return myLane->getInternalFollower();
    }
#endif
    return *myCurrEdge;
}


SUMOReal
MSVehicle::getAngle() const {
    Position p1;
    Position p2;
    if (isParking()) {
        return -myLane->getShape().rotationDegreeAtOffset(myLane->interpolateLanePosToGeometryPos(getPositionOnLane()));
    }
    if (getLaneChangeModel().isChangingLanes()) {
        // cannot use getPosition() because it already includes the offset to the side and thus messes up the angle
        p1 = myLane->geometryPositionAtOffset(myState.myPos);
    } else {
        p1 = getPosition();
    }
    if (myState.myPos >= myType->getLength()) {
        // vehicle is fully on the new lane
        p2 = myLane->geometryPositionAtOffset(myState.myPos - myType->getLength());
    } else {
        p2 = myFurtherLanes.size() > 0
             ? myFurtherLanes.back()->geometryPositionAtOffset(myFurtherLanes.back()->getPartialOccupatorEnd())
             : myLane->getShape().front();
        if (getLaneChangeModel().isChangingLanes() && myFurtherLanes.size() > 0 && getLaneChangeModel().getShadowLane(myFurtherLanes.back()) == 0) {
            // special case where there target lane has no predecessor
            p2 = myLane->getShape().front();
        }
    }
    SUMOReal result = (p1 != p2 ?
                       atan2(p1.x() - p2.x(), p2.y() - p1.y()) * 180. / M_PI :
                       -myLane->getShape().rotationDegreeAtOffset(myLane->interpolateLanePosToGeometryPos(getPositionOnLane())));
    if (getLaneChangeModel().isChangingLanes()) {
        const SUMOReal angleOffset = 60 / STEPS2TIME(MSGlobals::gLaneChangeDuration) * (getLaneChangeModel().isLaneChangeMidpointPassed() ? 1 - getLaneChangeModel().getLaneChangeCompletion() : getLaneChangeModel().getLaneChangeCompletion());
        result += getLaneChangeModel().getLaneChangeDirection() * angleOffset;
    }
    return result;
}


// ------------
bool
MSVehicle::addStop(const SUMOVehicleParameter::Stop& stopPar, std::string& errorMsg, SUMOTime untilOffset) {
    Stop stop;
    stop.lane = MSLane::dictionary(stopPar.lane);
    if (!stop.lane->allowsVehicleClass(myType->getVehicleClass())) {
        errorMsg = "Vehicle '" + myParameter->id + "' is not allowed to stop on lane '" + stopPar.lane + "'.";
        return false;
    }
    stop.busstop = MSNet::getInstance()->getBusStop(stopPar.busstop);
    stop.containerstop = MSNet::getInstance()->getContainerStop(stopPar.containerstop);
    stop.startPos = stopPar.startPos;
    stop.endPos = stopPar.endPos;
    stop.duration = stopPar.duration;
    stop.until = stopPar.until;
    stop.timeToBoardNextPerson = 0;
    stop.timeToLoadNextContainer = 0;
    stop.awaitedPersons = stopPar.awaitedPersons;
    stop.awaitedContainers = stopPar.awaitedContainers;
    if (stop.until != -1) {
        stop.until += untilOffset;
    }
    stop.triggered = stopPar.triggered;
    stop.containerTriggered = stopPar.containerTriggered;
    stop.parking = stopPar.parking;
    stop.reached = false;
    if (stop.startPos < 0 || stop.endPos > stop.lane->getLength()) {
        if (stop.busstop != 0) {
            errorMsg = "Bus stop '" + stop.busstop->getID() + "'";
        } else {
            errorMsg = "Stop";
        }
        errorMsg += " for vehicle '" + myParameter->id + "' on lane '" + stopPar.lane + "' has an invalid position.";
        return false;
    }
    if (stop.busstop != 0 && myType->getLength() / 2. > stop.endPos - stop.startPos) {
        errorMsg = "Bus stop '" + stop.busstop->getID() + "' on lane '" + stopPar.lane + "' is too short for vehicle '" + myParameter->id + "'.";
    }
    if (stop.containerstop != 0 && myType->getLength() / 2. > stop.endPos - stop.startPos) {
        errorMsg = "Container stop '" + stop.containerstop->getID() + "' on lane '" + stopPar.lane + "' is too short for vehicle '" + myParameter->id + "'.";
    }
    stop.edge = find(myCurrEdge, myRoute->end(), &stop.lane->getEdge());
    MSRouteIterator prevStopEdge = myCurrEdge;
    SUMOReal prevStopPos = myState.myPos;
    // where to insert the stop
    std::list<Stop>::iterator iter = myStops.begin();
    if (stopPar.index == STOP_INDEX_END || stopPar.index >= static_cast<int>(myStops.size())) {
        if (myStops.size() > 0) {
            prevStopEdge = myStops.back().edge;
            prevStopPos = myStops.back().endPos;
            iter = myStops.end();
            stop.edge = find(prevStopEdge, myRoute->end(), &stop.lane->getEdge());
            if (prevStopEdge == stop.edge && prevStopPos > stop.endPos) {
                stop.edge = find(prevStopEdge + 1, myRoute->end(), &stop.lane->getEdge());
            }
        }
    } else {
        if (stopPar.index == STOP_INDEX_FIT) {
            while (iter != myStops.end() && (iter->edge < stop.edge ||
                                             (iter->endPos < stop.endPos && iter->edge == stop.edge))) {
                prevStopEdge = iter->edge;
                prevStopPos = iter->endPos;
                ++iter;
            }
        } else {
            int index = stopPar.index;
            while (index > 0) {
                prevStopEdge = iter->edge;
                prevStopPos = iter->endPos;
                ++iter;
                --index;
            }
            stop.edge = find(prevStopEdge, myRoute->end(), &stop.lane->getEdge());
        }
    }
    if (stop.edge == myRoute->end() || prevStopEdge > stop.edge ||
            (prevStopEdge == stop.edge && prevStopPos > stop.endPos)) {
        if (stop.busstop != 0) {
            errorMsg = "Bus stop '" + stop.busstop->getID() + "'";
        } else {
            errorMsg = "Stop";
        }
        errorMsg += " for vehicle '" + myParameter->id + "' on lane '" + stopPar.lane + "' is not downstream the current route.";
        return false;
    }
    // David.C:
    //if (!stop.parking && (myCurrEdge == stop.edge && myState.myPos > stop.endPos - getCarFollowModel().brakeGap(myState.mySpeed))) {
    if (myCurrEdge == stop.edge && myState.myPos > stop.endPos - getCarFollowModel().brakeGap(myState.mySpeed)) {
        errorMsg = "Stop for vehicle '" + myParameter->id + "' on lane '" + stopPar.lane + "' is too close to break.";
        return false;
    }
    if (!hasDeparted() && myCurrEdge == stop.edge) {
        SUMOReal pos = -1;
        if (myParameter->departPosProcedure == DEPART_POS_GIVEN) {
            pos = myParameter->departPos;
            if (pos < 0.) {
                pos += (*myCurrEdge)->getLength();
            }
        }
        if (myParameter->departPosProcedure == DEPART_POS_BASE || myParameter->departPosProcedure == DEPART_POS_DEFAULT) {
            pos = MIN2(static_cast<SUMOReal>(getVehicleType().getLength() + POSITION_EPS), (*myCurrEdge)->getLength());
        }
        if (pos > stop.endPos) {
            if (stop.busstop != 0) {
                errorMsg = "Bus stop '" + stop.busstop->getID() + "'";
            } else {
                errorMsg = "Stop";
            }
            errorMsg += " for vehicle '" + myParameter->id + "' on lane '" + stopPar.lane + "' is before departPos.";
            return false;
        }
    }
    if (iter != myStops.begin()) {
        std::list<Stop>::iterator iter2 = iter;
        iter2--;
        if (stop.until >= 0 && iter2->until > stop.until) {
            if (stop.busstop != 0) {
                errorMsg = "Bus stop '" + stop.busstop->getID() + "'";
            } else {
                errorMsg = "Stop";
            }
            errorMsg += " for vehicle '" + myParameter->id + "' on lane '" + stopPar.lane + "' ends earlier than previous stop.";
        }
    }
    myStops.insert(iter, stop);
    return true;
}


bool
MSVehicle::isStopped() const {
    return !myStops.empty() && myStops.begin()->reached;
}


bool
MSVehicle::isParking() const {
    return isStopped() && myStops.begin()->parking;
}


bool
MSVehicle::isStoppedTriggered() const {
    return isStopped() && (myStops.begin()->triggered || myStops.begin()->containerTriggered);
}


bool
MSVehicle::isStoppedInRange(SUMOReal pos) const {
    return isStopped() && myStops.begin()->startPos <= pos && myStops.begin()->endPos >= pos;
}


SUMOReal
MSVehicle::processNextStop(SUMOReal currentVelocity) {
    if (myStops.empty()) {
        // no stops; pass
        return currentVelocity;
    }
    Stop& stop = myStops.front();
    if (stop.reached) {
        // ok, we have already reached the next stop
        // any waiting persons may board now
        bool boarded = MSNet::getInstance()->getPersonControl().boardAnyWaiting(&myLane->getEdge(), this, &stop);
        boarded &= stop.awaitedPersons.size() == 0;
        // load containers
        bool loaded = MSNet::getInstance()->getContainerControl().loadAnyWaiting(&myLane->getEdge(), this, &stop);
        loaded &= stop.awaitedContainers.size() == 0;
        if (boarded) {
            if (stop.busstop != 0) {
                const std::vector<MSTransportable*>& persons = myPersonDevice->getPersons();
                for (std::vector<MSTransportable*>::const_iterator i = persons.begin(); i != persons.end(); ++i) {
                    stop.busstop->removeTransportable(*i);
                }
            }
            // the triggering condition has been fulfilled. Maybe we want to wait a bit longer for additional riders (car pooling)
            stop.triggered = false;
            if (myAmRegisteredAsWaitingForPerson) {
                MSNet::getInstance()->getVehicleControl().unregisterOneWaitingForPerson();
                myAmRegisteredAsWaitingForPerson = false;
            }
        }
        if (loaded) {
            if (stop.containerstop != 0) {
                const std::vector<MSTransportable*>& containers = myContainerDevice->getContainers();
                for (std::vector<MSTransportable*>::const_iterator i = containers.begin(); i != containers.end(); ++i) {
                    stop.containerstop->removeTransportable(*i);
                }
            }
            // the triggering condition has been fulfilled
            stop.containerTriggered = false;
            if (myAmRegisteredAsWaitingForContainer) {
                MSNet::getInstance()->getVehicleControl().unregisterOneWaitingForContainer();
                myAmRegisteredAsWaitingForContainer = false;
            }
        }
        if (stop.duration <= 0 && !stop.triggered && !stop.containerTriggered) {
            resumeFromStopping();
        } else {
            // we have to wait some more time
            if (stop.triggered && !myAmRegisteredAsWaitingForPerson) {
                // we can only register after waiting for one step. otherwise we might falsely signal a deadlock
                MSNet::getInstance()->getVehicleControl().registerOneWaitingForPerson();
                myAmRegisteredAsWaitingForPerson = true;
            }
            if (stop.containerTriggered && !myAmRegisteredAsWaitingForContainer) {
                // we can only register after waiting for one step. otherwise we might falsely signal a deadlock
                MSNet::getInstance()->getVehicleControl().registerOneWaitingForContainer();
                myAmRegisteredAsWaitingForContainer = true;
            }
            stop.duration -= DELTA_T;
            return 0;
        }
    } else {
        // is the next stop on the current lane?
        if (stop.edge == myCurrEdge) {
            // get the stopping position
            SUMOReal endPos = stop.endPos;
            bool busStopsMustHaveSpace = true;
            if (stop.busstop != 0) {
                // on bus stops, we have to wait for free place if they are in use...
                endPos = stop.busstop->getLastFreePos(*this);
                // at least half the bus has to fit on non-empty bus stops
                if (endPos != stop.busstop->getEndLanePosition() && endPos - myType->getLength() / 2. < stop.busstop->getBeginLanePosition()) {
                    busStopsMustHaveSpace = false;
                }
            }
            bool containerStopsMustHaveSpace = true;
            // if the stop is a container stop we check if the vehicle fits into the last free position of the stop
            if (stop.containerstop != 0) {
                // on container stops, we have to wait for free place if they are in use...
                endPos = stop.containerstop->getLastFreePos(*this);
                if (endPos != stop.containerstop->getEndLanePosition() && endPos - myType->getLength() / 2. < stop.containerstop->getBeginLanePosition()) {
                    containerStopsMustHaveSpace = false;
                }
            }
            // we use the same offset for container stops as for bus stops. we might have to change it at some point!
            if (myState.pos() + getVehicleType().getMinGap() >= endPos - BUS_STOP_OFFSET && busStopsMustHaveSpace
                    && containerStopsMustHaveSpace && myLane == stop.lane) {
                // ok, we may stop (have reached the stop)
                stop.reached = true;
                MSNet::getInstance()->getVehicleControl().addWaiting(&myLane->getEdge(), this);
                MSNet::getInstance()->informVehicleStateListener(this, MSNet::VEHICLE_STATE_STARTING_STOP);
                // compute stopping time
                if (stop.until >= 0) {
                    if (stop.duration == -1) {
                        stop.duration = stop.until - MSNet::getInstance()->getCurrentTimeStep();
                    } else {
                        stop.duration = MAX2(stop.duration, stop.until - MSNet::getInstance()->getCurrentTimeStep());
                    }
                }
                if (stop.busstop != 0) {
                    // let the bus stop know the vehicle
                    stop.busstop->enter(this, myState.pos() + getVehicleType().getMinGap(), myState.pos() - myType->getLength());
                }
                if (stop.containerstop != 0) {
                    // let the container stop know the vehicle
                    stop.containerstop->enter(this, myState.pos() + getVehicleType().getMinGap(), myState.pos() - myType->getLength());
                }
            }
            // decelerate
            return getCarFollowModel().stopSpeed(this, getSpeed(), endPos - myState.pos());
        }
    }
    return currentVelocity;
}


const ConstMSEdgeVector
MSVehicle::getStopEdges() const {
    ConstMSEdgeVector result;
    for (std::list<Stop>::const_iterator iter = myStops.begin(); iter != myStops.end(); ++iter) {
        result.push_back(*iter->edge);
    }
    return result;
}


void
MSVehicle::planMove(const SUMOTime t, const MSVehicle* pred, const SUMOReal lengthsInFront) {
    planMoveInternal(t, pred, myLFLinkLanes);
    checkRewindLinkLanes(lengthsInFront, myLFLinkLanes);
    getLaneChangeModel().resetMoved();
}


void
MSVehicle::planMoveInternal(const SUMOTime t, const MSVehicle* pred, DriveItemVector& lfLinks) const {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gDebugSelectedVehicle == getID()) {
        int bla = 0;
    }
#endif
    // remove information about approaching links, will be reset later in this step
    for (DriveItemVector::iterator i = lfLinks.begin(); i != lfLinks.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    lfLinks.clear();
    //
    const MSCFModel& cfModel = getCarFollowModel();
    const SUMOReal vehicleLength = getVehicleType().getLength();
    const SUMOReal maxV = cfModel.maxNextSpeed(myState.mySpeed, this);
    SUMOReal laneMaxV = myLane->getVehicleMaxSpeed(this);
    // vBeg is the initial maximum velocity of this vehicle in this step
    SUMOReal v = MIN2(maxV, laneMaxV);
#ifndef NO_TRACI
    if (myInfluencer != 0) {
        const SUMOReal vMin = MAX2(SUMOReal(0), cfModel.getSpeedAfterMaxDecel(myState.mySpeed));
        v = myInfluencer->influenceSpeed(MSNet::getInstance()->getCurrentTimeStep(), v, v, vMin, maxV);
        // !!! recheck - why is it done, here?
        if (myInfluencer->isVTDControlled()) {
            return; // !!! temporary
        }
    }
#endif

    const SUMOReal dist = SPEED2DIST(maxV) + cfModel.brakeGap(maxV);
    const std::vector<MSLane*>& bestLaneConts = getBestLanesContinuation();
    assert(bestLaneConts.size() > 0);
#ifdef HAVE_INTERNAL_LANES
    bool hadNonInternal = false;
#else
    bool hadNonInternal = true;
#endif
    SUMOReal seen = myLane->getLength() - myState.myPos; // the distance already "seen"; in the following always up to the end of the current "lane"
    SUMOReal seenNonInternal = 0;
    SUMOReal vLinkPass = MIN2(estimateSpeedAfterDistance(seen, v, getVehicleType().getCarFollowModel().getMaxAccel()), laneMaxV); // upper bound
    unsigned int view = 0;
    DriveProcessItem* lastLink = 0;
    SUMOReal gap = 0;
    if (pred != 0) {
        if (pred == myLane->getPartialOccupator()) {
            gap = myLane->getPartialOccupatorEnd() - myState.myPos - getVehicleType().getMinGap();
        } else {
            gap = pred->getPositionOnLane() - pred->getVehicleType().getLength() - myState.myPos - getVehicleType().getMinGap();
        }
    }
    std::pair<const MSVehicle*, SUMOReal> leaderInfo = std::make_pair(pred, gap);
    // iterator over subsequent lanes and fill lfLinks until stopping distance or stopped
    const MSLane* lane = myLane;
    while (true) {
        // check leader on lane
        //  leader is given for the first edge only
        adaptToLeader(leaderInfo, seen, lastLink, lane, v, vLinkPass);
        if (getLaneChangeModel().hasShadowVehicle()) {
            // also slow down for leaders on the shadowLane
            const MSLane* shadowLane = getLaneChangeModel().getShadowLane(lane);
            if (shadowLane != 0) {
                std::pair<const MSVehicle*, SUMOReal> shadowLeaderInfo = shadowLane->getLeader(this, lane->getLength() - seen, false);
                adaptToLeader(shadowLeaderInfo, seen, lastLink, shadowLane, v, vLinkPass);
            }
        }

        // process stops
        if (!myStops.empty() && &myStops.begin()->lane->getEdge() == &lane->getEdge()) {
            // we are approaching a stop on the edge; must not drive further
            const Stop& stop = *myStops.begin();
            const SUMOReal endPos = stop.busstop == 0 ? stop.endPos : stop.busstop->getLastFreePos(*this);
            const SUMOReal stopDist = seen + endPos - lane->getLength();
            const SUMOReal stopSpeed = cfModel.stopSpeed(this, getSpeed(), stopDist);
            if (lastLink != 0) {
                lastLink->adaptLeaveSpeed(cfModel.stopSpeed(this, vLinkPass, endPos));
            }
            v = MIN2(v, stopSpeed);
            lfLinks.push_back(DriveProcessItem(v, stopDist));
            break;
        }

        // move to next lane
        //  get the next link used
        MSLinkCont::const_iterator link = MSLane::succLinkSec(*this, view + 1, *lane, bestLaneConts);
        //  check whether the vehicle is on its final edge
        if (myCurrEdge + view + 1 == myRoute->end()) {
            const SUMOReal arrivalSpeed = (myParameter->arrivalSpeedProcedure == ARRIVAL_SPEED_GIVEN ?
                                           myParameter->arrivalSpeed : laneMaxV);
            // subtract the arrival speed from the remaining distance so we get one additional driving step with arrival speed
            const SUMOReal distToArrival = seen + myArrivalPos - lane->getLength() - SPEED2DIST(arrivalSpeed);
            const SUMOReal va = cfModel.freeSpeed(this, getSpeed(), distToArrival, arrivalSpeed);
            v = MIN2(v, va);
            if (lastLink != 0) {
                lastLink->adaptLeaveSpeed(va);
            }
            lfLinks.push_back(DriveProcessItem(v, seen));
            break;
        }
        // check whether the lane is a dead end
        if (lane->isLinkEnd(link)) {
            SUMOReal va = MIN2(cfModel.stopSpeed(this, getSpeed(), seen), laneMaxV);
            if (lastLink != 0) {
                lastLink->adaptLeaveSpeed(va);
            }
            v = MIN2(va, v);
            lfLinks.push_back(DriveProcessItem(v, seen));
            break;
        }
        const bool yellowOrRed = (*link)->getState() == LINKSTATE_TL_RED ||
                                 (*link)->getState() == LINKSTATE_TL_REDYELLOW ||
                                 (*link)->getState() == LINKSTATE_TL_YELLOW_MAJOR ||
                                 (*link)->getState() == LINKSTATE_TL_YELLOW_MINOR;
        // We distinguish 3 cases when determining the point at which a vehicle stops:
        // - links that require stopping: here the vehicle needs to stop close to the stop line
        //   to ensure it gets onto the junction in the next step. Othwise the vehicle would 'forget'
        //   that it already stopped and need to stop again. This is necessary pending implementation of #999
        // - red/yellow light: here the vehicle 'knows' that it will have priority eventually and does not need to stop on a precise spot
        // - other types of minor links: the vehicle needs to stop as close to the junction as necessary
        //   to minimize the time window for passing the junction. If the
        //   vehicle 'decides' to accelerate and cannot enter the junction in
        //   the next step, new foes may appear and cause a collision (see #1096)
        // - major links: stopping point is irrelevant
        const SUMOReal laneStopOffset = yellowOrRed || (*link)->havePriority() ? DIST_TO_STOPLINE_EXPECT_PRIORITY : POSITION_EPS;
        const SUMOReal stopDist = MAX2(SUMOReal(0), seen - laneStopOffset);
        // check whether we need to slow down in order to finish a continuous lane change
        if (getLaneChangeModel().isChangingLanes()) {
            if (    // slow down to finish lane change before a turn lane
                ((*link)->getDirection() == LINKDIR_LEFT || (*link)->getDirection() == LINKDIR_RIGHT) ||
                // slow down to finish lane change before the shadow lane ends
                (getLaneChangeModel().isLaneChangeMidpointPassed() &&
                 (*link)->getViaLaneOrLane()->getParallelLane(-getLaneChangeModel().getLaneChangeDirection()) == 0)) {
                // XXX maybe this is too harsh. Vehicles could cut some corners here
                const SUMOReal timeRemaining = STEPS2TIME((1 - getLaneChangeModel().getLaneChangeCompletion()) * MSGlobals::gLaneChangeDuration);
                const SUMOReal va = MAX2((SUMOReal)0, (seen - POSITION_EPS) / timeRemaining);
                v = MIN2(va, v);
            }
        }

        // - even if red, if we cannot break we should issue a request
        // - always issue a request to leave the intersection we are currently on
        bool setRequest = v > 0 || (myLane->getEdge().isInternal() && lastLink == 0); 
        SUMOReal vLinkWait = MIN2(v, cfModel.stopSpeed(this, getSpeed(), stopDist));
        const SUMOReal brakeDist = cfModel.brakeGap(myState.mySpeed) - myState.mySpeed * cfModel.getHeadwayTime();
        if (yellowOrRed && seen >= brakeDist) {
            // the vehicle is able to brake in front of a yellow/red traffic light
            lfLinks.push_back(DriveProcessItem(*link, vLinkWait, vLinkWait, false, t + TIME2STEPS(seen / MAX2(vLinkWait, NUMERICAL_EPS)), vLinkWait, 0, 0, seen));
            //lfLinks.push_back(DriveProcessItem(0, vLinkWait, vLinkWait, false, 0, 0, stopDist));
            break;
        }

#ifdef HAVE_INTERNAL_LANES
        // we want to pass the link but need to check for foes on internal lanes
        const MSLink::LinkLeaders linkLeaders = (*link)->getLeaderInfo(seen, getVehicleType().getMinGap());
        for (MSLink::LinkLeaders::const_iterator it = linkLeaders.begin(); it != linkLeaders.end(); ++it) {
            // the vehicle to enter the junction first has priority
            const MSVehicle* leader = (*it).vehAndGap.first;
            if (leader == 0) {
                // leader is a pedestrian. Passing 'this' as a dummy.
                //std::cout << SIMTIME << " veh=" << getID() << " is blocked on link to " << (*link)->getViaLaneOrLane()->getID() << " by pedestrian. dist=" << it->distToCrossing << "\n";
                adaptToLeader(std::make_pair(this, -1), seen, lastLink, lane, v, vLinkPass, it->distToCrossing);
            } else if (leader->myLinkLeaders[(*link)->getJunction()].count(getID()) == 0) {
                // leader isn't already following us, now we follow it
                myLinkLeaders[(*link)->getJunction()].insert(leader->getID());
                adaptToLeader(it->vehAndGap, seen, lastLink, lane, v, vLinkPass, it->distToCrossing);
                if (lastLink != 0) {
                    // we are not yet on the junction with this linkLeader.
                    // at least we can drive up to the previous link and stop there
                    v = MAX2(v, lastLink->myVLinkWait);
                }
                // if blocked by a leader from the same lane we must yield our request
                if (v < SUMO_const_haltingSpeed && leader->getLane()->getLogicalPredecessorLane() == myLane->getLogicalPredecessorLane()) {
                    setRequest = false;
                }
            }
        }
        // if this is the link between two internal lanes we may have to slow down for pedestrians
        vLinkWait = MIN2(vLinkWait, v);
#endif

        if (lastLink != 0) {
            lastLink->adaptLeaveSpeed(laneMaxV);
        }
        SUMOReal arrivalSpeed = vLinkPass;
        // vehicles should decelerate when approaching a minor link
        // - unless they are close enough to have clear visibility and may start to accelerate again
        // - and unless they are so close that stopping is impossible (i.e. when a green light turns to yellow when close to the junction)
        if (!(*link)->havePriority() && stopDist > cfModel.getMaxDecel() && brakeDist < seen) {
            // vehicle decelerates just enough to be able to stop if necessary and then accelerates
            arrivalSpeed = cfModel.getMaxDecel() + cfModel.getMaxAccel();
        }
        // @note intuitively it would make sense to compare arrivalSpeed with getSpeed() instead of v
        // however, due to the current position update rule (ticket #860) the vehicle moves with v in this step
        const SUMOReal accel = (arrivalSpeed >= v) ? cfModel.getMaxAccel() : -cfModel.getMaxDecel();
        const SUMOReal accelTime = (arrivalSpeed - v) / accel;
        const SUMOReal accelWay = accelTime * (arrivalSpeed + v) * 0.5;
        const SUMOReal nonAccelWay = MAX2(SUMOReal(0), seen - accelWay);
        // will either drive as fast as possible and decelerate as late as possible
        // or accelerate as fast as possible and then hold that speed
        const SUMOReal nonAccelSpeed = MAX3(v, arrivalSpeed, SUMO_const_haltingSpeed);
        // subtract DELTA_T because t is the time at the end of this step and the movement is not carried out yet
        const SUMOTime arrivalTime = t - DELTA_T + TIME2STEPS(accelTime + nonAccelWay / nonAccelSpeed);

        // compute speed, time if vehicle starts braking now
        // if stopping is possible, arrivalTime can be arbitrarily large. A small value keeps fractional times (impatience) meaningful
        SUMOReal arrivalSpeedBraking = 0;
        SUMOTime arrivalTimeBraking = arrivalTime + TIME2STEPS(30);
        if (seen < cfModel.brakeGap(v)) {
            // vehicle cannot come to a complete stop in time
            // Because we use a continuous formula for computiing the possible slow-down
            // we need to handle the mismatch with the discrete dynamics
            if (seen < v) {
                arrivalSpeedBraking = arrivalSpeed; // no time left for braking after this step
            } else if (2 * (seen - v * cfModel.getHeadwayTime()) * -cfModel.getMaxDecel() + v * v >= 0) {
                arrivalSpeedBraking = estimateSpeedAfterDistance(seen - v * cfModel.getHeadwayTime(), v, -cfModel.getMaxDecel());
            } else {
                arrivalSpeedBraking = cfModel.getMaxDecel();
            }
            // due to discrecte/continuous mismatch we have to ensure that braking actually helps
            arrivalSpeedBraking = MIN2(arrivalSpeedBraking, arrivalSpeed);
            arrivalTimeBraking = MAX2(arrivalTime, t + TIME2STEPS(seen / ((v + arrivalSpeedBraking) * 0.5)));
        }
        lfLinks.push_back(DriveProcessItem(*link, v, vLinkWait, setRequest,
                                           arrivalTime, arrivalSpeed,
                                           arrivalTimeBraking, arrivalSpeedBraking,
                                           seen,
                                           estimateLeaveSpeed(*link, vLinkPass)));
#ifdef HAVE_INTERNAL_LANES
        if ((*link)->getViaLane() == 0) {
            hadNonInternal = true;
            ++view;
        }
#else
        ++view;
#endif
        // we need to look ahead far enough to see available space for checkRewindLinkLanes
        if (!setRequest || ((v <= 0 || seen > dist) && hadNonInternal && seenNonInternal > vehicleLength * CRLL_LOOK_AHEAD)) {
            break;
        }
        // get the following lane
        lane = (*link)->getViaLaneOrLane();
        laneMaxV = lane->getVehicleMaxSpeed(this);
        // the link was passed
        // compute the velocity to use when the link is not blocked by other vehicles
        //  the vehicle shall be not faster when reaching the next lane than allowed
        const SUMOReal va = MAX2(laneMaxV, cfModel.freeSpeed(this, getSpeed(), seen, laneMaxV));
        v = MIN2(va, v);
        seenNonInternal += lane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL ? 0 : lane->getLength();
        seen += lane->getLength();
        leaderInfo = lane->getLastVehicleInformation();
        leaderInfo.second = leaderInfo.second + seen - lane->getLength() - getVehicleType().getMinGap();
        vLinkPass = MIN2(estimateSpeedAfterDistance(lane->getLength(), v, getVehicleType().getCarFollowModel().getMaxAccel()), laneMaxV); // upper bound
        lastLink = &lfLinks.back();
    }

}


void
MSVehicle::adaptToLeader(const std::pair<const MSVehicle*, SUMOReal> leaderInfo,
                         const SUMOReal seen, DriveProcessItem* const lastLink,
                         const MSLane* const lane, SUMOReal& v, SUMOReal& vLinkPass,
                         SUMOReal distToCrossing) const {
    if (leaderInfo.first != 0) {
        const SUMOReal vsafeLeader = getSafeFollowSpeed(leaderInfo, seen, lane, distToCrossing);
        if (lastLink != 0) {
            lastLink->adaptLeaveSpeed(vsafeLeader);
        }
        v = MIN2(v, vsafeLeader);
        vLinkPass = MIN2(vLinkPass, vsafeLeader);
    }
}


SUMOReal
MSVehicle::getSafeFollowSpeed(const std::pair<const MSVehicle*, SUMOReal> leaderInfo,
                              const SUMOReal seen, const MSLane* const lane, SUMOReal distToCrossing) const {
    assert(leaderInfo.first != 0);
    const MSCFModel& cfModel = getCarFollowModel();
    SUMOReal vsafeLeader = 0;
    if (leaderInfo.second >= 0) {
        vsafeLeader = cfModel.followSpeed(this, getSpeed(), leaderInfo.second, leaderInfo.first->getSpeed(), leaderInfo.first->getCarFollowModel().getMaxDecel());
    } else {
        // the leading, in-lapping vehicle is occupying the complete next lane
        // stop before entering this lane
        vsafeLeader = cfModel.stopSpeed(this, getSpeed(), seen - lane->getLength() - POSITION_EPS);
    }
    if (distToCrossing >= 0) {
        // drive up to the crossing point with the current link leader
        vsafeLeader = MAX2(vsafeLeader, cfModel.stopSpeed(this, getSpeed(), distToCrossing));
    }
    return vsafeLeader;
}


bool
MSVehicle::executeMove() {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gDebugSelectedVehicle == getID()) {
        int bla = 0;
    }
#endif
    // get safe velocities from DriveProcessItems
    SUMOReal vSafe = 0; // maximum safe velocity
    SUMOReal vSafeMin = 0; // minimum safe velocity
    // the distance to a link which should either be crossed this step or in
    // front of which we need to stop
    SUMOReal vSafeMinDist = 0;
    myHaveToWaitOnNextLink = false;
#ifndef NO_TRACI
    if (myInfluencer != 0) {
        if (myInfluencer->isVTDControlled()) {
            return false;
        }
    }
#endif

    assert(myLFLinkLanes.size() != 0 || (myInfluencer != 0 && myInfluencer->isVTDControlled()));
    DriveItemVector::iterator i;
    for (i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        MSLink* link = (*i).myLink;
        // the vehicle must change the lane on one of the next lanes
        if (link != 0 && (*i).mySetRequest) {
            const LinkState ls = link->getState();
            // vehicles should brake when running onto a yellow light if the distance allows to halt in front
            const bool yellow = ls == LINKSTATE_TL_YELLOW_MAJOR || ls == LINKSTATE_TL_YELLOW_MINOR;
            const SUMOReal brakeGap = getCarFollowModel().brakeGap(myState.mySpeed) - getCarFollowModel().getHeadwayTime() * myState.mySpeed;
            if (yellow && ((*i).myDistance > brakeGap || myState.mySpeed < ACCEL2SPEED(getCarFollowModel().getMaxDecel()))) {
                vSafe = (*i).myVLinkWait;
                myHaveToWaitOnNextLink = true;
                link->removeApproaching(this);
                break;
            }
            //
#ifdef NO_TRACI
            const bool influencerPrio = false;
#else
            const bool influencerPrio = (myInfluencer != 0 && !myInfluencer->getRespectJunctionPriority());
#endif
            const bool opened = yellow || influencerPrio ||
                                link->opened((*i).myArrivalTime, (*i).myArrivalSpeed, (*i).getLeaveSpeed(),
                                             getVehicleType().getLength(), getImpatience(),
                                             getCarFollowModel().getMaxDecel(), getWaitingTime());
            // vehicles should decelerate when approaching a minor link
            if (opened && !influencerPrio && !link->havePriority() && !link->lastWasContMajor() && !link->isCont()) {
                if ((*i).myDistance > getCarFollowModel().getMaxDecel()) {
                    vSafe = (*i).myVLinkWait;
                    myHaveToWaitOnNextLink = true;
                    if (ls == LINKSTATE_EQUAL) {
                        link->removeApproaching(this);
                    }
                    break; // could be revalidated
                } else {
                    // past the point of no return. we need to drive fast enough
                    // to make it across the link. However, minor slowdowns
                    // should be permissible to follow leading traffic safely
                    // There is a problem in subsecond simulation: If we cannot
                    // make it across the minor link in one step, new traffic
                    // could appear on a major foe link and cause a collision
                    vSafeMin = MIN2((SUMOReal) DIST2SPEED(myLane->getLength() - getPositionOnLane() + POSITION_EPS), (*i).myVLinkPass);
                    vSafeMinDist = myLane->getLength() - getPositionOnLane();
                }
            }
            // have waited; may pass if opened...
            if (opened) {
                vSafe = (*i).myVLinkPass;
                if (vSafe < getCarFollowModel().getMaxDecel() && vSafe <= (*i).myVLinkWait && vSafe < getCarFollowModel().maxNextSpeed(getSpeed(), this)) {
                    // this vehicle is probably not gonna drive accross the next junction (heuristic)
                    myHaveToWaitOnNextLink = true;
                }
            } else {
                vSafe = (*i).myVLinkWait;
                myHaveToWaitOnNextLink = true;
                if (ls == LINKSTATE_EQUAL) {
                    link->removeApproaching(this);
                }
                break;
            }
        } else {
            vSafe = (*i).myVLinkWait;
            if (vSafe < getSpeed()) {
                myHaveToWaitOnNextLink = true;
            }
            break;
        }
    }
    if (vSafe + NUMERICAL_EPS < vSafeMin) {
        // cannot drive across a link so we need to stop before it
        vSafe = MIN2(vSafe, getCarFollowModel().stopSpeed(this, getSpeed(), vSafeMinDist));
        vSafeMin = 0;
        myHaveToWaitOnNextLink = true;
    }
    // vehicles inside a roundabout should maintain their requests
    if (myLane->getEdge().isRoundabout()) {
        myHaveToWaitOnNextLink = false;
    }

    // XXX braking due to lane-changing is not registered
    bool braking = vSafe < getSpeed();
    // apply speed reduction due to dawdling / lane changing but ensure minimum safe speed
    SUMOReal vNext = MAX2(getCarFollowModel().moveHelper(this, vSafe), vSafeMin);

    // vNext may be higher than vSafe without implying a bug:
    //  - when approaching a green light that suddenly switches to yellow
    //  - when using unregulated junctions
    //  - when using tau < step-size
    //  - when using unsafe car following models
    //  - when using TraCI and some speedMode / laneChangeMode settings
    //if (vNext > vSafe + NUMERICAL_EPS) {
    //    WRITE_WARNING("vehicle '" + getID() + "' cannot brake hard enough to reach safe speed "
    //            + toString(vSafe, 4) + ", moving at " + toString(vNext, 4) + " instead. time="
    //            + time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
    //}
    vNext = MAX2(vNext, (SUMOReal) 0.);
#ifndef NO_TRACI
    if (myInfluencer != 0) {
        const SUMOReal vMax = getVehicleType().getCarFollowModel().maxNextSpeed(myState.mySpeed, this);
        const SUMOReal vMin = MAX2(SUMOReal(0), getVehicleType().getCarFollowModel().getSpeedAfterMaxDecel(myState.mySpeed));
        vNext = myInfluencer->influenceSpeed(MSNet::getInstance()->getCurrentTimeStep(), vNext, vSafe, vMin, vMax);
        if (myInfluencer->isVTDControlled()) {
            vNext = 0;
        }
    }
#endif
    // visit waiting time
    if (vNext <= SUMO_const_haltingSpeed) {
        myWaitingTime += DELTA_T;
        braking = true;
    } else {
        myWaitingTime = 0;
    }
    if (braking) {
        switchOnSignal(VEH_SIGNAL_BRAKELIGHT);
    } else {
        switchOffSignal(VEH_SIGNAL_BRAKELIGHT);
    }
    // call reminders after vNext is set
    const SUMOReal pos = myState.myPos;

    // update position and speed
    myAcceleration = SPEED2ACCEL(vNext - myState.mySpeed);
    myState.myPos += SPEED2DIST(vNext);
    myState.mySpeed = vNext;
    myCachedPosition = Position::INVALID;
    std::vector<MSLane*> passedLanes;
    for (std::vector<MSLane*>::reverse_iterator i = myFurtherLanes.rbegin(); i != myFurtherLanes.rend(); ++i) {
        passedLanes.push_back(*i);
    }
    if (passedLanes.size() == 0 || passedLanes.back() != myLane) {
        passedLanes.push_back(myLane);
    }
    bool moved = false;
    std::string emergencyReason = " for unknown reasons";
    // move on lane(s)
    if (myState.myPos <= myLane->getLength()) {
        // we are staying at our lane
        //  there is no need to go over succeeding lanes
        workOnMoveReminders(pos, pos + SPEED2DIST(vNext), vNext);
    } else {
        // we are moving at least to the next lane (maybe pass even more than one)
        if (myCurrEdge != myRoute->end() - 1) {
            MSLane* approachedLane = myLane;
            // move the vehicle forward
            for (i = myLFLinkLanes.begin(); i != myLFLinkLanes.end() && approachedLane != 0 && myState.myPos > approachedLane->getLength(); ++i) {
                leaveLane(MSMoveReminder::NOTIFICATION_JUNCTION);
                MSLink* link = (*i).myLink;
                // check whether the vehicle was allowed to enter lane
                //  otherwise it is decelareted and we do not need to test for it's
                //  approach on the following lanes when a lane changing is performed
                // proceed to the next lane
                if (link != 0) {
                    approachedLane = link->getViaLaneOrLane();
#ifndef NO_TRACI
                    if (myInfluencer == 0 || myInfluencer->getEmergencyBrakeRedLight()) {
#endif
                        if (link->getState() == LINKSTATE_TL_RED) {
                            emergencyReason = " because of a red traffic light";
                            break;
                        }
#ifndef NO_TRACI
                    }
#endif
                } else {
                    emergencyReason = " because there is no connection to the next edge";
                    approachedLane = 0;
                    break;
                }
                if (approachedLane != myLane && approachedLane != 0) {
                    myState.myPos -= myLane->getLength();
                    assert(myState.myPos > 0);
                    enterLaneAtMove(approachedLane);
                    myLane = approachedLane;
                    if (getLaneChangeModel().isChangingLanes()) {
                        if (link->getDirection() == LINKDIR_LEFT || link->getDirection() == LINKDIR_RIGHT) {
                            // abort lane change
                            WRITE_WARNING("Vehicle '" + getID() + "' could not finish continuous lane change (turn lane) time=" +
                                          time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
                            getLaneChangeModel().endLaneChangeManeuver();
                        }
                    }
#ifdef HAVE_INTERNAL_LANES
                    // erase leaders when past the junction
                    if (link->getViaLane() == 0) {
                        myLinkLeaders[link->getJunction()].clear();
                    }
#endif
                    moved = true;
                    if (approachedLane->getEdge().isVaporizing()) {
                        leaveLane(MSMoveReminder::NOTIFICATION_VAPORIZED);
                        break;
                    }
                }
                passedLanes.push_back(approachedLane);
            }
        }
    }
    // clear previously set information
    for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
        (*i)->resetPartialOccupation(this);
    }
    myFurtherLanes.clear();

    if (myInfluencer != 0 && myInfluencer->isVTDControlled()) {
        myWaitingTime = 0;
        return false;
    }

    if (!hasArrived() && !myLane->getEdge().isVaporizing()) {
        if (myState.myPos > myLane->getLength()) {
            WRITE_WARNING("Vehicle '" + getID() + "' performs emergency stop at the end of lane '" + myLane->getID()
                          + emergencyReason
                          + " (decel=" + toString(myAcceleration - myState.mySpeed)
                          + "), time=" + time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
            MSNet::getInstance()->getVehicleControl().registerEmergencyStop();
            myState.myPos = myLane->getLength();
            myState.mySpeed = 0;
        }
        if (myState.myPos - getVehicleType().getLength() < 0 && passedLanes.size() > 0) {
            SUMOReal leftLength = getVehicleType().getLength() - myState.myPos;
            std::vector<MSLane*>::reverse_iterator i = passedLanes.rbegin() + 1;
            while (leftLength > 0 && i != passedLanes.rend()) {
                myFurtherLanes.push_back(*i);
                leftLength -= (*i)->setPartialOccupation(this, leftLength);
                ++i;
            }
        }
        updateBestLanes();
        // bestLanes need to be updated before lane changing starts
        if (getLaneChangeModel().isChangingLanes()) {
            getLaneChangeModel().continueLaneChangeManeuver(moved);
        }
        setBlinkerInformation(); // needs updated bestLanes
        // State needs to be reset for all vehicles before the next call to MSEdgeControl::changeLanes
        getLaneChangeModel().prepareStep();
    }
    // State needs to be reset for all vehicles before the next call to MSEdgeControl::changeLanes
    return moved;
}


SUMOReal
MSVehicle::getSpaceTillLastStanding(const MSLane* l, bool& foundStopped) const {
    SUMOReal lengths = 0;
    const MSLane::VehCont& vehs = l->getVehiclesSecure();
    for (MSLane::VehCont::const_iterator i = vehs.begin(); i != vehs.end(); ++i) {
        if ((*i)->getSpeed() < SUMO_const_haltingSpeed && !(*i)->getLane()->getEdge().isRoundabout()) {
            foundStopped = true;
            const SUMOReal ret = (*i)->getPositionOnLane() - (*i)->getVehicleType().getLengthWithGap() - lengths;
            l->releaseVehicles();
            return ret;
        }
        lengths += (*i)->getVehicleType().getLengthWithGap();
    }
    l->releaseVehicles();
    return l->getLength() - lengths;
}


void
MSVehicle::checkRewindLinkLanes(const SUMOReal lengthsInFront, DriveItemVector& lfLinks) const {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gDebugSelectedVehicle == getID()) {
        int bla = 0;
    }
#endif
#ifdef HAVE_INTERNAL_LANES
    if (MSGlobals::gUsingInternalLanes && !myLane->getEdge().isRoundabout()) {
        bool hadVehicle = false;
        SUMOReal seenSpace = -lengthsInFront;

        bool foundStopped = false;
        // compute available space until a stopped vehicle is found
        // this is the sum of non-interal lane length minus in-between vehicle lenghts
        for (unsigned int i = 0; i < lfLinks.size(); ++i) {
            // skip unset links
            DriveProcessItem& item = lfLinks[i];
            if (item.myLink == 0 || foundStopped) {
                item.availableSpace = seenSpace;
                item.hadVehicle = hadVehicle;
                continue;
            }
            // get the next lane, determine whether it is an internal lane
            const MSLane* approachedLane = item.myLink->getViaLane();
            if (approachedLane != 0) {
                if (item.myLink->hasFoes() && item.myLink->keepClear()/* && item.myLink->willHaveBlockedFoe()*/) {
                    seenSpace = seenSpace - approachedLane->getBruttoVehLenSum();
                    hadVehicle |= approachedLane->getVehicleNumber() != 0;
                } else {
                    seenSpace = seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getBruttoVehLenSum() + approachedLane->getLength();
                    hadVehicle |= approachedLane->getVehicleNumber() != 0;
                }
                item.availableSpace = seenSpace;
                item.hadVehicle = hadVehicle;
                continue;
            }
            approachedLane = item.myLink->getLane();
            const MSVehicle* last = approachedLane->getLastVehicle();
            if (last == 0) {
                last = approachedLane->getPartialOccupator();
                if (last != 0) {
                    /// XXX MAX2 redundant?
                    item.availableSpace = MAX2(seenSpace, seenSpace + approachedLane->getPartialOccupatorEnd() + last->getCarFollowModel().brakeGap(last->getSpeed()));
                    hadVehicle = true;
                    /// XXX spaceTillLastStanding should already be covered by getPartialOccupatorEnd()
                    seenSpace = seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getBruttoVehLenSum() + approachedLane->getLength();
                    /// XXX why not check BRAKELIGHT?
                    if (last->myHaveToWaitOnNextLink) {
                        foundStopped = true;
                    }
                } else {
                    seenSpace += approachedLane->getLength();
                    item.availableSpace = seenSpace;
                }
            } else {
                if (last->signalSet(VEH_SIGNAL_BRAKELIGHT)) {
                    const SUMOReal lastBrakeGap = last->getCarFollowModel().brakeGap(approachedLane->getLastVehicle()->getSpeed());
                    const SUMOReal lastGap = last->getPositionOnLane() - last->getVehicleType().getLengthWithGap() + lastBrakeGap - last->getSpeed() * last->getCarFollowModel().getHeadwayTime();
                    item.availableSpace = MAX2(seenSpace, seenSpace + lastGap);
                    seenSpace += getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getBruttoVehLenSum() + approachedLane->getLength();
                } else {
                    seenSpace += getSpaceTillLastStanding(approachedLane, foundStopped);
                    item.availableSpace = seenSpace;
                }
                if (last->myHaveToWaitOnNextLink) {
                    foundStopped = true;
                }
                hadVehicle = true;
            }
            item.hadVehicle = hadVehicle;
        }


#ifdef DEBUG_VEHICLE_GUI_SELECTION
        if (gDebugSelectedVehicle == getID()) {
            int bla = 0;
        }
#endif
        // check which links allow continuation and add pass available to the previous item
        for (int i = (int)(lfLinks.size() - 1); i > 0; --i) {
            DriveProcessItem& item = lfLinks[i - 1];
            const bool canLeaveJunction = item.myLink->getViaLane() == 0 || lfLinks[i].mySetRequest;
            const bool opened = item.myLink != 0 && canLeaveJunction && (item.myLink->havePriority() ||
#ifndef NO_TRACI
                                (myInfluencer != 0 && !myInfluencer->getRespectJunctionPriority()) ||
#endif
                                item.myLink->opened(item.myArrivalTime, item.myArrivalSpeed,
                                                    item.getLeaveSpeed(), getVehicleType().getLength(),
                                                    getImpatience(), getCarFollowModel().getMaxDecel(), getWaitingTime()));
            bool allowsContinuation = item.myLink == 0 || item.myLink->isCont() || !lfLinks[i].hadVehicle || opened;
            if (!opened && item.myLink != 0) {
                if (i > 1) {
                    DriveProcessItem& item2 = lfLinks[i - 2];
                    if (item2.myLink != 0 && item2.myLink->isCont()) {
                        allowsContinuation = true;
                    }
                }
            }
            if (allowsContinuation) {
                item.availableSpace = lfLinks[i].availableSpace;
            }
        }

        // find removalBegin
        int removalBegin = -1;
        for (unsigned int i = 0; hadVehicle && i < lfLinks.size() && removalBegin < 0; ++i) {
            // skip unset links
            const DriveProcessItem& item = lfLinks[i];
            if (item.myLink == 0) {
                continue;
            }
            /*
            SUMOReal impatienceCorrection = MAX2(SUMOReal(0), SUMOReal(SUMOReal(myWaitingTime)));
            if (seenSpace<getVehicleType().getLengthWithGap()-impatienceCorrection/10.&&nextSeenNonInternal!=0) {
                removalBegin = lastLinkToInternal;
            }
            */

            const SUMOReal leftSpace = item.availableSpace - getVehicleType().getLengthWithGap();
            if (leftSpace < 0/* && item.myLink->willHaveBlockedFoe()*/) {
                SUMOReal impatienceCorrection = 0;
                /*
                if(item.myLink->getState()==LINKSTATE_MINOR) {
                    impatienceCorrection = MAX2(SUMOReal(0), STEPS2TIME(myWaitingTime));
                }
                */
                if (leftSpace < -impatienceCorrection / 10. && item.myLink->hasFoes() && item.myLink->keepClear()) {
                    removalBegin = i;
                }
                //removalBegin = i;
            }
        }
        // abort requests
        if (removalBegin != -1 && !(removalBegin == 0 && myLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL)) {
            while (removalBegin < (int)(lfLinks.size())) {
                const SUMOReal brakeGap = getCarFollowModel().brakeGap(myState.mySpeed) - getCarFollowModel().getHeadwayTime() * myState.mySpeed;
                lfLinks[removalBegin].myVLinkPass = lfLinks[removalBegin].myVLinkWait;
                if (lfLinks[removalBegin].myDistance >= brakeGap || (lfLinks[removalBegin].myDistance > 0 && myState.mySpeed < ACCEL2SPEED(getCarFollowModel().getMaxDecel()))) {
                    lfLinks[removalBegin].mySetRequest = false;
                }
                ++removalBegin;
            }
        }
    }
#else
    UNUSED_PARAMETER(lengthsInFront);
#endif
    for (DriveItemVector::iterator i = lfLinks.begin(); i != lfLinks.end(); ++i) {
        if ((*i).myLink != 0) {
            if ((*i).myLink->getState() == LINKSTATE_ALLWAY_STOP) {
                (*i).myArrivalTime += (SUMOTime)RandHelper::rand((size_t)2); // tie braker
            }
            (*i).myLink->setApproaching(this, (*i).myArrivalTime, (*i).myArrivalSpeed, (*i).getLeaveSpeed(),
                                        (*i).mySetRequest, (*i).myArrivalTimeBraking, (*i).myArrivalSpeedBraking, getWaitingTime());
        }
    }
}


void
MSVehicle::activateReminders(const MSMoveReminder::Notification reason) {
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (rem->first->getLane() != 0 && rem->first->getLane() != getLane()) {
#ifdef _DEBUG
            if (myTraceMoveReminders) {
                traceMoveReminder("notifyEnter_skipped", rem->first, rem->second, true);
            }
#endif
            ++rem;
        } else {
            if (rem->first->notifyEnter(*this, reason)) {
#ifdef _DEBUG
                if (myTraceMoveReminders) {
                    traceMoveReminder("notifyEnter", rem->first, rem->second, true);
                }
#endif
                ++rem;
            } else {
#ifdef _DEBUG
                if (myTraceMoveReminders) {
                    traceMoveReminder("notifyEnter", rem->first, rem->second, false);
                }
#endif
                rem = myMoveReminders.erase(rem);
            }
        }
    }
}


bool
MSVehicle::enterLaneAtMove(MSLane* enteredLane, bool onTeleporting) {
    myAmOnNet = !onTeleporting;
    // vaporizing edge?
    /*
    if (enteredLane->getEdge().isVaporizing()) {
        // yep, let's do the vaporization...
        myLane = enteredLane;
        return true;
    }
    */
    // move mover reminder one lane further
    adaptLaneEntering2MoveReminder(*enteredLane);
    // set the entered lane as the current lane
    myLane = enteredLane;

    // internal edges are not a part of the route...
    if (enteredLane->getEdge().getPurpose() != MSEdge::EDGEFUNCTION_INTERNAL) {
        ++myCurrEdge;
    }
    if (!onTeleporting) {
        activateReminders(MSMoveReminder::NOTIFICATION_JUNCTION);
    } else {
        activateReminders(MSMoveReminder::NOTIFICATION_TELEPORT);
        // normal move() isn't called so reset position here
        myState.myPos = 0;
        myCachedPosition = Position::INVALID;
    }
    return hasArrived();
}


void
MSVehicle::enterLaneAtLaneChange(MSLane* enteredLane) {
    myAmOnNet = true;
    myLane = enteredLane;
    myCachedPosition = Position::INVALID;
    // need to update myCurrentLaneInBestLanes
    updateBestLanes();
    // switch to and activate the new lane's reminders
    // keep OldLaneReminders
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane->getMoveReminders().begin(); rem != enteredLane->getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
    activateReminders(MSMoveReminder::NOTIFICATION_LANE_CHANGE);
    MSLane* lane = myLane;
    SUMOReal leftLength = getVehicleType().getLength() - myState.myPos;
    for (int i = 0; i < (int)myFurtherLanes.size(); i++) {
        if (lane != 0) {
            lane = lane->getLogicalPredecessorLane(myFurtherLanes[i]->getEdge());
        }
        if (lane != 0) {
            myFurtherLanes[i]->resetPartialOccupation(this);
            myFurtherLanes[i] = lane;
            leftLength -= (lane)->setPartialOccupation(this, leftLength);
        } else {
            // keep the old values, but ensure there is no shadow
            if (myLaneChangeModel->isChangingLanes()) {
                myLaneChangeModel->setNoShadowPartialOccupator(myFurtherLanes[i]);
            }
        }
    }
}


void
MSVehicle::enterLaneAtInsertion(MSLane* enteredLane, SUMOReal pos, SUMOReal speed, MSMoveReminder::Notification notification) {
    myState = State(pos, speed);
    myCachedPosition = Position::INVALID;
    assert(myState.myPos >= 0);
    assert(myState.mySpeed >= 0);
    myWaitingTime = 0;
    myLane = enteredLane;
    myAmOnNet = true;
    // set and activate the new lane's reminders
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane->getMoveReminders().begin(); rem != enteredLane->getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
    activateReminders(notification);
    std::string msg;
    if (MSGlobals::gCheckRoutes && !hasValidRoute(msg)) {
        throw ProcessError("Vehicle '" + getID() + "' has no valid route. " + msg);
    }
    // build the list of lanes the vehicle is lapping into
    SUMOReal leftLength = myType->getLength() - pos;
    MSLane* clane = enteredLane;
    while (leftLength > 0) {
        clane = clane->getLogicalPredecessorLane();
        if (clane == 0) {
            break;
        }
        myFurtherLanes.push_back(clane);
        leftLength -= (clane)->setPartialOccupation(this, leftLength);
    }
}


void
MSVehicle::leaveLane(const MSMoveReminder::Notification reason) {
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (rem->first->notifyLeave(*this, myState.myPos + rem->second, reason)) {
#ifdef _DEBUG
            if (myTraceMoveReminders) {
                traceMoveReminder("notifyLeave", rem->first, rem->second, true);
            }
#endif
            ++rem;
        } else {
#ifdef _DEBUG
            if (myTraceMoveReminders) {
                traceMoveReminder("notifyLeave", rem->first, rem->second, false);
            }
#endif
            rem = myMoveReminders.erase(rem);
        }
    }
    if (reason != MSMoveReminder::NOTIFICATION_JUNCTION && reason != MSMoveReminder::NOTIFICATION_LANE_CHANGE) {
        // @note. In case of lane change, myFurtherLanes and partial occupation
        // are handled in enterLaneAtLaneChange()
        for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
            (*i)->resetPartialOccupation(this);
        }
        myFurtherLanes.clear();
    }
    if (reason >= MSMoveReminder::NOTIFICATION_TELEPORT) {
        myAmOnNet = false;
    }
    if (reason != MSMoveReminder::NOTIFICATION_PARKING && resumeFromStopping()) {
        WRITE_WARNING("Vehicle '" + getID() + "' aborts stop.");
    }
    if (reason != MSMoveReminder::NOTIFICATION_PARKING && reason != MSMoveReminder::NOTIFICATION_LANE_CHANGE) {
        while (!myStops.empty() && myStops.front().edge == myCurrEdge) {
            WRITE_WARNING("Vehicle '" + getID() + "' skips stop on lane '" + myStops.front().lane->getID() 
                    + "' time=" + time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".")
            myStops.pop_front();
        }
    }
}


MSAbstractLaneChangeModel&
MSVehicle::getLaneChangeModel() {
    return *myLaneChangeModel;
}


const MSAbstractLaneChangeModel&
MSVehicle::getLaneChangeModel() const {
    return *myLaneChangeModel;
}


const std::vector<MSVehicle::LaneQ>&
MSVehicle::getBestLanes() const {
    return *myBestLanes.begin();
}


void
MSVehicle::updateBestLanes(bool forceRebuild, const MSLane* startLane) {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gDebugSelectedVehicle == getID()) {
        int bla = 0;
        myLastBestLanesEdge = 0;
    }
#endif
    if (startLane == 0) {
        startLane = myLane;
    }
    assert(startLane != 0);
    if (myBestLanes.size() > 0 && !forceRebuild && myLastBestLanesEdge == &startLane->getEdge()) {
        updateOccupancyAndCurrentBestLane(startLane);
        return;
    }
    if (startLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL) {
        if (myBestLanes.size() == 0 || forceRebuild) {
            // rebuilt from previous non-internal lane (may backtrack twice if behind an internal junction)
            updateBestLanes(true, startLane->getLogicalPredecessorLane());
        }
        if (myLastBestLanesInternalLane == startLane && !forceRebuild) {
            return;
        }
        // adapt best lanes to fit the current internal edge:
        // keep the entries that are reachable from this edge
        const MSEdge* nextEdge = startLane->getInternalFollower();
        assert(nextEdge->getPurpose() != MSEdge::EDGEFUNCTION_INTERNAL);
        for (std::vector<std::vector<LaneQ> >::iterator it = myBestLanes.begin(); it != myBestLanes.end();) {
            std::vector<LaneQ>& lanes = *it;
            assert(lanes.size() > 0);
            if (&(lanes[0].lane->getEdge()) == nextEdge) {
                // keep those lanes which are successors of internal lanes from the edge of startLane
                std::vector<LaneQ> oldLanes = lanes;
                lanes.clear();
                const std::vector<MSLane*>& sourceLanes = startLane->getEdge().getLanes();
                for (std::vector<MSLane*>::const_iterator it_source = sourceLanes.begin(); it_source != sourceLanes.end(); ++it_source) {
                    for (std::vector<LaneQ>::iterator it_lane = oldLanes.begin(); it_lane != oldLanes.end(); ++it_lane) {
                        if ((*it_source)->getLinkCont()[0]->getLane() == (*it_lane).lane) {
                            lanes.push_back(*it_lane);
                            break;
                        }
                    }
                }
                assert(lanes.size() == startLane->getEdge().getLanes().size());
                // patch invalid bestLaneOffset and updated myCurrentLaneInBestLanes
                for (int i = 0; i < (int)lanes.size(); ++i) {
                    if (i + lanes[i].bestLaneOffset < 0) {
                        lanes[i].bestLaneOffset = -i;
                    }
                    if (i + lanes[i].bestLaneOffset >= (int)lanes.size()) {
                        lanes[i].bestLaneOffset = (int)lanes.size() - i - 1;
                    }
                    assert(i + lanes[i].bestLaneOffset >= 0);
                    assert(i + lanes[i].bestLaneOffset < (int)lanes.size());
                    if (lanes[i].bestContinuations[0] != 0) {
                        // patch length of bestContinuation to match expectations (only once)
                        lanes[i].bestContinuations.insert(lanes[i].bestContinuations.begin(), (MSLane*)0);
                    }
                    if (startLane->getLinkCont()[0]->getLane() == lanes[i].lane) {
                        myCurrentLaneInBestLanes = lanes.begin() + i;
                    }
                    assert(&(lanes[i].lane->getEdge()) == nextEdge);
                }
                myLastBestLanesInternalLane = startLane;
                updateOccupancyAndCurrentBestLane(startLane);
                return;
            } else {
                // remove passed edges
                it = myBestLanes.erase(it);
            }
        }
        assert(false); // should always find the next edge
    }
    // start rebuilding
    myLastBestLanesEdge = &startLane->getEdge();
    myBestLanes.clear();

    // get information about the next stop
    const MSEdge* nextStopEdge = 0;
    const MSLane* nextStopLane = 0;
    SUMOReal nextStopPos = 0;
    if (!myStops.empty()) {
        const Stop& nextStop = myStops.front();
        nextStopLane = nextStop.lane;
        nextStopEdge = &nextStopLane->getEdge();
        nextStopPos = nextStop.startPos;
    }
    if (myParameter->arrivalLaneProcedure == ARRIVAL_LANE_GIVEN && nextStopEdge == 0) {
        nextStopEdge = *(myRoute->end() - 1);
        nextStopLane = nextStopEdge->getLanes()[myParameter->arrivalLane];
        nextStopPos = myArrivalPos;
    }
    if (nextStopEdge != 0) {
        // make sure that the "wrong" lanes get a penalty. (penalty needs to be
        // large enough to overcome a magic threshold in MSLCM_DK2004.cpp:383)
        nextStopPos = MAX2(POSITION_EPS, MIN2((SUMOReal)nextStopPos, (SUMOReal)(nextStopEdge->getLength() - 2 * POSITION_EPS)));
    }

    // go forward along the next lanes;
    int seen = 0;
    SUMOReal seenLength = 0;
    bool progress = true;
    for (MSRouteIterator ce = myCurrEdge; progress;) {
        std::vector<LaneQ> currentLanes;
        const std::vector<MSLane*>* allowed = 0;
        const MSEdge* nextEdge = 0;
        if (ce != myRoute->end() && ce + 1 != myRoute->end()) {
            nextEdge = *(ce + 1);
            allowed = (*ce)->allowedLanes(*nextEdge, myType->getVehicleClass());
        }
        const std::vector<MSLane*>& lanes = (*ce)->getLanes();
        for (std::vector<MSLane*>::const_iterator i = lanes.begin(); i != lanes.end(); ++i) {
            LaneQ q;
            MSLane* cl = *i;
            q.lane = cl;
            q.bestContinuations.push_back(cl);
            q.bestLaneOffset = 0;
            q.length = cl->allowsVehicleClass(myType->getVehicleClass()) ? cl->getLength() : 0;
            q.allowsContinuation = allowed == 0 || find(allowed->begin(), allowed->end(), cl) != allowed->end();
            currentLanes.push_back(q);
        }
        //
        if (nextStopEdge == *ce) {
            progress = false;
            for (std::vector<LaneQ>::iterator q = currentLanes.begin(); q != currentLanes.end(); ++q) {
                if (nextStopLane != 0 && nextStopLane != (*q).lane) {
                    (*q).allowsContinuation = false;
                    (*q).length = nextStopPos;
                }
            }
        }

        myBestLanes.push_back(currentLanes);
        ++seen;
        seenLength += currentLanes[0].lane->getLength();
        ++ce;
        progress &= (seen <= 4 || seenLength < 3000);
        progress &= seen <= 8;
        progress &= ce != myRoute->end();
        /*
        if(progress) {
          progress &= (currentLanes.size()!=1||(*ce)->getLanes().size()!=1);
        }
        */
    }

    // we are examining the last lane explicitly
    if (myBestLanes.size() != 0) {
        SUMOReal bestLength = -1;
        int bestThisIndex = 0;
        int index = 0;
        std::vector<LaneQ>& last = myBestLanes.back();
        for (std::vector<LaneQ>::iterator j = last.begin(); j != last.end(); ++j, ++index) {
            if ((*j).length > bestLength) {
                bestLength = (*j).length;
                bestThisIndex = index;
            }
        }
        index = 0;
        for (std::vector<LaneQ>::iterator j = last.begin(); j != last.end(); ++j, ++index) {
            if ((*j).length < bestLength) {
                (*j).bestLaneOffset = bestThisIndex - index;
            }
        }
    }

    // go backward through the lanes
    // track back best lane and compute the best prior lane(s)
    for (std::vector<std::vector<LaneQ> >::reverse_iterator i = myBestLanes.rbegin() + 1; i != myBestLanes.rend(); ++i) {
        std::vector<LaneQ>& nextLanes = (*(i - 1));
        std::vector<LaneQ>& clanes = (*i);
        MSEdge& cE = clanes[0].lane->getEdge();
        int index = 0;
        SUMOReal bestConnectedLength = -1;
        SUMOReal bestLength = -1;
        for (std::vector<LaneQ>::iterator j = nextLanes.begin(); j != nextLanes.end(); ++j, ++index) {
            if ((*j).lane->isApproachedFrom(&cE) && bestConnectedLength < (*j).length) {
                bestConnectedLength = (*j).length;
            }
            if (bestLength < (*j).length) {
                bestLength = (*j).length;
            }
        }
        // compute index of the best lane (highest length and least offset from the best next lane)
        int bestThisIndex = 0;
        if (bestConnectedLength > 0) {
            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                LaneQ bestConnectedNext;
                bestConnectedNext.length = -1;
                if ((*j).allowsContinuation) {
                    for (std::vector<LaneQ>::const_iterator m = nextLanes.begin(); m != nextLanes.end(); ++m) {
                        if ((*m).lane->isApproachedFrom(&cE, (*j).lane)) {
                            if (bestConnectedNext.length < (*m).length || (bestConnectedNext.length == (*m).length && abs(bestConnectedNext.bestLaneOffset) > abs((*m).bestLaneOffset))) {
                                bestConnectedNext = *m;
                            }
                        }
                    }
                    if (bestConnectedNext.length == bestConnectedLength && abs(bestConnectedNext.bestLaneOffset) < 2) {
                        (*j).length += bestLength;
                    } else {
                        (*j).length += bestConnectedNext.length;
                    }
                    (*j).bestLaneOffset = bestConnectedNext.bestLaneOffset;
                }
                if (clanes[bestThisIndex].length < (*j).length || (clanes[bestThisIndex].length == (*j).length && abs(clanes[bestThisIndex].bestLaneOffset) > abs((*j).bestLaneOffset))) {
                    bestThisIndex = index;
                }
                copy(bestConnectedNext.bestContinuations.begin(), bestConnectedNext.bestContinuations.end(), back_inserter((*j).bestContinuations));
            }

        } else {
            int bestNextIndex = 0;
            int bestDistToNeeded = (int) clanes.size();
            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                if ((*j).allowsContinuation) {
                    int nextIndex = 0;
                    for (std::vector<LaneQ>::const_iterator m = nextLanes.begin(); m != nextLanes.end(); ++m, ++nextIndex) {
                        if ((*m).lane->isApproachedFrom(&cE, (*j).lane)) {
                            if (bestDistToNeeded > abs((*m).bestLaneOffset)) {
                                bestDistToNeeded = abs((*m).bestLaneOffset);
                                bestThisIndex = index;
                                bestNextIndex = nextIndex;
                            }
                        }
                    }
                }
            }
            clanes[bestThisIndex].length += nextLanes[bestNextIndex].length;
            copy(nextLanes[bestNextIndex].bestContinuations.begin(), nextLanes[bestNextIndex].bestContinuations.end(), back_inserter(clanes[bestThisIndex].bestContinuations));

        }
        // set bestLaneOffset for all lanes
        index = 0;
        for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
            if ((*j).length < clanes[bestThisIndex].length || ((*j).length == clanes[bestThisIndex].length && abs((*j).bestLaneOffset) > abs(clanes[bestThisIndex].bestLaneOffset))) {
                (*j).bestLaneOffset = bestThisIndex - index;
            } else {
                (*j).bestLaneOffset = 0;
            }
        }
    }
    updateOccupancyAndCurrentBestLane(startLane);
    return;
}


void
MSVehicle::updateOccupancyAndCurrentBestLane(const MSLane* startLane) {
    std::vector<LaneQ>& currLanes = *myBestLanes.begin();
    std::vector<LaneQ>::iterator i;
    for (i = currLanes.begin(); i != currLanes.end(); ++i) {
        SUMOReal nextOccupation = 0;
        for (std::vector<MSLane*>::const_iterator j = (*i).bestContinuations.begin() + 1; j != (*i).bestContinuations.end(); ++j) {
            nextOccupation += (*j)->getBruttoVehLenSum();
        }
        (*i).nextOccupation = nextOccupation;
        if ((*i).lane == startLane) {
            myCurrentLaneInBestLanes = i;
        }
    }
}


const std::vector<MSLane*>&
MSVehicle::getBestLanesContinuation() const {
    if (myBestLanes.empty() || myBestLanes[0].empty()) {
        return myEmptyLaneVector;
    }
    return (*myCurrentLaneInBestLanes).bestContinuations;
}


const std::vector<MSLane*>&
MSVehicle::getBestLanesContinuation(const MSLane* const l) const {
    const MSLane* lane = l;
    if (lane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL) {
        // internal edges are not kept inside the bestLanes structure
        lane = lane->getLinkCont()[0]->getLane();
    }
    if (myBestLanes.size() == 0) {
        return myEmptyLaneVector;
    }
    for (std::vector<LaneQ>::const_iterator i = myBestLanes[0].begin(); i != myBestLanes[0].end(); ++i) {
        if ((*i).lane == lane) {
            return (*i).bestContinuations;
        }
    }
    return myEmptyLaneVector;
}


int
MSVehicle::getBestLaneOffset() const {
    if (myBestLanes.empty() || myBestLanes[0].empty()) {
        return 0;
    } else {
        return (*myCurrentLaneInBestLanes).bestLaneOffset;
    }
}


void
MSVehicle::adaptBestLanesOccupation(int laneIndex, SUMOReal density) {
    std::vector<MSVehicle::LaneQ>& preb = myBestLanes.front();
    assert(laneIndex < (int)preb.size());
    preb[laneIndex].occupation = density + preb[laneIndex].nextOccupation;
}


bool
MSVehicle::fixPosition() {
    if (getPositionOnLane() > myLane->getLength()) {
        myState.myPos = myLane->getLength();
        myCachedPosition = Position::INVALID;
        return true;
    }
    return false;
}


SUMOReal
MSVehicle::getDistanceToPosition(SUMOReal destPos, const MSEdge* destEdge) {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    SUMOReal distance = 1000000.;
#else
    SUMOReal distance = std::numeric_limits<SUMOReal>::max();
#endif
    if (isOnRoad() && destEdge != NULL) {
        if (&myLane->getEdge() == *myCurrEdge) {
            // vehicle is on a normal edge
            distance = myRoute->getDistanceBetween(getPositionOnLane(), destPos, *myCurrEdge, destEdge);
        } else {
            // vehicle is on inner junction edge
            distance = myLane->getLength() - getPositionOnLane();
            distance += myRoute->getDistanceBetween(0, destPos, *(myCurrEdge + 1), destEdge);
        }
    }
    return distance;
}


std::pair<const MSVehicle* const, SUMOReal>
MSVehicle::getLeader(SUMOReal dist) const {
    if (myLane == 0) {
        return std::make_pair(static_cast<const MSVehicle*>(0), -1);
    }
    if (dist == 0) {
        dist = getCarFollowModel().brakeGap(getSpeed()) + getVehicleType().getMinGap();
    }
    const MSVehicle* lead = 0;
    const MSLane::VehCont& vehs = myLane->getVehiclesSecure();
    MSLane::VehCont::const_iterator pred = std::find(vehs.begin(), vehs.end(), this) + 1;
    if (pred != vehs.end()) {
        lead = *pred;
    }
    myLane->releaseVehicles();
    if (lead != 0) {
        return std::make_pair(lead, lead->getPositionOnLane() - lead->getVehicleType().getLength() - getPositionOnLane() - getVehicleType().getMinGap());
    }
    const SUMOReal seen = myLane->getLength() - getPositionOnLane();
    const std::vector<MSLane*>& bestLaneConts = getBestLanesContinuation(myLane);
    return myLane->getLeaderOnConsecutive(dist, seen, getSpeed(), *this, bestLaneConts);
}


SUMOReal
MSVehicle::getTimeGap() const {
    std::pair<const MSVehicle* const, SUMOReal> leaderInfo = getLeader();
    if (leaderInfo.first == 0 || getSpeed() == 0) {
        return -1;
    }
    return (leaderInfo.second + getVehicleType().getMinGap()) / getSpeed();
}


SUMOReal
MSVehicle::getCO2Emissions() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::CO2, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getCOEmissions() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::CO, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getHCEmissions() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::HC, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getNOxEmissions() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::NO_X, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getPMxEmissions() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::PM_X, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getFuelConsumption() const {
    return PollutantsInterface::compute(myType->getEmissionClass(), PollutantsInterface::FUEL, myState.speed(), myAcceleration, getSlope());
}


SUMOReal
MSVehicle::getHarmonoise_NoiseEmissions() const {
    return HelpersHarmonoise::computeNoise(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


void
MSVehicle::addPerson(MSTransportable* person) {
    if (myPersonDevice == 0) {
        myPersonDevice = MSDevice_Person::buildVehicleDevices(*this, myDevices);
        myMoveReminders.push_back(std::make_pair(myPersonDevice, 0.));
    }
    myPersonDevice->addPerson(person);
    if (myStops.size() > 0 && myStops.front().reached && myStops.front().triggered) {
        unsigned int numExpected = (unsigned int) myStops.front().awaitedPersons.size();
        if (numExpected != 0) {
            // I added the if-statement and number retrieval, assuming that it should be a "conditional short jump" only and
            //  in most cases we won't have the list of expected passenger - only for simulating car-sharing, probably.
            //  Bus drivers usually do not know the names of the passengers.
            myStops.front().awaitedPersons.erase(person->getID());
            numExpected = (unsigned int) myStops.front().awaitedPersons.size();
        }
        if (numExpected == 0) {
            myStops.front().duration = 0;
        }
    }
}

void
MSVehicle::addContainer(MSTransportable* container) {
    if (myContainerDevice == 0) {
        myContainerDevice = MSDevice_Container::buildVehicleDevices(*this, myDevices);
        myMoveReminders.push_back(std::make_pair(myContainerDevice, 0.));
    }
    myContainerDevice->addContainer(container);
    if (myStops.size() > 0 && myStops.front().reached && myStops.front().containerTriggered) {
        unsigned int numExpected = (unsigned int) myStops.front().awaitedContainers.size();
        if (numExpected != 0) {
            myStops.front().awaitedContainers.erase(container->getID());
            numExpected = (unsigned int) myStops.front().awaitedContainers.size();
        }
        if (numExpected == 0) {
            myStops.front().duration = 0;
        }
    }
}


unsigned int
MSVehicle::getPersonNumber() const {
    unsigned int boarded = myPersonDevice == 0 ? 0 : myPersonDevice->size();
    return boarded + myParameter->personNumber;
}

unsigned int
MSVehicle::getContainerNumber() const {
    unsigned int loaded = myContainerDevice == 0 ? 0 : myContainerDevice->size();
    return loaded + myParameter->containerNumber;
}


void
MSVehicle::setBlinkerInformation() {
    switchOffSignal(VEH_SIGNAL_BLINKER_RIGHT | VEH_SIGNAL_BLINKER_LEFT);
    int state = getLaneChangeModel().getOwnState();
    if ((state & LCA_LEFT) != 0) {
        switchOnSignal(VEH_SIGNAL_BLINKER_LEFT);
    } else if ((state & LCA_RIGHT) != 0) {
        switchOnSignal(VEH_SIGNAL_BLINKER_RIGHT);
    } else if (getLaneChangeModel().isChangingLanes()) {
        if (getLaneChangeModel().getLaneChangeDirection() == 1) {
            switchOnSignal(VEH_SIGNAL_BLINKER_LEFT);
        } else {
            switchOnSignal(VEH_SIGNAL_BLINKER_RIGHT);
        }
    } else {
        const MSLane* lane = getLane();
        MSLinkCont::const_iterator link = MSLane::succLinkSec(*this, 1, *lane, getBestLanesContinuation());
        if (link != lane->getLinkCont().end() && lane->getLength() - getPositionOnLane() < lane->getVehicleMaxSpeed(this) * (SUMOReal) 7.) {
            switch ((*link)->getDirection()) {
                case LINKDIR_TURN:
                case LINKDIR_LEFT:
                case LINKDIR_PARTLEFT:
                    switchOnSignal(VEH_SIGNAL_BLINKER_LEFT);
                    break;
                case LINKDIR_RIGHT:
                case LINKDIR_PARTRIGHT:
                    switchOnSignal(VEH_SIGNAL_BLINKER_RIGHT);
                    break;
                default:
                    break;
            }
        }
    }

}


void
MSVehicle::replaceVehicleType(MSVehicleType* type) {
    if (myType->amVehicleSpecific()) {
        delete myType;
    }
    myType = type;
}

unsigned int
MSVehicle::getLaneIndex() const {
    std::vector<MSLane*>::const_iterator laneP = std::find(myLane->getEdge().getLanes().begin(), myLane->getEdge().getLanes().end(), myLane);
    return (unsigned int) std::distance(myLane->getEdge().getLanes().begin(), laneP);
}


void
MSVehicle::setTentativeLaneAndPosition(MSLane* lane, const SUMOReal pos) {
    assert(lane != 0);
    myLane = lane;
    myState.myPos = pos;
}


#ifndef NO_TRACI
bool
MSVehicle::addTraciStop(MSLane* const lane, const SUMOReal startPos, const SUMOReal endPos, const SUMOTime duration, const SUMOTime until,
                        const bool parking, const bool triggered, const bool containerTriggered, std::string& errorMsg) {
    //if the stop exists update the duration
    for (std::list<Stop>::iterator iter = myStops.begin(); iter != myStops.end(); iter++) {
        if (iter->lane == lane && fabs(iter->endPos - endPos) < POSITION_EPS) {
            if (duration == 0 && !iter->reached) {
                myStops.erase(iter);
            } else {
                iter->duration = duration;
            }
            return true;
        }
    }

    SUMOVehicleParameter::Stop newStop;
    newStop.lane = lane->getID();
    newStop.startPos = startPos;
    newStop.endPos = endPos;
    newStop.duration = duration;
    newStop.until = until;
    newStop.triggered = triggered;
    newStop.containerTriggered = containerTriggered;
    newStop.parking = parking;
    newStop.index = STOP_INDEX_FIT;
    const bool result = addStop(newStop, errorMsg);
    if (myLane != 0) {
        updateBestLanes(true);
    }
    return result;
}


bool
MSVehicle::addTraciBusOrContainerStop(const std::string& stopId, const SUMOTime duration, const SUMOTime until, const bool parking,
                                      const bool triggered, const bool containerTriggered, const bool isContainerStop, std::string& errorMsg) {
    //if the stop exists update the duration
    for (std::list<Stop>::iterator iter = myStops.begin(); iter != myStops.end(); iter++) {
        const Named* const stop = isContainerStop ? (Named*)iter->containerstop : iter->busstop;
        if (stop != 0 && stop->getID() == stopId) {
            if (duration == 0 && !iter->reached) {
                myStops.erase(iter);
            } else {
                iter->duration = duration;
            }
            return true;
        }
    }

    SUMOVehicleParameter::Stop newStop;
    MSStoppingPlace* bs = 0;
    if (isContainerStop) {
        newStop.containerstop = stopId;
        bs = MSNet::getInstance()->getContainerStop(stopId);
        if (bs == 0) {
            errorMsg = "The container stop '" + stopId + "' is not known for vehicle '" + getID() + "'";
            return false;
        }
    } else {
        newStop.busstop = stopId;
        bs = MSNet::getInstance()->getBusStop(stopId);
        if (bs == 0) {
            errorMsg = "The bus stop '" + stopId + "' is not known for vehicle '" + getID() + "'";
            return false;
        }
    }
    newStop.duration = duration;
    newStop.until = until;
    newStop.triggered = triggered;
    newStop.containerTriggered = containerTriggered;
    newStop.parking = parking;
    newStop.index = STOP_INDEX_FIT;
    newStop.lane = bs->getLane().getID();
    newStop.endPos = bs->getEndLanePosition();
    newStop.startPos = bs->getBeginLanePosition();
    const bool result = addStop(newStop, errorMsg);
    if (myLane != 0) {
        updateBestLanes(true);
    }
    return result;
}


bool
MSVehicle::resumeFromStopping() {
    if (isStopped()) {
        if (myAmRegisteredAsWaitingForPerson) {
            MSNet::getInstance()->getVehicleControl().unregisterOneWaitingForPerson();
            myAmRegisteredAsWaitingForPerson = false;
        }
        if (myAmRegisteredAsWaitingForContainer) {
            MSNet::getInstance()->getVehicleControl().unregisterOneWaitingForContainer();
            myAmRegisteredAsWaitingForContainer = false;
        }
        // we have waited long enough and fulfilled any passenger-requirements
        if (myStops.front().busstop != 0) {
            // inform bus stop about leaving it
            myStops.front().busstop->leaveFrom(this);
        }
        // we have waited long enough and fulfilled any container-requirements
        if (myStops.front().containerstop != 0) {
            // inform container stop about leaving it
            myStops.front().containerstop->leaveFrom(this);
        }
        // the current stop is no longer valid
        MSNet::getInstance()->getVehicleControl().removeWaiting(&myLane->getEdge(), this);
        myStops.pop_front();
        // do not count the stopping time towards gridlock time.
        // Other outputs use an independent counter and are not affected.
        myWaitingTime = 0;
        // maybe the next stop is on the same edge; let's rebuild best lanes
        updateBestLanes(true);
        // continue as wished...
        MSNet::getInstance()->informVehicleStateListener(this, MSNet::VEHICLE_STATE_ENDING_STOP);
        return true;
    }
    return false;
}


MSVehicle::Stop&
MSVehicle::getNextStop() {
    return myStops.front();
}


MSVehicle::Influencer&
MSVehicle::getInfluencer() {
    if (myInfluencer == 0) {
        myInfluencer = new Influencer();
    }
    return *myInfluencer;
}


SUMOReal
MSVehicle::getSpeedWithoutTraciInfluence() const {
    if (myInfluencer != 0) {
        return myInfluencer->getOriginalSpeed();
    }
    return myState.mySpeed;
}


int
MSVehicle::influenceChangeDecision(int state) {
    if (hasInfluencer()) {
        state = getInfluencer().influenceChangeDecision(
                    MSNet::getInstance()->getCurrentTimeStep(),
                    myLane->getEdge(),
                    getLaneIndex(),
                    state);
    }
    return state;
}
#endif


void
MSVehicle::saveState(OutputDevice& out) {
    MSBaseVehicle::saveState(out);
    // here starts the vehicle internal part (see loading)
    std::vector<SUMOTime> internals;
    internals.push_back(myDeparture);
    internals.push_back((SUMOTime)distance(myRoute->begin(), myCurrEdge));
    out.writeAttr(SUMO_ATTR_STATE, toString(internals));
    out.writeAttr(SUMO_ATTR_POSITION, myState.myPos);
    out.writeAttr(SUMO_ATTR_SPEED, myState.mySpeed);
    out.closeTag();
}


void
MSVehicle::loadState(const SUMOSAXAttributes& attrs, const SUMOTime offset) {
    if (!attrs.hasAttribute(SUMO_ATTR_POSITION)) {
        throw ProcessError("Error: Invalid vehicles in state (may be a meso state)!");
    }
    unsigned int routeOffset;
    std::istringstream bis(attrs.getString(SUMO_ATTR_STATE));
    bis >> myDeparture;
    bis >> routeOffset;
    if (hasDeparted()) {
        myDeparture -= offset;
        myCurrEdge += routeOffset;
    }
    myState.myPos = attrs.getFloat(SUMO_ATTR_POSITION);
    myState.mySpeed = attrs.getFloat(SUMO_ATTR_SPEED);
    // no need to reset myCachedPosition here since state loading happens directly after creation
}


/****************************************************************************/
