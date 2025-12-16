export ICP_ROOT=/home/liangf/liang_oot/

g++ -std=c++11 -g -ggdb3 -O0 -o hugepage_no_usdm hugepage_no_usdm.cpp -I $ICP_ROOT/quickassist/utilities/libusdm_drv/ $ICP_ROOT/build/libusdm_drv_s.so  -laio

