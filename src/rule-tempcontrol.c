/*	Copyright (C) 2018, 2020 Harris M. Snyder

	This file is part of Tagfd.

	Tagfd is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Foobar is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Foobar.  If not, see <https://www.gnu.org/licenses/>.
*/

/*

    Simple example rule to control power output based on temperature at the thermostat. 

    Harris M. Snyder

*/

// ====================================================================================
//  Macro definitions required by 'ruletoolkit.h'
// ====================================================================================

#define RULENAME "rule-tempcontrol"

#define TAG_LIST \
    TAG(tempStatPV, 'I', DT_REAL64, "tstat.PV.degC")    \
    TAG(tempStatSP, 'I', DT_REAL64, "tstat.SP.degC")    \
    TAG(timer,      'I', DT_UINT32, "timer.4sec")       \
    TAG(boilerPower,'O', DT_REAL64, "outputPower.W")    \
    TAG(KPtag,      'I', DT_REAL64, "PID.KP")           \
    TAG(KItag,      'I', DT_REAL64, "PID.KI")           \
    TAG(KDtag,      'I', DT_REAL64, "PID.KD")           
    

#define TRIGGER timer
    
#include "ruletoolkit.h"

// ====================================================================================
//  Rule logic
// ====================================================================================

void RuleInit(void)
{
    boilerPower.quality = QUALITY_GOOD;
}


// Just a PID controller. 

const double timer_interval_s = 4;

double prev_err = 0;
double integral = 0;
double derivative = 0;

double outputPowerBias = 0;

void RuleExec(void)
{  
    double KP = KPtag.value.real64;
    double KI = KItag.value.real64;
    double KD = KDtag.value.real64;

    double error = tempStatSP.value.real64  -  tempStatPV.value.real64;
    integral += error * timer_interval_s;
    derivative = (error - prev_err) / timer_interval_s;
    
    double output = KP*error + KI*integral + KD*derivative + outputPowerBias;
    prev_err = error;
    
    if      (output < 1500) output = 0;
    else if (output < 3000) output = 3000;
    else if (output > 24000) output = 24000;
        
    boilerPower.value.real64 = output;
    WriteTag(&boilerPower);
}
