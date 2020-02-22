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

    Simple rule to simulate changing outdoor temperature. 
    Acts on simulated.outsideTemp.degreesC

    Harris M. Snyder

*/


// ====================================================================================
//  Macro definitions required by 'ruletoolkit.h'
// ====================================================================================

#define RULENAME "rule-tempsimulator"

#define TAG_LIST \
    TAG(otemp, 'O', DT_REAL64, "sim.outsideTemp.degC") \
    TAG(timer, 'I', DT_UINT32, "timer.1sec")

#define TRIGGER timer
    
#include "ruletoolkit.h"


// ====================================================================================
//  Rule logic
// ====================================================================================

const double omega = 2.0*M_PI/3600.0;

void RuleInit(void)
{
    otemp.quality = QUALITY_GOOD;
}

void RuleExec(void)
{
    static uint8_t ticks = 0;
    
    otemp.value.real64 = 17.0 * cos(ticks * omega) ;
    
    ticks++;
    if(ticks > 3600) ticks = 0;
    
    WriteTag(&otemp);
}

