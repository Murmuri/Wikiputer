import struct
import sys
import os

# Configuration matches WikiEngine.h
# struct WikiIndexEntry {
#     char title[52];
#     uint64_t offset;
#     uint32_t length;
# };
RECORD_SIZE = 64
TITLE_LIMIT = 52

def trim_index(index_path, max_dat_index, output_path):
    print(f"Trimming index {index_path}...")
    print(f"Retaining articles for wiki.dat.000 to wiki.dat.{max_dat_index:03d}")
    
    kept_count = 0
    total_count = 0
    
    try:
        with open(index_path, "rb") as f_in, open(output_path, "wb") as f_out:
            while True:
                chunk = f_in.read(RECORD_SIZE)
                if not chunk or len(chunk) < RECORD_SIZE:
                    break
                
                total_count += 1
                
                # Unpack offset (at 52 bytes, 8 bytes long)
                # Format: 52s Q I (52 chars, unsigned long long, unsigned int)
                # But we only need offset at 52
                offset = struct.unpack_from("<Q", chunk, 52)[0]
                
                # Decode file index (High 32 bits)
                file_index = (offset >> 32) & 0xFFFFFFFF
                
                if file_index <= max_dat_index:
                    f_out.write(chunk)
                    kept_count += 1
                
                if total_count % 100000 == 0:
                    print(f"Processed {total_count} entries... (Kept {kept_count})", end='\r')
                    
        print(f"\nDone! Created {output_path}")
        print(f"Total entries: {total_count}")
        print(f"Kept entries:  {kept_count}")
        
    except FileNotFoundError:
        print(f"Error: Could not open {index_path}")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python trim_index.py <path_to_wiki.idx> <max_file_index>")
        print("Example: python trim_index.py data/wiki.idx 3")
        print("  (This keeps articles for wiki.dat.000, .001, .002, and .003)")
        sys.exit(1)
        
    idx_path = sys.argv[1]
    max_idx = int(sys.argv[2])
    
    # Determine output path
    dir_name = os.path.dirname(idx_path)
    out_path = os.path.join(dir_name, "wiki_partial.idx")
    
    trim_index(idx_path, max_idx, out_path)
