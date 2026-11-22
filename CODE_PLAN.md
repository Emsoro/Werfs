# Werfs 实施计划与技术验证报告

> 版本：0.6.0.0
> 日期：2026-11-22
> 仓库：https://gitee.com/masonwu21/Werfs
> 技术基线：Windows 10/11 x64 · C++20 · MSVC 19.4x · MIT · 无 WebView/.NET/Qt/JS 引擎
> 参考项目（**不提交**）：`WinUI-Gallery-3.0.4/`（控件设计）、`microsoft-ui-xaml-winui3-release-2.7.0/`（XAML 解析）

---

## 0. 一句话说明

**Werfs = WAML（声明式界面） + C++20（自绘引擎） + MVVM（数据绑定），目标是做成完整的控件图库（Gallery）：每个控件都有可运行、可交互、可阅读代码的示例页。**

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

samples/                示例树：单向依赖框架
    shared/SampleHost   窗口、D2D、DPI、输入分发（不含任何页面逻辑）
    basic/              Basic.waml + BasicVm.h + App.cpp + resources/
    list/               List.waml  + ListVm.h  + App.cpp + resources/

tests/  bench/  tools/ 框架级工具，不依赖示例
build.ps1  CMakeLists.txt
```

**铁律**：`framework/` 里**没有**任何示例的 `.waml`、ViewModel 或 `IDR_WAML_*`。资源 id 属于示例，定义在各自的 `<Name>Resource.h`；框架只提供「按类型名 + id 读取 RCDATA」的通用接口。

新增示例 = 复制 `samples/basic/` 改名、改资源 id、在 `build.ps1` 的 `$SampleRegistry` 加一行即可，框架零改动。

> 资源头文件必须**每示例唯一**（`BasicResource.h` / `ListResource.h`）。同名的 `Resource.h` 会让同时引入两者的工具静默拿到其中一个，表现为「`IDR_WAML_*` 未声明」。
## 2. 能力清单

| 能力 | 状态 | 证据 |
|---|---|---|
| WAML 解析 + 静态校验（行列号 + 诊断码 + 建议） | ✅ | 99 用例 |
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
| **多示例 + 共享宿主**（`samples/shared`） | ✅ | 2 个示例（basic / list）共用同一 Win32+D2D 宿主 |
| **按页分派的验证工具** | ✅ | `snapshot` 与 `interact` 依据页面选择 ViewModel 与断言 |
| **文件选择器**（打开 / 保存 / 选文件夹） | ✅ 框架就绪 | `IFileDialogService` + Win32 实现；示例待做（§10b.1） |
| **动画**（隐式过渡 + 关键帧） | ⬜ 规划 | 需帧时钟与插值属性，见 §10b.2 |
| **Visibility + 转换器** | ✅ | `Collapsed` 不占位不点击；4 个内置转换器；未知名字是加载错误（7 用例） |
| **浮层**（Popup / Flyout） | ⬜ 规划 | z-order、点外关闭、边缘翻转，见 §10c.3 |
| **模态**（ContentDialog） | ⬜ 规划 | 遮罩 + 焦点陷阱，见 §10c.3 |
| **单选模型**（Selector 基类） | ✅ | `SelectedIndex`/`SelectedItem` 双向；`ListBox` 点击与键盘选中；7 用例 |
| **TabView**（多标签切换/关闭/新增） | ✅ | 数据驱动标签条、×关闭请求、+新增、横向溢出不依赖浮层；5 用例 |

## 3. 产物

| 产物 | 作用 |
|---|---|
| `waml_basic.exe` / `waml_list.exe` | 真实 Win32 窗口，可交互；`--selftest` 无窗口自检 |
| `waml_tests.exe` | 84 个单元/集成用例 |
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
| 来源 | `samples/<name>/<Name>.waml` | `WAML_DOC` RCDATA |
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
│ samples/   每个示例自带 WAML + ViewModel + 资源      │
│   basic/ list/ binding/ theming/ usercontrol/    │
│   files/ animation/                              │
│   ← 只写 WAML + ViewModel，不含渲染代码            │
│   shared/SampleHost  窗口/D2D/DPI/输入分发         │
├─────────────────────────────────────────────────┤
│ framework                                        │
│   waml     解析/校验/实例化                        │
│   mvvm     绑定/命令/集合                          │
│   core     控件/布局/输入/主题                      │
│   platform 资源/版本/路径/文件对话框(规划)           │
│   render   D2D / 软件光栅                          │
└─────────────────────────────────────────────────┘
```

**依赖方向恒为向下**：示例只依赖框架，框架不认识任何示例。新增能力（如文件对话框）加在 `platform`，示例通过接口调用。

## 9. 示例页面的区块模型

每个示例页由统一的区块构成，不是自由排版：

```
PageHeader        标题 + 一段说明（这个控件是什么、何时用）
ExampleCard ×N    每个示例教**一个**概念：
                    ├ 标题（例如 "TwoWay 绑定"）
                    ├ 可交互的演示区
                    └ 对应的 WAML 代码片段（可折叠）
相关控件          跳到相似控件的链接（页内锚点，非路由）
```

`basic` 与 `list` 目前是最朴素的形式（卡片直接排下来），`binding` 示例会把「代码片段可折叠」这一栏补上。

**不做路由**：曾经计划一个带侧栏导航的 gallery 应用（左栏列页、右栏切换）。现在每个示例是独立 exe，导航由操作系统完成——双击哪个跑哪个。少一层自研路由，也少一处 bug，代价只是切换示例要开新窗口。

---

## 10a. 示例矩阵（samples/）

示例不是「一堆 demo」，而是一条**由简到繁的学习路径**，同时是框架能力的验收矩阵。每个示例独立可构建、可运行、可单独截图，任何一个失败都说明某项框架能力退化。

### 10a.1 分层原则与顺序

示例按**概念难度递增**排列，顺序是刻意设计的：前四个不需要任何新框架能力，`usercontrol` 才开始逼出组件层设计。集大成的 gallery 已不再单独存在——每个示例本身就是它的一个分类页。

```
samples/
  1. basic/        基础：单个组件、单一数据源            ✅
  2. list/         集合：DataTemplate 与布局面板         ✅
  3. binding/      绑定：四种模式的完整对照              可立即开始
  4. theming/      主题：Light/Dark 与 token             可立即开始
  5. usercontrol/  组合：父子组件、两个独立数据源        需先补 DataContext
  6. files/        系统：打开/保存/选文件夹               需先补 IFileDialog（10b.1）
  7. editor/       集成：菜单 + 工具栏 + 多标签 + 未保存提示  需先补 Visibility/浮层/模态（10c）
  8. animation/    动效：隐式过渡与缓动                   需先补帧时钟（10b.2）
```

**为什么 `usercontrol` 排第五**：它依赖的能力（`DataContext` 可绑定）在框架里还不存在（见 10a.6）。把它排在概念演进的中段、而不是最前，可以让前几个示例立即开工，同时把框架改造集中在一次迭代里完成，而不是一开始就卡在设计决策上。

后三个（`files`、`editor`、`animation`）不是「控件演示」，而是**横切能力**的验收：它们要求框架提供全新机制，所以排在所有控件类示例之后，见 §10b、§10c。

`editor` 是**集成点**——它同时用到文件对话框、菜单、工具栏、标签栏和模态确认，是把前面所有能力串起来的那个示例，因此排在 `files` 之后。

**依赖方向**：示例之间**不互相引用**。每个示例自带 WAML、ViewModel 和资源 id。

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

### 10a.7 控件覆盖范围（原 gallery 职责）

曾经计划单独做一个 `gallery` 示例作为所有控件页的目录。**该计划已取消**：它本质上是把其他示例再抄一遍，维护成本高、教学价值低。取而代之的是——每个示例本身就是它的一个分类页，`build.ps1` 一次构建全部，想看哪个直接运行哪个。

因此本节保留下来的是**唯一仍然有用的部分**：控件清单与分级（下面 10a.7.1），用来决定「下一个做哪个控件」。

#### 10a.7.1 组件清单：来源与分级

清单取自本项目参考的 WinUI-Gallery 数据文件（`ControlInfoData.json`，**不提交**），18 个分组、126 个条目。剔除后剩 90 个待实现控件：

| 类别 | 数量 | 处理 |
|---|---|---|
| 概念/设计页（Fundamentals / Design / Accessibility） | 15 | 不是控件。其中 Binding、Style、Resources、Custom & User Controls 四项分别由 `binding/`、迭代 2、`usercontrol/` 覆盖 |
| 需要 OS 子系统集成 | 14 | **不做**：`WebView2`、`MediaPlayerElement`、`MapControl`、`Capture Element`、`Sound`、通知中心、Badge、JumpList、`AppWindow`、`AppWindowTitleBar`、`ContentIsland`、`System Backdrops`、`SystemBackdropElement`。注：**`Storage pickers` 已改判**——文件/文件夹选择不列为控件，而是作为横切能力实现，见 §10b.1 |
| 纯控件 | 97 | 已完成 15 个 → **82 个待实现**（其中 P0 余 16） |

**不追求一次做完**。按日常使用频率与实现成本分三档：

| 档位 | 数量 | 范围 | 何时做 |
|---|---|---|---|
| **P0 必做** | **23**（已完成 7） | 日常界面几乎必用、且**不需要新子系统** | 进行中 |
| P1 常用增强 | 16 | 共同卡在「浮层/弹出」这一层 | 浮层完成后顺带解锁 |
| P2 长尾 | 51 | 虚拟化集合、日期、媒体、动画、窗口管理 | 按需再议，不承诺 |

**P0 清单**（已完成 7 个标 ✅；原始 23 个 + 已有 9 个 = 32 个控件）：

| 分组 | 控件 | 新增框架能力 |
|---|---|---|
| 布局 | ✅ `Grid` | 行列定义 + 跨行跨列 |
| 布局 | ✅ `Canvas` | 绝对定位 |
| 布局 | ✅ `WrapPanel` | 换行排列 |
| 布局 | ✅ `VariableSizedWrapGrid` | 换行 + 单项跨行跨列 |
| 布局 | ✅ `Viewbox` | 缩放子内容 |
| 布局 | ✅ `Expander` | 折叠展开 + 高度变化重排 |
| 布局 | `RelativePanel` | **元素间相对关系**（需附加属性，见下） |
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

**布局这一组已覆盖 WinUI-Gallery 的 Layout 分类**：`Border`、`StackPanel`、`ScrollViewer` 已有；本轮补齐 6 个。`SplitView` 归 P1（需浮层式面板），`LayoutPanel` 是自定义布局基类，不作为示例条目。

**P0 里剩下的框架改造有五项**，其余是「注册类型 + 实现 Measure/Arrange/OnPaint」的重复劳动：

1. `RelativePanel` 的**附加属性机制**（`RelativePanel.RightOf="name"` 这类写法，当前解析器不支持带点号的属性名，也没有「按名字引用兄弟元素」的能力）
2. `Image` 的 WIC 解码与缓存
3. `PaintCtx` 增加 `DrawLine` / 路径绘制（支撑 `Line`、`Shape`、`ProgressRing`）
4. `RadioButton` 的组作用域
5. `TextBlock` 的 `FontFamily`（等宽字体，代码片段需要）

**已完成的三项**：`Grid` 行列布局、`VariableSizedWrapGrid` 跨行列、`TextBlock` 自动换行。

**关于 `RelativePanel` 的额外成本**：它需要两样框架目前都没有的东西——**附加属性**和**按 `x:Name` 查找兄弟元素**。`x:Name` 已经记录在 `NodeDesc::name` 里，但布局期没有「从名字解析到 Widget」的索引。若成本评估偏高，可降到 P1，用 `Grid` 的固定行列达成相近效果。

**P1 — 之后做（16 个）**：`ComboBox`、`Popup`、`Flyout`、`MenuFlyout`、`ToolTip`、`ContentDialog`、`TeachingTip`、`AutoSuggestBox`、`CommandBar`、`AppBarButton`、`AppBarToggleButton`、`AppBarSeparator`、`BreadcrumbBar`、`MenuBar`、`SplitView`、`RatingControl`

这 16 个共同卡在一个缺失子系统上：**浮层（Popup/Flyout）**。它要处理 z-order、点外关闭、焦点陷阱、窗口外定位，成本高于 P0 任意单项——但做完这一次，其中大多数会顺带完成。

> **P1 已部分提前**：`MenuBar`、`MenuFlyout`、`CommandBar` 及其按钮，加上 P2 里的 `TabView`，已合并进 **§10c** 单独规划——真实编辑器界面离不开它们，且共享同一套浮层与单选模型。分四批交付（§10c.4），第一批 `Visibility` 最便宜、可立刻做。

**P2 — 按需，不承诺（51 个）**：虚拟化集合（`ListView`/`GridView`/`TreeView`/`ItemsRepeater`/`FlipView`/`ItemsView`/`TableView`/`PullToRefresh`）、日期时间（4 个）、动画过渡（7 个）、导航容器（`NavigationView`/`TabView`/`Pivot`/`SelectorBar`）、分割按钮（3 个）、`ColorPicker`、`RichEditBox`、滚动增强（`PipsPager`/`PagerControl`/`SemanticZoom`/`AnnotatedScrollBar`/`ScrollView`）、画刷（`AcrylicBrush`/`RadialGradientBrush`）、`ThemeShadow`、`IconElement`/`AnimatedIcon`、`Compact Sizing`、`Clipboard`、`Chart`、`InkCanvas`、`AnimatedVisualPlayer`、`TitleBar`、`Window`、`LayoutPanel`、命令对象（`StandardUICommand`/`XamlUICommand`）

**为什么这样切**：P0 覆盖「写一个真实窗口界面」的绝大部分，且除 `RelativePanel`/`Image`/画线/组互斥外都是低风险增量。P1 被浮层统一卡住，集中攻克一次即可解锁一批。P2 里的虚拟化与动画是另一个量级（见 R4、§10b.2），混进 P0 会拖垮进度。

### 10a.8 实施顺序与依赖

| 顺序 | 示例 | 依赖的框架能力 | 状态 |
|---|---|---|---|
| 1 | `basic` | 无 | **✅ 已完成** |
| 2 | `list` | 无 | **✅ 已完成** |
| 3 | `binding` | 无 | 可立即开始 |
| 4 | `theming` | 无（主题已支持） | 可立即开始 |
| 5 | `usercontrol` | **`DataContext` 可绑定**（10a.6） | 需先补框架 |
| 6 | `files` | **`IFileDialogService`**（10b.1） | 需先补框架 |
| 7 | `editor` | **Visibility → Selector → 浮层 → 模态**（10c.3） | 需先补框架，分 4 批 |
| 8 | `animation` | **帧时钟 + 过渡**（10b.2） | 需先补框架 |

**关键判断**：`usercontrol` 是第一个被框架能力卡住的示例，也是**信息量最大**的一个——它逼出了「组件复用」这一层设计。排在第五意味着前四个可以先落地，把框架改造集中成一次迭代，而不是一开始就卡在设计决策上。

`files`、`editor`、`animation` 属于另一类：它们不是控件演示，而是**横切能力**的验收现场，各自要求框架补一套全新机制。详细设计见 §10b 与 §10c。

**三者之间的顺序有明确理由**：

1. `files` 先——纯增量，不碰渲染路径。
2. `editor` 其次——它是**集成点**，把文件对话框、菜单、工具栏、标签页、模态确认串起来。但它内部还要再分四批（§10c.4），其中 `Visibility` 最便宜、浮层最贵。
3. `animation` 最后——要改绘制与失效流程，且与滚动偏移的交互最微妙。

**关于控件覆盖数**：完整对齐 WinUI-Gallery 是 90 个待实现控件，**不一次做完**。已完成 15 个；P0 余 16 个，P1 的 16 个（含菜单、工具栏、`TabView` 相关）等浮层子系统完成后顺带解锁，P2 的 51 个按需再议。

### 10a.9 构建与打包

每个示例是**独立可执行文件**（便于单独调试与截图）：

| 目标 | 产物 | 说明 |
|---|---|---|
| *（无参数）* | 全部示例 | **默认**，等价于 `samples` |
| `samples` | 各示例 | 构建所有示例（release） |
| `basic` / `list` | 单个示例 | 只构指定示例 |
| `all` | 全部 | 示例 + 全部工具与测试 |

`build.ps1` 用 `$SampleRegistry` 表驱动：一个示例 = 表里一行，分发逻辑共用，不再为每个示例写目标函数。每个示例复用 `samples/shared/SampleHost`（Win32 窗口 + D2D + DPI + 输入分发），避免重复。

---

## 10b. 系统集成与动效（待实现能力）

这两项都**不是控件**，而是横切能力：文件选择器把 Win32 对话框桥接进 MVVM，动画给所有控件加一层随时间变化的属性层。共同点是**框架目前完全没有对应机制**，所以要先补基础设施，再做示例。

### 10b.1 文件与文件夹选择

#### 现状与缺口

示例目前只能演示内存里的数据。一旦要「打开一个文件」「另存为」，就撞上两个空缺：

1. **没有系统对话框的封装**。现代做法是 COM 的 `IFileOpenDialog` / `IFileSaveDialog`（Vista 起），能用它同时覆盖「选文件」「选文件夹」（`FOS_PICKFOLDERS`）「保存」。旧的 `GetOpenFileName` 是 ANSI 时代遗留，不支持文件夹选择，无现代导航栏，**不采用**。
2. **MVVM 里没有「请求宿主做某事」的通道**。ViewModel 不该直接 `new` 一个 HWND 对话框——那会让 ViewModel 依赖 UI 线程与窗口句柄，也就无法在无窗口的 `waml_interact` 里跑。

#### 设计

用**服务接口 + 命令**，ViewModel 只表达意图：

```cpp
// framework/src/platform/FileDialog.h
namespace wui {

struct FileFilter {
    std::wstring label;        // L"WAML 文件"
    std::wstring pattern;      // L"*.waml"
};

struct FileDialogRequest {
    std::wstring title;
    std::wstring defaultExtension;    // 不含点
    std::vector<FileFilter> filters;
    std::wstring initialPath;         // 可空
    bool pickFolders = false;         // true = 选目录
};

struct FileDialogResult {
    bool accepted = false;
    std::vector<std::wstring> paths;  // 多选时 >1
};

// 宿主实现；framework 只声明，不实现 Win32 部分。
class IFileDialogService {
public:
    virtual ~IFileDialogService() = default;
    virtual FileDialogResult ShowOpen(const FileDialogRequest&) = 0;
    virtual FileDialogResult ShowSave(const FileDialogRequest&) = 0;
};

// 当前宿主注册的服务；未注册时所有调用返回 accepted=false。
void SetFileDialogService(IFileDialogService* service);
IFileDialogService* FileDialogService();

} // namespace wui
```

ViewModel 侧只依赖接口，测试里注入一个返回固定结果的假实现即可。

#### WAML 用法

对话框不能凭空弹，必须由用户动作触发，所以挂成**命令**：

```xml
<StackPanel Orientation="Horizontal" Spacing="8">
  <Button Content="打开…"  Command="{Binding OpenCommand}"/>
  <Button Content="另存为…" Command="{Binding SaveCommand}"/>
  <Button Content="选文件夹…" Command="{Binding PickFolderCommand}"/>
</StackPanel>

<TextBlock Text="{Binding SelectedPath, StringFormat='已选择: {0}'}"
           TextWrapping="Wrap" Foreground="TextSecondary"/>
```

#### ViewModel 用法

```cpp
class EditorVm final : public sample::ObservableObject {
public:
    EditorVm() {
        open_.Set(std::make_shared<wui::RelayCommand>([this] { OnOpen(); }));
        Make("OpenCommand", open_);
        Make("SelectedPath", SelectedPath);
    }

    void OnOpen() {
        auto* dialogs = wui::FileDialogService();
        if (!dialogs) return;                       // 无宿主：静默，不崩溃

        wui::FileDialogRequest req;
        req.title = L"打开 WAML 文件";
        req.defaultExtension = L"waml";
        req.filters = {{L"WAML 文件", L"*.waml"}, {L"所有文件", L"*.*"}};

        const auto result = dialogs->ShowOpen(req);
        if (!result.accepted || result.paths.empty()) return;
        SelectedPath.Set(result.paths.front());
    }

    wui::ObservableProperty<std::wstring> SelectedPath{"SelectedPath", L"(未选择)"};
private:
    wui::ObservableProperty<wui::RefPtr> open_{"OpenCommand"};
};
```

#### 任务拆分

| 编号 | 任务 | 验收 |
|---|---|---|
| 10b.1.1 | `IFileDialogService` 接口 + 全局注册点 | 无宿主时返回 `accepted=false`，不崩 |
| 10b.1.2 | Win32 实现（`IFileOpenDialog` / `IFileSaveDialog` + `FOS_PICKFOLDERS` / `FOS_ALLOWMULTISELECT`） | 三种模式都能弹出并返回正确路径 |
| 10b.1.3 | COM 生命周期（`CoCreateInstance` + `CoTaskMemFree`） | 连续弹 100 次无泄漏、无崩溃 |
| 10b.1.4 | 宿主接入 `SampleHost` | 示例窗口里点击能弹出 |
| 10b.1.5 | 假实现供无窗口测试 | `waml_interact` 能断言「命令被调用」 |
| 10b.1.6 | `samples/files/` 示例 | 见下 |

#### 示例：`samples/files/`

| 区块 | 内容 |
|---|---|
| 1 打开文件 | 选文件 → 路径显示在界面 |
| 2 另存为 | 预设文件名 + 扩展名 + 过滤器 |
| 3 选文件夹 | `FOS_PICKFOLDERS`，只返回目录 |
| 4 只读演示 | 选中的文件读前 20 行显示（顺带验证平台层文件读取） |

**不做的部分**：不做最近文件、不做拖放（`WM_DROPFILES` 属另一能力）、不做路径记忆持久化。

#### 风险

| 风险 | 说明 | 应对 |
|---|---|---|
| R13 对话框阻塞 UI 线程 | 模态对话框期间不重绘 | 可接受（与系统行为一致）；不引入异步 |
| R14 COM 初始化顺序 | 宿主未 `CoInitialize` 时弹框失败 | `SampleHost` 已有 `ole32` 初始化，做成显式前置断言 |
| R15 无窗口环境 | 测试与 CI 无桌面 | 服务接口可注入假实现，测试永远不碰真对话框 |

### 10b.2 动画

#### 现状与缺口

所有属性变化都是**瞬时**的：`ScrollTo` 直接改 `offset_`，`SetExpanded` 直接改 `expanded_`。UI 能工作，但没有任何过渡，`Expander` 展开是硬跳变，悬停也没有渐变。

缺三样东西：

1. **帧时钟**：没有「每 16 ms 推进一次」的驱动。目前只在有输入时 `InvalidateRect`。
2. **可插值的属性**：属性槽是 `Value` 联合体，没有「从 A 到 B 的中间态」概念。
3. **动画对象与 WAML 语法**：用什么声明一段动画。

#### 设计（分两期，先做能立住的）

**一期：帧时钟 + 隐式过渡（`Transitions`）**

不做关键帧，只做「属性变化时自动补间」。这是 90% 场景，也避免了 `Storyboard`/`Timeline` 的重型语法。

```cpp
// framework/src/core/Animation.h
namespace wui {

enum class Easing { Linear, EaseOut, EaseInOut };

// 单个属性的补间状态。由 Widget 持有，按槽位索引。
struct Tween {
    int slot = -1;
    float from = 0, to = 0;
    float elapsed = 0, duration = 0;
    Easing easing = Easing::EaseOut;
    bool active = false;
};

// 全局帧时钟：宿主在消息循环里推进，返回 true 表示仍需重绘。
bool AdvanceAnimations(float deltaSeconds);
bool HasActiveAnimations();
void SetAnimationEnabled(bool);   // 测试与快照必须关掉

}
```

驱动方式：宿主在 `WM_TIMER` 或空闲时以固定步长推进，`AdvanceAnimations` 返回 true 时 `InvalidateRect`。**快照工具必须显式关闭动画**，否则确定性哈希会随帧漂移——这条要写进 `Snapshot.cpp`。

WAML 语法（新增 `Transitions` 属性，作用于单个控件）：

```xml
<Border Background="CardBackground" CornerRadius="8"
        Transition.Duration="0.18" Transition.Easing="EaseOut">
```

可动画的属性白名单（一期只做数值型）：`Opacity`、`CornerRadius`、`Width`、`Height`、`Margin`/`Padding` 的四个分量、`ScrollViewer` 的偏移、`Expander` 的展开高度。

**二期：关键帧（`Storyboard`）**

只有一期不够用时才做——例如「点按钮让卡片左右抖动」。届时引入：

```xml
<Storyboard x:Name="Shake">
  <DoubleAnimation Target="Card" Property="Canvas.Left"
                   From="0" To="12" Duration="0.1" AutoReverse="True" RepeatCount="3"/>
</Storyboard>
```

二期依赖 `x:Name` 查找（`RelativePanel` 也需要它，见 §10a.7.1），所以两者应一起做。

#### 任务拆分

| 编号 | 任务 | 验收 |
|---|---|---|
| 10b.2.1 | 帧时钟 + `AdvanceAnimations` | 测试里手动推进时间，断言中间值单调 |
| 10b.2.2 | 数值属性补间（`Opacity` 先行） | 0→1 过渡时 `Opacity` 落在 (0,1) |
| 10b.2.3 | `Transition.*` 属性注册 + 白名单 | 未列入的属性写 `Transition.Duration` 报诊断 |
| 10b.2.4 | `Expander` 接入（高度补间） | 折叠→展开不再硬跳 |
| 10b.2.5 | `ScrollViewer` 滚轮平滑滚动 | 一格滚轮产生约 150 ms 缓动 |
| 10b.2.6 | 快照工具强制关闭动画 | 双主题哈希仍确定 |
| 10b.2.7 | `samples/animation/` 示例 | 见下 |

#### 示例：`samples/animation/`

| 区块 | 内容 |
|---|---|
| 1 隐式过渡 | 点击切换 `Opacity` / `CornerRadius`，观察补间 |
| 2 缓动函数对比 | 三种 `Easing` 并排，同时启动 |
| 3 展开动画 | `Expander` 平滑展开 |
| 4 滚动缓动 | 长页面，滚轮有缓冲感 |
| 5 关闭动画 | 一个开关切 `SetAnimationEnabled(false)`，对比硬跳变 |

#### 风险

| 风险 | 说明 | 应对 |
|---|---|---|
| R16 确定性快照被破坏 | 动画让同一份 WAML 在不同帧渲染不同 | 快照/测试强制 `SetAnimationEnabled(false)`，并加门禁 |
| R17 帧驱动与空闲 | 无输入时也必须重绘，否则动画不动 | 有活动动画时持续 `InvalidateRect`，结束后停止（不空转） |
| R18 属性白名单蔓延 | 什么都能动会让布局每帧重算 | 布局类属性（`Width`/`Height`）标为 `AffectsMeasure`，动画期间限制只允许少量控件使用 |
| R19 与滚动修复的交互 | 滚动偏移现在在绘制期应用（§7.15），动画改的是同一个量 | 动画只改 `offset_` 的目标值，绘制期平移逻辑不变 |

### 10b.3 实施顺序建议

先做文件选择器，再做动画。理由：文件选择器是**纯增量**（新增文件 + 一个接口 + 一个示例），不触碰任何现有渲染路径；动画要改 `Widget` 的绘制与失效流程，风险高一个量级，应等滚动那批修复稳定之后再动。

两者都不阻塞 `binding` / `theming` 示例——那两个仍不依赖任何新能力，可以并行先做。

---

---

## 10c. 菜单 / 工具栏 / 多标签编辑界面（待实现能力）

这一组是**面向真实编辑器应用**的能力，比控件本身重要得多：真正的程序都有一个菜单栏、一条工具栏、一排标签页，以及「关掉时有未保存改动怎么办」。

它们共同压在同一套缺失机制上，所以放在一起规划，而不是拆成四个独立条目。

### 10c.1 共同缺口

| 缺口 | 影响 | 目前状态 |
|---|---|---|
| **浮层（Popup/Flyout）** | 菜单下拉、右键菜单、标签溢出列表都靠它 | **完全没有**。无 z-order、无点外关闭、无窗口外定位 |
| **模态（ContentDialog）** | 「未保存，是否保存？」必须模态 | **完全没有**。无遮罩、无焦点陷阱 |
| **单选模型（Selection）** | 标签栏要知道「当前是哪个」 | **没有**。`ListBox` 连选中都不支持 |
| **命令路由** | 菜单项与工具栏按钮调用同一个动作，且随焦点变化启用/禁用 | 只有控件级 `Command`，无全局路由 |

前两项已在 §10a.7 的 P1 里登记为「16 个控件被浮层卡住」。本节把它们**提前**，理由见 10c.4。

### 10c.2 控件与能力

#### 菜单

| 项 | 说明 | 新增能力 |
|---|---|---|
| `MenuBar` | 顶部横排（文件 / 编辑 / 查看） | 布局 + 浮层 |
| `MenuBarItem` | 一个可下拉的顶级项 | 浮层锚点 |
| `MenuFlyout` | 下拉面板 | **浮层** |
| `MenuFlyoutItem` | 单个菜单项（含图标、快捷键文本、禁用态） | 浮层内容 |
| `MenuFlyoutSeparator` | 分隔线 | 无（复用 `Line`） |
| `MenuFlyoutSubItem` | 二级子菜单 | 浮层嵌套 + 位置翻转 |

WAML 形态（`Flyout` 是附加属性形式，与 WinUI 一致）：

```xml
<MenuBar>
  <MenuBarItem Title="文件">
    <MenuFlyout>
      <MenuFlyoutItem Text="新建"  Accelerator="Ctrl+N"
                      Command="{Binding NewCommand}"/>
      <MenuFlyoutItem Text="打开…" Accelerator="Ctrl+O"
                      Command="{Binding OpenCommand}"/>
      <MenuFlyoutSeparator/>
      <MenuFlyoutItem Text="保存"  Accelerator="Ctrl+S"
                      Command="{Binding SaveCommand}"
                      IsEnabled="{Binding CanSave}"/>
      <MenuFlyoutSubItem Text="导出">
        <MenuFlyout>
          <MenuFlyoutItem Text="导出为 PNG" Command="{Binding ExportPngCommand}"/>
          <MenuFlyoutItem Text="导出为 SVG" Command="{Binding ExportSvgCommand}"/>
        </MenuFlyout>
      </MenuFlyoutSubItem>
    </MenuFlyout>
  </MenuBarItem>
  <MenuBarItem Title="编辑">…</MenuBarItem>
</MenuBar>
```

#### 工具栏

| 项 | 说明 | 新增能力 |
|---|---|---|
| `CommandBar` | 横排按钮组，可分主次区 | 布局 + 溢出 |
| `AppBarButton` | 图标 + 可选文字 | 图标（依赖 `Image`/`SymbolIcon`） |
| `AppBarToggleButton` | 可切换的工具栏按钮 | 复用 `ToggleButton` |
| `AppBarSeparator` | 竖分隔线 | `Line` 旋转或专用绘制 |
| 溢出（`IsCompact`） | 窗口窄时收进「⋯」 | 浮层 + 测量反馈 |

```xml
<CommandBar>
  <CommandBar.PrimaryCommands>
    <AppBarButton Icon="New"  Label="新建" Command="{Binding NewCommand}"/>
    <AppBarButton Icon="Save" Label="保存" Command="{Binding SaveCommand}"/>
    <AppBarSeparator/>
    <AppBarToggleButton Icon="Bold" Label="粗体" IsChecked="{Binding IsBold}"/>
  </CommandBar.PrimaryCommands>
</CommandBar>
```

#### 多标签

| 项 | 说明 | 新增能力 |
|---|---|---|
| `TabView` | 标签条 + 内容区 | **单选模型** + 横向滚动 |
| `TabViewItem` | 单个标签（标题 / 可关闭 / 内容） | 关闭按钮 + 脏标记 |
| 关闭 / 新增 | 每个标签的 `×`，末尾的 `+` | 命令 + 命中测试 |
| 溢出 | 标签过多时出现下拉列表 | 浮层 |
| 拖拽重排 | 可选，二期 | 拖放（`WM_DROPFILES` 之外的自定义拖拽） |

```xml
<TabView ItemsSource="{Binding Documents}"
         SelectedItem="{Binding ActiveDocument, Mode=TwoWay}"
         AddTabCommand="{Binding NewDocumentCommand}"
         CloseTabCommand="{Binding CloseDocumentCommand}">
  <TabView.ItemTemplate>
    <DataTemplate>
      <StackPanel Orientation="Horizontal" Spacing="6">
        <TextBlock Text="{Binding Title}"/>
        <!-- 未保存标记：有改动时显示 • -->
        <TextBlock Text="•" Foreground="Accent"
                   Visibility="{Binding IsDirty, Converter=...}"/>
      </StackPanel>
    </DataTemplate>
  </TabView.ItemTemplate>
</TabView>
```

> ⚠️ `Visibility` 与转换器当前都不存在，见 10c.3 的「绑定层补强」。

#### 未保存提示

这是本组里**唯一纯粹的流程问题**，也是最能体现 MVVM 价值的地方：

| 场景 | 行为 |
|---|---|
| 关闭单个标签 | 若 `IsDirty` → 弹确认（保存 / 不保存 / 取消） |
| 关闭窗口 | 逐个检查脏文档；多个脏文档可「全部保存」 |
| 切换到另一文档 | **不提示**（改动仍在内存，只是焦点变了） |

流程设计——**ViewModel 决定「问不问」，View 负责「怎么问」**：

```cpp
class DocumentVm {
public:
    void RequestClose() {
        if (!IsDirty.Get()) { Closed.emit(); return; }
        // 交给宿主弹窗：ViewModel 不持有对话框
        pendingClose_ = true;
        PromptCloseRequested.emit(FileTitle.Get());
    }

    // 宿主在用户选择后回调
    void ResolveClose(CloseChoice choice) {
        if (choice == CloseChoice::Save)     { Save(); Closed.emit(); }
        else if (choice == CloseChoice::Discard) { Closed.emit(); }
        // Cancel: 什么都不做，标签保留
        pendingClose_ = false;
    }
};
```

宿主侧用 `ContentDialog` 呈现三选一，按钮文案与默认按钮（Enter 触发「保存」还是「取消」）由 View 决定——**危险操作默认应落在「取消」上**。

### 10c.3 依赖的框架补强

菜单/工具栏/标签栏本身是「注册类型 + 实现 Measure/Arrange/OnPaint」，真正的成本在下面四项：

| 编号 | 补强项 | 说明 | 阻塞谁 |
|---|---|---|---|
| 10d.3.1 | **浮层子系统** | `PaintCtx` 增加「在当前控件之上绘制」的层；点外关闭；窗口边缘翻转；z-order | 菜单、子菜单、溢出列表 |
| 10d.3.2 | **模态遮罩** | 全窗口半透明层 + 焦点陷阱 + Esc 关闭 | 未保存提示 |
| 10d.3.3 | **单选模型** | `Selector` 基类：`SelectedIndex`/`SelectedItem`/`SelectionChanged`，`ListBox` 也接入 | `TabView`、未来的 `ComboBox` |
| 10d.3.4 | **命令路由** | 焦点所在控件 → 向上冒泡找处理器；快捷键分发（`Ctrl+S`） | 菜单快捷键、工具栏启用态 |
| 10d.3.5 | **绑定层补强** | `Visibility` 属性（`Visible`/`Collapsed`）+ `BoolToVisibility` 转换器 | 脏标记、禁用态的更复杂表达 |

**`Visibility` 值得单独说**：目前只能绑 `IsEnabled`，而「按条件显示/隐藏」在标签栏（脏标记）、信息栏、空状态里到处都是。它比浮层便宜得多，应该**先单独做完**——不等浮层。

### 10c.4 实施顺序

分成四批，每批独立可交付：

| 批次 | 内容 | 依赖 | 可验收的成果 |
|---|---|---|---|
| **A** | `Visibility` + `BoolToVisibility` | 无 | 能按条件隐藏元素；标签脏标记可用 |
| **B** | 单选模型 + `TabView` / `TabViewItem` | A | **多标签切换、关闭、新增**都能工作；关闭暂不提示 |
| **C** | 浮层 + `MenuBar` / `MenuFlyout` / `CommandBar` | A | 菜单可下拉、工具栏可溢出 |
| **D** | 模态 + 未保存提示 | B | 关脏标签时弹出「保存/不保存/取消」 |

**为什么这样排**：

- **A 最先**，因为它最便宜且立刻被 B 用上；单独做它也能改善现有示例（`InfoBar` 之类）。
- **B 排 C 之前**，尽管「多标签」听起来比「菜单」复杂。原因是：`TabView` 只需要**单选模型**（已能被 `ListBox` 复用），不需要浮层；而菜单没有浮层根本无法实现。先做能独立成立的那个。
- **C 的浮层**是这一整组的最大单项成本，但它同时解锁 P1 里的 16 个控件（`ComboBox`、`Flyout`、`ToolTip`、`ContentDialog`、`AutoSuggestBox`……），所以一次投入回报最大。
- **D 最后**，因为它是 B 和 C 的组合应用，本身不引入新机制。

### 10c.5 示例：`samples/editor/`

这是**第七个示例**，也是第一个「像真实程序」的示例：一个多文档文本编辑器骨架。

| 区块 | 内容 | 验证 |
|---|---|---|
| 1 菜单栏 | 文件（新建/打开/保存/另存为）/ 编辑（撤销/剪切/复制） | 下拉、快捷键、禁用态 |
| 2 工具栏 | 新建、保存、粗体开关 | 溢出（缩窄窗口） |
| 3 标签栏 | 三个初始文档，可切换、可关闭、可新建 | 单选、`×`、`+`、脏标记 |
| 4 未保存提示 | 改一个字符后关标签 | 三选一；选取消则标签保留 |
| 5 关闭窗口守卫 | 多个脏文档 | 逐个询问或「全部保存」 |
| 6 与文件对话框联动 | 菜单「打开」调用 §10b.1 | 打开真实文件并成为新标签 |

**这个示例同时验收 §10b.1（文件对话框）和 §10d（菜单/标签）**，是把前面所有能力串起来的集成点。建议放在 `files` 之后、`animation` 之前。

### 10c.6 风险

| 编号 | 风险 | 影响 | 应对 |
|---|---|---|---|
| R20 | 浮层的 z-order 与裁剪冲突 | `ScrollViewer` 的 clip 会裁掉弹出的菜单 | 浮层不走子树绘制，而是挂在根节点的「顶层列表」里 |
| R21 | 焦点陷阱不完整 | 模态期间 Tab 能跑到背后控件 | 模态期间把根节点标记为 `modalChild_`，命中测试直接短路 |
| R22 | 标签关闭时绑定泄漏 | 关掉标签后旧树仍订阅 VM | 关闭前 `DetachBindings()`，并有测试断言观察者归零 |
| R23 | 快捷键与 `TextBox` 冲突 | 在输入框里按 `Ctrl+A` 应全选文字而非触发菜单 | 命令路由先给焦点控件一次机会，未处理才冒泡 |
| R24 | 脏标记判定不准 | 撤销回初始状态后仍显示「已修改」 | 记录「初始快照哈希」，内容相同即视为干净 |
| R25 | 标签过多时布局退化 | 每个标签都参与测量，几十个时变慢 | 逐期：先做横向滚动，溢出下拉放二期 |

### 10c.7 不做

| 项 | 原因 |
|---|---|
| 拖拽重排标签 | 需要自研拖放（非 `WM_DROPFILES`），成本高于收益，二期再议 |
| 标签浮动成独立窗口 | 需要多窗口管理，另一个量级 |
| 撤销/重做栈 | 这是文档模型（`UndoStack`）而非 UI 能力，可由示例自己实现 |
| 富文本编辑 | `RichEditBox` 属 P2，与 GUI 框架层无关 |

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

## 12. 迭代 1：~~可导航的 Gallery 骨架~~ → **已取消**

原计划做一个带侧栏导航的单体应用：多页资源打包、VM 驱动路由、文件级引用其他示例的 WAML。

**取消理由**：这套设计的目的是「把六个示例组织成目录」，但六个示例本身已经是独立可执行文件，`build.ps1` 一条命令全部构建。为了在应用内部再模拟一次导航，需要引入多页资源枚举、路由、跨示例文件级引用三套机制——**每套都只为这一个应用服务**，而换来的是「不用关窗口就能切页」。

更关键的是它和工程质量冲突：文件级引用要求 gallery 在构建期读取 `samples/*/` 的源文件，这会让示例的构建产物依赖彼此的源码树，一处 WAML 语法错误会同时打挂多个目标。

**保留的部分**：其中「多文档资源打包」在单示例需要分页时仍有价值（例如 `theming` 示例要演示多种主题）。真到那时再单独评估，不作为路线节点。

**同期交付示例**：`binding/`、`theming/`（见 10a.3、10a.5），均不依赖任何新能力。

## 13. 迭代 2：布局与样式补齐（v0.7）

| 任务 | 说明 | 状态 |
|---|---|---|
| 2.1 `Grid` 控件 | 行列定义 + 跨行列 | **✅ 已完成** |
| 2.2 `Style` / `Setter` | 具名样式，可 `BasedOn` | 待做 |
| 2.3 `ResourceDictionary` | WAML 内定义颜色/尺寸常量 | 待做 |
| 2.4 等宽字体 | FontFamily 属性 | 待做 |
| 2.5 `StackPanel` 的 `Justify` | Start/Center/End/SpaceBetween | 待做 |

**风险**：`Style` 引入后，属性优先级（本地值 > 样式 > 默认值）需明确并测试。

**同期交付示例**：`samples/binding/`、`samples/theming/`（见 10a.3、10a.5，均不依赖本迭代的新能力，可与迭代 2 并行）。

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

## 15b. 迭代 3b：系统集成与动效（v0.8+）

三项横切能力，详细设计见 §10b 与 §10c。**顺序：文件对话框 → 编辑器界面 → 动画**，理由见下。

### 3b.A 文件选择器（先做）

| 任务 | 说明 | 验收 |
|---|---|---|
| 3b.A.1 | `IFileDialogService` 接口 + 注册点 | 无宿主时返回未接受，不崩溃 |
| 3b.A.2 | Win32 实现（打开 / 保存 / 选文件夹） | 三种模式均可用，路径正确 |
| 3b.A.3 | COM 生命周期与资源释放 | 连续 100 次无泄漏 |
| 3b.A.4 | 宿主接入 + 假实现供测试 | `waml_interact` 可断言命令被调用 |
| 3b.A.5 | `samples/files/` | 四个区块全部可交互 |

**为什么最先**：纯增量，不碰渲染路径，且后续的 `editor` 示例直接用它。

### 3b.B 编辑器界面（中间）

按 §10c.4 的四批推进，每批独立可交付：

| 任务 | 说明 | 验收 |
|---|---|---|
| 3b.B.1 | `Visibility` + `BoolToVisibility` | 按条件隐藏元素；脏标记可用 |
| 3b.B.2 | 单选模型（`Selector` 基类）+ `ListBox` 接入 | `SelectedIndex` 双向可用 |
| 3b.B.3 | `TabView` / `TabViewItem` | 多标签切换、关闭、新增可用 |
| 3b.B.4 | 浮层子系统（顶层列表 + 点外关闭 + 翻转） | 菜单能弹出且不被裁剪（R20） |
| 3b.B.5 | `MenuBar` / `MenuFlyout` / `CommandBar` | 菜单可下拉、工具栏可缩窄溢出 |
| 3b.B.6 | 模态 + `ContentDialog` | 遮罩、焦点陷阱、Esc 关闭 |
| 3b.B.7 | 未保存提示流程（三选一） | 选取消则标签保留 |
| 3b.B.8 | `samples/editor/` | 六个区块全部可交互 |

**为什么在动画之前**：它的前三批不依赖绘制流程改动（`Visibility` 是属性、`Selector` 是模型、`TabView` 用横向滚动），可以安全先做；只有浮层和模态需要新的绘制层，而那是「加一个顶层列表」，不是改现有绘制路径。

### 3b.C 动画（最后）

| 任务 | 说明 | 验收 |
|---|---|---|
| 3b.C.1 | 帧时钟 + `AdvanceAnimations` | 手动推进时间可断言中间值 |
| 3b.C.2 | 数值属性补间（`Opacity` 先行） | 过渡期间值落在两端之间 |
| 3b.C.3 | `Transition.*` 属性 + 白名单 | 白名单外属性报诊断 |
| 3b.C.4 | `Expander` 高度补间 | 不再硬跳变 |
| 3b.C.5 | `ScrollViewer` 平滑滚动 | 滚轮有约 150 ms 缓动 |
| 3b.C.6 | **快照强制关闭动画** | 双主题哈希仍确定（门禁） |
| 3b.C.7 | `samples/animation/` | 五个区块全部可交互 |

**关键约束**：动画一旦引入，**确定性快照**就必须显式关闭它，否则哈希随帧漂移。这条要写进 `Snapshot.cpp` 并有测试守住——§7.14/§7.15 已经证明「看起来能跑但显示不对」的缺陷代价很高。

**关键帧（`Storyboard`）留到二期**：它依赖 `x:Name` 查找，与 `RelativePanel` 是同一块基础设施，应一起做。

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
| R13 | 文件对话框阻塞 UI 线程 | 模态期间不重绘 | 可接受（与系统行为一致）；不引入异步 |
| R14 | COM 初始化顺序 | 宿主未 `CoInitialize` 时弹框失败 | `SampleHost` 已初始化 `ole32`，做成显式前置断言 |
| R15 | 无窗口环境无法测对话框 | CI 与 `waml_interact` 碰不到真对话框 | 服务接口可注入假实现，测试永不弹真框 |
| R16 | **动画破坏确定性快照** | 同一份 WAML 不同帧渲染不同，哈希漂移 | 快照与测试强制 `SetAnimationEnabled(false)`，加门禁测试 |
| R17 | 帧驱动与空闲空转 | 动画结束后仍在重绘，CPU 不降 | 仅在 `HasActiveAnimations()` 为真时续帧 |
| R18 | 动画属性白名单蔓延 | 布局类属性每帧重算，掉帧 | 一期只开放数值型；`AffectsMeasure` 属性限用 |
| R19 | 动画与滚动偏移交互 | 两者都改 `offset_`，可能互相覆盖 | 动画只改目标值，绘制期平移逻辑（§7.15）不动 |
| R20 | 浮层与裁剪冲突 | `ScrollViewer` 的 clip 会把弹出的菜单裁掉 | 浮层不进子树绘制，挂在根节点的顶层列表 |
| R21 | 焦点陷阱不完整 | 模态期间 Tab 跑到背后控件 | 模态时标记根节点，命中测试短路 |
| R22 | 标签关闭后绑定泄漏 | 旧树仍订阅 VM，内存增长 | 关闭前 `DetachBindings()`，测试断言观察者归零 |
| R23 | 快捷键与输入框冲突 | `Ctrl+A` 该全选文字而非触发菜单 | 命令路由先给焦点控件机会，未处理才冒泡 |
| R24 | 脏标记判定不准 | 撤销回初始状态仍显示「已修改」 | 记录初始内容哈希，相同即视为干净 |
| R25 | 标签过多布局退化 | 每标签都参与测量，几十个时变慢 | 一期横向滚动，溢出下拉放二期 |

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
1. **示例矩阵齐备**（10a）：`basic`、`list`、`binding`、`theming`、`usercontrol`、`files`、`editor`、`animation` 八个可独立运行的示例，`build.ps1` 无参数即全部构建。
2. **组件复用成立**：`usercontrol` 里两个卡片实例各有独立数据源，互不影响（10a.6 的四条断言）。
3. **系统集成可用**：`files` 示例里打开/保存/选文件夹三种对话框都能返回真实路径，且无窗口环境下不崩溃（10b.1）。
4. **编辑器界面可用**（§10c）：`editor` 示例能开多个标签、切换、关闭；改过内容后关标签会弹出「保存/不保存/取消」，选取消则标签保留。
5. **动效可用且不破坏确定性**：`animation` 示例的过渡肉眼连贯，同时双主题快照哈希仍确定（10b.2 + R16）。
6. **控件覆盖 P0 清单**：当前 15 个 + P0 余 16 个 = 31 个控件都有示例，每页 ≥3 个可交互示例。
7. **每个示例可读代码**：页面上能看到对应 WAML 片段，且与真实文件一致（文件级引用，非手抄）。
8. **主题可切换**：Light/Dark 即时生效，无需重启。
9. **构建零警告**（`/W4`），测试全绿，probe 门禁通过。
10. **单文件分发**：无 DLL、无外部资源目录。

## 22. 下一步（按投入产出排序）

1. **`binding` / `theming` 示例**（迭代 2 同期）：不依赖任何新框架能力，可立即开工。
2. **`DataContext` 可绑定**（迭代 2b.1）：`usercontrol` 被它卡住；底层已支持本地值优先，只缺标记层入口。
3. **`Visibility` + `BoolToVisibility`**（迭代 3b.B.1、§10c.3）：**性价比最高的一项**——一天量级的工作，立刻改善所有示例的条件显示，并解锁标签脏标记。
4. **`Style` / `ResourceDictionary`**（迭代 2.2–2.3）：示例页 WAML 属性重复较多，样式系统能显著缩短。
5. **文件选择器**（迭代 3b.A、§10b.1）：纯增量，不碰渲染路径，却能给所有示例加上真实文件 I/O。
6. **单选模型 + `TabView`**（§10c.4 批次 B）：多标签是「像真实程序」的第一步，且不依赖浮层。
7. **浮层 + 菜单/工具栏**（§10c.4 批次 C）：最大单项成本，但一次解锁 P1 的 16 个控件。
8. **模态 + 未保存提示**（§10c.4 批次 D）：`editor` 示例的收尾，本身不引入新机制。
9. **动画一期**（迭代 3b.C、§10b.2）：要改绘制与失效流程，风险最高；**动工前先写好 R16 的门禁测试**。
10. **`TextBox` 路线决策**（R10）：IME 是唯一会让工期翻倍的风险点。建议先做 2 天垂直切片（`WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` + 候选窗定位），不达标就切原生 `EDIT` 孤岛——**WAML 语法不变**。
11. **绑定失败可观测到所有宿主**：Debug 下断言 `BindingErrorCount()` 为 0，让「拼错属性名」在开发期立即暴露。
12. **`wamlc` 仍不做**：解析 8 KB 仅 175 µs，启动期成本可忽略。

每完成一项都应能独立构建、跑测试、截图验证。

---

# 附录

## A. 测试清单（99 用例）

`waml_tests.exe` 可带过滤子串只跑一组，例如 `waml_tests.exe grid`。

| 分组 | 数量 | 覆盖 |
|---|---|---|
| parse | 8 | 元素/属性/嵌套/注释/处理指令/实体/文本内容/属性元素/`x:Name` |
| diag | 11 | 15 类诊断码矩阵 + 建议质量 + 码点列号 + 转义 + 索引器拒绝 |
| value | 2 | 颜色（token/hex/非法）、Thickness（1/2/4 值） |
| instantiate | 3 | 树结构、默认值、绑定附加、DataContext 继承 |
| binding | 18 | 4 模式、默认模式、StringFormat、类型转换、嵌套路径、缺失路径、LostFocus、回声抑制、重绑、生命周期、诊断 sink、失败恢复 |
| list | 3 | 模板实例化、数据上下文隔离（回归）、订阅泄漏 |
| layout | 4 | 垂直/水平排列、Border padding、失效冒泡、显式尺寸 |
| grid | 5 | 星号分配、固定+星号混合、跨列、间距、auto 列自适应 |
| wrappanel / viewbox / expander | 3 | 换行、等比缩放、两态高度 |
| canvas | 3 | 绝对定位、子项尺寸、在 StackPanel 内的高度传递 |
| variablesizedwrapgrid | 2 | 跨列、跨行不重叠 |
| border | 1 | Canvas 内的显式高度 |
| scrollviewer | 5 | 滚动范围与钳制、短内容、空白处滚轮、视口外不可点、四角探测 |
| input | 6 | 点击/键盘/禁用、TextBox 输入、CheckBox 切换、命中测试 |
| render | 3 | 记录式确定性、光栅确定性、主题切换 |
| perf | 5 | 5 项预算守卫 |
| visibility | 2 | `Collapsed` 不占位、不参与命中测试与绘制 |
| converter | 3 | `BoolToVisibility` 双向、`Not` 取反、未知名字报加载错误并给建议 |
| filedialog | 2 | 无宿主时安全返回、假实现往返路径 |
| selector | 3 | 点击选中、双向往返、键盘移动并钳制 |
| tabview | 5 | 切换标签换内容、点×不选中、+调命令、增删后选中有效、标题来自模型 |
| *（无前缀）* | 2 | 页面级集成：basic 页滚动、滚动后按钮仍可点 |

## B. 复现方式

```bat
pwsh -File build.ps1 all

build\release\waml_tests.exe
build\release\waml_interact.exe
build\release\waml_snapshot.exe
build\release\waml_probe.exe

build\release\waml_basic.exe          :: 交互窗口（基础控件示例）
build\release\waml_basic.exe --selftest :: 写 selftest.log 并返回退出码
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
| `samples/basic` + `samples/list` | 各 4 + 资源 | ~800 |
| `samples/shared` | 2 | ~480 |

对照 H1 的「≤ 1500 行覆盖 WAML 子集」：`Parser.cpp` 实测 1327 行（含诊断文案、位置映射、标记扩展解析、验证器、实例化器），优于预期。
