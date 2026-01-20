import argparse
import zipfile
import libbbf
import sys
import os

def find_section_end(sections, page_count, current_idx, range_key):
    start_page = sections[current_idx]['startPage']
    
    # Iterate through subsequent sections
    for j in range(current_idx + 1, len(sections)):
        next_sec = sections[j]
        title = next_sec['title']
        
        if not range_key:
            if next_sec['startPage'] > start_page:
                return next_sec['startPage']
        else:
            if range_key in title:
                return next_sec['startPage']
                
    return page_count

def main():
    parser = argparse.ArgumentParser(description="Extract BBF to CBZ/Folder")
    parser.add_argument("input_bbf", help="Input .bbf file")
    parser.add_argument("--output", "-o", help="Output .cbz file (or directory if --dir)")
    parser.add_argument("--extract", action="store_true", help="Enable extraction mode (legacy flag support)")
    parser.add_argument("--dir", action="store_true", help="Extract to directory instead of CBZ")
    parser.add_argument("--section", help="Extract only specific section by name")
    parser.add_argument("--rangekey", help="String to match for end of range (e.g. 'Vol 2')")
    parser.add_argument("--verify", action="store_true", help="Verify integrity before extraction")
    parser.add_argument("--info", action="store_true", help="Show info and exit")
    
    args = parser.parse_args()
    
    reader = libbbf.BBFReader(args.input_bbf)
    if not reader.is_valid:
        print("Error: Invalid or corrupt BBF file.")
        sys.exit(1)

    # Info Mode
    if args.info:
        print(f"BBF Version: 1")
        print(f"Pages:  {reader.get_page_count()}")
        print(f"Assets: {reader.get_asset_count()}")
        print("\n[Sections]")
        secs = reader.get_sections()
        if not secs: print(" None.")
        for s in secs:
            print(f" - {s['title']:<20} (Start: {s['startPage']+1})")
        print("\n[Metadata]")
        for k, v in reader.get_metadata():
            print(f" - {k}: {v}")
        return

    # Verify Mode
    if args.verify:
        print("Verifying assets (XXH3)...")
        if reader.verify():
            print("Integrity OK.")
        else:
            print("Integrity Check FAILED.")
            sys.exit(1)
            
    # Extraction Mode
    if not args.output:
        print("Error: Output filename required (-o)")
        sys.exit(1)

    sections = reader.get_sections()
    total_pages = reader.get_page_count()
    
    start_idx = 0
    end_idx = total_pages
    
    # Calculate Range
    if args.section:
        found_sec_idx = -1
        for i, s in enumerate(sections):
            if s['title'] == args.section:
                found_sec_idx = i
                break
        
        if found_sec_idx == -1:
            print(f"Error: Section '{args.section}' not found.")
            sys.exit(1)
            
        start_idx = sections[found_sec_idx]['startPage']
        end_idx = find_section_end(sections, total_pages, found_sec_idx, args.rangekey)
        
        print(f"Extracting Section: '{args.section}' (Pages {start_idx+1}-{end_idx})")

    # Perform Extraction
    is_zip = not args.dir
    
    if is_zip:
        zf = zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED)
    else:
        if not os.path.exists(args.output):
            os.makedirs(args.output)

    pad_len = len(str(total_pages))
    
    count = 0
    for i in range(start_idx, end_idx):
        info = reader.get_page_info(i)
        data = reader.get_page_data(i)
        
        ext = ".png"
        if info['type'] == 1: ext = ".avif"
        elif info['type'] == 3: ext = ".jpg"
        
        fname = f"p{str(i+1).zfill(pad_len)}{ext}"
        
        if is_zip:
            zf.writestr(fname, data)
        else:
            with open(os.path.join(args.output, fname), 'wb') as f:
                f.write(data)
        
        count += 1
        if count % 10 == 0:
            print(f"\rExtracted {count} pages...", end="")

    if is_zip: zf.close()
    print(f"\nDone. Extracted {count} pages.")

if __name__ == "__main__":
    main()