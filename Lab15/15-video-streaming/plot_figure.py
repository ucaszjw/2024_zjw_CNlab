import os
import numpy as np
import math
import matplotlib.pyplot as plt

ABR_LIST = ['BB', 'MPC', 'RB']
EDGECOLOR = ['#EC8F3F', '#B8D200', '#865DFF', '#73F7DD', '#BE6DB7', '#4F709C', '#FF1700']

bitrates_list = []
rebuffs_list = []
qoes_list = []
ci_bitrate = []
ci_rebuff = []

abr_metrics = {}

if __name__ == '__main__':
    #  get data
    for abr in ABR_LIST:
        print(f"Processing {abr} data...")
        file_name = f'./results/log_{abr}_100s-formal'
        try:
            with open(file_name, 'r') as f:
                bitrate_list = []
                rebuffer_time_list = []
                qoe_list = []

                for line in f:
                    parse = line.split()
                    if len(parse) >= 7:
                        bitrate_list.append(float(parse[1]))
                        rebuffer_time_list.append(float(parse[3]))
                        qoe_list.append(float(parse[6]))

                bitrates_list.append(np.mean(bitrate_list))
                ci_b = 1.96 * np.std(bitrate_list, ddof=0) / math.sqrt(len(bitrate_list))  # 有偏估计
                ci_b = ci_b if ci_b else 20
                ci_bitrate.append(ci_b)
                rebuffs_list.append(np.mean(rebuffer_time_list))
                ci_rebuff.append(1.96 * np.std(rebuffer_time_list, ddof=0) / math.sqrt(len(rebuffer_time_list)))
                qoes_list.append(np.mean(qoe_list))

                print(f"Bitrates for {abr}: {bitrate_list}")
                print(f"Rebuffer times for {abr}: {rebuffer_time_list}")
                print(f"QoEs for {abr}: {qoe_list}")

        except FileNotFoundError:
            print(f"File {file_name} not found.")
        except Exception as e:
            print(f"Error processing {file_name}: {e}")

    print("Bitrates:", bitrates_list)
    print("Rebuffers:", rebuffs_list)
    print("QoEs:", qoes_list)
    print("CI Bitrate:", ci_bitrate)
    print("CI Rebuffer:", ci_rebuff)

    # plot qoe
    plt.figure(figsize=(3, 4))
    plt.bar(range(len(qoes_list)), qoes_list, tick_label=ABR_LIST, width=0.5)
    for a, b, i in zip(ABR_LIST, qoes_list, range(len(ABR_LIST))):
        plt.text(i, b + 0.01, "%.2f" % qoes_list[i], ha='center', fontsize=10)

    font = {'family': 'serif', 'size': 15}
    plt.xlabel('ABR', font)
    plt.ylabel('QoE', font)
    plt.title('Average QoE', font, y=1)
    plt.savefig('bar.png', dpi=300, bbox_inches='tight')
    plt.close()

    # plot error bar
    fig, ax = plt.subplots()
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)

    legends = []
    for idx, abr in enumerate(ABR_LIST):
        x = rebuffs_list[idx]
        y = bitrates_list[idx]
        legend = plt.errorbar(x, y, lw=2, color=EDGECOLOR[idx], yerr=ci_bitrate[idx], xerr=ci_rebuff[idx])
        legends.append(legend)

    label_font = {'family': 'serif', 'size': 15}
    plt.xlabel('Rebuffing Time(s)', label_font)
    plt.ylabel('Bitrate(Kbps)', label_font)
    plt.title('Bitrate and Rebuffering Time ErrorBar', font, y=1)

    plt.legend(legends, ABR_LIST, bbox_to_anchor=(0, 1),
               labelspacing=0.5, loc='upper left', frameon=True,
               prop={'family': 'serif', 'size': 13})

    plt.xlim(-0.1, 0.9)
    plt.ylim(500, 1900)

    # arrow
    x_pos = 0.5
    y_pos = 1100
    bbox_props = dict(boxstyle='rarrow, pad=0.4', fc='lightgray', ec='darkgray')
    t = plt.text(x_pos, y_pos, '  Better  ', ha='center', va='center', rotation=45,
                 font='serif', size=19, c='dimgray', weight='bold', bbox=bbox_props)

    plt.gca().invert_xaxis()
    plt.savefig('scatter.png', dpi=300, bbox_inches='tight')
    plt.close()