#!/usr/bin/python3
from http.server import BaseHTTPRequestHandler, HTTPServer
import sys
import os
import json
import time

import numpy as np
import time
import itertools

S_INFO = 5  # bit_rate, buffer_size, rebuffering_time, bandwidth_measurement, chunk_til_video_end
S_LEN = 8  # take how many chunks in the past


DEFAULT_QUALITY = 0  # default video quality without agent
REBUF_PENALTY = 4.3  # 1 sec rebuffering -> this number of Mbps
SMOOTH_PENALTY = 1

MPC_FUTURE_CHUNK_COUNT = 5
VIDEO_BIT_RATE = [300,750,1200,1850,2850,4300]  # Kbps
BITRATE_REWARD = [1, 2, 3, 12, 15, 20]
BITRATE_REWARD_MAP = {0: 0, 300: 1, 750: 2, 1200: 3, 1850: 12, 2850: 15, 4300: 20}
M_IN_K = 1000.0
CHUNK_TIL_VIDEO_END_CAP = 48.0
TOTAL_VIDEO_CHUNKS = 48
RANDOM_SEED = 42

# Buffer-based parameters
A_DIM = len(VIDEO_BIT_RATE)
RESEVOIR = 5
CUSHION = 10

SUMMARY_DIR = './results'
LOG_FILE = './results/log'
# in format of time_stamp bit_rate buffer_size rebuffer_time video_chunk_size download_time reward
NN_MODEL = None

CHUNK_COMBO_OPTIONS = []

# video chunk sizes
size_video1 = [4745543, 1508754, 2180406, 1523129, 719946, 2517063, 3637566, 2538135, 1657545, 1055700, 1094143, 1799956, 2325698, 2214183, 821006, 1786705, 2226672, 1777747, 2628912, 4244068, 1435641, 751492, 1532699, 967786, 1712874, 2089967, 2205771, 2121468, 2558390, 4346601, 2470001, 1965751, 1721090, 2063515, 2178321, 2311593, 1928459, 2197429, 2227948, 1206136, 2688476, 1912664, 1823445, 1791293, 2870763, 2989603, 3011142, 1317851, 3330573, 1866074]
size_video2 = [2577496, 1156874, 1551290, 1214757, 540783, 1544968, 2374115, 1725232, 1230034, 833530, 822025, 1239829, 1610835, 1662287, 508042, 998545, 1384679, 1067079, 1657174, 2896113, 995671, 525829, 1027000, 616459, 1025335, 1287920, 1487698, 1413462, 1706108, 2719554, 1647839, 1327068, 1175855, 1414178, 1488534, 1567177, 1233065, 1426162, 1500701, 768538, 1783376, 1245200, 1214570, 1118765, 1907635, 2086277, 2018982, 862448, 2184556, 1258380]
size_video3 = [1699215, 751058, 976905, 830351, 343245, 971535, 1511814, 1118076, 834116, 576923, 566135, 842170, 1050692, 1016256, 325747, 618601, 895556, 686194, 1087320, 1869845, 670375, 347257, 698996, 387765, 653375, 817910, 922576, 903980, 1117336, 1755823, 1100670, 867468, 759996, 941086, 978754, 1039307, 792918, 944199, 984718, 482842, 1159144, 798887, 782597, 679208, 1252176, 1391722, 1334549, 543054, 1407864, 798263]
size_video4 = [953879, 556529, 659859, 767743, 177715, 511220, 889581, 698151, 558638, 408326, 426299, 556930, 691773, 642325, 199602, 374764, 564234, 434691, 723380, 1231948, 444825, 221355, 472627, 245215, 410197, 536796, 578561, 574053, 711263, 1118459, 725268, 559124, 485711, 614182, 651474, 668176, 487936, 609814, 648785, 303672, 766648, 512461, 510856, 422074, 819611, 932895, 894088, 345468, 909000, 514041]
size_video5 = [523092, 389872, 422951, 473320, 116477, 341295, 539212, 429169, 361025, 268171, 304155, 354727, 438883, 375800, 131991, 237534, 356969, 278616, 453018, 778429, 281319, 142445, 292414, 153282, 263536, 324087, 362364, 356008, 444160, 675671, 466628, 350386, 303657, 390698, 410497, 433454, 302811, 391720, 402658, 187695, 476355, 318721, 326164, 259508, 509208, 587039, 570880, 211079, 560963, 312692]
size_video6 = [166948, 174428, 179563, 205620, 51831, 159446, 248337, 172293, 124557, 100518, 141867, 125536, 169017, 127995, 51413, 93512, 148194, 112780, 175575, 310170, 118079, 60705, 96834, 64996, 103740, 126850, 162636, 149200, 175642, 273735, 193627, 133415, 116736, 154434, 158548, 181112, 101552, 154842, 174149, 71536, 194030, 130499, 136333, 110249, 207259, 246663, 238480, 81000, 227712, 120540]

def get_reward(bit_rate, rebuf, last_bit_rate):
    return bit_rate - REBUF_PENALTY * rebuf - SMOOTH_PENALTY * np.abs(last_bit_rate - bit_rate)


def calculate_harmonic_bandwidth(past_bandwidths):
    while past_bandwidths[0] == 0.0:
        past_bandwidths = past_bandwidths[1:]
    bandwidth_sum = 0
    for past_val in past_bandwidths:
        bandwidth_sum += (1 / float(past_val))
    harmonic_bandwidth = 1.0 / (bandwidth_sum / len(past_bandwidths))

    return harmonic_bandwidth


def get_chunk_size(quality, index):
    if ( index < 0 or index > 48 ):
        return 0
    # note that the quality and video labels are inverted (i.e., quality 8 is highest and this pertains to video1)
    sizes = {5: size_video1[index], 4: size_video2[index], 3: size_video3[index], 2: size_video4[index], 1: size_video5[index], 0: size_video6[index]}
    return sizes[quality]

def make_request_handler(input_dict):
    class Request_Handler(BaseHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            self.input_dict = input_dict
            self.log_file = input_dict['log_file']
            self.state = np.zeros((S_INFO, S_LEN))
            self.abr_name = input_dict['abr_name']
            BaseHTTPRequestHandler.__init__(self, *args, **kwargs)

        def do_POST(self):
            content_length = int(self.headers['Content-Length'])
            post_data = json.loads(self.rfile.read(content_length))
            # print(post_data)

            rebuffer_time = float(post_data['RebufferTime'])

            # compute bandwidth measurement
            # start and finish times
            start_time = post_data['lastChunkStartTime']  # ms
            finish_time = post_data['lastChunkFinishTime']  # ms
            video_chunk_fetch_time = finish_time - start_time  # ms
            # chunksize
            video_chunk_size = post_data['lastChunkSize']  # B

            # compute number of video chunks left
            video_chunk_remain = TOTAL_VIDEO_CHUNKS - self.input_dict['video_chunk_count']
            self.input_dict['video_chunk_count'] += 1

            # dequeue history record
            self.state = np.roll(self.state, -1, axis=1)

            # this should be S_INFO number of terms
            try:
                self.state[0, -1] = VIDEO_BIT_RATE[post_data['lastquality']] / float(np.max(VIDEO_BIT_RATE))
                self.state[1, -1] = post_data['buffer']
                self.state[2, -1] = rebuffer_time
                self.state[3, -1] = float(video_chunk_size) / float(video_chunk_fetch_time) / M_IN_K  # kilo byte / ms = MBps
                self.state[4, -1] = np.minimum(video_chunk_remain, CHUNK_TIL_VIDEO_END_CAP) / float(CHUNK_TIL_VIDEO_END_CAP)
            except ZeroDivisionError:
                # this should occur VERY rarely (1 out of 3000), should be a dash issue
                # in this case we ignore the observation and roll back to an eariler one
                pass

            if int(post_data['lastRequest']) == 0:
                # first chunk, pass, choose the default quality
                send_data = str(0)
                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Length', len(send_data))
                self.send_header('Access-Control-Allow-Origin', "*")
                self.end_headers()
                self.wfile.write(send_data.encode())
            else:
                # write to log file
                bit_rate = VIDEO_BIT_RATE[post_data['lastquality']] / M_IN_K
                last_bit_rate = self.input_dict['last_bit_rate']
                self.input_dict['last_bit_rate'] = bit_rate

                reward = get_reward(bit_rate, rebuffer_time, last_bit_rate)

                content = str(time.time()) + '\t' + \
                            str(VIDEO_BIT_RATE[post_data['lastquality']]) + '\t' + \
                            str(post_data['buffer']) + '\t' + \
                            str(rebuffer_time) + '\t' + \
                            str(video_chunk_size) + '\t' + \
                            str(video_chunk_fetch_time) + '\t' + \
                            str(reward) + '\t' + \
                            str(float(video_chunk_size) / video_chunk_fetch_time * 8) + '\n'  # B / ms * 8 = b / ms = Kbps

                # log wall_time, bit_rate, buffer_size, rebuffer_time, video_chunk_size, download_time, reward
                self.log_file.write(content.encode())
                self.log_file.flush()

                if self.abr_name == 'MPC':
                    last_index = int(post_data['lastRequest'])
                    last_quality = int(post_data['lastquality'])
                    send_data = self.mpc_abr(last_index, last_quality)
                elif self.abr_name == 'BB':
                    send_data = self.bb_abr()
                elif self.abr_name == 'RB':
                    send_data = self.rb_abr()
                else:
                    raise Exception('unknown type of abr')

                if post_data['lastRequest'] == TOTAL_VIDEO_CHUNKS:
                    # end of video
                    send_data = "REFRESH"
                    self.input_dict['last_bit_rate'] = DEFAULT_QUALITY
                    self.input_dict['video_chunk_count'] = 0
                    self.log_file.write('\n')  # so that in the log we know where video ends

                self.send_response(200)
                self.send_header('Content-Type', 'text/plain')
                self.send_header('Content-Length', len(send_data))
                self.send_header('Access-Control-Allow-Origin', "*")
                self.end_headers()
                self.wfile.write(str(send_data).encode())

        def do_GET(self):
            self.send_response(200)
            #self.send_header('Cache-Control', 'Cache-Control: no-cache, no-store, must-revalidate max-age=0')
            self.send_header('Cache-Control', 'max-age=3000')
            self.send_header('Content-Length', 20)
            self.end_headers()
            self.wfile.write("console.log('here');")

        def log_message(self, format, *args):
            return

        def bb_abr(self):
            # get buffer size
            buffer_size = self.state[1, -1]
            if buffer_size < RESEVOIR:
                bit_rate = 0
            elif buffer_size >= RESEVOIR + CUSHION:
                bit_rate = A_DIM - 1
            else:
                bit_rate = (A_DIM - 1) * (buffer_size - RESEVOIR) / float(CUSHION)

            return str(int(bit_rate))

        def rb_abr(self):
            bit_rate = np.where(self.state[0, -1] * np.max(VIDEO_BIT_RATE) == VIDEO_BIT_RATE)[0][0]
    
            # first get harmonic mean of last 5 bandwidths
            past_bandwidths = self.state[3, -5:]
            harmonic_bandwidth = calculate_harmonic_bandwidth(past_bandwidths)

            future_bandwidth = harmonic_bandwidth * M_IN_K * 8  # future bandwidth prediction, BM * 1000 * 8 = kbps
    
            # find the largest quality whose bitrate <= harmonic_bandwidth
            bit_rate = 0
            for p in range(0, A_DIM):
                if VIDEO_BIT_RATE[p] < future_bandwidth:
                    bit_rate = p

            return str(bit_rate)

        def mpc_abr(self, last_index, last_quality):
            # first get harmonic mean of last 5 bandwidths
            past_bandwidths = self.state[3,-5:]
            future_bandwidth = calculate_harmonic_bandwidth(past_bandwidths)

            # future chunks length (try 4 if that many remaining)
            future_chunk_length = MPC_FUTURE_CHUNK_COUNT
            if TOTAL_VIDEO_CHUNKS - last_index < 4:
                future_chunk_length = TOTAL_VIDEO_CHUNKS - last_index

            # all possible combinations of 5 chunk bitrates (9^5 options)
            # iterate over list and for each, compute reward and store max reward combination
            max_reward = -100000000
            best_combo = ()
            start_buffer = self.state[1, -1]
            for full_combo in CHUNK_COMBO_OPTIONS:
                combo = full_combo[0:future_chunk_length]
                # calculate total rebuffer time for this combination (start with start_buffer and subtract
                # each download time and add 2 seconds in that order)
                curr_rebuffer_time = 0
                curr_buffer = start_buffer
                bitrate_sum = 0
                smoothness_diffs = 0
                for position in range(0, len(combo)):
                    chunk_quality = combo[position]
                    index = last_index + position + 1 # e.g., if last chunk is 3, then first iter is 3+0+1=4
                    download_time = (get_chunk_size(chunk_quality, index)/1000000.)/future_bandwidth # this is MB/MB/s --> seconds
                    if curr_buffer < download_time:
                        curr_rebuffer_time += (download_time - curr_buffer)
                        curr_buffer = 0
                    else:
                        curr_buffer -= download_time
                    curr_buffer += 4
                    
                    # hd reward
                    bitrate_sum += BITRATE_REWARD[chunk_quality]
                    smoothness_diffs += abs(BITRATE_REWARD[chunk_quality] - BITRATE_REWARD[last_quality])

                    last_quality = chunk_quality

                # hd reward
                reward = bitrate_sum - (8*curr_rebuffer_time) - (smoothness_diffs)

                if reward > max_reward:
                    max_reward = reward
                    best_combo = combo

            if best_combo != (): # some combo was good
                return str(best_combo[0])
            else:
                return '0' # no combo had reward better than -1000000 (ERROR) so send 0

    return Request_Handler


def run(abr, log_file_path, server_class=HTTPServer, port=8333):

    np.random.seed(RANDOM_SEED)

    if not os.path.exists(SUMMARY_DIR):
        os.makedirs(SUMMARY_DIR)

    # make chunk combination options
    for combo in itertools.product([0,1,2,3,4,5], repeat=5):
        CHUNK_COMBO_OPTIONS.append(combo)

    with open(log_file_path, 'wb') as log_file:

        last_bit_rate = DEFAULT_QUALITY
        # need this storage, because observation only contains total rebuffering time
        # we compute the difference to get

        video_chunk_count = 0

        input_dict = {'log_file': log_file,
                      'abr_name': abr,
                      'last_bit_rate': last_bit_rate,
                      'video_chunk_count': video_chunk_count}

        # interface to abr server
        handler_class = make_request_handler(input_dict=input_dict)

        server_address = ('localhost', port)
        httpd = server_class(server_address, handler_class)
        print('Listening on port ' + str(port))
        httpd.serve_forever()


def main():
    if len(sys.argv) == 3:
        abr = sys.argv[1]
        log_file_path = LOG_FILE + '_' + abr + '_' + sys.argv[2]
        run(abr, log_file_path)
    else:
        print('wrong arguments.')


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Keyboard interrupted.")
        try:
            sys.exit(0)
        except SystemExit:
            os._exit(0)
