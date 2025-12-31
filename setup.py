from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext
import os

# --- SAFE README READING ---
# This block checks multiple common filenames and handles errors gracefully.
# If no readme is found, it defaults to an empty string instead of crashing.
long_description = ""
possible_readmes = ["README.md", "readme.md", "README.txt"]

for f in possible_readmes:
    if os.path.exists(f):
        try:
            with open(f, "r", encoding="utf-8") as file:
                long_description = file.read()
            break
        except:
            pass
# ---------------------------

# Define the C++ extension
ext_modules = [
    Pybind11Extension(
        "libbbf",
        [
            "src/bindings.cpp",
            "src/libbbf.cpp"
        ],
        include_dirs=["src"],
        cxx_std=17,
    ),
]

setup(
    name="libbbf",
    version="0.2.10",
    author="EF1500",
    author_email="rosemilovelockofficial@proton.me",
    description="Bound Book Format (BBF) tools and bindings",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/ef1500/libbbf",
    
    packages=find_packages(), 
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    
    entry_points={
        "console_scripts": [
            "cbx2bbf=libbbf_tools.cbx2bbf:main",
            "bbf2cbx=libbbf_tools.bbf2cbx:main",
        ],
    },
    
    zip_safe=False,
    python_requires=">=3.11",
)