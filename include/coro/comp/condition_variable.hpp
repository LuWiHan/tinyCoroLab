/**
 * @file condition_variable.hpp
 * @author JiahuiWang
 * @brief lab5b
 * @version 1.1
 * @date 2025-03-24
 *
 * @copyright Copyright (c) 2025
 *
 */
#pragma once

#include <functional>

#include "coro/attribute.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/spinlock.hpp"
#include "coro/context.hpp"
#include "coro/task.hpp"

namespace coro
{
/**
 * @brief Welcome to tinycoro lab5b, in this part you will build the basic coroutine
 * synchronization component¡ª¡ªcondition_variable by modifing condition_variable.hpp
 * and condition_variable.cpp. Please ensure you have read the document of lab5b.
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

using cond_type = std::function<bool()>;

class condition_variable;
using cond_var = condition_variable;

// TODO[lab5b]: This condition_variable is an example to make complie success,
// You should delete it and add your implementation, I don't care what you do,
// but keep the member function and construct function's declaration same with example.
class condition_variable final
{
    struct waitting_element
    {
        context* ctx;
        std::coroutine_handle<> handle;
    };

    struct awaiter
    {
        awaiter(condition_variable* cv,mutex& m) noexcept : m_cv(cv),m_mtx(m) {}
        auto await_ready() -> bool ;
        auto await_suspend(std::coroutine_handle<> h) -> void;
        auto await_resume() -> void 
        { 
            detail::linfo.ctx->unregister_wait(m_register_cnt);
        }

        condition_variable* m_cv;
        mutex& m_mtx;
        int m_register_cnt{0};
    };

public:
    condition_variable() noexcept  = default;
    ~condition_variable() noexcept = default;

    CORO_NO_COPY_MOVE(condition_variable);

    auto wait(mutex& mtx) noexcept -> task<>;

    auto wait(mutex& mtx, cond_type&& cond) noexcept -> task<>;

    auto wait(mutex& mtx, cond_type& cond) noexcept -> task<>;

    auto notify_one() noexcept -> void;

    auto notify_all() noexcept -> void;
private:
    auto wait_impl(mutex& mtx) noexcept -> void;

    auto push_queue(waitting_element& e) noexcept -> void;

    auto pop_queue(waitting_element& e) noexcept -> bool;
private:
    detail::spinlock                m_spinlock;
    std::queue<waitting_element>    m_wait_queue;
};

}; // namespace coro
