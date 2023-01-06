import xlwt
import os
import re

rmem_target_dir = "/home/cxz/fork_test_rmem_result"
cxl_target_dir = "/home/cxz/fork_test_cxl_result"

rmem_pattern_lat = r"lat_w(\d+)_s(\d+)_cow(\d+)"
cxl_pattern_lat = r"lat_w(\d+)_s(\d+)_cow(\d+)$"
cxl_pattern_lat1 = r"lat_w(\d+)_s(\d+)_cow(\d+)_1"
cxl_pattern_lat2 = r"lat_w(\d+)_s(\d+)_cow(\d+)_2"



rmem_thread_map = {4: 1, 1024: 2, 2048: 3, 4096:4}
cxl_thread_map = {4: 1, 1024: 2, 2048: 3, 4096:4}

class Vividict(dict):
    def __missing__(self, key):
        value = self[key] = type(self)()
        return value



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



def generate_lat_result(target_sheet_avg,
                        target_dir, pattern, thread_map, row_offset=0, col_offset=0):
    target_map_avg = Vividict()
    for fi in os.listdir(target_dir):
        if re.match(pattern, fi):
            sum_result = get_lat_result(target_dir + "/" + fi)
            m = re.match(pattern, fi)
            write_page_size = int(m.group(2))
            cow = int(m.group(3))
            write_num = int(m.group(1))
            if target_map_avg[write_num][write_page_size][cow] == {}:
                target_map_avg[write_num][write_page_size][cow] = sum_result[3]
            else:
                target_map_avg[write_num][write_page_size][cow] = min(target_map_avg[write_num][write_page_size][cow],
                                                                sum_result[3])
    loop_list = [(target_map_avg, target_sheet_avg)]
    for _, val in enumerate(loop_list):
        target_map = val[0]
        target_sheet = val[1]
        target_sheet.write(row_offset, col_offset, "write_num/write_size")
        for i in thread_map.keys():
            target_sheet.write(row_offset, thread_map[i] + col_offset, i)
        for i in thread_map.keys():
            target_sheet.write(row_offset, thread_map[i] + col_offset + 4, "copy_" + str(i))
        row = 1
        for msg_size_key in sorted(target_map):
            target_sheet.write(row + row_offset, col_offset, msg_size_key)
            for num_thread_key in sorted(target_map[msg_size_key]):
                if num_thread_key in thread_map:
                    target_sheet.write(row + row_offset, thread_map[num_thread_key] + col_offset,
                                       target_map[msg_size_key][num_thread_key][0])
                else:
                    print("error: not find thread num {} in thread map".format(num_thread_key))
            for num_thread_key in sorted(target_map[msg_size_key]):
                if num_thread_key in thread_map:
                    target_sheet.write(row + row_offset, thread_map[num_thread_key] + col_offset + 4,
                                       target_map[msg_size_key][num_thread_key][1])
                else:
                    print("error: not find thread num {} in thread map".format(num_thread_key))
            row = row + 1


if __name__ == '__main__':
    workbook = xlwt.Workbook(encoding='utf-8')

    sheet_lat_avg = workbook.add_sheet('lat_avg')

    generate_lat_result(sheet_lat_avg, rmem_target_dir, rmem_pattern_lat,
                        rmem_thread_map, 0, 0)
    generate_lat_result(sheet_lat_avg, cxl_target_dir, cxl_pattern_lat,
                         cxl_thread_map, 15, 0)
    generate_lat_result(sheet_lat_avg, cxl_target_dir, cxl_pattern_lat1,
                         cxl_thread_map, 30, 0)
    generate_lat_result(sheet_lat_avg, cxl_target_dir, cxl_pattern_lat2,
                         cxl_thread_map, 45, 0)


    workbook.save('result.xls')
