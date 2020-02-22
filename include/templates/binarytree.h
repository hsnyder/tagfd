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

This file provides a simple binary tree.

You can provide your own malloc and free implementations.
If you do not define BTMALLOC and BTFREE before including this file, 
the standard library ones will be used. 

You _must_ provide a comparison function for your values. 
It should take the form int cmp (TYPE * a, TYPE * b), 
and it shall return < 0 if a goes before b, 0 if they are equal, or > 0 otherwise. 
To provide this comparator function, define BTCMP
*/





#ifndef BTMALLOC
	#ifndef BTFREE
		#include <stdlib.h>
		
		#define BTMALLOC malloc
		#define BTFREE free
		
		#define MUSTUNDEF_BTMALLOCANDFREE
	#else
		#error If you provide BTMALLOC you must provide BTFREE
	#endif
#endif

#ifndef BTCMP
	#error You must provide a comparison function BTCMP. See binarytree.h comments for details.
#endif

// ------------------------------------------------------------------------------------------
// DECLARATIONS SECTION
// ------------------------------------------------------------------------------------------

#ifdef TEMPLATE_DECL



struct NS(binTree)
{ 
	TYPE data; 
	struct NS(binTree) *R, *L;
}; 

#define BTNODE struct NS(binTree)

// Note: can't double-insert.
void NS(binTree_insert) ( BTNODE ** tree, TYPE value);
TYPE * NS(binTree_search)(BTNODE * tree, TYPE value);

void NS(binTree_orderedTraverse)(BTNODE * tree, void (*callback)(TYPE*, void*), void* callbackParam);

void NS(binTree_clear)(BTNODE * tree);


#undef BTNODE

#endif

// ------------------------------------------------------------------------------------------
// DEFINITIONS SECTION
// ------------------------------------------------------------------------------------------


#ifdef TEMPLATE_DEF


#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#define BTNODE struct NS(binTree)

void NS(binTree_insert) ( BTNODE ** tree, TYPE value)
{
	BTNODE * tmp = NULL;
	if(!(*tree))
	{
		tmp = (BTNODE *) BTMALLOC(sizeof(BTNODE));
		tmp->L = tmp->R = NULL;
		tmp->data = value;
		*tree = tmp;
		return;
	}
	
	if(BTCMP(&value, &(*tree)->data) < 0)
	{
		NS(binTree_insert) (&(*tree)->L, value);
	}
	else if(BTCMP(&value, &(*tree)->data) > 0)
	{
		NS(binTree_insert) (&(*tree)->R, value);
	}
}


void NS(binTree_orderedTraverse)(BTNODE * tree, void (*callback)(TYPE*, void*), void* callbackParam)
{
	if(!callback) return;
	if(tree)
	{
		NS(binTree_orderedTraverse) (tree->L, callback, callbackParam);
		callback(&tree->data, callbackParam);
		NS(binTree_orderedTraverse) (tree->R, callback, callbackParam);
	}
}

void NS(binTree_clear)(BTNODE * tree)
{
	if(tree)
	{
		NS(binTree_clear) (tree->L);
		NS(binTree_clear) (tree->R);
		BTFREE(tree);
	}
}

TYPE * NS(binTree_search)(BTNODE * tree, TYPE value)
{
	if(!tree) return NULL;
	
	if(BTCMP(&value, &tree->data) < 0)
	{
		return NS(binTree_search) (tree->L, value);
	}
	else if(BTCMP(&value, &tree->data) > 0)
	{
		return NS(binTree_search) (tree->R, value);
	}
	else
	{
		return &tree->data;
	}
}


#undef BTNODE


#endif

// If this file (rather than the user) provided definitions for BTMALLOC and BTFREE,
// ... then undefine them. 
#ifdef MUSTUNDEF_BTMALLOCANDFREE
	#undef BTMALLOC
	#undef BTFREE
#endif


#undef TYPE
#undef PREFIX
#undef CCAT2
#undef CCAT
#undef NS
#undef TEMPLATE_DECL
#undef TEMPLATE_DEF
