
````markdown
# NSET: Neural-Symbolic Entropy Tokenizer

```text
  _   _  _____  _____  _____ 
 | \ | |/ ____||  ___||_   _|
 |  \| | (___  | |__    | |  
 | . ` |\___ \ |  __|   | |  
 | |\  |____) || |___   | |  
 |_| \_|_____/ |_____|  |_|  
                             
 v6.0 | Persistent Memory | Macro Buster
````

**NSET** is a next-generation tokenizer designed specifically for **Code LLMs**.

Standard tokenizers (BPE, WordPiece) treat code as unstructured text, greedily merging characters based on frequency. This destroys semantic boundaries. NSET replaces greedy merging with **Syntactic Ground Truth** (via Tree-sitter) and **Rényi Entropy Segmentation** to create a vocabulary that aligns with the logical structure of software.

-----

## ⚡ Why NSET?

### The Problem with BPE

Byte Pair Encoding (BPE) is blind to syntax. It frequently creates "garbage tokens" that bridge distinct logical units.

  * *BPE sees:* `int count = 0;` $\rightarrow$ `["int", " count", " =", " 0", ";"]`
  * *BPE sees:* `my_variable` $\rightarrow$ `["my", "_var", "iable"]` (Arbitrary splits based on training data)

### The NSET Solution

NSET uses a hybrid **Neural-Symbolic** approach:

1.  **Symbolic Layer**: Uses `tree-sitter` to parse the Abstract Syntax Tree (AST). It knows `int` is a type and `count` is an identifier.
2.  **Entropy Layer**: Calculates the local "surprise" (Entropy) of character transitions within identifiers. It only splits when the statistical surprise exceeds a dynamic threshold.

| Feature | Standard BPE / Tiktoken | NSET (v6.0) |
| :--- | :--- | :--- |
| **Awareness** | Purely Statistical (Frequency) | **Syntactic (AST) + Statistical** |
| **Identifier Splitting** | Greedy Merging | **Entropy-Guided Segmentation** |
| **Token Size** | Variable (String/Int map) | **Fixed 8-Byte Atomic Struct** |
| **Symbols** | Separate Tokens (e.g., `;`, `{`, `}`) | **Embedded Metadata Attributes** |
| **Memory Strategy** | `malloc` per node | **Arena Allocation (Zero-Copy)** |

-----

## 🏗️ Architecture

### 1\. The 8-Byte Atomic Token

NSET abandons the traditional "string to ID" map for a hyper-optimized **Atomic Token**. Every token fits exactly into a 64-bit register, allowing for cache-oblivious processing and massive throughput.

```c
typedef struct {
    uint32_t root_id;       // MurmurHash of the root semantic word
    struct {
        uint8_t casing    : 2; // 00=lower, 01=Capitalized, ...
        uint8_t pre_space : 1; // Preceding whitespace flag
        uint8_t is_symbol : 1; // Is this syntax?
        uint8_t depth     : 3; // AST Scope Depth (0-7)
    } meta;
    uint16_t length;        // Length for reconstruction
} NSET_Token;
```

### 2\. Symbol Absorption

NSET does not waste context window space on punctuation. Common syntax markers like `;`, `,`, `(`, `)`, and `*` are **absorbed** into the metadata of the preceding token.

  * **Legacy**: `func`, `(`, `arg`, `)` $\rightarrow$ 4 Tokens
  * **NSET**: `func` (has\_paren=1), `arg` (has\_close=1) $\rightarrow$ **2 Tokens**

### 3\. The "Macro Buster"

C Preprocessor definitions (`#define`, `#ifdef`) often create massive, unstructured text blobs in standard datasets. NSET v6.0 detects these macro blocks and applies a granular splitting strategy to prevent vocabulary pollution.

-----

## 🚀 Quick Start

### Prerequisites

  * GCC or Clang
  * `tree-sitter` library installed (`libtree-sitter`, `libtree-sitter-c`)

### Building

NSET uses a unified `Makefile` for compilation.

```bash
# Clone the repository
git clone [https://github.com/yourusername/nset.git](https://github.com/yourusername/nset.git)
cd nset

# Build the optimized release binary
make

# Output will be in ./build/nset
```

### Running the Tokenizer

Run NSET on any C source file. It will automatically initialize the persistent vocabulary registry (`nset_vocab.bin`) if it doesn't exist.

```bash
./build/nset src/main.c
```

**Output:**

```text
>> Loading existing vocabulary into RAM...
>> Parsing structure of src/main.c (12044 bytes)...
>> Starting NSET Tokenization...
  [Entropy Split] 'net' (Surprise: 6.2)
  [Entropy Split] 'work' (Surprise: 0.1)
  [Final Token]   'manager'
>> Done. Generated 2405 tokens.
```

-----

## 📊 Tools & Analysis

The `tools/` directory contains Python scripts to audit your tokenizer's performance.

### Vocabulary Inspector

Checks the binary registry for "garbage" tokens (excessively long strings or noise) that often plague BPE models.

```bash
python3 tools/inspector.py
```

### Corpus Statistics

Simulates a training run, scanning your C codebase to calculate compression ratios and vocabulary density.

```bash
python3 tools/corpus_stat.py ~/my_c_projects/
```

-----

## 🧠 The Entropy Algorithm

NSET calculates the **Rényi Entropy** ($\alpha=2$) for character pairs.

$$ H_2(X) = -\log_2 \left( \sum_{i=1}^{n} p_i^2 \right) $$

In the context of a string like `contextswitch`:

1.  The model sees `context`. The transition `t` $\to$ `s` is statistically rare (high surprise) compared to `t` $\to$ `e` (as in `context`).
2.  The entropy spikes above the threshold ($5.0$).
3.  NSET cuts the token: `context` | `switch`.

This ensures splits happen at **semantic boundaries**, not just frequency boundaries.

-----

## 🤝 Contributing

We are looking for contributors to expand language support beyond C.

1.  Fork the repo.
2.  Add a new Tree-sitter grammar in `src/`.
3.  Submit a Pull Request.

## 📄 License

MIT License. See `LICENSE` for details.

```
```
