import time
import os
import sys
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import CPULimitedHost, OVSBridge
from mininet.link import TCLink
from mininet.cli import CLI

abr_algo = sys.argv[1] # BB, RB, MPC

user_name = os.getlogin()  # your own user name
video_playing_time = 100  # 100s
output_log_name = '100s-formal'  # any file name you like

class DynaBWTopo(Topo):
    def build(self):
        h1 = self.addHost('h1', cpu=.25)
        h2 = self.addHost('h2', cpu=.25)
        s1 = self.addSwitch('s1')
        self.addLink(h1, s1)
        self.addLink(h2, s1)
        
def config_net(net):
    h1, h2, s1 = net.get('h1', 'h2', 's1')
    h1.cmd('ifconfig h1-eth0 10.0.0.1/24')
    h2.cmd('ifconfig h2-eth0 10.0.0.2/24')
    
    h1.cmd('tc qdisc add dev h1-eth0 root fq pacing')
    s1.cmd('tc qdisc add dev s1-eth2 root handle 1: tbf rate 1Mbit latency 20ms buffer 8192') # 16384
    s1.cmd('tc qdisc add dev s1-eth2 parent 1: handle 2: netem delay 0ms loss 0')

def dynamic_bw(net, trace_file):
    s1 = net.get("s1")

    lines = open(trace_file, 'r').readlines()[1:]
    time_lst = [ float(l.strip().split()[0]) for l in lines ]
    bw_lst = [ float(l.strip().split()[1]) for l in lines ]

    last_time = 0
    for i in range(len(time_lst)):
        bw = bw_lst[i]
        cur_time = time_lst[i]

        s1.cmd('tc qdisc change dev s1-eth2 root handle 1: tbf rate {0}Mbit latency 20ms buffer 8192'.format(bw))
        s1.cmd('tc qdisc change dev s1-eth2 parent 1: handle 2: netem delay 20ms loss 0.1%')
        
        # print(bw)
        # time.sleep(cur_time-last_time)
        time.sleep(1)

        last_time = cur_time


def run_video(net):
    h1, h2 = net.get('h1', 'h2')

    h1.cmd('python3 -m http.server 80 -d ./video_server &')
    time.sleep(1)
    print("h1 server started")

	# start abr log server
    # may failed because of packages not available under root
    if abr_algo in [ 'BB', 'RB', 'MPC' ]:
        abr_log_server_command = 'python3 abr_log_server.py %s %s &' % (abr_algo, output_log_name)
    else:
        print('invalid abr algo name, should be in [ MPC, BB, RB ]')
        sys.exit(1)

    h2.cmd(abr_log_server_command)
    time.sleep(1)

    print("h2 ready to play")
    play_browser_command = 'su %s -c "python3 run_browser.py 10.0.0.1 %s" &' % (user_name, video_playing_time)
    h2.cmd(play_browser_command)

if __name__ == '__main__':
    os.system('mn -c > /dev/null 2>&1')

    topo = DynaBWTopo()
    net = Mininet(topo=topo, link=TCLink, switch=OVSBridge, controller=None)
    
    net.start()
    
    config_net(net)

    run_video(net)
    
    # background trace
    dynamic_bw(net, 'trace_file')

    net.stop()
