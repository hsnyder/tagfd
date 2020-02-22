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



/*

    ===============================
    Rule Toolkit
    ===============================
    
    Provides a collection of functions that are useful in writing control rules.
    This is a bit of an unorthodox ".h" file, in that it includes function
    definitions as well as declarations. The intention is that every rule will
    consist of a single .c file that includes this header file.
    
    THE FIVE STEPS TO RULE WRITING:
    
    Before including this file, a rule must provide:
     - The RULENAME macro
     - The TAG_LIST macro
     - The TRIGGER macro
    After including this file, the rule must provide:
     - The RuleInit function
     - The RuleExec function
     
    Each of these requirements will be explained in turn, below.
    
    
    RULENAME
    --------
    
    You must define this to be a string (the name of the rule). This string
    will appear in log messages. Example:
    
    #define RULENAME "MyRule"
    
    
    TAG_LIST
    --------
    
    Also before including this file, you must define the TAG_LIST macro. It's
    a bit ugly, but it saves a lot of boilerplate. An example follows:
    
    #define TAG_LIST \
        TAG(tempSP, 'I', DT_REAL64, "thermostat.SP.degC") \
        TAG(tempPV, 'I', DT_REAL64, "thermostat.PV.degC") \
        TAG(timer,  'I', DT_UINT32, "timers.1sec")        \
        TAG(output, 'O', DT_REAL64, "outputPower.kW")
        
    Note that the backslashes are used to escape the end of line character and
    make the macro definition carry on to the next line. It will not work if
    you have a space after the backslash, it must be followed immediately by
    the newline character. 
    
    Anyways, in the above example, once we make that declaration and include
    this file, we will be able to access the indicated tags through the 
    variables tempSP, tempPV, timer, and output, all of which will be declared
    as global tag_t variables. The names of the actual tags are given by the
    strings in the fourth parameter to TAG(). When the rule first starts up,
    it will automatically check the data types of all indicated tags, and if
    there is a mismatch (which would unquestionably represent a bug or mis-
    configuration), the rule aborts (rather than potentially corrupting data).
    
    The 'I' or 'O' indicator specifies whether the tag is intended as an 
    input variable or an output variable. Marking a tag as 'I' means that 
    the global variable will be kept up to date, being set behind the scenes
    whenever it's value changes. Marking a variable as 'O' means that it
    will not. Nothing about this marking actually prevents you from writing
    the tag's value, but if you're going both read and write a given tag, be
    sure you know what you're doing. For this purpose, you can also mark a
    tag 'B' (both), which has the same effect as 'I' but will make the intent
    behind your code clearer. 
    
    
    TRIGGER
    -------
    
    The last thing that needs to be done before including this toolkit is
    defining the TRIGGER macro. Define it to match the name of one of your
    tags from TAG_LIST (the name in the first argument to the TAG macro,
    not the name in the last argument). Example:
    
    #define TRIGGER timer
    
    This will configure the rule such that each time the indicated tag changes,
    the rule will be executed. You must choose an INPUT tag, or the rule will 
    never run. 
    
    
    RuleInit
    --------
    
    After you include this toolkit, you must provide a function matching the
    following delcaration. It can be empty if you wish, but should be used
    to do any initialization that might be required. It will be run once,
    during application startup. 
    
    void RuleInit(void)
    {
        // your initialization logic.
    }
    
    
    RuleExec
    --------
    
    You must also provide a function matching the following declaration.
    Every time the trigger tag changes, this function will be run. 
    
    
    void RuleExec(void)
    {
        // your rule logic 
    }
    
    You can define other functions as you see fit, and call them from
    RuleExec if you want.
    
    
    
   
    
*/

/*  Including this file twice will cause a compiler error (it will whether we 
    do this or not, so we might as well control what the error says).       */
#ifdef REDPINE_RULE_TOOLKIT_H

#error \
Multiple include of ruletoolkit.h: \
Whatever you're doing it's probably wrong. \
Check the rule-writing manual.

#else 
#define REDPINE_RULE_TOOLKIT_H
#endif


#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdbool.h>
#include "tagfd-shared.h"


/*
===============================================================================
    
    TOOLKIT FUNCTIONS
        
    Below are declarations and documentation for all the convenience methods
    that this toolkit provides you as a rule-writer. 

===============================================================================
*/

#define MASTERKILLSWITCH_TAGNAME "master.on"

//  Writes the provided tag to tagfd, and updates it's timestamp to now.
void WriteTag(tag_t * tag);

/*  Writes a message to the logs. Please only log abnormal things. 
    It works like printf, but you must provide a priority.
    The priority can be any of the syslog priority macros (see man 3 syslog)
    but it's best to stick to LOG_ERR, LOG_WARNING, or LOG_NOTICE.    */
void Log(int priority, const char * format, ...);

/*  Logs the message (in the same manner as Log() above), and then exits.
    Use when you need to crash.         */
void LogAbort(int priority, const char * format, ...);


/*
===============================================================================
    
    IMPLEMENTATION DETAILS
        
    If you're just trying to write rules, what you need is contained above,
    proceed only if interested. 

===============================================================================
*/




#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

// Opens the specified tag. 
// Returns a file descriptor, or dies trying. Uses the syslog. 
int assertOpenTag(const char * name)
{  

    char buf[TAG_NAME_LENGTH + 100];
    memset(buf, 0, (TAG_NAME_LENGTH + 100) * sizeof(char));
    int rc = snprintf(buf, TAG_NAME_LENGTH + 100, "/dev/tagfd/%s", name);
    if(rc < 1 || rc == TAG_NAME_LENGTH + 100)
        LogAbort(LOG_ERR,"Encountered a tag name that was too long.");

    int fd = open (buf, O_CLOEXEC | O_RDWR);
    if(fd < 0)
        LogAbort(LOG_ERR, "Couldn't open %s: %s",buf,strerror(errno));
        
    return fd;
}

// Reads an tag from an open file descriptor, or dies trying.
tag_t assertReadTag(int fd)
{
    tag_t tag;
    
    if(read(fd, &tag, sizeof(tag_t)) != sizeof(tag_t))
        LogAbort(LOG_ERR, "Read() call to tag failed: %s", strerror(errno));
  
    return tag;
}

// Writes an tag to an open file descriptor, or dies trying.
void assertWriteTag(int fd, tag_t tag)
{
    if(write(fd, &tag, sizeof(tag_t)) != sizeof(tag_t))
        LogAbort(LOG_ERR, "Write() call to tag failed: %s", strerror(errno));
}

// Writes a tag, or returns false. 
bool tryWriteTag(int fd, tag_t tag)
{
    if(write(fd, &tag, sizeof(tag_t)) != sizeof(tag_t))
        return false;
    return true;
}

// Checks that the given tag is of the indicated data type, 
// aborts program if it is not. 
void assertTagDataType(tag_t tag, uint8_t dtype)
{
    if(tag.dtype != dtype)
        LogAbort(LOG_ERR, "Tag had unexpected data type");
    
}

// Updates the specified tag's timestamp to the current time. 
// Note that the big numbers involved here get squirrelly on 32 bit
// platforms, so it is recommended that you use this function rather
// than manually setting the timestamp, otherwise you might accidentally
// cause overflows. 
void setTagTimestamp(tag_t * tag)
{
    struct timespec spec;

    clock_gettime(CLOCK_REALTIME, &spec);
	
	tag->timestamp = spec.tv_sec;
	tag->timestamp *= 1000;
	tag->timestamp += spec.tv_nsec / 1000000;
}


void Log(int priority, const char * format, ...)
{    
    va_list arg;
    va_start(arg,format);
    #ifdef NO_SYSLOG
    vprintf(format, arg);
    printf("\n");
    #else
    vsyslog(priority, format, arg);
    #endif
    va_end(arg);
}

void LogAbort(int priority, const char * format, ...)
{
    va_list arg;
    va_start(arg,format);
    #ifdef NO_SYSLOG
    vprintf(format, arg);
    printf("\n");
    #else
    vsyslog(priority, format, arg);
    #endif
    va_end(arg);
    
    exit(EXIT_FAILURE);
}

// --------------------------------
//  Rules only (compiled out of the engine).  
// --------------------------------
#ifndef __I_AM_NOT_A_RULE__

// Check for required macros. 
#ifndef RULENAME
    #error You must define RULENAME before including 'ruletoolkit.h'
#endif

#ifndef TAG_LIST
    #error You must define TAG_LIST before including 'ruletoolkit.h'
#endif

#ifndef TRIGGER
    #error You must define TRIGGER before including 'ruletoolkit.h'
#endif

// --- X-Macro stuff. -----------------

// Validity check #1: is the 'I'/'O'/'B' thing valid?
#define TAG(n, io, dt, str) \
    _Static_assert(io == 'I' || io == 'O' || io == 'B', "TAG_LIST problem: Invalid I/O indicator on tag " #n );
TAG_LIST
#undef TAG

// Validity check #2: is the data type valid?
#define TAG(n, io, dt, str) \
    _Static_assert(dt == DT_INT8     || \
                   dt == DT_UINT8    || \
                   dt == DT_INT16    || \
                   dt == DT_UINT16   || \
                   dt == DT_INT32    || \
                   dt == DT_UINT32   || \
                   dt == DT_INT64    || \
                   dt == DT_UINT64   || \
                   dt == DT_REAL32   || \
                   dt == DT_REAL64   || \
                   dt == DT_TIMESTAMP|| \
                   dt == DT_STRING,     \
                   "TAG_LIST problem: Invalid data type on tag " #n );
TAG_LIST
#undef TAG

// Define all the global tag_t variables. 
tag_t _toolkit_masterKillswitch;

#define TAG(n, io, dt, str) static tag_t n;
TAG_LIST
#undef TAG

// Define an array of pointers to those globals.
#define TAG(n, io, dt, str) &n, 
static 
tag_t* _toolkit_tagPtrs[] = {
    &_toolkit_masterKillswitch,
    TAG_LIST
};
#undef TAG

// Define an array storing the I/O indicators
#define TAG(n, io, dt, str) io,
static const 
char _toolkit_tagModes[] = {
    'I',
    TAG_LIST
};
#undef TAG

// Define an array storing the expected data types
#define TAG(n, io, dt, str) dt,
static const 
char _toolkit_tagDTypes[] = {
    DT_UINT8,
    TAG_LIST
};
#undef TAG

// Define an array storing the actual string tag names
#define TAG(n, io, dt, str) str,
static const 
char * _toolkit_tagNames[] = {
    MASTERKILLSWITCH_TAGNAME,
    TAG_LIST
};
#undef TAG



// --- Other globals such -----------------

// Record the number of tags in use
#define _TOOLKIT_NUM_TAGS (sizeof(_toolkit_tagPtrs)/sizeof(tag_t*))

// Create a list of pollfds
static
struct pollfd _toolkit_pollfds[_TOOLKIT_NUM_TAGS] ;



// --- Rule functions and boilerplate code. -----------------

void WriteTag(tag_t * tag)
{
    for(int i = 0; i < _TOOLKIT_NUM_TAGS; i++)
    {
        if(_toolkit_tagPtrs[i] == tag)
        {
            setTagTimestamp(tag);
            assertWriteTag(_toolkit_pollfds[i].fd, *tag);
            return;
        }
    }
    LogAbort(LOG_ERR, "Invalid tag pointer passed to WriteTag()");
}

void RuleInit(void);
void RuleExec(void);

int main(int argc, char ** argv)
{
    openlog(RULENAME, LOG_NDELAY, LOG_USER);
    
    memset(_toolkit_pollfds, 0, _TOOLKIT_NUM_TAGS * sizeof(struct pollfd));
    
    // loop over tags the rule writer provided, and do our setup. 
    for(int i = 0; i < _TOOLKIT_NUM_TAGS; i++)
    {
        // open the tag and perform initial read.
        _toolkit_pollfds[i].fd = assertOpenTag(_toolkit_tagNames[i]);
        *(_toolkit_tagPtrs[i]) = assertReadTag(_toolkit_pollfds[i].fd);
        
        // check the datatype matches expectation
        assertTagDataType(*(_toolkit_tagPtrs[i]), _toolkit_tagDTypes[i]);
        
        // set the poll events based on I/O setting. 
        if(_toolkit_tagModes[i] == 'I' || _toolkit_tagModes[i] == 'B')
            _toolkit_pollfds[i].events = POLLIN;
    }
    
    // Make sure the trigger they provided is actually in the list. 
    for(int i = 0; i < _TOOLKIT_NUM_TAGS; i++)
        if(_toolkit_tagPtrs[i] == &TRIGGER) 
            goto allgood;
    
    LogAbort(LOG_ERR, "Invalid TRIGGER was detected.");
    
    allgood:
    
    // CALL THEIR INITIALIZER
    RuleInit();
    
    // MAIN LOOP 
    while(_toolkit_masterKillswitch.value.u8)
    {
        // poll
        if (0 > poll(_toolkit_pollfds,_TOOLKIT_NUM_TAGS,-1))
            LogAbort(LOG_ERR, "Poll failed: %s", strerror(errno));
        
        // check all tags to see what happened.
        for(int i = 0; i < _TOOLKIT_NUM_TAGS; i++)
        {
            
            // did anything happen on the i-th tag?
            if(_toolkit_pollfds[i].revents)
            {
                // Is the pending event "normal read is now possible?"
                if(_toolkit_pollfds[i].revents == POLLIN || _toolkit_pollfds[i].revents == (POLLIN | POLLRDNORM))
                {
                    // Read the tag. 
                    *(_toolkit_tagPtrs[i]) = assertReadTag(_toolkit_pollfds[i].fd);
                    
                    
                    // Check if this is the trigger, possibly execute the rule. 
                    if(_toolkit_tagPtrs[i] == &TRIGGER)
                        RuleExec();
                }
                // Probably revise this at some point... but for now any other event will log an error and abort.
                else 
                {
                    LogAbort(LOG_ERR,"Poll: unexpected revents (%d) for tag %s", _toolkit_pollfds[i].revents, _toolkit_tagNames[i]);
                }
            }
        }
    }
    
    // Close fds (though currently I don't know how you'd ever get here)...
    for(int i = 0; i < _TOOLKIT_NUM_TAGS; i++)
    {
        close(_toolkit_pollfds[i].fd);
    }
    
    exit(EXIT_SUCCESS);
}


#endif 
