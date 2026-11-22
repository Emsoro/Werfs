# Gridform

**WAML + C++20 + MVVM 的原生 Windows UI 框架与控件图库。**

用声明式的 WAML 描述界面，用 C++20 编写逻辑，用 MVVM 连接两者。所有渲染、布局、输入、焦点都由自研的 C++ 引擎完成 —— 没有 WebView、没有 .NET、没有 Qt、没有 JS 引擎。产物是单文件 exe。

> 版本 **0.5.0.0** · MIT · Windows 10/11 x64

---

## 这解决什么问题

Windows 上做原生界面，传统有两条路：Win32 裸写（繁琐、易错）或引入重型框架（依赖多、体积大）。Gridform 走第三条：**保留 Win32 的轻量，但用标记语言描述界面**。

```xml
<?waml version="0.1"?>
<Page Background="AppBackground">
  <StackPanel Orientation="Vertical" Spacing="12" Margin="24">
    <TextBlock Text="{Binding Title}" FontSize="28" FontWeight="SemiBold"/>
    <TextBox Text="{Binding Name, Mode=TwoWay}" PlaceholderText="Type a name"/>
    <CheckBox Content="Enabled" IsChecked="{Binding Enabled, Mode=TwoWay}"/>
    <Button Content="+" Command="{Binding IncrementCommand}"/>
    <TextBlock Text="{Binding Count, StringFormat='Count: {0}'}"/>
  </StackPanel>
</Page>
```

改界面不必重编译（debug 模式下），改逻辑只碰 ViewModel。

---

## 特性

| 能力 | 说明 |
|---|---|
| **WAML 声明式界面** | 元素、属性、属性元素、文本内容、CDATA、实体、标记扩展 |
| **静态校验** | 未知元素/属性、类型转换失败、语法错误全部在**加载期**报错，带行列号与建议 |
| **四种绑定模式** | `OneTime` / `OneWay` / `TwoWay` / `OneWayToSource`，默认按目标属性决定 |
| **DataTemplate** | 列表项由 WAML 描述，按项实例化 |
| **自绘引擎** | Direct2D + DirectWrite，无窗口也能离屏渲染（WARP + WIC） |
| **高 DPI** | DIP 布局 + 像素输入，已验证 200% 缩放 |
| **主题** | Light/Dark 双主题，13 个语义 token |
| **单文件分发** | 图标、清单、版本信息、WAML 全部打包进 exe |

### 内置控件

按类别分组（共 15 个控件 + `DataTemplate`）：

| 类别 | 控件 |
|---|---|
| 布局 | `Grid`（行列 + 跨行跨列）· `StackPanel` · `WrapPanel` · `VariableSizedWrapGrid` · `Canvas` · `Viewbox` · `Expander` · `Border` · `Page` · `ScrollViewer` |
| 文本 | `TextBlock`（支持 `TextWrapping="Wrap"`）· `TextBox` |
| 输入 | `Button` · `CheckBox` |
| 集合 | `ListBox` + `DataTemplate` |

面板放置属性写在**子元素**上（本框架无附加属性语法）：

```xml
<Grid ColumnDefinitions="36,1*,auto" ColumnSpacing="12">
  <TextBlock Grid.Column="0" Text="1." />
  <TextBlock Grid.Column="1" Text="Item" />
  <TextBlock Grid.Column="2" Text="layout" />
</Grid>
```

`ColumnDefinitions` 支持 `Auto` / `1*` / `80`，大小写不敏感；`Canvas.Left`/`Top` 做绝对定位；`VariableSizedWrapGrid.ColumnSpan`/`RowSpan` 做混合尺寸贴片。

---

## 构建

需要 **PowerShell 5.1+** 和 **Visual Studio 2022**（含 C++ 工作负载）。脚本会自动定位 VS（优先 `vswhere`，回退到常规安装路径），无需手动打开 Developer PowerShell。

```powershell
pwsh -File build.ps1              # 优化构建，WAML 打包进 exe（默认 release）
pwsh -File build.ps1 debug        # 调试构建，WAML 运行时读磁盘文件
pwsh -File build.ps1 all          # 构建全部目标
pwsh -File build.ps1 tests        # 只构建测试
pwsh -File build.ps1 all -Clean   # 清空输出目录后重建
```

### 目标（Target）

| 目标 | 产物 | 说明 |
|---|---|---|
| `release` | `waml_demo.exe` | 优化构建，默认（gallery） |
| `debug` | `waml_demo.exe` | 符号 + WAML 读磁盘，改界面免重编 |
| `all` | 全部 | 以下全部都建（release） |
| `tests` | `waml_tests.exe` | 79 个用例 |
| `bench` | `waml_bench.exe` | 性能基线 |
| `snapshot` | `waml_snapshot.exe` | D2D 渲染 + 确定性校验 |
| `interact` | `waml_interact.exe` | 无窗口输入驱动 |
| `probe` | `waml_probe.exe` | 资源门禁，**构建后自动运行** |
| `basic` | `waml_basic.exe` | 基础控件示例 |
| `list` | `waml_list.exe` | 集合与布局示例 |
| `samples` | 全部示例 | 一次构建所有示例 |
| `probedbg` | 同上 | 调试构建的门禁 |
| `res` | `.res` | 只编译资源 |
| `icon` | `app.ico` | 重新生成图标 |
| `dpishot` | `waml_dpishot.exe` | DPI 截图工具 |

### 输出目录

产物默认在 `build\<config>\`。要输出到别处：

```powershell
pwsh -File build.ps1 all -OutDir D:\dist
```

也可以用环境变量（兼容旧用法，`-OutDir` 优先）：

```powershell
$env:WUI_OUT = 'D:\dist'
pwsh -File build.ps1 all
```

> **为什么建议输出到工程外**：由自动化/沙箱会话创建的可执行文件可能带上一条未解析 SID 的权限项（`S-1-4-*`），会让 SmartScreen 每次启动都弹「Windows 已保护你的电脑」，即使二进制完全正常、且与手动构建的逐字节相同。诊断：`icacls 目标.exe | findstr /i "S-1-4"`。

### 版本号

单一来源，CI 可用同名环境变量覆盖：

```powershell
$env:WUI_VERSION_MINOR = '6'
pwsh -File build.ps1 all
```

也可直接改 `samples/gallery/resources/Resource.h` 的默认值。

### 为什么是 PowerShell 直接驱动编译器

脚本直接调用 `cl.exe` / `link.exe` / `rc.exe`，不经过 CMake —— CMake 与 Ninja 都通过管道捕获子进程 stdout 来解析诊断，在受限环境下该管道无法创建，导致 configure **挂起而非报错**。直接调用工具链让子进程继承 stdio，受限与完全权限下都能工作。

从 cmd 迁到 PowerShell 还顺带消除了两个曾耗费大量排查时间的缺陷：

- `%~dp0` 末尾的反斜杠让 `/I"%~dp0"` 吞掉下一个参数，报 `D8003: missing source filename`；
- `/Fo"dir\"` 同样让尾部反斜杠转义引号，`.obj` 落到当前目录而非目标目录。

现在参数以数组传递，不再经过任何命令行解析。

CMake 仍然可用（`CMakeLists.txt` 已同步），适合常规开发机；本仓库以 `build.ps1` 为主路径。

---

## 目录结构

框架与示例是两棵独立的树：**框架不认识任何具体示例**，示例单向依赖框架。

```text
framework/src/
    core/       Widget 树、布局、失效传播、控件、值类型、主题
    mvvm/       ObservableProperty / ObservableList / RelayCommand / Binding
    waml/       Lexer、Parser、Validator、诊断、实例化
    platform/   Resources：资源与文件加载、VERSIONINFO、路径解析
    render/     SoftwareRender（测试用）、D2DRender（真实后端）

samples/                ← 示例，单向依赖框架
    shared/                共享宿主：窗口、D2D、DPI、输入分发（不含任何页面逻辑）
    gallery/               控件总览页
    basic/                 **已实现** 基础控件：TextBlock/Button/TextBox/CheckBox/Border
    list/                  **已实现** 集合与布局：ListBox + Grid/WrapPanel/Canvas/VariableSizedWrapGrid/Viewbox/Expander
    binding/               （规划 2）四种绑定模式对照
    theming/               （规划 3）Light/Dark 主题
    usercontrol/           （规划 4）父子组件、两个独立数据源

tests/   bench/   tools/     ← 框架级工具，不依赖示例
```

关键约束：`framework/` 里**没有**任何 `Gallery.waml`、`GalleryVm` 或 `IDR_WAML_*` 的引用。资源 id 属于示例，定义在 `samples/gallery/resources/Resource.h`；框架只提供「按类型名 + id 读取 RCDATA」的通用接口。

新增示例：复制 `samples/gallery/` 改名、改 `Resource.h` 里的 id、在 `build.ps1` 加一个目标函数即可，框架侧零改动。

### 示例路线

示例不是一堆 demo，而是一条由简到繁的学习路径，同时是框架能力的验收矩阵：

| 顺序 | 示例 | 内容 | 状态 |
|---|---|---|---|
| 1 | `basic` | **基础控件**：TextBlock / Button / TextBox / CheckBox / Border，单一数据源 | **已实现** |
| 2 | `list` | **集合 + 布局**：ListBox/DataTemplate、Grid、WrapPanel、VariableSizedWrapGrid、Canvas、Viewbox、Expander | **已实现** |
| 3 | `binding` | 四种绑定模式并排对照 | 规划中 |
| 4 | `theming` | Light/Dark 切换与 token | 规划中 |
| 5 | `usercontrol` | **父子组件 + 两个独立数据源**，验证组件复用 | 规划中，需先补 `DataContext` 可绑定 |
| 6 | `gallery` | 把全部控件页组织成可浏览的图库（产品形态） | 演进中 |

两个示例分工明确：`basic` 是**入门页**（五个控件、一个数据源，看完就会写第一段 WAML），`list` 是**布局验收现场**（把六个面板装进一个真实页面，也确实因此暴露了 `auto` 列宽、跨行重叠、文字换行三个缺陷）。

`usercontrol` 排在第五，因为它依赖的能力（`DataContext` 可绑定）目前还不存在；放在中段可以让前面几个示例先落地，而不是一开始就卡在设计决策上。

所有示例共用 `samples/shared/SampleHost`（Win32 窗口 + D2D + DPI + 输入分发），示例本身只提供 WAML、ViewModel 和标题。加一个新示例 = 新建目录 + 三件套 + 在 `build.ps1` 注册一个目标。详细规划见 `CODE_PLAN.md` §10a。

### Gallery 的组件范围

对齐 WinUI-Gallery 的完整清单是 **90 个待实现控件**（已剔除 15 个概念页与 14 个需要 OS 集成的条目）。本项目**不追求一次做完**，按使用频率分三档：

| 档位 | 数量 | 内容 | 何时做 |
|---|---|---|---|
| **P0 必做** | 23（**7 已完成**） | ✅ `Grid` `Canvas` `WrapPanel` `VariableSizedWrapGrid` `Viewbox` `Expander` ／ 待做 `RelativePanel` `ToggleButton` `RepeatButton` `HyperlinkButton` `RadioButton` `Slider` `ToggleSwitch` `PasswordBox` `NumberBox` `ProgressBar` `ProgressRing` `InfoBar` `InfoBadge` `Image` `PersonPicture` `Line` `Shape` | 进行中 |
| P1 常用增强 | 16 | `ComboBox` `Popup` `Flyout` `ToolTip` `ContentDialog` `MenuFlyout` `CommandBar` `AutoSuggestBox` 等 | 浮层子系统完成后 |
| P2 长尾 | 51 | 虚拟化集合、日期时间、动画过渡、导航容器、媒体等 | 按需，不承诺 |

目前 **15 个控件可用**（原有 9 个 + 本轮 6 个布局面板）。P0 完成后共 32 个。六项框架改造里已完成三项：`Grid` 行列布局、`VariableSizedWrapGrid` 跨行列换行、`TextBlock` 文字换行；待做 `RelativePanel` 附加属性、`Image` 的 WIC 解码、`PaintCtx` 画线/路径、`RadioButton` 组作用域。

---

## 工具

| 目标 | 用途 |
|---|---|
| `waml_demo.exe` | 真实 Win32 窗口 |
| `waml_demo.exe --selftest` | 无窗口自检，写 `selftest.log`，退出码表示结果 |
| `waml_tests.exe` | 65 个单元/集成用例 |
| `waml_interact.exe` | 无窗口驱动真实输入（点击、键盘、切换） |
| `waml_snapshot.exe` | D2D 渲染 + 确定性校验（两次渲染 SHA 必须相同） |
| `waml_probe.exe` | 资源与交付模式门禁（缺失资源、陈旧 `.res`、权限标记） |
| `waml_bench.exe` | 性能基线 |

所有工具遵循同一个交付开关：**debug 读 `samples/gallery/*.waml`，release 读 exe 内嵌资源**；显式传路径时以路径为准。

---

## 诊断

所有错误在加载期给出，带行列号（列按 Unicode 码点计）：

```text
demo.waml(12,5): error WAML0102: 'wui/TextBlock' has no property 'Txt' (did you mean 'Text'?)
```

| 码段 | 含义 |
|---|---|
| WAML0001–0004 | 词法、标签结构、重复属性、属性格式 |
| WAML0101–0106 | 未知元素、未知属性、只读属性、值转换失败、内容模型、必需属性缺失 |
| WAML0201–0204 | 标记扩展语法、模式不兼容、绑定路径、重复名字 |
| WAML0301 | DataTemplate 根元素数量 |

运行期绑定失败通过 `SetBindingDiagnostics()` 注入的 sink 上报，并提供 `BindingErrorCount()` 计数 —— **不会静默失败**。

---

## 性能基线

实测于本机（MSVC `/O2`，预热后取 median）：

| 操作 | 实测 |
|---|---|
| 解析+校验 | 45–53 MB/s |
| 绑定更新（OneWay） | 89 ns |
| 双向回写 | 111 ns |
| 布局（100 项列表） | ~102 µs |
| 软件光栅 1280×800 一帧 | 0.41 ms |

`ListBox` 不做虚拟化，**建议项数 ≤ 500**。

---

## 构建说明

本项目用 `build.ps1` 直接驱动 `cl.exe`/`link.exe`/`rc.exe`，而不是 CMake：CMake 与 Ninja 都通过管道捕获子进程 stdout 来解析诊断，在某些受限环境下该管道无法创建，导致 configure 挂起。这是**环境限制**，不是设计结论 —— 常规开发机请用 CMake（`CMakeLists.txt` 已保留并同步）。详见上文「为什么是 PowerShell 直接驱动编译器」。

版本号单一来源：`build.ps1` 的 `WUI_VERSION_*` → `resources/Resource.h` → `app.rc` → 回读到窗口标题。

---

## 已知边界

- **无样式系统**：没有 `Style`/`Setter`/`VisualState`，外观逐属性写。
- **无 Grid**：只有线性的 `StackPanel`，复杂版面需嵌套模拟。
- **无动画**：主题切换是瞬时的。
- **`ListBox` 无选中**：只渲染列表，无 `SelectedIndex`。
- **`DataContext` 不可绑定**：`{Binding ..., Source=...}` 在 v0.5 被明确拒绝，无法在 WAML 里给子树换数据源——这意味着**组件复用目前只能靠复制 WAML**。这是实现 `usercontrol` 示例的前置阻塞项。
- **不虚拟化**：见性能基线。

这些都在路线图里，见 `CODE_PLAN.md`。

---

## 文档

| 文件 | 内容 |
|---|---|
| `CODE_PLAN.md` | 实施计划 + 技术验证报告：架构决策、迭代路线、风险登记、缺陷复盘 |
| `samples/gallery/Gallery.waml` | 覆盖全部已实现语法的示例 |

---

## 许可

MIT
