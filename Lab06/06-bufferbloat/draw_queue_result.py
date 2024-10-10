# Description: Read data from all qlen-{maxq} deirectories and draw the plot of cwnd, qlen and rtt over time. Already matched with content in all the files above.

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

def read_qlen_data(file_path):
    times = []
    qlens = []
    try:
        with open(file_path, 'r') as file:
            for line in file:
                parts = line.split(',')
                times.append(float(parts[0].strip()))
                qlens.append(int(parts[1].strip()))
        print(f"成功读取数据：{len(times)} 个时间点，{len(qlens)} 个队列长度值")
    except Exception as e:
        print(f"读取文件时出错：{e}")
    return times, qlens

def read_rtt_data(file_path):
    times = []
    rtts = []
    try:
        with open(file_path, 'r') as file:
            for line in file:
                parts = line.split(',')
                times.append(float(parts[0].strip()))
                match = re.search(r'time=(\d+\.?\d*) ms', line)
                if match:
                    rtts.append(float(match.group(1)))
        print(f"成功读取数据：{len(times)} 个时间点，{len(rtts)} 个往返时间值")
    except Exception as e:
        print(f"读取文件时出错：{e}")
    return times, rtts

def adjust_times(times):
    if times:
        initial_time = times[0]
        adjusted_times = [time - initial_time for time in times]
        return adjusted_times
    return times

def plot_multiple_data(datasets, title, ylabel, xlabel='Time (s)', output_file=None):
    try:
        plt.figure()
        for times, values, label in datasets:
            plt.plot(times, values, label=label)
        plt.title(title)
        plt.xlabel(xlabel)
        plt.ylabel(ylabel)
        # plt.yscale('log') # 使用对数坐标
        plt.grid(True)
        plt.legend()
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
    maxq_values = [100, 80, 60, 40, 20]  # 不同maxq的文件
    # tc_ways = ['taildrop', 'red', 'codel'] # 不同tc_way的文件

    # 绘制 cwnd 数据
    cwnd_datasets = []
    for maxq in maxq_values:
        file_path = f'qlen-{maxq}/cwnd.txt'
        cwnd_times, cwnd_values = read_cwnd_data(file_path)
        if cwnd_times and cwnd_values:
            print(f"开始调整时间戳... (maxq: {maxq})")
            adjusted_times = adjust_times(cwnd_times)
            cwnd_datasets.append((adjusted_times, cwnd_values, f'maxq {maxq}'))
        else:
            print(f"没有读取到有效的数据 (maxq: {maxq})")

    if cwnd_datasets:
        print("开始绘制拥塞窗口图表...")
        plot_multiple_data(cwnd_datasets, 'Congestion Window Size Over Time', 'CWND (KB)', output_file='cwnd_plot.png')
        print("拥塞窗口图表绘制完成。")
    else:
        print("没有读取到任何有效的拥塞窗口数据")

    # 绘制 qlen 数据
    qlen_datasets = []
    for maxq in maxq_values:
        file_path = f'qlen-{maxq}/qlen.txt'
        qlen_times, qlen_values = read_qlen_data(file_path)
        if qlen_times and qlen_values:
            print(f"开始调整时间戳... (maxq: {maxq})")
            adjusted_times = adjust_times(qlen_times)
            qlen_datasets.append((adjusted_times, qlen_values, f'maxq={maxq}'))
        else:
            print(f"没有读取到有效的数据 (maxq: {maxq})")

    if qlen_datasets:
        print("开始绘制队列长度图表...")
        plot_multiple_data(qlen_datasets, 'Queue Length Over Time', 'Queue Length (packets)', output_file='qlen_plot.png')
        print("队列长度图表绘制完成。")
    else:
        print("没有读取到任何有效的队列长度数据")

    # 绘制 rtt 数据
    rtt_datasets = []
    for maxq in maxq_values:
    # for tc_way in tc_ways:
        file_path = f'qlen-{maxq}/rtt.txt'
        # file_path = f'{tc_way}/rtt.txt'
        rtt_times, rtt_values = read_rtt_data(file_path)
        if rtt_times and rtt_values:
            print(f"开始调整时间戳... (maxq: {maxq})")
            # print(f"开始调整时间戳... (tc_way: {tc_way})")
            adjusted_times = adjust_times(rtt_times)
            rtt_datasets.append((adjusted_times, rtt_values, f'maxq={maxq}'))
            # rtt_datasets.append((adjusted_times, rtt_values, f'tc_way={tc_way}'))
        else:
            print(f"没有读取到有效的数据 (maxq: {maxq})")
            # print(f"没有读取到有效的数据 (tc_way: {tc_way})")

    if rtt_datasets:
        print("开始绘制往返时间图表...")
        plot_multiple_data(rtt_datasets, 'Round Trip Time Over Time', 'RTT (ms)', output_file='rtt_plot.png')
        print("往返时间图表绘制完成。")
    else:
        print("没有读取到任何有效的往返时间数据")