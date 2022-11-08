#include "context.h"

namespace rmem
{
    ConditionResp::ConditionResp() : notified(false), resp(0)
    {
        RMEM_INFO("condition resp constructor");
    }
    ConditionResp::~ConditionResp()
    {
        RMEM_INFO("condition resp destory");
    }
    int ConditionResp::waiting_resp()
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (!notified)
        { // avoid fake notify
            cv.wait(lock);
        }
        if (resp != 0)
        {
            RMEM_INFO("%s", debug_msg.c_str());
        }
        notified = false;
        return resp;
    }
    int ConditionResp::waiting_resp(int timeout_ms)
    {
        rt_assert(timeout_ms > 0, "timeout value must > 0");
        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]()
                        { return notified == true; }))
        {
            RMEM_INFO("timeout finish wait");
        }
        else
        {
            RMEM_INFO("timeout!!!");
            // do nothing now
        }
        if (resp != 0)
        {
            RMEM_INFO("%s", debug_msg.c_str());
        }
        notified = false;
        return resp;
    }
    std::pair<int, unsigned long> ConditionResp::waiting_resp_extra(int timeout_ms)
    {
        rt_assert(timeout_ms > 0, "timeout value must > 0");
        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&]()
                        { return notified == true; }))
        {
            RMEM_INFO("timeout finish wait");
        }
        else
        {
            RMEM_INFO("timeout!!!");
            // do nothing now
        }
        if (resp != 0)
        {
            RMEM_INFO("%s", debug_msg.c_str());
        }
        int real_resp = resp;
        unsigned long real_resp_extra = extra_resp;

        notified = false;
        return {real_resp, real_resp_extra};
    }
    void ConditionResp::notify_waiter(int resp_value, std::string msg_value)
    {

        std::unique_lock<std::mutex> lock(mtx);
        rt_assert(!notified, "notify value must be false!");
        resp = resp_value;
        debug_msg = msg_value;

        notified = true;
        // also can use notify all(because only have one waiter)
        cv.notify_one();
    }

    void ConditionResp::notify_waiter_extra(int resp_value, unsigned long extra, std::string msg_value)
    {
        std::unique_lock<std::mutex> lock(mtx);
        rt_assert(!notified, "notify value must be false!");
        resp = resp_value;
        extra_resp = extra;
        debug_msg = msg_value;

        notified = true;
        // also can use notify all(because only have one waiter)
        cv.notify_one();
    }

}