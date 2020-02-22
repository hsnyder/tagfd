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

#ifndef REDPINEFD_SHARED_H
#define REDPINEFD_SHARED_H
/* 

    This file is shared between kernel and userspace code.
    That is why it doesn't include <stdint.h> (or anything 
    else, for that matter). Include <stdint.h> in your
    userspace code before including this file. 
    
*/


// Data type constants
#define DT_INVALID 0

#define DT_INT8 2
#define DT_UINT8 3
#define DT_INT16 4
#define DT_UINT16 5
#define DT_INT32 6
#define DT_UINT32 7
#define DT_INT64 8
#define DT_UINT64 9
#define DT_REAL32 10
#define DT_REAL64 11
#define DT_TIMESTAMP 12
#define DT_STRING 13

// Upper two bits of the quality are used to indicate 
// GOOD, UNCERTAIN, BAD, or DISCONNECTED.
// Lower 14 bits reserved for "vendor" use. 
#define QUALITY_MASK              0xC000
#define QUALITY_VENDOR_MASK       0x3FFF

#define QUALITY_UNCERTAIN         0x0000
#define QUALITY_DISCONNECTED      0x8000
#define QUALITY_BAD               0x4000
#define QUALITY_GOOD              0xC000


#define TAG_NAME_LENGTH 256
#define TAG_STRING_VALUE_LENGTH 16

typedef uint64_t timestamp_t;

typedef union tagvalue_u
{
	int8_t       i8;
	uint8_t      u8;
	int16_t      i16;
	uint16_t     u16;
	int32_t      i32;
	uint32_t     u32;
	int64_t      i64;
	uint64_t     u64;
	float        real32;
	double       real64;
	timestamp_t  timestamp;
	char         string[TAG_STRING_VALUE_LENGTH];
} tagvalue_t;

// This structure is what gets actually exchanged in tagfd
typedef struct tag_s 
{
	tagvalue_t    value;
	timestamp_t   timestamp; 	
	uint16_t      quality; 
	uint8_t       dtype;
} tag_t;

// Used by tfdconfig and the tagfd.master device
// (for creation of tags).
struct tag_config
{
	uint8_t  action;
	uint8_t  dtype;
	char     name[TAG_NAME_LENGTH];
};

#endif
