# @par
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
#  version: QAT20.L.1.2.30-00090

KERNELVERSION=$(shell uname -r | cut -d'.' -f1,2)
$(OBJECTS): 
	@echo Error: $@: To get object files in kernel space, you need to build a static library or a module;

obj-m+=$(OUTPUT_NAME).o
$(OUTPUT_NAME)-objs := $(patsubst %.c,%.o, $(MODULE_SOURCES)) $(ADDITIONAL_KERNEL_LIBS)

ifdef SOURCES
$(OUTPUT_NAME)-objs += $(patsubst %.c,%.o, $(SOURCES)) $(patsubst %.S,%.o, $(ASM_SOURCES))
lib-m := $(patsubst %.c,%.o, $(SOURCES)) $(patsubst %.S,%.o, $(ASM_SOURCES))
endif

ifdef KLIBRARY_SOURCES
lib-m += $(patsubst %.c,%.o, $(KLIBRARY_SOURCES))
endif

ifeq ($(shell awk 'BEGIN{print ("$(KERNELVERSION)" >= "6.1")}'),1)
FLAG := KBUILD_BUILTIN=1 single-build=1
else
FLAG := KBUILD_BUILTIN=1
endif

OUTPUT_OBJECTS = $(patsubst %.c,%.o, $(SOURCES)) $(patsubst %.c,%.o, $(KLIBRARY_SOURCES))

$(LIB_STATIC): dirs
	@echo 'Creating static library ${LIB_STATIC}'; \
	$(MAKE) -C $(KERNEL_SOURCE_ROOT)/ M=$(PWD) obj-m="" $(FLAG); \
	echo 'Copying outputs';\
	test -f lib.a  &&  (ar -t lib.a | xargs ar -rcsD $(LIB_STATIC)); \
	test -f $(LIB_STATIC)  &&  mv -f $(LIB_STATIC) $($(PROG_ACY)_FINAL_OUTPUT_DIR)/$(LIB_STATIC); \
	mv -f $(OBJ) $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f built-in.o  &&  mv -f built-in.o $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f $(OUTPUT_NAME).ko  &&  mv -f $(OUTPUT_NAME).ko $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f $(OUTPUT_NAME).o  &&  mv -f $(OUTPUT_NAME).o $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	$(RM) -rf *.a *.mod.* .*.cmd;


$(MODULENAME): dirs
	@echo 'Creating static library ${LIB_STATIC}'; \
	$(MAKE) -C $(KERNEL_SOURCE_ROOT)/ M=$(PWD) obj-m="" $(FLAG); \
	echo 'Copying outputs';\
	test -f lib.a && cp -f $(OUTPUT_OBJECTS) $(PWD) 2>/dev/null ||:;
	test -f lib.a  &&  (ar -t lib.a | xargs ar -rcsD $(LIB_STATIC)); \
	test -f $(LIB_STATIC)  &&  mv -f $(LIB_STATIC) $($(PROG_ACY)_FINAL_OUTPUT_DIR)/$(LIB_STATIC); \
	echo 'Creating kernel module'; \
	$(MAKE) -C $(KERNEL_SOURCE_ROOT)/ M=$(PWD); \
	echo 'Copying outputs';\
	mv -f $(OBJ) $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f built-in.o  &&  mv -f built-in.o $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f lib.a  &&  mv lib.a $($(PROG_ACY)_FINAL_OUTPUT_DIR)/$(LIB_STATIC);\
	test -f $(OUTPUT_NAME).ko  &&  mv -f $(OUTPUT_NAME).ko $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	test -f $(OUTPUT_NAME).o  &&  mv -f $(OUTPUT_NAME).o $($(PROG_ACY)_FINAL_OUTPUT_DIR);\
	$(RM) -rf *.o *.mod.* .*.cmd;


$(LIB_SHARED): 
	@echo Error: $@: You cannot build shared libraries in kernel space;

$(EXECUTABLE):
	@echo Error: $@: You cannot build executables in kernel space;



