/***************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 * 
 *   GPL LICENSE SUMMARY
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 * 
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 * 
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *   The full GNU General Public License is included in this distribution
 *   in the file called LICENSE.GPL.
 * 
 *   Contact Information:
 *   Intel Corporation
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
 * 
 *  version: QAT20.L.1.2.30-00090
 *
 ****************************************************************************/
#include "adf_ctl.h"

#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <sys/ioctl.h>

#include "config_section.h"
#include "dev_config.h"
#include "global.h"
#include "sections.h"
#include "utils.h"
#include <execinfo.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdarg.h>

class dev_config;
int qat_file = -1;

class config_section;
class section_general;

namespace adf_ctl
{
std::string config_filename;

int configure_dev(adf_dev_status_info* dev_info)
{
    int ret = 0;
    std::unique_ptr<dev_config> cfg;
    /* Create device configuration instance and
     * configure the device. All errors during configuration
     * are handled here using std exceptions. */
    try
    {
        cfg = std::unique_ptr<dev_config>(new dev_config(dev_info));
    }
    catch (std::exception& e)
    {
        std::cerr << "QAT Error: " << e.what() << std::endl;
        return -1;
    }
    try
    {
        cfg->configure_dev();
    }
    catch (std::exception& e)
    {
        std::cerr << "QAT Error: " << e.what() << std::endl;
        ret = -1;
    }
    return ret;
}

static int get_real_id(int dev_id)
{
    /* Just return ADF_CFG_ALL_DEVICES */
    if (dev_id == ADF_CFG_ALL_DEVICES)
    {
        return dev_id;
    }

    int fake_id = dev_id;
    if (ioctl(qat_file, IOCTL_GET_DEV_REAL_ID, &fake_id))
    {
        std::cerr << "Ioctl failed" << std::endl;
        return -1;
    }
    return fake_id;
}

int perform_start_dev(int dev_id)
{
    adf_dev_status_info dev_info;
    int ret = 0;
    dev_info.type = DEV_UNKNOWN;
    /* Start all devices that are not yet started */
    if (ADF_CFG_ALL_DEVICES == dev_id)
    {
        int num_devices, devs_found = 0;
        if (ioctl(qat_file, IOCTL_GET_NUM_DEVICES, &num_devices))
        {
            std::cerr << "Ioctl failed" << std::endl;
            return -1;
        }
        for (int i = 0; i < num_devices; i++)
        {
            dev_info.accel_id = get_real_id(i);
            if ((int)dev_info.accel_id == -1)
            {
                std::cerr << "Get real id failed" << std::endl;
                return -1;
            }
            if (ioctl(qat_file, IOCTL_STATUS_ACCEL_DEV, &dev_info))
            {
                if (errno != ENODEV)
                {
                    std::cerr << "Ioctl failed" << std::endl;
                    return -1;
                }
                continue;
            }


            if (dev_info.state)
            {
                    continue;
            }

            if (configure_dev(&dev_info))
            {
                ret = -1;
                std::cerr << "Failed to configure qat_dev" << i << std::endl;
            }

            if (++devs_found == num_devices)
                break;
        }
    }
    else
    {

        dev_info.accel_id = get_real_id(dev_id);
        if ((int)dev_info.accel_id == -1)
        {
            std::cerr << "Get real id failed" << std::endl;
            return -1;
        }
        if (ioctl(qat_file, IOCTL_STATUS_ACCEL_DEV, &dev_info))
        {
            std::cerr << "Ioctl failed" << std::endl;
            return -1;
        }
        if (dev_info.state)
        {
                std::cerr << "Device qat_dev" << dev_id << " already configured"
                          << std::endl;
                return 0;
        }


        if (configure_dev(&dev_info))
        {
            std::cerr << "Failed to configure qat_dev" << dev_id << std::endl;
            return -1;
        }
    }
    return ret;
}

void print_callstack() {
    void *buffer[100];
    int nptrs = backtrace(buffer, sizeof(buffer) / sizeof(buffer[0]));
    char **strings = backtrace_symbols(buffer, nptrs);
    
    if (strings == NULL) {
        perror("backtrace_symbols");
        return;
    }

    printf("Call stack:=====\n");
    for (int i = 0; i < nptrs; i++) {
        printf("%s\n", strings[i]);
    }
    printf("=====\n");

    free(strings);
}

void write_log(FILE *log, const char *format, ...) {
    va_list args;
    char buffer[4096];
    
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    printf("%s", buffer);
    if (log != NULL) {
        fprintf(log, "%s", buffer);
    }
}

void print_process_info(pid_t pid, FILE *log) {
    char path[PATH_MAX];
    char exe_path[PATH_MAX];
    char cmdline_path[PATH_MAX];
    char cmdline[4096];
    
    // get exe path
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    ssize_t len = readlink(path, exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        write_log(log, "Error getting exe path for PID %d: %s\n", pid, strerror(errno));
        return;
    }
    exe_path[len] = '\0';
    
    // get cmdline
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
    FILE *fp = fopen(cmdline_path, "r");
    if (fp) {
        size_t nread = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
        fclose(fp);
        cmdline[nread] = '\0';
        
        for (size_t i = 0; i < nread; i++) {
            if (cmdline[i] == '\0') {
                cmdline[i] = ' ';
            }
        }
    } else {
        strcpy(cmdline, "[unknown]");
    }
    
    write_log(log, "PID %d:\n  Path: %s\n  Cmdline: %s\n\n", pid, exe_path, cmdline);
}

void print_process_hierarchy(pid_t start_pid, FILE *log) {
    pid_t current_pid = start_pid;
    int generation = 1;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    
    write_log(log, "\n[%04d-%02d-%02d %02d:%02d:%02d] Process hierarchy for parent of PID %d:\n",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec, getpid());
    
    while (current_pid > 1) {
        write_log(log, "Generation %d: ", generation++);
        print_process_info(current_pid, log);
        
        char stat_path[PATH_MAX];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", current_pid);
        
        FILE *fp = fopen(stat_path, "r");
        if (!fp) {
            write_log(log, "Error opening stat file for PID %d: %s\n", current_pid, strerror(errno));
            break;
        }
        
        pid_t ppid;
        if (fscanf(fp, "%*d %*s %*c %d", &ppid) != 1) {
            fclose(fp);
            write_log(log, "Error reading stat file for PID %d\n", current_pid);
            break;
        }
        fclose(fp);
        
        current_pid = ppid;
    }
    
    write_log(log, "Generation %d: ", generation);
    print_process_info(1, log);
}

void print_parent_process_name() {
    pid_t ppid = getppid();
    char path[256];
    char name[256];

    // Open log file in append mode
    FILE *log = fopen("/tmp/qat_adf_ctl.log", "a");
    if (!log) {
        perror("Failed to open log file");
    }

    // Read parent process name from /proc/[ppid]/comm
    snprintf(path, sizeof(path), "/proc/%d/comm", ppid);
    FILE *fp = fopen(path, "r");
    if (fp) {
        fgets(name, sizeof(name), fp);
        write_log(log, "\n-----Parent process name: %s-----\n", name);
        fclose(fp);
    } else {
        perror("Failed to open /proc/[ppid]/comm");
    }

    {
        write_log(log, "---parent stack-------\n");
        print_process_hierarchy(ppid, log);
        write_log(log, "----------\n");
    }
    
    fclose(log);
}

int perform_stop_dev(int dev_id)
{
    adf_user_cfg_ctl_data ctl_data = { { 0 }, 0 };
    int ret = 0;
    print_callstack();
    print_parent_process_name();
    ctl_data.device_id = get_real_id(dev_id);
    if ((int)ctl_data.device_id == -1)
    {
        std::cerr << "Get real id failed" << std::endl;
        return -1;
    }

    ret = ioctl(qat_file, IOCTL_STOP_ACCEL_DEV, &ctl_data);

    if (ret)
    {
        if (EBUSY == errno)
            std::cerr << "device busy" << std::endl;
        else
            std::cerr << "Failed to stop device " << std::endl;
        return -1;
    }
    return 0;
}

void print_dev_info(int dev_id, adf_dev_status_info* dev_info)
{
    std::cout << " qat_dev" << dev_id << " - type: " << dev_info->name << ", "
              << " inst_id: " << (int)dev_info->instance_id << ", "
              << " node_id: " << (int)dev_info->node_id << ", "
              << " bsf: " << std::setbase(16) << std::setfill('0')
              << std::setw(4) << (int)dev_info->domain << std::setw(1) << ":"
              << std::setw(2) << (int)dev_info->bus << std::setw(1) << ":"
              << std::setw(2) << (int)dev_info->dev << std::setw(1) << "."
              << (int)dev_info->fun << ", " << std::setfill(' ')
              << std::setbase(10) << " #accel: " << (int)dev_info->num_accel
              << " #engines: " << (int)dev_info->num_ae
              << " state: " << (dev_info->state ? "up" : "down") << std::endl;
}

int perform_query_dev(int dev_id)
{
    adf_dev_status_info dev_info;

    dev_info.type = DEV_UNKNOWN;

    /* Start all devices that are not yet started */
    if (ADF_CFG_ALL_DEVICES == dev_id)
    {
        int num_devices, devs_found = 0;

        if (ioctl(qat_file, IOCTL_GET_NUM_DEVICES, &num_devices))
        {
            std::cerr << "Ioctl failed" << std::endl;
            return -1;
        }
        std::cout << "There is " << num_devices
                  << " QAT acceleration device(s) in the system:" << std::endl;
        for (int i = 0; i < num_devices; i++)
        {
            dev_info.accel_id = get_real_id(i);
            if ((int)dev_info.accel_id == -1)
            {
                std::cerr << "Get real id failed" << std::endl;
                return -1;
            }
            if (ioctl(qat_file, IOCTL_STATUS_ACCEL_DEV, &dev_info))
            {
                if (errno != ENODEV)
                {
                    std::cerr << "Ioctl failed" << std::endl;
                    return -1;
                }
                continue;
            }
            print_dev_info(i, &dev_info);
            if (++devs_found == num_devices)
                break;
        }
    }
    else
    {

        dev_info.accel_id = get_real_id(dev_id);
        if ((int)dev_info.accel_id == -1)
        {
            std::cerr << "Get real id failed" << std::endl;
            return -1;
        }
        if (ioctl(qat_file, IOCTL_STATUS_ACCEL_DEV, &dev_info))
        {
            std::cerr << "Ioctl failed" << std::endl;
            return -1;
        }
        print_dev_info(dev_id, &dev_info);
    }
    return 0;
}

int perform_reset_dev(int dev_id)
{
    adf_user_cfg_ctl_data ctl_data = { { 0 }, 0 };
    ctl_data.device_id = get_real_id(dev_id);
    if ((int)ctl_data.device_id == -1)
    {
        std::cerr << "Get real id failed" << std::endl;
        return -1;
    }
    int ret = ioctl(qat_file, IOCTL_RESET_ACCEL_DEV, &ctl_data);

    if (ret)
    {
        if (EBUSY == errno)
            std::cerr << "device busy" << std::endl;
        else
            std::cerr << "Failed to reset device " << std::endl;
        return -1;
    }
    return 0;
}

} // namespace adf_ctl
