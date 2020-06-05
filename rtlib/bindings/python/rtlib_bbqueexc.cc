#include "rtlib_bbqueexc.h"

void init_BbqueEXC(py::module &m) {
   py::class_<BbqueEXC, PyBbqueEXC>(m, "BbqueEXC")
      .def(py::init<std::string const &,
            std::string const &,
            RTLIB_Services_t * const>())
      .def("Start", &BbqueEXC::Start)
      .def("WaitCompletion", &BbqueEXC::WaitCompletion)
      .def("Terminate", &BbqueEXC::Terminate)
      .def("isRegistered", &BbqueEXC::isRegistered)
      .def("Enable", &BbqueEXC::Enable)
      .def("SetAWMConstraints",
            [](BbqueEXC &bbqueEXC, std::vector<RTLIB_Constraint> constraints)
            {
               return bbqueEXC.SetAWMConstraints(&constraints[0], constraints.size());
            })
      .def("ClearAWMConstraints", &BbqueEXC::ClearAWMConstraints)
      .def("SetGoalGap", &BbqueEXC::SetGoalGap)
      .def("GetUniqueID_String", &BbqueEXC::GetUniqueID_String)
      .def("GetUniqueID", &BbqueEXC::GetUniqueID)
      .def("GetAssignedResources",
            [](BbqueEXC &bbqueEXC, RTLIB_ResourceType r_type,
               RTLIB_Resources_Amount_Wrapper &r_amount_w)
            {
               return bbqueEXC.GetAssignedResources(r_type, r_amount_w.amount);
            })
      .def("GetAssignedResources",
            [](BbqueEXC &bbqueEXC, RTLIB_ResourceType r_type,
               RTLIB_Resources_Systems_Wrapper &r_systems_w)
            {
               return bbqueEXC.GetAssignedResources(r_type, r_systems_w.systems(), r_systems_w.number_of_systems());
            })
      .def("GetAffinityMask",
            [](BbqueEXC &bbqueEXC, RTLIB_AffinityMasks_Wrapper &a_masks_w)
            {
               return bbqueEXC.GetAffinityMask(a_masks_w.masks(), a_masks_w.number_of_masks());
            })
      .def("SetCPS", &BbqueEXC::SetCPS)
      .def("SetCPSGoal", (RTLIB_ExitCode (BbqueEXC::*)(float, float))&BbqueEXC::SetCPSGoal)
      .def("SetJPSGoal", &BbqueEXC::SetJPSGoal)
      .def("UpdateJPC", &BbqueEXC::UpdateJPC)
      .def("SetMinimumCycleTimeUs", &BbqueEXC::SetMinimumCycleTimeUs)
      .def("GetCPS", &BbqueEXC::GetCPS)
      .def("GetJPS", &BbqueEXC::GetJPS)
      .def("GetCTimeMs", &BbqueEXC::GetCTimeMs)
      .def("GetCTimeUs", &BbqueEXC::GetCTimeUs)
      .def("Cycles", &BbqueEXC::Cycles)
      .def("WorkingModeParams", &BbqueEXC::WorkingModeParams)
      .def("Done", &BbqueEXC::Done)
      .def("CurrentAWM", &BbqueEXC::CurrentAWM)
      .def("Configuration", &BbqueEXC::Configuration)
      .def_property_readonly("exc_name", &BbqueEXC::GetName)
      .def_property_readonly("rpc_name", &BbqueEXC::GetRpcName)
      .def_property_readonly("logger", [](PyBbqueEXC &bbqueEXC)
            {
               auto logger_w = std::unique_ptr<RTLIB_Logger_Wrapper>(
                   new RTLIB_Logger_Wrapper(bbqueEXC.get_logger()));
               return logger_w;
            }, py::return_value_policy::take_ownership);
}
