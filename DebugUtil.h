/*
 *  DebugUtils.h
 *  PsyScopeX
 *
 *  Created by lucaf on 07/03/2009.
 *  Copyright 2009Language, Cognition and  Development Laboratory, Sissa, Trieste. All rights reserved. All rights reserved.
 *
 */

#ifndef __DEBUG_UTIL_H__

    #define __DEBUG_UTIL_H__
    #include <stdio.h>

    #define xprintf(file, context, format, ...) do { \
        fprintf(file, "[%s:%s(%d)] " format, context, __func__, __LINE__, __VA_ARGS__); \
        fflush(file); \
    } while(0)

    #define eprintf(context, format, ...) xprintf(stderr, context, format, __VA_ARGS__)
    #define oprintf(context, format, ...) xprintf(stdout, context, format, __VA_ARGS__)

    #if DEBUG_SWITCH

        #define DEBUG_DECLARE(x) x

        #define DEBUG_OPEN(l) while (l & (DEBUG_SWITCH)) { 
        #define DEBUG_CLOSE   break; }

        #define DEBUG_LEVEL(l, x) \
            DEBUG_OPEN(l) \
            x; \
            DEBUG_CLOSE

    #else
        #define DEBUG_DECLARE(x)
        #define DEBUG_LEVEL(l, x)
    #endif

    typedef enum {
        eHexDumpMode_hex_only,
        eHexDumpMode_text_only,
        eHexDumpMode_hex_and_text
    } eHexDumpMode;

    char *hex_dump(unsigned char *data, int len, char *buf, int size, int n_chars_break, eHexDumpMode mode, int *n_printed_chars);

#endif


/*  Example. In your C source you might define:
    
    [...]
    #define DEBUG_BASIC     0x1
    #define DEBUG_MEDIUM    0x2
    #define DEBUG_EXTD      0x4
    #define DEBUG_VERBOSE   0x8

    #define DEBUG_SWITCH    (DEBUG_BASIC + DEBUG_MEDIUM + DEBUG_EXTD)
    #include "DebugUtils.h"
    
    [...]
    DEBUG_DECLARE(long time = GetTime());
    DEBUG_LEVEL(DEBUG_MEDIUM, printf("TimeStamp(%d): %ld", __LINE__, time))
*/
    