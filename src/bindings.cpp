#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "libbbf.h"
#include "bbf_reader.h"

namespace py = pybind11;

PYBIND11_MODULE(_bbf, m) {
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
        .def("close", [](BBFReader& r) 
        { 
          r.mmap.unmap(); 
          r.isValid = false; // Prevent further reads
        }, "Unmaps the file immediately.")

        .def("get_page_count", [](BBFReader& r) { return r.footer.pageCount; })
        .def("get_asset_count", [](BBFReader& r) { return r.footer.assetCount; })
        
        .def("verify", &BBFReader::verify, 
             py::call_guard<py::gil_scoped_release>(), 
             "Verify integrity. Returns: -1 (Success), -2 (Directory Fail), or >=0 (Index of corrupt asset).")
     
        .def("verify_page", &BBFReader::verifyPage,
             "Verify a single page, returns a dict <uint64_t, bool>, {calculated hash, match?}.")

        .def("get_sections", [](BBFReader& r) 
        {
            py::list result;
            const auto sections = r.getSections();
            for (const auto& s : sections) {
                py::dict d;
                d["title"] = s.title; 
                d["startPage"] = s.startPage;
                d["parent"] = s.parent;
                result.append(d);
            }
            return result;
        }, "Returns sections as [{'title': str, 'startPage': int, 'parent': int}]")
        
        .def("get_footer", &BBFReader::getFooterInfo, 
             "Returns a dict representing the footer.")

        .def("get_metadata", &BBFReader::getMetadata,
             "Returns a list of (Key, Value) tuples.")
             
        .def("get_page_info", &BBFReader::getPageInfo,
             "Returns dict with keys: length, offset, hash, type, decodedLength")

        .def("get_page_data", [](BBFReader& r, uint32_t idx) 
        {
             auto raw = r.getPageRaw(idx);
             if (!raw.first) return py::bytes("");
             return py::bytes(raw.first, raw.second);
        }, "Returns the raw bytes of the page asset (1-Copy).")

        .def("get_page_view", [](BBFReader& r, uint32_t idx) 
        {
             auto raw = r.getPageRaw(idx);
             if (!raw.first) return py::memoryview(py::bytes("")); 
             
             return py::memoryview::from_memory(
                 const_cast<char*>(raw.first), 
                 raw.second,
                 true // read-only
             );
        }, py::keep_alive<0, 1>(), 
           "Returns a zero-copy memoryview of the asset. Fastest method.");
}