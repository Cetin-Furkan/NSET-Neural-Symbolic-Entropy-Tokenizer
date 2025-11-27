# ==========================================
# NSET Build System
# ==========================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -O3 -march=native
LDFLAGS = -ltree-sitter -ltree-sitter-c -lm

# Directories
SRC_DIR = src
BUILD_DIR = build
EXP_DIR = src/experimental

# Targets
TARGET_MAIN = $(BUILD_DIR)/nset
TARGET_SCANNER = $(BUILD_DIR)/scanner
TARGET_ADVANCED = $(BUILD_DIR)/advanced

# Source Files
SRC_MAIN = $(SRC_DIR)/main.c
SRC_SCANNER = $(EXP_DIR)/scanner.c
SRC_ADVANCED = $(EXP_DIR)/advanced.c

.PHONY: all clean debug folders scanner advanced

# Default Target: Build everything
all: folders $(TARGET_MAIN)

# Main Production Build
$(TARGET_MAIN): $(SRC_MAIN)
	@echo "Compiling NSET Main Engine..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)
	@echo ">> Built: $@"

# Experimental Scanner Build
scanner: folders $(SRC_SCANNER)
	@echo "Compiling NSET Debug Scanner..."
	$(CC) $(CFLAGS) $(SRC_SCANNER) -o $(TARGET_SCANNER) $(LDFLAGS)
	@echo ">> Built: $(TARGET_SCANNER)"

# Experimental Advanced Build
advanced: folders $(SRC_ADVANCED)
	@echo "Compiling NSET Advanced (Experimental)..."
	$(CC) $(CFLAGS) $(SRC_ADVANCED) -o $(TARGET_ADVANCED) $(LDFLAGS)
	@echo ">> Built: $(TARGET_ADVANCED)"

# Ensure build directory exists
folders:
	@mkdir -p $(BUILD_DIR)

# Clean up build artifacts and local registry
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f nset_vocab.bin

# Debug build with symbols (O0)
debug: CFLAGS = -Wall -Wextra -std=c11 -g -O0
debug: all
	@echo ">> Debug build complete."
