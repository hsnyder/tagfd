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

    tfdlog: a logging utility that uses SQLite3 as a backend. 

    INCOMPLETE & NON-FUNCTIONAL
    
*/

#include "tagfd-toolkit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "tagfd-shared.h"
#include "tagfd-toolkit.h"

#include <sqlite3.h>

// Standard (for this repository) C template shenanigans
#define TYPE char*
#define PREFIX s
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#define SVDESTRUCTOR s_metafree // note that by adding this destructor, the vector must "own" the strings in it. I.e. use strdup
void s_metafree(char** ptr){free(*ptr);}
#include "templates/smallvector.h"

void usage()
{
    puts("To use tfdlog, pipe the output of tfdrelay into tfdlog's STDIN.");
}

struct svec g_argvTagNames;

void cleanup(void)
{
    svec_destroy(&g_argvTagNames);
}

int main(int argc, char ** argv)
{
    svec_init(&g_argvTagNames);
    
    #define STATE_PREAMBLE 1
    #define STATE_STREAM 2
    int state = STATE_PREAMBLE;
    
    char * line = NULL;
    size_t n = 0;
    ssize_t len;
    while((len = getline(&line, &n, stdin)) != -1)
    {
        if(state == STATE_PREAMBLE)
        {
            if(!strcmp(line, "\n")) state++;
            else
            {
                uint8_t dtype;
                
            }
        }
        else
        {
            
        }
    }
    
    if(errno)
    {
        printf("Error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {
        exit(EXIT_SUCCESS);
    }
}

