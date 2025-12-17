export ICP_ROOT=/home/liang_emr06/qat_oot/

g++ -std=c++11 -g -ggdb3 -O0 -fpermissive -o hugepage_no_usdm_test hugepage.cpp aio_test.cpp qat_test.cpp main.cpp \
-I $ICP_ROOT/quickassist/include/ -I $ICP_ROOT/quickassist/include/dc -I $ICP_ROOT/quickassist/include/lac \
-I $ICP_ROOT/quickassist/utilities/libusdm_drv/ -I $ICP_ROOT/quickassist/lookaside/access_layer/include/ \
-L $ICP_ROOT/build/  $ICP_ROOT/build/libqat_s.so  $ICP_ROOT/build/libusdm_drv_s.so  -laio

