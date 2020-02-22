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
	along with tagfd.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef REDPINEFD_TOOLKIT_H
#define REDPINEFD_TOOLKIT_H
/* 

    This file (and it's associated .c file, naturally) provide functions
    that are needed by virtually every program interacting with tagfd.
    It is separate from the rule toolkit becasue it has nothing to do with 
    writing rules or the control engine.

*/


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdint.h>
#include "tagfd-shared.h"

/*   
    This function walks through a specified directory, and calls a callback
    for each entry in the directory. A filter can be provided (or can be NULL),
    and if it is provided, only entries whose names start with the filter
    string will result in callback calls.
    
    This function also stats each file and provides the stat information as
    an argument to the callback. The user can provide an error callback if they
    wish, which will be called if stat fails on a file for any reason. The 
    stat error callback is optional. If NULL is passed, stat errors will be
    silently ignored. 
    
    The callbackParam arg will be passed as the first parameter to the callbacks
    when they are invoked. 
    
    The errMsg arg will be set, if this function fails, to a string that
    indicates what system call failed. 
    
    the entryCallback argument is a function pointer to the callback to be 
    invoked once for each entry in the directory (matching the filter, if
    one was provided). For convenience, both the name of the entry, and the full
    path (name including the directory) are provided to the callback. 
    
    If either callback returns anything but zero, the directory walk is aborted,
    and 1 is returned. 
    
    This function returns 0 on success, -1 on internal error, or 1 if a callback
    return value indicated an abort. On -1, errno will be set. 
*/
int walkDirectory(const char * directory, const char * filter,
                    void* callbackParam, const char ** errMsg,
                    int (*entryCallback) (void* param, const char * name, const char * path, struct stat sb),
                    int (*statErrorCallback) (void* param, const char * name, const char * path));
           
           
// ============================================================================
//  Tag-to-text functions
// ============================================================================



/*  These functions produce string representations of various parts of a tag,
    and do so in a human-readable format (HR). Example: data type would be
    printed as "uint8" rather than the underlying integer value that represents
    an 8 bit unsigned int. Note that the strings returned are allocated into an
    internal buffer that is re-used for each call. So if you need to persist
    the value returned, use strdup or copy it into a buffer of your own. The
    underlying buffers are thread-local. 
    
    Where applicable, "abbrev" makes the output shorter. 
    */
const char * tag_dtype_toStrHR(const tag_t *e );
const char * tag_value_toStrHR(const tag_t *e );
const char * tag_timestamp_toStrHR(const tag_t *e );
const char * tag_quality_toStrHR(const tag_t *e, bool abbrev);


/*  These functions are for the same purpose as the above block, except that 
    they don't do convenience conversions to a human-readable representation,
    all aspects of the tag are output as numeric values. These methods share
    buffers with the above set of functions. Prefer these functions if another
    machine is going to be reading the output, prefer the above for display to
    a human. Some functions return empty strings for invalid arguments. */
const char * tag_dtype_toStr(const tag_t *e );
const char * tag_value_toStr(const tag_t *e );
const char * tag_timestamp_toStr(const tag_t *e );
const char * tag_quality_toStr(const tag_t *e);
/*  Produces a string representation of a tag's quality, timestamp, and value*/
const char * tag_toStr_partial(const tag_t *e);


/*  These functions are the inverse functions for the _toStrHR family. */
uint8_t tag_dtype_fromStrHR(const char * str); // returns DT_INVALID on failure


/*  Inverse of tag_toStr_partial. Returns false on failure.*/
bool tag_fromStr_partial(const char * encoded, uint8_t dtype, tag_t * output);



#endif
