/**
 * @file latch.hpp
 * @author JiahuiWang
 * @brief lab4b
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "coro/comp/when_all.hpp"
#include "coro/detail/types.hpp"
#include "coro/meta_info.hpp"
#include "coro/spinlock.hpp"
#include "coro/context.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4b, in this part you will build the basic coroutine
 * synchronization component - latch by modifing latch.hpp and latch.cpp. Please ensure
 * you have read the document of lab4b.
 *
 * @warning You should carefully consider whether each implementation should be thread-safe.
 *
 * You should follow the rules below in this part:
 *
 * @note The location marked by todo is where you must add code, but you can also add code anywhere
 * you want, such as function and class definitions, even member variables.
 *
 * @note lab4 and lab5 are free designed lab, leave the interfaces that the test case will use,
 * and then, enjoy yourself!
 */

// TODO[lab4b]: This latch is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the function count_down() and wait()'s declaration same with example.
class latch
{
    struct waitting_element
    {
        context* ctx;
        std::coroutine_handle<> handle;
    };

    struct awaiter
    {
        awaiter(latch* l) : m_latch(l){}
        auto await_ready() -> bool { return m_latch->m_flag.load(std::memory_order_relaxed); }
        auto await_suspend(std::coroutine_handle<> h) -> bool
        {
            std::lock_guard<detail::spinlock> lock(m_latch->m_lock);
            if(m_latch->m_flag.load(std::memory_order_acquire))
                return false;
            waitting_element element;
            element.ctx = detail::linfo.ctx;
            element.handle = h;
            m_latch->m_wait_queue.push_back(element);
            m_register_cnt = 1;
            element.ctx->register_wait(m_register_cnt);
            return true;
        }
        auto await_resume() -> void { detail::linfo.ctx->unregister_wait(m_register_cnt); }

        latch*  m_latch;
        int     m_register_cnt{0};
    };
public:
    latch(std::uint64_t count) noexcept
        : m_cnt(count) {}
    latch(const latch&)                    = delete;
    latch(latch&&)                         = delete;
    auto operator=(const latch&) -> latch& = delete;
    auto operator=(latch&&) -> latch&      = delete;

    auto count_down() noexcept -> void
    {
        uint64_t cnt = m_cnt.fetch_sub(1,std::memory_order_acquire);
        if(cnt == 1) // 第一个计数减0
        {
            // 1.设置标值
            m_flag.store(true,std::memory_order_acquire);
            // 2.唤醒协程
            std::lock_guard<detail::spinlock> lock(m_lock);
            for(auto& e:m_wait_queue)
                e.ctx->submit_task(e.handle);
        }
        return;
    }

    auto wait() noexcept -> awaiter { return {this}; }
private:
    std::atomic_uint64_t            m_cnt;
    std::atomic_bool                m_flag{false};
    detail::spinlock                m_lock;
    std::vector<waitting_element>   m_wait_queue;
};

/**
 * @brief RAII for latch
 *
 */
class latch_guard
{
public:
    latch_guard(latch& l) noexcept : m_l(l) {}
    ~latch_guard() noexcept { m_l.count_down(); }

private:
    latch& m_l;
};

}; // namespace coro
