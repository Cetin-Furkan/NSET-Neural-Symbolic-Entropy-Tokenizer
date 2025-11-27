import struct
import os
import sys
import argparse
from collections import Counter

def read_registry(filename):
    """Generates tokens from the binary registry file."""
    if not os.path.exists(filename):
        print(f"Error: Registry file '{filename}' not found.")
        return

    with open(filename, "rb") as f:
        while True:
            # Format: [ID: 4 bytes][Len: 1 byte][Text: Len bytes]
            id_bytes = f.read(4)
            if not id_bytes: break
            
            token_id = struct.unpack("I", id_bytes)[0]
            len_bytes = f.read(1)
            if not len_bytes: break
            
            word_len = struct.unpack("B", len_bytes)[0]
            word_bytes = f.read(word_len)
            
            try:
                word = word_bytes.decode("utf-8")
            except UnicodeDecodeError:
                word = f"<BINARY_DATA_{word_bytes.hex()}>"
                
            yield token_id, word

def print_histogram(data, label="Length", buckets=10):
    if not data: return
    m = max(data)
    # Simple ASCII Histogram
    counts = Counter(data)
    sorted_keys = sorted(counts.keys())
    
    print(f"\n--- {label} Distribution ---")
    for k in sorted_keys[:buckets]:
        bar = "#" * int((counts[k] / len(data)) * 50)
        print(f"{k:>3}: {bar} ({counts[k]})")
    if len(sorted_keys) > buckets:
        print(f"... and {len(sorted_keys) - buckets} more tail buckets")

def inspect(filename):
    print(f"[*] Inspecting NSET Registry: {filename}")
    
    tokens = []
    lengths = []
    suspicious = []
    
    for tid, word in read_registry(filename):
        tokens.append(word)
        lengths.append(len(word))
        
        # Anomaly Detection
        if len(word) > 32:
            suspicious.append((tid, word, "Length > 32"))
        elif any(ord(c) < 32 and c not in ('\n', '\t', '\r') for c in word):
            suspicious.append((tid, word, "Control Char"))
        elif "\\x" in word or "<BINARY" in word:
            suspicious.append((tid, word, "Binary Artifact"))

    total = len(tokens)
    if total == 0:
        print("Registry is empty.")
        return

    avg_len = sum(lengths) / total
    
    print(f"\n[Statistics]")
    print(f"  Total Unique Tokens: {total:,}")
    print(f"  Average Length:      {avg_len:.2f} chars")
    print(f"  Max Length:          {max(lengths)} chars")
    print(f"  Min Length:          {min(lengths)} chars")

    print_histogram(lengths, label="Token Length", buckets=15)

    if suspicious:
        print(f"\n[!] Anomalies Detected ({len(suspicious)})")
        print("    ID      | Issue           | Token Sample")
        print("    " + "-"*50)
        for tid, word, issue in suspicious[:20]:
            safe_word = word.replace('\n', '\\n')[:25]
            print(f"    {tid:<8}| {issue:<16}| {safe_word}")
        if len(suspicious) > 20:
            print(f"    ... {len(suspicious)-20} more hidden.")
    else:
        print("\n[+] No obvious anomalies detected (clean vocabulary).")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="NSET Vocabulary Inspector")
    parser.add_argument("--file", default="nset_vocab.bin", help="Path to registry file")
    args = parser.parse_args()
    
    inspect(args.file)
