#pragma once

#include <atomic>

#include "config.h"

namespace coro::detail
{

template<class T>
struct alignas(config::kCacheLineSize)  atomic_ref_wrapper 
{
    alignas(std::atomic_ref<T>::required_alignment) T val;
};

}