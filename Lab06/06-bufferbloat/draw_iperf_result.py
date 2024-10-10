# Description: Read data from all iperf_result_{tc_way}.txt files and draw a line chart to show the bandwidth over time. Already matched with content in all the files above.

import matplotlib.pyplot as plt

# 定义文件名和标签
files = [
    ("iperf_result_20.txt", "maxq 20"),
    ("iperf_result_40.txt", "maxq 40"),
    ("iperf_result_60.txt", "maxq 60"),
    ("iperf_result_80.txt", "maxq 80"),
    ("iperf_result_100.txt", "maxq 100")
]

# 初始化数据存储
data = {}

# 读取数据
for file, label in files:
    with open(file, 'r') as f:
        lines = f.readlines()
        intervals = []
        bandwidths = []
        for line in lines:
            if line.startswith("[  1]"):
                parts = line.split()
                if len(parts) >= 7 and parts[6].replace('.', '', 1).isdigit():
                    interval = parts[2]
                    bandwidth = float(parts[6])
                    intervals.append(interval)
                    bandwidths.append(bandwidth)
        data[label] = (intervals, bandwidths)

# 作图
plt.figure(figsize=(10, 6))

for label, (intervals, bandwidths) in data.items():
    plt.plot(intervals, bandwidths, label=label)

plt.xlabel('Time Interval (sec)')
plt.ylabel('Bandwidth (Mbits/sec)')
plt.title('Bandwidth Over Time')
plt.legend()
plt.grid(True)

# 设置横坐标间隔为10
plt.xticks(range(0, len(intervals), 20))

# 保存图表到当前文件夹
plt.savefig('bandwidth_over_time.png')

# 显示图表
plt.show()