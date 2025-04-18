/*
    Lakeside Focuser
    Copyright (C) 2017 Phil Shepherd (psjshep@googlemail.com)
    Technical Information kindly supplied by Peter Chance at LakesideAstro (info@lakeside-astro.com)

    Code template from original Moonlite code by Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

/*
Modifications
0.1   psjshep xx-xxx-xxxx - 1st version
..
..
0.11  psjshep 17-Mar-2017 - changed PortT[0].text to serialConnection->port()

1.1 JM 29-11-2018: Misc fixes and improvements.
 */

#define LAKESIDE_VERSION_MAJOR 1
#define LAKESIDE_VERSION_MINOR 1

#include "lakeside.h"
#include <config.h>
#include "indicom.h"
#include "connectionplugins/connectionserial.h"

#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <memory>

// tty_read_section timeout in seconds
#define LAKESIDE_TIMEOUT    2
#define LAKESIDE_LEN        7

// Max number of Timeouts for a tty_read_section
// This is in case a buffer read is too fast
//  or nothing in the buffer during GetLakesideStatus()
#define LAKESIDE_TIMEOUT_RETRIES   2

static std::unique_ptr<Lakeside> lakeside(new Lakeside());

Lakeside::Lakeside()
{
    setVersion(LAKESIDE_VERSION_MAJOR, LAKESIDE_VERSION_MINOR);

    FI::SetCapability(FOCUSER_CAN_ABS_MOVE |
                      FOCUSER_CAN_REL_MOVE |
                      FOCUSER_CAN_ABORT    |
                      FOCUSER_CAN_REVERSE  |
                      FOCUSER_HAS_BACKLASH);
}

// Initialise
bool Lakeside::initProperties()
{
    INDI::Focuser::initProperties();

    // Current Direction
    //    IUFillSwitch(&MoveDirectionS[0], "Normal", "", ISS_ON);
    //    IUFillSwitch(&MoveDirectionS[1], "Reverse", "", ISS_OFF);
    //    IUFillSwitchVector(&MoveDirectionSP, MoveDirectionS, 2, getDeviceName(), "","Move Direction", MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Focuser temperature (degrees C) - read only
    IUFillNumber(&TemperatureN[0], "TEMPERATURE", "Celsius", "%3.2f", -50, 70., 0., 0.);
    IUFillNumberVector(&TemperatureNP, TemperatureN, 1, getDeviceName(), "FOCUS_TEMPERATURE", "Temperature (C)",
                       MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    // Focuser temperature (Kelvin)- read only & only read once at connect
    IUFillNumber(&TemperatureKN[0], "TEMPERATUREK", "Kelvin", "%3.2f", 0., 373.15, 0., 0.);
    IUFillNumberVector(&TemperatureKNP, TemperatureKN, 1, getDeviceName(), "FOCUS_TEMPERATUREK", "Temperature (K)",
                       MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    // Compensate for temperature
    IUFillSwitch(&TemperatureTrackingS[0], "Enable", "", ISS_OFF);
    IUFillSwitch(&TemperatureTrackingS[1], "Disable", "", ISS_ON);
    IUFillSwitchVector(&TemperatureTrackingSP, TemperatureTrackingS, 2, getDeviceName(), "Temperature Track", "",
                       MAIN_CONTROL_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Backlash 0-255
    //    IUFillNumber(&FocusBacklashN[0], "BACKLASH", "(0-255)", "%.f", 0, 255, 0, 0);
    //    IUFillNumberVector(&FocusBacklashNP, FocusBacklashN, 1, getDeviceName(), "BACKLASH", "Backlash", SETTINGS_TAB, IP_RW, 0, IPS_IDLE );
    FocusBacklashNP[0].setMin(0);
    FocusBacklashNP[0].setMax(255);
    FocusBacklashNP[0].setStep(10);
    FocusBacklashNP[0].setValue(0);

    // Maximum Travel - read only
    //    IUFillNumber(&MaxTravelN[0], "MAXTRAVEL", "No. Steps", "%.f", 1, 65536, 0, 10000);
    //    IUFillNumberVector(&MaxTravelNP, MaxTravelN, 1, getDeviceName(), "MAXTRAVEL", "Max travel(Via Ctrlr)", SETTINGS_TAB, IP_RO, 0, IPS_IDLE );
    FocusMaxPosNP.setPermission(IP_RO);

    // Step Size - read only
    IUFillNumber(&StepSizeN[0], "STEPSIZE", "No. Steps", "%.f", 1, 65536, 0, 1);
    IUFillNumberVector(&StepSizeNP, StepSizeN, 1, getDeviceName(), "STEPSIZE", "Step Size(Via Ctrlr)", SETTINGS_TAB, IP_RO, 0,
                       IPS_IDLE);

    // Active Temperature Slope - select 1 or 2
    IUFillSwitch(&ActiveTemperatureSlopeS[0], "Slope 1", "", ISS_ON);
    IUFillSwitch(&ActiveTemperatureSlopeS[1], "Slope 2", "", ISS_OFF);
    IUFillSwitchVector(&ActiveTemperatureSlopeSP, ActiveTemperatureSlopeS, 2, getDeviceName(), "Active Slope", "Active Slope",
                       SETTINGS_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Slope 1 : Directions
    IUFillSwitch(&Slope1DirS[0], "0", "", ISS_ON);
    IUFillSwitch(&Slope1DirS[1], "1", "", ISS_OFF);
    IUFillSwitchVector(&Slope1DirSP, Slope1DirS, 2, getDeviceName(), "Slope 1 Direction", "Slope 1 Direction", SETTINGS_TAB,
                       IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Slope 1 : Slope Increments (counts per degree, 0.1 step increments
    IUFillNumber(&Slope1IncN[0], "SLOPE1INC", "No. Steps (0-655356", "%.f", 0, 65536, 0, 0);
    IUFillNumberVector(&Slope1IncNP, Slope1IncN, 1, getDeviceName(), "SLOPE1INC", "Slope1 Increments", SETTINGS_TAB, IP_RW, 0,
                       IPS_IDLE );

    // slope 1 : Deadband - value between 0 and 255
    IUFillNumber(&Slope1DeadbandN[0], "SLOPE1DEADBAND", "(0-255)", "%.f", 0, 255, 0, 0);
    IUFillNumberVector(&Slope1DeadbandNP, Slope1DeadbandN, 1, getDeviceName(), "SLOPE1DEADBAND", "Slope 1 Deadband",
                       SETTINGS_TAB, IP_RW, 0, IPS_IDLE );

    // Slope 1 : Time Period (Minutes, 0.1 step increments
    IUFillNumber(&Slope1PeriodN[0], "SLOPE1PERIOD", "Minutes (0-99)", "%.f", 0, 99, 0, 0);
    IUFillNumberVector(&Slope1PeriodNP, Slope1PeriodN, 1, getDeviceName(), "SLOPE1PERIOD", "Slope 1 Period", SETTINGS_TAB,
                       IP_RW, 0, IPS_IDLE );

    // Slope 2 : Direction
    IUFillSwitch(&Slope2DirS[0], "0", "", ISS_ON);
    IUFillSwitch(&Slope2DirS[1], "1", "", ISS_OFF);
    IUFillSwitchVector(&Slope2DirSP, Slope2DirS, 2, getDeviceName(), "Slope 2 Direction", "", SETTINGS_TAB, IP_RW, ISR_1OFMANY,
                       0, IPS_IDLE);

    // slope 2 : Slope Increments (counts per degree, 0.1 step increments
    IUFillNumber(&Slope2IncN[0], "SLOPE2INC", "No. Steps (0-65536)", "%.f", 0, 65536, 0, 0);
    IUFillNumberVector(&Slope2IncNP, Slope2IncN, 1, getDeviceName(), "SLOPE2INC", "Slope 2 Increments", SETTINGS_TAB, IP_RW, 0,
                       IPS_IDLE );

    // slope 2 : Deadband - value between 0 and 255
    IUFillNumber(&Slope2DeadbandN[0], "SLOPE2DEADBAND", "Steps (0-255)", "%.f", 0, 255, 0, 0);
    IUFillNumberVector(&Slope2DeadbandNP, Slope2DeadbandN, 1, getDeviceName(), "SLOPE2DEADBAND", "Slope 2 Deadband",
                       SETTINGS_TAB, IP_RW, 0, IPS_IDLE );

    // slope 2 : Time Period (Minutes, 0.1 step increments)
    IUFillNumber(&Slope2PeriodN[0], "SLOPE2PERIOD", "Minutes (0-99)", "%.f", 0, 99, 0, 0);
    IUFillNumberVector(&Slope2PeriodNP, Slope2PeriodN, 1, getDeviceName(), "SLOPE2PERIOD", "Slope 2 Period", SETTINGS_TAB,
                       IP_RW, 0, IPS_IDLE );

    FocusAbsPosNP[0].setMin(0.);

    // shephpj - not used
    //FocusAbsPosNP[0].setMax(65536.);

    setDefaultPollingPeriod(1000);

    addDebugControl();

    return true;

}

bool Lakeside::updateProperties()
{
    INDI::Focuser::updateProperties();

    if (isConnected())
    {
        //defineProperty(&FocusBacklashNP);
        //defineProperty(&MaxTravelNP);
        defineProperty(&StepSizeNP);
        defineProperty(&TemperatureNP);
        defineProperty(&TemperatureKNP);
        //defineProperty(&MoveDirectionSP);
        defineProperty(&TemperatureTrackingSP);
        defineProperty(&ActiveTemperatureSlopeSP);
        defineProperty(&Slope1DirSP);
        defineProperty(&Slope1IncNP);
        defineProperty(&Slope1DeadbandNP);
        defineProperty(&Slope1PeriodNP);
        defineProperty(&Slope2DirSP);
        defineProperty(&Slope2IncNP);
        defineProperty(&Slope2DeadbandNP);
        defineProperty(&Slope2PeriodNP);

        GetFocusParams();

        LOG_INFO("Lakeside parameters updated, focuser ready for use.");
    }
    else
    {
        //deleteProperty(FocusBacklashNP.name);
        //deleteProperty(MaxTravelNP.name);
        deleteProperty(StepSizeNP.name);
        //deleteProperty(MoveDirectionSP.name);
        deleteProperty(TemperatureNP.name);
        deleteProperty(TemperatureKNP.name);
        deleteProperty(TemperatureTrackingSP.name);
        deleteProperty(ActiveTemperatureSlopeSP.name);
        deleteProperty(Slope1DirSP.name);
        deleteProperty(Slope1IncNP.name);
        deleteProperty(Slope1DeadbandNP.name);
        deleteProperty(Slope1PeriodNP.name);
        deleteProperty(Slope2DirSP.name);
        deleteProperty(Slope2IncNP.name);
        deleteProperty(Slope2DeadbandNP.name);
        deleteProperty(Slope2PeriodNP.name);
    }

    return true;

}

#if 0
// connect to focuser port
//
//        9600 baud
//        8 bits
//        0 parity
//        1 stop bit
//
bool Lakeside::Connect()
{
    int rc = 0;
    char errorMsg[MAXRBUF];

    //    if ( (rc = tty_connect(PortT[0].text, 9600, 8, 0, 1, &PortFD)) != TTY_OK)
    if ( (rc = tty_connect(serialConnection->port(), 9600, 8, 0, 1, &PortFD)) != TTY_OK)
    {
        tty_error_msg(rc, errorMsg, MAXRBUF);
        LOGF_INFO("Failed to connect to port %s, with Error %s", serialConnection->port(), errorMsg);
        return false;
    }

    LOGF_INFO("Connected to port %s", serialConnection->port());

    if (LakesideOnline())
    {
        LOGF_INFO("Lakeside is online on port %s", serialConnection->port());
        SetTimer(getCurrentPollingPeriod());
        return true;
    }
    else
    {
        LOGF_INFO("Unable to connect to Lakeside Focuser. Please ensure the controller is powered on and the port (%s) is correct.",
                  serialConnection->port());
        return false;
    }
}


// Disconnect from focuser
bool Lakeside::Disconnect()
{
    LOG_INFO("Lakeside is offline.");
    return INDI::Focuser::Disconnect();
}
#endif

bool Lakeside::Handshake()
{
    return LakesideOnline();
}

const char * Lakeside::getDefaultName()
{
    return "Lakeside";
}

//
// Send Lakeside a command
//
// In :
//      in_cmd : command to send to the focuser
//
// Returns true  for successful write
//         false for failed write
//
bool Lakeside::SendCmd(const char * in_cmd)
{
    int nbytes_written = 0, rc = -1;
    char errstr[MAXRBUF];

    LOGF_DEBUG("CMD <%s>", in_cmd);

    if ( (rc = tty_write_string(PortFD, in_cmd, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("SendCmd: Write for command (%s) failed - %s", in_cmd, errstr);
        return false;
    }

    return true;

}

//
// Read the Lakeside buffer, setting response to the contents
//
// Returns
//         true  : something to read in the buffer
//         false : error reading the buffer
//
bool Lakeside::ReadBuffer(char * response)
{
    int nbytes_read = 0, rc = -1;
    char resp[LAKESIDE_LEN] = {0};

    //strcpy(resp,"       ");
    // read until 0x23 (#) received
    if ( (rc = tty_read_section(PortFD, resp, 0x23, LAKESIDE_TIMEOUT, &nbytes_read)) != TTY_OK)
    {
        char errstr[MAXRBUF];
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("ReadBuffer: Read failed - %s", errstr);
        strncpy(response, "ERROR", LAKESIDE_LEN);
        return false;
    }

    //    char hex_cmd[LAKESIDE_LEN * 3] = {0};
    //    hexDump(hex_cmd, resp, LAKESIDE_LEN * 3);
    //    LOGF_DEBUG("RES <%s>", hex_cmd);

    resp[nbytes_read] = 0;
    LOGF_DEBUG("RES <%s>", resp);

    strncpy(response, resp, LAKESIDE_LEN);
    return true;
}

//
// check for OK# from Lakeside - i.e. it is responding
//
bool Lakeside::LakesideOnline()
{
    char resp[LAKESIDE_LEN] = {0};
    const char * cmd = "??#";

    //strcpy(resp,"       ");

    if (!SendCmd(cmd))
    {
        return false;
    }

    LOGF_DEBUG("LakesideOnline: Successfully sent (%s)", cmd);

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // if SendCmd succeeded, resp contains response from the command
    LOGF_DEBUG("LakesideOnline: Received (%s)", resp);

    if (!strncmp(resp, "OK#", 3))
    {
        LOG_DEBUG("LakesideOnline: Received OK# - Lakeside responded");
        return true;
    }
    else
    {
        LOGF_ERROR("LakesideOnline: OK# not found. Instead, received (%s)", resp);
        return false;
    }

}
// get current movement direction
//
// 0 = Normal
// 1 = Reversed
bool Lakeside::updateMoveDirection()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?D#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    //IUResetSwitch(&MoveDirectionSP);

    // direction is in form Dnnnnn#
    // where nnnnn is 0 for normal or 1 for reversed
    sscanf(resp, "D%5d#", &temp);

    if ( temp == 0)
    {
        FocusReverseSP[INDI_DISABLED].setState(ISS_ON);
        LOGF_DEBUG("updateMoveDirection: Move Direction is (%d)", temp);
    }
    else if ( temp == 1)
    {
        FocusReverseSP[INDI_ENABLED].setState(ISS_ON);
        LOGF_DEBUG("updateMoveDirection: Move Direction is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateMoveDirection: Unknown move Direction response (%s)", resp);
        return false;
    }

    return true;
}

// Decode contents of buffer
// Returns:
//          P : Position update found - FocusAbsPosNP[0].getValue() updated
//          T : Temperature update found - TemperatureN[0].value
//          K : Temperature in Kelvin update found - TemperatureKN[0].value
//          D : DONE# received
//          O : OK# received
//          E : Error due to unknown/malformed command having been sent
//          ? : unknown response received
char Lakeside::DecodeBuffer(char * in_response)
{
    int temp = 0, pos = 0, rc = -1;

    LOGF_DEBUG("DecodeBuffer: in_response (%s)", in_response);

    // if focuser finished moving, DONE# received
    if (!strncmp(in_response, "DONE#", 5))
    {
        return 'D';
    }

    // if focuser returned OK#
    if (!strncmp(in_response, "OK#", 3))
    {
        return 'O';
    }

    // if focuser returns an error for unknown command
    if (!strncmp(in_response, "!#", 2))
    {
        return 'E';
    }

    // Temperature update is Tnnnnnn# where nnnnn is left space padded
    if (!strcmp("TN/A#", in_response))
    {
        TemperatureNP.s = IPS_IDLE;
        return 'T';
    }
    else
    {
        rc = sscanf(in_response, "T%5d#", &temp);
        if (rc > 0)
        {
            // need to divide result by 2
            TemperatureN[0].value = ((int) temp) / 2.0;
            LOGF_DEBUG("DecodeBuffer: Result (%3.1f)", TemperatureN[0].value);

            return 'T';
        }
    }

    // Temperature update is Knnnnnn# where nnnnn is left space padded
    rc = sscanf(in_response, "K%5d#", &temp);
    if (rc > 0)
    {
        // need to divide result by 2
        TemperatureKN[0].value = ((int) temp) / 2.00;
        LOGF_DEBUG("DecodeBuffer: Result (%3.2f)", TemperatureKN[0].value);

        return 'K';
    }

    // look for step info Pnnnnn#
    rc = sscanf(in_response, "P%5d#", &pos);
    // focuser position returned Pnnnnn#
    if (rc > 0)
    {
        FocusAbsPosNP[0].setValue(pos);
        FocusAbsPosNP.apply();

        LOGF_DEBUG("DecodeBuffer: Returned position (%d)", pos);
        return 'P';
    }
    else
    {
        LOGF_ERROR("DecodeBuffer: Unknown response : (%s)", in_response);
        return '?';
    }
}

// Get Temperature in C from focuser
//
// Return :
//          true  : successfully got Temperature & updated INDI
//          false : Unable to get & update Temperature (timeout or other)
//
bool Lakeside::updateTemperature()
{
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?T#";
    char buffer_response = '?';

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    LOGF_DEBUG("updateTemperature: Read response (%s)", resp);

    // ascertain contents of buffer & update temp if necessary
    buffer_response = DecodeBuffer(resp);

    // if temperature updated, then return true
    if ( buffer_response == 'T' )
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Get Temperature in K from focuser
//
// Return :
//          true  : successfully got Temperature in K & updated INDI
//          false : Unable to get & update Temperature in K (timeout or other)
//
bool Lakeside::updateTemperatureK()
{
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?K#";
    char buffer_response = '?';

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    LOGF_DEBUG("updateTemperatureK: Read response (%s)", resp);

    // ascertain contents of buffer & update temp in K if necessary
    buffer_response = DecodeBuffer(resp);

    // if temperature updated, then return true
    if ( buffer_response == 'K' )
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Get position of focuser
//
// Return :
//          true  : successfully got focus position & updated INDI
//          false : Unable to get & update position (timeout or other)
//
bool Lakeside::updatePosition()
{
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?P#";
    char buffer_response = '?';

    if (!SendCmd(cmd))
    {
        return false;
    }

    LOGF_DEBUG("updatePosition: Successfully sent (%s)", cmd);

    if (!ReadBuffer(resp))
    {
        return false;
    }

    LOGF_DEBUG("updatePosition: Fetched (%s)", resp);

    // ascertain contents of buffer & update position if necessary
    buffer_response = DecodeBuffer(resp);

    if ( buffer_response == 'P' )
    {
        return true;
    }
    else
    {
        return false;
    }
}

// Get Backlash compensation
bool Lakeside::updateBacklash()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?B#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Backlash is in form Bnnnnn#
    // where nnnnn is 0 - 255, space left padded
    sscanf(resp, "B%5d#", &temp);

    if ( temp >= 0)
    {
        FocusBacklashNP[0].setValue(temp);
        LOGF_DEBUG("updateBacklash: Backlash is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateBacklash: Backlash request error (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 1 Increments
bool Lakeside::updateSlope1Inc()
{
    int temp = -1;
    char resp[LAKESIDE_LEN];
    char cmd[] = "?1#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 1 Increment is in form 1nnnnn#
    // where nnnnn is number of 0.1 step increments, space left padded
    sscanf(resp, "1%5d#", &temp);

    if ( temp >= 0)
    {
        Slope1IncN[0].value = temp;
        LOGF_DEBUG("updateSlope1Inc: Slope 1 Increments is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope1Inc: Slope 1 Increment request error (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 2 Increments
bool Lakeside::updateSlope2Inc()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?2#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 1 Increment is in form 1nnnnn#
    // where nnnnn is number of 0.1 step increments, space left padded
    sscanf(resp, "2%5d#", &temp);

    if ( temp >= 0)
    {
        Slope2IncN[0].value = temp;
        LOGF_DEBUG("updateSlope2Inc: Slope 2 Increments is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope2Inc: Slope 2 Increment request error (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 1 direction : 0 or 1
bool Lakeside::updateSlope1Dir()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?a#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 1 Direction is in form annnnn#
    // where nnnnn is either 0 or 1, space left padded
    sscanf(resp, "a%5d#", &temp);

    if ( temp == 0)
    {
        Slope1DirS[0].s = ISS_ON;
        LOGF_DEBUG("updateSlope1Dir: Slope 1 Direction is (%d)", temp);
    }
    else if ( temp == 1)
    {
        Slope1DirS[1].s = ISS_ON;
    }
    else
    {
        LOGF_ERROR("updateSlope1Dir: Unknown Slope 1 Direction response (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 2 direction : 0 or 1
bool Lakeside::updateSlope2Dir()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?b#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 2 Direction is in form annnnn#
    // where nnnnn is either 0 or 1, space left padded
    sscanf(resp, "b%5d#", &temp);

    if ( temp == 0)
    {
        Slope2DirS[0].s = ISS_ON;
        LOGF_DEBUG("updateSlope2Dir: Slope 2 Direction is (%d)", temp);
    }
    else if ( temp == 1)
    {
        Slope2DirS[1].s = ISS_ON;
    }
    else
    {
        LOGF_ERROR("updateSlope2Dir: Unknown Slope 2 Direction response (%s)", resp);
        return false;
    }

    return true;
}

// Get slope 1 deadband
bool Lakeside::updateSlope1Deadband()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?c#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Deadband is in form cnnnnn#
    // where nnnnn is 0 - 255, space left padded
    sscanf(resp, "c%5d#", &temp);

    if ( temp >= 0)
    {
        Slope1DeadbandN[0].value = temp;
        LOGF_DEBUG("updateSlope1Deadband: Slope 1 Deadband is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope1Deadband: Slope 1 Deadband request error (%s)", resp);
        return false;
    }

    return true;
}

// Get slope 2 deadband
bool Lakeside::updateSlope2Deadband()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?d#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Deadband is in form dnnnnn#
    // where nnnnn is 0 - 255, space left padded
    sscanf(resp, "d%5d#", &temp);

    if ( temp >= 0)
    {
        Slope2DeadbandN[0].value = temp;
        LOGF_DEBUG("updateSlope2Deadband: Slope 2 Deadband is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope2Deadband: Slope 2 Deadband request error (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 1 time period
bool Lakeside::updateSlope1Period()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?e#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 1 Period is in form ennnnn#
    // where nnnnn is number of 0.1 step increments, space left padded
    sscanf(resp, "e%5d#", &temp);

    if ( temp >= 0)
    {
        Slope1PeriodN[0].value = temp;
        LOGF_DEBUG("updateSlope1Period: Slope 1 Period is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope1Period: Slope 1 Period request error (%s)", resp);
        return false;
    }

    return true;
}

// get Slope 2 time period
bool Lakeside::updateSlope2Period()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?f#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // Slope 2 Period is in form ennnnn#
    // where nnnnn is number of 0.1 step increments, space left padded
    sscanf(resp, "f%5d#", &temp);

    if ( temp >= 0)
    {
        Slope2PeriodN[0].value = temp;
        LOGF_DEBUG("updateSlope2Period: Slope 2 Period is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateSlope2Period: Slope 2 Period request error (%s)", resp);
        return false;
    }

    return true;
}

// Get Max travel
bool Lakeside::updateMaxTravel()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?I#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // MaxTravel is in form Innnnn#
    // where nnnnn is 0 - 65536, space left padded
    sscanf(resp, "I%5d#", &temp);

    if ( temp > 0)
    {
        FocusMaxPosNP[0].setValue(temp);
        LOGF_DEBUG("updateMaxTravel: MaxTravel is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateMaxTravel: MaxTravel request error (%s)", resp);
        return false;
    }

    return true;
}

// get step size
bool Lakeside::updateStepSize()
{
    int temp = -1;
    char resp[LAKESIDE_LEN] = {0};
    char cmd[] = "?S#";

    if (!SendCmd(cmd))
    {
        return false;
    }

    LOGF_DEBUG("updateStepSize: Sent (%s)", cmd);

    if (!ReadBuffer(resp))
    {
        return false;
    }

    // StepSize is in form Snnnnn#
    // where nnnnn is 0 - ??, space left padded
    sscanf(resp, "S%5d#", &temp);

    if ( temp > 0)
    {
        StepSizeN[0].value = temp;
        LOGF_DEBUG("updateStepSize: step size is (%d)", temp);
    }
    else
    {
        LOGF_ERROR("updateStepSize: StepSize request error (%s)", resp);
        return false;
    }

    return true;
}

//
// NOTE : set via hand controller
//
bool Lakeside::setCalibration()
{
    return true;
}

// Move focuser to "position"
bool Lakeside::gotoPosition(uint32_t position)
{
    int calc_steps = 0;
    char cmd[LAKESIDE_LEN] = {0};

    // Lakeside only uses move NNNNN steps - goto step not available.
    // calculate as steps to move = current position - new position
    // if -ve then move out, else +ve moves in
    calc_steps = FocusAbsPosNP[0].getValue() - position;

    // MaxTravelN[0].value is set by "calibrate" via the control box, & read at connect
    if ( position > FocusMaxPosNP[0].getValue() )
    {
        LOGF_ERROR("Position requested (%ld) is out of bounds between %g and %g", position, FocusAbsPosNP[0].getMin(),
                   FocusMaxPosNP[0].getValue());
        FocusAbsPosNP.setState(IPS_ALERT);
        return false;
    }

    // -ve == Move Out
    if ( calc_steps < 0 )
    {
        snprintf(cmd, LAKESIDE_LEN,  "CO%d#", abs(calc_steps));
        LOGF_DEBUG("MoveFocuser: move-out cmd to send (%s)", cmd);
    }
    else
        // ve == Move In
        if ( calc_steps > 0 )
        {
            // Move in nnnnn steps = CInnnnn#
            snprintf(cmd, LAKESIDE_LEN,  "CI%d#", calc_steps);
            LOGF_DEBUG("MoveFocuser: move-in cmd to send (%s)", cmd);
        }
        else
        {
            // Zero == no steps to move
            LOGF_DEBUG("MoveFocuser: No steps to move. calc_steps = %d", calc_steps);
            FocusAbsPosNP.setState(IPS_OK);
            return false;
        }

    // flush ready to move
    tcflush(PortFD, TCIOFLUSH);

    if (!SendCmd(cmd))
    {
        FocusAbsPosNP.setState(IPS_ALERT);
        return false;
    }
    else
        LOGF_DEBUG("MoveFocuser: Sent cmd (%s)", cmd);

    // At this point, the move command has been sent, so set BUSY & return true
    FocusAbsPosNP.setState(IPS_BUSY);
    return true;
}

bool Lakeside::SetFocuserBacklash(int32_t steps)
{
    return setBacklash(steps);
}

bool Lakeside::SetFocuserBacklashEnabled(bool enabled)
{
    INDI_UNUSED(enabled);
    return true;
}

//
// Set backlash compensation
//
bool Lakeside::setBacklash(int backlash )
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRBnnn#
    snprintf(cmd, LAKESIDE_LEN, "CRB%d#", backlash);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Backlash steps set to %d", backlash);
    }
    else
    {
        LOGF_ERROR("setBacklash: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// NOTE : set via hand controller
//        Here for example
//
bool Lakeside::setStepSize(int stepsize )
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    // CRSnnnnn#
    snprintf(cmd, LAKESIDE_LEN,  "CRS%d#", stepsize);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_DEBUG("setStepSize: cmd (%s) - %s", cmd, resp);
    }
    else
    {
        LOGF_ERROR("setStepSize: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// NOTE : set via hand controller
//        Use calibrate routine on controller box
//
bool Lakeside::setMaxTravel(int /*maxtravel*/ )
{
    return true;
}

// Change Move Direction
// 0 = Normal direction
// 1 = Reverse direction
// In case motor connection is on reverse side of the focus shaft
// NOTE : This just reverses the voltage sent to the motor
//        & does NOT reverse the CI / CO commands
//bool Lakeside::setMoveDirection(int direction)
bool Lakeside::ReverseFocuser(bool enabled)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    strncpy(cmd, enabled ? "CRD1#" : "CRD0#", LAKESIDE_LEN);

    //    if (direction == 0)
    //        strncpy(cmd, "CRD0#", LAKESIDE_LEN);
    //    else
    //        if (direction == 1)
    //            strncpy(cmd, "CRD1#", LAKESIDE_LEN);
    //        else
    //        {
    //            LOGF_ERROR("setMoveDirection: Unknown direction (%d)", direction);
    //            return false;
    //        }

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_DEBUG("setMoveDirection: Completed cmd (%s). Result - %s", cmd, resp);
        if (!enabled)
            LOG_INFO("Move Direction : Normal");
        else
            LOG_INFO("Move Direction : Reversed");
    }
    else
    {
        LOGF_ERROR("setMoveDirection: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

// Enable/disable Temperature Tracking functionality
bool Lakeside::setTemperatureTracking(bool enable)
{
    int nbytes_written = 0, rc = -1;
    char errstr[MAXRBUF];
    char cmd[LAKESIDE_LEN] = {0};

    // flush all
    tcflush(PortFD, TCIOFLUSH);

    if (enable)
        strncpy(cmd, "CTN#", LAKESIDE_LEN);
    else
        strncpy(cmd, "CTF#", LAKESIDE_LEN);

    if ( (rc = tty_write_string(PortFD, cmd, &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("setTemperatureTracking: Write for command (%s) failed - %s", cmd, errstr);
        return false;
    }
    else
    {
        LOGF_DEBUG("setTemperatureTracking: Sent (%s)", cmd);
        if (enable)
            LOG_INFO("Temperature Tracking : Enabled");
        else
            LOG_INFO("Temperature Tracking : Disabled");
    }

    // NOTE: NO reply string is sent back

    return true;

}

// Set which Active Temperature slope to use : 1 or 2
bool Lakeside::setActiveTemperatureSlope(uint32_t active_slope)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    // flush all
    tcflush(PortFD, TCIOFLUSH);

    // slope in is either 1 or 2
    // CRg1# : Slope 1
    // CRg2# : Slope 2

    snprintf(cmd, LAKESIDE_LEN,  "CRg%d#", active_slope);

    if (!SendCmd(cmd))
    {
        return false;
    }

    LOGF_DEBUG("setActiveTemperatureSlope: Sent (%s)", cmd);

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Selected Active Temperature Slope is %d", active_slope);
    }
    else
    {
        LOGF_ERROR("setActiveTemperatureSlope: Unknown result (%s)", resp);
        return false;
    }

    return true;

}

//
// Set Slope 1 0.1 step increments
//
bool Lakeside::setSlope1Inc(uint32_t slope1_inc)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CR1nnn#
    snprintf(cmd, LAKESIDE_LEN,  "CR1%d#", slope1_inc);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 1 0.1 counts per degree set to %d", slope1_inc);
    }
    else
    {
        LOGF_ERROR("setSlope1Inc: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 2 0.1 step increments
//
bool Lakeside::setSlope2Inc(uint32_t slope2_inc)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CR2nnn#
    snprintf(cmd, LAKESIDE_LEN,  "CR2%d#", slope2_inc);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 2 0.1 counts per degree set to %d", slope2_inc);
    }
    else
    {
        LOGF_ERROR("setSlope2Inc: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set slope 1 direction 0 or 1
//
bool Lakeside::setSlope1Dir(uint32_t slope1_direction)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRannn#
    snprintf(cmd, LAKESIDE_LEN,  "CRa%d#", slope1_direction);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 1 Direction set to %d", slope1_direction);
    }
    else
    {
        LOGF_ERROR("setSlope1Dir: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 2 Direction 0 or 1
//
bool Lakeside::setSlope2Dir(uint32_t slope2_direction)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRannn#
    snprintf(cmd, LAKESIDE_LEN,  "CRb%d#", slope2_direction);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 2 Direction set to %d", slope2_direction);
    }
    else
    {
        LOGF_ERROR("setSlope2Dir: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 1 Deadband 0 - 255
//
bool Lakeside::setSlope1Deadband(uint32_t slope1_deadband)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRcnnn#
    snprintf(cmd, LAKESIDE_LEN,  "CRc%d#", slope1_deadband);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 1 deadband set to %d", slope1_deadband);
    }
    else
    {
        LOGF_ERROR("setSlope1Deadband: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 1 Deadband 0 - 255
//
bool Lakeside::setSlope2Deadband(uint32_t slope2_deadband)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRdnnn#
    snprintf(cmd, LAKESIDE_LEN,  "CRd%d#", slope2_deadband);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 2 deadband set to %d", slope2_deadband);
    }
    else
    {
        LOGF_ERROR("setSlope2Deadband: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 1 Period in minutes
//
bool Lakeside::setSlope1Period(uint32_t slope1_period)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRennn#
    snprintf(cmd, LAKESIDE_LEN,  "CRe%d#", slope1_period);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 1 Period set to %d", slope1_period);
    }
    else
    {
        LOGF_ERROR("setSlope1Period: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Set Slope 2 Period in minutes
//
bool Lakeside::setSlope2Period(uint32_t slope2_period)
{
    char cmd[LAKESIDE_LEN] = {0};
    char resp[LAKESIDE_LEN] = {0};

    tcflush(PortFD, TCIOFLUSH);

    //CRfnnn#
    snprintf(cmd, LAKESIDE_LEN,  "CRf%d#", slope2_period);

    if (!SendCmd(cmd))
    {
        return false;
    }

    if (!ReadBuffer(resp))
    {
        return false;
    }

    if (!strncmp(resp, "OK#", 3))
    {
        LOGF_INFO("Slope 2 Period set to %d", slope2_period);
    }
    else
    {
        LOGF_ERROR("setSlope2Period: Unknown result (%s)", resp);
        return false;
    }

    return true;
}

//
// Process client new switch
//
bool Lakeside::ISNewSwitch (const char * dev, const char * name, ISState * states, char * names[], int n)
{
    if(strcmp(dev, getDeviceName()) == 0)
    {
        // Move Direction
        //        if (!strcmp(MoveDirectionSP.name, name))
        //        {
        //            bool rc=false;
        //            int current_mode = IUFindOnSwitchIndex(&MoveDirectionSP);
        //            IUUpdateSwitch(&MoveDirectionSP, states, names, n);
        //            int target_mode = IUFindOnSwitchIndex(&MoveDirectionSP);
        //            if (current_mode == target_mode)
        //            {
        //                MoveDirectionSP.s = IPS_OK;
        //                IDSetSwitch(&MoveDirectionSP, nullptr);
        //            }
        //            // switch will be either 0 for normal or 1 for reverse
        //            rc = setMoveDirection(target_mode);

        //            if (rc == false)
        //            {
        //                IUResetSwitch(&MoveDirectionSP);
        //                MoveDirectionS[current_mode].s = ISS_ON;
        //                MoveDirectionSP.s = IPS_ALERT;
        //                IDSetSwitch(&MoveDirectionSP, nullptr);
        //                return false;
        //            }

        //            MoveDirectionSP.s = IPS_OK;
        //            IDSetSwitch(&MoveDirectionSP, nullptr);
        //            return true;
        //        }

        // Temperature Tracking
        if (!strcmp(TemperatureTrackingSP.name, name))
        {
            int last_index = IUFindOnSwitchIndex(&TemperatureTrackingSP);
            IUUpdateSwitch(&TemperatureTrackingSP, states, names, n);

            bool rc = setTemperatureTracking((TemperatureTrackingS[0].s == ISS_ON));

            if (rc == false)
            {
                TemperatureTrackingSP.s = IPS_ALERT;
                IUResetSwitch(&TemperatureTrackingSP);
                TemperatureTrackingS[last_index].s = ISS_ON;
                IDSetSwitch(&TemperatureTrackingSP, nullptr);
                return false;
            }

            TemperatureTrackingSP.s = IPS_OK;
            IDSetSwitch(&TemperatureTrackingSP, nullptr);

            return true;
        }

        // Active Temperature Slope
        if (!strcmp(ActiveTemperatureSlopeSP.name, name))
        {
            bool rc = false;
            int current_slope = IUFindOnSwitchIndex(&ActiveTemperatureSlopeSP);
            // current slope Selection will be either 1 or 2
            // Need to add 1 to array index, as it starts at 0
            current_slope++;
            IUUpdateSwitch(&ActiveTemperatureSlopeSP, states, names, n);
            int target_slope = IUFindOnSwitchIndex(&ActiveTemperatureSlopeSP);
            // target slope Selection will be either 1 or 2
            // Need to add 1 to array index, as it starts at 0
            target_slope++;
            if (current_slope == target_slope)
            {
                ActiveTemperatureSlopeSP.s = IPS_OK;
                IDSetSwitch(&ActiveTemperatureSlopeSP, nullptr);
            }

            rc = setActiveTemperatureSlope(target_slope);

            if (rc == false)
            {
                current_slope--;
                IUResetSwitch(&ActiveTemperatureSlopeSP);
                ActiveTemperatureSlopeS[current_slope].s = ISS_ON;
                ActiveTemperatureSlopeSP.s = IPS_ALERT;
                IDSetSwitch(&ActiveTemperatureSlopeSP, nullptr);
                return false;
            }

            ActiveTemperatureSlopeSP.s = IPS_OK;
            IDSetSwitch(&ActiveTemperatureSlopeSP, nullptr);
            return true;
        }

        // Slope 1 direction - either 0 or 1
        if (!strcmp(Slope1DirSP.name, name))
        {
            bool rc = false;
            int current_slope_dir1 = IUFindOnSwitchIndex(&Slope1DirSP);
            // current slope 1 Direction will be either 0 or 1

            IUUpdateSwitch(&Slope1DirSP, states, names, n);
            int target_slope_dir1 = IUFindOnSwitchIndex(&Slope1DirSP);
            // target slope Selection will be either 0 or 1

            if (current_slope_dir1 == target_slope_dir1)
            {
                Slope1DirSP.s = IPS_OK;
                IDSetSwitch(&Slope1DirSP, nullptr);
            }

            rc = setSlope1Dir(target_slope_dir1);

            if (rc == false)
            {
                IUResetSwitch(&Slope1DirSP);
                Slope1DirS[current_slope_dir1].s = ISS_ON;
                Slope1DirSP.s = IPS_ALERT;
                IDSetSwitch(&Slope1DirSP, nullptr);
                return false;
            }

            Slope1DirSP.s = IPS_OK;
            IDSetSwitch(&Slope1DirSP, nullptr);
            return true;
        }
    }

    // Slope 2 direction - either 0 or 1
    if (!strcmp(Slope2DirSP.name, name))
    {
        bool rc = false;
        int current_slope_dir2 = IUFindOnSwitchIndex(&Slope2DirSP);
        // current slope 2 Direction will be either 0 or 1

        IUUpdateSwitch(&Slope2DirSP, states, names, n);
        int target_slope_dir2 = IUFindOnSwitchIndex(&Slope2DirSP);
        // target slope 2 Selection will be either 0 or 1

        if (current_slope_dir2 == target_slope_dir2)
        {
            Slope2DirSP.s = IPS_OK;
            IDSetSwitch(&Slope2DirSP, nullptr);
        }

        rc = setSlope2Dir(target_slope_dir2);

        if (rc == false)
        {
            IUResetSwitch(&Slope2DirSP);
            Slope2DirS[current_slope_dir2].s = ISS_ON;
            Slope2DirSP.s = IPS_ALERT;
            IDSetSwitch(&Slope2DirSP, nullptr);
            return false;
        }

        Slope2DirSP.s = IPS_OK;
        IDSetSwitch(&Slope2DirSP, nullptr);
        return true;
    }

    return INDI::Focuser::ISNewSwitch(dev, name, states, names, n);
}

//
// Process client new number
//
bool Lakeside::ISNewNumber (const char * dev, const char * name, double values[], char * names[], int n)
{
    int i = 0;

    if(strcmp(dev, getDeviceName()) == 0)
    {
        //        // max travel - read only
        //        if (!strcmp (name, MaxTravelNP.name))
        //        {
        //            IUUpdateNumber(&MaxTravelNP, values, names, n);
        //            MaxTravelNP.s = IPS_OK;
        //            IDSetNumber(&MaxTravelNP, nullptr);
        //            return true;
        //        }

        // Backlash compensation
        //        if (!strcmp (name, FocusBacklashNP.name))
        //        {
        //            int new_back = 0 ;
        //            int nset = 0;

        //            for (nset = i = 0; i < n; i++)
        //            {
        //                //Find numbers with the passed names in SetFocusBacklashNP property
        //                INumber * eqp = IUFindNumber (&FocusBacklashNP, names[i]);

        //                //If the number found is Backlash (FocusBacklashN[0]) then process it
        //                if (eqp == &FocusBacklashN[0])
        //                {

        //                    new_back = (values[i]);

        //                    // limits
        //                    nset += new_back >= -0xff && new_back <= 0xff;
        //                }
        //                if (nset == 1)
        //                {

        //                    // Set the Lakeside state to BUSY
        //                    FocusBacklashNP.s = IPS_BUSY;
        //                    IDSetNumber(&FocusBacklashNP, nullptr);

        //                    if( !setBacklash(new_back))
        //                    {

        //                        FocusBacklashNP.s = IPS_IDLE;
        //                        IDSetNumber(&FocusBacklashNP, "Setting new backlash failed.");

        //                        return false ;
        //                    }

        //                    FocusBacklashNP.s = IPS_OK;
        //                    FocusBacklashNP[0].setValue(new_back);
        //                    IDSetNumber(&FocusBacklashNP, nullptr);

        //                    return true;
        //                }
        //                else
        //                {

        //                    FocusBacklashNP.s = IPS_IDLE;
        //                    IDSetNumber(&FocusBacklashNP, "Need exactly one parameter.");

        //                    return false ;
        //                }

        //            }
        //        }

        // Step size - read only
        if (!strcmp (name, StepSizeNP.name))
        {
            IUUpdateNumber(&StepSizeNP, values, names, n);
            StepSizeNP.s = IPS_OK;
            IDSetNumber(&StepSizeNP, nullptr);
            return true;
        }

        // Slope 1 Increments
        if (!strcmp (name, Slope1IncNP.name))
        {
            int new_Slope1Inc = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope1IncNP property
                INumber * eqp = IUFindNumber (&Slope1IncNP, names[i]);

                //If the number found is Slope1Inc (Slope1IncN[0]) then process it
                if (eqp == &Slope1IncN[0])
                {

                    new_Slope1Inc = (values[i]);

                    // limits
                    nset += new_Slope1Inc >= -0xff && new_Slope1Inc <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope1IncNP.s = IPS_BUSY;
                    IDSetNumber(&Slope1IncNP, nullptr);

                    if( !setSlope1Inc(new_Slope1Inc))
                    {

                        Slope1IncNP.s = IPS_IDLE;
                        IDSetNumber(&Slope1IncNP, "Setting new Slope1 increment failed.");

                        return false ;
                    }

                    Slope1IncNP.s = IPS_OK;
                    Slope1IncN[0].value = new_Slope1Inc;
                    IDSetNumber(&Slope1IncNP, nullptr) ;

                    return true;
                }
                else
                {

                    Slope1IncNP.s = IPS_IDLE;
                    IDSetNumber(&Slope1IncNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }

        // Slope 2 Increments
        if (!strcmp (name, Slope2IncNP.name))
        {
            int new_Slope2Inc = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope2IncNP property
                INumber * eqp = IUFindNumber (&Slope2IncNP, names[i]);

                //If the number found is Slope2Inc (Slope2IncN[0]) then process it
                if (eqp == &Slope2IncN[0])
                {

                    new_Slope2Inc = (values[i]);

                    // limits
                    nset += new_Slope2Inc >= -0xff && new_Slope2Inc <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope2IncNP.s = IPS_BUSY;
                    IDSetNumber(&Slope2IncNP, nullptr);

                    if( !setSlope2Inc(new_Slope2Inc))
                    {

                        Slope2IncNP.s = IPS_IDLE;
                        IDSetNumber(&Slope2IncNP, "Setting new Slope2 increment failed.");

                        return false ;
                    }

                    Slope2IncNP.s = IPS_OK;
                    Slope2IncN[0].value = new_Slope2Inc;
                    IDSetNumber(&Slope2IncNP, nullptr);

                    return true;
                }
                else
                {

                    Slope2IncNP.s = IPS_IDLE;
                    IDSetNumber(&Slope2IncNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }

        // Slope 1 Deadband
        if (!strcmp (name, Slope1DeadbandNP.name))
        {
            int new_Slope1Deadband = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope1DeadbandNP property
                INumber * eqp = IUFindNumber (&Slope1DeadbandNP, names[i]);

                //If the number found is Slope1Deadband (Slope1DeadbandN[0]) then process it
                if (eqp == &Slope1DeadbandN[0])
                {

                    new_Slope1Deadband = (values[i]);

                    // limits
                    nset += new_Slope1Deadband >= -0xff && new_Slope1Deadband <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope1DeadbandNP.s = IPS_BUSY;
                    IDSetNumber(&Slope1DeadbandNP, nullptr);

                    if( !setSlope1Deadband(new_Slope1Deadband))
                    {

                        Slope1DeadbandNP.s = IPS_IDLE;
                        IDSetNumber(&Slope1DeadbandNP, "Setting new Slope 1 Deadband failed.");

                        return false ;
                    }

                    Slope1DeadbandNP.s = IPS_OK;
                    Slope1DeadbandN[0].value = new_Slope1Deadband;
                    IDSetNumber(&Slope1DeadbandNP, nullptr) ;

                    return true;
                }
                else
                {

                    Slope1DeadbandNP.s = IPS_IDLE;
                    IDSetNumber(&Slope1DeadbandNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }

        // Slope 2 Deadband
        if (!strcmp (name, Slope2DeadbandNP.name))
        {
            int new_Slope2Deadband = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope2DeadbandNP property
                INumber * eqp = IUFindNumber (&Slope2DeadbandNP, names[i]);

                //If the number found is Slope2Deadband (Slope2DeadbandN[0]) then process it
                if (eqp == &Slope2DeadbandN[0])
                {

                    new_Slope2Deadband = (values[i]);

                    // limits
                    nset += new_Slope2Deadband >= -0xff && new_Slope2Deadband <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope2DeadbandNP.s = IPS_BUSY;
                    IDSetNumber(&Slope2DeadbandNP, nullptr);

                    if( !setSlope2Deadband(new_Slope2Deadband))
                    {

                        Slope2DeadbandNP.s = IPS_IDLE;
                        IDSetNumber(&Slope2DeadbandNP, "Setting new Slope 2 Deadband failed.");

                        return false ;
                    }

                    Slope2DeadbandNP.s = IPS_OK;
                    Slope2DeadbandN[0].value = new_Slope2Deadband;
                    IDSetNumber(&Slope2DeadbandNP, nullptr) ;

                    return true;
                }
                else
                {

                    Slope2DeadbandNP.s = IPS_IDLE;
                    IDSetNumber(&Slope2DeadbandNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }

        // Slope 1 Period Minutes
        if (!strcmp (name, Slope1PeriodNP.name))
        {
            int new_Slope1Period = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope1PeriodNP property
                INumber * eqp = IUFindNumber (&Slope1PeriodNP, names[i]);

                //If the number found is Slope1Period (Slope1PeriodN[0]) then process it
                if (eqp == &Slope1PeriodN[0])
                {

                    new_Slope1Period = (values[i]);

                    // limits
                    nset += new_Slope1Period >= -0xff && new_Slope1Period <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope1PeriodNP.s = IPS_BUSY;
                    IDSetNumber(&Slope1PeriodNP, nullptr);

                    if( !setSlope1Period(new_Slope1Period))
                    {

                        Slope1PeriodNP.s = IPS_IDLE;
                        IDSetNumber(&Slope1PeriodNP, "Setting new Slope 1 Period failed.");

                        return false ;
                    }

                    Slope1PeriodNP.s = IPS_OK;
                    Slope1PeriodN[0].value = new_Slope1Period;
                    IDSetNumber(&Slope1PeriodNP, nullptr);

                    return true;
                }
                else
                {

                    Slope1PeriodNP.s = IPS_IDLE;
                    IDSetNumber(&Slope1PeriodNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }

        // Slope 2 Period Minutes
        if (!strcmp (name, Slope2PeriodNP.name))
        {
            int new_Slope2Period = 0 ;
            int nset = 0;

            for (nset = i = 0; i < n; i++)
            {
                //Find numbers with the passed names in SetSlope2PeriodNP property
                INumber * eqp = IUFindNumber (&Slope2PeriodNP, names[i]);

                //If the number found is Slope2Period (Slope2PeriodN[0]) then process it
                if (eqp == &Slope2PeriodN[0])
                {

                    new_Slope2Period = (values[i]);

                    // limits
                    nset += new_Slope2Period >= -0xff && new_Slope2Period <= 0xff;
                }
                if (nset == 1)
                {

                    // Set the Lakeside state to BUSY
                    Slope2PeriodNP.s = IPS_BUSY;
                    IDSetNumber(&Slope2PeriodNP, nullptr);

                    if( !setSlope2Period(new_Slope2Period))
                    {

                        Slope2PeriodNP.s = IPS_IDLE;
                        IDSetNumber(&Slope2PeriodNP, "Setting new Slope 2 Period failed.");

                        return false ;
                    }

                    Slope2PeriodNP.s = IPS_OK;
                    Slope2PeriodN[0].value = new_Slope2Period;
                    IDSetNumber(&Slope2PeriodNP, nullptr);

                    return true;
                }
                else
                {

                    Slope2PeriodNP.s = IPS_IDLE;
                    IDSetNumber(&Slope2PeriodNP, "Need exactly one parameter.");

                    return false ;
                }

            }
        }
    }

    return INDI::Focuser::ISNewNumber(dev, name, values, names, n);
}

//
// Get focus paraameters
//
void Lakeside::GetFocusParams ()
{
    if (updatePosition())
        FocusAbsPosNP.apply();

    if (updateTemperature())
        IDSetNumber(&TemperatureNP, nullptr);

    // This is currently the only time Kelvin is read - just a nice to have
    if (updateTemperatureK())
        IDSetNumber(&TemperatureKNP, nullptr);

    if (updateBacklash())
        FocusBacklashNP.apply();

    if (updateMaxTravel())
        FocusMaxPosNP.apply();

    if (updateStepSize())
        IDSetNumber(&StepSizeNP, nullptr);

    if (updateMoveDirection())
        FocusReverseSP.apply();

    if (updateSlope1Inc())
        IDSetNumber(&Slope1IncNP, nullptr);

    if (updateSlope2Inc())
        IDSetNumber(&Slope2IncNP, nullptr);

    if (updateSlope1Dir())
        IDSetSwitch(&Slope1DirSP, nullptr);

    if (updateSlope2Dir())
        IDSetSwitch(&Slope2DirSP, nullptr);

    if (updateSlope1Deadband())
        IDSetNumber(&Slope1DeadbandNP, nullptr);

    if (updateSlope2Deadband())
        IDSetNumber(&Slope2DeadbandNP, nullptr);

    if (updateSlope1Period())
        IDSetNumber(&Slope1PeriodNP, nullptr);

    if (updateSlope1Period())
        IDSetNumber(&Slope2PeriodNP, nullptr);

}

IPState Lakeside::MoveRelFocuser(FocusDirection dir, uint32_t ticks)
{
    return MoveAbsFocuser(dir == FOCUS_INWARD ? FocusAbsPosNP[0].getValue() - ticks : FocusAbsPosNP[0].getValue() + ticks);
}

//
// Main Lakeside Absolute movement routine
//
IPState Lakeside::MoveAbsFocuser(uint32_t targetTicks)
{
    targetPos = targetTicks;
    bool rc = false;

    rc = gotoPosition(targetPos);

    return (rc ? IPS_BUSY : IPS_ALERT);
}

//
// Main timer hit routine
//
void Lakeside::TimerHit()
{
    bool IsMoving = false;
    int rc = -1;

    if (isConnected() == false)
    {
        SetTimer(getCurrentPollingPeriod());
        return;
    }

    // focuser supposedly moving...
    if (FocusAbsPosNP.getState() == IPS_BUSY )
    {
        // Get actual status from focuser
        // Note: GetLakesideStatus sends position count when moving.
        //       Status returns IMoving if moving
        IsMoving = GetLakesideStatus();
        if ( IsMoving )
        {
            // GetLakesideStatus() shows position as it is moving
            LOG_DEBUG("Focuser is in motion...");
        }
        else
        {
            // no longer moving, so reset state to IPS_OK or IDLE?
            // IPS_OK turns light green
            FocusAbsPosNP.setState(IPS_OK);
            // update position
            // This is necessary in case user clicks short step moves in quick succession
            // Lakeside will abort move if command received during move
            rc = updatePosition();
            FocusAbsPosNP.apply();
            LOGF_INFO("Focuser reached requested position %.f", FocusAbsPosNP[0].getValue());
        }
    }

    // focuser not moving, get temperature updates instead
    if (FocusAbsPosNP.getState() == IPS_OK || FocusAbsPosNP.getState() == IPS_IDLE)
    {
        // Get a temperature
        rc = updateTemperature();
        if (rc && fabs(lastTemperature - TemperatureN[0].value) > TEMPERATURE_THRESHOLD)
        {
            IDSetNumber(&TemperatureNP, nullptr);
            lastTemperature = TemperatureN[0].value;
        }
    }

    // IPS_ALERT - any alert situation generated
    //    if ( FocusAbsPosNP.getState() == IPS_ALERT )
    //    {
    //        LOG_DEBUG("TimerHit: Focuser state = IPS_ALERT");
    //    }

    SetTimer(getCurrentPollingPeriod());

}

//
// This will check the status is the focuser - used to check if moving
//
// Returns Pnnnnn#  : Focuser Moving                : return true
// empty (time out) : Focuser Idle - NOT moving     : return false
// Returns DONE#    : Focuser Finished moving       : return false
// Returns OK#      : Focuser NOT moving (catchall) : return false
bool Lakeside::GetLakesideStatus()
{
    int rc = -1, nbytes_read = 0, count_timeouts = 1, pos = 0;
    char errstr[MAXRBUF];
    char resp[LAKESIDE_LEN] = {0};
    bool read_buffer = true;
    char buffer_response = '?';

    // read buffer up to LAKESIDE_TIMEOUT_RETRIES times
    while (read_buffer)
    {
        //strcpy(resp,"       ");
        memset(resp, 0, sizeof(resp));
        // read until 0x23 (#) received
        if ( (rc = tty_read_section(PortFD, resp, 0x23, LAKESIDE_TIMEOUT, &nbytes_read)) != TTY_OK)
        {
            // Retry LAKESIDE_TIMEOUT_RETRIES times to make sure focuser
            // is not in between status returns
            count_timeouts++;
            LOGF_DEBUG("GetLakesideStatus: read buffer retry attempts : %d, error=%s", count_timeouts, errstr);

            if (count_timeouts > LAKESIDE_TIMEOUT_RETRIES)
            {
                tty_error_msg(rc, errstr, MAXRBUF);
                LOGF_DEBUG("GetLakesideStatus: Timeout limit (%d) reached reading buffer. Error - %s", LAKESIDE_TIMEOUT_RETRIES, errstr);

                // force a get focuser position update
                rc = updatePosition();

                // return false as focuser is NOT known to be moving
                return false;
            } // if (count_timeouts > LAKESIDE_TIMEOUT_RETRIES)
        }
        else
            read_buffer = false;  // break out of loop as buffer has been read
    }  // end while

    // At this point, something has been returned from the buffer
    // Therefore, decode response

    LOGF_DEBUG("GetLakesideStatus: Read buffer contains : %s", resp);

    // decode the contents of the buffer (Temp & Pos are also updated)
    buffer_response = DecodeBuffer(resp);

    // If DONE# then focuser has finished a move, so get position
    if ( buffer_response == 'D' )
    {
        LOG_DEBUG("GetLakesideStatus: Found DONE# after move request");

        // update the current position
        rc = updatePosition();

        // IPS_IDLE turns off light, IPS_OK turns light green
        FocusAbsPosNP.setState(IPS_OK);

        // return false as focuser is not known to be moving
        return false;
    }

    // If focuser moving > 200 steps, DecodeBuffer returns 'P'
    //    & updates position
    if ( buffer_response == 'P' )
    {
        // get step position for update message
        rc = sscanf(resp, "P%5d#", &pos);
        LOGF_INFO("Focuser Moving... position : %d", pos);
        // Update current position
        FocusAbsPosNP[0].setValue(pos);
        FocusAbsPosNP.apply();

        // return true as focuser IS moving
        return true;
    }

    // Possible that Temperature response still in the buffer?
    if ( buffer_response == 'T' )
    {
        LOGF_DEBUG("GetLakesideStatus: Temperature status response found - %s", resp);
        // return false as focuser is not known to be moving

        // IPS_IDLE turns off light, IPS_OK turns light green
        FocusAbsPosNP.setState(IPS_OK);

        return false;
    }

    // Possible that Temperature in K response still in the buffer?
    if ( buffer_response == 'K' )
    {
        LOGF_DEBUG("GetLakesideStatus: Temperature in K status response found - %s", resp);
        // return false as focuser is not known to be moving

        // IPS_IDLE turns off light, IPS_OK turns light green
        FocusAbsPosNP.setState(IPS_OK);

        return false;
    }

    // At this point, something else is returned
    LOGF_DEBUG("GetLakesideStatus: Unknown response from buffer read : (%s)", resp);
    FocusAbsPosNP.setState(IPS_OK);

    // return false as focuser is not known to be moving
    return false;

}

//
// send abort command
//
bool Lakeside::AbortFocuser()
{
    int rc = -1;
    char errstr[MAXRBUF];
    char cmd[] = "CH#";

    if (SendCmd(cmd))
    {
        // IPS_IDLE turns off light, IPS_OK turns light green
        FocusAbsPosNP.setState(IPS_IDLE);
        FocusAbsPosNP.setState(IPS_OK);
        LOG_INFO("Focuser Abort Sent");
        return true;
    }
    else
    {
        tty_error_msg(rc, errstr, MAXRBUF);
        LOGF_ERROR("AbortFocuser: Write command (%s) failed - %s", cmd, errstr);
        return false;
    }
}

/////////////////////////////////////////////////////////////////////////////
///
/////////////////////////////////////////////////////////////////////////////
void Lakeside::hexDump(char * buf, const char * data, int size)
{
    for (int i = 0; i < size; i++)
        sprintf(buf + 3 * i, "%02X ", static_cast<uint8_t>(data[i]));

    if (size > 0)
        buf[3 * size - 1] = '\0';
}


// End Lakeside Focuser
