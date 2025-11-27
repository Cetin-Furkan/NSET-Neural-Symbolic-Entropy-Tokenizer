import os
import sys
import time
import argparse
from concurrent.futures import ThreadPoolExecutor

def scan_file(path):
    """Returns size of file if it's a valid C/H file."""
    try:
        if path.endswith(('.c', '.h', '.cpp', '.hpp', '.cc')):
            return os.path.getsize(path), path.split('.')[-1]
    except OSError:
        pass
    return 0, None

def scan_corpus(root_path):
    print(f"[*] Scanning Corpus at: {os.path.abspath(root_path)}")
    print("    (Using ThreadPool for parallel scanning...)")
    
    start_time = time.time()
    total_size = 0
    file_counts = {'c': 0, 'h': 0, 'cpp': 0, 'other': 0}
    file_list = []

    # 1. Walk tree to get file paths
    all_files = []
    for root, dirs, files in os.walk(root_path):
        # Skip hidden folders
        dirs[:] = [d for d in dirs if not d.startswith('.')]
        for f in files:
            all_files.append(os.path.join(root, f))

    # 2. Parallel Process
    with ThreadPoolExecutor() as executor:
        results = executor.map(scan_file, all_files)
        
        for size, ext in results:
            if size > 0:
                total_size += size
                if ext in ['c', 'cc']: file_counts['c'] += 1
                elif ext in ['h', 'hpp']: file_counts['h'] += 1
                elif ext == 'cpp': file_counts['cpp'] += 1
                else: file_counts['other'] += 1

    duration = time.time() - start_time
    
    print(f"\n[Corpus Summary]")
    print(f"  Scan Time:    {duration:.2f}s")
    print(f"  Total Size:   {total_size / (1024*1024):.2f} MB")
    print(f"  Total Bytes:  {total_size:,}")
    
    print(f"\n[File Types]")
    print(f"  .c / .cc:     {file_counts['c']:,} files")
    print(f"  .h / .hpp:    {file_counts['h']:,} files")
    print(f"  .cpp:         {file_counts['cpp']:,} files")
    
    return total_size

def analyze_density(vocab_file, corpus_size):
    if not os.path.exists(vocab_file):
        print("\n[!] Vocabulary file not found. Run the tokenizer first.")
        return

    vocab_size_bytes = os.path.getsize(vocab_file)
    # Estimate count based on file size (approx 5 bytes overhead + avg len 6) ~ 11 bytes per token
    # Better to read it exactly if fast enough, but estimation works for quick stats
    
    print(f"\n[Comparison]")
    print(f"  Vocab Size:   {vocab_size_bytes / 1024:.2f} KB")
    
    if corpus_size > 0:
        ratio = (vocab_size_bytes / corpus_size) * 100
        print(f"  Vocab/Corpus Ratio: {ratio:.4f}%")
        print("  (Lower is generally better for generalization, unless underfitting)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("path", nargs="?", default=".", help="Root directory to scan")
    parser.add_argument("--vocab", default="nset_vocab.bin", help="Path to vocab registry")
    args = parser.parse_args()
    
    c_size = scan_corpus(args.path)
    analyze_density(args.vocab, c_size)
