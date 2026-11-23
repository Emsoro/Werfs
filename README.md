# Werfs

**WAML + C++20 + MVVM 的原生 Windows UI 框架与控件图库。**

用声明式的 WAML 描述界面，用 C++20 编写逻辑，用 MVVM 连接两者。所有渲染、布局、输入、焦点都由自研的 C++ 引擎完成 —— 没有 WebView、没有 .NET、没有 Qt、没有 JS 引擎。产物是单文件 exe。

> 版本 **0.9.0.0** · MIT · Windows 10/11 x64

---

## 这解决什么问题

Windows 上做原生界面，传统有两条路：Win32 裸写（繁琐、易错）或引入重型框架（依赖多、体积大）。Werfs 走第三条：**保留 Win32 的轻量，但用标记语言描述界面**。

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
| **四种绑定模式** | `OneTime` / `OneWay` / `TwoWay` / `OneWayToSource`，默认按目标属性决定；`FallbackValue`、`UpdateSourceTrigger=LostFocus` |
| **DataTemplate** | 列表项由 WAML 描述，按项实例化 |
| **自绘引擎** | Direct2D + DirectWrite，无窗口也能离屏渲染（WARP + WIC） |
| **高 DPI** | DIP 布局 + 像素输入，已验证 200% 缩放 |
| **主题** | Light/Dark 双主题，12 个语义 token；`HostOptions::themeProperty` 让页面用一个 bool 实时切换 |
| **应用外壳** | 菜单栏（含二级子菜单）· 工具栏（窄窗自动溢出）· 多标签（切换/关闭/新建/脏标记）· 模态确认对话框 |
| **文件对话框** | 打开 / 另存为 / 选文件夹，`IFileDialogService` 可注入假实现供无窗口测试 |
| **快捷键** | `Ctrl+N` `Ctrl+O` `Ctrl+S` `Ctrl+W`；菜单上显示的提示与真正生效的按键是同一个字符串 |
| **单文件分发** | 图标、清单、版本信息、WAML 全部打包进 exe |

### 内置控件

按类别分组（共 26 个控件 + `DataTemplate`）：

| 类别 | 控件 |
|---|---|
| 布局 | `Grid`（行列 + 跨行跨列）· `StackPanel` · `WrapPanel` · `VariableSizedWrapGrid` · `Canvas` · `Viewbox` · `Expander` · `Border` · `Page` · `ScrollViewer` |
| 文本 | `TextBlock`（`TextWrapping="Wrap"`）· `TextBox`（多行折行、填满列宽） |
| 输入 | `Button` · `CheckBox` |
| 集合 | `ListBox` + `DataTemplate` · `TabView`（切换/关闭/新建） |
| 外壳 | `MenuBar` · `MenuBarItem` · `MenuFlyout` · `MenuFlyoutItem` · `MenuFlyoutSubItem` · `MenuFlyoutSeparator` · `CommandBar` · `AppBarButton` · `AppBarSeparator` · `ContentDialog` |

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
| *（无参数）* | 全部示例 | **默认**：等价于 `samples` |
| `samples` | `waml_basic.exe` `waml_list.exe` `waml_binding.exe` `waml_theming.exe` `waml_editor.exe` | 构建所有示例（release） |
| `basic` | `waml_basic.exe` | 只构基础控件示例 |
| `list` | `waml_list.exe` | 只构集合与布局示例 |
| `binding` | `waml_binding.exe` | 只构绑定模式示例（四种模式对照） |
| `theming` | `waml_theming.exe` | 只构主题示例（token + 实时 Light/Dark） |
| `editor` | `waml_editor.exe` | 只构编辑器示例（菜单/工具栏/多标签/文件对话框） |
| `all` | 全部 | 示例 + 全部工具与测试 |
| `debug` | 各示例 | 符号 + WAML 读磁盘，改界面免重编 |
| `tests` | `waml_tests.exe` | 205 个用例 |
| `bench` | `waml_bench.exe` | 性能基线 |
| `snapshot` | `waml_snapshot.exe` | D2D 渲染 + 确定性校验 |
| `interact` | `waml_interact.exe` | 无窗口输入驱动 |
| `probe` | `waml_probe.exe` | 资源门禁，**构建后自动运行** |
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
$env:WUI_VERSION_MINOR = '7'
pwsh -File build.ps1 all
```

也可直接改 `samples/basic/resources/BasicResource.h` 的默认值。

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
    core/       Widget 树、布局、失效传播、控件、值类型、主题、浮层（Overlay）
    mvvm/       ObservableProperty / ObservableList / RelayCommand / Binding
    waml/       Lexer、Parser、Validator、诊断、实例化
    platform/   Resources（资源/版本/路径）、FileDialog（打开/保存/选文件夹）
    render/     SoftwareRender（测试用）、D2DRender（真实后端）

samples/                ← 示例，单向依赖框架
    shared/                共享宿主：窗口、D2D、DPI、输入分发、浮层分派
    basic/                 **已实现** 基础控件：TextBlock/Button/TextBox/CheckBox/Border
    list/                  **已实现** 集合与布局：ListBox + Grid/WrapPanel/Canvas/VariableSizedWrapGrid/Viewbox/Expander/TabView
    editor/                **已实现** 集成：菜单 + 工具栏 + 多标签 + 未保存提示 + 文件对话框
    binding/               **已实现** 四种绑定模式对照（含 FallbackValue / LostFocus）
    theming/               **已实现** Light/Dark 实时切换与 token 面板
    usercontrol/           （规划）父子组件、两个独立数据源   ← 需先补 DataContext 可绑定
    animation/             （规划）隐式过渡与缓动            ← 需先补帧时钟

tests/   bench/   tools/     ← 框架级工具，不依赖示例
```

关键约束：`framework/` 里**没有**任何示例的 `.waml`、ViewModel 或 `IDR_WAML_*`。资源 id 属于示例，定义在 `samples/basic/resources/BasicResource.h`；框架只提供「按类型名 + id 读取 RCDATA」的通用接口。

新增示例：复制 `samples/basic/` 改名、改 `Resource.h` 里的 id、在 `build.ps1` 的 `$SampleRegistry` 加一行即可，框架侧零改动。

> 各示例的资源头文件名必须唯一（`BasicResource.h`、`ListResource.h`…）。两个示例都用 `Resource.h` 时，工具同时引入两者会撞名，先命中的那个会静默胜出——这正是 `IDR_WAML_BASIC 未声明` 的由来。

### 示例路线

示例不是一堆 demo，而是一条由简到繁的学习路径，同时是框架能力的验收矩阵：

| 顺序 | 示例 | 内容 | 状态 |
|---|---|---|---|
| 1 | `basic` | **基础控件**：TextBlock / Button / TextBox / CheckBox / Border，单一数据源 | **已实现** |
| 2 | `list` | **集合 + 布局**：ListBox/DataTemplate、Grid、WrapPanel、VariableSizedWrapGrid、Canvas、Viewbox、Expander、TabView | **已实现** |
| 3 | `editor` | **集成**：菜单栏 + 工具栏 + 多标签 + 未保存提示 + 文件对话框 | **已实现** |
| 4 | `binding` | **绑定对照**：四种模式并排、StringFormat、FallbackValue、LostFocus | **已实现** |
| 5 | `theming` | **主题**：12 个 token 面板、Light/Dark 实时切换、字面量对照 | **已实现** |
| 6 | `usercontrol` | **父子组件 + 两个独立数据源**，验证组件复用 | 规划中，需先补 `DataContext` 可绑定 |
| 7 | `animation` | **隐式过渡 + 三种缓动 + 平滑滚动**，验证帧时钟 | 规划中，需先补帧时钟 |

五个示例分工明确：`basic` 是**入门页**（五个控件、一个数据源，看完就会写第一段 WAML），`list` 是**布局验收现场**（六个面板装进一个真实页面，也因此暴露了 `auto` 列宽、跨行重叠、文字换行三个缺陷），`editor` 是**集成点**（把文件对话框、菜单、工具栏、标签页、模态确认串成一个能真用的窗口），`binding` 是**行为对照**（同一个属性四种绑法并排，差异肉眼可见），`theming` 是**颜色分层**（token 跟主题走、字面量不动）。

> `files` 示例**已并入 `editor`**：它原本的四个区块（打开 / 另存为 / 选文件夹 / 读取预览）里，前三个和第四个都是编辑器的正常功能；单独做一个示例只会变成「按钮 → 弹框 → 显示路径」的纯演示，没有真实用途来暴露接线错误。

所有示例共用 `samples/shared/SampleHost`（Win32 窗口 + D2D + DPI + 输入分发 + 浮层分派），示例本身只提供 WAML、ViewModel 和标题。加一个新示例 = 新建目录 + 三件套 + 在 `build.ps1` 的注册表加一行。详细规划见 `CODE_PLAN.md` §10a。

### 控件覆盖范围

对齐 WinUI-Gallery 的完整清单是 **90 个待实现控件**（已剔除 15 个概念页与 14 个需要 OS 集成的条目）。本项目**不追求一次做完**，按使用频率分三档：

| 档位 | 数量 | 内容 | 何时做 |
|---|---|---|---|
| **P0 必做** | 23（**✅ 全部完成**） | ✅ `Grid` `Canvas` `WrapPanel` `VariableSizedWrapGrid` `Viewbox` `Expander` `RelativePanel` `ToggleButton` `RepeatButton` `HyperlinkButton` `RadioButton` `Slider` `ToggleSwitch` `PasswordBox` `NumberBox` `ProgressBar` `ProgressRing` `InfoBar` `InfoBadge` `Image` `PersonPicture` `Line` `Shape` | 已完成 |
| **已提前完成** | 12 | `TabView`（原 P2）· `MenuBar` `MenuBarItem` `MenuFlyout` `MenuFlyoutItem` `MenuFlyoutSubItem` `MenuFlyoutSeparator` `CommandBar` `AppBarButton` `AppBarSeparator` `ContentDialog`（原 P1，浮层完成后一并落地）· 以及 `OverlayLayer` 这个非控件能力 | 已实现 |
| P1 常用增强 | 16（**3 已完成**） | ✅ `ComboBox` `Flyout` `ToolTip` ／ 待做 `Popup` `ContentDialog`（已提前）`AutoSuggestBox` 等 | 进行中 |
| P2 长尾 | 51 | 虚拟化集合、日期时间、动画过渡、导航容器、媒体等 | 按需，不承诺 |

目前 **45 个控件可用**（原有 9 个 + 6 个布局面板 + 10 个外壳控件 + P0 补齐的 17 个 + P1 弹出的 3 个）。框架改造已全部完成：`Grid` 行列布局、`VariableSizedWrapGrid` 跨行列换行、`TextBlock`/`TextBox` 文字换行、**浮层子系统**（`OverlayLayer`，同时解锁菜单、工具栏溢出与模态）、**单选模型**、**快捷键路由**、**`RelativePanel` 附加属性与按名查兄弟**、**`Image` 的 WIC 解码（可注入）**、**`PaintCtx` 画线/椭圆/位图**、**`RadioButton` 组作用域**、**`HorizontalAlignment`/`VerticalAlignment`**、**`FontFamily` 等宽字体**、**承载任意控件子树的 `Flyout` 浮层**、**`VisualState` 与动画过渡**。

---

## 工具

| 目标 | 用途 |
|---|---|
| `waml_basic.exe` / `waml_list.exe` / `waml_binding.exe` / `waml_theming.exe` / `waml_editor.exe` | 真实 Win32 窗口 |
| `<sample>.exe --selftest` | 无窗口自检，写 `selftest.log`，退出码表示结果 |
| `waml_tests.exe` | 205 个单元/集成用例 |
| `waml_interact.exe <页面.waml>` | 无窗口驱动真实输入（点击、键盘、菜单、对话框） |
| `waml_snapshot.exe` | D2D 渲染 + 确定性校验（两次渲染 SHA 必须相同） |
| `waml_probe.exe` | 资源与交付模式门禁（缺失资源、陈旧 `.res`、权限标记） |
| `waml_bench.exe` | 性能基线 |

所有工具遵循同一个交付开关：**debug 读 `samples/*/*.waml`，release 读 exe 内嵌资源**；显式传路径时以路径为准。

### 编辑器示例怎么玩

`waml_editor.exe` 是唯一「像真实程序」的示例，可以直接上手：

| 操作 | 会发生什么 |
|---|---|
| 点标签 | 切换文档，编辑区换成那篇文本 |
| 点标签上的 `×` | 干净文档**直接关**；改过的文档弹「保存 / 不保存 / 取消」 |
| 在提示里选「取消」 | **标签还在，内容一字未丢** |
| 点 `+` 或 File ▸ New | 新建 `<Untitled N>` 并立刻激活 |
| 在编辑区打字 | 标签出现 `•`（脏标记），状态栏变 `modified`；删掉那个字又变回 `saved` |
| File ▸ Open | 弹真实文件对话框，选中的文本文件成为新标签 |
| File ▸ Save as | 弹真实保存对话框，写入磁盘并把当前内容设为「干净基准」 |
| 缩窄窗口 | 工具栏尾部按钮收进 `⋯`，点 `⋯` 展开成菜单 |
| `Ctrl+N/O/S/W` | 新建 / 打开 / 保存 / 关闭当前标签 |

菜单里的 **Save 在没有文档时是灰的** —— 打开 File 菜单按下「无文档」状态看一眼，就能确认菜单项启用状态是**打开菜单时**读的，不是加载界面时写死的。

### 写法示例：菜单 / 工具栏 / 模态

```xml
<Page Background="AppBackground">
  <MenuBar Height="34" Background="CardBackground">
    <MenuBarItem Title="File">
      <MenuFlyout>
        <MenuFlyoutItem Text="New"  Hint="Ctrl+N" Command="{Binding NewCommand}"/>
        <MenuFlyoutSeparator/>
        <!-- 启用状态在打开菜单时按 CanExecute 求值，无文档即为灰 -->
        <MenuFlyoutItem Text="Save" Hint="Ctrl+S" Command="{Binding SaveCommand}"/>
        <MenuFlyoutSubItem Text="Close">
          <MenuFlyout>
            <MenuFlyoutItem Text="Close tab" Command="{Binding CloseTabCommand}"/>
            <MenuFlyoutItem Text="Reopen last closed" Command="{Binding ReopenCommand}"/>
          </MenuFlyout>
        </MenuFlyoutSubItem>
      </MenuFlyout>
    </MenuBarItem>
  </MenuBar>

  <CommandBar Height="44">
    <AppBarButton Icon="✚" Label="New"  Command="{Binding NewCommand}"/>
    <AppBarButton Icon="▼" Label="Save" Command="{Binding SaveCommand}"/>
  </CommandBar>

  <TabView ItemsSource="{Binding Documents}"
           SelectedIndex="{Binding ActiveIndex, Mode=TwoWay}"
           CloseRequested="{Binding CloseRequested, Mode=TwoWay}"
           AddCommand="{Binding NewCommand}"/>

  <!-- 声明一次，按需显示：确实不进布局流，是挂在根节点上的浮层 -->
  <ContentDialog Title="{Binding PromptTitle}" Message="{Binding PromptMessage}"
                 IsOpen="{Binding PromptOpen, Mode=TwoWay}"
                 Result="{Binding PromptResult, Mode=TwoWay}"
                 PrimaryButtonText="Save" SecondaryButtonText="Don't save"
                 CloseButtonText="Cancel"/>
</Page>
```

三条约束值得记住：

- **`CloseRequested` 必须是 `TwoWay`**。`OneWay` 绑定会忽略目标侧写入，控件发布的关闭请求会凭空消失（`CODE_PLAN.md` §7.16）。
- **浮层不进布局**。`MenuBar`/`CommandBar` 占位，但它们弹出的菜单、工具栏的 `⋯`、`ContentDialog` 都画在根节点上，所以放在 `ScrollViewer` 里也不会被裁掉。
- **对话框的 `Enter` 落在主按钮、`Esc` 只落到「取消」**。丢弃数据的按钮永远不会成为默认按钮。

### 写法示例：样式 / 资源 / 触发器

```xml
<Page Background="AppBackground">
  <!-- 具名字面量：一次声明，任意字面量位置可用 -->
  <ResourceDictionary>
    <Color x:Key="Brand">#0A84FF</Color>
    <Number x:Key="CardRadius">6</Number>
    <Thickness x:Key="CardPad">16,12,16,12</Thickness>
  </ResourceDictionary>

  <!-- 具名样式：可 BasedOn 继承 -->
  <Style x:Key="Card" TargetType="Border">
    <Setter Property="Background" Value="CardBackground"/>
    <Setter Property="CornerRadius" Value="{StaticResource CardRadius}"/>
    <Setter Property="Padding" Value="{StaticResource CardPad}"/>
  </Style>

  <Style x:Key="HighlightCard" TargetType="Border" BasedOn="{StaticResource Card}">
    <Setter Property="Background" Value="{StaticResource Brand}"/>
  </Style>

  <!-- 触发器：条件成立时追加设置，不成立时自动恢复 -->
  <Style x:Key="TintedBox" TargetType="CheckBox">
    <Setter Property="Background" Value="CardBackground"/>
    <Trigger Property="IsChecked" Value="True">
      <Setter Property="Background" Value="ControlFill"/>
    </Trigger>
  </Style>

  <Border Style="{StaticResource Card}" BorderThickness="0,1,0,0"/>  <!-- 本地覆盖样式 -->
  <CheckBox Content="highlight me" Style="{StaticResource TintedBox}"/>
</Page>
```

四条约束值得记住：

- **优先级是「本地 > 样式 > 默认」**，触发器也属于样式，所以内联属性永远赢。写 `FontSize="11"` 的元素不会被它的样式或触发器改掉字号。
- **样式在加载期解析**。`Setter` 写了类型上不存在的属性、`BasedOn` 拼错、`Style` 引用不存在的 key，都是**带行号的加载错误**，不会在运行时静默无效。
- **样式必须在同一个文件里声明**，但可以写在任何位置（写在页面末尾引用在前面也合法）。
- **触发器会恢复**。它记录激活前的值，条件不再成立时写回；同时监视的属性被重置后，元素回到样式原本的样子，而不是永久留着触发值。

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

- **无 `VisualState` / 主题资源字典**：有 `Style`/`Setter`/`Trigger`，但没有多状态切换（`Normal`/`PointerOver`/`Pressed` 一站式的状态机）与过渡动画。触发器可以模拟简单的两态，复杂状态建议等动画落地。
- **样式不能绑定**：`Setter` 只接受字面量（可引用资源）。样式在加载期解析成槽位值，「一个样式对所有使用者共享一份值」在这个前提下才有意义；需要按数据变化的外观用触发器或绑定到元素自身。
- **样式不能跨文件共享**：没有合并字典，`<Style>` 只在声明它的文档内可见。

### 写法示例：等宽字体与弹出层

```xml
<!-- 等宽字体：代码片段靠它对齐列 -->
<TextBlock Text="name  = &quot;WAML&quot; /  value = 42"
           FontFamily="Consolas" FontSize="13"/>

<!-- ComboBox：Items 用 ';' 分隔（逗号会撞上「Paris, France」这类真实数据） -->
<ComboBox Items="C++;C#;Rust;Python;Go"
          SelectedIndex="{Binding LanguageIndex, Mode=TwoWay}"/>

<!-- Flyout：弹层里放真实控件，不是一排菜单项 -->
<Flyout Content="Advanced...">
  <StackPanel Spacing="8">
    <ToggleSwitch Content="Wrap long lines" IsChecked="{Binding WrapLines, Mode=TwoWay}"/>
    <Button Content="Reset" Command="{Binding ResetCommand}"/>
  </StackPanel>
</Flyout>

<!-- ToolTip：悬停显示，点击穿透到下面的控件 -->
<ToolTip Text="这行提示只在悬停时出现。">
  <TextBlock Text="把鼠标放在这句话上。"/>
</ToolTip>
```

四条约束值得记住：

- **`FontFamily` 空字符串 = 系统默认**，所以既有页面一个字都不用改。等宽族名（Consolas、Cascadia、Courier…）在 headless 文本服务里用 0.6 em 而不是 0.55 em 的字宽，这样布局测试能分辨字体，不用 DWrite 也能验证。
- **`ComboBox` 的 `SelectedText` 是从 `SelectedIndex` 派生的**，绑哪个都行，两者不可能互相矛盾。方向键在**收起状态下**直接改选中项（Windows 惯例），不必先展开列表。
- **`Flyout` 的内容会离开控件树**，进入浮层。这不是实现细节走漏：留在树里的弹层会被父布局测量（收起时仍占位），也会被上层的 `ScrollViewer` 裁剪。
- **`ToolTip` 不是浮层**，而且 `HitTest` 恒返回空。它只负责给下面的控件贴标签，一旦拦截点击就会破坏它描述的那个控件。

### 写法示例：视觉状态与动画

```xml
<Style x:Key="Lively" TargetType="Button">
  <Setter Property="FontSize" Value="14"/>

  <!-- 悬停：150ms 内放大到 16 -->
  <VisualState Name="PointerOver" Transition="0.15">
    <Setter Property="FontSize" Value="16"/>
  </VisualState>

  <!-- 按下：80ms 内缩小到 13，比悬停更快，手感才“跟手” -->
  <VisualState Name="Pressed" Transition="0.08">
    <Setter Property="FontSize" Value="13"/>
  </VisualState>

  <!-- 禁用：不过渡，直接生效 -->
  <VisualState Name="Disabled" Transition="0">
    <Setter Property="FontSize" Value="12"/>
  </VisualState>
</Style>

<Button Content="Hover me" Style="{StaticResource Lively}"/>
```

五条约束值得记住：

- **状态由输入层选择，属性做不到**。这就是 `VisualState` 与 `Trigger` 的分工：触发器监听某个属性是否等于某个值（元素自己知道条件成立）；而“指针正按着”不是属性，没有任何绑定能观察到它，所以悬停/按下只能用 `VisualState`。
- **优先级是「本地 > 视觉状态 > 样式 > 默认」**。状态赢过样式，因为它是更具体、更晚的决定（“这个按钮、此时此刻”）；但它输给本地值，理由和触发器一样。
- **离开状态会恢复**。每个状态进入时记下被它改动的槽位的原值，离开时写回。所以 `Normal` 不必把其它状态的每个属性再声明一遍。
- **`Normal` 是隐式的**，永远存在，表示“没有任何状态修饰”。不声明它也行——这正是关键：如果把它当成“未知状态名”而忽略，上一个状态的值就会永久留在控件上。
- **动画时间是注入的**。`AnimationHost::AdvanceAll(dt)` 接收调用方给的秒数：宿主给真实流逝时间，测试给 `1/60` 并断言精确中间值。内部去读系统时钟会让每个动画测试天生不稳定。

- **动画只覆盖视觉状态过渡**：`VisualState` 的 `Transition` 会插值颜色/数值/Thickness，但没有关键帧、没有嵌套时间轴，也不对布局属性做动画（改尺寸的过渡会触发重排，目前只让绘制属性动）。
- **`DataContext` 不可绑定**：`{Binding ..., Source=...}` 在 v0.5 被明确拒绝，无法在 WAML 里给子树换数据源——这意味着**组件复用目前只能靠复制 WAML**。这是实现 `usercontrol` 示例的前置阻塞项。
- **`TextBox` 无 IME / 选区 / 剪贴板**：只有插入、退格、删除、方向键、Home/End、回车。**中文输入不可用** —— 对一个文本编辑器示例来说这是最明显的短板，见 `CODE_PLAN.md` R10。
- **浮层只有一层**：菜单能在自己内部递归画子菜单，但「浮层里再开浮层」（例如对话框里的下拉框）不支持。
- **关窗口不拦截**：关闭整个窗口时不检查未保存文档。单标签的提示流程已完整，批量流程见 `CODE_PLAN.md` §10c.5。
- **不虚拟化**：见性能基线。

这些都在路线图里，见 `CODE_PLAN.md`。

---

## 文档

| 文件 | 内容 |
|---|---|
| `CODE_PLAN.md` | 实施计划 + 技术验证报告：架构决策、迭代路线、风险登记、缺陷复盘 |
| `samples/basic/Basic.waml` | 五个基础控件，单一数据源 |

---

## 许可

MIT