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
        .def("get_page_count", [](BBFReader& r) { return r.footer.pageCount; })
        .def("get_asset_count", [](BBFReader& r) { return r.footer.assetCount; })
        
        // FIX: Manually convert the C++ struct to a Python dictionary
        .def("get_sections", [](BBFReader& r) {
            py::list result;
            auto sections = r.getSections();
            for (const auto& s : sections) {
                py::dict d;
                d["title"] = s.title;
                d["startPage"] = s.startPage;
                d["parent"] = s.parent;
                result.append(d);
            }
            return result;
        }, "Returns a list of dictionaries [{'title': str, 'startPage': int, 'parent': int}]")
        
        .def("get_metadata", &BBFReader::getMetadata,
             "Returns a list of (Key, Value) tuples.")
        .def("get_page_data", [](BBFReader& r, uint32_t idx) {
             std::string s = r.getPageBytes(idx);
             return py::bytes(s);
        }, "Returns the raw bytes of the page asset.")
        .def("get_page_info", &BBFReader::getPageInfo,
             "Returns dict with keys: length, offset, hash, type.")
        .def("verify", &BBFReader::verify, py::call_guard<py::gil_scoped_release>(),
             "Performs full XXH3 integrity check on directory and all assets.");
}