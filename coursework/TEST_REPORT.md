# 测试记录（进行中）

## 2026-11-06：任务队列、竞争、SMP 与通知 RTT

最新完整验收为 [accept-20261106T100251Z-itzNvn](logs/accept-20261106T100251Z-itzNvn/)，status 为 PASS，所有步骤退出码为 0，含新增 task_queue 和 event_stress。2 核及 4 核独立构建和客体回归通过，三次独立单核启动完成 3072 项通知 RTT 样本。命令、结果、配置错误的处理和验证边界详见 [扩展验证](EXTENDED_VALIDATION.md)。

## 2026-11-06：epoll 就绪队列回归与课程要求核对

最终完整验收目录为 [accept-20261106T090048Z-UbBWyY](logs/accept-20261106T090048Z-UbBWyY/)，脚本退出 0，status 为 PASS，所有步骤退出码均为 0。包含 rustfmt、9 项 Rust 单元测试、RISC-V Clippy、release 构建、四组客体回归，以及输入和源码哈希校验。构建保留 10 条上游 unused 警告，Clippy 沿用 `-D warnings -A unused`。

新增的 [epoll 回归](tests/epoll_regression.c) 在 WSL Linux 上 26 项断言全部通过，见 [Linux 日志](logs/epoll-linux-reference.log)。同一客体程序在修复前的内核出现 7 项断言失败、退出 1，见 [旧内核日志](logs/epoll-before.log) 及 [输入哈希](logs/epoll-before-inputs.sha256)。修复后的 26 项断言全部通过，见 [最终客体日志](logs/accept-20261106T090048Z-UbBWyY/qemu-epoll_regression.log)。覆盖 MOD 更新、LT 去重和轮转、ONESHOT 重装、ET 再次通知和删除。

首次验收 [accept-20261106T085559Z-P08VUo](logs/accept-20261106T085559Z-P08VUo/) 的客体测试通过，但执行期间进一步限制了 epoll 去重集合只保存已返回的项，最终源码哈希检查正确拒绝了这次记录。固定最终源码后重新完整验收通过。该失败目录保留，不能作为最终成功记录。

新增 `.github/workflows/coursework.yml`，在 push、PR 或手动触发时运行固定 nightly 的主机核心测试及三项 Linux 对照。对应测试命令已在本机通过，远端 GitHub Actions 尚待推送触发。性能脚本现在显式指定客体程序，并记录和复核根镜像、启动脚本哈希。本轮仅检查该脚本语法，没有重新执行性能测量。

本轮验证仍限于单核 RISC-V QEMU 和 Linux 主机对照。新 epoll 去重集合带来的分配开销、SMP、同时修改注册项与等待的压力场景、新环境端到端重建，以及稳定版工具链兼容性仍待验证。要求与后续优先级见 [核对记录](REQUIREMENTS_REVIEW.md)。

## 2026-11-04：双进程演示与重新验收

同一静态程序 `coursework/tests/task_notifier.c` 在固定上游内核 SHA-256 `49d80147c4a9c5513ad37f905bbbed969f54762c158e717d365147676adc5061` 运行：[日志](logs/task-notifier-upstream.log) 中 epoll 可读后首次读到 3，断言失败、客体退出 1。当前内核 `61adf64301d99298d22a8e693b9d2f5d384a32266e74ea4ecd96fe134fa73ec2`：[日志](logs/task-notifier-initial.log) 三次读到 1，普通模式读到 3，均耗尽 EAGAIN、子进程退出 0、客体退出 0。两次使用相同程序 SHA-256 `82b89e2a07a92a29d8295c4c68e514cd801db18b21a420aaDE90b65028cc2746` 和根镜像 `636840a4feda55e10f5d1dce394fd733d16ed626b97668239024d9ac2c5aaef6`，相同 QEMU 参数。二进制小写比较时哈希不区分大小写。各次启动使用根镜像副本。旧版在第一批失败，因此未测试其普通模式。

`bash coursework/scripts/accept.sh` 最终退出 0，证据为 [本轮目录](logs/accept-20261104T152916Z-8eDLZu/)，其中 status 为 PASS、steps.tsv 的各步均为 0，包括新安全断言和演示。演示脚本单独执行退出 0。测试针对单核 RISC-V QEMU，不证明所有阻塞竞争或多核时序。该双进程负载没有进行吞吐或延迟计量。历史性能 JSON 记录了两个内核和客体程序哈希，均与本轮保留输入相符；它没有记录根镜像哈希，因此不能仅凭该 JSON 独立验证历史根镜像对应关系。历史性能数字只作为该次无竞争负载的记录，不作为本轮新测量。

2026-11-04 补充审查复现了安全与健壮性问题，见 [审查记录](review-20261104/README.md)。其后已按 [处理记录](SECURITY_FIXES.md) 修复或明确拒绝相关操作；原始失败日志保留。

## 2026-11-04：审查问题处理后的验证

- `bash coursework/scripts/security-fixes-build.sh`：最终 RISC-V release 构建退出 0，[日志](logs/security-fixes-build.log)。最终内核 SHA-256 `61adf64301d99298d22a8e693b9d2f5d384a32266e74ea4ecd96fe134fa73ec2`；修复前审查内核 `b70765d82f72845a0ca4fad1a7e5807693aa8c82c58a2e1e90b312af32806976` 保留在本机 `.local/pre-security-fixes/`。
- `bash coursework/scripts/security-fixes-qemu.sh`：最终内核退出 0，[日志](logs/security-fixes-qemu.log)。客体覆盖 `/dev/random`、`/dev/urandom` 读写明确失败及零长度行为，`getrandom` 未知 flags 和 `SIZE_MAX` 请求，凭据无变化与拒绝变更，`mincore` 惰性输出页、只读页、空指针、溢出、未映射页和跨 256 页边界，以及 eventfd EFAULT 后写入者唤醒。该测试确认拒绝服务，不代表已经提供安全随机数或凭据切换。
- `bash coursework/scripts/test-mincore-range.sh`：2 项主机单元测试通过，[日志](logs/security-fixes-unit.log)；`bash coursework/scripts/test-random-stream.sh`：2 项模拟源单元测试通过，[日志](logs/random-stream-unit.log)，覆盖 256 字节分块、短读、错误和部分成功。模拟字节不用于客体随机数。
- `bash coursework/scripts/security-fixes-three-regression.sh`：此前 `getcpu`、seccomp、eventfd 客体测试通过，[日志](logs/security-fixes-three-regression.log)。eventfd 计数和轮询单元测试分别 3 项、2 项通过。
- `bash coursework/scripts/security-fixes-clippy.sh`：最终检查退出 0，[日志](logs/security-fixes-clippy.log)，仍沿用上游 `-D warnings -A unused`。首次检查因手写取模触发 `manual_is_multiple_of` 退出 1，[原始日志](logs/security-fixes-clippy-initial.log)；修正后重建和重测。全源码隔离 rustfmt 检查及 `git diff --check` 通过，构建仍有 10 条上游未使用代码警告。
- 最终内核性能复测按旧、新、新、旧、旧、新交替启动六次，[原始 JSON](logs/three-perf-20261103T161622Z.json)：无竞争 eventfd 写/读一组的三次启动中位数的中位数由 10.260 微秒降为 5.094 微秒，约 50.35%；仅适用于该 WSL/QEMU 单核场景。

未覆盖：可信熵设备和真实密码学随机数、完整凭据切换、多核、真实硬件、复杂信号及 `mincore` 并发修改映射。拒绝不支持的操作可能使依赖这些能力的应用失败。

## 2026-11-03：三方面改进

- 修改前 QEMU 记录：`getcpu` 返回 `-1/ENOSYS`；`seccomp(SECCOMP_SET_MODE_FILTER, ..., NULL)` 和 `prctl(PR_SET_SECCOMP, ...)` 均返回 0，见 [基线探测](logs/three-baseline-probe.log)。原内核是上一轮 eventfd 修复后的构建，不是上游原始版本。
- 修改后的同一探测中，`getcpu` 返回 0、CPU 和节点均为 0，两个 seccomp 入口均返回 `-1/ENOSYS`，见 [修改后探测](logs/three-modified-probe.log)。
- 修改版 RISC-V release 构建通过，[日志](logs/three-modified-build.log)，保留原有 10 条未使用代码警告。`bash coursework/scripts/three-verify.sh` 的客体程序退出 0，[日志](logs/three-modified-verify.log)：覆盖 `getcpu` 的两个输出、各自为空、均为空和非法地址；两个 seccomp 入口的失败返回；eventfd 非阻塞空/满/非法/短读、信号量语义，以及阻塞模式由子进程写入唤醒。
- 上一轮 eventfd 系统测试在修改版 QEMU 通过，[日志](logs/three-eventfd-regression.log)。`bash coursework/scripts/three-poll-unit.sh` 两项通过，覆盖一次轮询的 Ready 与 Pending 后重试，[日志](logs/three-poll-unit.log)；上一轮计数单元测试 3 项通过。
- `bash coursework/scripts/three-clippy.sh` 退出 0，[日志](logs/three-clippy.log)，使用 `-D warnings -A unused`；直接使用隔离 rustfmt 的全源码检查和 `git diff --check` 通过。
- 非阻塞 eventfd 每组写入与读取，三次独立启动各自的 7 轮中位数：基线为 10.010、9.937、9.455 微秒；修改后为 4.974、4.918、5.199 微秒。每组的三次启动中位数分别为 **9.937 和 4.974 微秒，下降约 49.94%**。运行环境、样本、排序和适用范围见 [专题设计](THREE_ASPECTS.md) 和 [完整 JSON](logs/three-perf-20261103T143548Z.json)。
- 已从上游固定提交和上一轮 eventfd 快照重新生成性能基线并构建，[日志](logs/rebuilt-perf-baseline.log)；重新生成的内核哈希 `b8d8c27ff99746519dfc2c01b1d75360d4d6e8c02d6ef15afed050e1edb12865`，与本次测量所用的旧内核哈希不同。比对本轮涉及的旧版 syscall、seccomp、eventfd 及计数核心的五个源码文件，快照哈希相同；QEMU 对旧 getcpu/seccomp 行为及 eventfd 信号量语义的复核见对应 `rebuilt-perf-baseline-*.log`。这些检查不能证明两个二进制完全一致。

本轮未验证其他 CPU 架构、多核、真实硬件、复杂信号竞态、NUMA、所有用户内存故障或真实安全过滤。固定返回失败是避免错误报告安全措施已启用，不代表系统具备沙箱能力。

## 环境与上游基线

- WSL Ubuntu 24.04；QEMU 8.2.2；已有稳定 Rust 1.99.1 与 `riscv64gc-unknown-none-elf`。隔离安装 nightly-2026-02-25（rustc 1.95.0-nightly），rustup 完成工具链安装后因隔离 CARGO_HOME 尚未安装 rustup 自身返回 1；直接调用其工具链二进制可显示版本。
- 原始离线交叉检查 `bash coursework/scripts/baseline-offline.sh` 退出 1，缺少 `axalloc`，见 [日志](logs/baseline-offline.log)。这是空缓存环境问题，并非源码修改导致。
- 下载锁定依赖后，对 `git archive HEAD` 的原始源码执行 `bash coursework/scripts/baseline-nightly-check.sh`，退出 1，`lwext4_rust` 因 `cmake` 缺失构建失败，见 [日志](logs/baseline-nightly-check.log)。其 shell 命令也未引用含空格路径，不能保证补齐 cmake 后直接成功。
- 在方向 B 补齐依赖并将 Cargo 缓存复制到 WSL 临时无空格目录后，`bash coursework/scripts/baseline-temp-cache.sh` 退出 0，RISC-V `cargo check` 通过；上游 `starry-kernel` 有 10 条未使用代码警告，[日志](logs/baseline-temp-cache.log)。这只是交叉检查，不是已启动的系统。
- `bash coursework/scripts/baseline-build.sh` 完整构建原始 RISC-V 内核成功，退出 0，[日志](logs/baseline-make.log)。官方根文件系统下载和复制成功，`rootfs-riscv64.img` 为 ext4。
- `bash coursework/scripts/qemu-baseline.sh` 退出 0：QEMU 启动原始内核并到达 Shell；测试在初始计数为 3 的信号量读取处得到 3，测试程序按预期退出 1；包装脚本确认这是预期失败后退出 0，[日志](logs/qemu-baseline.log)。QEMU 单核、1 GiB、virt、无网络，使用根文件系统临时副本。

## 修改后的测试

- `bash coursework/scripts/test-counter.sh`：隔离 nightly 主机核心逻辑 3 项通过（普通模式、信号量耗尽、并发消费），[日志](logs/counter-host.log)。最初使用稳定 Rust 1.99 时，对上游沿用的 `fetch_update` 有弃用警告。
- `bash coursework/scripts/test-linux-reference.sh`：WSL Linux 主机上的 `eventfd` 语义对照通过，[日志](logs/eventfd-linux-host.log)；它不运行 StarryOS。
- `bash coursework/scripts/build-guest-test.sh`：静态 RISC-V 测试程序编译成功，随后用于下述 QEMU 对照。
- `bash coursework/scripts/format-check.sh` 与 `bash coursework/scripts/clippy-core.sh` 均退出 0，只覆盖修改相关的格式和计数核心逻辑，不等同于全内核检查。
- 使用独立工具链及符号链接后，`baseline-local-deps.sh` 仍失败：Cargo 将依赖源码解析回带空格的路径，CMake 参数拆分错误，见 [日志](logs/baseline-local-deps.log)。物理临时缓存解决了该次检查的路径问题。
- `bash coursework/scripts/modified-check.sh` 和 `bash coursework/scripts/modified-build.sh` 均退出 0；交叉检查与优化版完整构建分别见 [检查日志](logs/modified-check.log)、[构建日志](logs/modified-make.log)。构建仍报告与原始基线相同的 10 条未使用代码警告。
- `bash coursework/scripts/qemu-modified.sh` 退出 0：客体测试程序退出 0，检查信号量读取三次均为 1、耗尽后 EAGAIN、写入后再次读取及普通模式读取和耗尽，[日志](logs/qemu-modified.log)。这是同一测试程序和根文件系统的独立副本，与原始版比较，未使用方向 A 数据。
- `bash coursework/scripts/modified-clippy.sh` 退出 0：RISC-V `cargo clippy -- -D warnings -A unused`，[日志](logs/modified-clippy.log)，遵循上游 CI 忽略 unused 的范围；不是所有目标无警告。
- `bash coursework/scripts/format-all-check.sh` 对 `src/` 和 `kernel/src/` 的全部 Rust 源文件直接使用隔离 nightly rustfmt 检查，退出 0，[日志](logs/rustfmt-all-direct.log)。首次发现上游 LoongArch 配置两处注释空格与该 rustfmt 不符，已做纯格式修正。此检查并非 `cargo fmt --all`；初次调用后者曾意外触发共享 rustup 安装，处理记录见 AI 协作记录。

原始基线内核 SHA-256 `49d80147c4a9c5513ad37f905bbbed969f54762c158e717d365147676adc5061`；修改版内核 `eb670701ceb4ca8041e01bdd268a702070720b0b401d31b4bae12fd57bcbd30e`；原始根文件系统 `636840a4feda55e10f5d1dce394fd733d16ed626b97668239024d9ac2c5aaef6`；静态测试程序 `63cfeab9564574cfdfec28bd120fa15def400570829d440505250a947e184afe`。哈希只标识本次文件，不构成上游签名验证。

## 覆盖限制

上一阶段未覆盖 eventfd 用户缓冲区故障、阻塞唤醒竞态、设备限制、SMP、poll/epoll 全部时序或长时间压力。上一阶段没有性能测量；本轮的有限场景测量见文首。
