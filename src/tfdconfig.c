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

    tfdconfig: a configuration tool for tagfd.
    This program reads a configuration file and creates tags.
    It must be run as root. It reads the config file twice -
    parsing for validity on the first pass, and actually 
    sending the data to /dev/tagfd.master on the second. 
    It's definitely quick-and-dirty, but since it's "just" a
    tool, cleaning it up is low priority.
    
	Harris M. Snyder, 2018
    
    TODO: 
    - Code cleanup (low priority)

*/

#define CONFPATH "tagfd.conf"




#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFSZ 1024

#include "tagfd-shared.h"

#include "tagfd-toolkit.h"


#define STR(x) #x
#define STR2(x) STR(x)
#define STRLEN STR2(TAG_STRING_VALUE_LENGTH)


static const char * validTagNameChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_";

_Static_assert(BUFSZ > TAG_NAME_LENGTH, "BUFSZ must be greater thatn TAG_NAME_LENGTH.");


void usage()
{
    puts("Usage: tfdconfig [action] [data type] [name]");
    puts("This is the exact order and number of arguments. None are optional.");
    puts("");
    puts("[action]    Can be '+' (for 'add tag') or 't' (for 'test command').");
    puts("            Test command allows you to try a set of arguments without");
    puts("            actually creating a tag. ");
    puts("");
    puts("[data type] Can be one of: int8, uint8, int16, uint16, int32, uint32, ");
    puts("            int64, uint64, real32, real64, timestamp, string. The int ");
    puts("            types are self-explanatory. Real32 and 64 correspond to C's");
    puts("            float and double, respectively. Timestamp is the same as ");
    puts("            uint64, but represents a timestamp in epoch-milliseconds UTC.");
    puts("            String is a text string of at most "STRLEN" bytes (or a");
    puts("            binary blob of up to that size).");
    puts("");
    puts("[name]      is the name of the tag to be created. Valid tag names can");
    puts("            consist of alphanumeric characters plus any of .-_");
    exit(EXIT_FAILURE);
}


void go (const char * name, uint8_t dtype)
{
	int fd = open("/dev/tagfd.master", O_WRONLY);
	if(fd < 0)
	{
		printf("Couldn't open /dev/tagfd.master: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
    
	struct tag_config ecfg;
	memset(&ecfg, 0, sizeof(struct tag_config));
	
	ecfg.action = '+';
	ecfg.dtype = dtype;
	strncpy(ecfg.name, name, TAG_NAME_LENGTH-1);
	
	
	int rc = write(fd, &ecfg, sizeof(struct tag_config));
	if(rc < 0)
	{
		printf("Failed to create %s (%"PRIu8"): %s\n", name, dtype, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	printf("Created %s (%"PRIu8")\n", name, dtype);
	
	close(fd);
}

int main(int argc, char ** argv)
{
    if(argc != 4) usage();
    
    #define CREATE 1
    #define TEST 2
    int mode = CREATE;
    
    if      (!strcmp(argv[1], "+")) mode = CREATE;
    else if (!strcmp(argv[1], "t")) mode = TEST;
    else usage();
    
    uint8_t dtype = tag_dtype_fromStrHR(argv[2]);
    if(dtype == DT_INVALID)
    {
        printf("Unrecognized data type. \n");
        exit(EXIT_FAILURE);
    }
    
    // validate name
    if(strlen(argv[3]) < 1)
    {
        printf("Name too short.\n");
        exit(EXIT_FAILURE);
    }
    
    if(!strcmp(argv[3], ".") || !strcmp(argv[3], ".."))
    {
        printf("Invalid name.\n");
    }
    
    if(strlen(argv[3]) > TAG_NAME_LENGTH - 1)
    {
        printf("Name too long.\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < strlen (argv[3]); i++)
    {
        if(!strchr(validTagNameChars,argv[3][i]))
        {
            printf("Invalid name.\n");
            exit(EXIT_FAILURE);
        }	
    }
    
    // TODO: check if already exists. 
    
    if(mode == CREATE)
    {
        go(argv[3], dtype);
    }
    else
        printf("Test OK for: %s\n", argv[3]);
    
    
    exit(EXIT_SUCCESS);
}

// ---------------------------------------------------------------
// OLD VERSION
// ---------------------------------------------------------------

/*
int parse(FILE * f, int (*clbk)(const char * name, uint8_t dt))
{
	char buf [BUFSZ];
	char dtbuf [BUFSZ];
	char nbuf [BUFSZ];
	memset (buf, 0, BUFSZ);
	memset (dtbuf, 0, BUFSZ);
	memset (nbuf, 0, BUFSZ);
	
	
	int ln = 0;
	while(fgets(buf, BUFSZ-1, f))
	{
		ln++;
		if(strlen(buf) == BUFSZ-1 )
		{
			printf("Line %d too long.\n", ln);
			return -1;
		}
		
		bool blank = true;
		for(int i = 0 ; i < strlen(buf); i++)
		{
			if(!isspace(buf[i]))
			{
				blank = false;
				break;
			}
		}
		if(blank) continue;
		
		int scanrslt = sscanf(buf, "%s %s", dtbuf, nbuf);
		
		if(scanrslt != 2)
		{
			printf("Invalid line in file: '%s'\n", buf);
			return -1;
		}
		
			
		
		
		// validate dtype
		uint8_t dtype;
		if(0==strcmp(dtbuf, "INT8")) dtype = DT_INT8 ;
		else if(0==strcmp(dtbuf, "UINT8")) dtype = DT_UINT8; 
		else if(0==strcmp(dtbuf, "INT16")) dtype = DT_INT16 ;
		else if(0==strcmp(dtbuf, "UINT16")) dtype = DT_UINT16;
		else if(0==strcmp(dtbuf, "INT32")) dtype = DT_INT32 ;
		else if(0==strcmp(dtbuf, "UINT32")) dtype = DT_UINT32; 
		else if(0==strcmp(dtbuf, "INT64")) dtype = DT_INT64 ;
		else if(0==strcmp(dtbuf, "UINT64")) dtype = DT_UINT64; 
		else if(0==strcmp(dtbuf, "REAL32")) dtype = DT_REAL32 ;
		else if(0==strcmp(dtbuf, "REAL64")) dtype = DT_REAL64 ;
		else if(0==strcmp(dtbuf, "TIMESTAMP")) dtype = DT_TIMESTAMP; 
		else if(0==strcmp(dtbuf, "TEXT")) dtype = DT_STRING ;
		else
		{
			printf("Invalid data type '%s' on line %d .\n", dtbuf, ln);
			return -1;
		}
		
		// validate name
		if(strlen(nbuf) < 1)
		{
			printf("Name too short on line %d\n", ln);
			return -1;
		}
		
		if(strlen(nbuf) > TAG_NAME_LENGTH - 1)
		{
			printf("Name too long on line %d\n", ln);
			return -1;
		}
		
		for (int i = 0; i < strlen (nbuf); i++)
		{
			if(!strchr(validTagNameChars,nbuf[i]))
			{
				printf("Name invalid on line %d: '%s'\n",ln, nbuf);
				return -1;
			}	
		}
		
		if(clbk)
			if(clbk(nbuf,dtype))
				return -1;
			
		memset (buf, 0, BUFSZ);
		memset (dtbuf, 0, BUFSZ);
		memset (nbuf, 0, BUFSZ);
	}
	
	return 0;
}


int fd;

int go (const char * name, uint8_t dtype)
{
	
	struct tag_config ecfg;
	memset(&ecfg, 0, sizeof(struct tag_config));
	
	ecfg.action = '+';
	ecfg.dtype = dtype;
	strncpy(ecfg.name, name, TAG_NAME_LENGTH-1);
	
	
	int rc = write(fd, &ecfg, sizeof(struct tag_config));
	if(rc < 0)
	{
		printf("Failed to create %s (%"PRIu8"): %s\n", name, dtype, strerror(errno));
		return -1;
	}
	
	printf("Created %s (%"PRIu8")\n", name, dtype);
	
	return 0;
}


int main(void)
{
	fd = open("/dev/tagfd.master", O_WRONLY);
	if(fd < 0)
	{
		perror("open()");
		exit(EXIT_FAILURE);
	}
	
	
	FILE * f = fopen(CONFPATH, "r");
	if(!f)
	{
		printf("Open failed.\n");
		exit( EXIT_FAILURE);
	}
	
	int err = parse(f, NULL);
	if(err)
		exit(EXIT_FAILURE);
	
	fclose(f);
	
	f = fopen(CONFPATH, "r");
	
	//  go again, actually deliver this time. 
	
	err = parse(f, go);
	if(err)
		exit(EXIT_FAILURE);
	
	fclose(f);
	close(fd);
	
	exit(EXIT_SUCCESS);

} */
