# 2026-11-04 审查记录

审查对象为方向 B 的 RISC-V 构建，内核 SHA-256 `b70765d82f72845a0ca4fad1a7e5807693aa8c82c58a2e1e90b312af32806976`。以下问题在所选上游基线中已存在，eventfd 唤醒问题在本项目修改后的相关路径中仍存在。本次只添加审查材料，未修改内核修复这些问题。

**后续状态：**五项问题已按 [处理记录](../SECURITY_FIXES.md) 修正或以明确错误阻断，并通过修复后回归。随机数功能和完整凭据模型尚未实现；下表与日志保留的是修复前事实。

| 优先级 | 发现 | 本次证据与影响 |
| --- | --- | --- |
| P1 | `/dev/random` 和 `/dev/urandom` 固定种子，且使用非密码学 `SmallRng` | [第一次](evidence/random-1.log)、[第二次](evidence/random-2.log) 独立 QEMU 启动读取到相同前 32 字节；两个设备在该实验中也相同。`getrandom` 使用这两个设备，不能提供可用于密钥的随机性。 |
| P1 | `getrandom` 直接按用户指定长度分配 `Vec` | 客体传入 `SIZE_MAX` 触发 `capacity overflow` 内核 panic，见 [日志](evidence/getrandom.log)。这是一次性 QEMU 的故障注入，不是成功测试。 |
| P1 | `mincore` 持锁写回可触发同一地址空间锁重入 | 结果缓冲区是尚未触碰的匿名映射时，客体因重复获取同一把锁 panic，见 [QEMU 日志](evidence/mincore-guest.log)；[Linux 对照](evidence/mincore-host.log) 返回成功。 |
| P1 | `setuid` 等凭据接口返回成功但无状态改变 | 客体 `setuid(1234)` 返回 0，之后实际 UID 和 EUID 仍为 0，见 [日志](evidence/probe.log)。`setgid`、`setresuid`、`capset` 等相关占位接口也需整体审查。 |
| P2 | eventfd 在错误写回后未唤醒等待写入者 | 对满计数器执行无效指针读取会消费计数、返回 EFAULT；客体中的阻塞写入者仍未唤醒，后续一次成功读才唤醒，见 [QEMU 日志](evidence/wake-guest.log)。[Linux 对照](evidence/wake-host.log) 中写入者立即恢复。计数被消费本身与 Linux 实测一致，不应简单地将其判作错误。 |

建议先消除可预测随机数和两个可触发 panic 的路径，再处理虚假权限切换、eventfd 唤醒以及测试覆盖。随机数应使用可信熵源初始化的密码学安全生成器；若无法安全提供，应明确失败而非伪装为安全随机数。内存限制、锁重入和凭据修改均需分别补充错误路径与并发测试，不能仅靠修补返回码宣称实现完整隔离。

## 复现

本目录的 C 文件即本次测试输入。需要方向 B 现有的独立 RISC-V musl 工具链、内核、根文件系统、Python 3、debugfs 和 QEMU。以下命令每次复制方向 B 的原始根文件系统并启动自己的 QEMU；`getrandom` 和 `mincore` 在审查时的旧内核上会**有意触发客体内核 panic**，修复后应不再出现该故障；不应在真实系统或方向 A 的 QEMU 中执行。

```bash
cd '/mnt/d/OS experiment/course-design-b'
bash coursework/review-20261104/reproduce.sh random       # 旧内核输出固定字节；修复版拒绝读取
bash coursework/review-20261104/reproduce.sh credentials
bash coursework/review-20261104/reproduce.sh wake
bash coursework/review-20261104/reproduce.sh getrandom    # 旧内核会 panic
bash coursework/review-20261104/reproduce.sh mincore      # 旧内核会 panic
```

测试日志记录的是本次指定内核，而不是对之后修改版本的保证。Linux 对照只在独立主机测试进程中执行；测试中的 UID 变化及 QEMU panic 发生在隔离客体，不涉及宿主权限或方向 A 镜像。日志含本机路径，公开交付前需检查。
