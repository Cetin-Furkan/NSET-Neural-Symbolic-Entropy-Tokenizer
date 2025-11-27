/* * NSET v6.0 (Experimental) - The Analyzer
 * -------------------------------------------------------
 * Purpose: Detailed Debugging & Analysis
 * Unlike the production engine, this file prints WHY splits happen.
 * - Shows Entropy scores per split.
 * - Distinguishes between Structural splits (_) and Neural splits (Entropy).
 */

#include <tree_sitter/api.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Import shared logic
#include "../entropy.h"

// ==========================================
// 1. DATA STRUCTURES (V6 Standard)
// ==========================================
typedef struct {
    uint32_t root_id;
    uint32_t offset;
    uint16_t length;
    
    struct {
        uint16_t type       : 3; 
        uint16_t casing     : 2; 
        uint16_t pre_space  : 1; 
        uint16_t pre_break  : 1; 
        uint16_t has_joiner : 1; 
        uint16_t depth      : 3; 
        // Syntax Eater bits (kept for compatibility, though unused in pure scanner)
        uint16_t has_semi   : 1; 
        uint16_t has_comma  : 1; 
        uint16_t has_paren  : 1; 
        uint16_t has_star   : 1; 
        uint16_t has_close  : 1; 
    } meta;
} NSET_Token;

typedef struct {
    NSET_Token *tokens;
    size_t count;
    size_t capacity;
} Arena;

// ==========================================
// 2. HELPERS & MODEL
// ==========================================
EntropyModel global_model = {0};

uint32_t murmur_hash(const char *key, int len) {
    uint32_t h = 0x811c9dc5;
    for (int i=0; i<len; i++) { h ^= (uint8_t)tolower(key[i]); h *= 0x01000193; }
    return h;
}

uint8_t get_casing(const char *s, int len) {
    int caps=0;
    for(int i=0; i<len; i++) if(isupper(s[i])) caps++;
    if(caps==0) return 0;
    if(caps==len) return 2;
    if(caps==1 && isupper(s[0])) return 1;
    return 3;
}

void arena_push(Arena *a, NSET_Token t) {
    if (a->count >= a->capacity) return;
    a->tokens[a->count++] = t;
}

// ==========================================
// 3. SEEDING (Prior Knowledge)
// ==========================================
const char *SEED_VOCAB[] = {
    "include", "define", "ifndef", "endif", "return", "sizeof", "static", "inline",
    "struct", "typedef", "void", "char", "int", "float", "double", "long", "unsigned",
    "const", "signed", "short", "enum", "union", "volatile", "register", "extern",
    "auto", "bool", "complex", "imaginary", "restrict", "atomic",
    "goto", "break", "continue", "switch", "case", "default", "if", "else", "for",
    "do", "while", "printf", "fprintf", "sprintf", "snprintf", "scanf", "malloc",
    "calloc", "realloc", "free", "exit", "abort", "memcpy", "memset", "memmove",
    "strcpy", "strncpy", "strcat", "strlen", "strcmp", "strncmp", "strstr",
    "open", "close", "read", "write", "mmap", "munmap", "socket", "connect",
    "parser", "cursor", "node", "child", "sibling", "parent", "tree", "token"
};

void pretrain_model() {
    int vocab_size = sizeof(SEED_VOCAB) / sizeof(SEED_VOCAB[0]);
    // Train heavily to solidify these "ground truths"
    for (int n = 0; n < 50; n++) {
        for (int i = 0; i < vocab_size; i++) {
            model_train_sequence(&global_model, SEED_VOCAB[i], strlen(SEED_VOCAB[i]));
        }
    }
    printf(">> Model pre-trained with %d core C keywords.\n", vocab_size);
}

// ==========================================
// 4. THE ANALYTICAL TOKENIZER
// ==========================================
void subtokenize_identifier(Arena *arena, const char *text, int offset, int len, int depth, bool pre_space) {
    // Train online
    model_train_sequence(&global_model, text + offset, len);

    int start = 0;
    float threshold = 5.5f; // Slightly higher threshold for "Explainable" mode

    for (int i = 0; i < len - 1; i++) {
        uint8_t cur = (uint8_t)text[offset + i];
        uint8_t next = (uint8_t)text[offset + i + 1];

        // 1. Calculate metrics
        float surprise = calculate_surprise(&global_model, cur, next);
        bool is_underscore = (cur == '_');
        bool is_camel = (islower(cur) && isupper(next));
        
        bool split = false;
        
        // 2. Decision Logic
        if (is_underscore || is_camel) {
            split = true;
            // Debug Output
            printf("  [Struct Split]  '%.*s' -> Structurally forced\n", (i+1)-start, text + offset + start);
        } else if (surprise > threshold) {
            // Only split on entropy if the fragment isn't tiny
            int frag_len = (i + 1) - start;
            if (frag_len >= 3) {
                split = true;
                // Debug Output
                printf("  [Entropy Split] '%.*s' -> Surprise: %.2f (Threshold: %.1f)\n", 
                       frag_len, text + offset + start, surprise, threshold);
            }
        }

        // 3. Action
        if (split) {
            NSET_Token t = {0};
            t.root_id = murmur_hash(text + offset + start, (i+1)-start);
            t.offset = offset + start;
            t.length = (i+1) - start;
            t.meta.casing = get_casing(text + offset + start, t.length);
            t.meta.depth = depth;
            t.meta.pre_space = (start == 0) ? pre_space : 0;
            if (is_underscore && start > 0) t.meta.has_joiner = 1;

            arena_push(arena, t);
            start = i + 1;
        }
    }

    // Final chunk
    if (start < len) {
        NSET_Token t = {0};
        t.root_id = murmur_hash(text + offset + start, len - start);
        t.offset = offset + start;
        t.length = len - start;
        t.meta.casing = get_casing(text + offset + start, t.length);
        t.meta.depth = depth;
        t.meta.pre_space = (start == 0) ? pre_space : 0;
        
        arena_push(arena, t);
        printf("  [Final Token]   '%.*s'\n", t.length, text + offset + start);
    }
}

// ==========================================
// 5. MAIN
// ==========================================
extern const TSLanguage *tree_sitter_c();

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: ./scanner <file.c>\n");
        return 1;
    }

    pretrain_model();

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) { perror("Error opening file"); return 1; }
    
    struct stat sb; fstat(fd, &sb);
    const char *source_code = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());

    printf(">> Parsing structure of %s (%ld bytes)...\n", argv[1], sb.st_size);
    TSTree *tree = ts_parser_parse_string(parser, NULL, source_code, sb.st_size);
    TSNode root_node = ts_tree_root_node(tree);

    Arena arena;
    arena.tokens = malloc(sb.st_size * sizeof(NSET_Token));
    arena.count = 0; arena.capacity = sb.st_size;
    
    TSTreeCursor cursor = ts_tree_cursor_new(root_node);
    bool visited_children = false;
    int depth = 0;

    printf(">> Starting NSET Analysis Loop...\n\n");

    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (ts_node_child_count(node) == 0) {
            uint32_t start = ts_node_start_byte(node);
            uint32_t end = ts_node_end_byte(node);
            uint16_t len = end - start;
            const char *type = ts_node_type(node);
            bool pre_space = (start > 0 && isspace(source_code[start-1]));
            
            if (len > 0) {
                if (strstr(type, "identifier")) {
                    printf("Analyzed Identifier: %.*s\n", len, source_code + start); 
                    subtokenize_identifier(&arena, source_code, start, len, depth % 7, pre_space);
                } else {
                    // Standard Atomic Token
                    NSET_Token t = {0};
                    t.root_id = murmur_hash(source_code + start, len);
                    t.offset = start; t.length = len;
                    t.meta.depth = depth % 7;
                    t.meta.pre_space = pre_space;
                    if (isdigit(source_code[start])) t.meta.type = 2;
                    arena_push(&arena, t);
                }
            }
        }

        if (ts_tree_cursor_goto_first_child(&cursor)) { visited_children = false; depth++; }
        else if (ts_tree_cursor_goto_next_sibling(&cursor)) { visited_children = false; }
        else {
            do { if (!ts_tree_cursor_goto_parent(&cursor)) goto done; depth--; } 
            while (!ts_tree_cursor_goto_next_sibling(&cursor));
            visited_children = false;
        }
    }

done:
    printf("\n>> Analysis Complete.\n");
    printf(">> Total Tokens Generated: %lu\n", arena.count);
    
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    munmap((void*)source_code, sb.st_size);
    return 0;
}
