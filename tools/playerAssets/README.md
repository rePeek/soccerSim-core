# 14 / 20 骨骼默认人物对比

```sh
# 如果尚未生成默认人物模板，先执行：
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_player.py

blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_rig_comparison.py
```

输出位于 `build/player-rig-comparison/`：

- `default_14_bones.glb` / `.blend`：原版 14 骨骼人物。
- `default_20_bones.glb` / `.blend`：增加左右手腕、脚趾、锁骨的人物。
- `new_joints_demo_20.blend`：新增关节的独立变形演示，0–60 帧，30 FPS。
- `skeleton_20.json`：预览骨架定义。
- `validation.json`：静态形状、权重、各新增骨骼变形、GLB 字节与重导入验证。

在 Blender 中建议直接打开 `.blend`，已设置骨骼透视和 Pose Mode；
20 骨骼版选中了新增骨骼。选中单根骨骼按 `R` 旋转，`Alt+R` 清除旋转。
打开演示文件后按空格播放，可观察新增关节效果。
GLB 则使用 **File → Import → glTF 2.0** 导入；两份 GLB 均不带动画，贴图内嵌。

这是同一套低多边形网格和材质，静态外观应一致，不是增加了模型细节。
新增骨骼**有实际蒙皮权重**，由肘部、脚踝、肩部原权重按空间区域平滑拆分，
但只是程序生成的预览权重，并非美术精修；没有增加手指或足部拓扑。
插入锁骨时保留原骨骼绑定变换（允许 Blender 浮点往返误差）。

此 20 骨骼版为独立原型，不修改 `data/`、14 骨骼动作库或 C++ 运行时，
也尚未实现旧动作到新骨架的适配，不能直接套用旧动作包。
参数 `--template` 可指定原始 14 骨骼 `.blend`，`--output` 可指定独立输出目录。

---

# 完整运行时动作库 → GLB

```sh
blender --background --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_animations.py
```

默认输出：`build/player-assets/runtime-animations/`，每个运行时动作一个 GLB，
包括 baker 生成和镜像的动作，来源是 `assets/runtime/animations.simanim` v7。
文件名前缀是运行时 clip ID，避免重名；`manifest.json` 保留原动作名、ID、
100 FPS、帧数、动作元数据、触球点、原始 XY 根轨迹及文件 SHA-256。
GLB 保留完整 XYZ 根运动，使用 Y-up / 米；不擅自声明动作循环。

所有动作使用下方默认球员模板相同的 14 骨骼名称、层级、参考姿态。
每包带透明的 `__rig_binding__` 三角形，仅用于骨架识别，不包含人物外观。
外观仍由 `export_player.py` 单独生成；无需重新转换外观就能导出动作。

导出后读取每个实际 GLB 的动画 accessor 和逆绑定矩阵，在**每一个 10 ms 帧、
每一根骨骼**上比较源动作与 GLB 的蒙皮变换，最大矩阵元素误差不得超过
`1e-4`（含 float32 骨骼基变换误差）；同时检查动作名、骨骼集合和时长。验证汇总写入 manifest；
失败则非零退出，不写成功清单。未验证 Godot 装配。

可选参数：`--source path/to/animations.simanim`、`--data path/to/data`、
`--output path/to/output`。`--limit 2` 只用于冒烟测试，清单会标记
`complete: false`。重新导出会覆盖同名生成文件；不会自动清理目录内旧文件，
请以 manifest 为准，或使用新的输出目录。

读取器测试（不需要 Blender 模块，可使用独立 Python）：

```sh
python3 -m unittest discover -s tools/playerAssets -v
# 或 Blender 自带 Python：
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/test_simanim.py
```

这里的“全部”指模拟器实际使用的完整动作集，不包括未被 baker 收录的
编辑器辅助姿态 `.anim.util` 或未经预处理的原始半步模板。

---

# 默认球员模板：第一阶段

独立的离线 Blender 工具，不接入 CMake，不改变模拟逻辑、`data/` 或
`assets/runtime/animations.simanim`。输出默认放在 Git 忽略的 `build/player-assets/`。

## 生成与验证

在仓库根目录运行，使用 Blender 5.2（本机已验证 5.2.2 LTS）：

```sh
# 导入器单元测试；直接使用 Blender 自带 Python，无需额外安装 Python 包
blender --background --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/test_legacy.py

# 默认人物、独立测试动作、贴图及预览截图
blender --background --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_player.py -- \
  --output build/player-assets --render

# 验证导出的 GLB 字节，并重新导入 Blender
blender --background --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/verify_glb.py -- \
  --output build/player-assets
```

不需要截图时省略 `--render`。截图使用 CPU Cycles，不依赖显卡。
两条资产命令都支持 `--data /path/to/data`；禁止把生成目录放到该目录内部。
导出会覆盖生成目录的同名文件，**不要直接在生成文件里保存唯一一份 mod 工作成果**。

有独立 Python 3 时，也可运行：

```sh
python3 -m unittest discover -s tools/playerAssets -v
```

## 生成文件

| 文件 | 用途 |
| --- | --- |
| `player_template.blend` | 首先打开这个文件；默认人物、14 根骨骼、权重、UV、材质；无动作 |
| `default_body.glb` | 默认外观，包含参考骨架和蒙皮；无动作，PNG 贴图内嵌 |
| `walk_preview.blend` | 完整人物播放一个测试动作，时间轴 0–24 帧，100 FPS |
| `animations/test_walk.glb` | 单独的参考骨架与测试动作；不包含人物外观 |
| `textures/*.png` | 球衣、替代球衣、肤色、鞋、鞋底贴图 |
| `skeleton.json` | 预览骨架版本、名称、父子关系、静态偏移和 Blender 绑定矩阵 |
| `skin.json` | 网格节点、材质槽与外部 PNG 的关联 |
| `animations/manifest.json` | 动作名、来源、时间基准、根运动及源元数据 |
| `validation.json` | 数值验证、源文件 SHA-256、GLB 重导入结果 |
| `preview_rest.png` / `preview_walk.png` | 默认球员的静态/动作截图 |
| `preview_alternative_kit.png` | 相同人物、相同姿态，只替换整套球衣贴图的截图 |

版本是 `football_player_v1_preview`，尚不是对外承诺的稳定 mod 接口。

### 在 Blender 中试换球衣

1. 打开 `build/player-assets/player_template.blend`，另存到自己的 mod 工作目录。
2. 选中 `mesh_body`，进入 Shading 工作区。
3. 在材质 `kit_shared` 的 `ReplaceableBaseColor` 图像节点上，打开
   `build/player-assets/textures/alternative_kit.png`。
4. 开启材质预览，观察球衣、球裤和袜子变化；骨架、鞋、肤色不受影响。
5. 球鞋使用 `shoes` / `shoe_soles`；头部为 `face_skin`；裸露手臂、膝部
   为 `body_skin_arm` / `body_skin_knee`。修改同一个肤色图像可保持接缝颜色一致。

`.blend` 内已打包贴图，不依赖原来的 BMP 文件路径。GLB 也内嵌贴图：
**仅编辑旁边的 PNG，不会自动更新 GLB**。需要重新导出，或由后续 Godot 装配器
给材质覆盖外部贴图。`skin.json` 目前只是资产描述，没有现成的游戏加载器。

### 骨架、动作和蒙皮的分离

GLB 不能跨文件引用骨骼。外观和动作文件各带同一份参考骨架，实际装配时应
保留一套骨架，按名称映射。不要在不同文件中各自修改绑定姿态。

动作 GLB 有一个叫 `__rig_binding__` 的透明三角形（3 个顶点），仅用于让
导入器识别 glTF `skin` 为骨架，而不是普通节点；它不是人物部件。
加载动画库时不要把它当外观装配。未在 Godot 中测试装配和播放，报告明确标记
`godot_tested: false`。

## 转换依据与验证范围

绑定逻辑来自本仓库历史提交 `361a764`，无需下载/依赖其他项目：

- `src/gamedefines.cpp:GetVertexColors`：顶点色乘 255 并 half-up 舍入；
  同一世界坐标先出现的颜色优先。
- `src/scene/scene3d/node.cpp:Node::GetNodes`：深度优先遍历，13 个变形关节；
  `player` 是另外保留的根运动骨骼，不属于颜色编号表。
- `src/onthepitch/player/humanoid/humanoidbase.cpp:PrepareFullbodyModel`：
  每个色彩字节 `joint_id * 10 + weight_digit`，最多三个影响；先用
  `base.anim.util` 逆旋转并移动到 `straight.anim.util` 的关节位置，再用
  straight 骨架作为动画绑定姿态。保留原来的预混合逻辑，而非自动权重。
- `src/loaders/aseloader.cpp`：ASE 顶点已是世界坐标；`NODE_TM` 只参与法线
  处理，不能再应用到顶点位置。
- 当前 `tools/animBaker/animation/animation.cpp`：root translation、关节旋转
  替换、位置线性插值/旋转 SLERP、每帧 10 ms。

Blender 骨骼的显示方向与逻辑关节坐标轴不同，导入器显式做基变换。
Blender 内使用 Z-up，glTF 导出器统一转换到 Y-up。

导出时逐帧对比 Blender 蒙皮与独立 stdlib 实现的原版 LBS。
`verify_glb.py` 重新读取实际 GLB accessor、逆绑定矩阵和动画轨道，按骨骼名称把
独立动作包施加到外观包；在全部 25 帧、全部导出顶点上与参考 LBS 对比，
误差阈值 0.00002 米。同时检查两包参考姿态一致、权重归一化、100 Hz 时间基准、
GLB 内嵌 PNG、动作名与 manifest 一致，以及 Blender 重导入的骨架/蒙皮/动作。
这不是 C++ 浮点逐位一致性、完整运行时动作集或 Godot 兼容性的证明。

## 已知限制与下一步

- 原网格有 12 个子网格、453 个原始顶点、747 个三角形，低多边形且面部只有
  原始肤色材质；没有面部骨骼/表情系统。
- **球衣、球裤和袜子现在共用 `kit_shared` 和原始 atlas**。第一阶段只验证
  整套换贴图，尚未划分成可分别换形状的衣服部件；不能把它称为完整模块化人物。
- 头部、鞋、鞋底等已有独立网格，但任意第三方模型的接缝/绑定兼容尚未验证。
- 不导出原 GLSL、法线贴图、头发或旧式镜面材质；当前是基础 PBR 颜色材质。
- 测试动作来自 `templates/walkD000.anim`，是原始半步模板，持续 0.24 秒，
  保留位移且不声明循环。不是 baker 自动生成/镜像/预处理后的运行时完整步态。
- 未实现身高、骨长、体型、IK、动画重定向或模拟端体型同步。
- 不应改变骨架名称/层级/绑定姿态；制作新网格时绑定到这个模板，而不是自动生成新骨架。

下一步先在 Blender 验收模板，然后划分 `shirt` / `shorts` / `socks` 和皮肤
材质区域，制作独立外观部件，并实现 Godot 的共享骨架装配验证。
