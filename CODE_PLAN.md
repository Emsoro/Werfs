# Gridform 实施计划与技术验证报告

> 版本：0.6.0.0
> 日期：2026-11-22
> 仓库：https://gitee.com/masonwu21/Gridform
> 技术基线：Windows 10/11 x64 · C++20 · MSVC 19.4x · MIT · 无 WebView/.NET/Qt/JS 引擎
> 参考项目（**不提交**）：`WinUI-Gallery-3.0.4/`（控件设计）、`microsoft-ui-xaml-winui3-release-2.7.0/`（XAML 解析）

---

## 0. 一句话说明

**Gridform = WAML（声明式界面） + C++20（自绘引擎） + MVVM（数据绑定），目标是做成完整的控件图库（Gallery）：每个控件都有可运行、可交互、可阅读代码的示例页。**

当前 0.6.0.0 已完成**地基 + 布局层**：框架与示例分离、WAML 解析/校验/实例化、四种绑定模式、**15 个控件**（含 Grid/Canvas/WrapPanel/VariableSizedWrapGrid/Viewbox/Expander 六个布局面板）、两种渲染后端、**83 个测试**全绿，并交付 **3 个可独立运行的示例**（`basic` / `list` / `gallery`）共用一套宿主。

**下一阶段的主线是「示例矩阵」**（§10a）：把单个 `gallery` 拆成六个可独立运行的示例——`basic`（✅ 基础控件）、`list`（✅ 集合与布局）、`usercontrol`（**父子组件、两个数据源**）、`binding`（四种模式对照）、`theming`（主题）、`gallery`（集大成的目录）。它同时是框架能力的验收矩阵：`usercontrol` 要求的能力当今框架**还没有**，这正是下一块要补的（§14）。

---

# 第一部分：当前状态

## 1. 目录结构

```
framework/src/         框架：不认识任何示例
    core/              Widget 树、布局、失效传播、控件、值类型、主题
    mvvm/              ObservableProperty / RelayCommand / ObservableList / Binding
    waml/              Lexer、Parser、Validator、诊断、实例化
    platform/          Resources：资源与文件加载、VERSIONINFO、路径解析
    render/            SoftwareRender（测试）、D2DRender（真实）

samples/gallery/       示例：单向依赖框架
    Gallery.waml         界面描述
    GalleryVm.h          ViewModel（唯一一份，工具共用）
    App.cpp              Win32 宿主窗口
    resources/           app.rc、Resource.h、app.ico、app.manifest

tests/  bench/  tools/ 框架级工具，不依赖示例
build.ps1  CMakeLists.txt
```

**铁律**：`framework/` 里**没有**任何 `Gallery.waml`、`GalleryVm` 或 `IDR_WAML_*` 的引用。资源 id 属于示例，定义在 `samples/gallery/resources/Resource.h`；框架只提供「按类型名 + id 读取 RCDATA」的通用接口。

新增示例 = 复制 `samples/gallery/` 改名、改资源 id、在 `build.ps1` 加一个目标函数即可，框架零改动。

## 2. 能力清单

| 能力 | 状态 | 证据 |
|---|---|---|
| WAML 解析 + 静态校验（行列号 + 诊断码 + 建议） | ✅ | 83 用例 |
| 四种绑定模式 + 双向回写 + 回声抑制 | ✅ | 绑定更新 89 ns/次 |
| DataTemplate / ObservableList | ✅ | 100 项构建 288 µs |
| **15 控件**：Page/Border/StackPanel/ScrollViewer/TextBlock/Button/CheckBox/TextBox/ListBox + **Grid/Canvas/WrapPanel/VariableSizedWrapGrid/Viewbox/Expander** | ✅ | 类型注册表 16 项（含 DataTemplate） |
| **Grid 行列布局**：`ColumnDefinitions="1*,2*,auto"` + `Grid.Row/Column/RowSpan/ColumnSpan` + 间距 | ✅ | 5 个测试锁定星号分配、固定混合、跨列、间距、auto 自适应 |
| **WrapPanel / VariableSizedWrapGrid**：换行流式 + 跨行列贴片（占用网格算法） | ✅ | 2 个测试锁定换行与跨行不重叠 |
| **Canvas**：绝对定位（`Canvas.Left/Top`） | ✅ | 2 个测试锁定偏移与显式尺寸 |
| **Viewbox**：等比缩放子内容 | ✅ | 1 个测试锁定缩小适配 |
| **Expander**：折叠展开 + 点击头部切换 | ✅ | 1 个测试锁定两态高度 |
| **TextBlock 自动换行**（`TextWrapping="Wrap"`） | ✅ | list 示例长段落渲染验证 |
| ScrollViewer（滚轮 + 键盘 + 自绘滚动条） | ✅ | 6 个测试：滚动范围、钳制、空白处滚轮、视口外不可点、四角探测、**绘制像素随滚动变化** |
| D2D 真实渲染 + 确定性快照 | ✅ | 两次渲染 SHA 相同 |
| 主题切换（Light/Dark，13 token） | ✅ | snapshot 双主题 |
| 高 DPI（DIP 布局 + 像素输入） | ✅ | 已验证 200% |
| 资源打包（单文件 exe，无外部依赖） | ✅ | probe 门禁 |
| **多示例 + 共享宿主**（`samples/shared`） | ✅ | 3 个示例（gallery / list / basic）共用同一 Win32+D2D 宿主 |
| **按页分派的验证工具** | ✅ | `snapshot` 与 `interact` 依据页面选择 ViewModel 与断言，避免「拿 gallery 的期望去测别的页」 |

## 3. 产物

| 产物 | 作用 |
|---|---|
| `waml_demo.exe` | 真实 Win32 窗口，可交互；`--selftest` 无窗口自检 |
| `waml_tests.exe` | 65 个单元/集成用例 |
| `waml_interact.exe` | 无窗口驱动真实输入（点击、键盘、切换） |
| `waml_snapshot.exe` | D2D 渲染 + 确定性校验 |
| `waml_probe.exe` | 资源与交付模式门禁 |
| `waml_bench.exe` | 性能基线 |

构建：`pwsh -File build.ps1 <target>`，target 见 README

---

# 第二部分：技术验证结论

验证方式：配套一个可运行 spike，对 8 条高风险假设（H1–H8）做实测。

## 4. 结论：Go

**8 条假设全部通过；实施过程中发现并修复 10 个真实缺陷**（见第六部分），其中多个属于「静默失败」类型——这类缺陷若留到集成期，排查成本极高。

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

**不建议**在 v0.5 提前引入 codegen（`wamlc`）或虚拟化：当前数据远未触及预算。

**范围调整**：不虚拟化的 `ListBox` 在 ~500 项时接近交互可接受边界（100 项 288 µs，线性外推 500 项约 1.4 ms），文档需明确「≤ 500 项」上限。

## 5. 逐条假设

### H1 手写 Lexer/Parser 覆盖子集，行列诊断可用 —— **通过**

- 语法覆盖：元素/属性/自闭合/属性元素/文本内容/CDATA/注释/处理指令/实体（含 `&#x1F600;`）/标记扩展（含嵌套与引号）/`{}` 转义。
- 诊断：15 类诊断码全部有正反例覆盖，行列号精确到**码点**（UTF-8 中文前面的属性正确报在第 22 列而非第 24 字节列）。
- 未知元素/属性/枚举/token/绑定参数/绑定路径**全部**给出「最接近的名字」（Levenshtein，含阈值）。
- 实际输出：
  ```text
  test.waml(2,3): error WAML0101: 'Buton' is not a known WAML element (did you mean 'Button'?)
  test.waml(1,12): error WAML0102: 'wui/TextBlock' has no property 'Txt' (did you mean 'Text'?)
  test.waml(1,1): error WAML0104: 'TextPrimry' is not a valid color or theme token (did you mean 'TextPrimary'?)
  ```

**发现的问题**：元素诊断原本锚定在 `<`（第 1 列），改为锚定标签名（第 2 列起），与编译器习惯一致。

### H2 类型注册表 + 属性描述符 + 槽位，实例化无字符串查找 —— **通过**

- `Widget` 的值存在 `std::vector<Value>`，按类型内密集槽位索引；绑定在附加时解析一次路径，运行期只持有 `IPropertySource*`。
- 实例化 12 节点 5.0 µs、40 节点 18.2 µs、145 节点 97 µs ≈ **0.67 µs/节点**。
- **无反射的代价已量化**：9 个控件共 69 个属性注册，平均 **7.7 行/控件**。这个样板量在 20–30 控件规模下（约 200 行）可接受，但建议 v0.6 引入聚合式 `PropList` 初始化。
- **多模块限制已被规避**：属性表按实例构建，不依赖全局字符串表，不存在静态初始化顺序与跨 DLL 重复属性对象问题。

### H3 按属性对象订阅的绑定 ≤ 150 ns/次 —— **通过**

- OneWay **89–93 ns**，TwoWay **111–123 ns**（含通知遍历 + 类型转换 + 槽位写入 + 失效标记）。
- 路径每跳订阅：`{Binding Child.Value}` 验证了「中间对象替换 → 重建尾部链 → 继续跟随」的完整语义。

### H4 四种绑定模式 + 回声抑制 —— **通过**

| 模式 | 初始化 | VM→View | View→VM |
|---|---|---|---|
| OneTime | ✔ | ✘ | ✘ |
| OneWay | ✔ | ✔ | ✘ |
| TwoWay | ✔ | ✔ | ✔ |
| OneWayToSource | ✘ | ✘ | ✔ |

- 默认模式由**目标属性描述符**声明（`TextBox.Text` 默认 TwoWay），避免「忘记写 Mode 就静默失效」。
- 回声抑制：`applying_` 标志 + 值相等短路；连续 100 次回写无栈溢出、最终值正确。
- `LostFocus` 触发器正确延迟回写。
- 生命周期：`bindings released when widget destroyed`（observers 1→0）、`source destroyed before view`（视图存活、保留最后值）、`item binding updates do not leak`（列表清空后 observers 归零）。

### H5 DataTemplate 实例化可承受 100 项 —— **通过**

- 100 项构建 **288 µs**（p99 374 µs），布局 **102 µs**，帧内合计约 0.39 ms。
- **发现严重缺陷**：列表所有行渲染同一份数据（见 §6.1）。已修复并加回归测试。

### H6 软件路径可产出确定性快照 —— **通过（用 D2D WIC 而非 WARP 交换链）**

- 采用 **D2D WIC 位图渲染目标**（`ID2D1RenderTarget` on `IWICBitmap`），无需窗口、无需显示器、无需 D3D 交换链；与 D3D11 路径共享「D2D/DWrite 绘制抽象」，验证的是同一套渲染代码。
- 两次独立渲染器实例、两次 `Paint`：**像素哈希完全相同**。
- PNG 经 WIC 解码回验 720×900 成功；不同主题产生不同哈希。
- **未验证**：D3D11 + DXGI 交换链的确定性。快照金样应固定 DPI 与字体（当前用系统 Segoe UI，跨机器字体版本可能不同）。

### H7 自绘引擎与 WAML 耦合面小 —— **通过**

- 控件**零** WAML 感知：只声明 `TypeInfo`（类型名、内容模型、属性表），实现 `Measure/Arrange/OnPaint/输入`。
- WAML 侧只做「解析 → 写槽位 / 建 Binding」，不含任何渲染代码。
- 证据：新增控件只需在 `Types.cpp` 加约 8 行注册 + 一个类。

### H8 诊断质量足够 —— **通过**

- 6 处「最接近名字」建议全部有用。
- 诊断可机器解析：`file(line,col): severity CODE: message`，格式稳定有断言。
- **发现缺口**：绑定路径运行期失败原本**完全静默**。已增加 `IBindingDiagnostics` + `BindingErrorCount()`，见 §6.2。

## 6. 性能实测

### 6.1 解析 + 校验

| 文档 | 字节 | median | p99 | 吞吐 |
|---|---|---|---|---|
| 1 区块 / 12 节点 | 650 | 21.1 µs | 37.7 µs | 30.8 MB/s |
| 5 区块 / 40 节点 | 2258 | 42.4 µs | 66.4 µs | 53.3 MB/s |
| 20 区块 / 145 节点 | 8288 | 175.2 µs | 239.6 µs | 47.3 MB/s |

小文档吞吐偏低是固定开销（建 `SourceMap`、注册表首查）占比高；均在预算内。

### 6.2 规模曲线（线性证据）

每节点 1 属性，排除属性数量影响：

| 节点数 | 字节 | 耗时 | ns/节点 |
|---|---|---|---|
| 50 | 1088 | 30.3 µs | 541 |
| 100 | 2138 | 59.7 µs | 563 |
| 200 | 4238 | 112.7 µs | 547 |
| 400 | 8438 | 237.0 µs | 584 |
| 800 | 16838 | 469.2 µs | 582 |

**线性**（ns/节点稳定在 541–584）。修复前同样输入为 1243 → 1817 → 3179 → 5921 → 11315 ns/节点，**超线性（约 O(n²)）**。

### 6.3 其它

| 项 | median | p99 |
|---|---|---|
| 实例化 12 / 40 / 145 节点 | 5.0 / 18.2 / 97.0 µs | 8.2 / 36.2 / 218.6 µs |
| 绑定 OneWay / TwoWay | 92.5 / 114.5 ns | — |
| 布局 12 / 40 / 145 节点 + 100 项列表 | 44.4 / 55.9 / 93.8 µs | 62.0 / 92.9 / 168.8 µs |
| 软件光栅 1280×800 | 409.4 µs | 608.0 µs |
| DataTemplate 100 项构建 | 287.7 µs | 374.3 µs |

---

# 第三部分：实施中修复的缺陷

这些是 spike 的核心价值：**在写正式实现前就踩到了会在集成期才暴露的坑。**

## 7.1 【严重】列表所有行渲染同一份数据

**现象**：`ListBox` 的 5 行全部显示同一 `Title`，`Index` 全部相同。

**根因**：`Widget::SetDataContext` 的顺序错误。

```cpp
// 错误顺序
dataContext_ = dc;
RebindAll();                                   // ← 触发 ListBox 重建项，
                                               //    新项各自 SetDataContext(item)
for (auto& c : children_) c->SetDataContext(dataContext_);  // ← 又把 item 覆盖成父级 VM
```

**修复**：
1. **先**把继承的 context 下推给子级（跳过拥有自有 context 的子级），**再**执行 `RebindAll()`；
2. 子级若 `ownsDataContext_`，父级不再覆盖——即「本地值优先」。

**回归测试**：`list: each item keeps its own data context`；`waml_interact.exe` 断言 `row1 title = 'Item 2'`。

**影响**：这是 MVVM 列表的**功能性缺陷**。若未在 spike 发现，会以「模板不生效」形式出现，表面症状是「绑定没反应」，排查成本高。

## 7.2 【中】绑定路径错误完全静默

**现象**：`{Binding Tittle}`（拼错）运行期什么都不发生：目标保留旧值，无日志，无计数。

**修复**：
- 新增 `IBindingDiagnostics`（`OnBindingError(scope, path, message)`）+ `SetBindingDiagnostics()`；
- 新增 `BindingErrorCount()` 进程级计数；
- 错误信息含路径、控件类型与建议（`'Tittle' not found (did you mean 'Title'?)`）。

**关联缺陷**：`SetSource` 原本在 `source_ == source` 时直接 return，导致「先用空 context 失败、再重设同一 context」无法恢复。改为「仅当已成功解析到 leaf 时才短路」。

## 7.3 【中】解析在大单行文档上退化为 O(n²)

**现象**：8288 字节文档 1180 µs（7 MB/s），而 2258 字节 112 µs（20 MB/s）——体积 3.7×，耗时 10.6×。

**根因**：`SourceMap::Locate(offset)` 为算列号从行首逐码点扫描；解析器对每个元素、每个属性都立即调用。单行文档行首固定为 0，每次都是 O(n)，总计 O(n²)。

**修复**：解析期只存**字节偏移**，行列改为**只在真正产出诊断时**计算。诊断不发生时开销为零。

**验证**：曲线恢复线性；解析吞吐 7 → 47 MB/s（6.7×）。

## 7.4 【低】COM 生命周期导致的二次渲染崩溃

**现象**：第一次渲染成功，创建**第二个**渲染器时访问违例（`0xC0000005`）。

**根因**：构造函数 `CoInitializeEx`、析构函数 `CoUninitialize` 成对调用；第二个实例构造时，第一个实例的 `ComPtr` 成员尚未析构，而 COM 已被拆掉。

**修复**：改为进程级一次性 `EnsureCom()`，不在析构时 `CoUninitialize`。

**规范**：COM 引用计数按**进程**初始化，不按对象成对调用。

## 7.5 【低】`#define DrawText` 与 `PaintCtx::DrawText` 冲突

`windows.h` 在 `UNICODE` 下把 `DrawText` 展开为 `DrawTextW`，导致后端实现无法 override 纯虚方法。修复：包含 `windows.h` 后 `#undef DrawText`。

**规范**：平台层头文件必须 `#undef` 这组文本/GDI 宏，并禁止把 `windows.h` 带进公共头。

## 7.6 【严重】高 DPI 下界面「完全点不动、无法输入」

**现象**（200% 缩放机器）：窗口能打开、界面能画出来，但**点击任何控件都没有反应，键盘也无法输入**。

**根因**：DIP 与物理像素混用，三处单位不一致：

| 位置 | 用的单位 | 后果 |
|---|---|---|
| `ID2D1HwndRenderTarget`（默认 dpi 0） | D2D 按**系统 DPI** 把 DIP 放大 2× 再绘制 | 画面被放大 2 倍 |
| `ForceLayout(Size{clientW, clientH})` | 物理**像素** | 布局按 1× 排，与实际画面差 2 倍 |
| `WM_LBUTTONDOWN` 的 `GET_X_LPARAM` | 物理**像素**，直接当 DIP 做命中测试 | 点击落在错误控件上 |

**为什么 selftest 没抓到（假阳性）**：原 selftest 用控件自己的 `Bounds()`（DIP）直接 `MAKELPARAM` 发消息，等于**用 DIP 当像素**；而当时布局也是按像素排的，两边「错得一致」，所以恰好命中。它验证的是「内部坐标自洽」，不是「真实输入路径」。

**修复**：
1. 引入显式比例 `g.scale = GetDpiForSystem() / 96`；
2. 布局一律用 **DIP**；
3. 所有输入坐标经 `ToDip(px)` 换算后再做命中测试；
4. 处理 `WM_DPICHANGED`（更新 scale、`SetDpi`、清空画刷/字体缓存）；
5. 窗口尺寸**按 DIP 指定**（420×560 DIP），否则 100% 下的 780×960 px 在 200% 下只有 390×480 DIP，布局被挤扁。

**selftest 加固**（防再假阳性）：
- 点击坐标改为 `ToPixels(dip)`，即**模拟真实用户**的像素坐标；
- 新增**反向探针**：点击 (4,4) 空白处必须**不**改变状态——若 DPI 映射错了，正向用例可能偶然通过，反向用例不会；
- 现在驱动 TextBox 聚焦 + `WM_CHAR` 输入、CheckBox 切换，覆盖「无法输入」类症状。

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

**固化为规则**：
1. **演示/自检工具必须用「物理像素输入 + DIP 布局」验证**，不能拿内部坐标回灌；
2. **`Layout` 与命中测试一律在 DIP 空间**，平台层负责像素↔DIP 换算，且必须在 `SetProcessDpiAwarenessContext` **之后**读取 DPI（否则拿到虚拟化的 96）；
3. 高 DPI 矩阵仍需补 100%/125%/150% 与跨显示器移动。

## 7.7 【中】示例 WAML 与 ViewModel 属性名不一致

`Gallery.waml` 里写 `IsChecked="{Binding ShowSuggestions}"`，而 VM 注册的是 `Enabled`。因为 §7.2 之前没有诊断出口，这个错误被静默吞掉——复选框根本不参与绑定。

现已统一。这也正是加 `IBindingDiagnostics` 的意义：**这类问题应当在开发期直接报错，而不是等到「点了没反应」**。

## 7.8 【严重】资源打包的三个静默失败

**7.8.1 Manifest 类型写成宏名 → DPI 感知静默失效**

`app.rc` 里原本写 `1 RT_MANIFEST "app.manifest"`。rc.exe 不认识 `RT_MANIFEST`（其值 24 需要 `winuser.h`），把未知标识符当成**字符串类型名**，资源以 `"RT_MANIFEST"` 存储。Windows 只认 `RT_MANIFEST(24)`，因此清单**完全没有生效**：

- 进程仍是 DPI-unaware → 200% 下窗口被系统位图拉伸（模糊）
- `GetDpiForSystem` 被虚拟化为 96，掩盖了 §7.6 的 DPI 缺陷
- 无任何错误输出

修复：`1  24  "app.manifest"`，注释写明「必须用数字」。这意味着 **§7.6 的 DPI 修复此前只生效了一半**——`SetProcessDpiAwarenessContext` 有效，但清单里的 PerMonitorV2 从未被应用。

**7.8.2 VERSIONINFO 漏了数字 id**

`VS_VERSION_INFO VERSIONINFO`（无前导 id）能编译通过，exe 也能运行，但 shell 的文件属性里**没有任何版本信息**。修复：`1 VERSIONINFO ...`。

（附带陷阱：写成 `1 VS_VERSION_INFO VERSIONINFO` 会让 rc.exe 把 `VS_VERSION_INFO` 当文件名去找，报 `RC2135 file not found: VERSIONINFO`。）

**7.8.3 读取版本号的方式**

用 `FindResource(RT_VERSION)` + 强转 `VS_FIXEDFILEINFO*` 会读到全 0（长度前缀与 32 位对齐使其不是裸结构体）。必须用 `GetFileVersionInfo` + `VerQueryValueW`。另外 `verrsrc.h` 对该结构只有前向声明，新版 SDK 已移除 `ver.h`，需要包含 `winver.h`。

**验证**：`waml_probe.exe` 把三条都变成断言——manifest 必须能从 `RT_MANIFEST(24)` 找到且非 0 字节、VERSIONINFO 版本号必须等于本次编译的宏、图标组必须存在。

```text
WAML_DOC/101: 2694 bytes      [ ok ]
RT_GROUP_ICON/201: 118 bytes  [ ok ]
RT_MANIFEST/1: 1568 bytes     [ ok ]
RT_VERSION size: 1964         [ ok ]
file version: 0.6.0.0         [ ok ]
RESOURCE LAYOUT OK
```

**规范**：
1. 资源类型一律用**数字常量**，不用宏名；
2. 打包必须有**门禁测试**，不能靠人打开文件属性确认——缺 `.res` 的 exe 照常运行；
3. 增加「嵌入内容 == 源文件」的逐字节比较，防止 `.res` 陈旧。

## 7.9 Debug 读文件 / Release 读资源

**实现**：编译期开关 `WUI_WAML_LOOSE_FILES`（仅 debug 配置定义），不在运行时猜路径。

| | Debug | Release |
|---|---|---|
| 来源 | `samples/gallery/Gallery.waml` | `WAML_DOC` RCDATA |
| 改界面 | 改文件重跑即可 | 需重新构建 |
| 诊断文件名 | 真实路径 | `res:#101` |
| 失败回退 | 回退到嵌入资源 | 无回退 |

**为什么用编译期开关**：运行时「有文件就用文件」会让发布版因为工作目录里恰好有个同名旧文件而**静默加载错误界面**。编译期开关把「这是开发版」变成二进制属性，不可能被环境误触发。仍保留运行时逃生口 `WUI_WAML_DIR`。

**路径解析**：从 exe 目录**逐级向上**搜索（最多 6 层），而不是固定两级。写死「exe 目录 + 父目录」会漏，症状是「debug 开关没生效」（实际回退到了资源）。

**验证**：同一份 WAML 从资源渲染与从文件渲染，输出 PNG 的 SHA-256 完全相同，证明打包没有改变内容。Release 的 exe 单独拷到空目录仍能完整运行。

## 7.10 【环境】SmartScreen 每次启动拦截

**现象**：本地编译、未签名、无 MOTW 的 exe 双击弹「Windows 已保护你的电脑」，而同机其他未签名 exe 从不提示。

**根因**：**由自动化/沙箱会话创建的可执行文件带有一条未解析 SID 的 ACL 项**，被 SmartScreen 判为可疑来源。

```
S-1-4-151873615-607647326   Allow   DeleteSubDirectoriesAndFiles, Write, Delete, Synchronize
```

**决定性证据**（同一二进制、不同创建者/目录）：

| 文件 | 创建方式 | ACL 含 `S-1-4-*` | 双击 |
|---|---|---|---|
| 用户普通构建的 exe | 手动 `cl.exe` | 否 | 正常启动 |
| `waml_demo.exe` | 自动化会话构建 | **是** | 每次拦截 |
| 同一 exe 的**逐字节副本** | 自动化会话复制 | **是（继承）** | **每次拦截** |

最后一行是关键：**同一份二进制**，原文件能启动、副本被拦截，唯一差异是 ACL。这否定了「代码/编译参数/签名/MOTW/清单」等所有内容层面的解释。

**判定**：`icacls 目标.exe | findstr /i "S-1-4"`，有输出即被标记。

**解决**：产物输出到自动化创建的目录之外（`WUI_OUT`），或在普通终端里构建/复制一次。

**教训**：本问题被误判了五轮，全部是「先假设、后验证」，没有做对照实验。正确做法是**第一步就做最小对照**——要么让用户手动构建一个 20 行的 Win32 程序，要么做「同一二进制换目录」的实验。

期间顺带修掉的真实问题（与拦截无关但本身正确）：
- 清单不应编进 `.rc`，应走 `/MANIFESTINPUT:`（否则链接器跳过 `trustInfo` 注入）。
- 路径解析需逐级向上，固定两级会漏。
- `%~dp0` 末尾反斜杠在 `/I"%SRC%"` 中会吞掉下个参数（`D8003`）。
- `/Fo"dir\"` 同样吞反斜杠，.obj 落到当前目录。

## 7.11 【严重】`auto` 列被判成 0 像素固定列

**现象**：`samples/list` 的列表项模板里，第三列（`auto`）内容完全不显示，整行右侧一片空白。页面无报错，绑定错误数为 0。

**原因**：`ColumnDefinitions="36,1*,auto"` 的解析器只认大写 `"Auto"`。小写 `"auto"` 走到 `else` 分支，被当成「固定 0 像素」，这一列宽度恒为 0，子元素被测量进 0 宽度区域，`desired.w` 自然也是 0。

**为什么难发现**：布局成功、绘制成功、绑定成功、测试全绿——只是内容少了一列。这正是「静默失败」的典型形态。

**修复**：解析改为大小写不敏感（统一转小写后匹配）。同时修正另一处相关问题：`auto` 轨道测量子元素时必须给**无约束宽度**，否则用当前 0 宽去量，轨道永远塌缩。

**教训**：枚举名/关键字的大小写必须宽容。若坚持精确匹配，至少要在无法识别时**报诊断**而不是退化成 0。

## 7.12 【中】跨行贴片与后续项重叠

**现象**：`VariableSizedWrapGrid` 里一个 `RowSpan="2"` 的贴片会把后面一项盖在同一个格子上。

**原因**：原实现用「推进游标」的方式排布——一项结束就把游标移到下一格。遇到跨行项时，游标只前进一列，下一行同列的位置已被这个跨行项占用，于是重叠。

**修复**：改为**占用网格**算法——维护一张布尔占位图，每项扫描找到第一个能放下的矩形。`Measure` 与 `Arrange` 共用同一个 `PlanCells()`，保证两次布局不会对位置产生分歧。

**验证**：新增测试断言「三段贴片：第 0 项在 (0,0)、跨行项在 (0,1) 高 2 行、第 2 项必须跳到 (0,2) 而不是重叠」。

## 7.13 【中】`TextBlock` 不支持自动换行

**现象**：`samples/list` 里所有说明文字都被截断成一行，超出部分消失。

**原因**：`TextBlock::Measure` 完全忽略 `available`，按内容定尺寸且不换行。短文本没问题，长段落必然溢出。

**修复**：新增 `TextWrapping` 属性（`NoWrap`/`Wrap`）。`Wrap` 时按词贪心折行，长单词按字符断开，显式 `\n` 保留；`Measure` 用行高 × 行数作为高度，`OnPaint` 逐行绘制。

**设计取舍**：默认 `NoWrap`，与改动前行为一致，不影响任何现有页面。

## 7.14 【严重】滚轮在页面空白处完全无效

**现象**：`samples/basic` 页面内容明显超出窗口，但滚轮只在把光标移到某个控件上时才起作用；移到卡片之间的缝隙或右侧留白就完全不动。用户报的是「滚轮不起作用」。

**原因**（两层，缺一不可）：

1. `Widget::HitTest` 在 `children_` 非空时**只返回子节点的命中结果**，自身永不参与。指针落在空白处 → 返回 `nullptr` → 宿主回退到根节点 `Page` → `Page::OnPointerWheel` 向父级冒泡，但 `Page` 没有父级 → 返回 `false` → 事件被丢弃。滚轮从来没到过 `ScrollViewer`。

2. `StackPanel::Measure` **没有调用 `ApplySizeConstraints()`**，显式 `Height`/`Width` 被完全忽略。这让排查更困难：`ScrollViewer` 的滚动范围取决于内容高度，而内容的声明高度不生效，`MaxScroll` 会算成 0，看起来又像「本来就没东西可滚」。

**修复**：

- `ScrollViewer` 增加 `HitTest` 覆写：先问子节点（保证按钮点击正常），没有命中且点在视口内就**认领**该点击。同时加视口边界检查——滚出可视区的内容不再可点，与绘制时的裁剪保持一致。
- `StackPanel` 补上 `ApplySizeConstraints()`。顺带审计全部 14 个 `Measure` 实现，发现 `CheckBox` 同样遗漏，一并修复。

**教训**：`HitTest` 的「有子节点就不认领自己」是个危险的默认值——容器只要没铺满就会被指针事件穿透。凡是需要接收事件（滚轮、拖拽、背景点击）的容器，都应显式覆写 `HitTest`。另外，「显式尺寸不生效」会让滚动类问题伪装成布局问题，两者需要分开验证。

**测试**：新增 3 个用例锁定——空白处滚轮能滚动、视口外内容不可点、以及沿视口四角探测点全部能触发滚动（模拟宿主 `WM_MOUSEWHEEL` 的真实路径）。

**后续（同一问题的第二层，见 §7.15）**：滚轮生效后暴露出更根本的缺陷——滚动位置从未影响绘制。

---

## 7.15 【严重】滚动只动滚动条，内容不动

**现象**：滚轮修好之后（§7.14），滚动条滑块会移动、`ScrollOffset()` 也返回正确值，但界面上的内容纹丝不动。用户描述是「滚动条动了，界面没有滚动，下方的东西没上来」。

**原因**：滚动位置从未进入绘制路径。`ScrollViewer::OnPaint` 给子节点设了 `paintOffset_`，然后立刻恢复，而 `Widget::PaintTree` **根本不读 `paintOffset_`** —— 这个字段被写入后从未被消费。滚动条之所以会动，是因为滑块位置直接从 `offset_` 算出来；内容之所以不动，是因为绘制只用 `bounds_`。

**为什么前一个测试没抓到**：之前的断言只检查 `ScrollOffset() > 0`，而这个数值一直是正确的。**只验证状态、不验证像素**，是这个 bug 能活下来的根本原因。

**修复**：

1. `PaintCtx` 没有坐标变换栈，每个控件又都从自己的 `bounds_` 绘制，所以唯一的办法是在绘制期间平移 rect。新增 `PaintTreeAt(ctx, palette, shift)`，进入时 `bounds_ += shift`，退出时恢复——**绝不能让偏移留在 `bounds_` 里**，那是布局几何，会污染后续 `Arrange`。

2. shift 必须**沿整棵子树累加下传**。`bounds_` 是绝对坐标，只平移直接子节点的话，孙节点仍停在原地——这正是「面板动了、面板里的东西没动」的直接原因。

3. 引入 `Widget::PaintChildrenAt` 作为虚钩子：`PaintTreeAt` 在 `OnPaint` 之后调用它，`ScrollViewer` 覆写它以加上 `-offset_` 并 `PushClip`。**子节点不能在自己的 `OnPaint` 里画**，否则会被画两次（一次带偏移、一次不带），这也是第一版修复失败的另一个原因。

4. `HitTest` 相应对称：把屏幕坐标 + `offset_` 还原到内容空间，再向下传递。

**教训**：

- **验证要看到像素**。新增的测试直接比较两个滚动位置下 `SoftwareRasterizer` 的哈希，而不是读 `ScrollOffset()`。哈希不同才证明画面真的变了。
- **画与点必须用同一套坐标约定**。`PaintChildrenAt` 下传偏移、`HitTest` 回收偏移，两者必须严格对称，否则会出现「看得到点不到」或「点得到看不到」。
- 顺带给 `waml_snapshot` 加了滚动参数（第 5 个 argv），可以渲染任意滚动位置的画面，肉眼确认。

---

# 第四部分：目标架构

## 8. 分层

```
┌─────────────────────────────────────────────────┐
│ samples/gallery                                  │
│   Navigation · HomePage · ControlPages/*         │
│   ← 只写 WAML + ViewModel，不含渲染代码            │
├─────────────────────────────────────────────────┤
│ framework                                        │
│   waml   解析/校验/实例化                         │
│   mvvm   绑定/命令/集合                           │
│   core   控件/布局/输入/主题                       │
│   render D2D / 软件光栅                           │
│   platform 资源/版本/路径                          │
└─────────────────────────────────────────────────┘
```

## 9. Gallery 页面模型（参考 WinUI-Gallery）

每个控件页由统一的区块构成，不是自由排版：

```
PageHeader        标题 + 一段说明（这个控件是什么、何时用）
ExampleCard ×N    每个示例教**一个**概念：
                    ├ 标题（例如 "TwoWay 绑定"）
                    ├ 可交互的演示区
                    └ 对应的 WAML 代码片段（可折叠）
相关控件          跳到相似控件的链接
```

这个模型直接决定框架还缺什么：`ExampleCard` 需要**可复用的 WAML 片段**，代码展示需要**等宽字体文本块**，页面切换需要**路由**。

## 10. 导航模型

因为 ListBox 无选中、无 Grid，导航用**左侧窄栏 + 按钮列表 + VM 驱动路由**：

```
GalleryVm
  ├ Pages: ObservableList<PageEntry>     ← 侧栏数据源
  ├ CurrentPage: Property<RefPtr>        ← 当前页（决定右侧内容）
  └ NavigateCommand: RelayCommand(page)  ← 按钮点击切换
```

页面切换 = 换掉右侧 DataContext 并重新 Instantiate 对应 XAML。**不销毁窗口**。

---

## 10a. 示例矩阵（samples/）

示例不是「一堆 demo」，而是一条**由简到繁的学习路径**，同时是框架能力的验收矩阵。每个示例独立可构建、可运行、可单独截图，任何一个失败都说明某项框架能力退化。

### 10a.1 分层原则与顺序

示例按**概念难度递增**排列，顺序是刻意设计的：前四个不需要任何新框架能力，第五个（`usercontrol`）才开始逼出框架的组件层设计。

```
samples/
  1. basic/        基础：单个组件、单一数据源
  2. binding/      绑定：四种模式的完整对照
  3. list/         集合：DataTemplate 与列表
  4. theming/      主题：Light/Dark 与 token
  5. usercontrol/  组合：父子组件、两个独立数据源 ← 框架的关键缺口
  6. gallery/      集大成：按 WinUI-Gallery 的组件分类组织全部控件页
```

**为什么 `usercontrol` 排第五**：它依赖的能力（`DataContext` 可绑定）在框架里还不存在（见 10a.6）。把它排在概念演进的中段、而不是最前，可以让前四个示例立即开工，同时把框架改造集中在一次迭代里完成，而不是一开始就卡在设计决策上。

**依赖方向**：示例之间**不互相引用**。`gallery` 是唯一例外——它以**只读方式**复用其他示例的 WAML 与 ViewModel（见 10a.7），因为它就是「这些示例的目录」。

### 10a.2 `samples/basic/` — 基础组件

**目的**：用最少的代码展示「WAML 长什么样、怎么绑、怎么响应」。这是新用户的第一个入口。

**数据源**：一个 `BasicVm`，所有控件共享它。

| 示例 | 内容 | 教什么 | 框架能力 |
|---|---|---|---|
| TextBlock | 静态文本 + FontSize/FontWeight/Foreground | 声明式属性、主题 token | 已有 |
| Button | 点击改计数 | `Command` 绑定、失效重绘 | 已有 |
| TextBox | 输入回写 | `Mode=TwoWay`、默认模式 | 已有 |
| CheckBox | 勾选联动可见性 | 布尔绑定、绑定驱动状态 | 已有 |
| Border | 卡片外观 | Padding/CornerRadius/背景 | 已有 |

**验收**：五个控件各一个示例，全部可交互且状态正确。

### 10a.3 `samples/binding/` — 四种绑定模式对照
**目的**：绑定是 MVVM 的核心，但「OneTime 和 OneWay 到底差在哪」很难靠文字讲清。这里用**并排四列**、同一个数据源，让差异肉眼可见。

| 示例 | 内容 | 教什么 |
|---|---|---|
| 四模式并排 | 同一 `Name` 分别以 OneTime/OneWay/TwoWay/OneWayToSource 绑到四个 TextBox | 改 VM → 哪些变；改输入框 → 哪些回写 |
| StringFormat | `{Binding Count, StringFormat='Count: {0}'}` | 值格式化 |
| FallbackValue | 绑定一个不存在的路径 | 失败兜底与诊断上报 |
| LostFocus | `UpdateSourceTrigger=LostFocus` | 回写时机差异 |

**验收**：OneTime 列在 VM 变化后**不动**（这是最容易误判的一条）。

### 10a.4 `samples/list/` — 集合与模板

**目的**：列表是真实应用最常见的结构，也是 §7.1 缺陷的现场。

| 示例 | 内容 | 教什么 |
|---|---|---|
| 基础列表 | `ItemsSource` + `ItemTemplate` 显示 4 项 | DataTemplate 实例化 |
| 每项独立 | 每行显示**自己的** Index 与 Title | 数据上下文隔离（回归 §7.1） |
| 动态增删 | 按钮 Add/Remove 改 `ObservableList` | 集合变更通知 |
| 空列表 | 0 项时的表现 | 边界情况 |

**验收**：每行内容互不相同（直接对应曾经「所有行渲染同一份数据」的缺陷）。

### 10a.5 `samples/theming/` — 主题与 token

**目的**：展示「同一份 WAML 换主题」，这是 token 设计的意义所在。

| 示例 | 内容 | 教什么 |
|---|---|---|
| 主题切换 | 按钮在 Light/Dark 间切换，整页即时重绘 | token 解析与重绘边界 |
| token 面板 | 13 个 token 各显示一块色卡与其名字 | 语义化颜色的含义 |
| 硬编码 vs token | 左边 `#FF0000`，右边 `Accent`，切换主题 | token 跟随主题，字面量不变 |
| 自定义颜色 | `Foreground="#6B6B6B"` | 字面量与 token 混用 |

**验收**：切换后背景/文字/强调色全部变化，且**硬编码的红色保持不变**（证明 token 与字面量是两条路径）。

### 10a.6 `samples/usercontrol/` — 父子组件、两个数据源 ⭐

**目的**：这是**框架目前做不到**的一件事，也是它必须补上的能力——让一个子组件拥有**自己的**数据源，而不继承父级。

**场景**：一个「设置面板」页，页面本身绑 `PageVm`（标题、主题），嵌入两个 **`ServerCard`** 子组件实例，每个卡片绑**各自的** `ServerVm`（名称、端口、启用状态）。改卡片 1 的端口不影响卡片 2，也不影响页面。

```
PageVm (页面数据源)
 ├ Title, Theme
 └ Cards: ObservableList<ServerCardVm>   ← 每个元素是一个独立数据源
      ├ ServerCardVm#1  Name="alpha" Port=8080 Enabled=true
      └ ServerCardVm#2  Name="beta"  Port=9090 Enabled=false

ServerCard（可复用组件，自己的 VM 是自己的 DataContext）
 ├ Name / Port / Enabled 绑定到 **卡片自己的** VM
 └ 内部再嵌一个 StatusBadge，绑卡片 VM 的派生状态
```

**为什么这是关键能力**：没有它，「组件复用」只能靠复制粘贴 WAML，无法把「一个卡片」封装成一处定义、多处实例化、各自独立。MVVM 的价值有一半在这里。

**框架缺口（实测确认）**：

| 现状 | 证据 | 结论 |
|---|---|---|
| `Source=` 在 v0.5 被明确拒绝 | `Parser.cpp:1050` — "Source= is not supported in v0.5 (use DataContext inheritance)" | 无法在属性级指定数据源 |
| 无 `ContentControl` / `UserControl` 类型 | `Types.cpp` 注册表只有 10 个类型 | 无法定义「组件」这一层 |
| `DataContext` 无 WAML 可写属性 | 属性表里没有 `DataContext` | 无法在标记里给子树换数据源 |
| 已有基础已具备 | `Widget::SetDataContext(dc, own)` 支持「本地值优先」（`ownsDataContext_`），§7.1 的修复正是为此 | 底层就绪，只缺标记层入口 |

**要补的三件事**（按最小可行顺序）：

1. **`DataContext` 成为可绑定的 WAML 属性**（首选方案）
   `DataContext="{Binding SelectedServer}"` 即可让整棵子树切换到该对象。改动集中在属性表 + `Instantiate` 时的赋值路径，不需要新的控件类型。
   - 风险：`DataContext` 绑定本身依赖父级 context，初始化顺序必须「先解析绑定、再下推」，否则会自我覆盖。

2. **`Source=` 绑定参数**（备选/补充）
   解掉 v0.5 的禁用，允许 `{Binding Name, Source={StaticResource ...}}`。需要资源引用机制，成本高于方案 1，列为补充。

3. **`UserControl` 模板机制**（可选，成本最高）
   `samples/usercontrol/ServerCard.waml` 作为模板，`<UserControl Source="ServerCard.waml"/>` 实例化。这需要「编译期文档引用」，会让打包与路径解析复杂化。**建议延后**，先用 1 或 2 达成同样的可见效果。

**推荐路径**：先做方案 1。它用最小改动达成「父子两数据源」的目标，且不需要模板机制。方案 3 若做，应在 v1.0 之后单独评估。

**验收**：
- 页面标题改 `Theme`，两个卡片的 `Port` 不变；
- 卡片 1 改端口，卡片 2 不受影响；
- 卡片内嵌的 `StatusBadge` 跟随**卡片**的 `Enabled`，而非页面的；
- 用 `waml_interact.exe` 断言三者的独立性。

### 10a.7 `samples/gallery/` — 总的 Gallery

**目的**：把所有示例组织成一个可浏览的图库，即产品形态。它**不是再写一遍示例**，而是**索引**上面的示例。

**结构**：

```
┌──────────────┬──────────────────────────────────┐
│ 侧栏          │ 内容区                            │
│              │                                  │
│ ▸ 基础        │  PageHeader                       │
│   · TextBlock │  ┌────────────────────────────┐  │
│   · Button    │  │ ExampleCard                │  │
│   · TextBox   │  │  标题 / 演示区 / WAML 代码   │  │
│   · CheckBox  │  └────────────────────────────┘  │
│   · Border    │  ┌────────────────────────────┐  │
│ ▸ 组合        │  │ ExampleCard                │  │
│   · UserControl│ └────────────────────────────┘  │
│ ▸ 绑定        │                                  │
│   · 四模式     │                                  │
│ ▸ 集合        │                                  │
│   · 列表      │                                  │
└──────────────┴──────────────────────────────────┘
```

**如何复用其他示例**：gallery 通过**文件级引用**读取 `samples/basic/*.waml` 等文件（打包时全部编入资源，或 debug 时从磁盘读），**不复制内容**。这样示例只有一份，gallery 永远显示真实代码。

**导航模型**：因 `ListBox` 暂无选中（R3），侧栏先用**按钮列表**；迭代 3.3 完成后换成真正的列表控件。

#### 10a.7.1 组件清单：来源与分级

清单取自本项目参考的 WinUI-Gallery 数据文件（`ControlInfoData.json`，**不提交**），18 个分组、126 个条目。剔除后剩 90 个待实现控件：

| 类别 | 数量 | 处理 |
|---|---|---|
| 概念/设计页（Fundamentals / Design / Accessibility） | 15 | 不是控件。其中 Binding、Style、Resources、Custom & User Controls 四项分别由 `binding/`、迭代 2、`usercontrol/` 覆盖 |
| 需要 OS 子系统集成 | 14 | **不做**：`WebView2`、`MediaPlayerElement`、`MapControl`、`Capture Element`、`Sound`、通知中心、Badge、JumpList、`AppWindow`、`AppWindowTitleBar`、`ContentIsland`、`Storage pickers`、`System Backdrops`、`SystemBackdropElement` |
| 纯控件 | 97 | 已有 7 个 → **90 个待实现** |

**不追求一次做完 90 个**。按日常使用频率与实现成本分三档，本轮**只做 P0**：

| 档位 | 数量 | 范围 | 何时做 |
|---|---|---|---|
| **P0 必做** | **23** | 日常界面几乎必用、且**不需要新子系统** | 本轮目标 |
| P1 常用增强 | 16 | 共同卡在「浮层/弹出」这一层 | 浮层完成后顺带解锁 |
| P2 长尾 | 51 | 虚拟化集合、日期、媒体、动画、窗口管理 | 按需再议，不承诺 |

**P0 — 本轮要做的 23 个**（加已有 9 个 = 32 个控件）：

| 分组 | 控件 | 新增框架能力 |
|---|---|---|
| 布局 | `Grid` | **行列定义 + 跨行跨列**（卡片布局硬需求） |
| 布局 | `Canvas` | 绝对定位 |
| 布局 | `WrapPanel` | 换行排列 |
| 布局 | `VariableSizedWrapGrid` | 换行 + **单项跨行跨列**（Grid 的行列 + WrapPanel 的换行） |
| 布局 | `RelativePanel` | **元素间相对关系**（需附加属性，见下） |
| 布局 | `Viewbox` | 缩放子内容 |
| 布局 | `Expander` | 折叠展开（需高度变化重排） |
| 基础输入 | `ToggleButton` | 布尔 + 视觉状态 |
| 基础输入 | `RepeatButton` | 按住连续触发 |
| 基础输入 | `HyperlinkButton` | 链接样式 |
| 基础输入 | `RadioButton` | **组互斥**（需 GroupName 作用域） |
| 基础输入 | `Slider` | 拖动 + 数值范围 |
| 基础输入 | `ToggleSwitch` | 布尔 + 滑块位置 |
| 文本 | `PasswordBox` | 掩码显示 |
| 文本 | `NumberBox` | 数值输入 + 校验 |
| 状态 | `ProgressBar` | 纯渲染 |
| 状态 | `ProgressRing` | 弧形绘制 |
| 状态 | `InfoBar` | 内联提示条 |
| 状态 | `InfoBadge` | 数字角标 |
| 媒体 | `Image` | **WIC 解码 + 缩放** |
| 媒体 | `PersonPicture` | 圆形头像（Image + 裁剪） |
| 图形 | `Line` | `PaintCtx` 需加画线 |
| 图形 | `Shape` | 基础几何绘制 |

**布局这一组已覆盖 WinUI-Gallery 的 Layout 分类**：`Border`、`StackPanel`、`ScrollViewer` 已有；本轮补齐 `Grid`、`Canvas`、`WrapPanel`、`VariableSizedWrapGrid`、`RelativePanel`、`Viewbox`、`Expander`。`SplitView` 归 P1（需浮层式面板），`LayoutPanel` 是自定义布局基类，不进 gallery。

**P0 里真正的框架改造有六项**，其余是「注册类型 + 实现 Measure/Arrange/OnPaint」的重复劳动：

1. `Grid` 的行列布局算法
2. `VariableSizedWrapGrid` 的跨行列换行（复用 Grid 的跨行列 + WrapPanel 的换行）
3. `RelativePanel` 的**附加属性机制**（`RelativePanel.RightOf="name"` 这类写法，当前解析器不支持带点号的属性名，也没有「按名字引用兄弟元素」的能力）
4. `Image` 的 WIC 解码与缓存
5. `PaintCtx` 增加 `DrawLine` / 路径绘制（支撑 `Line`、`Shape`、`ProgressRing`）
6. `RadioButton` 的组作用域

**关于 `RelativePanel` 的额外成本**：它需要两样框架目前都没有的东西——**附加属性**（`RelativePanel.RightOf`）和**按 `x:Name` 查找兄弟元素**。`x:Name` 已经记录在 `NodeDesc::name` 里，但布局期没有「从名字解析到 Widget」的索引。若这两项成本评估偏高，可把它降到 P1，用 `Grid` 的固定行列达成相近效果——但既然要覆盖 Layout 分类，建议做。`RelativePanel` 的其余朝向关系还有：`LeftOf`、`Above`、`Below`、`AlignLeftWith`、`AlignTopWith` 等。

**P1 — 之后做（16 个）**：`ComboBox`、`Popup`、`Flyout`、`MenuFlyout`、`ToolTip`、`ContentDialog`、`TeachingTip`、`AutoSuggestBox`、`CommandBar`、`AppBarButton`、`AppBarToggleButton`、`AppBarSeparator`、`BreadcrumbBar`、`MenuBar`、`SplitView`、`RatingControl`

这 16 个共同卡在一个缺失子系统上：**浮层（Popup/Flyout）**。它要处理 z-order、点外关闭、焦点陷阱、窗口外定位，成本高于 P0 任意单项——但做完这一次，其中大多数会顺带完成。

**P2 — 按需，不承诺（51 个）**：虚拟化集合（`ListView`/`GridView`/`TreeView`/`ItemsRepeater`/`FlipView`/`ItemsView`/`TableView`/`PullToRefresh`）、日期时间（4 个）、动画过渡（7 个）、导航容器（`NavigationView`/`TabView`/`Pivot`/`SelectorBar`）、分割按钮（3 个）、`ColorPicker`、`RichEditBox`、滚动增强（`PipsPager`/`PagerControl`/`SemanticZoom`/`AnnotatedScrollBar`/`ScrollView`）、画刷（`AcrylicBrush`/`RadialGradientBrush`）、`ThemeShadow`、`IconElement`/`AnimatedIcon`、`Compact Sizing`、`Clipboard`、`Chart`、`InkCanvas`、`AnimatedVisualPlayer`、`TitleBar`、`Window`、`LayoutPanel`、命令对象（`StandardUICommand`/`XamlUICommand`）

**为什么这样切**：P0 覆盖「写一个真实窗口界面」的绝大部分，且除 Grid/Image/画线/组互斥外都是低风险增量。P1 被浮层这一项统一卡住，集中攻克一次即可解锁一批。P2 里的虚拟化与动画是另一个量级（见 R4、迭代 3），混进 P0 会拖垮进度。

### 10a.8 实施顺序与依赖

| 顺序 | 示例 | 依赖的框架能力 | 状态 |
|---|---|---|---|
| 1 | `basic` | 无 | **✅ 已完成** |
| 2 | `list` | 无 | **✅ 已完成** |
| 3 | `binding` | 无 | 可立即开始 |
| 4 | `theming` | 无（主题已支持） | 可立即开始 |
| 5 | `usercontrol` | **`DataContext` 可绑定**（10a.6） | 需先补框架 |
| 6 | `gallery` | 多文档资源 + 路由（迭代 1）、P0 控件（10a.7.1） | 依赖前五项 |

**关键判断**：`usercontrol` 是唯一被框架能力卡住的示例，也是**信息量最大**的一个——它逼出了「组件复用」这一层设计。排第五意味着前四个可以先落地，把框架改造集中成一次迭代，而不是一开始就卡在设计决策上。

**关于 gallery 的组件数**：完整对齐 WinUI-Gallery 是 90 个待实现控件，**不一次做完**。本次只做 P0 的 23 个（10a.7.1），加上已有 9 个共 32 个控件；P1 的 16 个等浮层子系统完成后顺带解锁；P2 的 51 个按需再议。

### 10a.9 构建与打包

每个示例是**独立可执行文件**（便于单独调试与截图）：

| 目标 | 产物 | 说明 |
|---|---|---|
| `basic` | `waml_basic.exe` | 单示例 |
| `usercontrol` | `waml_usercontrol.exe` | 单示例 |
| `binding` | `waml_binding.exe` | 单示例 |
| `list` | `waml_list.exe` | 单示例 |
| `theming` | `waml_theming.exe` | 单示例 |
| `release` / `all` | `waml_demo.exe` | **gallery**，产品形态 |

`build.ps1` 增加对应目标函数；`all` 构建全部。每个示例复用 `samples/shared/` 下的宿主代码（Win32 窗口 + D2D 初始化），避免六份重复——**这也是本次重构要做的第一件事**：把 `App.cpp` 里与「具体页面」无关的宿主逻辑抽出来。

---

# 第五部分：迭代路线

> 示例建设与框架迭代**并行推进**。示例矩阵见 10a；下面每轮的示例交付项都已标注对应小节。

## 11. 迭代 0：示例地基（v0.5.x）← 先做这个

在铺开多个示例之前，先把重复的宿主代码抽出来，否则六个示例会有六份 Win32 窗口 + D2D 初始化。

| 任务 | 说明 | 验收 |
|---|---|---|
| 0.1 抽出共享宿主 | `samples/shared/AppHost.{h,cpp}`：窗口、D2D、DPI、输入分发、主题 | 现有 gallery 改为调用它，行为不变 |
| 0.2 页面与宿主解耦 | 宿主只认「一个根 Widget + 一个 DataContext」 | 换 WAML 不改编译单元 |
| 0.3 抽出示例资源 | `resources/` 的 rc/manifest/ico 提到 `samples/shared/resources/` | 各示例共享同一份图标与清单 |
| 0.4 `samples/basic/` | 五个基础控件示例（10a.2） | ✅ 已交付 |
| 0.5 `build.ps1` 目标 | `basic` 目标 + 产物 `waml_basic.exe` | `pwsh -File build.ps1 basic` |

**为什么先做 0.1–0.3**：不做的话，后面每加一个示例就多一份复制粘贴，违反「框架不认识示例」的原则，也会让 §R6（代码漂移）提前发生。

## 12. 迭代 1：可导航的 Gallery 骨架（v0.6）

**目标**：能点开不同控件页，每页有标题和示例卡片。内容先做 3 个控件。

| 任务 | 说明 | 验收 |
|---|---|---|
| 1.1 多页资源打包 | `app.rc` 支持多份 WAML，按 id 枚举 | `EnumerateWamlResources` 返回 N>1 |
| 1.2 页面路由 | VM 持有 `CurrentPage`，切换时重建树 | 点击侧栏 → 右侧内容变化 |
| 1.3 ExampleCard 结构 | 统一卡片 WAML 片段（标题/演示区/代码区） | 3 页复用它 |
| 1.4 前 3 个控件页 | Button / TextBox / CheckBox | 每页 ≥3 个示例 |
| 1.5 侧栏导航 | 按钮列表 + 当前页高亮 | 高亮跟随路由 |
| 1.6 **文件级引用** | gallery 读取 `samples/*/` 的 WAML，不复制内容（10a.7.1） | 改示例文件，gallery 同步变化 |

**风险**：页面切换时的绑定泄漏（旧树未 detach）→ 切换前显式 `DetachBindings()`。

**同期交付示例**：`binding/`、`list/`、`theming/`（10a.3–10a.5），它们不依赖路由，且正好提供 gallery 的内容。

## 13. 迭代 2：布局与样式补齐（v0.7）

| 任务 | 说明 | 理由 |
|---|---|---|
| 2.1 `Grid` 控件 | 行列定义 + 跨行列 | 卡片「左演示/右代码」必须 |
| 2.2 `Style` / `Setter` | 具名样式，可 `BasedOn` | 否则每个控件重复写 10 个属性 |
| 2.3 `ResourceDictionary` | WAML 内定义颜色/尺寸常量 | 主题 token 不够表达设计系统 |
| 2.4 等宽字体 | FontFamily 属性 | 代码片段必须等宽 |
| 2.5 `StackPanel` 的 `Justify` | Start/Center/End/SpaceBetween | 页头标题与版本号同行 |

**风险**：`Style` 引入后，属性优先级（本地值 > 样式 > 默认值）需明确并测试。

**同期交付示例**：`samples/basic/`、`samples/binding/`、`samples/list/`、`samples/theming/`（见 10a.2–10a.5，均不依赖本迭代的新能力，可与迭代 2 并行）。

## 14. 迭代 2b：组件复用能力（v0.7+）⭐

**这是 `samples/usercontrol/` 的前置条件，也是 MVVM 价值兑现的关键一步**（详见 10a.6）。

| 任务 | 说明 | 验收 |
|---|---|---|
| 2b.1 `DataContext` 可绑定 | 让它成为 WAML 可写属性，`DataContext="{Binding SelectedServer}"` 切换子树数据源 | 子组件绑到自己的 VM，不继承父级 |
| 2b.2 初始化顺序修正 | 先解析绑定、再下推 context，避免自我覆盖 | 嵌套两层仍正确 |
| 2b.3 内置组件复用 | 「卡片」形态：同一 WAML 片段多处实例化、各自独立 | 改一卡片不影响另一卡片 |
| 2b.4 `samples/usercontrol/` | 父页面 + 两个子卡片 + 内嵌状态徽标 | 三数据源独立性有断言 |
| 2b.5（可选）`Source=` 绑定参数 | 解掉 v0.5 的禁用 | 仅在 2b.1 不够用时做 |

**判断依据**：`Widget::SetDataContext(dc, own)` 已支持「本地值优先」（`ownsDataContext_`，§7.1 的修复正是为此），所以底层就绪，**只缺标记层入口**——这也是选方案 1 而不是模板机制的原因。

**不做**：`UserControl` 模板机制（`Source="ServerCard.waml"`）。它需要编译期文档引用，会让打包与路径解析复杂化，收益与 2b.1 重叠。v1.0 之后再评估。

## 15. 迭代 3：控件覆盖到完整（v0.8）

| 批次 | 控件 | 新增框架能力 |
|---|---|---|
| 3.1 | TextBlock / Border / StackPanel / Page | 无 |
| 3.2 | Button / CheckBox / TextBox | 无 |
| 3.3 | ListBox | **选中支持**（SelectedIndex / SelectionChanged） |
| 3.4 | ScrollViewer | 无 |
| 3.5 | 新增 `Slider` | 拖动 + 数值属性 + 范围约束 |
| 3.6 | 新增 `ProgressBar` | 无输入，纯渲染 |
| 3.7 | 新增 `ToggleSwitch` | 布尔 + 动画位置 |
| 3.8 | 新增 `ComboBox` | 弹出层（需要 z-order 与浮层绘制） |
| 3.9 | 新增 `RadioButton` | 组互斥（需要 GroupName 作用域） |
| 3.10 | 新增 `Expander` | 折叠展开 + 高度动画 |

**每批次独立可交付**，不必等全部完成。

## 16. 迭代 4：示例质量（v0.9）

| 任务 | 说明 |
|---|---|
| 4.1 代码片段自动化 | 示例页的 WAML 代码块从源文件**读取**，不手抄（避免文档漂移） |
| 4.2 每个示例一概念 | 拆细不堆砌 |
| 4.3 主题切换控件 | UI 上的 Light/Dark 切换按钮 |
| 4.4 搜索 | 按控件名过滤侧栏 |
| 4.5 无障碍基础 | `AutomationRole` 已存在，补齐焦点顺序与键盘导航 |

## 17. 迭代 5：收尾（v1.0）

| 任务 | 说明 |
|---|---|
| 5.1 性能门禁 | 每个迭代跑 bench，退化即失败 |
| 5.2 打包 | 单文件 exe + 图标 + 版本信息（已具备） |
| 5.3 文档 | README 面向使用者，本文件面向贡献者 |
| 5.4 CI | 构建 + 65+ 测试 + probe 门禁 |

---

# 第六部分：决策与风险

## 18. 关键技术决策

| 决策 | 内容 | 为什么 |
|---|---|---|
| D1 | 不引入反射，用类型注册表 + 显式属性描述符 | 运行时零字符串查找，89 ns/更新 |
| D2 | 加载与实例化两段分离 | 校验错误在加载期暴露；一份编译视图可多次实例化 |
| D3 | MVVM 用显式属性表，按属性对象订阅 | 避免每次更新做字符串哈希 |
| D4 | 布局用 DIP，输入用像素 | 高 DPI 正确；已验证 200% |
| D5 | 框架不含任何示例符号 | 否则加第二个示例时框架要改 |
| D6 | 诊断必须可观测，不静默失败 | spike 期踩过「列表所有行渲染同一份数据」 |
| D7 | 用编译期开关切 debug/release 标记来源 | 运行时探测会让发布版静默读错文件 |
| D8 | 资源类型用数字常量而非宏名 | 宏名会被 rc.exe 当字符串类型，静默失效 |
| D9 | COM 按进程初始化，不按对象成对 | 否则第二个实例崩溃 |
| D10 | 构建脚本用 PowerShell 而非 cmd 批处理 | 参数以数组传递，消除 `%~dp0`/`/Fo"dir\"` 尾部反斜杠吞参数与转义引号两类缺陷；`call :label` 的读取缓冲边界问题（曾导致 `all` 目标偶发「cannot find the batch label」）也随之消失 |

## 19. 风险登记

| 编号 | 风险 | 影响 | 缓解 |
|---|---|---|---|
| R1 | 无 Grid 导致版面僵硬 | 卡片布局难看 | 迭代 2 优先做 |
| R2 | 无样式系统导致 WAML 冗长 | 示例可读性差 | 迭代 2 做 Setter |
| R3 | ListBox 无选中 | 导航只能按钮 | 迭代 3.3 |
| R4 | 不虚拟化，列表 >500 项卡顿 | 大规模示例受限 | 文档明确上限；v1.1 虚拟化 |
| R5 | 页面切换绑定泄漏 | 内存增长 | 切换时强制 detach + 测试 |
| R6 | 代码片段与真实 WAML 漂移 | 文档失信 | 迭代 4.1 自动提取 |
| R8 | 每控件 7.7 行注册样板 | 30 控件时约 230 行重复 | v0.6 引入聚合式 `PropList` |
| R9 | 跨机器字体不一致 | 快照金样误报 | CI 固定或内嵌字体子集 |
| R10 | IME 未实现 | 自绘 TextBox 无法输入中文 | 见 §20；需做垂直切片决策 |
| R11 | `DataContext` 不可绑定 | 无法复用组件，示例只能复制粘贴 WAML | 迭代 2b（见 10a.6），底层已就绪只缺标记层入口 |
| R12 | 示例之间复制宿主代码 | 六份 Win32+D2D 初始化重复，改动发散 | 迭代 0 先抽 `samples/shared/` |

## 20. 未覆盖 / 明确不做

| 项 | 状态 | 说明 |
|---|---|---|
| IME 组字、选区、剪贴板（自绘 TextBox） | **未实现** | 仅有插入/退格/删除/方向键/Home/End/回车。**中文输入不可用**，需 Go/No-Go 决策 |
| 虚拟化列表 | 未实现 | 计划 v1.1 |
| 样式/资源字典/触发器/动画 | 未实现 | 计划 v0.6+ |
| 无障碍（UIA Provider） | 未实现 | 仅保留 `AutomationRole` 属性位 |
| 原生孤岛（`NativeHost`） | 未实现 | 类型注册表预留了命名空间 |
| D3D11 + DXGI 交换链 | 未验证 | 用 D2D WIC 目标替代 |
| 多窗口 / 高 DPI 矩阵 | **部分验证** | 已测单显示器 200%；需补 100%/125%/150% 与跨显示器 |
| 跨机器字体一致性 | **风险** | 快照用系统 Segoe UI |
| `ElementName` / `RelativeSource` / `MultiBinding` | 未实现 | 计划 v0.6 |
| 多模块（DLL）类型注册 | 未验证 | 明确不支持 |

## 21. 验收标准（v1.0）

1. **启动即 gallery**：双击 exe，左栏列出所有示例，点击进入。
2. **示例矩阵齐备**（10a）：`basic`、`usercontrol`、`binding`、`list`、`theming`、`gallery` 六个可独立运行的示例。
3. **组件复用成立**：`usercontrol` 里两个卡片实例各有独立数据源，互不影响（10a.6 的四条断言）。
4. **控件页覆盖 P0 清单**：10a.7.1 的 23 个新控件 + 已有 9 个 = 32 个控件都有示例页，每页 ≥3 个可交互示例。
5. **每个示例可读代码**：页面上能看到对应 WAML 片段，且与真实文件一致（文件级引用，非手抄）。
6. **主题可切换**：Light/Dark 即时生效，无需重启。
7. **构建零警告**（`/W4`），测试全绿，probe 门禁通过。
8. **单文件分发**：无 DLL、无外部资源目录。

## 22. 下一步（按投入产出排序）

1. **抽出共享宿主**（迭代 0.1–0.3）：六个示例的前置条件。不做就是六份复制粘贴，改动发散，还会让 R6（代码漂移）提前发生。
2. **`samples/basic/`**（迭代 0.4）：不依赖任何新框架能力，能立刻验证「多示例」这条路走得通。
3. **`DataContext` 可绑定**（迭代 2b.1）：`usercontrol` 被它卡住；底层 `SetDataContext(dc, own)` 已支持本地值优先，只缺标记层入口，改动面小、信息量大。
4. **多页资源 + 路由 + 文件级引用**（迭代 1.1–1.2、1.6）：gallery 骨架，做完才有「图库」雏形。
5. **Grid**（迭代 2.1）：卡片布局硬需求，没有它每张卡都是竖排。
6. **ListBox 选中**（迭代 3.3）：把侧栏从按钮列表换成真正的列表控件。
7. **TextBox 路线决策**（R10）：IME 是唯一会让工期翻倍的风险点。建议先做 2 天垂直切片（`WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` + 候选窗定位），不达标就切原生 `EDIT` 孤岛——**WAML 语法不变**。
8. **绑定失败可观测到所有宿主**：Debug 下断言 `BindingErrorCount()` 为 0，让「拼错属性名」在开发期立即暴露。
9. **`wamlc` 仍不做**：解析 8 KB 仅 175 µs，启动期成本可忽略。

每完成一项都应能独立构建、跑测试、截图验证。

---

# 附录

## A. 测试清单（65 用例）

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
| scroll | 2 | 内容超出可滚动 + 两端钳制、短内容不滚动 |
| render | 3 | 记录式确定性、光栅确定性、主题切换 |
| perf | 5 | 5 项预算守卫 |

## B. 复现方式

```bat
pwsh -File build.ps1 all

build\release\waml_tests.exe
build\release\waml_interact.exe
build\release\waml_snapshot.exe
build\release\waml_probe.exe
build\release\waml_bench.exe
build\release\waml_demo.exe              :: 交互窗口
build\release\waml_demo.exe --selftest   :: 写 selftest.log 并返回退出码
```

## C. 构建环境说明

CMake/Ninja 在某些受限沙箱下**无法使用**：两者都通过管道捕获子进程 stdout 来解析诊断，而受限沙箱禁止创建该管道，导致 configure 挂起而非报错。`build.ps1` 直接驱动 `cl.exe`/`link.exe`（子进程继承 stdio），在受限与完全权限下都可工作。常规开发机仍应使用 CMake —— 这是**环境限制**，不是设计结论。

## D. 代码规模

| 区域 | 文件 | 行数 |
|---|---|---|
| `framework/src/waml` | 4 | ~1443 |
| `framework/src/mvvm` | 2 | ~652 |
| `framework/src/core` | 8 | ~2000 |
| `framework/src/render` | 4 | ~575 |
| `framework/src/platform` | 2 | ~400 |
| `tests` | 3 | ~1400 |
| `tools` | 5 | ~1300 |
| `bench` | 1 | ~257 |
| `samples/gallery` | 3 + 资源 | ~700 |
| **合计** | **~32** | **~8.7k** |

对照 H1 的「≤ 1500 行覆盖 WAML 子集」：`Parser.cpp` 实测 1327 行（含诊断文案、位置映射、标记扩展解析、验证器、实例化器），优于预期。
