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

	controlengined: A daemon to launch and monitor process control rules. 
    
    This program forks itself into a daemon (background process), and then
    launches and monitors all control rules. It also checks the list of 
    tagfd tags on the local machine and finds those whose names match
    "timer.[x]sec" where [x] is a positive integer. It expects these tags
    to be of an unsigned integer type (DT_UINT8, DT_UINT16, DT_UINT32, or
    DT_UINT64). It will automatically increment those tags at the specified
    interval. 
	
	Harris M. Snyder, 2018
	
    

    TODO: re-start dead rules.
    TODO: implement a system for enabling/disabling rules. 

*/


#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/timerfd.h>
#include <dirent.h>
#include <unistd.h>
#include <regex.h>

#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

// Defining this macro suppresses some of the stuff in the rule toolkit that 
// would break this program. 
#define __I_AM_NOT_A_RULE__ 1


#ifdef NO_DAEMON // can be defined for debugging. 
#define NO_SYSLOG
#endif

#include "ruletoolkit.h"

#include "tagfd-toolkit.h"

// Lock file for 
#define LOCKFILE "/var/run/controlengined/controlengined.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)


// ============================================================================
//  Logging functions 
// ============================================================================

// Prints to standard output and then exits cleanly.
void 
PrintAbort (const char * fmt, ...)
{
    va_list arg;
    va_start(arg,fmt);
    vprintf(fmt, arg);
    printf("\n");
    va_end(arg);
    exit(EXIT_FAILURE);
}


// ============================================================================
//  For timers
// ============================================================================


// Creates a timerfd timer (armed immediately) or dies trying. 
int assertSetupTimerFD(int intervalSeconds)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK  );
    if(tfd < 0) 
        LogAbort(LOG_ERR, "Couldn't create a timerfd.");
    
    
    struct itimerspec its;
    its.it_interval.tv_sec = intervalSeconds;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = intervalSeconds;
    its.it_value.tv_nsec = 0;
    if(timerfd_settime(tfd, 0, &its, NULL))
        LogAbort(LOG_ERR, "Couldn't set up timerfd.");
    
    return tfd;
}


void throwawayReadTimerFD(int fd)
{
    uint64_t exp = 0;
    int r = read(fd, &exp, sizeof(uint64_t));
    if (r == -1 && !(errno == EAGAIN || errno == EWOULDBLOCK))
        LogAbort(LOG_ERR, "Failed to read timerfd: %s", strerror(errno));
        
}

void incrementTimerTag(tag_t * tag)
{
    switch(tag->dtype)
    {
        case DT_UINT8:
            tag->value.u8++;
            break;
        case DT_UINT16:
            tag->value.u16++;
            break;
        case DT_UINT32:
            tag->value.u32++;
            break;
        case DT_UINT64:
            tag->value.u64++;
            break;
    }
}




// ============================================================================
//  Daemon functions - stuff involved in turning the program into a daemon. 
// ============================================================================


// Source: APUE, 3rd edition (Stevens & Rago) - modified. 
void 
daemonize (const char *name)
{
    // Clear file creation mask.
    umask(0);
    
    // Get maximum number of file descriptors.
    struct rlimit rl;
    if(getrlimit(RLIMIT_NOFILE, &rl) < 0)
        PrintAbort("%s: can't get file limit: %s", name, strerror(errno));
    
    // Become session leader to lose controlling TTY
    pid_t pid;
    if((pid = fork()) < 0)
        PrintAbort("%s: can't fork: %s", name, strerror(errno));
       
    else if (pid != 0) // Parent process exits
        exit(EXIT_SUCCESS);
        
    setsid(); 
    
    // Ensure future opens won't allocate a controlling TTY
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGHUP, &sa, NULL) < 0)
        PrintAbort("%s: can't ignore SIGHUP: %s", name, strerror(errno));
    if((pid = fork()) < 0)
        PrintAbort("%s: can't fork: %s", name, strerror(errno));
    else if (pid != 0) // Parent exits
        exit(EXIT_SUCCESS);
    
    // Change working directory to root.
    if(chdir("/") < 0)
        PrintAbort("%s: can't change directory to / : %s", name, strerror(errno));
    
    // Close all open file decscriptors.
    if(rl.rlim_max == RLIM_INFINITY)
        rl.rlim_max = 1024;
    for(int i = 0; i < rl.rlim_max; i++)
        close(i);
    
    // Attach descriptors 0, 1, and 2 to /dev/null.
    int fd0 = open("/dev/null", O_RDWR);
    int fd1 = dup(0);
    int fd2 = dup(0);
    
    // Initialize the log 
    openlog(name, LOG_CONS, LOG_DAEMON);
    if( fd0 != 0 || fd1 != 1 || fd2 != 2)
        LogAbort(LOG_ERR, "Unexpected file descriptors %d %d %d", fd0, fd1, fd2);
   
}


// Source: APUE, 3rd edition (Stevens & Rago) - modified. 
int
lockfile (int fd)
{
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_start = 0;
    fl.l_whence = SEEK_SET;
    fl.l_len = 0;
    return fcntl(fd, F_SETLK, &fl);
}


// Source: APUE, 3rd edition (Stevens & Rago) - modified. 
void
single_instance(void)
{
    // ----------------
	// Make sure only one instance of this program is running. 
	// ----------------
	
	int fd = open(LOCKFILE, O_RDWR|O_CREAT, LOCKMODE);
    if(fd < 0)
        LogAbort(LOG_ERR, "Can't open %s: %s (you may need to create the aforementioned "
                          "directory and ensure this process has permission to write to it).",
                          LOCKFILE, strerror(errno));
    
    if(lockfile(fd) <0)
    {
        if(errno == EACCES || errno == EAGAIN)
        {
            LogAbort(LOG_ERR,"Locking %s failed: already running.", LOCKFILE);
        }
        LogAbort(LOG_ERR, "Locking %s failed: %s", LOCKFILE, strerror(errno));
    }
    
    ftruncate(fd, 0);
    char buf [32];
    sprintf(buf, "%ld", (long)getpid());
    write(fd, buf, strlen(buf)+1);
	
}



// ============================================================================
//  Vector data type 
// ============================================================================

// Importing a vector data structure. 
// Uses a simple macro-based template system for C, see the file for details.
// We want a vector that can accept strings (char pointers). 
#define TYPE char*
#define PREFIX str_
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"


// We also want a vector type that takes ints.
#define TYPE int
#define PREFIX int_
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"

// And one more that takes struct pollfds
#define TYPE struct pollfd
#define PREFIX pfd_
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"

// Aaaand one more that takes tags
#define TYPE tag_t
#define PREFIX tag_
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"



// ============================================================================
//  globals used by main and other functions below, and their cleanup function
// ============================================================================

// bunch o' vectors
struct str_vec rulePathVec;     // Paths of all executable rules
struct str_vec timerNameVec;    // Paths of all timer tag device files
struct int_vec timerSecondsVec; // Intervals (s) of all timer tags
struct int_vec tagfds;          // File descriptors of all open tags
struct pfd_vec pollfds;         // List of file descriptors to poll
struct tag_vec tags;            // Actual tag objects

void cleanup(void)
{
    int i;
    
    // Our task basically amounts to cleaning up all those global vectors. 
    
    for(i = 0; i < str_vec_size(&rulePathVec); i++)
    {
        free(str_vec_ptr(&rulePathVec)[i]);
    }
    str_vec_destroy(&rulePathVec);
    
    for(i = 0; i < str_vec_size(&timerNameVec); i++)
    {
        free(str_vec_ptr(&timerNameVec)[i]);
    }
    str_vec_destroy(&timerNameVec);
    
    for(i = 0; i < pfd_vec_size(&pollfds); i++)
    {
        close(pfd_vec_ptr(&pollfds)[i].fd);
    }
    pfd_vec_destroy(&pollfds);
    
    for(i = 0; i < int_vec_size(&tagfds); i++)
    {
        close(int_vec_ptr(&tagfds)[i]);
    }
    int_vec_destroy(&tagfds);
    
    // nothing specific to clean up here.
    int_vec_destroy(&timerSecondsVec);
    tag_vec_destroy(&tags);
}


// ============================================================================
//  main() and it's helper functions
// ============================================================================


// directory walking callback for finding rules
int findRules(void* param, const char * name, const char * path, struct stat sb)
{
    struct str_vec * pathvec = param;
    
    if(!str_vec_append(pathvec, strdup(path)))
        PrintAbort("Vector append: %s", strerror(errno));
                
    return 0;
}

// context struct for following callback
struct findTagsCtx
{
    bool * foundMasterKillswitch;
    regex_t * rgx;
    struct str_vec * timerNameV;
    struct int_vec * timerSecondsV;
};

// directory walking callback for finding tagfd tags
int findTags(void* param, const char * name, const char * path, struct stat sb)
{
    struct findTagsCtx * ctx = param;
    
    if(!S_ISCHR(sb.st_mode)) return 0;
    
    if( 0 == strcmp(name, MASTERKILLSWITCH_TAGNAME))
        *ctx->foundMasterKillswitch = true;
    
    else 
    if(0==regexec(ctx->rgx, name, 0, NULL,0))
    {
        if(!str_vec_append(ctx->timerNameV, strdup(name)))
            PrintAbort("Vector append: %s ", strerror(errno) );
        
        int n = 0;
        if(1 != sscanf(name, "timer.%dsec", &n))
            PrintAbort("Regex matched but sscanf failed?");
        
        if( n < 1 )
            PrintAbort("Detected a timer tag with an invalid interval.");
        
        if(!int_vec_append(ctx->timerSecondsV, n))
            PrintAbort("Vector append: %s ", strerror(errno) );
    }
    return 0;
}

// directory walking callback for stat failure 
int cantStat(void* param, const char * name, const char * path)
{
    Log(LOG_WARNING, "Can't stat %s", path);
    return 0;
}

int main(int argc, char ** argv)
{
    
    // Initialize all of our vectors. 
    str_vec_init(&rulePathVec);
    str_vec_init(&timerNameVec);
    int_vec_init(&timerSecondsVec);
    pfd_vec_init(&pollfds);
    int_vec_init(&tagfds);
    tag_vec_init(&tags);
    
    // clean up all of our stuff on exit. 
    atexit(cleanup);
    
    
    if(argc < 2) 
        PrintAbort("Currently you must supply exactly one command line argument: "
                   "the absolute path the the folder where I can find the rules.");
     
    const char * rulesPath = argv[1];
    
    
    /*
    
        1) Scan the user-provided directory to find rule executables
        2) Scan the /dev/tagfd directory to find all tags (looking for timers)
        3) Turn into a daemon
        4) Open file descriptors
            - timer tags (that we write at intervals)
            - timerfd instances (that we poll, to know when to write the above)
            - master killswitch
        5) Launch rules
        6) Poll file descriptors (loop) until all children close and master 
           killswitch indicates system shutdown. 
        
        TODO: re-start dead children.
        TODO: implement a system for enabling/disabling rules. 
        
    */
    
    
    
    
    // --- Enumerate all available rules ------------------------
    
    
    // technically we just scan the provided path for executables that start with
    // "rule-".
    const char * err = NULL;
    if(walkDirectory(rulesPath, "rule-", &rulePathVec, &err, findRules, cantStat))
    {
        PrintAbort("%s failure when walking directory %s. errno: %s", err, rulesPath, strerror(errno));
    }
    
    
    
    // --- Find timers in the tag list. ------------------------

    bool foundMasterKillswitch = false;
    
    // regex for matching.
    regex_t rgx;
    if(regcomp(&rgx, "^timer\\.([0-9]+)sec$", REG_EXTENDED | REG_NOSUB))
        PrintAbort("Failed to compile regular expression.");
    
    struct findTagsCtx ftc = {
        .foundMasterKillswitch = & foundMasterKillswitch,
        .rgx = &rgx,
        .timerNameV = &timerNameVec,
        .timerSecondsV = &timerSecondsVec
    };
    
    if(walkDirectory("/dev/tagfd", NULL, &ftc, &err, findTags, cantStat))
    {
        PrintAbort("%s failure when walking directory /dev/tagfd. errno: %s", err, strerror(errno));
    }
    
    if(!foundMasterKillswitch)
        PrintAbort("Master killswitch tag '%s' is missing", MASTERKILLSWITCH_TAGNAME);
    
    
    
    // --- Make Daemon ------------------------
    #ifndef NO_DAEMON // can be defined for debugging. 
    
    daemonize("Tagfd control engine");
    single_instance();
    // The above functions exit if appropriate, so if we're here, we're the daemon.
    
    #endif
    
    // --- FD lists ------------------------
    
    // We have two lists of file descriptors. One is a list of pollfds that we use
    // for polling (i.e. timers), and the second list is the list of open tag fds.
    // their indices correspond, where applicable.

    // NB: We must do this after daemonizing because daemonize closes all fds 
    
    // Timers
    char ** timerStrArr = str_vec_ptr(&timerNameVec);
    int * timerIntArr   = int_vec_ptr(&timerSecondsVec);
    
    const int NTIMERS = int_vec_size(&timerSecondsVec);
    
    for(int i = 0; i < NTIMERS; i++)
    {
        // For each timer we need:
        //  - A timerfd instance (we store this in a struct pollfd so we can poll it)
        //  - An open file decriptor to the associated tag.
        //  - The actual tag object's intial value. 
        struct pollfd pfd;
        pfd.fd = assertSetupTimerFD(timerIntArr[i]);
        pfd.events = POLLIN;
        pfd.revents = 0;
        
        int tagfd = assertOpenTag(timerStrArr[i]);
        tag_t tagval = assertReadTag(tagfd);
        switch(tagval.dtype)
        {
            case DT_UINT8:
            case DT_UINT16:
            case DT_UINT32:
            case DT_UINT64:
                break;
            default:
                LogAbort(LOG_ERR, "Timer tags must have an unsigned integer data type. ");
        }
        
        tagval.quality = QUALITY_GOOD;
        
        if(!pfd_vec_append(&pollfds, pfd))
            LogAbort(LOG_ERR, "Vector append: %s", strerror(errno));
        
        if(!int_vec_append(&tagfds, tagfd))
            LogAbort(LOG_ERR, "Vector append: %s", strerror(errno));
        
        if(!tag_vec_append(&tags, tagval))
            LogAbort(LOG_ERR, "Vector append: %s", strerror(errno));
            
    }
    
    // master killswitch
    #define MASTERKILLSWITCH_FD_IDX NTIMERS
    // with the timer tags, we were writing them but not polling them.
    // this is the opposite. So we add the TAG fd to the poll list.
    struct pollfd ksw_pfd;
    ksw_pfd.fd = assertOpenTag(MASTERKILLSWITCH_TAGNAME);
    ksw_pfd.revents = 0;
    ksw_pfd.events = POLLIN;
    tag_t ksw_tag = assertReadTag(ksw_pfd.fd);    

    if(ksw_tag.dtype != DT_UINT8)
        LogAbort(LOG_ERR, "Master killswitch tag had unexpected data type (should be UINT8).");
    
    if(!pfd_vec_append(&pollfds, ksw_pfd))
        LogAbort(LOG_ERR, "Vector append: %s", strerror(errno));
    
    if(!tag_vec_append(&tags, ksw_tag))
        LogAbort(LOG_ERR, "Vector append: %s", strerror(errno));
    
    
    
    
 
    
    
    // --- Launch rules ------------------------
    
    int nChildren = 0;
    
    for(int i = 0; i < str_vec_size(&rulePathVec); i++)
    {
        char * thisRulePath = str_vec_ptr(&rulePathVec)[i];
        pid_t fpid = fork();
        if(fpid == 0)
        {
            // I am the child.
            char *newargv[] = { NULL, NULL };
            char *newenviron[] = { NULL };
            execve(thisRulePath, newargv, newenviron);
            // execve only returns if there is an error. 
            LogAbort(LOG_ERR, "execve() failed for path '%s': %s", thisRulePath, strerror(errno));
        }
        else if(fpid < 0)
        {
            // An error happened. 
            LogAbort(LOG_ERR, "Can't fork: %s", strerror(errno));
        }
        nChildren++;
    }
    
       
    
    // --- Monitor ------------------------
   
    // Switching between poll and waitpid is ugly, please suggest better solutions.
    while(nChildren > 0 || tag_vec_ptr(&tags)[MASTERKILLSWITCH_FD_IDX].value.u8 > 0)
    {
        // check for dead children
        if(nChildren > 0)
        {
            pid_t whichChild;
            do
            {
                whichChild = waitpid(-1, NULL ,WNOHANG); // WNOHANG so it doesn't block
                if(whichChild < 0)
                    LogAbort(LOG_ERR, "waitpid() produced an error: %s", strerror(errno));
                else if(whichChild > 0)
                {
                    // TODO actually use this information.
                    nChildren--;
                }
            } while (whichChild > 0 && nChildren > 0);
        }
        
        // Notice we're only polling for a max of 3 seconds, then we check children again. 
        int prc = poll(pfd_vec_ptr(&pollfds), pfd_vec_size(&pollfds), 3000);
        if(prc < 0 && errno != EINTR)
        {
            LogAbort(LOG_ERR, "Poll failed: %s", strerror(errno));
        }
        
        struct pollfd * pfdPtr;
        
        // Check on our timers. 
        for(int i = 0; i < NTIMERS; i++)
        {
            pfdPtr = &pfd_vec_ptr(&pollfds)[i];
            if(pfdPtr->revents)
            {
                if(!(pfdPtr->revents & POLLIN))
                    LogAbort(LOG_ERR, "Unexpected revents on timer %s: %d", str_vec_ptr(&timerNameVec)[i], pfdPtr->revents);
                
                throwawayReadTimerFD(pfdPtr->fd);
                
                tag_t * tagPtr = &tag_vec_ptr(&tags)[i];
                
                incrementTimerTag(tagPtr);
                setTagTimestamp(tagPtr);
                if(!tryWriteTag(int_vec_ptr(&tagfds)[i], *tagPtr))
                    Log(LOG_ERR, "Failed to write tag %s: %s", str_vec_ptr(&timerNameVec)[i], strerror(errno));
            }
            
        }
        
        // Check master killswitch.
        pfdPtr = &pfd_vec_ptr(&pollfds)[MASTERKILLSWITCH_FD_IDX];
        if(pfdPtr->revents)
        {
            if(!(pfdPtr->revents & POLLIN))
                LogAbort(LOG_ERR, "Unexpected revents on master killswitch: %d", pfdPtr->revents);
            
            // Read it. 
            tag_vec_ptr(&tags)[MASTERKILLSWITCH_FD_IDX] = assertReadTag(pfdPtr->fd);
        }
      
        
    }
    
    
    // set all timers to disconnected status
    for(int i = 0; i < NTIMERS; i++)
    {
        tag_t * tagp = &tag_vec_ptr(&tags)[i];
        setTagTimestamp(tagp);
        tagp->quality = QUALITY_DISCONNECTED;
        tryWriteTag(int_vec_ptr(&tagfds)[i], *tagp);
    }
    
    Log(LOG_NOTICE, "Clean shutdown.");
    
    exit(EXIT_SUCCESS);
}
