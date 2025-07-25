#include "coro/scheduler.hpp"
#include "coro/context.hpp"
#include <atomic>

namespace coro
{
auto scheduler::init_impl(size_t ctx_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    detail::init_meta_info();
    m_ctx_cnt = ctx_cnt;
    m_ctxs    = detail::ctx_container{};
    m_ctxs.reserve(m_ctx_cnt);
    m_ctx_stop_flag.resize(m_ctx_cnt,{0});
    m_ctx_run_cnt.store(ctx_cnt);
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs.emplace_back(std::make_unique<context>());
        m_ctxs[i]->set_stop_cb([this,i]()->bool{
            // 将ctx置为停止状态，flag表示ctx停止前的状态，1表示stop，0表示run
            auto flag = std::atomic_ref<int>(m_ctx_stop_flag[i].val).fetch_or(1,std::memory_order_acq_rel);
            if(this->m_ctx_run_cnt.fetch_sub(1-flag,std::memory_order_acq_rel) == 1-flag)
            {
                this->stop_impl();
                return true;
            }
            return false;
        });
        m_ctxs[i]->start();
    }
    m_dispatcher.init(m_ctx_cnt, &m_ctxs);

#ifdef ENABLE_MEMORY_ALLOC
    coro::allocator::memory::mem_alloc_config config;
    m_mem_alloc.init(config);
    ginfo.mem_alloc = &m_mem_alloc;
#endif
}

auto scheduler::loop_impl() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    stop_impl();
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->join();
    }
    m_ctxs.clear();
    m_ctx_cnt = 0;
    m_ctx_stop_flag.clear();
    m_ctx_run_cnt.store(0);
#ifdef ENABLE_MEMORY_ALLOC
    ginfo.mem_alloc = nullptr;
#endif
}

auto scheduler::stop_impl() noexcept -> void
{
    // TODO[lab2b]: example function
    // This is an example which just notify stop signal to each context,
    // if you don't need this, function just ignore or delete it
    // 通知停止
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->notify_stop();
    }
}

auto scheduler::submit_task_impl(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    size_t ctx_id = m_dispatcher.dispatch();
    // 当scheduler往ctx分配任务时，如果ctx是stop状态（即满足退出条件），需要将其置为run状态
    // 并增加ctx运行状态数
    auto flag = std::atomic_ref<int>(m_ctx_stop_flag[ctx_id].val).fetch_and(0,std::memory_order_acq_rel);
    m_ctx_run_cnt.fetch_add(flag,std::memory_order_acq_rel);
    m_ctxs[ctx_id]->submit_task(handle);

}
}; // namespace coro
