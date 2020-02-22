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

    tfdrelay: a streaming program for tagfd. 
    
	Harris M. Snyder, 2018

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>



#include "tagfd-shared.h"

#include "tagfd-toolkit.h"

#include <sys/poll.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>



// import a vector data type - uses a simple macro based template system for C
// specialize the vector for struct pollfd. 
// and we want it to close the file descriptors when they get removed.
#define TYPE struct pollfd
#define PREFIX fd
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#define SVDESTRUCTOR pollfd_destructor
void pollfd_destructor(struct pollfd * pfd) { close(pfd->fd); }
#include "templates/smallvector.h"

// we want another specialization of this data type that can take strings
// and we want it to free the strings when it's destroyed. 
#define TYPE char*
#define PREFIX s
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#define SVDESTRUCTOR s_metafree // note that by adding this destructor, the vector must "own" the strings in it. I.e. use strdup
void s_metafree(char** ptr){free(*ptr);}
#include "templates/smallvector.h"

// we want another specialization of this data type that can take tags
#define TYPE tag_t
#define PREFIX t
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"


struct svec   g_argv;

struct svec   g_tagNames;
struct fdvec  g_fds;

bool          g_opt_dash_a = false; // -a flag was passed. 
bool          g_opt_dash_n = false; // -n flag was passed.


void usage(void)
{
    puts("Usage: tfdrelay [-a] [-n] [tag-names]");
    puts("");
    puts("If you use -a, then all tags will be watched, and [tag-names] is ignored.");
    puts("");
    puts("If you use -n, then tag names will be explicitly printed on each data push.");
    puts("This is intended to make the stream more human-readable. Without -n, tag ");
    puts("Names are printed one time, when the program starts up, and each is ");
    puts("associated with a sequential, zero-based index to accelerate stream parsing");
    puts("at the site of stream consumption. ");
    puts("");
    puts("[tag-names] must be supplied if not using -a, and is a space-separated list");
    puts("of tagfd tag names that you want to watch. ");
    
    exit(EXIT_SUCCESS);
}


// directory walking callback for finding tags
int findTags(void* param, const char * name, const char * path, struct stat sb)
{
    // make sure we're looking at a char device.
    if(!S_ISCHR(sb.st_mode)) return 0;

    // unless we're adding all tags...
    if(!g_opt_dash_a)
    {
        // scan the argv list to see if we're supposed to be adding this tag. 
        for(int i = 0 ; i < svec_size(&g_argv); i++)
        {
            if(!strcmp(name, svec_ptr(&g_argv)[i]))
            {
                // remove it from the vector 
                svec_remove(&g_argv, i);
                goto proceed;
            }
        }
        // this tag isn't on our list so skip it. 
        return 0;
    }
    
    proceed:

    if(!svec_append(&g_tagNames, strdup(name)))
    {
        printf("Error: failed vector append: %s\n", strerror(errno));
        return -1;
    }
    
    struct pollfd pfd = {
        .events = POLLIN, 
        .revents = 0, 
        .fd = open(path, O_RDONLY)
    };
    
    if(pfd.fd < 0)
    {
        printf("Error: failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    if(!fdvec_append(&g_fds, pfd))
    {
        printf("Error: failed vector append: %s\n", strerror(errno));
        return -1;
    }
        
    return 0;
}


// directory walking callback for stat failure 
int cantStat(void* param, const char * name, const char * path)
{
    printf("Error: can't stat %s: %s\n", path, strerror(errno));
    return -1;
}


// called on exit. 
void cleanup(void)
{
    svec_destroy(&g_argv);
    svec_destroy(&g_tagNames);
    fdvec_destroy(&g_fds);
}


void tag_print_name(tag_t tag, const char * tagname)
{
    printf("n %s %s\n", tagname,tag_toStr_partial(&tag));
}

void tag_print_index(tag_t tag, int i)
{
    printf("i %d %s\n", i, tag_toStr_partial(&tag));   
}



static volatile int g_sigint = 0;

void sigint_handler(int dummy) {
    g_sigint = 1;
}

int main(int argc, char ** argv)
{
    svec_init(&g_argv);
    svec_init(&g_tagNames);
    fdvec_init(&g_fds);
    
    atexit(cleanup);
    
    signal(SIGINT, sigint_handler);
    
    if(argc < 2) usage();
    
    // parse command line args. 
    for(int i = 1; i < argc; i++)
    {
        if     (!strcmp(argv[i],"-a")) g_opt_dash_a = true;
        else if(!strcmp(argv[i],"-n")) g_opt_dash_n = true;
        else
        {
            if(!svec_append(&g_argv, strdup(argv[i])))
            {
                printf("Error: Vector append failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
    }
        
    // walk the tag directory to find tags. 
    const char * errMsg ; 
    int wrc = walkDirectory("/dev/tagfd", NULL, NULL, &errMsg, findTags, cantStat);
    if(wrc == 1) exit(EXIT_FAILURE);
    if(wrc == -1)
    {
        printf("Error: %s failed when trying to walk /dev/tagfd: %s\n", errMsg, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // check for unfound tags.
    for(int i = 0; i < svec_size(&g_argv); i++)
    {
        printf("Error: Tag not found: %s\n", svec_ptr(&g_argv)[i]);
        exit(EXIT_FAILURE);
    }
    
    // Output the index-tagname association list. 
    // We need to store the values in this loop because our "protocol" requires us to output those separately. 
    struct tvec tags;
    tvec_init(&tags);
    for(int i = 0; i < fdvec_size(&g_fds); i++)
    {
        struct pollfd pfd = fdvec_ptr(&g_fds)[i];
        char * tagname = svec_ptr(&g_tagNames)[i];
        
        tag_t tag;
        if(sizeof(tag_t) != read(pfd.fd, &tag, sizeof(tag_t)))
        {
            printf("Error: failed to read tag %s: %s", tagname , strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        if(!tvec_append(&tags, tag))
        {
            printf("Error: Vector append failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        printf("a %d %s %d\n", i, tagname, tag.dtype);
    }
    printf("\n");
    
    // Output initial values. 
    for(int i = 0; i < tvec_size(&tags); i++)
    {
        if(g_opt_dash_n)
            tag_print_name(tvec_ptr(&tags)[i], svec_ptr(&g_tagNames)[i]);
        else
            tag_print_index(tvec_ptr(&tags)[i], i);
    }
    
    tvec_destroy(&tags); // don't need this anymore. 
    
    // poll forever. 
    while(!g_sigint)
    {
        int rc = poll(fdvec_ptr(&g_fds), fdvec_size(&g_fds), -1);
        if(rc < 1)
        {
            if(errno == EINTR) continue;
            
            printf("Error: poll failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        
        for(int i = 0; i < fdvec_size(&g_fds); i++)
        {
            struct pollfd pfd = fdvec_ptr(&g_fds)[i];
            char * tagname = svec_ptr(&g_tagNames)[i];
            if(pfd.revents == POLLIN || pfd.revents == (POLLIN & POLLRDNORM))
            {
                tag_t tag;
                if(sizeof(tag_t) != read(pfd.fd, &tag, sizeof(tag_t)))
                {
                    printf("Error: failed to read tag %s: %s", tagname , strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if(g_opt_dash_n)
                    tag_print_name(tag, tagname);
                else
                    tag_print_index(tag, i);
                
            }
            else if(pfd.revents)
            {
                printf("Error: unexpected revents on tag %s: %d\n", tagname, pfd.revents);
                exit(EXIT_FAILURE);
            }
        }
    }
    
    exit(EXIT_SUCCESS);
}
