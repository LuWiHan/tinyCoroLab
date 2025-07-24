#include "coro/context.hpp"
#include "coro/scheduler.hpp"
#include <coroutine>

namespace coro
{
context::context() noexcept
{
    m_id = ginfo.context_id.fetch_add(1, std::memory_order_relaxed);
}

auto context::init() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.init();
    linfo.ctx = this;
}

auto context::deinit() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    linfo.ctx = nullptr;
    m_engine.deinit();
}

auto context::start() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job = make_unique<jthread>(
        [this](stop_token token)
        {
            this->init();
            this->run(token);
            this->deinit();
        });
}

auto context::notify_stop() noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_job->request_stop();
    m_engine.notify();
}

auto context::submit_task(std::coroutine_handle<> handle) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_engine.submit_task(handle);
}

auto context::register_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_wait_num += register_cnt;
}

auto context::unregister_wait(int register_cnt) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    m_wait_num -= register_cnt;
}

auto context::run(stop_token token) noexcept -> void
{
    // TODO[lab2b]: Add you codes
    while (!(token.stop_requested() && task_completed()))
    {
        // 2.处理计算任务
        while (m_engine.ready())
            m_engine.exec_one_task();

        // 3.处理IO任务
        // 如果要求工作线程退出，说明不需要engine继续等待事件，处理完所有任务后就结束
        // 因此这里就往eventfd写值，让engine不会阻塞在eventfd，即不会阻塞等事件
        if (token.stop_requested())
            m_engine.notify();
        m_engine.poll_submit();
    }
}

auto context::task_completed() noexcept -> bool
{
    return m_engine.empty_io() && !m_engine.ready() && m_wait_num == 0;
}

}; // namespace coro