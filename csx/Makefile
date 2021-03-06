.PHONY: all clean

all: spmv libcsx.a libcsx.so

# Configuration
llvmconf     = llvm-config

dynarray_dir = ../lib/dynarray
spm_dir      = ../lib/spm
prfcnt_dir   = ../lib/prfcnt
script_dir   = ../scripts
mult_template_dir = $(shell pwd)/mult_templates
csx_prefix   = $(shell pwd)/..
llvm_dir     = $(shell $(llvmconf) --includedir)
llvm_prefix  = $(shell $(llvmconf) --prefix)

CC          ?= gcc
CXX         ?= g++
LD           = $(CXX)
CFLAGS      ?= -Wall -O3 -fPIC
CXXFLAGS    ?= -g -rdynamic -O3 -Wall -Woverloaded-virtual -Wdisabled-optimization -fPIC
LIBS         = -lnuma -lpthread -lboost_regex
AR           = ar
AR_FLAGS     = cru

# Always add the following flags
override CPPFLAGS += -DMULT_TEMPLATE_DIR=\"$(mult_template_dir)\" -DCLANG_PREFIX=\"$(llvm_prefix)\" -DCSX_PREFIX=\"$(csx_prefix)\" -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -D_GNU_SOURCE -D__STDC_FORMAT_MACROS
override CPPFLAGS += -I$(dynarray_dir) -I$(spm_dir) -I$(prfcnt_dir) -I$(llvm_dir)
override LDFLAGS  += -Wl,--allow-multiple-definition

# cairomm
CAIROMM_FLAGS   = $(shell pkg-config cairomm-1.0 --cflags)
CAIROMM_LIBS    = $(shell pkg-config cairomm-1.0 --libs)

# LLVM and Clang
llvm_deps       = core analysis executionengine jit native bitreader ipo linker bitwriter asmparser
LLVM_CPPFLAGS	=
LLVM_LDFLAGS    = $(shell $(llvmconf) --ldflags --libs $(llvm_deps))
CLANG_LDFLAGS   = -Wl,--start-group -lclangAnalysis -lclangAST -lclangBasic -lclangCodeGen -lclangDriver -lclangFrontend -lclangFrontendTool -lclangIndex -lclangLex -lclangParse -lclangRewrite -lclangSema -lclangSerialization -lclangStaticAnalyzerCheckers -lclangStaticAnalyzerCore -lclangStaticAnalyzerFrontend -Wl,--end-group
dynarray_dep    = $(dynarray_dir)/dynarray.o

CSX_OBJECTS = spmv.o jit.o llvm_jit_help.o mmf.o spm.o csx.o drle.o template_text.o jit_util.o compiler.o $(spm_dir)/libspmv.o

ifeq ($(shell $(script_dir)/numa_lib.sh FOO), FOO)
	override CPPFLAGS += -DSPM_NUMA
endif

CXX_COMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS)
LINK        = $(LD) $(LDFLAGS)
ARCHIVE     = $(AR) $(AR_FLAGS)

draw.o: draw.cc spm.h draw.h
	$(CXX_COMPILE) $(CAIROMM_FLAGS) $< -c -o $@

draw: draw.cc spm.h draw.h spm.o mmf.o
	$(LINK) $(CAIROMM_FLAGS) $(CAIROMM_LIBS) $(dynarray_dep) spm.o mmf.o $<  -o $@

csx.o: csx.cc csx.h spm.h delta.h $(spm_dir)/numa_util.h $(dynarray_dir)/dynarray.h ctl_ll.h
	$(CXX_COMPILE) -I$(dynarray_dir) $< -c -o $@

test.o: test.cc drle.h spm.h
	$(CXX_COMPILE) $< -c -o $@

drle_test: mmf.o spm.o drle.o test.o drle_test.cc
	$(LINK) $^ -o $@

%.o: %.cc %.h
	$(CXX_COMPILE) -c $<

elmerif.o: elmerif.cc elmerif.h spmv.h spm.h csr.h
	$(CXX_COMPILE) -DSPM_CRS_BITS=32 -c $<
jit.o: spm.h csx.h drle.h template_text.h jit_util.h jit_config.h
compiler.o: jit_util.h
spmv.o: mmf.h csx.h drle.h jit.h ctl_ll.h $(spm_dir)/spm_mt.h $(spm_dir)/mt_lib.h $(spm_dir)/spmv_method.h $(spm_dir)/spm_crs.h $(spm_dir)/spm_mt.h $(prfcnt_dir)/timer.h
elmerif.o: csr.h spmv.h
spm.o: mmf.h $(dynarray_dir)/dynarray.h
drle.o: spm.h
main.o: spmv.h

libcsx.so: $(CSX_OBJECTS) elmerif.o
	$(LINK) -shared $^ $(LIBS) $(dynarray_dep) $(CLANG_LDFLAGS) $(LLVM_LDFLAGS) -o $@

libcsx.a: $(CSX_OBJECTS)
	$(ARCHIVE) $@ $^

spmv: main.o libcsx.a
	$(LINK) $(LIBS) $^ $(CLANG_LDFLAGS) $(LLVM_LDFLAGS) $(dynarray_dep) -o $@

clean:
	rm -f *.o libcsx.so libcsx.a spmv
