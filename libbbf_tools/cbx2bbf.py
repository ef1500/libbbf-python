import argparse
import os
import zipfile
import shutil
import tempfile
import libbbf
import re
import sys


class PagePlan:
    def __init__(self, path, filename, order=0):
        self.path = path
        self.filename = filename
        self.order = order # 0=unspecified, >0=start, <0=end

def compare_pages(a):
    if a.order > 0:
        return (0, a.order) 
    elif a.order == 0:
        return (1, a.filename) 
    else:
        return (2, a.order)

def trim_quotes(s):
    if not s: return ""
    if len(s) >= 2 and s.startswith('"') and s.endswith('"'):
        return s[1:-1]
    return s

def main():
    parser = argparse.ArgumentParser(description="Mux CBZ/images to BBF (bbfenc compatible)")
    parser.add_argument("inputs", nargs="+", help="Input files (.cbz, images) or directories")
    parser.add_argument("--output", "-o", help="Output .bbf file", default="out.bbf")
    
    # Matching bbfenc options
    parser.add_argument("--order", help="Text file defining page order (filename:index)")
    parser.add_argument("--sections", help="Text file defining sections")
    parser.add_argument("--section", action="append", help="Add section 'Name:Target[:Parent]'")
    parser.add_argument("--meta", action="append", help="Add metadata 'Key:Value'")
    
    args = parser.parse_args()

    # 1. Parse Order File
    order_map = {}
    if args.order and os.path.exists(args.order):
        with open(args.order, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line: continue
                if ':' in line:
                    fname, val = line.rsplit(':', 1)
                    order_map[trim_quotes(fname)] = int(val)
                else:
                    order_map[trim_quotes(line)] = 0

    manifest = []
    
    # We need to extract CBZs to temp to get individual file paths for the Builder
    # bbfenc processes directories and images. Since Python zipfile needs extraction 
    # to pass a path to the C++ fstream, we extract everything to a temp dir.
    temp_dir = tempfile.mkdtemp(prefix="bbfmux_")
    
    try:
        print("Gathering inputs...")
        for inp in args.inputs:
            inp = trim_quotes(inp)
            
            if os.path.isdir(inp):
                # Directory input
                for root, dirs, files in os.walk(inp):
                    for f in files:
                        if f.lower().endswith(('.png', '.avif', '.jpg', '.jpeg')):
                            full_path = os.path.join(root, f)
                            p = PagePlan(full_path, f)
                            if f in order_map: p.order = order_map[f]
                            manifest.append(p)
                            
            elif zipfile.is_zipfile(inp):
                # CBZ input
                print(f"Extracting {os.path.basename(inp)}...")
                with zipfile.ZipFile(inp, 'r') as zf:
                    # Extract all valid images
                    for name in zf.namelist():
                        if name.lower().endswith(('.png', '.avif', '.jpg', '.jpeg')):
                            
                            extracted_path = zf.extract(name, temp_dir)
                            fname = os.path.basename(name)
                            
                            p = PagePlan(extracted_path, fname)
                            
                            if fname in order_map: p.order = order_map[fname]
                            
                            # Also check if the ZIP name itself has an order (less likely for pages)
                            zip_name = os.path.basename(inp)
                            if zip_name in order_map and len(manifest) == 0: 
                                # TODO: I'll figure this out LATER!
                                pass 
                                
                            manifest.append(p)
            else:
                #Single image file
                fname = os.path.basename(inp)
                p = PagePlan(inp, fname)
                if fname in order_map: p.order = order_map[fname]
                manifest.append(p)

        #Sort Manifest
        print(f"Sorting {len(manifest)} pages...")
        manifest.sort(key=compare_pages)

        file_to_page = {}
        #allow --section="Vol 1":"chapter1.cbx" to work
        input_file_start_map = {} # TODO: DO THIS LATER!
        
        for idx, p in enumerate(manifest):
            file_to_page[p.filename] = idx
            # For now, we rely on exact filename matching as per bbfenc.

        # Structure: Name:Target[:Parent]
        sec_reqs = []
        
        # From file
        if args.sections and os.path.exists(args.sections):
            with open(args.sections, 'r', encoding='utf-8') as f:
                for line in f:
                    if not line.strip(): continue
                    parts = [trim_quotes(x) for x in line.strip().split(':')]
                    if len(parts) >= 2:
                        sec_reqs.append({
                            'name': parts[0],
                            'target': parts[1],
                            'parent': parts[2] if len(parts) > 2 else None
                        })

        # From args
        if args.section:
            for s in args.section:
                # Naive split on colon, might break if titles have colons, 
                # but bbfenc does simplistic parsing too.
                parts = [trim_quotes(x) for x in s.split(':')]
                if len(parts) >= 2:
                    sec_reqs.append({
                        'name': parts[0],
                        'target': parts[1],
                        'parent': parts[2] if len(parts) > 2 else None
                    })

        #Initialize Builder
        builder = libbbf.BBFBuilder(args.output)
        
        # Write Pages
        print("Writing pages to BBF...")
        for p in manifest:
            ext = os.path.splitext(p.filename)[1].lower()
            ftype = 2 # PNG default
            if ext == '.avif': ftype = 1
            elif ext in ['.jpg', '.jpeg']: ftype = 3
            
            if not builder.add_page(p.path, ftype):
                print(f"Failed to add page: {p.path}")
                sys.exit(1)

        # Write Sections
        section_name_to_idx = {}
        # We need to process sections in order to resolve parents correctly if they refer to
        # sections defined earlier in the list.
        
        for i, req in enumerate(sec_reqs):
            target = req['target']
            name = req['name']
            parent_name = req['parent']
            
            page_index = 0
            
            # Is target a number?
            if target.lstrip('-').isdigit():
                val = int(target)
                page_index = max(0, val - 1) # 1-based to 0-based
            else:
                # It's a filename
                if target in file_to_page:
                    page_index = file_to_page[target]
                else:
                    print(f"Warning: Section target '{target}' not found in manifest. Defaulting to Pg 1.")
            
            parent_idx = 0xFFFFFFFF
            if parent_name and parent_name in section_name_to_idx:
                parent_idx = section_name_to_idx[parent_name]
                
            builder.add_section(name, page_index, parent_idx)
            section_name_to_idx[name] = i # bbfenc uses index in the vector

        # Write Metadata
        if args.meta:
            for m in args.meta:
                if ':' in m:
                    k, v = m.split(':', 1)
                    builder.add_metadata(trim_quotes(k), trim_quotes(v))

        print("Finalizing...")
        builder.finalize()
        print(f"Created {args.output}")

    finally:
        shutil.rmtree(temp_dir)

if __name__ == "__main__":
    main()