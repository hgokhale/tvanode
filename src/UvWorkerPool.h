/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <vector>
#include <uv.h>

class UvWorkerPool
{
public:
  UvWorkerPool(int initializeSize)
  {
    uv_mutex_init(&_lock);
    for (int i = 0; i < initializeSize; i++)
    {
      _pool.push_back(new uv_work_t());
    }
  }

  ~UvWorkerPool()
  {
    while (!_pool.empty())
    {
      uv_work_t* w = _pool.back();
      delete w;
      _pool.pop_back();
    }

    uv_mutex_destroy(&_lock);
  }

  inline uv_work_t* get()
  {
    uv_work_t* w;

    uv_mutex_lock(&_lock);
    if (_pool.empty())
    {
      w = new uv_work_t();
    }
    else
    {
      w = _pool.back();
      _pool.pop_back();
    }
    uv_mutex_unlock(&_lock);

    return w;
  }

  inline void put(uv_work_t* w)
  {
    uv_mutex_lock(&_lock);
    _pool.push_back(w);
    uv_mutex_unlock(&_lock);
  }

private:
  std::vector<uv_work_t*> _pool;
  uv_mutex_t _lock;
};
