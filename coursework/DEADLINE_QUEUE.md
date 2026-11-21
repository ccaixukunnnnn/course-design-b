# 使用 timerfd 的任务超时重派

本项目补上了 StarryOS 的单调时钟一次性 `timerfd`，并用一个双工作进程程序展示任务超时后的重派。调度者用同一个 `epoll` 监听 `timerfd` 的截止通知和 `eventfd` 的完成通知，任务与结果通过管道传送。

在已准备好的 WSL 仓库里先构建并验收，再运行演示。

```bash
bash coursework/scripts/accept.sh
bash coursework/scripts/demo.sh deadline_queue
```

超时场景中，任务 42 的第一次尝试停在测试用管道上。另一进程可以继续处理任务 43 和 44。定时器到期后，调度者重派任务 42，并递增该任务的尝试编号。第一次尝试此后交回的结果会被丢弃，只有第二次尝试可以提交。执行顺序可以变化，最终应看到 `TIMEOUT_RECOVERY committed=3 requeued=1 stale=1 PASS`。

第二个场景先确保完成通知和截止通知都可读，再用一次 `epoll_wait` 取出两者。调度规则是同一批就绪事件内先提交有效结果，再处理尚未完成的任务的截止通知。这一场景应看到 `COMPLETION_AND_TIMEOUT committed=1 requeued=0 stale=0 PASS`。

```mermaid
sequenceDiagram
    participant S as 调度进程
    participant W1 as 工作进程一
    participant W2 as 工作进程二
    participant E as epoll
    S->>W1: 派发任务 42，尝试 1
    S->>W2: 派发任务 43 和 44
    W2->>E: eventfd 完成通知
    E-->>S: timerfd 截止通知
    S->>W2: 重派任务 42，尝试 2
    W1->>E: 尝试 1 的迟到通知
    W2->>E: 尝试 2 的完成通知
    E-->>S: 返回完成通知
    S->>S: 拒绝旧尝试，只提交当前结果
```

## 内核实现范围

`timerfd_create`、`timerfd_settime` 和 `timerfd_gettime` 支持 `CLOCK_MONOTONIC`、相对及绝对截止时间、一次性及周期定时、取消、`TFD_NONBLOCK` 和 `TFD_CLOEXEC`。到期后可用 `poll`、`epoll` 或阻塞读取等待，成功读取返回一个 64 位到期计数。非阻塞读取在没有到期事件时返回 `EAGAIN`。其他时钟和 `TFD_TIMER_CANCEL_ON_SET` 目前不支持。[Linux 接口说明](https://man7.org/linux/man-pages/man2/timerfd_create.2.html)

一个内核任务维护按截止时间排序的队列。重装和关闭会移除旧项，定时任务在唤醒时重新确认该截止时间仍有效。若设置的绝对时间已经到期，系统调用会在返回前通知 `epoll`。首轮客体演示发现，仅等待后台唤醒会使 `poll` 先看到可读状态，但 `epoll` 还收不到该事件。队列用弱引用关联文件描述符，所以定时项不会延长文件的生命周期。定时任务固定在一个 CPU 上，因为现有运行时按 CPU 保存等待计时器。

`timerfd_regression.c` 验证提前重装、绝对截止时间、取消未到期和未读取的事件、阻塞读取以及多个定时描述符。`deadline_queue.c` 验证重派、迟到结果和同批完成与超时。两者都加入 Linux 对照、单核客体验收与多核脚本。演示使用固定任务和最多一次重派，保证的是调度者只接收一个结果。工作进程可能执行多次，因此带外部副作用的任务仍需要幂等处理或单独的提交协议。

## 周期计数与工作进程退出恢复

```bash
bash coursework/scripts/demo.sh timerfd_periodic
bash coursework/scripts/demo.sh worker_recovery
```

周期模式下，首次到期将文件描述符标为可读。若消费者暂停，内核保留最早未读的截止时间，等读取时用经过时间除以间隔，计算累计到期次数。下一期限沿首次设置的节拍推进。已过去的周期无需逐次排队，也不会因为每次读取的延迟而累计漂移。`timerfd_gettime` 可查询间隔和到下次到期的时间，重设或取消会清除旧的未读计数。

`timerfd_periodic` 设置 100 毫秒周期并暂停读取 550 毫秒，随后一次读取应拿到至少 5 次到期。实际计数由采样时刻决定。用例根据读前和读后的单调时钟，校验累计次数和后续通知仍沿初始节拍，并验证重设成一次性及取消未读事件。

`worker_recovery` 给两个工作进程各分配独立命令管道和结果管道。监管者通过 `epoll` 监听结果和 20 毫秒周期 `timerfd`，用 `waitpid(WNOHANG)` 确认并回收已退出的子进程。每个进程最多有一个在途任务。回收后先检查旧管道是否留下完整结果，再创建新工作进程。未提交的任务递增尝试编号并重派。结果已完整送达的任务直接提交，避免重复分配。演示设有重启上限，范围为调度器存活期间的进程级恢复。

| 故障注入时机 | 预期结果 |
| --- | --- |
| 工作进程拿到任务 42 后、写结果前退出 | `EXIT_BEFORE_RESULT committed=3 restarted=1 requeued=1 PASS` |
| 工作进程写完整结果后退出 | `EXIT_AFTER_RESULT committed=2 restarted=1 requeued=0 PASS` |

两个用例均加入 Linux 对照、RISC-V 单核验收和扩展多核验证。
