/****************************************************************************/
/// @file    RORouteHandler.cpp
/// @author  Daniel Krajzewicz
/// @author  Jakob Erdmann
/// @author  Sascha Krieg
/// @author  Michael Behrisch
/// @date    Mon, 9 Jul 2001
/// @version $Id$
///
// Parser and container for routes during their loading
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

#include <string>
#include <map>
#include <vector>
#include <iostream>
#include <utils/iodevices/OutputDevice.h>
#include <utils/xml/SUMOSAXHandler.h>
#include <utils/xml/SUMOXMLDefinitions.h>
#include <utils/common/MsgHandler.h>
#include <utils/common/StringTokenizer.h>
#include <utils/common/UtilExceptions.h>
#include <utils/options/OptionsCont.h>
#include <utils/xml/SUMOVehicleParserHelper.h>
#include <utils/xml/SUMOSAXReader.h>
#include <utils/xml/XMLSubSys.h>
#include <utils/iodevices/OutputDevice_String.h>
#include "RONet.h"
#include "ROLane.h"
#include "RORouteHandler.h"

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS


// ===========================================================================
// method definitions
// ===========================================================================
RORouteHandler::RORouteHandler(RONet& net, const std::string& file,
                               const bool tryRepair,
                               const bool emptyDestinationsAllowed,
                               const bool ignoreErrors) :
    SUMORouteHandler(file),
    myPedestrianRouter(0),
    myNet(net),
    myActivePlan(0),
    myActiveContainerPlan(0),
    myActivePlanSize(0),
    myActiveContainerPlanSize(0),
    myTryRepair(tryRepair),
    myEmptyDestinationsAllowed(emptyDestinationsAllowed),
    myErrorOutput(ignoreErrors ? MsgHandler::getWarningInstance() : MsgHandler::getErrorInstance()),
    myCurrentVTypeDistribution(0),
    myCurrentAlternatives(0) {
    myActiveRoute.reserve(100);
}


RORouteHandler::~RORouteHandler() {
    delete myPedestrianRouter;
}


void
RORouteHandler::parseFromViaTo(std::string element,
                               const SUMOSAXAttributes& attrs) {
    myActiveRoute.clear();
    bool useTaz = OptionsCont::getOptions().getBool("with-taz");
    if (useTaz && !myVehicleParameter->wasSet(VEHPARS_FROM_TAZ_SET) && !myVehicleParameter->wasSet(VEHPARS_TO_TAZ_SET)) {
        WRITE_WARNING("Taz usage was requested but no taz present in " + element + " '" + myVehicleParameter->id + "'!");
        useTaz = false;
    }
    bool ok = true;
    if ((useTaz || !attrs.hasAttribute(SUMO_ATTR_FROM)) && myVehicleParameter->wasSet(VEHPARS_FROM_TAZ_SET)) {
        const ROEdge* fromTaz = myNet.getEdge(myVehicleParameter->fromTaz + "-source");
        if (fromTaz == 0) {
            myErrorOutput->inform("Source taz '" + myVehicleParameter->fromTaz + "' not known for " + element + " '" + myVehicleParameter->id + "'!");
        } else if (fromTaz->getNumSuccessors() == 0) {
            myErrorOutput->inform("Source taz '" + myVehicleParameter->fromTaz + "' has no outgoing edges for " + element + " '" + myVehicleParameter->id + "'!");
        } else {
            myActiveRoute.push_back(fromTaz);
        }
    } else {
        parseEdges(attrs.getOpt<std::string>(SUMO_ATTR_FROM, myVehicleParameter->id.c_str(), ok, "", true),
                   myActiveRoute, "for " + element + " '" + myVehicleParameter->id + "'");
    }
    if (!attrs.hasAttribute(SUMO_ATTR_VIA)) {
        myInsertStopEdgesAt = (int)myActiveRoute.size();
    }
    parseEdges(attrs.getOpt<std::string>(SUMO_ATTR_VIA, myVehicleParameter->id.c_str(), ok, "", true),
               myActiveRoute, "for " + element + " '" + myVehicleParameter->id + "'");
    if ((useTaz || !attrs.hasAttribute(SUMO_ATTR_TO)) && myVehicleParameter->wasSet(VEHPARS_TO_TAZ_SET)) {
        const ROEdge* toTaz = myNet.getEdge(myVehicleParameter->toTaz + "-sink");
        if (toTaz == 0) {
            myErrorOutput->inform("Sink taz '" + myVehicleParameter->toTaz + "' not known for " + element + " '" + myVehicleParameter->id + "'!");
        } else if (toTaz->getNumPredecessors() == 0) {
            myErrorOutput->inform("Sink taz '" + myVehicleParameter->toTaz + "' has no incoming edges for " + element + " '" + myVehicleParameter->id + "'!");
        } else {
            myActiveRoute.push_back(toTaz);
        }
    } else {
        parseEdges(attrs.getOpt<std::string>(SUMO_ATTR_TO, myVehicleParameter->id.c_str(), ok, "", true),
                   myActiveRoute, "for " + element + " '" + myVehicleParameter->id + "'");
    }
    myActiveRouteID = "!" + myVehicleParameter->id;
    if (myVehicleParameter->routeid == "") {
        myVehicleParameter->routeid = myActiveRouteID;
    }
}


void
RORouteHandler::myStartElement(int element,
                               const SUMOSAXAttributes& attrs) {
    SUMORouteHandler::myStartElement(element, attrs);
    switch (element) {
        case SUMO_TAG_PERSON:
            myActivePlan = new OutputDevice_String(false, 1);
            myActivePlanSize = 0;
            myActivePlan->openTag(SUMO_TAG_PERSON);
            (*myActivePlan) << attrs;
            break;
        case SUMO_TAG_RIDE: {
            myActivePlan->openTag(SUMO_TAG_RIDE);
            (*myActivePlan) << attrs;
            myActivePlan->closeTag();
            myActivePlanSize++;
            break;
        }
        case SUMO_TAG_WALK: {
            if (attrs.hasAttribute(SUMO_ATTR_EDGES)) {
                // copy walk as it is
                // XXX allow --repair?
                myActivePlan->openTag(SUMO_TAG_WALK);
                (*myActivePlan) << attrs;
                myActivePlan->closeTag();
                myActivePlanSize++;
            } else {
                routePedestrian(attrs, *myActivePlan);
            }
            break;
        }
        case SUMO_TAG_CONTAINER:
            myActiveContainerPlan = new OutputDevice_String(false, 1);
            myActiveContainerPlanSize = 0;
            myActiveContainerPlan->openTag(SUMO_TAG_CONTAINER);
            (*myActiveContainerPlan) << attrs;
            break;
        case SUMO_TAG_TRANSPORT: {
            myActiveContainerPlan->openTag(SUMO_TAG_TRANSPORT);
            (*myActiveContainerPlan) << attrs;
            myActiveContainerPlan->closeTag();
            myActiveContainerPlanSize++;
            break;
        }
        case SUMO_TAG_TRANSHIP: {
            if (attrs.hasAttribute(SUMO_ATTR_EDGES)) {
                // copy walk as it is
                // XXX allow --repair?
                myActiveContainerPlan->openTag(SUMO_TAG_TRANSHIP);
                (*myActiveContainerPlan) << attrs;
                myActiveContainerPlan->closeTag();
                myActiveContainerPlanSize++;
            } else {
                routePedestrian(attrs, *myActiveContainerPlan);
            }
            break;
        }
        case SUMO_TAG_FLOW:
            myActiveRouteProbability = DEFAULT_VEH_PROB;
            parseFromViaTo("flow", attrs);
            break;
        case SUMO_TAG_TRIP: {
            myActiveRouteProbability = DEFAULT_VEH_PROB;
            parseFromViaTo("trip", attrs);
        }
        break;
        default:
            break;
    }
    // parse embedded vtype information
    if (myCurrentVType != 0 && element != SUMO_TAG_VTYPE && element != SUMO_TAG_PARAM) {
        SUMOVehicleParserHelper::parseVTypeEmbedded(*myCurrentVType, element, attrs);
        return;
    }
}


void
RORouteHandler::openVehicleTypeDistribution(const SUMOSAXAttributes& attrs) {
    bool ok = true;
    myCurrentVTypeDistributionID = attrs.get<std::string>(SUMO_ATTR_ID, 0, ok);
    if (ok) {
        myCurrentVTypeDistribution = new RandomDistributor<SUMOVTypeParameter*>();
        if (attrs.hasAttribute(SUMO_ATTR_VTYPES)) {
            const std::string vTypes = attrs.get<std::string>(SUMO_ATTR_VTYPES, myCurrentVTypeDistributionID.c_str(), ok);
            StringTokenizer st(vTypes);
            while (st.hasNext()) {
                SUMOVTypeParameter* type = myNet.getVehicleTypeSecure(st.next());
                myCurrentVTypeDistribution->add(1., type);
            }
        }
    }
}


void
RORouteHandler::closeVehicleTypeDistribution() {
    if (myCurrentVTypeDistribution != 0) {
        if (myCurrentVTypeDistribution->getOverallProb() == 0) {
            delete myCurrentVTypeDistribution;
            myErrorOutput->inform("Vehicle type distribution '" + myCurrentVTypeDistributionID + "' is empty.");
        } else if (!myNet.addVTypeDistribution(myCurrentVTypeDistributionID, myCurrentVTypeDistribution)) {
            delete myCurrentVTypeDistribution;
            myErrorOutput->inform("Another vehicle type (or distribution) with the id '" + myCurrentVTypeDistributionID + "' exists.");
        }
        myCurrentVTypeDistribution = 0;
    }
}


void
RORouteHandler::openRoute(const SUMOSAXAttributes& attrs) {
    myActiveRoute.clear();
    myInsertStopEdgesAt = -1;
    // check whether the id is really necessary
    std::string rid;
    if (myCurrentAlternatives != 0) {
        myActiveRouteID = myCurrentAlternatives->getID();
        rid =  "distribution '" + myCurrentAlternatives->getID() + "'";
    } else if (myVehicleParameter != 0) {
        // ok, a vehicle is wrapping the route,
        //  we may use this vehicle's id as default
        myVehicleParameter->routeid = myActiveRouteID = "!" + myVehicleParameter->id; // !!! document this
        if (attrs.hasAttribute(SUMO_ATTR_ID)) {
            WRITE_WARNING("Ids of internal routes are ignored (vehicle '" + myVehicleParameter->id + "').");
        }
    } else {
        bool ok = true;
        myActiveRouteID = attrs.get<std::string>(SUMO_ATTR_ID, 0, ok);
        if (!ok) {
            return;
        }
        rid = "'" + myActiveRouteID + "'";
    }
    if (myVehicleParameter != 0) { // have to do this here for nested route distributions
        rid =  "for vehicle '" + myVehicleParameter->id + "'";
    }
    bool ok = true;
    if (attrs.hasAttribute(SUMO_ATTR_EDGES)) {
        parseEdges(attrs.get<std::string>(SUMO_ATTR_EDGES, myActiveRouteID.c_str(), ok), myActiveRoute, rid);
    }
    myActiveRouteRefID = attrs.getOpt<std::string>(SUMO_ATTR_REFID, myActiveRouteID.c_str(), ok, "");
    if (myActiveRouteRefID != "" && myNet.getRouteDef(myActiveRouteRefID) == 0) {
        myErrorOutput->inform("Invalid reference to route '" + myActiveRouteRefID + "' in route " + rid + ".");
    }
    if (myCurrentAlternatives != 0 && !attrs.hasAttribute(SUMO_ATTR_PROB)) {
        WRITE_WARNING("No probability for a route in '" + rid + "', using default.");
    }
    myActiveRouteProbability = attrs.getOpt<SUMOReal>(SUMO_ATTR_PROB, myActiveRouteID.c_str(), ok, DEFAULT_VEH_PROB);
    if (ok && myActiveRouteProbability < 0) {
        myErrorOutput->inform("Invalid probability for route '" + myActiveRouteID + "'.");
    }
    myActiveRouteColor = attrs.hasAttribute(SUMO_ATTR_COLOR) ? new RGBColor(attrs.get<RGBColor>(SUMO_ATTR_COLOR, myActiveRouteID.c_str(), ok)) : 0;
    ok = true;
    myCurrentCosts = attrs.getOpt<SUMOReal>(SUMO_ATTR_COST, myActiveRouteID.c_str(), ok, -1);
    if (ok && myCurrentCosts != -1 && myCurrentCosts < 0) {
        myErrorOutput->inform("Invalid cost for route '" + myActiveRouteID + "'.");
    }
}


void
RORouteHandler::myEndElement(int element) {
    SUMORouteHandler::myEndElement(element);
    switch (element) {
        case SUMO_TAG_VTYPE:
            if (myNet.addVehicleType(myCurrentVType)) {
                if (myCurrentVTypeDistribution != 0) {
                    myCurrentVTypeDistribution->add(myCurrentVType->defaultProbability, myCurrentVType);
                }
            }
            myCurrentVType = 0;
            break;
        case SUMO_TAG_TRIP:
            closeRoute(true);
            closeVehicle();
            delete myVehicleParameter;
            myVehicleParameter = 0;
            myInsertStopEdgesAt = -1;
            break;
        default:
            break;
    }
}


void
RORouteHandler::closeRoute(const bool mayBeDisconnected) {
    if (myActiveRoute.size() == 0) {
        if (myActiveRouteRefID != "" && myCurrentAlternatives != 0) {
            myCurrentAlternatives->addAlternativeDef(myNet.getRouteDef(myActiveRouteRefID));
            myActiveRouteID = "";
            myActiveRouteRefID = "";
            return;
        }
        if (myVehicleParameter != 0) {
            myErrorOutput->inform("The route for vehicle '" + myVehicleParameter->id + "' has no edges.");
        } else {
            myErrorOutput->inform("Route '" + myActiveRouteID + "' has no edges.");
        }
        myActiveRouteID = "";
        myActiveRouteStops.clear();
        return;
    }
    if (myActiveRoute.size() == 1 && myActiveRoute.front()->getFunc() == ROEdge::ET_DISTRICT) {
        myErrorOutput->inform("The routing information for vehicle '" + myVehicleParameter->id + "' is insufficient.");
        myActiveRouteID = "";
        myActiveRouteStops.clear();
        return;
    }
    RORoute* route = new RORoute(myActiveRouteID, myCurrentCosts, myActiveRouteProbability, myActiveRoute,
                                 myActiveRouteColor, myActiveRouteStops);
    myActiveRoute.clear();
    if (myCurrentAlternatives == 0) {
        if (myNet.getRouteDef(myActiveRouteID) != 0) {
            delete route;
            if (myVehicleParameter != 0) {
                myErrorOutput->inform("Another route for vehicle '" + myVehicleParameter->id + "' exists.");
            } else {
                myErrorOutput->inform("Another route (or distribution) with the id '" + myActiveRouteID + "' exists.");
            }
            myActiveRouteID = "";
            myActiveRouteStops.clear();
            return;
        } else {
            myCurrentAlternatives = new RORouteDef(myActiveRouteID, 0, mayBeDisconnected || myTryRepair, mayBeDisconnected);
            myCurrentAlternatives->addLoadedAlternative(route);
            myNet.addRouteDef(myCurrentAlternatives);
            myCurrentAlternatives = 0;
        }
    } else {
        myCurrentAlternatives->addLoadedAlternative(route);
    }
    myActiveRouteID = "";
    myActiveRouteStops.clear();
}


void
RORouteHandler::openRouteDistribution(const SUMOSAXAttributes& attrs) {
    // check whether the id is really necessary
    bool ok = true;
    std::string id;
    if (myVehicleParameter != 0) {
        // ok, a vehicle is wrapping the route,
        //  we may use this vehicle's id as default
        myVehicleParameter->routeid = id = "!" + myVehicleParameter->id; // !!! document this
        if (attrs.hasAttribute(SUMO_ATTR_ID)) {
            WRITE_WARNING("Ids of internal route distributions are ignored (vehicle '" + myVehicleParameter->id + "').");
        }
    } else {
        id = attrs.get<std::string>(SUMO_ATTR_ID, 0, ok);
        if (!ok) {
            return;
        }
    }
    // try to get the index of the last element
    int index = attrs.getOpt<int>(SUMO_ATTR_LAST, id.c_str(), ok, 0);
    if (ok && index < 0) {
        myErrorOutput->inform("Negative index of a route alternative (id='" + id + "').");
        return;
    }
    // build the alternative cont
    myCurrentAlternatives = new RORouteDef(id, index, myTryRepair, false);
    if (attrs.hasAttribute(SUMO_ATTR_ROUTES)) {
        ok = true;
        StringTokenizer st(attrs.get<std::string>(SUMO_ATTR_ROUTES, id.c_str(), ok));
        while (st.hasNext()) {
            const std::string routeID = st.next();
            const RORouteDef* route = myNet.getRouteDef(routeID);
            if (route == 0) {
                myErrorOutput->inform("Unknown route '" + routeID + "' in distribution '" + id + "'.");
            } else {
                myCurrentAlternatives->addAlternativeDef(route);
            }
        }
    }
}


void
RORouteHandler::closeRouteDistribution() {
    if (myCurrentAlternatives != 0) {
        if (myCurrentAlternatives->getOverallProb() == 0) {
            myErrorOutput->inform("Route distribution '" + myCurrentAlternatives->getID() + "' is empty.");
            delete myCurrentAlternatives;
        } else if (!myNet.addRouteDef(myCurrentAlternatives)) {
            myErrorOutput->inform("Another route (or distribution) with the id '" + myCurrentAlternatives->getID() + "' exists.");
            delete myCurrentAlternatives;
        }
        myCurrentAlternatives = 0;
    }
}


void
RORouteHandler::closeVehicle() {
    // get the vehicle id
    if (myVehicleParameter->departProcedure == DEPART_GIVEN && myVehicleParameter->depart < string2time(OptionsCont::getOptions().getString("begin"))) {
        return;
    }
    // get vehicle type
    SUMOVTypeParameter* type = myNet.getVehicleTypeSecure(myVehicleParameter->vtypeid);
    if (type == 0) {
        myErrorOutput->inform("The vehicle type '" + myVehicleParameter->vtypeid + "' for vehicle '" + myVehicleParameter->id + "' is not known.");
        type = myNet.getVehicleTypeSecure(DEFAULT_VTYPE_ID);
    } else {
        // fix the type id in case we used a distribution
        myVehicleParameter->vtypeid = type->id;
    }
    // get the route
    RORouteDef* route = myNet.getRouteDef(myVehicleParameter->routeid);
    if (route == 0) {
        myErrorOutput->inform("The route of the vehicle '" + myVehicleParameter->id + "' is not known.");
        return;
    }
    if (route->getID()[0] != '!') {
        route = route->copy("!" + myVehicleParameter->id);
    }
    // build the vehicle
    if (!MsgHandler::getErrorInstance()->wasInformed()) {
        ROVehicle* veh = new ROVehicle(*myVehicleParameter, route, type, &myNet);
        if (myNet.addVehicle(myVehicleParameter->id, veh)) {
            registerLastDepart();
        }
    }
}


void
RORouteHandler::closePerson() {
    myActivePlan->closeTag();
    if (myActivePlanSize > 0) {
        myNet.addPerson(myVehicleParameter->depart, myActivePlan->getString());
        registerLastDepart();
    } else {
        WRITE_WARNING("Discarding person '" + myVehicleParameter->id + "' because it's plan is empty");
    }
    delete myVehicleParameter;
    myVehicleParameter = 0;
    delete myActivePlan;
    myActivePlan = 0;
    myActivePlanSize = 0;
}

void
RORouteHandler::closeContainer() {
    myActiveContainerPlan->closeTag();
    if (myActiveContainerPlanSize > 0) {
        myNet.addContainer(myVehicleParameter->depart, myActiveContainerPlan->getString());
        registerLastDepart();
    } else {
        WRITE_WARNING("Discarding container '" + myVehicleParameter->id + "' because it's plan is empty");
    }
    delete myVehicleParameter;
    myVehicleParameter = 0;
    delete myActiveContainerPlan;
    myActiveContainerPlan = 0;
    myActiveContainerPlanSize = 0;
}


void
RORouteHandler::closeFlow() {
    // @todo: consider myScale?
    if (myVehicleParameter->repetitionNumber == 0) {
        delete myVehicleParameter;
        myVehicleParameter = 0;
        return;
    }
    // let's check whether vehicles had to depart before the simulation starts
    myVehicleParameter->repetitionsDone = 0;
    const SUMOTime offsetToBegin = string2time(OptionsCont::getOptions().getString("begin")) - myVehicleParameter->depart;
    while (myVehicleParameter->repetitionsDone * myVehicleParameter->repetitionOffset < offsetToBegin) {
        myVehicleParameter->repetitionsDone++;
        if (myVehicleParameter->repetitionsDone == myVehicleParameter->repetitionNumber) {
            delete myVehicleParameter;
            myVehicleParameter = 0;
            return;
        }
    }
    if (myNet.getVehicleTypeSecure(myVehicleParameter->vtypeid) == 0) {
        myErrorOutput->inform("The vehicle type '" + myVehicleParameter->vtypeid + "' for flow '" + myVehicleParameter->id + "' is not known.");
    }
    if (myVehicleParameter->routeid[0] == '!' && myNet.getRouteDef(myVehicleParameter->routeid) == 0) {
        closeRoute(true);
    }
    if (myNet.getRouteDef(myVehicleParameter->routeid) == 0) {
        myErrorOutput->inform("The route '" + myVehicleParameter->routeid + "' for flow '" + myVehicleParameter->id + "' is not known.");
        delete myVehicleParameter;
        myVehicleParameter = 0;
        return;
    }
    myActiveRouteID = "";
    if (!MsgHandler::getErrorInstance()->wasInformed()) {
        if (myNet.addFlow(myVehicleParameter, OptionsCont::getOptions().getBool("randomize-flows"))) {
            registerLastDepart();
        } else {
            myErrorOutput->inform("Another flow with the id '" + myVehicleParameter->id + "' exists.");
        }
    }
    myVehicleParameter = 0;
    myInsertStopEdgesAt = -1;
}


void
RORouteHandler::addStop(const SUMOSAXAttributes& attrs) {
    if (myActivePlan) {
        myActivePlan->openTag(SUMO_TAG_STOP);
        (*myActivePlan) << attrs;
        myActivePlan->closeTag();
        myActivePlanSize++;
        return;
    }
    if (myActiveContainerPlan) {
        myActiveContainerPlan->openTag(SUMO_TAG_STOP);
        (*myActiveContainerPlan) << attrs;
        myActiveContainerPlan->closeTag();
        myActiveContainerPlanSize++;
        return;
    }
    std::string errorSuffix;
    if (myVehicleParameter != 0) {
        errorSuffix = " in vehicle '" + myVehicleParameter->id + "'.";
    } else {
        errorSuffix = " in route '" + myActiveRouteID + "'.";
    }
    SUMOVehicleParameter::Stop stop;
    bool ok = parseStop(stop, attrs, errorSuffix, myErrorOutput);
    if (!ok) {
        return;
    }
    // try to parse the assigned bus stop
    ROEdge* edge = 0;
    if (stop.busstop != "") {
        const SUMOVehicleParameter::Stop* busstop = myNet.getBusStop(stop.busstop);
        if (busstop == 0) {
            myErrorOutput->inform("Unknown bus stop '" + stop.busstop + "'" + errorSuffix);
            return;
        }
        stop.lane = busstop->lane;
        stop.endPos = busstop->endPos;
        stop.startPos = busstop->startPos;
        edge = myNet.getEdge(stop.lane.substr(0, stop.lane.rfind('_')));
    } // try to parse the assigned container stop
    else if (stop.containerstop != "") {
        const SUMOVehicleParameter::Stop* containerstop = myNet.getContainerStop(stop.containerstop);
        if (containerstop == 0) {
            myErrorOutput->inform("Unknown container stop '" + stop.containerstop + "'" + errorSuffix);
        }
        stop.lane = containerstop->lane;
        stop.endPos = containerstop->endPos;
        stop.startPos = containerstop->startPos;
        edge = myNet.getEdge(stop.lane.substr(0, stop.lane.rfind('_')));
    } else {
        // no, the lane and the position should be given
        stop.lane = attrs.getOpt<std::string>(SUMO_ATTR_LANE, 0, ok, "");
        if (!ok || stop.lane == "") {
            myErrorOutput->inform("A stop must be placed on a bus stop, a container stop or a lane" + errorSuffix);
            return;
        }
        edge = myNet.getEdge(stop.lane.substr(0, stop.lane.rfind('_')));
        if (edge == 0) {
            myErrorOutput->inform("The lane '" + stop.lane + "' for a stop is not known" + errorSuffix);
            return;
        }
        stop.endPos = attrs.getOpt<SUMOReal>(SUMO_ATTR_ENDPOS, 0, ok, edge->getLength());
        stop.startPos = attrs.getOpt<SUMOReal>(SUMO_ATTR_STARTPOS, 0, ok, stop.endPos - 2 * POSITION_EPS);
        const bool friendlyPos = attrs.getOpt<bool>(SUMO_ATTR_FRIENDLY_POS, 0, ok, false);
        if (!ok || !checkStopPos(stop.startPos, stop.endPos, edge->getLength(), POSITION_EPS, friendlyPos)) {
            myErrorOutput->inform("Invalid start or end position for stop" + errorSuffix);
            return;
        }
    }
    if (myVehicleParameter != 0) {
        myVehicleParameter->stops.push_back(stop);
    } else {
        myActiveRouteStops.push_back(stop);
    }
    if (myInsertStopEdgesAt >= 0) {
        myActiveRoute.insert(myActiveRoute.begin() + myInsertStopEdgesAt, edge);
        myInsertStopEdgesAt++;
    }
}


void
RORouteHandler::parseEdges(const std::string& desc, ConstROEdgeVector& into,
                           const std::string& rid) {
    if (desc[0] == BinaryFormatter::BF_ROUTE) {
        std::istringstream in(desc, std::ios::binary);
        char c;
        in >> c;
        FileHelpers::readEdgeVector(in, into, rid);
    } else {
        for (StringTokenizer st(desc); st.hasNext();) {
            const std::string id = st.next();
            const ROEdge* edge = myNet.getEdge(id);
            if (edge == 0) {
                myErrorOutput->inform("The edge '" + id + "' within the route " + rid + " is not known.");
            } else {
                into.push_back(edge);
            }
        }
    }
}


bool
RORouteHandler::routePedestrian(const SUMOSAXAttributes& attrs, OutputDevice& plan) {
    bool ok = true;
    const char* id = myVehicleParameter->id.c_str();
    SUMOReal departPos = attrs.getOpt<SUMOReal>(SUMO_ATTR_DEPARTPOS, id, ok, 0);
    SUMOReal arrivalPos = attrs.getOpt<SUMOReal>(SUMO_ATTR_ARRIVALPOS, id, ok, -NUMERICAL_EPS);
    assert(!attrs.hasAttribute(SUMO_ATTR_EDGES));
    assert(myActiveRoute.size() == 0);
    const std::string fromID = attrs.get<std::string>(SUMO_ATTR_FROM, id, ok);
    const std::string toID = attrs.get<std::string>(SUMO_ATTR_TO, id, ok);
    const ROEdge* from = myNet.getEdge(fromID);
    if (from == 0) {
        myErrorOutput->inform("The edge '" + fromID + "' within a walk of " + myVehicleParameter->id + " is not known."
                              + "\n The route can not be build.");
        ok = false;
    }
    const ROEdge* to = myNet.getEdge(toID);
    if (to == 0) {
        myErrorOutput->inform("The edge '" + toID + "' within a walk of " + myVehicleParameter->id + " is not known."
                              + "\n The route can not be build.");
        ok = false;
    }
    if (ok) {
        if (myPedestrianRouter == 0) {
            myPedestrianRouter = new ROPedestrianRouterDijkstra();
        }
        myPedestrianRouter->compute(from, to,
                                    SUMOVehicleParameter::interpretEdgePos(departPos, from->getLength(), SUMO_ATTR_DEPARTPOS, "person walking from " + fromID),
                                    SUMOVehicleParameter::interpretEdgePos(arrivalPos, to->getLength(), SUMO_ATTR_ARRIVALPOS, "person walking to " + toID),
                                    DEFAULT_PEDESTRIAN_SPEED, 0, 0, myActiveRoute);
        if (myActiveRoute.empty()) {
            myErrorOutput->inform("No connection found between '" + fromID + "' and '" + toID + "' for person '" + myVehicleParameter->id + "'.");
            return false;
        }
        myActivePlan->openTag(SUMO_TAG_WALK);
        if (attrs.hasAttribute(SUMO_ATTR_DEPARTPOS)) {
            plan.writeAttr(SUMO_ATTR_DEPARTPOS, attrs.get<SUMOReal>(SUMO_ATTR_DEPARTPOS, id, ok));
        }
        if (attrs.hasAttribute(SUMO_ATTR_ARRIVALPOS)) {
            plan.writeAttr(SUMO_ATTR_ARRIVALPOS, attrs.get<SUMOReal>(SUMO_ATTR_ARRIVALPOS, id, ok));
        }
        if (attrs.hasAttribute(SUMO_ATTR_DURATION)) {
            plan.writeAttr(SUMO_ATTR_DURATION, attrs.getSUMOTimeReporting(SUMO_ATTR_DURATION, id, ok));
        }
        if (attrs.hasAttribute(SUMO_ATTR_SPEED)) {
            plan.writeAttr(SUMO_ATTR_SPEED, attrs.get<SUMOReal>(SUMO_ATTR_SPEED, id, ok));
        }
        if (attrs.hasAttribute(SUMO_ATTR_BUS_STOP)) {
            plan.writeAttr(SUMO_ATTR_BUS_STOP, attrs.get<std::string>(SUMO_ATTR_BUS_STOP, id, ok));
        }
        plan.writeAttr(SUMO_ATTR_EDGES, myActiveRoute);
        myActivePlan->closeTag();
        myActivePlanSize++;
    }
    myActiveRoute.clear();
    return ok;
}


/****************************************************************************/
