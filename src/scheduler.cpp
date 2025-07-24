#include "coro/scheduler.hpp"
#include "coro/context.hpp"

namespace coro
{
auto scheduler::init_impl(size_t ctx_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    detail::init_meta_info();
    m_stop    = false;
    m_ctx_cnt = ctx_cnt;
    m_ctxs    = detail::ctx_container{};
    m_ctxs.reserve(m_ctx_cnt);
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs.emplace_back(std::make_unique<context>());
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
    m_ctxs.clear();
    m_ctx_cnt = 0;
#ifdef ENABLE_MEMORY_ALLOC
    ginfo.mem_alloc = nullptr;
#endif
}

auto scheduler::stop_impl() noexcept -> void
{
    // TODO[lab2b]: example function
    // This is an example which just notify stop signal to each context,
    // if you don't need this, function just ignore or delete it
    // join等待工作线程退出
    m_stop = true;
    for (int i = 0; i < m_ctx_cnt; i++)
    {
        m_ctxs[i]->join();
    }
}

auto scheduler::submit_task_impl(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    // 如果scheduler开始停止后，ctx向scheduler提交任务，scheduler只会将该任务分发给该ctx
    // 因为其他ctx可能已经执行完退出了
    if (!m_stop)
    {
        size_t ctx_id = m_dispatcher.dispatch();
        m_ctxs[ctx_id]->submit_task(handle);
    }
    else
    {
        linfo.ctx->submit_task(handle);
    }
}
}; // namespace coro
