# WPS 文件内容提取库 — 需求文档

## 1. 项目概述

### 1.1 项目名称
`libwpsextract` — WPS 文件内容提取库

### 1.2 项目目标
开发一个 C 语言库，用于从 WPS Office 文档中提取文本内容。支持将提取的内容以结构化或纯文本形式返回给调用方，构建系统使用 Makefile。

### 1.3 适用场景
- 全文检索系统中对 WPS 文件建立索引
- 文档格式转换工具（WPS → TXT）
- 内容审查与敏感词过滤
- 办公自动化中的数据提取与归档

---

## 2. 文件格式说明

### 2.1 目标格式

| 扩展名 | 类型 | 说明 |
|--------|------|------|
| `.wps` | WPS 文字 | 对应 Microsoft Word (.docx)，基于 OOXML 的 ZIP 归档 |
| `.et`  | WPS 表格 | 对应 Microsoft Excel (.xlsx)，基于 OOXML 的 ZIP 归档 |
| `.dps` | WPS 演示 | 对应 Microsoft PowerPoint (.pptx)，基于 OOXML 的 ZIP 归档 |

> 以上三种格式均为 ZIP 压缩包，内部包含 XML 文件，文本内容存储于 `word/document.xml`、`xl/sharedStrings.xml`、`ppt/slides/slide*.xml` 等路径中。老旧的 Microsoft Works (.wps) 格式不在本库支持范围内。

---

## 3. 功能需求

### 3.1 核心功能

| 编号 | 功能 | 优先级 | 描述 |
|------|------|--------|------|
| F1 | 提取 WPS 文字(.wps)的文本内容 | P0 | 从 .wps 文件中提取正文文本，保留段落结构 |
| F2 | 提取 WPS 表格(.et)的文本内容 | P0 | 从 .et 文件中提取所有单元格文本 |
| F3 | 提取 WPS 演示(.dps)的文本内容 | P0 | 从 .dps 文件中提取所有幻灯片文本 |
| F4 | 文件格式自动识别 | P1 | 根据文件扩展名或内部特征自动判断文件类型 |
| F5 | 纯文本输出 | P1 | 输出去除格式标记的纯文本 |
| F6 | 结构化输出 | P2 | 按段落/单元格/幻灯片返回结构化数据 |

### 3.2 辅助功能

| 编号 | 功能 | 优先级 | 描述 |
|------|------|--------|------|
| F7 | 错误处理 | P0 | 对损坏文件、不支持的格式、内存不足等异常返回明确错误码 |
| F8 | 内存管理 | P0 | 所有分配的内存需提供对应的释放接口 |
| F9 | 流式提取 | P2 | 支持大文件的流式处理，避免一次性加载全部内容到内存 |
| F10 | 选择性提取 | P3 | 支持只提取指定页面/工作表/幻灯片范围的内容 |

---

## 4. 非功能需求

### 4.1 性能要求
| 编号 | 要求 | 指标 |
|------|------|------|
| N1 | 处理速度 | 10 MB 文件在 3 秒内完成提取（常规 x86 桌面环境） |
| N2 | 内存占用 | 峰值内存不超过文件大小的 3 倍 |
| N3 | 启动延迟 | 库初始化时间 < 50ms |

### 4.2 兼容性
| 编号 | 要求 | 说明 |
|------|------|------|
| N4 | 操作系统 | 支持 Linux，预留 Windows/macOS 移植接口 |
| N5 | C 标准 | C99 或以上 |
| N6 | 编译器 | GCC ≥ 7、Clang ≥ 10 |

### 4.3 可维护性
| 编号 | 要求 | 说明 |
|------|------|------|
| N7 | 依赖最小化 | 尽可能减少外部依赖 |
| N8 | 代码分层 | 公共 API / 格式解析 / 工具函数 清晰分层 |
| N9 | 单元测试 | 每个模块配备对应测试用例 |

### 4.4 安全性
| 编号 | 要求 | 说明 |
|------|------|------|
| N10 | 缓冲区安全 | 所有字符串操作使用安全函数，避免缓冲区溢出 |
| N11 | ZIP 炸弹防护 | 检测并拒绝解压比过高的恶意压缩包 |
| N12 | 输入校验 | 对所有外部输入进行合法性校验 |

---

## 5. 接口需求概述

### 5.1 API 风格
- 使用 C 语言标准调用约定
- 所有公开 API 使用统一前缀 `wpsext_`
- 所有公开 API 均返回错误码以指示执行结果
- 提供句柄（handle）模式管理上下文

### 5.2 期望的核心接口（示例）

```c
// 初始化/清理
int wpsext_init(void);
void wpsext_cleanup(void);

// 从文件提取文本
int wpsext_extract_file(const char *path, char **out_text, size_t *out_len);

// 释放提取结果
void wpsext_free_text(char *text);

// 获取错误描述
const char *wpsext_strerror(int errcode);

// 以回调方式提取（流式）
int wpsext_extract_stream(const char *path,
                          int (*callback)(const char *chunk, size_t len, void *ctx),
                          void *ctx);
```

### 5.3 错误码定义
库需定义清晰的错误码枚举，至少包含：

| 错误码 | 含义 |
|--------|------|
| WPSEXT_OK | 成功 |
| WPSEXT_ERR_FILE | 文件打开/读取失败 |
| WPSEXT_ERR_FORMAT | 格式不支持或文件损坏 |
| WPSEXT_ERR_MEMORY | 内存分配失败 |
| WPSEXT_ERR_INTERNAL | 内部错误 |
| WPSEXT_ERR_INVALID_ARG | 无效参数 |

---

## 6. 构建系统需求

### 6.1 Makefile 目标

| 目标 | 说明 |
|------|------|
| `make` / `make all` | 编译静态库和动态库 |
| `make static` | 仅编译静态库 `libwpsextract.a` |
| `make shared` | 仅编译动态库 `libwpsextract.so` |
| `make test` | 编译并运行测试 |
| `make clean` | 清理构建产物 |
| `make install` | 安装库和头文件到系统目录 |
| `make uninstall` | 卸载已安装的文件 |

### 6.2 编译选项
- 默认启用 `-Wall -Wextra` 警告级别
- Debug 模式与 Release 模式可切换（`DEBUG=1`）
- 支持 `PREFIX` 变量自定义安装路径
- 支持 `CC`、`CFLAGS`、`LDFLAGS` 等标准变量覆盖

---

## 7. 目录结构规划

```
wps/
├── Makefile
├── README.md
├── docs/
│   ├── requirements.md    # 本文档
│   └── spec.md            # 规格说明
├── include/
│   └── wpsextract.h       # 公开头文件
├── src/
│   ├── wpsextract.c       # 公共 API 实现
│   ├── format_detect.c    # 格式识别
│   ├── xml_parser.c       # XML 解析
│   ├── zip_reader.c       # ZIP 读取
│   ├── wps_text.c         # .wps 文字提取
│   ├── et_table.c         # .et 表格提取
│   ├── dps_slide.c        # .dps 演示提取
│   └── internal.h         # 内部头文件
├── tests/
│   ├── test_main.c
│   └── data/              # 测试用 WPS 文件
└── examples/
    └── extract.c          # 示例程序
```

---

## 8. 版本规划

| 版本 | 内容 |
|------|------|
| v0.1.0 | 基础框架：ZIP 读取 + 公共 API + 错误体系 |
| v0.2.0 | .wps 文字提取 |
| v0.3.0 | .et 表格提取 |
| v0.4.0 | .dps 演示提取 |
| v0.5.0 | 流式提取 + 性能优化 |
| v1.0.0 | 完整测试覆盖 + 文档完善 |
