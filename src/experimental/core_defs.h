/* * NSET v6.0 - Core Definitions
 * -------------------------------------------------------
 * Shared Data Structures for the NSET Ecosystem
 */

#ifndef NSET_CORE_DEFS_H
#define NSET_CORE_DEFS_H

#include <stdint.h>
#include <stddef.h>

// ==========================================
// 1. THE ATOMIC TOKEN (V6 Standard)
// ==========================================
// Optimized for 8-byte alignment and bitfield compactness
typedef struct {
    uint32_t root_id;       // MurmurHash of the root semantic word
    uint32_t offset;        // Byte offset in the source file
    uint16_t length;        // Length of the token
    
    struct {
        uint16_t type       : 3; // 0=Word, 1=String, 2=Num, ...
        uint16_t casing     : 2; // 00=lower, 01=Cap, 10=All, 11=Camel
        uint16_t pre_space  : 1; // Preceding whitespace
        uint16_t pre_break  : 1; // Preceding newline
        uint16_t has_joiner : 1; // Contains underscore
        uint16_t depth      : 3; // AST Depth (0-7)
        
        // Syntax Eater Attributes
        uint16_t has_semi   : 1; 
        uint16_t has_comma  : 1; 
        uint16_t has_paren  : 1; 
        uint16_t has_star   : 1; 
        uint16_t has_close  : 1; 
    } meta;
} NSET_Token;

// ==========================================
// 2. ARENA STRUCTURE
// ==========================================
typedef struct {
    NSET_Token *tokens;
    size_t count;
    size_t capacity;
} Arena;

#endif // NSET_CORE_DEFS_H
