/* * NSET v6.0 - Persistent Memory & Macro Buster
 * -------------------------------------------------------
 * 1. Loads existing vocab at startup (Fixes Duplicates)
 * 2. Splits Macros/Preproc definitions (Fixes Blobs)
 * 3. Length Guard: Forces split on anything > 32 chars
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

// Professional Includes
#include "entropy.h"

// Compile via Makefile

// ==========================================
// DATA STRUCTURES
// ==========================================
// Note: This differs slightly from core.h because we need 'offset'
// for runtime tracking within the memory-mapped file.
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
// REGISTRY (PERSISTENT MEMORY)
// ==========================================
#define SEEN_TABLE_SIZE 4194304 // 4M entries
uint32_t *seen_hashes = NULL;
FILE *vocab_file = NULL;

void init_registry() {
    seen_hashes = calloc(SEEN_TABLE_SIZE, sizeof(uint32_t));
}

void load_registry() {
    FILE *f = fopen("nset_vocab.bin", "rb");
    if (!f) return; 

    printf(">> Loading existing vocabulary into RAM...\n");
    uint32_t id;
    uint8_t len;
    
    while (fread(&id, sizeof(uint32_t), 1, f) == 1) {
        if (fread(&len, sizeof(uint8_t), 1, f) != 1) break;
        fseek(f, len, SEEK_CUR); 
        
        uint32_t idx = id % SEEN_TABLE_SIZE;
        while (seen_hashes[idx] != 0) idx = (idx + 1) % SEEN_TABLE_SIZE;
        seen_hashes[idx] = id;
    }
    fclose(f);
}

bool has_seen_id(uint32_t id) {
    uint32_t idx = id % SEEN_TABLE_SIZE;
    while (seen_hashes[idx] != 0) {
        if (seen_hashes[idx] == id) return true;
        idx = (idx + 1) % SEEN_TABLE_SIZE;
    }
    return false;
}

void register_token(uint32_t id, const char *text, int len) {
    if (!has_seen_id(id)) {
        uint32_t idx = id % SEEN_TABLE_SIZE;
        while (seen_hashes[idx] != 0) idx = (idx + 1) % SEEN_TABLE_SIZE;
        seen_hashes[idx] = id;
        
        if (vocab_file) {
            uint8_t l = (len > 255) ? 255 : (uint8_t)len;
            fwrite(&id, sizeof(uint32_t), 1, vocab_file);
            fwrite(&l, sizeof(uint8_t), 1, vocab_file);
            fwrite(text, 1, l, vocab_file);
        }
    }
}

// ==========================================
// HELPERS
// ==========================================
// Global Statistical Model (Defined in entropy.h struct)
EntropyModel global_model = {0};

const char *LOCKED_VOCAB[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", 
    "double", "else", "enum", "extern", "float", "for", "goto", "if", "int", 
    "long", "register", "return", "short", "signed", "sizeof", "static", "struct", 
    "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
    "define", "include", "ifdef", "ifndef", "endif",
    "printf", "malloc", "free", "size_t", "uint32_t", "uint8_t", "uint16_t",
    "NULL", "true", "false", "bool", "file", "path", "buffer", "length",
    "count", "offset", "data", "node", "tree", "parser", "cursor", "root"
};

int vocab_cmp(const void *a, const void *b) {
    return strcmp((const char*)a, *(const char**)b);
}

bool is_word_locked(const char *str, int len) {
    char buffer[64];
    if (len >= 64) return false;
    for(int i=0; i<len; i++) buffer[i] = tolower(str[i]);
    buffer[len] = '\0';
    const char *key = buffer;
    return bsearch(key, LOCKED_VOCAB, sizeof(LOCKED_VOCAB)/sizeof(char*), sizeof(char*), vocab_cmp) != NULL;
}

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

void arena_push(Arena *a, NSET_Token t, const char *code, size_t total_size) {
    if (a->count >= a->capacity) return;
    uint32_t next_pos = t.offset + t.length;
    while (next_pos < total_size && isspace(code[next_pos])) next_pos++;

    if (next_pos < total_size) {
        char next_char = code[next_pos];
        if (next_char == ';') t.meta.has_semi = 1;
        else if (next_char == ',') t.meta.has_comma = 1;
        else if (next_char == '(') t.meta.has_paren = 1;
        else if (next_char == ')') t.meta.has_close = 1;
        else if (next_char == '*') t.meta.has_star = 1;
    }
    register_token(t.root_id, code + t.offset, t.length);
    a->tokens[a->count++] = t;
}

// ==========================================
// IDENTIFIER PROCESSOR
// ==========================================
void process_identifier(Arena *arena, const char *src, int offset, int len, int depth, bool pre_space, size_t file_size) {
    // 1. Check Locks
    if (is_word_locked(src + offset, len)) {
        NSET_Token t = {0};
        t.root_id = murmur_hash(src + offset, len);
        t.offset = offset; t.length = len;
        t.meta.depth = depth; t.meta.pre_space = pre_space;
        arena_push(arena, t, src, file_size);
        
        // Train the model on this locked word so it learns "this is normal"
        model_train_sequence(&global_model, src + offset, len);
        return;
    }

    // 2. Train on current word
    model_train_sequence(&global_model, src + offset, len);

    // 3. Splitter Logic
    int start = 0;
    float entropy_threshold = 5.0f; 
    int tokens_emitted = 0;

    for (int i = 0; i < len; i++) {
        uint8_t cur = (uint8_t)src[offset + i];
        
        // A. Hard Split: Underscore
        if (cur == '_') {
            if (i > start) {
                NSET_Token t = {0};
                t.root_id = murmur_hash(src + offset + start, i-start);
                t.offset = offset + start; t.length = i-start;
                t.meta.casing = get_casing(src + offset + start, i-start);
                t.meta.depth = depth;
                t.meta.pre_space = (tokens_emitted == 0) ? pre_space : 0;
                arena_push(arena, t, src, file_size);
                tokens_emitted++;
            }
            // Mark previous token as having joiner
            if (tokens_emitted > 0) arena->tokens[arena->count - 1].meta.has_joiner = 1;
            start = i + 1;
            continue;
        }

        // B. Soft Split: Entropy or CamelCase
        if (i < len - 1) {
            uint8_t next = (uint8_t)src[offset + i + 1];
            bool split = false;
            
            // CamelCase Check
            if (islower(cur) && isupper(next)) split = true;
            // Entropy Check (Renamed from get_entropy to match header)
            else if (calculate_surprise(&global_model, cur, next) > entropy_threshold) {
                int left_len = (i + 1) - start;
                int right_len = len - (i + 1);
                
                // Safety: Don't split if it breaks a locked word or creates tiny fragments
                if (is_word_locked(src + offset + start, left_len)) split = true;
                else if (left_len >= 4 && right_len >= 3) split = true;
            }

            if (split) {
                NSET_Token t = {0};
                t.root_id = murmur_hash(src + offset + start, (i+1)-start);
                t.offset = offset + start; t.length = (i+1)-start;
                t.meta.casing = get_casing(src + offset + start, (i+1)-start);
                t.meta.depth = depth;
                t.meta.pre_space = (tokens_emitted == 0) ? pre_space : 0;
                arena_push(arena, t, src, file_size);
                tokens_emitted++;
                start = i + 1;
            }
        }
    }

    if (start < len) {
        NSET_Token t = {0};
        t.root_id = murmur_hash(src + offset + start, len-start);
        t.offset = offset + start; t.length = len-start;
        t.meta.casing = get_casing(src + offset + start, len-start);
        t.meta.depth = depth;
        t.meta.pre_space = (tokens_emitted == 0) ? pre_space : 0;
        arena_push(arena, t, src, file_size);
    }
}

// ==========================================
// MAIN LOOP
// ==========================================
extern const TSLanguage *tree_sitter_c();

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <file.c>\n", argv[0]);
        return 1;
    }

    init_registry();
    load_registry();

    vocab_file = fopen("nset_vocab.bin", "ab");
    if (!vocab_file) return 1;

    // Pre-Train
    int vocab_size = sizeof(LOCKED_VOCAB)/sizeof(char*);
    for(int n=0; n<20; n++) for(int i=0; i<vocab_size; i++) 
        model_train_sequence(&global_model, LOCKED_VOCAB[i], strlen(LOCKED_VOCAB[i]));

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("Error opening file");
        return 1;
    }
    struct stat sb; fstat(fd, &sb);
    const char *code = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c());
    TSTree *tree = ts_parser_parse_string(parser, NULL, code, sb.st_size);
    TSNode root = ts_tree_root_node(tree);

    Arena arena;
    arena.tokens = malloc(sb.st_size * sizeof(NSET_Token));
    arena.count = 0; arena.capacity = sb.st_size;

    TSTreeCursor cursor = ts_tree_cursor_new(root);
    bool visited_children = false;
    int depth = 0;

    for (;;) {
        TSNode node = ts_tree_cursor_current_node(&cursor);
        if (ts_node_child_count(node) == 0) {
            uint32_t start = ts_node_start_byte(node);
            uint32_t end = ts_node_end_byte(node);
            uint16_t len = end - start;
            const char *type = ts_node_type(node);
            bool pre_space = (start > 0 && isspace(code[start-1]) && code[start-1]!='\n');
            bool pre_break = (start > 0 && code[start-1] == '\n');
            
            if (len > 0) {
                bool already_eaten = false;
                if (arena.count > 0) {
                    NSET_Token *prev = &arena.tokens[arena.count-1];
                    char first_char = code[start];
                    if (first_char == ';' && prev->meta.has_semi) already_eaten = true;
                    if (first_char == ',' && prev->meta.has_comma) already_eaten = true;
                    if (first_char == '(' && prev->meta.has_paren) already_eaten = true;
                    if (first_char == ')' && prev->meta.has_close) already_eaten = true;
                    if (first_char == '*' && prev->meta.has_star) already_eaten = true;
                }

                if (!already_eaten) {
                    bool is_preproc = (strncmp(type, "preproc", 7) == 0);
                    bool is_macro_blob = (len > 32 && !is_word_locked(code+start, len));

                    if (strstr(type, "identifier")) {
                         process_identifier(&arena, code, start, len, depth%7, pre_space, sb.st_size);
                    }
                    else if (strcmp(type, "comment") == 0 || strcmp(type, "string_literal") == 0 || is_preproc || is_macro_blob) {
                        int sub_start = 0;
                        for(int i=0; i<len; i++) {
                            char c = code[start + i];
                            if (isspace(c) || ispunct(c)) {
                                if (i > sub_start) {
                                    int sub_len = i - sub_start;
                                    NSET_Token t = {0};
                                    t.root_id = murmur_hash(code + start + sub_start, sub_len);
                                    t.offset = start + sub_start; t.length = sub_len;
                                    t.meta.depth = depth%7;
                                    t.meta.type = 1; 
                                    arena_push(&arena, t, code, sb.st_size);
                                }
                                sub_start = i + 1;
                            }
                        }
                        if (sub_start < len) {
                            NSET_Token t = {0};
                            t.root_id = murmur_hash(code + start + sub_start, len - sub_start);
                            t.offset = start + sub_start; t.length = len - sub_start;
                            t.meta.type = 1;
                            arena_push(&arena, t, code, sb.st_size);
                        }
                    }
                    else {
                        NSET_Token t = {0};
                        t.root_id = murmur_hash(code + start, len);
                        t.offset = start; t.length = len;
                        t.meta.depth = depth%7; 
                        t.meta.pre_space = pre_space;
                        t.meta.pre_break = pre_break;
                        if (isdigit(code[start])) t.meta.type = 2;
                        arena_push(&arena, t, code, sb.st_size);
                    }
                }
            }
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) { visited_children=false; depth++; }
        else if (ts_tree_cursor_goto_next_sibling(&cursor)) { visited_children=false; }
        else { 
            do { if (!ts_tree_cursor_goto_parent(&cursor)) goto done; depth--; } 
            while (!ts_tree_cursor_goto_next_sibling(&cursor)); 
            visited_children=false; 
        }
    }

done:
    if (vocab_file) fclose(vocab_file);
    free(seen_hashes);
    free(arena.tokens);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    munmap((void*)code, sb.st_size);
    printf(">> Tokenization Complete.\n");
    return 0;
}
