import xlwt
import os
import re

lb_test_target_dir1 = "/home/cxz/chain_test_result"

erpc_pattern_band0 = r"band_b(\d+)_t(\d+)_c(\d+)_l0"
erpc_pattern_band1 = r"band_b(\d+)_t(\d+)_c(\d+)_l1"
erpc_pattern_band2 = r"band_b(\d+)_t(\d+)_c(\d+)_l2"
erpc_pattern_band3 = r"band_b(\d+)_t(\d+)_c(\d+)_l3"
erpc_pattern_band4 = r"band_b(\d+)_t(\d+)_c(\d+)_l4"
erpc_pattern_band5 = r"band_b(\d+)_t(\d+)_c(\d+)_l5"

erpc_pattern_lat0 = r"lat_b(\d+)_t(\d+)_c(\d+)_l0"
erpc_pattern_lat1 = r"lat_b(\d+)_t(\d+)_c(\d+)_l1"
erpc_pattern_lat2 = r"lat_b(\d+)_t(\d+)_c(\d+)_l2"
erpc_pattern_lat3 = r"lat_b(\d+)_t(\d+)_c(\d+)_l3"
erpc_pattern_lat4 = r"lat_b(\d+)_t(\d+)_c(\d+)_l4"
erpc_pattern_lat5 = r"lat_b(\d+)_t(\d+)_c(\d+)_l5"

erpc_thread_map = {1: 1, 8: 2}


class Vividict(dict):
    def __missing__(self, key):
        value = self[key] = type(self)()
        return value


def get_band_result(file_name):
    sum_result = 0
    with open(file_name, "r") as f:
        for line in f:
            sum_result += float(line)
    f.close()
    return sum_result


def get_lat_result(file_name):
    result_99 = -1
    result_995 = -1
    result_999 = -1
    result_avg = -1
    with open(file_name, "r") as f:
        next(f)
        next(f)
        for line in f.readlines():
            if line[0] != '#':
                wordlist = line.split()
                if float(wordlist[1]) >= 0.99 and result_99 == -1:
                    result_99 = float(wordlist[0])
                if float(wordlist[1]) >= 0.995 and result_995 == -1:
                    result_995 = float(wordlist[0])
                if float(wordlist[1]) >= 0.999 and result_999 == -1:
                    result_999 = float(wordlist[0])
            else:

                if line.find("Mean") != -1:
                    tmp = re.findall(r"[-+]?\d*\.\d+|\d+", line)
                    result_avg = float(tmp[0])

    f.close()
    return result_99, result_995, result_999, result_avg


def generate_band_result(target_sheet, target_dirs, pattern, thread_map, row_offset=0, col_offset=0):
    target_map = Vividict()
    for target_dir in target_dirs:
        for fi in os.listdir(target_dir):
            if re.match(pattern, fi):
                sum_result = get_band_result(target_dir + "/" + fi)
                m = re.match(pattern, fi)
                msg_size = int(m.group(1))
                num_thread = int(m.group(2))
                concurrency = int(m.group(3))
                if target_map[msg_size][num_thread] == {}:
                    target_map[msg_size][num_thread] = sum_result
                else:
                    target_map[msg_size][num_thread] = max(target_map[msg_size][num_thread], sum_result)

    target_sheet.write(row_offset, col_offset, "msg_size/thread_num")
    for i in thread_map.keys():
        target_sheet.write(row_offset, thread_map[i] + col_offset, i)
    row = 1
    for msg_size_key in sorted(target_map):
        target_sheet.write(row + row_offset, col_offset, msg_size_key)
        for num_thread_key in sorted(target_map[msg_size_key]):
            if num_thread_key in thread_map:
                target_sheet.write(row + row_offset, thread_map[num_thread_key] + col_offset,
                                   target_map[msg_size_key][num_thread_key])
            else:
                print("error: not find thread num {} in thread map".format(num_thread_key))
        row = row + 1


def generate_lat_result(target_sheet_99, target_sheet_995, target_sheet_999, target_sheet_avg,
                        target_dirs, pattern, thread_map, row_offset=0, col_offset=0):
    target_map_99 = Vividict()
    target_map_995 = Vividict()
    target_map_999 = Vividict()
    target_map_avg = Vividict()

    for target_dir in target_dirs:
        for fi in os.listdir(target_dir):
            if re.match(pattern, fi):
                sum_result = get_lat_result(target_dir + "/" + fi)
                m = re.match(pattern, fi)
                msg_size = int(m.group(1))
                num_thread = int(m.group(2))
                concurrency = int(m.group(3))

                if target_map_99[msg_size][num_thread] == {}:
                    target_map_99[msg_size][num_thread] = sum_result[0]
                else:
                    target_map_99[msg_size][num_thread] = min(target_map_99[msg_size][num_thread], sum_result[0])

                if target_map_995[msg_size][num_thread] == {}:
                    target_map_995[msg_size][num_thread] = sum_result[1]
                else:
                    target_map_995[msg_size][num_thread] = min(target_map_995[msg_size][num_thread], sum_result[1])

                if target_map_999[msg_size][num_thread] == {}:
                    target_map_999[msg_size][num_thread] = sum_result[2]
                else:
                    target_map_999[msg_size][num_thread] = min(target_map_999[msg_size][num_thread], sum_result[2])

                if target_map_avg[msg_size][num_thread] == {}:
                    target_map_avg[msg_size][num_thread] = sum_result[3]
                else:
                    target_map_avg[msg_size][num_thread] = min(target_map_avg[msg_size][num_thread], sum_result[3])

    loop_list = [(target_map_99, target_sheet_99), (target_map_995, target_sheet_995),
                 (target_map_999, target_sheet_999), (target_map_avg, target_sheet_avg)]
    for _, val in enumerate(loop_list):
        target_map = val[0]
        target_sheet = val[1]
        target_sheet.write(row_offset, col_offset, "msg_size/thread_num")
        for i in thread_map.keys():
            target_sheet.write(row_offset, thread_map[i] + col_offset, i)
        row = 1
        for msg_size_key in sorted(target_map):
            target_sheet.write(row + row_offset, col_offset, msg_size_key)
            for num_thread_key in sorted(target_map[msg_size_key]):
                if num_thread_key in thread_map:
                    target_sheet.write(row + row_offset, thread_map[num_thread_key] + col_offset,
                                       target_map[msg_size_key][num_thread_key])
                else:
                    print("error: not find thread num {} in thread map".format(num_thread_key))
            row = row + 1


if __name__ == '__main__':
    workbook = xlwt.Workbook(encoding='utf-8')
    sheet_band = workbook.add_sheet('band')

    sheet_lat_99 = workbook.add_sheet('lat_99%')

    sheet_lat_995 = workbook.add_sheet('lat_99.5%')

    sheet_lat_999 = workbook.add_sheet('lat_99.9%')

    sheet_lat_avg = workbook.add_sheet('lat_avg')

    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band0,
                         erpc_thread_map, 0, 0)
    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band1,
                         erpc_thread_map, 15, 0)
    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band2,
                         erpc_thread_map, 30, 0)
    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band3,
                         erpc_thread_map, 45, 0)
    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band4,
                         erpc_thread_map, 60, 0)
    generate_band_result(sheet_band, [lb_test_target_dir1], erpc_pattern_band5,
                         erpc_thread_map, 75, 0)


    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat0,
                        erpc_thread_map, 0, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat1,
                        erpc_thread_map, 15, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat2,
                        erpc_thread_map, 30, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat3,
                        erpc_thread_map, 45, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat4,
                        erpc_thread_map, 60, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg,
                        [lb_test_target_dir1], erpc_pattern_lat5,
                        erpc_thread_map, 75, 0)

    workbook.save('result.xls')
