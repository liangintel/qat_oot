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

CORE_AFFINITY_COUNTER=1

OUTPUT_DIR="/etc/"

CUT_ENDING_CHAR=""

# read arguments:
# -f: input template file
# -n: how many dev files should be generated
# -o: specify output directory (default: /etc)
# -c: rename long extensions to .conf (example: x.conf.dc.sym -> x.conf)
while getopts f:n:o:c FLAG; do
    case "${FLAG}" in
        f) TEMPLATE_PATH=${OPTARG} ;;
        n) INPUT_COUNT=${OPTARG} ;;
        o) OUTPUT_DIR=${OPTARG} ;;
        c) CUT_ENDING_CHAR="*" ;;
    esac
done

if [[ ! -f ${TEMPLATE_PATH} ]]; then
    echo "ERROR: Template file not found! ${TEMPLATE_PATH}"
    exit 1
fi

if [[ -z ${INPUT_COUNT} ]]; then
    echo "ERROR: Expected -n argument!"
    exit 1
fi

# check if INPUT_COUNT variable is a string, that contains only numbers
if [[ ! ${INPUT_COUNT} =~ ^[0-9]+$ ]]; then
    echo "ERROR: Invalid -n argument - not a number!"
    exit 1
fi

if [[ ${INPUT_COUNT} -lt 1 ]]; then
    echo "ERROR: Invalid -n argument - less than 1!"
    exit 1
fi

if [[ ! -d ${OUTPUT_DIR} ]]; then
    echo "ERROR: Output directory \"${OUTPUT_DIR}\" does not exist!"
    exit 1
fi

for ((FILE_ITERATION = 0; FILE_ITERATION < $INPUT_COUNT; FILE_ITERATION++)); do
    TEMPLATE_BASE_NAME=${TEMPLATE_PATH##*/}
    if [[ $TEMPLATE_BASE_NAME == *"_template.conf"* ]]; then
        OUTPUT_FILE_NAME=${TEMPLATE_BASE_NAME/_template.conf$CUT_ENDING_CHAR/_dev$FILE_ITERATION.conf}
    else
        OUTPUT_FILE_NAME=${TEMPLATE_BASE_NAME/_dev0.conf$CUT_ENDING_CHAR/_dev$FILE_ITERATION.conf}
    fi
    echo "Generating ${OUTPUT_FILE_NAME}"

    if [[ -f ${OUTPUT_DIR}/${OUTPUT_FILE_NAME} ]]; then
        echo "ERROR: File ${OUTPUT_DIR}/${OUTPUT_FILE_NAME} exists!"
        exit 1
    fi

    while read -r LINE; do
        # Process lines containing "CoreAffinity = " string, unless they start with "#"
        if [[ "$LINE" != \#* ]] && [[ "$LINE" == *"CoreAffinity = "* ]]; then
            echo ${LINE/ = */ = $CORE_AFFINITY_COUNTER} >> ${OUTPUT_DIR}/${OUTPUT_FILE_NAME}
            ((CORE_AFFINITY_COUNTER = CORE_AFFINITY_COUNTER + 1))
        else
            echo "$LINE" >> ${OUTPUT_DIR}/${OUTPUT_FILE_NAME}
        fi
    done < ${TEMPLATE_PATH}
done
