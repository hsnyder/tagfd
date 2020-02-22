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

#include "tagfd-toolkit.h"
// see the .h file for comments. 

#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>


#include <float.h>
#define STR(x) #x
#define STR2(x) STR(x)
#define WR32 STR2(FLT_DECIMAL_DIG)
#define WR64 STR2(DBL_DECIMAL_DIG)

static _Thread_local const char * tfdtoolErr_opendir = "opendir()";
static _Thread_local const char * tfdtoolErr_snprintf = "snprintf()";
static _Thread_local const char * tfdtoolErr_ncp = "[no callback provided]";

int walkDirectory(const char * directory, const char * filter,
                    void* callbackParam, const char ** errMsg,
                    int (*entryCallback) (void* param, const char * name, const char * path, struct stat sb),
                    int (*statErrorCallback) (void* param, const char * name, const char * path))
{
    DIR * dir = opendir(directory); 
	if(!dir)
    {
        *errMsg = tfdtoolErr_opendir;
        return -1;
    }
    
    if(!entryCallback)
    {
        *errMsg = tfdtoolErr_ncp;
        errno = EINVAL;
        return -1;
    }
    
    // Walk the directory
    
    struct dirent * entry;
    int filtlen = filter ? strlen(filter) : 0;
	while((entry = readdir(dir)))
	{
        // check the entry against the filter.
        if(filter && 0 != strncmp(filter, entry->d_name, filtlen))
        {
            continue;
        }
            
        
        // assemble the full path of the entry. 
		char pathbuf [PATH_MAX+100];
		int snprc = snprintf(pathbuf, PATH_MAX+100, "%s/%s", directory, entry->d_name);
        if(snprc < 0 || snprc == PATH_MAX+100)
        {
            *errMsg = tfdtoolErr_snprintf;
            return -1;
        }
        
        // stat and check for error, fire callback if appropriate. 
		struct stat sb;
        if(stat(pathbuf, &sb))
        {
            if(statErrorCallback)
            {
                if(statErrorCallback(callbackParam, entry->d_name, pathbuf))
                {
                    closedir(dir);
                    return 1;
                }
            }
            continue;
        }
        
        // invoke the user callback. 
        if(entryCallback(callbackParam, entry->d_name, pathbuf, sb))
        {
            closedir(dir);
            return 1;
        }
        
	}
    
    closedir(dir);
    return 0;
}














// ====================================================================================
//  Tag-to-text functions
// ====================================================================================



static _Thread_local const char * INT8 = "int8";
static _Thread_local const char * UINT8 = "uint8";
static _Thread_local const char * INT16 = "int16";
static _Thread_local const char * UINT16 = "uint16";
static _Thread_local const char * INT32 = "int32";
static _Thread_local const char * UINT32 = "uint32";
static _Thread_local const char * INT64 = "int64";
static _Thread_local const char * UINT64 = "uint64";
static _Thread_local const char * REAL32 = "real32";
static _Thread_local const char * REAL64 = "real64";
static _Thread_local const char * TIMESTAMP = "timestamp";
static _Thread_local const char * STRING = "string";

const char * tag_dtype_toStrHR(const tag_t *e )
{
	switch(e->dtype)
	{
		case DT_INT8:
			return INT8;
			
		case DT_UINT8:
			return UINT8;
			
		case DT_INT16:
			return INT16;
			
		case DT_UINT16:
			return UINT16;
			
		case DT_INT32:
			return INT32;
			
		case DT_UINT32:
			return UINT32;
			
		case DT_INT64:
			return INT64;
			
		case DT_UINT64:
			return UINT64;
			
		case DT_REAL32:
			return REAL32;
			
		case DT_REAL64:
			return REAL64;
			
		case DT_TIMESTAMP:
			return TIMESTAMP;
			
		case DT_STRING:
			return STRING;
			
			
	}
	return NULL;
}


#define BUFSZ 128
static _Thread_local char evstr[BUFSZ];
static _Thread_local char etstr[BUFSZ];
static _Thread_local char eqstr[BUFSZ];

void prts(timestamp_t ts, char * buf, size_t buflen)
{
	char buff[20];
	uint64_t intermediate = ts / 1000;
	time_t t = intermediate;
	strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&t));
	uint64_t ms_ = ts % 1000;
	int ms = ms_;
	snprintf(buf,buflen,"%s.%03d", buff, ms);
}

const char * tag_value_toStrHR(const tag_t *e )
{
	memset(evstr,0,BUFSZ);
	switch(e->dtype)
	{
		case DT_INT8:
			sprintf(evstr,"%"PRId8, e->value.i8);
			break;
		case DT_UINT8:
			sprintf(evstr,"%"PRIu8, e->value.u8);
			break;
		case DT_INT16:
			sprintf(evstr,"%"PRId16, e->value.i16);
			break;
		case DT_UINT16:
			sprintf(evstr,"%"PRIu16, e->value.u16);
			break;
		case DT_INT32:
			sprintf(evstr,"%"PRId32, e->value.i32);
			break;
		case DT_UINT32:
			sprintf(evstr,"%"PRIu32, e->value.u32);
			break;
		case DT_INT64:
			sprintf(evstr,"%"PRId64, e->value.i64);
			break;
		case DT_UINT64:
			sprintf(evstr,"%"PRIu64, e->value.u64);
			break;
		case DT_REAL32:
			sprintf(evstr,"%f", e->value.real32);
			break;
		case DT_REAL64:
			sprintf(evstr,"%f", e->value.real64);
			break;
		case DT_TIMESTAMP:
			prts(e->value.timestamp, evstr, BUFSZ);
			break;
		case DT_STRING:
			memcpy(evstr, e->value.string, TAG_STRING_VALUE_LENGTH);
			break;
	}
	return evstr;
}
const char * tag_timestamp_toStrHR(const tag_t *e )
{
	memset(etstr,0,BUFSZ);
	prts(e->timestamp, etstr, BUFSZ);
	return etstr;
}
const char * tag_quality_toStrHR(const tag_t *e, bool abbrev)
{
	memset(eqstr,0,BUFSZ);
	switch(e->quality & QUALITY_MASK)
	{
		case QUALITY_GOOD:
            if(abbrev)
                sprintf(eqstr, "GD %d", e->quality & QUALITY_VENDOR_MASK);
            else
                sprintf(eqstr, "GOOD (%d)", e->quality & QUALITY_VENDOR_MASK);
			break;
			
		case QUALITY_UNCERTAIN:
            if(abbrev)
                sprintf(eqstr, "UN %d", e->quality & QUALITY_VENDOR_MASK);
            else
                sprintf(eqstr, "UNCERTAIN (%d)", e->quality & QUALITY_VENDOR_MASK);
			break;
		
		case QUALITY_BAD:
            if(abbrev)
                sprintf(eqstr, "BD %d", e->quality & QUALITY_VENDOR_MASK);
            else
                sprintf(eqstr, "BAD (%d)", e->quality & QUALITY_VENDOR_MASK);
			break;
		
		case QUALITY_DISCONNECTED:
            if(abbrev)
                sprintf(eqstr, "DC %d", e->quality & QUALITY_VENDOR_MASK);
            else
                sprintf(eqstr, "DISCONNECTED (%d)", e->quality & QUALITY_VENDOR_MASK);
			break;
	}
	return eqstr;
}



static _Thread_local char edstr[BUFSZ];

const char * tag_dtype_toStr(const tag_t *e )
{
    sprintf(edstr,"%"PRIu8, e->dtype);
    return edstr;
}
const char * tag_value_toStr(const tag_t *e )
{
    memset(evstr,0,BUFSZ);
    
    if     (e->dtype == DT_INT8) sprintf(evstr,"%"PRId8, e->value.i8);
    else if(e->dtype == DT_UINT8) sprintf(evstr,"%"PRIu8, e->value.u8);
    else if(e->dtype == DT_INT16) sprintf(evstr,"%"PRId16, e->value.i16);
    else if(e->dtype == DT_UINT16) sprintf(evstr,"%"PRIu16, e->value.u16);
    else if(e->dtype == DT_INT32) sprintf(evstr,"%"PRId32, e->value.i32);
    else if(e->dtype == DT_UINT32) sprintf(evstr,"%"PRIu32, e->value.u32);
    else if(e->dtype == DT_INT64) sprintf(evstr,"%"PRId64, e->value.i64);
    else if(e->dtype == DT_UINT64) sprintf(evstr,"%"PRIu64, e->value.u64);
    else if(e->dtype == DT_REAL32) sprintf(evstr,"%."WR32"e", e->value.real32);
    else if(e->dtype == DT_REAL64) sprintf(evstr,"%."WR64"le", e->value.real64);
    else if(e->dtype == DT_TIMESTAMP) sprintf(evstr,"%"PRIu64, e->value.timestamp);
    else if(e->dtype == DT_STRING)  memcpy(evstr,e->value.string,TAG_STRING_VALUE_LENGTH);

    return evstr;
}
const char * tag_timestamp_toStr(const tag_t *e )
{
    sprintf(etstr,"%"PRIu64, e->timestamp);
    return etstr;
}
const char * tag_quality_toStr(const tag_t *e)
{
    sprintf(edstr,"%"PRIu16, e->quality);
    return edstr;
}

static _Thread_local char wholeTagBuf[2*BUFSZ];
const char * tag_toStr_partial(const tag_t *e)
{
    memset(wholeTagBuf, 0, 2*BUFSZ);
    sprintf(wholeTagBuf, "%s %s %s", tag_quality_toStr(e), tag_timestamp_toStr(e), tag_value_toStr(e));
    return wholeTagBuf;
}

bool tag_fromStr_partial(const char * encoded, uint8_t dtype, tag_t * output)
{
    int n;
    uint16_t q;
    timestamp_t ts;
    tagvalue_t v;
    switch(dtype)
    {
        case DT_INT8:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNd8, &q, &ts, &v.i8);
			break;
		case DT_UINT8:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNu8, &q, &ts, &v.i8);
			break;
		case DT_INT16:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNd16, &q, &ts, &v.i16);
			break;
		case DT_UINT16:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNu16, &q, &ts, &v.u16);
			break;
		case DT_INT32:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNd32, &q, &ts, &v.i32);
			break;
		case DT_UINT32:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNu32, &q, &ts, &v.u32);
			break;
		case DT_INT64:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNd64, &q, &ts, &v.i64);
			break;
		case DT_UINT64:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNu64, &q, &ts, &v.u64);
			break;
		case DT_REAL32:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %e", &q, &ts, &v.real32);
			break;
		case DT_REAL64:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %le", &q, &ts, &v.real64);
			break;
		case DT_TIMESTAMP:
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %"SCNu64, &q, &ts, &v.timestamp);
			break;
		case DT_STRING:;
            int pos;
			n = sscanf(encoded, "%"SCNu16 " %"SCNu64 " %n", &q, &ts, &pos);
            memset(v.string,0,TAG_STRING_VALUE_LENGTH);
			strncpy(v.string, encoded+pos, TAG_STRING_VALUE_LENGTH);
			break;
        
        default:
            return false;
    }
    
    if(!(n == 3 || (n==2 && dtype==DT_STRING))) return false;
    
    output->quality = q;
    output->timestamp = ts;
    output->value = v;
    
    return true;
}



uint8_t tag_dtype_fromStrHR(const char * str)
{
    if     (!strcmp(str, "int8")) return DT_INT8 ;
    else if(!strcmp(str, "uint8")) return DT_UINT8; 
    else if(!strcmp(str, "int16")) return DT_INT16 ;
    else if(!strcmp(str, "uint16")) return DT_UINT16;
    else if(!strcmp(str, "int32")) return DT_INT32 ;
    else if(!strcmp(str, "uint32")) return DT_UINT32; 
    else if(!strcmp(str, "int64")) return DT_INT64 ;
    else if(!strcmp(str, "uint64")) return DT_UINT64; 
    else if(!strcmp(str, "real32")) return DT_REAL32 ;
    else if(!strcmp(str, "real64")) return DT_REAL64 ;
    else if(!strcmp(str, "timestamp")) return DT_TIMESTAMP; 
    else if(!strcmp(str, "string")) return DT_STRING ;
    else return DT_INVALID;
}
