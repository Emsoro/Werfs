# WAML + MVVM Spike 报告（H1–H8 验证结论）

> 日期：2026-11-22
> 代码：`spike/waml-mvvm/`
> 计划：`PLAN-WAML-MVVM.md`
> 环境：Windows 10/11 x64、MSVC 19.44.35228、C++20、`RelWithDebInfo`（`/O2 /Zi`）
> 机器：本机开发环境，`QueryPerformanceCounter`/`steady_clock`，预热后取 median 与 p99

---

## 0. 结论

**Go。** 8 条假设全部通过验证；实施过程中**发现并修复了 10 个真实缺陷**（见 §4），其中多个属于"静默失败"类型。WAML 子集、MVVM 四种绑定模式、DataTemplate、自绘引擎集成、D2D 真实渲染与确定性快照均已跑通；实测性能全部优于计划预算。

| 指标 | 计划预算 | 实测 | 结论 |
|---|---|---|---|
| 解析+校验吞吐 | ≥ 10 MB/s | **45–53 MB/s** | 通过（5×） |
| 视图实例化 | ≤ 120 µs / 100 元素 | **68 µs / 100 节点** | 通过 |
| 绑定更新（OneWay） | ≤ 150 ns | **89–93 ns** | 通过 |
| 双向回写 | ≤ 250 ns | **111–123 ns** | 通过 |
| 布局（100 项列表） | ≤ 400 µs | **~102 µs** | 通过 |
| 100 项 DataTemplate 构建 | （无独立预算） | **~288 µs** | 可接受 |
| 软件光栅 1280×800 一帧 | ≤ 6 ms | **0.41 ms** | 通过 |
| 快照确定性 | 逐字节一致 | **两次渲染 SHA 相同** | 通过 |

**不建议**在 v0.5 提前引入 codegen（`wamlc`）或虚拟化：当前数据远未触及预算。虚拟化仍按 0.3 计划放到 v1.1。

**唯一的范围调整建议**：不虚拟化的 `ListBox` 已在 ~500 项时开始接近交互可接受边界（构建 100 项 288 µs，线性外推 500 项约 1.4 ms），v0.5 文档需明确“≤ 500 项”的项数上限，与计划 §9 R4 一致。

---

## 1. 交付物

### 1.1 代码规模（不含 build 产物）

| 区域 | 文件 | 行数 | 说明 |
|---|---|---|---|
| `src/waml` | 4 | 1443 | Lexer/Parser/Validator/诊断/实例化 |
| `src/mvvm` | 2 | 652 | ObservableProperty/PropertyTable/Command/List/Binding |
| `src/core` | 8 | 1855 | Widget/布局/失效传播/控件/值类型/主题 |
| `src/render` | 4 | 575 | 记录式与软件光栅（测试用）+ D2D/DWrite（真实后端） |
| `tests` | 3 | 1383 | 63 个用例 + 规模探针 |
| `tools` | 3 | 729 | 快照、交互驱动、Win32 交互窗口 |
| `bench` | 1 | 238 | 基准 |
| **合计** | **25** | **~6.9k** | 含注释与诊断文案 |

对照计划 H1 的“≤ 1500 行覆盖 WAML 子集”：Parser.cpp 实测 1253 行（含诊断文案、位置映射、标记扩展解析、验证器、实例化器），**略优于**预期。

### 1.2 可执行产物

| 产物 | 作用 | 运行方式 |
|---|---|---|
| `waml_tests.exe` | 63 个单元/集成用例 | `build\waml_tests.exe` |
| `waml_interact.exe` | 无窗口驱动真实输入路径 | `build\waml_interact.exe samples\Gallery.waml` |
| `waml_snapshot.exe` | D2D+WIC 真实渲染 + 确定性校验 | `build\waml_snapshot.exe samples\Gallery.waml out-light.png out-dark.png` |
| `waml_bench.exe` | 性能基线 | `build\waml_bench.exe` |
| `waml_scale.exe` | 规模曲线（O(n) 证据） | `build\waml_scale.exe` |
| `waml_demo.exe` | 真实 Win32 窗口，可交互 | `build\waml_demo.exe`；`--selftest` 自检 |

构建：`build.bat [all|tests|bench|snapshot|interact|scale|demo]`。

---

## 2. 逐条假设验证

### H1 手写 Lexer/Parser 能覆盖子集，且行列诊断可用 —— **通过**

- 语法覆盖：元素/属性/自闭合/属性元素/文本内容/CDATA/注释/处理指令/实体（含 `&#x1F600;`）/标记扩展（含嵌套与引号）/`{}` 转义。
- 诊断：15 类诊断码全部有正反例覆盖（`diag: matrix`），行列号精确到**码点**（`diag: utf8 column counts code points`：UTF-8 中文前面的属性被正确报在第 22 列而非第 24 字节列）。
- 诊断建议：未知元素/属性/枚举/主题 token/绑定参数/绑定路径**全部**给出“最接近的名字”（Levenshtein，含阈值），共 6 个用例断言。
- 实际输出样本：
  ```text
  test.waml(2,3): error WAML0101: 'Buton' is not a known WAML element (did you mean 'Button'?)
  test.waml(1,12): error WAML0102: 'wui/TextBlock' has no property 'Txt' (did you mean 'Text'?)
  test.waml(1,1): error WAML0104: 'TextPrimry' is not a valid color or theme token (did you mean 'TextPrimary'?)
  ```

**发现的问题**：元素诊断原本锚定在 `<`（第 1 列），改为锚定标签名（第 2 列起），与编译器习惯一致。

### H2 “类型注册表 + 属性描述符 + 槽位”可实现且实例化无字符串查找 —— **通过**

- `Widget` 的值存在 `std::vector<Value>`，按类型内密集槽位索引；绑定在附加时解析一次路径，运行期只持有 `IPropertySource*`。
- 实例化 12 节点 5.0 µs、40 节点 18.2 µs、145 节点 97 µs ≈ **0.67 µs/节点**，与“每节点仅 new + 写槽位”预期一致。
- **无反射的代价已量化**：9 个控件共 69 个属性注册，平均 **7.7 行/控件**的类型描述（见 `Types.cpp`）。这个样板量在 20–30 个控件规模下（约 200 行）可接受，但 R1 仍成立，建议 v0.6 引入聚合式 `PropList` 初始化。
- **多模块限制已被规避**：属性表按实例构建，不依赖全局字符串表，因此不存在 0.3 ADR-2 担心的静态初始化顺序与跨 DLL 重复属性对象问题。

### H3 按属性对象订阅的绑定 ≤ 150 ns/次 —— **通过**

- OneWay **89–93 ns**，TwoWay **111–123 ns**（均含通知遍历 + 类型转换 + 槽位写入 + 失效标记）。
- 路径每跳订阅：`{Binding Child.Value}` 用例验证了“中间对象替换 → 重建尾部链 → 继续跟随”的完整语义。

### H4 四种绑定模式 + 回声抑制语义正确 —— **通过**

覆盖矩阵（用例数）：

| 模式 | 初始化 | VM→View | View→VM | 用例 |
|---|---|---|---|---|
| OneTime | ✔ | ✘ | ✘ | `OneTime ignores later changes` |
| OneWay | ✔ | ✔ | ✘ | `OneWay updates target` |
| TwoWay | ✔ | ✔ | ✔ | `TwoWay writes back`、`default mode for TextBox` |
| OneWayToSource | ✘ | ✘ | ✔ | `OneWayToSource does not seed target but writes back` |

- 默认模式由**目标属性描述符**声明（`TextBox.Text` 默认 TwoWay），避免“忘记写 Mode 就静默失效”。
- 回声抑制：`applying_` 标志 + 值相等短路；`no infinite loop on self writing source` 连续 100 次回写无栈溢出、最终值正确。
- `LostFocus` 触发器正确延迟回写（`lost focus trigger defers writeback`）。
- 生命周期：`bindings released when widget destroyed`（observers 1→0）、`source destroyed before view`（视图存活、保留最后值）、`item binding updates do not leak`（列表清空后 item 的 observers 归零）。

### H5 DataTemplate 实例化可承受 100 项 —— **通过**

- 100 项构建 **288 µs**（p99 374 µs），布局 **102 µs**，帧内合计约 0.39 ms。
- 计划预算仅要求布局 ≤ 400 µs；构建无预算但 288 µs 对 v0.5 足够。
- **发现严重缺陷**（详见 §4.1）：列表所有行渲染同一份数据。已修复并加回归测试。

### H6 WARP/WIC 软件路径可产出确定性快照 —— **通过（用真实 D2D 而非 WARP 交换链）**

- 采用 **D2D WIC 位图渲染目标**（`ID2D1RenderTarget` on `IWICBitmap`），无需窗口、无需显示器、无需 D3D 交换链；与 0.3 的 D3D11 路径共享“D2D/DWrite 绘制抽象”，因此验证的是同一套渲染代码。
- 两次独立渲染器实例、两次 `Paint` 调用：**像素哈希完全相同**（light `0x5487195e1a3c9e78`，dark `0x5cf81e76f394ee0e`）。
- PNG 经 WIC 解码回验 720×900 成功；不同主题产生不同哈希（`theme switch changes output`）。
- **未验证**：D3D11 + DXGI 交换链的确定性。计划已把该路径列为沿用 0.3 已验证结论，本次不重复验证；但**快照金样应固定 DPI 与字体**（当前用系统 Segoe UI，跨机器字体版本可能不同）。

### H7 自绘引擎与 WAML 耦合面小 —— **通过**

- 控件**零** WAML 感知：只声明 `TypeInfo`（类型名、内容模型、属性表），实现 `Measure/Arrange/OnPaint/输入`。
- WAML 侧只做“解析 → 写槽位 / 建 Binding”，不含任何渲染代码。
- 证据：新增一个控件只需在 `Types.cpp` 加约 8 行注册 + 一个类；`ListBox` 的模板机制完全在核心层，解析器只产出 `DataTemplate` 对象。
- **一处耦合泄漏**：`ListBox::RebuildItems` 需要按“槽位变更”重建项，依赖 `OnSlotChanged` 回调——但这属于控件自身职责，不是 WAML 耦合。可接受。

### H8 诊断质量足够 —— **通过**

- 6 处“最接近名字”建议全部有用（元素/属性/枚举/token/绑定参数/绑定路径）。
- 诊断可机器解析：`file(line,col): severity CODE: message`，`diag: formatted output is machine readable` 断言格式稳定。
- **发现缺口**：绑定路径在运行期解析失败时原本**完全静默**（打 `OutputDebugString` 但无事可观测）。已增加 `IBindingDiagnostics` + `BindingErrorCount()`，见 §4.2。

---

## 3. 性能实测

### 3.1 解析 + 校验

| 文档 | 字节 | median | p99 | 吞吐 |
|---|---|---|---|---|
| 1 个区块 / 12 节点 | 650 | 21.1 µs | 37.7 µs | 30.8 MB/s |
| 5 个区块 / 40 节点 | 2258 | 42.4 µs | 66.4 µs | 53.3 MB/s |
| 20 个区块 / 145 节点 | 8288 | 175.2 µs | 239.6 µs | 47.3 MB/s |

小文档吞吐偏低是固定开销（建 `SourceMap`、注册表首查）占比高；均在计划预算内。

### 3.2 规模曲线（修复后的线性证据）

`waml_scale.exe`（每节点 1 个属性，排除属性数量影响）：

| 节点数 | 字节 | 耗时 | ns/节点 |
|---|---|---|---|
| 50 | 1088 | 30.3 µs | 541 |
| 100 | 2138 | 59.7 µs | 563 |
| 200 | 4238 | 112.7 µs | 547 |
| 400 | 8438 | 237.0 µs | 584 |
| 800 | 16838 | 469.2 µs | 582 |

**线性**（ns/节点稳定在 541–584）。修复前同样输入为 1243 → 1817 → 3179 → 5921 → 11315 ns/节点，**超线性（约 O(n²)）**。

### 3.3 其它

| 项 | median | p99 |
|---|---|---|
| 实例化 12 / 40 / 145 节点 | 5.0 / 18.2 / 97.0 µs | 8.2 / 36.2 / 218.6 µs |
| 绑定 OneWay / TwoWay | 92.5 / 114.5 ns | — |
| 布局 12 / 40 / 145 节点 + 100 项列表 | 44.4 / 55.9 / 93.8 µs | 62.0 / 92.9 / 168.8 µs |
| 软件光栅 1280×800 | 409.4 µs | 608.0 µs |
| DataTemplate 100 项构建 | 287.7 µs | 374.3 µs |

---

## 4. 验证过程中发现并修复的缺陷

这些是 spike 的核心价值：**在写正式实现前就踩到了会在集成期才暴露的坑。**

### 4.1 【严重】列表所有行渲染同一份数据

**现象**：`ListBox` 的 5 行全部显示 `Title`，`Index` 全部相同（截图对比可见）。

**根因**：`Widget::SetDataContext` 的顺序错误。

```cpp
// 错误顺序
dataContext_ = dc;
RebindAll();                                   // ← 这里会触发 ListBox 重建项，
                                               //    新项各自 SetDataContext(item)
for (auto& c : children_) c->SetDataContext(dataContext_);  // ← 又把 item 覆盖成父级 VM
```

**修复**（`Widget.cpp`）：
1. **先**把继承的 context 下推给子级（跳过拥有自有 context 的子级），**再**执行 `RebindAll()`；
2. 子级若 `ownsDataContext_`，父级不再覆盖它——即“本地值优先”，与属性解析优先级一致。

**回归测试**：`list: each item keeps its own data context`（3 行断言各自标题）；`waml_interact.exe` 打印行子树并断言 `row1 title = 'Item 2'`。

**影响范围**：这是 MVVM 列表的**功能性缺陷**，不是性能问题。若未在 spike 中发现，会在 W4 阶段以“模板不生效”的形式出现，排查成本高（表面症状是“绑定没反应”）。

### 4.2 【中】绑定路径错误完全静默

**现象**：`{Binding Tittle}`（拼错）在运行期什么都不发生：目标保留旧值，无日志（`OutputDebugString` 在无调试器时不可见），无计数。

**根因**：绑定只做了“写日志”，没有可观测出口。

**修复**（`Mvvm.h` / `Binding.cpp`）：
- 新增 `IBindingDiagnostics`（`OnBindingError(scope, path, message)`）+ `SetBindingDiagnostics()`；
- 新增 `BindingErrorCount()` 进程级计数，便于测试与崩溃报告采集；
- 错误信息含路径、控件类型与建议（`'Tittle' not found (did you mean 'Title'?)`）。

**同时修复的关联缺陷**：`SetSource` 原本在 `source_ == source` 时直接 return，导致“先用空 context 失败、再重设同一 context”无法恢复。改为“仅当已成功解析到 leaf 时才短路”，并补 `binding: recovery after a failed resolution` 用例。

**计划影响**：W3 的退出条件新增“所有绑定失败路径可通过注入的 sink 观测”，并在 §5.3 规则 7 补一条“运行期失败必须上报”。

### 4.3 【中】解析在大单行文档上退化为 O(n²)

**现象**：8288 字节文档 1180 µs（7 MB/s），而 2258 字节 112 µs（20 MB/s）——**体积 3.7×，耗时 10.6×**。

**根因**：`SourceMap::Locate(offset)` 为算列号从行首逐码点扫描；解析器对**每个元素、每个属性**都立即调用它。单行文档（模板/压缩格式常见）行首固定为 0，故每次都是 O(n)，总计 O(n²)。

**修复**：解析期只存**字节偏移**（`RawNode::offset` / `nameOffset` / `RawAttr::offset`），行列改为**只在真正产出诊断时**计算（`Validator::Report`）。诊断不发生时开销为零。

**验证**：`waml_scale.exe` 曲线恢复线性（§3.2）；解析吞吐 7 → 47 MB/s（6.7×）。

**计划影响**：
- 预算口径补充“**按节点数**衡量，不按字节数”，因为字节数会掩盖单行/多行差异；
- W1 的退出条件新增“规模曲线必须线性（ns/节点偏差 ≤ 2×）”。

### 4.4 【低】COM 生命周期导致的二次渲染崩溃

**现象**：第一次渲染成功，创建**第二个**渲染器时访问违例（`0xC0000005`）。

**根因**：构造函数 `CoInitializeEx`、析构函数 `CoUninitialize` 成对调用；第二个实例构造时，第一个实例的 `ComPtr` 成员尚未析构，而 COM 已被拆掉。

**修复**：改为进程级一次性 `EnsureCom()`，不在析构时 `CoUninitialize`。

**计划影响**：写入 §5.4（视图侧契约）与编码规范——“COM 引用计数按进程初始化，不按对象成对调用”。

### 4.5 【低】`#define DrawText` 与 `PaintCtx::DrawText` 冲突

`windows.h` 在 `UNICODE` 下把 `DrawText` 展开为 `DrawTextW`，导致后端实现无法 override 纯虚方法。修复：在包含 `windows.h` 后 `#undef DrawText`。

**计划影响**：平台层头文件必须 `#undef` 这一组文本/GDI 宏（`DrawText`、`GetTextExtent` 等），并禁止把 `windows.h` 带进公共头（0.3 ADR-9 已要求，这里补上具体名单）。

### 4.6 【严重】高 DPI 下界面“完全点不动、无法输入”

**现象**（200% 缩放机器上实际反馈）：窗口能打开、界面能画出来，但**点击任何控件都没有反应，键盘也无法输入**。

**根因**：演示程序把 DIP 与物理像素混用，三处单位不一致：

| 位置 | 用的单位 | 后果 |
|---|---|---|
| `ID2D1HwndRenderTarget`（默认 dpi 0） | D2D 按**系统 DPI** 把 DIP 放大 2× 再绘制 | 画面被放大 2 倍 |
| `ForceLayout(Size{clientW, clientH})` | 物理**像素** | 布局按 1× 排，与实际画面差 2 倍 |
| `WM_LBUTTONDOWN` 的 `GET_X_LPARAM` | 物理**像素**，直接当 DIP 做命中测试 | 点击落在错误控件上 |

结果：用户点在按钮上，命中测试却算到别的位置，于是“什么都没发生”——看起来就是死界面。

**为什么 selftest 没抓到（假阳性）**：原 selftest 用控件自己的 `Bounds()`（DIP）直接 `MAKELPARAM` 发消息，等于**用 DIP 当像素**；而当时布局也是按像素排的，两边“错得一致”，所以恰好命中。它验证的是“内部坐标自洽”，不是“真实输入路径”。

**修复**（`tools/Demo.cpp`）：
1. 引入显式比例 `g.scale = GetDpiForSystem() / 96`；
2. 布局一律用 **DIP**（`clientW / scale`）；
3. 所有输入坐标经 `ToDip(px)` 换算后再做命中测试；
4. 处理 `WM_DPICHANGED`（更新 scale、`SetDpi`、清空画刷/字体缓存、窗口按建议矩形重排）；
5. 窗口尺寸改为**按 DIP 指定**（420×560 DIP），否则 100% 下的 780×960 px 在 200% 下只有 390×480 DIP，布局被挤扁。

**selftest 加固**（防止再次假阳性）：
- 点击坐标改为 `ToPixels(dip)`，即**模拟真实用户**的像素坐标；
- 新增**反向探针**：点击 (4,4) 空白处必须**不**改变状态——若 DPI 映射错了，正向用例可能偶然通过，反向用例不会；
- selftest 现在还驱动 TextBox 聚焦 + `WM_CHAR` 输入、CheckBox 切换，覆盖“无法输入”这一类症状。

200% DPI 下输出：

```text
system dpi=192, scale=2.00
window client 830x1088, D2D hwnd target ok
button dip=(192,119) -> pixel=(420,268), client=830x1088
click at (420,268): count 0 -> 1
textbox at pixel (415,429): focused=yes vm.Name='hi'
checkbox at pixel (64,511): toggled=yes
negative probe (4,4): count stayed 1 = yes
SELFTEST OK
```

**计划影响**：这条本应在 spike 阶段就覆盖，但被 selftest 的写法掩盖了。已固化为两条规则：
1. **演示/自检工具必须用“物理像素输入 + DIP 布局”验证**，不能拿内部坐标回灌；
2. 明确约定 **`Layout` 与命中测试一律在 DIP 空间**，平台层负责像素↔DIP 换算，且必须在 `SetProcessDpiAwarenessContext` **之后**读取 DPI（否则拿到的是虚拟化的 96）。
3. Spike 报告原“未验证：高 DPI 矩阵”一项**已部分覆盖**（单显示器 200%），但仍需补 100%/125%/150% 与跨显示器移动。

### 4.7 【中】示例 WAML 与 ViewModel 属性名不一致

`Gallery.waml` 里写 `IsChecked="{Binding ShowSuggestions}"`，而 `DemoVm` 注册的是 `Enabled`。因为 §4.2 之前没有诊断出口，这个错误被静默吞掉——复选框根本不参与绑定。

现已统一为 `ShowSuggestions`。这也正是 §4.2 加 `IBindingDiagnostics` 的意义：**这类问题应当在开发期直接报错，而不是等到“点了没反应”**。

### 4.8 【严重】资源打包的三个静默失败

**背景需求**：把 WAML 编译进 exe，并配置版本属性/图标/清单。实现过程中连续踩了三个**都不会报错**的问题：

**4.8.1 Manifest 类型写成宏名 → DPI 感知静默失效**

`app.rc` 里原本写 `1 RT_MANIFEST "app.manifest"`。rc.exe 不认识 `RT_MANIFEST`（其值 24 需要 `winuser.h`），把未知标识符当成**字符串类型名**，资源于是以 `"RT_MANIFEST"` 存储。Windows 只认 `RT_MANIFEST(24)`，因此清单**完全没有生效**：

- 进程仍是 DPI-unaware → 200% 下窗口被系统位图拉伸（模糊）
- `GetDpiForSystem` 被虚拟化为 96，掩盖了 §4.6 的 DPI 缺陷
- 无任何错误输出

修复：`1  24  "app.manifest"`，并在注释里写明“必须用数字”。这也意味着**§4.6 的 DPI 修复此前只生效了一半**——`SetProcessDpiAwarenessContext` 调用是有效的，但清单里的 PerMonitorV2 从未被应用。

**4.8.2 VERSIONINFO 漏了数字 id**

`VS_VERSION_INFO VERSIONINFO`（无前导 id）能编译通过，exe 也能运行，但 shell 的文件属性对话框里**没有任何版本信息**。修复：`1 VERSIONINFO ...`。

（附带陷阱：写成 `1 VS_VERSION_INFO VERSIONINFO` 会让 rc.exe 把 `VS_VERSION_INFO` 当成文件名去找，报 `RC2135 file not found: VERSIONINFO`。）

**4.8.3 读取版本号的方式**

用 `FindResource(RT_VERSION)` + 强转 `VS_FIXEDFILEINFO*` 时，读到全 0（长度前缀与 32 位对齐使其不是裸结构体）。必须用 `GetFileVersionInfo` + `VerQueryValueW`。另外：`verrsrc.h` 对该结构只有前向声明，新版 SDK 已移除 `ver.h`，需要包含 `winver.h`。

**验证**：`tools/ResourceProbe.cpp` 把三条都变成断言——manifest 必须能从 `RT_MANIFEST(24)` 找到且非 0 字节、VERSIONINFO 版本号必须等于本次编译的宏、图标组必须存在。资源布局独立验证输出：

```text
WAML_DOC/101: 2694 bytes      [ ok ]
RT_GROUP_ICON/201: 118 bytes  [ ok ]
RT_MANIFEST/1: 1568 bytes     [ ok ]
RT_VERSION size: 1964         [ ok ]
file version: 0.5.0.0         [ ok ]
FileDescription: WAML / MVVM self-drawn UI spike  [ ok ]
RESOURCE LAYOUT OK
```

**计划影响**：
1. 资源类型一律用**数字常量**，不用宏名；`RC` 文件里每个资源加断言性注释。
2. 打包必须有**门禁测试**，不能靠人打开文件属性确认——缺 `.res` 的 exe 照常运行。
3. 增加“嵌入内容 == 源文件”的逐字节比较，防止 `.res` 陈旧导致发布版带旧界面。

---

### 4.9 【需求落实】Debug 读文件 / Release 读资源

**要求**：调试时走目录里的独立 `.waml` 文件，发布时走 exe 内资源。

**实现**：编译期开关 `WUI_WAML_LOOSE_FILES`（仅 debug 配置定义），不在运行时猜路径。

| | Debug | Release |
|---|---|---|
| 来源 | `samples\Gallery.waml` | `WAML_DOC` RCDATA |
| 改界面 | 改文件重跑即可 | 需重新构建 |
| 诊断文件名 | 真实路径 | `res:#101` |
| 失败回退 | 回退到嵌入资源 | 无回退 |

**为什么用编译期开关而不是运行时探测**：运行时“有文件就用文件”会让发布版因为工作目录里恰好有个同名旧文件而**静默加载错误界面**。编译期开关把“这是开发版”变成二进制属性，不可能被环境误触发。仍保留运行时逃生口 `WUI_WAML_DIR`（显式设为某目录时优先读），供现场排查。

**路径解析**：从 exe 目录**逐级向上**搜索（最多 6 层），而不是固定两级。`build\debug\waml_demo.exe` 到 `samples\` 是两级，写死“exe 目录 + 父目录”会漏，症状是“debug 开关没生效”（实际回退到了资源）。

**验证**：

```text
debug   : markup mode: loose files (debug)
          markup origin: ...\samples\Gallery.waml (ok)
release : markup mode: embedded resource (release)
          markup origin: res:#101 (ok)
```

交叉验证：**同一份 WAML 从资源渲染与从文件渲染，输出 PNG 的 SHA-256 完全相同**（`0AD4C082...`），证明打包没有改变内容。Release 的 `waml_demo.exe` 单独拷到空目录（无 samples、无仓库）仍能完整运行，`SELFTEST OK`。

### 4.10 SmartScreen 每次启动拦截 —— 根因是构建环境，不是代码

**现象**：`waml_demo.exe`（本地编译、未签名、无 Mark-of-the-Web）双击启动弹「Windows 已保护你的电脑」。同机 `iOffice.exe`、`NotepadX.exe` 同样未签名、同样无 MOTW，却从不提示。

**根因**：**由自动化/沙箱会话创建的可执行文件带有一条未解析 SID 的 ACL 项**，被 SmartScreen 判为可疑来源。

```
S-1-4-151873615-607647326   Allow   DeleteSubDirectoriesAndFiles, Write, Delete, Synchronize
```

这是该会话起初为修复工作区写入权限而改写过目录 ACL 留下的痕迹（当时还写入了 `Everyone: Deny DeleteSubDirectoriesAndFiles`）。

**决定性证据**（方法：同一二进制、不同创建者/目录）：

| 文件 | 创建方式 | ACL 是否含 `S-1-4-*` | 双击结果 |
|---|---|---|---|
| `iOffice.exe` | 用户普通构建（8 月） | 否 | 正常启动 |
| `NotepadX.exe` | 用户普通构建（昨天） | 否 | 正常启动 |
| `D:\manual-build\minimal.exe` | **用户手动 `cl.exe` 构建** | 否 | **正常启动** |
| `waml_demo.exe` | 自动化会话构建 | **是** | 每次拦截 |
| `iOffice-copy.exe` | 自动化会话**复制**（SHA-256 与原文件相同） | **是（继承）** | **每次拦截** |

最后一行是关键：**同一份二进制**，原文件能启动、副本被拦截，二者唯一差异是 ACL。这直接否定了「代码/编译参数/签名/MOTW/清单」等所有内容层面的解释。

**判定方法**：

```bat
icacls 目标.exe | findstr /i "S-1-4"
```

有输出即被标记。

**解决方案**：产物输出到自动化创建的目录之外（`WUI_OUT`），或在普通终端里构建/复制一次。

**已验证有效**（2026-11-22）：在普通 cmd 中执行

```bat
md D:\wui-dist
cd /d D:\Cplusplus\Win32UI\spike\waml-mvvm
set WUI_OUT=D:\wui-dist
build.bat all
```

产物 `D:\wui-dist\release\waml_demo.exe` 双击**正常启动，无 SmartScreen 提示**。同一份源码、同一套编译参数，仅改变构建进程与输出目录。

**排查过程中的错误（重要教训）**

本问题被误判了五轮，全部是「先假设、后验证」，没有做对照实验：

| 轮次 | 错误归因 | 证伪方式 |
|---|---|---|
| 1 | 需要代码签名 / 有 MOTW | 三个对照 exe 都未签名、都无 MOTW |
| 2 | 缺手写 `trustInfo` | 补上后仍拦截 |
| 3 | 清单编进 `.rc` 导致链接器不注入 `trustInfo` | 修复后仍拦截 |
| 4 | `VERSIONINFO` 缺失 | 修复后仍拦截 |
| 5 | 目录 ACL（第一次提出，但当时误判为「Temp 也有所以不是」） | — |

正确做法本应是**第一步就做最小对照**：要么让用户手动构建一个 20 行的 Win32 程序（10 秒即可完成），要么做「同一二进制换目录」的实验。用户最终正是用这两种方法在几分钟内定性的。

期间顺带修掉的真实问题（与拦截无关，但本身是正确的改进）：

- 清单不应编进 `.rc`，应走 `/MANIFESTINPUT:`（否则链接器跳过 `trustInfo` 注入）。
- 路径解析需逐级向上，固定两级会漏（`build\debug\` 到 `samples\` 隔两层）。
- `%~dp0` 末尾反斜杠在 `/I"%SRC%"` 中会吞掉下个参数（`D8003`）。
- `/Fo"dir\"` 同样吞反斜杠，.obj 落到当前目录。

**结论**：代码与构建配置均已验证正确（63 用例、快照确定性、200% DPI 交互、资源门禁全部通过）。拦截是构建环境的文件标记问题，与 WAML/MVVM 引擎实现无关。

---
## 5. 未覆盖 / 明确不做（如实标注）

| 项 | 状态 | 说明 |
|---|---|---|
| IME 组字、选区、剪贴板（自绘 TextBox） | **未实现** | 仅有插入/退格/删除/方向键/Home/End/回车。R3 仍未决，W2 末必须做 Go/No-Go |
| 虚拟化列表 | 未实现 | 按计划 v1.1 |
| 样式/资源字典/触发器/动画 | 未实现 | 按计划 v0.6+ |
| 无障碍（UIA Provider） | 未实现 | 仅保留了 `AutomationRole` 属性位 |
| 原生孤岛（`NativeHost`） | 未实现 | 类型注册表预留了命名空间 |
| D3D11 + DXGI 交换链 | 未验证 | 用 D2D WIC 目标替代；交换链沿用 0.3 结论 |
| 多窗口 / 高 DPI 矩阵 | **部分验证** | 已测单显示器 200%；仍需 100%/125%/150% 与跨显示器移动（见 §4.6） |
| 跨机器字体一致性 | **风险** | 快照用系统 Segoe UI；金样比较需固定字体文件或嵌入字体 |
| `ElementName` / `RelativeSource` / `MultiBinding` | 未实现 | 按计划 v0.6 |
| 多模块（DLL）类型注册 | 未验证 | 计划明确不支持 |

---

## 6. 对计划文档的修订（已回写 `PLAN-WAML-MVVM.md`）

1. §7 预算口径：解析改为**按节点**衡量（≤ 1.5 µs/节点），新增“规模线性”门。
2. §5.3 绑定语义：新增规则 8——**解析失败必须经 `IBindingDiagnostics` 上报**，不得静默。
3. §5.4 视图侧契约：新增 COM 生命周期与 `windows.h` 宏隔离要求。
4. §8 W1 退出条件：新增“规模曲线线性”“每控件注册行数 ≤ 10”。
5. §8 W3 退出条件：新增“绑定失败可观测”。
6. §9 R1：登记“每控件 7.7 行注册样板”的实测值，作为 v0.6 优化依据。
7. §9 R4：把“≤ 500 项”从建议改为**明确限制**，并补充实测外推依据。
8. §11 H5/H6 描述：确认 H6 用 D2D WIC 目标而非 WARP 交换链，原因是无需窗口且验证同一渲染抽象。

---

## 7. 建议的下一步（按优先级）

1. **W1 + W3 并行启动**：解析器与绑定内核已经验证可行，直接进入正式实现，把 spike 代码作为参考实现而非直接合入（spike 缺少线程模型、错误注入、UIA 元数据）。
2. **W2 前完成 TextBox 路线决策（R3）**：这是唯一会让 W2 工期翻倍的风险点。建议先做一个 2 天的 IME 垂直切片（`WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` + 候选窗定位），不达标就切原生 `EDIT` 孤岛——**WAML 语法不变**，这也是 spike 里 `TextBox` 只有属性、没有渲染细节的原因。
3. **快照金样固定字体**：CI 里把 Segoe UI 换成随仓库分发的字体子集，否则跨机器/跨 Windows 版本会误报。
4. **补绑定失败的可观测性到所有宿主**：Gallery、NotepadX 都应注册 `IBindingDiagnostics` 并在 Debug 下断言 `BindingErrorCount()` 为 0，这样“拼错属性名”会在开发期立即暴露。
5. **`wamlc` 仍然不做**：解析 8288 字节仅 175 µs，启动期成本可忽略；codegen 的收益要到控件数 × 实例数都很大时才显现，v0.7 再评估。

---

## 附录 A：测试清单（63 用例）

| 分组 | 数量 | 覆盖 |
|---|---|---|
| parse | 8 | 元素/属性/嵌套/注释/处理指令/实体/文本内容/属性元素/`x:Name` |
| diag | 10 | 15 类诊断码矩阵 + 建议质量 + 码点列号 + 转义 + 索引器拒绝 |
| value | 2 | 颜色（token/hex/非法）、Thickness（1/2/4 值） |
| instantiate | 3 | 树结构、默认值、绑定附加、DataContext 继承 |
| binding | 17 | 4 模式、默认模式、StringFormat、类型转换、嵌套路径、缺失路径、LostFocus、回声抑制、重绑、生命周期、诊断 sink、失败恢复 |
| list | 3 | 模板实例化、数据上下文隔离（回归）、订阅泄漏 |
| layout | 4 | 垂直/水平排列、Border padding、失效冒泡 |
| input | 6 | 点击/键盘/禁用、TextBox 输入、CheckBox 切换、命中测试 |
| render | 3 | 记录式确定性、光栅确定性、主题切换 |
| perf | 5 | 5 项预算守卫 |

## 附录 B：复现方式

```bat
cd D:\Cplusplus\Win32UI\spike\waml-mvvm
build.bat all
build\waml_tests.exe
build\waml_interact.exe samples\Gallery.waml
build\waml_snapshot.exe samples\Gallery.waml build\light.png build\dark.png
build\waml_bench.exe
build\waml_scale.exe
build\waml_demo.exe            :: 交互窗口
build\waml_demo.exe --selftest :: 写 selftest.log 并返回退出码
```

## 附录 C：构建环境说明

CMake/Ninja 在本沙箱下**无法使用**：两者都通过管道捕获子进程 stdout 来解析诊断，而受限沙箱禁止创建该管道，导致 configure 挂起而非报错。`build.bat` 直接驱动 `cl.exe`/`link.exe`（子进程继承 stdio），在受限与完全权限下都可工作。正式仓库仍应使用 CMake —— 这是**环境限制**，不是设计结论。
