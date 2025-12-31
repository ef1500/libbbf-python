from setuptools import setup, find_packages
from pybind11.setup_helpers import Pybind11Extension, build_ext
import os

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
    version="0.2.0",
    author="EF1500",
    author_email="rosemilovelockofficial@proton.me",
    description="Bound Book Format (BBF) tools and bindings",
    long_description=open("readme.md").read(),
    long_description_content_type="text/markdown",
    url="https://github.com/ef1500/libbbf",
    
    # This finds the 'libbbf_tools' folder
    packages=find_packages(), 
    
    # This compiles the C++ code
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    
    # This creates the command line tools 'cbx2bbf' and 'bbf2cbx'
    entry_points={
        "console_scripts": [
            "cbx2bbf=libbbf_tools.cbx2bbf:main",
            "bbf2cbx=libbbf_tools.bbf2cbx:main",
        ],
    },
    
    zip_safe=False,
    python_requires=">=3.11",
)
