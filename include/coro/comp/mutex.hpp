/**
 * @file mutex.hpp
 * @author JiahuiWang
 * @brief lab4d
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <type_traits>

#include "coro/comp/mutex_guard.hpp"
#include "coro/detail/types.hpp"
#include "coro/context.hpp"
#include "coro/spinlock.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab4d, in this part you will build the basic coroutine
 * synchronization component----mutex by modifing mutex.hpp and mutex.cpp.
 * Please ensure you have read the document of lab4d.
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

class context;

// TODO[lab4d]: This mutex is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class mutex
{
    struct waitting_element
    {
        context* ctx;
        std::coroutine_handle<> handle;
    };

    struct awaiter
    {
        awaiter(mutex* m) noexcept : m_mtx(m) {}
        auto await_ready() -> bool ;
        auto await_suspend(std::coroutine_handle<> h) -> bool;
        auto await_resume() -> void 
        { 
            detail::linfo.ctx->unregister_wait(m_register_cnt);
        }

        mutex* m_mtx;
        int m_register_cnt{0};
    };

    // Just make lock_guard() compile success
    struct guard_awaiter : awaiter
    {
        guard_awaiter(mutex* m) noexcept : awaiter(m) {}
        auto   await_resume() -> detail::lock_guard<mutex> 
        { 
            awaiter::await_resume();
            return detail::lock_guard<mutex>(*m_mtx); 
        }
    };

public:
    mutex() noexcept {}
    ~mutex() noexcept {}

    auto try_lock() noexcept -> bool;

    auto lock() noexcept -> awaiter { return {this}; };

    auto unlock() noexcept -> void;

    auto lock_guard() noexcept -> guard_awaiter { return {this}; };
private:
    auto is_lock() noexcept -> bool;
private:
    std::atomic_bool                m_lock_state{false};
    detail::spinlock                m_spinlock;
    std::queue<waitting_element>    m_wait_queue;
};

}; // namespace coro
