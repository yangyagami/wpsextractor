# libwpsextract Makefile
# WPS 文件内容提取库

CC       ?= gcc
AR       ?= ar
PREFIX   ?= /usr/local

# 编译选项
CFLAGS   ?= -std=c99 -Wall -Wextra -O2 -DNDEBUG -D_POSIX_C_SOURCE=200809L
LDFLAGS  ?=

# Debug 模式: make DEBUG=1
ifdef DEBUG
	CFLAGS = -std=c99 -Wall -Wextra -g -O0 -D_POSIX_C_SOURCE=200809L
endif

# 目录
SRCDIR   = src
INCDIR   = include
BUILDDIR = build
TESTDIR  = tests
EXAMPLEDIR = examples

# 源文件
SRCS = $(SRCDIR)/wpsextract.c \
       $(SRCDIR)/zip_reader.c \
       $(SRCDIR)/xml_parser.c \
       $(SRCDIR)/format_detect.c \
       $(SRCDIR)/wps_text.c \
       $(SRCDIR)/wps_binary.c \
       $(SRCDIR)/et_table.c \
       $(SRCDIR)/et_binary.c \
       $(SRCDIR)/ole2_reader.c \
       $(SRCDIR)/strbuf.c

# 目标文件
OBJS = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# 库文件
STATIC_LIB  = $(BUILDDIR)/libwpsextract.a
SHARED_LIB  = $(BUILDDIR)/libwpsextract.so

# 头文件
HEADER = $(INCDIR)/wpsextract.h

# 需要链接的库 (zlib)
LDLIBS = -lz

.PHONY: all static shared clean install uninstall test example

all: static shared

# 创建构建目录
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# 编译 .o 文件
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(HEADER) $(SRCDIR)/internal.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -fPIC -c $< -o $@

# 静态库
static: $(STATIC_LIB)

$(STATIC_LIB): $(OBJS)
	$(AR) rcs $@ $^

# 动态库
shared: $(SHARED_LIB)

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS) $(LDLIBS)

# 清理
clean:
	rm -rf $(BUILDDIR)
	rm -f $(EXAMPLEDIR)/extract

# 安装
install: all
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/include
	install -m 644 $(STATIC_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(SHARED_LIB) $(DESTDIR)$(PREFIX)/lib/
	install -m 644 $(HEADER) $(DESTDIR)$(PREFIX)/include/
	ldconfig

# 卸载
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/libwpsextract.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libwpsextract.so
	rm -f $(DESTDIR)$(PREFIX)/include/wpsextract.h

# 示例程序
example: $(EXAMPLEDIR)/extract

$(EXAMPLEDIR)/extract: $(EXAMPLEDIR)/extract.c $(STATIC_LIB) $(HEADER)
	$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@ $(STATIC_LIB) $(LDLIBS)

# 测试 (占位)
test:
	@echo "Tests not yet implemented."
