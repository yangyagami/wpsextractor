# WPS 文件内容提取库 — 测试用例文档

## 1. 文档说明

### 1.1 用例格式约定

每个用例采用统一格式：

| 字段 | 说明 |
|------|------|
| **用例编号** | 模块缩写-序号，如 `API-001` |
| **测试类别** | 单元测试 / 集成测试 / 异常测试 / 边界测试 |
| **优先级** | P0（必测）/ P1（重要）/ P2（补充） |
| **前置条件** | 执行用例前需满足的环境与数据条件 |
| **测试输入** | 具体的输入数据或操作 |
| **预期输出** | 预期的返回值、输出内容或行为 |
| **清理步骤** | 用例执行后的资源回收 |

---

## 2. 单元测试 — 公共 API 层

### 2.1 初始化与清理

---

**用例 API-001：正常初始化**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-001 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | 库未初始化 |
| 测试输入 | 调用 `wpsext_init()` |
| 预期输出 | 返回 `WPSEXT_OK`，库处于可用状态 |
| 清理步骤 | 调用 `wpsext_cleanup()` |

---

**用例 API-002：重复初始化**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-002 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化 |
| 测试输入 | 再次调用 `wpsext_init()` |
| 预期输出 | 返回 `WPSEXT_OK`，无副作用，不泄漏资源 |
| 清理步骤 | 调用 `wpsext_cleanup()` |

---

**用例 API-003：清理后重新初始化**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-003 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化并完成清理 |
| 测试输入 | 调用 `wpsext_init()` |
| 预期输出 | 返回 `WPSEXT_OK` |
| 清理步骤 | 调用 `wpsext_cleanup()` |

---

**用例 API-004：未初始化即调用 extract**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-004 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库未初始化或已清理 |
| 测试输入 | 直接调用 `wpsext_extract_file(NULL, "test.wps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_ERR_INTERNAL`（或等价错误），不崩溃 |
| 清理步骤 | 无需清理 |

---

### 2.2 上下文管理

---

**用例 API-010：创建默认上下文**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-010 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | 库已初始化 |
| 测试输入 | 调用 `wpsext_ctx_create(NULL, &ctx)` |
| 预期输出 | 返回 `WPSEXT_OK`，`ctx` 不为 NULL |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

**用例 API-011：创建自定义选项上下文**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-011 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化 |
| 测试输入 | `wpsext_options_t opts = { .max_file_size=1024, .structured=1 }`<br>调用 `wpsext_ctx_create(&opts, &ctx)` |
| 预期输出 | 返回 `WPSEXT_OK`，`ctx` 不为 NULL，选项被正确保存 |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

**用例 API-012：ctx 参数为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-012 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化 |
| 测试输入 | 调用 `wpsext_ctx_create(NULL, NULL)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 无需清理 |

---

**用例 API-013：销毁 NULL 上下文**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-013 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 无 |
| 测试输入 | 调用 `wpsext_ctx_destroy(NULL)` |
| 预期输出 | 无操作，不崩溃 |
| 清理步骤 | 无需清理 |

---

### 2.3 文件类型检测

---

**用例 API-020：检测 .wps 文字文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-020 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/simple.wps` 存在且有效 |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/simple.wps", &type)` |
| 预期输出 | 返回 `WPSEXT_OK`，`type == WPSEXT_TYPE_WPS` |
| 清理步骤 | 无需清理 |

---

**用例 API-021：检测 .et 表格文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-021 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/table.et` 存在且有效 |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/table.et", &type)` |
| 预期输出 | 返回 `WPSEXT_OK`，`type == WPSEXT_TYPE_ET` |
| 清理步骤 | 无需清理 |

---

**用例 API-022：检测 .dps 演示文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-022 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/presentation.dps` 存在且有效 |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/presentation.dps", &type)` |
| 预期输出 | 返回 `WPSEXT_OK`，`type == WPSEXT_TYPE_DPS` |
| 清理步骤 | 无需清理 |

---

**用例 API-023：检测不存在的文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-023 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | 确认 `tests/data/not_exist.xyz` 不存在 |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/not_exist.xyz", &type)` |
| 预期输出 | 返回 `WPSEXT_ERR_FILE` |
| 清理步骤 | 无需清理 |

---

**用例 API-024：检测非 WPS 的 ZIP 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-024 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/not_wps.zip` 是普通 ZIP（不含 WPS 特征） |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/not_wps.zip", &type)` |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT` 或 `type == WPSEXT_TYPE_UNKNOWN` |
| 清理步骤 | 无需清理 |

---

**用例 API-025：检测普通文本文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-025 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/plain.txt` 是普通文本文件 |
| 测试输入 | 调用 `wpsext_detect_type("tests/data/plain.txt", &type)` |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT` |
| 清理步骤 | 无需清理 |

---

### 2.4 文本提取（一次性）

---

**用例 API-030：提取 .wps 文件文本**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-030 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/simple.wps` 含已知文本内容 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/simple.wps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 非 NULL，`len > 0`，内容匹配预期 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 API-031：提取 .et 文件文本**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-031 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/table.et` 含已知单元格数据 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/table.et", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 非 NULL，单元格间以 `\t` 分隔，行间以 `\n` 分隔 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 API-032：提取 .dps 文件文本**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-032 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/presentation.dps` 含多页幻灯片 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/presentation.dps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 非 NULL，幻灯片间以 `\n---\n` 分隔 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 API-033：path 参数为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-033 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化 |
| 测试输入 | `wpsext_extract_file(NULL, NULL, &text, &len)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 无需清理 |

---

**用例 API-034：out_text 参数为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-034 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 存在 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/simple.wps", NULL, &len)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 无需清理 |

---

**用例 API-035：out_len 为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-035 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 存在 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/simple.wps", &text, NULL)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 正常返回 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 API-036：提取空 WPS 文档**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-036 |
| 测试类别 | 边界测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/empty.wps` 无任何文本内容 |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/empty.wps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 返回空字符串 `""`，`len == 0` |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 API-037：超过 max_file_size 限制**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-037 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 创建上下文 `opts.max_file_size = 10`，存在一个 > 10 字节的 WPS 文件 |
| 测试输入 | 使用该上下文提取文件 |
| 预期输出 | 返回 `WPSEXT_ERR_TOO_LARGE` |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

**用例 API-038：超过 max_output_size 限制**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-038 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 创建上下文 `opts.max_output_size = 5`，WPS 文件提取文本 > 5 字节 |
| 测试输入 | 使用该上下文提取文件 |
| 预期输出 | 返回 `WPSEXT_ERR_TOO_LARGE` |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

**用例 API-039：max_file_size = 0（无限制）**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-039 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | 创建上下文 `opts.max_file_size = 0`，使用任意 WPS 文件 |
| 测试输入 | 使用该上下文提取文件 |
| 预期输出 | 返回 `WPSEXT_OK`，正常提取 |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

### 2.5 流式提取

---

**用例 API-040：流式提取 .wps 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-040 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 含多段落文本 |
| 测试输入 | 调用 `wpsext_extract_stream(NULL, "tests/data/simple.wps", callback, userdata)` |
| 预期输出 | 返回 `WPSEXT_OK`，callback 被调用 ≥ 1 次，累积内容与 `wpsext_extract_file` 一致 |
| 清理步骤 | 回调中自行清理 |

---

**用例 API-041：回调中途终止**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-041 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 含多段落 |
| 测试输入 | 回调函数首次调用返回 1（终止），调用流式提取 |
| 预期输出 | 返回 1（回调返回值），callback 只被调用 1 次 |
| 清理步骤 | 无需清理 |

---

**用例 API-042：回调为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-042 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 存在 |
| 测试输入 | `wpsext_extract_stream(NULL, "tests/data/simple.wps", NULL, NULL)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 无需清理 |

---

### 2.6 版本与错误信息

---

**用例 API-050：获取版本字符串**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-050 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | 库已初始化（或未初始化也可） |
| 测试输入 | 调用 `wpsext_version()` |
| 预期输出 | 返回非 NULL 字符串，格式匹配 `MAJOR.MINOR.PATCH`（如 `"0.1.0"`） |
| 清理步骤 | 无需清理 |

---

**用例 API-051：获取错误描述**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-051 |
| 测试类别 | 单元测试 |
| 优先级 | P2 |
| 前置条件 | 无 |
| 测试输入 | 分别对 `WPSEXT_OK`、`WPSEXT_ERR_FILE` 等所有错误码调用 `wpsext_strerror()` |
| 预期输出 | 每个错误码返回不同且非空的描述字符串 |
| 清理步骤 | 无需清理 |

---

**用例 API-052：获取未知错误码的描述**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-052 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | 无 |
| 测试输入 | 调用 `wpsext_strerror(99999)`（未定义错误码） |
| 预期输出 | 返回非 NULL 字符串（如 `"Unknown error"`），不崩溃 |
| 清理步骤 | 无需清理 |

---

**用例 API-053：内存释放安全调用**

| 字段 | 内容 |
|------|------|
| 用例编号 | API-053 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | 无 |
| 测试输入 | 依次调用 `wpsext_free_text(NULL)`、`wpsext_free_text(text_after_extract)` |
| 预期输出 | 均不崩溃，无内存泄漏 |
| 清理步骤 | 无需清理（已清理） |

---

## 3. 单元测试 — ZIP Reader 模块

---

**用例 ZIP-001：打开有效 ZIP 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-001 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/simple.wps` 是有效的 ZIP 归档 |
| 测试输入 | 调用 `zip_open("tests/data/simple.wps")` |
| 预期输出 | 返回非 NULL 句柄 |
| 清理步骤 | 调用 `zip_close(zr)` |

---

**用例 ZIP-002：打开无效文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-002 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/plain.txt` 不是 ZIP 文件 |
| 测试输入 | 调用 `zip_open("tests/data/plain.txt")` |
| 预期输出 | 返回 NULL |
| 清理步骤 | 无需清理 |

---

**用例 ZIP-003：打开不存在的文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-003 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | 文件路径不存在 |
| 测试输入 | 调用 `zip_open("/nonexistent/file.zip")` |
| 预期输出 | 返回 NULL |
| 清理步骤 | 无需清理 |

---

**用例 ZIP-004：遍历条目**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-004 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | 已打开 `tests/data/simple.wps` |
| 测试输入 | 调用 `zip_foreach(zr, callback, ctx)`，回调中收集条目名列表 |
| 预期输出 | 返回 `WPSEXT_OK`，收集到的条目列表包含 `word/document.xml`、`[Content_Types].xml` 等 |
| 清理步骤 | 调用 `zip_close(zr)` |

---

**用例 ZIP-005：遍历时空回调为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-005 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 已打开有效 ZIP 文件 |
| 测试输入 | 调用 `zip_foreach(zr, NULL, NULL)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 调用 `zip_close(zr)` |

---

**用例 ZIP-006：按名读取存在的条目**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-006 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | 已打开 `tests/data/simple.wps` |
| 测试输入 | `zip_read_entry(zr, "word/document.xml", &data, &size)` |
| 预期输出 | 返回 `WPSEXT_OK`，`data` 非 NULL，`size > 0`，内容为有效 XML |
| 清理步骤 | 释放 data，调用 `zip_close(zr)` |

---

**用例 ZIP-007：按名读取不存在的条目**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-007 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 已打开有效 ZIP 文件 |
| 测试输入 | `zip_read_entry(zr, "nonexistent/path.xml", &data, &size)` |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT`（条目不存在） |
| 清理步骤 | 调用 `zip_close(zr)` |

---

**用例 ZIP-008：读取损坏 ZIP 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-008 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | `tests/data/corrupt.zip` 是损坏的 ZIP 文件 |
| 测试输入 | 打开文件并尝试遍历/读取 |
| 预期输出 | `zip_open` 返回 NULL 或 `zip_foreach`/`zip_read_entry` 返回错误码 |
| 清理步骤 | 调用 `zip_close(zr)`（如打开成功） |

---

**用例 ZIP-009：空 ZIP 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-009 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | 有一个不含任何条目的有效 ZIP 文件 |
| 测试输入 | 打开并遍历条目 |
| 预期输出 | `zip_open` 成功，`zip_foreach` 返回 `WPSEXT_OK`，回调未被调用 |
| 清理步骤 | 调用 `zip_close(zr)` |

---

**用例 ZIP-010：ZIP 炸弹防护**

| 字段 | 内容 |
|------|------|
| 用例编号 | ZIP-010 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 有一个解压比极大的 ZIP 文件（如 1KB 压缩 → 1GB 解压） |
| 测试输入 | 尝试读取条目内容 |
| 预期输出 | 返回 `WPSEXT_ERR_TOO_LARGE` 或解压大小超过限制时拒绝解压 |
| 清理步骤 | 调用 `zip_close(zr)` |

---

## 4. 单元测试 — XML Parser 模块

---

**用例 XML-001：解析简单 XML**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-001 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | XML 数据：`<root>hello</root>` |
| 测试输入 | 调用 `xml_parse_sax(handler, ctx, data, size)` |
| 预期输出 | 回调依次收到：`on_start_element("root")` → `on_characters("hello", 5)` → `on_end_element("root")`<br>返回 `WPSEXT_OK` |
| 清理步骤 | 无需清理 |

---

**用例 XML-002：解析嵌套 XML**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-002 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | XML 数据：`<a><b>text</b></a>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | 回调顺序：`<a>` → `<b>` → `text` → `</b>` → `</a>` |
| 清理步骤 | 无需清理 |

---

**用例 XML-003：解析带属性的 XML**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-003 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | XML 数据：`<p id="1" style="bold">text</p>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | `on_start_element` 收到 `name="p"`, `attrs={"id","1","style","bold",NULL}` |
| 清理步骤 | 无需清理 |

---

**用例 XML-004：解析空元素**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-004 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | XML 数据：`<root/>` 或 `<root></root>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | 两种形式均返回 `WPSEXT_OK`，回调按 SAX 规则处理 |
| 清理步骤 | 无需清理 |

---

**用例 XML-005：解析大文本**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-005 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | XML 数据含 10KB 文本节点 |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | `on_characters` 可能被分多次调用，累积长度 = 10KB |
| 清理步骤 | 无需清理 |

---

**用例 XML-006：解析无效 XML（未闭合标签）**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-006 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | XML 数据：`<root>text`（缺少 `</root>`） |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT` |
| 清理步骤 | 无需清理 |

---

**用例 XML-007：解析带 XML 声明的文档**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-007 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | XML 数据：`<?xml version="1.0"?><root>text</root>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | 返回 `WPSEXT_OK`，XML 声明被正确跳过 |
| 清理步骤 | 无需清理 |

---

**用例 XML-008：解析带命名空间的 XML**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-008 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | XML 数据：`<w:p xmlns:w="...">text</w:p>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | 返回 `WPSEXT_OK`，`on_start_element` 收到名称为 `w:p`（或 `p`，取决于实现约定） |
| 清理步骤 | 无需清理 |

---

**用例 XML-009：解析 XML 实体**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-009 |
| 测试类别 | 单元测试 |
| 优先级 | P2 |
| 前置条件 | XML 数据：`<root>&lt;hello&gt;</root>` |
| 测试输入 | 调用 `xml_parse_sax(...)` |
| 预期输出 | `on_characters` 收到 `<hello>`（实体被解码） |
| 清理步骤 | 无需清理 |

---

**用例 XML-010：handler 为 NULL**

| 字段 | 内容 |
|------|------|
| 用例编号 | XML-010 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 任意 XML 数据 |
| 测试输入 | 调用 `xml_parse_sax(NULL, NULL, data, size)` |
| 预期输出 | 返回 `WPSEXT_ERR_INVALID_ARG` |
| 清理步骤 | 无需清理 |

---

## 5. 单元测试 — 格式检测模块

---

**用例 FMT-001：通过扩展名检测 .wps**

| 字段 | 内容 |
|------|------|
| 用例编号 | FMT-001 |
| 测试类别 | 单元测试 |
| 优先级 | P1 |
| 前置条件 | `tests/data/simple.wps` 是有效的 WPS 文字文件 |
| 测试输入 | 调用内部 `format_detect()` |
| 预期输出 | 返回 `WPSEXT_TYPE_WPS` |
| 清理步骤 | 关闭 ZIP 句柄 |

---

**用例 FMT-002：通过 [Content_Types].xml 区分格式**

| 字段 | 内容 |
|------|------|
| 用例编号 | FMT-002 |
| 测试类别 | 单元测试 |
| 优先级 | P0 |
| 前置条件 | 三个文件：`simple.wps`、`table.et`、`presentation.dps` |
| 测试输入 | 分别检测三个文件 |
| 预期输出 | 分别返回 `WPSEXT_TYPE_WPS`、`WPSEXT_TYPE_ET`、`WPSEXT_TYPE_DPS` |
| 清理步骤 | 关闭 ZIP 句柄 |

---

**用例 FMT-003：检测无扩展名但有效的 WPS 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | FMT-003 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | 将 `simple.wps` 重命名为 `nodotfile`（无扩展名） |
| 测试输入 | 调用 `wpsext_detect_type("nodotfile", &type)` |
| 预期输出 | 返回 `WPSEXT_OK`，`type == WPSEXT_TYPE_WPS`（通过内部特征检测） |
| 清理步骤 | 恢复文件名 |

---

**用例 FMT-004：错误扩展名但内容是 WPS**

| 字段 | 内容 |
|------|------|
| 用例编号 | FMT-004 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | 将 `simple.wps` 重命名为 `fake.txt` |
| 测试输入 | 调用 `wpsext_detect_type("fake.txt", &type)` |
| 预期输出 | 返回 `WPSEXT_OK`，通过内部特征正确识别为 `WPSEXT_TYPE_WPS` |
| 清理步骤 | 恢复文件名 |

---

## 6. 集成测试 — 端到端提取

---

**用例 INT-001：提取 WPS 文字文档全文**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-001 |
| 测试类别 | 集成测试 |
| 优先级 | P0 |
| 前置条件 | `simple.wps` 包含三个段落："第一段"、"第二段"、"第三段" |
| 测试输入 | `wpsext_extract_file(NULL, "tests/data/simple.wps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，`text` 的内容为 `"第一段\n第二段\n第三段\n"` |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 INT-002：提取多语言文字**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-002 |
| 测试类别 | 集成测试 |
| 优先级 | P0 |
| 前置条件 | `multilang.wps` 包含中文、日文、英文、阿拉伯文混合文本 |
| 测试输入 | 提取该文件 |
| 预期输出 | 返回 `WPSEXT_OK`，UTF-8 编码正确，各种语言文字无乱码 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 INT-003：提取带格式的文档**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-003 |
| 测试类别 | 集成测试 |
| 优先级 | P1 |
| 前置条件 | `formatted.wps` 含粗体、斜体、不同字号、表格等 |
| 测试输入 | 提取该文件 |
| 预期输出 | 返回 `WPSEXT_OK`，文本内容正确提取，格式标记已被剥离 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 INT-004：提取表格多工作表**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-004 |
| 测试类别 | 集成测试 |
| 优先级 | P0 |
| 前置条件 | `table.et` 含两个工作表：Sheet1 (2行3列)、Sheet2 (1行2列) |
| 测试输入 | 提取该文件 |
| 预期输出 | 返回 `WPSEXT_OK`，文本包含所有单元格数据，单元格间以 `\t` 分隔 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 INT-005：提取演示多幻灯片**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-005 |
| 测试类别 | 集成测试 |
| 优先级 | P0 |
| 前置条件 | `presentation.dps` 含 3 页幻灯片，每页含标题和正文 |
| 测试输入 | 提取该文件 |
| 预期输出 | 返回 `WPSEXT_OK`，输出包含幻灯片分隔符 `\n---\n`，共 3 段 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 INT-006：初始化→提取→清理 完整生命周期**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-006 |
| 测试类别 | 集成测试 |
| 优先级 | P0 |
| 前置条件 | 所有测试文件可用 |
| 测试输入 | 顺序执行：`init()` → `extract(simple.wps)` → `extract(table.et)` → `extract(presentation.dps)` → `cleanup()` |
| 预期输出 | 三次提取均成功，无内存泄漏 |
| 清理步骤 | 调用 `wpsext_cleanup()` |

---

**用例 INT-007：流式提取大文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | INT-007 |
| 测试类别 | 集成测试 |
| 优先级 | P1 |
| 前置条件 | `huge.wps` 文件大小 > 5 MB |
| 测试输入 | 使用 `wpsext_extract_stream` 流式提取，回调拼装完整文本 |
| 预期输出 | 返回 `WPSEXT_OK`，拼装结果与 `wpsext_extract_file` 结果一致 |
| 清理步骤 | 无需清理 |

---

## 7. 异常测试

---

**用例 ERR-001：损坏的 ZIP 内部结构**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-001 |
| 测试类别 | 异常测试 |
| 优先级 | P0 |
| 前置条件 | `corrupt.zip` 是损坏的 ZIP 文件 |
| 测试输入 | 尝试打开并提取 |
| 预期输出 | 返回非 `WPSEXT_OK` 错误码，不崩溃，无内存泄漏 |
| 清理步骤 | 释放可能分配的资源 |

---

**用例 ERR-002：空文件（0 字节）**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-002 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 有一个 0 字节文件 `empty_file` |
| 测试输入 | `wpsext_detect_type` 和 `wpsext_extract_file` |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT` |
| 清理步骤 | 无需清理 |

---

**用例 ERR-003：仅 ZIP 头但内容截断**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-003 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | 文件仅有 4 字节 ZIP magic `PK\x03\x04`，后续内容缺失 |
| 测试输入 | 尝试打开并提取 |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT`，不崩溃 |
| 清理步骤 | 释放可能分配的资源 |

---

**用例 ERR-004：有效 ZIP 但内部 XML 损坏**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-004 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | WPS 文件中 `word/document.xml` 内容被随机覆盖 |
| 测试输入 | 尝试提取 |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT`，不崩溃 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 ERR-005：WPS 文件缺少 document.xml**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-005 |
| 测试类别 | 异常测试 |
| 优先级 | P1 |
| 前置条件 | ZIP 文件包含 `[Content_Types].xml` 表明为 WPS 但缺少 `word/document.xml` |
| 测试输入 | 尝试提取 |
| 预期输出 | 返回 `WPSEXT_ERR_FORMAT` |
| 清理步骤 | 无需清理 |

---

**用例 ERR-006：OOM 模拟（内存分配失败）**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-006 |
| 测试类别 | 异常测试 |
| 优先级 | P2 |
| 前置条件 | 通过注入/模拟使 `malloc` 返回 NULL |
| 测试输入 | 执行提取操作 |
| 预期输出 | 返回 `WPSEXT_ERR_MEMORY`，不崩溃，不泄漏已分配资源 |
| 清理步骤 | 恢复 malloc 行为 |

---

**用例 ERR-007：并发调用（同一上下文）**

| 字段 | 内容 |
|------|------|
| 用例编号 | ERR-007 |
| 测试类别 | 异常测试 |
| 优先级 | P2 |
| 前置条件 | 库已初始化，已创建上下文 |
| 测试输入 | 两个线程同时使用同一上下文提取不同文件 |
| 预期输出 | 未定义行为 → 文档说明「不保证线程安全」，至少不应崩溃 |
| 清理步骤 | 调用 `wpsext_ctx_destroy(ctx)` |

---

## 8. 边界测试

---

**用例 EDGE-001：单字符文档**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-001 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | WPS 文件仅含一个字符 `"A"` |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`，`text = "A\n"` 或 `"A"` |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-002：超长段落（单段 > 1MB）**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-002 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | WPS 文件包含一个长度 > 1MB 的段落 |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`，文本完整提取，长度正确 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-003：特殊字符（\0、制表符、换行符）**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-003 |
| 测试类别 | 边界测试 |
| 优先级 | P1 |
| 前置条件 | WPS 文件中包含制表符、换行符等特殊字符 |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`，特殊字符正确处理（文本内不应有裸 `\0`） |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-004：大量嵌套元素**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-004 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | WPS 文件 XML 嵌套深度 > 50 层 |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`（SAX 解析不关心深度），文本正确提取 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-005：文件名含特殊字符**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-005 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | WPS 文件路径含空格、中文、特殊符号（如 `/tmp/测试 文件.wps`） |
| 测试输入 | `wpsext_extract_file(NULL, "/tmp/测试 文件.wps", &text, &len)` |
| 预期输出 | 返回 `WPSEXT_OK`，正常提取 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-006：use-after-free 防护**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-006 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | 提取完成后 `wpsext_free_text(text)` |
| 测试输入 | 再次调用 `wpsext_free_text(text)` |
| 预期输出 | 不崩溃（通过内部标记或 NULL 后忽略） |
| 清理步骤 | 无需清理 |

---

**用例 EDGE-007：压缩方式为 store 的 WPS 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-007 |
| 测试类别 | 边界测试 |
| 优先级 | P2 |
| 前置条件 | WPS 文件内部条目使用 store 模式（method=0，不压缩） |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`，正常提取 |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

**用例 EDGE-008：压缩方式为 deflate 的 WPS 文件**

| 字段 | 内容 |
|------|------|
| 用例编号 | EDGE-008 |
| 测试类别 | 边界测试 |
| 优先级 | P0 |
| 前置条件 | WPS 文件内部条目使用 deflate 模式（method=8） |
| 测试输入 | 提取文本 |
| 预期输出 | 返回 `WPSEXT_OK`，正常提取（依赖 zlib 可用） |
| 清理步骤 | 调用 `wpsext_free_text(text)` |

---

## 9. 测试数据清单

### 9.1 需准备的测试文件

| 文件名 | 说明 | 对应用例 |
|--------|------|----------|
| `simple.wps` | 含 3 段简单文本的 WPS 文字文档 | API-020, API-030, ZIP-004, INT-001 |
| `multilang.wps` | 含中/日/英/阿拉伯文的多语言文档 | INT-002 |
| `formatted.wps` | 含粗/斜/表格/不同字号 | INT-003 |
| `empty.wps` | 空文档（无文本内容） | API-036 |
| `single_char.wps` | 仅含一个字符 | EDGE-001 |
| `long_paragraph.wps` | 单段落 > 1MB | EDGE-002 |
| `table.et` | 含 2 个工作表，有数据 | API-021, API-031, INT-004 |
| `presentation.dps` | 含 3 页幻灯片 | API-022, API-032, INT-005 |
| `corrupt.zip` | 损坏的 ZIP 文件 | ZIP-008, ERR-001 |
| `not_wps.zip` | 普通 ZIP 文件（不含 WPS 特征） | API-024 |
| `plain.txt` | 普通文本文件 | API-025, ZIP-002 |
| `empty_file` | 0 字节空文件 | ERR-002 |
| `truncated.zip` | 仅 ZIP magic 的文件 | ERR-003 |
| `store_mode.wps` | 内部使用 store 压缩模式 | EDGE-007 |
| `huge.wps` | 大文件（> 5MB） | INT-007 |

### 9.2 测试数据生成建议

- WPS 文字文档可用 WPS Office 或 LibreOffice 创建后保存为 `.wps` 格式
- `.et` 和 `.dps` 同理
- 损坏/特殊文件用手工构造（二进制编辑器或脚本）
- 大文件由脚本生成包含大量段落的文档

---

## 10. 用例统计

| 类别 | 用例数 | P0 | P1 | P2 |
|------|--------|----|----|-----|
| 公共 API — 初始化/清理 | 4 | 1 | 3 | 0 |
| 公共 API — 上下文管理 | 4 | 1 | 3 | 0 |
| 公共 API — 格式检测 | 6 | 4 | 1 | 1 |
| 公共 API — 一次性提取 | 10 | 4 | 5 | 1 |
| 公共 API — 流式提取 | 3 | 0 | 2 | 1 |
| 公共 API — 版本/错误 | 4 | 0 | 2 | 2 |
| ZIP Reader | 10 | 5 | 4 | 1 |
| XML Parser | 10 | 3 | 6 | 1 |
| 格式检测 | 4 | 1 | 1 | 2 |
| 集成测试 | 7 | 5 | 2 | 0 |
| 异常测试 | 7 | 1 | 5 | 1 |
| 边界测试 | 8 | 1 | 4 | 3 |
| **合计** | **77** | **26** | **38** | **13** |

---

*本文档定义了 libwpsextract 的测试用例。所有用例应在 CI 中自动化执行，P0 用例作为合入门槛。*
