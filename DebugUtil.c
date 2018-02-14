//
//  ByteOrder.h
//
//  Author: Filippin luca
//  luca.filippin@gmail.com
//

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "DebugUtil.h"

#define HEXDGT "0123456789ABCDEF"

/*
    Three possible output mode:

    hex only:    {XX XX XX XX XX...}
    text only:   {TTTTTTTTTT...}
    hex & text:  {XX XX XX... | TTT}
    
    If n_chars_break > 0, it's the upper limit to the output lines length. When the output doesn't fit
    in, a newline is started.
 
    If size > 0 and buf is NULL, the output buffer of size bytes is allocated dynamically.
    If size = 0 then a buffer as large as needed to contain the whole output is allocated dynamically.
    The function returns NULL in case of error.
*/


char *hex_dump(unsigned char *data, int len, char *buf, int size, int n_chars_break, eHexDumpMode mode, int *n_printed_chars) {
    int n_chars_since_break, n_chars_text = 0, n_chars_split = 0, n_chars_tail = 0;
    int i, j, k, n = 0;
    
    if (size < 0 || data == NULL || len <= 0) 
        return NULL;
    
	if (size > 0)
    	n_chars_break = n_chars_break > 0 ? (n_chars_break < size ? n_chars_break : size - 1) : size - 1;    
    
	switch (mode) {
            
    	case eHexDumpMode_hex_and_text: // ex: {XX XX XX XX | TTTT} 
        	n_chars_text = (n_chars_break - 2) / 4;
        	if (n_chars_text <= 0)
        	    return NULL;
			n_chars_text =  n_chars_text > len ? len : n_chars_text;
        	n_chars_break = 4 * n_chars_text + 2;
        	n_chars_split = 3 * n_chars_text - 1;
			if (len % n_chars_text) {
				n_chars_tail  =  3 * (n_chars_text - len % n_chars_text);
				size = size == 0 ? (n_chars_break + 1) * (len / n_chars_text) + n_chars_split + 2 + (len % n_chars_text) + 1 : size;
			} else {
				n_chars_tail = 0;
				size = size == 0 ? (n_chars_break + 1) * (len / n_chars_text) : size;
			}
    		break;
            
    	case eHexDumpMode_hex_only:  // {XX XX XX XX}
			n_chars_text = (n_chars_break - 2)/3 + 1;
        	n_chars_break = (n_chars_break - 2)/3 * 3;
        	if (n_chars_break <= 0)
            	return NULL;
			n_chars_break += 2;
			size = size == 0 ? ((n_chars_break + 1) * (len / n_chars_text) + (len % n_chars_text ? (len % n_chars_text) * 3 : 0)) : size;
			n_chars_text = 0;
    		break;
            
		case eHexDumpMode_text_only: // {TTTTTT}
			size = size == 0 ? len + (len / n_chars_break - 1) + 1 : size;
			break;
            
		default:
			return NULL;
	}
    
    if (buf == NULL)
        buf = malloc(size);
    
    if (buf == NULL)
        return NULL;
    
    if (mode != eHexDumpMode_text_only) {
        n_chars_since_break = 0;
		
        for(i = 0, n = 0; n < size - 1;) {
            
            if (n + 2 >= size)
				break;
            
            buf[n++] = HEXDGT[(data[i] >> 4) & 0x0F];
            buf[n++] = HEXDGT[(data[i] & 0x0F)];
			n_chars_since_break += 2;
			i++;
            
			if (i < len) {
				
				if (n_chars_break - n_chars_since_break > 0 && n_chars_split - n_chars_since_break != 0) {
            		if (n + 3 >= size || (n + 1 >= size && n_chars_split == 0))
            			break;
               		buf[n++] = ' ';
					n_chars_since_break++;
				}
                
			} 
            
			if (n_chars_split > 0) {
                
				if (i == len && n_chars_tail > 0) {
					n_chars_text = len % n_chars_text;
					if (n + n_chars_tail + 4 >= size - 1)
						break;
					n_chars_since_break += n_chars_tail;
					while (n_chars_tail-- > 0)
						buf[n++] = ' ';
				}
                
				if (n_chars_since_break == n_chars_split) {
                	if (n + 4 >= size)
                    	break;
					buf[n++] = ' ';
                	buf[n++] = '|';
                	buf[n++] = ' ';
                	for (j = 0; j < n_chars_text && n < size - 1; j++)
                    	buf[n++] = isprint(data[j + i - n_chars_text]) ? data[j + i - n_chars_text] : '.';
					n_chars_since_break += j + 3;
            	}
                
			}
            
			if (i == len)
				break;
            
            if (n_chars_break - n_chars_since_break == 0) {
				if (n + 3 >= size)
                    break;
                buf[n++] = '\n';
                n_chars_since_break = 0;
            }
            
        }
    } else {
        for(i = 0, k = 0; i < len && n < size - 1; i++) {
            buf[n++] = isprint(data[i]) ? data[i] : '.';
            
			if ((n - k) % n_chars_break == 0 && i < len - 1) {
                if (n >= size - 1) 
                    break;
                buf[n++] = '\n';
				k++;
            }
        }
    }
    
    buf[n] = '\0';
    
    if (n_printed_chars != NULL)
        *n_printed_chars = n;
    
    return n > 0 ? buf : NULL;
}
