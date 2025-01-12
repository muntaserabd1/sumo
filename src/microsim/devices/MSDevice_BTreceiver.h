/****************************************************************************/
/// @file    MSDevice_BTreceiver.h
/// @author  Daniel Krajzewicz
/// @author  Michael Behrisch
/// @date    14.08.2013
/// @version $Id$
///
// A BT receiver
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
// Copyright (C) 2013-2015 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/
#ifndef MSDevice_BTreceiver_h
#define MSDevice_BTreceiver_h


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include "MSDevice.h"
#include "MSDevice_BTsender.h"
#include <microsim/MSNet.h>
#include <utils/common/SUMOTime.h>
#include <utils/common/Command.h>
#include <utils/common/RandHelper.h>


// ===========================================================================
// class declarations
// ===========================================================================
class SUMOVehicle;


// ===========================================================================
// class definitions
// ===========================================================================
/**
 * @class MSDevice_BTreceiver
 * @brief A BT receiver
 *
 * @see MSDevice
 */
class MSDevice_BTreceiver : public MSDevice {
public:
    /** @brief Inserts MSDevice_BTreceiver-options
     * @param[filled] oc The options container to add the options to
     */
    static void insertOptions(OptionsCont& oc);


    /** @brief Build devices for the given vehicle, if needed
     *
     * The options are read and evaluated whether a bt-receiver-device shall be built
     *  for the given vehicle.
     *
     * The built device is stored in the given vector.
     *
     * @param[in] v The vehicle for which a device may be built
     * @param[filled] into The vector to store the built device in
     */
    static void buildVehicleDevices(SUMOVehicle& v, std::vector<MSDevice*>& into);


    /** @brief Returns the configured range
     * @return the device range
     */
    static SUMOReal getRange() {
        return myRange;
    }



public:
    /// @brief Destructor.
    ~MSDevice_BTreceiver();



    /// @name Methods inherited from MSMoveReminder.
    /// @{

    /** @brief Adds the vehicle to running vehicles if it (re-) enters the network
     *
     * @param[in] veh The entering vehicle.
     * @param[in] reason how the vehicle enters the lane
     * @return Always true
     * @see MSMoveReminder::notifyEnter
     * @see MSMoveReminder::Notification
     */
    bool notifyEnter(SUMOVehicle& veh, Notification reason);


    /** @brief Checks whether the reminder still has to be notified about the vehicle moves
     *
     * Indicator if the reminders is still active for the passed
     * vehicle/parameters. If false, the vehicle will erase this reminder
     * from it's reminder-container.
     *
     * @param[in] veh Vehicle that asks this reminder.
     * @param[in] oldPos Position before move.
     * @param[in] newPos Position after move with newSpeed.
     * @param[in] newSpeed Moving speed.
     *
     * @return True if vehicle hasn't passed the reminder completely.
     */
    bool notifyMove(SUMOVehicle& veh, SUMOReal oldPos, SUMOReal newPos, SUMOReal newSpeed);


    /** @brief Moves (the known) vehicle from running to arrived vehicles' list
     *
     * @param[in] veh The leaving vehicle.
     * @param[in] lastPos Position on the lane when leaving.
     * @param[in] isArrival whether the vehicle arrived at its destination
     * @param[in] isLaneChange whether the vehicle changed from the lane
     * @see leaveDetectorByLaneChange
     * @see MSMoveReminder
     * @see MSMoveReminder::notifyLeave
     */
    bool notifyLeave(SUMOVehicle& veh, SUMOReal lastPos, Notification reason);
    //@}



    /** @class MeetingPoint
     * @brief Holds the information about exact positions/speeds/time of the begin/end of a meeting
     */
    class MeetingPoint {
    public:
        /** @brief Constructor
         * @param[in] _t The time of the meeting
         * @param[in] _observerState The position, speed, lane etc. the observer had at the time
         * @param[in] _seenState The position, speed, lane etc. the seen vehicle had at the time
         */
        MeetingPoint(SUMOReal _t, const MSDevice_BTsender::VehicleState& _observerState,
                     const MSDevice_BTsender::VehicleState& _seenState)
            : t(_t), observerState(_observerState), seenState(_seenState) {}

        /// @brief Destructor
        ~MeetingPoint() {}

    public:
        /// @brief The time of the meeting
        const SUMOReal t;
        /// @brief The state the observer had at the time
        const MSDevice_BTsender::VehicleState observerState;
        /// @brief The state the seen vehicle had at the time
        const MSDevice_BTsender::VehicleState seenState;

    private:
        /// @brief Invalidated assignment operator.
        MeetingPoint& operator=(const MeetingPoint&);

    };



    /** @class SeenDevice
     * @brief Class representing a single seen device
     */
    class SeenDevice {
    public:
        /** @brief Constructor
         * @param[in] meetingBegin_ Description of the meeting's begin
         */
        SeenDevice(const MeetingPoint& meetingBegin_)
            : meetingBegin(meetingBegin_), meetingEnd(0), lastView(meetingBegin_.t), nextView(-1.) {}

        /// @brief Destructor
        ~SeenDevice() {
            delete meetingEnd;
            for (std::vector<MeetingPoint*>::iterator i = recognitionPoints.begin(); i != recognitionPoints.end(); ++i) {
                delete *i;
            }
            recognitionPoints.clear();
        }


    public:
        /// @brief Description of the meeting's begin
        const MeetingPoint meetingBegin;
        /// @brief Description of the meeting's end
        MeetingPoint* meetingEnd;
        /// @brief Last recognition point
        SUMOReal lastView;
        /// @brief Next possible recognition point
        SUMOReal nextView;
        /// @brief List of recognition points
        std::vector<MeetingPoint*> recognitionPoints;
        /// @brief string of travelled receiver edges
        std::string receiverRoute;
        /// @brief string of travelled sender edges
        std::string senderRoute;

    private:
        /// @brief Invalidated assignment operator.
        SeenDevice& operator=(const SeenDevice&);

    };



    /** @brief Clears the given containers deleting the stored items
     * @param[in] c The currently seen container to clear
     * @param[in] s The seen container to clear
     */
    static void cleanUp(std::map<std::string, SeenDevice*>& c, std::map<std::string, std::vector<SeenDevice*> >& s);



protected:
    /** @brief Constructor
     *
     * @param[in] holder The vehicle that holds this device
     * @param[in] id The ID of the device
     */
    MSDevice_BTreceiver(SUMOVehicle& holder, const std::string& id);



private:
    /// @brief Whether the bt-system was already initialised
    static bool myWasInitialised;

    /// @brief The range of the device
    static SUMOReal myRange;

    /// @brief The offtime of the device
    static SUMOReal myOffTime;


    /** @class VehicleInformation
     * @brief Stores the information of a vehicle
     */
    class VehicleInformation : public MSDevice_BTsender::VehicleInformation {
    public:
        /** @brief Constructor
         * @param[in] id The id of the vehicle
         * @param[in] range Recognition range of the vehicle
         */
        VehicleInformation(const std::string& id, const SUMOReal _range) : MSDevice_BTsender::VehicleInformation(id), range(_range) {}

        /// @brief Destructor
        ~VehicleInformation() {}

        /// @brief Recognition range of the vehicle
        const SUMOReal range;

        /// @brief The map of devices seen by the vehicle at removal time
        std::map<std::string, SeenDevice*> currentlySeen;

        /// @brief The past episodes of removed vehicle
        std::map<std::string, std::vector<SeenDevice*> > seen;

    private:
        /// @brief Invalidated copy constructor.
        VehicleInformation(const VehicleInformation&);

        /// @brief Invalidated assignment operator.
        VehicleInformation& operator=(const VehicleInformation&);

    };



    /** @class BTreceiverUpdate
     * @brief A global update performer
     */
    class BTreceiverUpdate : public Command {
    public:
        /// @brief Constructor
        BTreceiverUpdate();

        /// @brief Destructor
        ~BTreceiverUpdate();

        /** @brief Performs the update
         * @param[in] currentTime The current simulation time
         * @return Always DELTA_T - the time to being called back
         */
        SUMOTime execute(SUMOTime currentTime);


        /** @brief Rechecks the visibility for a given receiver/sender pair
         * @param[in] receiver Definition of the receiver vehicle
         * @param[in] sender Definition of the sender vehicle
         */
        void updateVisibility(VehicleInformation& receiver, MSDevice_BTsender::VehicleInformation& sender);


        /** @brief Informs the receiver about a sender entering it's radius
         * @param[in] atOffset The time offset to the current time step
         * @param[in] receiverState The position, speed, lane etc. the observer had at the time
         * @param[in] senderID The ID of the entering sender
         * @param[in] senderState The position, speed, lane etc. the seen vehicle had at the time
         * @param[in] currentlySeen The container storing episodes
         */
        void enterRange(SUMOReal atOffset, const MSDevice_BTsender::VehicleState& receiverState,
                        const std::string& senderID, const MSDevice_BTsender::VehicleState& senderState,
                        std::map<std::string, SeenDevice*>& currentlySeen);


        /** @brief Removes the sender from the currently seen devices to past episodes
         * @param[in] receiverInfo The static information of the observer (id, route, etc.)
         * @param[in] receiverState The position, speed, lane etc. the observer had at the time
         * @param[in] senderInfo The static information of the seen vehicle (id, route, etc.)
         * @param[in] senderState The position, speed, lane etc. the seen vehicle had at the time
         * @param[in] tOffset The time offset to the current time step
         */
        void leaveRange(VehicleInformation& receiverInfo, const MSDevice_BTsender::VehicleState& receiverState,
                        MSDevice_BTsender::VehicleInformation& senderInfo, const MSDevice_BTsender::VehicleState& senderState,
                        SUMOReal tOffset);




        /** @brief Adds a point of recognition
         * @param[in] tEnd The time of the recognition
         * @param[in] receiverState The position, speed, lane etc. the observer had at the time
         * @param[in] senderState The position, speed, lane etc. the seen vehicle had at the time
         * @param[in] senderDevice The device of the entering sender
         */
        void addRecognitionPoint(const SUMOReal tEnd, const MSDevice_BTsender::VehicleState& receiverState,
                                 const MSDevice_BTsender::VehicleState& senderState,
                                 SeenDevice* senderDevice) const;


        /** @brief Writes the output
         * @param[in] id The id of the receiver
         * @param[in] seen The information about seen senders
         * @param[in] allRecognitions Whether all recognitions shall be written
         */
        void writeOutput(const std::string& id, const std::map<std::string, std::vector<SeenDevice*> >& seen,
                         bool allRecognitions);




    };


    static SUMOReal inquiryDelaySlots(const int backoffLimit);

    /// @brief A random number generator used to determine whether the opposite was recognized
    static MTRand sRecognitionRNG;

    /// @brief The list of arrived receivers
    static std::map<std::string, VehicleInformation*> sVehicles;



private:
    /// @brief Invalidated copy constructor.
    MSDevice_BTreceiver(const MSDevice_BTreceiver&);

    /// @brief Invalidated assignment operator.
    MSDevice_BTreceiver& operator=(const MSDevice_BTreceiver&);


};

#endif

/****************************************************************************/

