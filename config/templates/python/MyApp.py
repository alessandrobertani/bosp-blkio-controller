#!/usr/bin/env python
import sys
import os
script_path = os.path.dirname(os.path.abspath(__file__))
barbeque_path = (script_path + "/../../lib/bbque/bindings/python" +
		str(sys.version_info[0]) + "." + str(sys.version_info[1]))
sys.path.append(barbeque_path)
import barbeque

class MyApp(barbeque.BbqueEXC):

    def __init__(self, name, recipe, rtlib_services):
        super(MyApp, self).__init__(name, recipe, rtlib_services)
        self.logger.Info("EXC Unique Identifier (UID): {}".format(self.GetUniqueID()))

    def onSetup(self):
        self.logger.Notice("MyApp.onSetup()")

        # Initialization code...

        return barbeque.RTLIB_ExitCode.RTLIB_OK

    def onConfigure(self, awm_id):
        self.logger.Notice("MyApp.onConfigure(): EXC [{}], AWM[{:02d}]"
                           .format(self.exc_name, awm_id))

        # Information about assigned resources
        res_w = barbeque.RTLIB_Resources_Amount_Wrapper()
        ret = self.GetAssignedResources(barbeque.RTLIB_ResourceType.PROC_ELEMENT, res_w)
        cpu_time = res_w.amount
        ret = self.GetAssignedResources(barbeque.RTLIB_ResourceType.PROC_NR, res_w)
        cpu_nr = res_w.amount
        ret = self.GetAssignedResources(barbeque.RTLIB_ResourceType.GPU, res_w)
	gpu_nr = res_w.amount
        ret = self.GetAssignedResources(barbeque.RTLIB_ResourceType.MEMORY, res_w)
        mem = res_w.amount

        self.logger.Notice("MyApp.onConfigure(): [{}], AWM[{:02d}] => "
                           "cpu_time={:3d}, nr_cpus={:2d}, nr_gpus={:2d}, mem={:3d}"
                           .format(self.exc_name, awm_id, cpu_time, cpu_nr, gpu_nr, mem))

        # Configure the application w.r.t to the assigned resources
        # ...

        return barbeque.RTLIB_ExitCode.RTLIB_OK

    def onRun(self):
	# A computation run...
        self.logger.Notice("MyApp.onRun(): EXC [{}], current cycle={:d}"
                            .format(self.exc_name, self.Cycles()))

        # ...just run 30 cycles end then exit
	if self.Cycles() == 30:
            return barbeque.RTLIB_ExitCode.RTLIB_EXC_WORKLOAD_NONE

        return barbeque.RTLIB_ExitCode.RTLIB_OK

    def onMonitor(self):
        wmp = self.WorkingModeParams()

        # Anything to check? Performance?

        self.logger.Notice("MyApp.onMonitor(): EXC [{}], AWM[{:02d}] => CPS={:.2f}"
                            .format(self.exc_name, wmp.awm_id, self.GetCPS()))
        return barbeque.RTLIB_ExitCode.RTLIB_OK

    def onRelease(self):
        self.logger.Notice("MyApp.onRelease(): terminating...")

        # Cleanup code...

        return barbeque.RTLIB_ExitCode.RTLIB_OK


def main():

    # Wrapper for RTLIB_Services
    services_wrapper = barbeque.RTLIB_Services_Wrapper()

    # Initialize rpc channel with the resource manager
    error_check = barbeque.RTLIB_Init("MyApp", services_wrapper)
    if (error_check != barbeque.RTLIB_ExitCode.RTLIB_OK) or (not services_wrapper.services):
        print ("RTLIB initialization failed")
        return

    # Run
    exc = MyApp("MyApp", "MyApp", services_wrapper.services)
    exc.Start()
    exc.WaitCompletion()

if __name__ == '__main__':
    main()
