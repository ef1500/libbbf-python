# libbbf-python: Bound Book Format (BBF) Tools & Python Bindings

[![PyPI](https://img.shields.io/pypi/v/libbbf.svg)](https://pypi.org/v/libbbf/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

> [!WARNING]
> Official Source Notice: Please only download resources from this repository (ef1500/libbbf-python). External mirrors or forks may contain malware.

## Project Overview

`libbbf-python` provides Python bindings and command-line tools for the [Bound Book Format (BBF)](https://github.com/ef1500/libbbf). The Bound Book Format is an archive format designed for digital books and comics. This package allows Python developers to programmatically create and read `.bbf` files, and provides convenient command-line utilities to convert between common archive formats (like CBZ/CBX) and BBF.

The core of `libbbf-python` is a `pybind11` wrapper around the high-performance C++ `libbbf` library, ensuring speed and native integration.

## Features

*   **`libbbf` Python Bindings**: Access the full `BBFBuilder` (for creating BBF files) and `BBFReader` (for reading and extracting BBF files) functionality directly in Python.
*   **`cbx2bbf` Command-Line Tool**: Convert one or more CBZ/CBX archives (or directories of images) into a single `.bbf` file. Supports advanced features like custom page ordering, section markers (chapters, volumes), and metadata.
*   **`bbf2cbx` Command-Line Tool**: Extract the contents of a `.bbf` file back into a `.cbz` archive or an image directory. Supports integrity verification, section-specific extraction, and range extraction using keywords.
*   **High Performance**: Leverages C++ for core operations (file I/O, hashing, memory mapping) for optimal speed.
*   **Integrity Checks**: Built-in XXH3 hashing for robust data verification during creation and extraction.

## Installation

You can install `libbbf-python` directly from PyPI:

```bash
pip install libbbf
```

**Note on C++ Compiler:**
For platforms where pre-built binary wheels are not available (e.g., specific Linux distributions or older Python versions), `pip` will attempt to build the package from source. This requires a C++ compiler (like GCC/Clang on Linux/macOS or MSVC on Windows) to be installed on your system.

## Command-Line Tools Usage

Once installed, two command-line tools will be available globally: `cbx2bbf` and `bbf2cbx`.

### `cbx2bbf`: Create BBF Files from CBZ/Images

**Usage:**
```
cbx2bbf <inputs...> [options] --output <output.bbf>
```

**Inputs:**
Can be individual image files (`.png`, `.avif`, `.jpg`), directories containing images, or `.cbz`/`.cbx` archives.

**Options:**
*   `--output <output.bbf>`, `-o <output.bbf>`: **Required.** Specifies the output BBF filename.
*   `--order <path.txt>`: Use a text file to define page order. Format: `filename:index` (e.g., `cover.png:1`, `credits.png:-1` for last page).
*   `--sections <path.txt>`: Use a text file to define multiple sections. Format: `Name:Target[:Parent]`.
*   `--section "Name:Target[:Parent]"`: Add a single section marker. Target can be a 1-based page index or a filename. Example: `--section "Chapter 1:001.png"` or `--section "Volume 1:my_comic.cbx:Series"`.
*   `--meta "Key:Value"`: Add archival metadata (e.g., `--meta "Title:Akira"`).

**Examples:**

1.  **Basic Conversion:**
    ```bash
    cbx2bbf my_comic.cbz -o output.bbf
    ```

2.  **Converting Multiple CBZs with Sections and Metadata:**
    ```bash
    cbx2bbf vol1.cbz vol2.cbz \
        --section "Volume 1:vol1.cbz" \
        --section "Volume 2:vol2.cbz" \
        --meta "Title:My Awesome Series" \
        --meta "Author:Me" \
        -o series.bbf
    ```
    *(Note: `vol1.cbz` and `vol2.cbz` as targets will automatically map to the first page of those archives after sorting)*

3.  **Converting a Directory of Images with a Custom Order File:**
    ```bash
    # pages.txt content:
    # cover.png:1
    # page001.png:2
    # ...
    # credits.png:-1

    cbx2bbf ./my_pages_folder/ --order pages.txt -o my_book.bbf
    ```

### `bbf2cbx`: Extract BBF Files

**Usage:**
```
bbf2cbx <input.bbf> [options] --output <output.cbz_or_dir>
```

**Options:**
*   `--output <output_path>`, `-o <output_path>`: **Required.** Output `.cbz` file or directory name.
*   `--dir`: Extract to a directory instead of a `.cbz` archive.
*   `--section "Name"`: Extract only a specific section defined in the BBF.
*   `--rangekey "String"`: When extracting a section, find the end of extraction by matching this string against the *next* section's title. Useful for extracting "Vol 1" until "Vol 2" begins.
*   `--verify`: Perform a full XXH3 integrity check on all assets and the directory hash before extraction.
*   `--info`: Display book structure and metadata without extracting.

**Examples:**

1.  **Extract entire BBF to CBZ with Verification:**
    ```bash
    bbf2cbx my_book.bbf -o my_book_extracted.cbz --verify
    ```

2.  **Extract a specific section to a folder:**
    ```bash
    bbf2cbx series.bbf --section "Volume 1" --dir -o ./output/vol1/
    ```

3.  **Extract a range using `rangekey`:**
    ```bash
    bbf2cbx series.bbf --section "Volume 1" --rangekey "Volume 2" -o vol1_only.cbz
    ```

4.  **Display BBF Information:**
    ```bash
    bbf2cbx series.bbf --info
    ```

## Using `libbbf`

You can also interface libbbf directly from python.

Example 1: Creating a .bbf file
```python
from libbbf import BBFBuilder
import os

# Create a new Bound Book
builder = BBFBuilder("my_comic.bbf")

# Add Metadata
builder.add_metadata("Title", "Solo Leveling")
builder.add_metadata("Author", "Chugong")

# Add Pages (Assets are automatically deduplicated by hash!)
page_files = sorted(os.listdir("./chapter_images"))
for i, filename in enumerate(page_files):
    full_path = os.path.join("./chapter_images", filename)
    
    # Flags: 0 = Standard
    builder.add_page(full_path, type=1, flags=0) 

    # Define a Chapter every 20 pages
    if i % 20 == 0:
        builder.add_section(f"Chapter {i // 20 + 1}", start_page=i)

# Finalize writes the footer and optimized tables
builder.finalize()
print("BBF created successfully.")
```

Example 2: Verifying a .bbf file
```python
import libbbf
import time

reader = libbbf.BBFReader("massive_archive.bbf")

print("Verifying file integrity...")
start = time.time()

# This releases the GIL, so it's thread-safe and incredibly fast
is_valid = reader.verify()

end = time.time()

if is_valid:
    print(f"SUCCESS: Integrity verified in {end - start:.4f}s")
else:
    print("ERROR: Corruption detected!")
```

## Contributing

Contributions, issues, and feature requests are welcome! For libbbf, free to check the [issues page](https://github.com/ef1500/libbbf/issues) (or create one if it doesn't exist yet).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---
