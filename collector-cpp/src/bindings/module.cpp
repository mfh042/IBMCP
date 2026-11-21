#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/attr.h>
#include <pybind11/eval.h>
#include "collector/collector.h"
#include "collector/exports.h"

namespace py = pybind11;
using namespace ibmcp;

static py::object _make_exception_instance(py::object exc_class, const CollectorError& e) {
    py::object inst = exc_class(py::str(e.what()));
    inst.attr("rc") = e.rc();
    inst.attr("stage") = e.stage();
    inst.attr("device_key") = e.device_key();
    inst.attr("point_id") = e.point_id();
    return inst;
}

static void _attach_common_attrs(py::object cls) {
    cls.attr("rc") = py::none();
    cls.attr("stage") = py::none();
    cls.attr("device_key") = py::none();
    cls.attr("point_id") = py::none();
}

template <class F>
static decltype(auto) guarded(const char* stage, F&& f) {
    try {
        return f();
    } catch (const ProtocolNotSupported& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("ProtocolNotSupported");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const InvalidConnection& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("InvalidConnection");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const PlanRevisionConflict& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("PlanRevisionConflict");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const NotInitialized& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("NotInitialized");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const ControlQueueFull& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("ControlQueueFull");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const DeviceNotInPlan& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("DeviceNotInPlan");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const CollectorError& e) {
        py::set_error(PyExc_TypeError, "");
        py::object exc = py::module_::import("industrial_collector._core").attr("CollectorError");
        PyErr_Clear();
        throw _make_exception_instance(exc, e);
    } catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
}

PYBIND11_MODULE(_core, m) {
    m.doc() = "IBMCP industrial collector C++ extension";

    py::object base_exc = py::register_exception<CollectorError>(m, "CollectorError");
    _attach_common_attrs(base_exc);
    py::object exc1 = py::register_exception<ProtocolNotSupported>(m, "ProtocolNotSupported", base_exc);
    _attach_common_attrs(exc1);
    py::object exc2 = py::register_exception<InvalidConnection>(m, "InvalidConnection", base_exc);
    _attach_common_attrs(exc2);
    py::object exc3 = py::register_exception<PlanRevisionConflict>(m, "PlanRevisionConflict", base_exc);
    _attach_common_attrs(exc3);
    py::object exc4 = py::register_exception<NotInitialized>(m, "NotInitialized", base_exc);
    _attach_common_attrs(exc4);
    py::object exc5 = py::register_exception<ControlQueueFull>(m, "ControlQueueFull", base_exc);
    _attach_common_attrs(exc5);
    py::object exc6 = py::register_exception<DeviceNotInPlan>(m, "DeviceNotInPlan", base_exc);
    _attach_common_attrs(exc6);

    py::class_<Collector, std::unique_ptr<Collector, py::nodelete>>(m, "Collector")
        .def_static("instance", [&]() -> Collector& {
            return guarded("Collector.instance", []() -> Collector& {
                return Collector::instance();
            });
        }, py::return_value_policy::reference)

        .def("init", [&](Collector& self, py::dict cfg) {
            py::gil_scoped_release release;
            return guarded("Collector.init", [&]() -> int {
                std::map<std::string, std::string> converted;
                for (auto item : cfg) {
                    converted[py::str(item.first).cast<std::string>()] = py::str(item.second).cast<std::string>();
                }
                return self.init(converted);
            });
        }, py::arg("cfg") = py::dict())

        .def("health", [&](Collector& self) {
            return guarded("Collector.health", [&]() {
                return self.health();
            });
        })

        .def("list_protocols", [&](Collector& self) {
            return guarded("Collector.list_protocols", [&]() {
                return self.list_protocols();
            });
        })

        .def("protocol_capabilities", [&](Collector& self) {
            return guarded("Collector.protocol_capabilities", [&]() {
                return self.protocol_capabilities();
            });
        })

        .def("apply_plan", [&](Collector& self, py::dict plan_json, int revision) {
            py::gil_scoped_release release;
            return guarded("Collector.apply_plan", [&]() -> int {
                std::map<std::string, std::string> converted;
                for (auto item : plan_json) {
                    converted[py::str(item.first).cast<std::string>()] = py::str(item.second).cast<std::string>();
                }
                return self.apply_plan(converted, revision);
            });
        }, py::arg("plan"), py::arg("revision"))

        .def("set_event_callback", [&](Collector& self, std::function<void(const std::map<std::string, std::string>&)> cb) {
            return guarded("Collector.set_event_callback", [&]() {
                self.set_event_callback(cb);
                return py::none();
            });
        }, py::arg("cb"))

        .def("snapshot_since", [&](Collector& self, int since_seq) {
            return guarded("Collector.snapshot_since", [&]() {
                return self.snapshot_since(since_seq);
            });
        }, py::arg("since_seq"))

        .def("write_point", [&](Collector& self, const std::string& device_key, int point_id, const std::string& value) {
            py::gil_scoped_release release;
            return guarded("Collector.write_point", [&]() -> int {
                return self.write_point(device_key, point_id, value);
            });
        }, py::arg("device_key"), py::arg("point_id"), py::arg("value"))

        .def("shutdown", [&](Collector& self) {
            py::gil_scoped_release release;
            return guarded("Collector.shutdown", [&]() {
                self.shutdown();
                return py::none();
            });
        });
}