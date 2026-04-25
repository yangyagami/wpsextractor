# 测试数据准备清单

所有文件放入 `tests/data/` 目录。

---

## 一、WPS 文字 (.wps) — 用 WPS 文字创建

### 1. simple.wps
创建三段落文字：
```
第一段：这是第一段测试文本。
第二段：这是第二段测试文本。
第三段：这是第三段测试文本。
```
保存为 `simple.wps`。

### 2. empty.wps
新建空白文档，不输入任何内容，直接保存为 `empty.wps`。

### 3. multilang.wps
在文档中依次输入以下内容（每行一段）：
```
Hello World! This is English text.
你好世界！这是中文文本。
こんにちは世界！これは日本語のテキストです。
مرحبا بالعالم! هذا نص عربي.
```
保存为 `multilang.wps`。

### 4. formatted.wps
创建带有格式的文档：
- 第一段：正常文字"这是普通文字。"
- 第二段：**加粗**文字"这是加粗文字。"（选中后 Ctrl+B）
- 第三段：*斜体*文字"这是斜体文字。"（选中后 Ctrl+I）
- 插入一个 2 行 × 2 列的表格，内容如下：

  | 姓名 | 年龄 |
  |------|------|
  | 张三 | 28   |

保存为 `formatted.wps`。

> 预期：提取后只保留纯文本，格式标记被剥离，表格按 `\t` 分隔列、`\n` 分隔行。

### 5. single_char.wps
只输入一个字符 `A`，保存为 `single_char.wps`。

### 6. long_paragraph.wps
创建一个超长段落。可以这样操作：
- 输入一段任意文字
- 复制粘贴该段落多次，直到内容长度超过 1MB
- 可以通过查看文件属性确认大小
保存为 `long_paragraph.wps`。

> 如果手动操作太慢，可以跳过此文件，后续用脚本补充。

---

## 二、WPS 表格 (.et) — 用 WPS 表格创建

### 7. table.et
**Sheet1** 内容（3 行 × 3 列）：

| 商品 | 数量 | 单价 |
|------|------|------|
| 苹果 | 10   | 5.0  |
| 香蕉 | 20   | 3.0  |

**Sheet2** 内容（2 行 × 2 列）：

| 日期       | 备注   |
|------------|--------|
| 2026-04-25 | 测试   |

保存为 `table.et`。

> 预期提取：Sheet1 和 Sheet2 数据均被提取，列间 `\t`、行间 `\n`。

---

## 三、WPS 演示 (.dps) — 用 WPS 演示创建

### 8. presentation.dps
创建 3 页幻灯片：

- **第 1 页**：标题输入"项目介绍"，正文输入"这是一个测试项目。"
- **第 2 页**：标题输入"功能说明"，正文输入"支持文字、表格、演示提取。"
- **第 3 页**：标题输入"总结"，正文输入"测试完成。"

保存为 `presentation.dps`。

> 预期提取：三页幻灯片文本用 `\n---\n` 分隔。

---

## 四、异常/边界文件 — 手工构造

以下文件需要用其他方式构造（ZIP 操作或二进制编辑）：

### 9. corrupt.zip
复制任意有效的 `.wps` 文件，用十六进制编辑器（如 `hexedit` 或 `dd`）从中间裁掉一截：
```bash
# 取前 70% 部分，模拟截断
dd if=simple.wps of=corrupt.zip bs=1 count=$(($(stat -c%s simple.wps) * 70 / 100))
```

### 10. not_wps.zip
创建一个普通 ZIP 包，里面随便放几个文本文件：
```bash
echo "hello" > a.txt
echo "world" > b.txt
zip not_wps.zip a.txt b.txt
rm a.txt b.txt
```

### 11. plain.txt
随便写一行文字保存：
```bash
echo "This is a plain text file." > plain.txt
```

### 12. empty_file
```bash
touch empty_file
```

### 13. truncated.zip
```bash
# 只写 ZIP magic 头 4 字节
printf 'PK\x03\x04' > truncated.zip
```

### 14. store_mode.wps
正常 WPS 文件默认是 deflate 压缩。要创建 store 模式（不压缩）的文件，可以：
```bash
# 用 zip -0（零压缩）重新打包一个 WPS 文件
cp simple.wps tmp.zip
unzip tmp.zip -d tmpdir
cd tmpdir && zip -0 -r ../store_mode.wps * && cd ..
rm -rf tmpdir tmp.zip
```

### 15. huge.wps
如果 `long_paragraph.wps` 已满足 > 5MB，直接复制一份即可；否则复制 `simple.wps` 中的 XML 内容多次后重新打包使其 > 5MB。

---

## 汇总

| # | 文件名 | 创建方式 | 必须 |
|---|--------|----------|------|
| 1 | `simple.wps` | WPS 文字 | ✅ |
| 2 | `empty.wps` | WPS 文字 | ✅ |
| 3 | `multilang.wps` | WPS 文字 | ✅ |
| 4 | `formatted.wps` | WPS 文字 | ✅ |
| 5 | `single_char.wps` | WPS 文字 | ✅ |
| 6 | `long_paragraph.wps` | WPS 文字 | 可选 |
| 7 | `table.et` | WPS 表格 | ✅ |
| 8 | `presentation.dps` | WPS 演示 | ✅ |
| 9 | `corrupt.zip` | `dd` 截断 | ✅ |
| 10 | `not_wps.zip` | `zip` 命令 | ✅ |
| 11 | `plain.txt` | `echo` | ✅ |
| 12 | `empty_file` | `touch` | ✅ |
| 13 | `truncated.zip` | `printf` | ✅ |
| 14 | `store_mode.wps` | `zip -0` | 可选 |
| 15 | `huge.wps` | 脚本/手动 | 可选 |
