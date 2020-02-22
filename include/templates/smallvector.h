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
---- [BOILERPLATE INFO ABOUT TEMPLATE SYSTEM] ---------------------------
       (see further down for file-specific information)

This file uses a simple template system for C.

To create a type based on the template, define TYPE and (optionally) PREFIX before including this file.
You also must define TEMPLATE_DECL and/or TEMPLATE_DEF. 
If you define TEMPLATE_DECL, the function declarations will appear at the include site.
If you define TEMPLATE_DEF, the actual function definitions will also appear at the include site. 
This file will undefine all of the aforementioned constants, so you don't need to worry about cleaning them up yourself. 

Example of use:
I want a specialization of this template that takes integers, and I want to prefix the names of all functions with "n_".

In my .h file, I say:

#define TEMPLATE_DECL
#define TYPE int
#define PREFIX n_
#include "thisfile.h"

In my .c file, I say:

#define TEMPLATE_DEF
#define TYPE int
#define PREFIX n_
#include "thisfile.h"


Some credit for this system is due to:
https://stackoverflow.com/questions/2873850/is-there-an-equivalent-in-c-for-c-templates

*/

#ifndef PREFIX
    #define PREFIX
#endif
#define CCAT2(x, y) x ## y
#define CCAT(x, y) CCAT2(x, y)
#define NS(x) CCAT(PREFIX, x) // NS for namespace

#ifndef TYPE
    #error Template argument missing.
#endif

#ifndef TEMPLATE_DECL
	#ifndef TEMPLATE_DEF
		#error You must define either or both of TEMPLATE_DECL and TEMPLATE_DEF
	#endif
#endif


/*

---- [INFORMATION SPECIFIC TO THIS FILE] ---------------------------

This file provides a simple small-size optimized vector data structure.
By "small size optimized", I mean that if it doesn't grow past a certain 
number of elements, the contents are stored inside the struct, which can be
on the stack. If you have a ton of these, it could save some pointer indirection
and improve cache coherency. If you exceed that size, it will allocate memory
on the heap and proceed as normal. Be aware of this when storing pointers to 
the vector's contents... Appending to the vector may invalidate existing 
pointers. You can set the expected size by defining SVSIZE. The default 
value is 10.

You can provide your own realloc and free implementations.
If you do not define SVREALLOC and SVFREE before including this file, 
the standard library ones will be used. 

You can also provide a destructor function. This will get called on every 
element removed from the vector (including all elements when the vector is
destroyed). To supply such a function, define SVDESTRUCTOR to the name of the
destructor function. The destructor must take an argument of type TYPE*


*/

#ifndef SVREALLOC
	#ifndef SVFREE
		#include <stdlib.h>
		
		#define SVREALLOC realloc
		#define SVFREE free
		
		#define MUSTUNDEF_MALLOCFREE
	#else
		#error If you provide SVREALLOC you must provide SVFREE
	#endif
#endif

#ifndef SVSIZE 
	#define SVSIZE 10
#endif


// ------------------------------------------------------------------------------------------
// DECLARATIONS SECTION
// ------------------------------------------------------------------------------------------

#ifdef TEMPLATE_DECL

#include <stdbool.h>

struct NS(vec)
{ 
	TYPE store_inline[SVSIZE]; 
	TYPE *store_heap;
	int c;
	int n;
}; 
#define VEC struct NS(vec)

void NS(vec_init)    ( VEC * v );
void NS(vec_destroy) ( VEC * v );

bool NS(vec_append)  ( VEC * v, TYPE val);
bool NS(vec_remove)  ( VEC * v, int idx);

TYPE * NS(vec_ptr)  ( VEC * v );
int    NS(vec_size)    ( VEC * v );

#undef VEC

#endif

// ------------------------------------------------------------------------------------------
// DEFINITIONS SECTION
// ------------------------------------------------------------------------------------------


#ifdef TEMPLATE_DEF


#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VEC struct NS(vec)

void NS(vec_init)    ( VEC * v )
{
	memset(v,0,sizeof(VEC));
	v->c = SVSIZE;
}

void NS(vec_destroy) ( VEC * v )
{
    #ifdef SVDESTRUCTOR
    TYPE * p = NS(vec_ptr(v));
    for(int i = 0; i < NS(vec_size(v)); i++)
        SVDESTRUCTOR(p+i);
    #endif
	if(v->store_heap) SVFREE(v->store_heap);
	memset(v,0,sizeof(VEC));
}

bool NS(vec_append)  ( VEC * v, TYPE val)
{
	bool was_inline = v->store_heap == NULL;
	if(v->n == v->c)
	{
		TYPE * ptr = SVREALLOC(v->store_heap, sizeof(TYPE) * 2 * v->c);
		if(!ptr) return false;
		v->store_heap = ptr;
		v->c *= 2;
		
		if(was_inline)
			memcpy(v->store_heap, v->store_inline, sizeof(TYPE) * SVSIZE);
	}
	
	if(v->store_heap)
	{
		v->store_heap[v->n] = val;
		v->n++;
	}
	else
	{
		v->store_inline[v->n] = val;
		v->n++;
	}
	return true;
}

bool NS(vec_remove)  ( VEC * v, int idx)
{
	if(idx >= v->n || idx < 0) return false;
	
	TYPE * arr_start = v->store_heap ? v->store_heap : v->store_inline;
	
    #ifdef SVDESTRUCTOR
    SVDESTRUCTOR(arr_start+idx);
    #endif
    
	int nmov = v->n - idx - 1;
	if(nmov > 0)
		memmove(arr_start + idx, arr_start + idx + 1, sizeof(TYPE) * nmov);
	v->n--;
	return true;
}


TYPE * NS(vec_ptr)  ( VEC * v )
{
	return v->store_heap ? v->store_heap : v->store_inline;
}
int    NS(vec_size)    ( VEC * v )
{
	return v->n;
}



#undef VEC

#endif

// If this file (rather than the user) provided definitions for SVREALLOC and SVFREE,
// ... then undefine them. 
#ifdef MUSTUNDEF_MALLOCFREE
	#undef SVREALLOC
	#undef SVFREE
#endif

#ifdef SVDESTRUCTOR
#undef SVDESTRUCTOR
#endif

#undef TYPE
#undef PREFIX
#undef CCAT2
#undef CCAT
#undef NS
#undef TEMPLATE_DECL
#undef TEMPLATE_DEF
