/****************************************************************************/
/// @file    XMLSubSys.h
/// @author  Daniel Krajzewicz
/// @author  Michael Behrisch
/// @date    Mon, 1 Jul 2002
/// @version $Id$
///
// Utility methods for initialising, closing and using the XML-subsystem
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
// Copyright (C) 2002-2015 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/
#ifndef XMLSubSys_h
#define XMLSubSys_h


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <vector>
#include <xercesc/sax2/SAX2XMLReader.hpp>


// ===========================================================================
// class declarations
// ===========================================================================
class GenericSAXHandler;
class SUMOSAXHandler;
class SUMOSAXReader;


// ===========================================================================
// class definitions
// ===========================================================================
/**
 * @class XMLSubSys
 * @brief Utility methods for initialising, closing and using the XML-subsystem
 *
 * The Xerces-parsers need an initialisation and should also be closed.
 *
 * As we use xerces for both the input files and the configuration we
 *  would have to check whether the system was initialised before. Instead,
 *  we call XMLSubSys::init(bool) once at the beginning of our application and
 *  XMLSubSys::close() at the end.
 *
 * Closing and initialising the XML subsystem is necessary. Still, we never
 *  encountered any problems with it. Once, after some modifications, SUMO
 *  crashed when closing the XML sub system. The reason was a memory leak
 *  within the microsim-module. On initialisation, a SAX2XMLReader is built
 *  which can be used during later process. It is destroyed when the subsystem
 *  is closed.
 *
 * In addition to initialisation and shutdown, this module allows to build
 *  SAXReaders and/or running a given handler on a given file without
 *  dealing with the reader at all.
 *
 * @todo make schema checking optional
 */
class XMLSubSys {
public:
    /**
     * @brief Initialises the xml-subsystem.
     *
     * Calls XMLPlatformUtils::Initialize(). If this fails, the exception is
     *  caught and its content is reported using a ProcessError.
     *
     * @exception ProcessError If the initialisation fails
     */
    static void init();


    /**
     * @brief Enables or disables validation.
     *
     * The setting is only valid for parsers created after the call. Existing parsers are not adapted.
     *
     * @param[in] validationScheme Whether validation of XML-documents against schemata shall be enabled
     * @param[in] netValidationScheme Whether validation of SUMO networks against schemata shall be enabled
     */
    static void setValidation(const std::string& validationScheme, const std::string& netValidationScheme);


    /**
     * @brief Closes the xml-subsystem
     *
     * Deletes the built reader and calls XMLPlatformUtils::Terminate();
     */
    static void close();


    /**
     * @brief Builds a reader and assigns the handler to it
     *
     * Tries to build a SAX2XMLReader using "getSAXReader()". If this
     *  fails, 0 is returned. Otherwise, the given handler is assigned
     *  to the reader as the current DefaultHandler and ErrorHandler.
     *
     * @param[in] handler The handler to assign to the built reader
     * @return The built Xerces-SAX-reader, 0 if something failed
     * @see getSAXReader()
     */
    static SUMOSAXReader* getSAXReader(SUMOSAXHandler& handler);


    /**
     * @brief Sets the given handler for the default reader
     *
     * Uses the reader built on init() which is stored in myReader.
     *
     * @param[in] handler The handler to assign to the built reader
     */
    static void setHandler(GenericSAXHandler& handler);


    /**
     * @brief Runs the given handler on the given file; returns if everything's ok
     *
     * Uses the reader built on init() which is stored in myReader to parse the given
     *  file.
     *
     * All exceptions are catched and reported to the error-instance of the MsgHandler.
     *  Also, if the reader could not be built, this is reported.
     *
     * The method returns true if everything went ok. This means, that the reader could be
     *  built, no exception was caught, and nothing was reported to the error-instance
     *  of the MsgHandler.
     *
     * @param[in] handler The handler to assign to the built reader
     * @param[in] file    The file to run the parser at
     * @param[in] isNet   whether a network gets loaded
     * @return true if the parsing was done without errors, false otherwise (error was printed)
     */
    static bool runParser(GenericSAXHandler& handler,
                          const std::string& file, const bool isNet = false);


private:
    /// @brief The XML Readers used for repeated parsing
    static std::vector<SUMOSAXReader*> myReaders;

    /// @brief Information whether the reader is parsing
    static unsigned int myNextFreeReader;

    /// @brief Information whether built reader/parser shall validate XML-documents against schemata
    static XERCES_CPP_NAMESPACE::SAX2XMLReader::ValSchemes myValidationScheme;

    /// @brief Information whether built reader/parser shall validate SUMO networks against schemata
    static XERCES_CPP_NAMESPACE::SAX2XMLReader::ValSchemes myNetValidationScheme;

};


#endif

/****************************************************************************/

