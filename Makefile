TITLE := Aether
VERSION := 0.88
DISPLAY_TITLE := $(TITLE)
TITLE_ID := LLMT00001
CONTENT_ID := IV0001-LLMT00001_00-LLMSERVER0000000
APP_CATEGORY ?= gde

LIBS := -lc -lkernel -lc++ -lSceLibcInternal -lSceSysmodule \
        -lSceUserService -lSceSystemService -lSceCommonDialog -lSceMsgDialog \
        -lSceNet -lSceNetCtl -lxnet \
        -lSceVideoOut -lScePad -lScePigletv2VSH -lm

PROJDIR := source
LLAMA_ROOT := external/llama.cpp
LLAMA_SRC_DIR := $(LLAMA_ROOT)/src
LLAMA_MOD_DIR := $(LLAMA_ROOT)/src/models
GGML_SRC_DIR := $(LLAMA_ROOT)/ggml/src
GGML_CPU_DIR := $(LLAMA_ROOT)/ggml/src/ggml-cpu
GGML_X86_DIR := $(LLAMA_ROOT)/ggml/src/ggml-cpu/arch/x86
INTDIR := build

INCLUDES := -Iinclude -I$(LLAMA_ROOT)/include -I$(LLAMA_ROOT)/ggml/include \
            -I$(LLAMA_SRC_DIR) -I$(GGML_SRC_DIR) -I$(GGML_CPU_DIR) \
            -I$(LLAMA_ROOT)/vendor

CPU_FLAGS := -mtune=btver2 -mavx -mssse3 -msse4.1 -msse4.2 -mno-avx2 -mno-fma -mno-f16c \
             -mno-avx512f -mno-avx512dq
GGML_DEFINES := -DGGML_USE_CPU -DGGML_USE_LLAMAFILE=0 \
                -DGGML_VERSION=\"ps4-port-0.30\" -DGGML_COMMIT=\"ps4\"

EXTRAFLAGS := -Wall -Wno-c++11-narrowing -fcolor-diagnostics -D__ORBIS__ -D__PS4__ -O2 \
              -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_BSD_SOURCE \
              -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
              $(CPU_FLAGS) $(GGML_DEFINES)

TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
LINK_SCRIPT := link.ps4.x
CREATE_FSELF := external/create-fself/create-fself-v1.0-linux
SHIM := -include include/ps4_libc_shim.h

CFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) \
          -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include $(INCLUDES) \
          $(SHIM) -std=c11 -D_DEFAULT_SOURCE
CXXFLAGS := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c $(EXTRAFLAGS) \
            -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include \
            -isystem $(TOOLCHAIN)/include/c++/v1 $(INCLUDES) \
            $(SHIM) -std=c++17 -Wno-invalid-noreturn
LDFLAGS := -m elf_x86_64 -pie --script $(LINK_SCRIPT) --eh-frame-hdr \
           -z max-page-size=0x4000 -z common-page-size=0x4000 --no-rosegment \
           -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o \
           /usr/lib/llvm-10/lib/clang/10.0.0/lib/linux/libclang_rt.builtins-x86_64.a

CC := clang
CCX := clang++
LD := ld.lld
CDIR := linux

MAIN_CPP := $(wildcard $(PROJDIR)/*.cpp)
MAIN_C :=
MAIN_OBJS := $(patsubst $(PROJDIR)/%.cpp,$(INTDIR)/main_%.cpp.o,$(MAIN_CPP)) \
             $(patsubst $(PROJDIR)/%.c,$(INTDIR)/main_%.c.o,$(MAIN_C))
LLAMA_CPP := $(wildcard $(LLAMA_SRC_DIR)/*.cpp)
LLAMA_OBJS := $(patsubst $(LLAMA_SRC_DIR)/%.cpp,$(INTDIR)/llama_%.cpp.o,$(LLAMA_CPP))
LLAMA_MOD_CPP := $(wildcard $(LLAMA_MOD_DIR)/*.cpp)
LLAMA_MOD_OBJS := $(patsubst $(LLAMA_MOD_DIR)/%.cpp,$(INTDIR)/llamamod_%.cpp.o,$(LLAMA_MOD_CPP))
GGML_C := $(wildcard $(GGML_SRC_DIR)/*.c)
GGML_CPP := $(wildcard $(GGML_SRC_DIR)/*.cpp)
GGML_OBJS := $(patsubst $(GGML_SRC_DIR)/%.c,$(INTDIR)/ggml_%.c.o,$(GGML_C)) \
             $(patsubst $(GGML_SRC_DIR)/%.cpp,$(INTDIR)/ggml_%.cpp.o,$(GGML_CPP))
GGML_CPU_C := $(wildcard $(GGML_CPU_DIR)/*.c)
GGML_CPU_CPP := $(wildcard $(GGML_CPU_DIR)/*.cpp)
GGML_CPU_OBJS := $(patsubst $(GGML_CPU_DIR)/%.c,$(INTDIR)/ggmlcpu_%.c.o,$(GGML_CPU_C)) \
                 $(patsubst $(GGML_CPU_DIR)/%.cpp,$(INTDIR)/ggmlcpu_%.cpp.o,$(GGML_CPU_CPP))
GGML_X86_C := $(wildcard $(GGML_X86_DIR)/*.c)
GGML_X86_CPP := $(wildcard $(GGML_X86_DIR)/*.cpp)
GGML_X86_OBJS := $(patsubst $(GGML_X86_DIR)/%.c,$(INTDIR)/ggmlx86_%.c.o,$(GGML_X86_C)) \
                 $(patsubst $(GGML_X86_DIR)/%.cpp,$(INTDIR)/ggmlx86_%.cpp.o,$(GGML_X86_CPP))

OBJS := $(MAIN_OBJS) $(LLAMA_OBJS) $(LLAMA_MOD_OBJS) $(GGML_OBJS) $(GGML_CPU_OBJS) $(GGML_X86_OBJS)

_unused := $(shell mkdir -p $(INTDIR))
PKG_OUT := $(TITLE)-v$(VERSION).pkg

all: $(PKG_OUT)

$(PKG_OUT): pkg.gp4 eboot.bin sce_sys/param.sfo sce_sys/pic1.png web/index.html
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .
	mv $(CONTENT_ID).pkg $(PKG_OUT)

sce_sys/param.sfo: Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 32
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value '$(APP_CATEGORY)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(DISPLAY_TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

eboot.bin: $(OBJS) $(LINK_SCRIPT) Makefile $(CREATE_FSELF) tools/patch_oelf_dynamic.py
	$(LD) $(OBJS) -o $(INTDIR)/source.elf $(LDFLAGS)
	cp $(CREATE_FSELF) /tmp/create-fself
	chmod +x /tmp/create-fself
	/tmp/create-fself -in=$(INTDIR)/source.elf -out=/tmp/oeboot.bin -eboot=/tmp/unused_eboot.bin \
		-paid 0x3800000000000011 \
		-ptype fake \
		-authinfo 000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
	python3 tools/patch_oelf_dynamic.py /tmp/oeboot.bin
	python3 $(TOOLCHAIN)/scripts/make_fself.py /tmp/oeboot.bin /tmp/eboot.bin \
		--paid 0x3800000000000011 \
		--ptype fake \
		--auth-info 000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
	cp /tmp/oeboot.bin $(INTDIR)/source.oelf
	cp /tmp/eboot.bin eboot.bin

$(INTDIR)/main_%.cpp.o: $(PROJDIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<
$(INTDIR)/main_%.c.o: $(PROJDIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<
$(INTDIR)/llama_%.cpp.o: $(LLAMA_SRC_DIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<
$(INTDIR)/llamamod_%.cpp.o: $(LLAMA_MOD_DIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<
$(INTDIR)/ggml_%.c.o: $(GGML_SRC_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<
$(INTDIR)/ggml_%.cpp.o: $(GGML_SRC_DIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<
$(INTDIR)/ggmlcpu_%.c.o: $(GGML_CPU_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<
$(INTDIR)/ggmlcpu_%.cpp.o: $(GGML_CPU_DIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<
$(INTDIR)/ggmlx86_%.c.o: $(GGML_X86_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<
$(INTDIR)/ggmlx86_%.cpp.o: $(GGML_X86_DIR)/%.cpp
	$(CCX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(INTDIR) eboot.bin sce_sys/param.sfo *.pkg
