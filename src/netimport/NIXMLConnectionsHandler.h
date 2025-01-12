/****************************************************************************/
/// @file    NIXMLConnectionsHandler.h
/// @author  Daniel Krajzewicz
/// @author  Jakob Erdmann
/// @author  Michael Behrisch
/// @date    Tue, 20 Nov 2001
/// @version $Id$
///
// Importer for edge connections stored in XML
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
#ifndef NIXMLConnectionsHandler_h
#define NIXMLConnectionsHandler_h


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <utils/xml/SUMOSAXHandler.h>
#include <netbuild/NBConnection.h>


// ===========================================================================
// class declarations
// ===========================================================================
class NBEdge;
class NBEdgeCont;
class NBNodeCont;
class NBTrafficLightLogicCont;
class MsgHandler;


// ===========================================================================
// class definitions
// ===========================================================================
/**
 * @class NIXMLConnectionsHandler
 * @brief Importer for edge connections stored in XML
 *
 * This importer parses connections, and prohibitions, and is able
 *  to reset connections between edges.
 */
class NIXMLConnectionsHandler : public SUMOSAXHandler {
public:
    /** @brief Constructor
     * @param[in] ec The edge container which includes the edges to change connections of
     */
    NIXMLConnectionsHandler(NBEdgeCont& ec, NBNodeCont& nc, NBTrafficLightLogicCont& tlc);


    /// @brief Destructor
    ~NIXMLConnectionsHandler();


protected:
    /// @name inherited from GenericSAXHandler
    //@{

    /** @brief Called on the opening of a tag;
     *
     * @param[in] element ID of the currently opened element
     * @param[in] attrs Attributes within the currently opened element
     * @exception ProcessError If something fails
     * @see GenericSAXHandler::myStartElement
     */
    void myStartElement(int element,
                        const SUMOSAXAttributes& attrs);
    //@}

private:
    /** @brief Returns the connection described by def
     *
     * def should have the following format <FROM_EDGE_ID>[_<FROM_LANE_NO>]-><TO_EDGE_ID>[_<TO_LANE_NO>].
     *
     * @param[in] defRole "prohibitor" or "prohibited" - used for error reporting
     * @param[in] def The definition of the connection
     * @return The parsed connection
     */
    NBConnection parseConnection(const std::string& defRole, const std::string& def);


    /** @brief Parses a connection when it describes a lane-2-lane relationship
     * @param[in] attrs The attributes to get further information about the connection from
     * @param[in] from The edge at which the connection starts (the on incoming into a node)
     * @param[in] to The edge at which the connection ends (the on outgoing from a node)
     */
    void parseLaneBound(const SUMOSAXAttributes& attrs, NBEdge* from, NBEdge* to);


    /** @brief Parses information about lane-2-lane connection when it describes a lane-2-lane relationship
     * @param[in] attrs The attributes to get further information about the connection from
     * @param[in] fromEdge The edge at which the connection starts (the on incoming into a node)
     * @param[in] toEdge The edge at which the connection ends (the on outgoing from a node)
     * @param[out] fromLane The lane at which connection starts
     * @param[out] toLane The lane at which connection ends
     */
    bool parseLaneInfo(const SUMOSAXAttributes& attributes, NBEdge* fromEdge, NBEdge* toEdge, int* fromLane, int* toLane);


    /** @brief Parses information about lane-2-lane connection in deprecated format.
     * @param[in] attrs The attributes to get further information about the connection from
     * @param[in] fromEdge The edge at which the connection starts (the on incoming into a node)
     * @param[in] toEdge The edge at which the connection ends (the on outgoing from a node)
     * @param[out] fromLane The lane at which connection starts
     * @param[out] toLane The lane at which connection ends
     */
    inline bool parseDeprecatedLaneDefinition(const SUMOSAXAttributes& attributes,
            NBEdge* fromEdge, NBEdge* toEdge,
            int* fromLane, int* toLane);


    /** @brief Parses information about lane-2-lane connection.
     * @param[in] attrs The attributes to get further information about the connection from
     * @param[out] fromLane The lane at which connection starts
     * @param[out] toLane The lane at which connection ends
     */
    inline bool parseLaneDefinition(const SUMOSAXAttributes& attributes, int* fromLane, int* toLane);


    /** @brief Parses a crossing and updates the referenced node
     * @param[in] attrs The attributes to get the crossings's values from
     */
    void addCrossing(const SUMOSAXAttributes& attrs);

    /** @brief Parses a customShape and updates the referenced node
     * @param[in] attrs The attributes to get the customShapes's values from
     */
    void addCustomShape(const SUMOSAXAttributes& attrs);

private:
    /// @brief The edge container to fill
    NBEdgeCont& myEdgeCont;

    /// @brief The edge container to fill
    NBNodeCont& myNodeCont;

    /** @brief The traffic lights container to add built tls to (when
     * invalidating tls)  */
    NBTrafficLightLogicCont& myTLLogicCont;

    /// @brief Information whether we have a deprecated attribute
    bool myHaveWarnedAboutDeprecatedLanes;

    /// @brief the handler for loading errors
    MsgHandler* const myErrorMsgHandler;

private:
    /// @brief invalidated copy constructor
    NIXMLConnectionsHandler(const NIXMLConnectionsHandler& s);

    /// @brief invalidated assignment operator
    NIXMLConnectionsHandler& operator=(const NIXMLConnectionsHandler& s);


};


#endif

/****************************************************************************/

