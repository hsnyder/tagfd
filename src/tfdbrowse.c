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

	tfdbrowse: An ncurses interface to tagfd. 
	Currently output-only (you can monitor the values of tags but not change them).
    Somewhat quick-and-dirty, but cleanup is low priority, as this is "just" a tool.
    
    You can run it with -a to automatically watch all tags. 
	
	Harris M. Snyder, 2018
	
	
	TODO:
	- Scrolling
	- Alphabetical sort of live data list
    - Code cleanup (low priority)

*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>

#include <ncurses.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <dirent.h>

#include "tagfd-shared.h"

#include "tagfd-toolkit.h"


// ====================================================================================
//  UNIVERSAL ERROR HANDLER FUNCTION
// ====================================================================================

static
void error(const char * str, ...)
{
	endwin();
	
	va_list args;
	va_start (args, str);
	if(str)
	{
		vprintf(str, args);
		printf(": %s\n", strerror(errno));
	}
	else 
	{
		puts(strerror(errno));
	}
	va_end (args);
	
	exit(EXIT_FAILURE);
}

#define ASSERT(condition, str, ...) if(!(condition))error(str,__VA_ARGS__);


// ====================================================================================
//  FILE DESCRIPTOR LIST & ASSOCIATED MANAGEMENT FUNCTIONS
// ====================================================================================

/*

	We keep a list of pollfd structures for our poll() call.
	Since struct pollfd lacks a spare field for our use (to store related data), we
	also maintain a second list to store our ancillary data. A given index in gl_fds
	corresponds to the same index in gl_ancillary. 
	
*/

struct ancillary
{
	void (*pollinHandler) (struct pollfd *, void *);
	void * handlerArg ;
};

struct pollfd    * gl_fds       = NULL;
struct ancillary * gl_ancillary = NULL;

#define DEFAULT_MAX_FDS 50
int             gl_n_fds    = 0; // number of structures in the list
int             gl_max_fds  = 0; // allocated capacity of the list

// Adds a file descriptor to the list of those that we want to poll. 
static
void add_fd(int fd, struct ancillary ancil)
{
	// Allocate if necessary 
	if(gl_n_fds == gl_max_fds)
	{
		size_t new_max = (2*gl_max_fds) > DEFAULT_MAX_FDS ? (2*gl_max_fds) : DEFAULT_MAX_FDS;
		gl_fds       = realloc(gl_fds, sizeof(struct pollfd) * new_max );
		gl_ancillary = realloc(gl_ancillary, sizeof(struct ancillary) * new_max );
		if(!gl_fds || !gl_ancillary) error(NULL);
		gl_max_fds = new_max;
	}
	
	// Pick a slot
	struct pollfd    * pfd = gl_fds + gl_n_fds;
	struct ancillary * anc = gl_ancillary + gl_n_fds;

	memset(pfd, 0, sizeof(struct pollfd));
	pfd->fd = fd;
	pfd->events = POLLIN;
	*anc = ancil;
	
	gl_n_fds++;
	
}


// Removes a file descriptor from the list of those that we want to poll. 
static 
void rm_fd(int fd)
{
	// Iterate the fd list.
	for(int i = 0; i < gl_n_fds; i++)
	{
		// When we find the matching structure...
		if(gl_fds[i].fd == fd)
		{
			// Consolidate the list to remove gaps. 
			int nmov = gl_n_fds - i - 1;
			if(nmov > 0)
			{
				memmove(gl_fds + i, gl_fds + i + 1, sizeof(struct pollfd) * nmov);
				memmove(gl_ancillary + i, gl_ancillary + i + 1, sizeof(struct ancillary) * nmov);
			}
			gl_n_fds--;
			
		}
	}
}



// ====================================================================================
//  REDPINEFD FUNCTIONS
// ====================================================================================

struct tag_dev
{
	char name[TAG_NAME_LENGTH];
	int watching;
	tag_t tag;
	
};

int ecmp(struct tag_dev * a, struct tag_dev * b)
{
	return strcmp(a->name, b->name);
}

// Importing a binary tree data structure. 
// Uses a simple macro-based template system for C, see the file for details.
#define TYPE struct tag_dev
#define BTCMP ecmp
#define TEMPLATE_DECL
#define TEMPLATE_DEF
#include "templates/binarytree.h"

struct binTree * gl_tagDevTree = NULL;
static int gl_nTagDevs = 0;
static int gl_nTagDevsWatched = 0;

// Ugly, but... advance declaration for needed function 
void draw_win_main(int inputevent);
void process_data(struct pollfd * pfd, void * arg)
{
	struct tag_dev * edev = (struct tag_dev *)arg;
	ASSERT(sizeof(tag_t) == read(pfd->fd, &edev->tag, sizeof(tag_t)), "Failed to read tag %s", edev->name);
	draw_win_main(-1);
}


static void binTreeTraverse_addTag(struct tag_dev * ed, void * _)
{
    char namebuffer[PATH_MAX+100];
    sprintf(namebuffer, "/dev/tagfd/%s", ed->name);
    int fd = open(namebuffer, O_RDWR);
    
    ASSERT(fd > 0, "Failed to open %n", ed->name);
    struct ancillary anc = {.pollinHandler = process_data, .handlerArg = ed};
    add_fd(fd, anc);
    ed->watching = fd;
    gl_nTagDevsWatched++;
}

static 
void setupTagList(bool add_all)
{

	DIR * devdir = opendir("/dev/tagfd"); 
	if(!devdir) return;
	gl_nTagDevs = 0;
	
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
			fprintf(stderr, "Couldn't stat %s, check permissions.\n", pathbuf);
		}
		else
		{
			if(S_ISCHR(statbuf.st_mode))
			{
				struct tag_dev edv ;
				memset(&edv,0,sizeof(edv));
				strcpy(edv.name, entry->d_name);
				binTree_insert(&gl_tagDevTree, edv);
				gl_nTagDevs++;
			}
		}
	}
	
    if(add_all)
    {
        binTree_orderedTraverse(gl_tagDevTree, binTreeTraverse_addTag, NULL);
    }
    
	closedir(devdir);
}

// ====================================================================================
//  UI FUNCTIONS
// ====================================================================================


// ncurses windows.
static WINDOW * gl_win_tab = NULL;
static WINDOW * gl_win_main = NULL;
static WINDOW * gl_win_inst = NULL;

// Console size.
static int gl_rows,gl_cols;

// Tab definitions. 
#define TAB_TAG_LIST   0
#define TAB_LIVE_DATA     1

// Stores what tab is currently being shown. 
static int gl_selectedTabIndex = TAB_TAG_LIST; 

void draw_win_tab(int inputevent)
{
	static const int NTABS = 2;
	static const char * TABS[] = {"TAG LIST", "LIVE DATA"};
	
	// Handle input (if any)
	switch(inputevent)
	{
		case KEY_LEFT:
			if(gl_selectedTabIndex > 0) gl_selectedTabIndex--;
			break;
			
		case KEY_RIGHT:
			if(gl_selectedTabIndex < NTABS-1) gl_selectedTabIndex++;
			break;
		
		case 0: // take no action
			break;			
			
		default:
			error("draw_win_tab() call bug (inputevent %d)", inputevent);
			break;
	}
	
	
	wclear(gl_win_tab);
	for(int i = 0 ; i < NTABS; i++)
	{
		if(i == gl_selectedTabIndex) // gl_selectedTabIndex if appropriate. 
			wattron(gl_win_tab, A_REVERSE);
		
		wprintw(gl_win_tab, " %-25s", TABS[i]);
		wattroff(gl_win_tab, A_REVERSE);
	}
	wrefresh(gl_win_tab);
	
	// Usused, but saving the logic in case I change my mind...
	/*
		// Distribute TABS evenly across the top 
		// (but let's not do that...)
		int wordlen = strlen(TABS[i]);
		int xcord = (i+1) * gl_cols / (NTABS+1) - wordlen/2; 
		mvwprintw(gl_win_tab, 0, xcord, TABS[i]);
		*/
		
}

// Context structure for tag_dev binary tree traversal callback functions. 
struct tagBinTreeTraverseContext
{
	int ofInterest; // the index within the tree of the tag of interest
	int count; // used internally by the callback functions
	struct tag_dev * output; // this is not used in printTag, only in nthTag
};

// Callback function for traversal of the binary tree of tag_devs - prints them out to the main window. 
void printTag(struct tag_dev * ed, void * param)
{
	struct tagBinTreeTraverseContext * context = (struct tagBinTreeTraverseContext *) param;
	
	int chr = ' ';
	if(ed->watching) chr = 'x';
	
	if(context->count == context->ofInterest)
		wattron(gl_win_main, A_REVERSE);
	wprintw(gl_win_main, "[%c] %s\n", chr, ed->name);
	wattroff(gl_win_main, A_REVERSE);
	
	context->count++;
}

// Callback function for traversal of the binary tree of tag_devs - locates the nth element (ordered by name). 
void nthTag(struct tag_dev * ed, void * param)
{
	struct tagBinTreeTraverseContext * context = (struct tagBinTreeTraverseContext *) param;
	
	if(context->count == context->ofInterest)
		context->output = ed;
	
	context->count++;
}

void draw_win_main(int inputevent)
{
	
	static int hilight = -1;    // index of hilighted row (-1 will result in no hilight)
	static int hilight_lim = 0; // upper limit of what the hilighted row index can be (i.e. the number of rows)
	
	static int selectedTabCached = -1; 
	// We cache the value of gl_selectedTabIndex from the previous call, 
	// because we need to know whether the selected tab changed, in addition 
	// to just needing to know what the current tab is. 
	
	if(selectedTabCached != gl_selectedTabIndex)
	{
		hilight = -1;
		hilight_lim = 0;
		selectedTabCached = gl_selectedTabIndex;
	}
	
	#define SET_LIMIT(lim)  hilight_lim = (lim);  if(hilight >= (lim))  hilight = (lim)-1;
	
	// Handle input (if any)
	switch(inputevent)
	{
		case KEY_UP:
			if(hilight > -1) hilight--;
			break;
			
		case KEY_DOWN:
			if(hilight < hilight_lim - 1) hilight++;
			break;
			
		case ' ':
			// add selected item to watched fd list. 
			if(hilight > -1)
			{
				struct tagBinTreeTraverseContext travCtx = {.count = 0, .ofInterest = hilight, .output = NULL};
				binTree_orderedTraverse(gl_tagDevTree, nthTag, &travCtx);
				// we now have the name of the selected tag 
				
				// is the tag already selected?
				if(travCtx.output->watching)
				{
					close(travCtx.output->watching);
					rm_fd(travCtx.output->watching);
					travCtx.output->watching = 0;
					gl_nTagDevsWatched--;
				}
				else
				{
					char namebuffer[PATH_MAX+100];
					sprintf(namebuffer, "/dev/tagfd/%s", travCtx.output->name);
					int fd = open(namebuffer, O_RDWR);
					
					ASSERT(fd > 0, "Failed to open %n", travCtx.output->name);
					struct ancillary anc = {.pollinHandler = process_data, .handlerArg = travCtx.output};
					add_fd(fd, anc);
					travCtx.output->watching = fd;
					gl_nTagDevsWatched++;
				}
			}
			break;
		
		case 0: // take no action
			break;
			
		case -1: // this only happens when new data shows up.
			if(gl_selectedTabIndex != TAB_LIVE_DATA) return;
			break;
			
		default:
			error("draw_win_main() call bug (inputevent %d)", inputevent);
			break;
	}
	
	wclear(gl_win_main);
	
	// --- TAG LIST TAB --------------------------------------
	
	if(gl_selectedTabIndex == TAB_TAG_LIST)
	{
		SET_LIMIT(gl_nTagDevs);
		
		if(gl_nTagDevs == 0)
		{
			wprintw(gl_win_main, "[No tags]");
		}
		else
		{
			struct tagBinTreeTraverseContext printContext = {.count = 0, .ofInterest = hilight};
			binTree_orderedTraverse(gl_tagDevTree, printTag, &printContext);
		}
		
		
	}
	
	// --- LIVE DATA TAB --------------------------------------
	
	else if(gl_selectedTabIndex == TAB_LIVE_DATA)
	{
		SET_LIMIT(gl_nTagDevsWatched);
		
		int count = -2;
		for(int i = 0; i < gl_n_fds; i++)
		{
			if(gl_ancillary[i].handlerArg)
			{
				if(count < 0) count = 0;
				else count++;
			}
			else
			{
				continue;
			}
			
			struct tag_dev * edev = (struct tag_dev *) gl_ancillary[i].handlerArg;
			
			if(count == hilight)
			{
				wattron(gl_win_main, A_REVERSE);
			}
			//wprintw(gl_win_main, "%-23s   %-20s   %-23s   %s\n", tag_valueToStr(&edev->tag), tag_qualityToStr(&edev->tag), tag_timestampToStr(&edev->tag), edev->name);
			//wprintw(gl_win_main, "%-*s   %-21s   %-21s   %-10s\n", gl_cols-67, edev->name, tag_valueToStr(&edev->tag), tag_timestampToStr(&edev->tag), tag_qualityToStr(&edev->tag));
			wprintw(gl_win_main, "%-8s  %21s  %21s  %s\n", tag_quality_toStrHR(&edev->tag, true), tag_timestamp_toStrHR(&edev->tag), tag_value_toStrHR(&edev->tag), edev->name );
			wattroff(gl_win_main, A_REVERSE);
			
		}
	}
	
	else
	{
		error("BUG DETECTED :( value of 'gl_selectedTabIndex' is wrong it's %d", gl_selectedTabIndex);
	}
	
	wrefresh(gl_win_main);
	return;
}


void draw_win_inst()
{
	wclear(gl_win_inst);
	#define INSTRUCTION(key, val) \
	wattron(gl_win_inst, A_REVERSE);\
	wprintw(gl_win_inst, key);\
	wattroff(gl_win_inst, A_REVERSE);\
	wprintw(gl_win_inst, " " val "\t ");
	

	INSTRUCTION("L/R arrows", "Change tab");
	INSTRUCTION("U/D arrows", "Navigate");
	INSTRUCTION("q ", "Quit");
	INSTRUCTION("F1", "Redraw screen");
	if(gl_selectedTabIndex == TAB_TAG_LIST)
	{
		INSTRUCTION("Space", "Select/deselect");
	}
	
	wrefresh(gl_win_inst);
}



void resize_wins(void)
{
	clear();
	curs_set(0);
	
	// Get screen size. 
	getmaxyx(stdscr,gl_rows,gl_cols);
	
	// Tab window (top)
	if(gl_win_tab)delwin(gl_win_tab);
	gl_win_tab = newwin(1,gl_cols,0,0);
	
	// Main window (center)
	if(gl_win_main)delwin(gl_win_main);
	gl_win_main = newwin(gl_rows-3,gl_cols,2,0);
	
	// Instruction window (bottom)
	if(gl_win_inst)delwin(gl_win_inst);
	gl_win_inst = newwin(1,gl_cols,gl_rows-1,0);
	
	// Refresh and draw
	refresh();
	draw_win_tab(0);
	draw_win_main(0);
	draw_win_inst();
}


void process_input(struct pollfd * notused1, void * notused2)
{
	
	// Get and dispatch input. 
	int c = getch();
	
	switch(c)
	{
		// --- Q: quit program ------------
		case 'q':
		case 'Q':
			exit(EXIT_SUCCESS);
			
		// --- SPACE: select -----------
		case ' ':
			draw_win_main(' ');
			break;
		
		// --- L/R arrow: change tab ------------	
		case KEY_LEFT:
			draw_win_tab(KEY_LEFT);
			draw_win_main(0);
			draw_win_inst();
			break;
			
		case KEY_RIGHT:
			draw_win_tab(KEY_RIGHT);
			draw_win_main(0);
			draw_win_inst();
			break;
			
		// --- U/D arrow: navigate main window ------------	
		case KEY_UP:
			draw_win_main(KEY_UP);
			break;
			
		case KEY_DOWN:
			draw_win_main(KEY_DOWN);
			break;
			
		// --- F1: redraw everything ------------	
		case KEY_F(1):
			resize_wins();
			break;

		// --- Otherwise: no action ------------
		default: 
			break;
	}
	
	
}




// ====================================================================================
//  MAIN FUNCTION                     .... (and a cleanup function)
// ====================================================================================

void my_atexit(void)
{	
	endwin(); // ncurses shutdown
	
	if(gl_fds)
		free(gl_fds);
	
	if(gl_ancillary)
		free(gl_ancillary);
	
	if(gl_tagDevTree)
		binTree_clear(gl_tagDevTree);
}

int main(int argc, char ** argv)
{	
	struct ancillary anc = {.pollinHandler = process_input, .handlerArg = NULL};
	add_fd(STDIN_FILENO, anc );
	atexit(my_atexit);
	
    bool add_all = argc > 1 && !strcmp(argv[1],"-a");
    if(add_all) gl_selectedTabIndex = TAB_LIVE_DATA;
	setupTagList(add_all);
	
	// ncurses setup
	initscr();
	noecho();
	raw();
	
	// Allow arrow keys and F keys
	keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
	
	// Draw the UI for the first time. 
	resize_wins();
	
	while(1)
	{
		// Poll event descriptors
		if(0 > poll(gl_fds, gl_n_fds, -1)) 
		{
			if(errno == EINTR) // interrupted by a signal: probably window resize. 
			{
				resize_wins();
				continue;
			}
			else
			{
				error ("poll() failed");
			}
		}
		
		// Dispatch 
		for(int i = 0; i < gl_n_fds; i++)
		{
			// If there is a POLLIN event on this fd... 
			if(gl_fds[i].revents & POLLIN)
			{
				// Call the handler function for that fd (after a safety check to prevent segfaults).
				if(gl_ancillary[i].pollinHandler)
					gl_ancillary[i].pollinHandler(&gl_fds[i],gl_ancillary[i].handlerArg);
				else
					error("Bug: empty pollinHandler for fd %d (if the handler is empty why are you polling it?\n", gl_fds[i].fd);
			}
			else if(gl_fds[i].revents)
			{
				error("Unexpected revents %d on fd %d",gl_fds[i].revents, gl_fds[i].fd);
			}
		}
		
	}

	exit(EXIT_SUCCESS);
}
