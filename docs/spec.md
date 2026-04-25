# WPS 文件内容提取库 — 规格说明 (Spec)

## 1. 范围与约定

### 1.1 文档目的
本文档定义 `libwpsextract` 库的详细技术规格，包括 API 接口、数据流、模块划分、状态机等。本文档不包含具体实现代码，仅定义行为契约。

### 1.2 术语定义

| 术语 | 定义 |
|------|------|
| WPS 文字 | WPS Office 文字处理程序生成的 `.wps` 文件 |
| WPS 表格 | WPS Office 电子表格程序生成的 `.et` 文件 |
| WPS 演示 | WPS Office 演示程序生成的 `.dps` 文件 |
| OOXML | Office Open XML，WPS 文件的内部存储格式 |
| ZIP reader | 从 ZIP 归档中读取条目内容的功能模块 |

### 1.3 文档约定
- `[in]` — 调用方传入参数，不得为 NULL（除非明确标注）
- `[out]` — 库写入结果的参数，不得为 NULL
- `[in,out]` — 双向参数
- `[optional]` — 可为 NULL

---

## 2. 模块架构

### 2.1 分层架构

```
┌──────────────────────────────────────┐
│          Public API Layer            │  wpsextract.h
│  (wpsext_init / extract / free …)   │
├──────────────────────────────────────┤
│        Extraction Logic Layer        │  src/*.c
│  ┌──────────┐ ┌───────┐ ┌────────┐  │
│  │ wps_text │ │et_tbl │ │dps_sld │  │
│  └────┬─────┘ └───┬───┘ └───┬────┘  │
├───────┴───────────┴─────────┴────────┤
│          Core Service Layer          │
│  ┌──────────┐ ┌──────────┐          │
│  │  ZIP     │ │   XML    │          │
│  │  Reader  │ │  Parser  │          │
│  └──────────┘ └──────────┘          │
├──────────────────────────────────────┤
│        Utility / Common Layer        │
│  (error handling, allocator, buffer) │
└──────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 职责 | 对外接口 |
|------|------|----------|
| `wpsextract` | 公共 API 入口、库初始化、上下文管理 | `wpsext_init`, `wpsext_extract_file`, … |
| `format_detect` | 判断文件类型（wps/et/dps），识别内部格式 | 内部接口 |
| `zip_reader` | 打开 ZIP 文件，按路径读取条目内容 | 内部接口 |
| `xml_parser` | 解析 XML 文本，提供 SAX 风格的回调式解析 | 内部接口 |
| `wps_text` | 从 .wps 的 `word/document.xml` 提取段落文本 | 内部接口 |
| `et_table` | 从 .et 的 sheet*.xml 及 sharedStrings.xml 提取单元格文本 | 内部接口 |
| `dps_slide` | 从 .dps 的 slide*.xml 提取幻灯片文本 | 内部接口 |

---

## 3. API 详细规格

### 3.1 数据类型

```c
/* 错误码枚举 */
typedef enum {
    WPSEXT_OK             =  0,
    WPSEXT_ERR_FILE       = -1,
    WPSEXT_ERR_FORMAT     = -2,
    WPSEXT_ERR_MEMORY     = -3,
    WPSEXT_ERR_INTERNAL   = -4,
    WPSEXT_ERR_INVALID_ARG = -5,
    WPSEXT_ERR_TOO_LARGE  = -6   /* 文件超过大小限制 */
} wpsext_error_t;

/* 文件类型枚举 */
typedef enum {
    WPSEXT_TYPE_UNKNOWN = 0,
    WPSEXT_TYPE_WPS     = 1,
    WPSEXT_TYPE_ET      = 2,
    WPSEXT_TYPE_DPS     = 3
} wpsext_filetype_t;

/* 提取上下文句柄（不透明指针） */
typedef struct wpsext_ctx wpsext_ctx_t;

/* 提取选项 */
typedef struct {
    size_t          max_file_size;   /* 0 = 无限制 */
    size_t          max_output_size; /* 0 = 无限制 */
    int             include_headers; /* 是否包含页眉页脚 */
    int             structured;      /* 是否结构化输出 */
} wpsext_options_t;
```

### 3.2 完整 API 规范

#### 3.2.1 库初始化

```c
/**
 * @brief 初始化库，必须在其他 API 之前调用
 * @return WPSEXT_OK 成功
 *         WPSEXT_ERR_INTERNAL 初始化失败
 *
 * 线程安全性：非线程安全，应在单线程环境下调用
 * 可重复调用：是，重复调用无副作用
 */
int wpsext_init(void);
```

```c
/**
 * @brief 释放库资源
 *
 * 线程安全性：非线程安全
 * 调用后可以再次 wpsext_init()
 */
void wpsext_cleanup(void);
```

#### 3.2.2 创建与销毁上下文

```c
/**
 * @brief 创建提取上下文
 * @param[in]  opts     提取选项，NULL 表示使用默认值
 * @param[out] ctx      返回上下文句柄
 * @return WPSEXT_OK 成功
 *         WPSEXT_ERR_MEMORY 内存分配失败
 *         WPSEXT_ERR_INVALID_ARG ctx 为 NULL
 */
int wpsext_ctx_create(const wpsext_options_t *opts, wpsext_ctx_t **ctx);
```

```c
/**
 * @brief 销毁上下文，释放关联资源
 * @param[in] ctx  上下文句柄，可为 NULL（无操作）
 */
void wpsext_ctx_destroy(wpsext_ctx_t *ctx);
```

#### 3.2.3 文件类型检测

```c
/**
 * @brief 检测 WPS 文件类型
 * @param[in]  path     文件路径
 * @param[out] type     返回文件类型
 * @return WPSEXT_OK 成功
 *         WPSEXT_ERR_FILE 文件无法访问
 *         WPSEXT_ERR_FORMAT 无法识别格式
 *
 * 检测策略：
 *   1. 文件扩展名 (.wps/.et/.dps) → 快速判断
 *   2. ZIP magic number (PK\x03\x04) → 确认为归档格式
 *   3. 内部特征文件 → 区分 wps/et/dps
 *       - [Content_Types].xml 中包含特定 ContentType
 */
int wpsext_detect_type(const char *path, wpsext_filetype_t *type);
```

#### 3.2.4 文本提取（一次性）

```c
/**
 * @brief 从 WPS 文件一次性提取全部文本
 * @param[in]  ctx      上下文句柄，NULL 表示使用默认设置
 * @param[in]  path     文件路径
 * @param[out] out_text 返回提取的文本（调用方负责释放）
 * @param[out] out_len  返回文本长度（不含终止符），可为 NULL
 * @return WPSEXT_OK 成功
 *         WPSEXT_ERR_FILE 文件无法访问
 *         WPSEXT_ERR_FORMAT 格式不支持或损坏
 *         WPSEXT_ERR_MEMORY 内存不足
 *         WPSEXT_ERR_TOO_LARGE 超过大小限制
 *
 * 行为约定：
 *   - 返回的文本为 UTF-8 编码，以 '\0' 结尾
 *   - 段落之间使用 '\n' 分隔
 *   - 表格单元格之间使用 '\t' 分隔，行之间使用 '\n'
 *   - 幻灯片之间使用 "\n---\n" 分隔
 *   - 调用方必须通过 wpsext_free_text() 释放 out_text
 */
int wpsext_extract_file(wpsext_ctx_t *ctx,
                        const char *path,
                        char **out_text,
                        size_t *out_len);
```

#### 3.2.5 文本提取（流式/回调）

```c
/**
 * @brief 回调函数类型：每提取到一段文本即调用
 * @param[in] chunk  文本块（可能不完整，不以 '\0' 结尾）
 * @param[in] len    文本块字节长度
 * @param[in] userdata  用户自定义数据
 * @return 0 继续提取，非 0 终止提取
 */
typedef int (*wpsext_callback_t)(const char *chunk, size_t len, void *userdata);

/**
 * @brief 以流式回调方式提取文本
 * @param[in] ctx       上下文句柄
 * @param[in] path      文件路径
 * @param[in] callback  回调函数
 * @param[in] userdata  传递给回调的用户数据
 * @return WPSEXT_OK 成功
 *         WPSEXT_ERR_*  其他错误
 *         >0 回调返回的非零值（提取被用户终止）
 */
int wpsext_extract_stream(wpsext_ctx_t *ctx,
                          const char *path,
                          wpsext_callback_t callback,
                          void *userdata);
```

#### 3.2.6 内存释放

```c
/**
 * @brief 释放 wpsext_extract_file() 返回的文本
 * @param[in] text  待释放的文本指针，可为 NULL
 */
void wpsext_free_text(char *text);
```

#### 3.2.7 错误信息

```c
/**
 * @brief 获取错误码对应的描述字符串
 * @param[in] errcode  错误码
 * @return 错误描述字符串（静态存储，不可释放）
 */
const char *wpsext_strerror(int errcode);
```

#### 3.2.8 版本信息

```c
/** 主版本号 */
#define WPSEXT_VERSION_MAJOR 0
/** 次版本号 */
#define WPSEXT_VERSION_MINOR 1
/** 修订号 */
#define WPSEXT_VERSION_PATCH 0

/**
 * @brief 获取版本字符串（格式: "MAJOR.MINOR.PATCH"）
 */
const char *wpsext_version(void);
```

---

## 4. 数据流规格

### 4.1 .wps 文字提取流程

```
输入: .wps 文件
  │
  ├─[1] 打开文件，读取前 4 字节验证 ZIP magic
  │
  ├─[2] 解析 ZIP 中央目录
  │       定位 entry: word/document.xml
  │
  ├─[3] 解压 word/document.xml 条目到内存
  │
  ├─[4] XML SAX 解析
  │       ┌─ 遇到 <w:p>   → 新段落开始
  │       ├─ 遇到 <w:r><w:t> → 捕获文本片段
  │       └─ 遇到 </w:p>  → 段落结束，追加 '\n'
  │
  └─[5] 输出拼接后的完整文本
```

### 4.2 .et 表格提取流程

```
输入: .et 文件
  │
  ├─[1] 验证 ZIP magic
  │
  ├─[2] 定位并解压 xl/sharedStrings.xml
  │       构建共享字符串表（sst[] 数组）
  │
  ├─[3] 遍历所有 xl/worksheets/sheet*.xml
  │       对每个 sheet:
  │       ├─ 解析 <row> 元素
  │       ├─ 对每个 <c>（单元格）:
  │       │   ├─ 若 t="s" → 从 sst[] 取对应索引的文本
  │       │   └─ 若 t="inlineStr" 或无 t → 直接取 <is><t> 文本
  │       └─ 行末追加 '\n'，单元格间追加 '\t'
  │
  └─[4] 输出全部工作表文本
```

### 4.3 .dps 演示提取流程

```
输入: .dps 文件
  │
  ├─[1] 验证 ZIP magic
  │
  ├─[2] 定位 ppt/presentation.xml 获取幻灯片列表
  │
  ├─[3] 遍历所有 ppt/slides/slide*.xml
  │       对每页幻灯片:
  │       ├─ 解析 <a:p>（文本段落）
  │       ├─ 提取 <a:r><a:t> 文本片段
  │       └─ 幻灯片结束追加 "\n---\n"
  │
  └─[4] 输出全部幻灯片文本
```

---

## 5. 内部模块接口规格

### 5.1 ZIP Reader 模块

```c
/* ZIP 条目信息 */
typedef struct {
    char     *name;          /* 条目路径名 */
    size_t    compressed;    /* 压缩后大小 */
    size_t    uncompressed;  /* 解压后大小 */
    uint16_t  method;        /* 压缩方法 (0=store, 8=deflate) */
    uint32_t  crc32;         /* CRC32 校验值 */
} zip_entry_t;

/* ZIP 读句柄（不透明） */
typedef struct zip_reader zip_reader_t;

/**
 * @brief 打开 ZIP 文件
 * @return NULL 打开失败
 */
zip_reader_t *zip_open(const char *path);

/**
 * @brief 关闭 ZIP 文件
 */
void zip_close(zip_reader_t *zr);

/**
 * @brief 遍历 ZIP 条目
 * @param callback  回调，返回非 0 终止遍历
 *                  回调中  entry 仅在回调期间有效
 */
int zip_foreach(zip_reader_t *zr,
                int (*callback)(const zip_entry_t *entry, void *ctx),
                void *ctx);

/**
 * @brief 按名称读取 ZIP 条目内容（解压后）
 * @param[out] out_data  返回数据，调用方负责释放
 * @param[out] out_size  数据大小
 * @return WPSEXT_OK / WPSEXT_ERR_FORMAT / WPSEXT_ERR_MEMORY
 */
int zip_read_entry(zip_reader_t *zr,
                   const char *entry_name,
                   uint8_t **out_data,
                   size_t *out_size);
```

### 5.2 XML Parser 模块

```c
/**
 * @brief SAX 风格 XML 解析器回调
 */
typedef struct {
    /** 元素开始: <name attr="val"> */
    void (*on_start_element)(void *ctx,
                             const char *name,
                             const char **attrs);   /* NULL 终止的键值对数组 */

    /** 元素结束: </name> */
    void (*on_end_element)(void *ctx, const char *name);

    /** 文本内容 */
    void (*on_characters)(void *ctx,
                          const char *data,
                          size_t len);
} xml_sax_handler_t;

/**
 * @brief 解析 XML 数据
 * @param handler  回调集合
 * @param handler_ctx  传递给回调的上下文
 * @param data  XML 数据
 * @param size  数据大小
 * @return WPSEXT_OK / WPSEXT_ERR_FORMAT
 */
int xml_parse_sax(const xml_sax_handler_t *handler,
                  void *handler_ctx,
                  const uint8_t *data,
                  size_t size);
```

### 5.3 格式检测模块（format_detect）

```c
/**
 * @brief 根据文件内容检测 WPS 文件子类型
 *
 * 检测逻辑:
 *   1. 检查是否是有效的 ZIP 文件
 *   2. 查找 [Content_Types].xml 条目
 *   3. 判断 ContentType 是否匹配目标格式
 *
 * @return wpsext_filetype_t 枚举值
 */
wpsext_filetype_t format_detect(zip_reader_t *zr);
```

---

## 6. 错误处理规格

### 6.1 错误传播机制

```
┌─────────┐    返回错误码     ┌─────────┐
│ 上层 API │ ───────────────> │ 调用方  │
└────┬────┘                  └─────────┘
     │ 向上传播
     │
┌────▼────┐
│ 内部模块 │  每个内部函数返回 int 错误码
└────┬────┘  调用栈逐层回传，不丢失错误信息
     │
┌────▼──────┐
│ 工具函数  │ 内存分配失败 → ERR_MEMORY
└───────────┘ IO 失败       → ERR_FILE
             格式错误       → ERR_FORMAT
```

### 6.2 资源清理契约

- 任何返回非 `WPSEXT_OK` 的 API，其 `[out]` 参数状态未定义（调用方不应使用）
- `wpsext_free_text()` 可安全传入 NULL
- `wpsext_ctx_destroy()` 可安全传入 NULL
- 同一上下文不可跨线程使用

---

## 7. 构建规格

### 7.1 输出产物

| 产物 | 路径 | 说明 |
|------|------|------|
| 静态库 | `build/libwpsextract.a` | 静态链接库 |
| 动态库 | `build/libwpsextract.so` | 动态链接库 (Linux) |
| 公开头文件 | `include/wpsextract.h` | 用户引用的头文件 |

### 7.2 编译标志

```makefile
# Release（默认）
CFLAGS  = -std=c99 -Wall -Wextra -O2 -DNDEBUG

# Debug
CFLAGS  = -std=c99 -Wall -Wextra -g -O0
```

### 7.3 外部依赖

| 依赖 | 用途 | 备注 |
|------|------|------|
| zlib | ZIP deflate 解压 | 可选，若不可用则仅支持 store 模式 |
| 无 / libc 内置 | XML 解析 | 自研简易 SAX 解析器 |
| 无 / libc 内置 | ZIP 解析 | 自研简易 ZIP 结构解析 |

> 设计目标：核心功能零外部依赖，zlib 作为可选的增强依赖来支持 deflate 压缩。

---

## 8. 测试规格

### 8.1 测试用例分类

| 类别 | 内容 | 数量要求 |
|------|------|----------|
| 单元测试 | 每个内部模块独立测试（ZIP reader、XML parser、各格式提取器） | 每个模块 ≥ 3 个用例 |
| 集成测试 | 端到端提取完整 WPS/ET/DPS 文件 | ≥ 5 个文件 |
| 异常测试 | 损坏文件、空文件、超大文件、非 WPS 文件 | ≥ 8 个用例 |
| 边界测试 | 空文档、单字符、极限长度、特殊字符/多语言 | ≥ 5 个用例 |

### 8.2 测试数据

测试用 WPS 文件存放于 `tests/data/`：
```
tests/data/
├── simple.wps       # 简单文字文档
├── multilang.wps    # 多语言文字文档
├── empty.wps        # 空文档
├── table.et         # 含数据的表格
├── presentation.dps # 多页演示
├── corrupt.zip      # 损坏的 ZIP
├── not_wps.zip      # 普通的 ZIP 文件
└── huge.wps         # 大文件（可选，不提交到仓库）
```

---

## 9. 命名空间约定

| 分类 | 前缀 | 示例 |
|------|------|------|
| 公共 API | `wpsext_` | `wpsext_init()` |
| 公共类型/枚举 | `wpsext_` | `wpsext_error_t` |
| 公共宏/常量 | `WPSEXT_` | `WPSEXT_ERR_FILE` |
| 内部函数 | 模块名 + `_` | `zip_open()`, `xml_parse_sax()` |
| 内部宏 | 模块大写 + `_` | `ZIP_MAX_ENTRIES` |

---

## 10. 变更记录

| 日期 | 版本 | 变更描述 | 作者 |
|------|------|----------|------|
| 2026-04-25 | v0.1 | 初始版本 | — |

---

*本文档为规格说明，定义了 `libwpsextract` 的行为契约。具体实现需严格遵循本文档定义的所有接口签名、行为语义和错误处理约定。*
