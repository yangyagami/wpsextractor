# 开发里程碑记录

## 里程碑概览

| 里程碑 | 版本 | 内容 | 状态 |
|--------|------|------|------|
| M1 | v0.1.0 | 基础框架：目录结构 + 公开头文件 + 错误体系 + 公共 API | ✅ 已完成 |
| M2 | v0.2.0 | .wps 文字提取：ZIP Reader + XML Parser + wps_text | 🔄 进行中 |
| M3 | v0.3.0 | .et 表格提取 | ⬜ 待开始 |
| M4 | v0.4.0 | .dps 演示提取 | ⬜ 待开始 |
| M5 | v0.5.0 | 流式提取 + 性能优化 | ⬜ 待开始 |
| M6 | v1.0.0 | 完整测试覆盖 + 文档完善 | ⬜ 待开始 |

---

## M1: 基础框架 (v0.1.0) ✅

**目标**：搭建项目骨架，实现公共 API、错误体系、ZIP Reader、XML Parser、格式检测

### 任务清单

- [x] 创建目录结构
- [x] 创建公开头文件 `include/wpsextract.h`（类型定义、API 声明）
- [x] 创建内部头文件 `src/internal.h`
- [x] 实现 `wpsextract.c`（init/cleanup、strerror、version、上下文管理、extract_file、extract_stream）
- [x] 实现 `zip_reader.c`（ZIP 打开/关闭/遍历/读取条目，支持 store 和 deflate(zlib)）
- [x] 实现 `xml_parser.c`（简易 SAX 解析器，支持元素/属性/文本/CDATA/注释/实体解码）
- [x] 实现 `format_detect.c`（通过 [Content_Types].xml 自动识别 wps/et/dps）
- [x] 实现 `strbuf.c`（动态字符串构建器）
- [x] 编写 Makefile（static/shared/clean/install/uninstall/example）
- [x] 编写示例程序 `examples/extract.c`

### 实现日志

| 日期 | 内容 | 备注 |
|------|------|------|
| 2026-04-25 | 全部 8 个源文件实现完成，零警告编译通过 | 仅依赖 zlib |

---

## M2: .wps 文字提取 (v0.2.0) ✅

**目标**：同时支持 OOXML 格式和旧版 WPS 二进制格式的 .wps 文件文本提取

### 任务清单

- [x] 实现 `wps_text.c`（OOXML/ZIP 格式）
- [x] 处理 `<w:p>`, `<w:r>`, `<w:t>` 标签
- [x] 支持 `<w:br/>` 换行符、`<w:tab/>` 制表符
- [x] 支持 `xml:space="preserve"` 空白保留
- [x] 段落间以 `\n` 分隔
- [x] 集成到 `wpsext_extract_file()`
- [x] 实现 `wps_binary.c`（旧版 WPS 二进制格式）
  - [x] OLE2 文件解析：FAT、DIFAT、Mini FAT、Mini Stream、目录遍历
  - [x] FIB 解析：获取 ccpText（正文文本字符数）
  - [x] Pcdt 解析：从 0Table 找到分段表
  - [x] UTF-16LE 文本读取 + 控制字符清洗
- [x] 自动识别 ZIP 和 OLE2 两种 .wps 格式

### 实现日志

| 日期 | 内容 | 备注 |
|------|------|------|
| 2026-04-25 | 完成 wps_text.c (OOXML) | 等待实际 .wps 文件测试 |
| 2026-04-26 | 完成 wps_binary.c (OLE2) + 集成 + 测试通过 | `'这是一个测试。'` 成功提取 |

---

## 当前项目结构

```
wps/
├── Makefile
├── docs/
│   ├── milestones.md
│   ├── requirements.md
│   └── spec.md
├── include/
│   └── wpsextract.h         # 公开 API 头文件
├── src/
│   ├── internal.h           # 内部头文件
│   ├── wpsextract.c         # 公共 API 实现
│   ├── zip_reader.c         # ZIP 文件读取 (store + deflate/zlib)
│   ├── xml_parser.c         # SAX 风格 XML 解析器
│   ├── format_detect.c      # 文件格式自动识别
│   ├── wps_text.c           # .wps 文字提取
│   └── strbuf.c             # 动态字符串构建器
├── examples/
│   └── extract.c            # 命令行提取示例
├── tests/
│   └── data/                # 测试数据目录（待填充）
└── build/                   # 构建产物
    ├── libwpsextract.a
    └── libwpsextract.so
```

## 待实现

- [ ] .et 表格提取 (`et_table.c`)
- [ ] .dps 演示提取 (`dps_slide.c`)
- [ ] 流式提取（真正的流式，非一次性后分块）
- [ ] 单元测试
