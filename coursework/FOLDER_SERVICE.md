# 目录事件通知与文件处理服务

StarryOS 的 `inotify` 目录监视器把文件名变化送入事件队列，用户态服务用一个 `epoll` 同时等待投递事件和两个工作进程的结果。文件经 `stage/` 写完后原子移动到 `inbox/`，处理结果写完后原子发布到 `outbox/`。下游可以用同名结果确认作业完成。

## 答辩演示

从已构建当前内核的 Ubuntu/WSL 项目目录运行演示。

```sh
bash coursework/scripts/demo.sh folder_service
```

| 阶段 | 屏幕输出 | 可讲解的机制 |
| --- | --- | --- |
| 正常投递 | `COMMIT sample.job attempt=1` | `rename` 产生目录事件，`epoll` 唤醒服务，工作进程读取完整文件 |
| 进程退出 | `RESTART`、`REQUEUE crash.job attempt=2`、两个 `COMMIT` | 回收退出的进程，替换进程并重派未提交作业，另一作业也完成 |
| 队列溢出 | `OVERFLOW`、`RECONCILED submitted=163 verified=163` | 内核报告事件丢失，服务重扫输入目录，用结果目录和在途作业表去重 |

程序逐个核对 163 份结果的字节数、摘要和错误码，全部正确后打印 `VERIFY: PASS`。第三阶段在读取事件前批量投递 160 个文件，触发真实的内核队列溢出。Linux 宿主的队列容量通常更大，对照测试会先投递交替命名的无关事件以触发同一分支。

也可以在投屏时请老师指定文件名和内容。用下列命令启动私有 QEMU，等出现 `starry:~#`。

```sh
bash coursework/scripts/demo.sh folder_service --interactive
```

随后在 StarryOS 控制台执行第一行，等出现 `WATCH_READY`，再投递和查看结果。每次投递使用新的 `.job` 文件名。

```sh
/three-test watch /root/folder-live &
/three-test put /root/folder-live answer.job 'hello StarryOS'
cat /root/folder-live/outbox/answer.job
```

投递后等待 `COMMIT answer.job` 再执行 `cat`。按 `Ctrl+A`，松开后按 `X` 退出该 QEMU。演示用独立的根文件系统副本。

演示使用根文件系统上的 `/root`。当前 StarryOS 的 `/tmp` 使用内存文件系统，重命名后可能丢失文件缓存中的内容。

## 实现范围

- `inotify_init1` 支持非阻塞及执行新程序时自动关闭描述符（`IN_NONBLOCK`、`IN_CLOEXEC`）。`inotify_add_watch`、`inotify_rm_watch` 支持按目录注册和移除。内核挂载实例与 inode 共同标识目录，监视会跟随同一挂载内的目录重命名。
- 内核在创建、删除和重命名相关系统调用成功后产生事件。`IN_MOVED_FROM` 和 `IN_MOVED_TO` 共用一个 cookie，用户态可把两条记录关联起来。一个实例最多保留 128 个监视和 128 条普通待处理事件。溢出时保留一个 `IN_Q_OVERFLOW` 标记。`read` 读取完整的可变长记录，可用 `epoll` 等待。
- 当前监视范围是单个目录及其直接子项的名字变化。完整文件读写事件、递归监视和跨挂载统一订阅属于后续扩展。
- 服务每个工作目录由单个主管进程管理，任务名唯一，投递后输入文件保持不变。服务最多保留 32 个待派作业和两个进行中的作业。验证覆盖工作进程退出和通知丢失后的恢复，处理结果使用 FNV-1a 摘要校验内容。

`coursework/tests/inotify_regression.c` 验证系统调用、记录边界、`epoll`、移动 cookie 和移除监视。`coursework/tests/folder_service.c` 验证从投递到结果发布的完整流程。两项均已纳入 `coursework/scripts/accept.sh` 和课程回归工作流。
