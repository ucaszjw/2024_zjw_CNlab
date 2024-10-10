import matplotlib.pyplot as plt
import re

def read_cwnd_data(file_path):
    times = []
    cwnds = []
    try:
        with open(file_path, 'r') as file:
            for line in file:
                parts = line.split(',')
                times.append(float(parts[0].strip()))
                match = re.search(r'cwnd:(\d+)', line)
                if match:
                    cwnds.append(int(match.group(1)))
        print(f"成功读取数据：{len(times)} 个时间点，{len(cwnds)} 个拥塞窗口值")
    except Exception as e:
        print(f"读取文件时出错：{e}")
    return times, cwnds

def read_data(file_path, pattern):
    times = []
    values = []
    try:
        with open(file_path, 'r') as file:
            for line in file:
                parts = line.split(',')
                times.append(float(parts[0].strip()))
                match = re.search(pattern, line)
                if match:
                    values.append(float(match.group(1)))
        print(f"成功读取数据：{len(times)} 个时间点，{len(values)} 个值")
    except Exception as e:
        print(f"读取文件时出错：{e}")
    return times, values

def adjust_times(times):
    if times:
        initial_time = times[0]
        adjusted_times = [time - initial_time for time in times]
        return adjusted_times
    return times

def plot_data(times, values, title, ylabel, xlabel='Time (s)', output_file=None):
    try:
        plt.figure()
        plt.plot(times, values)
        plt.title(title)
        plt.xlabel(xlabel)
        plt.ylabel(ylabel)
        plt.grid(True)
        print("绘制图表中")
        if output_file:
            print(f"尝试保存图表为 {output_file}")
            plt.savefig(output_file)
            print(f"图表已保存为 {output_file}")
        else:
            print("未提供输出文件名，无法保存图表")
    except Exception as e:
        print(f"绘制图表时出错：{e}")

if __name__ == '__main__':
    # 读取并绘制 cwnd.txt
    cwnd_times, cwnd_values = read_cwnd_data('qlen-100/cwnd.txt')
    if cwnd_times and cwnd_values:
        print("开始调整时间戳...")
        adjusted_times = adjust_times(cwnd_times)
        print("开始绘制图表...")
        plot_data(adjusted_times, cwnd_values, 'Congestion Window Size Over Time', 'CWND (packets)', output_file='cwnd_plot.png')
        print("图表绘制完成。")
    else:
        print("没有读取到有效的数据")

    # 读取并绘制 qlen.txt
    qlen_times, qlen_values = read_data('qlen-100/qlen.txt', r'(\d+)$')
    if qlen_times and qlen_values:
        print("开始调整时间戳...")
        adjusted_qlen_times = adjust_times(qlen_times)
        print("开始绘制队列长度图表...")
        plot_data(adjusted_qlen_times, qlen_values, 'Queue Length Over Time', 'Queue Length (packets)', output_file='qlen_plot.png')
        print("队列长度图表绘制完成。")
    else:
        print("没有读取到有效的队列长度数据")

    # 读取并绘制 rtt.txt
    rtt_times, rtt_values = read_data('qlen-100/rtt.txt', r'time=(\d+\.?\d*) ms')
    if rtt_times and rtt_values:
        print("开始调整时间戳...")
        adjusted_rtt_times = adjust_times(rtt_times)
        print("开始绘制RTT图表...")
        plot_data(adjusted_rtt_times, rtt_values, 'RTT Over Time', 'RTT (ms)', output_file='rtt_plot.png')
        print("RTT图表绘制完成。")
    else:
        print("没有读取到有效的RTT数据")