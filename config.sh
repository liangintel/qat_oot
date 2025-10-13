#!/bin/bash

################################################################
# This file is provided under a dual BSD/GPLv2 license.  When using or
#   redistributing this file, you may do so under either license.
# 
#   GPL LICENSE SUMMARY
# 
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
# 
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of version 2 of the GNU General Public License as
#   published by the Free Software Foundation.
# 
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
# 
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#   The full GNU General Public License is included in this distribution
#   in the file called LICENSE.GPL.
# 
#   Contact Information:
#   Intel Corporation
# 
#   BSD LICENSE
# 
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
#   All rights reserved.
# 
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
# 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
# 
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# 
#  version: QAT20.L.1.2.30-00090
################################################################

devdir=""
sku=-1
config_file=""
cfg_valid=0

detect_hw()
{
    for dir in /sys/bus/pci/devices/*/; do
        if [[ -d $dir ]]; then
            local vendor
            local device
            read vendor < $dir/vendor
            read device < $dir/device
            if [[ $vendor == "0x8086" && $device == "0x18a0" ]]; then
                devdir=$dir
            fi
        fi

    done
}

validate_cfg()
{
    local fsize=$(stat -c "%s" $1/config)

    if [[ $fsize == 4096 ]]; then
        cfg_valid=1
    fi
}

# BTS SKUs need additional fuse checks

detect_sku()
{
    local fuse1=$(od -t x4 -j 0x354 -A n -N 4 $1/config | sed 's/^[[:space:]]*//')
    local lfuse=$(od -t x4 -j 0x04c -A n -N 4 $1/config | sed 's/^[[:space:]]*//')
    local noasym=$((0x$lfuse & 0xc))

    case $fuse1 in
        "00000000")
            sku=0
            ;;
        "f000f000")
            sku=1
            ;;
        "ffc0ffc0")
            sku=2
            ;;
        *)
            sku=-1
            ;;
    esac

    if [[ $noasym != "0" ]]; then
        sku=$sku"_noasym"
    else
        sku=$sku"_asym"
    fi
}

display_sku()
{
    case $1 in
        "0_asym")
            echo "C4xxx High SKU is detected"
            ;;
        "1_asym")
            echo "C4xxx Medium SKU is detected"
            ;;
        "2_asym")
            echo "C4xxx Low SKU is detected"
            ;;
        "0_noasym")
            echo "C4xxx High Symmetric-only SKU is detected"
            ;;
        "1_noasym")
            echo "C4xxx Medium Symmetric-only SKU is detected"
            ;;
        "2_noasym")
            echo "C4xxx Low Symmetric-only SKU is detected"
            ;;
        *)
            echo "Unknown C4xxx SKU"
            return -2
            ;;
    esac

}

# Args:
#    $1 - config filename template
#    $2 - "DC_CONF" if a dc/dc+sym config is to be selected
#    $3 - "SYM_CONF" if a sym/dc+sym config is to be selected
#    $4 - SKU designation
#

get_config_filename()
{
    local filename=$1
    local sel_dc=$2
    local sel_sym=$3
    local sku=$4
    local sriov=0

    # Check if passed configuration file is for SRIOV.
    if [[ $filename =~ "c4xxxvf" ]]; then
        echo "Detected SRIOV configuration file."
        sriov=1
    fi

    if [[ $sel_dc == "DC_CONF" && ! ($sku =~ "noasym") ]]; then
        filename=$filename".dc"
    fi

    if [[ $sel_sym == "SYM_CONF" || $sku =~ "noasym" ]]; then
        filename=$filename".sym"
    fi

    if [ $sriov == 1 ]; then
        filename=$filename".vm"
    fi

    # Add SKU suffix
    case $sku in
        "0"*) ;;

        "1"*)
            # Physical SKU type does not apply to SRIOV configuration.
            if [ $sriov == 0 ]; then
                filename=$filename".med"
            fi
            ;;
        "2"*)
            # Physical SKU type does not apply to SRIOV configuration.
            if [ $sriov == 0 ]; then
                filename=$filename".low"
            fi
            ;;
        *)
            return -2
            ;;
    esac

    config_file=$filename
}

main()
{
    local template=$1
    local platform=$2
    local sel_dc=$3
    local sel_sym=$4
    local target=$5
    local dir_name

    if [ $# -eq 0 ]; then
        detect_hw
        if [[ $devdir != "" ]]; then
            detect_sku $devdir
        else
            return -1
        fi
        display_sku $sku
        return 0
    fi
    detect_hw

    if [[ $devdir != "" ]]; then
        validate_cfg $devdir
    else
        return -1
    fi

    if [[ $cfg_valid == 0 ]]; then
        config_file=$template
        dir_name=$(dirname $config_file)
        echo "Possibly running inside a VM"
        echo "Selecting default config file $config_file"
        echo "Editing file $config_file before installing the package may be required"

        cp -Lp $config_file $target

        return 0
    fi

    detect_sku $devdir

    display_sku $sku

    if [[ $sku == -1 ]]; then
        return -2
    fi

    get_config_filename $template $sel_dc $sel_sym $sku

    echo "Config file selected: "$config_file

    if [[ ! -f $config_file ]]; then
        echo "File $config_file does not exist"
        return -3
    fi

    cp -Lp $config_file $target
}

# Args:
#    $1 - config filename template
#    $2 - HW types
#    $3 - "DC_CONF" if a dc/dc+sym config is to be selected
#    $4 - "SYM_CONF" if a sym/dc+sym config is to be selected
#    $5 - target filename for the selected config file
#

if [ $# -eq 0 ]; then
    # Invoke main function without argument to display SKU version
    main
else
    main $1 $2 $3 $4 $5
fi
