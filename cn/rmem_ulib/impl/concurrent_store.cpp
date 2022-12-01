#include "context.h"
#include "configs.h"
namespace rmem
{
    ConcurrentStroe::ConcurrentStroe() : session_num(-1), remote_session_num(-1),num_sm_resps_(0),num_sm_reqs_(0) {
        spsc_queue = new SPSCAtomicQueue(AsyncReceivedReqSize);
    }
    ConcurrentStroe::~ConcurrentStroe() {
        delete spsc_queue;
    }

    void ConcurrentStroe::insert_session(int session, int remote_session)
    {
        spin_lock.lock();
        session_num=session;
        remote_session_num=remote_session;
        spin_lock.unlock();
    }
    void ConcurrentStroe::clear_session()
    {
        spin_lock.lock();
        session_num=-1;
        remote_session_num=-1;
        spin_lock.unlock();
    }
}