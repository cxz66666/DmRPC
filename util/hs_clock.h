#pragma once

#include <iostream>
#include <chrono>
#include <vector>

namespace rmem
{
    class Timer
    {
    public:
        Timer()
            : t1(res::zero()), t2(res::zero())
        {
            tic();
        }

        ~Timer() = default;

        void tic()
        {
            t1 = clock::now();
        }

        void toc(size_t thread_id)
        {
            t2 = clock::now();
            res_vec.push_back(std::chrono::duration_cast<res>(t2 - t1).count());
            std::cout << "thread:" << thread_id << "  time: "
                      << std::chrono::duration_cast<res>(t2 - t1).count() / 1e6 << "s." << std::endl;
        }
        long toc()
        {
            t2 = clock::now();
            return std::chrono::duration_cast<res>(t2 - t1).count();
        }
        double get_average_time()
        {
            assert(res_vec.size() > 1);
            long sum = 0;
            for (size_t i = 0; i < res_vec.size() - 1; i++)
            {
                sum += res_vec[i];
            }
            return static_cast<double>(sum) / (res_vec.size() - 1);
        }

    private:
        typedef std::chrono::high_resolution_clock clock;
        typedef std::chrono::microseconds res;
        std::vector<long> res_vec;
        clock::time_point t1;
        clock::time_point t2;
    };
}
