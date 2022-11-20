#include "context.h"

namespace rmem
{
    ConcurrentStroe::ConcurrentStroe() : num_sm_resps_(0), num_sm_reqs_(0) {}
    ConcurrentStroe::~ConcurrentStroe() = default;
    int ConcurrentStroe::get_session_num()
    {
        spin_lock.lock();
        int res = session_num_vec_.empty() ? -1 : session_num_vec_[0].first;
        spin_lock.unlock();
        return res;
    }

    int ConcurrentStroe::get_remote_session_num()
    {
        spin_lock.lock();
        int res = session_num_vec_.empty() ? -1 : session_num_vec_[0].second;
        spin_lock.unlock();
        return res;
    }

    void ConcurrentStroe::insert_session(int session, int remote_session)
    {
        spin_lock.lock();
        session_num_vec_.push_back({session, remote_session});
        spin_lock.unlock();
    }
    void ConcurrentStroe::clear_session()
    {
        spin_lock.lock();
        session_num_vec_.clear();
        spin_lock.unlock();
    }
}