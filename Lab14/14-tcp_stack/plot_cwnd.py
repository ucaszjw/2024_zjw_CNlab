import matplotlib.pyplot as plt

# 读取cwnd.txt文件
time = []
cwnd = []

with open('cwnd.txt', 'r') as file:
    for line in file:
        parts = line.split()
        if len(parts) == 3:
            time.append(int(parts[0]))
            cwnd.append(float(parts[1]))

# 绘制图像
plt.figure(figsize=(10, 6))
plt.plot(time, cwnd, label='cwnd', color='b')

# 设置图像标题和标签
plt.title('Congestion Window (cwnd) Over Time')
plt.xlabel('Time (microseconds)')
plt.ylabel('Congestion Window (cwnd)')
plt.legend()
plt.grid(True)

# 保存图像到当前文件夹
plt.savefig('cwnd_plot.png')
