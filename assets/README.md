# 资产目录与权威来源

```text
assets/
├── animations/
│   ├── manifest.json       1452 个动作的稳定 ID、足球元数据、触球信息
│   └── *.glb               100 Hz 骨骼动画及共享参考骨架；没有人物外观
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

CMake 的 `football_animation_assets` 目标用 Python 3.10+ 标准库生成：

```text
assets/animations/*.glb + manifest.json + skeletons/player.json
                  ↓ animation_assets.py bake
<cmake-build-dir>/assets/runtime/animations.simanim
                  ↓ AnimationLibrary::Load
             C++ 动作选择 / 骨骼计算
                  ↓
             Godot 外观蒙皮 / 渲染
```

普通构建不需要 Blender，不调用旧 `.anim` baker，也不读取 `data/media/animations`。
CMake 跟踪全部 GLB、元数据、骨架与编译器的依赖；修改来源后重新构建就会更新缓存。
运行时及资产相关测试由 CMake 指向各自构建目录中的缓存，不能再指向源码树旧路径。
当前资源路径仍由 CMake 编译进运行时。部署到别处时需要一起提供缓存并解决
该路径的定位（例如保留对应路径）；本次没有新增可重定位的资源加载接口。

手工生成缓存：

```sh
python3 tools/playerAssets/animation_assets.py bake \
  --manifest assets/animations/manifest.json \
  --output build/asset-cache/animations.simanim
```

缓存采用原 v7 字节布局以保持现有 C++ 算法不变，不是另一份手工维护的动作来源。
迁移时逐字节验证全部 1452 个 GLB 重建的缓存与旧库一致，包括 50,546 帧、
所有关节旋转、根运动、选择参数与触球数据。输入错误会失败，不回退到旧资产；
编译使用临时文件和原子替换，失败不覆盖上一次成功缓存。

## 足球元数据

manifest 中 `id` 连续且按现有运行时顺序排列，不得随意重排。`name` 保留迁移前
标识（部分含历史机器路径），它是字符串标识而非文件查找路径；实际文件由 `file`
给出。选择参数、进出速度/方向、脚别、保留状态、触球帧与触球点都在 manifest。
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
动作权威来源。`tools/animBaker` 是显式选择才构建的历史恢复工具。
`animation_assets.py migrate-legacy` 是一次性迁移/恢复命令，不参与正常构建。

`tools/playerAssets/` 中预览工具默认读取新 GLB catalog，保留读取生成缓存的能力。
它们的 `.blend`、实验骨架和截图仍输出到 Git 忽略的 `build/`。重新制作外观后，
可以用 `animation_assets.py character --source ... --output ... --skeleton ...`
把静态 Blender 导出包的显示骨骼基转换为共享逻辑骨骼基，再发布到 characters。
角色发布工具适用于现有身份变换、同层级的 14 骨骼模板，不是通用骨架重定向器。

## 验证

```sh
python3 -m unittest discover -s tools/playerAssets -v
nix develop --command bash -c 'cmake --preset debug && cmake --build --preset debug -j 4 && ctest --test-dir build/debug -L animation --output-on-failure'
```

测试覆盖完整 catalog/cache 往返、实际 GLB 编辑改变缓存、缺失元数据、ID/path、
坏时间轴、错误参考基、非有限数、越界 accessor、失败缓存保留、人物共享骨架，
以及既有动画选择、姿态、触球与 locomotion 单元测试。不需要跑整场比赛。
