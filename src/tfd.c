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

	tfd: A command-line tool for interacting with tagfd
    Somewhat quick-and-dirty, but cleanup is low priority, as this is "just" a tool.
	
	Harris M. Snyder, 2018
	
	TODO:
	- Input of timestamp value types.
    - Code cleanup.
	
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <time.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <dirent.h>



// Defining this macro suppresses some of the stuff in the rule toolkit that 
// would break this program. 
#define __I_AM_NOT_A_RULE__ 1
#define NO_SYSLOG 1 // some toolkit functions use the syslog. We don't want to. 
#include "ruletoolkit.h"

#include "tagfd-toolkit.h"

typedef struct named_tag
{
	tag_t tag;
	char name[TAG_NAME_LENGTH];
} named_tag_t;

// sorting function. 
int eNameCmp(const void * a_, const void * b_)
{
	const named_tag_t * a = a_;
	const named_tag_t * b = b_;
	return strcmp(a->name, b->name);
}



// ====================================================================================
//  COMMAND HANDLER: LIST
// ====================================================================================

int countDigits (int n) {
    if (n < 0) return countDigits ((n == INT_MIN) ? INT_MAX: -n);
    if (n < 10) return 1;
    return 1 + countDigits (n / 10);
}

// Importing a vector data structure. 
// Uses a simple macro-based template system for C, see the file for details.
#define TYPE named_tag_t
#define PREFIX tag_
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/smallvector.h"

void list(const char * filter )
{
	struct tag_vec tags;
	tag_vec_init(&tags);
	
	// Open /dev/tagfd/ directory
	DIR * devdir = opendir("/dev/tagfd"); 
	if(!devdir)
	{
		perror("Can't open /dev/tagfd");
		exit(EXIT_FAILURE);
	}
	
	// Walk the /dev/tagfd directory
	while(1)
	{
		struct dirent * entry = readdir(devdir);
		if(!entry) break;
		
		char pathbuf [PATH_MAX+100];
		sprintf(pathbuf, "/dev/tagfd/%s", entry->d_name);
		struct stat statbuf;
		if (lstat(pathbuf, &statbuf) < 0)
		{
			printf("Couldn't stat %s, check permissions.\n", pathbuf);
		}
		else
		{
			if(S_ISCHR(statbuf.st_mode))
			{
				if(filter && 0 != strncmp(filter, entry->d_name, strlen(filter))) 
					continue;
				
				named_tag_t e ;
				memset(&e,0,sizeof(e));
				strcpy(e.name, entry->d_name);
				
                int fd = assertOpenTag(e.name);
				e.tag = assertReadTag(fd);
                close(fd);
				
				tag_vec_append(&tags, e);
			}
		}
	}
	
	
	closedir(devdir);
	
	
	// sort the tag list by name, if necessary.
	if(tag_vec_size(&tags) > 1)
		qsort( tag_vec_ptr(&tags),
			   tag_vec_size(&tags),
			   sizeof(named_tag_t),
			   eNameCmp);
		
	
	
	int ndigits = countDigits(tag_vec_size(&tags));
	for(int i = 0; i < tag_vec_size(&tags); i++)
	{
		printf("  %*d)  %-9s  %s\n", ndigits, i+1, tag_dtype_toStrHR(& tag_vec_ptr(&tags)[i].tag), tag_vec_ptr(&tags)[i].name );
	}
}


// ====================================================================================
//  COMMAND HANDLER: HELP
// ====================================================================================

void help(void)
{
	//printf(PROGRAM " v" VERSION " supported commands:\n\n");
	/*
	printf(

"\thelp                     Show this message.\n\n"

"\tlist [filter]            List all tags. If you supply a filter string, only tags whose names start with the provided string will be shown.\n\n"


"\tv [tag] [value]          Set value. You can specify the item either by entering it's name, "
"or by entering '#' follwed by the number appearing next to the item in the output of the 'list' command. "
"For example, if list outputs 3) SomeItem, then you could enter '#3' to affect that item, rather than typing out it's whole name. "
"The supplied value must be appropriate for the item's data type. To supply a value of the 'timestamp' data type, use the format: "
"YYYY-MM-DD hh:mm:ss.lll where lll is milliseconds. Updating an item's value updates its timestamp as well. \n\n"

"\tq [tag] [value] [vendor]  Set quality. Item value can be specified the same way as for the 'v' command. Value should be "
"'GOOD', 'UNCERTAIN', 'BAD', or 'DISCONNECTED'. Vendor is optional, and should be a positive integer, or 0 (0 is the default).\n\n"
	
	);
	*/
	
	printf("Usage: tfd <command> [<args>]\n\n");
	
	printf("Here are the commands and the arguments they take:\n");
	printf("\n");
	
	printf("   %-4s  %s\n", "help", "Displays this message.");
	printf("\n");
	
	printf("   %-4s  %s\n", "list", "Lists all tagfd tags found.");
	printf("   %-4s  %s\n", "", "Can accept a single argument (a string). ");
	printf("   %-4s  %s\n", "", "If supplied, only tags whose names begin with the string will be listed. ");
	printf("\n");
	
	printf("   %-4s  %s\n", "r", "READ a tag. Requires 1 argument (the name of the tag to read).");
	printf("\n");
	
	printf("   %-4s  %s\n", "sv", "SET VALUE of a tag. Requires 2 arguments:");
	printf("   %-4s  %s\n", "", "- Name of the tag to read ");
	printf("   %-4s  %s\n", "", "- New value (must be appropriate for the data type). ");
	printf("   %-4s  %s\n", "", "  Remember, if the value contains spaces, it must be ");
	printf("   %-4s  %s\n", "", "  surrounded in quotes or the shell may interpret it ");
	printf("   %-4s  %s\n", "", "  as multiple arguments. For timestamp values, use  ");
	printf("   %-4s  %s\n", "", "  the format \"YYYY-MM-DD hh:mm:ss.lll\" (l for ms). ");
	printf("\n");
	
	printf("   %-4s  %s\n", "sq", "SET QUALITY of a tag. Requires 2 arguments:");
	printf("   %-4s  %s\n", "", "- Name of the tag to read ");
	printf("   %-4s  %s\n", "", "- New quality: GOOD, UNCERTAIN, BAD, or DISCONNECTED.");
	printf("   %-4s  %s\n", "", "You can also supply an optional third argument:");
	printf("   %-4s  %s\n", "", "- 'Vendor' quality. This should be a nonnegative ");
	printf("   %-4s  %s\n", "", "  integer, maximum 16,383.");
	
	printf("\n");
	
}


// ====================================================================================
//  COMMAND HANDLERS: SETTING VALUE AND QUALITY
// ====================================================================================


void setvalue(const char * tag, const char * value)
{
    int fd = assertOpenTag(tag);
	tag_t e = assertReadTag(fd);
	switch(e.dtype)
	{
		case DT_INT8:;
			int8_t i8val;
			if(!sscanf(value,"%"SCNd8,&i8val)) goto invalid;
			e.value.i8 = i8val;
			break;
		
		case DT_UINT8:;
			uint8_t u8val;
			if(!sscanf(value,"%"SCNu8,&u8val)) goto invalid;
			e.value.u8 = u8val;
			break;
		
		case DT_INT16:;
			int16_t i16val;
			if(!sscanf(value,"%"SCNd16,&i16val)) goto invalid;
			e.value.i16 = i16val;
			break;
		
		case DT_UINT16:;
			uint16_t u16val;
			if(!sscanf(value,"%"SCNu16,&u16val)) goto invalid;
			e.value.u16 = u16val;
			break;
		
		case DT_INT32:;
			int32_t i32val;
			if(!sscanf(value,"%"SCNd32,&i32val)) goto invalid;
			e.value.i32 = i32val;
			break;
			
		case DT_UINT32:;
			uint32_t u32val;
			if(!sscanf(value,"%"SCNu32,&u32val)) goto invalid;
			e.value.u32 = u32val;
			break;
			
		case DT_INT64:;
			int64_t i64val;
			if(!sscanf(value,"%"SCNd64,&i64val)) goto invalid;
			e.value.i64 = i64val;
			break;
			
		case DT_UINT64:;
			uint64_t u64val;
			if(!sscanf(value,"%"SCNu64,&u64val)) goto invalid;
			e.value.u64 = u64val;
			break;
			
		case DT_REAL32:;
			float r32val;
			if(!sscanf(value, "%f",&r32val)) goto invalid;
			e.value.real32 = r32val;
			break;
			
		case DT_REAL64:;
			double r64val;
			if(!sscanf(value, "%lf",&r64val)) goto invalid;
			e.value.real64 = r64val;
			break;
			
		case DT_TIMESTAMP:;
			/*
			int y,m,d,h,m,s,ms;
			if(sscanf(value, "%d-%d-%d %d:%d:%d.%d", &y,&m,&d,&h,&m,&s,&ms) != 7) goto invalid;
			struct tm mytm;
			*/
			printf("Setting timestamp values not implemented.\n");
			exit(EXIT_FAILURE);
			break;
			
		case DT_STRING:;
			memset(e.value.string,0, TAG_STRING_VALUE_LENGTH);
			strncpy(e.value.string,value,TAG_STRING_VALUE_LENGTH-1);
			break;
		
		default:
			printf("Invalid data type %s.\n", tag_dtype_toStr(&e));
			break;
	}
	
	setTagTimestamp(&e);
	assertWriteTag(fd, e);
    close(fd);
	
	return;
	
	invalid:
	printf("Invalid value '%s' for data type %s.\n", value, tag_dtype_toStrHR(&e));
	close(fd);
}

void setquality(const char * tag, const char * quality, const char * vendor)
{
    int fd = assertOpenTag(tag);
	tag_t e = assertReadTag(fd);
	
	uint16_t vnd = e.quality & QUALITY_VENDOR_MASK;
	if(vendor)
	{
		if(!sscanf(vendor, "%"SCNu16, &vnd))
		{
			printf("Vendor quality value format incorrect (must be a positive integer or zero).\n");
            close(fd);
			return;
		}
		vnd &= QUALITY_VENDOR_MASK;
	}
	
	if(0 == strcmp(quality, "GOOD"))
	{
		e.quality = QUALITY_GOOD | vnd;
	}
	else if(0 == strcmp(quality, "UNCERTAIN"))
	{
		e.quality = QUALITY_UNCERTAIN | vnd;
	}
	else if(0 == strcmp(quality, "BAD"))
	{
		e.quality = QUALITY_BAD | vnd;
	}
	else if(0 == strcmp(quality, "DISCONNECTED"))
	{
		e.quality = QUALITY_DISCONNECTED | vnd;
	}
	else 
	{
		printf("Invalid quality (must be GOOD, UNCERTAIN, BAD, or DISCONNECTED).\n");
        close(fd);
		return;
	}
	
	setTagTimestamp(&e);
	assertWriteTag(fd, e);
    close(fd);
}


// ====================================================================================
//  MAIN
// ====================================================================================

int main(int argc, char ** argv)
{
	
	if(argc < 2)
		goto args;
	
	if(0 == strcmp(argv[1], "help"))
	{
		help();
	}
	else if(0 == strcmp(argv[1], "list"))
	{
		if(argc == 3) list(argv[2]);
		else if(argc == 2) list(NULL);
		else goto args;
	}
	else if(0 == strcmp(argv[1], "r"))
	{
		if(argc != 3) goto args;
        int fd = assertOpenTag(argv[2]);
		tag_t ent = assertReadTag(fd);
        close(fd);
		printf("name      %s\n"
		       "dtype     %s\n"
			   "quality   %s\n"
			   "timestamp %s\n"
			   "value     %s\n",
			argv[2], 
			tag_dtype_toStrHR(&ent), 
			tag_quality_toStrHR(&ent, false), 
			tag_timestamp_toStrHR(&ent),
			tag_value_toStrHR(&ent));
	}
	else if(0 == strcmp(argv[1], "sv"))
	{
		if(argc != 4) goto args;
		setvalue(argv[2], argv[3]);
		
	}
	else if(0 == strcmp(argv[1], "sq"))
	{
		if(argc != 4 && argc != 5) goto args;
		if(argc == 4)
			setquality(argv[2], argv[3], NULL);
		else
			setquality(argv[2], argv[3], argv[4]);
	}
	else
	{
		printf("Invalid command (try 'help').\n");
	}
	
	exit(EXIT_SUCCESS);
	
	args:
	
	printf("Missing, incorrect, or extraneous arguments (try 'help').\n");
	exit(EXIT_SUCCESS);
	
}
