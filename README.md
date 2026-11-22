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

`Page` · `Border` · `StackPanel` · `ScrollViewer` · `TextBlock` · `Button` · `CheckBox` · `TextBox` · `ListBox` · `DataTemplate`

---

## 构建

需要 **Visual Studio 2022**（含 C++ 工作负载）。

```bat
build.bat release        :: 优化构建，WAML 打包进 exe
build.bat debug          :: 调试构建，WAML 运行时读磁盘文件
build.bat all            :: 构建全部目标
build.bat tests          :: 只构建测试
```

产物默认在 `build\release\`。要输出到别处：

```bat
set WUI_OUT=D:\dist
build.bat release
```

> **关于输出目录**：由自动化/沙箱会话创建的可执行文件可能带上一条未解析 SID 的权限项（`S-1-4-*`），会让 SmartScreen 每次启动都弹「Windows 已保护你的电脑」，即使二进制完全正常且与手动构建的逐字节相同。把产物输出到工程外的普通目录可规避。诊断：`icacls 目标.exe | findstr /i "S-1-4"`。

CMake 也可用（`CMakeLists.txt` 已同步），但本仓库以 `build.bat` 为主路径 —— 理由见下方「构建说明」。

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

samples/gallery/        ← 示例应用，自带 VM、WAML、资源
    Gallery.waml
    GalleryVm.h
    App.cpp
    resources/

tests/   bench/   tools/     ← 框架级工具，不依赖示例
```

关键约束：`framework/` 里**没有**任何 `Gallery.waml`、`GalleryVm` 或 `IDR_WAML_*` 的引用。资源 id 属于示例，定义在 `samples/gallery/resources/Resource.h`；框架只提供「按类型名 + id 读取 RCDATA」的通用接口。

新增示例：复制 `samples/gallery/` 改名、改 `Resource.h` 里的 id、在 `build.bat` 加一段目标即可，框架侧零改动。

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

本项目用 `build.bat` 直接驱动 `cl.exe`/`link.exe`/`rc.exe`，而不是 CMake：CMake 与 Ninja 都通过管道捕获子进程 stdout 来解析诊断，在某些受限环境下该管道无法创建，导致 configure 挂起。这是**环境限制**，不是设计结论 —— 正式环境请用 CMake（`CMakeLists.txt` 已保留并同步）。

版本号单一来源：`build.bat` 的 `WUI_VERSION_*` → `resources/Resource.h` → `app.rc` → 回读到窗口标题。

---

## 已知边界

- **无样式系统**：没有 `Style`/`Setter`/`VisualState`，外观逐属性写。
- **无 Grid**：只有线性的 `StackPanel`，复杂版面需嵌套模拟。
- **无动画**：主题切换是瞬时的。
- **`ListBox` 无选中**：只渲染列表，无 `SelectedIndex`。
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
