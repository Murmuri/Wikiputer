import struct
import zlib
import xml.etree.ElementTree as ET
import re
import os
import argparse
import bz2

# --- Configuration ---
# Minimum article length to include (compressed bytes approx)
MIN_ARTICLE_SIZE = 50 
# Skip redirection pages
SKIP_REDIRECTS = True

def extract_intro(text):
  
    if not text:
        return ""
    
    intro_end_patterns = [
        r'\n=+\s*[^=\n]+\s*=+\n',
        r'\n----\n',
        r'{{[Cc]lear}}',
        r'{{[Tt]OC}}',
        r'__TOC__',
        r'\n\|}'
    ]
    
    combined_pattern = '|'.join(intro_end_patterns)
    match = re.search(combined_pattern, text)
    
    if match:
        intro = text[:match.start()].strip()
    else:
        intro = text[:5000].strip()
    
    return intro

def clean_wiki_text(text, only_intro=False):
    if not text:
        return ""
    
    # Remove redirection tags
    if re.match(r'#REDIRECT', text, re.IGNORECASE):
        return None if SKIP_REDIRECTS else text
    
    if only_intro:
        text = extract_intro(text)

    # Remove [[File:...]]
    text = re.sub(r'\[\[File:.*?\]\]', '', text)
    # Remove [[Image:...]]
    text = re.sub(r'\[\[Image:.*?\]\]', '', text)
    # Remove references <ref>...</ref>
    text = re.sub(r'<ref.*?>.*?</ref>', '', text, flags=re.DOTALL)
    text = re.sub(r'<ref.*?>', '', text)
    
    # Simplify links [[Page|Text]] -> Text
    text = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]+)\]\]', r'\1', text)
    
    # Remove headers === ... ===
    text = re.sub(r'={2,}(.*?)={2,}', r'\n\1\n', text)
    
    # Remove templates {{...}} - primitive approach
    # Note: Recursive templates are hard with regex. This is a best-effort.
    text = re.sub(r'\{\{.*?\}\}', '', text, flags=re.DOTALL)
    
    # Basic cleanup
    text = text.replace("'''", "").replace("''", "")
    text = re.sub(r'\n{3,}', '\n\n', text).strip()
    
    return text

def convert_xml_dump(xml_file, output_dir, only_intro=False):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    index_path = os.path.join(output_dir, "wiki.idx")
    data_path = os.path.join(output_dir, "wiki.dat")

    print(f"Converting {xml_file}...")
    print(f"Mode: {'Only introductions' if only_intro else 'Full articles'}")
    
    articles_processed = 0
    offset = 0
    
    index_entries = []

    # Open input file (handle BZ2 or plain)
    if xml_file.endswith('.bz2'):
        source = bz2.open(xml_file, 'rb')
    else:
        source = open(xml_file, 'rb')

    # Configuration for file splitting
    MAX_FILE_SIZE = 2 * 1024 * 1024 * 1024 # 2GB limit to be safe for FAT32 and signed 32-bit seek
    
    current_dat_file = None
    current_file_index = 0
    current_file_size = 0
    
    def open_next_dat_file():
        nonlocal current_dat_file, current_file_index, current_file_size
        if current_dat_file:
            current_dat_file.close()
        
        filename = f"wiki.dat.{current_file_index:03d}"
        path = os.path.join(output_dir, filename)
        current_dat_file = open(path, "wb")
        current_file_size = 0
        print(f"Started new data file: {filename}")
        current_file_index += 1

    open_next_dat_file()

    try:
        # Stream Parsing XML
        context = ET.iterparse(source, events=("end",))
        
        title = None
        
        for event, elem in context:
            tag = elem.tag.split('}')[-1] 
            
            if tag == 'title':
                title = elem.text
            elif tag == 'text':
                raw_text = elem.text
                if title and raw_text:
                    clean_text = clean_wiki_text(raw_text, only_intro)
        
                    if clean_text and len(clean_text) > MIN_ARTICLE_SIZE:
                        # Compress
                        compressed = zlib.compress(clean_text.encode('utf-8'))
                        length = len(compressed)
                        
                        # Check file size limit
                        if current_file_size + length > MAX_FILE_SIZE:
                            open_next_dat_file()
                            
                        # Write
                        current_dat_file.write(compressed)
                        
                        # Global offset logic? 
                        # Option A: Index stores (FileIndex, LocalOffset) - Changes Index Format (12 bytes vs 8 bytes)
                        # Option B: Index stores Global Offset. WikiEngine calculates FileIndex = Offset / MAX_FILE_SIZE.
                        # Option B requires fixed file size chunks?
                        # If we split exactly when full, files are variable size (close to MAX).
                        # We cannot use Global Offset if sizes vary unless we track them.
                        # Simplest: Index stores Global 64-bit offset.
                        # BUT we must enforce PADDING to align to fixed boundary? 
                        # Or: Just change Index to store File Index (2 bytes) + Local Offset (4 bytes)?
                        # "offset" field in struct is 8 bytes (64 bit).
                        # We can repack it: High 2 bytes = File Index, Low 6 bytes = Offset? 
                        # Or specific bits.
                        # Let's use: Offset field (64-bit) = (FileIndex << 32) | LocalOffset.
                        # This works if LocalOffset < 4GB. MAX_FILE_SIZE = 2GB fits in 32 bits.
                        # So GlobalOffset is logical.
                        
                        packed_offset = ((current_file_index - 1) << 32) | current_file_size
                        
                        # Store in index
                        index_entries.append((title, packed_offset, length))
                        
                        current_file_size += length
                        articles_processed += 1
                        
                        if articles_processed % 1000 == 0:
                            print(f"Processed {articles_processed} articles...")
            
            if tag == 'page':
                elem.clear() # clear memory

    finally:
        source.close()
        if current_dat_file:
            current_dat_file.close()

    print("Sorting index...")
    index_entries.sort(key=lambda x: x[0])

    print("Writing index...")
    # Fixed record size: 64 bytes
    RECORD_SIZE = 64
    TITLE_LIMIT = 52

    with open(index_path, "wb") as f_idx:
        for title, off, length in index_entries:
            # Enforce title limit
            title_bytes = title.encode('utf-8')[:TITLE_LIMIT-1] 
            
            packed = struct.pack(f'<{TITLE_LIMIT}sQI', title_bytes, off, length)
            f_idx.write(packed)

    print(f"Done! Processed {articles_processed} articles.")
    print(f"Files created in {output_dir}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert MediaWiki XML dump to fast-search format")
    parser.add_argument("input", help="Input XML file (.xml or .xml.bz2)")
    parser.add_argument("--out", default="data", help="Output directory")
    parser.add_argument("--intro", action="store_true", 
                       help="Extract only introduction (first section) of each article")
    args = parser.parse_args()
    
    convert_xml_dump(args.input, args.out, args.intro)