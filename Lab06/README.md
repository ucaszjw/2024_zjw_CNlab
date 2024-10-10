# 绘图脚本使用说明

06-bufferbloat 文件夹中提供 draw_queue_result.py 和 draw_iperf_result.py 脚本，用于绘制实验数据的图像。

在生成qlen所有数据之后，执行 [draw_queue_result.py](06-bufferbloat/draw_queue_result.py) 脚本，可以得到不同maxq下cwnd、qlen、rtt随时间变化的图像。此外，绘制不同队列处理算法下的rtt随时间变化的图像，需根据注释内容进行修改。

在生成iperf所有数据之后，执行 [draw_iperf_result.py](06-bufferbloat/draw_iperf_result.py) 脚本，可以得到不同maxqiperf吞吐率随时间变化的图像。