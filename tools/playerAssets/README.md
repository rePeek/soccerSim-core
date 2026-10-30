# 正式资产与预览工具

# 手工“过人/转身”动作：三个场景

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_skill_gallery.py
```

输出 `build/player-skill-gallery/`，三个 `.blend`，每个 **7 个** 14 骨骼人物，
打开按空格播放：

| 文件 | 内容 |
| --- | --- |
| `skills_turns_180.blend` | 静止/带球/走路/冲刺的 180° 转身与转身加速 |
| `skills_turns_release.blend` | 齐达内凌空、转身传球/射门/高球、胸部停球 |
| `skills_hold_ball.blend` | 45°/90°/135°/180° 护球转身 |

全部是原始手工片段，0.5 倍速、原地、末尾展示复位；不是自动合成动作。
同目录 PNG 是各场景的单帧渲染预览（静态自检，非交付资产）。

**`shot/dribble/090_zidane` 不是地面回旋，而是高球凌空动作。**
触球点在 `z = 0.80 m`，因此峰值抬脚高至 `0.83 m`（约髖高）是原素材设计，
不是骨骼映射或蒙皮造成；想在地面低脚回旋需另做/另选 180° 带球转身素材。

**原动作库没有踩单车、牛尾巴、油炸丸子、插花脚等专门花式动作**；
可直接称为“过人”的素材就是上述转身/护球类。名称含 `180_turn` 的传球、
射门、高球版本很可能共用同一转身身体动作，仅收尾不同。
护球类没有烘焙触球数据，因此场景不画球。

`validation.json` 按场景记录每个动作的来源、帧数、速度参数、转向角度、
触球帧、源关节误差，以及原地根位移和重新打开 .blend 后的运动检查。

---


正式动作、外观和骨架已经分到 `assets/animations/`、`assets/characters/`、
`assets/skeletons/`。完整目录和 GLB 源输入契约见 [assets/README.md](../../assets/README.md)。
`animation_assets.py` 是不依赖 Blender 的正式资产编译器；下面各 Blender 工具
输出的是 `build/` 下的建模/动画预览，不是另一套正式动作来源。

```sh
python3 tools/playerAssets/animation_assets.py bake \
  --manifest assets/animations/manifest.json \
  --output build/asset-cache/animations.simanim
```

正常 CMake 构建会自动执行缓存编译。所有动作预览默认读取正式 GLB catalog；
`simanim.load()` 同时支持 catalog JSON 与生成的 v7 缓存，不能依赖已移除的
`assets/runtime/animations.simanim`。旧 ASE/XML/.anim 工具只用于离线外观参考和历史恢复。

正式外观包（皮肤、球衣、球鞋、发型）由 `export_character_parts.py` 生成，
需要下方 `default_14_smooth.blend` 预览模板：

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_character_parts.py
```

输出直接写入 `assets/characters/{base,kit,shoes,hair}/`，并同步更新
`assets/characters/manifest.json`。脚本会校验每个包使用同一套 14 骨骼、
相同逆绑定矩阵、内嵌 PNG、无动画，并检查来源文件未被修改。
`--output`/`--build` 可用于写另一套目录；不传则更新正式资产。
注意：球衣、球裤、袜子在原模型中同属一个 kit 网格，无法单独拆分。

---

# 给平滑版人物添加头发

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_player_hair.py -- --gallery

# 同一套六动作预览，换成带头发的模型
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_action_gallery.py -- \
  --template build/player-hair/default_14_hair.blend \
  --output build/player-action-gallery-hair
```

依赖下方 `build/player-smooth/default_14_smooth.blend`，原始低面数 14 骨骼
静态模板也可通过 `--template` 使用。默认黑色短发 `short01`。

`build/player-hair/` 内：

- `default_14_hair.blend` / `.glb`：平滑人物 + 头发；GLB 内嵌所有贴图。
- `run_14_hair.blend`：带头发的原地跑，打开按空格播放。
- `hairstyles_preview.blend`：`--gallery` 生成的六种发型头部近景。
- `textures/hair_black.png`：发色贴图副本（实际文件名随发色变化）。
- `validation.json`：来源哈希、网格/骨骼、实际 GLB 头发权重、重导入及
  跑步全部 241 帧的头发蒙皮变换验证。

可选发型：`--style short01|short02|medium01|medium02|long01|long02`。
可选发色：`--color black|brown|blonde|darkblonde|red`。建议不同组合使用不同
`--output`，例如 `--style long01 --color brown --output build/player-hair-long-brown`。

复用仓库原有 ASE 发型与 BMP 贴图，没有下载外部资产。按照历史提交
`361a764` 的 `HumanoidBase::UpdateFullbodyNodes`，头发坐标为头颈关节局部坐标，
整块头发 100% 绑定现有 `neck`；没有增加骨骼，也没有发丝模拟。
默认头发也进行两级细分，98 → 2,352 个三角形；加上平滑人物共 20,280 个
三角形。`--hair-levels 0|1|2` 可调整，0 保留原始低面数发型。

这是传统实体网格头发，不是逐根毛发、发片透明材质或物理长发；不同原始
发型的正面可能很相似，需要转到侧面/背面比较。不会替代面部建模。

---

# 高面数平滑预览（仍为 14 骨骼）

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_smooth_player.py

# 用相同六个动作查看高面数模型
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_action_gallery.py -- \
  --template build/player-smooth/default_14_smooth.blend \
  --output build/player-action-gallery-smooth
```

依赖下方生成的原版 14 骨骼模板。输出 `build/player-smooth/`：

- `default_14_smooth.blend` / `.glb`：静态高面数人物，包含原骨架、蒙皮及贴图。
- `original_vs_smooth_run.blend`：同场景左原版、右平滑版，同相位原地跑。
- `validation.json`：面数、权重、绑定姿态、GLB 实际几何及重导入检查。

默认每个部件做两级 Catmull–Clark 细分，**747 → 17,928 个三角形**。
为避免拆开的衣服/皮肤部件因收缩出现新缝隙，开放边界设为完全折痕，
内部表面进行平滑；细分在蒙皮前应用，插值权重显式限制四影响并归一化。
14 根骨骼及其绑定矩阵不变，兼容现有动作。GLB 包含真实细分网格，
不是依赖 Blender 专有 modifier 才能显示的效果。

这仅是平滑预览，**不是重新雕刻的高清人物**：没有新增五官、手指、衣褶，
没有更高分辨率贴图；开放边界仍保留原轮廓。要获得写实外观，需要重新建模、
重拓扑、UV 和贴图制作，而不是继续增加细分级数。

可用 `--levels 1|2|3` 调整级数，支持 `--template`、`--output`。
所有原始资产保持不变。

---

# 14 骨骼：加速、转向、带球同场景预览

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_action_gallery.py
```

前置：生成 `build/player-rig-comparison/default_14_bones.blend`。
输出：`build/player-action-gallery/actions_14_in_place.blend`，直接打开按空格播放。
共六个 14 骨骼人物，按两行三列排列（下排是转向 90°及两种带球）：

| 上排左 → 右 | 下排左 → 右 |
| --- | --- |
| 静止起步，0 → 3.5 m/s | 90°转向，5 → 3.5 m/s |
| 跑动加速，5 → 7 m/s | 5 m/s 带球触球动作 |
| 45°转向，5 m/s | 7 m/s 带球触球动作 |

全部采用原始运行时动作，移除 XY 根位移、保留高度与转向；没有额外骨骼、
约束或 IK。为了观察细节，实际动作按 **0.5 倍速**播放。每个 1 秒展示段
包含起始停顿、源动作、末尾停顿及姿态复位过渡，重复三段。复位不是实际
游戏动作连接；加速效果应看身体姿态而非人物位移。时间轴标出了复位区间。

带球栏有球体参考，按源触球点锚定的线性路径演示，并非物理仿真、完整球轨迹
或连续带球证明；人物部分仍来自真实球控动作，不是普通跑步加一个球。
两行是便于观看的展示排版，上排人物被整体抬高，并不表示跳跃。

`validation.json` 记录六个动作来源、速度、角度、触球数据、源关节误差、
原地根位移与重复检查，并重新打开实际保存的 `.blend` 验证每个人都在运动。
支持 `--template` 和 `--output`，原始模型和运行时动作库不变。

---

# 同场景 14 / 20 骨骼原地跑对比

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_run_comparison.py
```

前置：生成下方 `default_14_bones.blend` 和 `default_20_bones.blend`。
输出：`build/player-run-comparison/run_14_vs_20_in_place.blend` / `.glb`。
Blender 场景左侧 14 骨骼、右侧 20 骨骼，有文字标签；打开 `.blend` 按空格播放。
两版同模型/材质、同跑步源、同相位、同速度、同 XY 根运动移除方式，
100 FPS、2.4 秒；两套 Blender Action 在同一时间轴播放，GLB 合成一个共享动画。
GLB 不包含文字标签，但保留两个独立人物及骨架。

没有增加骨骼约束或 IK；20 骨骼版仅多出程序补充的关节动作与对应预览权重。
逐帧检查共享 14 骨骼的骨架空间变换一致（容许 `2e-5` 浮点误差），
不对“更自然”作自动验收判断；新增细节的观感及足部接触仍需人工检查。
`validation.json` 记录同步、骨骼、约束数量、导出时间轴及重导入播放检查。

支持 `--templates path/to/static/templates` 和 `--output path/to/output`。

---

# 20 骨骼跑步预览

生成下方 20 骨骼模板后运行：

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/playerAssets/export_run_20.py
```

输出目录：`build/player-run-20/`。

- `run_20_in_place.blend` / `.glb`：原地跑，适合近距离检查人物和新增关节。
- `run_20_forward.blend` / `.glb`：保留前进根运动，速度约 7 m/s。
- 两份 GLB 都包含完整人物网格、20 骨骼、蒙皮、内嵌贴图和一个跑步动作。
- 默认 100 FPS、5 个完整步态周期、2.4 秒；`.blend` 打开后按空格播放。
- GLB 通过 File → Import → glTF 2.0 导入；glTF 本身不声明循环播放。
- 原地版首尾姿态闭合，前进版每周期累计位移，不能直接跳回起始位置循环。

动作来源为运行时 `movement/sprint/000_idlelevel1.anim` 及其镜像半步，
每半步 24 个时间间隔；用 4 tick 过渡平滑轻微肩部接缝。
旧关节世界姿态映射到新骨架，插入锁骨后补偿肩部局部变换，
新增的手腕（约 ±8°）、脚趾（最高 18°）和锁骨（约 ±3°）使用周期函数补动画。
这是离线预览，不是新增关节的动作捕捉数据，也没有足底锁定 IK 或 C++ 接入。

`validation.json` 记录每帧 GLB 蒙皮变换对比、100 Hz 时间轴检查、
原地循环端点检查及 Blender 重导入后的骨骼/动作数量和模型变形测试。
支持 `--template`、`--output`、`--cycles`（1–20）。原始模板及动作库保持不变。

---

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
包含原始生成和镜像动作，来源是 `assets/animations/manifest.json`。
此导出器生成 Blender 显示骨骼基的预览，不应覆盖 `assets/` 的逻辑骨骼基 GLB。
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

可选参数：`--source path/to/manifest.json`（也可读生成缓存）、`--data path/to/data`、
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

独立的离线 Blender 外观工具，不改变模拟逻辑、`data/` 或正式 `assets/`。
输出默认放在 Git 忽略的 `build/player-assets/`；不参与正常 CMake 动作编译。

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
