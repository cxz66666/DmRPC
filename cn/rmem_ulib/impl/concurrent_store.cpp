#include "context.h"

namespace rmem
{
    ConcurrentStroe::ConcurrentStroe() : num_sm_resps_(0), num_sm_reqs_(0) {}
    ConcurrentStroe::~ConcurrentStroe() = default;
    int ConcurrentStroe::get_session_num()
    {
        spin_lock.lock();
        int res = session_num_vec_.size() == 0 ? -1 : session_num_vec_[0];
        spin_lock.unlock();
        return res;
    }
    void ConcurrentStroe::insert_session(int session)
    {
        spin_lock.lock();
        session_num_vec_.push_back(session);
        spin_lock.unlock();
        return;
    }
    void ConcurrentStroe::clear_session()
    {
        spin_lock.lock();
        session_num_vec_.clear();
        spin_lock.unlock();
        return;
    }
}