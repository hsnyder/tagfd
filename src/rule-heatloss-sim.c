/*	Copyright (C) 2018, 2020 Harris M. Snyder

	This file is part of Tagfd.

	Tagfd is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Tagfd is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Tagfd.  If not, see <https://www.gnu.org/licenses/>.
*/

/*

    Simple rule that simulates house heat loss to the outside. 

    Harris M. Snyder

*/

// ====================================================================================
//  Macro definitions required by 'ruletoolkit.h'
// ====================================================================================

#define RULENAME "rule-heatloss-sim"

#define TAG_LIST \
    TAG(tempStatPV,  'O', DT_REAL64, "tstat.PV.degC")           \
    TAG(tempOutside, 'I', DT_REAL64, "sim.outsideTemp.degC")    \
    TAG(boilerPower, 'I', DT_REAL64, "outputPower.W")           \
    TAG(hlcoeff,     'I', DT_REAL64, "coeff.heatloss.W_degCm2") \
    TAG(housesize,   'I', DT_INT32,  "houseSize.m2")            \
    TAG(timer,       'I', DT_UINT32, "timer.1sec")

#define TRIGGER timer
    
#include "ruletoolkit.h"


// ====================================================================================
//  Rule logic
// ====================================================================================

void RuleInit(void)
{
    tempStatPV.quality = QUALITY_GOOD;
}

void RuleExec(void)
{  
    // do our calculation.
    double Toutside = tempOutside.value.real64;
    double Tinside = tempStatPV.value.real64;
    
    double Qout = housesize.value.i32 * hlcoeff.value.real64 * (Tinside - Toutside);
    double Qin = boilerPower.value.real64;
    double Tchg = (Qin-Qout) / (housesize.value.i32 * hlcoeff.value.real64); // times one second..
    
    tempStatPV.value.real64 += Tchg;

    WriteTag(&tempStatPV);
}
