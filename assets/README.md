# 资产目录与权威来源

```text
assets/
├── animations/
│   ├── manifest.json       1452 个逻辑动作的稳定 ID
│   └── *.glb               283 个 authored 源动作
├── motion_templates/
│   ├── manifest.json       10 个 canonical 运动模板
│   ├── generation_keys.json 非线性过渡生成所用的控制采样帧号
│   └── *.glb               14 骨骼模板，C++ 直接读取
├── characters/
│   ├── manifest.json       可替换外观包及各插槽 (skin/kit/shoes/hair)
│   ├── default/body.glb    完整默认外观（平滑人物 + 黑色短发）
│   ├── default/body_smooth.glb / body_low.glb  无头发平滑版 / 原版
│   ├── base/base.glb       头、双臂、双膝（皮肤）
│   ├── kit/{home,away,goalkeeper,referee}.glb  球衣/球裤/袜子
│   ├── shoes/default.glb   球鞋与鞋底
│   ├── hair/{short,medium,long}*.glb  六种发型
│   └── */textures/*.png    贴图副本，可直接替换试色
└── skeletons/player.json   骨骼名称、层级、逻辑参考偏移和运行时数组顺序
```

**GLB + manifest + skeleton 是正式资产。源码树不再保存 `.simanim`。**
人物 GLB 内含网格、UV、材质、PNG、蒙皮权重、逆绑定矩阵和参考骨架，不含动作。
动作包各带同名参考骨架；glTF 不能跨文件引用骨架。组装时保留一套 Skeleton，
按名称映射动作。动作包中的透明三角形 `__rig_binding__` 仅用于导入器识别 skin，
不是人物外观，应在组装时丢弃。20 骨骼实验版不在正式目录中。

## 可替换外观插槽

`characters/manifest.json` 把外观拆成四个插槽；每包都带完整 14 骨骼骨架和
相同逆绑定矩阵，因此可以只保留一套 Skeleton、按骨骼名称映射后叠加：

| 插槽 | 包 | 内容 |
| --- | --- | --- |
| skin | `base/base.glb` | 头、双臂、双膝 |
| kit | `kit/{home,away,goalkeeper,referee}.glb` | 躯干/球衣、球裤、袜子 |
| shoes | `shoes/default.glb` | 左右球鞋与鞋底 |
| hair | `hair/<style>.glb` | 六种发型，100% 绑定 `neck` |

`default/body.glb` 是现成的完整默认外观，不需要装配；也保留无头发平滑版
与原版低面数版。每个包内嵌 PNG；旁边的 `textures/*.png` 是便于修改的副本，
**改 PNG 不会自动更新 GLB**，需要重新导出。球衣贴图可选 home/away/
goalkeeper/referee；发色可选黑/棕/金/深金/红。

限制：球衣、球裤、躯干和袜子共用一个 kit 网格/材质，没有能单独替换的
上衣或短裤；头发是绑定到 `neck` 的实体网格，没有发丝模拟；没有五官、
手指、脚趾或法线贴图。不提供 20 骨骼实验版。


## 构建与 C++ 运行时

C++ 通过 CPM 引入的 cgltf 直接解析规范化 GLB：

```text
assets/animations/*.glb + manifest.json + skeletons/player.json
assets/motion_templates/*.glb + manifest.json + generation_keys.json
                  ↓ AnimationLibrary::Load
             内存中的完整 AnimationClip 动作库
                  ↓
             C++ 动作选择 / 骨骼计算
                  ↓
             Godot 外观蒙皮 / 渲染
```

普通构建和 C++ 运行时不需要 Python、Blender、旧 `.anim`、`player.object`
或已移除的 baker；也不生成或读取 `animations.simanim` / `autogen.simanim`。
283 个 authored 动作直接导入；manifest 只保留这些 file-backed 条目。
所有资源在初始化时准备，多个 Player 共享动作库，不在每帧或每个球员上重新导入。

`generation_keys.json` 保存各骨骼的控制采样帧号，而不是另一份姿态数据。
生成器按这些帧从 GLB 取值，进行非线性混合、转向、倾斜和根轨迹生成，
以保留原生成器的采样节奏。修改模板时应同步检查控制帧、动作选择和触球元数据。

运行时资源路径仍由 CMake 编译为 `assets/animations/manifest.json` 的绝对路径；
部署时需要保留资源路径或由调用者向 `Load` 提供正确路径，并提供完整资产树。
编辑 GLB 后下次资源加载即生效，无需重建缓存；Simulation 在进程内共享动作库，
不会对运行中的匹配自动热重载。失败加载不发布半成品，保留先前成功加载的库。

支撑脚轨道继续由 `DeriveFootSupport` 从姿态派生；本次没有新增 GLB FootIK extras。
`DeriveMotionQuality` 在加载后测量根轨迹与支撑脚的滑动、循环缝和方向一致性。
对“半步 + 其镜像”的直线 movement 动作，导入器会尝试合成完整循环：用支撑脚
FK 反推根轨迹去滑步，再按支撑脚位移重采样为恒定速度的循环；只有修复后的动作
自己通过严格 IK-free 门禁时才会替换内存中的动作，否则保留原 GLB 轨道并回退。
该过程不写回 GLB、不生成第二套磁盘格式，也不在运行时做 Foot IK。
C++ 已移除 `.simanim` 读写、内容哈希及资源版本校验；动画直接使用当前 GLB。
目前不提供磁盘录像归档功能，不包含录像格式、版本兼容、CRC 或后台写盘线程。
模拟侧的内存快照和快照历史仍保留，不会写出录像文件。

## 足球元数据

manifest 中 `id` 连续且按现有运行时顺序排列，不得随意重排。`name` 保留迁移前
标识（部分含历史机器路径），它是字符串标识而非文件查找路径。当前 manifest 只保留 authored 条目：

- authored：`file` 指向 283 个 GLB；

选择参数、进出速度/方向、脚别、保留状态、触球帧与触球点都在 manifest。
这些字段仍是足球语义的权威来源；姿态直接来自对应 GLB。
这些值使用 **C++ Z-up Stadium Frame / 米 / 弧度 / 100 Hz 帧号**，没有随 GLB
转换为 Y-up。不要仅改动画而忘记同步受影响的动作选择/触球数据。

## GLB 源动画约定（v1）

这不是任意 glTF 文件的通用导入器，而是有意严格的、可验证的资产输入契约：

- 标准 GLB 2.0，内嵌单个 BIN，无压缩、外部 buffer 或 sparse accessor。
- `extras.rig_version` 为 `football_player_v1`。
- Y-up、米；骨骼节点名、父子关系和参考偏移与共享 skeleton 一致。
- **逻辑关节参考旋转为单位旋转**，骨架对象变换为单位变换；不同于 Blender
  为显示骨骼尾端而生成的局部坐标轴。人物 GLB 与动作 GLB 都已统一到此逻辑基。
- 每个文件一个动作，名字匹配 manifest；从 0 秒开始、100 Hz、逐帧 LINEAR。
- 14 条 float32 quaternion rotation 轨道 + `player` 的 XYZ translation 轨道。
- 不能有非根平移、骨骼缩放、缺失/优化掉的常量轨道或非单位参考旋转。
- 根运动保留 XYZ；模拟平面根轨迹从它的 XY 派生，Z 为零。
- 换轴只做分量置换/取反，不归一化、平滑或重采样源关键帧，避免改变物理输入。

可以直接在 Blender/Godot 中导入查看。编辑后输出必须满足上述契约；任意
Blender 默认导出（显示骨骼基、轨道优化等）**不保证可直接作为源文件编译**，
编译器会拒绝不匹配数据，而不是猜测坐标或静默丢轨道。`export_animations.py`
仍是 Blender 预览导出器，其输出不应覆盖正式 canonical GLB。

## 旧资料与离线外观工具

`data/` 中旧 `.anim`、ASE、XML、BMP 保留为来源/建模参考；不再是正常构建时的
动作权威来源。旧 `.anim` baker（原 `tools/animBaker`）已经移除，需要时从 git
历史恢复。`animation_assets.py migrate-legacy` 是一次性迁移/恢复命令，不参与正常构建。
`import_templates.py` 是一次性迁移/恢复工具，把旧模板转换为 canonical GLB、
模板 manifest 及控制采样帧配置；正常构建不调用它。

历史 Python 缓存编译/预览工具仍可读取已有缓存或 GLB，但它们不再是运行时权威，
也不能在没有历史生成产物的前提下从零重建完整 procedural catalog。
它们的 `.blend`、实验骨架和截图仍输出到 Git 忽略的 `build/`。重新制作外观后，
可以用 `animation_assets.py character --source ... --output ... --skeleton ...`
把静态 Blender 导出包的显示骨骼基转换为共享逻辑骨骼基，再发布到 characters。
角色发布工具适用于现有身份变换、同层级的 14 骨骼模板，不是通用骨架重定向器。

## 验证

```sh
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j 4 && ctest --test-dir build/debug -L animation --output-on-failure'
```

C++ 测试覆盖完整 catalog 导入、镜像、程序动作参考姿态、源 GLB 编辑即时生效、
坏时间轴/参考基/访问越界/非有限数拒绝，以及失败后旧动作库保留。
同时运行既有动作选择、姿态、触球与 locomotion 测试；不需要跑整场比赛。
