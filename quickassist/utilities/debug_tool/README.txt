/******************************************************************************
 *
 *   BSD LICENSE
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 *  version: QAT20.L.1.2.30-00090
 *
 *****************************************************************************/

===============================================================================
The aim of this script is to gather data that should help to debug an issue.
===============================================================================

Running the script
===============================================================================

1) Install the Intel® QuickAssist Technology Software for Hardware Version 2.0
   package at <INSTALL DIR>
2) Make sure that the driver is up
     a) #export ICP_ROOT=<INSTALL DIR>
     b) #cd $ICP_ROOT/build
     c) #./adf_ctl status
        root@system:/QAT/build# ./adf_ctl status
         Checking status of all devices.
         There is 8 QAT acceleration device(s) in the system:
         qat_dev0 - type: 4xxx,  inst_id: 0,  node_id: 0,  bsf: 0000:6b:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev1 - type: 4xxx,  inst_id: 1,  node_id: 0,  bsf: 0000:70:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev2 - type: 4xxx,  inst_id: 2,  node_id: 0,  bsf: 0000:75:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev3 - type: 4xxx,  inst_id: 3,  node_id: 0,  bsf: 0000:7a:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev4 - type: 4xxx,  inst_id: 4,  node_id: 1,  bsf: 0000:e8:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev5 - type: 4xxx,  inst_id: 5,  node_id: 1,  bsf: 0000:ed:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev6 - type: 4xxx,  inst_id: 6,  node_id: 1,  bsf: 0000:f2:00.0,  #accel: 1 #engines: 9 state: up
         qat_dev7 - type: 4xxx,  inst_id: 7,  node_id: 1,  bsf: 0000:f7:00.0,  #accel: 1 #engines: 9 state: up
        root@system:/QAT/build
    e) Ensure all the devices are up
3) Go to the directory containing the debug dump script
      #cd $ICP_ROOT/quickassist/utilities/debug_tool
4) Run the test case which triggers the issue until the issue is recreated
5) Execute the icp_dump script
     #./icp_dump.sh <full path debug file>

     The resulting debug tar ball will be created and stored in location passed in above.

6) Provide the created debug tarball to your Intel® representative

The following is a list of all the details that will be gathered by the script and stored in the tar ball:
* output of lspci -vvv
* output of lstopo-no-graphics -v
* /sys/kernel/debug/qat_*/* directories
* output of adf_ctl status
* config files /etc/*_dev*.conf
* firmware md5sum
* environment variables
* output of uname -a
* kernol logs: output of /var/log/messages | journalctl
* kernel config files
* device sku
* BIOS version
* amount of Memory on the system
* /proc/cmdline
* kernel modules loaded
* version of the qat driver installed

Output on a system with 4xxx device has the following tree structure:
=====================================================================
ICP_debug
├── BIOS.txt
├── Sku_details.txt
├── adf_ctl_status.txt
├── cmdline.txt
├── config_files
│   ├── 4xxx_dev0.conf
│   ├── 4xxx_dev1.conf
│   ├── 4xxx_dev2.conf
│   ├── 4xxx_dev3.conf
│   ├── 4xxx_dev4.conf
│   ├── 4xxx_dev5.conf
│   ├── 4xxx_dev6.conf
│   └── 4xxx_dev7.conf
├── environment.txt
├── firmware_files
│   ├── qat_4xxx_md5sum.txt
│   └── qat_4xxx_mmp_md5sum.txt
├── kernel_config_files
│   ├── config-4.18.0-338.el8.x86_64
│   ├── config-4.18.0-348.2.1.el8_5.x86_64
│   └── config-4.18.0-373.el8.x86_64
├── kernel_log_files
│   └── kernel_log.txt
├── lsmod.txt
├── lspci.txt
├── memory.txt
├── qat_4xxx_0000_6b_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_70_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_75_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_7a_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_e8_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_ed_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_f2_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_4xxx_0000_f7_00.0
│   ├── cnv_errors
│   ├── dev_cfg
│   ├── fw_counters
│   ├── heartbeat
│   ├── heartbeat_failed
│   ├── heartbeat_sent
│   ├── heartbeat_sim_fail
│   ├── pm_status
│   ├── transport
│   │   ├── bank_000
│   │   │   └── config
│   │   ├── bank_001
│   │   │   └── config
│   │   ├── bank_002
│   │   │   └── config
│   │   ├── bank_003
│   │   │   └── config
│   │   ├── bank_004
│   │   │   └── config
│   │   ├── bank_005
│   │   │   └── config
│   │   ├── bank_006
│   │   │   └── config
│   │   ├── bank_007
│   │   │   └── config
│   │   ├── bank_008
│   │   │   └── config
│   │   ├── bank_009
│   │   │   └── config
│   │   ├── bank_010
│   │   │   └── config
│   │   ├── bank_011
│   │   │   └── config
│   │   ├── bank_012
│   │   │   └── config
│   │   ├── bank_013
│   │   │   └── config
│   │   ├── bank_014
│   │   │   └── config
│   │   ├── bank_015
│   │   │   └── config
│   │   ├── bank_016
│   │   │   └── config
│   │   ├── bank_017
│   │   │   └── config
│   │   ├── bank_018
│   │   │   └── config
│   │   ├── bank_019
│   │   │   └── config
│   │   ├── bank_020
│   │   │   └── config
│   │   ├── bank_021
│   │   │   └── config
│   │   ├── bank_022
│   │   │   └── config
│   │   ├── bank_023
│   │   │   └── config
│   │   ├── bank_024
│   │   │   └── config
│   │   ├── bank_025
│   │   │   └── config
│   │   ├── bank_026
│   │   │   └── config
│   │   ├── bank_027
│   │   │   └── config
│   │   ├── bank_028
│   │   │   └── config
│   │   ├── bank_029
│   │   │   └── config
│   │   ├── bank_030
│   │   │   └── config
│   │   ├── bank_031
│   │   │   └── config
│   │   ├── bank_032
│   │   │   └── config
│   │   ├── bank_033
│   │   │   └── config
│   │   ├── bank_034
│   │   │   └── config
│   │   ├── bank_035
│   │   │   └── config
│   │   ├── bank_036
│   │   │   └── config
│   │   ├── bank_037
│   │   │   └── config
│   │   ├── bank_038
│   │   │   └── config
│   │   ├── bank_039
│   │   │   └── config
│   │   ├── bank_040
│   │   │   └── config
│   │   ├── bank_041
│   │   │   └── config
│   │   ├── bank_042
│   │   │   └── config
│   │   ├── bank_043
│   │   │   └── config
│   │   ├── bank_044
│   │   │   └── config
│   │   ├── bank_045
│   │   │   └── config
│   │   ├── bank_046
│   │   │   └── config
│   │   ├── bank_047
│   │   │   └── config
│   │   ├── bank_048
│   │   │   └── config
│   │   ├── bank_049
│   │   │   └── config
│   │   ├── bank_050
│   │   │   └── config
│   │   ├── bank_051
│   │   │   └── config
│   │   ├── bank_052
│   │   │   └── config
│   │   ├── bank_053
│   │   │   └── config
│   │   ├── bank_054
│   │   │   └── config
│   │   ├── bank_055
│   │   │   └── config
│   │   ├── bank_056
│   │   │   └── config
│   │   ├── bank_057
│   │   │   └── config
│   │   ├── bank_058
│   │   │   └── config
│   │   ├── bank_059
│   │   │   └── config
│   │   ├── bank_060
│   │   │   └── config
│   │   ├── bank_061
│   │   │   └── config
│   │   ├── bank_062
│   │   │   └── config
│   │   └── bank_063
│   │       └── config
│   ├── version
│   │   ├── fw
│   │   ├── hw
│   │   └── mmp
│   └── vqat
├── qat_swversion.txt
├── testtree.txt
└── uname.txt

548 directories, 625 files

Legal/Disclaimers
===================

INFORMATION IN THIS DOCUMENT IS PROVIDED IN CONNECTION WITH INTEL(R) PRODUCTS.
NO LICENSE, EXPRESS OR IMPLIED, BY ESTOPPEL OR OTHERWISE, TO ANY INTELLECTUAL
PROPERTY RIGHTS IS GRANTED BY THIS DOCUMENT. EXCEPT AS PROVIDED IN INTEL'S
TERMS AND CONDITIONS OF SALE FOR SUCH PRODUCTS, INTEL ASSUMES NO LIABILITY
WHATSOEVER, AND INTEL DISCLAIMS ANY EXPRESS OR IMPLIED WARRANTY, RELATING TO
SALE AND/OR USE OF INTEL PRODUCTS INCLUDING LIABILITY OR WARRANTIES RELATING
TO FITNESS FOR A PARTICULAR PURPOSE, MERCHANTABILITY, OR INFRINGEMENT OF ANY
PATENT, COPYRIGHT OR OTHER INTELLECTUAL PROPERTY RIGHT. Intel products are
not intended for use in medical, life saving, life sustaining, critical control
 or safety systems, or in nuclear facility applications.

Intel may make changes to specifications and product descriptions at any time,
without notice.

(C) Intel Corporation 2022

* Other names and brands may be claimed as the property of others.

===============================================================================

