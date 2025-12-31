#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "libbbf.h"
#include "bbf_reader.h"

namespace py = pybind11;

PYBIND11_MODULE(libbbf, m) {
    m.doc() = "Bound Book Format (BBF) Python Bindings";

    // --- BBFBuilder (Writer) ---
    py::class_<BBFBuilder>(m, "BBFBuilder")
        .def(py::init<const std::string &>())
        .def("add_page", &BBFBuilder::addPage, 
             py::arg("path"), py::arg("type"), py::arg("flags") = 0,
             "Add a page from a file path.")
        .def("add_section", &BBFBuilder::addSection, 
             py::arg("title"), py::arg("start_page"), py::arg("parent") = 0xFFFFFFFF,
             "Add a section. start_page is the 0-based index.")
        .def("add_metadata", &BBFBuilder::addMetadata,
             "Add Key:Value metadata.")
        .def("finalize", &BBFBuilder::finalize,
             "Write the footer and close the file.");

    // --- BBFReader (Reader) ---
    py::class_<BBFReader>(m, "BBFReader")
        .def(py::init<const std::string &>())
        .def_readonly("is_valid", &BBFReader::isValid)
        .def_readonly("footer", &BBFReader::footer) // Optional: expose footer struct directly if bound
        .def("get_page_count", [](BBFReader& r) { return r.footer.pageCount; })
        .def("get_asset_count", [](BBFReader& r) { return r.footer.assetCount; })
        
        .def("verify", &BBFReader::verify, 
             py::call_guard<py::gil_scoped_release>(), // IMPORTANT: Release GIL during long hashing
             "Verify integrity of index and assets. Multithreaded.")

        .def("get_sections", [](BBFReader& r) {
            // Optimizing the conversion loop
            py::list result;
            const auto sections = r.getSections();
            for (const auto& s : sections) {
                py::dict d;
                d["title"] = s.title; // Moves string
                d["startPage"] = s.startPage;
                d["parent"] = s.parent;
                result.append(d);
            }
            return result;
        }, "Returns sections as [{'title': str, 'startPage': int, 'parent': int}]")
        
        .def("get_metadata", &BBFReader::getMetadata,
             "Returns a list of (Key, Value) tuples.")

        .def("get_page_data", [](BBFReader& r, uint32_t idx) {
             auto raw = r.getPageRaw(idx);
             if (!raw.first) return py::bytes("");
             // 1-Copy: Copies from mmap -> Python Bytes Object
             return py::bytes(raw.first, raw.second);
        }, "Returns the raw bytes of the page asset (1-Copy).")

        .def("get_page_view", [](BBFReader& r, uint32_t idx) {
             auto raw = r.getPageRaw(idx);
             if (!raw.first) return py::memoryview(py::bytes("")); 
             
             // 0-Copy: Direct view into mmap
             // Warning: This view crashes Python if BBFReader is garbage collected before the view!
             // To fix this lifetime issue, we use 'py::keep_alive'.
             return py::memoryview::from_memory(
                 const_cast<char*>(raw.first), 
                 raw.second,
                 true // read-only
             );
        }, py::keep_alive<0, 1>(), // Keep BBFReader (1) alive while memoryview (0) exists
           "Returns a zero-copy memoryview of the asset. Fastest method.");
}