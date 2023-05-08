import xlwt
import os
import re

erpc_target_dir = "/home/cxz/social_network_result/erpc"
rmem_target_dir = "/home/cxz/social_network_result/rmem"

user_pattern_lat = r"latency(\d+)-user"
home_pattern_lat = r"latency(\d+)-home"
write_pattern_lat = r"latency(\d+)-write"

thread_map = {1: 1, 2: 2, 3: 3, 4: 4, 5:5, 6:6, 7:7, 8:8, 9:9, 10:10, 12:11, 16:12, 32:13}

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


def generate_lat_result(target_sheet_99, target_sheet_995, target_sheet_999, target_sheet_avg,
                        target_dir, row_offset=0, col_offset=0, extra_latency=0):
    target_map_99 = Vividict()
    target_map_995 = Vividict()
    target_map_999 = Vividict()
    target_map_avg = Vividict()
    for fi in os.listdir(target_dir):
        if re.match(user_pattern_lat, fi):
            sum_result = get_lat_result(target_dir + "/" + fi)
            m = re.match(user_pattern_lat, fi)
            concurrency = int(m.group(1))

            if target_map_99["user"][concurrency] == {}:
                target_map_99["user"][concurrency] = sum_result[0]+extra_latency
            if target_map_995["user"][concurrency] == {}:
                target_map_995["user"][concurrency] = sum_result[1]+extra_latency

            if target_map_999["user"][concurrency] == {}:
                target_map_999["user"][concurrency] = sum_result[2]+extra_latency

            if target_map_avg["user"][concurrency] == {}:
                target_map_avg["user"][concurrency] = sum_result[3]+extra_latency

        if re.match(home_pattern_lat, fi):
            sum_result = get_lat_result(target_dir + "/" + fi)
            m = re.match(home_pattern_lat, fi)
            concurrency = int(m.group(1))

            if target_map_99["home"][concurrency] == {}:
                target_map_99["home"][concurrency] = sum_result[0]+extra_latency
            if target_map_995["home"][concurrency] == {}:
                target_map_995["home"][concurrency] = sum_result[1]+extra_latency

            if target_map_999["home"][concurrency] == {}:
                target_map_999["home"][concurrency] = sum_result[2]+extra_latency

            if target_map_avg["home"][concurrency] == {}:
                target_map_avg["home"][concurrency] = sum_result[3]+extra_latency

        if re.match(write_pattern_lat, fi):
            sum_result = get_lat_result(target_dir + "/" + fi)
            m = re.match(write_pattern_lat, fi)
            concurrency = int(m.group(1))

            if target_map_99["write"][concurrency] == {}:
                target_map_99["write"][concurrency] = sum_result[0]
            if target_map_995["write"][concurrency] == {}:
                target_map_995["write"][concurrency] = sum_result[1]

            if target_map_999["write"][concurrency] == {}:
                target_map_999["write"][concurrency] = sum_result[2]

            if target_map_avg["write"][concurrency] == {}:
                target_map_avg["write"][concurrency] = sum_result[3]
    loop_list = [(target_map_99, target_sheet_99), (target_map_995, target_sheet_995),
                 (target_map_999, target_sheet_999), (target_map_avg, target_sheet_avg)]
    for _, val in enumerate(loop_list):
        target_map = val[0]
        target_sheet = val[1]
        row = 1
        for type_key in sorted(target_map):
            target_sheet.write(row + row_offset, col_offset, type_key)
            for num in sorted(target_map[type_key]):
                target_sheet.write(row + row_offset, thread_map[num] + col_offset,
                                   target_map[type_key][num])
            row += 1


if __name__ == '__main__':
    workbook = xlwt.Workbook(encoding='utf-8')
    sheet_lat_99 = workbook.add_sheet('lat_99%')

    sheet_lat_995 = workbook.add_sheet('lat_99.5%')

    sheet_lat_999 = workbook.add_sheet('lat_99.9%')

    sheet_lat_avg = workbook.add_sheet('lat_avg')

    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg, erpc_target_dir, 0, 0, 0)
    generate_lat_result(sheet_lat_99, sheet_lat_995, sheet_lat_999, sheet_lat_avg, rmem_target_dir, 6, 0, 10)

    workbook.save('result.xls')
