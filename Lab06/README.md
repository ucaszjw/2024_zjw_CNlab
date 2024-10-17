# 绘图脚本使用说明

06-bufferbloat 文件夹中提供 [`draw_queue_result.py`](06-bufferbloat/draw_queue_result.py) 和 [`draw_iperf_result.py`](06-bufferbloat/draw_iperf_result.py) 脚本，用于绘制实验数据的图像。

## [`draw_queue_result.py`](06-bufferbloat/draw_queue_result.py) 脚本

该脚本读取 `qlen-{maxq}` 目录中的数据文件，并绘制 `cwnd`、`qlen` 和 `rtt` 随时间变化的图像。

### 使用方法

1. 确保在所有 `qlen-{maxq}` 目录中存在以下文件：
   - `cwnd.txt`：包含拥塞窗口数据
   - `qlen.txt`：包含队列长度数据
   - `rtt.txt`：包含往返时间数据

2. 运行脚本：

   ```sh
   python3 draw_queue_result.py
   ```

3. 脚本将生成以下图像文件：
   - `cwnd_plot.png`：拥塞窗口随时间变化的图像
   - `qlen_plot.png`：队列长度随时间变化的图像
   - `rtt_plot.png`：往返时间随时间变化的图像

### 数据格式（必须检查！如果数据格式不同，会导致图片无法正确生成）

- `cwnd.txt` 文件格式：

  ```plaintext
  时间戳, <任意字符> ,cwnd:值, <任意字符>
  ```

- `qlen.txt` 文件格式：

  ```plaintext
  时间戳, 队列长度
  ```

- `rtt.txt` 文件格式：

  ```plaintext
  时间戳, <任意字符>, time=值 ms
  ```

## [`draw_iperf_result.py`](06-bufferbloat/draw_iperf_result.py) 脚本

该脚本读取所有 `iperf_result_{tc_way}.txt` 文件的数据，并绘制带宽随时间变化的折线图。

### 使用方法

1. 确保在当前目录中存在以下文件（使用`mitigate_bufferbloat.py`生成的文件名均为`iperf_result.txt`，每次生成之后需手动改名：
   - `iperf_result_20.txt`
   - `iperf_result_40.txt`
   - `iperf_result_60.txt`
   - `iperf_result_80.txt`
   - `iperf_result_100.txt`

2. 运行脚本：

   ```sh
   python3 draw_iperf_result.py
   ```

3. 脚本将生成以下图像文件：
   - `bandwidth_over_time.png`：带宽随时间变化的图像

### 数据格式（必须检查！如果数据格式不同，会导致图片无法正确生成）

- `iperf_result_{tc_way}.txt` 文件格式（注意！时间戳中间不能有空格）：

  ```plaintext
  [  1] 时间戳 sec <任意字符> 带宽 Mbits/sec
  ```

确保数据文件格式正确，并且文件路径与脚本中的路径匹配。运行脚本后，将在当前目录生成相应的图像文件。
