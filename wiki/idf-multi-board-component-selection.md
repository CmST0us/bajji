# ESP-IDF 多板型构建：组件依赖扫描看不到普通 CMake 变量

## 现象与根因

为 Passport 加入 `-D BAJJI_BOARD=ai_passport` 后，顶层已经选择 C3，组件仍报
`BMI270_BMM150_Sensor: unknown name`。并非没有拉取依赖；StopWatch 的四个外部组件
是按板型故意从 `EXTRA_COMPONENT_DIRS` 排除的。

ESP-IDF 6.0 `tools/cmake/build.cmake` 的依赖展开在单独的 early-expansion 上下文中
读取各组件 CMakeLists。普通项目 cache 变量 `BAJJI_BOARD` 不在这一上下文内；因此
`board_hal/CMakeLists.txt` 中直接按该变量分支时走进 StopWatch 的 REQUIRES。
`IDF_TARGET` 则由构建系统传入这一过程。

## 已验证的改法

顶层仍用 `BAJJI_BOARD` 选择配置、依赖锁与目标芯片；组件根据 `IDF_TARGET` 选择源文件
和 REQUIRES。当前两块板分别唯一对应 C3 和 S3；两者完整构建均已通过。
不要把这一规则扩展成“一个芯片永远只对应一块板”：增加同芯片第二板时，需要显式将板型
作为构建属性传入依赖扫描，或分拆独立组件。

`dependencies.ai_passport.lock` 与 `dependencies.lock` 分开保存，防止交替构建覆盖锁里的
目标芯片。生成 sdkconfig 和构建目录也分别隔离。共享 `managed_components/` 仍要求串行
执行不同板型的依赖解析/构建，尤其当两个配置以后选择不同组件版本时。
