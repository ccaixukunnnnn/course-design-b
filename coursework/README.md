# 方向 B：StarryOS 功能、性能与安全改进

本目录记录课程设计；根目录为 [Starry-OS/StarryOS](https://github.com/Starry-OS/StarryOS) 源码，基线提交 `2e075accf4fb0aefdd1d252ebd9ccf29727d9923`（2026-03-09），Apache-2.0，见根目录 LICENSE 和 NOTICE。该提交的树没有子模块，尽管上游 README 提及递归克隆。上游已将较新开发移至 tgoskits，此仓库选作可溯源的独立历史基线，不代表最新版。上游工具链固定 nightly-2026-02-25；与讲义所写稳定版存在差异，尚未证明稳定版可构建。

## 状态与复现

多生产者任务、通知往返性能、独立重建和 2/4 核验证的命令与证据见 [扩展验证](EXTENDED_VALIDATION.md)。使用 `bash coursework/scripts/demo.sh task_queue` 演示四生产者任务处理。

2026-11-06 对讲义书内第 141–145 页的核对、epoll 新修复及提分路线见 [要求核对与改进记录](REQUIREMENTS_REVIEW.md)。

主线题目：《StarryOS 事件通知机制的语义修复、性能优化与系统调用安全加固》。已准备 Ubuntu/WSL 环境下，从项目目录执行：

```sh
cd '/mnt/d/OS experiment/course-design-b'
bash coursework/scripts/accept.sh   # 全量离线验收，输出新证据目录
bash coursework/scripts/demo.sh     # 仅重编译演示并启动私有 QEMU
```

演示程序、证据索引、评分对应及五分钟讲稿见 [交付说明](DELIVERY.md)。根文件系统、RISC-V musl GCC、nightly 和缓存须预先存在；这不是一次已验证的全新环境安装。`demo.sh` 使用当前已构建内核，不自动重新构建，内核改动后先运行验收。每次 QEMU 都复制方向 B 的根文件系统并只关闭自己启动的进程。

在上一轮 eventfd 信号量修复的基础上，本轮补充 `getcpu`、优化非阻塞 eventfd 热路径，并使未实现的 seccomp 入口返回失败。三方面方案、基线和限制见 [专题设计](THREE_ASPECTS.md)，实际证据见 [测试报告](TEST_REPORT.md)。课程要求的学生独立决策、真实成员记录、评审、公开仓库提交与演示答辩仍待完成。目录 `.local/` 包含隔离的下载、工具与构建产物，不随源码提交。`rootfs-riscv64.img` 为上游镜像，`make/disk.img` 为其副本，均与方向 A 无关。上游 `make rootfs` 会覆盖 `make/disk.img`；本项目包装脚本在该文件存在时拒绝执行。

2026-11-04 审查发现的五项问题已按 [处理记录](SECURITY_FIXES.md) 修复或以明确错误阻断，并通过回归。当前**没有可用的安全随机数或完整凭据模型**，相关接口会拒绝未支持的操作；原始发现和日志见 [审查记录](review-20261104/README.md)。

已在 Ubuntu 24.04 / WSL 使用：

```sh
cd '/mnt/d/OS experiment/course-design-b'
bash coursework/scripts/test-counter.sh
bash coursework/scripts/test-linux-reference.sh
bash coursework/scripts/build-guest-test.sh
bash coursework/scripts/baseline-build.sh
bash coursework/scripts/qemu-baseline.sh
bash coursework/scripts/modified-build.sh
bash coursework/scripts/qemu-modified.sh
bash coursework/scripts/modified-clippy.sh
bash coursework/scripts/format-all-check.sh
```

本轮三方面验证（先准备上段所述环境和缓存）：

```sh
bash coursework/scripts/build-three-tests.sh
bash coursework/scripts/three-build.sh
bash coursework/scripts/three-verify.sh
bash coursework/scripts/three-eventfd-regression.sh
bash coursework/scripts/three-poll-unit.sh
bash coursework/scripts/three-clippy.sh
python3 coursework/scripts/three-perf-compare.py
```

五项审查问题的修复验证：

```sh
bash coursework/scripts/test-mincore-range.sh
bash coursework/scripts/test-random-stream.sh
bash coursework/scripts/security-fixes-build.sh
bash coursework/scripts/security-fixes-qemu.sh
bash coursework/scripts/security-fixes-three-regression.sh
bash coursework/scripts/security-fixes-clippy.sh
```

性能对照需要 `.local/pre-three/kernel.bin`。若没有保留该文件，可执行 `prepare-perf-baseline.sh` 从固定上游提交和 `coursework/baseline-eventfd/` 源码快照创建独立基线，再执行 `build-perf-baseline.sh`。脚本不覆盖已有测量内核；新构建内核可能因构建路径等因素与历史内核哈希不同。性能脚本按六次独立 QEMU 启动交替运行并生成带时间戳的日志和 JSON，不修改上一次日志。

这些命令要求本次准备的 `.local/` 工具、根文件系统和无空格临时缓存；重启 WSL 后临时缓存可能消失，应重跑 `bash coursework/scripts/create-temporary-cache.sh`，再运行 `baseline-build.sh` 创建路径链接。新环境需按上游 README 安装对应依赖，或参考 `install-nightly.sh`、`local-system-deps.sh`、`local-musl.sh`、`fetch.sh` 和 `install-build-tools.sh` 进行隔离准备；这些脚本针对本机 WSL 路径，迁移时须调整。首次安装 nightly 可能在下载完成后因隔离 CARGO_HOME 缺少 rustup 代理返回非零，本次实际如此，必须逐项核对组件，不能忽略失败。首次执行 `bash coursework/scripts/prepare-baseline.sh` 创建上游源码快照；已有快照时脚本拒绝覆盖。只有在 `make/disk.img` 不存在时才执行 `bash coursework/scripts/prepare-rootfs.sh`。临时目录位于 WSL `/tmp`，可在确认不用后由用户清理。

`test-linux-reference.sh` 仅在 WSL Linux 主机运行对照；QEMU 脚本每次复制根文件系统并只终止其自己启动的实例，不连接或使用方向 A 镜像。交叉工具链和上游缓存位于受忽略的 `.local/`，不可直接当作源码交付；正式重建需要下载依赖。上游构建入口是 `make ARCH=riscv64 build`，但本机路径含空格，依赖脚本不能正确处理其实际路径，故使用本目录的无空格临时缓存包装脚本。

设计见 [DESIGN.md](DESIGN.md)，AI 协作见 [AI_COLLABORATION.md](AI_COLLABORATION.md)，报告及答辩准备见 [REPORT.md](REPORT.md)。提交前应核对讲义要求、许可证、成员贡献、真实评审与指定公开仓库；不得将未执行的工作记为完成。
