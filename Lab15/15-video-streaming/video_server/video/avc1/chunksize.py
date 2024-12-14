import os

root_path = os.getcwd()
dir_nums = [1, 2, 3, 4, 5, 6]
video_nums = [x for x in range(1, 51)]

chunksize_list = []

for d in dir_nums:
    chunksize_list = []

    for v in video_nums:

        path = './%s/seg-%s.m4s' % (d, v)

        size = os.path.getsize(path)

        chunksize_list.append(size)

    print(len(chunksize_list))

    print(chunksize_list)

size = os.path.getsize('./1/seg-1.m4s')